#include "app/MarkPanel.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPoint>
#include <QSignalBlocker>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {

QString StorageSummaryText(const QString& path, bool using_fallback) {
  if (path.isEmpty()) {
    return QStringLiteral("存储：—");
  }
  const QString base = QFileInfo(path).fileName();
  const QString where =
      using_fallback ? QStringLiteral("%APPDATA% 回退") : QStringLiteral("sidecar");
  const QString suffix =
      QFileInfo(path).isFile() ? QString() : QStringLiteral("（未写入）");
  return QStringLiteral("存储：%1 · %2%3").arg(where, base, suffix);
}

QString NoteSummary(const std::string& note) {
  QString text = QString::fromStdString(note);
  text.replace(QLatin1Char('\r'), QLatin1Char(' '));
  text.replace(QLatin1Char('\n'), QLatin1Char(' '));
  text = text.simplified();
  constexpr int kMaxChars = 72;
  if (text.size() > kMaxChars) {
    text = text.left(kMaxChars - 1) + QStringLiteral("…");
  }
  return text;
}

QString LabelText(const lapian::core::Mark& mark) {
  const QString label = QString::fromStdString(mark.label);
  if (!label.isEmpty()) {
    return label;
  }
  return QStringLiteral("帧 %1").arg(static_cast<qlonglong>(mark.frame));
}

}  // namespace

