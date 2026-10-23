#pragma once

// T8.3.1 设置对话框：在线缓存目录/配额、占用、打开目录、清理孤儿、清空缓存。
//
// 只做 UI 编排；真正的目录校验/清空/清理由 MainWindow 回调（CacheManager +
// CacheStore）执行，便于探针在无窗口环境复用同一套核心逻辑。

#include <QDialog>
#include <QString>

#include <functional>
#include <memory>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

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
    std::function<bool(const QString& root, int quota_gb, QString* error)> apply;
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

  lapian::online::CacheStore* cache_ = nullptr;
  std::unique_ptr<lapian::online::CacheManager> manager_;
  Callbacks callbacks_;
  QString pending_root_;
  bool session_active_ = false;
  QString session_description_;

  QLineEdit* root_edit_ = nullptr;
  QPushButton* browse_button_ = nullptr;
  QPushButton* default_button_ = nullptr;
  QLabel* root_hint_label_ = nullptr;
  QLabel* root_state_label_ = nullptr;
  QSpinBox* quota_spin_ = nullptr;
  QLabel* usage_label_ = nullptr;
  QLabel* free_label_ = nullptr;
  QLabel* records_label_ = nullptr;
  QLabel* session_label_ = nullptr;
  QPushButton* open_button_ = nullptr;
  QPushButton* cleanup_button_ = nullptr;
  QPushButton* clear_button_ = nullptr;
  QPushButton* save_button_ = nullptr;
  QPushButton* close_button_ = nullptr;
  QLabel* status_label_ = nullptr;
};

}  // namespace lapian::app
