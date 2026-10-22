#include "online/BiliAuth.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include "online/Sessdata.h"

namespace lapian::online {
namespace {

QString SanitizeApiMessage(const QString& message) {
  QString text = message;
  text.remove(QLatin1Char('\r'));
  text.remove(QLatin1Char('\n'));
  text = text.trimmed();
  if (text.size() > 120) {
    text = text.left(120) + QStringLiteral("…");
  }
  return text;
}

QString WithAnonymousSuffix(const QString& base) {
  if (base.contains(QStringLiteral("将按未登录"))) {
    return base;
  }
  return base + QStringLiteral("；将按未登录清晰度播放");
}

AuthResult MakeFailure(AuthError error, const QString& message,
                       const QString& detail = QString()) {
  AuthResult result;
  result.state = AuthState::kLoginUnavailable;
  result.error = error;
  result.message = message;
  result.detail = detail;
  return result;
}

AuthResult HttpFailure(int http_status, qint64 elapsed_ms) {
  AuthResult result = MakeFailure(
      AuthError::kHttp, QString(), QStringLiteral("nav_http_status=%1").arg(http_status));
  result.http_status = http_status;
  result.elapsed_ms = elapsed_ms;
  if (http_status == 412 || http_status == 429) {
    result.message = QStringLiteral(
                         "登录检查被拒绝（HTTP %1，可能触发风控/限频）；"
                         "将按未登录清晰度播放，请稍后重试")
                         .arg(http_status);
  } else {
    result.message = QStringLiteral("登录检查失败（HTTP %1）；将按未登录清晰度播放")
                         .arg(http_status);
  }
  return result;
}

}  // namespace

QString BuildNavUrl() {
  return QStringLiteral("https://api.bilibili.com/x/web-interface/nav");
}

QString AuthStateName(AuthState state) {
  switch (state) {
    case AuthState::kAnonymous:
      return QStringLiteral("anonymous");
    case AuthState::kNotChecked:
      return QStringLiteral("not_checked");
    case AuthState::kChecking:
      return QStringLiteral("checking");
    case AuthState::kLoggedIn:
      return QStringLiteral("logged_in");
    case AuthState::kLoginUnavailable:
      return QStringLiteral("login_unavailable");
  }
  return QStringLiteral("unknown");
}

QString AuthErrorName(AuthError error) {
  switch (error) {
    case AuthError::kNone:
      return QStringLiteral("none");
    case AuthError::kNoCredential:
      return QStringLiteral("no_credential");
    case AuthError::kInvalidCredential:
      return QStringLiteral("invalid_credential");
    case AuthError::kApi:
      return QStringLiteral("api");
    case AuthError::kHttp:
      return QStringLiteral("http");
    case AuthError::kNetwork:
      return QStringLiteral("network");
    case AuthError::kTimeout:
      return QStringLiteral("timeout");
    case AuthError::kCancelled:
      return QStringLiteral("cancelled");
    case AuthError::kParse:
      return QStringLiteral("parse");
  }
  return QStringLiteral("unknown");
}

AuthResult ParseNavJson(const QByteArray& body, int http_status,
                        qint64 elapsed_ms) {
  if (http_status < 200 || http_status >= 300) {
    return HttpFailure(http_status, elapsed_ms);
  }

  QJsonParseError parse_error{};
  const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
  if (document.isNull() || !document.isObject()) {
    AuthResult result = MakeFailure(
        AuthError::kParse,
        QStringLiteral("登录检查响应无法解析；将按未登录清晰度播放"),
        QStringLiteral("nav_parse_error=%1").arg(parse_error.error));
    result.http_status = http_status;
    result.elapsed_ms = elapsed_ms;
    return result;
  }

  const QJsonObject root = document.object();
  const int code = root.value(QStringLiteral("code")).toInt(-1);
  const QString api_message =
      SanitizeApiMessage(root.value(QStringLiteral("message")).toString());
  const QJsonObject data = root.value(QStringLiteral("data")).toObject();
  const bool is_login = data.value(QStringLiteral("isLogin")).toBool(false);
  const QJsonValue vip_value = data.value(QStringLiteral("vipStatus"));
  const int vip_status = vip_value.isDouble() ? vip_value.toInt(-1) : -1;

  if (code == 0 && is_login) {
    AuthResult result;
    result.state = AuthState::kLoggedIn;
    result.is_login = true;
    result.api_code = 0;
    result.http_status = http_status;
    result.vip_status = vip_status;
    result.elapsed_ms = elapsed_ms;
    result.message = QStringLiteral("已登录");
    result.detail = QStringLiteral("nav_ok code=0 vip_status=%1").arg(vip_status);
    return result;
  }

  if (code == -101 || (code == 0 && !is_login)) {
    AuthResult result = MakeFailure(
        AuthError::kApi,
        QStringLiteral("登录不可用/凭据失效；将按未登录清晰度播放"),
        QStringLiteral("nav_api_code=%1 is_login=0").arg(code));
    result.api_code = code;
    result.http_status = http_status;
    result.vip_status = vip_status;
    result.elapsed_ms = elapsed_ms;
    return result;
  }

  AuthResult result = MakeFailure(
      AuthError::kApi, QString(),
      QStringLiteral("nav_api_code=%1 http_status=%2").arg(code).arg(http_status));
  if (api_message.isEmpty()) {
    result.message = QStringLiteral("登录检查失败（接口 code %1）；将按未登录清晰度播放")
                         .arg(code);
  } else {
    result.message =
        QStringLiteral("登录检查失败（接口 code %1：%2）；将按未登录清晰度播放")
            .arg(code)
            .arg(api_message);
  }
  result.api_code = code;
  result.http_status = http_status;
  result.elapsed_ms = elapsed_ms;
  return result;
}

AuthResult CheckNav(const QString& sessdata, IHttpClient* http,
                    const HttpCancelToken& cancel, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();

  if (sessdata.isEmpty()) {
    AuthResult result = MakeFailure(AuthError::kNoCredential,
                                    QStringLiteral("未配置凭据"));
    result.state = AuthState::kAnonymous;
    result.elapsed_ms = timer.elapsed();
    return result;
  }
  if (!IsSafeSessdataValue(sessdata)) {
    AuthResult result = MakeFailure(
        AuthError::kInvalidCredential,
        QStringLiteral("SESSDATA 格式无效，请重新保存凭据；将按未登录清晰度播放"));
    result.elapsed_ms = timer.elapsed();
    return result;
  }
  if (http == nullptr) {
    AuthResult result = MakeFailure(
        AuthError::kNetwork,
        QStringLiteral("网络客户端未初始化；将按未登录清晰度播放"));
    result.elapsed_ms = timer.elapsed();
    return result;
  }
  if (cancel.IsCancelled()) {
    AuthResult result = MakeFailure(AuthError::kCancelled,
                                    QStringLiteral("登录检查已取消"));
    result.elapsed_ms = timer.elapsed();
    return result;
  }

  HttpRequest request;
  request.url = BuildNavUrl();
  request.timeout_ms = timeout_ms > 0 ? timeout_ms : 2000;
  request.sessdata = sessdata;
  const HttpResponse response = http->Get(request, cancel);

  if (cancel.IsCancelled() || response.cancelled) {
    AuthResult result = MakeFailure(AuthError::kCancelled,
                                    QStringLiteral("登录检查已取消"));
    result.http_status = response.http_status;
    result.elapsed_ms = response.elapsed_ms;
    result.request_cookie_sent = response.request_cookie_sent;
    return result;
  }
  if (response.timed_out) {
    AuthResult result = MakeFailure(
        AuthError::kTimeout,
        QStringLiteral("登录检查超时（%1ms）；将按未登录清晰度播放")
            .arg(request.timeout_ms),
        QStringLiteral("nav_timeout_ms=%1").arg(request.timeout_ms));
    result.http_status = response.http_status;
    result.elapsed_ms = response.elapsed_ms;
    result.request_cookie_sent = response.request_cookie_sent;
    return result;
  }
  if (!response.ok) {
    AuthResult result = HttpFailure(response.http_status, response.elapsed_ms);
    if (response.http_status < 400) {
      // DNS/TLS/连接失败等：保留客户端的可读错误，并明确不假装登录。
      result.error = AuthError::kNetwork;
      result.message = WithAnonymousSuffix(
          response.error.isEmpty()
              ? QStringLiteral("网络连接失败，请检查网络后重试")
              : response.error);
      result.detail = QStringLiteral("nav_transport_error=%1")
                          .arg(response.win32_error);
    }
    result.request_cookie_sent = response.request_cookie_sent;
    return result;
  }

  AuthResult result = ParseNavJson(response.body, response.http_status,
                                   response.elapsed_ms);
  result.request_cookie_sent = response.request_cookie_sent;
  return result;
}

AuthState BiliAuthSession::state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

AuthResult BiliAuthSession::result() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return result_;
}

bool BiliAuthSession::has_credential() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return has_credential_;
}

void BiliAuthSession::SetCredentialPresent(bool present) {
  std::lock_guard<std::mutex> lock(mutex_);
  has_credential_ = present;
  state_ = present ? AuthState::kNotChecked : AuthState::kAnonymous;
  result_ = AuthResult();
  result_.state = state_;
}

bool BiliAuthSession::BeginCheck() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!has_credential_ || state_ != AuthState::kNotChecked) {
    return false;
  }
  state_ = AuthState::kChecking;
  return true;
}

void BiliAuthSession::CompleteCheck(const AuthResult& result) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != AuthState::kChecking) {
    return;
  }
  result_ = result;
  state_ = result.ok() ? AuthState::kLoggedIn : AuthState::kLoginUnavailable;
  result_.state = state_;
}

void BiliAuthSession::CancelCheck() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == AuthState::kChecking) {
    state_ = has_credential_ ? AuthState::kNotChecked : AuthState::kAnonymous;
  }
}

void BiliAuthSession::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  has_credential_ = false;
  state_ = AuthState::kAnonymous;
  result_ = AuthResult();
  result_.state = state_;
}

}  // namespace lapian::online