MarkPanel::MarkPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("markPanel"));
  // 自定义 QWidget 子类必须显式让样式表绘制背景，否则列表以外的区域会露出窗口底色。
  setAttribute(Qt::WA_StyledBackground, true);
  setMinimumWidth(240);
  setMaximumWidth(420);
  setStyleSheet(QStringLiteral(
      "#markPanel { background:#18181b; border-left:1px solid #3a3a40; }"
      "#markPanel QLabel { color:#d8d8dc; }"
      "#markTitle { color:#f2f2f4; font-weight:600; }"
      "#markCount { color:#a8a8b2; }"
      "#markStorage { color:#a8a8b2; }"
      "#markHint { color:#777781; }"
      "#markWarning { color:#ffd37a; background:#3a2d00; border:1px solid #6b5500; "
      "border-radius:3px; padding:3px 5px; }"
      "#markEmpty { color:#777781; }"
      "QTreeWidget { background:#18181b; border:0; color:#d8d8dc; "
      "alternate-background-color:#1c1c20; outline:0; }"
      "QTreeWidget::item { padding:3px 2px; }"
      "QTreeWidget::item:selected { background:#2d3242; color:#ffffff; }"
      "QHeaderView::section { background:#202024; color:#a8a8b2; border:0; "
      "border-bottom:1px solid #3a3a40; padding:3px 4px; }"
      "#markEditor { background:#141416; border-top:1px solid #3a3a40; }"
      "#markEditorTitle { color:#a8a8b2; }"
      "#markEditorHint { color:#777781; }"
      "#markLabelEdit, #markNoteEdit { background:#101012; color:#e8e8ec; "
      "border:1px solid #3a3a40; border-radius:3px; padding:3px 4px; "
      "selection-background-color:#3d4a6b; }"
      "#markLabelEdit:focus, #markNoteEdit:focus { border:1px solid #5b7bd5; }"
      // 3C：嵌进检查器时去掉自己的边框/底色，让父面板卡片透出来。
      "#markPanel[embedded=\"true\"] { background:transparent; border:0; }"
      "#markPanel[embedded=\"true\"] QTreeWidget { background:transparent; }"));

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(10, 8, 10, 8);
  root->setSpacing(6);

  title_row_ = new QWidget(this);
  auto* title_row = new QVBoxLayout(title_row_);
  title_row->setContentsMargins(0, 0, 0, 0);
  title_row->setSpacing(1);
  auto* title_line = new QHBoxLayout();
  title_line->setSpacing(6);
  title_label_ = new QLabel(QStringLiteral("标记"), title_row_);
  title_label_->setObjectName(QStringLiteral("markTitle"));
  QFont title_font = title_label_->font();
  title_font.setPointSizeF(title_font.pointSizeF() + 1.0);
  title_label_->setFont(title_font);
  count_label_ = new QLabel(QStringLiteral("0 个"), title_row_);
  count_label_->setObjectName(QStringLiteral("markCount"));
  title_line->addWidget(title_label_);
  title_line->addStretch(1);
  title_line->addWidget(count_label_);
  title_row->addLayout(title_line);

  storage_label_ = new QLabel(QStringLiteral("存储：—"), title_row_);
  storage_label_->setObjectName(QStringLiteral("markStorage"));
  storage_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  storage_label_->setWordWrap(true);
  title_row->addWidget(storage_label_);
  root->addWidget(title_row_);

  warning_label_ = new QLabel(this);
  warning_label_->setObjectName(QStringLiteral("markWarning"));
  warning_label_->setWordWrap(true);
  warning_label_->hide();
  root->addWidget(warning_label_);

  hint_label_ = new QLabel(QStringLiteral("Ctrl+M 添加 · Delete 删除选中"), this);
  hint_label_->setObjectName(QStringLiteral("markHint"));
  root->addWidget(hint_label_);

  tree_ = new QTreeWidget(this);
  tree_->setObjectName(QStringLiteral("markTree"));
  tree_->setColumnCount(3);
  tree_->setHeaderLabels({QStringLiteral("帧"), QStringLiteral("标签"),
                          QStringLiteral("备注")});
  tree_->setRootIsDecorated(false);
  tree_->setUniformRowHeights(true);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  tree_->setAllColumnsShowFocus(true);
  tree_->header()->setStretchLastSection(true);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Fixed);
  tree_->header()->resizeSection(0, 58);
  tree_->header()->setSectionResizeMode(1, QHeaderView::Interactive);
  tree_->header()->resizeSection(1, 96);
  root->addWidget(tree_, 1);

  empty_label_ = new QLabel(QStringLiteral("还没有标记\nCtrl+M 在当前帧添加"), this);
  empty_label_->setObjectName(QStringLiteral("markEmpty"));
  empty_label_->setAlignment(Qt::AlignCenter);
  empty_label_->setWordWrap(true);
  root->addWidget(empty_label_, 1);

  // T5.3：选中标记后在底部编辑标签/备注；默认隐藏，避免没有选中时占位。
  editor_box_ = new QWidget(this);
  editor_box_->setObjectName(QStringLiteral("markEditor"));
  editor_box_->setAttribute(Qt::WA_StyledBackground, true);
  auto* editor_layout = new QVBoxLayout(editor_box_);
  editor_layout->setContentsMargins(0, 6, 0, 0);
  editor_layout->setSpacing(4);
  editor_title_ = new QLabel(QStringLiteral("编辑"), editor_box_);
  editor_title_->setObjectName(QStringLiteral("markEditorTitle"));
  editor_layout->addWidget(editor_title_);

  label_edit_ = new QLineEdit(editor_box_);
  label_edit_->setObjectName(QStringLiteral("markLabelEdit"));
  label_edit_->setPlaceholderText(QStringLiteral("标签（Enter 保存）"));
  label_edit_->setClearButtonEnabled(true);
  editor_layout->addWidget(label_edit_);

  note_edit_ = new QPlainTextEdit(editor_box_);
  note_edit_->setObjectName(QStringLiteral("markNoteEdit"));
  note_edit_->setPlaceholderText(QStringLiteral("备注（Ctrl+Enter 保存，Enter 换行）"));
  note_edit_->setTabChangesFocus(true);
  note_edit_->setFixedHeight(88);
  editor_layout->addWidget(note_edit_);

  auto* editor_hint =
      new QLabel(QStringLiteral("Enter 保存标签 · Ctrl+Enter 保存备注"), editor_box_);
  editor_hint->setObjectName(QStringLiteral("markEditorHint"));
  editor_layout->addWidget(editor_hint);

  editor_box_->hide();
  root->addWidget(editor_box_);

  label_edit_->installEventFilter(this);
  note_edit_->installEventFilter(this);
  connect(label_edit_, &QLineEdit::returnPressed, this, [this]() { CommitEditor(); });

  delete_action_ = new QAction(QStringLiteral("删除标记"), this);
  delete_action_->setObjectName(QStringLiteral("deleteMarkAction"));
  delete_action_->setShortcut(QKeySequence(Qt::Key_Delete));
  delete_action_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  connect(delete_action_, &QAction::triggered, this, &MarkPanel::DeleteCurrent);
  addAction(delete_action_);

  connect(tree_, &QTreeWidget::itemClicked, this,
          [this](QTreeWidgetItem* item, int) { ActivateItem(item); });
  connect(tree_, &QTreeWidget::itemActivated, this,
          [this](QTreeWidgetItem* item, int) { ActivateItem(item); });
  connect(tree_, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
            HandleCurrentChanged(current);
          });
  connect(tree_, &QWidget::customContextMenuRequested, this,
          &MarkPanel::ShowContextMenu);

  Clear();
}

