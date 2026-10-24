#include "app/OnlineM2TestRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

#include "app/MainWindow.h"
#include "app/ProcessMemory.h"
#include "online/BiliAuth.h"
#include "online/BiliResolveClient.h"
#include "online/IHttpClient.h"
#include "online/Sessdata.h"
#include "online/WinHttpClient.h"

namespace lapian::app {
namespace {

QString OneLine(const QString& text) {
  QString one = text;
  one.replace(QLatin1Char('\n'), QStringLiteral(" | "));
  return one;
}

QString Num(qint64 value) { return QString::number(value); }

double Percentile(std::vector<double> values, double percentile) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const double rank = percentile * (values.size() - 1);
  const std::size_t low = static_cast<std::size_t>(std::floor(rank));
  const std::size_t high = static_cast<std::size_t>(std::ceil(rank));
  if (low == high) {
    return values[low];
  }
  return values[low] + (values[high] - values[low]) * (rank - low);
}

struct Secret {
  bool ok = false;
  QString sessdata;
  QString error;
};

QString ResolveSecretPath(const QString& explicit_path) {
  if (!explicit_path.isEmpty()) {
    return explicit_path;
  }
  const QString env = qEnvironmentVariable("LAPIAN_SESSDATA_FILE");
  if (!env.isEmpty()) {
    return env;
  }
  const QStringList candidates = {
      QDir::current().filePath(QStringLiteral(".secrets/bilibili_sessdata.txt")),
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral(".secrets/bilibili_sessdata.txt")),
  };
  for (const QString& candidate : candidates) {
    if (QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return candidates.first();
}

Secret ReadSecret(const QString& explicit_path) {
  Secret secret;
  const QString path = ResolveSecretPath(explicit_path);
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    secret.error = QStringLiteral("missing_sessdata_file");
    return secret;
  }
  const auto normalized =
      lapian::online::NormalizeSessdata(QString::fromUtf8(file.readAll()));
  if (!normalized.ok) {
    secret.error = QStringLiteral("invalid_sessdata");
    return secret;
  }
  secret.ok = true;
  secret.sessdata = normalized.value;
  return secret;
}

// T8.14 OP4/OP5：缓存命中路径不应发起任何 HTTP；一旦被调用就计数并失败。
class CountingHttpClient final : public lapian::online::IHttpClient {
 public:
  explicit CountingHttpClient(std::shared_ptr<std::atomic<int>> calls)
      : calls_(std::move(calls)) {}

  lapian::online::HttpResponse Get(
      const lapian::online::HttpRequest&,
      const lapian::online::HttpCancelToken&) override {
    calls_->fetch_add(1);
    lapian::online::HttpResponse response;
    response.error = QStringLiteral("T8.14 缓存命中探针：HTTP 不应被调用");
    return response;
  }

 private:
  std::shared_ptr<std::atomic<int>> calls_;
};

}  // namespace

OnlineM2TestRunner::OnlineM2TestRunner(MainWindow* window,
                                       OnlineM2TestOptions options)
    : window_(window), options_(std::move(options)) {}

void OnlineM2TestRunner::Start() {
  QTimer::singleShot(0, window_, [this]() { Run(); });
}

void OnlineM2TestRunner::PumpFor(int ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
}

bool OnlineM2TestRunner::PumpUntil(const std::function<bool()>& predicate,
                                   int timeout_ms, const char* label) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    if (predicate()) {
      return true;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  if (qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG")) {
    std::fprintf(stderr, "T8.10 m2_wait_timeout label=%s ms=%lld\n", label,
                 static_cast<long long>(timer.elapsed()));
    std::fflush(stderr);
  }
  return predicate();
}

bool OnlineM2TestRunner::StartAndWaitReady(const QString& bvid,
                                           const QString& title,
                                           qint64 duration_ms,
                                           const QString& sessdata,
                                           int timeout_ms,
                                           double* ready_ms) {
  lapian::app::OnlineRequest request;
  request.bvid = bvid;
  request.cid = options_.cid;
  request.page = 1;
  request.title = title;
  request.duration_ms = duration_ms;
  request.sessdata = sessdata;
  request.requested_qn = options_.requested_qn;
  QElapsedTimer timer;
  timer.start();
  const bool started = window_->StartOnlineSession(request);
  const bool ready = started && window_->WaitForOnlineReadyForTest(timeout_ms);
  if (ready_ms != nullptr) {
    *ready_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  }
  return ready;
}

