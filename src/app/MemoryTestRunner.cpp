#include "app/MemoryTestRunner.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QSize>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <utility>

#include "app/MainWindow.h"
#include "app/ProcessMemory.h"

namespace lapian::app {
namespace {

constexpr std::size_t kMiB = 1024 * 1024;
constexpr double kBytesPerMiB = 1024.0 * 1024.0;

// T7.3：心跳 200ms；10s 无心跳只记录，30s 仍无心跳直接失败退出。
constexpr int kSoakHeartbeatMs = 200;
constexpr int kSoakWatchdogPollMs = 1000;
constexpr std::int64_t kSoakStallLogMs = 10000;
constexpr std::int64_t kSoakHardStallMs = 30000;

std::string Utf8(const QString& text) { return text.toUtf8().toStdString(); }

std::int64_t SteadyNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

MemoryTestRunner::MemoryTestRunner(MainWindow* window, QString mode, QString tag,
                                   int seconds, int steps, int switch_cycles,
                                   int interval_seconds, bool soak_loop,
                                   QString file_a, QString file_b, QObject* parent)
    : QObject(parent == nullptr ? window : parent),
      window_(window),
      mode_text_(std::move(mode)),
      tag_(std::move(tag)),
      seconds_(seconds > 0 ? seconds : 60),
      steps_(steps > 0 ? steps : 40),
      switch_cycles_(switch_cycles > 0 ? switch_cycles : 3),
      interval_seconds_(interval_seconds > 0 ? interval_seconds : 30),
      soak_loop_(soak_loop),
      file_a_(std::move(file_a)),
      file_b_(std::move(file_b)) {
  if (!ParseMode(mode_text_)) {
    mode_ = Mode::kPause;
  }
}

MemoryTestRunner::~MemoryTestRunner() { StopSoakMonitoring(); }

void MemoryTestRunner::Start() {
  QTimer::singleShot(0, this, &MemoryTestRunner::Begin);
}

bool MemoryTestRunner::ParseMode(const QString& text) {
  if (text == QStringLiteral("empty")) {
    mode_ = Mode::kEmpty;
    return true;
  }
  if (text == QStringLiteral("pause")) {
    mode_ = Mode::kPause;
    return true;
  }
  if (text == QStringLiteral("play")) {
    mode_ = Mode::kPlay;
    return true;
  }
  if (text == QStringLiteral("step")) {
    mode_ = Mode::kStep;
    return true;
  }
  if (text == QStringLiteral("switch")) {
    mode_ = Mode::kSwitch;
    return true;
  }
  if (text == QStringLiteral("soak")) {
    mode_ = Mode::kSoak;
    return true;
  }
  return false;
}

void MemoryTestRunner::Begin() {
  // 打开失败会弹模态框；测量模式自动关掉，避免挂死。
  modal_watchdog_ = new QTimer(this);
  modal_watchdog_->setInterval(250);
  connect(modal_watchdog_, &QTimer::timeout, this, []() {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (box != nullptr) {
      box->done(QMessageBox::Cancel);
    }
  });
  modal_watchdog_->start();

  if (mode_ == Mode::kSoak) {
    std::fprintf(stderr,
                 "T7.3 soak_begin mode=%s tag=%s seconds=%d interval_s=%d\n",
                 mode_text_.toUtf8().constData(), tag_.toUtf8().constData(),
                 seconds_, interval_seconds_);
    std::fflush(stderr);
    StartSoakMonitoring();
    if (failed_) {
      return;
    }
  }

  std::fprintf(stderr, "T7.2 memory_begin mode=%s tag=%s seconds=%d\n",
               mode_text_.toUtf8().constData(), tag_.toUtf8().constData(), seconds_);
  std::fflush(stderr);

  if (mode_ == Mode::kEmpty) {
    phase_ = QStringLiteral("empty");
    phase_timer_.start();
    Sample(QStringLiteral("start"));
    ScheduleMilestones(seconds_, [this]() { SummaryAndExit(); });
    return;
  }
  if (mode_ == Mode::kSwitch && file_b_.isEmpty()) {
    Fail(QStringLiteral("switch 模式需要 --file-b"));
    return;
  }
  if (file_a_.isEmpty()) {
    Fail(QStringLiteral("缺少 --file"));
    return;
  }
  phase_ = QStringLiteral("a");
  OpenAndWait(file_a_, QStringLiteral("a"), [this]() { OnReady(); });
}

void MemoryTestRunner::OpenAndWait(const QString& path, const QString& phase,
                                   std::function<void()> next) {
  phase_ = phase;
  open_poll_count_ = 0;
  const QString expected_name = QFileInfo(path).fileName();
  window_->OpenFile(path);
  PollOpen(expected_name, std::move(next));
}

void MemoryTestRunner::PollOpen(const QString& expected_name,
                                std::function<void()> next) {
  if (failed_) {
    return;
  }
  if (!window_->HasMediaForTest() ||
      window_->InfoFileNameForTest() != expected_name) {
    if (++open_poll_count_ > 100) {
      Fail(QStringLiteral("打开失败或超时：") + expected_name);
      return;
    }
    QTimer::singleShot(50, this, [this, expected_name, next]() {
      PollOpen(expected_name, next);
    });
    return;
  }
  if (!window_->IndexReadyForTest()) {
    // V10 这类 2h 文件的完整索引可能超过 15s；soak 放宽到 120s。
    const int index_poll_limit = mode_ == Mode::kSoak ? 2400 : 300;
    if (++open_poll_count_ > index_poll_limit) {
      Fail(QStringLiteral("索引超时：") + expected_name);
      return;
    }
    QTimer::singleShot(50, this, [this, expected_name, next]() {
      PollOpen(expected_name, next);
    });
    return;
  }
  phase_timer_.restart();
  Sample(phase_ + QStringLiteral("-steady"));
  next();
}

void MemoryTestRunner::OnReady() {
  phase_timer_.restart();
  Sample(QStringLiteral("steady-start"));

  if (mode_ == Mode::kSoak) {
    // 有限素材用整片 A/B 循环模拟“持续播放”；V10 用 --memory-no-loop
    // 做真正的单文件连续播放。报告中必须区分两者。
    const qint64 last = window_->LastFrameIndexForTest();
    window_->GoToInputForTest(QStringLiteral("0"));
    if (soak_loop_) {
      window_->SetLoopInForTest();
      window_->GoToInputForTest(QString::number(last));
      window_->SetLoopOutForTest();
      window_->ToggleLoopForTest();
      window_->GoToInputForTest(QStringLiteral("0"));
    }
    const QString loop_state = window_->LoopStateForTest();
    std::fprintf(stderr,
                 "T7.3 soak_setup tag=%s frames=%lld loop_enabled=%d loop=%s "
                 "rate=%.2f\n",
                 tag_.toUtf8().constData(), static_cast<long long>(last),
                 soak_loop_ ? 1 : 0, Utf8(loop_state).c_str(),
                 window_->SpeedForTest());
    std::fflush(stderr);
    if (soak_loop_ && !loop_state.contains(QStringLiteral("loop=1"))) {
      Fail(QStringLiteral("soak 循环设置失败：") + loop_state);
      return;
    }
    if (!soak_loop_ && loop_state.contains(QStringLiteral("loop=1"))) {
      Fail(QStringLiteral("soak 无循环设置失败：") + loop_state);
      return;
    }

    soak_timer_.restart();
    soak_collecting_ = true;
    max_ui_gap_ms_ = 0.0;
    window_->StartPlaybackForTest();
    Sample(QStringLiteral("soak-start"));
    ScheduleSoak();
    return;
  }

  if (mode_ == Mode::kPlay) {
    window_->StartPlaybackForTest();
    Sample(QStringLiteral("play-start"));
    ScheduleMilestones(seconds_, [this]() { SummaryAndExit(); });
    return;
  }

  if (mode_ == Mode::kStep) {
    const qint64 last = window_->LastFrameIndexForTest();
    const qint64 start = std::min<qint64>(300, std::max<qint64>(0, last));
    window_->GoToInputForTest(QString::number(start));
    Sample(QStringLiteral("step-start"));
    RunSteps(true, steps_, [this]() {
      Sample(QStringLiteral("after-backward"));
      RunSteps(false, steps_, [this]() {
        Sample(QStringLiteral("after-forward"));
        SummaryAndExit();
      });
    });
    return;
  }

  if (mode_ == Mode::kSwitch) {
    auto cycle = std::make_shared<std::function<void()>>();
    *cycle = [this, cycle]() {
      // 一个“cycle”= A→B→A 两次打开；cycles=3 时共 6 次打开，最终回到 A。
      if (switch_done_ >= switch_cycles_ * 2) {
        Sample(QStringLiteral("switch-final"));
        SummaryAndExit();
        return;
      }
      ++switch_done_;
      const bool to_b = (switch_done_ % 2 == 1);
      const QString phase =
          (to_b ? QStringLiteral("b-") : QStringLiteral("a-")) +
          QString::number(switch_done_);
      OpenAndWait(to_b ? file_b_ : file_a_, phase, [cycle]() { (*cycle)(); });
    };
    (*cycle)();
    return;
  }

  // pause：索引就绪后保持暂停，观察 60s 稳态。
  ScheduleMilestones(seconds_, [this]() { SummaryAndExit(); });
}

void MemoryTestRunner::ScheduleMilestones(int seconds, std::function<void()> done) {
  const int milestones[] = {5, 10, 20, 30, 45, 60};
  for (int at : milestones) {
    if (at >= seconds) {
      break;
    }
    QTimer::singleShot(at * 1000, this, [this, at]() {
      Sample(QStringLiteral("t%1").arg(at));
    });
  }
  QTimer::singleShot(seconds * 1000, this, [this, done]() {
    if (window_->IsPlayingForTest()) {
      window_->TogglePlaybackForTest();
    }
    Sample(QStringLiteral("final"));
    done();
  });
}

void MemoryTestRunner::ScheduleSoak() {
  auto tick = std::make_shared<std::function<void()>>();
  *tick = [this, tick]() {
    if (soak_finished_ || failed_) {
      return;
    }
    const int elapsed = static_cast<int>(soak_timer_.elapsed() / 1000);
    Sample(QStringLiteral("soak-t%1").arg(elapsed));
    if (elapsed >= seconds_) {
      FinishSoak();
      return;
    }
    // 用剩余时长收敛最后一次采样，避免 QTimer 链式漂移把 60s 跑成 70s。
    const int delay_s = std::min(interval_seconds_, seconds_ - elapsed);
    QTimer::singleShot(delay_s * 1000, this, [tick]() { (*tick)(); });
  };
  QTimer::singleShot(std::min(interval_seconds_, seconds_) * 1000, this,
                     [tick]() { (*tick)(); });
}

double MemoryTestRunner::WarmupSeconds() const {
  // 2h 浸泡去掉前 60s（波形/索引/首帧后的堆高水位）；短时探针按 10% 缩放。
  return std::min(60.0, std::max(5.0, static_cast<double>(seconds_) * 0.1));
}

void MemoryTestRunner::FinishSoak() {
  if (soak_finished_) {
    return;
  }
  Sample(QStringLiteral("soak-final"));
  soak_finished_ = true;
  soak_collecting_ = false;
  if (window_->IsPlayingForTest()) {
    window_->TogglePlaybackForTest();
  }
  StopSoakMonitoring();

  SoakThresholds thresholds;
  thresholds.memory_limit_mb = static_cast<double>(LimitMiBForMode());
  const double warmup_s = WarmupSeconds();
  if (!AnalyzeSoak(soak_samples_, warmup_s, thresholds, &soak_analysis_)) {
    Fail(QStringLiteral("soak 样本不足"));
    return;
  }
  const SoakAnalysis& a = soak_analysis_;
  const QSize display = window_->SnapshotDisplaySizeForTest();
  std::string flags;
  auto add_flag = [&flags](const char* flag) {
    if (!flags.empty()) {
      flags += ",";
    }
    flags += flag;
  };
  if (a.memory_over_limit) {
    add_flag("memory_over_limit");
  }
  if (a.memory_growth) {
    add_flag("memory_growth");
  }
  if (a.handle_growth) {
    add_flag("handle_growth");
  }
  if (a.thread_growth) {
    add_flag("thread_growth");
  }
  if (a.cache_nonempty) {
    add_flag("cache_nonempty");
  }
  if (a.playback_stopped) {
    add_flag("playback_stopped");
  }
  if (a.frame_stall) {
    add_flag("frame_stall");
  }
  if (a.ui_stall) {
    add_flag("ui_stall");
  }
  if (flags.empty()) {
    flags = "none";
  }

  std::fprintf(
      stderr,
      "T7.3 soak_summary mode=soak tag=%s soak_loop=%d display=%dx%d "
      "duration_s=%.1f samples=%zu steady=%zu warmup_s=%.0f limit_mb=%.0f "
      "pass=%d ws_start_mb=%.1f ws_final_mb=%.1f ws_max_mb=%.1f growth_mb=%.1f "
      "slope_mb_per_h=%.2f baseline_blocks=%zu baseline_growth_mb=%.1f "
      "baseline_slope_mb_per_h=%.2f private_max_mb=%.1f "
      "handles=%u/%u/%u threads=%u/%u/%u audio_underruns=%llu "
      "loop_jumps=%d cache_max=%d frame_stall_events=%zu "
      "playback_stopped_events=%zu ui_gap_max_ms=%.0f flags=%s\n",
      tag_.toUtf8().constData(), soak_loop_ ? 1 : 0, display.width(),
      display.height(), a.duration_s, a.total_samples, a.steady_samples,
      warmup_s, thresholds.memory_limit_mb, a.passed ? 1 : 0, a.ws_start_mb,
      a.ws_final_mb, a.ws_max_mb, a.ws_growth_mb, a.ws_slope_mb_per_hour,
      a.baseline_blocks, a.baseline_growth_mb, a.baseline_slope_mb_per_hour,
      a.private_max_mb, a.handles_start, a.handles_end, a.handles_max,
      a.threads_start, a.threads_end, a.threads_max,
      static_cast<unsigned long long>(a.audio_underruns_end), a.loop_jumps_end,
      a.cache_max_entries, a.frame_stall_events, a.playback_stopped_events,
      a.ui_gap_max_ms, flags.c_str());
  std::fflush(stderr);

  if (modal_watchdog_ != nullptr) {
    modal_watchdog_->stop();
  }
  QCoreApplication::exit(a.passed ? 0 : 3);
}

void MemoryTestRunner::Sample(const QString& phase) {
  ProcessMemorySample sample;
  if (!SampleProcessMemory(&sample) || !sample.valid) {
    std::fprintf(stderr, "T7.2 memory phase=%s invalid\n",
                 phase.toUtf8().constData());
    std::fflush(stderr);
    return;
  }
  max_ws_bytes_ = std::max(max_ws_bytes_, sample.working_set_bytes);
  max_private_bytes_ = std::max(max_private_bytes_, sample.private_bytes);
  last_ws_bytes_ = sample.working_set_bytes;
  last_private_bytes_ = sample.private_bytes;
  const double t_s = phase_timer_.isValid()
                         ? static_cast<double>(phase_timer_.elapsed()) / 1000.0
                         : 0.0;
  const std::string cache = Utf8(window_->FrameCacheStateForTest());
  std::fprintf(stderr,
               "T7.2 memory mode=%s tag=%s phase=%s t_s=%.1f ws_mb=%.1f "
               "private_mb=%.1f peak_ws_mb=%.1f handles=%u threads=%u "
               "playing=%d cache=%s\n",
               mode_text_.toUtf8().constData(), tag_.toUtf8().constData(),
               phase.toUtf8().constData(), t_s,
               static_cast<double>(sample.working_set_bytes) / kBytesPerMiB,
               static_cast<double>(sample.private_bytes) / kBytesPerMiB,
               static_cast<double>(sample.peak_working_set_bytes) / kBytesPerMiB,
               sample.handle_count, sample.thread_count,
               window_->IsPlayingForTest() ? 1 : 0, cache.c_str());
  std::fflush(stderr);

  if (mode_ != Mode::kSoak || !soak_collecting_ || soak_finished_) {
    return;
  }
  SoakSample soak;
  soak.t_s = static_cast<double>(soak_timer_.elapsed()) / 1000.0;
  soak.ws_mb = static_cast<double>(sample.working_set_bytes) / kBytesPerMiB;
  soak.private_mb = static_cast<double>(sample.private_bytes) / kBytesPerMiB;
  soak.peak_ws_mb =
      static_cast<double>(sample.peak_working_set_bytes) / kBytesPerMiB;
  soak.handles = sample.handle_count;
  soak.threads = sample.thread_count;
  soak.frame = window_->CurrentFrameForTest();
  soak.playing = window_->IsPlayingForTest();
  soak.loop_jumps = window_->LoopJumpCountForTest();
  soak.audio_underruns = window_->AudioUnderrunCountForTest();
  soak.audio_state = Utf8(window_->AudioStateForTest());
  soak.cache_entries = window_->FrameCacheEntryCountForTest();
  soak.ui_gap_max_ms = max_ui_gap_ms_;
  max_ui_gap_ms_ = 0.0;
  soak_samples_.push_back(soak);

  std::fprintf(stderr,
               "T7.3 soak_sample tag=%s phase=%s t_s=%.1f ws_mb=%.1f "
               "private_mb=%.1f handles=%u threads=%u frame=%lld playing=%d "
               "loop_jumps=%d audio_underruns=%llu audio_state=%s waveform=%s "
               "cache_entries=%d ui_gap_ms=%.0f\n",
               tag_.toUtf8().constData(), phase.toUtf8().constData(), soak.t_s,
               soak.ws_mb, soak.private_mb, soak.handles, soak.threads,
               static_cast<long long>(soak.frame), soak.playing ? 1 : 0,
               soak.loop_jumps,
               static_cast<unsigned long long>(soak.audio_underruns),
               soak.audio_state.c_str(),
               Utf8(window_->WaveformStateForTest()).c_str(),
               soak.cache_entries, soak.ui_gap_max_ms);
  std::fflush(stderr);
}

void MemoryTestRunner::StartSoakMonitoring() {
  soak_timer_.start();
  heartbeat_ms_.store(SteadyNowMs());
  watchdog_stall_logged_.store(false);
  watchdog_stop_.store(false);

  heartbeat_timer_ = new QTimer(this);
  heartbeat_timer_->setInterval(kSoakHeartbeatMs);
  connect(heartbeat_timer_, &QTimer::timeout, this, [this]() {
    const std::int64_t now = SteadyNowMs();
    const std::int64_t previous = heartbeat_ms_.exchange(now);
    if (!soak_collecting_ || previous <= 0) {
      return;
    }
    max_ui_gap_ms_ =
        std::max(max_ui_gap_ms_, static_cast<double>(now - previous));
  });
  heartbeat_timer_->start();

  try {
    watchdog_thread_ = std::thread([this]() { WatchdogLoop(); });
  } catch (const std::exception& exception) {
    Fail(QStringLiteral("看门狗线程启动失败：") +
         QString::fromUtf8(exception.what()));
  }
}

void MemoryTestRunner::StopSoakMonitoring() {
  if (heartbeat_timer_ != nullptr) {
    heartbeat_timer_->stop();
  }
  watchdog_stop_.store(true);
  if (watchdog_thread_.joinable()) {
    watchdog_thread_.join();
  }
}

void MemoryTestRunner::WatchdogLoop() {
  while (!watchdog_stop_.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kSoakWatchdogPollMs));
    if (watchdog_stop_.load(std::memory_order_relaxed)) {
      break;
    }
    const std::int64_t gap = SteadyNowMs() - heartbeat_ms_.load();
    if (gap > kSoakStallLogMs &&
        !watchdog_stall_logged_.exchange(true, std::memory_order_relaxed)) {
      std::fprintf(stderr, "T7.3 soak_watchdog stall_ms=%lld\n",
                   static_cast<long long>(gap));
      std::fflush(stderr);
    }
    if (gap > kSoakHardStallMs) {
      std::fprintf(stderr, "T7.3 soak_watchdog hard_stall_ms=%lld\n",
                   static_cast<long long>(gap));
      std::fflush(stderr);
      // UI 线程 30s 无心跳属于硬卡死；直接失败，避免 2h 浸泡挂到外部超时。
      _Exit(6);
    }
  }
}