void MarkPanel::Clear() {
  SetMarks({});
  SetStorage(QString(), false);
  SetWarnings({});
}

void MarkPanel::SetMarks(const std::vector<lapian::core::Mark>& marks) {
  // 先提交旧编辑，避免整表刷新把用户刚输入的文本丢掉。
  FlushEditor();
  marks_ = marks;
  Rebuild();
  count_label_->setText(QStringLiteral("%1 个").arg(marks_.size()));
  const bool empty = marks_.empty();
  tree_->setVisible(!empty);
  empty_label_->setVisible(empty);
  if (empty) {
    ClearEditor();
    return;
  }
  LoadEditorForCurrent();
}

void MarkPanel::SetStorage(const QString& path, bool using_fallback) {
  storage_label_->setText(StorageSummaryText(path, using_fallback));
  if (path.isEmpty()) {
    storage_label_->setToolTip(QStringLiteral("尚未打开文件"));
    storage_label_->setStyleSheet(QString());
    return;
  }
  const QString detail =
      using_fallback
          ? QStringLiteral("%1\n源视频目录不可写，已回退到 %%APPDATA%%\\拉片软件\\marks。")
                .arg(QDir::toNativeSeparators(path))
          : QStringLiteral("%1\n与视频同目录的 sidecar 文件。")
                .arg(QDir::toNativeSeparators(path));
  storage_label_->setToolTip(detail);
  storage_label_->setStyleSheet(using_fallback
                                    ? QStringLiteral("color:#ffd37a;")
                                    : QStringLiteral("color:#a8a8b2;"));
}

void MarkPanel::SetWarnings(const std::vector<lapian::core::MarkWarning>& warnings) {
  warning_count_ = static_cast<int>(warnings.size());
  if (warnings.empty()) {
    warning_label_->clear();
    warning_label_->setToolTip(QString());
    warning_label_->hide();
    return;
  }

  QStringList lines;
  lines.reserve(warning_count_);
  for (const lapian::core::MarkWarning& warning : warnings) {
    QString line = QString::fromStdString(warning.message);
    if (!warning.path.empty()) {
      line += QStringLiteral("\n  ") + QDir::toNativeSeparators(
                                          QString::fromStdString(warning.path));
    }
    lines.push_back(line);
  }
  if (warning_count_ == 1) {
    warning_label_->setText(QStringLiteral("⚠ ") +
                            QString::fromStdString(warnings.front().message));
  } else {
    warning_label_->setText(
        QStringLiteral("⚠ %1 条存储提示（悬停查看）").arg(warning_count_));
  }
  warning_label_->setToolTip(lines.join(QStringLiteral("\n\n")));
  warning_label_->show();
}

void MarkPanel::SelectMark(const std::string& id) {
  QTreeWidgetItem* item = ItemForId(QString::fromStdString(id));
  if (item == nullptr) {
    return;
  }
  if (tree_->currentItem() != item) {
    tree_->setCurrentItem(item);  // currentItemChanged -> flush + load
  } else {
    LoadEditorForCurrent();
  }
  tree_->scrollToItem(item);
}

