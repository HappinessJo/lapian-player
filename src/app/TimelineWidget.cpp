#include "app/TimelineWidget.h"

#include <QColor>
#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

constexpr int kTimelineBaseHeight = 72;
constexpr int kRulerHeight = 58;
constexpr int kWaveformHeight = 56;
constexpr double kPlayheadMarginPx = 8.0;

}  // namespace

TimelineWidget::TimelineWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("timeline"));
  setFixedHeight(kTimelineBaseHeight);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  // 不抢走视频区的键盘焦点；滚轮缩放不需要焦点。
  setFocusPolicy(Qt::NoFocus);
  setToolTip(QStringLiteral("滚轮缩放 · Shift+滚轮横向滚动 · +/- 缩放"));

  scroll_bar_ = new QScrollBar(Qt::Horizontal, this);
  scroll_bar_->setObjectName(QStringLiteral("timelineScrollBar"));
  scroll_bar_->setFocusPolicy(Qt::NoFocus);
  scroll_bar_->setFixedHeight(kScrollBarHeight);
  scroll_bar_->setStyleSheet(QStringLiteral(
      "QScrollBar:horizontal { background:#18181b; height:14px; margin:0; border:0; }"
      "QScrollBar::handle:horizontal { background:#4a4a52; min-width:24px; "
      "border-radius:3px; }"
      "QScrollBar::handle:horizontal:hover { background:#5c5c66; }"
      "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }"
      "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { "
      "background:transparent; }"));

  connect(scroll_bar_, &QScrollBar::valueChanged, this, [this](int value) {
    model_.SetScroll(static_cast<double>(value));
    update();
  });
}

void TimelineWidget::Clear() {
  ClearWaveform();
  loop_range_.Clear();
  SetFrameCount(0);
}

void TimelineWidget::SetFrameCount(std::int64_t count) {
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
  SyncScrollBar();
  update();
}

void TimelineWidget::SetCurrentFrame(std::int64_t frame) {
  if (model_.frame_count() <= 0) {
    current_frame_ = 0;
    update();
    return;
  }
  current_frame_ =
      std::clamp<std::int64_t>(frame, 0, model_.frame_count() - 1);
  if (follow_playhead_) {
    model_.EnsureFrameVisible(current_frame_, kPlayheadMarginPx);
    SyncScrollBar();
  }
  update();
}

void TimelineWidget::FitToWindow() {
  fit_mode_ = true;
  model_.Fit();
  SyncScrollBar();
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
  model_.SetPixelsPerFrame(pixels_per_frame);
  fit_mode_ = model_.IsAtFit();
  SyncScrollBar();
  update();
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
  if (!scrubbing_) {
    follow_before_scrub_ = follow_playhead_;
    follow_playhead_ = false;
  }
  scrubbing_ = true;
  drag_target_frame_ = FrameAtPosition(event->position());
  SetCurrentFrame(drag_target_frame_);
  emit scrubStarted(static_cast<qint64>(drag_target_frame_));
  event->accept();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!scrubbing_ || !(event->buttons() & Qt::LeftButton)) {
    QWidget::mouseMoveEvent(event);
    return;
  }
  const std::int64_t target = FrameAtPosition(event->position());
  if (target != drag_target_frame_) {
    drag_target_frame_ = target;
    SetCurrentFrame(drag_target_frame_);
    emit scrubTargetChanged(static_cast<qint64>(drag_target_frame_));
  }
  event->accept();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !scrubbing_) {
    QWidget::mouseReleaseEvent(event);
    return;
  }
  const std::int64_t target = FrameAtPosition(event->position());
  if (target != drag_target_frame_) {
    drag_target_frame_ = target;
    SetCurrentFrame(drag_target_frame_);
    emit scrubTargetChanged(static_cast<qint64>(drag_target_frame_));
  }
  scrubbing_ = false;
  follow_playhead_ = follow_before_scrub_;
  emit scrubFinished(static_cast<qint64>(drag_target_frame_));
  event->accept();
}

int TimelineWidget::RulerHeight() const {
  return std::max(0, height() - kScrollBarHeight - WaveformHeight());
}

int TimelineWidget::WaveformHeight() const {
  return waveform_active_ && waveform_visible_ ? kWaveformHeight : 0;
}

