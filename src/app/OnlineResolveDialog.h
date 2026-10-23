#pragma once

// T8.1 在线解析对话框：输入 BV/URL → 本地校验 → 匿名 view → 回显确认。
// 本任务不取流、不播放、不缓存；确认只发信号，T8.5 再接“开始取流”。

#include <QDialog>
#include <QSize>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "online/BiliAuth.h"
#include "online/BiliResolver.h"
#include "online/CredentialStore.h"

class QCloseEvent;
class QComboBox;
class QGroupBox;
class QImage;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QVBoxLayout;
class QWidget;

namespace lapian::online {
class HttpCancelToken;
class IHttpClient;
class QrLoginController;
}  // namespace lapian::online

// T8.6：确认时把回显的标题/UP/时长一并交给会话（仍以 bvid/cid/page 为主键）。
struct OnlineConfirmInfo {
  QString bvid;
  qint64 cid = 0;
  int page = 0;
  QString title;
  QString owner;
  qint64 duration_ms = 0;
  QString page_title;
};

// T8.7：解析前先查本地完整缓存；命中则不发 view/playurl 请求。
struct OnlineCacheSummary {
  QString bvid;
  qint64 cid = 0;
  QString title;
  QString owner;
  qint64 duration_ms = 0;
  int qn = 0;
  int codecid = 0;
  double fps = 0.0;
};

class OnlineResolveDialog : public QDialog {
  Q_OBJECT

 public:
  explicit OnlineResolveDialog(QWidget* parent = nullptr);
  ~OnlineResolveDialog() override;

  // 产品入口：MainWindow 的 Ctrl+U 调用 OpenOnlineDialog()。
  void StartResolve();

  // T8.7：完整缓存探测（不联网）。命中时直接回显缓存元数据，确认后由
  // OnlineSession 的 OpenComplete 路径打开；探测失败/未命中才走 view。
  using CacheProbe =
      std::function<bool(const QString& bvid, OnlineCacheSummary* summary,
                         QString* error)>;
  void SetCacheProbe(CacheProbe probe) { cache_probe_ = std::move(probe); }
  bool CacheHitForTest() const { return last_cache_hit_; }

  // 测试入口（不绕过产品路径）：注入 fake HTTP、同步等待、读取回显字段。
  void SetHttpClientForTest(std::unique_ptr<lapian::online::IHttpClient> client);
  void SetInputForTest(const QString& text);
  void StartResolveForTest() { StartResolve(); }
  bool WaitForResolveForTest(int timeout_ms);
  void SelectPageForTest(int index);
  void ClickConfirmForTest();
  void CloseForTest();
  qint64 CancelAndCloseForTest();
  qint64 CancelForTest();
  bool IsResolvingForTest() const { return resolving_; }
  bool HasWorkerThreadForTest() const { return worker_.joinable(); }
  bool HasResultForTest() const { return has_video_; }
  bool ConfirmEnabledForTest() const;
  bool ConfirmedForTest() const { return confirmed_; }
  QString InputForTest() const;
  QString StatusTextForTest() const;
  QString ErrorTextForTest() const;
  QString TitleTextForTest() const;
  QString OwnerTextForTest() const;
  QString DurationTextForTest() const;
  QString TotalDurationTextForTest() const;
  QString CurrentPageTextForTest() const;
  QString QualityHintForTest() const;
  int PageCountForTest() const;
  int SelectedPageIndexForTest() const;
  QString PageLabelAtForTest(int index) const;
  // T8.6：产品确认路径读取回显元数据；无结果时 bvid 为空。
  OnlineConfirmInfo ConfirmInfo() const;
  int LastHttpRequestsForTest() const { return last_http_requests_; }
  int TotalHttpRequestsForTest() const { return total_http_requests_; }
  qint64 LastLocalValidationMsForTest() const { return last_local_validation_ms_; }
  qint64 LastResolveMsForTest() const { return last_resolve_ms_; }
  qint64 LastCancelMsForTest() const { return last_cancel_ms_; }

  // T8.2 凭据/自检测试入口（不绕过产品路径：仍走 Save/Clear/StartResolve）。
  void SetCredentialPathForTest(const QString& path);
  bool SaveCredentialForTest(const QString& text);
  bool ClearCredentialForTest();
  bool HasCredentialForTest() const { return auth_.has_credential(); }
  QString AuthStatusTextForTest() const;
  QString AuthStateForTest() const;
  bool AuthCheckingForTest() const { return auth_checking_; }
  int NavRequestCountForTest() const { return nav_request_count_; }
  bool WaitForAuthForTest(int timeout_ms);
  bool PrivacyNoticeVisibleForTest() const;
  QString PrivacyNoticeTextForTest() const;
  void HidePrivacyNoticeForTest();
  void SetAuthTimeoutForTest(int timeout_ms);