void MarkPanel::SelectNearFrame(std::int64_t frame) {
  if (tree_->topLevelItemCount() == 0) {
    return;
  }
  QTreeWidgetItem* candidate = nullptr;
  for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
    QTreeWidgetItem* item = tree_->topLevelItem(i);
    bool ok = false;
    const qlonglong item_frame = item->text(0).toLongLong(&ok);
    if (!ok) {
      continue;
    }
    if (item_frame >= frame) {
      candidate = item;
      break;
    }
  }
  if (candidate == nullptr) {
    candidate = tree_->topLevelItem(tree_->topLevelItemCount() - 1);
  }
  if (tree_->currentItem() != candidate) {
    tree_->setCurrentItem(candidate);  // currentItemChanged -> flush + load
  } else {
    LoadEditorForCurrent();
  }
  tree_->scrollToItem(candidate);
}

QString MarkPanel::SelectedMarkId() const {
  QTreeWidgetItem* item = tree_->currentItem();
  if (item == nullptr) {
    return QString();
  }
  return item->data(0, Qt::UserRole).toString();
}

bool MarkPanel::HasSelection() const { return tree_->currentItem() != nullptr; }

bool MarkPanel::FlushEditor() { return CommitEditor(); }

void MarkPanel::SetEditorTextForTest(const QString& label, const QString& note) {
  if (editing_id_.isEmpty()) {
    return;
  }
  label_edit_->setText(label);
  note_edit_->setPlainText(note);
}

QString MarkPanel::EditorLabelForTest() const { return label_edit_->text(); }

QString MarkPanel::EditorNoteForTest() const { return note_edit_->toPlainText(); }

bool MarkPanel::FocusNoteEditorForTest() {
  note_edit_->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  return QApplication::focusWidget() == note_edit_;
}

void MarkPanel::UpdateMarkText(const QString& id, const QString& label,
                               const QString& note) {
  lapian::core::Mark* mark = FindMarkMutable(id);
  if (mark == nullptr) {
    return;
  }
  mark->label = label.toStdString();
  mark->note = note.toStdString();
  QTreeWidgetItem* item = ItemForId(id);
  if (item != nullptr) {
    UpdateItemText(item, *mark);
  }
}

int MarkPanel::WarningCount() const { return warning_count_; }

void MarkPanel::SetEmbeddedMode(bool embedded) {
  if (embedded_ == embedded) {
    return;
  }
  embedded_ = embedded;
  // 表头/存储位置/Ctrl+M 提示由 InspectorPanel 的头、tab 计数与页脚承担；
  // 只隐藏，不销毁（markStorage 对象仍在，StorageSummary() 仍可读）。
  if (title_row_ != nullptr) {
    title_row_->setVisible(!embedded);
  }
  if (hint_label_ != nullptr) {
    hint_label_->setVisible(!embedded);
  }
  if (tree_ != nullptr && tree_->header() != nullptr) {
    tree_->header()->setVisible(!embedded);
    // 嵌进检查器时行首留 6px（原型 .mrow 的 8px 内边距的近似）。
    tree_->setIndentation(embedded ? 6 : 0);
  }
  if (layout() != nullptr) {
    layout()->setContentsMargins(embedded ? 0 : 10, embedded ? 0 : 8,
                                 embedded ? 0 : 10, embedded ? 0 : 8);
  }
  setProperty("embedded", embedded);
  // 属性选择器只在重新 polish 时生效：重设同一份样式表即可（只在切换时一次）。
  setStyleSheet(styleSheet());
  if (!marks_.empty()) {
    Rebuild();  // 行配色/标签回退随嵌入模式变（选中 id 由 Rebuild 保留）
  }
  updateGeometry();
}

QString MarkPanel::StorageSummary() const { return storage_label_->text(); }

