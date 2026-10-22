#pragma once

// 最小 HTTP 抽象。只做 JSON GET；不解析业务 JSON、不决定档位。
// T8.2 起支持“显式 SESSDATA”：只有 BiliAuth 的 nav 请求会带它，且客户端只允许
// 发给 https://api.bilibili.com；不建 Cookie jar、不做多账号、不自动重定向。

#include <QByteArray>
#include <QString>

#include <condition_variable>
#include <mutex>

namespace lapian::online {

// 线程安全取消令牌。HTTP 客户端在阻塞等待时轮询它；取消后客户端应尽快
// 关闭请求并返回 cancelled（T8.1 目标 ≤100ms）。
class HttpCancelToken {
 public:
  HttpCancelToken() = default;

  void Cancel() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      cancelled_ = true;
    }
    cv_.notify_all();
  }

  bool IsCancelled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cancelled_;
  }

 private:
  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
  bool cancelled_ = false;
};

struct HttpRequest {
  QString url;
  // 整体上限（DNS/连接/发送/读取共享）；默认 6s，给 O1-A1 的 5s 留余量。
  int timeout_ms = 6000;
  // 显式凭据：非空时客户端只向 https://api.bilibili.com 发送
  // "Cookie: SESSDATA=<value>"，绝不跟随重定向、不使用 WinHTTP cookie jar。
  // 其它 host、非 HTTPS 或含 CR/LF/; 的值一律拒绝发送（防止泄漏/注入）。
  QString sessdata;
};

struct HttpResponse {
  bool ok = false;
  bool cancelled = false;
  bool timed_out = false;
  int http_status = 0;
  QByteArray body;
  // 可直接展示的错误（不含 URL/凭据）。
  QString error;
  int win32_error = 0;
  qint64 elapsed_ms = 0;
  // 只用于诊断：本次请求是否实际加了 SESSDATA 头（不含值，不是 secret）。
  bool request_cookie_sent = false;
};

class IHttpClient {
 public:
  virtual ~IHttpClient() = default;
  virtual HttpResponse Get(const HttpRequest& request,
                            const HttpCancelToken& cancel) = 0;
};

}  // namespace lapian::online
