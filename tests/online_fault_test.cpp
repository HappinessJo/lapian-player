// T8.13 网络异常/并发加固单测（纯注入，不联网、不需要 SESSDATA、无 GUI）。
//
// 覆盖（对应 tasks-v2 T8.13 / delta O7、O4-A5/A7、O5-A5/A6/A7）：
// - 下载中断网：重试 + Range 续传，字节不重复、hash 一致；
// - 全部线路失败：≤5s 可读错误、`.part` 保留、已发布前缀仍可读（EIO 而非 EOF/EXIT）、
//   重新打开从断点续传；
// - 未缓存 seek 断网：阻塞读在错误发生后 ≤5s 以 EIO 返回，已缓存区仍可读；
// - 主 CDN 403 → 备份切换，不重复下载已缓存字节；
// - URL 过期 → 刷新一次后从断点继续；刷新失败 → 可读错误 + 保留 `.part`；
// - API 错误矩阵（-400/-404/62002/-101/412/超时）+ 真实 WinHTTP 4.5s 超时上界；
// - 网络无响应看门狗：OnlineSession 4s 判定 → ≤5s 可读错误，缓存区仍可播，关闭 ≤1s；
// - 缓存损坏（截断）→ OnlineSession 自动 Rebuild 后重下成功，不用坏数据冒充可播放；
// - 磁盘不足（总字节 > 可用空间）→ ≤5s 可读错误，不崩，在线记录 sentinel 不动；
// - DASH 双输入单流失败：会话聚合失败、两路都进终态、缓存不写完成、可读错误；
// - 关闭阻塞：缓冲中 Cancel ≤100ms、Close ≤1s；
// - 日志脱敏：错误/状态消息不含 SESSDATA/签名参数/完整 URL；
// - 并发 4 读者：默认 8s smoke；`--readers-minutes 10` 跑真实 10 分钟。
//
// 退出码：0 = 全部通过；1 = 失败；77 = 缺少素材（由 CTest 标 SKIP；本文件不依赖素材）。

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QVector>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#include "app/OnlineSession.h"
#include "online/CacheStore.h"
#include "online/DownloadJob.h"
#include "online/DownloadTransport.h"
#include "online/GrowingSource.h"
#include "online/PlayUrl.h"
#include "online/WinHttpClient.h"

extern "C" {
#include <libavutil/error.h>
}

// 本机静默服务器（只监听 127.0.0.1）：winsock2.h 必须在 windows.h 之前。
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace {

using lapian::app::OnlineReadyInfo;
using lapian::app::OnlineRequest;
using lapian::app::OnlineSession;
using lapian::app::OnlineState;
using lapian::app::OnlineStateName;
using lapian::online::BiliResolveError;
using lapian::online::CacheEntry;
using lapian::online::CacheKey;
using lapian::online::CacheStore;
using lapian::online::DownloadJob;
using lapian::online::DownloadOptions;
using lapian::online::DownloadPlan;
using lapian::online::DownloadRequest;
using lapian::online::DownloadState;
using lapian::online::DownloadStats;
using lapian::online::DownloadTransportResult;
using lapian::online::DownloadTransportStatus;
using lapian::online::GrowingCursor;
using lapian::online::GrowingSource;
using lapian::online::HttpCancelToken;
using lapian::online::HttpResponse;
using lapian::online::IHttpClient;
using lapian::online::IDownloadSink;
using lapian::online::IDownloadTransport;
using lapian::online::PlayUrlData;
using lapian::online::DurlSegment;

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* what) {
  ++g_checks;
  if (condition) {
    std::printf("[ OK ] %s\n", what);
    return;
  }
  std::printf("[FAIL] %s\n", what);
  std::fflush(stdout);
  ++g_failures;
}

void CheckEqI64(qint64 actual, qint64 expected, const char* what) {
  ++g_checks;
  if (actual == expected) {
    std::printf("[ OK ] %s\n", what);
    return;
  }
  std::printf("[FAIL] %s\n  实际：%lld\n  期望：%lld\n", what,
              static_cast<long long>(actual), static_cast<long long>(expected));
  std::fflush(stdout);
  ++g_failures;
}

void CheckLe(qint64 actual, qint64 expected, const char* what) {
  ++g_checks;
  if (actual <= expected) {
    std::printf("[ OK ] %s（%lld ≤ %lld）\n", what,
                static_cast<long long>(actual),
                static_cast<long long>(expected));
    return;
  }
  std::printf("[FAIL] %s\n  实际：%lld\n  上限：%lld\n", what,
              static_cast<long long>(actual), static_cast<long long>(expected));
  std::fflush(stdout);
  ++g_failures;
}

void CheckContains(const QString& text, const QString& needle, const char* what) {
  ++g_checks;
  if (text.contains(needle)) {
    std::printf("[ OK ] %s\n", what);
    return;
  }
  std::printf("[FAIL] %s\n  文本：%s\n  缺少：%s\n", what,
              text.toUtf8().constData(), needle.toUtf8().constData());
  ++g_failures;
}

void CheckNotContains(const QString& text, const QString& needle,
                      const char* what) {
  ++g_checks;
  if (!text.contains(needle)) {
    std::printf("[ OK ] %s\n", what);
    return;
  }
  std::printf("[FAIL] %s\n  文本：%s\n  不应包含：%s\n", what,
              text.toUtf8().constData(), needle.toUtf8().constData());
  ++g_failures;
}

void Section(const char* name) {
  std::printf("\n---- %s ----\n", name);
  std::fflush(stdout);
}

QString HashBytes(const QByteArray& bytes) {
  return QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString HashFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QString();
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file)) {
    return QString();
  }
  return QString::fromLatin1(hash.result().toHex());
}

// 合成“媒体字节”：确定性伪随机，便于 hash 对比与“不重复写”检查。
QByteArray MakeSample(qint64 bytes) {
  QByteArray data;
  data.resize(static_cast<int>(bytes));
  std::uint32_t state = 0x9e3779b9u;
  for (qint64 i = 0; i < bytes; ++i) {
    state = state * 1664525u + 1013904223u;
    data[static_cast<int>(i)] = static_cast<char>((state >> 24) & 0xff);
  }
  return data;
}

// ---------------------------------------------------------------- fake CDN

struct AttemptBehavior {
  int http_status = 0;         // 非 0：直接返回该状态（403/404/500…）
  bool network_fail = false;   // 直接网络失败（断网/连接被拒）
  bool stall = false;          // 阻塞到取消（模拟无响应的连接）
  int stall_ms = 0;            // 阻塞 N 毫秒后继续（模拟休眠/卡顿后恢复）
  qint64 bytes_limit = -1;     // 本次最多给这么多字节（-1 = 给到文件末尾）
  bool ignore_range = false;
  bool start_mismatch = false;
  bool wrong_total = false;
  qint64 chunk_bytes = 128 * 1024;
  int chunk_delay_ms = 0;
};

struct UrlScript {
  QVector<AttemptBehavior> attempts;  // 第 N 次请求用第 N 条；最后一条重复
  int probe_http_status = 206;        // DASH total probe（range_end>=0）用
};

class FakeTransport final : public IDownloadTransport {
 public:
  QByteArray data;                 // 各 URL 共用同一份字节（hash 对比用）
  QHash<QString, UrlScript> scripts;
  bool ignore_cancel = false;      // 故意写坏：验证产品不依赖它

