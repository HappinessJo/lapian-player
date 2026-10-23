#include "online/QrLoginController.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <utility>

#include "online/QrCode.h"
#include "online/Sessdata.h"

namespace lapian::online {
namespace {

HttpRequest MakeQrRequest(const QString& url, int timeout_ms) {
  HttpRequest request;
  request.url = url;
  request.timeout_ms = timeout_ms;
  // 明确不带 SESSDATA：QR generate/poll 是匿名接口，Cookie 只发给 API。
  request.sessdata.clear();
  return request;
}

}  // namespace

QrLoginController::QrLoginController(IHttpClient* http,
                                     CredentialStore* credentials,
                                     QObject* parent)
    : QObject(parent), http_(http), credentials_(credentials) {
  poll_timer_ = new QTimer(this);
  poll_timer_->setInterval(poll_interval_ms_);
  connect(poll_timer_, &QTimer::timeout, this, &QrLoginController::PollOnce);

  expiry_timer_ = new QTimer(this);
  expiry_timer_->setSingleShot(true);
  connect(expiry_timer_, &QTimer::timeout, this, [this]() {
    if (session_.state() != QrLoginState::kWaiting &&
        session_.state() != QrLoginState::kScanned) {
      return;
    }
    StopTimers();
    session_.Expire();
    ClearQrImage();
    SetState(session_.state(), session_.message());
  });
}

QrLoginController::~QrLoginController() {
  Cancel();
  JoinWorker();
}

void QrLoginController::Start() {
  if (session_.state() == QrLoginState::kGenerating ||
      session_.state() == QrLoginState::kWaiting ||
      session_.state() == QrLoginState::kScanned ||
      session_.state() == QrLoginState::kVerifying) {
    return;
  }
  if (http_ == nullptr) {
    session_.Fail(QStringLiteral("网络客户端未初始化"));
    SetState(session_.state(), session_.message());
    emit loginFailed(session_.message());
    return;
  }
  JoinWorker();
  StopTimers();
  ++generation_;
  poll_errors_ = 0;
  job_active_ = true;
  session_.Reset();
  session_.Start();
  ClearQrImage();
  SetState(session_.state(), session_.message());
  StartGenerate();
}

void QrLoginController::Refresh() {
  switch (session_.state()) {
    case QrLoginState::kExpired:
    case QrLoginState::kFailed:
    case QrLoginState::kCancelled:
    case QrLoginState::kSuccess:
      Start();
      return;
    default:
      return;
  }
}

void QrLoginController::Cancel() {
  QElapsedTimer timer;
  timer.start();
  switch (session_.state()) {
    case QrLoginState::kGenerating:
    case QrLoginState::kWaiting:
    case QrLoginState::kScanned:
    case QrLoginState::kVerifying:
      break;
    default:
      return;
  }
  StopTimers();
  ++generation_;
  const std::shared_ptr<HttpCancelToken> token = cancel_;
  if (token != nullptr) {
    token->Cancel();
  }
  JoinWorker();
  session_.Cancel();
  ClearQrImage();
  SetState(session_.state(), session_.message());
  last_cancel_ms_ = timer.elapsed();
}

void QrLoginController::Close() {
  if (session_.state() == QrLoginState::kSuccess) {
    StopTimers();
    JoinWorker();
    return;
  }
  Cancel();
}

QrLoginState QrLoginController::state() const { return session_.state(); }

QString QrLoginController::message() const { return state_message_; }

QImage QrLoginController::qrImage() const { return qr_image_; }

AuthResult QrLoginController::authResult() const { return auth_result_; }

bool QrLoginController::IsBusy() const {
  return session_.state() == QrLoginState::kGenerating ||
         session_.state() == QrLoginState::kWaiting ||
         session_.state() == QrLoginState::kScanned ||
         session_.state() == QrLoginState::kVerifying;
}

void QrLoginController::SetPollIntervalForTest(int ms) {
  poll_interval_ms_ = std::clamp(ms, 20, 10000);
  if (poll_timer_ != nullptr) {
    poll_timer_->setInterval(poll_interval_ms_);
  }
}

void QrLoginController::SetExpiryMsForTest(int ms) {
  expiry_ms_ = std::clamp(ms, 100, kQrLoginExpiryMs * 10);
}

void QrLoginController::SetNavTimeoutForTest(int ms) {
  nav_timeout_ms_ = std::clamp(ms, 100, 10000);
}

void QrLoginController::StartGenerate() {
  const int generation = generation_;
  auto outcome = std::make_shared<Outcome>();
  outcome->generation = generation;
  auto token = std::make_shared<HttpCancelToken>();
  cancel_ = token;
  outcome_ = outcome;
  IHttpClient* http = http_;
  QrLoginController* self = this;

  worker_ = std::thread([self, http, token, outcome]() {
    if (http == nullptr) {
      std::lock_guard<std::mutex> lock(outcome->mutex);
      outcome->error = QStringLiteral("网络客户端未初始化");
      outcome->done = true;
      self->PostOutcome(outcome,
                        &QrLoginController::HandleGenerateOutcome);
      return;
    }
    if (token->IsCancelled()) {
      std::lock_guard<std::mutex> lock(outcome->mutex);
      outcome->cancelled = true;
      outcome->done = true;
      self->PostOutcome(outcome,
                        &QrLoginController::HandleGenerateOutcome);
      return;
    }
    const HttpResponse response =
        http->Get(MakeQrRequest(BuildQrGenerateUrl(), 6000), *token);
    {
      std::lock_guard<std::mutex> lock(outcome->mutex);
      outcome->http_status = response.http_status;
      if (response.cancelled || token->IsCancelled()) {
        outcome->cancelled = true;
      } else if (response.timed_out) {
        outcome->timed_out = true;
        outcome->error = QStringLiteral("网络超时，请重试");
      } else if (!response.ok) {
        outcome->error =
            response.http_status == 412 || response.http_status == 429
                ? QStringLiteral("二维码登录请求被拒绝，请稍后重试")
                : (response.error.isEmpty()
                       ? QStringLiteral("网络请求失败，请重试")
                       : response.error);
      } else {
        outcome->generate = ParseQrGenerateJson(response.body);
        outcome->ok = outcome->generate.ok;
        outcome->error = outcome->generate.error;
      }
      outcome->done = true;
    }
    self->PostOutcome(outcome, &QrLoginController::HandleGenerateOutcome);
  });
}

void QrLoginController::PollOnce() {
  if (job_active_ || http_ == nullptr) {
    return;
  }
  const QrLoginState state = session_.state();
  if (state != QrLoginState::kWaiting && state != QrLoginState::kScanned) {
    return;
  }
  const QString key = session_.qrcodeKey();
  const QString url = BuildQrPollUrl(key);
  if (url.isEmpty()) {
    StopTimers();
    session_.Fail(QStringLiteral("二维码 key 已失效"));
    SetState(session_.state(), session_.message());
    emit loginFailed(session_.message());
    return;
  }

  const int generation = generation_;
  auto outcome = std::make_shared<Outcome>();
  outcome->generation = generation;
  auto token = std::make_shared<HttpCancelToken>();
  cancel_ = token;
  outcome_ = outcome;
  IHttpClient* http = http_;
  QrLoginController* self = this;
  job_active_ = true;

  worker_ = std::thread([self, http, token, outcome, url]() {
    const HttpResponse response = http->Get(MakeQrRequest(url, 6000), *token);
    {
      std::lock_guard<std::mutex> lock(outcome->mutex);
      outcome->http_status = response.http_status;
      if (response.cancelled || token->IsCancelled()) {
        outcome->cancelled = true;
      } else if (response.timed_out) {
        outcome->timed_out = true;
        outcome->error = QStringLiteral("网络超时，请重试");
      } else if (!response.ok) {
        outcome->error =
            response.http_status == 412 || response.http_status == 429
                ? QStringLiteral("扫码请求被拒绝，请稍后重试")
                : (response.error.isEmpty()
                       ? QStringLiteral("网络请求失败，请重试")
                       : response.error);
      } else {
        outcome->poll =
            ParseQrPollJson(response.body, response.response_sessdata);
        outcome->ok = outcome->poll.status != QrPollStatus::kFailed;
        outcome->error = outcome->poll.error;
      }
      outcome->done = true;
    }
    self->PostOutcome(outcome, &QrLoginController::HandlePollOutcome);
  });
}

void QrLoginController::StartVerify(const QString& sessdata) {
  const int generation = generation_;
  auto outcome = std::make_shared<Outcome>();
  outcome->generation = generation;
  auto token = std::make_shared<HttpCancelToken>();
  cancel_ = token;
  outcome_ = outcome;
  IHttpClient* http = http_;
  CredentialStore* credentials = credentials_;
  const int nav_timeout_ms = nav_timeout_ms_;
  QrLoginController* self = this;
  QString secret = sessdata;
  job_active_ = true;

  worker_ = std::thread([self, http, credentials, token, outcome, secret,
                         nav_timeout_ms]() mutable {
    AuthResult nav;
    bool saved = false;
    QString save_error;
    if (http == nullptr) {
      nav.state = AuthState::kLoginUnavailable;
      nav.error = AuthError::kNetwork;
      nav.message = QStringLiteral("网络客户端未初始化");
    } else {
      nav = CheckNav(secret, http, *token, nav_timeout_ms);
    }
    if (token->IsCancelled()) {
      {
        std::lock_guard<std::mutex> lock(outcome->mutex);
        outcome->cancelled = true;
        outcome->done = true;
      }
      ClearSecret(&secret);
      self->PostOutcome(outcome, &QrLoginController::HandleVerifyOutcome);
      return;
    }
    if (nav.ok() && credentials != nullptr) {
      saved = credentials->Save(secret, &save_error);
    }
    ClearSecret(&secret);
    {
      std::lock_guard<std::mutex> lock(outcome->mutex);
      outcome->nav = nav;
      outcome->credential_saved = saved;
      outcome->save_error = save_error;
      outcome->ok = nav.ok() && saved;
      if (!outcome->ok) {
        outcome->error =
            nav.ok() ? (save_error.isEmpty()
                            ? QStringLiteral("无法保存登录凭据")
                            : save_error)
                     : (nav.message.isEmpty()
                            ? QStringLiteral("登录自检失败，请改用粘贴 SESSDATA")
                            : nav.message);
      }
      outcome->done = true;
    }
    self->PostOutcome(outcome, &QrLoginController::HandleVerifyOutcome);
  });
}

void QrLoginController::PostOutcome(
    const std::shared_ptr<Outcome>& outcome,
    void (QrLoginController::*handler)(const std::shared_ptr<Outcome>&)) {
  QMetaObject::invokeMethod(
      this,
      [this, outcome, handler]() { (this->*handler)(outcome); },
      Qt::QueuedConnection);
}

void QrLoginController::HandleGenerateOutcome(
    const std::shared_ptr<Outcome>& outcome) {
  JoinWorker();
  job_active_ = false;
  if (!IsCurrent(outcome)) {
    return;
  }
  if (outcome->cancelled) {
    return;
  }
  if (!outcome->ok) {
    session_.OnGenerated(QrGenerateResult{false, {}, outcome->error, 0});
    ClearQrImage();
    SetState(session_.state(), session_.message());
    emit loginFailed(session_.message());
    return;
  }
  session_.OnGenerated(outcome->generate);
  if (session_.state() != QrLoginState::kWaiting) {
    ClearQrImage();
    SetState(session_.state(), session_.message());
    emit loginFailed(session_.message());
    return;
  }
  const QrImageResult rendered = RenderQrImage(session_.qrUrl(), 6, 4);
  if (!rendered.ok()) {
    session_.Fail(rendered.error);
    ClearQrImage();
    SetState(session_.state(), session_.message());
    emit loginFailed(session_.message());
    return;
  }
  qr_image_ = rendered.image;
  emit qrImageChanged(qr_image_);
  SetState(session_.state(), session_.message());
  poll_timer_->start(poll_interval_ms_);
  expiry_timer_->start(expiry_ms_);
}

void QrLoginController::HandlePollOutcome(
    const std::shared_ptr<Outcome>& outcome) {
  JoinWorker();
  job_active_ = false;
  if (!IsCurrent(outcome)) {
    return;
  }
  if (outcome->cancelled) {
    return;
  }

  if (!outcome->ok) {
    const bool rate_limited =
        outcome->http_status == 412 || outcome->http_status == 429;
    if (rate_limited) {
      StopTimers();
      session_.Fail(outcome->error);
      ClearQrImage();
      SetState(session_.state(), session_.message());
      emit loginFailed(session_.message());
      return;
    }
    ++poll_errors_;
    if (poll_errors_ >= 3) {
      StopTimers();
      session_.Fail(outcome->error.isEmpty()
                        ? QStringLiteral("二维码登录网络异常，请重试")
                        : outcome->error);
      ClearQrImage();
      SetState(session_.state(), session_.message());
      emit loginFailed(session_.message());
      return;
    }
    SetState(session_.state(),
             QStringLiteral("网络异常，继续等待…（%1/3）").arg(poll_errors_));
    return;
  }
  poll_errors_ = 0;

  switch (outcome->poll.status) {
    case QrPollStatus::kWaiting:
      if (session_.state() != QrLoginState::kScanned) {
        session_.OnPolled(outcome->poll);
        SetState(session_.state(), session_.message());
      }
      return;
    case QrPollStatus::kScanned:
      session_.OnPolled(outcome->poll);
      SetState(session_.state(), session_.message());
      return;
    case QrPollStatus::kExpired:
      StopTimers();
      session_.OnPolled(outcome->poll);
      ClearQrImage();
      SetState(session_.state(), session_.message());
      return;
    case QrPollStatus::kFailed:
      StopTimers();
      session_.OnPolled(outcome->poll);
      ClearQrImage();
      SetState(session_.state(), session_.message());
      emit loginFailed(session_.message());
      return;
    case QrPollStatus::kSuccess:
      StopTimers();
      ClearQrImage();
      session_.OnPolled(outcome->poll);
      SetState(session_.state(), session_.message());
      if (session_.state() == QrLoginState::kVerifying) {
        QString sessdata = session_.sessdata();
        StartVerify(sessdata);
        ClearSecret(&sessdata);
      }
      return;
  }
}

void QrLoginController::HandleVerifyOutcome(
    const std::shared_ptr<Outcome>& outcome) {
  JoinWorker();
  job_active_ = false;
  if (!IsCurrent(outcome)) {
    return;
  }
  if (outcome->cancelled) {
    return;
  }
  if (outcome->nav.ok() && outcome->credential_saved) {
    session_.OnVerified(true, QString());
    session_.ClearSessdata();
    auth_result_ = outcome->nav;
    SetState(QrLoginState::kSuccess, QStringLiteral("已登录"));
    emit loginSucceeded();
    return;
  }
  const QString message =
      outcome->nav.ok() ? outcome->error
                        : (outcome->nav.message.isEmpty()
                               ? QStringLiteral("登录自检失败，请改用粘贴 SESSDATA")
                               : outcome->nav.message);
  session_.OnVerified(false, message);
  SetState(session_.state(), session_.message());
  emit loginFailed(session_.message());
}

void QrLoginController::SetState(QrLoginState state, const QString& message) {
  state_message_ = message;
  emit stateChanged(static_cast<int>(state), message);
}

void QrLoginController::StopTimers() {
  if (poll_timer_ != nullptr) {
    poll_timer_->stop();
  }
  if (expiry_timer_ != nullptr) {
    expiry_timer_->stop();
  }
}

void QrLoginController::JoinWorker() {
  if (worker_.joinable()) {
    worker_.join();
  }
}

void QrLoginController::ClearQrImage() {
  qr_image_ = QImage();
  emit qrImageChanged(qr_image_);
}

bool QrLoginController::IsCurrent(
    const std::shared_ptr<Outcome>& outcome) const {
  return outcome != nullptr && outcome->generation == generation_ &&
         session_.state() != QrLoginState::kCancelled &&
         session_.state() != QrLoginState::kIdle;
}

}  // namespace lapian::online
