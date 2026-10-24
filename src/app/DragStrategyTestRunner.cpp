#include "app/DragStrategyTestRunner.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QSize>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "app/MainWindow.h"

namespace lapian::app {
namespace {

constexpr int kSlowUpdates = 60;
constexpr int kSlowIntervalMs = 33;
constexpr int kFastSeconds = 2;
constexpr double kSlowLagP95LimitFrames = 2.0;
constexpr double kFastForwardFpsFloor = 55.0;
constexpr double kFastBackwardFpsFloor = 45.0;
constexpr double kReverse20FpsFloor = 25.0;
constexpr double kFastForwardGapLimitMs = 50.0;
constexpr double kFastBackwardGapLimitMs = 60.0;
constexpr double kFarCoarseLimitMs = 100.0;
constexpr double kReleaseLimitMs = 150.0;

}  // namespace

DragStrategyTestRunner::DragStrategyTestRunner(MainWindow* window, QString file,
                                               QObject* parent)
    : QObject(parent == nullptr ? window : parent),
      window_(window),
      file_(std::move(file)) {}

void DragStrategyTestRunner::Start() {
  QTimer::singleShot(0, this, &DragStrategyTestRunner::Begin);
}

void DragStrategyTestRunner::Begin() {
  if (file_.isEmpty()) {
    Fail(QStringLiteral("缺少 --file"));
    return;
  }
  window_->OpenFile(file_);
  PollOpen();
}

void DragStrategyTestRunner::PollOpen() {
  if (failed_) {
    return;
  }
  const QString expected = QFileInfo(file_).fileName();
  if (!window_->HasMediaForTest() ||
      window_->InfoFileNameForTest() != expected) {
    if (++open_poll_count_ > 200) {
      Fail(QStringLiteral("打开失败或超时：") + expected);
      return;
    }
    QTimer::singleShot(50, this, &DragStrategyTestRunner::PollOpen);
    return;
  }
  if (!window_->IndexReadyForTest()) {
    if (++open_poll_count_ > 600) {
      Fail(QStringLiteral("索引超时：") + expected);
      return;
    }
    QTimer::singleShot(50, this, &DragStrategyTestRunner::PollOpen);
    return;
  }
  OnReady();
}

void DragStrategyTestRunner::OnReady() {
  last_frame_ = window_->LastFrameIndexForTest();
  frame_rate_ = window_->FrameRateForTest();
  if (last_frame_ < 240) {
    Fail(QStringLiteral("素材帧数太少，无法做策略矩阵：%1").arg(last_frame_));
    return;
  }
  std::fprintf(stderr,
               "T8.17 drag_strategy_begin file=%s frames=%lld fps=%.3f hw=%d "
               "slow_interval_ms=%d slow_updates=%d fast_seconds=%d\n",
               file_.toUtf8().constData(), static_cast<long long>(last_frame_),
               frame_rate_, window_->HardwareDecodeForTest() ? 1 : 0,
               kSlowIntervalMs, kSlowUpdates, kFastSeconds);
  std::fflush(stderr);
  window_->ClearFocusForTest();
  window_->FocusVideoForTest();
  present_connection_ = connect(window_, &MainWindow::framePresented, this,
                                &DragStrategyTestRunner::OnPresent);
  StartNext();
}

void DragStrategyTestRunner::ResetScenario(const QString& scenario,
                                           const QString& expected_mode) {
  current_ = Result{};
  current_.scenario = scenario;
  current_.expected_mode = expected_mode;
  scenario_active_ = true;
  has_present_ = false;
  exact_seen_ = false;
  far_finish_scheduled_ = false;
  slow_updates_ = 0;
  fast_boundary_hit_ = false;
  fast_active_seconds_ = 0.0;
  fast_active_presents_ = 0;
  last_present_ = std::chrono::steady_clock::now();
  scenario_timer_.start();
}

void DragStrategyTestRunner::StartNext() {
  if (failed_) {
    return;
  }
  switch (next_) {
    case 0:
      StartSlow(true);
      return;
    case 1:
      StartSlow(false);
      return;
    case 2:
      StartFast(60, false);
      return;
    case 3:
      StartFast(-60, false);
      return;
    case 4:
      StartFast(-20, true);
      return;
    case 5:
      StartFar(true);
      return;
    case 6:
      StartFar(false);
      return;
    case 7:
      RunReleaseSamples();
      return;
    default:
      Finish();
      return;
  }
}

void DragStrategyTestRunner::StartSlow(bool forward) {
  slow_forward_ = forward;
  const qint64 start = forward
                           ? std::min<qint64>(std::max<qint64>(120, last_frame_ / 4),
                                              last_frame_ - 120)
                           : std::max<qint64>(last_frame_ - 120, 160);
  scenario_start_frame_ = start;
  scenario_target_ = start;
  ResetScenario(forward ? QStringLiteral("slow_forward")
                        : QStringLiteral("slow_backward"),
                QStringLiteral("exact"));
  phase_ = Phase::Slow;
  current_.configured_step = 1;
  fast_presents_start_ = window_->PresentedFrameCountForTest();
  window_->ScrubStartForTest(start);
  QTimer::singleShot(60, this, &DragStrategyTestRunner::StepSlow);
}

void DragStrategyTestRunner::StepSlow() {
  if (!scenario_active_ || failed_) {
    return;
  }
  if (slow_updates_ >= kSlowUpdates) {
    QTimer::singleShot(90, this, &DragStrategyTestRunner::FinishScenario);
    return;
  }
  ++slow_updates_;
  if (slow_forward_) {
    scenario_target_ = std::min<qint64>(last_frame_ - 1, scenario_target_ + 1);
  } else {
    scenario_target_ = std::max<qint64>(0, scenario_target_ - 1);
  }
  ++current_.updates;
  window_->ScrubTargetForTest(scenario_target_);
  QTimer::singleShot(kSlowIntervalMs, this, &DragStrategyTestRunner::StepSlow);
}

void DragStrategyTestRunner::StartFast(int step, bool reverse20) {
  fast_step_ = step;
  const qint64 start =
      step > 0 ? std::min<qint64>(40, last_frame_ / 4)
               : std::max<qint64>(40, last_frame_ - 40);
  scenario_start_frame_ = start;
  scenario_target_ = start;
  ResetScenario(reverse20
                    ? QStringLiteral("reverse_20")
                    : (step > 0 ? QStringLiteral("fast_forward")
                                : QStringLiteral("fast_backward")),
                step > 0 ? QStringLiteral("fast") : QStringLiteral("reverse"));
  phase_ = reverse20 ? Phase::Reverse20 : Phase::Fast;
  current_.configured_step = std::abs(step);
  fast_presents_start_ = window_->PresentedFrameCountForTest();
  window_->ScrubStartForTest(start);
  scenario_timer_.restart();
  QTimer::singleShot(10, Qt::PreciseTimer, this,
                     &DragStrategyTestRunner::TickFast);
}

void DragStrategyTestRunner::TickFast() {
  if (!scenario_active_ || failed_) {
    return;
  }
  if (scenario_timer_.elapsed() >= kFastSeconds * 1000) {
    const double seconds = static_cast<double>(scenario_timer_.elapsed()) / 1000.0;
    current_.fps = seconds > 0.0
                       ? static_cast<double>(window_->PresentedFrameCountForTest() -
                                             fast_presents_start_) /
                             seconds
                       : 0.0;
    current_.active_fps =
        fast_boundary_hit_ && fast_active_seconds_ > 0.0
            ? static_cast<double>(fast_active_presents_) / fast_active_seconds_
            : current_.fps;
    FinishScenario();
    return;
  }
  scenario_target_ =
      std::clamp<qint64>(scenario_target_ + fast_step_, 0, last_frame_ - 1);
  if (!fast_boundary_hit_ &&
      (scenario_target_ == 0 || scenario_target_ == last_frame_ - 1)) {
    fast_boundary_hit_ = true;
    fast_active_seconds_ = static_cast<double>(scenario_timer_.elapsed()) / 1000.0;
    fast_active_presents_ =
        window_->PresentedFrameCountForTest() - fast_presents_start_;
  }
  ++current_.updates;
  window_->ScrubTargetForTest(scenario_target_);
  QTimer::singleShot(10, Qt::PreciseTimer, this,
                     &DragStrategyTestRunner::TickFast);
}

void DragStrategyTestRunner::StartFar(bool forward) {
  phase_ = Phase::Far;
  far_start_ = forward ? std::min<qint64>(300, last_frame_ / 4)
                       : std::max<qint64>(300, last_frame_ - 300);
  far_target_ = forward ? std::max<qint64>(3000, last_frame_ - 300)
                        : std::max<qint64>(600, last_frame_ / 3);
  far_target_ = std::clamp<qint64>(far_target_, 0, last_frame_ - 1);
  if (far_target_ == far_start_) {
    far_target_ = std::clamp<qint64>(far_target_ + (forward ? 1000 : -1000), 0,
                                     last_frame_ - 1);
  }
  scenario_start_frame_ = far_start_;
  scenario_target_ = far_target_;
  ResetScenario(forward ? QStringLiteral("far_forward")
                        : QStringLiteral("far_backward"),
                QStringLiteral("far_jump"));
  current_.configured_step = far_target_ > far_start_ ? far_target_ - far_start_
                                                      : far_start_ - far_target_;
  window_->ScrubStartForTest(far_start_);
  QTimer::singleShot(300, this, &DragStrategyTestRunner::StartFarTarget);
}

void DragStrategyTestRunner::StartFarTarget() {
  if (!scenario_active_ || failed_) {
    return;
  }
  phase_ = Phase::Far;
  scenario_timer_.restart();
  has_present_ = false;
  exact_seen_ = false;
  far_finish_scheduled_ = false;
  last_present_ = std::chrono::steady_clock::now();
  ++current_.updates;
  window_->ScrubTargetForTest(far_target_);
  QTimer::singleShot(2000, this, [this]() {
    if (scenario_active_ && phase_ == Phase::Far) {
      FinishScenario();
    }
  });
}

void DragStrategyTestRunner::FinishFarSoon() {
  if (far_finish_scheduled_) {
    return;
  }
  far_finish_scheduled_ = true;
  QTimer::singleShot(120, this, &DragStrategyTestRunner::FinishScenario);
}

void DragStrategyTestRunner::OnPresent() {
  if (!scenario_active_ || failed_) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (!has_present_) {
    has_present_ = true;
    current_.first_present_ms = ScenarioElapsedMs();
  } else {
    const double gap_ms =
        std::chrono::duration<double, std::milli>(now - last_present_).count();
    current_.max_gap_ms = std::max(current_.max_gap_ms, gap_ms);
  }
  last_present_ = now;

  const PreviewTestDiagnostics diagnostics =
      window_->PreviewDiagnosticsForTest();
  if (diagnostics.displayed_frame < 0) {
    return;
  }
  ++current_.presents;
  current_.last_mode = diagnostics.drag_mode;
  current_.last_reason = diagnostics.drag_mode_reason;
  current_.last_step = diagnostics.drag_step;
  current_.last_interval_ms = diagnostics.drag_interval_ms;
  current_.last_speed_fps = diagnostics.drag_speed_frames_per_s;
  current_.last_run = diagnostics.drag_same_direction_run;
  current_.cache_hit_rate = diagnostics.cache_hit_rate;
  current_.hw_active = diagnostics.hw_active;
  if (diagnostics.displayed_hardware) {
    ++current_.hw_presents;
  }
  if (diagnostics.displayed_from_cache) {
    ++current_.cache_presents;
  }
  current_.lag_frames.push_back(
      static_cast<double>(diagnostics.target_lag_frames));
  current_.lag_ms.push_back(diagnostics.target_lag_ms);

  if (diagnostics.displayed_coarse) {
    ++current_.coarse_presents;
    if (exact_seen_) {
      ++current_.coarse_after_exact;
    }
    if (phase_ == Phase::Far && current_.first_coarse_ms < 0.0) {
      current_.first_coarse_ms = ScenarioElapsedMs();
    }
    return;
  }

  if (phase_ == Phase::Far) {
    if (diagnostics.displayed_frame == far_target_) {
      exact_seen_ = true;
      current_.exact_ms = ScenarioElapsedMs();
      if (current_.first_coarse_ms >= 0.0) {
        current_.exact_after_coarse_ms =
            current_.exact_ms - current_.first_coarse_ms;
      }
      FinishFarSoon();
    }
    return;
  }
  if (phase_ == Phase::Slow &&
      diagnostics.displayed_frame == diagnostics.target_frame) {
    exact_seen_ = true;
  }
}

void DragStrategyTestRunner::FinishScenario() {
  if (!scenario_active_) {
    return;
  }
  scenario_active_ = false;
  const PreviewTestDiagnostics before_release =
      window_->PreviewDiagnosticsForTest();
  current_.cache_hit_rate = before_release.cache_hit_rate;
  current_.hw_active = before_release.hw_active;
  QElapsedTimer release_timer;
  release_timer.start();
  window_->ScrubFinishForTest(scenario_target_);
  current_.release_ms =
      static_cast<double>(release_timer.nsecsElapsed()) / 1.0e6;
  release_samples_.push_back(current_.release_ms);

  const double elapsed_s = ScenarioElapsedMs() / 1000.0;
  if (current_.fps <= 0.0) {
    current_.fps = elapsed_s > 0.0
                       ? static_cast<double>(current_.presents) / elapsed_s
                       : 0.0;
  }
  if (current_.active_fps <= 0.0) {
    current_.active_fps = current_.fps;
  }

  const double lag_p50 = Percentile(current_.lag_frames, 0.50);
  const double lag_p95 = Percentile(current_.lag_frames, 0.95);
  const double lag_max = MaxOf(current_.lag_frames);
  const double lag_ms_p95 = Percentile(current_.lag_ms, 0.95);

  // 每个场景的通过条件写死在这里，和 docs/perf/drag-preview-dp3.md 一致；
  // 不改 P4/P6/P7 阈值，也不把“fps 高”当成“跟随目标”。
  current_.pass = false;
  current_.pass_reason = QStringLiteral("unknown");
  if (current_.scenario == QStringLiteral("slow_forward") ||
      current_.scenario == QStringLiteral("slow_backward")) {
    const bool lag_ok = !current_.lag_frames.empty() &&
                        lag_p95 <= kSlowLagP95LimitFrames;
    const bool mode_ok = current_.last_mode == QStringLiteral("exact");
    const bool coarse_ok = current_.coarse_after_exact == 0;
    current_.pass = lag_ok && mode_ok && coarse_ok;
    current_.pass_reason =
        QStringLiteral("lag_p95_le_2=%1 mode_exact=%2 no_coarse_after_exact=%3")
            .arg(lag_ok ? 1 : 0)
            .arg(mode_ok ? 1 : 0)
            .arg(coarse_ok ? 1 : 0);
  } else if (current_.scenario == QStringLiteral("fast_forward")) {
    const bool fps_ok = current_.active_fps >= kFastForwardFpsFloor;
    const bool gap_ok = current_.max_gap_ms <= kFastForwardGapLimitMs;
    const bool mode_ok = current_.last_mode == QStringLiteral("fast");
    current_.pass = fps_ok && gap_ok && mode_ok;
    current_.pass_reason =
        QStringLiteral("active_fps_ge_55=%1 gap_le_50=%2 mode_fast=%3")
            .arg(fps_ok ? 1 : 0)
            .arg(gap_ok ? 1 : 0)
            .arg(mode_ok ? 1 : 0);
  } else if (current_.scenario == QStringLiteral("fast_backward")) {
    const bool fps_ok = current_.active_fps >= kFastBackwardFpsFloor;
    const bool gap_ok = current_.max_gap_ms <= kFastBackwardGapLimitMs;
    const bool mode_ok = current_.last_mode == QStringLiteral("reverse");
    current_.pass = fps_ok && gap_ok && mode_ok;
    current_.pass_reason =
        QStringLiteral("active_fps_ge_45=%1 gap_le_60=%2 mode_reverse=%3")
            .arg(fps_ok ? 1 : 0)
            .arg(gap_ok ? 1 : 0)
            .arg(mode_ok ? 1 : 0);
  } else if (current_.scenario == QStringLiteral("reverse_20")) {
    const bool fps_ok = current_.fps >= kReverse20FpsFloor;
    const bool mode_ok = current_.last_mode == QStringLiteral("reverse");
    current_.pass = fps_ok && mode_ok;
    current_.pass_reason = QStringLiteral("fps_ge_25=%1 mode_reverse=%2")
                               .arg(fps_ok ? 1 : 0)
                               .arg(mode_ok ? 1 : 0);
  } else if (current_.scenario == QStringLiteral("far_forward") ||
             current_.scenario == QStringLiteral("far_backward")) {
    const bool coarse_ok = current_.first_coarse_ms >= 0.0 &&
                           current_.first_coarse_ms <= kFarCoarseLimitMs;
    const bool order_ok = current_.exact_after_coarse_ms > 0.0 &&
                          current_.coarse_after_exact == 0;
    const bool exact_ok = current_.exact_ms >= 0.0;
    const bool release_ok = current_.release_ms <= kReleaseLimitMs;
    const bool mode_ok = current_.last_mode == QStringLiteral("far_jump");
    current_.pass = coarse_ok && order_ok && exact_ok && release_ok && mode_ok;
    current_.pass_reason =
        QStringLiteral("coarse_le_100=%1 coarse_before_exact=%2 exact=%3 "
                       "release_le_150=%4 mode_far=%5")
            .arg(coarse_ok ? 1 : 0)
            .arg(order_ok ? 1 : 0)
            .arg(exact_ok ? 1 : 0)
            .arg(release_ok ? 1 : 0)
            .arg(mode_ok ? 1 : 0);
  } else {
    current_.pass = true;
    current_.pass_reason = QStringLiteral("record_only");
  }

  std::fprintf(
      stderr,
      "T8.17 drag_strategy scenario=%s mode=%s reason=%s step=%lld "
      "step_cfg=%d interval_ms=%lld speed_fps=%.0f run=%d updates=%d "
      "presents=%d hw_presents=%d cache_presents=%d "
      "fps=%.1f active_fps=%.1f max_gap_ms=%.1f first_present_ms=%.1f "
      "lag_samples=%zu lag_frames_p50=%.1f lag_frames_p95=%.1f "
      "lag_frames_max=%.1f lag_ms_p95=%.1f coarse_presents=%d "
      "coarse_after_exact=%d first_coarse_ms=%.1f exact_after_coarse_ms=%.1f "
      "exact_ms=%.1f release_ms=%.1f cache_hit_rate=%.3f hw_active=%d "
      "pass=%d pass_reason=%s\n",
      current_.scenario.toUtf8().constData(),
      current_.last_mode.toUtf8().constData(),
      current_.last_reason.toUtf8().constData(),
      static_cast<long long>(current_.last_step),
      current_.configured_step,
      static_cast<long long>(current_.last_interval_ms),
      current_.last_speed_fps, current_.last_run, current_.updates,
      current_.presents, current_.hw_presents, current_.cache_presents,
      current_.fps, current_.active_fps,
      current_.max_gap_ms, current_.first_present_ms,
      current_.lag_frames.size(), lag_p50, lag_p95, lag_max, lag_ms_p95,
      current_.coarse_presents, current_.coarse_after_exact,
      current_.first_coarse_ms, current_.exact_after_coarse_ms,
      current_.exact_ms, current_.release_ms, current_.cache_hit_rate,
      current_.hw_active ? 1 : 0, current_.pass ? 1 : 0,
      current_.pass_reason.toUtf8().constData());
  std::fflush(stderr);

  results_.push_back(current_);
  phase_ = Phase::Idle;
  ++next_;
  QTimer::singleShot(80, this, &DragStrategyTestRunner::StartNext);
}

void DragStrategyTestRunner::RunReleaseSamples() {
  phase_ = Phase::Release;
  release_samples_done_ = 0;
  RunReleaseSample();
}

void DragStrategyTestRunner::RunReleaseSample() {
  if (release_samples_done_ >= release_samples_target_) {
    Finish();
    return;
  }
  qint64 target = -1;
  const int max_prefix = std::max<int>(12, static_cast<int>(2.0 * frame_rate_));
  for (int attempt = 0; attempt < 200; ++attempt) {
    const qint64 candidate =
        12 + static_cast<qint64>(rng_.bounded(static_cast<quint32>(
                 std::max<qint64>(1, last_frame_ - 20))));
    const qint64 distance = window_->FrameDistanceFromKeyframeForTest(candidate);
    if (distance >= 6 && distance <= max_prefix) {
      target = candidate;
      break;
    }
  }
  if (target < 0) {
    target = std::min<qint64>(last_frame_ - 1, max_prefix + 12);
  }
  ++release_samples_done_;
  window_->ScrubStartForTest(std::max<qint64>(0, target - 8));
  window_->ScrubTargetForTest(target);
  QTimer::singleShot(50, this, [this, target]() {
    QElapsedTimer timer;
    timer.start();
    window_->ScrubFinishForTest(target);
    release_samples_.push_back(
        static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
    QTimer::singleShot(30, this,
                       &DragStrategyTestRunner::RunReleaseSample);
  });
}

double DragStrategyTestRunner::ScenarioElapsedMs() const {
  return static_cast<double>(scenario_timer_.elapsed());
}

double DragStrategyTestRunner::Percentile(std::vector<double> values,
                                          double fraction) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  std::size_t index = static_cast<std::size_t>(
      static_cast<double>(values.size() - 1) * fraction);
  if (index >= values.size()) {
    index = values.size() - 1;
  }
  return values[index];
}

double DragStrategyTestRunner::MaxOf(const std::vector<double>& values) {
  return values.empty() ? 0.0
                        : *std::max_element(values.begin(), values.end());
}

void DragStrategyTestRunner::Finish() {
  const double release_p95 = Percentile(release_samples_, 0.95);
  const bool release_pass = !release_samples_.empty() &&
                            release_p95 <= kReleaseLimitMs;
  bool all_pass = release_pass;
  bool slow_fwd = false;
  bool slow_back = false;
  bool fast_fwd = false;
  bool fast_back = false;
  bool reverse20 = false;
  bool far_fwd = false;
  bool far_back = false;
  for (const Result& result : results_) {
    all_pass = all_pass && result.pass;
    if (result.scenario == QStringLiteral("slow_forward")) {
      slow_fwd = result.pass;
    } else if (result.scenario == QStringLiteral("slow_backward")) {
      slow_back = result.pass;
    } else if (result.scenario == QStringLiteral("fast_forward")) {
      fast_fwd = result.pass;
    } else if (result.scenario == QStringLiteral("fast_backward")) {
      fast_back = result.pass;
    } else if (result.scenario == QStringLiteral("reverse_20")) {
      reverse20 = result.pass;
    } else if (result.scenario == QStringLiteral("far_forward")) {
      far_fwd = result.pass;
    } else if (result.scenario == QStringLiteral("far_backward")) {
      far_back = result.pass;
    }
  }
  const PreviewTestDiagnostics diagnostics =
      window_->PreviewDiagnosticsForTest();
  std::fprintf(
      stderr,
      "T8.17 drag_strategy_summary file=%s frames=%lld hw_active=%d "
      "slow_forward=%d slow_backward=%d fast_forward=%d fast_backward=%d "
      "reverse_20=%d far_forward=%d far_backward=%d cases=%zu "
      "release_samples=%zu release_p95=%.1f release_pass=%d "
      "cache_bytes=%lld cache_peak_bytes=%lld thumbnail_bytes=%lld "
      "thumbnail_peak_bytes=%lld all_pass=%d\n",
      file_.toUtf8().constData(), static_cast<long long>(last_frame_),
      diagnostics.hw_active ? 1 : 0, slow_fwd ? 1 : 0, slow_back ? 1 : 0,
      fast_fwd ? 1 : 0, fast_back ? 1 : 0, reverse20 ? 1 : 0, far_fwd ? 1 : 0,
      far_back ? 1 : 0, results_.size(), release_samples_.size(), release_p95,
      release_pass ? 1 : 0, static_cast<long long>(diagnostics.cache_bytes),
      static_cast<long long>(diagnostics.cache_peak_bytes),
      static_cast<long long>(diagnostics.thumbnail_bytes),
      static_cast<long long>(diagnostics.thumbnail_peak_bytes),
      all_pass ? 1 : 0);
  std::fflush(stderr);
  QCoreApplication::exit(0);
}

void DragStrategyTestRunner::Fail(const QString& reason) {
  failed_ = true;
  std::fprintf(stderr, "T8.17 drag_strategy_fail reason=%s\n",
               reason.toUtf8().constData());
  std::fflush(stderr);
  QCoreApplication::exit(2);
}

}  // namespace lapian::app
