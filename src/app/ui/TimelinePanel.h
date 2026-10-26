#pragma once

#include <cstdint>

#include <QWidget>

#include "app/ui/Tokens.h"
#include "core/LoopRange.h"

class QAction;
class QLabel;
class QToolButton;
class RangeBar;
class TimelineWidget;

// 时间轴面板（规划 §2.1/§4.5 + 原型 plan.html）：
//   面板头 26（工作区/视图/缩放；dense 无头）+ 视图窗滑块 13 + 工作区滑块 15 + 画布。
// 3B 起画布 = 既有 TimelineWidget 原对象（由 MainWindow 经 AdoptTimeline 塞进来）。
// 两条滑块只读 TimelineModel，**不存第二份视图状态**（规划 Q3）。
class TimelinePanel : public QWidget {
  Q_OBJECT

 public:
  explicit TimelinePanel(QWidget* parent = nullptr);

  // 画布容器：既有 TimelineWidget 由 MainWindow 塞进来。
  QWidget* canvasHost() const { return canvas_host_; }
  QWidget* head() const { return head_; }
  RangeBar* viewBar() const { return view_bar_; }
  RangeBar* workBar() const { return work_bar_; }
  TimelineWidget* timeline() const { return timeline_; }

  // 画布（原对象）：接信号 + 把模型交给两条滑块。旧壳不调用（没有面板）。
  void AdoptTimeline(TimelineWidget* timeline);
  // 工作区（A/B）：唯一数据源是 MainWindow 的 LoopRange，这里只投影。
  void SetWorkRange(const lapian::core::LoopRange& range);
  void SetMediaReady(bool ready);
  void SetPlayheadFrame(std::int64_t frame);

  // 缩放按钮绑既有 QAction（zoom_in / zoom_out）；适配没有 QAction，用信号。
  void BindActions(QAction* zoom_in, QAction* zoom_out);
  void UpdateMeta(const QString& work_text, const QString& view_text);
  void ApplyTokens(const lapian::ui::Tokens& tokens);
  void SetHeadVisible(bool visible);
  bool head_visible() const;

 signals:
  void fitRequested();
  // 面板头（工作区/视图数值）+ 循环按钮订阅；画布每次缩放/滚动/视口变化都发。
  void viewChanged();
  // 工作区滑块手势（MainWindow 按既有 SetLoopIn/SetLoopOut 语义落地）。
  void workInRequested(qint64 frame);
  void workOutRequested(qint64 frame);
  void workPanRequested(qint64 in, qint64 out);
  void workClearRequested();

 private:
  QWidget* head_ = nullptr;
  QLabel* work_meta_ = nullptr;
  QLabel* view_meta_ = nullptr;
  QToolButton* zoom_out_ = nullptr;
  QToolButton* zoom_in_ = nullptr;
  QToolButton* fit_ = nullptr;
  RangeBar* view_bar_ = nullptr;
  RangeBar* work_bar_ = nullptr;
  QWidget* canvas_host_ = nullptr;
  TimelineWidget* timeline_ = nullptr;
};
