// T2.7 时间轴几何单测 + 43.2 万帧（2h@60fps）离屏绘制基准。

#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "app/TimelineWidget.h"
#include "core/TimelineModel.h"

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
  if (condition) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n", what.c_str());
    ++g_failures;
  }
}

bool NearlyEqual(double a, double b, double scale = 1e-9) {
  const double diff = std::abs(a - b);
  return diff <= scale * std::max({1.0, std::abs(a), std::abs(b)});
}

void TestModel() {
  using lapian::core::TimelineModel;

  TimelineModel model;
  model.SetFrameCount(432000);
  model.SetViewportWidth(1920.0);
  model.Fit();

  Check(NearlyEqual(model.pixels_per_frame(), 1920.0 / 432000.0),
        "43.2 万帧 fit 比例正确");
  Check(NearlyEqual(model.MaxScroll(), 0.0), "fit 时无可滚动距离");
  Check(model.FrameAtX(0.0) == 0, "x=0 映射到第 0 帧");
  Check(model.FrameAtX(-10.0) == 0, "左侧越界 clamp 到第 0 帧");
  Check(model.FrameAtX(1e12) == 431999, "右侧越界 clamp 到末帧");

  model.SetPixelsPerFrame(8.0);
  Check(NearlyEqual(model.pixels_per_frame(), 8.0), "8px/帧可达");
  Check(NearlyEqual(model.FrameToX(10), 80.0), "第 10 帧 x=80");
  Check(model.FrameAtX(80.0) == 10, "x=80 命中第 10 帧");
  Check(model.FrameAtX(87.999) == 10, "第 10 帧右边界内仍为第 10 帧");
  Check(model.FrameAtX(88.0) == 11, "x=88 命中第 11 帧（floor）");

  model.SetScroll(0.0);
  model.SetPixelsPerFrame(2.0);
  const double anchor = 400.0;
  const double anchor_frame =
      (model.scroll() + anchor) / model.pixels_per_frame();
  model.ZoomAt(2.0, anchor);
  Check(NearlyEqual(model.pixels_per_frame(), 4.0), "缩放后比例翻倍");
  Check(NearlyEqual((model.scroll() + anchor) / model.pixels_per_frame(),
                    anchor_frame),
        "缩放保持鼠标锚点下的连续帧位置");
  Check(model.FrameAtX(anchor) == static_cast<std::int64_t>(
                                      std::floor(anchor_frame)),
        "锚点映射的帧号不变");

  model.SetPixelsPerFrame(8.0);
  model.ZoomAt(2.0, anchor);
  Check(NearlyEqual(model.pixels_per_frame(), 8.0), "缩放上限 8px/帧");
  model.SetPixelsPerFrame(0.0001);
  Check(model.pixels_per_frame() >= model.MinPixelsPerFrame() - 1e-12,
        "缩放不低于全片适配");

  model.SetPixelsPerFrame(8.0);
  model.SetScroll(-10.0);
  Check(NearlyEqual(model.scroll(), 0.0), "滚动左侧 clamp 到 0");
  model.SetScroll(1e12);
  Check(NearlyEqual(model.scroll(), model.MaxScroll()), "滚动右侧 clamp 到上限");

  model.SetScroll(0.0);
  model.EnsureFrameVisible(1000, 8.0);
  const double visible_x = model.FrameToX(1000);
  Check(visible_x >= 0.0 && visible_x + 8.0 <= model.viewport_width(),
        "EnsureFrameVisible 把第 1000 帧带入可见区");

  model.Fit();
  const std::vector<lapian::core::TimelineTick> ticks = model.BuildTicks();
  Check(!ticks.empty(), "fit 时生成可见刻度");
  Check(ticks.size() < 600, "43.2 万帧 fit 的刻度数量有界");
  bool sorted = true;
  bool has_major = false;
  bool in_range = true;
  for (std::size_t i = 0; i < ticks.size(); ++i) {
    has_major = has_major || ticks[i].major;
    in_range = in_range && ticks[i].x >= -0.5 &&
               ticks[i].x <= model.viewport_width() + 0.5;
    if (i > 0 && ticks[i - 1].frame >= ticks[i].frame) {
      sorted = false;
    }
  }
  Check(sorted, "刻度按帧号升序且不重复");
  Check(has_major, "刻度中包含带标签的主刻度");
  Check(in_range, "刻度都位于可见区内");

  Check(TimelineModel::NiceStep(64.0, 8.0) == 10, "NiceStep(8px)=10");
  Check(TimelineModel::NiceStep(64.0, 2.0) == 50, "NiceStep(2px)=50");
  Check(TimelineModel::NiceStep(64.0, 1.0) == 100, "NiceStep(1px)=100");
  Check(TimelineModel::NiceStep(64.0, 0.1) == 1000, "NiceStep(0.1px)=1000");

  model.SetFrameCount(120);
  model.SetViewportWidth(1920.0);
  model.Fit();
  Check(NearlyEqual(model.pixels_per_frame(), 16.0),
        "短片段 fit 可为 16px/帧");
  Check(NearlyEqual(model.MaxPixelsPerFrame(), 16.0),
        "短片段缩放上限放宽到 fit");
  Check(NearlyEqual(model.MaxScroll(), 0.0), "短片段 fit 无滚动");

  model.SetFrameCount(0);
  model.Fit();
  Check(NearlyEqual(model.pixels_per_frame(), 0.0), "空时间轴比例为 0");
  Check(model.BuildTicks().empty(), "空时间轴无刻度");
  Check(model.FrameAtX(123.0) == 0, "空时间轴坐标 clamp 到 0");
}

