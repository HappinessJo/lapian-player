// T2.7 时间轴几何单测 + 43.2 万帧（2h@60fps）离屏绘制基准。

#include <QApplication>
#include <QByteArray>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>

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
}

}  // namespace

int main(int argc, char** argv) {
  // 测试不弹窗；在 QApplication 构造前固定离屏平台。
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  QApplication app(argc, argv);

  std::printf("T2.7 时间轴测试\n");
  TestModel();
  TestWidgetViewState();
  TestPaintBenchmark();

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("全部通过\n");
  return 0;
}
