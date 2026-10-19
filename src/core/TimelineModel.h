#pragma once

#include <cstdint>
#include <vector>

namespace lapian::core {

// 时间轴上的一个刻度。major=true 的刻度带帧号标签。
struct TimelineTick {
  std::int64_t frame = 0;
  double x = 0.0;
  bool major = false;
};

// T2.7 时间轴几何：等距帧格（1 帧 1 格）。
// 只负责帧号↔像素、缩放/滚动约束和可见刻度；不绘制、不依赖 Qt。
// 第 N 帧占据 [N, N+1)，坐标命中按 floor 取整；T2.8 直接复用 FrameAtX。
class TimelineModel {
 public:
  // 最大缩放：1 帧 8 逻辑像素。极短片段如果 fit 已超过 8px/帧，
  // 上限会放宽到 fit，保证“全片适配”仍然可用。
  static constexpr double kMaxPixelsPerFrame = 8.0;
  // 主刻度标签的最小间距；次刻度不足 4px 时不画。
  static constexpr double kMinLabelSpacingPx = 64.0;
  static constexpr double kMinTickSpacingPx = 4.0;

  void SetFrameCount(std::int64_t count);
  void SetViewportWidth(double width);

  std::int64_t frame_count() const { return frame_count_; }
  double viewport_width() const { return viewport_width_; }

  double FitPixelsPerFrame() const;
  double MinPixelsPerFrame() const;
  double MaxPixelsPerFrame() const;
  double pixels_per_frame() const { return pixels_per_frame_; }
  double scroll() const { return scroll_; }
  double MaxScroll() const;
  bool IsAtFit() const;

  // 全片适配：缩放取最小值、滚动归零。
  void Fit();
  void SetPixelsPerFrame(double pixels_per_frame);
  void SetScroll(double scroll);
  // 以视口 x 为锚点缩放；返回缩放后的实际比例。
  double ZoomAt(double factor, double anchor_x);

  double FrameToX(std::int64_t frame) const;
  // 第 N 帧占据 [N, N+1)，按 floor 取整；越界 clamp 到首/末帧。
  std::int64_t FrameAtX(double x) const;

  // 当前帧变化时调用：仅在越出可见区时做最小滚动。
  void EnsureFrameVisible(std::int64_t frame, double margin_px = 0.0);

  // 只生成可见区内的刻度，按帧号升序；不会遍历全片。
  std::vector<TimelineTick> BuildTicks() const;

  // 返回 >= min_pixels 的最小 1/2/5×10^n；供单测和渲染共用。
  static std::int64_t NiceStep(double min_pixels, double pixels_per_frame);

 private:
  void ClampScaleAndScroll();
  void ClampScroll();
  std::int64_t FirstVisibleFrame() const;
  std::int64_t LastVisibleFrame() const;

  std::int64_t frame_count_ = 0;
  double viewport_width_ = 0.0;
  double pixels_per_frame_ = 0.0;
  double scroll_ = 0.0;
};

}  // namespace lapian::core