void TestWidgetViewState() {
  TimelineWidget widget;
  widget.resize(1280, 72);
  widget.SetFrameCount(720);
  widget.SetCurrentFrame(600);

  const double fit = widget.pixels_per_frame();
  Check(widget.current_frame() == 600, "时间轴当前帧为 600");

  widget.ZoomIn();
  Check(widget.pixels_per_frame() > fit, "ZoomIn 增大 px/帧");
  Check(widget.current_frame() == 600, "缩放不改变当前帧");

  widget.SetPixelsPerFrame(8.0);
  const double max = widget.pixels_per_frame();
  widget.ZoomIn();
  Check(NearlyEqual(widget.pixels_per_frame(), max), "ZoomIn 不超过上限");
  widget.ZoomOut();
  Check(widget.pixels_per_frame() < max, "ZoomOut 降低 px/帧");

  widget.FitToWindow();
  Check(NearlyEqual(widget.pixels_per_frame(), fit), "FitToWindow 复位全片适配");

  widget.SetCurrentFrame(10000);
  Check(widget.current_frame() == 719, "当前帧右侧 clamp 到末帧");
  widget.SetCurrentFrame(-5);
  Check(widget.current_frame() == 0, "当前帧左侧 clamp 到首帧");

  widget.Clear();
  Check(widget.current_frame() == 0, "Clear 后当前帧归零");
  Check(NearlyEqual(widget.pixels_per_frame(), 0.0), "Clear 后时间轴为空");
  widget.ZoomIn();
  Check(NearlyEqual(widget.pixels_per_frame(), 0.0), "空时间轴缩放无效果");
}

void TestPointerInteraction() {
  TimelineWidget widget;
  widget.resize(1280, 72);
  widget.SetFrameCount(720);
  widget.SetPixelsPerFrame(8.0);

  Check(widget.FrameAtPosition(QPointF(-1.0, 30.0)) == 0,
        "负坐标命中首帧");
  Check(widget.FrameAtPosition(QPointF(0.0, 30.0)) == 0,
        "x=0 命中第 0 帧");
  Check(widget.FrameAtPosition(QPointF(7.999, 30.0)) == 0,
        "第 0 帧右边界内命中第 0 帧");
  Check(widget.FrameAtPosition(QPointF(8.0, 30.0)) == 1,
        "x=8 命中第 1 帧");
  Check(widget.FrameAtPosition(QPointF(80.0, 30.0)) == 10,
        "x=80 命中第 10 帧");

  qint64 started = -1;
  qint64 changed = -1;
  qint64 finished = -1;
  QObject::connect(&widget, &TimelineWidget::scrubStarted,
                   [&started](qint64 frame) { started = frame; });
  QObject::connect(&widget, &TimelineWidget::scrubTargetChanged,
                   [&changed](qint64 frame) { changed = frame; });
  QObject::connect(&widget, &TimelineWidget::scrubFinished,
                   [&finished](qint64 frame) { finished = frame; });

  QMouseEvent press(QEvent::MouseButtonPress, QPointF(80.0, 30.0),
                    QPointF(80.0, 30.0), Qt::LeftButton, Qt::LeftButton,
                    Qt::NoModifier);
  QApplication::sendEvent(&widget, &press);
  Check(started == 10, "按下时间轴发出目标帧 10");
  Check(widget.current_frame() == 10, "按下后播放头到第 10 帧");

  QMouseEvent move(QEvent::MouseMove, QPointF(160.0, 30.0),
                   QPointF(160.0, 30.0), Qt::NoButton, Qt::LeftButton,
                   Qt::NoModifier);
  QApplication::sendEvent(&widget, &move);
  Check(changed == 20, "拖动到 x=160 目标帧为 20");
  Check(widget.current_frame() == 20, "拖动中播放头跟随目标帧");

  QMouseEvent release(QEvent::MouseButtonRelease, QPointF(160.0, 30.0),
                      QPointF(160.0, 30.0), Qt::LeftButton, Qt::NoButton,
                      Qt::NoModifier);
  QApplication::sendEvent(&widget, &release);
  Check(finished == 20, "松手发出最终目标帧 20");
  Check(widget.current_frame() == 20, "松手后播放头仍在第 20 帧");
}

// C5：时间轴放大后拖动播放头的边缘自动平移（auto-pan）。
void SendTimelineMouse(TimelineWidget& widget, QEvent::Type type,
                       const QPointF& position, Qt::MouseButton button,
                       Qt::MouseButtons buttons) {
  QMouseEvent event(type, position, position, button, buttons, Qt::NoModifier);
  QApplication::sendEvent(&widget, &event);
}

// 400px 宽 + 2000 帧 + 视图窗 50 帧 = 8px/帧（最大缩放），内容 16000px，
// MaxScroll=15600；视图从第 1000 帧开始（scroll=8000）。
void PrepareZoomedTimeline(TimelineWidget& widget) {
  widget.resize(400, 72);
  widget.SetFrameCount(2000);
  widget.SetViewWindow(1000, 50);
}

void SpinEventLoop(int milliseconds) {
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec();
}

