#include "app/OnlineIndexTestRunner.h"

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
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "app/MainWindow.h"
#include "audio/AudioDecoder.h"
#include "audio/WaveformGenerator.h"
#include "media/MediaProbe.h"
#include "media/OnlineIndexBuilder.h"
#include "media/VideoDecoder.h"
#include "online/BiliAuth.h"
#include "online/BiliResolveClient.h"
#include "online/GrowingSource.h"
#include "online/Sessdata.h"
#include "online/WinHttpClient.h"

namespace lapian::app {
namespace {

using Clock = std::chrono::steady_clock;

QString OneLine(QString text) {
  text.replace(QLatin1Char('\n'), QStringLiteral(" | "));
  return text;
}

double Percentile(std::vector<qint64> values, double percentile) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const double rank = percentile * static_cast<double>(values.size() - 1);
  const std::size_t low = static_cast<std::size_t>(std::floor(rank));
  const std::size_t high = static_cast<std::size_t>(std::ceil(rank));
  if (low == high) {
    return static_cast<double>(values[low]);
  }
  return static_cast<double>(values[low]) +
         static_cast<double>(values[high] - values[low]) * (rank - static_cast<double>(low));
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

// 受控慢增长：按 bytes_per_sec 从 initial 发布到 total；后台线程只碰 GrowingSource。
class SlowPublisher {
 public:
  SlowPublisher(std::shared_ptr<lapian::online::GrowingSource> source,
                qint64 initial_bytes, qint64 bytes_per_sec)
      : source_(std::move(source)),
        initial_bytes_(initial_bytes),
        bytes_per_sec_(std::max<qint64>(1, bytes_per_sec)) {}

  ~SlowPublisher() { Stop(); }

