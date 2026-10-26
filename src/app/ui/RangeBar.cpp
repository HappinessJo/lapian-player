#include "app/ui/RangeBar.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "app/ui/Theme.h"
#include "core/TimelineModel.h"

namespace {

// 原型 plan.html §"时间轴" 的取值（.ztrack/.wtrack/.zwin/.wspan/.out/.ph）。
// 颜色照抄：轨道 #101014 / #0e0e12 + 1px #26262e，视图窗 = accent 22% 底、
// accent 60% 边框、手柄实心 accent；工作区 = #2b3140 / 开循环走 accent 24%。
//
// 横向 8px 内边距（原型 `.zrow` 的 `padding: 2px 8px`）**必须与画布一致**：
// `TimelinePanel` 给画布容器也留了 8px 左右外边距，于是
//   滑块上的帧 x = 8 + ratio × (面板宽 − 16)
//   画布上的帧 x = 8 + ratio × (面板宽 − 16)     ← 同一个 ratio，同一像素
// 两侧相等（`TimelineBarAlignForTest()` 断言 |dx| ≤ 1）。若只给滑块缩 8px 而画布
// 铺满，工作区两端会比画布"短一截"（用户 2026-10-26 反馈的那个偏差）。
constexpr int kTrackInset = 8;
constexpr double kViewHandleWidth = 5.0;
constexpr double kWorkHandleWidth = 7.0;
constexpr double kMinThumbWidth = 6.0;
constexpr double kMinWorkSpanWidth = 8.0;
constexpr double kEdgeGrabPx = 7.0;

}  // namespace

RangeBar::RangeBar(Mode mode, QWidget* parent) : QWidget(parent), mode_(mode) {
  setObjectName(mode == Mode::kView ? QStringLiteral("v2ViewBar")
                                    : QStringLiteral("v2WorkBar"));
  setFocusPolicy(Qt::NoFocus);
  setMouseTracking(true);
  setAttribute(Qt::WA_StyledBackground, false);
  setToolTip(mode == Mode::kView
                 ? QStringLiteral("视图窗：拖两端缩放 · 拖中间平移 · 双击适应窗口")
                 : QStringLiteral("工作区：拖两端改 A/B · 拖中间平移 · "
                                  "双击清除 A/B（Shift+L）"));
  setFixedHeight(mode == Mode::kView ? tokens_.row_zoom : tokens_.row_work);

  auto make_handle = [this](const char* name, bool left) {
    auto* handle = new QWidget(this);
    handle->setObjectName(QString::fromLatin1(name));
    handle->setFocusPolicy(Qt::NoFocus);
    // 只做 objectName 可寻址 + 几何断言：命中判定在父控件里算，避免子控件吃事件。
    handle->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    handle->setAttribute(Qt::WA_NoSystemBackground, true);
    handle->setCursor(left ? Qt::SizeHorCursor : Qt::SizeHorCursor);
    handle->hide();
    return handle;
  };
  if (mode_ == Mode::kView) {
    left_handle_widget_ = make_handle("v2ViewHandleLeft", true);
    right_handle_widget_ = make_handle("v2ViewHandleRight", false);
  } else {
    left_handle_widget_ = make_handle("v2WorkHandleLeft", true);
    right_handle_widget_ = make_handle("v2WorkHandleRight", false);
  }
}

void RangeBar::SetModel(const lapian::core::TimelineModel* model) {
  model_ = model;
  ComputeRects();
  UpdateHandleWidgets();
  update();
}

void RangeBar::SetWorkRange(bool has_in, bool has_out, bool enabled,
                            std::int64_t in, std::int64_t out) {
  if (has_in_ == has_in && has_out_ == has_out && work_enabled_ == enabled &&
      work_in_ == in && work_out_ == out) {
    return;
  }
  has_in_ = has_in;
  has_out_ = has_out;
  work_enabled_ = enabled;
  work_in_ = in;
  work_out_ = out;
  ComputeRects();
  UpdateHandleWidgets();
  update();
}

