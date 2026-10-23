// T8.5 durl 取流单测（fake transport，不联网、不需要 SESSDATA）。
//
// 覆盖（对应 tasks-v2 T8.5 验收）：
// - playurl URL 构造/JSON 解析/错误码/无 durl/无 size/非法 URL；
// - 顺序下载 + 206 + Content-Range 对账 + MarkComplete 改名；
// - existing>0 续传：Range 起点 == .part 长度，最终 hash 正确；
// - 200 回退：truncate 从 0 重下，range_ignored=1，无重复字节；
// - 备份 URL 切换、刷新 playurl 一次成功/失败；
// - Content-Range 起点/总长不一致：失败并保留 .part；
// - 取消 ≤100ms、.part 保留、source Cancel；再次 Job 可续传；
// - 暂停/恢复；
// - 最终 .mp4 可被 FFmpeg 打开。
//
// 退出码：0 = 全部通过；1 = 失败；77 = 缺少本地样本（由 CTest 标 SKIP）。

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QVector>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
}

#include "online/CacheStore.h"
#include "online/DownloadJob.h"
#include "online/DownloadTransport.h"
#include "online/GrowingSource.h"
#include "online/PlayUrl.h"

namespace {

using lapian::online::BiliErrorKind;
using lapian::online::BiliResolveError;
using lapian::online::CacheEntry;
using lapian::online::CacheKey;
using lapian::online::CacheStore;
using lapian::online::DownloadJob;
using lapian::online::DownloadPlan;
using lapian::online::DownloadRequest;
using lapian::online::DownloadState;
using lapian::online::DownloadStats;
using lapian::online::DownloadTransportResult;
using lapian::online::DownloadTransportStatus;
using lapian::online::GrowingSource;
using lapian::online::HttpCancelToken;
using lapian::online::IDownloadSink;
using lapian::online::IDownloadTransport;
using lapian::online::PlayUrlData;

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* what) {
  ++g_checks;
  if (condition) {
    std::printf("[ OK ] %s\n", what);
  } else {
    std::printf("[FAIL] %s\n", what);
    ++g_failures;
  }
}

