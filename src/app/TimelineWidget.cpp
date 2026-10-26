#include "app/TimelineWidget.h"

#include "app/ui/Tokens.h"

#include <QColor>
#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

constexpr int kTimelineBaseHeight = 72;
constexpr int kWaveformHeight = 56;
constexpr double kPlayheadMarginPx = 8.0;
// 越界播放头贴边箭头（Fill/Fixed 共用）。
constexpr double kOffViewArrowWidth = 6.0;

// C5：放大后拖动播放头到左右边缘的自动平移。
// 边缘带宽度按逻辑像素（DPI 无关）；步长随鼠标超出边缘触发线的距离增大。
constexpr int kAutoPanEdgePx = 10;
constexpr int kAutoPanIntervalMs = 16;
constexpr double kAutoPanBaseStepPx = 6.0;
constexpr double kAutoPanOvershootFactor = 0.35;
constexpr double kAutoPanMaxStepPx = 28.0;

}  // namespace

TimelineWidget::TimelineWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("timeline"));
  setFixedHeight(kTimelineBaseHeight);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  // 不抢走视频区的键盘焦点；滚轮缩放不需要焦点。
  setFocusPolicy(Qt::NoFocus);
  setToolTip(QStringLiteral("滚轮缩放 · Shift+滚轮横向滚动 · +/- 缩放"));
  // 3B：内部横向滚动条已删除（视图窗滑块取代）；横向平移由滚轮/滑块负责。
  // C5：边缘自动平移定时器。只在放大后拖动播放头到边缘时运行，parent=this
  // 保证控件销毁/隐藏后不会再有 tick。
  auto_pan_timer_ = new QTimer(this);
  auto_pan_timer_->setTimerType(Qt::PreciseTimer);
  auto_pan_timer_->setInterval(kAutoPanIntervalMs);
  connect(auto_pan_timer_, &QTimer::timeout, this, &TimelineWidget::AutoPanTick);
}

void TimelineWidget::Clear() {
  ClearWaveform();
  loop_range_.Clear();
  marks_.clear();
  SetFrameCount(0);
}

void TimelineWidget::SetFrameCount(std::int64_t count) {
  StopAutoPan();
  if (scrubbing_) {
    scrubbing_ = false;
    follow_playhead_ = follow_before_scrub_;
  }
  if (count <= 0) {
    loop_range_.Clear();
  }
  current_frame_ = 0;
  fit_mode_ = true;
  model_.SetFrameCount(count);
  model_.SetViewportWidth(width());
  model_.Fit();
  emit playheadChanged(0);
  NotifyViewChanged();
}

void TimelineWidget::SetCurrentFrame(std::int64_t frame) {
  if (model_.frame_count() <= 0) {
    current_frame_ = 0;
    emit playheadChanged(0);
    update();
    return;
  }
  const double scroll_before = model_.scroll();
  current_frame_ =
      std::clamp<std::int64_t>(frame, 0, model_.frame_count() - 1);
  if (follow_playhead_) {
    model_.EnsureFrameVisible(current_frame_, kPlayheadMarginPx);
  }
  if (std::abs(model_.scroll() - scroll_before) > 1e-9) {
    NotifyViewChanged();
  }
  emit playheadChanged(static_cast<qint64>(current_frame_));
  update();
}

void TimelineWidget::NotifyViewChanged() {
  fit_mode_ = model_.IsAtFit();
  emit viewChanged();
}

void TimelineWidget::FitToWindow() {
  StopAutoPan();
  fit_mode_ = true;
  model_.Fit();
  NotifyViewChanged();
  update();
}

void TimelineWidget::ZoomIn() {
  if (model_.frame_count() <= 0) {
    return;
  }
  double anchor = model_.FrameToX(current_frame_);
  if (anchor < 0.0 || anchor > static_cast<double>(width())) {
    anchor = static_cast<double>(width()) / 2.0;
  }
  ApplyZoom(1.25, anchor);
}

void TimelineWidget::ZoomOut() {
  if (model_.frame_count() <= 0) {
    return;
  }
  double anchor = model_.FrameToX(current_frame_);
  if (anchor < 0.0 || anchor > static_cast<double>(width())) {
    anchor = static_cast<double>(width()) / 2.0;
  }
  ApplyZoom(0.8, anchor);
}

void TimelineWidget::SetPixelsPerFrame(double pixels_per_frame) {
  StopAutoPan();
  model_.SetPixelsPerFrame(pixels_per_frame);
  NotifyViewChanged();
  update();
}

