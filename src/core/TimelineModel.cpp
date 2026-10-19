#include "core/TimelineModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lapian::core {
namespace {

constexpr double kEpsilon = 1e-9;

double ClampDouble(double value, double low, double high) {
  return std::max(low, std::min(value, high));
}

}  // namespace

void TimelineModel::SetFrameCount(std::int64_t count) {
  frame_count_ = std::max<std::int64_t>(0, count);
  if (frame_count_ == 0) {
    pixels_per_frame_ = 0.0;
    scroll_ = 0.0;
    return;
  }
  ClampScaleAndScroll();
}

void TimelineModel::SetViewportWidth(double width) {
  viewport_width_ = std::max(0.0, width);
  ClampScaleAndScroll();
}

double TimelineModel::FitPixelsPerFrame() const {
  if (frame_count_ <= 0 || viewport_width_ <= 0.0) {
    return 0.0;
  }
  return viewport_width_ / static_cast<double>(frame_count_);
}

double TimelineModel::MinPixelsPerFrame() const { return FitPixelsPerFrame(); }

double TimelineModel::MaxPixelsPerFrame() const {
  if (frame_count_ <= 0 || viewport_width_ <= 0.0) {
    return kMaxPixelsPerFrame;
  }
  // 极短片段全片适配可能已经超过 8px/帧；此时以 fit 为上限，保证全片可见。
  return std::max(kMaxPixelsPerFrame, FitPixelsPerFrame());
}

void TimelineModel::Fit() {
  pixels_per_frame_ = FitPixelsPerFrame();
  scroll_ = 0.0;
}

bool TimelineModel::IsAtFit() const {
  const double fit = FitPixelsPerFrame();
  return std::abs(pixels_per_frame_ - fit) <=
         std::max(kEpsilon, std::abs(fit) * 1e-9);
}

void TimelineModel::SetPixelsPerFrame(double pixels_per_frame) {
  if (!(pixels_per_frame > 0.0)) {
    return;
  }
  pixels_per_frame_ = pixels_per_frame;
  ClampScaleAndScroll();
}

void TimelineModel::SetScroll(double scroll) {
  scroll_ = scroll;
  ClampScroll();
}

double TimelineModel::MaxScroll() const {
  if (frame_count_ <= 0 || viewport_width_ <= 0.0 || pixels_per_frame_ <= 0.0) {
    return 0.0;
  }
  const double content_width =
      static_cast<double>(frame_count_) * pixels_per_frame_;
  return std::max(0.0, content_width - viewport_width_);
}

double TimelineModel::ZoomAt(double factor, double anchor_x) {
  if (frame_count_ <= 0 || viewport_width_ <= 0.0 || pixels_per_frame_ <= 0.0 ||
      !(factor > 0.0)) {
    return pixels_per_frame_;
  }

  const double anchor = ClampDouble(anchor_x, 0.0, viewport_width_);
  const double frame_position = (scroll_ + anchor) / pixels_per_frame_;
  const double next = ClampDouble(pixels_per_frame_ * factor,
                                  MinPixelsPerFrame(), MaxPixelsPerFrame());
  if (std::abs(next - pixels_per_frame_) <= kEpsilon) {
    return pixels_per_frame_;
  }

  pixels_per_frame_ = next;
  scroll_ = frame_position * pixels_per_frame_ - anchor;
  ClampScroll();
  return pixels_per_frame_;
}