void MarkPanel::Rebuild() {
  const QString selected = SelectedMarkId();
  const QSignalBlocker blocker(tree_);
  tree_->clear();
  for (const lapian::core::Mark& mark : marks_) {
    auto* item = new QTreeWidgetItem(tree_);
    item->setText(0, QString::number(static_cast<qlonglong>(mark.frame)));
    item->setTextAlignment(0, (embedded_ ? Qt::AlignLeft : Qt::AlignRight) |
                                   Qt::AlignVCenter);
    if (embedded_) {
      // 原型 .mrow .fno：帧号 accent、Consolas 600 11px、左对齐。
      item->setForeground(0, QColor(QStringLiteral("#5b8def")));
      QFont frame_font;
      frame_font.setFamilies({QStringLiteral("Consolas"),
                              QStringLiteral("Cascadia Mono")});
      frame_font.setStyleHint(QFont::Monospace);
      frame_font.setPixelSize(11);
      frame_font.setWeight(QFont::DemiBold);
      item->setFont(0, frame_font);
    }
    UpdateItemText(item, mark);
    const QString id = QString::fromStdString(mark.id);
    item->setData(0, Qt::UserRole, id);
    item->setToolTip(0, QStringLiteral("标记 %1\n双击/回车跳到该帧").arg(id));
  }
  if (!selected.isEmpty()) {
    QTreeWidgetItem* item = ItemForId(selected);
    if (item != nullptr) {
      tree_->setCurrentItem(item);
    }
  }
}

void MarkPanel::UpdateItemText(QTreeWidgetItem* item,
                               const lapian::core::Mark& mark) {
  if (item == nullptr) {
    return;
  }
  // 嵌入检查器时帧号已经在第 0 列（accent），MarkStore 自动生成的"帧 N"
  // 不再重复到标签列（用户自己写的同名标签仍显示）。独立使用 MarkPanel 时
  // 行为不变，tests/mark_panel_test 仍断言 "帧 1"。
  const std::string auto_label = "帧 " + std::to_string(mark.frame);
  const bool auto_generated = mark.label.empty() || mark.label == auto_label;
  item->setText(1, embedded_ && auto_generated
                        ? QString()
                        : LabelText(mark));
  item->setText(2, NoteSummary(mark.note));
  const QString full_note = QString::fromStdString(mark.note);
  item->setToolTip(2, full_note.isEmpty() ? QString() : full_note);
}

void MarkPanel::ActivateItem(QTreeWidgetItem* item) {
  if (item == nullptr) {
    return;
  }
  FlushEditor();
  const QString id = item->data(0, Qt::UserRole).toString();
  if (id.isEmpty()) {
    return;
  }
  bool ok = false;
  const qlonglong frame = item->text(0).toLongLong(&ok);
  if (!ok) {
    return;
  }
  emit markActivated(id, frame);
}

void MarkPanel::DeleteCurrent() {
  // 文本框聚焦时 Delete 属于文本编辑，不删除标记（spec §3.7）。
  if (IsEditorFocused()) {
    return;
  }
  FlushEditor();
  QTreeWidgetItem* item = tree_->currentItem();
  if (item == nullptr) {
    return;
  }
  const QString id = item->data(0, Qt::UserRole).toString();
  if (!id.isEmpty()) {
    emit deleteRequested(id);
  }
}

void MarkPanel::ShowContextMenu(const QPoint& pos) {
  FlushEditor();
  const QPoint viewport_pos = tree_->viewport()->mapFrom(tree_, pos);
  QTreeWidgetItem* item = tree_->itemAt(viewport_pos);
  if (item == nullptr) {
    return;
  }
  tree_->setCurrentItem(item);

  QMenu menu(this);
  QAction* jump = menu.addAction(QStringLiteral("跳到该帧"));
  menu.addSeparator();
  menu.addAction(delete_action_);
  QAction* chosen =
      menu.exec(tree_->viewport()->mapToGlobal(viewport_pos));
  if (chosen == jump) {
    ActivateItem(item);
  }
}

QTreeWidgetItem* MarkPanel::ItemForId(const QString& id) const {
  if (id.isEmpty()) {
    return nullptr;
  }
  for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
    QTreeWidgetItem* item = tree_->topLevelItem(i);
    if (item->data(0, Qt::UserRole).toString() == id) {
      return item;
    }
  }
  return nullptr;
}

void MarkPanel::HandleCurrentChanged(QTreeWidgetItem* current) {
  if (current == nullptr) {
    FlushEditor();
    ClearEditor();
    return;
  }
  LoadEditorForCurrent();
}