void TimelineWidget::ZoomAtFactor(double factor, double anchor_x) {
  ApplyZoom(factor, anchor_x);
}

void TimelineWidget::SetViewWindow(double start_frame, double span_frames) {
  StopAutoPan();
  if (model_.frame_count() <= 0 || width() <= 0) {
    return;
  }
  const double viewport = std::max(1.0, static_cast<double>(width()));
  const double count = static_cast<double>(model_.frame_count());
  const double span = std::clamp(span_frames, 1.0, std::max(1.0, count));
  const double before_ppf = model_.pixels_per_frame();
  const double before_scroll = model_.scroll();
  // 先落比例（会被 fit/8px 夹取），再用**实际**比例反推滚动，保证起点贴合。
  model_.SetPixelsPerFrame(viewport / span);
  model_.SetScroll(start_frame * model_.pixels_per_frame());
  NotifyViewChanged();
  if (std::abs(before_ppf - model_.pixels_per_frame()) > 1e-9 ||
      std::abs(before_scroll - model_.scroll()) > 1e-9) {
    update();
  }
}

void TimelineWidget::SetScrollPixels(double scroll_px) {
  // 外部滚动（滚轮/滑块/面板）会打断自动平移；auto-pan 自己的 tick 例外，
  // 否则每一步都会把自己停掉（见 AutoPanTick）。
  if (!auto_pan_scrolling_) {
    StopAutoPan();
  }
  model_.SetScroll(scroll_px);
  NotifyViewChanged();
  update();
}

std::pair<std::int64_t, std::int64_t> TimelineWidget::ViewFrames() const {
  const std::int64_t count = model_.frame_count();
  if (count <= 0 || model_.pixels_per_frame() <= 0.0 || width() <= 0) {
    return {0, 0};
  }
  const double ppf = model_.pixels_per_frame();
  const std::int64_t first = std::clamp<std::int64_t>(
      static_cast<std::int64_t>(std::floor(model_.scroll() / ppf)), 0,
      count - 1);
  const std::int64_t last = std::clamp<std::int64_t>(
      static_cast<std::int64_t>(
          std::floor((model_.scroll() + static_cast<double>(width())) / ppf)),
      first, count - 1);
  return {first, last};
}

bool TimelineWidget::PlayheadVisible() const {
  if (model_.frame_count() <= 0) {
    return false;
  }
  const double x = model_.FrameToX(current_frame_);
  return x >= -0.5 && x <= static_cast<double>(width()) + 0.5;
}

std::int64_t TimelineWidget::FrameAtPosition(const QPointF& position) const {
  if (model_.frame_count() <= 0) {
    return 0;
  }
  const double ratio = devicePixelRatioF();
  double x = position.x();
  if (ratio > 0.0) {
    // 高 DPI 下先对齐到物理像素，再做帧格 floor。
    x = std::floor(x * ratio) / ratio;
  }
  return model_.FrameAtX(x);
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || model_.frame_count() <= 0) {
    QWidget::mousePressEvent(event);
    return;
  }
  // C5：按在边缘不启动自动平移——必须真的拖动进入/越过边缘。
  StopAutoPan();
  if (!scrubbing_) {
    follow_before_scrub_ = follow_playhead_;
    follow_playhead_ = false;
  }
  scrubbing_ = true;
  // T5.2：点在标记线附近时吸附到标记帧，和普通拖动/播放头点击共用同一条
  // scrub 路径；A/B 标签不参与命中，避免与循环边界抢点击。
  const std::int64_t mark_frame = MarkFrameAtX(event->position().x());
  drag_target_frame_ =
      mark_frame >= 0 ? mark_frame : FrameAtPosition(event->position());
  SetCurrentFrame(drag_target_frame_);
  emit scrubStarted(static_cast<qint64>(drag_target_frame_));
  event->accept();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!scrubbing_ || !(event->buttons() & Qt::LeftButton)) {
    StopAutoPan();
    QWidget::mouseMoveEvent(event);
    return;
  }
  const double raw_x = event->position().x();
  // C5：放大后拖到左/右边缘时，目标帧先落在**边缘帧**，而不是画布外的原始
  // x。否则一次远拖（如 x=width+100）会先跳到画布外的帧，再被 auto-pan
  // 第一次 tick 用 clamp 后的 x 拉回来，造成播放头/目标帧"先右跳再左退"。
  // 方向/overshoot 仍用原始 x（见 UpdateAutoPan）。
  double target_x = raw_x;
  if (AutoPanAllowed()) {
    const double right_trigger = static_cast<double>(width()) - kAutoPanEdgePx;
    if (raw_x < kAutoPanEdgePx || raw_x > right_trigger) {
      target_x = std::clamp(
          raw_x, 0.0, std::max(0.0, static_cast<double>(width()) - 1.0));
    }
  }
  const std::int64_t target =
      FrameAtPosition(QPointF(target_x, event->position().y()));
  if (target != drag_target_frame_) {
    drag_target_frame_ = target;
    SetCurrentFrame(drag_target_frame_);
    emit scrubTargetChanged(static_cast<qint64>(drag_target_frame_));
  }
  // C5：先更新目标帧，再按当前鼠标 x 判定边缘自动平移（进入边缘立刻先走一步）。
  // 每次都用控件当前宽度，不缓存旧几何（窄窗/全屏/高 DPI 都成立）。
  UpdateAutoPan(raw_x);
  event->accept();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !scrubbing_) {
    QWidget::mouseReleaseEvent(event);
    return;
  }
  // C5：松手时若刚才在自动平移，保留最后一次 tick 算出的目标帧；不能再按
  // 鼠标 x 重算——拖动到画布外时那样会跳帧（x 已超出可见范围）。
  auto_pan_was_active_on_release_ = auto_pan_direction_ != 0;
  StopAutoPan();
  if (!auto_pan_was_active_on_release_) {
    const std::int64_t target = FrameAtPosition(event->position());
    if (target != drag_target_frame_) {
      drag_target_frame_ = target;
      SetCurrentFrame(drag_target_frame_);
      emit scrubTargetChanged(static_cast<qint64>(drag_target_frame_));
    }
  }
  scrubbing_ = false;
  follow_playhead_ = follow_before_scrub_;
  emit scrubFinished(static_cast<qint64>(drag_target_frame_));
  event->accept();
}