  void Start() {
    stop_.store(false);
    thread_ = std::thread([this]() {
      const auto started = Clock::now();
      source_->Publish(std::min(initial_bytes_, source_->Size()));
      while (!stop_.load(std::memory_order_relaxed)) {
        const double seconds =
            std::chrono::duration<double>(Clock::now() - started).count();
        const qint64 target = std::min<qint64>(
            source_->Size(),
            initial_bytes_ + static_cast<qint64>(seconds * bytes_per_sec_));
        source_->Publish(target);
        if (target >= source_->Size()) {
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    });
  }

  void Stop() {
    stop_.store(true, std::memory_order_relaxed);
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  std::shared_ptr<lapian::online::GrowingSource> source_;
  qint64 initial_bytes_ = 0;
  qint64 bytes_per_sec_ = 1;
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

}  // namespace

OnlineIndexTestRunner::OnlineIndexTestRunner(MainWindow* window,
                                             OnlineIndexTestOptions options)
    : window_(window), options_(std::move(options)) {}

void OnlineIndexTestRunner::Start() {
  QTimer::singleShot(0, window_, [this]() { Run(); });
}

void OnlineIndexTestRunner::PumpFor(int ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
}

bool OnlineIndexTestRunner::PumpUntil(
    const std::function<bool()>& predicate, int timeout_ms, const char* label) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    if (predicate()) {
      return true;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  std::fprintf(stderr, "T8.11 online_index_wait_timeout label=%s ms=%lld\n",
               label, static_cast<long long>(timer.elapsed()));
  std::fflush(stderr);
  return predicate();
}

void OnlineIndexTestRunner::Check(const QString& name, bool ok,
                                  const QString& detail) {
  ++checks_;
  if (!ok) {
    ++failures_;
  }
  std::printf("online_index_check %s ok=%d%s%s\n", name.toUtf8().constData(),
              ok ? 1 : 0, detail.isEmpty() ? "" : " detail=",
              OneLine(detail).toUtf8().constData());
  std::fflush(stdout);
}

void OnlineIndexTestRunner::Finish() {
  std::printf("online_index_result checks=%d failures=%d verdict=%s\n", checks_,
              failures_, failures_ == 0 ? "pass" : "fail");
  std::fflush(stdout);
  QCoreApplication::exit(failures_ == 0 ? 0 : 1);
}

bool OnlineIndexTestRunner::StartAndWaitReady(const QString& bvid,
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
  QElapsedTimer timer;
  timer.start();
  const bool started = window_->StartOnlineSession(request);
  const bool ready = started && window_->WaitForOnlineReadyForTest(timeout_ms);
  if (ready_ms != nullptr) {
    *ready_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  }
  return ready;
}

void OnlineIndexTestRunner::Run() {
  const QString tag =
      options_.tag.isEmpty() ? options_.bvid : options_.tag;
  std::printf("T8.11 online_index begin tag=%s bvid=%s cid=%lld "
              "throttle_bps=%lld offline=%s\n",
              tag.toUtf8().constData(), options_.bvid.toUtf8().constData(),
              static_cast<long long>(options_.cid),
              static_cast<long long>(options_.throttle_bytes_per_sec),
              options_.offline_video.toUtf8().constData());
  std::fflush(stdout);
  if (!options_.offline_video.isEmpty()) {
    RunOfflineSlow();
    return;
  }

  Secret secret = ReadSecret(options_.sessdata_file);
  if (!secret.ok) {
    Check(QStringLiteral("secret"), false, secret.error);
    Finish();
    return;
  }
  lapian::online::WinHttpClient http;
  lapian::online::HttpCancelToken cancel;
  const lapian::online::AuthResult auth =
      lapian::online::CheckNav(secret.sessdata, &http, cancel, 4000);
  Check(QStringLiteral("nav_login"), auth.is_login, auth.message);
  if (!auth.is_login) {
    lapian::online::ClearSecret(&secret.sessdata);
    Finish();
    return;
  }
  lapian::online::BiliResolveClient resolver(&http);
  const lapian::online::BiliResolveResult resolved =
      resolver.Resolve(options_.bvid, cancel);
  Check(QStringLiteral("view_ok"), resolved.error.ok(), resolved.error.message);
  if (!resolved.error.ok() || resolved.video.pages.isEmpty() ||
      resolved.video.pages.first().cid != options_.cid) {
    Check(QStringLiteral("view_cid_matches"), false);
    lapian::online::ClearSecret(&secret.sessdata);
    Finish();
    return;
  }
  const qint64 duration_ms =
      resolved.video.pages.first().duration_seconds * 1000;

  window_->raise();
  window_->activateWindow();
  window_->FocusVideoForTest();
  window_->SetOnlineDashEnabledForTest(true);
  window_->SetOnlineDownloadThrottleForTest(options_.throttle_bytes_per_sec);

  // UI 心跳：16ms 精确定时器，>200ms 记一次停顿。
  QElapsedTimer heartbeat_clock;
  heartbeat_clock.start();
  qint64 heartbeat_last = 0;
  qint64 heartbeat_max = 0;
  QTimer heartbeat;
  heartbeat.setInterval(16);
  heartbeat.setTimerType(Qt::PreciseTimer);
  QObject::connect(&heartbeat, &QTimer::timeout, [&]() {
    const qint64 now = heartbeat_clock.elapsed();
    if (heartbeat_last > 0) {
      const qint64 gap = now - heartbeat_last;
      if (gap > 200) {
        std::printf("online_index_ui_gap_event gap_ms=%lld at_ms=%lld\n",
                    static_cast<long long>(gap), static_cast<long long>(now));
        std::fflush(stdout);
      }
      heartbeat_max = std::max(heartbeat_max, gap);
    }
    heartbeat_last = now;
  });
  heartbeat.start();

  double ready_ms = 0.0;
  const bool ready = StartAndWaitReady(
      options_.bvid, resolved.video.title, duration_ms, secret.sessdata,
      options_.timeout_ms, &ready_ms);
  Check(QStringLiteral("first_frame_ready"), ready,
        window_->OnlineLastErrorForTest());
  if (!ready) {
    heartbeat.stop();
    window_->ShutdownOnlineForTest();
    lapian::online::ClearSecret(&secret.sessdata);
    Finish();
    return;
  }
  const double fps = window_->OnlineVideoFpsForTest() > 0.0
                         ? window_->OnlineVideoFpsForTest()
                         : window_->FrameRateForTest();
  const double duration_s = window_->OnlineDurationSecondsForTest();
  const qint64 container_frames = window_->OnlineContainerFramesForTest();
  std::printf("T8.11 online_index_ready ms=%.1f fps=%.3f duration_s=%.3f "
              "container_frames=%lld total_bytes=%lld dash=%d\n",
              ready_ms, fps, duration_s,
              static_cast<long long>(container_frames),
              static_cast<long long>(window_->OnlineTotalBytesForTest()),
              window_->OnlineDashForTest() ? 1 : 0);
  std::fflush(stdout);

  // 首次索引可用（stable>0）耗时；然后等稳定前缀覆盖 30s 内容。
  QElapsedTimer phase;
  phase.start();
  const bool first_index = PumpUntil(
      [this]() { return window_->OnlineStableFramesForTest() > 0; },
      options_.timeout_ms, "first_index");
  const qint64 first_index_ms = phase.elapsed();
  Check(QStringLiteral("index_first_available"), first_index);
  Check(QStringLiteral("index_first_within_20s"), first_index_ms <= 20000,
        QStringLiteral("ms=%1").arg(first_index_ms));

  const int expected_30_frames =
      static_cast<int>(std::llround(30.0 * fps));
  phase.restart();
  const bool reached_30 = PumpUntil(
      [this, expected_30_frames]() {
        return window_->OnlineStableFramesForTest() >= expected_30_frames ||
               window_->OnlineIndexCompleteForTest();
      },
      options_.timeout_ms, "index_30s");
  const qint64 stable30_ms = phase.elapsed();
  Check(QStringLiteral("index_30s_reached"), reached_30,
        QStringLiteral("stable=%1 expected=%2 ms=%3")
            .arg(window_->OnlineStableFramesForTest())
            .arg(expected_30_frames)
            .arg(stable30_ms));
  const qint64 published_at_30 = window_->OnlinePublishedBytesForTest();
  const qint64 total_bytes_for_30 = window_->OnlineTotalBytesForTest();
  const int stable_at_check = static_cast<int>(window_->OnlineStableFramesForTest());
  const bool complete_at_check = window_->OnlineIndexCompleteForTest();
  const QString total_state_at_check = window_->OnlineTotalStateForTest();
  const int waveform_at_check = window_->OnlineWaveformBucketCountForTest();
  const qint64 cached_seconds_diagnostic = window_->OnlineCachedSecondsForTest();
  std::printf(
      "T8.11 online_index_at_30s stable=%d expected=%d published=%lld "
      "total_bytes=%lld cached_s_byte_ratio=%lld complete=%d total_state=%s "
      "waveform_buckets=%d ms=%lld\n",
      stable_at_check, expected_30_frames,
      static_cast<long long>(published_at_30),
      static_cast<long long>(total_bytes_for_30),
      static_cast<long long>(cached_seconds_diagnostic),
      complete_at_check ? 1 : 0, total_state_at_check.toUtf8().constData(),
      waveform_at_check, static_cast<long long>(stable30_ms));
  std::fflush(stdout);
  if (!complete_at_check) {
    // 稳定前缀覆盖 30s 内容；此时缓存仍在增长（未完成），说明是渐进可用。
    Check(QStringLiteral("index_covers_30s"), stable_at_check >= expected_30_frames,
          QStringLiteral("stable=%1 expected=%2")
              .arg(stable_at_check)
              .arg(expected_30_frames));
    Check(QStringLiteral("index_30s_before_complete"),
          published_at_30 > 0 && published_at_30 < total_bytes_for_30,
          QStringLiteral("published=%1 total=%2")
              .arg(published_at_30)
              .arg(total_bytes_for_30));
    Check(QStringLiteral("total_state_estimated_while_scanning"),
          total_state_at_check.startsWith(QStringLiteral("estimate:")),
          total_state_at_check);
    Check(QStringLiteral("waveform_started_progressive"), waveform_at_check > 0,
          QStringLiteral("buckets=%1").arg(waveform_at_check));
  }

  // 缓存区 seek：只选已进入稳定前缀且距关键帧 ≤2s 的目标，落点必须 0。
  std::vector<qint64> targets;
  const qint64 seek_limit = std::min<qint64>(
      window_->OnlineStableFramesForTest() - 1, container_frames - 1);
  for (qint64 candidate = 1;
       candidate < seek_limit && static_cast<int>(targets.size()) < options_.seeks;
       candidate += std::max<qint64>(1, seek_limit / (options_.seeks * 3 + 1))) {
    const qint64 distance = window_->FrameDistanceFromKeyframeForTest(candidate);
    if (distance >= 0 && distance <= 48) {
      targets.push_back(candidate);
    }
  }
  int seek_failures = 0;
  std::vector<qint64> seek_samples;
  std::vector<qint64> seek_back_samples;
  for (const qint64 target : targets) {
    QElapsedTimer timer;
    timer.start();
    window_->GoToFrameForTest(target);
    const bool ok = PumpUntil(
        [this, target]() {
          return window_->OnlineSeekIdleForTest() &&
                 window_->CurrentFrameForTest() == target;
        },
        3000, "cached_seek");
    seek_samples.push_back(timer.elapsed());
    if (!ok || window_->CurrentFrameForTest() != target) {
      ++seek_failures;
    }
  }
  Check(QStringLiteral("cached_seek_landing_zero"), seek_failures == 0,
        QStringLiteral("failures=%1 n=%2")
            .arg(seek_failures)
            .arg(targets.size()));
  const double seek_p95 = Percentile(seek_samples, 0.95);
  Check(QStringLiteral("cached_seek_p95_le_200ms"),
        seek_p95 >= 0.0 && seek_p95 <= 200.0,
        QStringLiteral("p95=%1").arg(seek_p95, 0, 'f', 1));
  std::printf("T8.11 online_index_seek n=%lld failures=%d p95=%.1f\n",
              static_cast<long long>(seek_samples.size()), seek_failures,
              seek_p95);
  std::fflush(stdout);

  // 未缓存 seek：下载未完成时目标超过稳定前缀，必须立即显示“缓冲中（目标帧 N）”，
  // 改回已缓存目标后旧等待被取消并精确落地。
  if (!window_->OnlineIndexCompleteForTest() &&
      window_->OnlineStableFramesForTest() + 300 < container_frames) {
    const qint64 uncached_target =
        std::min<qint64>(container_frames - 1,
                         window_->OnlineStableFramesForTest() + 300);
    QElapsedTimer wait_timer;
    wait_timer.start();
    window_->GoToFrameForTest(uncached_target);
    const qint64 wait_ui_ms = wait_timer.elapsed();
    const QString wait_status = window_->StatusMessageForTest();
    Check(QStringLiteral("uncached_seek_prompt_shown"),
          wait_status.contains(QStringLiteral("缓冲中（目标帧")) &&
              wait_status.contains(QString::number(uncached_target)),
          wait_status);
    Check(QStringLiteral("uncached_seek_prompt_le_100ms"), wait_ui_ms <= 100,
          QStringLiteral("ms=%1").arg(wait_ui_ms));
    QElapsedTimer cancel_timer;
    cancel_timer.start();
    const qint64 back_target = targets.empty() ? 0 : targets.back();
    window_->GoToFrameForTest(back_target);
    const qint64 cancel_ui_ms = cancel_timer.elapsed();
    Check(QStringLiteral("uncached_cancel_ui_le_100ms"), cancel_ui_ms <= 100,
          QStringLiteral("ms=%1").arg(cancel_ui_ms));
    const bool back = PumpUntil(
        [this, back_target]() {
          return window_->OnlineSeekIdleForTest() &&
                 window_->CurrentFrameForTest() == back_target;
        },
        5000, "back_cached");
    seek_back_samples.push_back(cancel_ui_ms);
    Check(QStringLiteral("uncached_cancel_back_exact"), back,
          QStringLiteral("frame=%1 target=%2")
              .arg(window_->CurrentFrameForTest())
              .arg(back_target));
    std::printf(
        "T8.11 online_index_uncached target=%lld ui_ms=%lld cancel_ui_ms=%lld "
        "back_ok=%d\n",
        static_cast<long long>(uncached_target),
        static_cast<long long>(wait_ui_ms),
        static_cast<long long>(cancel_ui_ms), back ? 1 : 0);
    std::fflush(stdout);
  } else {
    Check(QStringLiteral("uncached_seek_skipped_complete"), true,
          QStringLiteral("download complete before uncached test"));
    std::printf("T8.11 online_index_uncached skipped=complete_cache\n");
    std::fflush(stdout);
  }

  // 缓存完成：索引必须读到 EOF，总帧数切精确。
  const bool complete =
      PumpUntil([this]() { return window_->OnlineIndexCompleteForTest(); },
                std::max(options_.timeout_ms, 120000), "index_complete");
  Check(QStringLiteral("index_complete"), complete);
  const int exact_frames = window_->OnlineIndexFramesForTest();
  const QString exact_state = window_->OnlineTotalStateForTest();
  Check(QStringLiteral("total_state_exact_after_complete"),
        exact_state == QStringLiteral("exact:%1").arg(exact_frames),
        exact_state);
  Check(QStringLiteral("exact_frames_match_index"),
        exact_frames == window_->OnlineStableFramesForTest(),
        QStringLiteral("index=%1 stable=%2")
            .arg(exact_frames)
            .arg(window_->OnlineStableFramesForTest()));
  // CFR 样本容器估算与精确值允许 ±2 帧（OB-3 212.261s×25 会四舍五入到 5307）。
  Check(QStringLiteral("exact_frames_close_to_container"),
        container_frames > 0 && std::llabs(exact_frames - container_frames) <= 2,
        QStringLiteral("exact=%1 container=%2")
            .arg(exact_frames)
            .arg(container_frames));
  std::printf("T8.11 online_index_complete frames=%d container=%lld state=%s\n",
              exact_frames, static_cast<long long>(container_frames),
              exact_state.toUtf8().constData());
  std::fflush(stdout);

  // 增量统计：读字节/总字节应接近 1（不随 650ms 周期全量重扫）。
  const QString stats = window_->OnlineIndexStatsForTest();
  double read_bytes = 0.0;
  qint64 reopens = -1;
  qint64 steps = -1;
  for (const QString& part : stats.split(QLatin1Char(' '))) {
    const QStringList kv = part.split(QLatin1Char('='));
    if (kv.size() != 2) {
      continue;
    }
    if (kv[0] == QStringLiteral("read_bytes")) {
      read_bytes = kv[1].toDouble();
    } else if (kv[0] == QStringLiteral("reopens")) {
      reopens = kv[1].toLongLong();
    } else if (kv[0] == QStringLiteral("steps")) {
      steps = kv[1].toLongLong();
    }
  }
  const qint64 total_bytes = window_->OnlineTotalBytesForTest();
  const double read_ratio =
      total_bytes > 0 ? read_bytes / static_cast<double>(total_bytes) : -1.0;
  Check(QStringLiteral("incremental_no_full_rescan"),
        read_ratio > 0.0 && read_ratio <= 2.5,
        QStringLiteral("read_ratio=%1 read_bytes=%2 total=%3")
            .arg(read_ratio, 0, 'f', 3)
            .arg(static_cast<long long>(read_bytes))
            .arg(static_cast<long long>(total_bytes)));
  std::printf("T8.11 online_index_stats steps=%lld reopens=%lld %s "
              "read_ratio=%.3f\n",
              static_cast<long long>(steps), static_cast<long long>(reopens),
              stats.toUtf8().constData(), read_ratio);
  std::fflush(stdout);

  // 波形：完成后就绪；偏移 ≤1 帧。
  const bool waveform_ready = PumpUntil(
      [this]() {
        const QString state = window_->WaveformStateForTest();
        return state == QStringLiteral("ready") ||
               state.startsWith(QStringLiteral("running"));
      },
      30000, "waveform");
  Check(QStringLiteral("waveform_running_or_ready"), waveform_ready,
        window_->WaveformStateForTest());
  const int waveform_buckets = window_->OnlineWaveformBucketCountForTest();
  const double waveform_offset_frames =
      window_->OnlineWaveformOffsetFramesForTest();
  Check(QStringLiteral("waveform_buckets_cover_duration"),
        duration_s > 0.0 &&
            waveform_buckets >=
                static_cast<int>(duration_s * 200.0 * 0.95),
        QStringLiteral("buckets=%1 expected≈%2")
            .arg(waveform_buckets)
            .arg(static_cast<int>(duration_s * 200.0)));
  Check(QStringLiteral("waveform_offset_within_1_frame"),
        waveform_offset_frames >= 0.0 && waveform_offset_frames <= 1.0,
        QStringLiteral("offset_frames=%1").arg(waveform_offset_frames, 0, 'f', 3));
  std::printf("T8.11 online_index_waveform state=%s buckets=%d "
              "offset_frames=%.3f duration_s=%.3f\n",
              window_->WaveformStateForTest().toUtf8().constData(),
              waveform_buckets, waveform_offset_frames, duration_s);
  std::fflush(stdout);

  heartbeat.stop();
  heartbeat_max =
      std::max(heartbeat_max, heartbeat_clock.elapsed() - heartbeat_last);
  Check(QStringLiteral("ui_gap_le_200ms"), heartbeat_max <= 200,
        QStringLiteral("max_gap_ms=%1").arg(heartbeat_max));
  std::printf("T8.11 online_index_ui_gap max_ms=%lld\n",
              static_cast<long long>(heartbeat_max));
  std::fflush(stdout);

  QElapsedTimer close_timer;
  close_timer.start();
  const bool closed = window_->CloseForTest();
  const qint64 close_ms = close_timer.elapsed();
  Check(QStringLiteral("close"), closed);
  Check(QStringLiteral("close_le_1000ms"), close_ms <= 1000,
        QStringLiteral("ms=%1").arg(close_ms));
  std::printf("T8.11 online_index_close ms=%lld\n",
              static_cast<long long>(close_ms));
  std::fflush(stdout);
  lapian::online::ClearSecret(&secret.sessdata);
  Finish();
}

void OnlineIndexTestRunner::RunOfflineSlow() {
  const QString video_path =
      QFileInfo(options_.offline_video).absoluteFilePath();
  const QFileInfo video_info(video_path);
  if (!video_info.exists() || video_info.size() <= 0) {
    Check(QStringLiteral("offline_video_exists"), false, video_path);
    Finish();
    return;
  }
  const qint64 rate =
      options_.throttle_bytes_per_sec > 0 ? options_.throttle_bytes_per_sec
                                          : 2000000;
  const lapian::media::MediaProbeResult probe =
      lapian::media::ProbeMedia(video_path.toUtf8().constData());
  Check(QStringLiteral("offline_probe"), probe.ok,
        QString::fromStdString(probe.error_detail));
  const double fps = probe.info.video.frame_rate.ToDouble();
  const double duration_s = static_cast<double>(probe.info.duration_us) / 1.0e6;
  const qint64 total = video_info.size();
  std::printf("T8.11 offline_index begin path=%s bytes=%lld fps=%.3f "
              "duration_s=%.3f rate_bps=%lld\n",
              video_path.toUtf8().constData(), static_cast<long long>(total),
              fps, duration_s, static_cast<long long>(rate));
  std::fflush(stdout);
  if (!probe.ok || fps <= 0.0 || duration_s <= 0.0) {
    Finish();
    return;
  }

  // 完整文件的一次性索引作为“精确总帧数”基准（只用于验收对比）。
  auto full_source =
      std::make_shared<lapian::online::GrowingSource>(total);
  full_source->Publish(total);
  const lapian::media::OnlineIndexResult full =
      lapian::media::BuildOnlineIndex(full_source, video_path.toUtf8().constData(),
                                      probe.info.video.stream_index, 0, nullptr);
  Check(QStringLiteral("offline_full_index"), full.ok,
        QString::fromStdString(full.error));
  const int expected_frames = static_cast<int>(full.index.size());
  std::printf("T8.11 offline_full frames=%d complete=%d\n", expected_frames,
              full.complete ? 1 : 0);
  std::fflush(stdout);
  if (!full.ok || expected_frames <= 0) {
    Finish();
    return;
  }

  auto source = std::make_shared<lapian::online::GrowingSource>(total);
  SlowPublisher publisher(source, 512 * 1024, rate);
  publisher.Start();
  std::string create_error;
  auto builder = lapian::media::IncrementalOnlineIndexBuilder::Create(
      source, video_path.toUtf8().constData(), probe.info.video.stream_index, 48,
      &create_error);
  Check(QStringLiteral("offline_builder_open"), builder != nullptr,
        QString::fromStdString(create_error));
  if (!builder) {
    publisher.Stop();
    Finish();
    return;
  }

  QElapsedTimer wall;
  wall.start();
  qint64 first_index_ms = -1;
  qint64 stable30_ms = -1;
  qint64 published_at_30 = -1;
  qint64 read_at_30 = -1;
  int stable_at_30 = -1;
  int previous_stable = 0;
  int last_total = -1;
  int prefix_changes = 0;
  int stable_regressions = 0;
  int snapshots = 0;
  std::vector<std::int64_t> previous_prefix;
  lapian::media::OnlineIndexResult snapshot;
  while (wall.elapsed() < 180000) {
    const lapian::media::OnlineIndexStepInfo step =
        builder->Step(nullptr, 40, 8192);
    if (!step.ok) {
      Check(QStringLiteral("offline_step_ok"), false,
            QString::fromStdString(step.error));
      break;
    }
    if (builder->total_frames() != last_total) {
      lapian::media::OnlineIndexResult fresh;
      if (builder->Snapshot(&fresh)) {
        ++snapshots;
        if (first_index_ms < 0 && fresh.stable_frames > 0) {
          first_index_ms = wall.elapsed();
        }
        if (fresh.stable_frames < previous_stable) {
          ++stable_regressions;
        }
        const int compare = std::min(previous_stable, fresh.stable_frames);
        for (int i = 0; i < compare; ++i) {
          if (fresh.index.PtsOfFrame(i) != previous_prefix[static_cast<std::size_t>(i)]) {
            ++prefix_changes;
            break;
          }
        }
        previous_prefix.resize(static_cast<std::size_t>(fresh.stable_frames));
        for (int i = 0; i < fresh.stable_frames; ++i) {
          previous_prefix[static_cast<std::size_t>(i)] =
              fresh.index.PtsOfFrame(i);
        }
        previous_stable = fresh.stable_frames;
        last_total = builder->total_frames();
        snapshot = std::move(fresh);
        const int expected_30 = static_cast<int>(std::llround(30.0 * fps));
        if (stable30_ms < 0 && snapshot.stable_frames >= expected_30) {
          stable30_ms = wall.elapsed();
          stable_at_30 = snapshot.stable_frames;
          published_at_30 = static_cast<qint64>(source->Published());
          read_at_30 = builder->stats().read_bytes;
          std::printf(
              "T8.11 offline_stable30 ms=%lld published=%lld total=%lld "
              "read_bytes=%lld lag_bytes=%lld stable=%d expected30=%d\n",
              static_cast<long long>(stable30_ms),
              static_cast<long long>(published_at_30),
              static_cast<long long>(total),
              static_cast<long long>(read_at_30),
              static_cast<long long>(published_at_30 - read_at_30),
              stable_at_30, expected_30);
          std::fflush(stdout);
        }
      }
    }
    if (step.complete) {
      break;
    }
    if (step.at_frontier) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  publisher.Stop();
  if (!builder->Snapshot(&snapshot)) {
    Check(QStringLiteral("offline_final_snapshot"), false,
          QString::fromStdString(builder->last_error()));
    Finish();
    return;
  }
  const lapian::media::OnlineIndexStats stats = builder->stats();
  const double read_ratio =
      static_cast<double>(stats.read_bytes) / static_cast<double>(total);
  const int expected_30 = static_cast<int>(std::llround(30.0 * fps));
  std::printf(
      "T8.11 offline_index first_index_ms=%lld stable30_ms=%lld "
      "published_at_30=%lld read_at_30=%lld stable_at_30=%d expected30=%d "
      "stable=%d expected=%d snapshots=%d prefix_changes=%d "
      "stable_regressions=%d steps=%lld reopens=%lld read_bytes=%lld "
      "read_ratio=%.3f max_step_ms=%.2f complete=%d\n",
      static_cast<long long>(first_index_ms),
      static_cast<long long>(stable30_ms),
      static_cast<long long>(published_at_30),
      static_cast<long long>(read_at_30), stable_at_30, expected_30,
      snapshot.stable_frames, expected_frames, snapshots, prefix_changes,
      stable_regressions, static_cast<long long>(stats.steps),
      static_cast<long long>(stats.reopens),
      static_cast<long long>(stats.read_bytes), read_ratio,
      stats.max_step_ms, snapshot.complete ? 1 : 0);
  std::fflush(stdout);

  Check(QStringLiteral("offline_complete"), snapshot.complete);
  Check(QStringLiteral("offline_exact_total"), snapshot.stable_frames == expected_frames,
        QStringLiteral("stable=%1 expected=%2")
            .arg(snapshot.stable_frames)
            .arg(expected_frames));
  Check(QStringLiteral("offline_stable_monotonic"), stable_regressions == 0,
        QStringLiteral("regressions=%1").arg(stable_regressions));
  Check(QStringLiteral("offline_prefix_stable"), prefix_changes == 0,
        QStringLiteral("changes=%1").arg(prefix_changes));
  Check(QStringLiteral("offline_index_30s_before_complete"),
        stable30_ms >= 0 && published_at_30 > 0 && published_at_30 < total,
        QStringLiteral("published_at_30=%1 total=%2")
            .arg(published_at_30)
            .arg(total));
  Check(QStringLiteral("offline_index_keeps_up_with_publish"),
        read_at_30 > 0 && published_at_30 - read_at_30 <= 512 * 1024,
        QStringLiteral("lag_bytes=%1").arg(published_at_30 - read_at_30));
  Check(QStringLiteral("offline_stable30"), stable_at_30 >= expected_30,
        QStringLiteral("stable_at_30=%1 expected30=%2")
            .arg(stable_at_30)
            .arg(expected_30));

  // 对照：T8.10 旧路径“每次从文件起点重建到发布前沿”在 30s 前缀上的单次
  // 耗时/读字节；新路径同一时刻的 Step 最大耗时见 stats.max_step_ms。
  if (published_at_30 > 0) {
    auto rebuild_source =
        std::make_shared<lapian::online::GrowingSource>(total);
    rebuild_source->Publish(published_at_30);
    QElapsedTimer rebuild_timer;
    rebuild_timer.start();
    const lapian::media::OnlineIndexResult rebuild =
        lapian::media::BuildOnlineIndex(
            rebuild_source, video_path.toUtf8().constData(),
            probe.info.video.stream_index, 48, nullptr);
    const qint64 rebuild_ms = rebuild_timer.elapsed();
    std::printf(
        "T8.11 offline_full_rebuild_at_30s ms=%lld frames=%d prefix=%lld "
        "incremental_max_step_ms=%.2f\n",
        static_cast<long long>(rebuild_ms), rebuild.stable_frames,
        static_cast<long long>(published_at_30), stats.max_step_ms);
    std::fflush(stdout);
    Check(QStringLiteral("offline_full_rebuild_slower_than_step"),
          rebuild_ms > 0 &&
              stats.max_step_ms < static_cast<double>(rebuild_ms),
          QStringLiteral("rebuild_ms=%1 step_ms=%2")
              .arg(rebuild_ms)
              .arg(stats.max_step_ms, 0, 'f', 2));
  }
  Check(QStringLiteral("offline_incremental_no_full_rescan"),
        read_ratio > 0.0 && read_ratio <= 2.0,
        QStringLiteral("read_ratio=%1").arg(read_ratio, 0, 'f', 3));
  Check(QStringLiteral("offline_step_bound"), stats.max_step_ms <= 200.0,
        QStringLiteral("max_step_ms=%1").arg(stats.max_step_ms, 0, 'f', 2));

  // 波形渐进：独立音频源按同一速率增长，只追加 bucket、不重置。
  if (!options_.offline_audio.isEmpty()) {
    const QString audio_path =
        QFileInfo(options_.offline_audio).absoluteFilePath();
    const QFileInfo audio_info(audio_path);
    if (!audio_info.exists() || audio_info.size() <= 0) {
      Check(QStringLiteral("offline_audio_exists"), false, audio_path);
    } else {
      lapian::audio::AudioFormat audio_format;
      audio_format.sample_rate = 48000;
      audio_format.channels = 1;
      lapian::audio::AudioDecoder audio_decoder;
      std::string audio_error;
      const bool audio_opened = audio_decoder.Open(
          audio_path.toUtf8().constData(), -1, audio_format, &audio_error);
      Check(QStringLiteral("offline_audio_probe"), audio_opened,
            QString::fromStdString(audio_error));
      const int audio_stream_index = audio_decoder.info().stream_index;
      const double audio_duration_s =
          static_cast<double>(audio_decoder.info().duration_us) / 1.0e6;
      audio_decoder.Close();
      std::int64_t video_start_us = 0;
      {
        lapian::media::VideoDecoder video_decoder;
        std::string decode_error;
        if (video_decoder.Open(video_path.toUtf8().constData(),
                               probe.info.video.stream_index, false,
                               &decode_error)) {
          lapian::media::DecodedFrameInfo first_frame;
          if (video_decoder.DecodeFirstFrame(&first_frame, &decode_error)) {
            video_start_us = first_frame.pts_us;
          }
          video_decoder.Close();
        }
      }
      const double offset_seconds =
          -static_cast<double>(video_start_us) / 1.0e6;
      auto audio_source = std::make_shared<lapian::online::GrowingSource>(
          audio_info.size());
      SlowPublisher audio_publisher(audio_source, 128 * 1024, rate);
      audio_publisher.Start();
      lapian::audio::WaveformGenerator generator;
      const bool started = generator.StartGrowing(
          audio_source, audio_path.toUtf8().constData(), audio_stream_index,
          offset_seconds);
      Check(QStringLiteral("offline_waveform_started"), started);
      if (started) {
        std::vector<int> bucket_counts;
        std::vector<lapian::audio::WaveformBucket> early_buckets;
        bool captured_early = false;
        QElapsedTimer waveform_wall;
        waveform_wall.start();
        while (waveform_wall.elapsed() < 60000) {
          const auto data = generator.data();
          const int count = data ? static_cast<int>(data->bucket_count()) : 0;
          if (bucket_counts.empty() || bucket_counts.back() != count) {
            bucket_counts.push_back(count);
            if (!captured_early && count > 2000) {
              data->CopyRange(0, 2000, &early_buckets);
              captured_early = true;
            }
          }
          const lapian::audio::WaveformStatus status = generator.status();
          if (status.state == lapian::audio::WaveformState::kReady ||
              status.state == lapian::audio::WaveformState::kFailed) {
            break;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        audio_publisher.Stop();
        // 等 ready（发布完后线程读到 EOF）。
        QElapsedTimer ready_wait;
        ready_wait.start();
        while (ready_wait.elapsed() < 15000) {
          if (generator.status().state == lapian::audio::WaveformState::kReady) {
            break;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        const auto data = generator.data();
        const int final_count =
            data ? static_cast<int>(data->bucket_count()) : 0;
        bool monotonic = true;
        int intermediate = 0;
        for (std::size_t i = 1; i < bucket_counts.size(); ++i) {
          if (bucket_counts[i] < bucket_counts[i - 1]) {
            monotonic = false;
          }
          if (bucket_counts[i] > 0) {
            ++intermediate;
          }
        }
        std::vector<lapian::audio::WaveformBucket> late_early;
        if (captured_early && data) {
          data->CopyRange(0, 2000, &late_early);
        }
        bool prefix_unchanged =
            captured_early && early_buckets.size() == late_early.size();
        if (prefix_unchanged) {
          for (std::size_t i = 0; i < early_buckets.size(); ++i) {
            if (early_buckets[i].min != late_early[i].min ||
                early_buckets[i].max != late_early[i].max) {
              prefix_unchanged = false;
              break;
            }
          }
        }
        const double offset_frames =
            std::abs(offset_seconds) * fps;
        const int expected_buckets =
            static_cast<int>(audio_duration_s * 200.0);
        std::printf(
            "T8.11 offline_waveform state=%d counts=%zu final=%d expected=%d "
            "early_prefix_unchanged=%d monotonic=%d intermediate=%d "
            "offset_frames=%.3f offset_s=%.3f\n",
            static_cast<int>(generator.status().state), bucket_counts.size(),
            final_count, expected_buckets, prefix_unchanged ? 1 : 0,
            monotonic ? 1 : 0, intermediate, offset_frames, offset_seconds);
        std::fflush(stdout);
        Check(QStringLiteral("offline_waveform_ready"),
              generator.status().state == lapian::audio::WaveformState::kReady);
        Check(QStringLiteral("offline_waveform_progressive"),
              bucket_counts.size() >= 3 && intermediate >= 2,
              QStringLiteral("samples=%1 intermediate=%2")
                  .arg(bucket_counts.size())
                  .arg(intermediate));
        Check(QStringLiteral("offline_waveform_monotonic"), monotonic);
        Check(QStringLiteral("offline_waveform_prefix_unchanged"),
              prefix_unchanged);
        Check(QStringLiteral("offline_waveform_covers_duration"),
              final_count >= static_cast<int>(expected_buckets * 0.97),
              QStringLiteral("final=%1 expected=%2")
                  .arg(final_count)
                  .arg(expected_buckets));
        Check(QStringLiteral("offline_waveform_offset_within_1_frame"),
              offset_frames <= 1.0,
              QStringLiteral("offset_frames=%1").arg(offset_frames, 0, 'f', 3));
        const double waveform_end_seconds =
            static_cast<double>(final_count) / 200.0 + offset_seconds;
        Check(QStringLiteral("offline_waveform_end_within_1_frame"),
              std::abs(waveform_end_seconds - audio_duration_s) <= 1.0 / fps,
              QStringLiteral("end=%1 duration=%2 frame=%3")
                  .arg(waveform_end_seconds, 0, 'f', 3)
                  .arg(audio_duration_s, 0, 'f', 3)
                  .arg(1.0 / fps, 0, 'f', 3));
        generator.Cancel();
      }
    }
  }
  Finish();
}

}  // namespace lapian::app
