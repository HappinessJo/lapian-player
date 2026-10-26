#include "app/CacheSettingsDialog.h"

#include <QComboBox>
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
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/AppSettings.h"
#include "online/CacheManager.h"
#include "online/CacheStore.h"

namespace lapian::app {

using lapian::online::CacheManager;
using lapian::online::CacheStore;

namespace {

// 3D：语义色走全局 QSS 的属性选择器（禁止 per-widget setStyleSheet）。
void SetStateProperty(QWidget* widget, const char* state) {
  if (widget == nullptr) {
    return;
  }
  widget->setProperty("state", QString::fromLatin1(state));
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
}

// D3：缓存挡位下拉的 itemData（顺序与文案固定，见计划 §3.3）。
enum class PreviewPreset : int {
  kLowMemory = 0,    // 省内存（720P / 128MiB）
  kBalanced = 1,     // 均衡（1080P / 256MiB）
  kRecommended = 2,  // 推荐（1080P / 512MiB）
  kLong = 3,         // 长拖（1080P / 1GiB）
  kCustom = 4,       // 高级自定义（不改分辨率）
};

// 16:9 估计帧高（偶数，与 OnlinePreviewDecoder 的估算一致）。
int PreviewFrameHeightForMaxSide(int max_side) {
  return std::max(2, (max_side * 9 / 16 + 1) & ~1);
}

// 分辨率标签：1920→1080P、1280→720P，其它按高度 <h>P（计划 §3.3）。
QString PreviewMaxSideLabel(int max_side) {
  if (max_side == 1920) {
    return QStringLiteral("1080P");
  }
  if (max_side == 1280) {
    return QStringLiteral("720P");
  }
  return QStringLiteral("%1P").arg(PreviewFrameHeightForMaxSide(max_side));
}

QString PreviewResolutionLabel(lapian::core::PreviewResolution resolution) {
  switch (resolution) {
    case lapian::core::PreviewResolution::k1080p:
      return QStringLiteral("1080P");
    case lapian::core::PreviewResolution::k720p:
      return QStringLiteral("720P");
    case lapian::core::PreviewResolution::kAuto:
    default:
      return QStringLiteral("自动");
  }
}

// (分辨率, 缓存) → 挡位：只有完全匹配预设才算预设档，否则显示高级自定义。
PreviewPreset DerivePreviewPreset(lapian::core::PreviewResolution resolution,
                                  int cache_mb) {
  if (cache_mb == lapian::core::kPreviewCacheMinMb720p &&
      resolution == lapian::core::PreviewResolution::k720p) {
    return PreviewPreset::kLowMemory;
  }
  if (cache_mb == lapian::core::kPreviewCacheMinMb1080p &&
      resolution == lapian::core::PreviewResolution::k1080p) {
    return PreviewPreset::kBalanced;
  }
  if (cache_mb == lapian::core::kPreviewCacheDefaultMb &&
      resolution == lapian::core::PreviewResolution::k1080p) {
    return PreviewPreset::kRecommended;
  }
  if (cache_mb == 1024 &&
      resolution == lapian::core::PreviewResolution::k1080p) {
    return PreviewPreset::kLong;
  }
  return PreviewPreset::kCustom;
}

}  // namespace

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
  root_state_label_->setObjectName(QStringLiteral("cacheRootState"));
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