bool TimelineWidget::AutoPanAllowed() const {
  // 四条同时满足才允许边缘自动平移；fit/缩到最小（MaxScroll()==0 或
  // IsAtFit()）时绝不启动，拖出边界只把目标 clamp 到首/末帧。
  return scrubbing_ && width() > 0 && model_.frame_count() > 0 &&
         model_.MaxScroll() > 0.0 && !model_.IsAtFit();
}

void TimelineWidget::UpdateAutoPan(double x) {
  auto_pan_x_ = x;
  if (!AutoPanAllowed()) {
    StopAutoPan();
    return;
  }
  const double edge = static_cast<double>(kAutoPanEdgePx);
  const double right_trigger = static_cast<double>(width()) - edge;
  int direction = 0;
  double overshoot = 0.0;
  if (x > right_trigger) {
    direction = 1;
    overshoot = x - right_trigger;
  } else if (x < edge) {
    direction = -1;
    overshoot = edge - x;
  }
  if (direction == 0) {
    // 回到画布中间：停 timer，保持现有目标帧不动。
    StopAutoPan();
    return;
  }
  const bool entering = auto_pan_direction_ != direction;
  auto_pan_direction_ = direction;
  auto_pan_overshoot_ = std::max(0.0, overshoot);
  if (entering) {
    // 进入边缘立刻先走一步（不等 16ms），然后由定时器继续。
    AutoPanTick();
    if (auto_pan_direction_ == 0) {
      return;  // 已经在内容首/尾，无可滚动
    }
  }
  if (auto_pan_timer_ != nullptr && !auto_pan_timer_->isActive()) {
    auto_pan_timer_->start();
  }
}

void TimelineWidget::AutoPanTick() {
  if (auto_pan_direction_ == 0) {
    return;
  }
  if (!AutoPanAllowed()) {
    StopAutoPan();
    return;
  }
  const double before = model_.scroll();
  const double overshoot = std::max(0.0, auto_pan_overshoot_);
  const double step =
      std::min(kAutoPanMaxStepPx,
               kAutoPanBaseStepPx + kAutoPanOvershootFactor * overshoot);
  auto_pan_scrolling_ = true;
  SetScrollPixels(before + static_cast<double>(auto_pan_direction_) * step);
  auto_pan_scrolling_ = false;

  // 滚动后用 clamp 到 [0, width-1] 的鼠标 x 重算目标帧：播放头保持在视口
  // 边缘，视图继续按鼠标位置方向推进（到内容首/尾由 scroll 夹取自然停住）。
  const double x = std::clamp(
      auto_pan_x_, 0.0, std::max(0.0, static_cast<double>(width()) - 1.0));
  const std::int64_t target = model_.FrameAtX(x);
  if (target != drag_target_frame_) {
    drag_target_frame_ = target;
    SetCurrentFrame(target);
    emit scrubTargetChanged(static_cast<qint64>(target));
  }

  const bool scroll_moved = std::abs(model_.scroll() - before) > 1e-9;
  if (!scroll_moved || !AutoPanAllowed()) {
    StopAutoPan();
  }
}