  DownloadTransportResult GetRange(const DownloadRequest& request,
                                   IDownloadSink* sink,
                                   const HttpCancelToken& cancel) override {
    QVector<DownloadRequest> ignored;
    int attempt_index = 0;
    UrlScript script;
    {
      QMutexLocker lock(&mutex_);
      requests_.append(request);
      request_counts_[request.url] += 1;
      attempt_index = request_counts_[request.url] - 1;
      script = scripts.value(request.url);
    }
    DownloadTransportResult result;

    // DASH 总字节探测：range_end >= 0，只需要响应头。
    if (request.range_end >= 0) {
      result.http_status = script.probe_http_status;
      if (result.http_status == 206) {
        result.content_range_start = 0;
        result.content_range_end = request.range_end;
        result.content_range_total = data.size();
        result.status = DownloadTransportStatus::kSinkError;
        result.error = QStringLiteral("probe");
        QString sink_error;
        if (sink != nullptr && sink->Write(
                                   reinterpret_cast<const std::uint8_t*>(
                                       data.constData()),
                                   1, &sink_error)) {
          // 探测 sink 接受了 1 字节（我们的 FirstChunkSink 不会接受）。
          result.status = DownloadTransportStatus::kOk;
          result.ok = true;
        }
      } else {
        result.status = DownloadTransportStatus::kHttpStatus;
        result.error = QStringLiteral("HTTP %1").arg(result.http_status);
      }
      return result;
    }

    AttemptBehavior behavior;
    if (!script.attempts.isEmpty()) {
      behavior = script.attempts.at(
          std::min<int>(attempt_index, script.attempts.size() - 1));
    }

    if (behavior.http_status != 0) {
      result.http_status = behavior.http_status;
      result.status = DownloadTransportStatus::kHttpStatus;
      result.error = QStringLiteral("HTTP %1").arg(behavior.http_status);
      return result;
    }
    if (behavior.network_fail) {
      result.status = DownloadTransportStatus::kNetwork;
      result.error = QStringLiteral("网络中断（注入）");
      return result;
    }
    if (request.offset > data.size()) {
      result.http_status = 416;
      result.status = DownloadTransportStatus::kHttpStatus;
      result.error = QStringLiteral("HTTP 416");
      return result;
    }

    result.http_status = request.offset > 0 ? 206 : 200;
    if (result.http_status == 206) {
      result.content_range_start = request.offset;
      result.content_range_end = data.size() - 1;
      result.content_range_total = behavior.wrong_total ? data.size() + 1
                                                        : data.size();
      if (behavior.start_mismatch) {
        result.content_range_start = request.offset + 1;
        result.status = DownloadTransportStatus::kRangeStartMismatch;
        result.start_mismatch = true;
        result.error = QStringLiteral("start mismatch");
        return result;
      }
      if (behavior.wrong_total) {
        result.status = DownloadTransportStatus::kTotalMismatch;
        result.total_mismatch = true;
        result.error = QStringLiteral("total mismatch");
        return result;
      }
    } else {
      result.content_length = data.size();
    }
    if (behavior.ignore_range && request.offset > 0) {
      result.status = DownloadTransportStatus::kRangeIgnored;
      result.range_ignored = true;
      result.error = QStringLiteral("range ignored");
      return result;
    }

    // 先交付本次允许的字节（bytes_limit），再模拟“连接静默 / 系统挂起”。
    qint64 offset = request.offset;
    auto write_until = [&](qint64 end) -> bool {
      while (offset < end) {
        if (cancel.IsCancelled() && !ignore_cancel) {
          result.status = DownloadTransportStatus::kCancelled;
          result.cancelled = true;
          result.bytes_written = offset - request.offset;
          result.error = QStringLiteral("cancelled");
          return false;
        }
        const qint64 chunk =
            std::min<qint64>(behavior.chunk_bytes, end - offset);
        QString sink_error;
        if (sink == nullptr ||
            !sink->Write(reinterpret_cast<const std::uint8_t*>(
                             data.constData() + offset),
                         static_cast<std::size_t>(chunk), &sink_error)) {
          result.status = DownloadTransportStatus::kSinkError;
          result.bytes_written = offset - request.offset;
          result.error = sink_error;
          return false;
        }
        {
          QMutexLocker lock(&mutex_);
          sink_bytes_ += chunk;
        }
        offset += chunk;
        if (behavior.chunk_delay_ms > 0) {
          std::this_thread::sleep_for(
              std::chrono::milliseconds(behavior.chunk_delay_ms));
        }
      }
      return true;
    };
    qint64 limit = data.size();
    if (behavior.bytes_limit >= 0) {
      limit = std::min<qint64>(limit, request.offset + behavior.bytes_limit);
    }
    if (!write_until(limit)) {
      return result;
    }
    if (behavior.stall) {
      // 连接还在，但不再给任何字节（模拟 CDN 静默/网络黑洞）。
      {
        QMutexLocker lock(&mutex_);
        stall_entries_ += 1;
        last_stall_start_ms_ = NowMs();
      }
      while (!cancel.IsCancelled() || ignore_cancel) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      result.status = DownloadTransportStatus::kCancelled;
      result.cancelled = true;
      result.bytes_written = offset - request.offset;
      result.error = QStringLiteral("cancelled");
      return result;
    }
    if (behavior.stall_ms > 0) {
      // 模拟系统挂起/线程停顿：N 毫秒后同一连接继续给数据（不重复字节）。
      const std::int64_t suspend_start = NowMs();
      while (NowMs() - suspend_start < behavior.stall_ms) {
        if (cancel.IsCancelled() && !ignore_cancel) {
          result.status = DownloadTransportStatus::kCancelled;
          result.cancelled = true;
          result.bytes_written = offset - request.offset;
          result.error = QStringLiteral("cancelled");
          return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      if (!write_until(data.size())) {
        return result;
      }
    }
    result.bytes_written = offset - request.offset;
    result.first_byte_ms = 0;
    result.elapsed_ms = 0;
    if (offset < data.size()) {
      result.status = DownloadTransportStatus::kNetwork;
      result.error = QStringLiteral("连接中断（注入）");
      return result;
    }
    result.status = DownloadTransportStatus::kOk;
    result.ok = true;
    return result;
  }

  QVector<DownloadRequest> requests() const {
    QMutexLocker lock(&mutex_);
    return requests_;
  }

  qint64 sink_bytes() const {
    QMutexLocker lock(&mutex_);
    return sink_bytes_;
  }

  int requests_for(const QString& url) const {
    QMutexLocker lock(&mutex_);
    return request_counts_.value(url);
  }

  int stall_entries() const {
    QMutexLocker lock(&mutex_);
    return stall_entries_;
  }

  qint64 last_stall_start_ms() const {
    QMutexLocker lock(&mutex_);
    return last_stall_start_ms_;
  }

 private:
  static std::int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  mutable QMutex mutex_;
  QVector<DownloadRequest> requests_;
  QHash<QString, int> request_counts_;
  qint64 sink_bytes_ = 0;
  int stall_entries_ = 0;
  std::int64_t last_stall_start_ms_ = 0;
};

// ---------------------------------------------------------------- fake API

class FakeHttp final : public IHttpClient {
 public:
  QByteArray body;
  int http_status = 200;
  bool network_fail = false;
  bool timed_out = false;
  int calls = 0;
  int delay_ms = 0;
  QString last_url;

  HttpResponse Get(const lapian::online::HttpRequest& request,
                   const HttpCancelToken& cancel) override {
    {
      QMutexLocker lock(&mutex_);
      ++calls;
      last_url = request.url;
    }
    const std::int64_t start = NowMs();
    while (delay_ms > 0 && NowMs() - start < delay_ms) {
      if (cancel.IsCancelled()) {
        HttpResponse cancelled;
        cancelled.cancelled = true;
        cancelled.error = QStringLiteral("cancelled");
        return cancelled;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    HttpResponse response;
    if (timed_out) {
      response.timed_out = true;
      response.error = QStringLiteral("网络无响应");
      return response;
    }
    if (network_fail) {
      response.error = QStringLiteral("网络连接失败");
      return response;
    }
    response.ok = http_status >= 200 && http_status < 300;
    response.http_status = http_status;
    response.body = body;
    if (!response.ok) {
      response.error = QStringLiteral("HTTP %1").arg(http_status);
    }
    response.elapsed_ms = NowMs() - start;
    return response;
  }

  int call_count() const {
    QMutexLocker lock(&mutex_);
    return calls;
  }

 private:
  static std::int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
  mutable QMutex mutex_;
};

// 本机慢服务器（只监听 127.0.0.1；不产生外部网络流量）。
class SilentServer {
 public:
  SilentServer() {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener_ == INVALID_SOCKET) {
      return;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener_, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) != 0) {
      closesocket(listener_);
      listener_ = INVALID_SOCKET;
      return;
    }
    sockaddr_in bound{};
    int length = sizeof(bound);
    if (getsockname(listener_, reinterpret_cast<sockaddr*>(&bound), &length) !=
        0) {
      closesocket(listener_);
      listener_ = INVALID_SOCKET;
      return;
    }
    port_ = ntohs(bound.sin_port);
    if (listen(listener_, 4) != 0) {
      closesocket(listener_);
      listener_ = INVALID_SOCKET;
      return;
    }
    thread_ = std::thread([this]() { AcceptLoop(); });
  }

  ~SilentServer() {
    stop_ = true;
    if (listener_ != INVALID_SOCKET) {
      closesocket(listener_);
      listener_ = INVALID_SOCKET;
    }
    if (thread_.joinable()) {
      thread_.join();
    }
    WSACleanup();
  }

  bool ok() const { return port_ > 0; }
  QString Url(const QString& path) const {
    return QStringLiteral("http://127.0.0.1:%1%2").arg(port_).arg(path);
  }

 private:
  void AcceptLoop() {
    while (!stop_) {
      const SOCKET client = accept(listener_, nullptr, nullptr);
      if (client == INVALID_SOCKET) {
        if (stop_) {
          return;
        }
        continue;
      }
      // 读掉请求头后保持连接不响应：模拟“DNS 通、服务器不回”。
      char buffer[4096];
      recv(client, buffer, sizeof(buffer), 0);
      while (!stop_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (closing_) {
          break;
        }
      }
      closesocket(client);
    }
  }

  SOCKET listener_ = INVALID_SOCKET;
  int port_ = 0;
  std::atomic<bool> stop_{false};
  std::atomic<bool> closing_{false};
  std::thread thread_;
};

// ---------------------------------------------------------------- 公共夹具

QString HashOf(const QByteArray& data) { return HashBytes(data); }

CacheKey MakeKey(const QString& bvid = QStringLiteral("BV1BMnv6jE5S"),
                 qint64 cid = 42915922022LL, int qn = 64, int codecid = 7) {
  CacheKey key;
  key.bvid = bvid;
  key.cid = cid;
  key.qn = qn;
  key.codecid = codecid;
  return key;
}

DownloadPlan MakePlan(const QStringList& urls, qint64 total, int qn = 64,
                      int codecid = 7) {
  DownloadPlan plan;
  plan.urls = urls;
  plan.total_bytes = total;
  plan.qn = qn;
  plan.codecid = codecid;
  plan.format = QStringLiteral("mp4720");
  plan.metadata.duration_ms = 1000;
  plan.metadata.width = 1280;
  plan.metadata.height = 720;
  plan.metadata.fps = 30.0;
  return plan;
}

void SeedPart(CacheStore* cache, const CacheKey& key, const QByteArray& data,
              qint64 bytes) {
  QString part_path;
  qint64 existing = 0;
  QString error;
  if (!cache->Reserve(key, data.size(), &error) ||
      !cache->OpenPart(key, &part_path, &existing, &error)) {
    std::printf("[FAIL] seed reserve: %s\n", error.toUtf8().constData());
    ++g_failures;
    return;
  }
  QFile file(part_path);
  if (!file.open(QIODevice::WriteOnly)) {
    std::printf("[FAIL] seed open\n");
    ++g_failures;
    return;
  }
  file.write(data.constData(), bytes);
  file.close();
}

// T8.17：种一条完整缓存（data.part → data.mp4），可选写 DASH meta。
bool SeedComplete(CacheStore* cache, const CacheKey& key,
                  const QByteArray& data,
                  const lapian::online::CacheMetadata* metadata = nullptr) {
  QString part_path;
  qint64 existing = 0;
  QString error;
  if (!cache->Reserve(key, data.size(), &error) ||
      !cache->OpenPart(key, &part_path, &existing, &error)) {
    std::printf("[FAIL] seed complete reserve: %s\n", error.toUtf8().constData());
    ++g_failures;
    return false;
  }
  QFile file(part_path);
  if (!file.open(QIODevice::WriteOnly)) {
    std::printf("[FAIL] seed complete open\n");
    ++g_failures;
    return false;
  }
  file.write(data);
  file.close();
  if (!cache->MarkComplete(key, &error)) {
    std::printf("[FAIL] seed complete mark: %s\n", error.toUtf8().constData());
    ++g_failures;
    return false;
  }
  if (metadata != nullptr && !cache->SetMetadata(key, *metadata, &error)) {
    std::printf("[FAIL] seed complete meta: %s\n", error.toUtf8().constData());
    ++g_failures;
    return false;
  }
  return true;
}

CacheKey MakeAudioKey(const QString& bvid, qint64 cid, int audio_id) {
  CacheKey key;
  key.bvid = bvid;
  key.cid = cid;
  key.kind = lapian::online::CacheStreamKind::kAudio;
  key.audio_id = audio_id;
  return key;
}

struct JobOutcome {
  DownloadState state = DownloadState::kIdle;
  DownloadStats stats;
  bool started = false;
  std::shared_ptr<GrowingSource> source;
  QString start_error;
};

JobOutcome RunJob(CacheStore* cache, const CacheKey& key,
                  FakeTransport* transport, const DownloadPlan& plan,
                  lapian::online::DownloadRefreshCallback refresh = {},
                  DownloadOptions options = {}, int wait_ms = 30000) {
  JobOutcome outcome;
  outcome.source = std::make_shared<GrowingSource>(plan.total_bytes);
  DownloadJob job(cache, key, outcome.source, transport, plan,
                  std::move(refresh), options);
  QString error;
  outcome.started = job.Start(&error);
  if (!outcome.started) {
    outcome.state = job.state();
    outcome.stats = job.stats();
    outcome.stats.error = error;
    outcome.start_error = error;
    return outcome;
  }
  job.Wait(wait_ms);
  outcome.state = job.state();
  outcome.stats = job.stats();
  return outcome;
}

bool PumpUntil(const std::function<bool()>& predicate, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (predicate()) {
      return true;
    }
    QThread::msleep(1);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  return predicate();
}

PlayUrlData MakeDurl(const QStringList& urls, qint64 total, int qn = 64,
                     int codecid = 7) {
  PlayUrlData data;
  data.quality = qn;
  data.codecid = codecid;
  data.format = QStringLiteral("mp4720");
  data.timelength_ms = 10000;
  DurlSegment segment;
  segment.url = urls.value(0);
  for (int i = 1; i < urls.size(); ++i) {
    segment.backup_urls.append(urls.at(i));
  }
  segment.size = total;
  segment.length_ms = 10000;
  data.durls.push_back(segment);
  return data;
}

// OnlineSession 持有 unique_ptr<IDownloadTransport>；测试需要保留假 transport
// 以便断言请求序列/字节数。用共享指针 + 转发壳，避免把 QMutex 拷进会话。
class TransportRef final : public IDownloadTransport {
 public:
  explicit TransportRef(std::shared_ptr<FakeTransport> inner)
      : inner_(std::move(inner)) {}
  DownloadTransportResult GetRange(const DownloadRequest& request,
                                   IDownloadSink* sink,
                                   const HttpCancelToken& cancel) override {
    return inner_->GetRange(request, sink, cancel);
  }

 private:
  std::shared_ptr<FakeTransport> inner_;
};

class HttpRef final : public IHttpClient {
 public:
  explicit HttpRef(std::shared_ptr<FakeHttp> inner) : inner_(std::move(inner)) {}
  HttpResponse Get(const lapian::online::HttpRequest& request,
                   const HttpCancelToken& cancel) override {
    return inner_->Get(request, cancel);
  }

 private:
  std::shared_ptr<FakeHttp> inner_;
};

// ---------------------------------------------------------------- T1 断网续传

void TestDisconnectResume() {
  Section("T1 下载中断网：重试 + Range 续传，无重复字节");
  QTemporaryDir root;
  CacheStore cache(root.path());
  FakeTransport transport;
  transport.data = MakeSample(600 * 1024);
  const QString url = QStringLiteral("https://cdn.test/v.mp4");
  UrlScript script;
  AttemptBehavior first;
  first.bytes_limit = 200 * 1024;  // 前两次各给 200KB 后断连
  first.network_fail = false;
  script.attempts = {first, first, AttemptBehavior{}};
  transport.scripts.insert(url, script);
  const CacheKey key = MakeKey();
  DownloadOptions options;
  options.backoff_ms[0] = 60;
  options.backoff_ms[1] = 80;
  options.backoff_ms[2] = 100;
  const JobOutcome outcome =
      RunJob(&cache, key, &transport, MakePlan({url}, transport.data.size()),
             {}, options);
  Check(outcome.state == DownloadState::kCompleted,
        "disconnect: completed after retries");
  Check(outcome.stats.retries >= 2, "disconnect: retried");
  CheckEqI64(outcome.stats.backup_switches, 0, "disconnect: same URL resumed");
  const QVector<DownloadRequest> requests = transport.requests();
  Check(requests.size() >= 3, "disconnect: has 3+ range requests");
  Check(requests.size() >= 2 && requests.at(1).offset == 200 * 1024,
        "disconnect: second request starts at 200KB");
  Check(requests.size() >= 3 && requests.at(2).offset == 400 * 1024,
        "disconnect: third request starts at 400KB");
  const QString mp4 = cache.Mp4Path(key);
  CheckEqI64(QFileInfo(mp4).size(), transport.data.size(),
             "disconnect: file size exact");
  Check(HashFile(mp4) == HashOf(transport.data), "disconnect: sha256");
  CheckEqI64(transport.sink_bytes(), transport.data.size(),
             "disconnect: no duplicate bytes written");
}

// ---------------------------------------------------------------- T2 全线路失败

void TestAllUrlsDown() {
  Section("T2 全部线路失败：≤5s 可读错误 + 缓存区可读 + 断点续传");
  QTemporaryDir root;
  CacheStore cache(root.path());
  FakeTransport transport;
  transport.data = MakeSample(700 * 1024);
  const QStringList urls = {
      QStringLiteral("https://cdn1.test/v.mp4"),
      QStringLiteral("https://cdn2.test/v.mp4"),
      QStringLiteral("https://cdn3.test/v.mp4")};
  for (const QString& url : urls) {
    UrlScript script;
    AttemptBehavior down;
    down.network_fail = true;
    script.attempts = {down};
    transport.scripts.insert(url, script);
  }
  const CacheKey key = MakeKey();
  const qint64 prefix = 300 * 1024;
  SeedPart(&cache, key, transport.data, prefix);

  QElapsedTimer timer;
  timer.start();
  const JobOutcome outcome =
      RunJob(&cache, key, &transport, MakePlan(urls, transport.data.size()));
  const qint64 elapsed = timer.elapsed();
  CheckLe(elapsed, 5000, "all_down: readable failure within 5s");
  Check(outcome.state == DownloadState::kFailed, "all_down: job failed");
  CheckContains(outcome.stats.error, QStringLiteral("网络"),
                "all_down: readable error mentions network");
  Check(QFileInfo::exists(cache.PartPath(key)), "all_down: .part retained");
  Check(!QFileInfo::exists(cache.Mp4Path(key)), "all_down: no .mp4");
  CheckEqI64(QFileInfo(cache.PartPath(key)).size(), prefix,
             "all_down: .part length unchanged");
  Check(outcome.stats.backup_switches == 2,
        "all_down: tried all 3 URLs");

  // 已发布前缀仍可读；前沿读必须是 EIO（不能伪装 EOF/EXIT）。
  Check(outcome.source->HasFailed(), "all_down: source marked failed");
  CheckEqI64(outcome.source->Published(), prefix,
             "all_down: published prefix kept");
  QString cursor_error;
  auto cursor = GrowingCursor::Open(outcome.source, cache.PartPath(key),
                                    &cursor_error);
  Check(cursor != nullptr, "all_down: cursor opens after failure");
  if (cursor != nullptr) {
    std::uint8_t buffer[64 * 1024] = {};
    CheckEqI64(cursor->Seek(0, SEEK_SET), 0, "all_down: seek 0 within prefix");
    const int read = cursor->Read(buffer, sizeof(buffer));
    CheckEqI64(read, static_cast<qint64>(sizeof(buffer)),
               "all_down: cached prefix still readable");
    Check(cursor->Seek(prefix, SEEK_SET) == prefix,
          "all_down: seek to frontier");
    const int frontier = cursor->Read(buffer, sizeof(buffer));
    Check(frontier == AVERROR(EIO),
          "all_down: frontier read = EIO (not EOF/EXIT)");
    cursor->Close();
  }

  // 恢复网络后重开：必须从断点续传，hash 一致。
  FakeTransport healthy;
  healthy.data = transport.data;
  const JobOutcome resumed =
      RunJob(&cache, key, &healthy, MakePlan({urls.value(0)}, healthy.data.size()));
  Check(resumed.state == DownloadState::kCompleted,
        "all_down: reopen completes");
  const QVector<DownloadRequest> requests = healthy.requests();
  Check(!requests.isEmpty() && requests.first().offset == prefix,
        "all_down: reopen resumes at .part length");
  Check(HashFile(cache.Mp4Path(key)) == HashOf(healthy.data),
        "all_down: resume sha256");
}

// ---------------------------------------------------------------- T3 未缓存 seek

void TestUncachedSeekWhileDown() {
  Section("T3 未缓存 seek 断网：阻塞读 ≤5s 以 EIO 返回");
  QTemporaryDir root;
  CacheStore cache(root.path());
  FakeTransport transport;
  transport.data = MakeSample(800 * 1024);
  const QString url = QStringLiteral("https://cdn.test/v.mp4");
  UrlScript script;
  AttemptBehavior down;
  down.network_fail = true;
  script.attempts = {down};
  transport.scripts.insert(url, script);
  const CacheKey key = MakeKey();
  const qint64 prefix = 200 * 1024;
  SeedPart(&cache, key, transport.data, prefix);

  JobOutcome outcome;
  auto source = std::make_shared<GrowingSource>(transport.data.size());
  auto cursor =
      GrowingCursor::Open(source, cache.PartPath(key), nullptr);
  Check(cursor != nullptr, "seek_down: cursor opened");
  if (cursor == nullptr) {
    return;
  }
  source->Publish(prefix);
  std::atomic<int> read_result{1};
  std::atomic<std::int64_t> read_ms{-1};
  std::thread reader([&]() {
    std::uint8_t buffer[32 * 1024] = {};
    const auto start = std::chrono::steady_clock::now();
    cursor->Seek(prefix, SEEK_SET);
    const int result = cursor->Read(buffer, sizeof(buffer));
    read_result = result;
    read_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - start)
                  .count();
  });

  DownloadJob job(&cache, key, source, &transport,
                  MakePlan({url}, transport.data.size()));
  QString start_error;
  Check(job.Start(&start_error), "seek_down: job started");
  const bool finished = job.Wait(8000);
  const qint64 job_elapsed_ms = job.stats().elapsed_ms;
  reader.join();
  Check(finished, "seek_down: job finished");
  Check(job.state() == DownloadState::kFailed, "seek_down: job failed");
  Check(read_result.load() == AVERROR(EIO),
        "seek_down: blocked read returns EIO");
  CheckLe(read_ms.load(), 5000, "seek_down: read released within 5s");
  CheckLe(job_elapsed_ms, 5000, "seek_down: failure within 5s");

  // 已缓存区仍可读（同一 source/新 cursor）。
  auto cached = GrowingCursor::Open(source, cache.PartPath(key), nullptr);
  Check(cached != nullptr, "seek_down: cached cursor opens");
  if (cached != nullptr) {
    std::uint8_t buffer[16 * 1024] = {};
    cached->Seek(0, SEEK_SET);
    CheckEqI64(cached->Read(buffer, sizeof(buffer)), sizeof(buffer),
               "seek_down: cached region still readable");
    cached->Close();
  }
  cursor->Close();
}

// ---------------------------------------------------------------- T4 主 CDN 403

void TestBackupSwitch() {
  Section("T4 主 CDN 403：切备份，不重复下载已缓存字节");
  QTemporaryDir root;
  CacheStore cache(root.path());
  FakeTransport transport;
  transport.data = MakeSample(500 * 1024);
  const QString primary = QStringLiteral("https://cdn1.test/v.mp4");
  const QString backup = QStringLiteral("https://cdn2.test/v.mp4");
  UrlScript forbidden;
  AttemptBehavior b403;
  b403.http_status = 403;
  forbidden.attempts = {b403};
  transport.scripts.insert(primary, forbidden);
  const CacheKey key = MakeKey();
  const qint64 prefix = 300 * 1024;
  SeedPart(&cache, key, transport.data, prefix);
  const JobOutcome outcome = RunJob(
      &cache, key, &transport, MakePlan({primary, backup}, transport.data.size()));
  Check(outcome.state == DownloadState::kCompleted, "backup: completed");
  CheckEqI64(outcome.stats.backup_switches, 1, "backup: one switch");
  const QVector<DownloadRequest> requests = transport.requests();
  Check(requests.size() >= 2 && requests.at(1).url == backup,
        "backup: second request uses backup URL");
  Check(requests.size() >= 2 && requests.at(1).offset == prefix,
        "backup: backup starts at cached prefix");
  Check(HashFile(cache.Mp4Path(key)) == HashOf(transport.data),
        "backup: sha256");
  CheckEqI64(transport.sink_bytes(), transport.data.size() - prefix,
             "backup: only missing bytes downloaded");
}

// ---------------------------------------------------------------- T5 URL 过期

void TestUrlExpiredRefresh() {
  Section("T5 URL 过期：刷新一次后从断点继续");
  QTemporaryDir root;
  CacheStore cache(root.path());
  FakeTransport transport;
  transport.data = MakeSample(600 * 1024);
  const QStringList urls = {QStringLiteral("https://cdn1.test/v.mp4"),
                            QStringLiteral("https://cdn2.test/v.mp4"),
                            QStringLiteral("https://cdn3.test/v.mp4")};
  for (const QString& url : urls) {
    UrlScript expired;
    AttemptBehavior b403;
    b403.http_status = 403;
    expired.attempts = {b403};
    transport.scripts.insert(url, expired);
  }
  const QString fresh = QStringLiteral("https://fresh.test/v.mp4?sig=REFresh");
  const CacheKey key = MakeKey();
  const qint64 prefix = 300 * 1024;
  SeedPart(&cache, key, transport.data, prefix);
  int refresh_calls = 0;
  const JobOutcome outcome = RunJob(
      &cache, key, &transport, MakePlan(urls, transport.data.size()),
      [&](DownloadPlan* plan, const HttpCancelToken&, QString*) {
        ++refresh_calls;
        *plan = MakePlan({fresh}, transport.data.size());
        return true;
      });
  Check(outcome.state == DownloadState::kCompleted,
        "expired: completed after refresh");
  CheckEqI64(refresh_calls, 1, "expired: refresh called once");
  CheckEqI64(outcome.stats.refreshes, 1, "expired: one refresh");
  const QVector<DownloadRequest> requests = transport.requests();
  bool fresh_used_at_prefix = false;
  for (const DownloadRequest& request : requests) {
    if (request.url == fresh && request.offset == prefix) {
      fresh_used_at_prefix = true;
    }
  }
  Check(fresh_used_at_prefix, "expired: fresh URL resumes at prefix");
  Check(HashFile(cache.Mp4Path(key)) == HashOf(transport.data),
        "expired: sha256");
}

void TestRefreshFailure() {
  Section("T6 刷新失败：可读错误 + 保留 .part");
  QTemporaryDir root;
  CacheStore cache(root.path());
  FakeTransport transport;
  transport.data = MakeSample(400 * 1024);
  const QStringList urls = {QStringLiteral("https://cdn1.test/v.mp4"),
                            QStringLiteral("https://cdn2.test/v.mp4")};
  for (const QString& url : urls) {
    UrlScript expired;
    AttemptBehavior b403;
    b403.http_status = 403;
    expired.attempts = {b403};
    transport.scripts.insert(url, expired);
  }
  const CacheKey key = MakeKey();
  const qint64 prefix = 200 * 1024;
  SeedPart(&cache, key, transport.data, prefix);
  QElapsedTimer timer;
  timer.start();
  const JobOutcome outcome = RunJob(
      &cache, key, &transport, MakePlan(urls, transport.data.size()),
      [](DownloadPlan*, const HttpCancelToken&, QString* error) {
        *error = QStringLiteral("刷新取流失败（注入）");
        return false;
      });
  CheckLe(timer.elapsed(), 5000, "refresh_fail: within 5s");
  Check(outcome.state == DownloadState::kFailed, "refresh_fail: failed");
  CheckContains(outcome.stats.error, QStringLiteral("刷新"),
                "refresh_fail: readable error");
  Check(QFileInfo::exists(cache.PartPath(key)), "refresh_fail: .part retained");
  Check(!QFileInfo::exists(cache.Mp4Path(key)), "refresh_fail: no .mp4");
  CheckEqI64(QFileInfo(cache.PartPath(key)).size(), prefix,
             "refresh_fail: prefix intact");
}

// ---------------------------------------------------------------- T7 API 矩阵

void TestApiErrorMatrix() {
  Section("T7 API 错误矩阵：-400/-404/62002/-101/412/超时 均可读");
  struct Case {
    const char* name;
    int api_code;
    int http_status;
    bool timed_out;
    const char* needle;
  };
  const Case cases[] = {
      {"api_-400", -400, 200, false, "-400"},
      {"api_-404", -404, 200, false, "-404"},
      {"api_62002", 62002, 200, false, "62002"},
      {"api_-101", -101, 200, false, "-101"},
      {"api_412", 0, 412, false, "412"},
      {"api_timeout", 0, 0, true, "超时"},
  };
  for (const Case& one : cases) {
    FakeHttp http;
    if (one.timed_out) {
      http.timed_out = true;
    } else if (one.http_status == 412) {
      http.http_status = 412;
      http.body = QByteArrayLiteral(R"json({"code":-412,"message":"请求被拒绝"})json");
    } else {
      http.body = QByteArrayLiteral(R"json({"code":)json") +
                  QByteArray::number(one.api_code) +
                  QByteArrayLiteral(R"json(,"message":"test error","data":null})json");
    }
    lapian::online::PlayUrlFetchOptions options;
    options.timeout_ms = lapian::online::kOnlineApiTimeoutMs;
    lapian::online::HttpCancelToken cancel;
    QElapsedTimer timer;
    timer.start();
    const lapian::online::PlayUrlFetchResult result = lapian::online::FetchPlayUrl(
        &http, QStringLiteral("BV1BMnv6jE5S"), 42915922022LL, 80, options, cancel);
    const qint64 elapsed = timer.elapsed();
    Check(!result.error.ok(), one.name);
    CheckContains(result.error.message, QString::fromUtf8(one.needle),
                  (QString::fromUtf8(one.name) + "_message").toUtf8().constData());
    CheckLe(elapsed, 5000,
            (QString::fromUtf8(one.name) + "_within_5s").toUtf8().constData());
    Check(!result.error.message.contains(QStringLiteral("http://")),
          (QString::fromUtf8(one.name) + "_no_url").toUtf8().constData());
  }
  Check(lapian::online::kOnlineApiTimeoutMs <= 5000,
        "api_timeout_constant_le_5s");
}

void TestRealHttpTimeoutBound() {
  Section("T8 真实 WinHTTP 超时上界（本机 127.0.0.1 静默服务器）");
  SilentServer server;
  Check(server.ok(), "http_timeout: local server started");
  if (!server.ok()) {
    return;
  }
  lapian::online::WinHttpClient client;
  lapian::online::HttpRequest request;
  request.url = server.Url(QStringLiteral("/x/player/playurl"));
  request.timeout_ms = lapian::online::kOnlineApiTimeoutMs;
  lapian::online::HttpCancelToken cancel;
  QElapsedTimer timer;
  timer.start();
  const lapian::online::HttpResponse response = client.Get(request, cancel);
  const qint64 elapsed = timer.elapsed();
  Check(response.timed_out, "http_timeout: product timeout fired");
  CheckLe(elapsed, 5000, "http_timeout: within 5s");
  Check(!response.ok, "http_timeout: not ok");
  Check(response.error.contains(QStringLiteral("网络")) ||
            !response.error.isEmpty(),
        "http_timeout: readable error");
}

// ---------------------------------------------------------------- T9 看门狗

struct SessionHarness {
  std::unique_ptr<OnlineSession> session;
  QString cache_root;
  QString error;
  QString last_state;
  int state_events = 0;
  bool ready = false;
  OnlineReadyInfo ready_info;
  QHash<QString, int> state_counts;
  // 可选：错误/首帧相对计时的起点（由各测试设置），用于断言“≤5s”。
  QElapsedTimer* clock = nullptr;
  qint64 ready_ms = -1;
  qint64 error_ms = -1;

