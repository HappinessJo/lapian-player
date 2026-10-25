#include "app/ui/Theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QStringList>
#include <QWidget>

#include "app/ui/Tokens.h"

namespace lapian::ui {
namespace {

QString FontFamilyOrFallback(const QStringList& candidates) {
  // 用 hasFamily() 而不是 families()：Qt 在 Windows 上完整枚举字体族要 200–400ms，
  // 而这段在冷启动 P1 路径上（主题在构造窗口之前应用）。
  for (const QString& candidate : candidates) {
    if (QFontDatabase::hasFamily(candidate)) {
      return candidate;
    }
  }
  return QString();
}

}  // namespace

QColor Color(const char* hex) { return QColor(QLatin1String(hex)); }

QFont UiFont(double pixel_size, int weight) {
  static const QString family =
      FontFamilyOrFallback({QStringLiteral("Microsoft YaHei UI"),
                            QStringLiteral("Microsoft YaHei"),
                            QStringLiteral("Segoe UI")});
  QFont font;
  if (!family.isEmpty()) {
    font.setFamily(family);
  }
  font.setPixelSize(std::max(6, static_cast<int>(pixel_size + 0.5)));
  font.setWeight(static_cast<QFont::Weight>(weight));
  return font;
}

QFont MonoFont(double pixel_size, int weight) {
  static const QString family =
      FontFamilyOrFallback({QStringLiteral("Consolas"),
                            QStringLiteral("Cascadia Mono"),
                            QStringLiteral("Cascadia Code")});
  QFont font;
  if (!family.isEmpty()) {
    font.setFamily(family);
  } else {
    font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  }
  font.setPixelSize(std::max(6, static_cast<int>(pixel_size + 0.5)));
  font.setWeight(static_cast<QFont::Weight>(weight));
  return font;
}

void SetStyledBackground(QWidget* widget) {
  if (widget != nullptr) {
    widget->setAttribute(Qt::WA_StyledBackground, true);
  }
}

void SetStyledBackground(std::initializer_list<QWidget*> widgets) {
  for (QWidget* widget : widgets) {
    SetStyledBackground(widget);
  }
}

QString GlobalStyleSheet() {
  // 单份 QSS：颜色/边框/内边距。**字号不写在这里**——字号由 Theme::UiFont /
  // MonoFont 用 setPixelSize 按令牌下发（规划 §4.7），避免 QSS 覆盖控件字体、
  // 也避免换档位时重新拼样式表。
  static const char* kTemplate = R"QSS(
QWidget { color: @text; background: transparent; }
QMainWindow, QDialog, QMessageBox { background: @surface; }
#v2PlayerShell, #v2Workspace { background: @surface; }
#v2TopBar { background: @surface2; border-bottom: 1px solid @border; }
#v2BannerSlot { background: @surface2; border-bottom: 1px solid @border; }
#v2Transport { background: @surface; border-top: 1px solid @border; }
#v2TimelinePanel { background: @surface; border-top: 1px solid @border; }
#v2TimelineHead { background: @surface; }
#v2Inspector { background: @surface2; border-left: 1px solid @border; }
#v2InspectorRail { background: @surface2; }
#v2StageEmpty, #v2StageError, #v2StageLoading { background: @surface; }
#v2EmptyTitle, #v2ErrorTitle { color: @text; }
#v2EmptyHint, #v2ErrorReason, #v2LoadingText { color: @text_meta; }
#v2FileLabel { color: @text; }
#v2SpecChip, #v2ModeChip { color: @text_meta; background: @surface2;
  border: 1px solid @border; border-radius: 3px; }
#infoTimecode { color: @text; }
#v2FrameNumber, #v2FrameTotal { color: @text_meta; }
#v2StatusMessage { color: @text_meta; }
QStatusBar { background: @surface2; border-top: 1px solid @border; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @text_meta; }
QToolButton { background: transparent; border: 1px solid transparent;
  border-radius: 3px; color: @text; }
QToolButton:hover { background: @surface3; border-color: @border; }
QToolButton:pressed { background: @border; }
QToolButton:checked { background: @accent_bg; border-color: @accent_border;
  color: @accent; }
QToolButton:disabled { color: @text_mute; }
QPushButton { background: transparent; border: 1px solid transparent;
  border-radius: 3px; color: @text; }
QPushButton:hover { background: @surface3; border-color: @border; }
QPushButton:pressed { background: @border; }
QPushButton:checked { background: @accent_bg; border-color: @accent_border;
  color: @accent; }
QPushButton:disabled { color: @text_mute; }
QPushButton:flat { border: none; }
QMenu { background: @surface2; border: 1px solid @border; color: @text; }
QMenu::item { padding: 4px 18px; }
QMenu::item:selected { background: @accent_bg; color: @text; }
QMenu::item:disabled { color: @text_mute; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }
QToolTip { background: @surface3; color: @text; border: 1px solid @border_strong;
  padding: 3px 6px; }
QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QSpinBox {
  background: @surface2; color: @text; border: 1px solid @border;
  border-radius: 3px; selection-background-color: @accent_border; }
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus,
QSpinBox:focus { border: 1px solid @accent; }
QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled,
QComboBox:disabled { color: @text_mute; }
QTreeWidget, QListWidget, QTableView, QTreeView, QListView {
  background: @surface2; color: @text; border: 1px solid @border;
  alternate-background-color: @surface3; }
QTreeWidget::item:selected, QListWidget::item:selected,
QTreeView::item:selected, QListView::item:selected {
  background: @accent_bg; color: @text; }
QHeaderView::section { background: @surface2; color: @text_dim;
  border: none; border-bottom: 1px solid @border; padding: 3px 6px; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: @border_strong; min-height: 24px;
  border-radius: 5px; }
QScrollBar::handle:vertical:hover { background: @text_mute; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
  background: transparent; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: @border_strong; min-width: 24px;
  border-radius: 5px; }
QScrollBar::handle:horizontal:hover { background: @text_mute; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
  background: transparent; }
QSlider::groove:horizontal { height: 4px; background: @border;
  border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal { background: @text; width: 10px; margin: -4px 0;
  border-radius: 5px; }
QSlider::handle:horizontal:disabled { background: @text_mute; }
QSlider::sub-page:horizontal:disabled { background: @border_strong; }
QSplitter::handle { background: @surface; }
QProgressBar { background: @surface2; border: 1px solid @border;
  border-radius: 2px; text-align: center; color: @text_dim; }
QProgressBar::chunk { background: @accent; }
QCheckBox, QRadioButton { color: @text; }
QGroupBox { border: 1px solid @border; border-radius: 3px; margin-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px;
  color: @text_dim; }
)QSS";
  QString qss = QString::fromUtf8(kTemplate);
  qss.replace(QLatin1String("@surface3"), QLatin1String(kColorSurface3));
  qss.replace(QLatin1String("@surface2"), QLatin1String(kColorSurface2));
  qss.replace(QLatin1String("@surface"), QLatin1String(kColorSurface));
  qss.replace(QLatin1String("@border_strong"), QLatin1String(kColorBorderStrong));
  qss.replace(QLatin1String("@border"), QLatin1String(kColorBorder));
  qss.replace(QLatin1String("@text_meta"), QLatin1String(kColorTextMeta));
  qss.replace(QLatin1String("@text_mute"), QLatin1String(kColorTextMute));
  qss.replace(QLatin1String("@text_dim"), QLatin1String(kColorTextDim));
  qss.replace(QLatin1String("@text"), QLatin1String(kColorText));
  qss.replace(QLatin1String("@accent_border"), QLatin1String(kColorAccentBorder));
  qss.replace(QLatin1String("@accent_bg"), QLatin1String(kColorAccentBg));
  qss.replace(QLatin1String("@accent"), QLatin1String(kColorAccent));
  return qss;
}

