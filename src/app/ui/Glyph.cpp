#include "app/ui/Glyph.h"

#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSignalBlocker>
#include <QPolygonF>
#include <QToolButton>

#include <algorithm>

namespace lapian::ui {
namespace {

// 全部图标在 16×16 的逻辑网格上画，再按 size/16 缩放。
constexpr qreal kGrid = 16.0;

QPolygonF Poly(std::initializer_list<QPointF> points) {
  QPolygonF polygon;
  for (const QPointF& point : points) {
    polygon << point;
  }
  return polygon;
}

void DrawGlyph(QPainter& painter, GlyphId id, const QColor& color) {
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  QPen stroke(color);
  stroke.setWidthF(1.6);
  stroke.setCapStyle(Qt::RoundCap);
  stroke.setJoinStyle(Qt::RoundJoin);
  QPen thin(color);
  thin.setWidthF(1.3);
  thin.setCapStyle(Qt::RoundCap);

  switch (id) {
    case GlyphId::kToStart:
      painter.drawRect(QRectF(3.0, 3.0, 2.0, 10.0));
      painter.drawPolygon(Poly({{6.5, 3.0}, {6.5, 13.0}, {13.5, 8.0}}));
      break;
    case GlyphId::kStepBack:
      painter.drawPolygon(Poly({{10.5, 3.0}, {10.5, 13.0}, {3.5, 8.0}}));
      break;
    case GlyphId::kPlay:
      painter.drawPolygon(Poly({{5.0, 3.0}, {5.0, 13.0}, {13.0, 8.0}}));
      break;
    case GlyphId::kPause:
      painter.drawRect(QRectF(4.5, 3.0, 2.6, 10.0));
      painter.drawRect(QRectF(8.9, 3.0, 2.6, 10.0));
      break;
    case GlyphId::kStepFwd:
      painter.drawPolygon(Poly({{5.5, 3.0}, {5.5, 13.0}, {12.5, 8.0}}));
      break;
    case GlyphId::kToEnd:
      painter.drawPolygon(Poly({{2.5, 3.0}, {2.5, 13.0}, {9.5, 8.0}}));
      painter.drawRect(QRectF(11.0, 3.0, 2.0, 10.0));
      break;
    case GlyphId::kSetIn:
      painter.drawRect(QRectF(3.6, 2.0, 1.8, 12.0));
      painter.drawPolygon(Poly({{13.0, 3.8}, {13.0, 12.2}, {7.2, 8.0}}));
      break;
    case GlyphId::kSetOut:
      painter.drawRect(QRectF(10.6, 2.0, 1.8, 12.0));
      painter.drawPolygon(Poly({{3.0, 3.8}, {3.0, 12.2}, {8.8, 8.0}}));
      break;
    case GlyphId::kVolume:
      painter.drawPolygon(Poly({{2.0, 6.0}, {4.6, 6.0}, {7.6, 3.0}, {7.6, 13.0},
                                {4.6, 10.0}, {2.0, 10.0}}));
      painter.setPen(stroke);
      painter.setBrush(Qt::NoBrush);
      painter.drawArc(QRectF(6.2, 4.6, 5.6, 6.8), -55 * 16, 110 * 16);
      painter.drawArc(QRectF(6.2, 2.2, 9.2, 11.6), -50 * 16, 100 * 16);
      break;
    case GlyphId::kMute:
      painter.drawPolygon(Poly({{2.0, 6.0}, {4.6, 6.0}, {7.6, 3.0}, {7.6, 13.0},
                                {4.6, 10.0}, {2.0, 10.0}}));
      painter.setPen(stroke);
      painter.drawLine(QPointF(9.6, 5.8), QPointF(13.8, 10.2));
      painter.drawLine(QPointF(13.8, 5.8), QPointF(9.6, 10.2));
      break;
    case GlyphId::kLoop: {
      painter.setPen(stroke);
      painter.setBrush(Qt::NoBrush);
      QPainterPath path;
      path.moveTo(4.4, 12.0);
      path.lineTo(10.0, 12.0);
      path.arcTo(QRectF(8.0, 4.0, 6.0, 6.0), 270, -120);
      painter.drawPath(path);
      painter.setPen(Qt::NoPen);
      painter.setBrush(color);
      painter.drawPolygon(Poly({{5.4, 9.4}, {5.4, 14.6}, {2.2, 12.0}}));
      painter.drawPolygon(Poly({{12.2, 6.6}, {9.0, 4.0}, {12.2, 1.6}}));
      break;
    }
    case GlyphId::kWave: {
      const double heights[] = {4.0, 9.0, 12.0, 8.0, 5.0};
      for (int i = 0; i < 5; ++i) {
        const double x = 2.6 + i * 2.7;
        const double h = heights[i];
        painter.drawRoundedRect(QRectF(x, 8.0 - h / 2.0, 1.7, h), 0.85, 0.85);
      }
      break;
    }
    case GlyphId::kFlag:
      painter.setPen(thin);
      painter.drawLine(QPointF(4.2, 2.0), QPointF(4.2, 14.0));
      painter.setPen(Qt::NoPen);
      painter.drawPolygon(Poly({{5.2, 2.4}, {13.2, 5.0}, {5.2, 7.8}}));
      break;
    case GlyphId::kCamera:
      painter.setPen(stroke);
      painter.setBrush(Qt::NoBrush);
      painter.drawRoundedRect(QRectF(1.8, 4.8, 12.4, 8.4), 1.6, 1.6);
      painter.drawEllipse(QPointF(8.0, 9.0), 2.6, 2.6);
      painter.setPen(Qt::NoPen);
      painter.setBrush(color);
      painter.drawRoundedRect(QRectF(5.6, 2.8, 4.8, 2.2), 0.8, 0.8);
      break;
    case GlyphId::kFull: {
      painter.setPen(stroke);
      painter.setBrush(Qt::NoBrush);
      const double a = 2.6;
      const double b = 5.4;
      const double c = 13.4;
      // 四个角
      painter.drawLine(QPointF(a, b), QPointF(a, a));
      painter.drawLine(QPointF(a, a), QPointF(b, a));
      painter.drawLine(QPointF(c - b + a, a), QPointF(c, a));
      painter.drawLine(QPointF(c, a), QPointF(c, b));
      painter.drawLine(QPointF(c, c - b + a), QPointF(c, c));
      painter.drawLine(QPointF(c, c), QPointF(c - b + a, c));
      painter.drawLine(QPointF(b, c), QPointF(a, c));
      painter.drawLine(QPointF(a, c), QPointF(a, c - b + a));
      break;
    }
    case GlyphId::kPanel:
      painter.setPen(stroke);
      painter.setBrush(Qt::NoBrush);
      painter.drawRoundedRect(QRectF(2.2, 3.0, 11.6, 10.0), 1.8, 1.8);
      painter.drawLine(QPointF(10.0, 3.0), QPointF(10.0, 13.0));
      break;
    case GlyphId::kOpen:
      painter.setPen(stroke);
      painter.setBrush(Qt::NoBrush);
      painter.drawPolyline(Poly({{2.2, 12.6}, {2.2, 4.2}, {6.4, 4.2}, {7.8, 6.0},
                                 {13.8, 6.0}, {13.8, 12.6}, {2.2, 12.6}}));
      break;
    case GlyphId::kOnline:
      painter.setPen(stroke);
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(QPointF(8.0, 8.0), 5.6, 5.6);
      painter.setPen(Qt::NoPen);
      painter.setBrush(color);
      painter.drawPolygon(Poly({{6.6, 5.4}, {6.6, 10.6}, {10.6, 8.0}}));
      break;
    case GlyphId::kHelp:
      painter.setPen(stroke);
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(QPointF(8.0, 8.0), 5.8, 5.8);
      painter.setPen(QPen(color));
      {
        QFont font = painter.font();
        font.setPixelSize(9);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(QRectF(2.0, 3.2, 12.0, 10.0), Qt::AlignCenter,
                         QStringLiteral("?"));
      }
      break;
    case GlyphId::kSettings: {
      painter.setPen(Qt::NoPen);
      painter.setBrush(color);
      painter.save();
      painter.translate(8.0, 8.0);
      for (int i = 0; i < 8; ++i) {
        painter.drawRoundedRect(QRectF(-1.1, -7.4, 2.2, 3.0), 0.7, 0.7);
        painter.rotate(45.0);
      }
      painter.restore();
      painter.setPen(QPen(color, 2.0));
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(QPointF(8.0, 8.0), 3.7, 3.7);
      break;
    }
    case GlyphId::kClock:
      painter.setPen(stroke);
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(QPointF(8.0, 8.0), 5.6, 5.6);
      painter.drawLine(QPointF(8.0, 4.8), QPointF(8.0, 8.2));
      painter.drawLine(QPointF(8.0, 8.2), QPointF(10.6, 9.4));
      break;
    case GlyphId::kPlus:
      painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
      painter.drawLine(QPointF(8.0, 3.4), QPointF(8.0, 12.6));
      painter.drawLine(QPointF(3.4, 8.0), QPointF(12.6, 8.0));
      break;
    case GlyphId::kChevronUp:
      painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawPolyline(Poly({{4.4, 10.0}, {8.0, 6.2}, {11.6, 10.0}}));
      break;
    case GlyphId::kChevronDown:
      painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawPolyline(Poly({{4.4, 6.2}, {8.0, 10.0}, {11.6, 6.2}}));
      break;
    case GlyphId::kChevronLeft:
      painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawPolyline(Poly({{10.0, 4.4}, {6.2, 8.0}, {10.0, 11.6}}));
      break;
    case GlyphId::kChevronRight:
      painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawPolyline(Poly({{6.0, 4.4}, {9.8, 8.0}, {6.0, 11.6}}));
      break;
    case GlyphId::kInfo:
      painter.setPen(stroke);
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(QPointF(8.0, 8.0), 5.6, 5.6);
      painter.setPen(Qt::NoPen);
      painter.setBrush(color);
      painter.drawEllipse(QPointF(8.0, 5.2), 1.0, 1.0);
      painter.drawRoundedRect(QRectF(7.2, 7.0, 1.6, 5.0), 0.8, 0.8);
      break;
    case GlyphId::kImage:
      painter.setPen(stroke);
      painter.setBrush(Qt::NoBrush);
      painter.drawRoundedRect(QRectF(2.0, 3.4, 12.0, 9.2), 1.6, 1.6);
      painter.drawEllipse(QPointF(5.8, 6.6), 1.1, 1.1);
      painter.drawPolyline(Poly({{3.4, 11.4}, {7.2, 8.0}, {9.8, 10.4}, {11.4, 8.8},
                                 {12.8, 10.2}}));
      break;
    case GlyphId::kCheck:
      painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawPolyline(Poly({{3.6, 8.4}, {6.6, 11.4}, {12.4, 4.8}}));
      break;
    case GlyphId::kFilter:
      painter.drawPolygon(Poly({{2.4, 3.0}, {13.6, 3.0}, {9.2, 8.4}, {9.2, 13.0},
                                {6.8, 11.6}, {6.8, 8.4}}));
      break;
    case GlyphId::kFolder:
      painter.setPen(stroke);
      painter.setBrush(Qt::NoBrush);
      painter.drawPolyline(Poly({{2.2, 12.8}, {2.2, 4.0}, {6.2, 4.0}, {7.6, 5.8},
                                 {13.8, 5.8}, {13.8, 12.8}, {2.2, 12.8}}));
      break;
  }
}

}  // namespace

QPixmap GlyphPixmap(GlyphId id, int size, const QColor& color,
                    qreal device_pixel_ratio) {
  qreal dpr = device_pixel_ratio;
  if (dpr <= 0.0) {
    dpr = QGuiApplication::primaryScreen() != nullptr
              ? QGuiApplication::primaryScreen()->devicePixelRatio()
              : 1.0;
  }
  const int logical = std::max(8, size);
  QPixmap pixmap(static_cast<int>(logical * dpr),
                 static_cast<int>(logical * dpr));
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.scale(logical / kGrid, logical / kGrid);
  DrawGlyph(painter, id, color);
  painter.end();
  return pixmap;
}

void BindGlyph(QToolButton* button, GlyphId id, const QColor& normal,
               const QColor& active, int size) {
  if (button == nullptr) {
    return;
  }
  const QIcon normal_icon(GlyphPixmap(id, size, normal));
  const QIcon active_icon(GlyphPixmap(id, size, active));
  button->setIcon(normal_icon);
  button->setIconSize(QSize(size, size));
  const quint64 id_value = static_cast<quint64>(id);
  QObject::connect(button, &QToolButton::toggled, button,
                   [button, id, normal, active, size, id_value](bool on) {
                     Q_UNUSED(id_value);
                     button->setIcon(QIcon(GlyphPixmap(id, size, on ? active : normal)));
                   });
  if (button->isCheckable() && button->isChecked()) {
    button->setIcon(active_icon);
  }
}

void BindToolAction(QToolButton* button, QAction* action, GlyphId id,
                    const QColor& normal, const QColor& active, int size) {
  if (button == nullptr || action == nullptr) {
    return;
  }
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setFocusPolicy(Qt::NoFocus);
  button->setAutoRaise(true);
  button->setIconSize(QSize(size, size));
  button->setCheckable(action->isCheckable());
  auto sync = [button, action]() {
    const QSignalBlocker blocker(button);
    button->setEnabled(action->isEnabled());
    button->setChecked(action->isChecked());
  };
  sync();
  BindGlyph(button, id, normal, active, size);
  QObject::connect(action, &QAction::changed, button, sync);
  QObject::connect(button, &QToolButton::clicked, action, [action]() {
    if (action->isEnabled()) {
      action->trigger();
    }
  });
}

}  // namespace lapian::ui
