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
  // 单份 QSS：颜色/边框/圆角/内边距。**字号不写在这里**——字号由 Theme::UiFont /
  // MonoFont 用 setPixelSize 按令牌下发（规划 §4.7），避免 QSS 覆盖控件字体、
  // 也避免换档位时重新拼样式表。
  //
  // 容器层级照 docs/ui-v2-proto/plan.html：应用底色 #0e0e10 更暗，
  // 画面/时间轴/检查器是"面板卡片"（#17171a + 1px #2b2b33 + 4px 圆角）。
  static const char* kTemplate = R"QSS(
QWidget { color: @text; background: transparent; }
QMainWindow, QDialog, QMessageBox { background: @appbg; }
#v2PlayerShell, #v2Workspace { background: @appbg; }
#v2TopBar { background: @surface; border-bottom: 1px solid @border; }
#v2BannerSlot { background: @surface; border-bottom: 1px solid @border; }
#v2BannerSlot[kind="warn"] { background: #1b1a13; border-bottom-color: @warn_border; }
#v2BannerSlot[kind="error"] { background: @err_bg; border-bottom-color: @err_border; }
#v2BannerSlot[kind="info"] { background: @surface2; border-bottom-color: @border; }
#infoBanner { color: @warn; }
#v2BannerSlot[kind="error"] #infoBanner { color: @err; }
#v2BannerSlot[kind="info"] #infoBanner { color: @text_dim; }
#infoBannerClose { color: @text_meta; background: transparent; border: none;
  padding: 0 4px; }
#infoBannerClose:hover { color: @text; background: @surface3; border-radius: 3px; }
#v2TimelinePanel, #v2Inspector {
  background: @surface; border: 1px solid @border; border-radius: 4px; }
#v2VideoCard { background: @surface; border: 1px solid transparent;
  border-radius: 4px; }
#v2Stage, #v2StageVideo { background: #000000; }
#v2Transport { background: transparent; border-top: 1px solid @border; }
#v2TimelineHead { background: @surface2; border-bottom: 1px solid @border;
  border-top-left-radius: 4px; border-top-right-radius: 4px; }
#v2TimelineMeta, #v2WorkMeta, #v2ViewMeta { color: @text_dim; }
#v2MetaSlash { color: @text_mute; }
#speedInfo { color: @text_dim; }
#v2TransportSep { background: @border; }
#v2OpenButton, #v2OnlineButton { border-radius: 3px; }
#v2OpenButton:hover, #v2OnlineButton:hover { background: @surface3; }
#v2OpenButton:disabled, #v2OnlineButton:disabled { color: @text_mute; }
#v2PlayButton { background: @accent; border-radius: 3px; border: none; }
#v2PlayButton:hover { background: #6d9bf5; }
#v2PlayButton:pressed { background: #4d7de0; }
#v2PlayButton:disabled { background: @border; }
#v2ZoomIn, #v2ZoomOut, #v2ZoomFit { color: @text_dim; }
#v2InspectorRail { background: transparent; color: @text_meta; }
#v2InspectorHead { background: @surface2; border-bottom: 1px solid @border;
  border-top-left-radius: 4px; border-top-right-radius: 4px; }
#v2InspectorFooter { background: @surface2; border-top: 1px solid @border;
  border-bottom-left-radius: 4px; border-bottom-right-radius: 4px; }
#v2StoreText { color: @text_meta; }
#v2StoreText[warning="true"] { color: @warn; }
#v2MarkBadge, #v2ShotBadge { color: @text_mute; }
#v2InspectorAdd, #v2InspectorExpand, #v2InspectorCollapse { color: @text_dim; }
#v2ShotGrid { background: transparent; }
#v2ShotEmpty { color: @text_meta; }
#v2ShotNote { color: @text_mute; }
#v2InfoTable { background: transparent; border: none; }
#v2InfoTable::item { border-bottom: 1px dashed #232329; padding: 2px 8px; }
#v2StageEmpty, #v2StageError, #v2StageLoading { background: #0c0c0e; }
#v2EmptyTitle, #v2ErrorTitle { color: @text; }
#v2EmptyHint, #v2ErrorReason, #v2LoadingText, #v2EmptyKbd { color: @text_meta; }
#v2FileLabel { color: @text; }
#v2SpecChip, #v2ModeChip { color: @text_dim; background: @chipbg;
  border: 1px solid @border; border-radius: 10px; }
#v2SpecChip QLabel, #v2ModeChip { color: @text_dim; }
/* P3：清晰度入口是 chip 按钮（surface3 底 + 1px 边框 + 胶囊圆角），
   与模式 chip 同高；hover/pressed/disabled 三态。 */
#onlineQualityButton {
  color: @text_dim; background: @chipbg; border: 1px solid @border;
  border-radius: 10px; padding: 0 7px; }