void MemoryTestRunner::RunSteps(bool backward, int count, std::function<void()> done) {
  window_->ClearFocusForTest();
  window_->FocusVideoForTest();
  auto remaining = std::make_shared<int>(count);
  auto samples = std::make_shared<std::vector<double>>();
  samples->reserve(static_cast<std::size_t>(count));
  auto tick = std::make_shared<std::function<void()>>();
  *tick = [this, backward, remaining, samples, tick, done, count]() {
    if (*remaining <= 0) {
      std::vector<double> sorted = *samples;
      std::sort(sorted.begin(), sorted.end());
      const double p50 = sorted.empty() ? 0.0 : sorted[sorted.size() / 2];
      const std::size_t p95_index =
          sorted.empty() ? 0 : static_cast<std::size_t>(
                                     static_cast<double>(sorted.size() - 1) * 0.95);
      const double p95 = sorted.empty() ? 0.0 : sorted[p95_index];
      const double max_ms = sorted.empty() ? 0.0 : sorted.back();
      std::fprintf(stderr,
                   "T7.2 step_stats direction=%s n=%zu p50_ms=%.2f p95_ms=%.2f "
                   "max_ms=%.2f cache=%s\n",
                   backward ? "backward" : "forward", sorted.size(), p50, p95,
                   max_ms, Utf8(window_->FrameCacheStateForTest()).c_str());
      std::fflush(stderr);
      done();
      return;
    }
    QElapsedTimer timer;
    timer.start();
    window_->PressKeyForTest(backward ? Qt::Key_PageUp : Qt::Key_PageDown, 0);
    samples->push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
    --(*remaining);
    const int done_steps = count - *remaining;
    if (done_steps % 20 == 0) {
      Sample(QStringLiteral("step-%1").arg(done_steps));
    }
    QTimer::singleShot(0, this, [tick]() { (*tick)(); });
  };
  (*tick)();
}