void MarkPanel::LoadEditorForCurrent() {
  QTreeWidgetItem* item = tree_->currentItem();
  const QString id = item == nullptr ? QString() : item->data(0, Qt::UserRole).toString();
  if (id.isEmpty()) {
    FlushEditor();
    ClearEditor();
    return;
  }
  if (editing_id_ == id) {
    // 同一标记（例如 SetMarks 重建后恢复选中）不覆盖正在编辑的文本与光标。
    const lapian::core::Mark* mark = FindMark(id);
    if (mark != nullptr) {
      editor_title_->setText(QStringLiteral("编辑 · 帧 %1")
                                 .arg(static_cast<qlonglong>(mark->frame)));
      editor_box_->show();
    }
    return;
  }
  FlushEditor();
  const lapian::core::Mark* mark = FindMark(id);
  if (mark == nullptr) {
    ClearEditor();
    return;
  }
  editing_id_ = id;
  label_edit_->setText(QString::fromStdString(mark->label));
  note_edit_->setPlainText(QString::fromStdString(mark->note));
  editor_title_->setText(
      QStringLiteral("编辑 · 帧 %1").arg(static_cast<qlonglong>(mark->frame)));
  editor_box_->show();
}

void MarkPanel::ClearEditor() {
  editing_id_.clear();
  label_edit_->clear();
  note_edit_->clear();
  editor_title_->clear();
  editor_box_->hide();
}

bool MarkPanel::CommitEditor() {
  if (committing_ || editing_id_.isEmpty()) {
    return false;
  }
  lapian::core::Mark* mark = FindMarkMutable(editing_id_);
  if (mark == nullptr) {
    ClearEditor();
    return false;
  }
  committing_ = true;
  const QString id = editing_id_;
  const QString label = label_edit_->text().trimmed();
  const QString note = note_edit_->toPlainText();
  const std::string label_utf8 = label.toStdString();
  const std::string note_utf8 = note.toStdString();
  const bool changed = mark->label != label_utf8 || mark->note != note_utf8;
  if (changed) {
    mark->label = label_utf8;
    mark->note = note_utf8;
    QTreeWidgetItem* item = ItemForId(id);
    if (item != nullptr) {
      UpdateItemText(item, *mark);
    }
  }
  committing_ = false;
  if (changed) {
    emit markTextEdited(id, label, note);
  }
  return changed;
}

const lapian::core::Mark* MarkPanel::FindMark(const QString& id) const {
  for (const lapian::core::Mark& mark : marks_) {
    if (QString::fromStdString(mark.id) == id) {
      return &mark;
    }
  }
  return nullptr;
}

lapian::core::Mark* MarkPanel::FindMarkMutable(const QString& id) {
  for (lapian::core::Mark& mark : marks_) {
    if (QString::fromStdString(mark.id) == id) {
      return &mark;
    }
  }
  return nullptr;
}

bool MarkPanel::IsEditorFocused() const {
  QWidget* focus = QApplication::focusWidget();
  if (focus == nullptr) {
    return false;
  }
  return focus == label_edit_ || focus == note_edit_ ||
         label_edit_->isAncestorOf(focus) || note_edit_->isAncestorOf(focus);
}

bool MarkPanel::eventFilter(QObject* watched, QEvent* event) {
  if (watched != label_edit_ && watched != note_edit_) {
    return QWidget::eventFilter(watched, event);
  }
  if (event->type() == QEvent::ShortcutOverride) {
    auto* key = static_cast<QKeyEvent*>(event);
    // spec §3.7：文本框聚焦时，除 Esc 外不触发全局快捷键。接受
    // ShortcutOverride 即让 Qt 不把该键交给 QAction；Esc 留给全屏退出等全局语义。
    if (key->key() != Qt::Key_Escape) {
      event->accept();
      return true;
    }
    return false;
  }
  if (event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    const bool is_enter =
        key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
    if (watched == label_edit_ && is_enter) {
      CommitEditor();
      event->accept();
      return true;
    }
    if (watched == note_edit_ && is_enter &&
        (key->modifiers() & Qt::ControlModifier) != 0) {
      CommitEditor();
      event->accept();
      return true;
    }
    // 备注里的普通 Enter 交给 QPlainTextEdit 换行。
    return false;
  }
  if (event->type() == QEvent::FocusOut) {
    CommitEditor();
    return false;
  }
  return QWidget::eventFilter(watched, event);
}