void RangeBar::SetPlayheadFrame(std::int64_t frame) {
  if (playhead_ == frame) {
    return;
  }
  playhead_ = frame;
  if (mode_ == Mode::kView) {
    update();
  }
}

void RangeBar::SetMediaReady(bool ready) {
  if (media_ready_ == ready) {
    return;
  }
  media_ready_ = ready;
  update();
}

void RangeBar::ApplyTokens(const lapian::ui::Tokens& tokens) {
  tokens_ = tokens;
  setFixedHeight(mode_ == Mode::kView ? tokens.row_zoom : tokens.row_work);
  ComputeRects();
  UpdateHandleWidgets();
  update();
}

void RangeBar::RefreshView() {
  ComputeRects();
  UpdateHandleWidgets();
  update();
}

bool RangeBar::HasMedia() const {  return media_ready_ && model_ != nullptr && model_->frame_count() > 0 &&
         model_->pixels_per_frame() > 0.0 && model_->viewport_width() > 0.0;
}

qint64 RangeBar::MaxFrame() const {
  return model_ == nullptr ? 0 : std::max<std::int64_t>(0, model_->frame_count() - 1);
}

double RangeBar::TrackLeft() const { return static_cast<double>(kTrackInset); }

double RangeBar::TrackWidth() const {
  return std::max(1.0, static_cast<double>(width()) - 2.0 * kTrackInset);
}

int RangeBar::TrackHeight() const {
  return mode_ == Mode::kView ? tokens_.track_zoom : tokens_.track_work;
}

int RangeBar::TrackTop() const {
  return std::max(0, (height() - TrackHeight()) / 2);
}

double RangeBar::ViewStartFrame() const {
  if (model_ == nullptr || model_->pixels_per_frame() <= 0.0) {
    return 0.0;
  }
  return model_->scroll() / model_->pixels_per_frame();
}

double RangeBar::ViewSpanFrames() const {
  if (model_ == nullptr || model_->pixels_per_frame() <= 0.0) {
    return 1.0;
  }
  return std::max(1.0, model_->viewport_width() / model_->pixels_per_frame());
}

double RangeBar::XForFrame(double frame) const {
  const double count =
      model_ == nullptr ? 0.0 : static_cast<double>(model_->frame_count());
  if (count <= 0.0) {
    return TrackLeft();
  }
  const double ratio = std::clamp(frame / count, 0.0, 1.0);
  return TrackLeft() + ratio * TrackWidth();
}

double RangeBar::XForViewFrame(double frame) const {
  const double start = ViewStartFrame();
  const double span = ViewSpanFrames();
  const double ratio = std::clamp((frame - start) / span, 0.0, 1.0);
  return TrackLeft() + ratio * TrackWidth();
}

double RangeBar::FrameForX(double x) const {
  const double count =
      model_ == nullptr ? 0.0 : static_cast<double>(model_->frame_count());
  if (count <= 0.0) {
    return 0.0;
  }
  const double ratio =
      std::clamp((x - TrackLeft()) / TrackWidth(), 0.0, 1.0);
  return ratio * count;
}

double RangeBar::ViewFrameForX(double x) const {
  const double ratio =
      std::clamp((x - TrackLeft()) / TrackWidth(), 0.0, 1.0);
  return ViewStartFrame() + ratio * ViewSpanFrames();
}