void TestAutoPanRightEdge() {
  TimelineWidget widget;
  PrepareZoomedTimeline(widget);
  Check(widget.model().MaxScroll() > 0.0 && !widget.model().IsAtFit(),
        "auto-pan 右边缘前置：放大后可滚动");
  Check(NearlyEqual(widget.scroll_px(), 8000.0), "auto-pan 前置：视图从第 1000 帧开始");

  qint64 started = -1;
  qint64 finished = -1;
  qint64 changed = -1;
  int changed_count = 0;
  QObject::connect(&widget, &TimelineWidget::scrubStarted,
                   [&started](qint64 frame) { started = frame; });
  QObject::connect(&widget, &TimelineWidget::scrubTargetChanged,
                   [&changed, &changed_count](qint64 frame) {
                     changed = frame;
                     ++changed_count;
                   });
  QObject::connect(&widget, &TimelineWidget::scrubFinished,
                   [&finished](qint64 frame) { finished = frame; });

  SendTimelineMouse(widget, QEvent::MouseButtonPress, QPointF(200.0, 30.0),
                    Qt::LeftButton, Qt::LeftButton);
  const double scroll_before = widget.scroll_px();
  const std::int64_t current_before = widget.current_frame();
  Check(started == 1025 && current_before == 1025,
        "auto-pan 前置：按下 x=200 目标为第 1025 帧");
  Check(!widget.AutoPanActiveForTest(), "只按下不启动 auto-pan");
  Check(NearlyEqual(widget.scroll_px(), scroll_before), "只按下不滚动");

  SendTimelineMouse(widget, QEvent::MouseMove, QPointF(300.0, 30.0),
                    Qt::NoButton, Qt::LeftButton);
  Check(!widget.AutoPanActiveForTest(), "画布中间拖动不启动 auto-pan");
  Check(NearlyEqual(widget.scroll_px(), scroll_before), "画布中间拖动不滚动");

  // 拖到 x=420（超出 400px 画布 20px）：进入边缘先同步走一步（不等 16ms）。
  SendTimelineMouse(widget, QEvent::MouseMove, QPointF(420.0, 30.0),
                    Qt::NoButton, Qt::LeftButton);
  Check(widget.AutoPanActiveForTest(), "拖到右边缘启动 auto-pan");
  const double entry_step = widget.scroll_px() - scroll_before;
  Check(NearlyEqual(entry_step, 16.5),
        "进入右边缘立即滚动 6+0.35*30=16.5px");
  Check(widget.current_frame() > current_before, "右边缘目标帧比拖动前大");

  // 更远（x=1000，overshoot=610）：单步被 28px 上限夹住。
  SendTimelineMouse(widget, QEvent::MouseMove, QPointF(1000.0, 30.0),
                    Qt::NoButton, Qt::LeftButton);
  const double scroll_before_capped = widget.scroll_px();
  widget.AutoPanStepForTest();
  Check(NearlyEqual(widget.scroll_px() - scroll_before_capped, 28.0),
        "超出边缘很远时单步上限 28px");

  for (int i = 0; i < 12; ++i) {
    widget.AutoPanStepForTest();
  }
  const double scroll_after_ticks = widget.scroll_px();
  Check(scroll_after_ticks > scroll_before + 300.0, "连续 tick 持续右移");
  Check(widget.current_frame() > current_before + 20, "连续 tick 后当前帧增大");
  Check(changed_count > 0 && changed == widget.current_frame(),
        "scrubTargetChanged 收到新目标");

  // 真实 16ms 定时器（不是只有测试钩子）：200ms 事件循环内继续滚动。
  const double scroll_before_timer = widget.scroll_px();
  SpinEventLoop(200);
  Check(widget.scroll_px() > scroll_before_timer,
        "16ms 定时器驱动 auto-pan（200ms 内继续滚动）");

  std::printf(
      "[auto-pan] right scroll=%.1f->%.1f delta=+%.1f current=%lld->%lld "
      "view=%lld..%lld active=%d\n",
      scroll_before, widget.scroll_px(), widget.scroll_px() - scroll_before,
      static_cast<long long>(current_before),
      static_cast<long long>(widget.current_frame()),
      static_cast<long long>(widget.ViewFrames().first),
      static_cast<long long>(widget.ViewFrames().second),
      widget.AutoPanActiveForTest() ? 1 : 0);

  const std::int64_t last_current = widget.current_frame();
  const double scroll_at_release = widget.scroll_px();
  const auto view_at_release = widget.ViewFrames();
  SendTimelineMouse(widget, QEvent::MouseButtonRelease, QPointF(420.0, 30.0),
                    Qt::LeftButton, Qt::NoButton);
  Check(finished == last_current, "松手发出最后 tick 的目标帧");
  Check(widget.current_frame() == last_current, "松手停在最后目标帧");
  Check(finished == widget.current_frame(), "scrubFinished == current_frame");
  Check(!widget.AutoPanActiveForTest(), "松手后 auto-pan 停止");
  Check(NearlyEqual(widget.scroll_px(), scroll_at_release), "松手不额外滚动");
  Check(widget.ViewFrames() == view_at_release, "松手不改变视图范围");
}

void TestAutoPanLeftEdge() {
  TimelineWidget widget;
  PrepareZoomedTimeline(widget);

  qint64 finished = -1;
  QObject::connect(&widget, &TimelineWidget::scrubFinished,
                   [&finished](qint64 frame) { finished = frame; });

  SendTimelineMouse(widget, QEvent::MouseButtonPress, QPointF(200.0, 30.0),
                    Qt::LeftButton, Qt::LeftButton);
  const double scroll_before = widget.scroll_px();
  const std::int64_t current_before = widget.current_frame();

  SendTimelineMouse(widget, QEvent::MouseMove, QPointF(-20.0, 30.0),
                    Qt::NoButton, Qt::LeftButton);
  Check(widget.AutoPanActiveForTest(), "拖到左边缘启动 auto-pan");
  const double entry_step = scroll_before - widget.scroll_px();
  Check(NearlyEqual(entry_step, 16.5),
        "进入左边缘立即滚动 -16.5px（左右对称）");
  Check(widget.current_frame() < current_before, "左边缘目标帧比拖动前小");

  for (int i = 0; i < 15; ++i) {
    widget.AutoPanStepForTest();
  }
  const double scroll_after_ticks = widget.scroll_px();
  Check(scroll_after_ticks < scroll_before, "连续 tick 持续左移");
  Check(widget.current_frame() < current_before, "连续 tick 后当前帧继续减小");

  std::printf(
      "[auto-pan] left scroll=%.1f->%.1f delta=%.1f current=%lld->%lld "
      "view=%lld..%lld active=%d\n",
      scroll_before, scroll_after_ticks, scroll_after_ticks - scroll_before,
      static_cast<long long>(current_before),
      static_cast<long long>(widget.current_frame()),
      static_cast<long long>(widget.ViewFrames().first),
      static_cast<long long>(widget.ViewFrames().second),
      widget.AutoPanActiveForTest() ? 1 : 0);

  const std::int64_t last_current = widget.current_frame();
  SendTimelineMouse(widget, QEvent::MouseButtonRelease, QPointF(-20.0, 30.0),
                    Qt::LeftButton, Qt::NoButton);
  Check(finished == last_current, "左边缘松手停在最后目标帧");
  Check(!widget.AutoPanActiveForTest(), "左边缘松手后 auto-pan 停止");

  // 到内容首帧（scroll=0）必须停住，不越过 0。
  widget.SetScrollPixels(0.0);
  Check(!widget.AutoPanActiveForTest(), "外部 SetScrollPixels 停 auto-pan");
  SendTimelineMouse(widget, QEvent::MouseButtonPress, QPointF(200.0, 30.0),
                    Qt::LeftButton, Qt::LeftButton);
  Check(widget.current_frame() == 25, "scroll=0 时按下 x=200 目标为第 25 帧");
  SendTimelineMouse(widget, QEvent::MouseMove, QPointF(-20.0, 30.0),
                    Qt::NoButton, Qt::LeftButton);
  Check(!widget.AutoPanActiveForTest(), "已到内容首帧时 auto-pan 立即停止");
  Check(NearlyEqual(widget.scroll_px(), 0.0), "内容首帧不再向左滚动");
  Check(widget.current_frame() == 0, "内容首帧目标 clamp 到第 0 帧");
  for (int i = 0; i < 5; ++i) {
    widget.AutoPanStepForTest();
  }
  Check(NearlyEqual(widget.scroll_px(), 0.0) && widget.current_frame() == 0,
        "内容首帧连续 tick 不越过 0");
  SendTimelineMouse(widget, QEvent::MouseButtonRelease, QPointF(-20.0, 30.0),
                    Qt::LeftButton, Qt::NoButton);
  Check(finished == 0 && widget.current_frame() == 0, "内容首帧松手为第 0 帧");
}

