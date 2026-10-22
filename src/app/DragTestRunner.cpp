#include "app/DragTestRunner.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QSize>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>

#include "app/MainWindow.h"

namespace lapian::app {
namespace {

struct Stats {
  double p50 = 0.0;
  double p95 = 0.0;
  double max = 0.0;
  std::size_t over = 0;
};

Stats Summarize(const std::vector<double>& values, double threshold_ms) {
  Stats stats;
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

}  // namespace

DragTestRunner::DragTestRunner(MainWindow* window, QString file, int samples,
                               QString tag, QObject* parent)
    : QObject(parent == nullptr ? window : parent),
      window_(window),
      file_(std::move(file)),
      tag_(std::move(tag)),
      samples_(samples > 0 ? samples : 20) {}

void DragTestRunner::Start() {
  QTimer::singleShot(0, this, &DragTestRunner::Begin);
}

void DragTestRunner::Begin() {
  if (file_.isEmpty()) {
    Fail(QStringLiteral("缺少 --file"));
    return;
  }
  window_->OpenFile(file_);
  PollOpen();
}

void DragTestRunner::PollOpen() {
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
    QTimer::singleShot(50, this, &DragTestRunner::PollOpen);
    return;
  }
  if (!window_->IndexReadyForTest()) {
    if (++open_poll_count_ > 600) {
      Fail(QStringLiteral("索引超时：") + expected);
      return;
    }
    QTimer::singleShot(50, this, &DragTestRunner::PollOpen);
    return;
  }
  OnReady();
}

void DragTestRunner::OnReady() {
  const QSize display = window_->SnapshotDisplaySizeForTest();
  is_4k_ = display.width() >= 3840 || display.height() >= 2160;
  last_frame_ = window_->LastFrameIndexForTest();
  const double fps = window_->FrameRateForTest();
  max_prefix_frames_ = fps > 0.0 ? static_cast<int>(std::lround(2.0 * fps)) : 60;
  if (max_prefix_frames_ < 12) {
    max_prefix_frames_ = 12;
  }
  std::fprintf(stderr,
               "T7.4 drag_begin tag=%s file=%s display=%dx%d frames=%lld "
               "fps=%.3f hw=%d max_prefix_frames=%d samples=%d\n",
               tag_.toUtf8().constData(), file_.toUtf8().constData(),
               display.width(), display.height(),
               static_cast<long long>(last_frame_), fps,
               window_->HardwareDecodeForTest() ? 1 : 0, max_prefix_frames_,
               samples_);
  std::fflush(stderr);

  if (last_frame_ < 60) {
    Fail(QStringLiteral("素材帧数太少，无法做 4K 扫掠：%1").arg(last_frame_));
    return;
  }
  window_->ClearFocusForTest();
  window_->FocusVideoForTest();
  QTimer::singleShot(80, this, &DragTestRunner::RunP9);
}

void DragTestRunner::RunP9() {
  // P9：冷随机跳转 20 次，只保留目标距前关键帧 ≤2s 的样本。
  for (int i = 0; i < samples_; ++i) {
    qint64 target = -1;
    qint64 prefix = -1;
    for (int attempt = 0; attempt < 200; ++attempt) {
      const qint64 candidate =
          2 + static_cast<qint64>(rng_.bounded(static_cast<quint32>(
                  std::max<qint64>(1, last_frame_ - 4))));
      const qint64 distance = window_->FrameDistanceFromKeyframeForTest(candidate);
      if (distance >= 2 && distance <= max_prefix_frames_) {
        target = candidate;
        prefix = distance;
        break;
      }
    }
    if (target < 0) {
      Fail(QStringLiteral("找不到距关键帧 ≤2s 的目标"));
      return;
    }
    QElapsedTimer timer;
    timer.start();
    window_->GoToFrameForTest(target);
    const double ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
    p9_ms_.push_back(ms);
    p9_prefix_.push_back(prefix);
  }
  RunP6(0, samples_);
}

void DragTestRunner::RunP6(int index, int total) {
  if (index >= total) {
    RunP4();
    return;
  }
  qint64 target = -1;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const qint64 candidate =
        12 + static_cast<qint64>(rng_.bounded(static_cast<quint32>(
                 std::max<qint64>(1, last_frame_ - 16))));
    const qint64 distance = window_->FrameDistanceFromKeyframeForTest(candidate);
    if (distance >= 6 && distance <= max_prefix_frames_) {
      target = candidate;
      break;
    }
  }
  if (target < 0) {
    target = std::min<qint64>(last_frame_ - 1, max_prefix_frames_ + 12);
  }
  p6_target_ = target;
  p6_index_ = index;
  p6_total_ = total;
  const qint64 start = std::max<qint64>(0, target - 8);
  // 产品拖动路径：开始预览 → 中间目标 → 松手精确。
  window_->ScrubStartForTest(start);
  window_->ScrubTargetForTest(start + 3);
  window_->ScrubTargetForTest(target);
  QTimer::singleShot(48, this, [this]() {
    window_->ScrubTargetForTest(p6_target_);
    QTimer::singleShot(16, this, [this]() {
      QElapsedTimer timer;
      timer.start();
      window_->ScrubFinishForTest(p6_target_);
      p6_ms_.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
      QTimer::singleShot(16, this,
                         [this]() { RunP6(p6_index_ + 1, p6_total_); });
    });
  });
}