void RangeBar::ComputeRects() {
  thumb_rect_ = QRect();
  left_handle_rect_ = QRect();
  right_handle_rect_ = QRect();

  const int track_top = TrackTop();
  const int track_h = TrackHeight();
  const int track_bottom = track_top + track_h;

  if (mode_ == Mode::kView) {
    // 视图窗 = 全片坐标上的 [scroll/ppf, (scroll+viewport)/ppf]。
    const double left_f = ViewStartFrame();
    const double right_f = left_f + ViewSpanFrames();
    double x0 = XForFrame(left_f);
    double x1 = XForFrame(right_f);
    if (x1 - x0 < kMinThumbWidth) {
      const double center = (x0 + x1) / 2.0;
      x0 = center - kMinThumbWidth / 2.0;
      x1 = center + kMinThumbWidth / 2.0;
    }
    x0 = std::clamp(x0, TrackLeft(), TrackLeft() + TrackWidth());
    x1 = std::clamp(x1, TrackLeft(), TrackLeft() + TrackWidth());
    // 手柄挂在轨道上下各 2px（原型 top/bottom -2）。
    thumb_rect_ = QRect(QPoint(static_cast<int>(std::floor(x0)),
                               std::max(0, track_top - 2)),
                        QPoint(static_cast<int>(std::ceil(x1)) - 1,
                               track_bottom + 1));
    const int hw = static_cast<int>(kViewHandleWidth);
    left_handle_rect_ = QRect(static_cast<int>(std::lround(x0)) - 3,
                              thumb_rect_.top(), hw, thumb_rect_.height());
    right_handle_rect_ = QRect(static_cast<int>(std::lround(x1)) - 2,
                               thumb_rect_.top(), hw, thumb_rect_.height());
  } else {
    // 工作区 = 可见范围坐标上的 [A, B]（含 B 帧格）；未设 A/B 时代表"全片"。
    double left_f = 0.0;
    double right_f = static_cast<double>(MaxFrame()) + 1.0;
    if (has_in_) {
      left_f = static_cast<double>(work_in_);
      right_f = static_cast<double>(has_out_ ? work_out_ : work_in_) + 1.0;
    }
    double x0 = XForViewFrame(left_f);
    double x1 = XForViewFrame(right_f);
    if (x1 - x0 < kMinWorkSpanWidth) {
      const double center = (x0 + x1) / 2.0;
      x0 = center - kMinWorkSpanWidth / 2.0;
      x1 = center + kMinWorkSpanWidth / 2.0;
    }
    x0 = std::clamp(x0, TrackLeft(), TrackLeft() + TrackWidth());
    x1 = std::clamp(x1, TrackLeft(), TrackLeft() + TrackWidth());
    if (!has_in_) {
      // "全片"：撑满整条轨道（与原型 state.loop = 0..TOTAL-1 一致）。
      x0 = TrackLeft();
      x1 = TrackLeft() + TrackWidth();
    }
    thumb_rect_ = QRect(QPoint(static_cast<int>(std::floor(x0)),
                               std::max(0, track_top - 2)),
                        QPoint(static_cast<int>(std::ceil(x1)) - 1,
                               track_bottom + 1));
    const int hw = static_cast<int>(kWorkHandleWidth);
    left_handle_rect_ = QRect(static_cast<int>(std::lround(x0)) - 3,
                              thumb_rect_.top(), hw, thumb_rect_.height());
    right_handle_rect_ = QRect(static_cast<int>(std::lround(x1)) - 3,
                               thumb_rect_.top(), hw, thumb_rect_.height());
  }
  // 轨道左右各留 8px（与画布的 8px 外边距一致）；端点手柄在最边缘时可能探出
  // 半个身位，这里把**手柄矩形**夹回控件内（坐标映射仍按 8..width-8，不受影响）。
  for (QRect* handle : {&left_handle_rect_, &right_handle_rect_}) {
    if (handle->isNull()) {
      continue;
    }
    const int max_x = std::max(0, width() - handle->width());
    handle->moveLeft(std::clamp(handle->left(), 0, max_x));
  }
}

void RangeBar::UpdateHandleWidgets() {
  if (left_handle_widget_ == nullptr || right_handle_widget_ == nullptr) {
    return;
  }
  const bool visible = left_handle_rect_.isValid() && !left_handle_rect_.isEmpty();
  left_handle_widget_->setGeometry(left_handle_rect_);
  right_handle_widget_->setGeometry(right_handle_rect_);
  left_handle_widget_->setVisible(visible);
  right_handle_widget_->setVisible(visible);
}

void RangeBar::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  ComputeRects();
  UpdateHandleWidgets();
}