void TestAutoPanDisabledAtFit() {
  TimelineWidget widget;
  widget.resize(400, 72);
  widget.SetFrameCount(2000);
  widget.FitToWindow();
  Check(NearlyEqual(widget.model().MaxScroll(), 0.0) && widget.model().IsAtFit(),
        "fit 前置：全片在视图内、MaxScroll=0");
  const double scroll_fit = widget.scroll_px();
  const auto view_fit = widget.ViewFrames();
  Check(view_fit.first == 0 && view_fit.second == 1999,
        "fit 前置：可见范围 0..1999");

  qint64 changed = -1;
  qint64 finished = -1;
  QObject::connect(&widget, &TimelineWidget::scrubTargetChanged,
                   [&changed](qint64 frame) { changed = frame; });
  QObject::connect(&widget, &TimelineWidget::scrubFinished,
                   [&finished](qint64 frame) { finished = frame; });

  // 右边缘：不启动、不滚动，目标 clamp 到末帧。
  SendTimelineMouse(widget, QEvent::MouseButtonPress, QPointF(200.0, 30.0),
                    Qt::LeftButton, Qt::LeftButton);
  Check(widget.current_frame() == 1000, "fit 时按下 x=200 目标为第 1000 帧");
  SendTimelineMouse(widget, QEvent::MouseMove, QPointF(420.0, 30.0),
                    Qt::NoButton, Qt::LeftButton);
  widget.AutoPanStepForTest();
  SpinEventLoop(300);
  Check(!widget.AutoPanActiveForTest(), "fit 时右边缘绝不启动 auto-pan");
  Check(NearlyEqual(widget.scroll_px(), scroll_fit), "fit 时右边缘不滚动");
  Check(widget.ViewFrames() == view_fit, "fit 时右边缘视图范围不变");
  Check(widget.current_frame() == 1999, "fit 时右侧越界目标 clamp 到末帧");
  Check(changed == 1999, "fit 时 scrubTargetChanged 仍报末帧");

  // 左边缘：同样不滚动，目标 clamp 到首帧。
  SendTimelineMouse(widget, QEvent::MouseMove, QPointF(-20.0, 30.0),
                    Qt::NoButton, Qt::LeftButton);
  widget.AutoPanStepForTest();
  SpinEventLoop(300);
  Check(!widget.AutoPanActiveForTest(), "fit 时左边缘绝不启动 auto-pan");
  Check(NearlyEqual(widget.scroll_px(), scroll_fit), "fit 时左边缘不滚动");
  Check(widget.ViewFrames() == view_fit, "fit 时左边缘视图范围不变");
  Check(widget.current_frame() == 0, "fit 时左侧越界目标 clamp 到首帧");
  SendTimelineMouse(widget, QEvent::MouseButtonRelease, QPointF(-20.0, 30.0),
                    Qt::LeftButton, Qt::NoButton);
  Check(finished == 0 && widget.current_frame() == 0, "fit 时松手停在首帧");

  // "缩到最小" = 比例被夹回 fit：同样不许 auto-pan。
  widget.SetPixelsPerFrame(0.001);
  Check(widget.model().IsAtFit(), "最小缩放被夹到全片适配");
  SendTimelineMouse(widget, QEvent::MouseButtonPress, QPointF(200.0, 30.0),
                    Qt::LeftButton, Qt::LeftButton);
  SendTimelineMouse(widget, QEvent::MouseMove, QPointF(420.0, 30.0),
                    Qt::NoButton, Qt::LeftButton);
  widget.AutoPanStepForTest();
  Check(!widget.AutoPanActiveForTest(), "缩到最小时右边缘不启动 auto-pan");
  Check(NearlyEqual(widget.scroll_px(), 0.0), "缩到最小时右边缘不滚动");
  Check(widget.current_frame() == 1999, "缩到最小时目标 clamp 到末帧");
  SendTimelineMouse(widget, QEvent::MouseMove, QPointF(-20.0, 30.0),
                    Qt::NoButton, Qt::LeftButton);
  SendTimelineMouse(widget, QEvent::MouseButtonRelease, QPointF(-20.0, 30.0),
                    Qt::LeftButton, Qt::NoButton);
  Check(widget.current_frame() == 0, "缩到最小时左侧目标 clamp 到首帧");

  std::printf(
      "[auto-pan] fit scroll=%.1f->%.1f view=%lld..%lld active=0 current=%lld\n",
      scroll_fit, widget.scroll_px(),
      static_cast<long long>(widget.ViewFrames().first),
      static_cast<long long>(widget.ViewFrames().second),
      static_cast<long long>(widget.current_frame()));
}