void TimelineWidget::StopAutoPan() {
  if (auto_pan_timer_ != nullptr && auto_pan_timer_->isActive()) {
    auto_pan_timer_->stop();
  }
  auto_pan_direction_ = 0;
  auto_pan_overshoot_ = 0.0;
}

int TimelineWidget::ContentBottom() const {
  // kFixed：底部保留 14px（旧滚动条占位，见头文件的 kFixedBottomReserve）。
  // kFill ：画布区吃满面板给的高度。
  if (height_policy_ == HeightPolicy::kFixed) {
    return std::max(0, height() - kFixedBottomReserve);
  }
  return std::max(0, height());
}

int TimelineWidget::RulerHeight() const {
  // 两种策略都按"实际高度"派生：标尺 = 内容高 − 波形高。
  return std::max(0, ContentBottom() - WaveformHeight());
}

int TimelineWidget::WaveformHeight() const {
  if (!(waveform_active_ && waveform_visible_)) {
    return 0;
  }
  if (height_policy_ == HeightPolicy::kFill) {
    // Fill（v2 壳层）：波形跟着面板高度走，但**封顶 96**、下限 8
    // （规划 §4.5：拉到 340 时波形不跟着变成巨大色块，多出的高度留白）。
    const int available = ContentBottom();
    const int target = static_cast<int>(available * 0.62);
    return std::clamp(target, 8, lapian::ui::TokensFor(
                                     lapian::ui::Density::kRegular)
                                     .waveform_cap);
  }
  return kWaveformHeight;
}

void TimelineWidget::UpdateHeight() {
  if (height_policy_ == HeightPolicy::kFill) {
    // Fill：高度由外层面板决定（v2 的 TimelinePanel），这里不再 setFixedHeight。
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    updateGeometry();
    update();
    return;
  }
  setFixedHeight(kTimelineBaseHeight + WaveformHeight());
  updateGeometry();
  update();
}

void TimelineWidget::SetHeightPolicy(HeightPolicy policy) {
  if (height_policy_ == policy) {
    return;
  }
  height_policy_ = policy;
  if (height_policy_ == HeightPolicy::kFill) {
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  } else {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    UpdateHeight();
  }
  UpdateHeight();
}

void TimelineWidget::SetWaveformSource(WaveformCountFn count,
                                       WaveformRangeFn range,
                                       double offset_seconds,
                                       double fallback_frame_seconds,
                                       FramePtsFn frame_pts) {
  waveform_count_ = std::move(count);
  waveform_range_ = std::move(range);
  frame_pts_ = std::move(frame_pts);
  waveform_offset_seconds_ = offset_seconds;
  fallback_frame_seconds_ =
      fallback_frame_seconds > 1e-6 ? fallback_frame_seconds : 1.0 / 30.0;
  waveform_message_.clear();
  waveform_active_ = true;
  UpdateHeight();
}

void TimelineWidget::SetWaveformMessage(const QString& message) {
  waveform_count_ = nullptr;
  waveform_range_ = nullptr;
  frame_pts_ = nullptr;
  waveform_message_ = message;
  waveform_active_ = true;
  UpdateHeight();
}

void TimelineWidget::ClearWaveform() {
  waveform_count_ = nullptr;
  waveform_range_ = nullptr;
  frame_pts_ = nullptr;
  waveform_message_.clear();
  waveform_active_ = false;
  UpdateHeight();
}

void TimelineWidget::SetWaveformVisible(bool visible) {
  if (waveform_visible_ == visible) {
    return;
  }
  waveform_visible_ = visible;
  UpdateHeight();
}

void TimelineWidget::SetLoopRange(const lapian::core::LoopRange& range) {
  const bool changed = loop_range_.has_in() != range.has_in() ||
                       loop_range_.has_out() != range.has_out() ||
                       loop_range_.enabled() != range.enabled() ||
                       loop_range_.in() != range.in() ||
                       loop_range_.out() != range.out();
  loop_range_ = range;
  update();
  if (changed) {
    emit loopRangeChanged();
  }
}

