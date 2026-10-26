#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

#include <cstdint>

#include "app/ui/Tokens.h"

class QAbstractButton;
class QAction;
class QGridLayout;
class QLabel;
class QResizeEvent;
class QScrollArea;
class QStackedWidget;
class QTableWidget;
class QToolButton;
class QVBoxLayout;
class MarkPanel;

namespace lapian::ui {

// UI v2 3C 检查器（规划 §2.1/§2.2/§7.1）：
//   头 26（标记/截图/信息三个 tab + 计数徽标 + 加标记 + 折叠）
//   stack 三页：page0 = 既有 MarkPanel 原对象 / page1 = 截图网格 / page2 = 信息表
//   页脚 = 存储位置（全应用唯一常显处）
//   折叠 44 = 竖排“标记 N / 截图 M”+ 展开按钮；展开 304（220–560）由 PlayerShell 夹取。
//
// 只做容器与 tab 切换，不含业务：计数/截图条目/信息行/存储路径都由 MainWindow 推。
// 全 NoFocus（tab 与按钮），只有列表与编辑框可聚焦 —— 键位矩阵依赖这一点。
class InspectorPanel : public QWidget {
  Q_OBJECT

 public:
  enum class Tab {
    kMarks = 0,
    kShots = 1,
    kInfo = 2,
  };

  struct ShotItem {
    QString path;
    qint64 frame = -1;
    QString timecode;  // HH:MM:SS:FF；空表示不显示
  };

  struct InfoRow {
    QString label;
    QString value;
  };

  // 截图网格首屏上限（规划 3C）：超过时只铺最近的 60 张，徽标仍显示真实总数。
  static constexpr int kMaxShotTiles = 60;

  explicit InspectorPanel(QWidget* parent = nullptr);

  // 把既有 MarkPanel 原对象收进 page0（不重建、不改 objectName）。
  void AdoptMarkPanel(MarkPanel* panel);
  MarkPanel* markPanel() const { return mark_panel_; }

  // 折叠按钮绑既有“添加标记”QAction（M5 第 13 行的第二入口）。
  void BindAddAction(QAction* action);

  void ApplyTokens(const lapian::ui::Tokens& tokens);

  // 折叠只改可见性：MarkPanel 与数据都不重建（--mark-test 断言依赖这一点）。
  void SetCollapsed(bool collapsed);
  bool collapsed() const { return collapsed_; }

  // 切 tab 前先 mark_panel_->FlushEditor()（T5.3 契约）。
  void SetTab(Tab tab);
  Tab tab() const { return tab_; }

  void SetMarkCount(int count);
  void SetShotCount(int count);
  void SetShotItems(const QVector<ShotItem>& items);
  void SetInfoRows(const QVector<InfoRow>& rows);
  void SetStorageText(const QString& text, bool warning);

  int markCount() const { return mark_count_; }
  int shotCount() const { return shot_total_count_; }

  // 测试/诊断（只读、无副作用）：collapsed/tab/stack 页/计数/宽度/轨道文本。
  QString StateForTest() const;

 signals:
  void collapseRequested();
  void expandRequested();
  void shotActivated(qint64 frame, const QString& path);
  void tabChanged(int tab);

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void UpdateCounts();
  void UpdateRailText();
  void UpdateStoreElide();
  void UpdateShotGeometry();
  void RebuildShotTiles();
  void SetCurrentPage();
  QWidget* BuildHead();
  QWidget* BuildShotsPage();
  QWidget* BuildInfoPage();
  QWidget* BuildFooter();

  lapian::ui::Tokens tokens_ = lapian::ui::TokensFor(lapian::ui::Density::kRegular);

  // 折叠 rail
  QWidget* rail_ = nullptr;
  QLabel* rail_text_ = nullptr;
  QToolButton* expand_button_ = nullptr;

  // 展开态：头 + stack + 页脚
  QWidget* body_ = nullptr;
  QWidget* head_ = nullptr;
  QAbstractButton* tab_marks_ = nullptr;
  QAbstractButton* tab_shots_ = nullptr;
  QAbstractButton* tab_info_ = nullptr;
  QLabel* mark_badge_ = nullptr;
  QLabel* shot_badge_ = nullptr;
  QToolButton* add_button_ = nullptr;
  QToolButton* collapse_button_ = nullptr;

  QStackedWidget* stack_ = nullptr;
  QWidget* marks_page_ = nullptr;
  QWidget* shots_page_ = nullptr;
  QWidget* info_page_ = nullptr;

  QScrollArea* shot_scroll_ = nullptr;
  QWidget* shot_grid_ = nullptr;
  QGridLayout* shot_layout_ = nullptr;
  QLabel* shot_empty_ = nullptr;
  QLabel* shot_note_ = nullptr;
  QVector<QWidget*> shot_tiles_;

  QTableWidget* info_table_ = nullptr;

  QWidget* footer_ = nullptr;
  QLabel* store_text_ = nullptr;
  QString store_full_text_;
  bool store_warning_ = false;

  MarkPanel* mark_panel_ = nullptr;
  Tab tab_ = Tab::kMarks;
  bool collapsed_ = false;
  int mark_count_ = 0;
  int shot_total_count_ = 0;
  QVector<ShotItem> shot_items_;
  QVector<InfoRow> info_rows_;
  bool shot_tiles_built_ = false;
};

}  // namespace lapian::ui
