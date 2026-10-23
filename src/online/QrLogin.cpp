#include "online/QrLogin.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QUrl>
#include <QUrlQuery>

#include "online/Sessdata.h"

namespace lapian::online {
namespace {

bool IsValidQrKey(const QString& key) {
  if (key.isEmpty() || key.size() > 128) {
    return false;
  }
  for (const QChar ch : key) {
    const ushort code = ch.unicode();
    const bool ok = (code >= '0' && code <= '9') ||
                    (code >= 'A' && code <= 'Z') ||
                    (code >= 'a' && code <= 'z') || code == '-' || code == '_';
    if (!ok) {
      return false;
    }
  }
  return true;
}

bool IsHttpsUrl(const QString& url) {
  if (url.isEmpty() || url.size() > 4096) {
    return false;
  }
  const QUrl parsed(url);
  return parsed.isValid() && parsed.scheme() == QStringLiteral("https") &&
         !parsed.host().isEmpty();
}

QString ExtractSessdataFromUrl(const QString& text) {
  if (text.isEmpty()) {
    return QString();
  }
  const QUrl url(text);
  if (!url.isValid()) {
    return QString();
  }
  const QUrlQuery query(url);
  if (!query.hasQueryItem(QStringLiteral("SESSDATA"))) {
    return QString();
  }
  // 保留百分号编码：SESSDATA 的 Cookie 值通常是 `%2C` 等编码形式，
  // 解码成逗号会被 NormalizeSessdata 拒绝（也会与 Cookie 原值不一致）。
  return query.queryItemValue(QStringLiteral("SESSDATA"), QUrl::FullyEncoded);
}

QJsonObject ParseRoot(const QByteArray& body, bool* ok) {
  *ok = false;
  QJsonParseError parse_error;
  const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    return {};
  }
  *ok = true;
  return document.object();
}

}  // namespace

QString BuildQrGenerateUrl() {
  return QStringLiteral(
      "https://passport.bilibili.com/x/passport-login/web/qrcode/generate");
}