void TimelineWidget::SetMarks(std::vector<std::int64_t> frames) {
  std::sort(frames.begin(), frames.end());
  frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
  marks_ = std::move(frames);
  update();
}

void TimelineWidget::ClearMarks() {
  if (marks_.empty()) {
    return;
  }
  marks_.clear();
  update();
}

std::int64_t TimelineWidget::MarkFrameAtX(double x, double tolerance_px) const {
  if (marks_.empty() || model_.frame_count() <= 0) {
    return -1;
  }
  const double tolerance = std::max(1.0, tolerance_px);
  std::int64_t best_frame = -1;
  double best_distance = tolerance + 1.0;
  for (const std::int64_t frame : marks_) {
    if (frame < 0 || frame >= model_.frame_count()) {
      continue;
    }
    const double distance = std::abs(model_.FrameToX(frame) - x);
    if (distance <= tolerance && distance < best_distance) {
      best_distance = distance;
      best_frame = frame;
    }
  }
  return best_frame;
}

double TimelineWidget::FramePtsSeconds(std::int64_t frame) const {
  if (frame_pts_) {
    const std::int64_t pts_us = frame_pts_(frame);
    if (pts_us > 0) {
      return static_cast<double>(pts_us) / 1.0e6;
    }
  }
  return static_cast<double>(frame) * fallback_frame_seconds_;
}

void TimelineWidget::DrawWaveform(QPainter& painter, const QRectF& rect) {
  painter.fillRect(rect, QColor(QStringLiteral("#141417")));
  painter.setPen(QColor(QStringLiteral("#2b2b31")));
  painter.drawLine(QPointF(rect.left(), rect.top()),
                   QPointF(rect.right(), rect.top()));

  QFont hint_font = painter.font();
  hint_font.setPointSizeF(8.5);
  painter.setFont(hint_font);

  if (!waveform_message_.isEmpty()) {
    painter.setPen(QColor(QStringLiteral("#777781")));
    painter.drawText(rect, Qt::AlignCenter, waveform_message_);
    return;
  }
  if (!waveform_count_ || !waveform_range_ || model_.frame_count() <= 0) {
    return;
  }

  const std::size_t bucket_count = waveform_count_();
  if (bucket_count == 0) {
    painter.setPen(QColor(QStringLiteral("#777781")));
    painter.drawText(rect, Qt::AlignCenter, QStringLiteral("波形生成中…"));
    return;
  }

  const std::int64_t frame_count = model_.frame_count();
  const double center = rect.center().y();
  const double amplitude = std::max(1.0, rect.height() / 2.0 - 4.0);
  const int first_x = std::max(0, static_cast<int>(std::floor(rect.left())));
  const int last_x =
      std::min(width(), static_cast<int>(std::ceil(rect.right())));

  painter.setPen(QColor(QStringLiteral("#7aa2f7")));
  for (int x = first_x; x < last_x; ++x) {
    const std::int64_t frame0 = model_.FrameAtX(static_cast<double>(x) + 0.5);
    const double start_seconds = FramePtsSeconds(frame0);
    std::int64_t frame1 = model_.FrameAtX(static_cast<double>(x + 1) + 0.5);
    double end_seconds =
        frame1 >= frame_count ? start_seconds + fallback_frame_seconds_
                              : FramePtsSeconds(frame1);
    if (frame1 <= frame0) {
      frame1 = frame0 + 1;
      end_seconds = frame1 >= frame_count
                        ? start_seconds + fallback_frame_seconds_
                        : FramePtsSeconds(frame1);
    }
    if (end_seconds <= start_seconds) {
      end_seconds = start_seconds + fallback_frame_seconds_;
    }

    const double audio_start = start_seconds + waveform_offset_seconds_;
    const double audio_end = end_seconds + waveform_offset_seconds_;
    const std::int64_t raw_begin =
        static_cast<std::int64_t>(std::floor(audio_start * 200.0));
    const std::int64_t raw_end =
        static_cast<std::int64_t>(std::ceil(audio_end * 200.0));
    const std::size_t begin = static_cast<std::size_t>(
        std::clamp<std::int64_t>(raw_begin, 0,
                                 static_cast<std::int64_t>(bucket_count)));
    const std::size_t end = static_cast<std::size_t>(
        std::clamp<std::int64_t>(raw_end, 0,
                                 static_cast<std::int64_t>(bucket_count)));
    if (end <= begin) {
      continue;
    }

    const WaveformBucketView aggregate = waveform_range_(begin, end);
    const double high = static_cast<double>(aggregate.max) / 32768.0;
    const double low = static_cast<double>(aggregate.min) / 32768.0;
    double y_top = center - high * amplitude;
    double y_bottom = center - low * amplitude;
    if (y_top > y_bottom) {
      std::swap(y_top, y_bottom);
    }
    if (y_bottom - y_top < 1.0) {
      y_bottom = y_top + 1.0;
    }
    painter.drawLine(QPointF(static_cast<double>(x) + 0.5, y_top),
                     QPointF(static_cast<double>(x) + 0.5, y_bottom));
  }
}

