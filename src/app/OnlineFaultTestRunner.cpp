#include "app/OnlineFaultTestRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>
#include <tuple>
#include <vector>

#include <windows.h>

#include <dbghelp.h>

#pragma comment(lib, "dbghelp.lib")

#include "app/MainWindow.h"
#include "online/CacheStore.h"
#include "online/DownloadJob.h"
#include "online/DownloadTransport.h"
#include "online/PlayUrl.h"

namespace lapian::app {
namespace {

using lapian::online::CacheEntry;
using lapian::online::CacheKey;
using lapian::online::CacheStore;
using lapian::online::DownloadJob;
using lapian::online::DownloadPlan;
using lapian::online::DownloadRequest;
using lapian::online::DownloadState;
using lapian::online::DownloadTransportResult;
using lapian::online::DownloadTransportStatus;
using lapian::online::DurlSegment;
using lapian::online::HttpCancelToken;
using lapian::online::IDownloadSink;
using lapian::online::IDownloadTransport;
using lapian::online::PlayUrlData;


QString OneLine(const QString& text) {
  QString one = text;
  one.replace(QLatin1Char('\n'), QStringLiteral(" | "));
  return one;
}

std::int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// ---------------------------------------------------------------- 故障脚本

struct AttemptBehavior {
  int http_status = 0;        // 非 0：直接返回该状态（403/404/500…）
  bool network_fail = false;  // 直接网络失败
  bool stall = false;         // 交付 bytes_limit 后连接静默（阻塞到取消）
  int stall_ms = 0;           // 交付 bytes_limit 后停顿 N 毫秒再继续（休眠模拟）
  qint64 bytes_limit = -1;    // 本次最多交付多少字节（-1 = 到文件末尾）
  qint64 chunk_bytes = 256 * 1024;
  int chunk_delay_ms = 0;
};

// 从本地媒体文件提供字节的假 CDN；按请求序号取脚本，最后一条重复。
class FaultTransport final : public IDownloadTransport {
 public:
  QByteArray data;
  QVector<AttemptBehavior> script;
  QString url;

