#pragma once

// T8.3.1 设置对话框：在线缓存目录/配额、占用、打开目录、清理孤儿、清空缓存。
//
// 只做 UI 编排；真正的目录校验/清空/清理由 MainWindow 回调（CacheManager +
// CacheStore）执行，便于探针在无窗口环境复用同一套核心逻辑。
//
// D3（2026-10-27）：同一入口加“拖动预览”分组（分辨率/缓存挡位/自定义/
// 估算/自动修正提示）。保存仍走同一个 apply 回调，MainWindow 一次
// SettingsStore::Save 同时写 cache 与 preview 键；拖动预览下次在线会话生效。

#include <QDialog>
#include <QString>

#include <functional>
#include <memory>

#include "core/AppSettings.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QShowEvent;
class QSpinBox;
class QWidget;

namespace lapian::online {
class CacheManager;
class CacheStore;
}  // namespace lapian::online

namespace lapian::app {

class CacheSettingsDialog : public QDialog {
  Q_OBJECT

 public:
  struct Callbacks {
    // 返回 false 时 error 为可读原因；成功后产品侧负责写 settings/cache。
    // D3：一次保存同时写 cache 与 preview 键（单次 SettingsStore::Save）；
    // preview_cache_mb 已在对话框内按 D1 纯函数抬下限/夹上限/夹可用内存 25%。
    std::function<bool(const QString& root, int quota_gb,
                       lapian::core::PreviewResolution preview_resolution,
                       int preview_cache_mb, QString* error)> apply;
    // keep_current 由 MainWindow 按在线会话决定；kept_entries 返回保留条数。
    std::function<bool(QString* error, qint64* removed_bytes, int* kept_entries)>
        clear;
    std::function<bool(QString* error, qint64* removed_bytes,
                       int* removed_entries)>
        cleanup;
    std::function<bool(QString* error)> open_directory;
  };

  CacheSettingsDialog(lapian::online::CacheStore* cache,
                      QString records_root, bool session_active,
                      QString session_description, Callbacks callbacks,
                      QWidget* parent = nullptr);
  ~CacheSettingsDialog() override;

  void Refresh();
  void SetSessionActive(bool active, const QString& description = QString());

  // D3：拖动预览上下文。source_max_side = max(源宽,源高)，source_fps 是源
  // 标称帧率，available_memory_mb 是当前可用物理内存；未知一律传 0。
  // MainWindow 在 exec() 前按当前会话/系统填；任何时候都可调用（只影响
  // 估算与提示，不改变已选值）。
  void SetPreviewContext(int source_max_side, double source_fps,
                         int available_memory_mb);
  // D3：已保存的预览设置。MainWindow 在 exec() 前用 loaded_settings_ 填；
  // 不调用时默认 auto/512（新建对话框的默认值）。
  void SetPreviewSettings(lapian::core::PreviewResolution resolution,
                          int cache_mb);

  // 自动化 hooks（--online-cache-manage-test --ui），不改变产品语义。
  QString RootTextForTest() const;
  int QuotaGbForTest() const;
  QString UsageTextForTest() const;
  QString RecordsTextForTest() const;
  QString StatusTextForTest() const;
  bool SaveEnabledForTest() const;
  bool ClearEnabledForTest() const;
  bool QuotaEnabledForTest() const;
  void SetPendingRootForTest(const QString& root);
  void SetQuotaForTest(int quota_gb);
  bool ApplyForTest(QString* error);
  void ClearWithoutConfirmForTest();
  void CleanupForTest();
  void OpenDirectoryForTest();

