#include "app/GopBoundaryTestRunner.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSize>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "app/MainWindow.h"

namespace lapian::app {
namespace {

bool IsTwoSecondGop(double gop_ms) {
  return gop_ms >= 1800.0 && gop_ms <= 2200.0;
}

bool IsLongGop(double gop_ms) { return gop_ms > 2200.0; }

const char* ClassOf(double gop_ms) {
  if (IsTwoSecondGop(gop_ms)) {
    return "2s";
  }
  return IsLongGop(gop_ms) ? "long_gop" : "short_gop";
}

}  // namespace

GopBoundaryTestRunner::GopBoundaryTestRunner(MainWindow* window, QString file,
                                             int samples, QString tag,
                                             QObject* parent)
    : QObject(parent == nullptr ? window : parent),
      window_(window),
      file_(std::move(file)),
      tag_(std::move(tag)),
      samples_(samples > 0 ? samples : 20) {}

void GopBoundaryTestRunner::Start() {
  QTimer::singleShot(0, this, &GopBoundaryTestRunner::Begin);
}

void GopBoundaryTestRunner::Begin() {
  if (file_.isEmpty()) {
    Fail(QStringLiteral("缺少 --file"));
    return;
  }
  window_->OpenFile(file_);
  PollOpen();
}

void GopBoundaryTestRunner::PollOpen() {
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
    QTimer::singleShot(50, this, &GopBoundaryTestRunner::PollOpen);
    return;
  }
  if (!window_->IndexReadyForTest()) {
    if (++open_poll_count_ > 1200) {
      Fail(QStringLiteral("索引超时：") + expected);
      return;
    }
    QTimer::singleShot(50, this, &GopBoundaryTestRunner::PollOpen);
    return;
  }
  OnReady();
}

void GopBoundaryTestRunner::OnReady() {
  const QSize display = window_->SnapshotDisplaySizeForTest();
  is_4k_ = display.width() >= 3840 || display.height() >= 2160;
  threshold_ms_ = is_4k_ ? 150.0 : 80.0;
  last_frame_ = window_->LastFrameIndexForTest();
  fps_ = window_->FrameRateForTest();
  std::fprintf(stderr,
               "T7.11 gop_boundary_begin tag=%s file=%s display=%dx%d "
               "frames=%lld fps=%.3f hw=%d samples=%d threshold_ms=%.0f "
               "path=keyframe_target_up\n",
               tag_.toUtf8().constData(), file_.toUtf8().constData(),
               display.width(), display.height(),
               static_cast<long long>(last_frame_), fps_,
               window_->HardwareDecodeForTest() ? 1 : 0, samples_, threshold_ms_);
  std::fflush(stderr);
  if (last_frame_ < 2 || fps_ <= 0.0) {
    Fail(QStringLiteral("素材帧数/帧率无效"));
    return;
  }
  window_->ClearFocusForTest();
  window_->FocusVideoForTest();
  CollectBoundaries();
  if (failed_) {
    return;
  }
  QTimer::singleShot(50, this, &GopBoundaryTestRunner::RunSample);
}