void ApplyTheme(QApplication& app, bool include_style_sheet) {
  QPalette palette;
  palette.setColor(QPalette::Window, Color(kColorSurface));
  palette.setColor(QPalette::WindowText, Color(kColorText));
  palette.setColor(QPalette::Base, Color(kColorSurface2));
  palette.setColor(QPalette::AlternateBase, Color(kColorSurface3));
  palette.setColor(QPalette::ToolTipBase, Color(kColorSurface3));
  palette.setColor(QPalette::ToolTipText, Color(kColorText));
  palette.setColor(QPalette::Text, Color(kColorText));
  palette.setColor(QPalette::Button, Color(kColorSurface2));
  palette.setColor(QPalette::ButtonText, Color(kColorText));
  palette.setColor(QPalette::BrightText, Color(kColorErr));
  palette.setColor(QPalette::Link, Color(kColorAccent));
  palette.setColor(QPalette::Highlight, Color(kColorAccentBg));
  palette.setColor(QPalette::HighlightedText, Color(kColorText));
  palette.setColor(QPalette::PlaceholderText, Color(kColorTextMute));
  // 禁用态整体压暗；text-mute 只用于这里与装饰（M2）。
  palette.setColor(QPalette::Disabled, QPalette::WindowText,
                   Color(kColorTextMute));
  palette.setColor(QPalette::Disabled, QPalette::Text, Color(kColorTextMute));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                   Color(kColorTextMute));
  palette.setColor(QPalette::Disabled, QPalette::Highlight,
                   Color(kColorBorder));
  app.setPalette(palette);

  app.setFont(UiFont(12.0));
  if (include_style_sheet) {
    app.setStyleSheet(GlobalStyleSheet());
  }
}

}  // namespace lapian::ui
