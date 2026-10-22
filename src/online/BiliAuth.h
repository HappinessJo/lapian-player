#pragma once

// T8.2 登录自检（nav）：只判断“SESSDATA 是否仍可用于登录”，不解析 view、
// 不取流、不做账号体系。
//
// 语义：
// - 只有 HTTP 2xx + code=0 + data.isLogin=true 才算通过；
// - -101 / code=0 但 isLogin=false → 登录不可用（明确提示，不静默降级）；
// - 412/429、超时、DNS/TLS、解析失败 → 可读错误，继续匿名；
// - BiliAuthSession 缓存“本会话已检查”的结果：首次提交 BV 时检查一次，
//   清除凭据立即回 anonymous，取消中的检查回 not_checked（下次可重试）。

#include <QByteArray>
#include <QString>

#include <mutex>

#include "online/IHttpClient.h"

namespace lapian::online {

enum class AuthState {
  kAnonymous = 0,        // 没有凭据
  kNotChecked,           // 有凭据但本会话尚未检查
  kChecking,             // 检查中
  kLoggedIn,             // code=0 && isLogin=true
  kLoginUnavailable,     // 凭据失效/网络/TLS/超时/风控
};

enum class AuthError {
  kNone = 0,
  kNoCredential,
  kInvalidCredential,
  kApi,
  kHttp,
  kNetwork,
  kTimeout,
  kCancelled,
  kParse,
};

struct AuthResult {
  AuthState state = AuthState::kAnonymous;
  bool is_login = false;
  int api_code = 0;
  int http_status = 0;
  int vip_status = -1;  // 仅证据用；不落盘、不打印账号标识
  qint64 elapsed_ms = 0;
  AuthError error = AuthError::kNone;
  QString message;  // 可直接给用户；不含 Cookie/URL 查询串
  QString detail;   // 只进日志；不含 Cookie/URL 查询串
  bool request_cookie_sent = false;

  bool ok() const { return state == AuthState::kLoggedIn; }
};

// 固定 nav 地址：https://api.bilibili.com/x/web-interface/nav（无查询参数）。
QString BuildNavUrl();

// 解析 nav JSON（HTTP 状态/耗时由调用方传入）。纯函数，便于 fixture 单测。
AuthResult ParseNavJson(const QByteArray& body, int http_status,
                        qint64 elapsed_ms);

// 同步执行 nav；调用方负责放到工作线程。timeout 建议 ≤2000ms。
// sessdata 必须是已规范化的值；空值返回 kNoCredential（不发请求）。
AuthResult CheckNav(const QString& sessdata, IHttpClient* http,
                    const HttpCancelToken& cancel, int timeout_ms = 2000);

QString AuthStateName(AuthState state);
QString AuthErrorName(AuthError error);

// 会话级状态缓存。线程安全；只缓存“已检查”的结果。
class BiliAuthSession {
 public:
  AuthState state() const;
  AuthResult result() const;
  bool has_credential() const;

  // 保存/加载凭据后调用：true → not_checked（下次提交时自检），
  // false → anonymous（立即按未登录处理）。
  void SetCredentialPresent(bool present);

  // not_checked → checking；已在检查/已检查时返回 false。
  bool BeginCheck();

  // 只在 checking 时生效；取消/清除后到达的旧结果应被忽略。
  void CompleteCheck(const AuthResult& result);

  // 取消中的检查回 not_checked（允许下次重试），不缓存“取消”。
  void CancelCheck();

  // 清除凭据：立即回 anonymous 并丢弃结果。
  void Reset();

 private:
  mutable std::mutex mutex_;
  AuthState state_ = AuthState::kAnonymous;
  bool has_credential_ = false;
  AuthResult result_;
};

}  // namespace lapian::online