void GopBoundaryTestRunner::CollectBoundaries() {
  // 关键帧 = FrameDistanceFromKeyframeForTest(frame)==0。扫描上限 40 万帧，
  // 2h@24fps 约 17.3 万帧；关键帧上限 4000 个，避免超长片单拖慢启动。
  std::vector<qint64> keyframes;
  const qint64 scan_limit = std::min<qint64>(last_frame_, 400000);
  for (qint64 frame = 0; frame <= scan_limit && keyframes.size() < 4000;
       ++frame) {
    if (window_->FrameDistanceFromKeyframeForTest(frame) == 0) {
      keyframes.push_back(frame);
    }
  }
  for (std::size_t i = 1; i < keyframes.size(); ++i) {
    Boundary boundary;
    boundary.previous_keyframe = keyframes[i - 1];
    boundary.keyframe = keyframes[i];
    boundary.gop_frames = boundary.keyframe - boundary.previous_keyframe;
    if (boundary.gop_frames > 0) {
      boundaries_.push_back(boundary);
    }
  }
  if (boundaries_.empty()) {
    Fail(QStringLiteral("找不到 GOP 边界"));
    return;
  }

  std::size_t two_s = 0;
  std::size_t long_gop = 0;
  std::size_t short_gop = 0;
  for (const Boundary& boundary : boundaries_) {
    const double gop_ms =
        static_cast<double>(boundary.gop_frames) * 1000.0 / fps_;
    if (IsTwoSecondGop(gop_ms)) {
      ++two_s;
    } else if (IsLongGop(gop_ms)) {
      ++long_gop;
    } else {
      ++short_gop;
    }
  }
  std::fprintf(stderr,
               "T7.11 gop_boundary_plan tag=%s keyframes=%zu boundaries=%zu "
               "gop_2s=%zu gop_short=%zu gop_long=%zu samples=%d\n",
               tag_.toUtf8().constData(), keyframes.size(), boundaries_.size(),
               two_s, short_gop, long_gop, samples_);
  std::fflush(stderr);

  // 均匀取样，避免只测文件开头；边界不足时循环使用（同一 GOP 可重复测）。
  const std::size_t total = boundaries_.size();
  const std::size_t wanted = static_cast<std::size_t>(samples_);
  for (std::size_t i = 0; i < wanted; ++i) {
    std::size_t index = 0;
    if (total <= wanted) {
      index = i % total;
    } else {
      index = (i * total) / wanted;
    }
    sample_order_.push_back(index);
  }
}

void GopBoundaryTestRunner::RunSample() {
  if (failed_) {
    return;
  }
  if (next_sample_ >= static_cast<int>(sample_order_.size())) {
    Finish();
    return;
  }
  const Boundary boundary = boundaries_[sample_order_[next_sample_]];
  QElapsedTimer timer;
  timer.start();
  // 精确 seek 到 GOP 第一个关键帧：目标本身是关键帧，解码器只解到该帧，
  // 不会像 T7.9 的非关键帧目标那样保留“目标前一帧”，因此紧接着的 PgUp
  // 必须从上一个关键帧解完整个 GOP，这才是跨 GOP 边界的真实成本。
  window_->GoToFrameForTest(boundary.keyframe);
  const double seek_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  const bool seek_ok = window_->CurrentFrameForTest() == boundary.keyframe;

  timer.restart();
  window_->PressKeyForTest(Qt::Key_PageUp, 0);
  const double up_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  const bool frame_ok =
      seek_ok && window_->CurrentFrameForTest() == boundary.keyframe - 1;

  Sample sample;
  sample.boundary = boundary;
  sample.seek_ms = seek_ms;
  sample.up_ms = up_ms;
  sample.frame_ok = frame_ok;
  measured_.push_back(sample);

  const double gop_ms =
      static_cast<double>(boundary.gop_frames) * 1000.0 / fps_;
  std::fprintf(stderr,
               "T7.11 gop_boundary_sample i=%d previous_keyframe=%lld "
               "keyframe=%lld gop_frames=%lld gop_ms=%.1f seek_ms=%.2f "
               "up_ms=%.2f frame_ok=%d class=%s\n",
               next_sample_, static_cast<long long>(boundary.previous_keyframe),
               static_cast<long long>(boundary.keyframe),
               static_cast<long long>(boundary.gop_frames), gop_ms, seek_ms,
               up_ms, frame_ok ? 1 : 0, ClassOf(gop_ms));
  std::fflush(stderr);
  ++next_sample_;
  QTimer::singleShot(0, this, &GopBoundaryTestRunner::RunSample);
}

GopBoundaryTestRunner::GroupStats GopBoundaryTestRunner::Summarize(
    const std::vector<double>& values, double threshold_ms) const {
  GroupStats stats;
  stats.n = values.size();
  if (values.empty()) {
    return stats;
  }
  std::vector<double> sorted = values;
  std::sort(sorted.begin(), sorted.end());
  stats.p50 = sorted[sorted.size() / 2];
  const std::size_t p95_index = static_cast<std::size_t>(
      static_cast<double>(sorted.size() - 1) * 0.95);
  stats.p95 = sorted[p95_index];
  stats.max = sorted.back();
  for (double value : sorted) {
    if (value > threshold_ms) {
      ++stats.over;
    }
  }
  return stats;
}