void DragTestRunner::RunP4() {
  // P4：2s 连续向前拖动（每 16ms 调一次目标），统计实际 Present 的预览帧率。
  p4_start_frame_ = std::min<qint64>(40, last_frame_ / 4);
  p4_delta_ = 0;
  window_->ScrubStartForTest(p4_start_frame_);
  p4_presents_start_ = window_->PresentedFrameCountForTest();
  p4_timer_.start();
  auto tick = std::make_shared<std::function<void()>>();
  *tick = [this, tick]() {
    if (p4_timer_.elapsed() >= 2000) {
      p4_seconds_ = static_cast<double>(p4_timer_.elapsed()) / 1000.0;
      p4_presents_ = window_->PresentedFrameCountForTest() - p4_presents_start_;
      p4_fps_ = p4_seconds_ > 0.0 ? p4_presents_ / p4_seconds_ : 0.0;
      window_->ScrubFinishForTest(std::min<qint64>(
          last_frame_, p4_start_frame_ + p4_delta_));
      QTimer::singleShot(30, this, &DragTestRunner::RunP7);
      return;
    }
    p4_delta_ += 3;
    window_->ScrubTargetForTest(
        std::min<qint64>(last_frame_, p4_start_frame_ + p4_delta_));
    QTimer::singleShot(16, this, [tick]() { (*tick)(); });
  };
  QTimer::singleShot(16, this, [tick]() { (*tick)(); });
}

void DragTestRunner::RunP7() {
  window_->ClearFocusForTest();
  window_->FocusVideoForTest();
  const qint64 spot_ceiling = std::max<qint64>(0, last_frame_ - 40);
  qint64 mid = std::min<qint64>(spot_ceiling, 250);
  // 避开“目标正好是关键帧”的样本：那测到的是“从上一个 GOP 头部解到目标前一帧”，
  // 不是 spec P7 的“精确 seek 解到目标后紧接 PgUp”。10s GOP 代理上约 400ms，
  // 属长 GOP 可用档，80ms 不适用；这里选关键帧后 8 帧，覆盖正常 seek 解码路径。
  if (window_->FrameDistanceFromKeyframeForTest(mid) == 0 && mid + 8 <= spot_ceiling) {
    mid += 8;
  }
  const qint64 spot_distance = window_->FrameDistanceFromKeyframeForTest(mid);
  std::fprintf(stderr,
               "T7.4 p7_after_seek_target frame=%lld keyframe_distance=%lld\n",
               static_cast<long long>(mid), static_cast<long long>(spot_distance));
  std::fflush(stderr);
  // spec：精确 seek 后紧接 PgUp/PgDn 各测一次（单样本，单独报告）。
  {
    window_->GoToFrameForTest(mid);
    QElapsedTimer timer;
    timer.start();
    window_->PressKeyForTest(Qt::Key_PageUp, 0);
    p7_spot_ms_.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
    window_->GoToFrameForTest(mid);
    timer.restart();
    window_->PressKeyForTest(Qt::Key_PageDown, 0);
    p7_spot_ms_.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
  }
  // 后退 20 步：从精确 seek 后的位置开始，第一步会填缓存，其余命中。
  window_->GoToFrameForTest(std::min<qint64>(300, last_frame_ - 60));
  for (int i = 0; i < samples_; ++i) {
    QElapsedTimer timer;
    timer.start();
    window_->PressKeyForTest(Qt::Key_PageUp, 0);
    p7_backward_ms_.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
  }
  // 前进 20 步：解码器已热，走顺序解码。
  for (int i = 0; i < samples_; ++i) {
    QElapsedTimer timer;
    timer.start();
    window_->PressKeyForTest(Qt::Key_PageDown, 0);
    p7_forward_ms_.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
  }
  RunP8();
}

void DragTestRunner::RunP8() {
  // P8：同方向长按 2s；统计帧数/秒与 Present 帧数（每帧呈现）。
  window_->ClearFocusForTest();
  window_->FocusVideoForTest();
  window_->GoToFrameForTest(30);
  p8_start_frame_ = window_->CurrentFrameForTest();
  p8_presents_start_ = window_->PresentedFrameCountForTest();
  p8_timer_.start();
  window_->PressKeyDownForTest(Qt::Key_PageDown, 0);
  QTimer::singleShot(2000, this, [this]() {
    window_->PressKeyUpForTest(Qt::Key_PageDown, 0);
    const double elapsed = static_cast<double>(p8_timer_.elapsed()) / 1000.0;
    QTimer::singleShot(120, this, [this, elapsed]() {
      p8_seconds_ = elapsed;
      p8_frames_ = window_->CurrentFrameForTest() - p8_start_frame_;
      p8_presents_ = window_->PresentedFrameCountForTest() - p8_presents_start_;
      Finish();
    });
  });
}

