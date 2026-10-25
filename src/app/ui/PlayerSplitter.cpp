#include "app/ui/PlayerSplitter.h"

#include <QColor>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include "app/ui/Theme.h"
#include "app/ui/Tokens.h"

PlayerSplitHandle::PlayerSplitHandle(Qt::Orientation orientation,
                                     PlayerSplitter* parent)
    : QSplitterHandle(orientation, parent) {
  setObjectName(orientation == Qt::Horizontal ? QStringLiteral("v2HSplitHandle")
                                              : QStringLiteral("v2VSplitHandle"));
  // 不抢焦点：所有全局快捷键（Space/PgUp/方向键）必须先到窗口事件过滤器。
  setFocusPolicy(Qt::NoFocus);
  setMouseTracking(true);
  accent_ = lapian::ui::Color(lapian::ui::kColorAccent);
  line_ = lapian::ui::Color(lapian::ui::kColorBorder);
}

void PlayerSplitHandle::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  const QRect r = rect();
  painter.fillRect(r, lapian::ui::Color(lapian::ui::kColorSurface));
  // 常态一条 1px 分隔线；悬停时换成 2px accent 细线（规划 §7.4）。
  const int thickness = hovered_ ? 2 : 1;
  const QColor color = hovered_ ? accent_ : line_;
  if (orientation() == Qt::Horizontal) {
    const int x = r.center().x() - thickness / 2;
    painter.fillRect(QRect(x, r.top(), thickness, r.height()), color);
  } else {
    const int y = r.center().y() - thickness / 2;
    painter.fillRect(QRect(r.left(), y, r.width(), thickness), color);
  }
}

void PlayerSplitHandle::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event != nullptr && event->button() == Qt::LeftButton) {
    emit resetRequested();
    event->accept();
    return;
  }
  QSplitterHandle::mouseDoubleClickEvent(event);
}

void PlayerSplitHandle::enterEvent(QEnterEvent* event) {
  hovered_ = true;
  update();
  QSplitterHandle::enterEvent(event);
}

void PlayerSplitHandle::leaveEvent(QEvent* event) {
  hovered_ = false;
  update();
  QSplitterHandle::leaveEvent(event);
}

PlayerSplitter::PlayerSplitter(Qt::Orientation orientation, QWidget* parent)
    : QSplitter(orientation, parent) {
  setChildrenCollapsible(false);
  setHandleWidth(lapian::ui::TokensFor(lapian::ui::Density::kRegular).handle);
  setFocusPolicy(Qt::NoFocus);
  setOpaqueResize(true);
}

void PlayerSplitter::SetHandleWidthPx(int px) {
  if (handleWidth() != px && px > 0) {
    setHandleWidth(px);
  }
}

void PlayerSplitter::SetCollapsed(bool collapsed) {
  if (collapsed_ == collapsed) {
    return;
  }
  collapsed_ = collapsed;
  // 折叠时禁用 handle：用户拖不动 44px 的检查器，也不会把画面压成 0。
  const int count = this->count();
  for (int i = 0; i < count; ++i) {
    if (QSplitterHandle* handle = this->handle(i)) {
      handle->setEnabled(!collapsed);
    }
  }
}

QSplitterHandle* PlayerSplitter::createHandle() {
  auto* handle = new PlayerSplitHandle(orientation(), this);
  connect(handle, &PlayerSplitHandle::resetRequested, this,
          &PlayerSplitter::resetRequested);
  return handle;
}