void RangeBar::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const double left = TrackLeft();
  const double track_w = TrackWidth();
  const int track_top = TrackTop();
  const int track_h = TrackHeight();

  // 轨道底
  const QRectF track(left, track_top, track_w, track_h);
  const QColor track_bg = mode_ == Mode::kView ? QColor(QStringLiteral("#101014"))
                                               : QColor(QStringLiteral("#0e0e12"));
  painter.setPen(QPen(QColor(QStringLiteral("#26262e")), 1.0));
  painter.setBrush(track_bg);
  painter.drawRoundedRect(track.adjusted(0.5, 0.5, -0.5, -0.5),
                          mode_ == Mode::kView ? 2.0 : 3.0,
                          mode_ == Mode::kView ? 2.0 : 3.0);

  if (!HasMedia()) {
    // 空态：两条滑块 30% 透明、不可拖（规划 §3 状态 1）。
    painter.setOpacity(0.3);
    if (mode_ == Mode::kWork) {
      painter.fillRect(QRectF(left, track_top - 2, track_w, track_h + 4),
                       QColor(QStringLiteral("#2b3140")));
    }
    return;
  }

  if (mode_ == Mode::kView) {
    // 视图窗
    const QColor win_bg(QStringLiteral("#29344e"));
    const QColor win_border(QStringLiteral("#37558f"));
    painter.setPen(QPen(win_border, 1.0));
    painter.setBrush(win_bg);
    painter.drawRoundedRect(thumb_rect_.adjusted(0, 0, 0, 0), 2.0, 2.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(lapian::ui::Color(lapian::ui::kColorAccent));
    painter.drawRoundedRect(left_handle_rect_, 2.0, 2.0);
    painter.drawRoundedRect(right_handle_rect_, 2.0, 2.0);

    // 播放头标记：挂在轨道上，按整条百分比定位（原型 `.ztrack .ph`）。
    const double ph_x = XForFrame(static_cast<double>(playhead_));
    if (ph_x >= left - 1.0 && ph_x <= left + track_w + 1.0) {
      QColor white(Qt::white);
      white.setAlphaF(0.85);
      painter.setPen(QPen(white, 1.5));
      painter.drawLine(QPointF(ph_x, track_top - 3.0),
                       QPointF(ph_x, track_top + track_h + 3.0));
    }
    return;
  }

  // 工作区：区外压暗 + 跨度块 + 手柄
  const bool on = has_in_ && has_out_ && work_enabled_;
  const QRectF span = thumb_rect_;
  QColor out(0, 0, 0);
  out.setAlphaF(0.55);
  if (span.left() > left) {
    painter.fillRect(QRectF(left, track_top, span.left() - left, track_h), out);
  }
  if (span.right() < left + track_w) {
    painter.fillRect(
        QRectF(span.right(), track_top, left + track_w - span.right(), track_h),
        out);
  }
  painter.setPen(QPen(on ? QColor(QStringLiteral("#37558f"))
                        : QColor(QStringLiteral("#4a5370")),
                     1.0));
  painter.setBrush(on ? QColor(QStringLiteral("#304064"))
                      : QColor(QStringLiteral("#2b3140")));
  painter.drawRoundedRect(span, 3.0, 3.0);
  painter.setPen(Qt::NoPen);
  painter.setBrush(on ? lapian::ui::Color(lapian::ui::kColorAccent)
                      : QColor(QStringLiteral("#8a93a8")));
  painter.drawRoundedRect(left_handle_rect_, 2.0, 2.0);
  painter.drawRoundedRect(right_handle_rect_, 2.0, 2.0);
  painter.setRenderHint(QPainter::Antialiasing, false);
}

void RangeBar::UpdateCursor(const QPoint& pos) {
  if (!HasMedia()) {
    setCursor(Qt::ArrowCursor);
    return;
  }
  const bool near_left =
      std::abs(static_cast<double>(pos.x()) - thumb_rect_.left()) <= kEdgeGrabPx;
  const bool near_right =
      std::abs(static_cast<double>(pos.x()) - thumb_rect_.right()) <= kEdgeGrabPx;
  if (near_left || near_right) {
    setCursor(Qt::SizeHorCursor);
  } else if (thumb_rect_.contains(pos)) {
    setCursor(Qt::OpenHandCursor);
  } else {
    setCursor(Qt::ArrowCursor);
  }
}