void TestAutoPanStopsAtContentEnd() {
  TimelineWidget widget;
  PrepareZoomedTimeline(widget);
  qint64 finished = -1;
  QObject::connect(&widget, &TimelineWidget::scrubFinished,
                   [&finished](qint64 frame) { finished = frame; });

  widget.SetScrollPixels(widget.model().MaxScroll());
  const double scroll_at_end = widget.scroll_px();
  Check(NearlyEqual(scroll_at_end, 15600.0), "末尾前置：scroll 在 MaxScroll");

  SendTimelineMouse(widget, QEvent::MouseButtonPress, QPointF(200.0, 30.0),
                    Qt::LeftButton, Qt::LeftButton);
  Check(widget.current_frame() == 1975, "末尾前置：按下目标为第 1975 帧");
  SendTimelineMouse(widget, QEvent::MouseMove, QPointF(420.0, 30.0),
                    Qt::NoButton, Qt::LeftButton);
  Check(!widget.AutoPanActiveForTest(), "到内容末尾 auto-pan 立即停止");
  Check(NearlyEqual(widget.scroll_px(), scroll_at_end), "内容末尾不再滚动");
  Check(widget.current_frame() == 1999, "内容末尾目标为末帧");
  for (int i = 0; i < 8; ++i) {
    widget.AutoPanStepForTest();
  }
  Check(NearlyEqual(widget.scroll_px(), scroll_at_end), "末尾连续 tick 不越过上限");
  Check(widget.current_frame() == 1999, "末尾连续 tick 不循环回第 0 帧");

  std::printf("[auto-pan] end scroll=%.1f->%.1f active=0 current=%lld view=%lld..%lld\n",
              scroll_at_end, widget.scroll_px(),
              static_cast<long long>(widget.current_frame()),
              static_cast<long long>(widget.ViewFrames().first),
              static_cast<long long>(widget.ViewFrames().second));

  SendTimelineMouse(widget, QEvent::MouseButtonRelease, QPointF(420.0, 30.0),
                    Qt::LeftButton, Qt::NoButton);
  Check(finished == 1999 && widget.current_frame() == 1999,
        "末尾松手为末帧（不循环）");
  Check(!widget.AutoPanActiveForTest(), "末尾松手后 auto-pan 停止");
}

void TestAutoPanStopsOnViewChanges() {
  auto start_pan = [](TimelineWidget& widget) {
    PrepareZoomedTimeline(widget);
    SendTimelineMouse(widget, QEvent::MouseButtonPress, QPointF(200.0, 30.0),
                      Qt::LeftButton, Qt::LeftButton);
    SendTimelineMouse(widget, QEvent::MouseMove, QPointF(420.0, 30.0),
                      Qt::NoButton, Qt::LeftButton);
  };

  {
    TimelineWidget widget;
    start_pan(widget);
    Check(widget.AutoPanActiveForTest(), "SetFrameCount 前置：auto-pan 在跑");
    widget.SetFrameCount(0);
    Check(!widget.AutoPanActiveForTest(), "SetFrameCount(0) 停 auto-pan");
  }
  {
    TimelineWidget widget;
    start_pan(widget);
    widget.Clear();
    Check(!widget.AutoPanActiveForTest(), "Clear 停 auto-pan");
  }
  {
    TimelineWidget widget;
    start_pan(widget);
    widget.SetViewWindow(0, 100);
    Check(!widget.AutoPanActiveForTest(), "SetViewWindow 停 auto-pan");
  }
  {
    TimelineWidget widget;
    start_pan(widget);
    widget.SetPixelsPerFrame(2.0);
    Check(!widget.AutoPanActiveForTest(), "SetPixelsPerFrame 停 auto-pan");
  }
  {
    TimelineWidget widget;
    start_pan(widget);
    widget.SetScrollPixels(4000.0);
    Check(!widget.AutoPanActiveForTest(), "外部 SetScrollPixels 停 auto-pan");
    Check(NearlyEqual(widget.scroll_px(), 4000.0), "外部 SetScrollPixels 生效");
  }
  {
    TimelineWidget widget;
    start_pan(widget);
    widget.ZoomIn();
    Check(!widget.AutoPanActiveForTest(), "ZOOM 停 auto-pan");
  }
  {
    TimelineWidget widget;
    start_pan(widget);
    widget.FitToWindow();
    Check(!widget.AutoPanActiveForTest(), "FitToWindow 停 auto-pan");
  }
  {
    // resize 事件只在控件已创建后同步派发，先 show 一次再改几何。
    TimelineWidget widget;
    widget.show();
    QApplication::processEvents();
    start_pan(widget);
    Check(widget.AutoPanActiveForTest(), "resize 前置：auto-pan 在跑");
    widget.resize(500, 72);
    QApplication::processEvents();
    Check(!widget.AutoPanActiveForTest(), "resizeEvent 停 auto-pan");
  }
  {
    // 只按在右边缘、不拖动：不启动也不滚动。
    TimelineWidget widget;
    PrepareZoomedTimeline(widget);
    const double scroll_before = widget.scroll_px();
    SendTimelineMouse(widget, QEvent::MouseButtonPress, QPointF(395.0, 30.0),
                      Qt::LeftButton, Qt::LeftButton);
    Check(!widget.AutoPanActiveForTest(), "只按在右边缘不启动 auto-pan");
    SpinEventLoop(120);
    Check(NearlyEqual(widget.scroll_px(), scroll_before),
          "只按在右边缘不滚动");
    SendTimelineMouse(widget, QEvent::MouseButtonRelease, QPointF(395.0, 30.0),
                      Qt::LeftButton, Qt::NoButton);
    Check(!widget.AutoPanActiveForTest(), "松手后仍未启动 auto-pan");
  }
}