#onlineQualityButton:hover { border-color: @accent; color: @text; }
#onlineQualityButton:pressed { background: @border; color: @text; }
#onlineQualityButton:disabled { color: @text_mute; background: @surface2;
  border-color: @border; }
#v2ModeChip[kind="ok"] { color: @ok; background: @ok_bg; border-color: @ok_border; }
#v2ModeChip[kind="cache"] { color: @cache; background: @cache_bg;
  border-color: @cache_border; }
#v2ModeChip[kind="warn"] { color: @warn; background: @warn_bg;
  border-color: @warn_border; }
#v2ModeChip[kind="error"] { color: @err; background: @err_bg;
  border-color: @err_border; }
#onlineFpsBadge { color: @text_dim; background: @surface3;
  border: 1px solid @border; border-radius: 3px; padding: 0 5px; }
#onlineFpsBadge[kind="warn"] { color: @warn; background: @warn_bg;
  border-color: @warn_border; }
#v2KbdBadge { color: @text_mute; background: @kbdbg; border: 1px solid @kbdborder;
  border-radius: 2px; }
#v2Timecode { color: @text; }
#v2FrameNumber { color: @text_dim; }
#v2FrameTotal { color: @text_mute; }
#v2ReadoutEdit { padding: 0 4px; }
#v2SpeedButton { color: @text_dim; }
#v2BufferSlot { background: transparent; }
#v2BufferText { color: @text_meta; }
#v2StatusMessage { color: @text_dim; }
#noticeInfo { color: @warn; }
#cacheInfo { color: @text_dim; }
#v2StatusSep { background: @border; }
QStatusBar { background: @surface; border-top: 1px solid @border; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @text_dim; }
QToolButton { background: transparent; border: 1px solid transparent;
  border-radius: 3px; color: @text_dim; }
QToolButton:hover { background: @surface3; color: @text; }
QToolButton:pressed { background: @border; }
QToolButton:checked { background: @accent_on_bg; border-color: @accent_on_border;
  color: @accent; }
QToolButton:disabled { color: @text_mute; }
QPushButton { background: transparent; border: 1px solid transparent;
  border-radius: 3px; color: @text_dim; }
QPushButton:hover { background: @surface3; color: @text; }
QPushButton:pressed { background: @border; }
QPushButton:checked { background: @accent_on_bg; border-color: @accent_on_border;
  color: @accent; }
QPushButton:disabled { color: @text_mute; }
QPushButton:flat { border: none; }
#v2EmptyOpen, #v2EmptyOnline, #v2ErrorRetry, #v2ErrorQuality, #v2ErrorOpenLocal,
#v2LoadingCancel { border: 1px solid @border; background: @surface2;
  border-radius: 3px; padding: 3px 12px; color: @text; }
#v2EmptyOpen:hover, #v2EmptyOnline:hover, #v2ErrorRetry:hover,
#v2ErrorQuality:hover, #v2ErrorOpenLocal:hover, #v2LoadingCancel:hover {
  border-color: @accent; color: @accent; }
QMenu { background: @surface2; border: 1px solid @border; color: @text; }
QMenu::item { padding: 4px 18px; }
QMenu::item:selected { background: @accent_on_bg; color: @text; }
QMenu::item:disabled { color: @text_mute; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }
/* 对话框：CacheSettingsDialog / OnlineResolveDialog / 帮助 QMessageBox 全部走
   这一份 QSS + 令牌（3D 收口；不再有 per-widget setStyleSheet）。 */
QDialog { background: @appbg; }
QDialog QLabel, QMessageBox QLabel { color: @text_dim; }
QGroupBox { color: @text_dim; }
#onlinePrivacyNotice { color: @text_meta; background: @surface2;
  border: 1px solid @border; border-radius: 3px; padding: 6px; }
#onlineStatus { color: @text_meta; }
#onlineQualityHint { color: @warn; }
#onlineError { color: @err; }
#onlineCredentialHint, #onlineQrHint, #onlineQrStatus { color: @text_meta; }
#onlineAuthStatus { color: @text_meta; }
#onlineAuthStatus[state="ok"] { color: @ok; }
#onlineAuthStatus[state="warn"] { color: @warn; }
#onlineAuthStatus[state="error"] { color: @err; }
#onlineQrImage { background: #ffffff; border: 1px solid @border_strong; }
#cacheRootState { color: @text_meta; }
#cacheRootState[state="ok"] { color: @ok; }
#cacheRootState[state="warn"] { color: @warn; }
#cacheRootState[state="error"] { color: @err; }
#cacheStatusLabel { color: @text_meta; }
#cacheStatusLabel[state="ok"] { color: @ok; }
#cacheStatusLabel[state="error"] { color: @err; }
#v2HelpDialog QLabel { color: @text; }
QToolTip { background: @surface3; color: @text; border: 1px solid @border_strong;
  padding: 3px 6px; }
QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QSpinBox {
  background: @surface2; color: @text; border: 1px solid @border;
  border-radius: 3px; selection-background-color: @accent_on_border; }
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus,
QSpinBox:focus { border: 1px solid @accent; }
QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled,
QComboBox:disabled { color: @text_mute; }
QTreeWidget, QListWidget, QTableView, QTreeView, QListView {
  background: @surface; color: @text; border: 1px solid @border;
  alternate-background-color: @surface2; }
QTreeWidget::item:selected, QListWidget::item:selected,
QTreeView::item:selected, QListView::item:selected {
  background: @accent_on_bg; color: @text; }
QHeaderView::section { background: @surface2; color: @text_dim;
  border: none; border-bottom: 1px solid @border; padding: 3px 6px; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: @border_strong; min-height: 24px;
  border-radius: 6px; border: 2px solid @surface; }
QScrollBar::handle:vertical:hover { background: @text_mute; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
  background: transparent; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: @border_strong; min-width: 24px;
  border-radius: 6px; border: 2px solid @surface; }
QScrollBar::handle:horizontal:hover { background: @text_mute; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
  background: transparent; }
QSlider::groove:horizontal { height: 3px; background: @border;
  border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal { background: @text_dim; width: 11px; margin: -5px 0;
  border-radius: 5px; }
QSlider::handle:horizontal:hover { background: @text; }
QSlider::handle:horizontal:disabled { background: @text_mute; }
QSlider::sub-page:horizontal:disabled { background: @border_strong; }
QSplitter::handle { background: @appbg; }
QProgressBar { background: @surface2; border: 1px solid @border;
  border-radius: 2px; text-align: center; color: @text_dim; }
QProgressBar::chunk { background: @accent; border-radius: 2px; }
QCheckBox, QRadioButton { color: @text; }
QGroupBox { border: 1px solid @border; border-radius: 4px; margin-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px;
  color: @text_dim; }
)QSS";
  QString qss = QString::fromUtf8(kTemplate);
  struct Pair {
    const char* token;
    const char* value;
  };
  // 顺序很重要：长 token 必须排在它的前缀之前（@surface3 先于 @surface，
  // @ok_bg 先于 @ok，@accent_on_bg 先于 @accent），否则会被前缀吃掉。
  const Pair pairs[] = {
      {"@accent_on_border", kColorAccentOnBorder},
      {"@accent_on_bg", kColorAccentOnBg},
      {"@ok_border", kColorOkBorder},
      {"@ok_bg", kColorOkBg},
      {"@warn_border", kColorWarnBorder},
      {"@warn_bg", kColorWarnBg},
      {"@err_border", kColorErrBorder},
      {"@err_bg", kColorErrBg},
      {"@cache_border", kColorCacheBorder},
      {"@cache_bg", kColorCacheBg},
      {"@kbdborder", kColorKbdBorder},
      {"@kbdbg", kColorKbdBg},
      {"@appbg", kColorAppBg},
      {"@chipbg", kColorChipBg},
      {"@surface3", kColorSurface3},
      {"@surface2", kColorSurface2},
      {"@surface", kColorSurface},
      {"@border_strong", kColorBorderStrong},
      {"@border", kColorBorder},
      {"@text_meta", kColorTextMeta},
      {"@text_mute", kColorTextMute},
      {"@text_dim", kColorTextDim},
      {"@text", kColorText},
      {"@accent", kColorAccent},
      {"@ok", kColorOk},
      {"@warn", kColorWarn},
      {"@err", kColorErr},
      {"@cache", kColorCache},
  };
  for (const Pair& pair : pairs) {
    qss.replace(QLatin1String(pair.token), QLatin1String(pair.value));
  }
  return qss;
}

void ApplyTheme(QApplication& app, bool include_style_sheet) {
  QPalette palette;
  palette.setColor(QPalette::Window, Color(kColorAppBg));
  palette.setColor(QPalette::WindowText, Color(kColorText));
  palette.setColor(QPalette::Base, Color(kColorSurface));
  palette.setColor(QPalette::AlternateBase, Color(kColorSurface2));
  palette.setColor(QPalette::ToolTipBase, Color(kColorSurface3));
  palette.setColor(QPalette::ToolTipText, Color(kColorText));
  palette.setColor(QPalette::Text, Color(kColorText));
  palette.setColor(QPalette::Button, Color(kColorSurface));
  palette.setColor(QPalette::ButtonText, Color(kColorText));
  palette.setColor(QPalette::BrightText, Color(kColorErr));
  palette.setColor(QPalette::Link, Color(kColorAccent));
  palette.setColor(QPalette::Highlight, Color(kColorAccentOnBorder));
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