void GopBoundaryTestRunner::Finish() {
  std::vector<const Sample*> two_s;
  std::vector<const Sample*> short_gop;
  std::vector<const Sample*> long_gop;
  for (const Sample& sample : measured_) {
    const double gop_ms =
        static_cast<double>(sample.boundary.gop_frames) * 1000.0 / fps_;
    if (IsTwoSecondGop(gop_ms)) {
      two_s.push_back(&sample);
    } else if (IsLongGop(gop_ms)) {
      long_gop.push_back(&sample);
    } else {
      short_gop.push_back(&sample);
    }
  }

  auto print_group = [this](const char* name, const std::vector<const Sample*>& samples,
                            double threshold_ms, bool gated,
                            const char* suffix) {
    std::vector<double> values;
    std::size_t frame_failures = 0;
    for (const Sample* sample : samples) {
      values.push_back(sample->up_ms);
      if (!sample->frame_ok) {
        ++frame_failures;
      }
    }
    const GroupStats stats = Summarize(values, threshold_ms);
    const bool pass = gated && stats.n > 0 && frame_failures == 0 &&
                      stats.p95 <= threshold_ms;
    std::fprintf(stderr,
                 "T7.11 gop_boundary_group class=%s n=%zu p50_ms=%.2f "
                 "p95_ms=%.2f max_ms=%.2f over=%zu threshold_ms=%.0f "
                 "frame_failures=%zu pass=%d%s\n",
                 name, stats.n, stats.p50, stats.p95, stats.max, stats.over,
                 threshold_ms, frame_failures, gated ? (pass ? 1 : 0) : -1,
                 suffix);
    std::fflush(stderr);
    return pass;
  };

  const char* two_s_suffix = is_4k_ ? " tier=4k_diagnostic" : "";
  const bool two_s_pass =
      print_group("2s", two_s, threshold_ms_, !is_4k_, two_s_suffix);
  if (!short_gop.empty()) {
    print_group("short_gop", short_gop, threshold_ms_, !is_4k_, two_s_suffix);
  }
  if (!long_gop.empty()) {
    print_group("long_gop", long_gop, 0.0, false, " tier=long_gop_usable");
  }

  const GroupStats two_s_stats = [&]() {
    std::vector<double> values;
    for (const Sample* sample : two_s) {
      values.push_back(sample->up_ms);
    }
    return Summarize(values, threshold_ms_);
  }();
  std::size_t frame_failures = 0;
  for (const Sample& sample : measured_) {
    if (!sample.frame_ok) {
      ++frame_failures;
    }
  }
  const int pass = is_4k_ ? -1 : (two_s.empty() ? -1 : (two_s_pass ? 1 : 0));
  const char* verdict = is_4k_ ? "4k_diagnostic"
                               : (two_s.empty() ? "no_2s_samples"
                                                : (two_s_pass ? "pass" : "fail"));
  std::fprintf(stderr,
               "T7.11 gop_boundary_summary tag=%s n=%zu 2s_n=%zu "
               "2s_p50_ms=%.2f 2s_p95_ms=%.2f 2s_max_ms=%.2f 2s_over=%zu "
               "threshold_ms=%.0f frame_failures=%zu long_gop_n=%zu "
               "verdict=%s pass=%d\n",
               tag_.toUtf8().constData(), measured_.size(), two_s_stats.n,
               two_s_stats.p50, two_s_stats.p95, two_s_stats.max,
               two_s_stats.over, threshold_ms_, frame_failures, long_gop.size(),
               verdict, pass);
  std::fflush(stderr);

  if (measured_.empty()) {
    Fail(QStringLiteral("没有产生边界样本"));
    return;
  }
  QCoreApplication::exit(0);
}

void GopBoundaryTestRunner::Fail(const QString& reason) {
  failed_ = true;
  std::fprintf(stderr, "T7.11 gop_boundary_fail tag=%s reason=%s\n",
               tag_.toUtf8().constData(), reason.toUtf8().constData());
  std::fflush(stderr);
  QCoreApplication::exit(2);
}

}  // namespace lapian::app