// C5：一次远拖到画布外（x=width+100）时目标帧必须单调推进。修前 mouseMove 用
// 原始 x 先把目标跳到画布外，第一次 tick 又用 clamp 后的 x 拉回来 → 播放头
// "先右跳再左退"、scrubTargetChanged 抖动。
void TestAutoPanNoBackJump() {
  {
    TimelineWidget widget;
    PrepareZoomedTimeline(widget);
    qint64 finished = -1;
    QObject::connect(&widget, &TimelineWidget::scrubFinished,
                     [&finished](qint64 frame) { finished = frame; });

    SendTimelineMouse(widget, QEvent::MouseButtonPress, QPointF(200.0, 30.0),
                      Qt::LeftButton, Qt::LeftButton);
    const double scroll_start = widget.scroll_px();
    const std::int64_t current_start = widget.current_frame();
    Check(current_start == 1025, "远拖前置：按下目标为第 1025 帧");

    // 单次 move 直接到 x=500（400px 画布外 100px），不经过边缘带。
    SendTimelineMouse(widget, QEvent::MouseMove, QPointF(500.0, 30.0),
                      Qt::NoButton, Qt::LeftButton);
    Check(widget.AutoPanActiveForTest(), "远拖到画布外启动 auto-pan");
    const std::int64_t current_after_move = widget.current_frame();
    Check(current_after_move >= current_start, "远拖落点目标不回退（先落在边缘帧）");

    bool current_monotonic = true;
    bool scroll_monotonic = true;
    bool view_monotonic = true;
    std::int64_t previous_current = current_after_move;
    double previous_scroll = widget.scroll_px();
    auto previous_view = widget.ViewFrames();
    std::string current_sequence = std::to_string(current_after_move);
    for (int i = 0; i < 15; ++i) {
      widget.AutoPanStepForTest();
      const std::int64_t current = widget.current_frame();
      current_monotonic = current_monotonic && current >= previous_current;
      scroll_monotonic = scroll_monotonic && widget.scroll_px() >= previous_scroll;
      const auto view = widget.ViewFrames();
      view_monotonic = view_monotonic && view.first >= previous_view.first;
      current_sequence += "->" + std::to_string(current);
      previous_current = current;
      previous_scroll = widget.scroll_px();
      previous_view = view;
    }
    Check(current_monotonic, "远拖右边缘 current_frame 单调不减（不回跳）");
    Check(scroll_monotonic, "远拖右边缘 scroll 单调不减");
    Check(view_monotonic, "远拖右边缘 ViewFrames().first 单调推进");
    Check(previous_current > current_start + 20, "远拖右边缘目标帧持续推进");
    Check(widget.scroll_px() > scroll_start + 300.0, "远拖右边缘视图持续右移");

    std::printf(
        "[auto-pan] no-backjump right current=%s scroll=%.1f->%.1f "
        "view=%lld..%lld active=%d\n",
        current_sequence.c_str(), scroll_start, widget.scroll_px(),
        static_cast<long long>(widget.ViewFrames().first),
        static_cast<long long>(widget.ViewFrames().second),
        widget.AutoPanActiveForTest() ? 1 : 0);

    const std::int64_t last_current = widget.current_frame();
    const double scroll_at_release = widget.scroll_px();
    const auto view_at_release = widget.ViewFrames();
    SendTimelineMouse(widget, QEvent::MouseButtonRelease, QPointF(500.0, 30.0),
                      Qt::LeftButton, Qt::NoButton);
    Check(finished == last_current, "画布外松手 scrubFinished == 最后一次 current");
    Check(widget.current_frame() == last_current, "画布外松手目标帧不回跳");
    Check(NearlyEqual(widget.scroll_px(), scroll_at_release),
          "画布外松手不再滚动");
    Check(widget.ViewFrames() == view_at_release, "画布外松手视图范围不变");
    Check(!widget.AutoPanActiveForTest(), "画布外松手 auto-pan 停止");
  }

  {
    // 左边缘对称：一次 move 到 x=-100。
    TimelineWidget widget;
    PrepareZoomedTimeline(widget);
    qint64 finished = -1;
    QObject::connect(&widget, &TimelineWidget::scrubFinished,
                     [&finished](qint64 frame) { finished = frame; });

    SendTimelineMouse(widget, QEvent::MouseButtonPress, QPointF(200.0, 30.0),
                      Qt::LeftButton, Qt::LeftButton);
    const double scroll_start = widget.scroll_px();
    const std::int64_t current_start = widget.current_frame();

    SendTimelineMouse(widget, QEvent::MouseMove, QPointF(-100.0, 30.0),
                      Qt::NoButton, Qt::LeftButton);
    Check(widget.AutoPanActiveForTest(), "远拖到左画布外启动 auto-pan");
    const std::int64_t current_after_move = widget.current_frame();
    Check(current_after_move <= current_start, "远拖左落点目标不前进（先落在边缘帧）");

    bool current_monotonic = true;
    bool scroll_monotonic = true;
    bool view_monotonic = true;
    std::int64_t previous_current = current_after_move;
    double previous_scroll = widget.scroll_px();
    auto previous_view = widget.ViewFrames();
    std::string current_sequence = std::to_string(current_after_move);
    for (int i = 0; i < 15; ++i) {
      widget.AutoPanStepForTest();
      const std::int64_t current = widget.current_frame();
      current_monotonic = current_monotonic && current <= previous_current;
      scroll_monotonic = scroll_monotonic && widget.scroll_px() <= previous_scroll;
      const auto view = widget.ViewFrames();
      view_monotonic = view_monotonic && view.first <= previous_view.first;
      current_sequence += "->" + std::to_string(current);
      previous_current = current;
      previous_scroll = widget.scroll_px();
      previous_view = view;
    }
    Check(current_monotonic, "远拖左边缘 current_frame 单调不增（不回跳）");
    Check(scroll_monotonic, "远拖左边缘 scroll 单调不增");
    Check(view_monotonic, "远拖左边缘 ViewFrames().first 单调左移");
    Check(previous_current < current_start - 20, "远拖左边缘目标帧持续左移");
    Check(widget.scroll_px() < scroll_start - 300.0, "远拖左边缘视图持续左移");

    std::printf(
        "[auto-pan] no-backjump left current=%s scroll=%.1f->%.1f "
        "view=%lld..%lld active=%d\n",
        current_sequence.c_str(), scroll_start, widget.scroll_px(),
        static_cast<long long>(widget.ViewFrames().first),
        static_cast<long long>(widget.ViewFrames().second),
        widget.AutoPanActiveForTest() ? 1 : 0);

    const std::int64_t last_current = widget.current_frame();
    SendTimelineMouse(widget, QEvent::MouseButtonRelease, QPointF(-100.0, 30.0),
                      Qt::LeftButton, Qt::NoButton);
    Check(finished == last_current, "左画布外松手 scrubFinished == 最后一次 current");
    Check(widget.current_frame() == last_current, "左画布外松手目标帧不回跳");
    Check(!widget.AutoPanActiveForTest(), "左画布外松手 auto-pan 停止");
  }
}