  // D3：拖动预览（分辨率 + 缓存挡位 + 自定义 + 估算/修正提示）。
  // 与缓存目录不同：在线会话中也可编辑，保存后下次在线会话生效（不热切换）。
  auto* preview_group = new QGroupBox(QStringLiteral("拖动预览"), this);
  auto* preview_layout = new QVBoxLayout(preview_group);
  auto* preview_row = new QHBoxLayout();
  auto* preview_resolution_label =
      new QLabel(QStringLiteral("预览分辨率"), preview_group);
  preview_resolution_combo_ = new QComboBox(preview_group);
  preview_resolution_combo_->setObjectName(
      QStringLiteral("previewResolutionCombo"));
  preview_resolution_combo_->addItem(
      QStringLiteral("自动（推荐）"),
      static_cast<int>(lapian::core::PreviewResolution::kAuto));
  preview_resolution_combo_->addItem(
      QStringLiteral("1080P"),
      static_cast<int>(lapian::core::PreviewResolution::k1080p));
  preview_resolution_combo_->addItem(
      QStringLiteral("720P"),
      static_cast<int>(lapian::core::PreviewResolution::k720p));
  auto* preview_cache_label =
      new QLabel(QStringLiteral("预览缓存"), preview_group);
  preview_cache_combo_ = new QComboBox(preview_group);
  preview_cache_combo_->setObjectName(QStringLiteral("previewCacheCombo"));
  preview_cache_combo_->addItem(
      QStringLiteral("省内存（720P / 128MiB）"),
      static_cast<int>(PreviewPreset::kLowMemory));
  preview_cache_combo_->addItem(
      QStringLiteral("均衡（1080P / 256MiB）"),
      static_cast<int>(PreviewPreset::kBalanced));
  preview_cache_combo_->addItem(
      QStringLiteral("推荐（1080P / 512MiB）"),
      static_cast<int>(PreviewPreset::kRecommended));
  preview_cache_combo_->addItem(
      QStringLiteral("长拖（1080P / 1GiB）"),
      static_cast<int>(PreviewPreset::kLong));
  preview_cache_combo_->addItem(QStringLiteral("高级自定义"),
                                static_cast<int>(PreviewPreset::kCustom));
  preview_row->addWidget(preview_resolution_label);
  preview_row->addWidget(preview_resolution_combo_);
  preview_row->addSpacing(16);
  preview_row->addWidget(preview_cache_label);
  preview_row->addWidget(preview_cache_combo_, 1);
  preview_layout->addLayout(preview_row);

  auto* preview_custom_row = new QHBoxLayout();
  auto* preview_custom_label =
      new QLabel(QStringLiteral("自定义缓存"), preview_group);
  preview_cache_spin_ = new QSpinBox(preview_group);
  preview_cache_spin_->setObjectName(QStringLiteral("previewCacheSpin"));
  preview_cache_spin_->setRange(lapian::core::kPreviewCacheMinMb720p,
                                lapian::core::kPreviewCacheMaxMb);
  preview_cache_spin_->setSingleStep(64);
  preview_cache_spin_->setSuffix(QStringLiteral(" MiB"));
  preview_custom_row->addWidget(preview_custom_label);
  preview_custom_row->addWidget(preview_cache_spin_);
  preview_custom_row->addStretch(1);
  preview_layout->addLayout(preview_custom_row);

  auto* preview_estimate_row = new QHBoxLayout();
  preview_estimate_label_ = new QLabel(preview_group);
  preview_estimate_label_->setObjectName(
      QStringLiteral("previewEstimateLabel"));
  preview_restore_button_ =
      new QPushButton(QStringLiteral("恢复推荐设置"), preview_group);
  preview_restore_button_->setObjectName(
      QStringLiteral("previewRestoreButton"));
  preview_estimate_row->addWidget(preview_estimate_label_, 1);
  preview_estimate_row->addWidget(preview_restore_button_);
  preview_layout->addLayout(preview_estimate_row);

