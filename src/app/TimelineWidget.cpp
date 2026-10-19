#include "app/TimelineWidget.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPaintEvent>
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

namespace {

constexpr int kTimelineHeight = 72;
constexpr double kPlayheadMarginPx = 8.0;

}  // namespace

TimelineWidget::TimelineWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("timeline"));
  setFixedHeight(kTimelineHeight);
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

void TimelineWidget::Clear() { SetFrameCount(0); }

void TimelineWidget::SetFrameCount(std::int64_t count) {
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

int TimelineWidget::RulerHeight() const {
  return std::max(0, height() - kScrollBarHeight);
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

  const double playhead_x = model_.FrameToX(current_frame_);
  if (playhead_x >= -1.0 && playhead_x <= static_cast<double>(width()) + 1.0) {
    const double cell_width = std::max(1.0, model_.pixels_per_frame());
    painter.fillRect(QRectF(playhead_x, 0.0, cell_width,
                            static_cast<double>(RulerHeight())),
                     QColor(255, 180, 84, 28));
    painter.setPen(QPen(QColor(QStringLiteral("#ffb454")), 1.0));
    painter.drawLine(QPointF(playhead_x + 0.5, 0.0),
                     QPointF(playhead_x + 0.5,
                             static_cast<double>(RulerHeight())));

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
