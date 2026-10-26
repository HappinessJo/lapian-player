#pragma once

#include <QWidget>

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "core/LoopRange.h"
#include "core/TimelineModel.h"

class QMouseEvent;
class QPaintEvent;
class QPainter;
class QPointF;
class QResizeEvent;
class QWheelEvent;

// T2.7/T2.8 时间轴：帧刻度、缩放、横向滚动、播放头与指针命中。
// 只负责视图与目标帧语义；暂停/seek 由 MainWindow 处理，T2.9 再接预览解码。
class TimelineWidget : public QWidget {
  Q_OBJECT

 public:
  explicit TimelineWidget(QWidget* parent = nullptr);

  // 高度策略（规划附录 A.6）：
  //  kFixed = 现状（72 + 波形 56，setFixedHeight）——tests/timeline_test.cpp 的
  //           128/72 断言依赖它，**默认不变**。
  //  kFill  = v2 壳层用：高度跟随外层面板（96–340），波形按高度算并封顶 96。
  enum class HeightPolicy {
    kFixed = 0,
    kFill = 1,
  };
  void SetHeightPolicy(HeightPolicy policy);
  HeightPolicy height_policy() const { return height_policy_; }

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
  // 滚轮/两条滑块共用：以画布 x 为锚点缩放（与 ZoomIn/Out 同一条夹取路径）。
  void ZoomAtFactor(double factor, double anchor_x);
  // 缩放窗滑块：给"视图起点帧 + 跨度帧"，换算成 pixels_per_frame/scroll 后
  // 仍走 TimelineModel 的夹取（1 帧 ≥8px 上限、fit 下限、滚动上下限）。
  void SetViewWindow(double start_frame, double span_frames);
  // 平移到绝对 scroll 像素（负/越界由 TimelineModel 夹取）。
  void SetScrollPixels(double scroll_px);
  double pixels_per_frame() const { return model_.pixels_per_frame(); }
  // UI v2（3A）：时间轴面板头要显示"视图 0–4572 · 适应窗口"。只读投影，
  // 不新增视图状态（规划 Q3：唯一 source of truth 仍是 TimelineModel）。
  std::int64_t frame_count() const { return model_.frame_count(); }
  double scroll_px() const { return model_.scroll(); }
  bool fit_mode() const { return fit_mode_; }
  // 视图唯一真源（3B：两条滑块直接读它，不存第二份视图状态）。
  const lapian::core::TimelineModel& model() const { return model_; }
  // 视图窗 = 画布可见范围（first, last）；面板头/滑块/断言共用同一份算法。
  std::pair<std::int64_t, std::int64_t> ViewFrames() const;
  // 播放头是否落在可见范围内；越界时画布贴边画方向箭头（M7：播放头常驻）。
  bool PlayheadVisible() const;

  // 指针命中：第 N 帧占据 [N, N+1)，按物理像素对齐后 floor。
  // T2.8 的鼠标事件与 T2.9/T2.10 的拖动路径共用这一入口。
  std::int64_t FrameAtPosition(const QPointF& position) const;

  // T3.6 波形：bucket 时间轴是音频时间，显示时减 offset 再映射到帧格。
  // data 可能仍在增长（后台生成），count/bucket 每次 paint 重新取。
  struct WaveformBucketView {
    std::int16_t min = 0;
    std::int16_t max = 0;
  };
  using WaveformCountFn = std::function<std::size_t()>;
  using WaveformRangeFn =
      std::function<WaveformBucketView(std::size_t, std::size_t)>;
  using FramePtsFn = std::function<std::int64_t(std::int64_t)>;

  void SetWaveformSource(WaveformCountFn count, WaveformRangeFn range,
                         double offset_seconds, double fallback_frame_seconds,
                         FramePtsFn frame_pts);
  void SetWaveformMessage(const QString& message);
  void ClearWaveform();
  void SetWaveformVisible(bool visible);
  bool waveform_visible() const { return waveform_visible_; }
  bool waveform_active() const { return waveform_active_; }

  // T4.1 A/B 色条与标签；循环开启时高亮。数据校验由 core::LoopRange 负责。
  void SetLoopRange(const lapian::core::LoopRange& range);
  const lapian::core::LoopRange& loop_range() const { return loop_range_; }

  // T5.2 标记：只保存帧号（同一帧多个标记只画一个），排序去重。
  // 绘制只遍历标记并跳过不可见项；命中测试按像素距离取最近标记。
  void SetMarks(std::vector<std::int64_t> frames);
  void ClearMarks();
  const std::vector<std::int64_t>& marks() const { return marks_; }
  // 返回距离 x（逻辑像素）不超过 tolerance_px 的最近标记帧；没有返回 -1。
  std::int64_t MarkFrameAtX(double x, double tolerance_px = 5.0) const;

 signals:
  // 按下时发一次；拖动中目标帧变化时发；松开时发最终目标帧。
  void scrubStarted(qint64 frame);
  void scrubTargetChanged(qint64 frame);
  void scrubFinished(qint64 frame);

  // 3B：状态同步去轮询（不再用 250ms 定时器刷面板头/循环按钮）。
  // viewChanged = 缩放/滚动/视口宽度变化（面板两条滑块 + 面板头订阅）；
  // playheadChanged = 播放头帧变化（滑块上的播放头标记订阅）；
  // loopRangeChanged = A/B 或循环开关变化（面板头 + Transport 循环按钮订阅）。
  void viewChanged();
  void playheadChanged(qint64 frame);
  void loopRangeChanged();

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  // kFixed 的底部留白：旧实现里这里是 14px 横向滚动条，3A/3B 删掉滚动条
  // （视图窗滑块取代）后**几何必须保持**——tests/timeline_test.cpp 的
  // 128/72 高度、A/B 标签 y=42..53、次刻度 y=36.. 等像素断言都按它算。
  // kFill（v2 面板）没有这段留白：高度由面板画布区决定。
  static constexpr int kFixedBottomReserve = 14;

  int ContentBottom() const;
  int RulerHeight() const;
  int WaveformHeight() const;
  void UpdateHeight();
  void NotifyViewChanged();
  void DrawWaveform(QPainter& painter, const QRectF& rect);
  void DrawLoopRange(QPainter& painter, const QRectF& rect);
  void DrawMarks(QPainter& painter, const QRectF& rect);
  void DrawPlayhead(QPainter& painter);
  double FramePtsSeconds(std::int64_t frame) const;
  void ApplyZoom(double factor, double anchor_x);

  lapian::core::TimelineModel model_;
  HeightPolicy height_policy_ = HeightPolicy::kFixed;
  std::int64_t current_frame_ = 0;
  std::int64_t drag_target_frame_ = 0;
  bool fit_mode_ = true;
  bool follow_playhead_ = true;
  bool follow_before_scrub_ = true;
  bool scrubbing_ = false;
  WaveformCountFn waveform_count_;
  WaveformRangeFn waveform_range_;
  FramePtsFn frame_pts_;
  QString waveform_message_;
  double waveform_offset_seconds_ = 0.0;
  double fallback_frame_seconds_ = 1.0 / 30.0;
  bool waveform_active_ = false;
  bool waveform_visible_ = true;
  lapian::core::LoopRange loop_range_;
  std::vector<std::int64_t> marks_;
};