void TestWaveformRender() {
  TimelineWidget widget;
  widget.resize(960, 72);
  widget.SetFrameCount(120);
  widget.SetPixelsPerFrame(8.0);
  widget.show();
  QApplication::processEvents();

  widget.SetWaveformSource(
      []() { return static_cast<std::size_t>(1000); },
      [](std::size_t begin, std::size_t end) {
        if (begin <= 833 && end > 833) {
          return TimelineWidget::WaveformBucketView{30000, -30000};
        }
        return TimelineWidget::WaveformBucketView{0, 0};
      },
      0.0, 1.0 / 24.0,
      [](std::int64_t frame) { return frame * 41667; });
  Check(widget.height() == 128, "波形显示时时间轴增高 56px");

  QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::black);
  {
    QPainter painter(&image);
    widget.render(&painter);
  }

  int waveform_pixels = 0;
  for (int y = 58; y < 114; ++y) {
    for (int x = 800; x < 808; ++x) {
      const QColor color = image.pixelColor(x, y);
      if (color.blue() > 150 && color.blue() > color.red() + 40) {
        ++waveform_pixels;
      }
    }
  }
  Check(waveform_pixels > 0, "frame100 的 click 桶画在对应像素列");

  widget.SetWaveformVisible(false);
  Check(widget.height() == 72, "W 关闭后波形区高度归零");
  widget.SetWaveformVisible(true);
  widget.ClearWaveform();
  Check(widget.height() == 72, "清空波形后恢复基础高度");
}

void TestLoopRender() {
  using lapian::core::LoopRange;

  TimelineWidget widget;
  widget.resize(960, 72);
  widget.SetFrameCount(120);
  widget.SetPixelsPerFrame(8.0);
  widget.show();
  QApplication::processEvents();

  auto render = [&widget]() {
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    widget.render(&painter);
    return image;
  };
  auto average_green = [](const QImage& image, int x0, int x1, int y0, int y1) {
    int sum = 0;
    int count = 0;
    for (int y = y0; y < y1; ++y) {
      for (int x = x0; x < x1; ++x) {
        sum += image.pixelColor(x, y).green();
        ++count;
      }
    }
    return count > 0 ? static_cast<double>(sum) / count : 0.0;
  };

  // A=11/B=21：边界 x=88/176 不与主刻度（每 10 帧 x=80/160）重合，
  // 区间内取 x=120、y=30（次刻度从 y=36 起，不干扰）。
  LoopRange range;
  range.SetIn(11);
  range.SetOut(21);
  widget.SetLoopRange(range);
  QImage image = render();

  const QColor inside = image.pixelColor(120, 24);
  const QColor outside = image.pixelColor(40, 24);
  const QColor edge = image.pixelColor(88, 24);
  Check(inside.green() > inside.red() && inside.green() > inside.blue(),
        "未开循环时 A/B 区间内出现淡色条");
  Check(outside.green() <= 30, "区间外保持时间轴底色");
  Check(edge.green() > edge.red() + 20, "入点 A 处画边界线");
  const double tag_green = average_green(image, 91, 102, 42, 53);
  Check(tag_green > 90.0, "入点标签块可见");

  range.Toggle();
  widget.SetLoopRange(range);
  image = render();
  const QColor active_inside = image.pixelColor(120, 24);
  const QColor active_edge = image.pixelColor(88, 24);
  Check(active_inside.green() > active_inside.red() + 8,
        "循环开启后色条高亮");
  Check(active_edge.green() > 180 && active_edge.red() > 120,
        "循环开启后边界线变亮绿");
  const double active_tag_green = average_green(image, 91, 102, 42, 53);
  Check(active_tag_green > 160.0, "循环开启后 A 标签高亮");

  LoopRange a_only;
  a_only.SetIn(11);
  widget.SetLoopRange(a_only);
  image = render();
  Check(image.pixelColor(120, 24).green() <= 30, "只有 A 时不画区间色条");
  Check(image.pixelColor(88, 24).green() > 50, "只有 A 时仍画入点线");

  LoopRange empty;
  widget.SetLoopRange(empty);
  image = render();
  Check(image.pixelColor(88, 24).green() <= 30, "清除 A/B 后色条与边界线消失");
  Check(average_green(image, 91, 102, 42, 53) < 60.0, "清除 A/B 后 A 标签消失");
}