  preview_hint_label_ = new QLabel(preview_group);
  preview_hint_label_->setObjectName(QStringLiteral("previewHintLabel"));
  preview_hint_label_->setWordWrap(true);
  preview_layout->addWidget(preview_hint_label_);
  root_layout->addWidget(preview_group);

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
  // D3：拖动预览交互。预设切换同时改分辨率；分辨率切换只抬下限、不下调。
  connect(preview_resolution_combo_, &QComboBox::currentIndexChanged, this,
          [this](int) { OnPreviewResolutionChanged(); });
  connect(preview_cache_combo_, &QComboBox::currentIndexChanged, this,
          [this](int) { OnPreviewPresetChanged(); });
  connect(preview_cache_spin_, &QSpinBox::valueChanged, this, [this](int value) {
    preview_cache_mb_ = value;
    preview_custom_mode_ = true;
    preview_lifted_ = false;
    preview_limited_by_memory_ = false;
    UpdatePreviewUi();
  });
  connect(preview_restore_button_, &QPushButton::clicked, this,
          [this]() { RestoreRecommended(); });
  UpdatePreviewUi();
}

void CacheSettingsDialog::Refresh() {
  UpdateSummaryUi();
  UpdatePendingUi();
  session_label_->setVisible(session_active_);
  if (session_active_) {
    session_label_->setText(
        session_description_.isEmpty()
            ? QStringLiteral("当前在线会话继续使用旧目录；新目录/配额在本次"
                             "会话结束后生效（下次在线打开使用）。清空缓存会"
                             "保留当前会话的条目。")
            : session_description_);
  }
  // P1（两阶段保存）：在线会话中允许编辑/保存；MainWindow 只写 settings 并
  // 置 pending，不替换当前 OnlineSession 持有的 CacheStore。
  browse_button_->setEnabled(cache_ != nullptr);
  default_button_->setEnabled(cache_ != nullptr);
  quota_spin_->setEnabled(cache_ != nullptr);
  save_button_->setEnabled(cache_ != nullptr);
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
    SetStateProperty(root_state_label_, "warn");
    root_state_label_->setText(
        session_active_
            ? QStringLiteral("目录已更改：保存后本次在线会话仍用旧目录，"
                             "会话结束后生效（下次在线打开使用）。")
            : QStringLiteral("目录已更改，点击“保存”后生效。"));
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
    SetStateProperty(root_state_label_, "error");
    root_state_label_->setText(
        QStringLiteral("目录不可用：%1").arg(summary.root_error));
  } else if (summary.root_exists) {
    SetStateProperty(root_state_label_, "ok");
    root_state_label_->setText(QStringLiteral("目录可写。"));
  } else {
    SetStateProperty(root_state_label_, "muted");
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
  if (cache_ == nullptr || !callbacks_.apply) {
    return fail(QStringLiteral("在线缓存未初始化，无法保存。"));
  }
  const QString normalized = CacheManager::NormalizeRoot(pending_root_);
  const int quota_gb = quota_spin_->value();
  // D3：交给 callbacks.apply 之前先按 D1 纯函数修正拖动预览的非法组合
  // （抬下限/夹 2GiB/夹可用内存 25%），并让提示显示“已自动调整”。
  const int preview_mb = ApplyPreviewClamp();
  QString apply_error;
  if (!callbacks_.apply(normalized, quota_gb, preview_resolution_, preview_mb,
                        &apply_error)) {
    return fail(apply_error.isEmpty() ? QStringLiteral("保存失败") : apply_error);
  }
  pending_root_ = normalized;
  // P1：在线会话中保存是两阶段的——本次会话仍用旧 CacheStore，MainWindow 只
  // 写 settings + pending；新目录/配额在会话结束后生效。
  // D3：拖动预览也只在下次在线会话生效（不重建当前预览解码器）。
  const QString preview_state =
      QStringLiteral("%1 / %2MiB")
          .arg(PreviewResolutionLabel(preview_resolution_))
          .arg(preview_mb);
  SetStatus(session_active_
                ? QStringLiteral("已保存：缓存目录/配额在本次会话结束后生效；"
                                 "拖动预览 %1 下次在线会话生效。")
                      .arg(preview_state)
                : QStringLiteral(
                      "已保存：缓存目录/配额已生效，并在下次启动继续使用；"
                      "拖动预览 %1 下次在线会话生效。")
                      .arg(preview_state),
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
  SetStateProperty(status_label_, error ? "error" : "ok");
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

// ---- D3：拖动预览 ---------------------------------------------------------

void CacheSettingsDialog::SetPreviewContext(int source_max_side,
                                            double source_fps,
                                            int available_memory_mb) {
  preview_source_max_side_ = std::max(0, source_max_side);
  preview_source_fps_ =
      std::isfinite(source_fps) && source_fps > 0.0 ? source_fps : 0.0;
  preview_available_memory_mb_ = std::max(0, available_memory_mb);
  UpdatePreviewUi();
}

void CacheSettingsDialog::SetPreviewSettings(
    lapian::core::PreviewResolution resolution, int cache_mb) {
  preview_resolution_ = resolution;
  preview_cache_mb_ = std::clamp(cache_mb,
                                 lapian::core::kPreviewCacheMinMb720p,
                                 lapian::core::kPreviewCacheMaxMb);
  preview_custom_mode_ = false;
  preview_lifted_ = false;
  preview_limited_by_memory_ = false;
  UpdatePreviewUi();
}

void CacheSettingsDialog::UpdatePreviewUi() {
  if (preview_resolution_combo_ == nullptr || preview_cache_combo_ == nullptr ||
      preview_cache_spin_ == nullptr) {
    return;
  }
  {
    // 程序化同步 combo 不回弹到事件处理，避免预设/分辨率互相改写时递归。
    const QSignalBlocker blocker(preview_resolution_combo_);
    const int index = preview_resolution_combo_->findData(
        static_cast<int>(preview_resolution_));
    if (index >= 0) {
      preview_resolution_combo_->setCurrentIndex(index);
    }
  }
  const PreviewPreset derived =
      DerivePreviewPreset(preview_resolution_, preview_cache_mb_);
  const PreviewPreset shown =
      preview_custom_mode_ ? PreviewPreset::kCustom : derived;
  {
    const QSignalBlocker blocker(preview_cache_combo_);
    const int index = preview_cache_combo_->findData(static_cast<int>(shown));
    if (index >= 0) {
      preview_cache_combo_->setCurrentIndex(index);
    }
  }
  {
    const QSignalBlocker blocker(preview_cache_spin_);
    preview_cache_spin_->setValue(std::clamp(
        preview_cache_mb_, lapian::core::kPreviewCacheMinMb720p,
        lapian::core::kPreviewCacheMaxMb));
  }
  // 预设档显示预设值但禁用；只有“高级自定义”允许编辑 spin。
  preview_cache_spin_->setEnabled(shown == PreviewPreset::kCustom);
  if (preview_estimate_label_ != nullptr) {
    preview_estimate_label_->setText(PreviewEstimateText());
  }
  if (preview_hint_label_ != nullptr) {
    preview_hint_label_->setText(PreviewHintText());
    SetStateProperty(preview_hint_label_,
                     PreviewHintIsWarning() ? "warn" : "muted");
  }
}

int CacheSettingsDialog::ApplyPreviewClamp() {
  const int min_mb =
      lapian::core::PreviewCacheMinMbForResolution(preview_resolution_);
  const bool lifted = preview_cache_mb_ < min_mb;
  bool limited_by_memory = false;
  const int clamped = lapian::core::ClampPreviewCacheMb(
      preview_cache_mb_, preview_resolution_, preview_available_memory_mb_,
      &limited_by_memory);
  preview_lifted_ = lifted;
  // 下限与内存上限同时触发时只报“抬下限”：此时 25% 上限可能低于下限。
  preview_limited_by_memory_ = limited_by_memory && !lifted;
  preview_cache_mb_ = clamped;
  UpdatePreviewUi();
  return preview_cache_mb_;
}

void CacheSettingsDialog::ApplyPreviewResolutionChange(
    lapian::core::PreviewResolution resolution) {
  preview_resolution_ = resolution;
  preview_limited_by_memory_ = false;
  const int min_mb = lapian::core::PreviewCacheMinMbForResolution(resolution);
  // 切到 auto/1080P 且当前缓存低于下限时立即抬到下限；切到 720P 不主动下调
  // （计划 §3.2：低于下限自动抬升，不为了省内存偷偷丢用户设置）。
  if (resolution != lapian::core::PreviewResolution::k720p &&
      preview_cache_mb_ < min_mb) {
    preview_cache_mb_ = min_mb;
    preview_lifted_ = true;
  } else {
    preview_lifted_ = false;
  }
  UpdatePreviewUi();
}

void CacheSettingsDialog::OnPreviewResolutionChanged() {
  if (preview_resolution_combo_ == nullptr) {
    return;
  }
  ApplyPreviewResolutionChange(static_cast<lapian::core::PreviewResolution>(
      preview_resolution_combo_->currentData().toInt()));
}

void CacheSettingsDialog::OnPreviewPresetChanged() {
  if (preview_cache_combo_ == nullptr) {
    return;
  }
  const PreviewPreset preset = static_cast<PreviewPreset>(
      preview_cache_combo_->currentData().toInt());
  preview_lifted_ = false;
  preview_limited_by_memory_ = false;
  switch (preset) {
    case PreviewPreset::kLowMemory:
      preview_custom_mode_ = false;
      preview_resolution_ = lapian::core::PreviewResolution::k720p;
      preview_cache_mb_ = lapian::core::kPreviewCacheMinMb720p;
      break;
    case PreviewPreset::kBalanced:
      preview_custom_mode_ = false;
      preview_resolution_ = lapian::core::PreviewResolution::k1080p;
      preview_cache_mb_ = lapian::core::kPreviewCacheMinMb1080p;
      break;
    case PreviewPreset::kRecommended:
      preview_custom_mode_ = false;
      preview_resolution_ = lapian::core::PreviewResolution::k1080p;
      preview_cache_mb_ = lapian::core::kPreviewCacheDefaultMb;
      break;
    case PreviewPreset::kLong:
      preview_custom_mode_ = false;
      preview_resolution_ = lapian::core::PreviewResolution::k1080p;
      preview_cache_mb_ = 1024;
      break;
    case PreviewPreset::kCustom:
    default:
      // 高级自定义不改分辨率，只允许直接编辑缓存值。
      preview_custom_mode_ = true;
      break;
  }
  UpdatePreviewUi();
}

void CacheSettingsDialog::RestoreRecommended() {
  preview_resolution_ = lapian::core::PreviewResolution::kAuto;
  preview_cache_mb_ = lapian::core::kPreviewCacheDefaultMb;
  preview_custom_mode_ = false;
  preview_lifted_ = false;
  preview_limited_by_memory_ = false;
  UpdatePreviewUi();
}

int CacheSettingsDialog::PreviewMaxSide() const {
  lapian::core::AppSettings settings;
  settings.preview_resolution = preview_resolution_;
  settings.preview_cache_mb = preview_cache_mb_;
  return lapian::core::ResolvePreviewConfig(settings, preview_source_max_side_, 0)
      .max_side;
}

QString CacheSettingsDialog::PreviewEstimateText() const {
  const int max_side = PreviewMaxSide();
  const int frame_height = PreviewFrameHeightForMaxSide(max_side);
  const qint64 frame_bytes =
      static_cast<qint64>(max_side) * frame_height * 3 / 2;
  // fps：源 >1 时四舍五入到最近整数；未知/异常回退 30（计划 §3.3）。
  int fps = 30;
  if (preview_source_fps_ > 1.0) {
    fps = std::max(1, static_cast<int>(std::lround(preview_source_fps_)));
  }
  double seconds = 0.0;
  if (frame_bytes > 0 && fps > 0) {
    seconds = static_cast<double>(preview_cache_mb_) * 1024.0 * 1024.0 /
              static_cast<double>(frame_bytes) / static_cast<double>(fps);
  }
  return QStringLiteral("当前约可缓存 %1 秒（%2 / %3fps）")
      .arg(QString::number(seconds, 'f', 1), PreviewMaxSideLabel(max_side))
      .arg(fps);
}

QString CacheSettingsDialog::PreviewHintText() const {
  QStringList hints;
  if (preview_lifted_) {
    hints << QStringLiteral("%1 最低需要 %2MiB，已自动调整")
                 .arg(PreviewResolutionLabel(preview_resolution_))
                 .arg(lapian::core::PreviewCacheMinMbForResolution(
                     preview_resolution_));
  }
  // 源更小：不向上放大。auto 也走这条（实际输出仍受源限制），720P 是用户
  // 主动降采样，不提示。
  if (preview_source_max_side_ > 0 &&
      preview_resolution_ != lapian::core::PreviewResolution::k720p &&
      PreviewMaxSide() < 1920) {
    hints << QStringLiteral("源分辨率 %1P，实际预览仍为 %2P")
                 .arg(PreviewFrameHeightForMaxSide(preview_source_max_side_))
                 .arg(PreviewFrameHeightForMaxSide(PreviewMaxSide()));
  }
  if (preview_limited_by_memory_) {
    hints << QStringLiteral("已按可用内存 25% 限制为 %1 MiB")
                 .arg(preview_cache_mb_);
  }
  hints << QStringLiteral("修改后下次打开在线会话生效");
  return hints.join(QLatin1Char('\n'));
}

bool CacheSettingsDialog::PreviewHintIsWarning() const {
  if (preview_lifted_ || preview_limited_by_memory_) {
    return true;
  }
  return preview_source_max_side_ > 0 &&
         preview_resolution_ != lapian::core::PreviewResolution::k720p &&
         PreviewMaxSide() < 1920;
}

bool CacheSettingsDialog::PreviewCustomPresetSelected() const {
  return preview_custom_mode_ ||
         DerivePreviewPreset(preview_resolution_, preview_cache_mb_) ==
             PreviewPreset::kCustom;
}

lapian::core::PreviewResolution CacheSettingsDialog::PreviewResolutionForTest()
    const {
  return preview_resolution_;
}

int CacheSettingsDialog::PreviewCacheMbForTest() const {
  return preview_cache_mb_;
}

QString CacheSettingsDialog::PreviewEstimateTextForTest() const {
  return preview_estimate_label_ == nullptr ? QString()
                                            : preview_estimate_label_->text();
}

QString CacheSettingsDialog::PreviewHintTextForTest() const {
  return preview_hint_label_ == nullptr ? QString()
                                        : preview_hint_label_->text();
}

void CacheSettingsDialog::SetPreviewResolutionForTest(
    lapian::core::PreviewResolution resolution) {
  ApplyPreviewResolutionChange(resolution);
}

void CacheSettingsDialog::SetPreviewCacheMbForTest(int mb) {
  preview_cache_mb_ = std::clamp(mb, lapian::core::kPreviewCacheMinMb720p,
                                 lapian::core::kPreviewCacheMaxMb);
  preview_custom_mode_ = true;
  preview_lifted_ = false;
  preview_limited_by_memory_ = false;
  UpdatePreviewUi();
}

void CacheSettingsDialog::RestoreRecommendedForTest() { RestoreRecommended(); }

bool CacheSettingsDialog::PreviewControlsEnabledForTest() const {
  if (preview_resolution_combo_ == nullptr || preview_cache_combo_ == nullptr ||
      preview_cache_spin_ == nullptr || preview_restore_button_ == nullptr) {
    return false;
  }
  if (!preview_resolution_combo_->isEnabled() ||
      !preview_cache_combo_->isEnabled() ||
      !preview_restore_button_->isEnabled()) {
    return false;
  }
  return preview_cache_spin_->isEnabled() == PreviewCustomPresetSelected();
}

}  // namespace lapian::app
