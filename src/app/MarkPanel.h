#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <string>
#include <vector>

#include "core/MarkStore.h"

class QAction;
class QLabel;
class QPoint;
class QTreeWidget;
class QTreeWidgetItem;

// T5.2 标记侧栏：帧号/标签/备注摘要、点击跳转、删除入口、实际存储位置与警告。
// 只做视图与用户意图；确认对话框、跳转、保存由 MainWindow 负责。
class MarkPanel : public QWidget {
  Q_OBJECT

 public:
  explicit MarkPanel(QWidget* parent = nullptr);

  void Clear();
  void SetMarks(const std::vector<lapian::core::Mark>& marks);
  // path 为空表示无文件；using_fallback=true 时明确显示“%APPDATA% 回退”。
  void SetStorage(const QString& path, bool using_fallback);
  void SetWarnings(const std::vector<lapian::core::MarkWarning>& warnings);

  void SelectMark(const std::string& id);
  // 删除后选中最接近原帧的标记（优先 >= frame，否则最后一个）。
  void SelectNearFrame(std::int64_t frame);
  QString SelectedMarkId() const;

  int MarkCount() const { return static_cast<int>(marks_.size()); }
  int WarningCount() const;
  QString StorageSummary() const;

 signals:
  void markActivated(const QString& id, qint64 frame);
  void deleteRequested(const QString& id);

 private:
  void Rebuild();
  void ActivateItem(QTreeWidgetItem* item);
  void DeleteCurrent();
  void ShowContextMenu(const QPoint& pos);
  QTreeWidgetItem* ItemForId(const QString& id) const;

  QLabel* title_label_ = nullptr;
  QLabel* count_label_ = nullptr;
  QLabel* storage_label_ = nullptr;
  QLabel* warning_label_ = nullptr;
  QLabel* empty_label_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  QAction* delete_action_ = nullptr;

  std::vector<lapian::core::Mark> marks_;
  int warning_count_ = 0;
};
