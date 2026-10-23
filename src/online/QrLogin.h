#pragma once

// T8.2.2 扫码登录纯逻辑（不碰 UI/网络线程，便于 fake 单测）。
//
// 流程：generate → 本地渲染二维码 → 1.5s poll → 等待/已扫/过期/成功。
// 成功只提取 SESSDATA；其它 cookie/refresh_token/昵称/UID 全部丢弃。
// 状态机只允许合法迁移；成功/取消/过期后不再 poll。

#include <QByteArray>
#include <QString>

#include "online/Sessdata.h"

namespace lapian::online {

constexpr int kQrLoginExpiryMs = 180 * 1000;
constexpr int kQrPollIntervalMs = 1500;

// https://passport.bilibili.com/x/passport-login/web/qrcode/generate
QString BuildQrGenerateUrl();
// 只接受 1–128 位 [A-Za-z0-9_-]；非法 key 返回空串（调用方不应发请求）。
QString BuildQrPollUrl(const QString& qrcode_key);

struct QrGenerateData {
  QString qrcode_key;
  QString qr_url;  // 二维码内容（官方登录 URL）；不入日志
};

struct QrGenerateResult {
  bool ok = false;
  QrGenerateData data;
  QString error;
  int api_code = 0;
};

// 解析 generate JSON。成功 → ok；否则 error 可读，不包含 URL/查询串。
QrGenerateResult ParseQrGenerateJson(const QByteArray& body);

enum class QrPollStatus {
  kWaiting = 0,  // 86101 未扫码
  kScanned,      // 86090 已扫码待确认
  kExpired,      // 86038 已过期
  kSuccess,      // 0；sessdata 非空
  kFailed,       // 其它/坏 JSON
};

struct QrPollResult {
  QrPollStatus status = QrPollStatus::kFailed;
  QString sessdata;  // 仅 kSuccess；调用方负责 ClearSecret
  QString message;   // 可直接展示（不含 key/URL/SESSDATA）
  QString error;     // 仅 kFailed；可读
  int api_code = 0;
  int data_code = 0;
};

// sessdata_from_cookie：WinHTTP 从 Set-Cookie 中只提取的 SESSDATA；
// data.url 查询参数优先，缺失时回退它。两者都没有 → kFailed。
QrPollResult ParseQrPollJson(const QByteArray& body,
                             const QString& sessdata_from_cookie = QString());

enum class QrLoginState {
  kIdle = 0,
  kGenerating,
  kWaiting,
  kScanned,
  kExpired,
  kVerifying,  // 已拿到 SESSDATA，正在 nav 自检（还不算已登录）
  kSuccess,    // nav 通过（才允许 UI 显示已登录）
  kFailed,
  kCancelled,
};

QString QrLoginStateName(QrLoginState state);
QString QrPollStatusName(QrPollStatus status);
bool IsQrLoginTerminal(QrLoginState state);

// 单线程状态机；不创建线程、不联网、不写盘。
class QrLoginSession {
 public:
  QrLoginState state() const { return state_; }
  QString message() const { return message_; }
  QString qrcodeKey() const { return qrcode_key_; }
  QString qrUrl() const { return qr_url_; }
  // 仅 kVerifying/kSuccess 非空；调用方用完 ClearSecret；Cancel/失败会清空。
  QString sessdata() const { return sessdata_; }
  // 成功保存到 DPAPI 后调用，尽快丢弃内存明文。
  void ClearSessdata() { ClearSecret(&sessdata_); }

  // 从 idle/expired/failed/cancelled/success 开始新一轮 generate。
  bool Start();
  bool OnGenerated(const QrGenerateResult& result);
  // 只在 waiting/scanned 接受；success → kVerifying。
  bool OnPolled(const QrPollResult& result);
  // 只在 kVerifying 接受；nav 通过才是 kSuccess，否则 kFailed 并清空 sessdata。
  bool OnVerified(bool nav_ok, const QString& message);
  bool Expire();
  bool Cancel();
  // 控制器级失败（412/429/坏 JSON/渲染失败）：进入 kFailed 并清空秘密。
  bool Fail(const QString& message);
  void Reset();

 private:
  bool TransitionTo(QrLoginState next, const QString& message);
  void ClearSecrets();

  QrLoginState state_ = QrLoginState::kIdle;
  QString message_;
  QString qrcode_key_;
  QString qr_url_;
  QString sessdata_;
};

}  // namespace lapian::online
