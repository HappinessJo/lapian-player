#pragma once

#include <cstdint>

#include <QWidget>

#include "app/ui/Tokens.h"

namespace lapian::core {
class TimelineModel;
}

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

// 时间轴上的两条滑块（规划 §2.2 + 原型 plan.html `.ztrack` / `.wtrack`）：
//
//  kView（单值条：行高 13 / 视觉轨道 4）——**全片坐标**上的"视图窗"：
//    拖中间 = 平移（改 TimelineModel::scroll）
//    拖两端 = 缩放（改 pixels_per_frame，另一端固定）
//    点空白 = 把视图窗居中到点击处
//    滚轮 = 以播放头为锚点缩放；Shift+滚轮 = 平移
//    双击   = 适应窗口
//  kWork（区间条：行高 15 / 视觉轨道 10）——**可见范围坐标**上的工作区（A/B）：
//    拖两端 = 改 A/B（走 MainWindow 既有的 SetLoopIn/SetLoopOut 语义）
//    拖中间 = 平移区间；点空白 = 把区间平移到点击处（保跨度）
//    双击   = 清除 A/B
//
// 铁律（规划 Q3）：两条滑块都**只读** TimelineModel（+ MainWindow 给的工作区值），
// 自己不存任何视图状态；手势只发请求，夹取仍走 TimelineWidget → TimelineModel
// 的同一条路径（滚轮 / +- / 适配 / 拖拽共用）。
class RangeBar : public QWidget {
  Q_OBJECT

 public:
  enum class Mode {
    kView = 0,
    kWork = 1,
  };

  explicit RangeBar(Mode mode, QWidget* parent = nullptr);

  // 视图唯一真源（画布同一个对象）。空指针 = 无媒体，条整体置灰。
  void SetModel(const lapian::core::TimelineModel* model);
  // 工作区（区间条用；单值条忽略）。
  void SetWorkRange(bool has_in, bool has_out, bool enabled, std::int64_t in,
                    std::int64_t out);
  void SetPlayheadFrame(std::int64_t frame);
  void SetMediaReady(bool ready);
  void ApplyTokens(const lapian::ui::Tokens& tokens);
  // 视图（scroll/pixels_per_frame）变化后重算几何：不传值，直接重读 TimelineModel。
  void RefreshView();

  Mode mode() const { return mode_; }

  // 测试缝（只读、无副作用）：
  // 视图窗覆盖的帧区间（与画布可见范围同源）；工作区条返回 A/B。
  QString ViewWindowForTest() const;
  QRect thumbGeometryForTest() const { return thumb_rect_; }
  QRect leftHandleGeometryForTest() const { return left_handle_rect_; }
  QRect rightHandleGeometryForTest() const { return right_handle_rect_; }
  // 合成一次拖拽（与真实鼠标事件走同一路径），返回是否命中可拖区域。
  bool DragForTest(int from_x, int to_x);

 signals:
  // 视图窗：起点帧 + 跨度帧（两条滑块都换算到帧，夹取交给 TimelineWidget）。
  void viewWindowRequested(double start_frame, double span_frames);
  void scrollRequested(double scroll_px);
  void zoomAtRequested(double factor, double anchor_x);
  void fitRequested();
  // 工作区：拖左端 / 拖右端 / 拖中间（同时给两端）/ 双击清除。
  void workInRequested(qint64 frame);
  void workOutRequested(qint64 frame);
  void workPanRequested(qint64 in, qint64 out);
  void workClearRequested();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  enum class Grab {
    kNone = 0,
    kLeft,
    kRight,
    kMiddle,
  };

  double TrackLeft() const;
  double TrackWidth() const;
  int TrackHeight() const;
  int TrackTop() const;
  // 单值条：整条轨道 = 全片 [0, frame_count]。
  double XForFrame(double frame) const;
  // 区间条：整条轨道 = 画布可见范围。
  double XForViewFrame(double frame) const;
  double FrameForX(double x) const;         // 全片映射反解
  double ViewFrameForX(double x) const;     // 可见范围映射反解
  void ComputeRects();
  void UpdateHandleWidgets();
  void UpdateCursor(const QPoint& pos);
  void BeginDrag(Grab grab, double press_x);
  void ApplyDrag(double x);
  bool HasMedia() const;
  double ViewStartFrame() const;
  double ViewSpanFrames() const;
  qint64 MaxFrame() const;

  Mode mode_ = Mode::kView;
  const lapian::core::TimelineModel* model_ = nullptr;
  bool media_ready_ = false;

  bool has_in_ = false;
  bool has_out_ = false;
  bool work_enabled_ = false;
  std::int64_t work_in_ = 0;
  std::int64_t work_out_ = 0;
  std::int64_t playhead_ = 0;

  bool dragging_ = false;
  Grab grab_ = Grab::kNone;
  double drag_start_x_ = 0.0;
  double drag_start_view_ = 0.0;   // 拖动开始时的视图起点帧
  double drag_start_span_ = 0.0;   // 拖动开始时的视图跨度帧
  std::int64_t drag_start_in_ = 0;
  std::int64_t drag_start_out_ = 0;

  QRect thumb_rect_;
  QRect left_handle_rect_;
  QRect right_handle_rect_;

  // 只做 objectName 可寻址 + 几何断言用；不吃鼠标事件（命中在父控件里判）。
  QWidget* left_handle_widget_ = nullptr;
  QWidget* right_handle_widget_ = nullptr;

  lapian::ui::Tokens tokens_ = lapian::ui::TokensFor(lapian::ui::Density::kRegular);
};