void OnlineM2TestRunner::Run() {
  auto check = [this](const QString& name, bool ok,
                      const QString& detail = QString()) {
    ++checks_;
    if (!ok) {
      ++failures_;
    }
    std::printf("online_m2_check %s ok=%d%s%s\n",
                name.toUtf8().constData(), ok ? 1 : 0,
                detail.isEmpty() ? "" : " detail=",
                OneLine(detail).toUtf8().constData());
    std::fflush(stdout);
  };
  auto finish = [this]() {
    std::printf("online_m2_result checks=%d failures=%d verdict=%s\n", checks_,
                failures_, failures_ == 0 ? "pass" : "fail");
    std::fflush(stdout);
    QCoreApplication::exit(failures_ == 0 ? 0 : 1);
  };

  const QString tag =
      options_.tag.isEmpty() ? options_.bvid : options_.tag;
  std::printf("T8.10 online_m2 begin tag=%s bvid=%s cid=%lld force_durl=%d "
              "cache_hit_only=%d play_seconds=%d seeks=%d\n",
              tag.toUtf8().constData(), options_.bvid.toUtf8().constData(),
              static_cast<long long>(options_.cid), options_.force_durl ? 1 : 0,
              options_.cache_hit_only ? 1 : 0, options_.play_seconds,
              options_.seeks);
  std::fflush(stdout);

  // T8.14 OP4/OP5：直接打开缓存命中路径，不做 nav/view/playurl；注入
  // 永不成功的 HTTP 客户端，命中失败时也不会偷偷联网。
  if (options_.op4 || options_.op5) {
    auto http_calls = std::make_shared<std::atomic<int>>(0);
    window_->SetOnlineHttpClientForTest(
        std::make_unique<CountingHttpClient>(http_calls));
    lapian::app::OnlineRequest request;
    request.bvid = options_.bvid;
    request.cid = options_.cid;
    request.page = 1;
    request.title = options_.op4 ? QStringLiteral("T8.14 OP4")
                                 : QStringLiteral("T8.14 OP5");
    request.duration_ms = 0;
    request.requested_qn = options_.requested_qn;
    QElapsedTimer open_timer;
    open_timer.start();
    const bool started = window_->StartOnlineSession(request);
    const bool ready =
        started && window_->WaitForOnlineReadyForTest(options_.timeout_ms);
    const double ready_ms =
        static_cast<double>(open_timer.nsecsElapsed()) / 1.0e6;
    check(QStringLiteral("cache_only_started"), started,
          window_->OnlineLastErrorForTest());
    check(QStringLiteral("cache_only_ready"), ready,
          window_->OnlineLastErrorForTest());
    if (!ready) {
      window_->ShutdownOnlineForTest();
      finish();
      return;
    }
    check(QStringLiteral("cache_only_hit"), window_->OnlineCacheHitForTest());
    check(QStringLiteral("cache_only_dash"), window_->OnlineDashForTest());
    check(QStringLiteral("cache_only_qn80"), window_->OnlineQualityForTest() == 80,
          QStringLiteral("qn=%1").arg(window_->OnlineQualityForTest()));
    check(QStringLiteral("cache_only_first_frame_le_2500ms"),
          ready_ms <= 2500.0,
          QStringLiteral("ms=%1").arg(ready_ms, 0, 'f', 1));
    check(QStringLiteral("cache_only_zero_playurl"),
          window_->OnlinePlayUrlRequestsForTest() == 0,
          QStringLiteral("n=%1").arg(window_->OnlinePlayUrlRequestsForTest()));
    check(QStringLiteral("cache_only_zero_dash"),
          window_->OnlineDashRequestsForTest() == 0,
          QStringLiteral("n=%1").arg(window_->OnlineDashRequestsForTest()));
    check(QStringLiteral("cache_only_zero_quality_api"),
          window_->OnlineQualityRequestsForTest() == 0,
          QStringLiteral("n=%1").arg(window_->OnlineQualityRequestsForTest()));
    check(QStringLiteral("cache_only_zero_http"), http_calls->load() == 0,
          QStringLiteral("n=%1").arg(http_calls->load()));
    std::printf(
        "online_m2_cache_only mode=%s ready_ms=%.1f qn=%d dash=%d playurl=%lld "
        "dash_requests=%lld quality_requests=%d http=%d\n",
        options_.op4 ? "op4" : "op5", ready_ms,
        window_->OnlineQualityForTest(), window_->OnlineDashForTest() ? 1 : 0,
        static_cast<long long>(window_->OnlinePlayUrlRequestsForTest()),
        static_cast<long long>(window_->OnlineDashRequestsForTest()),
        window_->OnlineQualityRequestsForTest(), http_calls->load());
    std::fflush(stdout);

    if (options_.op5) {
      window_->ShutdownOnlineForTest();
      finish();
      return;
    }

    // OP4：全缓存、连续播放；16ms 心跳统计 UI gap / 帧更新停顿。
    const bool indexed = PumpUntil(
        [this]() { return window_->IndexReadyForTest(); }, 30000,
        "op4_index");
    check(QStringLiteral("op4_index_ready"), indexed,
          QStringLiteral("frames=%1")
              .arg(static_cast<long long>(window_->IndexFrameCountForTest())));
    window_->GoToFrameForTest(0);
    const bool at_zero = PumpUntil(
        [this]() {
          return window_->OnlineSeekIdleForTest() &&
                 window_->CurrentFrameForTest() == 0;
        },
        5000, "op4_frame0");
    check(QStringLiteral("op4_frame0"), at_zero,
          QStringLiteral("frame=%1")
              .arg(static_cast<long long>(window_->CurrentFrameForTest())));

    QElapsedTimer clock;
    clock.start();
    qint64 last_tick_ms = clock.elapsed();
    qint64 last_frame = window_->CurrentFrameForTest();
    qint64 last_pts = window_->CurrentPtsUsForTest();
    qint64 last_change_ms = clock.elapsed();
    qint64 ui_gap_max_ms = 0;
    int ui_gaps_gt_200 = 0;
    int frame_stalls_gt_200 = 0;
    int freeze_stalls_gt_200 = 0;
    int frame_changes = 0;
    bool in_frame_stall = false;
    std::uint64_t peak_ws = 0;
    qint64 last_memory_ms = 0;
    QTimer heartbeat;
    heartbeat.setInterval(16);
    heartbeat.setTimerType(Qt::PreciseTimer);
    QObject::connect(&heartbeat, &QTimer::timeout, [&]() {
      const qint64 now = clock.elapsed();
      const qint64 gap = now - last_tick_ms;
      last_tick_ms = now;
      ui_gap_max_ms = std::max(ui_gap_max_ms, gap);
      const qint64 frame = window_->CurrentFrameForTest();
      const qint64 pts = window_->CurrentPtsUsForTest();
      const bool changed = frame != last_frame || pts != last_pts;
      if (changed) {
        ++frame_changes;
        last_frame = frame;
        last_pts = pts;
        last_change_ms = now;
        in_frame_stall = false;
      }
      if (gap > 200) {
        ++ui_gaps_gt_200;
        // P5：UI 事件循环冻结且画面在同一间隔内没有更新。
        if (!changed) {
          ++freeze_stalls_gt_200;
        }
      }
      if (window_->OnlinePlayingForTest() &&
          now - last_change_ms > 200 && !in_frame_stall) {
        ++frame_stalls_gt_200;
        in_frame_stall = true;
      }
      if (now - last_memory_ms >= 1000) {
        last_memory_ms = now;
        ProcessMemorySample sample;
        if (SampleProcessMemory(&sample)) {
          peak_ws = std::max(peak_ws, sample.working_set_bytes);
        }
      }
    });
    const qulonglong underruns_before = window_->AudioUnderrunCountForTest();
    const int presented_before = window_->PresentedFrameCountForTest();
    heartbeat.start();
    window_->StartPlaybackForTest();
    last_change_ms = clock.elapsed();
    last_frame = window_->CurrentFrameForTest();
    last_pts = window_->CurrentPtsUsForTest();
    const qint64 play_ms =
        static_cast<qint64>(options_.play_seconds) * 1000;
    qint64 steady_ms = 0;
    qint64 segment_start_ms = clock.elapsed();
    int restarts = 0;
    while (true) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(1);
      const qint64 now = clock.elapsed();
      if (steady_ms + (now - segment_start_ms) >= play_ms) {
        break;
      }
      if (!window_->OnlinePlayingForTest()) {
        steady_ms += now - segment_start_ms;
        if (steady_ms >= play_ms) {
          break;
        }
        if (!window_->OnlineReadyForTest()) {
          break;
        }
        // 素材短于 5 分钟：播到 EOF 后回到 0 继续；这是探针动作，
        // 停心跳并重置基线，避免把有意 seek 算成播放停顿。
        ++restarts;
        heartbeat.stop();
        window_->GoToFrameForTest(0);
        const bool restarted = PumpUntil(
            [this]() {
              return window_->OnlineSeekIdleForTest() &&
                     window_->CurrentFrameForTest() == 0;
            },
            5000, "op4_restart");
        window_->StartPlaybackForTest();
        last_frame = window_->CurrentFrameForTest();
        last_pts = window_->CurrentPtsUsForTest();
        last_change_ms = clock.elapsed();
        in_frame_stall = false;
        segment_start_ms = clock.elapsed();
        heartbeat.start();
        if (!restarted) {
          break;
        }
      }
    }
    if (window_->OnlinePlayingForTest()) {
      steady_ms += clock.elapsed() - segment_start_ms;
    }
    window_->PausePlaybackForTest();
    heartbeat.stop();
    const qulonglong underruns =
        window_->AudioUnderrunCountForTest() - underruns_before;
    const int presented =
        window_->PresentedFrameCountForTest() - presented_before;
    const int stalls_gt_200 =
        std::max(frame_stalls_gt_200, freeze_stalls_gt_200);
    std::printf(
        "online_m2_op4 steady_ms=%lld restarts=%d ui_gap_max_ms=%lld "
        "ui_gaps_gt_200=%d frame_stalls_gt_200=%d freeze_stalls_gt_200=%d "
        "stalls_gt_200=%d frame_changes=%d presented=%d underruns=%llu "
        "memory_peak_mb=%.1f\n",
        static_cast<long long>(steady_ms), restarts,
        static_cast<long long>(ui_gap_max_ms), ui_gaps_gt_200,
        frame_stalls_gt_200, freeze_stalls_gt_200, stalls_gt_200,
        frame_changes, presented,
        static_cast<unsigned long long>(underruns),
        static_cast<double>(peak_ws) / (1024.0 * 1024.0));
    std::fflush(stdout);
    check(QStringLiteral("op4_steady_300s"), steady_ms >= play_ms - 1000,
          QStringLiteral("steady_ms=%1 target=%2")
              .arg(static_cast<long long>(steady_ms))
              .arg(play_ms));
    check(QStringLiteral("op4_stalls_gt_200_zero"), stalls_gt_200 == 0,
          QStringLiteral("stalls=%1 frame=%2 freeze=%3")
              .arg(stalls_gt_200)
              .arg(frame_stalls_gt_200)
              .arg(freeze_stalls_gt_200));
    check(QStringLiteral("op4_underruns_zero"), underruns == 0,
          QStringLiteral("n=%1").arg(static_cast<unsigned long long>(underruns)));
    check(QStringLiteral("op4_memory_le_800mb"),
          peak_ws > 0 && peak_ws <= 800ull * 1024ull * 1024ull,
          QStringLiteral("peak_mb=%1")
              .arg(static_cast<double>(peak_ws) / (1024.0 * 1024.0), 0, 'f', 1));
    check(QStringLiteral("op4_frames_advanced"), presented > 0,
          QStringLiteral("presented=%1").arg(presented));
    window_->ShutdownOnlineForTest();
    finish();
    return;
  }

  Secret secret = ReadSecret(options_.sessdata_file);
  if (!secret.ok) {
    check(QStringLiteral("secret"), false, secret.error);
    finish();
    return;
  }
  lapian::online::WinHttpClient http;
  lapian::online::HttpCancelToken cancel;
  const lapian::online::AuthResult auth =
      lapian::online::CheckNav(secret.sessdata, &http, cancel, 4000);
  check(QStringLiteral("nav_login"), auth.is_login, auth.message);
  check(QStringLiteral("nav_normal_user"), auth.vip_status == 0,
        QStringLiteral("vip_status=%1").arg(auth.vip_status));
  if (!auth.is_login || auth.vip_status != 0) {
    lapian::online::ClearSecret(&secret.sessdata);
    finish();
    return;
  }

  lapian::online::BiliResolveClient resolver(&http);
  const lapian::online::BiliResolveResult resolved =
      resolver.Resolve(options_.bvid, cancel);
  check(QStringLiteral("view_ok"), resolved.error.ok(), resolved.error.message);
  if (!resolved.error.ok() || resolved.video.pages.isEmpty() ||
      resolved.video.pages.first().cid != options_.cid) {
    if (resolved.error.ok()) {
      check(QStringLiteral("view_cid_matches"), false,
            QStringLiteral("cid=%1")
                .arg(resolved.video.pages.isEmpty()
                         ? 0
                         : resolved.video.pages.first().cid));
    }
    lapian::online::ClearSecret(&secret.sessdata);
    finish();
    return;
  }
  const qint64 duration_ms =
      resolved.video.pages.first().duration_seconds * 1000;

  window_->raise();
  window_->activateWindow();
  window_->FocusVideoForTest();
  window_->SetOnlineDashEnabledForTest(!options_.force_durl);

  // 缓存命中专用：不播放，只验证 0 网络 + 首帧。
  if (options_.cache_hit_only) {
    double ready_ms = 0.0;
    const bool ready = StartAndWaitReady(options_.bvid, resolved.video.title,
                                         duration_ms, secret.sessdata,
                                         options_.timeout_ms, &ready_ms);
    check(QStringLiteral("cache_hit_ready"), ready,
          window_->OnlineLastErrorForTest());
    check(QStringLiteral("cache_hit_hit"), window_->OnlineCacheHitForTest());
    check(QStringLiteral("cache_hit_dash"), window_->OnlineDashForTest());
    check(QStringLiteral("cache_hit_first_frame_le_2500ms"), ready_ms <= 2500.0,
          QStringLiteral("ms=%1").arg(ready_ms, 0, 'f', 1));
    check(QStringLiteral("cache_hit_zero_playurl"),
          window_->OnlinePlayUrlRequestsForTest() == 0,
          QStringLiteral("n=%1").arg(window_->OnlinePlayUrlRequestsForTest()));
    check(QStringLiteral("cache_hit_zero_dash"),
          window_->OnlineDashRequestsForTest() == 0,
          QStringLiteral("n=%1").arg(window_->OnlineDashRequestsForTest()));
    check(QStringLiteral("cache_hit_zero_quality_api"),
          window_->OnlineQualityRequestsForTest() == 0,
          QStringLiteral("n=%1").arg(window_->OnlineQualityRequestsForTest()));
    std::printf("online_m2_cache_hit ready_ms=%.1f qn=%d dash=%d\n", ready_ms,
                window_->OnlineQualityForTest(), window_->OnlineDashForTest() ? 1 : 0);
    std::fflush(stdout);
    window_->ShutdownOnlineForTest();
    lapian::online::ClearSecret(&secret.sessdata);
    finish();
    return;
  }

  double ready_ms = 0.0;
  const bool ready = StartAndWaitReady(
      options_.bvid, resolved.video.title, duration_ms, secret.sessdata,
      options_.timeout_ms, &ready_ms);
  check(QStringLiteral("first_frame_ready"), ready,
        window_->OnlineLastErrorForTest());
  if (!ready) {
    window_->ShutdownOnlineForTest();
    lapian::online::ClearSecret(&secret.sessdata);
    finish();
    return;
  }
  const int qn = window_->OnlineQualityForTest();
  const bool dash = window_->OnlineDashForTest();
  const double fps = window_->OnlineDashFpsForTest() > 0.0
                         ? window_->OnlineDashFpsForTest()
                         : window_->FrameRateForTest();
  const double frame_ms = fps > 0.0 ? 1000.0 / fps : 33.333;
  std::printf(
      "online_m2_first ready_ms=%.1f qn=%d dash=%d audio_id=%d fps=%.3f "
      "frame_ms=%.2f total=%lld audio_total=%lld playurl_requests=%lld\n",
      ready_ms, qn, dash ? 1 : 0, window_->OnlineAudioIdForTest(), fps,
      frame_ms, static_cast<long long>(window_->OnlineTotalBytesForTest()),
      static_cast<long long>(window_->OnlineAudioTotalBytesForTest()),
      static_cast<long long>(window_->OnlinePlayUrlRequestsForTest()));
  std::fflush(stdout);

  if (options_.force_durl) {
    // M1 回退：必须仍是单文件 durl，且能播放。
    check(QStringLiteral("durl_fallback_not_dash"), !dash);
    check(QStringLiteral("durl_fallback_has_video"), window_->OnlineTotalBytesForTest() > 0);
    check(QStringLiteral("durl_fallback_first_frame"), ready_ms <= 8000.0,
          QStringLiteral("ms=%1").arg(ready_ms, 0, 'f', 1));
    window_->StartPlaybackForTest();
    PumpFor(3000);
    check(QStringLiteral("durl_fallback_play_advanced"),
          window_->CurrentFrameForTest() > 0,
          QStringLiteral("frame=%1")
              .arg(static_cast<long long>(window_->CurrentFrameForTest())));
    window_->PausePlaybackForTest();
    window_->ShutdownOnlineForTest();
    lapian::online::ClearSecret(&secret.sessdata);
    finish();
    return;
  }

  check(QStringLiteral("dash_dual"), dash);
  check(QStringLiteral("dash_1080p_qn80"), qn == 80,
        QStringLiteral("qn=%1").arg(qn));
  check(QStringLiteral("dash_audio_present"),
        window_->OnlineAudioIdForTest() > 0 &&
            window_->OnlineAudioTotalBytesForTest() > 0,
        QStringLiteral("audio_id=%1 total=%2")
            .arg(window_->OnlineAudioIdForTest())
            .arg(window_->OnlineAudioTotalBytesForTest()));
  check(QStringLiteral("dash_fps_expected"),
        std::fabs(fps - 30.0) < 0.05 || std::fabs(fps - 29.412) < 0.05 ||
            std::fabs(fps - 25.0) < 0.05,
        QStringLiteral("fps=%1").arg(fps, 0, 'f', 3));

  // 索引至少覆盖 2 秒，音画采样/seek 才有帧号。
  const int min_frames = static_cast<int>(std::ceil(fps * 2.0));
  const bool index_ready =
      window_->WaitForOnlineIndexFramesForTest(min_frames, 20000);
  check(QStringLiteral("index_ready"), index_ready,
        QStringLiteral("frames=%1 stable=%2")
            .arg(window_->OnlineIndexFramesForTest())
            .arg(window_->OnlineStableFramesForTest()));

  // 60s 播放 + 音画偏差采样（音频主时钟 vs 当前显示帧 PTS）。
  window_->StartPlaybackForTest();
  const bool audio_started = PumpUntil(
      [this]() {
        return window_->OnlineAudioStateForTest() == 2 ||
               window_->OnlineAudioClockForTest(nullptr);
      },
      5000, "audio_start");
  check(QStringLiteral("audio_started"), audio_started,
        QStringLiteral("state=%1").arg(window_->OnlineAudioStateForTest()));
  std::vector<double> drift_ms;
  std::vector<double> drift_abs_ms;
  std::uint64_t peak_ws = 0;
  QElapsedTimer play_timer;
  play_timer.start();
  QElapsedTimer sample_timer;
  sample_timer.start();
  QElapsedTimer memory_timer;
  memory_timer.start();
  const qint64 play_ms = static_cast<qint64>(options_.play_seconds) * 1000;
  while (play_timer.elapsed() < play_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
    if (!window_->OnlinePlayingForTest()) {
      break;
    }
    if (sample_timer.elapsed() >= 20) {
      sample_timer.restart();
      double audio_seconds = 0.0;
      if (window_->OnlineAudioClockForTest(&audio_seconds) &&
          window_->CurrentPtsUsForTest() >= 0) {
        const double frame_seconds =
            static_cast<double>(window_->CurrentPtsUsForTest()) / 1.0e6 +
            frame_ms / 2000.0;  // 帧中心：一帧代表 [pts, pts+1/fps) 区间
        const double delta = (audio_seconds - frame_seconds) * 1000.0;
        drift_ms.push_back(delta);
        drift_abs_ms.push_back(std::fabs(delta));
      }
    }
    if (memory_timer.elapsed() >= 1000) {
      memory_timer.restart();
      ProcessMemorySample sample;
      if (SampleProcessMemory(&sample)) {
        peak_ws = std::max(peak_ws, sample.working_set_bytes);
      }
    }
  }
  const qint64 played_ms = play_timer.elapsed();
  const int played_frames = window_->CurrentFrameForTest();
  window_->PausePlaybackForTest();
  check(QStringLiteral("played_60s"), played_ms >= play_ms - 1000,
        QStringLiteral("ms=%1").arg(played_ms));
  check(QStringLiteral("play_advanced"), played_frames > 0,
        QStringLiteral("frames=%1").arg(played_frames));
  check(QStringLiteral("av_samples"),
        drift_ms.size() >=
            static_cast<std::size_t>(std::max(10, options_.play_seconds * 10)),
        QStringLiteral("n=%1").arg(drift_ms.size()));
  if (!drift_abs_ms.empty()) {
    const double p50 = Percentile(drift_abs_ms, 0.50);
    const double p95 = Percentile(drift_abs_ms, 0.95);
    const double max = *std::max_element(drift_abs_ms.begin(), drift_abs_ms.end());
    std::printf("online_m2_av_drift n=%zu p50_ms=%.1f p95_ms=%.1f max_ms=%.1f "
                "frame_ms=%.2f\n",
                drift_abs_ms.size(), p50, p95, max, frame_ms);
    std::fflush(stdout);
    check(QStringLiteral("av_drift_p95_le_1frame"), p95 <= frame_ms,
          QStringLiteral("p95=%1 frame=%2").arg(p95, 0, 'f', 2).arg(frame_ms, 0, 'f', 2));
    // 单帧边界处的调度抖动允许不超过 1.5 帧；同时报告严格 max。
    check(QStringLiteral("av_drift_max_le_1_5frame"), max <= frame_ms * 1.5,
          QStringLiteral("max=%1 frame=%2").arg(max, 0, 'f', 2).arg(frame_ms, 0, 'f', 2));
  }

  // 已缓存区 20 次 seek：目标距前关键帧 ≤2s（P9 口径）；落点误差 0。
  const bool cache_complete = window_->WaitForOnlineCacheCompleteForTest(20000);
  check(QStringLiteral("cache_complete"), cache_complete,
        window_->OnlineProgressForTest());
  if (!window_->WaitForOnlineIndexFramesForTest(
          std::min<int>(window_->OnlineIndexFramesForTest() + 1, 100000), 20000)) {
    // 索引已完成时上面的 predicate 立即为真；失败只记录。
  }
  const qint64 last_frame = window_->OnlineLastFrameForTest();
  check(QStringLiteral("last_frame_known"), last_frame > 0,
        QStringLiteral("last=%1").arg(last_frame));
  const int max_keyframe_distance =
      std::max(1, static_cast<int>(std::floor(fps * 2.0 + 0.5)));
  const qint64 seek_hi = std::min<qint64>(
      last_frame, static_cast<qint64>(std::ceil(fps * 60.0)));
  std::mt19937 rng(20261024u);
  std::uniform_int_distribution<qint64> dist(0, std::max<qint64>(1, seek_hi));
  std::vector<qint64> targets;
  for (int attempt = 0; attempt < 400 && targets.size() < static_cast<std::size_t>(options_.seeks); ++attempt) {
    const qint64 frame = dist(rng);
    const qint64 distance = window_->FrameDistanceFromKeyframeForTest(frame);
    if (distance < 0 || distance > max_keyframe_distance) {
      continue;
    }
    if (!window_->OnlineFrameCachedForTest(frame)) {
      continue;
    }
    targets.push_back(frame);
  }
  check(QStringLiteral("seek_targets_found"),
        targets.size() == static_cast<std::size_t>(options_.seeks),
        QStringLiteral("n=%1/%2").arg(targets.size()).arg(options_.seeks));

  std::vector<double> seek_ms;
  int landing_failures = 0;
  int pts_failures = 0;
  int audio_resume_failures = 0;
  for (const qint64 target : targets) {
    QElapsedTimer seek_timer;
    seek_timer.start();
    window_->GoToFrameForTest(target);
    const bool landed = PumpUntil(
        [this, target]() {
          return window_->OnlineSeekIdleForTest() &&
                 window_->CurrentFrameForTest() == target;
        },
        6000, "online_seek");
    const double elapsed = static_cast<double>(seek_timer.nsecsElapsed()) / 1.0e6;
    if (!landed) {
      ++landing_failures;
      continue;
    }
    seek_ms.push_back(elapsed);
    const qint64 expected_pts = window_->OnlineFramePtsForTest(target);
    if (expected_pts >= 0 &&
        std::llabs(window_->CurrentPtsUsForTest() - expected_pts) > 1000) {
      ++pts_failures;
    }
    const double target_seconds =
        static_cast<double>(window_->CurrentPtsUsForTest()) / 1.0e6;
    // seek 后音频必须从当前帧起，而不是旧位置/从头。
    window_->StartPlaybackForTest();
    PumpUntil(
        [this]() {
          return window_->OnlineAudioStateForTest() == 2 ||
                 window_->OnlineAudioClockForTest(nullptr);
        },
        2000, "audio_resume");
    PumpFor(250);
    double audio_seconds = 0.0;
    const bool clock = window_->OnlineAudioClockForTest(&audio_seconds);
    const double position =
        clock ? audio_seconds : window_->OnlineAudioPositionForTest();
    window_->PausePlaybackForTest();
    PumpFor(40);
    const double expected = target_seconds + 0.25;
    if (std::fabs(position - expected) > 0.55) {
      ++audio_resume_failures;
      if (qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG")) {
        std::fprintf(stderr,
                     "T8.10 m2_audio_resume_mismatch frame=%lld target=%.3f "
                     "pos=%.3f clock=%d\n",
                     static_cast<long long>(target), target_seconds, position,
                     clock ? 1 : 0);
        std::fflush(stderr);
      }
    }
  }
  if (!seek_ms.empty()) {
    const double p50 = Percentile(seek_ms, 0.50);
    const double p95 = Percentile(seek_ms, 0.95);
    const double max = *std::max_element(seek_ms.begin(), seek_ms.end());
    std::printf(
        "online_m2_seek n=%zu landing_failures=%d pts_failures=%d "
        "p50_ms=%.1f p95_ms=%.1f max_ms=%.1f audio_resume_failures=%d\n",
        seek_ms.size(), landing_failures, pts_failures, p50, p95, max,
        audio_resume_failures);
    std::fflush(stdout);
  }
  check(QStringLiteral("seek_landing_zero"), landing_failures == 0,
        QStringLiteral("failures=%1").arg(landing_failures));
  check(QStringLiteral("seek_pts_exact"), pts_failures == 0,
        QStringLiteral("failures=%1").arg(pts_failures));
  check(QStringLiteral("seek_audio_from_current"), audio_resume_failures == 0,
        QStringLiteral("failures=%1").arg(audio_resume_failures));

  // 单流 EOF：视频先结束时应正常停在末帧，不被仍在播放的音频拖住/死锁。
  if (last_frame > 10) {
    window_->GoToFrameForTest(last_frame - 2);
    const bool end_landed = PumpUntil(
        [this, last_frame]() {
          return window_->OnlineSeekIdleForTest() &&
                 window_->CurrentFrameForTest() == last_frame - 2;
        },
        6000, "eof_seek");
    check(QStringLiteral("eof_seek_landed"), end_landed,
          QStringLiteral("distance=%1")
              .arg(window_->FrameDistanceFromKeyframeForTest(last_frame - 2)));
    if (end_landed) {
      const qint64 expected_pts = window_->OnlineFramePtsForTest(last_frame - 2);
      check(QStringLiteral("eof_seek_pts_exact"),
            expected_pts >= 0 &&
                std::llabs(window_->CurrentPtsUsForTest() - expected_pts) <= 1000,
            QStringLiteral("expected=%1 actual=%2")
                .arg(expected_pts)
                .arg(window_->CurrentPtsUsForTest()));
    }
    window_->StartPlaybackForTest();
    const bool stopped = PumpUntil(
        [this]() { return !window_->OnlinePlayingForTest(); }, 6000,
        "eof_stop");
    check(QStringLiteral("video_eof_stops_playback"), stopped,
          QStringLiteral("frame=%1")
              .arg(static_cast<long long>(window_->CurrentFrameForTest())));
    window_->PausePlaybackForTest();
    PumpFor(100);
  }

  ProcessMemorySample memory;
  if (SampleProcessMemory(&memory)) {
    peak_ws = std::max(peak_ws, memory.working_set_bytes);
    std::printf("online_m2_memory working_set_mb=%.1f peak_mb=%.1f "
                "private_mb=%.1f handles=%u threads=%u\n",
                static_cast<double>(memory.working_set_bytes) / (1024.0 * 1024.0),
                static_cast<double>(peak_ws) / (1024.0 * 1024.0),
                static_cast<double>(memory.private_bytes) / (1024.0 * 1024.0),
                memory.handle_count, memory.thread_count);
    std::fflush(stdout);
  }
  check(QStringLiteral("memory_le_800mb"),
        peak_ws > 0 && peak_ws <= 800ull * 1024ull * 1024ull,
        QStringLiteral("peak_mb=%1")
            .arg(static_cast<double>(peak_ws) / (1024.0 * 1024.0), 0, 'f', 1));

  QElapsedTimer close_timer;
  close_timer.start();
  window_->ShutdownOnlineForTest();
  const qint64 close_ms = close_timer.elapsed();
  check(QStringLiteral("close_le_1000ms"), close_ms <= 1000,
        QStringLiteral("ms=%1").arg(close_ms));
  check(QStringLiteral("online_mode_closed"), !window_->OnlineModeForTest());
  PumpFor(100);

  if (!options_.skip_cache_hit) {
    // 再打开同一 BV：完整缓存命中必须 0 网络、首帧 ≤2.5s、仍是 DASH 双输入。
    double hit_ms = 0.0;
    const bool hit_ready = StartAndWaitReady(
        options_.bvid, resolved.video.title, duration_ms, secret.sessdata,
        options_.timeout_ms, &hit_ms);
    check(QStringLiteral("cache_hit_ready"), hit_ready,
          window_->OnlineLastErrorForTest());
    check(QStringLiteral("cache_hit_hit"), window_->OnlineCacheHitForTest());
    check(QStringLiteral("cache_hit_dash"), window_->OnlineDashForTest());
    check(QStringLiteral("cache_hit_first_frame_le_2500ms"), hit_ms <= 2500.0,
          QStringLiteral("ms=%1").arg(hit_ms, 0, 'f', 1));
    check(QStringLiteral("cache_hit_zero_playurl"),
          window_->OnlinePlayUrlRequestsForTest() == 0,
          QStringLiteral("n=%1").arg(window_->OnlinePlayUrlRequestsForTest()));
    check(QStringLiteral("cache_hit_zero_dash"),
          window_->OnlineDashRequestsForTest() == 0,
          QStringLiteral("n=%1").arg(window_->OnlineDashRequestsForTest()));
    check(QStringLiteral("cache_hit_zero_quality_api"),
          window_->OnlineQualityRequestsForTest() == 0,
          QStringLiteral("n=%1").arg(window_->OnlineQualityRequestsForTest()));
    std::printf("online_m2_cache_hit ready_ms=%.1f qn=%d dash=%d\n", hit_ms,
                window_->OnlineQualityForTest(), window_->OnlineDashForTest() ? 1 : 0);
    std::fflush(stdout);
    window_->ShutdownOnlineForTest();
  }

  lapian::online::ClearSecret(&secret.sessdata);
  finish();
}

}  // namespace lapian::app
