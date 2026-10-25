#pragma once

#include <QColor>
#include <QFont>
#include <QString>

#include <initializer_list>

class QApplication;
class QWidget;

namespace lapian::ui {

// 令牌色值 → QColor（#rrggbb）。
QColor Color(const char* hex);

// 界面字体：Microsoft YaHei UI → 系统 UI 字体回退（规划 §4.2，不打包字体）。
QFont UiFont(double pixel_size, int weight = QFont::Normal);
// 等宽数字字体：Consolas → Cascadia Mono → QFontDatabase::FixedFont。
QFont MonoFont(double pixel_size, int weight = QFont::Normal);

// 铁律（规划 §7.2）：任何自绘容器必须显式 WA_StyledBackground，否则
// 全局 QSS 的 background 不会画到它身上，等价于重演 InfoBar 的 1.28:1。
void SetStyledBackground(QWidget* widget);
void SetStyledBackground(std::initializer_list<QWidget*> widgets);

// 单份全局 QSS（只拼一次；不做 per-widget setStyleSheet）。
QString GlobalStyleSheet();

// 只调一次：QPalette + 应用字体 + 一份全局 QSS。
// **只在 UI v2 打开时调用**，旧壳保持现有外观（回滚要求）。
void ApplyTheme(QApplication& app, bool include_style_sheet = true);

}  // namespace lapian::ui
