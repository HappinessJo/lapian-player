#pragma once

#include <QWidget>

#include <cstdint>

#include "core/TimelineModel.h"

class QMouseEvent;
class QPaintEvent;
class QPointF;
class QResizeEvent;
class QScrollBar;
class QWheelEvent;

// T2.7/T2.8 时间轴：帧刻度、缩放、横向滚动、播放头与指针命中。
// 只负责视图与目标帧语义；暂停/seek 由 MainWindow 处理，T2.9 再接预览解码。
class TimelineWidget : public QWidget {
  Q_OBJECT

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

  // 指针命中：第 N 帧占据 [N, N+1)，按物理像素对齐后 floor。
  // T2.8 的鼠标事件与 T2.9/T2.10 的拖动路径共用这一入口。
  std::int64_t FrameAtPosition(const QPointF& position) const;

 signals:
  // 按下时发一次；拖动中目标帧变化时发；松开时发最终目标帧。
  void scrubStarted(qint64 frame);
  void scrubTargetChanged(qint64 frame);
  void scrubFinished(qint64 frame);

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
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
  std::int64_t drag_target_frame_ = 0;
  bool fit_mode_ = true;
  bool follow_playhead_ = true;
  bool follow_before_scrub_ = true;
  bool scrubbing_ = false;
};
