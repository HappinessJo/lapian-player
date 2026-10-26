#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <string>
#include <vector>

#include "core/MarkStore.h"

class QAction;
class QEvent;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPoint;
class QTreeWidget;
class QTreeWidgetItem;

// T5.2 标记侧栏：帧号/标签/备注摘要、点击跳转、删除入口、实际存储位置与警告。
// T5.3 备注编辑：选中标记后可在底部编辑标签与备注，失焦/回车提交。
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

  // T5.3：把编辑框里未提交的文本发给 markTextEdited；返回是否真的提交了修改。
  // 切换选中、切换文件、关窗之前必须调用。
  bool FlushEditor();
  bool HasSelection() const;

  // T5.3 测试入口：等价于用户在编辑框中键入（不提交）。
  void SetEditorTextForTest(const QString& label, const QString& note);
  QString EditorLabelForTest() const;
  QString EditorNoteForTest() const;
  bool FocusNoteEditorForTest();

  // 只更新一条标记的列表文本，不重建列表、不改变选中与滚动位置。
  void UpdateMarkText(const QString& id, const QString& label, const QString& note);

  // UI v2 3C：嵌进 InspectorPanel 时收掉自己的表头/存储位置/提示（这些在新壳里
  // 由检查器头、tab 计数与页脚承担）。默认 false —— tests/mark_panel_test.cpp
  // 仍独立构造 MarkPanel，行为与既有一致。只改可见性与外边距，不销毁任何对象。
  void SetEmbeddedMode(bool embedded);
  bool embedded() const { return embedded_; }

  int MarkCount() const { return static_cast<int>(marks_.size()); }
  int WarningCount() const;
  QString StorageSummary() const;

 signals:
  void markActivated(const QString& id, qint64 frame);
  void deleteRequested(const QString& id);
  // T5.3：标签/备注已提交（失焦、Enter、Ctrl+Enter、切换选中或显式 flush）。
  void markTextEdited(const QString& id, const QString& label, const QString& note);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void Rebuild();
  void ActivateItem(QTreeWidgetItem* item);
  void DeleteCurrent();
  void ShowContextMenu(const QPoint& pos);
  QTreeWidgetItem* ItemForId(const QString& id) const;
  void UpdateItemText(QTreeWidgetItem* item, const lapian::core::Mark& mark);

  void HandleCurrentChanged(QTreeWidgetItem* current);
  void LoadEditorForCurrent();
  void ClearEditor();
  bool CommitEditor();
  const lapian::core::Mark* FindMark(const QString& id) const;
  lapian::core::Mark* FindMarkMutable(const QString& id);
  bool IsEditorFocused() const;

  QLabel* title_label_ = nullptr;
  QLabel* count_label_ = nullptr;
  QLabel* storage_label_ = nullptr;
  QLabel* warning_label_ = nullptr;
  QLabel* empty_label_ = nullptr;
  QLabel* hint_label_ = nullptr;
  QWidget* title_row_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  QAction* delete_action_ = nullptr;

  QWidget* editor_box_ = nullptr;
  QLabel* editor_title_ = nullptr;
  QLineEdit* label_edit_ = nullptr;
  QPlainTextEdit* note_edit_ = nullptr;

  std::vector<lapian::core::Mark> marks_;
  int warning_count_ = 0;
  QString editing_id_;
  bool committing_ = false;
  bool embedded_ = false;
};