void DragTestRunner::PrintDistribution(const char* name,
                                       std::vector<double>* samples,
                                       double threshold_ms) {
  const Stats stats = Summarize(*samples, threshold_ms);
  const bool pass = !samples->empty() && stats.p95 <= threshold_ms;
  std::fprintf(stderr,
               "T7.4 %s n=%zu p50_ms=%.2f p95_ms=%.2f max_ms=%.2f over=%zu "
               "threshold_ms=%.0f pass=%d\n",
               name, samples->size(), stats.p50, stats.p95, stats.max,
               stats.over, threshold_ms, pass ? 1 : 0);
  std::fflush(stderr);
}

void DragTestRunner::LogSummary() {
  const double p4_threshold = is_4k_ ? 20.0 : 30.0;
  const double p6_threshold = is_4k_ ? 300.0 : 150.0;
  const double p7_threshold = is_4k_ ? 150.0 : 80.0;
  const double p9_threshold = is_4k_ ? 300.0 : 200.0;

  PrintDistribution("p9_jump", &p9_ms_, p9_threshold);
  PrintDistribution("p6_release", &p6_ms_, p6_threshold);
  PrintDistribution("p7_backward", &p7_backward_ms_, p7_threshold);
  PrintDistribution("p7_forward", &p7_forward_ms_, p7_threshold);
  {
    const Stats stats = Summarize(p7_spot_ms_, p7_threshold);
    std::fprintf(stderr,
                 "T7.4 p7_after_seek spot_updown_ms=%.2f/%.2f max_ms=%.2f "
                 "threshold_ms=%.0f\n",
                 p7_spot_ms_.size() > 0 ? p7_spot_ms_[0] : 0.0,
                 p7_spot_ms_.size() > 1 ? p7_spot_ms_[1] : 0.0, stats.max,
                 p7_threshold);
    std::fflush(stderr);
  }
  const bool p4_pass = p4_fps_ >= p4_threshold;
  const bool p6_pass =
      !p6_ms_.empty() && Summarize(p6_ms_, p6_threshold).p95 <= p6_threshold;
  const bool p7_back_pass =
      !p7_backward_ms_.empty() &&
      Summarize(p7_backward_ms_, p7_threshold).p95 <= p7_threshold;
  const bool p7_fwd_pass =
      !p7_forward_ms_.empty() &&
      Summarize(p7_forward_ms_, p7_threshold).p95 <= p7_threshold;
  const bool p9_pass =
      !p9_ms_.empty() && Summarize(p9_ms_, p9_threshold).p95 <= p9_threshold;
  const double p8_fps = p8_seconds_ > 0.0
                            ? static_cast<double>(p8_frames_) / p8_seconds_
                            : 0.0;
  const bool p8_pass = p8_fps >= 12.0 && p8_presents_ >= p8_frames_;

  std::fprintf(stderr,
               "T7.4 drag_summary tag=%s hw=%d display=%dx%d "
               "p4_fps=%.1f p4_presents=%d p4_s=%.2f p4_pass=%d "
               "p6_p95=%.1f p6_pass=%d p7_back_p95=%.1f p7_fwd_p95=%.1f "
               "p7_pass=%d p8_fps=%.1f p8_frames=%lld p8_presents=%d "
               "p8_pass=%d p9_p95=%.1f p9_prefix_max=%lld p9_pass=%d cache=%s\n",
               tag_.toUtf8().constData(), window_->HardwareDecodeForTest() ? 1 : 0,
               window_->SnapshotDisplaySizeForTest().width(),
               window_->SnapshotDisplaySizeForTest().height(), p4_fps_,
               p4_presents_, p4_seconds_, p4_pass ? 1 : 0,
               p6_ms_.empty() ? 0.0 : Summarize(p6_ms_, p6_threshold).p95,
               p6_pass ? 1 : 0,
               p7_backward_ms_.empty()
                   ? 0.0
                   : Summarize(p7_backward_ms_, p7_threshold).p95,
               p7_forward_ms_.empty()
                   ? 0.0
                   : Summarize(p7_forward_ms_, p7_threshold).p95,
               (p7_back_pass && p7_fwd_pass) ? 1 : 0, p8_fps,
               static_cast<long long>(p8_frames_), p8_presents_,
               p8_pass ? 1 : 0,
               p9_ms_.empty() ? 0.0 : Summarize(p9_ms_, p9_threshold).p95,
               p9_prefix_.empty()
                   ? 0
                   : *std::max_element(p9_prefix_.begin(), p9_prefix_.end()),
               p9_pass ? 1 : 0,
               window_->FrameCacheStateForTest().toUtf8().constData());
  std::fflush(stderr);
}

void DragTestRunner::Finish() {
  LogSummary();
  QCoreApplication::exit(0);
}

void DragTestRunner::Fail(const QString& reason) {
  failed_ = true;
  std::fprintf(stderr, "T7.4 drag_fail tag=%s reason=%s\n",
               tag_.toUtf8().constData(), reason.toUtf8().constData());
  std::fflush(stderr);
  QCoreApplication::exit(2);
}

}  // namespace lapian::app
