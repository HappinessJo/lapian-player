#pragma once

#include <QWidget>

#include "app/ui/Tokens.h"

class QAction;
class QLabel;
class QToolButton;

// 时间轴面板（规划 §2.1 + 原型 plan.html）：26px 面板头（工作区/视图/缩放）+ 画布。
// 3A 的画布仍是既有 TimelineWidget（两条滑块 RangeBar 属 3B）；
// 面板头只是把"工作区/视图/缩放"这三个数值的唯一常显位置摆出来。
class TimelinePanel : public QWidget {
  Q_OBJECT

 public:
  explicit TimelinePanel(QWidget* parent = nullptr);

  // 画布容器：既有 TimelineWidget 由 MainWindow 塞进来。
  QWidget* canvasHost() const { return canvas_host_; }
  QWidget* head() const { return head_; }

  // 缩放按钮绑既有 QAction（zoom_in / zoom_out）；适配没有 QAction，用信号。
  void BindActions(QAction* zoom_in, QAction* zoom_out);
  void UpdateMeta(const QString& work_text, const QString& view_text);
  void ApplyTokens(const lapian::ui::Tokens& tokens);
  void SetHeadVisible(bool visible);

 signals:
  void fitRequested();

 private:
  QWidget* head_ = nullptr;
  QLabel* work_meta_ = nullptr;
  QLabel* view_meta_ = nullptr;
  QToolButton* zoom_out_ = nullptr;
  QToolButton* zoom_in_ = nullptr;
  QToolButton* fit_ = nullptr;
  QWidget* canvas_host_ = nullptr;
};
