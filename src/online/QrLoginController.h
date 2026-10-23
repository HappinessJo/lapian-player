#pragma once

// T8.2.2 扫码登录控制器：worker 线程做 generate/poll/nav，UI 线程用 QTimer
// 每 1.5s 轮询一次；180s 过期；可取消；成功只保留 SESSDATA（DPAPI）+ nav 通过。
//
// 线程/UI 约定：
// - 所有公开方法只在 UI 线程调用；worker 结果通过 queued invoke 回到 UI 线程；
// - Cancel 置 HttpCancelToken 后 join，目标 ≤100ms（WinHTTP 取消轮询 20ms）；
// - 成功前不显示已登录：poll 只到 kVerifying，nav 通过才是 kSuccess；
// - 失败/过期/取消保留粘贴入口（本类不碰 OnlineResolveDialog 的粘贴控件）；
// - 不后台轮询：只有用户 Start/Refresh 后才请求。

#include <QImage>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "online/BiliAuth.h"
#include "online/CredentialStore.h"
#include "online/IHttpClient.h"
#include "online/QrLogin.h"

class QTimer;

namespace lapian::online {

class QrLoginController : public QObject {
  Q_OBJECT

 public:
  QrLoginController(IHttpClient* http, CredentialStore* credentials,
                    QObject* parent = nullptr);
  ~QrLoginController() override;

  QrLoginController(const QrLoginController&) = delete;
  QrLoginController& operator=(const QrLoginController&) = delete;

  void Start();
  void Refresh();
  // 取消是终态：≤100ms 回到可操作；保留粘贴入口；成功后 Cancel 不撤销登录。
  void Cancel();
  // 对话框关闭：未成功则 Cancel；已成功保留凭据。
  void Close();

  QrLoginState state() const;
  QString message() const;
  QImage qrImage() const;
  AuthResult authResult() const;
  bool IsBusy() const;
  qint64 lastCancelMsForTest() const { return last_cancel_ms_; }

  // 测试入口：缩短轮询/过期/自检超时；不改变产品默认值。
  void SetPollIntervalForTest(int ms);
  void SetExpiryMsForTest(int ms);
  void SetNavTimeoutForTest(int ms);

  // 测试/证据：二维码内容与 key（不是 SESSDATA）；不写日志。
  QString qrContentForTest() const { return session_.qrUrl(); }
  QString qrKeyForTest() const { return session_.qrcodeKey(); }

 signals:
  // state 用 int 传输（QrLoginState 的整数值），方便跨线程/测试连接。
  void stateChanged(int state, const QString& message);
  void qrImageChanged(const QImage& image);
  void loginSucceeded();
  void loginFailed(const QString& message);

 private:
  struct Outcome {
    std::mutex mutex;
    bool done = false;
    int generation = 0;
    bool ok = false;
    bool cancelled = false;
    bool timed_out = false;
    int http_status = 0;
    QString error;
    QrGenerateResult generate;
    QrPollResult poll;
    AuthResult nav;
    bool credential_saved = false;
    QString save_error;
  };

  void StartGenerate();
  void PollOnce();
  void StartVerify(const QString& sessdata);
  void HandleGenerateOutcome(const std::shared_ptr<Outcome>& outcome);
  void HandlePollOutcome(const std::shared_ptr<Outcome>& outcome);
  void HandleVerifyOutcome(const std::shared_ptr<Outcome>& outcome);
  void PostOutcome(const std::shared_ptr<Outcome>& outcome,
                   void (QrLoginController::*handler)(
                       const std::shared_ptr<Outcome>&));
  void SetState(QrLoginState state, const QString& message);
  void StopTimers();
  void JoinWorker();
  void ClearQrImage();
  bool IsCurrent(const std::shared_ptr<Outcome>& outcome) const;

  IHttpClient* http_ = nullptr;
  CredentialStore* credentials_ = nullptr;
  QrLoginSession session_;
  QImage qr_image_;
  AuthResult auth_result_;
  QString state_message_;

  QTimer* poll_timer_ = nullptr;
  QTimer* expiry_timer_ = nullptr;
  int poll_interval_ms_ = kQrPollIntervalMs;
  int expiry_ms_ = kQrLoginExpiryMs;
  int nav_timeout_ms_ = 2000;

  std::thread worker_;
  std::shared_ptr<HttpCancelToken> cancel_;
  std::shared_ptr<Outcome> outcome_;
  int generation_ = 0;
  int poll_errors_ = 0;
  bool job_active_ = false;
  qint64 last_cancel_ms_ = -1;
};

}  // namespace lapian::online