void RangeBar::BeginDrag(Grab grab, double press_x) {
  dragging_ = true;
  grab_ = grab;
  drag_start_x_ = press_x;
  drag_start_view_ = ViewStartFrame();
  drag_start_span_ = ViewSpanFrames();
  drag_start_in_ = has_in_ ? work_in_ : 0;
  drag_start_out_ = has_in_ ? (has_out_ ? work_out_ : work_in_) : MaxFrame();
  setCursor(grab == Grab::kMiddle ? Qt::ClosedHandCursor : Qt::SizeHorCursor);
}

void RangeBar::ApplyDrag(double x) {
  if (mode_ == Mode::kView) {
    const double min_span = 1.0;
    if (grab_ == Grab::kLeft) {
      const double end = drag_start_view_ + drag_start_span_;
      double start = FrameForX(x);
      start = std::clamp(start, 0.0, end - min_span);
      emit viewWindowRequested(start, end - start);
    } else if (grab_ == Grab::kRight) {
      const double start = drag_start_view_;
      double end = FrameForX(x);
      end = std::clamp(end, start + min_span,
                       static_cast<double>(model_->frame_count()));
      emit viewWindowRequested(start, end - start);
    } else {
      const double delta = FrameForX(x) - FrameForX(drag_start_x_);
      double start = drag_start_view_ + delta;
      const double count = static_cast<double>(model_->frame_count());
      start = std::clamp(start, 0.0, std::max(0.0, count - drag_start_span_));
      emit viewWindowRequested(start, drag_start_span_);
    }
    return;
  }

  if (!HasMedia()) {
    return;
  }
  const qint64 last = MaxFrame();
  if (grab_ == Grab::kLeft) {
    const qint64 frame = static_cast<qint64>(std::llround(ViewFrameForX(x)));
    emit workInRequested(std::clamp<qint64>(frame, 0, last));
  } else if (grab_ == Grab::kRight) {
    const qint64 frame = static_cast<qint64>(std::llround(ViewFrameForX(x)));
    emit workOutRequested(std::clamp<qint64>(frame, 0, last));
  } else {
    const double delta =
        std::round(ViewFrameForX(x) - ViewFrameForX(drag_start_x_));
    const qint64 span = std::max<qint64>(0, drag_start_out_ - drag_start_in_);
    qint64 in = static_cast<qint64>(std::llround(drag_start_in_ + delta));
    in = std::clamp<qint64>(in, 0, std::max<qint64>(0, last - span));
    emit workPanRequested(in, in + span);
  }
}

void RangeBar::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !HasMedia()) {
    QWidget::mousePressEvent(event);
    return;
  }
  const QPoint pos = event->position().toPoint();
  const double x = event->position().x();
  const bool near_left =
      std::abs(x - thumb_rect_.left()) <= kEdgeGrabPx;
  const bool near_right =
      std::abs(x - thumb_rect_.right()) <= kEdgeGrabPx;
  if (near_left && near_right) {
    // 跨度极窄（A=B 定帧 / 视图窗最短）：按点击落在中轴线哪边分左右端。
    BeginDrag(x <= thumb_rect_.center().x() + 0.5 ? Grab::kLeft : Grab::kRight,
              x);
  } else if (near_left) {
    BeginDrag(Grab::kLeft, x);
  } else if (near_right) {
    BeginDrag(Grab::kRight, x);
  } else if (thumb_rect_.contains(pos)) {
    BeginDrag(Grab::kMiddle, x);
  } else {
    // 点空白：视图窗/工作区整体居中到点击处（原型行为），随后可继续拖动。
    if (mode_ == Mode::kView) {
      const double span = ViewSpanFrames();
      const double count = static_cast<double>(model_->frame_count());
      double start = FrameForX(x) - span / 2.0;
      start = std::clamp(start, 0.0, std::max(0.0, count - span));
      emit viewWindowRequested(start, span);
    } else {
      const qint64 last = MaxFrame();
      const qint64 cur_in = has_in_ ? work_in_ : 0;
      const qint64 cur_out = has_in_ ? (has_out_ ? work_out_ : work_in_)
                                     : last;
      const qint64 span = std::max<qint64>(1, cur_out - cur_in + 1);
      qint64 in = static_cast<qint64>(std::llround(ViewFrameForX(x))) - span / 2;
      in = std::clamp<qint64>(in, 0, std::max<qint64>(0, last - span + 1));
      const qint64 out = std::min<qint64>(last, in + span - 1);
      emit workPanRequested(in, out);
    }
    BeginDrag(Grab::kMiddle, x);
  }
  event->accept();
}