  DownloadTransportResult GetRange(const DownloadRequest& request,
                                   IDownloadSink* sink,
                                   const HttpCancelToken& cancel) override {
    int attempt = 0;
    {
      QMutexLocker lock(&mutex_);
      requests_.append(request);
      ++request_count_;
      attempt = request_count_ - 1;
    }
    DownloadTransportResult result;
    AttemptBehavior behavior;
    if (!script.isEmpty()) {
      behavior = script.at(std::min<int>(attempt, script.size() - 1));
    }
    if (behavior.http_status != 0) {
      result.http_status = behavior.http_status;
      result.status = DownloadTransportStatus::kHttpStatus;
      result.error = QStringLiteral("HTTP %1").arg(behavior.http_status);
      return result;
    }
    if (behavior.network_fail) {
      result.status = DownloadTransportStatus::kNetwork;
      result.error = QStringLiteral("网络连接失败（注入）");
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
      result.content_range_total = data.size();
    } else {
      result.content_length = data.size();
    }
    qint64 limit = data.size();
    if (behavior.bytes_limit >= 0) {
      limit = std::min<qint64>(limit, request.offset + behavior.bytes_limit);
    }
    qint64 offset = request.offset;
    auto write_until = [&](qint64 end) -> bool {
      while (offset < end) {
        if (cancel.IsCancelled()) {
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
        offset += chunk;
        {
          QMutexLocker lock(&mutex_);
          sink_bytes_ += chunk;
        }
        if (behavior.chunk_delay_ms > 0) {
          std::this_thread::sleep_for(
              std::chrono::milliseconds(behavior.chunk_delay_ms));
        }
      }
      return true;
    };
    if (!write_until(limit)) {
      return result;
    }
    if (behavior.stall) {
      {
        QMutexLocker lock(&mutex_);
        stalled_ = true;
        if (last_stall_ms_ == 0) {
          last_stall_ms_ = NowMs();
        }
      }
      while (!cancel.IsCancelled()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      result.status = DownloadTransportStatus::kCancelled;
      result.cancelled = true;
      result.bytes_written = offset - request.offset;
      result.error = QStringLiteral("cancelled");
      return result;
    }
    if (behavior.stall_ms > 0) {
      const std::int64_t start = NowMs();
      {
        QMutexLocker lock(&mutex_);
        suspended_ms_ += behavior.stall_ms;
      }
      while (NowMs() - start < behavior.stall_ms) {
        if (cancel.IsCancelled()) {
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
  bool stalled() const {
    QMutexLocker lock(&mutex_);
    return stalled_;
  }
  std::int64_t last_stall_ms() const {
    QMutexLocker lock(&mutex_);
    return last_stall_ms_;
  }
  std::int64_t suspended_ms() const {
    QMutexLocker lock(&mutex_);
    return suspended_ms_;
  }
  std::int64_t sink_bytes() const {
    QMutexLocker lock(&mutex_);
    return sink_bytes_;
  }

 private:
  mutable QMutex mutex_;
  QVector<DownloadRequest> requests_;
  int request_count_ = 0;
  bool stalled_ = false;
  std::int64_t last_stall_ms_ = 0;
  std::int64_t suspended_ms_ = 0;
  std::int64_t sink_bytes_ = 0;
};

// MainWindow 持有 unique_ptr<IDownloadTransport>；测试保留共享指针做断言。
class FaultTransportRef final : public IDownloadTransport {
 public:
  explicit FaultTransportRef(std::shared_ptr<FaultTransport> inner)
      : inner_(std::move(inner)) {}
  DownloadTransportResult GetRange(const DownloadRequest& request,
                                   IDownloadSink* sink,
                                   const HttpCancelToken& cancel) override {
    return inner_->GetRange(request, sink, cancel);
  }

 private:
  std::shared_ptr<FaultTransport> inner_;
};

PlayUrlData MakeDurl(const QString& url, qint64 total, int qn = 64,
                     int codecid = 7) {
  PlayUrlData data;
  data.quality = qn;
  data.codecid = codecid;
  data.format = QStringLiteral("mp4720");
  data.timelength_ms = 8000;
  DurlSegment segment;
  segment.url = url;
  segment.size = total;
  segment.length_ms = 8000;
  data.durls.push_back(segment);
  return data;
}

QString FaultBvid(int index) {
  return QStringLiteral("BV1FAULT%1").arg(index, 4, 10, QLatin1Char('0'));
}

// 测试侧崩溃栈（只在探针进程里安装）：把未处理异常变成可审查的符号化栈，
// 而不是只有 WER 的 RVA。产品不安装这个过滤器。
LONG WINAPI FaultCrashFilter(EXCEPTION_POINTERS* info) {
  static LONG handled = 0;
  if (InterlockedExchange(&handled, 1) != 0) {
    return EXCEPTION_EXECUTE_HANDLER;
  }
  const DWORD code =
      info != nullptr && info->ExceptionRecord != nullptr
          ? info->ExceptionRecord->ExceptionCode
          : 0;
  void* fault = info != nullptr && info->ExceptionRecord != nullptr
                    ? info->ExceptionRecord->ExceptionAddress
                    : nullptr;
  std::printf("online_fault_crash code=0x%08lx addr=%p\n",
              static_cast<unsigned long>(code), fault);
  void* frames[48] = {};
  const USHORT count = CaptureStackBackTrace(0, 48, frames, nullptr);
  HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  SymInitialize(process, nullptr, TRUE);
  for (USHORT i = 0; i < count; ++i) {
    char buffer[sizeof(SYMBOL_INFO) + 256] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    DWORD64 displacement = 0;
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD line_displacement = 0;
    const bool has_line = SymGetLineFromAddr64(
        process, reinterpret_cast<DWORD64>(frames[i]), &line_displacement,
        &line);
    if (SymFromAddr(process, reinterpret_cast<DWORD64>(frames[i]),
                    &displacement, symbol)) {
      std::printf("online_fault_crash_frame %u %s+0x%llx %s:%lu\n", i,
                  symbol->Name,
                  static_cast<unsigned long long>(displacement),
                  has_line ? line.FileName : "",
                  has_line ? line.LineNumber : 0);
    } else {
      std::printf("online_fault_crash_frame %u addr=%p\n", i, frames[i]);
    }
    std::fflush(stdout);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

QByteArray LoadMedia(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

// 缓存损坏种子：用真实 DownloadJob 填完整缓存，再截断 data.mp4。
bool SeedCorruptCache(const QString& root, const QString& media_path,
                      const CacheKey& key, QString* error) {
  QByteArray data = LoadMedia(media_path);
  if (data.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("素材不可读：%1").arg(media_path);
    }
    return false;
  }
  CacheStore cache(root);
  auto source = std::make_shared<lapian::online::GrowingSource>(data.size());
  FaultTransport transport;
  transport.data = data;
  transport.url = QStringLiteral("https://cdn.fault.test/seed.mp4");
  DownloadPlan plan;
  plan.urls = {transport.url};
  plan.total_bytes = data.size();
  plan.qn = key.qn;
  plan.codecid = key.codecid;
  plan.format = QStringLiteral("mp4720");
  plan.metadata.duration_ms = 8000;
  DownloadJob job(&cache, key, source, &transport, plan, {}, {});
  QString start_error;
  if (!job.Start(&start_error)) {
    if (error != nullptr) {
      *error = start_error;
    }
    return false;
  }
  if (!job.Wait(30000) || job.state() != DownloadState::kCompleted) {
    if (error != nullptr) {
      *error = job.stats().error;
    }
    return false;
  }
  const QString mp4 = cache.Mp4Path(key);
  QFile file(mp4);
  if (!file.open(QIODevice::ReadWrite)) {
    if (error != nullptr) {
      *error = QStringLiteral("无法打开种子缓存：%1").arg(mp4);
    }
    return false;
  }
  file.resize(data.size() / 3);
  file.close();
  const auto broken = cache.Load(key, error);
  if (!broken.has_value() || !broken->broken) {
    if (error != nullptr) {
      *error = QStringLiteral("截断后未检测为损坏");
    }
    return false;
  }
  return true;
}

}  // namespace

OnlineFaultTestRunner::OnlineFaultTestRunner(MainWindow* window,
                                             OnlineFaultTestOptions options)
    : window_(window), options_(std::move(options)) {}

void OnlineFaultTestRunner::Start() {
  QTimer::singleShot(0, window_, [this]() { Run(); });
}

void OnlineFaultTestRunner::PumpFor(int ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
}

bool OnlineFaultTestRunner::PumpUntil(const std::function<bool()>& predicate,
                                      int timeout_ms, const char* label) {
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
  const bool ok = predicate();
  if (!ok) {
    std::printf("online_fault_wait_timeout label=%s ms=%lld\n", label,
                static_cast<long long>(timer.elapsed()));
    std::fflush(stdout);
  }
  return ok;
}

void OnlineFaultTestRunner::Check(const QString& name, bool ok,
                                  const QString& detail) {
  ++checks_;
  ++scenario_checks_;
  if (!ok) {
    ++failures_;
    ++scenario_failures_;
  }
  std::printf("online_fault_check %s ok=%d%s%s\n", name.toUtf8().constData(),
              ok ? 1 : 0, detail.isEmpty() ? "" : " detail=",
              OneLine(detail).toUtf8().constData());
  std::fflush(stdout);
}

bool OnlineFaultTestRunner::RunScenario(const QString& name,
                                        const std::function<void()>& body) {
  if (!options_.only.isEmpty() && options_.only != name) {
    return true;
  }
  scenario_name_ = name;
  scenario_checks_ = 0;
  scenario_failures_ = 0;
  // 每个场景从干净会话开始；缓存/记录根保留（跨场景验证“标记不丢”）。
  if (window_->OnlineModeForTest() || window_->HasMediaForTest()) {
    window_->ShutdownOnlineForTest();
  }
  window_->ClearOnlineTestPlayUrlForTest();
  window_->SetOnlineDashEnabledForTest(false);
  window_->SetOnlineQualityCatalogEnabledForTest(false);
  PumpFor(30);

  const auto start = std::chrono::steady_clock::now();
  body();
  const qint64 elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
  ++scenarios_;
  const bool ok = scenario_failures_ == 0;
  if (ok) {
    ++scenario_pass_;
  } else {
    ++scenario_fail_;
  }
  std::printf(
      "online_fault_scenario name=%s status=%s checks=%d failures=%d "
      "elapsed_ms=%lld\n",
      name.toUtf8().constData(), ok ? "pass" : "fail", scenario_checks_,
      scenario_failures_, static_cast<long long>(elapsed));
  std::fflush(stdout);
  return ok;
}

QString OnlineFaultTestRunner::CurrentBvid() const { return options_.bvid; }

void OnlineFaultTestRunner::Run() {
  const QString media_path =
      options_.media_path.isEmpty()
          ? (QDir::currentPath() +
             QStringLiteral("/build/test-media/faststart-720p.mp4"))
          : options_.media_path;
  QByteArray media = LoadMedia(media_path);
  if (media.isEmpty() && options_.media_path.isEmpty()) {
    const QString fallback =
        QDir::currentPath() + QStringLiteral("/media/proxy/proxy-V1.mp4");
    media = LoadMedia(fallback);
  }
  if (media.isEmpty()) {
    std::printf(
        "online_fault_fail reason=media_missing path=%s usage=--fault-media "
        "<mp4>\n",
        media_path.toUtf8().constData());
    std::fflush(stdout);
    QCoreApplication::exit(2);
    return;
  }
  SetUnhandledExceptionFilter(FaultCrashFilter);
  std::printf(
      "T8.13 online_fault begin tag=%s media=%s bytes=%lld cache_root=%s "
      "records_root=%s\n",
      options_.tag.toUtf8().constData(), media_path.toUtf8().constData(),
      static_cast<long long>(media.size()), options_.cache_root.toUtf8().constData(),
      options_.records_root.toUtf8().constData());
  std::fflush(stdout);

  window_->raise();
  window_->activateWindow();
  window_->FocusVideoForTest();

  // UI 心跳：16ms；>200ms 视为 UI 停顿（O7-A9）。
  QElapsedTimer heartbeat_clock;
  heartbeat_clock.start();
  qint64 heartbeat_last = 0;
  qint64 heartbeat_max = 0;
  int heartbeat_over_200 = 0;
  // 测试侧冻结看门狗：UI 线程 >30s 没有心跳（通常意味着 UI 线程被阻塞）时
  // 直接以 exit=3 结束，留下最后一条 check/scenario 标签，而不是让父进程超时。
  std::atomic<std::int64_t> ui_tick{0};
  std::atomic<bool> watchdog_stop{false};
  std::thread hang_watchdog([&]() {
    std::int64_t last_tick = -1;
    std::int64_t last_change = NowMs();
    while (!watchdog_stop.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      const std::int64_t tick = ui_tick.load();
      if (tick != last_tick) {
        last_tick = tick;
        last_change = NowMs();
      } else if (NowMs() - last_change > 30000) {
        std::printf(
            "online_fault_hang scenario=%s last_checks=%d last_failures=%d "
            "ui_tick=%lld\n",
            scenario_name_.toUtf8().constData(), scenario_checks_,
            scenario_failures_, static_cast<long long>(tick));
        std::fflush(stdout);
        std::_Exit(3);
      }
    }
  });
  QTimer heartbeat;
  heartbeat.setInterval(16);
  heartbeat.setTimerType(Qt::PreciseTimer);
  QObject::connect(&heartbeat, &QTimer::timeout, [&]() {
    ui_tick.store(ui_tick.load() + 1);
    const qint64 now = heartbeat_clock.elapsed();
    if (heartbeat_last > 0) {
      const qint64 gap = now - heartbeat_last;
      if (gap > 200) {
        ++heartbeat_over_200;
        std::printf("online_fault_ui_gap_event gap_ms=%lld at_ms=%lld\n",
                    static_cast<long long>(gap), static_cast<long long>(now));
        std::fflush(stdout);
      }
      heartbeat_max = std::max(heartbeat_max, gap);
    }
    heartbeat_last = now;
  });
  heartbeat.start();

  const qint64 total = media.size();
  int bvid_counter = 0;

  // 每场景一个合法 bvid/cid，避免缓存/记录互相污染。
  auto begin_session = [&](int index, const QString& url,
                           const QVector<AttemptBehavior>& script,
                           qint64 ready_threshold = 0,
                           const QString& sessdata = QString()) {
    const QString bvid = FaultBvid(index);
    auto transport = std::make_shared<FaultTransport>();
    transport->data = media;
    transport->url = url;
    transport->script = script;
    if (ready_threshold > 0) {
      window_->SetOnlineReadyThresholdForTest(ready_threshold);
    }
    window_->SetOnlineTransportForTest(
        std::make_unique<FaultTransportRef>(transport));
    window_->SetOnlinePlayUrlForTest(MakeDurl(url, total));
    OnlineRequest request;
    request.bvid = bvid;
    request.cid = options_.cid > 0 ? options_.cid : 42915922022LL;
    request.page = options_.page;
    request.title = QStringLiteral("T8.13 故障注入");
    request.owner = QStringLiteral("test");
    request.duration_ms = 8000;
    request.sessdata = sessdata;
    QElapsedTimer timer;
    timer.start();
    const bool started = window_->StartOnlineSession(request);
    return std::make_tuple(started, transport, timer);
  };

  auto wait_ready = [&](int timeout_ms) {
    return PumpUntil([&]() { return window_->OnlineReadyForTest(); }, timeout_ms,
                     "ready");
  };

  // ---------------------------------------------------------------- S1 断网重试/续传
  RunScenario(QStringLiteral("disconnect_retry"), [&]() {
    const QString url = QStringLiteral("https://cdn.fault.test/s1.mp4");
    QVector<AttemptBehavior> script(2);
    script[0].bytes_limit = 400 * 1024;  // 交付 400KB 后断连
    auto [started, transport, timer] = begin_session(++bvid_counter, url, script);
    Check(QStringLiteral("started"), started);
    Check(QStringLiteral("ready"), wait_ready(10000),
          window_->OnlineLastErrorForTest());
    Check(QStringLiteral("no_error"), window_->OnlineLastErrorForTest().isEmpty(),
          window_->OnlineLastErrorForTest());
    // 下载重试后继续（第二/三次请求 offset > 0）。
    const QVector<DownloadRequest> requests = transport->requests();
    Check(QStringLiteral("retried"), requests.size() >= 2,
          QStringLiteral("requests=%1").arg(requests.size()));
    Check(QStringLiteral("resume_offset"),
          requests.size() >= 2 && requests.at(1).offset >= 400 * 1024,
          QStringLiteral("offset=%1")
              .arg(requests.size() >= 2 ? requests.at(1).offset : -1));
    Check(QStringLiteral("ready_le_5s"), timer.elapsed() <= 5000,
          QStringLiteral("ms=%1").arg(timer.elapsed()));
    // 已缓存区可播放：跳帧落点 + 播放推进。
    window_->GoToFrameForTest(2);
    const bool landed = PumpUntil(
        [&]() {
          return window_->OnlineSeekIdleForTest() &&
                 window_->CurrentFrameForTest() == 2;
        },
        5000, "seek2");
    Check(QStringLiteral("cached_seek_landed"), landed,
          QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
    window_->AddMarkForTest();
    Check(QStringLiteral("mark_added"), window_->MarkCountForTest() == 1,
          QStringLiteral("count=%1").arg(window_->MarkCountForTest()));
    window_->StartPlaybackForTest();
    PumpFor(400);
    window_->PausePlaybackForTest();
    Check(QStringLiteral("play_advanced"), window_->CurrentFrameForTest() > 2,
          QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
    QElapsedTimer close_timer;
    close_timer.start();
    window_->ShutdownOnlineForTest();
    Check(QStringLiteral("close_le_1s"), close_timer.elapsed() <= 1000,
          QStringLiteral("ms=%1").arg(close_timer.elapsed()));
    Check(QStringLiteral("closed"), !window_->OnlineModeForTest());
  });

  // ---------------------------------------------------------------- S2 下载中断网：已缓存可播
  RunScenario(QStringLiteral("dead_cached_playable"), [&]() {
    const QString url = QStringLiteral("https://cdn.fault.test/s2.mp4");
    QVector<AttemptBehavior> script(1);
    script[0].bytes_limit = 500 * 1024;
    script[0].stall = true;  // 交付后连接静默
    auto [started, transport, timer] = begin_session(++bvid_counter, url, script);
    Check(QStringLiteral("started"), started);
    Check(QStringLiteral("ready"), wait_ready(10000),
          window_->OnlineLastErrorForTest());
    const qint64 ready_ms = timer.elapsed();
    window_->AddMarkForTest();
    Check(QStringLiteral("mark_before_failure"), window_->MarkCountForTest() == 1);
    const bool error_arrived = PumpUntil(
        [&]() { return !window_->OnlineLastErrorForTest().isEmpty(); }, 8000,
        "error");
    const qint64 error_ms = timer.elapsed();
    Check(QStringLiteral("error_arrived"), error_arrived);
    Check(QStringLiteral("error_le_5s"),
          ready_ms <= 5000 && error_ms <= 5000,
          QStringLiteral("ready=%1 error=%2").arg(ready_ms).arg(error_ms));
    Check(QStringLiteral("error_readable"),
          window_->OnlineLastErrorForTest().contains(QStringLiteral("网络")),
          window_->OnlineLastErrorForTest());
    Check(QStringLiteral("error_status_bar"),
          window_->StatusMessageForTest().contains(QStringLiteral("在线错误")) ||
              window_->StatusMessageForTest().contains(QStringLiteral("网络")),
          window_->StatusMessageForTest());
    Check(QStringLiteral("mark_survives_failure"),
          window_->MarkCountForTest() == 1,
          QStringLiteral("count=%1").arg(window_->MarkCountForTest()));
    // 已缓存区仍可播放：跳到已缓存帧并验证 PTS（部分索引未就绪时按 fps 推算）。
    const std::int64_t target = 2;
    qint64 expected_pts = window_->OnlineFramePtsForTest(target);
    if (expected_pts < 0) {
      const double fps = window_->FrameRateForTest();
      if (fps > 0.0) {
        expected_pts =
            static_cast<qint64>(std::llround(static_cast<double>(target) *
                                             1.0e6 / fps));
      }
    }
    window_->GoToFrameForTest(target);
    const bool landed = PumpUntil(
        [&]() {
          return window_->OnlineSeekIdleForTest() &&
                 window_->CurrentFrameForTest() == target;
        },
        5000, "cached_seek_after_error");
    Check(QStringLiteral("cached_playable_after_error"), landed,
          QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
    if (landed) {
      Check(QStringLiteral("cached_pts_exact"),
            expected_pts >= 0 &&
                std::llabs(window_->CurrentPtsUsForTest() - expected_pts) <= 1000,
            QStringLiteral("expected=%1 actual=%2")
                .arg(expected_pts)
                .arg(window_->CurrentPtsUsForTest()));
    }
    QElapsedTimer close_timer;
    close_timer.start();
    window_->ShutdownOnlineForTest();
    Check(QStringLiteral("close_le_1s"), close_timer.elapsed() <= 1000,
          QStringLiteral("ms=%1").arg(close_timer.elapsed()));
    Check(QStringLiteral("transport_stalled"), transport->stalled());
  });

  // ---------------------------------------------------------------- S3 未缓存 seek 断网
  RunScenario(QStringLiteral("uncached_seek_offline"), [&]() {
    const QString url = QStringLiteral("https://cdn.fault.test/s3.mp4");
    QVector<AttemptBehavior> script(1);
    script[0].bytes_limit = 1200 * 1024;
    script[0].stall = true;
    auto [started, transport, timer] = begin_session(++bvid_counter, url, script);
    Check(QStringLiteral("started"), started);
    Check(QStringLiteral("ready"), wait_ready(10000),
          window_->OnlineLastErrorForTest());
    window_->AddMarkForTest();
    const bool indexed = window_->WaitForOnlineIndexFramesForTest(30, 8000);
    Check(QStringLiteral("index_ready"), indexed,
          QStringLiteral("frames=%1").arg(window_->OnlineIndexFramesForTest()));
    // 取公认未缓存的目标：接近片尾（8s@30 → ~240 帧），远超 1.2MB 已发布前缀。
    const std::int64_t last_frame = window_->OnlineLastFrameForTest();
    const qint64 target =
        std::max<qint64>(30, last_frame > 0 ? std::min<qint64>(200, last_frame - 2)
                                            : 200);
    QElapsedTimer seek_timer;
    seek_timer.start();
    window_->GoToFrameForTest(target);
    PumpFor(60);
    const QString prompt = window_->StatusMessageForTest();
    const bool prompt_fast =
        prompt.contains(QStringLiteral("缓冲中")) ||
        window_->IsOnlineBufferingForTest();
    Check(QStringLiteral("buffering_prompt_le_100ms"), prompt_fast,
          QStringLiteral("ms=%1 text=%2")
              .arg(seek_timer.elapsed())
              .arg(prompt));
    // 断网时 seek 侧可能先给“后果文案”（到达文件末尾），随后被会话的网络
    // 错误覆盖；这里等“网络原因”出现，并单独记录首条错误。
    const qint64 seek_fail_ms = timer.elapsed();
    const QString first_error = window_->OnlineLastErrorForTest();
    const bool error_arrived = PumpUntil(
        [&]() {
          return window_->OnlineLastErrorForTest().contains(
              QStringLiteral("网络"));
        },
        8000, "seek_error");
    const qint64 error_ms = timer.elapsed();
    Check(QStringLiteral("seek_error_arrived"), error_arrived,
          QStringLiteral("first=%1 seek_fail_ms=%2")
              .arg(first_error.isEmpty() ? QStringLiteral("<none>") : first_error)
              .arg(seek_fail_ms));
    Check(QStringLiteral("seek_error_le_5s"), error_ms <= 5000,
          QStringLiteral("ms=%1").arg(error_ms));
    Check(QStringLiteral("seek_error_readable"),
          window_->OnlineLastErrorForTest().contains(QStringLiteral("网络")),
          window_->OnlineLastErrorForTest());
    std::printf("online_fault_seek_error first=%s final=%s seek_fail_ms=%lld "
                "network_ms=%lld\n",
                OneLine(first_error.isEmpty() ? QStringLiteral("<none>")
                                             : first_error)
                    .toUtf8()
                    .constData(),
                OneLine(window_->OnlineLastErrorForTest()).toUtf8().constData(),
                static_cast<long long>(seek_fail_ms),
                static_cast<long long>(error_ms));
    std::fflush(stdout);
    window_->PausePlaybackForTest();
    // 已缓存区仍可播。
    window_->GoToFrameForTest(1);
    const bool cached_ok = PumpUntil(
        [&]() {
          return window_->OnlineSeekIdleForTest() &&
                 window_->CurrentFrameForTest() == 1;
        },
        5000, "cached_seek_after_seek_error");
    Check(QStringLiteral("cached_still_playable"), cached_ok,
          QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
    QElapsedTimer close_timer;
    close_timer.start();
    window_->ShutdownOnlineForTest();
    Check(QStringLiteral("close_le_1s"), close_timer.elapsed() <= 1000,
          QStringLiteral("ms=%1").arg(close_timer.elapsed()));

    // 恢复网络后重开：从 .part 续传 + 标记仍在 + 目标帧可落地。
    const QString bvid = FaultBvid(bvid_counter);
    auto healthy = std::make_shared<FaultTransport>();
    healthy->data = media;
    healthy->url = url;
    window_->SetOnlineTransportForTest(
        std::make_unique<FaultTransportRef>(healthy));
    window_->SetOnlinePlayUrlForTest(MakeDurl(url, total));
    OnlineRequest request;
    request.bvid = bvid;
    request.cid = options_.cid > 0 ? options_.cid : 42915922022LL;
    request.page = options_.page;
    request.title = QStringLiteral("T8.13 故障注入");
    request.duration_ms = 8000;
    Check(QStringLiteral("reopen_started"), window_->StartOnlineSession(request));
    Check(QStringLiteral("reopen_ready"), wait_ready(15000),
          window_->OnlineLastErrorForTest());
    const QVector<DownloadRequest> requests = healthy->requests();
    Check(QStringLiteral("reopen_resumed"),
          !requests.isEmpty() && requests.first().offset > 0,
          QStringLiteral("offset=%1")
              .arg(requests.isEmpty() ? -1 : requests.first().offset));
    Check(QStringLiteral("mark_survives_reopen"), window_->MarkCountForTest() == 1,
          QStringLiteral("count=%1").arg(window_->MarkCountForTest()));
    window_->GoToFrameForTest(target);
    const bool target_ok = PumpUntil(
        [&]() {
          return window_->OnlineSeekIdleForTest() &&
                 window_->CurrentFrameForTest() == target;
        },
        15000, "reopen_target_seek");
    Check(QStringLiteral("reopen_target_landed"), target_ok,
          QStringLiteral("frame=%1 target=%2")
              .arg(window_->CurrentFrameForTest())
              .arg(target));
    QElapsedTimer close2;
    close2.start();
    window_->ShutdownOnlineForTest();
    Check(QStringLiteral("reopen_close_le_1s"), close2.elapsed() <= 1000,
          QStringLiteral("ms=%1").arg(close2.elapsed()));
  });

  // ---------------------------------------------------------------- S4 网络无响应看门狗
  RunScenario(QStringLiteral("stall_watchdog"), [&]() {
    const QString url = QStringLiteral("https://cdn.fault.test/s4.mp4");
    QVector<AttemptBehavior> script(1);
    script[0].bytes_limit = 500 * 1024;
    script[0].stall = true;
    auto [started, transport, timer] = begin_session(++bvid_counter, url, script);
    Check(QStringLiteral("started"), started);
    Check(QStringLiteral("ready"), wait_ready(10000),
          window_->OnlineLastErrorForTest());
    const qint64 ready_ms = timer.elapsed();
    const bool error_arrived = PumpUntil(
        [&]() { return !window_->OnlineLastErrorForTest().isEmpty(); }, 8000,
        "watchdog_error");
    const qint64 error_ms = timer.elapsed();
    Check(QStringLiteral("watchdog_fired"), error_arrived);
    Check(QStringLiteral("watchdog_le_5s"),
          ready_ms <= 5000 && error_ms <= 5000,
          QStringLiteral("ready=%1 error=%2").arg(ready_ms).arg(error_ms));
    Check(QStringLiteral("watchdog_message"),
          window_->OnlineLastErrorForTest().contains(
              QStringLiteral("网络无响应")),
          window_->OnlineLastErrorForTest());
    std::printf(
        "online_fault_watchdog ready_ms=%lld error_ms=%lld stall_age_ms=%lld\n",
        static_cast<long long>(ready_ms), static_cast<long long>(error_ms),
        static_cast<long long>(NowMs() - transport->last_stall_ms()));
    std::fflush(stdout);
    QElapsedTimer close_timer;
    close_timer.start();
    window_->ShutdownOnlineForTest();
    Check(QStringLiteral("close_le_1s"), close_timer.elapsed() <= 1000,
          QStringLiteral("ms=%1").arg(close_timer.elapsed()));
  });

  // ---------------------------------------------------------------- S5 磁盘不足
  RunScenario(QStringLiteral("disk_full"), [&]() {
    const QString url = QStringLiteral("https://cdn.fault.test/s5.mp4");
    const qint64 huge = 2LL * 1024 * 1024 * 1024 * 1024 * 1024;  // 2 PB
    auto transport = std::make_shared<FaultTransport>();
    transport->data = media;
    transport->url = url;
    window_->SetOnlineTransportForTest(
        std::make_unique<FaultTransportRef>(transport));
    window_->SetOnlinePlayUrlForTest(MakeDurl(url, huge));
    OnlineRequest request;
    request.bvid = FaultBvid(++bvid_counter);
    request.cid = options_.cid > 0 ? options_.cid : 42915922022LL;
    request.page = options_.page;
    request.title = QStringLiteral("T8.13 磁盘不足");
    request.duration_ms = 8000;
    QElapsedTimer timer;
    timer.start();
    const bool started = window_->StartOnlineSession(request);
    const bool error_arrived = PumpUntil(
        [&]() { return !window_->OnlineLastErrorForTest().isEmpty(); }, 5000,
        "disk_full_error");
    Check(QStringLiteral("error_arrived"), error_arrived,
          QStringLiteral("started=%1").arg(started ? 1 : 0));
    Check(QStringLiteral("error_le_5s"), timer.elapsed() <= 5000,
          QStringLiteral("ms=%1").arg(timer.elapsed()));
    const QString error = window_->OnlineLastErrorForTest();
    Check(QStringLiteral("error_readable"),
          error.contains(QStringLiteral("空间")) ||
              error.contains(QStringLiteral("磁盘")) ||
              error.contains(QStringLiteral("不足")),
          error);
    Check(QStringLiteral("not_ready"), !window_->OnlineReadyForTest());
    QElapsedTimer close_timer;
    close_timer.start();
    window_->ShutdownOnlineForTest();
    Check(QStringLiteral("close_le_1s"), close_timer.elapsed() <= 1000,
          QStringLiteral("ms=%1").arg(close_timer.elapsed()));
  });

  // ---------------------------------------------------------------- S6 缓存损坏自动重建
  RunScenario(QStringLiteral("cache_corrupt_rebuild"), [&]() {
    const QString url = QStringLiteral("https://cdn.fault.test/s6.mp4");
    const QString bvid = FaultBvid(++bvid_counter);
    const qint64 cid = options_.cid > 0 ? options_.cid : 42915922022LL;
    CacheKey key;
    key.bvid = bvid;
    key.cid = cid;
    key.qn = 64;
    key.codecid = 7;
    QString seed_error;
    const bool seeded =
        SeedCorruptCache(options_.cache_root, media_path, key, &seed_error);
    Check(QStringLiteral("corrupt_seeded"), seeded, seed_error);
    if (seeded) {
      // 让 MainWindow 重新打开同一缓存根并重新扫描（检测损坏）。
      window_->SetOnlineCacheRootForTest(options_.cache_root);
    }
    auto transport = std::make_shared<FaultTransport>();
    transport->data = media;
    transport->url = url;
    window_->SetOnlineTransportForTest(
        std::make_unique<FaultTransportRef>(transport));
    window_->SetOnlinePlayUrlForTest(MakeDurl(url, total));
    OnlineRequest request;
    request.bvid = bvid;
    request.cid = cid;
    request.page = options_.page;
    request.title = QStringLiteral("T8.13 缓存损坏");
    request.duration_ms = 8000;
    QElapsedTimer timer;
    timer.start();
    Check(QStringLiteral("started"), window_->StartOnlineSession(request));
    Check(QStringLiteral("ready_after_rebuild"), wait_ready(15000),
          window_->OnlineLastErrorForTest());
    Check(QStringLiteral("not_served_as_hit"), !window_->OnlineCacheHitForTest());
    Check(QStringLiteral("no_fatal_error"),
          window_->OnlineLastErrorForTest().isEmpty(),
          window_->OnlineLastErrorForTest());
    Check(QStringLiteral("downloaded_from_zero"),
          transport->requests().size() >= 1 &&
              transport->requests().first().offset == 0);
    Check(QStringLiteral("ready_le_5s"), timer.elapsed() <= 5000,
          QStringLiteral("ms=%1").arg(timer.elapsed()));
    window_->AddMarkForTest();
    Check(QStringLiteral("mark_added"), window_->MarkCountForTest() == 1);
    const QString record_path = window_->OnlineRecordPathForTest();
    Check(QStringLiteral("record_written"),
          !record_path.isEmpty() && QFileInfo::exists(record_path), record_path);
    const qint64 record_size =
        record_path.isEmpty() ? 0 : QFileInfo(record_path).size();
    QElapsedTimer close_timer;
    close_timer.start();
    window_->ShutdownOnlineForTest();
    Check(QStringLiteral("close_le_1s"), close_timer.elapsed() <= 1000,
          QStringLiteral("ms=%1").arg(close_timer.elapsed()));

    // 重建完成后重开：缓存命中 0 网络，标记/记录仍在（不被重建删除）。
    auto hit_transport = std::make_shared<FaultTransport>();
    hit_transport->data = media;
    hit_transport->url = url;
    window_->SetOnlineTransportForTest(
        std::make_unique<FaultTransportRef>(hit_transport));
    window_->SetOnlinePlayUrlForTest(MakeDurl(url, total));
    QElapsedTimer hit_timer;
    hit_timer.start();
    Check(QStringLiteral("reopen_started"), window_->StartOnlineSession(request));
    Check(QStringLiteral("reopen_cache_hit_ready"), wait_ready(8000),
          window_->OnlineLastErrorForTest());
    Check(QStringLiteral("reopen_is_cache_hit"), window_->OnlineCacheHitForTest());
    Check(QStringLiteral("reopen_zero_download"),
          hit_transport->requests().isEmpty(),
          QStringLiteral("requests=%1").arg(hit_transport->requests().size()));
    Check(QStringLiteral("reopen_le_2500ms"), hit_timer.elapsed() <= 2500,
          QStringLiteral("ms=%1").arg(hit_timer.elapsed()));
    Check(QStringLiteral("mark_after_reopen"), window_->MarkCountForTest() == 1,
          QStringLiteral("count=%1").arg(window_->MarkCountForTest()));
    Check(QStringLiteral("record_size_preserved"),
          !record_path.isEmpty() && QFileInfo(record_path).size() >= record_size,
          QStringLiteral("before=%1 after=%2")
              .arg(record_size)
              .arg(QFileInfo(record_path).size()));
    window_->ShutdownOnlineForTest();
  });

  // ---------------------------------------------------------------- S7 下载中关闭
  RunScenario(QStringLiteral("close_during_download"), [&]() {
    const QString url = QStringLiteral("https://cdn.fault.test/s7.mp4");
    QVector<AttemptBehavior> script(1);
    script[0].stall = true;  // 一个字节都没给就静默
    auto [started, transport, timer] = begin_session(++bvid_counter, url, script);
    Check(QStringLiteral("started"), started);
    const bool stuck = PumpUntil(
        [&]() { return transport->stalled(); }, 5000, "download_stuck");
    Check(QStringLiteral("download_stuck"), stuck);
    QElapsedTimer close_timer;
    close_timer.start();
    window_->ShutdownOnlineForTest();
    const qint64 close_ms = close_timer.elapsed();
    Check(QStringLiteral("close_le_1s"), close_ms <= 1000,
          QStringLiteral("ms=%1").arg(close_ms));
    Check(QStringLiteral("closed"), !window_->OnlineModeForTest());
    std::printf("online_fault_close during=download close_ms=%lld\n",
                static_cast<long long>(close_ms));
    std::fflush(stdout);
  });

  // ---------------------------------------------------------------- S8 未缓存 seek 中关闭
  RunScenario(QStringLiteral("close_during_uncached_seek"), [&]() {
    const QString url = QStringLiteral("https://cdn.fault.test/s8.mp4");
    QVector<AttemptBehavior> script(1);
    script[0].bytes_limit = 1200 * 1024;
    script[0].stall = true;
    auto [started, transport, timer] = begin_session(++bvid_counter, url, script);
    Check(QStringLiteral("started"), started);
    Check(QStringLiteral("ready"), wait_ready(10000),
          window_->OnlineLastErrorForTest());
    const bool indexed = window_->WaitForOnlineIndexFramesForTest(30, 8000);
    Check(QStringLiteral("index_ready"), indexed,
          QStringLiteral("frames=%1").arg(window_->OnlineIndexFramesForTest()));
    const std::int64_t last_frame = window_->OnlineLastFrameForTest();
    const qint64 target =
        std::max<qint64>(30, last_frame > 0 ? std::min<qint64>(200, last_frame - 2)
                                            : 200);
    window_->GoToFrameForTest(target);
    PumpFor(50);
    QElapsedTimer close_timer;
    close_timer.start();
    window_->ShutdownOnlineForTest();
    const qint64 close_ms = close_timer.elapsed();
    Check(QStringLiteral("close_le_1s"), close_ms <= 1000,
          QStringLiteral("ms=%1").arg(close_ms));
    Check(QStringLiteral("closed"), !window_->OnlineModeForTest());
    std::printf("online_fault_close during=uncached_seek close_ms=%lld\n",
                static_cast<long long>(close_ms));
    std::fflush(stdout);
  });

  // ---------------------------------------------------------------- S9 休眠模拟
  RunScenario(QStringLiteral("suspend_resume"), [&]() {
    const QString url = QStringLiteral("https://cdn.fault.test/s9.mp4");
    QVector<AttemptBehavior> script(1);
    script[0].bytes_limit = 600 * 1024;
    script[0].stall_ms = 3000;  // 交付 600KB 后停 3s（模拟系统挂起/线程冻结）
    auto [started, transport, timer] = begin_session(++bvid_counter, url, script);
    Check(QStringLiteral("started"), started);
    Check(QStringLiteral("ready"), wait_ready(10000),
          window_->OnlineLastErrorForTest());
    const bool complete = window_->WaitForOnlineCacheCompleteForTest(20000);
    Check(QStringLiteral("download_completed_after_suspend"), complete,
          window_->OnlineProgressForTest());
    Check(QStringLiteral("suspend_injected"), transport->suspended_ms() >= 3000,
          QStringLiteral("suspended_ms=%1").arg(transport->suspended_ms()));
    const QVector<DownloadRequest> requests = transport->requests();
    Check(QStringLiteral("single_connection_resumed"),
          requests.size() == 1 && requests.first().offset == 0,
          QStringLiteral("requests=%1 first_offset=%2")
              .arg(requests.size())
              .arg(requests.isEmpty() ? -1 : requests.first().offset));
    Check(QStringLiteral("no_duplicate_bytes"), transport->sink_bytes() == total,
          QStringLiteral("sink=%1 total=%2")
              .arg(transport->sink_bytes())
              .arg(total));
    Check(QStringLiteral("no_error"), window_->OnlineLastErrorForTest().isEmpty(),
          window_->OnlineLastErrorForTest());
    window_->GoToFrameForTest(10);
    const bool landed = PumpUntil(
        [&]() {
          return window_->OnlineSeekIdleForTest() &&
                 window_->CurrentFrameForTest() == 10;
        },
        5000, "suspend_seek");
    Check(QStringLiteral("seek_after_resume"), landed,
          QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
    QElapsedTimer close_timer;
    close_timer.start();
    window_->ShutdownOnlineForTest();
    Check(QStringLiteral("close_le_1s"), close_timer.elapsed() <= 1000,
          QStringLiteral("ms=%1").arg(close_timer.elapsed()));
  });

  // ---------------------------------------------------------------- S10 脱敏
  RunScenario(QStringLiteral("redaction"), [&]() {
    const QString sentinel_sessdata =
        QStringLiteral("SENTINEL_SESSDATA_8c1d4a");
    const QString url =
        QStringLiteral("https://cdn.fault.test/s10.mp4?deadline=1700000000&"
                       "upsig=SENTINELSIG123&oi=999&n=abc");
    QVector<AttemptBehavior> script(1);
    script[0].http_status = 403;  // 直接 403：错误文案不得带签名 URL
    auto [started, transport, timer] =
        begin_session(++bvid_counter, url, script, 0, sentinel_sessdata);
    Check(QStringLiteral("started"), started);
    const bool failed = PumpUntil(
        [&]() { return !window_->OnlineLastErrorForTest().isEmpty(); }, 5000,
        "redaction_error");
    Check(QStringLiteral("failed"), failed);
    const QString messages =
        window_->OnlineLastErrorForTest() + QStringLiteral("\n") +
        window_->StatusMessageForTest() + QStringLiteral("\n") +
        window_->OnlineProgressForTest() + QStringLiteral("\n") +
        window_->OnlineStateForTest() + QStringLiteral("\n") +
        window_->OnlineGateDebugForTest();
    Check(QStringLiteral("no_sessdata"),
          !messages.contains(sentinel_sessdata));
    Check(QStringLiteral("no_deadline"),
          !messages.contains(QStringLiteral("deadline=")));
    Check(QStringLiteral("no_upsig"),
          !messages.contains(QStringLiteral("upsig=")));
    Check(QStringLiteral("no_signature"),
          !messages.contains(QStringLiteral("SENTINELSIG")));
    Check(QStringLiteral("no_full_url"),
          !messages.contains(QStringLiteral("cdn.fault.test")));
    QElapsedTimer close_timer;
    close_timer.start();
    window_->ShutdownOnlineForTest();
    Check(QStringLiteral("close_le_1s"), close_timer.elapsed() <= 1000,
          QStringLiteral("ms=%1").arg(close_timer.elapsed()));
  });

  heartbeat.stop();
  watchdog_stop.store(true);
  if (hang_watchdog.joinable()) {
    hang_watchdog.join();
  }
  std::printf("online_fault_ui_gap max_ms=%lld over_200_count=%d\n",
              static_cast<long long>(heartbeat_max), heartbeat_over_200);
  std::fflush(stdout);
  // 真实系统休眠不可自动执行：明确写“未验证”，不冒充通过。
  std::printf(
      "online_fault_unverified item=real_windows_sleep reason=no_automated_s3_s4\n");
  std::fflush(stdout);
  const bool verdict = failures_ == 0 && heartbeat_over_200 == 0;
  std::printf(
      "online_fault_result tag=%s scenarios=%d pass=%d fail=%d unverified=1 "
      "checks=%d failures=%d ui_gap_ms=%lld verdict=%s\n",
      options_.tag.toUtf8().constData(), scenarios_, scenario_pass_,
      scenario_fail_, checks_, failures_,
      static_cast<long long>(heartbeat_max), verdict ? "pass" : "fail");
  std::fflush(stdout);
  QCoreApplication::exit(verdict ? 0 : 1);
}

}  // namespace lapian::app