double TimelineModel::FrameToX(std::int64_t frame) const {
  if (frame_count_ <= 0 || pixels_per_frame_ <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(frame) * pixels_per_frame_ - scroll_;
}

std::int64_t TimelineModel::FrameAtX(double x) const {
  if (frame_count_ <= 0 || pixels_per_frame_ <= 0.0) {
    return 0;
  }
  const double frame_position = (x + scroll_) / pixels_per_frame_;
  if (frame_position <= 0.0) {
    return 0;
  }
  if (frame_position >= static_cast<double>(frame_count_)) {
    return frame_count_ - 1;
  }
  return static_cast<std::int64_t>(std::floor(frame_position));
}

void TimelineModel::EnsureFrameVisible(std::int64_t frame, double margin_px) {
  if (frame_count_ <= 0 || viewport_width_ <= 0.0 || pixels_per_frame_ <= 0.0) {
    return;
  }
  frame = std::clamp<std::int64_t>(frame, 0, frame_count_ - 1);
  const double margin =
      ClampDouble(margin_px, 0.0, viewport_width_ / 3.0);
  const double left = FrameToX(frame);
  const double right = left + pixels_per_frame_;

  if (left < margin) {
    scroll_ = static_cast<double>(frame) * pixels_per_frame_ - margin;
  } else if (right > viewport_width_ - margin) {
    scroll_ = static_cast<double>(frame + 1) * pixels_per_frame_ -
              (viewport_width_ - margin);
  }
  ClampScroll();
}

std::int64_t TimelineModel::FirstVisibleFrame() const {
  if (frame_count_ <= 0 || pixels_per_frame_ <= 0.0) {
    return 0;
  }
  const double raw = scroll_ / pixels_per_frame_;
  if (raw <= 0.0) {
    return 0;
  }
  if (raw >= static_cast<double>(frame_count_)) {
    return frame_count_ - 1;
  }
  return static_cast<std::int64_t>(std::floor(raw));
}

std::int64_t TimelineModel::LastVisibleFrame() const {
  if (frame_count_ <= 0 || pixels_per_frame_ <= 0.0) {
    return 0;
  }
  const double raw = (scroll_ + viewport_width_) / pixels_per_frame_;
  if (raw <= 0.0) {
    return 0;
  }
  if (raw >= static_cast<double>(frame_count_)) {
    return frame_count_ - 1;
  }
  return static_cast<std::int64_t>(std::floor(raw));
}

std::vector<TimelineTick> TimelineModel::BuildTicks() const {
  std::vector<TimelineTick> ticks;
  if (frame_count_ <= 0 || viewport_width_ <= 0.0 || pixels_per_frame_ <= 0.0) {
    return ticks;
  }

  std::int64_t major = NiceStep(kMinLabelSpacingPx, pixels_per_frame_);
  if (major <= 0) {
    return ticks;
  }
  // 极端长片/极小缩放下避免 (first + major - 1) 溢出；只保留首个标签也够用。
  major = std::min(major, frame_count_);

  std::int64_t minor = 0;
  if (major % 5 == 0) {
    minor = major / 5;
  } else if (major % 2 == 0) {
    minor = major / 2;
  }
  if (minor > 0 &&
      static_cast<double>(minor) * pixels_per_frame_ < kMinTickSpacingPx) {
    minor = 0;
  }

  const std::int64_t first = FirstVisibleFrame();
  const std::int64_t last = LastVisibleFrame();

  auto append = [this, &ticks](std::int64_t frame, bool is_major) {
    if (frame < 0 || frame >= frame_count_) {
      return;
    }
    const double x = FrameToX(frame);
    if (x < -0.5 || x > viewport_width_ + 0.5) {
      return;
    }
    ticks.push_back(TimelineTick{frame, x, is_major});
  };

  if (minor > 0) {
    const std::int64_t start = (first / minor) * minor;
    for (std::int64_t frame = start; frame <= last;) {
      if (frame % major != 0) {
        append(frame, false);
      }
      if (frame > last - minor) {
        break;
      }
      frame += minor;
    }
  }

  const std::int64_t major_start = ((first + major - 1) / major) * major;
  for (std::int64_t frame = major_start; frame <= last;) {
    append(frame, true);
    if (frame > last - major) {
      break;
    }
    frame += major;
  }

  std::sort(ticks.begin(), ticks.end(),
            [](const TimelineTick& a, const TimelineTick& b) {
              return a.frame < b.frame;
            });
  return ticks;
}

std::int64_t TimelineModel::NiceStep(double min_pixels,
                                     double pixels_per_frame) {
  if (!(min_pixels > 0.0) || !(pixels_per_frame > 0.0)) {
    return 0;
  }
  const double raw = min_pixels / pixels_per_frame;
  if (!std::isfinite(raw) || raw <= 0.0) {
    return 0;
  }

  const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
  const double normalized = raw / magnitude;
  double nice = 10.0;
  if (normalized <= 1.0 + kEpsilon) {
    nice = 1.0;
  } else if (normalized <= 2.0 + kEpsilon) {
    nice = 2.0;
  } else if (normalized <= 5.0 + kEpsilon) {
    nice = 5.0;
  }

  const double step = std::max(1.0, nice * magnitude);
  if (step >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
    return std::numeric_limits<std::int64_t>::max();
  }
  return static_cast<std::int64_t>(std::ceil(step - kEpsilon));
}

void TimelineModel::ClampScaleAndScroll() {
  if (frame_count_ <= 0 || viewport_width_ <= 0.0) {
    pixels_per_frame_ = 0.0;
    scroll_ = 0.0;
    return;
  }
  if (!(pixels_per_frame_ > 0.0)) {
    pixels_per_frame_ = MinPixelsPerFrame();
  }
  pixels_per_frame_ = ClampDouble(pixels_per_frame_, MinPixelsPerFrame(),
                                  MaxPixelsPerFrame());
  ClampScroll();
}

void TimelineModel::ClampScroll() {
  scroll_ = ClampDouble(scroll_, 0.0, MaxScroll());
}

}  // namespace lapian::core