void RangeBar::mouseMoveEvent(QMouseEvent* event) {
  if (!dragging_) {
    UpdateCursor(event->position().toPoint());
    QWidget::mouseMoveEvent(event);
    return;
  }
  ApplyDrag(event->position().x());
  event->accept();
}

void RangeBar::mouseReleaseEvent(QMouseEvent* event) {
  if (!dragging_) {
    QWidget::mouseReleaseEvent(event);
    return;
  }
  dragging_ = false;
  grab_ = Grab::kNone;
  UpdateCursor(event->position().toPoint());
  event->accept();
}

void RangeBar::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !HasMedia()) {
    QWidget::mouseDoubleClickEvent(event);
    return;
  }
  if (mode_ == Mode::kView) {
    emit fitRequested();
  } else {
    emit workClearRequested();
  }
  event->accept();
}

void RangeBar::wheelEvent(QWheelEvent* event) {
  if (!HasMedia() || model_ == nullptr) {
    event->ignore();
    return;
  }
  const QPoint angle = event->angleDelta();
  const double delta = angle.y() != 0 ? static_cast<double>(angle.y())
                                      : static_cast<double>(angle.x());
  if (delta == 0.0) {
    event->ignore();
    return;
  }
  if (event->modifiers().testFlag(Qt::ShiftModifier)) {
    // Shift+滚轮 = 平移（与画布一致）。
    emit scrollRequested(model_->scroll() - delta);
    event->accept();
    return;
  }
  const double steps = std::clamp(delta / 120.0, -4.0, 4.0);
  const double factor = std::pow(1.25, steps);
  // 锚点 = 播放头的画布 x（不可见时用画布中心），与 +/- 走同一条缩放路径。
  double anchor = model_->FrameToX(playhead_);
  if (anchor < 0.0 || anchor > model_->viewport_width()) {
    anchor = model_->viewport_width() / 2.0;
  }
  emit zoomAtRequested(factor, anchor);
  event->accept();
}

QString RangeBar::ViewWindowForTest() const {
  if (!HasMedia()) {
    return QStringLiteral("view=empty work=empty");
  }
  const double start = ViewStartFrame();
  const double span = ViewSpanFrames();
  const std::int64_t count = model_->frame_count();
  const std::int64_t first = std::clamp<std::int64_t>(
      static_cast<std::int64_t>(std::floor(start)), 0, count - 1);
  const std::int64_t last = std::clamp<std::int64_t>(
      static_cast<std::int64_t>(std::floor(start + span)), first, count - 1);
  if (mode_ == Mode::kView) {
    return QStringLiteral("view=%1-%2").arg(first).arg(last);
  }
  return QStringLiteral("work=%1-%2").arg(work_in_).arg(work_out_);
}

bool RangeBar::DragForTest(int from_x, int to_x) {
  if (!HasMedia()) {
    return false;
  }
  const QPointF from(static_cast<double>(from_x),
                     static_cast<double>(height()) / 2.0);
  const QPointF to(static_cast<double>(to_x),
                   static_cast<double>(height()) / 2.0);
  QMouseEvent press(QEvent::MouseButtonPress, from, mapToGlobal(from.toPoint()),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(this, &press);
  const bool started = dragging_;
  if (started) {
    QMouseEvent move(QEvent::MouseMove, to, mapToGlobal(to.toPoint()),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(this, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, to,
                        mapToGlobal(to.toPoint()), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(this, &release);
  }
  return started;
}