  void NoteError(const QString& message) {
    if (!error.isEmpty()) {
      return;
    }
    error = message;
    if (clock != nullptr) {
      error_ms = clock->elapsed();
    }
  }

  void Connect() {
    session->SetCallbacks(
        [this](OnlineState state, const QString& message) {
          last_state = OnlineStateName(state);
          state_counts[last_state] += 1;
          ++state_events;
          if (state == OnlineState::kError) {
            NoteError(message);
          }
        },
        [](const lapian::app::OnlineProgress&) {},
        [this](const OnlineReadyInfo& info) {
          if (ready) {
            return;
          }
          ready = true;
          ready_info = info;
          if (clock != nullptr) {
            ready_ms = clock->elapsed();
          }
        },
        [this](const QString& message) { NoteError(message); },
        [](const lapian::app::OnlineQualityCatalog&) {});
  }
};

OnlineRequest MakeSessionRequest(const CacheKey& key) {
  OnlineRequest request;
  request.bvid = key.bvid;
  request.cid = key.cid;
  request.page = 1;
  request.title = QStringLiteral("fault test");
  request.owner = QStringLiteral("test");
  request.duration_ms = 10000;
  return request;
}

void TestSessionStallWatchdog() {
  Section("T9 网络无响应看门狗：4s 判定 → ≤5s 可读错误，缓存区仍可播");
  QTemporaryDir root;
  SessionHarness harness;
  harness.cache_root = root.path();
  CacheStore cache(root.path());
  harness.session = std::make_unique<OnlineSession>(&cache);
  auto transport = std::make_shared<FakeTransport>();
  transport->data = MakeSample(1200 * 1024);
  const QString url = QStringLiteral("https://cdn.test/stall.mp4");
  UrlScript script;
  AttemptBehavior good;
  good.bytes_limit = 400 * 1024;  // 先给 400KB（够首帧），然后挂住不响应
  good.stall = true;
  script.attempts = {good};
  transport->scripts.insert(url, script);
  harness.session->SetPlayUrlDataForTest(
      MakeDurl({url}, transport->data.size(), 64, 7));
  harness.session->SetDownloadTransportForTest(
      std::make_unique<TransportRef>(transport));
  harness.session->SetReadyThresholdForTest(300 * 1024);
  harness.session->SetQualityCatalogEnabledForTest(false);
  harness.Connect();
  const CacheKey key = MakeKey();
  QElapsedTimer start;
  start.start();
  harness.clock = &start;
  QString start_error;
  Check(harness.session->Start(MakeSessionRequest(key), &start_error),
        "stall: session started");
  Check(PumpUntil([&]() { return harness.ready; }, 8000),
        "stall: first frame ready from cached prefix");
  Check(harness.ready_info.source != nullptr, "stall: source exposed");
  Check(PumpUntil([&]() { return !harness.error.isEmpty(); }, 8000),
        "stall: readable error arrives");
  CheckLe(harness.error_ms, 5000, "stall: error within 5s of start");
  Check(harness.session->state() == OnlineState::kError,
        "stall: state = error");
  CheckContains(harness.error, QStringLiteral("网络无响应"),
                "stall: error says 网络无响应");
  Check(harness.error.contains(QStringLiteral("缓存")) ||
            harness.error.contains(QStringLiteral("播放")),
        "stall: error mentions cached playback");

  // 已发布前缀仍可读；前沿 EIO。
  const qint64 published_now =
      harness.ready_info.source ? harness.ready_info.source->Published() : 0;
  Check(published_now > 0, "stall: published prefix > 0");
  QString cursor_error;
  std::unique_ptr<GrowingCursor> cursor;
  if (harness.ready_info.source) {
    cursor = GrowingCursor::Open(harness.ready_info.source,
                                 harness.ready_info.part_path, &cursor_error);
  }
  Check(cursor != nullptr, "stall: cursor opens after watchdog error");
  if (cursor != nullptr) {
    std::uint8_t buffer[32 * 1024] = {};
    cursor->Seek(0, SEEK_SET);
    CheckEqI64(cursor->Read(buffer, sizeof(buffer)), sizeof(buffer),
               "stall: cached prefix readable after error");
    const qint64 frontier = harness.ready_info.source->Published();
    cursor->Seek(frontier, SEEK_SET);
    Check(cursor->Read(buffer, sizeof(buffer)) == AVERROR(EIO),
          "stall: frontier read = EIO");
    cursor->Close();
  }

  QElapsedTimer close_timer;
  close_timer.start();
  harness.session->Cancel();
  const qint64 cancel_ms = close_timer.elapsed();
  harness.session->Close();
  const qint64 close_ms = close_timer.elapsed();
  CheckLe(cancel_ms, 100, "stall: Cancel ≤100ms");
  CheckLe(close_ms, 1000, "stall: Close ≤1s");
}

// ---------------------------------------------------------------- T10 缓存损坏

void TestCorruptCacheRebuild() {
  Section("T10 缓存损坏（截断）：自动 Rebuild 后重下成功，不用坏数据冒充");
  QTemporaryDir root;
  QTemporaryDir records_root;
  const QString records_sentinel =
      QDir(records_root.path()).filePath(QStringLiteral("mark-sentinel.json"));
  {
    QFile sentinel(records_sentinel);
    Check(sentinel.open(QIODevice::WriteOnly), "corrupt: records sentinel created");
    sentinel.write("{\"sentinel\":1}");
    sentinel.close();
  }
  CacheStore cache(root.path());
  auto healthy = std::make_shared<FakeTransport>();
  healthy->data = MakeSample(900 * 1024);
  const QString url = QStringLiteral("https://cdn.test/corrupt.mp4");
  const CacheKey key = MakeKey();
  // 先填一个完整缓存。
  const JobOutcome first =
      RunJob(&cache, key, healthy.get(), MakePlan({url}, healthy->data.size()));
  Check(first.state == DownloadState::kCompleted, "corrupt: seed complete cache");
  const QString mp4 = cache.Mp4Path(key);
  Check(QFileInfo::exists(mp4), "corrupt: data.mp4 exists");
  // 截断：meta 说 900KB，实际只有 300KB。
  QFile truncate_file(mp4);
  Check(truncate_file.open(QIODevice::ReadWrite), "corrupt: open data.mp4");
  truncate_file.resize(300 * 1024);
  truncate_file.close();
  QString load_error;
  const auto broken = cache.Load(key, &load_error);
  Check(broken.has_value() && broken->broken, "corrupt: detected as broken");

  SessionHarness harness;
  harness.cache_root = root.path();
  harness.session = std::make_unique<OnlineSession>(&cache);
  harness.session->SetPlayUrlDataForTest(
      MakeDurl({url}, healthy->data.size(), 64, 7));
  harness.session->SetDownloadTransportForTest(
      std::make_unique<TransportRef>(healthy));
  harness.session->SetReadyThresholdForTest(300 * 1024);
  harness.session->SetQualityCatalogEnabledForTest(false);
  harness.Connect();
  QElapsedTimer timer;
  timer.start();
  QString start_error;
  const bool started = harness.session->Start(MakeSessionRequest(key), &start_error);
  Check(started, "corrupt: session start rebuilds instead of failing");
  Check(PumpUntil([&]() { return harness.ready; }, 8000),
        "corrupt: rebuilt cache opens/plays");
  CheckLe(timer.elapsed(), 5000, "corrupt: rebuild+ready within 5s");
  Check(harness.session->cache_hit() == false,
        "corrupt: not served as cache hit");
  Check(harness.error.isEmpty(),
        "corrupt: no fatal error (auto rebuild succeeded)");
  Check(QFileInfo(records_sentinel).size() > 0,
        "corrupt: records sentinel untouched");
  // 关闭：完整缓存最终落盘（defer_finalize 语义：reader 关掉后才 rename）。
  harness.session->Cancel();
  if (harness.session->HasPendingFinalize()) {
    QString finalize_error;
    Check(harness.session->FinalizeAfterReadersStopped(&finalize_error),
          "corrupt: finalize after rebuild");
  }
  QElapsedTimer close_timer;
  close_timer.start();
  harness.session->Close();
  CheckLe(close_timer.elapsed(), 1000, "corrupt: close ≤1s");
  const auto entry = cache.Load(key, nullptr);
  Check(entry.has_value() && entry->complete && !entry->broken,
        "corrupt: cache complete after rebuild+finalize");
  Check(!QFileInfo::exists(cache.PartPath(key)),
        "corrupt: .part renamed away");
}

// ---------------------------------------------------------------- T11 磁盘不足

void TestDiskFull() {
  Section("T11 磁盘不足（总字节 > 可用空间）：≤5s 可读错误，不崩");
  QTemporaryDir root;
  CacheStore cache(root.path());
  auto transport = std::make_shared<FakeTransport>();
  transport->data = MakeSample(64 * 1024);
  const QString url = QStringLiteral("https://cdn.test/huge.mp4");
  const qint64 huge = 2LL * 1024 * 1024 * 1024 * 1024 * 1024;  // 2 PB
  SessionHarness harness;
  harness.session = std::make_unique<OnlineSession>(&cache);
  harness.session->SetPlayUrlDataForTest(MakeDurl({url}, huge, 64, 7));
  harness.session->SetDownloadTransportForTest(
      std::make_unique<TransportRef>(transport));
  harness.session->SetQualityCatalogEnabledForTest(false);
  harness.Connect();
  QElapsedTimer timer;
  timer.start();
  QString start_error;
  const bool started =
      harness.session->Start(MakeSessionRequest(MakeKey()), &start_error);
  const bool failed =
      PumpUntil([&]() { return !harness.error.isEmpty(); }, 5000);
  const qint64 elapsed = timer.elapsed();
  Check(failed, "disk_full: readable failure");
  CheckLe(elapsed, 5000, "disk_full: within 5s");
  if (started) {
    Check(harness.session->state() == OnlineState::kError ||
              harness.session->state() == OnlineState::kClosed,
          "disk_full: session not playing");
  }
  Check(harness.error.contains(QStringLiteral("空间")) ||
            harness.error.contains(QStringLiteral("磁盘")) ||
            harness.error.contains(QStringLiteral("不足")),
        "disk_full: error mentions disk space");
  QElapsedTimer close_timer;
  close_timer.start();
  harness.session->Cancel();
  harness.session->Close();
  CheckLe(close_timer.elapsed(), 1000, "disk_full: close ≤1s");
}

// ---------------------------------------------------------------- T12 DASH 单流失败

QByteArray MakeDashJson(const QString& video_url, const QString& audio_url,
                        qint64 duration_seconds = 10, int video_qn = 80,
                        int video_codecid = 7, int audio_id = 30280) {
  QJsonObject video;
  video.insert(QStringLiteral("id"), video_qn);
  video.insert(QStringLiteral("baseUrl"), video_url);
  video.insert(QStringLiteral("backupUrl"), QJsonArray());
  video.insert(QStringLiteral("bandwidth"), 2000000);
  video.insert(QStringLiteral("mimeType"), QStringLiteral("video/mp4"));
  video.insert(QStringLiteral("codecs"),
               video_codecid == 12 ? QStringLiteral("hvc1.1.6.L150.90")
                                   : QStringLiteral("avc1.640028"));
  video.insert(QStringLiteral("width"), 1920);
  video.insert(QStringLiteral("height"), 1080);
  video.insert(QStringLiteral("frameRate"), QStringLiteral("30.000"));
  video.insert(QStringLiteral("codecid"), video_codecid);
  QJsonObject video_segment;
  video_segment.insert(QStringLiteral("Initialization"), QStringLiteral("0-917"));
  video_segment.insert(QStringLiteral("indexRange"), QStringLiteral("918-1245"));
  video.insert(QStringLiteral("SegmentBase"), video_segment);
  QJsonArray videos;
  videos.append(video);

  QJsonObject audio;
  audio.insert(QStringLiteral("id"), audio_id);
  audio.insert(QStringLiteral("baseUrl"), audio_url);
  audio.insert(QStringLiteral("backupUrl"), QJsonArray());
  audio.insert(QStringLiteral("bandwidth"), 128000);
  audio.insert(QStringLiteral("mimeType"), QStringLiteral("audio/mp4"));
  audio.insert(QStringLiteral("codecs"), QStringLiteral("mp4a.40.2"));
  audio.insert(QStringLiteral("codecid"), 0);
  QJsonObject audio_segment;
  audio_segment.insert(QStringLiteral("Initialization"), QStringLiteral("0-836"));
  audio_segment.insert(QStringLiteral("indexRange"), QStringLiteral("837-1152"));
  audio.insert(QStringLiteral("SegmentBase"), audio_segment);
  QJsonArray audios;
  audios.append(audio);

  QJsonObject dash;
  dash.insert(QStringLiteral("duration"), duration_seconds);
  dash.insert(QStringLiteral("video"), videos);
  dash.insert(QStringLiteral("audio"), audios);
  QJsonObject data;
  data.insert(QStringLiteral("quality"), 80);
  data.insert(QStringLiteral("format"), QStringLiteral("flv720"));
  data.insert(QStringLiteral("timelength"), duration_seconds * 1000);
  data.insert(QStringLiteral("video_codecid"), 7);
  data.insert(QStringLiteral("dash"), dash);
  QJsonObject root;
  root.insert(QStringLiteral("code"), 0);
  root.insert(QStringLiteral("message"), QStringLiteral("0"));
  root.insert(QStringLiteral("data"), data);
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

void TestDashSingleStreamFailure() {
  Section("T12 DASH 双输入单流失败：聚合失败、两路终态、缓存不写完成");
  QTemporaryDir root;
  CacheStore cache(root.path());
  const QString video_url = QStringLiteral("https://cdn.test/dash-video.m4s");
  const QString audio_url = QStringLiteral("https://cdn.test/dash-audio.m4s");
  auto http = std::make_shared<FakeHttp>();
  http->body = MakeDashJson(video_url, audio_url);
  auto transport = std::make_shared<FakeTransport>();
  transport->data = MakeSample(1500 * 1024);  // 视频流字节
  {
    UrlScript video_script;
    AttemptBehavior good;
    video_script.attempts = {good};
    transport->scripts.insert(video_url, video_script);
    UrlScript audio_script;
    AttemptBehavior audio_first;
    audio_first.bytes_limit = 200 * 1024;  // 先给 200KB 触发 ready
    AttemptBehavior audio_down;
    audio_down.network_fail = true;
    audio_script.attempts = {audio_first, audio_down, audio_down, audio_down,
                             audio_down};
    transport->scripts.insert(audio_url, audio_script);
  }

  SessionHarness harness;
  harness.session = std::make_unique<OnlineSession>(&cache);
  harness.session->SetHttpClientForTest(std::make_unique<HttpRef>(http));
  harness.session->SetDownloadTransportForTest(
      std::make_unique<TransportRef>(transport));
  harness.session->SetReadyThresholdForTest(200 * 1024);
  harness.session->SetQualityCatalogEnabledForTest(false);
  harness.session->SetDashEnabledForTest(true);
  harness.Connect();
  QElapsedTimer timer;
  timer.start();
  QString start_error;
  const bool started =
      harness.session->Start(MakeSessionRequest(MakeKey()), &start_error);
  Check(started, "dash_fail: session started");
  Check(PumpUntil([&]() { return harness.session->dash_mode(); }, 8000),
        "dash_fail: dash dual input engaged");
  Check(PumpUntil([&]() { return !harness.error.isEmpty(); }, 8000),
        "dash_fail: aggregated failure reported");
  CheckLe(timer.elapsed(), 5000, "dash_fail: failure within 5s");
  Check(harness.session->state() == OnlineState::kError,
        "dash_fail: state = error");
  CheckContains(harness.error, QStringLiteral("音频"),
                "dash_fail: error identifies audio stream");
  const DownloadState video_state = harness.session->job_state();
  Check(video_state != DownloadState::kDownloading &&
            video_state != DownloadState::kRetrying &&
            video_state != DownloadState::kRefreshing,
        "dash_fail: video job in terminal state");
  // 两路都不应该留下“完成”的缓存。
  QVector<CacheEntry> entries;
  QVector<CacheEntry> broken;
  cache.Scan(&entries, &broken, nullptr);
  bool any_complete = false;
  for (const CacheEntry& entry : entries) {
    if (entry.complete) {
      any_complete = true;
    }
  }
  Check(!any_complete, "dash_fail: no cache entry marked complete");
  Check(transport->requests_for(video_url) >= 1,
        "dash_fail: video stream was requested");
  QElapsedTimer close_timer;
  close_timer.start();
  harness.session->Cancel();
  harness.session->Close();
  CheckLe(close_timer.elapsed(), 1000, "dash_fail: close ≤1s");
  Check(harness.session->job_state() == DownloadState::kIdle,
        "dash_fail: both download jobs joined/released on close");
}

// ---------------------------------------------------------------- T8.17 切档缓存

int MediaRequestsFor(FakeTransport* transport, const QString& url) {
  int count = 0;
  for (const DownloadRequest& request : transport->requests()) {
    if (request.url == url && request.range_end < 0) {
      ++count;
    }
  }
  return count;
}

void TestDashCompleteCacheHit() {
  Section("T8.17 DASH 完整视频+音频：精确缓存命中、0 网络、0 DownloadJob");
  QTemporaryDir root;
  CacheStore cache(root.path());
  const QString bvid = QStringLiteral("BV1BMnv6jE5S");
  const qint64 cid = 42915922022;
  const QString video_url = QStringLiteral("https://cdn.test/dash-v-hit.m4s");
  const QString audio_url = QStringLiteral("https://cdn.test/dash-a-hit.m4s");
  auto transport = std::make_shared<FakeTransport>();
  transport->data = MakeSample(400 * 1024);
  auto http = std::make_shared<FakeHttp>();
  http->body = MakeDashJson(video_url, audio_url);

  const CacheKey video_key = MakeKey(bvid, cid, 80, 7);
  const CacheKey audio_key = MakeAudioKey(bvid, cid, 30280);
  lapian::online::CacheMetadata meta;
  meta.dash = true;
  meta.audio_id = 30280;
  meta.duration_ms = 10000;
  meta.width = 1920;
  meta.height = 1080;
  meta.fps = 30.0;
  Check(SeedComplete(&cache, video_key, transport->data, &meta), "hit: seed video");
  Check(SeedComplete(&cache, audio_key, transport->data), "hit: seed audio");

  SessionHarness harness;
  harness.session = std::make_unique<OnlineSession>(&cache);
  harness.session->SetHttpClientForTest(std::make_unique<HttpRef>(http));
  harness.session->SetDownloadTransportForTest(
      std::make_unique<TransportRef>(transport));
  harness.session->SetQualityCatalogEnabledForTest(false);
  harness.Connect();
  OnlineRequest request = MakeSessionRequest(video_key);
  request.requested_qn = 80;
  request.requested_codecid = 7;
  QString start_error;
  Check(harness.session->Start(request, &start_error), "hit: session started");
  Check(PumpUntil([&]() { return harness.ready; }, 8000), "hit: ready");
  Check(harness.ready_info.cache_hit, "hit: cache_hit");
  Check(harness.ready_info.dash, "hit: dash");
  Check(harness.ready_info.part_path.endsWith(QStringLiteral("data.mp4")),
        "hit: video path data.mp4");
  Check(harness.ready_info.audio_part_path.endsWith(QStringLiteral("data.mp4")),
        "hit: audio path data.mp4");
  CheckEqI64(harness.ready_info.total_bytes, transport->data.size(),
             "hit: video total");
  CheckEqI64(harness.ready_info.audio_total_bytes, transport->data.size(),
             "hit: audio total");
  Check(harness.ready_info.source != nullptr &&
            harness.ready_info.source->Published() == transport->data.size(),
        "hit: video source fully published");
  Check(harness.ready_info.audio_source != nullptr &&
            harness.ready_info.audio_source->Published() ==
                transport->data.size(),
        "hit: audio source fully published");
  CheckEqI64(harness.session->PlayUrlRequestsForTest(), 0,
             "hit: 0 playurl requests");
  CheckEqI64(harness.session->DashRequestsForTest(), 0,
             "hit: 0 DASH catalog requests");
  CheckEqI64(http->call_count(), 0, "hit: 0 HTTP calls");
  Check(transport->requests().isEmpty(), "hit: 0 CDN requests");
  Check(harness.error.isEmpty(), "hit: no error callback");
  harness.session->Close();
}

void TestDashMissingAudio() {
  Section("T8.17 DASH 视频完整/音频缺失：复用视频、只补音频、不误报");
  QTemporaryDir root;
  CacheStore cache(root.path());
  const QString bvid = QStringLiteral("BV1BMnv6jE5S");
  const qint64 cid = 42915922022;
  const QString video_url = QStringLiteral("https://cdn.test/dash-v-miss.m4s");
  const QString audio_url = QStringLiteral("https://cdn.test/dash-a-miss.m4s");
  auto transport = std::make_shared<FakeTransport>();
  transport->data = MakeSample(400 * 1024);
  auto http = std::make_shared<FakeHttp>();
  http->body = MakeDashJson(video_url, audio_url);

  const CacheKey video_key = MakeKey(bvid, cid, 80, 7);
  const CacheKey audio_key = MakeAudioKey(bvid, cid, 30280);
  lapian::online::CacheMetadata meta;
  meta.dash = true;
  meta.audio_id = 30280;
  meta.duration_ms = 10000;
  meta.width = 1920;
  meta.height = 1080;
  meta.fps = 30.0;
  Check(SeedComplete(&cache, video_key, transport->data, &meta),
        "miss_audio: seed video only");
  const QString video_mp4 = cache.Mp4Path(video_key);
  const QString video_hash = HashFile(video_mp4);

  SessionHarness harness;
  harness.session = std::make_unique<OnlineSession>(&cache);
  harness.session->SetHttpClientForTest(std::make_unique<HttpRef>(http));
  harness.session->SetDownloadTransportForTest(
      std::make_unique<TransportRef>(transport));
  harness.session->SetQualityCatalogEnabledForTest(false);
  harness.session->SetReadyThresholdForTest(64 * 1024);
  harness.Connect();
  OnlineRequest request = MakeSessionRequest(video_key);
  request.requested_qn = 80;
  request.requested_codecid = 7;
  QString start_error;
  Check(harness.session->Start(request, &start_error),
        "miss_audio: session started");
  Check(PumpUntil([&]() { return harness.ready; }, 8000),
        "miss_audio: ready");
  Check(!harness.ready_info.cache_hit, "miss_audio: not reported as full hit");
  Check(harness.ready_info.dash, "miss_audio: dash");
  CheckEqI64(harness.ready_info.source->Published(), transport->data.size(),
             "miss_audio: video reused fully");
  CheckEqI64(MediaRequestsFor(transport.get(), video_url), 0,
             "miss_audio: video not re-downloaded");
  Check(MediaRequestsFor(transport.get(), audio_url) >= 1,
        "miss_audio: audio downloaded");
  Check(harness.error.isEmpty(), "miss_audio: no error callback");
  Check(harness.session->LastErrorForTest().isEmpty(),
        "miss_audio: no user-facing complete-cache error");
  Check(PumpUntil([&]() { return harness.session->HasPendingFinalize(); },
                  8000),
        "miss_audio: audio job finalizable");
  QString finalize_error;
  Check(harness.session->FinalizeAfterReadersStopped(&finalize_error),
        "miss_audio: finalize audio");
  const std::optional<CacheEntry> audio = cache.Load(audio_key);
  Check(audio.has_value() && audio->complete,
        "miss_audio: audio entry complete");
  const std::optional<CacheEntry> video = cache.Load(video_key);
  Check(video.has_value() && video->complete,
        "miss_audio: video entry still complete");
  CheckEqI64(QFileInfo(video_mp4).size(), transport->data.size(),
             "miss_audio: video file unchanged");
  Check(HashFile(video_mp4) == video_hash,
        "miss_audio: video bytes unchanged");
  harness.session->Close();
}

void TestDashRequestedCodecMismatch() {
  Section("T8.17 请求 codecid 与缓存不同：下载请求的 HEVC，不静默复用 AVC");
  QTemporaryDir root;
  CacheStore cache(root.path());
  const QString bvid = QStringLiteral("BV1BMnv6jE5S");
  const qint64 cid = 42915922022;
  const QString hevc_url = QStringLiteral("https://cdn.test/dash-v-hevc.m4s");
  const QString audio_url = QStringLiteral("https://cdn.test/dash-a-hevc.m4s");
  auto transport = std::make_shared<FakeTransport>();
  transport->data = MakeSample(400 * 1024);
  auto http = std::make_shared<FakeHttp>();
  http->body = MakeDashJson(hevc_url, audio_url, 10, 80, 12, 30280);

  const CacheKey avc_key = MakeKey(bvid, cid, 80, 7);
  const CacheKey hevc_key = MakeKey(bvid, cid, 80, 12);
  const CacheKey audio_key = MakeAudioKey(bvid, cid, 30280);
  lapian::online::CacheMetadata meta;
  meta.dash = true;
  meta.audio_id = 30280;
  meta.duration_ms = 10000;
  meta.width = 1920;
  meta.height = 1080;
  meta.fps = 30.0;
  Check(SeedComplete(&cache, avc_key, transport->data, &meta),
        "codec: seed AVC video");
  Check(SeedComplete(&cache, audio_key, transport->data),
        "codec: seed audio");
  const QString avc_mp4 = cache.Mp4Path(avc_key);
  const QString avc_hash = HashFile(avc_mp4);

  SessionHarness harness;
  harness.session = std::make_unique<OnlineSession>(&cache);
  harness.session->SetHttpClientForTest(std::make_unique<HttpRef>(http));
  harness.session->SetDownloadTransportForTest(
      std::make_unique<TransportRef>(transport));
  harness.session->SetQualityCatalogEnabledForTest(false);
  harness.session->SetReadyThresholdForTest(64 * 1024);
  harness.Connect();
  OnlineRequest request = MakeSessionRequest(avc_key);
  request.requested_qn = 80;
  request.requested_codecid = 12;
  QString start_error;
  Check(harness.session->Start(request, &start_error),
        "codec: session started");
  Check(PumpUntil([&]() { return harness.ready; }, 8000), "codec: ready");
  Check(!harness.ready_info.cache_hit, "codec: not a cache hit");
  CheckEqI64(harness.ready_info.codecid, 12, "codec: actual codecid 12");
  CheckEqI64(harness.ready_info.qn, 80, "codec: actual qn 80");
  Check(MediaRequestsFor(transport.get(), hevc_url) >= 1,
        "codec: requested HEVC downloaded");
  Check(harness.error.isEmpty(), "codec: no error callback");
  Check(PumpUntil([&]() { return harness.session->HasPendingFinalize(); },
                  8000),
        "codec: HEVC finalizable");
  QString finalize_error;
  Check(harness.session->FinalizeAfterReadersStopped(&finalize_error),
        "codec: finalize HEVC");
  const std::optional<CacheEntry> hevc = cache.Load(hevc_key);
  Check(hevc.has_value() && hevc->complete, "codec: HEVC entry complete");
  const std::optional<CacheEntry> avc = cache.Load(avc_key);
  Check(avc.has_value() && avc->complete, "codec: AVC entry still complete");
  CheckEqI64(QFileInfo(avc_mp4).size(), transport->data.size(),
             "codec: AVC file unchanged");
  Check(HashFile(avc_mp4) == avc_hash, "codec: AVC bytes unchanged");
  harness.session->Close();
}

void TestDashResolvedBothComplete() {
  Section("T8.17 解析后实际键两条都完整：走 cache-hit，不下载媒体");
  QTemporaryDir root;
  CacheStore cache(root.path());
  const QString bvid = QStringLiteral("BV1BMnv6jE5S");
  const qint64 cid = 42915922022;
  const QString video_url = QStringLiteral("https://cdn.test/dash-v-post.m4s");
  const QString audio_url = QStringLiteral("https://cdn.test/dash-a-post.m4s");
  auto transport = std::make_shared<FakeTransport>();
  transport->data = MakeSample(400 * 1024);
  auto http = std::make_shared<FakeHttp>();
  http->body = MakeDashJson(video_url, audio_url);

  const CacheKey video_key = MakeKey(bvid, cid, 80, 7);
  const CacheKey audio_key = MakeAudioKey(bvid, cid, 30280);
  lapian::online::CacheMetadata stale_meta;
  stale_meta.dash = true;
  stale_meta.audio_id = 9999;  // meta 指向不存在的音频，迫使解析后按实际键检查
  stale_meta.duration_ms = 10000;
  stale_meta.width = 1920;
  stale_meta.height = 1080;
  stale_meta.fps = 30.0;
  Check(SeedComplete(&cache, video_key, transport->data, &stale_meta),
        "post: seed video with stale audio_id");
  Check(SeedComplete(&cache, audio_key, transport->data),
        "post: seed actual audio");

  SessionHarness harness;
  harness.session = std::make_unique<OnlineSession>(&cache);
  harness.session->SetHttpClientForTest(std::make_unique<HttpRef>(http));
  harness.session->SetDownloadTransportForTest(
      std::make_unique<TransportRef>(transport));
  harness.session->SetQualityCatalogEnabledForTest(false);
  harness.Connect();
  OnlineRequest request = MakeSessionRequest(video_key);
  request.requested_qn = 80;
  request.requested_codecid = 7;
  QString start_error;
  Check(harness.session->Start(request, &start_error), "post: session started");
  Check(PumpUntil([&]() { return harness.ready; }, 8000), "post: ready");
  Check(harness.ready_info.cache_hit, "post: cache_hit after resolve");
  Check(harness.ready_info.dash, "post: dash");
  CheckEqI64(harness.session->PlayUrlRequestsForTest(), 0,
             "post: 0 playurl requests");
  CheckEqI64(harness.session->DashRequestsForTest(), 1,
             "post: 1 DASH catalog request");
  CheckEqI64(MediaRequestsFor(transport.get(), video_url), 0,
             "post: no video media request");
  CheckEqI64(MediaRequestsFor(transport.get(), audio_url), 0,
             "post: no audio media request");
  CheckEqI64(harness.ready_info.source->Published(), transport->data.size(),
             "post: video source fully published");
  CheckEqI64(harness.ready_info.audio_source->Published(),
             transport->data.size(), "post: audio source fully published");
  Check(harness.error.isEmpty(), "post: no error callback");
  harness.session->Close();
}

// ---------------------------------------------------------------- T13 关闭/取消

void TestCloseWhileBuffering() {
  Section("T13 关闭阻塞：缓冲中/未缓存读等待时 Cancel ≤100ms、Close ≤1s");
  QTemporaryDir root;
  CacheStore cache(root.path());
  auto transport = std::make_shared<FakeTransport>();
  transport->data = MakeSample(600 * 1024);
  const QString url = QStringLiteral("https://cdn.test/slow.mp4");
  UrlScript script;
  AttemptBehavior stall;
  stall.stall = true;  // 一直阻塞，直到被取消
  script.attempts = {stall};
  transport->scripts.insert(url, script);
  SessionHarness harness;
  harness.session = std::make_unique<OnlineSession>(&cache);
  harness.session->SetPlayUrlDataForTest(
      MakeDurl({url}, transport->data.size()));
  harness.session->SetDownloadTransportForTest(
      std::make_unique<TransportRef>(transport));
  harness.session->SetQualityCatalogEnabledForTest(false);
  harness.Connect();
  QString start_error;
  Check(harness.session->Start(MakeSessionRequest(MakeKey()), &start_error),
        "close: session started");
  // 等下载线程真正进入阻塞的 GetRange（假实现计数 stall_entries）。
  const bool entered = PumpUntil([&]() {
    return transport->stall_entries() > 0;
  }, 8000);
  Check(entered, "close: download thread stuck in GetRange");
  QElapsedTimer timer;
  timer.start();
  harness.session->Cancel();
  const qint64 cancel_ms = timer.elapsed();
  harness.session->Close();
  const qint64 close_ms = timer.elapsed();
  CheckLe(cancel_ms, 100, "close: Cancel ≤100ms");
  CheckLe(close_ms, 1000, "close: Close ≤1s with blocked download");
  Check(harness.session->state() == OnlineState::kClosed ||
            harness.session->state() == OnlineState::kIdle,
        "close: session closed");
}

// ---------------------------------------------------------------- T14 脱敏

void TestRedaction() {
  Section("T14 日志脱敏：错误/状态不含 SESSDATA/签名参数/完整 URL");
  const QString sentinel_sessdata =
      QStringLiteral("SENTINEL_SESSDATA_9f3a2b7c");
  const QString signed_url =
      QStringLiteral("https://cdn.test/v.mp4?deadline=1700000000&upsig=SENTINELSIG&"
                     "oi=123&n=abc");
  QTemporaryDir root;
  CacheStore cache(root.path());
  auto transport = std::make_shared<FakeTransport>();
  transport->data = MakeSample(300 * 1024);
  UrlScript forbidden;
  AttemptBehavior b403;
  b403.http_status = 403;
  forbidden.attempts = {b403};
  transport->scripts.insert(signed_url, forbidden);
  SessionHarness harness;
  harness.session = std::make_unique<OnlineSession>(&cache);
  harness.session->SetPlayUrlDataForTest(
      MakeDurl({signed_url}, transport->data.size()));
  harness.session->SetDownloadTransportForTest(
      std::make_unique<TransportRef>(transport));
  harness.session->SetQualityCatalogEnabledForTest(false);
  harness.Connect();
  OnlineRequest request = MakeSessionRequest(MakeKey());
  request.sessdata = sentinel_sessdata;
  QString start_error;
  harness.session->Start(request, &start_error);
  PumpUntil([&]() { return !harness.error.isEmpty(); }, 8000);
  harness.session->Cancel();
  harness.session->Close();
  const QString all_messages =
      harness.error + QStringLiteral("\n") + harness.last_state + QStringLiteral("\n") +
      start_error + QStringLiteral("\n") + harness.session->LastErrorForTest();
  CheckNotContains(all_messages, sentinel_sessdata, "redaction: no SESSDATA");
  CheckNotContains(all_messages, QStringLiteral("deadline="),
                   "redaction: no deadline param");
  CheckNotContains(all_messages, QStringLiteral("upsig="),
                   "redaction: no upsig param");
  CheckNotContains(all_messages, QStringLiteral("SENTINELSIG"),
                   "redaction: no signature value");
  CheckNotContains(all_messages, QStringLiteral("https://cdn.test"),
                   "redaction: no full CDN URL");
  // 缓存 meta 也不能含签名 URL/SESSDATA。
  QVector<CacheEntry> entries;
  cache.Scan(&entries, nullptr, nullptr);
  for (const CacheEntry& entry : entries) {
    QFile meta(QDir(entry.dir).filePath(QStringLiteral("meta.json")));
    if (!meta.open(QIODevice::ReadOnly)) {
      continue;
    }
    const QString text = QString::fromUtf8(meta.readAll());
    CheckNotContains(text, QStringLiteral("upsig"), "redaction: meta no upsig");
    CheckNotContains(text, sentinel_sessdata, "redaction: meta no SESSDATA");
  }
}

// ---------------------------------------------------------------- T15 并发 4 读者

void TestFourReaders(int minutes) {
  Section(minutes >= 1 ? "T15 并发 4 读者（长跑）" : "T15 并发 4 读者（smoke）");
  QTemporaryDir root;
  CacheStore cache(root.path());
  const qint64 total = 8 * 1024 * 1024;
  QByteArray sample = MakeSample(total);
  const QString url = QStringLiteral("https://cdn.test/soak.mp4");
  FakeTransport transport;
  transport.data = sample;
  // 受控速率：默认 smoke 8s 内下完；长跑按总时长摊平。
  const int seconds = std::max(8, minutes * 60);
  UrlScript script;
  AttemptBehavior steady;
  steady.chunk_bytes = 32 * 1024;
  steady.chunk_delay_ms = 20;
  AttemptBehavior transient;
  transient.chunk_bytes = 32 * 1024;
  transient.bytes_limit = 128 * 1024;  // 先断一次（模拟瞬时断网）
  script.attempts = {transient, steady};
  transport.scripts.insert(url, script);
  const CacheKey key = MakeKey();
  auto source = std::make_shared<GrowingSource>(total);
  DownloadOptions options;
  options.defer_finalize = true;  // 读者持有句柄时不能 rename（真实产品路径）
  options.throttle_bytes_per_sec = total / seconds;
  DownloadJob job(&cache, key, source, &transport, MakePlan({url}, total), {},
                  options);
  QString start_error;
  const bool job_started = job.Start(&start_error);
  Check(job_started, "readers: download started");
  if (!job_started) {
    return;
  }

  constexpr int kReaders = 4;
  std::atomic<bool> stop{false};
  std::array<std::atomic<std::int64_t>, kReaders> ops;
  std::array<std::atomic<int>, kReaders> errors;
  std::vector<std::thread> readers;
  for (int i = 0; i < kReaders; ++i) {
    ops[i] = 0;
    errors[i] = 0;
    readers.emplace_back([&, i]() {
      QString cursor_error;
      auto cursor = GrowingCursor::Open(source, cache.PartPath(key), &cursor_error);
      if (!cursor) {
        errors[i] = 1000;
        return;
      }
      cursor->SetWaitTimeoutMs(60000);  // 只用于诊断；10min 慢增长下不能误判丢唤醒
      std::mt19937_64 rng(static_cast<std::uint64_t>(i) + 777);
      std::uint8_t buffer[32 * 1024] = {};
      while (!stop.load()) {
        const qint64 published = source->Published();
        const qint64 upper =
            std::min<qint64>(published + 64 * 1024, total - 32 * 1024);
        const qint64 target =
            upper > 0 ? static_cast<qint64>(rng() % static_cast<std::uint64_t>(upper))
                      : 0;
        if (cursor->Seek(target, SEEK_SET) < 0) {
          errors[i] = 2000;
          return;
        }
        const int read = cursor->Read(buffer, sizeof(buffer));
        if (read < 0) {
          if (read != AVERROR_EXIT) {
            errors[i] = read;
          }
          return;
        }
        ++ops[i];
      }
      cursor->Close();
    });
  }

  QElapsedTimer run_timer;
  run_timer.start();
  QElapsedTimer sample_timer;
  sample_timer.start();
  std::int64_t last_total_ops = 0;
  qint64 last_progress_ms = 0;
  int pauses = 0;
  int stalls = 0;
  const qint64 run_ms = static_cast<qint64>(seconds) * 1000;
  while (run_timer.elapsed() < run_ms) {
    QThread::msleep(50);
    if (job.state() != DownloadState::kDownloading &&
        job.state() != DownloadState::kPreparing &&
        job.state() != DownloadState::kRetrying &&
        job.state() != DownloadState::kPaused) {
      break;  // 下载完成/失败
    }
    if (sample_timer.elapsed() >= 1000) {
      sample_timer.restart();
      std::int64_t total_ops = 0;
      for (int i = 0; i < kReaders; ++i) {
        total_ops += ops[i].load();
      }
      if (total_ops > last_total_ops) {
        last_total_ops = total_ops;
        last_progress_ms = run_timer.elapsed();
      } else if (run_timer.elapsed() - last_progress_ms > 120000) {
        Check(false, "readers: no progress for 120s (deadlock)");
        break;
      }
      // 下载暂停/恢复交错（每 20s 一次，1s 暂停）。
      if (minutes >= 1 && run_timer.elapsed() / 20000 > pauses) {
        ++pauses;
        job.Pause();
        QThread::msleep(1000);
        job.Resume();
      }
      // 瞬时断网已在开头注入（AttemptBehavior transient）；这里只统计重试次数。
      stalls = static_cast<int>(job.stats().retries);
    }
  }
  QElapsedTimer close_timer;
  close_timer.start();
  stop = true;
  source->Cancel();
  for (auto& reader : readers) {
    reader.join();
  }
  job.Cancel();
  job.Wait(3000);
  const qint64 close_ms = close_timer.elapsed();
  std::printf("fault_readers minutes=%d seconds=%d close_ms=%lld "
              "state=%s retries=%lld stalls=%d pauses=%d\n",
              minutes, seconds, static_cast<long long>(close_ms),
              lapian::online::DownloadStateName(job.state()).toUtf8().constData(),
              static_cast<long long>(job.stats().retries), stalls, pauses);
  std::fflush(stdout);
  CheckLe(close_ms, 1000, "readers: close ≤1s");
  std::int64_t total_ops = 0;
  for (int i = 0; i < kReaders; ++i) {
    CheckEqI64(errors[i].load(), 0,
               (QStringLiteral("readers: reader %1 no error").arg(i))
                   .toUtf8()
                   .constData());
    Check(ops[i].load() > 0,
          (QStringLiteral("readers: reader %1 made progress").arg(i))
              .toUtf8()
              .constData());
    total_ops += ops[i].load();
  }
  Check(total_ops > 20, "readers: total ops > 20");
  const auto stats = source->Stats();
  CheckEqI64(stats.timeouts, 0, "readers: no lost wakeups");
  CheckEqI64(stats.active_waiters, 0, "readers: no active waiters after close");
  Check(stats.waits > 0, "readers: blocking waits exercised");
  std::printf("fault_readers_ops total=%lld waits=%lld published=%lld\n",
              static_cast<long long>(total_ops),
              static_cast<long long>(stats.waits),
              static_cast<long long>(stats.published));
  std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  // 测试必须有自己的临时根：TMP 不可写时 QTemporaryDir 会失败，
  // CacheStore("") 会退回真实 %LOCALAPPDATA% 缓存——绝不允许。
  {
    QTemporaryDir probe;
    if (!probe.isValid()) {
      std::printf("online_fault_test skip: QTemporaryDir invalid (set TMP/TEMP "
                  "to a writable dir)\n");
      std::fflush(stdout);
      return 77;
    }
  }
  int readers_minutes = 0;
  const QStringList args = QCoreApplication::arguments();
  for (int i = 1; i < args.size(); ++i) {
    if (args.at(i) == QStringLiteral("--readers-minutes") && i + 1 < args.size()) {
      readers_minutes = args.at(++i).toInt();
    } else if (args.at(i).startsWith(QStringLiteral("--readers-minutes="))) {
      readers_minutes = args.at(i)
                            .mid(static_cast<int>(QStringLiteral("--readers-minutes=").size()))
                            .toInt();
    }
  }
  if (readers_minutes < 0) {
    readers_minutes = 0;
  }
  std::printf("T8.13 online_fault_test begin checks=%d readers_minutes=%d "
              "api_timeout_ms=%d stall_timeout_ms=%d\n",
              0, readers_minutes, lapian::online::kOnlineApiTimeoutMs,
              lapian::online::kDownloadStallTimeoutMs);
  std::fflush(stdout);

  TestDisconnectResume();
  TestAllUrlsDown();
  TestUncachedSeekWhileDown();
  TestBackupSwitch();
  TestUrlExpiredRefresh();
  TestRefreshFailure();
  TestApiErrorMatrix();
  TestRealHttpTimeoutBound();
  TestSessionStallWatchdog();
  TestCorruptCacheRebuild();
  TestDiskFull();
  TestDashSingleStreamFailure();
  TestDashCompleteCacheHit();
  TestDashMissingAudio();
  TestDashRequestedCodecMismatch();
  TestDashResolvedBothComplete();
  TestCloseWhileBuffering();
  TestRedaction();
  TestFourReaders(readers_minutes);

  std::printf("\nT8.13 online_fault_test result checks=%d failures=%d verdict=%s\n",
              g_checks, g_failures, g_failures == 0 ? "pass" : "fail");
  std::fflush(stdout);
  return g_failures == 0 ? 0 : 1;
}