void TestMarkRenderAndHit() {
  TimelineWidget widget;
  widget.resize(960, 72);
  widget.SetFrameCount(120);
  widget.SetPixelsPerFrame(8.0);
  widget.SetMarks({33, 11});
  widget.show();
  QApplication::processEvents();

  Check(widget.marks().size() == 2, "时间轴保存两个标记");
  Check(widget.marks().front() == 11, "标记按帧号排序");

  QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::black);
  {
    QPainter painter(&image);
    widget.render(&painter);
  }
  const QColor marker_line = image.pixelColor(264, 40);
  const QColor marker_flag = image.pixelColor(264, 4);
  const QColor background = image.pixelColor(40, 40);
  Check(marker_line.red() > marker_line.green() + 20,
        "帧 33 标记画粉色竖线");
  Check(marker_flag.red() > 200 && marker_flag.green() < 160,
        "标记顶部小旗可见");
  Check(background.red() < 80, "无标记区域保持底色");

  Check(widget.MarkFrameAtX(264.0) == 33, "标记 x 命中帧 33");
  Check(widget.MarkFrameAtX(268.0) == 33, "4px 内仍吸附到帧 33");
  Check(widget.MarkFrameAtX(272.0) == -1, "超过 5px 不吸附");
  Check(widget.MarkFrameAtX(88.0) == 11, "第二个标记可命中");

  qint64 started = -1;
  qint64 finished = -1;
  QObject::connect(&widget, &TimelineWidget::scrubStarted,
                   [&started](qint64 frame) { started = frame; });
  QObject::connect(&widget, &TimelineWidget::scrubFinished,
                   [&finished](qint64 frame) { finished = frame; });
  QMouseEvent press(QEvent::MouseButtonPress, QPointF(264.5, 30.0),
                    QPointF(264.5, 30.0), Qt::LeftButton, Qt::LeftButton,
                    Qt::NoModifier);
  QApplication::sendEvent(&widget, &press);
  Check(started == 33, "点击标记线吸附到帧 33");
  Check(widget.current_frame() == 33, "点击标记后播放头到帧 33");
  QMouseEvent release(QEvent::MouseButtonRelease, QPointF(264.5, 30.0),
                      QPointF(264.5, 30.0), Qt::LeftButton, Qt::NoButton,
                      Qt::NoModifier);
  QApplication::sendEvent(&widget, &release);
  Check(finished == 33, "松手精确目标为帧 33");

  widget.Clear();
  Check(widget.marks().empty(), "Clear 清空标记");
  Check(widget.MarkFrameAtX(264.0) == -1, "清空后没有标记命中");
}

void TestMarkDensityBenchmark() {
  TimelineWidget widget;
  widget.resize(1920, 72);
  widget.SetFrameCount(432000);
  std::vector<std::int64_t> frames;
  frames.reserve(100);
  for (int i = 0; i < 100; ++i) {
    frames.push_back(i * 1000);
  }
  widget.SetMarks(frames);
  widget.show();
  QApplication::processEvents();

  QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
  auto render_once = [&image, &widget]() {
    image.fill(Qt::black);
    QPainter painter(&image);
    widget.render(&painter);
  };
  for (int i = 0; i < 5; ++i) {
    render_once();
  }

  auto measure = [&](const char* name) {
    std::vector<double> samples;
    samples.reserve(200);
    for (int i = 0; i < 200; ++i) {
      QElapsedTimer timer;
      timer.start();
      render_once();
      samples.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
    }
    std::sort(samples.begin(), samples.end());
    const double p95 =
        samples[static_cast<std::size_t>(samples.size() * 0.95)];
    std::printf("[perf] %s p50=%.3fms p95=%.3fms\n", name,
                samples[samples.size() / 2], p95);
    Check(p95 < 16.0, std::string(name) + " 标记重绘 p95 < 16ms");
  };

  widget.FitToWindow();
  measure("fit 432k + 100 marks");

  widget.SetPixelsPerFrame(8.0);
  measure("8px/frame + 100 marks");
}

void TestPaintBenchmark() {
  TimelineWidget widget;
  widget.resize(1920, 72);
  widget.SetFrameCount(432000);
  widget.SetCurrentFrame(216000);
  widget.show();
  QApplication::processEvents();

  QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
  auto render_once = [&image, &widget]() {
    image.fill(Qt::black);
    QPainter painter(&image);
    widget.render(&painter);
  };

  for (int i = 0; i < 10; ++i) {
    render_once();
  }

  auto measure = [&](const char* name, bool move_playhead) {
    std::vector<double> samples;
    samples.reserve(200);
    for (int i = 0; i < 200; ++i) {
      if (move_playhead) {
        widget.SetCurrentFrame((static_cast<std::int64_t>(i) * 2161) % 432000);
      }
      QElapsedTimer timer;
      timer.start();
      render_once();
      samples.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
    }
    std::sort(samples.begin(), samples.end());
    const double p50 = samples[samples.size() / 2];
    const double p95 = samples[static_cast<std::size_t>(samples.size() * 0.95)];
    std::printf("[perf] %s p50=%.3fms p95=%.3fms\n", name, p50, p95);
    Check(p95 < 16.0, std::string(name) + " 重绘 p95 < 16ms");
  };

  widget.FitToWindow();
  measure("fit 432k", false);

  widget.SetPixelsPerFrame(8.0);
  measure("8px/frame + scroll", true);

  widget.SetWaveformSource(
      []() { return static_cast<std::size_t>(1440000); },
      [](std::size_t begin, std::size_t end) {
        const std::int16_t value =
            (begin + end) % 2 == 0 ? static_cast<std::int16_t>(12000)
                                   : static_cast<std::int16_t>(-12000);
        return TimelineWidget::WaveformBucketView{value, value};
      },
      0.0, 1.0 / 60.0,
      [](std::int64_t frame) { return frame * 16667; });
  widget.FitToWindow();
  measure("fit 432k + 2h waveform", false);
}

}  // namespace

int main(int argc, char** argv) {
  // 测试不弹窗；在 QApplication 构造前固定离屏平台。
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  QApplication app(argc, argv);

  std::printf("T2.7 时间轴测试\n");
  TestModel();
  TestWidgetViewState();
  TestPointerInteraction();
  TestAutoPanRightEdge();
  TestAutoPanLeftEdge();
  TestAutoPanNoBackJump();
  TestAutoPanDisabledAtFit();
  TestAutoPanStopsAtContentEnd();
  TestAutoPanStopsOnViewChanges();
  TestWaveformRender();
  TestLoopRender();
  TestMarkRenderAndHit();
  TestMarkDensityBenchmark();
  TestPaintBenchmark();

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("全部通过\n");
  return 0;
}