void TimelineWidget::DrawLoopRange(QPainter& painter, const QRectF& rect) {
  if (model_.frame_count() <= 0 || !loop_range_.has_in()) {
    return;
  }

  const std::int64_t last_frame = model_.frame_count() - 1;
  const std::int64_t frame_in =
      std::clamp<std::int64_t>(loop_range_.in(), 0, last_frame);
  const double frame_px = std::max(1.0, model_.pixels_per_frame());
  const double left = model_.FrameToX(frame_in);
  double right = left + frame_px;
  if (loop_range_.has_out()) {
    const std::int64_t frame_out =
        std::clamp<std::int64_t>(loop_range_.out(), frame_in, last_frame);
    right = model_.FrameToX(frame_out) + frame_px;
  }
  if (right < left) {
    right = left + frame_px;
  }

  const bool active = loop_range_.enabled();
  const QColor edge = active ? QColor(158, 206, 106) : QColor(95, 127, 69);
  if (loop_range_.has_out()) {
    // 色条覆盖入点帧格左缘到出点帧格右缘；循环开启时提高不透明度。
    painter.fillRect(
        QRectF(left, rect.top(), right - left, rect.height()),
        active ? QColor(158, 206, 106, 64) : QColor(158, 206, 106, 26));
  }

  QPen edge_pen(edge);
  edge_pen.setWidthF(active ? 2.0 : 1.0);
  painter.setPen(edge_pen);
  if (left >= -1.0 && left <= static_cast<double>(width()) + 1.0) {
    painter.drawLine(QPointF(left + 0.5, rect.top()),
                     QPointF(left + 0.5, rect.bottom()));
  }
  if (loop_range_.has_out() && right >= -1.0 &&
      right <= static_cast<double>(width()) + 1.0) {
    painter.drawLine(QPointF(right - 0.5, rect.top()),
                     QPointF(right - 0.5, rect.bottom()));
  }

  // A/B 标签：kFixed 仍画在标尺下缘（tests/timeline_test.cpp 按 y=42..53 采样）；
  // kFill 跟着原型画在标尺下方的标记带（`.ztrack` 之下），不再压住帧号标签。
  constexpr double kTagWidth = 15.0;
  constexpr double kTagHeight = 15.0;
  const double tag_y =
      height_policy_ == HeightPolicy::kFill
          ? static_cast<double>(RulerHeight()) + 1.0
          : std::max(1.0, static_cast<double>(RulerHeight()) - kTagHeight - 3.0);
  QFont tag_font = painter.font();
  tag_font.setPointSizeF(8.0);
  tag_font.setBold(true);
  painter.setFont(tag_font);
  const bool active_tag = active;
  auto draw_tag = [&](const QString& text, double boundary_x, bool place_right) {
    double tag_x = place_right ? boundary_x + 1.0 : boundary_x - kTagWidth - 1.0;
    tag_x = std::clamp(tag_x, 0.0,
                       std::max(0.0, static_cast<double>(width()) - kTagWidth));
    painter.setPen(Qt::NoPen);
    painter.setBrush(edge);
    painter.drawRect(QRectF(tag_x, tag_y, kTagWidth, kTagHeight));
    painter.setPen(active_tag ? QColor(QStringLiteral("#101a0c"))
                              : QColor(QStringLiteral("#d8e8cf")));
    painter.drawText(QRectF(tag_x, tag_y, kTagWidth, kTagHeight),
                     Qt::AlignCenter, text);
  };
  if (left >= 0.0 && left <= static_cast<double>(width())) {
    draw_tag(QStringLiteral("A"), left, true);
  }
  if (loop_range_.has_out() && right >= 0.0 &&
      right <= static_cast<double>(width())) {
    // A/B 很近时 B 标签放到右边界外侧，避免压住 A。
    const bool place_right = (right - left) < 2.0 * kTagWidth + 6.0;
    draw_tag(QStringLiteral("B"), right, place_right);
  }
}