int MemoryTestRunner::LimitMiBForMode() const {
  const QSize display = window_->SnapshotDisplaySizeForTest();
  const bool is_4k = display.width() >= 3840 || display.height() >= 2160;
  if (mode_ == Mode::kEmpty) {
    return 250;  // P10 空态基线
  }
  if (mode_ == Mode::kPause) {
    // P10 的口径是 V1（1080p）打开暂停；4K 暂停不是 P10 断言。
    return is_4k ? 0 : 250;
  }
  if (mode_ == Mode::kSoak) {
    // P14 是 2h 1080p ≤1.2GB；4K 浸泡只做诊断，放宽到 P11 的 1.5GB。
    return is_4k ? 1536 : 1200;
  }
  return is_4k ? 1536 : 800;
}

void MemoryTestRunner::SummaryAndExit() {
  StopSoakMonitoring();
  const int limit_mb = LimitMiBForMode();
  const bool pass =
      limit_mb <= 0 || max_ws_bytes_ <= static_cast<std::uint64_t>(limit_mb) * kMiB;
  const QSize display = window_->SnapshotDisplaySizeForTest();
  std::fprintf(stderr,
               "T7.2 memory_summary mode=%s tag=%s display=%dx%d limit_mb=%d "
               "pass=%d max_ws_mb=%.1f max_private_mb=%.1f final_ws_mb=%.1f "
               "final_private_mb=%.1f cache=%s\n",
               mode_text_.toUtf8().constData(), tag_.toUtf8().constData(),
               display.width(), display.height(), limit_mb, pass ? 1 : 0,
               static_cast<double>(max_ws_bytes_) / kBytesPerMiB,
               static_cast<double>(max_private_bytes_) / kBytesPerMiB,
               static_cast<double>(last_ws_bytes_) / kBytesPerMiB,
               static_cast<double>(last_private_bytes_) / kBytesPerMiB,
               Utf8(window_->FrameCacheStateForTest()).c_str());
  std::fflush(stderr);
  if (modal_watchdog_ != nullptr) {
    modal_watchdog_->stop();
  }
  QCoreApplication::exit(0);
}

void MemoryTestRunner::Fail(const QString& reason) {
  failed_ = true;
  std::fprintf(stderr, "T7.2 memory_fail mode=%s tag=%s reason=%s\n",
               mode_text_.toUtf8().constData(), tag_.toUtf8().constData(),
               reason.toUtf8().constData());
  std::fflush(stderr);
  StopSoakMonitoring();
  if (modal_watchdog_ != nullptr) {
    modal_watchdog_->stop();
  }
  QCoreApplication::exit(2);
}

}  // namespace lapian::app