  // T8.2.2 扫码登录测试入口（仍走产品控件/控制器）。
  void StartQrLoginForTest();
  void SetQrPollIntervalForTest(int ms);
  void SetQrExpiryForTest(int ms);
  QString QrStateForTest() const;
  QString QrStatusTextForTest() const;
  QSize QrImageSizeForTest() const;
  bool QrPanelVisibleForTest() const;
  qint64 CancelQrForTest();

  // 主窗口关窗/析构时调用：取消并 join 工作线程，不留下挂起请求。
  void Shutdown();

 signals:
  void confirmed(const QString& bvid, qint64 cid, int page);
  void resolveCompleted(bool ok);

 protected:
  void closeEvent(QCloseEvent* event) override;

 private slots:
  void PollResolve();
  void OnPageChanged(int index);
  void OnConfirm();
  void CancelResolve();
  void OnSaveCredential();
  void OnClearCredential();
  void OnHidePrivacy();
  void OnStartQrLogin();
  void OnQrStateChanged(int state, const QString& message);
  void OnQrImageChanged(const QImage& image);
  void OnQrLoginSucceeded();
  void OnQrRefresh();
  void OnQrCancel();

 private:
  struct ResolveOutcome {
    std::mutex mutex;
    bool done = false;
    lapian::online::BiliVideo video;
    lapian::online::BiliResolveError error;
    int http_requests = 0;
    qint64 elapsed_ms = 0;
  };

  struct AuthOutcome {
    std::mutex mutex;
    bool done = false;
    std::uint64_t generation = 0;
    lapian::online::AuthResult result;
  };

  void SetupUi();
  void SetupCredentialUi(QVBoxLayout* root);
  void ResetResult();
  void SetBusy(bool busy);
  void ShowResult(const lapian::online::BiliVideo& video);
  void ShowError(const lapian::online::BiliResolveError& error);
  void UpdateSelectedPage();
  void UpdateConfirmState();
  void ReloadCredentialState();
  void UpdateAuthUi();
  void UpdateQualityHint();
  void StartAuthCheck();
  void PollAuth();
  void CancelAuthCheck();
  void JoinAuthWorker();
  bool SaveCredentialText(const QString& text, QString* error);
  bool ClearCredentialNow();
  void EnsureQrController();

  QLineEdit* input_ = nullptr;
  QPushButton* resolve_button_ = nullptr;
  QPushButton* confirm_button_ = nullptr;
  QPushButton* cancel_button_ = nullptr;
  QComboBox* page_combo_ = nullptr;
  QLabel* status_label_ = nullptr;
  QLabel* error_label_ = nullptr;
  QLabel* title_label_ = nullptr;
  QLabel* owner_label_ = nullptr;
  QLabel* total_duration_label_ = nullptr;
  QLabel* page_info_label_ = nullptr;
  QLabel* page_duration_label_ = nullptr;
  QLabel* quality_label_ = nullptr;
  QTimer* poll_timer_ = nullptr;

  // T8.2 最小凭据 UI：粘贴 SESSDATA、保存、清除、状态；不做账号体系。
  QLineEdit* credential_input_ = nullptr;
  QPushButton* credential_save_button_ = nullptr;
  QPushButton* credential_clear_button_ = nullptr;
  QLabel* auth_status_label_ = nullptr;
  QLabel* privacy_label_ = nullptr;
  QPushButton* privacy_hide_button_ = nullptr;

  // T8.2.2 扫码登录：粘贴入口永久保留；这是并存的第二条入口。
  QPushButton* qr_start_button_ = nullptr;
  QWidget* qr_panel_ = nullptr;
  QLabel* qr_image_label_ = nullptr;
  QLabel* qr_status_label_ = nullptr;
  QPushButton* qr_refresh_button_ = nullptr;
  QPushButton* qr_cancel_button_ = nullptr;
  std::unique_ptr<lapian::online::QrLoginController> qr_controller_;

  std::unique_ptr<lapian::online::IHttpClient> http_;
  std::shared_ptr<lapian::online::HttpCancelToken> cancel_;
  std::shared_ptr<ResolveOutcome> outcome_;
  std::thread worker_;
  CacheProbe cache_probe_;
  bool last_cache_hit_ = false;

  lapian::online::CredentialStore credentials_;
  lapian::online::BiliAuthSession auth_;
  std::shared_ptr<lapian::online::HttpCancelToken> auth_cancel_;
  std::shared_ptr<AuthOutcome> auth_outcome_;
  std::thread auth_worker_;
  bool auth_checking_ = false;
  bool credential_corrupt_ = false;
  int auth_timeout_ms_ = 2000;
  int nav_request_count_ = 0;
  std::uint64_t auth_generation_ = 0;

  lapian::online::BiliVideo video_;
  bool has_video_ = false;
  bool resolving_ = false;
  bool confirmed_ = false;
  qint64 last_local_validation_ms_ = 0;
  qint64 last_resolve_ms_ = 0;
  qint64 last_cancel_ms_ = -1;
  int last_http_requests_ = 0;
  int total_http_requests_ = 0;
};
