#include "app/MarkPanel.h"

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
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
      "border-bottom:1px solid #3a3a40; padding:3px 4px; }"));

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(10, 8, 10, 8);
  root->setSpacing(6);

  auto* title_row = new QVBoxLayout();
  title_row->setSpacing(1);
  auto* title_line = new QHBoxLayout();
  title_line->setSpacing(6);
  title_label_ = new QLabel(QStringLiteral("标记"), this);
  title_label_->setObjectName(QStringLiteral("markTitle"));
  QFont title_font = title_label_->font();
  title_font.setPointSizeF(title_font.pointSizeF() + 1.0);
  title_label_->setFont(title_font);
  count_label_ = new QLabel(QStringLiteral("0 个"), this);
  count_label_->setObjectName(QStringLiteral("markCount"));
  title_line->addWidget(title_label_);
  title_line->addStretch(1);
  title_line->addWidget(count_label_);
  title_row->addLayout(title_line);

  storage_label_ = new QLabel(QStringLiteral("存储：—"), this);
  storage_label_->setObjectName(QStringLiteral("markStorage"));
  storage_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  storage_label_->setWordWrap(true);
  title_row->addWidget(storage_label_);
  root->addLayout(title_row);

  warning_label_ = new QLabel(this);
  warning_label_->setObjectName(QStringLiteral("markWarning"));
  warning_label_->setWordWrap(true);
  warning_label_->hide();
  root->addWidget(warning_label_);

  auto* hint = new QLabel(QStringLiteral("Ctrl+M 添加 · Delete 删除选中"), this);
  hint->setObjectName(QStringLiteral("markHint"));
  root->addWidget(hint);

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
  marks_ = marks;
  Rebuild();
  count_label_->setText(QStringLiteral("%1 个").arg(marks_.size()));
  const bool empty = marks_.empty();
  tree_->setVisible(!empty);
  empty_label_->setVisible(empty);
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
  tree_->setCurrentItem(item);
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
  tree_->setCurrentItem(candidate);
  tree_->scrollToItem(candidate);
}

QString MarkPanel::SelectedMarkId() const {
  QTreeWidgetItem* item = tree_->currentItem();
  if (item == nullptr) {
    return QString();
  }
  return item->data(0, Qt::UserRole).toString();
}

int MarkPanel::WarningCount() const { return warning_count_; }

QString MarkPanel::StorageSummary() const { return storage_label_->text(); }

void MarkPanel::Rebuild() {
  const QString selected = SelectedMarkId();
  const QSignalBlocker blocker(tree_);
  tree_->clear();
  for (const lapian::core::Mark& mark : marks_) {
    auto* item = new QTreeWidgetItem(tree_);
    item->setText(0, QString::number(static_cast<qlonglong>(mark.frame)));
    item->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
    const QString label = QString::fromStdString(mark.label);
    item->setText(1, label.isEmpty()
                         ? QStringLiteral("帧 %1").arg(static_cast<qlonglong>(mark.frame))
                         : label);
    const QString note = NoteSummary(mark.note);
    item->setText(2, note);
    const QString id = QString::fromStdString(mark.id);
    item->setData(0, Qt::UserRole, id);
    const QString full_note = QString::fromStdString(mark.note);
    if (!full_note.isEmpty()) {
      item->setToolTip(2, full_note);
    }
    item->setToolTip(0, QStringLiteral("标记 %1\n双击/回车跳到该帧").arg(id));
  }
  if (!selected.isEmpty()) {
    QTreeWidgetItem* item = ItemForId(selected);
    if (item != nullptr) {
      tree_->setCurrentItem(item);
    }
  }
}

void MarkPanel::ActivateItem(QTreeWidgetItem* item) {
  if (item == nullptr) {
    return;
  }
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
