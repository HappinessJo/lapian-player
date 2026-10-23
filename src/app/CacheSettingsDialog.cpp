#include "app/CacheSettingsDialog.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

#include "core/AppSettings.h"
#include "online/CacheManager.h"
#include "online/CacheStore.h"

namespace lapian::app {

using lapian::online::CacheManager;
using lapian::online::CacheStore;

CacheSettingsDialog::CacheSettingsDialog(CacheStore* cache, QString records_root,
                                         bool session_active,
                                         QString session_description,
                                         Callbacks callbacks, QWidget* parent)
    : QDialog(parent),
      cache_(cache),
      manager_(std::make_unique<CacheManager>(cache, records_root)),
      callbacks_(std::move(callbacks)),
      session_active_(session_active),
      session_description_(std::move(session_description)) {
  if (cache_ != nullptr) {
    pending_root_ = cache_->root();
  } else {
    pending_root_ = CacheStore::DefaultRoot();
  }
  BuildUi();
  Refresh();
}

CacheSettingsDialog::~CacheSettingsDialog() = default;

void CacheSettingsDialog::BuildUi() {
  setWindowTitle(QStringLiteral("设置 — 在线缓存"));
  setMinimumWidth(640);

  auto* root_layout = new QVBoxLayout(this);
  root_layout->setContentsMargins(14, 12, 14, 12);
  root_layout->setSpacing(10);

  auto* intro = new QLabel(
      QStringLiteral("在线模式会把播放数据缓存到本地。清空缓存不会删除在线标记、"
                     "备注或截图；当前正在播放/下载的条目会保留。"),
      this);
  intro->setWordWrap(true);
  root_layout->addWidget(intro);

  // 缓存目录
  auto* dir_group = new QGroupBox(QStringLiteral("缓存目录"), this);
  auto* dir_layout = new QVBoxLayout(dir_group);
  auto* dir_row = new QHBoxLayout();
  root_edit_ = new QLineEdit(dir_group);
  root_edit_->setReadOnly(true);
  root_edit_->setObjectName(QStringLiteral("cacheRootEdit"));
  browse_button_ = new QPushButton(QStringLiteral("更改目录..."), dir_group);
  browse_button_->setObjectName(QStringLiteral("cacheBrowseButton"));
  default_button_ = new QPushButton(QStringLiteral("恢复默认"), dir_group);
  default_button_->setObjectName(QStringLiteral("cacheDefaultButton"));
  dir_row->addWidget(root_edit_, 1);
  dir_row->addWidget(browse_button_);
  dir_row->addWidget(default_button_);
  dir_layout->addLayout(dir_row);
  root_hint_label_ = new QLabel(
      QStringLiteral("更改目录不会迁移旧缓存；旧目录中的文件会原样保留，可在旧目录"
                     "手动删除。保存后写入设置，下次启动继续生效。"),
      dir_group);
  root_hint_label_->setWordWrap(true);
  dir_layout->addWidget(root_hint_label_);
  root_state_label_ = new QLabel(dir_group);
  root_state_label_->setWordWrap(true);
  dir_layout->addWidget(root_state_label_);
  root_layout->addWidget(dir_group);

  // 配额与占用
  auto* quota_group = new QGroupBox(QStringLiteral("配额与占用"), this);
  auto* quota_layout = new QVBoxLayout(quota_group);
  auto* quota_row = new QHBoxLayout();
  auto* quota_label = new QLabel(QStringLiteral("缓存配额"), quota_group);
  quota_spin_ = new QSpinBox(quota_group);
  quota_spin_->setRange(lapian::core::kCacheQuotaMinGb,
                        lapian::core::kCacheQuotaMaxGb);
  quota_spin_->setSuffix(QStringLiteral(" GB"));
  quota_spin_->setSingleStep(1);
  quota_spin_->setValue(cache_ == nullptr
                            ? lapian::core::kCacheQuotaDefaultGb
                            : static_cast<int>(
                                  std::clamp<qint64>(cache_->quota() /
                                                         (1024LL * 1024LL * 1024LL),
                                                      lapian::core::kCacheQuotaMinGb,
                                                      lapian::core::kCacheQuotaMaxGb)));
  quota_spin_->setObjectName(QStringLiteral("cacheQuotaSpin"));
  quota_row->addWidget(quota_label);
  quota_row->addWidget(quota_spin_);
  quota_row->addStretch(1);
  quota_layout->addLayout(quota_row);
  usage_label_ = new QLabel(quota_group);
  usage_label_->setWordWrap(true);
  usage_label_->setObjectName(QStringLiteral("cacheUsageLabel"));
  quota_layout->addWidget(usage_label_);
  free_label_ = new QLabel(quota_group);
  free_label_->setWordWrap(true);
  quota_layout->addWidget(free_label_);
  root_layout->addWidget(quota_group);

  // 在线记录
  auto* records_group = new QGroupBox(QStringLiteral("在线记录（不随缓存清空）"), this);
  auto* records_layout = new QVBoxLayout(records_group);
  records_label_ = new QLabel(records_group);
  records_label_->setWordWrap(true);
  records_label_->setObjectName(QStringLiteral("cacheRecordsLabel"));
  records_layout->addWidget(records_label_);
  root_layout->addWidget(records_group);

  session_label_ = new QLabel(this);
  session_label_->setWordWrap(true);
  session_label_->setObjectName(QStringLiteral("cacheSessionLabel"));
  session_label_->hide();
  root_layout->addWidget(session_label_);

  auto* action_row = new QHBoxLayout();
  open_button_ = new QPushButton(QStringLiteral("打开缓存目录"), this);
  open_button_->setObjectName(QStringLiteral("cacheOpenButton"));
  cleanup_button_ = new QPushButton(QStringLiteral("清理孤儿"), this);
  cleanup_button_->setObjectName(QStringLiteral("cacheCleanupButton"));
  clear_button_ = new QPushButton(QStringLiteral("清空缓存..."), this);
  clear_button_->setObjectName(QStringLiteral("cacheClearButton"));
  action_row->addWidget(open_button_);
  action_row->addWidget(cleanup_button_);
  action_row->addStretch(1);
  action_row->addWidget(clear_button_);
  root_layout->addLayout(action_row);

  status_label_ = new QLabel(this);
  status_label_->setWordWrap(true);
  status_label_->setObjectName(QStringLiteral("cacheStatusLabel"));
  status_label_->setMinimumHeight(32);
  root_layout->addWidget(status_label_);

  auto* button_row = new QHBoxLayout();
  button_row->addStretch(1);
  save_button_ = new QPushButton(QStringLiteral("保存"), this);
  save_button_->setObjectName(QStringLiteral("cacheSaveButton"));
  save_button_->setDefault(true);
  close_button_ = new QPushButton(QStringLiteral("关闭"), this);
  close_button_->setObjectName(QStringLiteral("cacheCloseButton"));
  button_row->addWidget(save_button_);
  button_row->addWidget(close_button_);
  root_layout->addLayout(button_row);

  connect(browse_button_, &QPushButton::clicked, this,
          &CacheSettingsDialog::ChooseDirectory);
  connect(default_button_, &QPushButton::clicked, this,
          &CacheSettingsDialog::ResetToDefault);
  connect(open_button_, &QPushButton::clicked, this,
          &CacheSettingsDialog::OpenDirectory);
  connect(cleanup_button_, &QPushButton::clicked, this,
          &CacheSettingsDialog::PerformCleanup);
  connect(clear_button_, &QPushButton::clicked, this,
          [this]() { PerformClear(true); });
  connect(save_button_, &QPushButton::clicked, this,
          [this]() { ApplyInternal(true, nullptr); });
  connect(close_button_, &QPushButton::clicked, this, &QDialog::reject);
}

void CacheSettingsDialog::Refresh() {
  UpdateSummaryUi();
  UpdatePendingUi();
  session_label_->setVisible(session_active_);
  if (session_active_) {
    session_label_->setText(
        session_description_.isEmpty()
            ? QStringLiteral("在线会话打开中：不能更改缓存目录或配额；清空缓存会"
                             "保留当前会话的条目。")
            : session_description_);
  }
  browse_button_->setEnabled(!session_active_ && cache_ != nullptr);
  default_button_->setEnabled(!session_active_ && cache_ != nullptr);
  quota_spin_->setEnabled(!session_active_ && cache_ != nullptr);
  save_button_->setEnabled(!session_active_ && cache_ != nullptr);
  clear_button_->setEnabled(cache_ != nullptr);
  cleanup_button_->setEnabled(cache_ != nullptr);
  open_button_->setEnabled(cache_ != nullptr);
  clear_button_->setText(session_active_ ? QStringLiteral("清空其他缓存...")
                                         : QStringLiteral("清空缓存..."));
}

void CacheSettingsDialog::SetSessionActive(bool active,
                                           const QString& description) {
  session_active_ = active;
  session_description_ = description;
  Refresh();
}

void CacheSettingsDialog::UpdatePendingUi() {
  if (root_edit_ == nullptr) {
    return;
  }
  root_edit_->setText(QDir::toNativeSeparators(pending_root_));
  root_edit_->setToolTip(QDir::toNativeSeparators(pending_root_));
  const bool changed =
      cache_ != nullptr &&
      QDir::cleanPath(pending_root_) != QDir::cleanPath(cache_->root());
  if (changed) {
    root_state_label_->setStyleSheet(QStringLiteral("color:#d8b45a;"));
    root_state_label_->setText(QStringLiteral("目录已更改，点击“保存”后生效。"));
  }
}

void CacheSettingsDialog::UpdateSummaryUi() {
  if (manager_ == nullptr || cache_ == nullptr) {
    usage_label_->setText(QStringLiteral("在线缓存未初始化。"));
    free_label_->clear();
    records_label_->setText(QStringLiteral("在线记录不可用。"));
    return;
  }
  const lapian::online::CacheSummary summary = manager_->Summary();
  if (!summary.root_exists) {
    usage_label_->setText(
        QStringLiteral("缓存目录尚未创建；首次在线播放时会创建。"));
  } else {
    usage_label_->setText(
        QStringLiteral("当前占用 %1 / 配额 %2（%3 条：完成 %4、未完成 %5、损坏 %6）")
            .arg(CacheStore::FormatBytes(summary.usage_bytes),
                 CacheStore::FormatBytes(summary.quota_bytes))
            .arg(summary.entries)
            .arg(summary.complete_entries)
            .arg(summary.incomplete_entries)
            .arg(summary.broken_entries));
  }
  if (summary.free_bytes < 0) {
    free_label_->setText(QStringLiteral("磁盘可用空间：读取失败。"));
  } else {
    free_label_->setText(
        QStringLiteral("磁盘可用空间：%1")
            .arg(CacheStore::FormatBytes(summary.free_bytes)));
  }
  if (!summary.root_error.isEmpty()) {
    root_state_label_->setStyleSheet(QStringLiteral("color:#e06c75;"));
    root_state_label_->setText(
        QStringLiteral("目录不可用：%1").arg(summary.root_error));
  } else if (summary.root_exists) {
    root_state_label_->setStyleSheet(QStringLiteral("color:#6fbf73;"));
    root_state_label_->setText(QStringLiteral("目录可写。"));
  } else {
    root_state_label_->setStyleSheet(QStringLiteral("color:#9aa0a6;"));
    root_state_label_->setText(QStringLiteral("目录尚未创建。"));
  }

  const lapian::online::CacheRecordsSummary records = manager_->Records();
  records_label_->setText(
      QStringLiteral("在线记录：%1 条（%2）\n目录：%3\n"
                     "截图保存在图片目录；清空缓存不会删除记录和截图。")
          .arg(records.count)
          .arg(CacheStore::FormatBytes(records.bytes),
               QDir::toNativeSeparators(records.root)));
  records_label_->setToolTip(QDir::toNativeSeparators(records.root));
}

void CacheSettingsDialog::ChooseDirectory() {
  const QString chosen = QFileDialog::getExistingDirectory(
      this, QStringLiteral("选择在线缓存目录"), pending_root_,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  if (chosen.isEmpty()) {
    return;
  }
  pending_root_ = CacheManager::NormalizeRoot(chosen);
  SetStatus(QStringLiteral("已选择新目录；点击“保存”后生效。"), false);
  UpdatePendingUi();
}

void CacheSettingsDialog::ResetToDefault() {
  pending_root_ = CacheStore::DefaultRoot();
  SetStatus(QStringLiteral("已恢复默认目录；点击“保存”后生效。"), false);
  UpdatePendingUi();
}

bool CacheSettingsDialog::ApplyInternal(bool close_on_success, QString* error) {
  auto fail = [&](const QString& message) {
    SetStatus(message, true);
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (session_active_) {
    return fail(QStringLiteral("在线会话打开中，不能更改缓存目录或配额；"
                               "请先关闭在线视频。"));
  }
  if (cache_ == nullptr || !callbacks_.apply) {
    return fail(QStringLiteral("在线缓存未初始化，无法保存。"));
  }
  const QString normalized = CacheManager::NormalizeRoot(pending_root_);
  const int quota_gb = quota_spin_->value();
  QString apply_error;
  if (!callbacks_.apply(normalized, quota_gb, &apply_error)) {
    return fail(apply_error.isEmpty() ? QStringLiteral("保存失败") : apply_error);
  }
  pending_root_ = normalized;
  SetStatus(QStringLiteral("已保存：缓存目录/配额已生效，并在下次启动继续使用。"),
            false);
  if (close_on_success) {
    accept();
  }
  return true;
}

void CacheSettingsDialog::PerformClear(bool confirm) {
  if (cache_ == nullptr || !callbacks_.clear) {
    SetStatus(QStringLiteral("在线缓存未初始化，无法清空。"), true);
    return;
  }
  const lapian::online::CacheSummary summary = manager_->Summary();
  const QString usage = CacheStore::FormatBytes(summary.usage_bytes);
  if (confirm) {
    const QString text =
        QStringLiteral("将删除缓存目录中的视频数据（当前占用 %1）。\n\n"
                       "不会删除在线标记、备注、截图，也不会删除在线记录；"
                       "当前在线会话的条目会保留。\n\n确定清空缓存吗？")
            .arg(usage);
    const QMessageBox::StandardButton answer =
        QMessageBox::question(this, QStringLiteral("清空缓存"), text,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      return;
    }
  }
  QString clear_error;
  qint64 removed = 0;
  int kept = 0;
  if (!callbacks_.clear(&clear_error, &removed, &kept)) {
    const QString message = clear_error.isEmpty()
                                ? QStringLiteral("清空缓存失败")
                                : clear_error;
    SetStatus(message, true);
    if (confirm) {
      QMessageBox::warning(this, QStringLiteral("清空缓存失败"), message);
    }
    return;
  }
  QString text = QStringLiteral("已清空缓存：删除 %1")
                     .arg(CacheStore::FormatBytes(removed));
  if (kept > 0) {
    text += QStringLiteral("；保留当前在线会话 %1 条").arg(kept);
  }
  text += QStringLiteral("。在线记录与截图未改变。");
  SetStatus(text, false);
  UpdateSummaryUi();
}

void CacheSettingsDialog::PerformCleanup() {
  if (cache_ == nullptr || !callbacks_.cleanup) {
    SetStatus(QStringLiteral("在线缓存未初始化，无法清理。"), true);
    return;
  }
  QString error;
  qint64 removed = 0;
  int entries = 0;
  if (!callbacks_.cleanup(&error, &removed, &entries)) {
    SetStatus(error.isEmpty() ? QStringLiteral("清理失败") : error, true);
    return;
  }
  SetStatus(QStringLiteral("已清理孤儿/过期缓存：%1 个条目，%2。")
                .arg(entries)
                .arg(CacheStore::FormatBytes(removed)),
            false);
  UpdateSummaryUi();
}

void CacheSettingsDialog::OpenDirectory() {
  if (cache_ == nullptr || !callbacks_.open_directory) {
    SetStatus(QStringLiteral("在线缓存未初始化，无法打开目录。"), true);
    return;
  }
  QString error;
  if (!callbacks_.open_directory(&error)) {
    SetStatus(error.isEmpty() ? QStringLiteral("无法打开缓存目录") : error, true);
    return;
  }
  SetStatus(QStringLiteral("已打开缓存目录。"), false);
}

void CacheSettingsDialog::SetStatus(const QString& text, bool error) {
  if (status_label_ == nullptr) {
    return;
  }
  status_label_->setStyleSheet(error ? QStringLiteral("color:#e06c75;")
                                     : QStringLiteral("color:#6fbf73;"));
  status_label_->setText(text);
}

QString CacheSettingsDialog::RootTextForTest() const {
  return root_edit_ == nullptr ? QString() : root_edit_->text();
}

int CacheSettingsDialog::QuotaGbForTest() const {
  return quota_spin_ == nullptr ? 0 : quota_spin_->value();
}

QString CacheSettingsDialog::UsageTextForTest() const {
  return usage_label_ == nullptr ? QString() : usage_label_->text();
}

QString CacheSettingsDialog::RecordsTextForTest() const {
  return records_label_ == nullptr ? QString() : records_label_->text();
}

QString CacheSettingsDialog::StatusTextForTest() const {
  return status_label_ == nullptr ? QString() : status_label_->text();
}

bool CacheSettingsDialog::SaveEnabledForTest() const {
  return save_button_ != nullptr && save_button_->isEnabled();
}

bool CacheSettingsDialog::ClearEnabledForTest() const {
  return clear_button_ != nullptr && clear_button_->isEnabled();
}

bool CacheSettingsDialog::QuotaEnabledForTest() const {
  return quota_spin_ != nullptr && quota_spin_->isEnabled();
}

void CacheSettingsDialog::SetPendingRootForTest(const QString& root) {
  pending_root_ = CacheManager::NormalizeRoot(root);
  UpdatePendingUi();
}

void CacheSettingsDialog::SetQuotaForTest(int quota_gb) {
  if (quota_spin_ != nullptr) {
    quota_spin_->setValue(quota_gb);
  }
}

bool CacheSettingsDialog::ApplyForTest(QString* error) {
  return ApplyInternal(false, error);
}

void CacheSettingsDialog::ClearWithoutConfirmForTest() { PerformClear(false); }

void CacheSettingsDialog::CleanupForTest() { PerformCleanup(); }

void CacheSettingsDialog::OpenDirectoryForTest() { OpenDirectory(); }

}  // namespace lapian::app