void TimelineWidget::UpdateHeight() {
  setFixedHeight(kTimelineBaseHeight + WaveformHeight());
  updateGeometry();
  update();
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
  loop_range_ = range;
  update();
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

  // A/B 标签画在标尺下缘，避开顶部的帧号标签。
  QFont tag_font = painter.font();
  tag_font.setPointSizeF(8.0);
  tag_font.setBold(true);
  painter.setFont(tag_font);
  constexpr double kTagWidth = 15.0;
  constexpr double kTagHeight = 15.0;
  const double tag_y =
      std::max(1.0, static_cast<double>(RulerHeight()) - kTagHeight - 3.0);
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

void TimelineWidget::ApplyZoom(double factor, double anchor_x) {
  if (model_.frame_count() <= 0) {
    return;
  }
  const double before = model_.pixels_per_frame();
  const double after = model_.ZoomAt(factor, anchor_x);
  fit_mode_ = model_.IsAtFit();
  if (std::abs(after - before) > 1e-9) {
    SyncScrollBar();
    update();
  }
}

void TimelineWidget::SyncScrollBar() {
  if (scroll_bar_ == nullptr) {
    return;
  }
  const double max_scroll = model_.MaxScroll();
  const double max_scroll_clamped =
      std::min(max_scroll, static_cast<double>(std::numeric_limits<int>::max()));
  const int max_int = static_cast<int>(std::ceil(max_scroll_clamped));

  QSignalBlocker blocker(scroll_bar_);
  scroll_bar_->setRange(0, max_int);
  scroll_bar_->setPageStep(std::max(1, width()));
  scroll_bar_->setSingleStep(std::max(1, width() / 10));
  scroll_bar_->setValue(static_cast<int>(std::lround(model_.scroll())));
  scroll_bar_->setEnabled(max_int > 0);
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
                     QStringLiteral("打开视频后显示帧刻度"));
    return;
  }

  QFont tick_font = painter.font();
  tick_font.setPointSizeF(8.5);
  painter.setFont(tick_font);

  const std::vector<lapian::core::TimelineTick> ticks = model_.BuildTicks();
  for (const lapian::core::TimelineTick& tick : ticks) {
    const double line_x = std::floor(tick.x) + 0.5;
    if (line_x < 0.0 || line_x > static_cast<double>(width())) {
      continue;
    }
    const int tick_top = tick.major ? 21 : 36;
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
    painter.drawText(QRectF(label_x, 3.0, static_cast<double>(width()) - label_x,
                            16.0),
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
                       static_cast<double>(height() - kScrollBarHeight)));

  const double playhead_x = model_.FrameToX(current_frame_);
  const double playhead_bottom =
      static_cast<double>(height() - kScrollBarHeight);
  if (playhead_x >= -1.0 && playhead_x <= static_cast<double>(width()) + 1.0) {
    const double cell_width = std::max(1.0, model_.pixels_per_frame());
    painter.fillRect(QRectF(playhead_x, 0.0, cell_width, playhead_bottom),
                     QColor(255, 180, 84, 28));
    painter.setPen(QPen(QColor(QStringLiteral("#ffb454")), 1.0));
    painter.drawLine(QPointF(playhead_x + 0.5, 0.0),
                     QPointF(playhead_x + 0.5, playhead_bottom));

    QPolygonF head;
    head << QPointF(playhead_x - 5.0, 0.0) << QPointF(playhead_x + 5.0, 0.0)
         << QPointF(playhead_x, 7.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#ffb454")));
    painter.drawPolygon(head);
  }
}

void TimelineWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (scroll_bar_ != nullptr) {
    const int scroll_y = std::max(0, height() - kScrollBarHeight);
    scroll_bar_->setGeometry(0, scroll_y, width(), kScrollBarHeight);
  }
  model_.SetViewportWidth(width());
  if (fit_mode_) {
    model_.Fit();
  }
  if (follow_playhead_ && model_.frame_count() > 0) {
    model_.EnsureFrameVisible(current_frame_, kPlayheadMarginPx);
  }
  SyncScrollBar();
  update();
}

void TimelineWidget::wheelEvent(QWheelEvent* event) {
  if (model_.frame_count() <= 0) {
    event->ignore();
    return;
  }

  const QPoint angle = event->angleDelta();
  if (event->modifiers().testFlag(Qt::ShiftModifier) && angle.y() != 0) {
    model_.SetScroll(model_.scroll() - static_cast<double>(angle.y()));
    SyncScrollBar();
    update();
    event->accept();
    return;
  }
  if (angle.x() != 0 && angle.y() == 0) {
    model_.SetScroll(model_.scroll() + static_cast<double>(angle.x()));
    SyncScrollBar();
    update();
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
