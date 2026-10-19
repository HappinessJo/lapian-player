#pragma once

#include <QWidget>

#include <cstdint>

#include "core/TimelineModel.h"

class QPaintEvent;
class QResizeEvent;
class QScrollBar;
class QWheelEvent;

// T2.7 时间轴：帧刻度、缩放、横向滚动与播放头。
// 只负责渲染和视图状态；点击/拖动定位留给 T2.8，不在这里 seek 或暂停。
class TimelineWidget : public QWidget {
 public:
  explicit TimelineWidget(QWidget* parent = nullptr);

  void Clear();
  void SetFrameCount(std::int64_t count);
  void SetCurrentFrame(std::int64_t frame);
  std::int64_t current_frame() const { return current_frame_; }

  // 播放头离开可见区时是否做最小滚动跟随。T2.8 拖拽期间可暂时关闭。
  void SetFollowPlayhead(bool follow) { follow_playhead_ = follow; }
  bool follow_playhead() const { return follow_playhead_; }

  void FitToWindow();
  void ZoomIn();
  void ZoomOut();
  // 直接指定比例，主要供测试/后续缩放档位使用；仍受 fit/8px 上下限约束。
  void SetPixelsPerFrame(double pixels_per_frame);
  double pixels_per_frame() const { return model_.pixels_per_frame(); }

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  static constexpr int kScrollBarHeight = 14;

  int RulerHeight() const;
  void SyncScrollBar();
  void ApplyZoom(double factor, double anchor_x);

  lapian::core::TimelineModel model_;
  QScrollBar* scroll_bar_ = nullptr;
  std::int64_t current_frame_ = 0;
  bool fit_mode_ = true;
  bool follow_playhead_ = true;
};