void TimelineWidget::DrawMarks(QPainter& painter, const QRectF& rect) {
  if (marks_.empty() || model_.frame_count() <= 0) {
    return;
  }
  const double bottom = rect.bottom();
  const QColor line_color(247, 118, 142, 70);
  const QColor flag_color(QStringLiteral("#f7768e"));
  // kFill：旗标落在标尺下方的标记带里（原型把标记画在 ruler 之下），
  // 否则会盖住帧号标签；kFixed 保持 y=0..8（单测按 y=4 采样小旗）。
  const double flag_top =
      height_policy_ == HeightPolicy::kFill
          ? static_cast<double>(RulerHeight()) + 1.0
          : 0.0;
  painter.save();
  int last_x = std::numeric_limits<int>::min();
  for (const std::int64_t frame : marks_) {
    if (frame < 0 || frame >= model_.frame_count()) {
      continue;
    }
    const double x = model_.FrameToX(frame);
    if (x < -1.0 || x > static_cast<double>(width()) + 1.0) {
      continue;
    }
    const int rounded_x = static_cast<int>(std::lround(x));
    // 全片适配时 100 个标记可能落在同一像素列；只画一次，避免无意义重绘。
    if (rounded_x == last_x) {
      continue;
    }
    last_x = rounded_x;
    const double line_x = std::floor(x) + 0.5;
    painter.setPen(QPen(line_color, 1.0));
    painter.drawLine(QPointF(line_x, flag_top + 8.0), QPointF(line_x, bottom));
    painter.fillRect(QRectF(line_x - 1.0, flag_top, 3.0, 8.0), flag_color);
  }
  painter.restore();
}

void TimelineWidget::ApplyZoom(double factor, double anchor_x) {
  if (model_.frame_count() <= 0) {
    return;
  }
  StopAutoPan();
  const double before = model_.pixels_per_frame();
  const double after = model_.ZoomAt(factor, anchor_x);
  NotifyViewChanged();
  if (std::abs(after - before) > 1e-9) {
    update();
  }
}