QString BuildQrPollUrl(const QString& qrcode_key) {
  if (!IsValidQrKey(qrcode_key)) {
    return QString();
  }
  QUrl url(QStringLiteral(
      "https://passport.bilibili.com/x/passport-login/web/qrcode/poll"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("qrcode_key"), qrcode_key);
  url.setQuery(query);
  return url.toString();
}

QrGenerateResult ParseQrGenerateJson(const QByteArray& body) {
  QrGenerateResult result;
  bool root_ok = false;
  const QJsonObject root = ParseRoot(body, &root_ok);
  if (!root_ok) {
    result.error = QStringLiteral("二维码登录响应格式无效，请稍后重试");
    return result;
  }
  const QJsonValue code_value = root.value(QStringLiteral("code"));
  if (!code_value.isDouble()) {
    result.error = QStringLiteral("二维码登录响应缺少错误码");
    return result;
  }
  result.api_code = code_value.toInt();
  if (result.api_code != 0) {
    result.error = QStringLiteral("二维码登录接口返回错误（%1）")
                       .arg(result.api_code);
    return result;
  }
  const QJsonValue data_value = root.value(QStringLiteral("data"));
  if (!data_value.isObject()) {
    result.error = QStringLiteral("二维码登录响应缺少 data");
    return result;
  }
  const QJsonObject data = data_value.toObject();
  const QString key = data.value(QStringLiteral("qrcode_key")).toString();
  if (!IsValidQrKey(key)) {
    result.error = QStringLiteral("二维码登录响应缺少有效的 qrcode_key");
    return result;
  }
  const QString url = data.value(QStringLiteral("url")).toString();
  if (!IsHttpsUrl(url)) {
    result.error = QStringLiteral("二维码登录响应缺少有效的二维码地址");
    return result;
  }
  result.ok = true;
  result.data.qrcode_key = key;
  result.data.qr_url = url;
  return result;
}

QrPollResult ParseQrPollJson(const QByteArray& body,
                             const QString& sessdata_from_cookie) {
  QrPollResult result;
  bool root_ok = false;
  const QJsonObject root = ParseRoot(body, &root_ok);
  if (!root_ok) {
    result.error = QStringLiteral("二维码登录状态响应格式无效");
    return result;
  }
  const QJsonValue code_value = root.value(QStringLiteral("code"));
  if (!code_value.isDouble()) {
    result.error = QStringLiteral("二维码登录状态响应缺少错误码");
    return result;
  }
  result.api_code = code_value.toInt();
  if (result.api_code != 0) {
    result.error = QStringLiteral("二维码登录状态接口返回错误（%1）")
                       .arg(result.api_code);
    return result;
  }
  const QJsonValue data_value = root.value(QStringLiteral("data"));
  if (!data_value.isObject()) {
    result.error = QStringLiteral("二维码登录状态响应缺少 data");
    return result;
  }
  const QJsonObject data = data_value.toObject();
  const QJsonValue data_code_value = data.value(QStringLiteral("code"));
  if (!data_code_value.isDouble()) {
    result.error = QStringLiteral("二维码登录状态响应缺少 data.code");
    return result;
  }
  result.data_code = data_code_value.toInt();
  const QString data_message = data.value(QStringLiteral("message")).toString();
  if (!data_message.isEmpty() && data_message.size() <= 200) {
    result.message = data_message;
  }

  switch (result.data_code) {
    case 86101:
      result.status = QrPollStatus::kWaiting;
      result.message = QStringLiteral("等待扫码…");
      return result;
    case 86090:
      result.status = QrPollStatus::kScanned;
      result.message = QStringLiteral("已扫码，请在手机上确认登录");
      return result;
    case 86038:
      result.status = QrPollStatus::kExpired;
      result.message = QStringLiteral("二维码已过期，请点击刷新");
      return result;
    case 0:
      break;
    default:
      result.status = QrPollStatus::kFailed;
      result.error = QStringLiteral("二维码登录状态未知（code=%1）")
                         .arg(result.data_code);
      return result;
  }

  QString sessdata = ExtractSessdataFromUrl(
      data.value(QStringLiteral("url")).toString());
  if (sessdata.isEmpty()) {
    sessdata = sessdata_from_cookie;
  }
  const SessdataNormalization normalized = NormalizeSessdata(sessdata);
  ClearSecret(&sessdata);
  if (!normalized.ok) {
    result.status = QrPollStatus::kFailed;
    result.error = QStringLiteral("登录成功响应没有可用的 SESSDATA");
    return result;
  }
  result.status = QrPollStatus::kSuccess;
  result.sessdata = normalized.value;
  result.message = QStringLiteral("扫码成功，正在自检登录状态…");
  return result;
}

QString QrLoginStateName(QrLoginState state) {
  switch (state) {
    case QrLoginState::kIdle:
      return QStringLiteral("idle");
    case QrLoginState::kGenerating:
      return QStringLiteral("generating");
    case QrLoginState::kWaiting:
      return QStringLiteral("waiting");
    case QrLoginState::kScanned:
      return QStringLiteral("scanned");
    case QrLoginState::kExpired:
      return QStringLiteral("expired");
    case QrLoginState::kVerifying:
      return QStringLiteral("verifying");
    case QrLoginState::kSuccess:
      return QStringLiteral("success");
    case QrLoginState::kFailed:
      return QStringLiteral("failed");
    case QrLoginState::kCancelled:
      return QStringLiteral("cancelled");
  }
  return QStringLiteral("unknown");
}

QString QrPollStatusName(QrPollStatus status) {
  switch (status) {
    case QrPollStatus::kWaiting:
      return QStringLiteral("waiting");
    case QrPollStatus::kScanned:
      return QStringLiteral("scanned");
    case QrPollStatus::kExpired:
      return QStringLiteral("expired");
    case QrPollStatus::kSuccess:
      return QStringLiteral("success");
    case QrPollStatus::kFailed:
      return QStringLiteral("failed");
  }
  return QStringLiteral("unknown");
}

bool IsQrLoginTerminal(QrLoginState state) {
  return state == QrLoginState::kExpired || state == QrLoginState::kSuccess ||
         state == QrLoginState::kFailed || state == QrLoginState::kCancelled;
}

bool QrLoginSession::TransitionTo(QrLoginState next, const QString& message) {
  state_ = next;
  message_ = message;
  return true;
}

void QrLoginSession::ClearSecrets() {
  ClearSecret(&sessdata_);
  qrcode_key_.clear();
  qr_url_.clear();
}

bool QrLoginSession::Start() {
  switch (state_) {
    case QrLoginState::kGenerating:
    case QrLoginState::kWaiting:
    case QrLoginState::kScanned:
    case QrLoginState::kVerifying:
      return false;
    case QrLoginState::kIdle:
    case QrLoginState::kExpired:
    case QrLoginState::kSuccess:
    case QrLoginState::kFailed:
    case QrLoginState::kCancelled:
      break;
  }
  ClearSecrets();
  return TransitionTo(QrLoginState::kGenerating,
                      QStringLiteral("正在生成二维码…"));
}

bool QrLoginSession::OnGenerated(const QrGenerateResult& result) {
  if (state_ != QrLoginState::kGenerating) {
    return false;
  }
  if (!result.ok) {
    ClearSecrets();
    return TransitionTo(QrLoginState::kFailed, result.error);
  }
  qrcode_key_ = result.data.qrcode_key;
  qr_url_ = result.data.qr_url;
  return TransitionTo(QrLoginState::kWaiting,
                      QStringLiteral("请使用 B 站手机客户端扫码"));
}

bool QrLoginSession::OnPolled(const QrPollResult& result) {
  if (state_ != QrLoginState::kWaiting && state_ != QrLoginState::kScanned) {
    return false;
  }
  switch (result.status) {
    case QrPollStatus::kWaiting:
      return TransitionTo(QrLoginState::kWaiting,
                          QStringLiteral("等待扫码…"));
    case QrPollStatus::kScanned:
      return TransitionTo(QrLoginState::kScanned,
                          QStringLiteral("已扫码，请在手机上确认登录"));
    case QrPollStatus::kExpired:
      ClearSecrets();
      return TransitionTo(QrLoginState::kExpired,
                          QStringLiteral("二维码已过期，请点击刷新"));
    case QrPollStatus::kFailed:
      ClearSecrets();
      return TransitionTo(QrLoginState::kFailed, result.error);
    case QrPollStatus::kSuccess:
      if (result.sessdata.isEmpty()) {
        ClearSecrets();
        return TransitionTo(QrLoginState::kFailed,
                            QStringLiteral("登录成功响应没有可用的 SESSDATA"));
      }
      sessdata_ = result.sessdata;
      return TransitionTo(QrLoginState::kVerifying,
                          QStringLiteral("扫码成功，正在自检登录状态…"));
  }
  return false;
}

bool QrLoginSession::OnVerified(bool nav_ok, const QString& message) {
  if (state_ != QrLoginState::kVerifying) {
    return false;
  }
  if (!nav_ok) {
    ClearSecrets();
    return TransitionTo(
        QrLoginState::kFailed,
        message.isEmpty() ? QStringLiteral("登录自检失败，请改用粘贴 SESSDATA")
                          : message);
  }
  return TransitionTo(QrLoginState::kSuccess, QStringLiteral("已登录"));
}

bool QrLoginSession::Expire() {
  if (state_ != QrLoginState::kGenerating &&
      state_ != QrLoginState::kWaiting && state_ != QrLoginState::kScanned) {
    return false;
  }
  ClearSecrets();
  return TransitionTo(QrLoginState::kExpired,
                      QStringLiteral("二维码已过期，请点击刷新"));
}

bool QrLoginSession::Cancel() {
  switch (state_) {
    case QrLoginState::kGenerating:
    case QrLoginState::kWaiting:
    case QrLoginState::kScanned:
    case QrLoginState::kVerifying:
      break;
    default:
      return false;
  }
  ClearSecrets();
  return TransitionTo(QrLoginState::kCancelled,
                      QStringLiteral("扫码登录已取消"));
}

void QrLoginSession::Reset() {
  ClearSecrets();
  state_ = QrLoginState::kIdle;
  message_.clear();
}

bool QrLoginSession::Fail(const QString& message) {
  ClearSecrets();
  return TransitionTo(QrLoginState::kFailed,
                      message.isEmpty() ? QStringLiteral("扫码登录失败")
                                        : message);
}

}  // namespace lapian::online