  // D3：拖动预览 hooks（--online-cache-manage-test --ui）。
  lapian::core::PreviewResolution PreviewResolutionForTest() const;
  int PreviewCacheMbForTest() const;
  QString PreviewEstimateTextForTest() const;
  QString PreviewHintTextForTest() const;
  void SetPreviewResolutionForTest(
      lapian::core::PreviewResolution resolution);
  void SetPreviewCacheMbForTest(int mb);
  void RestoreRecommendedForTest();
  // 分辨率/缓存/恢复按钮均可用，且自定义 spin 的可用状态与挡位一致。
  bool PreviewControlsEnabledForTest() const;
  // D3b：当前窗口尺寸与可用屏幕（w/h/avail_w/avail_h/scroll）。
  QString DialogSizeForTest() const;
  // D3b：自然尺寸 = 滚动内容 + 固定 footer；QScrollArea 不会把它传给顶层窗口。
  QSize sizeHint() const override;

 protected:
  // D3b：显示时按内容 adjustSize，再夹到可用屏幕的 90%（超出交给滚动区）。
  void showEvent(QShowEvent* event) override;

 private:
  void BuildUi();
  void ChooseDirectory();
  void ResetToDefault();
  bool ApplyInternal(bool close_on_success, QString* error);
  void PerformClear(bool confirm);
  void PerformCleanup();
  void OpenDirectory();
  void SetStatus(const QString& text, bool error);
  void UpdatePendingUi();
  void UpdateSummaryUi();
  void AdjustToScreen();
  QRect AvailableScreenRect() const;

  // D3：拖动预览。UpdatePreviewUi 统一同步下拉/数值/估算/提示；保存前
  // ApplyPreviewClamp 用 D1 纯函数修正非法组合并更新提示。
  void UpdatePreviewUi();
  int ApplyPreviewClamp();
  void ApplyPreviewResolutionChange(lapian::core::PreviewResolution resolution);
  void OnPreviewResolutionChanged();
  void OnPreviewPresetChanged();
  void RestoreRecommended();
  int PreviewMaxSide() const;
  QString PreviewEstimateText() const;
  QString PreviewHintText() const;
  bool PreviewHintIsWarning() const;
  bool PreviewCustomPresetSelected() const;

  lapian::online::CacheStore* cache_ = nullptr;
  std::unique_ptr<lapian::online::CacheManager> manager_;
  Callbacks callbacks_;
  QString pending_root_;
  bool session_active_ = false;
  QString session_description_;

  // D3：当前选择与上下文。preview_custom_mode_ 记住用户是否显式选了
  // “高级自定义”（避免 pair 恰好等于预设值时被反推回预设、spin 被禁用）。
  lapian::core::PreviewResolution preview_resolution_ =
      lapian::core::PreviewResolution::kAuto;
  int preview_cache_mb_ = lapian::core::kPreviewCacheDefaultMb;
  bool preview_custom_mode_ = false;
  bool preview_lifted_ = false;
  bool preview_limited_by_memory_ = false;
  int preview_source_max_side_ = 0;
  double preview_source_fps_ = 0.0;
  int preview_available_memory_mb_ = 0;

  QLineEdit* root_edit_ = nullptr;
  QScrollArea* scroll_area_ = nullptr;
  QWidget* scroll_content_ = nullptr;
  QWidget* footer_ = nullptr;
  QPushButton* browse_button_ = nullptr;
  QPushButton* default_button_ = nullptr;
  QLabel* root_hint_label_ = nullptr;
  QLabel* root_state_label_ = nullptr;
  QSpinBox* quota_spin_ = nullptr;
  QLabel* usage_label_ = nullptr;
  QLabel* free_label_ = nullptr;
  QLabel* records_label_ = nullptr;
  QLabel* session_label_ = nullptr;
  QComboBox* preview_resolution_combo_ = nullptr;
  QComboBox* preview_cache_combo_ = nullptr;
  QSpinBox* preview_cache_spin_ = nullptr;
  QLabel* preview_estimate_label_ = nullptr;
  QLabel* preview_hint_label_ = nullptr;
  QPushButton* preview_restore_button_ = nullptr;
  QPushButton* open_button_ = nullptr;
  QPushButton* cleanup_button_ = nullptr;
  QPushButton* clear_button_ = nullptr;
  QPushButton* save_button_ = nullptr;
  QPushButton* close_button_ = nullptr;
  QLabel* status_label_ = nullptr;
};

}  // namespace lapian::app