void CheckEq(const QString& actual, const QString& expected, const char* what) {
  ++g_checks;
  if (actual == expected) {
    std::printf("[ OK ] %s\n", what);
    return;
  }
  std::printf("[FAIL] %s\n  实际：%s\n  期望：%s\n", what,
              actual.toUtf8().constData(), expected.toUtf8().constData());
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
  ++g_failures;
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

QString HashBytes(const QByteArray& bytes) {
  return QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool CanOpenMedia(const QString& path, QString* error) {
  AVFormatContext* context = nullptr;
  const QByteArray utf8_path = path.toUtf8();
  int ret = avformat_open_input(&context, utf8_path.constData(), nullptr, nullptr);
  if (ret < 0) {
    if (error != nullptr) {
      *error = QStringLiteral("avformat_open_input=%1").arg(ret);
    }
    return false;
  }
  ret = avformat_find_stream_info(context, nullptr);
  const bool ok = ret >= 0 && context->nb_streams > 0;
  if (!ok && error != nullptr) {
    *error = QStringLiteral("find_stream_info=%1").arg(ret);
  }
  avformat_close_input(&context);
  return ok;
}

struct FakeBehavior {
  int http_status = 0;         // 非 0 = 直接返回该 HTTP 状态（403/500 等）
  bool ignore_range = false;   // offset>0 时忽略 Range，返回“200 全量”
  bool start_mismatch = false; // 206 但 Content-Range 起点 != offset
  qint64 total_override = -1;  // Content-Range 总长（用于总长不一致）
  int disconnects = 0;         // 前 N 次尝试在 disconnect_after 字节后断连
  qint64 disconnect_after = 0; // 每次连接最多给这么多字节
  int disconnect_delay_ms = 0; // 断连后等待（模拟断网 5s）
  bool block = false;          // 一直阻塞到取消（测取消延迟）
  int chunk_delay_ms = 0;      // 每块延迟（测暂停）
  qint64 chunk_bytes = 128 * 1024;
};

class FakeTransport final : public IDownloadTransport {
 public:
  QByteArray data;
  QHash<QString, FakeBehavior> behaviors;
  QStringList urls;

  DownloadTransportResult GetRange(const DownloadRequest& request,
                                   IDownloadSink* sink,
                                   const HttpCancelToken& cancel) override {
    DownloadTransportResult result;
    {
      QMutexLocker lock(&mutex_);
      requests_.append(request);
    }
    FakeBehavior behavior = behaviors.value(request.url);
    if (behavior.http_status != 0) {
      result.status = DownloadTransportStatus::kHttpStatus;
      result.http_status = behavior.http_status;
      result.error = QStringLiteral("HTTP %1").arg(behavior.http_status);
      return result;
    }
    if (request.offset > data.size()) {
      result.status = DownloadTransportStatus::kHttpStatus;
      result.http_status = 416;
      result.error = QStringLiteral("HTTP 416");
      return result;
    }
    if (behavior.ignore_range && request.offset > 0) {
      result.status = DownloadTransportStatus::kRangeIgnored;
      result.range_ignored = true;
      result.http_status = 200;
      result.error = QStringLiteral("range ignored");
      return result;
    }
    if (behavior.block) {
      while (!cancel.IsCancelled()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      result.status = DownloadTransportStatus::kCancelled;
      result.cancelled = true;
      result.error = QStringLiteral("cancelled");
      return result;
    }

    result.http_status = request.offset > 0 ? 206 : 200;
    if (result.http_status == 206) {
      result.content_range_total = data.size();
      result.content_range_start = request.offset;
      result.content_range_end = data.size() - 1;
      if (behavior.start_mismatch) {
        result.content_range_start = request.offset + 1;
        result.status = DownloadTransportStatus::kRangeStartMismatch;
        result.start_mismatch = true;
        result.error = QStringLiteral("start mismatch");
        return result;
      }
    } else {
      result.content_length = data.size();
    }
    if (behavior.total_override >= 0) {
      result.content_range_total = behavior.total_override;
      result.total_mismatch = true;
      result.status = DownloadTransportStatus::kTotalMismatch;
      result.error = QStringLiteral("total mismatch");
      return result;
    }

    qint64 limit = data.size();
    bool will_disconnect = false;
    {
      QMutexLocker lock(&mutex_);
      if (behavior.disconnects > 0 &&
          behavior.disconnect_after >= 0) {
        limit = std::min<qint64>(limit, request.offset + behavior.disconnect_after);
        will_disconnect = limit < data.size();
        if (will_disconnect) {
          --behaviors[request.url].disconnects;
        }
      }
    }
    qint64 offset = request.offset;
    const qint64 start_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count();
    while (offset < limit) {
      if (cancel.IsCancelled()) {
        result.status = DownloadTransportStatus::kCancelled;
        result.cancelled = true;
        result.bytes_written = offset - request.offset;
        result.error = QStringLiteral("cancelled");
        return result;
      }
      const qint64 chunk =
          std::min<qint64>(behavior.chunk_bytes, limit - offset);
      QString sink_error;
      if (sink == nullptr ||
          !sink->Write(reinterpret_cast<const std::uint8_t*>(
                           data.constData() + offset),
                       static_cast<std::size_t>(chunk), &sink_error)) {
        result.status = DownloadTransportStatus::kSinkError;
        result.bytes_written = offset - request.offset;
        result.error = sink_error;
        return result;
      }
      offset += chunk;
      if (behavior.chunk_delay_ms > 0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(behavior.chunk_delay_ms));
      }
    }
    const qint64 end_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
    result.bytes_written = offset - request.offset;
    result.first_byte_ms = 0;
    result.elapsed_ms = (end_ns - start_ns) / (1000 * 1000);
    if (will_disconnect || offset < data.size()) {
      if (will_disconnect && behavior.disconnect_delay_ms > 0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(behavior.disconnect_delay_ms));
      }
      result.status = DownloadTransportStatus::kNetwork;
      result.error = QStringLiteral("connection closed");
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

 private:
  mutable QMutex mutex_;
  QVector<DownloadRequest> requests_;
};

CacheKey MakeKey() {
  CacheKey key;
  key.bvid = QStringLiteral("BV1BMnv6jE5S");
  key.cid = 42915922022LL;
  key.qn = 64;
  key.codecid = 7;
  return key;
}

DownloadPlan MakePlan(const QStringList& urls, qint64 total) {
  DownloadPlan plan;
  plan.urls = urls;
  plan.total_bytes = total;
  plan.qn = 64;
  plan.codecid = 7;
  plan.format = QStringLiteral("mp4720");
  plan.metadata.duration_ms = 1000;
  return plan;
}

QString PartPathFor(CacheStore* cache, const CacheKey& key) {
  return cache->PartPath(key);
}

QString Mp4PathFor(CacheStore* cache, const CacheKey& key) {
  return cache->Mp4Path(key);
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

struct JobOutcome {
  DownloadState state = DownloadState::kIdle;
  DownloadStats stats;
  bool ok = false;
};

JobOutcome RunJob(CacheStore* cache, const CacheKey& key, FakeTransport* transport,
                  const DownloadPlan& plan,
                  lapian::online::DownloadRefreshCallback refresh = {},
                  qint64* source_published = nullptr,
                  std::shared_ptr<GrowingSource>* source_out = nullptr) {
  JobOutcome outcome;
  auto source = std::make_shared<GrowingSource>(plan.total_bytes);
  DownloadJob job(cache, key, source, transport, plan, std::move(refresh));
  QString error;
  if (!job.Start(&error)) {
    outcome.state = job.state();
    outcome.stats = job.stats();
    outcome.stats.error = error;
    if (source_out != nullptr) {
      *source_out = source;
    }
    return outcome;
  }
  outcome.ok = job.Wait(60000);
  outcome.state = job.state();
  outcome.stats = job.stats();
  if (source_published != nullptr) {
    *source_published = source->Published();
  }
  if (source_out != nullptr) {
    *source_out = source;
  }
  return outcome;
}

QByteArray LoadSample() {
  const QStringList candidates = {
      QDir::current().filePath(QStringLiteral("build/test-media/faststart-720p.mp4")),
      QDir::current().filePath(QStringLiteral("build/test-media/faststart-60s.mp4")),
  };
  for (const QString& path : candidates) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
      return file.readAll();
    }
  }
  return {};
}

}  // namespace

int main() {
  // ---------------------------------------------------------------- playurl
  {
    const QString url = lapian::online::BuildPlayUrlUrl(
        QStringLiteral("BV1BMnv6jE5S"), 42915922022LL, 64);
    Check(url.startsWith(QStringLiteral("https://api.bilibili.com/x/player/playurl?")),
          "playurl host/path");
    Check(url.contains(QStringLiteral("bvid=BV1BMnv6jE5S")), "playurl bvid");
    Check(url.contains(QStringLiteral("cid=42915922022")), "playurl cid");
    Check(url.contains(QStringLiteral("qn=64")), "playurl qn=64");
    Check(url.contains(QStringLiteral("fnval=0")), "playurl fnval=0");
    Check(lapian::online::BuildPlayUrlUrl(QStringLiteral("av123"), 1, 64).isEmpty(),
          "playurl invalid bvid -> empty");
    Check(lapian::online::BuildPlayUrlUrl(QStringLiteral("BV1BMnv6jE5S"), 0, 64).isEmpty(),
          "playurl invalid cid -> empty");

    const QByteArray good = R"({
      "code": 0, "message": "0",
      "data": {
        "quality": 64, "format": "mp4720", "timelength": 110528,
        "video_codecid": 7,
        "durl": [{
          "order": 1, "length": 110528, "size": 14330392,
          "url": "https://cn-gdgz-fx-01-05.bilivideo.com/video.mp4?deadline=1&sig=2",
          "backup_url": [
            "https://upos-sz-mirrorcos.bilivideo.com/video.mp4?deadline=3",
            "https://upos-hz-mirrorakam.akamaized.net/video.mp4?deadline=4"
          ]
        }]
      }
    })";
    PlayUrlData parsed;
    BiliResolveError error = lapian::online::ParsePlayUrlJson(good, &parsed);
    Check(error.ok(), "parse playurl ok");
    CheckEqI64(parsed.quality, 64, "parse actual qn");
    CheckEq(parsed.format, QStringLiteral("mp4720"), "parse format");
    CheckEqI64(parsed.codecid, 7, "parse codecid");
    CheckEqI64(parsed.TotalBytes(), 14330392, "parse total size");
    CheckEqI64(parsed.durls.first().AllUrls().size(), 3, "main + 2 backups");
    Check(parsed.durls.first().length_ms == 110528, "parse durl length");
    Check(lapian::online::IsM1Quality(parsed.quality), "qn 64 is M1 720P");
    CheckEq(lapian::online::QualityName(16), QStringLiteral("360P"),
            "quality name 360P");

    PlayUrlData no_durl;
    error = lapian::online::ParsePlayUrlJson(
        R"({"code":0,"data":{"quality":64,"format":"mp4720","durl":[]}})",
        &no_durl);
    Check(!error.ok(), "no durl is an error");
    CheckEq(error.detail, QStringLiteral("no-durl"), "no durl detail");
    CheckEqI64(no_durl.quality, 64, "no durl still records actual qn");

    PlayUrlData no_size;
    error = lapian::online::ParsePlayUrlJson(
        R"({"code":0,"data":{"quality":64,"format":"mp4720","durl":[{"url":"https://cdn.example/x.mp4"}]}})",
        &no_size);
    CheckEq(error.detail, QStringLiteral("no-size"), "missing size detail");

    PlayUrlData api_error;
    error = lapian::online::ParsePlayUrlJson(
        R"({"code":-400,"message":"请求错误"})", &api_error);
    Check(error.kind == BiliErrorKind::kApi, "api -400 kind");
    Check(error.api_code == -400, "api -400 code");
    Check(error.message.contains(QStringLiteral("参数")), "api -400 message");

    PlayUrlData bad_json;
    error = lapian::online::ParsePlayUrlJson("{not json", &bad_json);
    Check(error.kind == BiliErrorKind::kParse, "bad json kind");

    PlayUrlData http_url;
    error = lapian::online::ParsePlayUrlJson(
        R"({"code":0,"data":{"quality":64,"format":"mp4","video_codecid":7,"durl":[{"size":10,"url":"http://cdn.example/x.mp4"}]}})",
        &http_url);
    Check(!error.ok(), "http durl rejected");
  }

  const QByteArray sample = LoadSample();
  if (sample.isEmpty()) {
    std::printf("[SKIP] 缺少 build/test-media/faststart-720p.mp4 或 faststart-60s.mp4\n");
    return 77;
  }
  const QString sample_hash = HashBytes(sample);
  const qint64 total = sample.size();
  std::printf("sample_bytes=%lld sample_sha256=%s\n",
              static_cast<long long>(total), sample_hash.toUtf8().constData());

  // ---------------------------------------------------------------- 正常下载
  {
    QTemporaryDir root;
    CacheStore cache(root.path());
    FakeTransport transport;
    transport.data = sample;
    transport.urls = {QStringLiteral("https://cdn1.example/v.mp4")};
    const CacheKey key = MakeKey();
    const JobOutcome outcome =
        RunJob(&cache, key, &transport,
               MakePlan(transport.urls, total));
    Check(outcome.state == DownloadState::kCompleted, "normal: completed");
    Check(outcome.stats.error.isEmpty(), "normal: no error");
    CheckEqI64(outcome.stats.written_bytes, total, "normal: written == total");
    CheckEqI64(outcome.stats.http_206, 0, "normal: offset 0 uses 200");
    CheckEqI64(outcome.stats.http_200, 1, "normal: one 200");
    CheckEqI64(outcome.stats.range_requests, 1, "normal: one range request");
    const QVector<DownloadRequest> requests = transport.requests();
    Check(!requests.isEmpty() && requests.first().offset == 0,
          "normal: first range offset 0");
    Check(!QFileInfo::exists(PartPathFor(&cache, key)), "normal: .part renamed");
    const QString mp4 = Mp4PathFor(&cache, key);
    Check(QFileInfo::exists(mp4), "normal: .mp4 exists");
    CheckEqI64(QFileInfo(mp4).size(), total, "normal: .mp4 size");
    CheckEq(HashFile(mp4), sample_hash, "normal: .mp4 sha256");
    QString probe_error;
    Check(CanOpenMedia(mp4, &probe_error),
          probe_error.isEmpty()
              ? "normal: ffprobe opens .mp4"
              : qPrintable(QStringLiteral("normal: ffprobe opens .mp4 (%1)")
                               .arg(probe_error)));
    QString open_path;
    QString open_error;
    Check(cache.OpenComplete(key, &open_path, &open_error),
          "normal: OpenComplete");
  }

  // ---------------------------------------------------------------- 续传
  {
    QTemporaryDir root;
    CacheStore cache(root.path());
    FakeTransport transport;
    transport.data = sample;
    transport.urls = {QStringLiteral("https://cdn1.example/v.mp4")};
    const CacheKey key = MakeKey();
    const qint64 prefix = total / 3;
    SeedPart(&cache, key, sample, prefix);
    const JobOutcome outcome =
        RunJob(&cache, key, &transport, MakePlan(transport.urls, total));
    Check(outcome.state == DownloadState::kCompleted, "resume: completed");
    CheckEqI64(outcome.stats.existing_bytes, prefix, "resume: existing bytes");
    const QVector<DownloadRequest> requests = transport.requests();
    Check(!requests.isEmpty() && requests.first().offset == prefix,
          "resume: first range offset == existing");
    CheckEqI64(outcome.stats.http_206, 1, "resume: 206");
    CheckEqI64(outcome.stats.downloaded_bytes, total - prefix,
                "resume: downloaded == remaining");
    const QString mp4 = Mp4PathFor(&cache, key);
    CheckEqI64(QFileInfo(mp4).size(), total, "resume: no duplicate bytes");
    CheckEq(HashFile(mp4), sample_hash, "resume: sha256");
  }

  // ---------------------------------------------------------------- 200 回退
  {
    QTemporaryDir root;
    CacheStore cache(root.path());
    FakeTransport transport;
    transport.data = sample;
    transport.urls = {QStringLiteral("https://cdn1.example/v.mp4")};
    FakeBehavior behavior;
    behavior.ignore_range = true;
    transport.behaviors.insert(transport.urls.first(), behavior);
    const CacheKey key = MakeKey();
    const qint64 prefix = total / 2;
    SeedPart(&cache, key, sample, prefix);
    const JobOutcome outcome =
        RunJob(&cache, key, &transport, MakePlan(transport.urls, total));
    Check(outcome.state == DownloadState::kCompleted, "200 fallback: completed");
    Check(outcome.stats.range_ignored, "200 fallback: range_ignored");
    Check(outcome.stats.range_ignored_truncated,
          "200 fallback: truncated (no readers)");
    const QVector<DownloadRequest> requests = transport.requests();
    Check(requests.size() >= 2 && requests.at(0).offset == prefix &&
              requests.at(1).offset == 0,
          "200 fallback: resume offset then restart at 0");
    const QString mp4 = Mp4PathFor(&cache, key);
    CheckEqI64(QFileInfo(mp4).size(), total, "200 fallback: size not doubled");
    CheckEq(HashFile(mp4), sample_hash, "200 fallback: sha256");
  }

  // ---------------------------------------------------------------- 备份切换
  {
    QTemporaryDir root;
    CacheStore cache(root.path());
    FakeTransport transport;
    transport.data = sample;
    transport.urls = {QStringLiteral("https://cdn1.example/v.mp4"),
                      QStringLiteral("https://cdn2.example/v.mp4"),
                      QStringLiteral("https://cdn3.example/v.mp4")};
    FakeBehavior forbidden;
    forbidden.http_status = 403;
    transport.behaviors.insert(transport.urls.at(0), forbidden);
    const CacheKey key = MakeKey();
    const JobOutcome outcome =
        RunJob(&cache, key, &transport, MakePlan(transport.urls, total));
    Check(outcome.state == DownloadState::kCompleted, "backup: completed");
    CheckEqI64(outcome.stats.backup_switches, 1, "backup: one switch");
    CheckEqI64(outcome.stats.refreshes, 0, "backup: no refresh needed");
    const QVector<DownloadRequest> requests = transport.requests();
    Check(requests.size() >= 2 &&
              requests.at(1).url == transport.urls.at(1),
          "backup: second URL used");
    CheckEq(HashFile(Mp4PathFor(&cache, key)), sample_hash, "backup: sha256");
  }

  // ---------------------------------------------------------------- 刷新一次成功
  {
    QTemporaryDir root;
    CacheStore cache(root.path());
    FakeTransport transport;
    transport.data = sample;
    transport.urls = {QStringLiteral("https://cdn1.example/v.mp4"),
                      QStringLiteral("https://cdn2.example/v.mp4"),
                      QStringLiteral("https://cdn3.example/v.mp4")};
    for (const QString& url : transport.urls) {
      FakeBehavior forbidden;
      forbidden.http_status = 403;
      transport.behaviors.insert(url, forbidden);
    }
    transport.behaviors.insert(QStringLiteral("https://fresh.example/v.mp4"),
                               FakeBehavior());
    const CacheKey key = MakeKey();
    DownloadPlan plan = MakePlan(transport.urls, total);
    int refresh_calls = 0;
    const JobOutcome outcome = RunJob(
        &cache, key, &transport, plan,
        [&](DownloadPlan* fresh, const HttpCancelToken&, QString*) {
          ++refresh_calls;
          *fresh = MakePlan({QStringLiteral("https://fresh.example/v.mp4")},
                            total);
          return true;
        });
    Check(outcome.state == DownloadState::kCompleted, "refresh: completed");
    CheckEqI64(refresh_calls, 1, "refresh: called once");
    CheckEqI64(outcome.stats.refreshes, 1, "refresh: stats one");
    CheckEqI64(outcome.stats.backup_switches, 2, "refresh: tried 3 urls");
    CheckEq(HashFile(Mp4PathFor(&cache, key)), sample_hash, "refresh: sha256");
  }

  // ---------------------------------------------------------------- 刷新失败
  {
    QTemporaryDir root;
    CacheStore cache(root.path());
    FakeTransport transport;
    transport.data = sample;
    transport.urls = {QStringLiteral("https://cdn1.example/v.mp4"),
                      QStringLiteral("https://cdn2.example/v.mp4"),
                      QStringLiteral("https://cdn3.example/v.mp4")};
    for (const QString& url : transport.urls) {
      FakeBehavior forbidden;
      forbidden.http_status = 403;
      transport.behaviors.insert(url, forbidden);
    }
    const CacheKey key = MakeKey();
    const JobOutcome outcome = RunJob(
        &cache, key, &transport, MakePlan(transport.urls, total),
        [](DownloadPlan*, const HttpCancelToken&, QString* error) {
          *error = QStringLiteral("刷新取流失败（测试）");
          return false;
        });
    Check(outcome.state == DownloadState::kFailed, "refresh fail: failed");
    Check(outcome.stats.error.contains(QStringLiteral("刷新")),
          "refresh fail: readable error");
    Check(QFileInfo::exists(PartPathFor(&cache, key)),
          "refresh fail: .part retained");
    Check(!QFileInfo::exists(Mp4PathFor(&cache, key)),
          "refresh fail: no .mp4");
    Check(!outcome.stats.error.contains(QStringLiteral("http")),
          "refresh fail: no signed URL in error");
  }

  // ---------------------------------------------------------------- 总长不一致
  {
    QTemporaryDir root;
    CacheStore cache(root.path());
    FakeTransport transport;
    transport.data = sample;
    transport.urls = {QStringLiteral("https://cdn1.example/v.mp4")};
    FakeBehavior behavior;
    behavior.total_override = total + 1;
    transport.behaviors.insert(transport.urls.first(), behavior);
    const CacheKey key = MakeKey();
    const JobOutcome outcome =
        RunJob(&cache, key, &transport, MakePlan(transport.urls, total));
    Check(outcome.state == DownloadState::kFailed, "total mismatch: failed");
    Check(outcome.stats.error.contains(QStringLiteral("总大小")),
          "total mismatch: readable error");
    Check(QFileInfo::exists(PartPathFor(&cache, key)),
          "total mismatch: .part retained");
    Check(!QFileInfo::exists(Mp4PathFor(&cache, key)),
          "total mismatch: no .mp4");
    Check(outcome.stats.av_error != 0, "total mismatch: source failed");
  }

  // ---------------------------------------------------------------- 起点不一致
  {
    QTemporaryDir root;
    CacheStore cache(root.path());
    FakeTransport transport;
    transport.data = sample;
    transport.urls = {QStringLiteral("https://cdn1.example/v.mp4")};
    FakeBehavior behavior;
    behavior.start_mismatch = true;
    transport.behaviors.insert(transport.urls.first(), behavior);
    const CacheKey key = MakeKey();
    const qint64 prefix = total / 4;
    SeedPart(&cache, key, sample, prefix);
    const JobOutcome outcome =
        RunJob(&cache, key, &transport, MakePlan(transport.urls, total));
    Check(outcome.state == DownloadState::kFailed, "start mismatch: failed");
    Check(outcome.stats.error.contains(QStringLiteral("续传起点")),
          "start mismatch: readable error");
    Check(QFileInfo::exists(PartPathFor(&cache, key)),
          "start mismatch: .part retained");
  }

  // ---------------------------------------------------------------- 断连续传
  {
    QTemporaryDir root;
    CacheStore cache(root.path());
    FakeTransport transport;
    transport.data = sample;
    transport.urls = {QStringLiteral("https://cdn1.example/v.mp4")};
    FakeBehavior behavior;
    behavior.disconnects = 1;
    behavior.disconnect_after = total / 4;
    behavior.disconnect_delay_ms = 5000;
    transport.behaviors.insert(transport.urls.first(), behavior);
    const CacheKey key = MakeKey();
    QElapsedTimer timer;
    timer.start();
    const JobOutcome outcome =
        RunJob(&cache, key, &transport, MakePlan(transport.urls, total));
    Check(outcome.state == DownloadState::kCompleted,
          "disconnect: completed after resume");
    Check(timer.elapsed() >= 5000,
          "disconnect: 5s interruption observed");
    Check(outcome.stats.range_requests >= 2, "disconnect: retried range");
    const QVector<DownloadRequest> requests = transport.requests();
    Check(requests.size() >= 2 && requests.at(1).offset == total / 4,
          "disconnect: resume offset == bytes written");
    CheckEq(HashFile(Mp4PathFor(&cache, key)), sample_hash,
            "disconnect: sha256");
  }

  // ---------------------------------------------------------------- 取消 + 再续传
  {
    QTemporaryDir root;
    CacheStore cache(root.path());
    FakeTransport transport;
    transport.data = sample;
    transport.urls = {QStringLiteral("https://cdn1.example/v.mp4")};
    FakeBehavior behavior;
    behavior.block = true;
    transport.behaviors.insert(transport.urls.first(), behavior);
    const CacheKey key = MakeKey();
    auto source = std::make_shared<GrowingSource>(total);
    DownloadJob job(&cache, key, source, &transport, MakePlan(transport.urls, total));
    QString error;
    Check(job.Start(&error), "cancel: start");
    for (int i = 0; i < 100 && transport.requests().isEmpty(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    QElapsedTimer timer;
    timer.start();
    job.Cancel();
    const bool finished = job.Wait(2000);
    const qint64 cancel_ms = timer.elapsed();
    Check(finished, "cancel: worker finished");
    CheckLe(cancel_ms, 100, "cancel: <=100ms");
    Check(job.state() == DownloadState::kCancelled, "cancel: state cancelled");
    Check(source->IsCancelled(), "cancel: source cancelled");
    Check(QFileInfo::exists(PartPathFor(&cache, key)), "cancel: .part retained");
    Check(!QFileInfo::exists(Mp4PathFor(&cache, key)), "cancel: no .mp4");

    // 同一 .part 再开一个 Job：从当前长度续传并完成。
    FakeTransport second;
    second.data = sample;
    second.urls = transport.urls;
    const JobOutcome resumed =
        RunJob(&cache, key, &second, MakePlan(second.urls, total));
    Check(resumed.state == DownloadState::kCompleted,
          "cancel then resume: completed");
    CheckEq(HashFile(Mp4PathFor(&cache, key)), sample_hash,
            "cancel then resume: sha256");
  }

  // ---------------------------------------------------------------- 暂停/恢复
  {
    QTemporaryDir root;
    CacheStore cache(root.path());
    FakeTransport transport;
    transport.data = sample;
    transport.urls = {QStringLiteral("https://cdn1.example/v.mp4")};
    FakeBehavior behavior;
    behavior.chunk_delay_ms = 5;
    behavior.chunk_bytes = 64 * 1024;
    transport.behaviors.insert(transport.urls.first(), behavior);
    const CacheKey key = MakeKey();
    auto source = std::make_shared<GrowingSource>(total);
    DownloadJob job(&cache, key, source, &transport, MakePlan(transport.urls, total));
    QString error;
    Check(job.Start(&error), "pause: start");
    for (int i = 0; i < 300 && source->Published() < 128 * 1024; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    job.Pause();
    bool paused = false;
    for (int i = 0; i < 200; ++i) {
      if (job.state() == DownloadState::kPaused) {
        paused = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Check(paused, "pause: state paused");
    const qint64 published = source->Published();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CheckEqI64(source->Published(), published, "pause: no growth while paused");
    job.Resume();
    Check(job.Wait(60000), "pause: finished after resume");
    Check(job.state() == DownloadState::kCompleted, "pause: completed");
    CheckEq(HashFile(Mp4PathFor(&cache, key)), sample_hash, "pause: sha256");
  }

  std::printf("checks=%d failures=%d\n", g_checks, g_failures);
  std::fflush(stdout);
  return g_failures == 0 ? 0 : 1;
}