void TimelineWidget::DrawPlayhead(QPainter& painter) {
  if (model_.frame_count() <= 0) {
    return;
  }
  const double bottom = static_cast<double>(ContentBottom());
  const double playhead_x = model_.FrameToX(current_frame_);
  const QColor playhead_color(QStringLiteral("#ffb454"));

  if (playhead_x >= -1.0 && playhead_x <= static_cast<double>(width()) + 1.0) {
    const double cell_width = std::max(1.0, model_.pixels_per_frame());
    painter.fillRect(QRectF(playhead_x, 0.0, cell_width, bottom),
                     QColor(255, 180, 84, 28));
    painter.setPen(QPen(playhead_color, 1.0));
    painter.drawLine(QPointF(playhead_x + 0.5, 0.0),
                     QPointF(playhead_x + 0.5, bottom));

    QPolygonF head;
    head << QPointF(playhead_x - 5.0, 0.0) << QPointF(playhead_x + 5.0, 0.0)
         << QPointF(playhead_x, 7.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(playhead_color);
    painter.drawPolygon(head);
    return;
  }

  // M7：播放头常驻——滚出可见范围时贴边画方向箭头（不乱画在画布上）。
  const bool before_view = playhead_x < 0.0;
  const double edge = before_view ? 0.0 : static_cast<double>(width());
  const double dir = before_view ? 1.0 : -1.0;
  painter.setPen(Qt::NoPen);
  painter.setBrush(playhead_color);
  QPolygonF arrow;
  arrow << QPointF(edge, bottom / 2.0 - 6.0) << QPointF(edge, bottom / 2.0 + 6.0)
        << QPointF(edge + dir * kOffViewArrowWidth, bottom / 2.0);
  painter.drawPolygon(arrow);
  painter.setPen(QPen(playhead_color, 1.0));
  painter.drawLine(QPointF(before_view ? 0.5 : edge - 0.5, 0.0),
                   QPointF(before_view ? 0.5 : edge - 0.5, bottom));
}

void TimelineWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, false);

  const QRect ruler_rect(0, 0, width(), RulerHeight());
  painter.fillRect(rect(), QColor(QStringLiteral("#18181b")));
  painter.fillRect(ruler_rect, QColor(QStringLiteral("#1c1c20")));
  painter.setPen(QColor(QStringLiteral("#2b2b31")));
  painter.drawLine(0, RulerHeight() - 1, width(), RulerHeight() - 1);

  if (model_.frame_count() <= 0) {
    painter.setPen(QColor(QStringLiteral("#777781")));
    QFont hint_font = painter.font();
    hint_font.setPointSizeF(9.0);
    painter.setFont(hint_font);
    painter.drawText(ruler_rect.adjusted(12, 0, -12, 0),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("打开视频后显示帧刻度与波形"));
    return;
  }

  QFont tick_font = painter.font();
  tick_font.setPointSizeF(8.5);
  painter.setFont(tick_font);

  // 刻度几何：kFixed 与旧实现逐像素一致（单测按 y=36 起算次刻度）；
  // kFill 标尺更矮，按实际高度压缩，避免刻度顶进波形区。
  const int ruler_h = RulerHeight();
  int major_tick_top = 21;
  int minor_tick_top = 36;
  double label_top = 3.0;
  double label_height = 16.0;
  if (height_policy_ == HeightPolicy::kFill) {
    label_top = 2.0;
    label_height = std::max(10.0, static_cast<double>(ruler_h) - 15.0);
    major_tick_top = std::max(6, ruler_h * 3 / 5);
    minor_tick_top = std::max(major_tick_top, ruler_h * 4 / 5);
  }

  const std::vector<lapian::core::TimelineTick> ticks = model_.BuildTicks();
  for (const lapian::core::TimelineTick& tick : ticks) {
    const double line_x = std::floor(tick.x) + 0.5;
    if (line_x < 0.0 || line_x > static_cast<double>(width())) {
      continue;
    }
    const int tick_top = tick.major ? major_tick_top : minor_tick_top;
    painter.setPen(tick.major ? QColor(QStringLiteral("#55555f"))
                              : QColor(QStringLiteral("#32323a")));
    painter.drawLine(QPointF(line_x, tick_top),
                     QPointF(line_x, RulerHeight() - 2));

    if (!tick.major) {
      continue;
    }
    const double label_x = tick.x + 4.0;
    if (label_x >= static_cast<double>(width())) {
      continue;
    }
    painter.setPen(QColor(QStringLiteral("#a8a8b2")));
    painter.drawText(QRectF(label_x, label_top,
                            static_cast<double>(width()) - label_x,
                            label_height),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QString::number(static_cast<qlonglong>(tick.frame)));
  }

  if (WaveformHeight() > 0) {
    DrawWaveform(painter,
                 QRectF(0.0, static_cast<double>(RulerHeight()),
                        static_cast<double>(width()),
                        static_cast<double>(WaveformHeight())));
  }

  // 色条叠加在刻度/波形之上，播放头最后绘制以保持可见。
  DrawLoopRange(painter,
                QRectF(0.0, 0.0, static_cast<double>(width()),
                       static_cast<double>(ContentBottom())));
  // 标记在色条之上、播放头之下：粉色竖线 + 小旗。
  DrawMarks(painter,
            QRectF(0.0, 0.0, static_cast<double>(width()),
                   static_cast<double>(ContentBottom())));

  DrawPlayhead(painter);
}

void TimelineWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  // C5：几何变化（窄窗/全屏/DPI 切换）打断自动平移；下一次 tick 用新宽度。
  StopAutoPan();
  const double scroll_before = model_.scroll();
  const double ppf_before = model_.pixels_per_frame();
  model_.SetViewportWidth(width());
  if (fit_mode_) {
    model_.Fit();
  }
  if (follow_playhead_ && model_.frame_count() > 0) {
    model_.EnsureFrameVisible(current_frame_, kPlayheadMarginPx);
  }
  if (std::abs(model_.scroll() - scroll_before) > 1e-9 ||
      std::abs(model_.pixels_per_frame() - ppf_before) > 1e-9) {
    NotifyViewChanged();
  }
  update();
}

void TimelineWidget::wheelEvent(QWheelEvent* event) {
  if (model_.frame_count() <= 0) {
    event->ignore();
    return;
  }

  const QPoint angle = event->angleDelta();
  if (event->modifiers().testFlag(Qt::ShiftModifier) && angle.y() != 0) {
    SetScrollPixels(model_.scroll() - static_cast<double>(angle.y()));
    event->accept();
    return;
  }
  if (angle.x() != 0 && angle.y() == 0) {
    SetScrollPixels(model_.scroll() + static_cast<double>(angle.x()));
    event->accept();
    return;
  }
  if (angle.y() == 0) {
    event->ignore();
    return;
  }

  const double steps = std::clamp(angle.y() / 120.0, -4.0, 4.0);
  ApplyZoom(std::pow(1.25, steps), event->position().x());
  event->accept();
}
