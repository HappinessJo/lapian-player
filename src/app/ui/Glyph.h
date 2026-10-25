#pragma once

#include <QColor>
#include <QPixmap>
#include <QSize>

class QIcon;
class QAction;
class QToolButton;

namespace lapian::ui {

// 图标集合（对应 docs/ui-v2-proto/plan.html 里的 <symbol> 集）。
// 用 QPainter 画：不引入 qrc/PNG 资产、不依赖 Qt6Svg、不走图标字体，
// 颜色随令牌走，2× 渲染由 devicePixelRatio 处理。
enum class GlyphId {
  kToStart,
  kStepBack,
  kPlay,
  kPause,
  kStepFwd,
  kToEnd,
  kSetIn,
  kSetOut,
  kVolume,
  kMute,
  kLoop,
  kWave,
  kFlag,
  kCamera,
  kFull,
  kPanel,
  kOpen,
  kOnline,
  kHelp,
  kSettings,
  kClock,
  kPlus,
  kChevronUp,
  kChevronDown,
  kChevronLeft,
  kChevronRight,
  kInfo,
  kImage,
  kCheck,
  kFilter,
};

// 以逻辑像素 size 绘制（内部按 devicePixelRatio 放大）。
QPixmap GlyphPixmap(GlyphId id, int size, const QColor& color,
                    qreal device_pixel_ratio = 0.0);

// 把图标装到 QToolButton 上；toggled 时在 normal/active 两色之间切换
// （原型 .btn.on：accent 18% 底 + accent 文字 + 35% 边框）。
void BindGlyph(QToolButton* button, GlyphId id, const QColor& normal,
               const QColor& active, int size = 16);

// 把按钮接到既有 QAction 上（enabled/checked 单一真源）。
// **不用 QToolButton::setDefaultAction**：那个 API 会在 action 每次 changed() 时
// 把 icon 重设成 action->icon()（这些 action 没有 icon），自绘图标会消失，
// 按钮退化成"…"。这里改成单向同步 + clicked → trigger。
void BindToolAction(QToolButton* button, QAction* action, GlyphId id,
                    const QColor& normal, const QColor& active, int size = 16);

}  // namespace lapian::ui
