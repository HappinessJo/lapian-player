#include "online/WinHttpClient.h"

#include "online/Sessdata.h"
#include "online/WinHttpApi.h"

#include <QElapsedTimer>
#include <QString>
#include <QUrl>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>

#include <windows.h>
#include <winhttp.h>

namespace lapian::online {
namespace {

constexpr int kDefaultTimeoutMs = 6000;
constexpr std::size_t kMaxBodyBytes = 2 * 1024 * 1024;
constexpr int kCancelPollMs = 20;
constexpr int kHandleCloseWaitMs = 1000;

bool OnlineLogEnabled() {
  static const bool enabled = qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG");
  return enabled;
}

QString MapWinHttpError(DWORD error) {
  switch (error) {
    case ERROR_WINHTTP_TIMEOUT:
      return QStringLiteral("网络超时，请重试");
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
      return QStringLiteral("无法解析服务器地址（DNS），请检查网络后重试");
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR:
      return QStringLiteral("网络连接失败，请检查网络后重试");
    case ERROR_WINHTTP_SECURE_FAILURE:
      return QStringLiteral("TLS/证书校验失败，无法安全连接");
    case ERROR_WINHTTP_OPERATION_CANCELLED:
      return QStringLiteral("请求已取消");
    case ERROR_WINHTTP_INVALID_URL:
      return QStringLiteral("请求地址无效");
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
      return QStringLiteral("不支持的请求协议");
    default:
      return QStringLiteral("网络请求失败（WinHTTP 错误 %1）").arg(error);
  }
}

struct WinHttpState {
  std::mutex mutex;
  std::condition_variable cv;
  // WinHTTP 的异步 API 在本机回环上会在 ReceiveResponse/ReadData 内同步回调；
  // 递归锁让同一线程的回调可以重入，同时仍挡住其它线程的关闭/读取竞争。
  std::recursive_mutex api_mutex;
  std::atomic<int> refs{1};

  HINTERNET session = nullptr;
  HINTERNET connection = nullptr;
  HINTERNET request = nullptr;

  std::atomic<bool> closing{false};
  bool done = false;
  std::atomic<bool> handle_closed{false};

  bool cancelled = false;
  bool timed_out = false;
  bool failed = false;
  bool response_requested = false;
  bool reading = false;

  int http_status = 0;
  QByteArray body;
  std::array<char, 16 * 1024> read_buffer{};
  QString error;
  DWORD win32_error = 0;

  void AddRef() { refs.fetch_add(1, std::memory_order_relaxed); }

  void Release() {
    if (refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }

  void Finish(bool success, const QString& message, DWORD code) {
    std::lock_guard<std::mutex> lock(mutex);
    if (done) {
      return;
    }
    done = true;
    failed = !success;
    if (!success) {
      error = message;
      win32_error = code;
    }
    cv.notify_all();
  }
};

void CloseHandleIfPresent(detail::WinHttpApi& api, HINTERNET* handle) {
  if (handle != nullptr && *handle != nullptr) {
    api.close(*handle);
    *handle = nullptr;
  }
}

void FailWithLastError(WinHttpState* state) {
  const DWORD error = GetLastError();
  state->Finish(false, MapWinHttpError(error), error);
}

void StartRead(WinHttpState* state, HINTERNET request) {
  detail::WinHttpApi& api = detail::WinHttpApiInstance();
  state->reading = true;
  if (!api.read_data(request, state->read_buffer.data(),
                     static_cast<DWORD>(state->read_buffer.size()), nullptr)) {
    state->reading = false;
    FailWithLastError(state);
  }
}

void OnRequestError(WinHttpState* state, void* info) {
  DWORD error = ERROR_WINHTTP_CONNECTION_ERROR;
  if (info != nullptr) {
    const auto* result = static_cast<const WINHTTP_ASYNC_RESULT*>(info);
    error = static_cast<DWORD>(result->dwError);
  }
  if (state->closing) {
    return;  // 取消时 WinHTTP 也会报 operation cancelled；不要覆盖已确定的取消状态
  }
  state->Finish(false, MapWinHttpError(error), error);
}

void HandleCallback(HINTERNET handle, DWORD_PTR context, DWORD code, void* info,
                    DWORD length) {
  auto* state = reinterpret_cast<WinHttpState*>(context);
  if (state == nullptr) {
    return;
  }

  if (code == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->handle_closed = true;
    }
    state->cv.notify_all();
    state->Release();  // 回调持有的引用；HANDLE_CLOSING 后不会再有回调
    return;
  }

  if (code == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
    OnRequestError(state, info);
    return;
  }
  if (code == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE) {
    if (!state->closing) {
      state->Finish(false, QStringLiteral("TLS/证书校验失败，无法安全连接"),
                    ERROR_WINHTTP_SECURE_FAILURE);
    }
    return;
  }

  detail::WinHttpApi& api = detail::WinHttpApiInstance();
  std::lock_guard<std::recursive_mutex> api_lock(state->api_mutex);
  if (state->closing) {
    return;
  }

  switch (code) {
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE: {
      if (state->response_requested) {
        break;
      }
      state->response_requested = true;
      if (!api.receive_response(handle, nullptr)) {
        FailWithLastError(state);
      }
      break;
    }
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE: {
      if (state->reading) {
        break;
      }
      DWORD status = 0;
      DWORD status_size = sizeof(status);
      if (!api.query_headers(handle,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                             WINHTTP_NO_HEADER_INDEX)) {
        FailWithLastError(state);
        break;
      }
      state->http_status = static_cast<int>(status);
      StartRead(state, handle);
      break;
    }
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE: {
      state->reading = false;
      if (length == 0) {
        state->Finish(true, QString(), 0);
        break;
      }
      if (state->body.size() + static_cast<qsizetype>(length) >
          static_cast<qsizetype>(kMaxBodyBytes)) {
        state->Finish(false, QStringLiteral("接口响应过大，已停止读取"), 0);
        break;
      }
      state->body.append(state->read_buffer.data(), static_cast<int>(length));
      StartRead(state, handle);
      break;
    }
    default:
      break;
  }
}

void CALLBACK WinHttpStatusCallback(HINTERNET handle, DWORD_PTR context, DWORD code,
                                    void* info, DWORD length) {
  HandleCallback(handle, context, code, info, length);
}

struct ParsedUrl {
  bool ok = false;
  QString error;
  QString host;
  QString object;
  QString log_path;
  INTERNET_PORT port = 0;
  bool secure = false;
};

ParsedUrl ParseUrl(const QString& url) {
  ParsedUrl parsed;
  const std::wstring wide = url.toStdWString();
  URL_COMPONENTS components{};
  components.dwStructSize = sizeof(components);
  components.dwSchemeLength = static_cast<DWORD>(-1);
  components.dwHostNameLength = static_cast<DWORD>(-1);
  components.dwUrlPathLength = static_cast<DWORD>(-1);
  components.dwExtraInfoLength = static_cast<DWORD>(-1);

  detail::WinHttpApi& api = detail::WinHttpApiInstance();
  if (!api.crack_url(wide.c_str(), 0, 0, &components)) {
    parsed.error = QStringLiteral("请求地址格式无效");
    return parsed;
  }

  auto component = [](LPWSTR text, DWORD length) {
    if (text == nullptr || length == 0) {
      return QString();
    }
    return QString::fromWCharArray(text, static_cast<int>(length));
  };

  const QString scheme = component(components.lpszScheme, components.dwSchemeLength).toLower();
  if (scheme != QStringLiteral("https") && scheme != QStringLiteral("http")) {
    parsed.error = QStringLiteral("只支持 HTTPS/HTTP 请求");
    return parsed;
  }
  parsed.secure = scheme == QStringLiteral("https");
  parsed.host = component(components.lpszHostName, components.dwHostNameLength);
  if (parsed.host.isEmpty()) {
    parsed.error = QStringLiteral("请求地址缺少主机名");
    return parsed;
  }
  parsed.port = components.nPort;
  QString path = component(components.lpszUrlPath, components.dwUrlPathLength);
  if (path.isEmpty()) {
    path = QStringLiteral("/");
  }
  const QString extra = component(components.lpszExtraInfo, components.dwExtraInfoLength);
  parsed.object = path + extra;
  parsed.log_path = path;  // 日志不带查询串，避免把签名参数写出去
  parsed.ok = true;
  return parsed;
}

// Cookie 只允许发往 https://api.bilibili.com:443；不跟随重定向，因此不会
// 在 30x 后把 Cookie 带到别的 host。
bool IsApiCookieTarget(const ParsedUrl& parsed) {
  return parsed.secure && parsed.port == 443 &&
         parsed.host.compare(QStringLiteral("api.bilibili.com"),
                             Qt::CaseInsensitive) == 0;
}

}  // namespace

HttpResponse WinHttpClient::Get(const HttpRequest& request,
                                const HttpCancelToken& cancel) {
  HttpResponse response;
  QElapsedTimer timer;
  timer.start();

  if (cancel.IsCancelled()) {
    response.cancelled = true;
    response.error = QStringLiteral("请求已取消");
    response.elapsed_ms = timer.elapsed();
    return response;
  }

  detail::WinHttpApi& api = detail::WinHttpApiInstance();
  QString load_error;
  if (!api.Load(&load_error)) {
    response.error = QStringLiteral("系统网络组件不可用：") + load_error;
    response.elapsed_ms = timer.elapsed();
    if (OnlineLogEnabled()) {
      std::fprintf(stderr, "T8.1 http_load_failed error=%s\n",
                   response.error.toUtf8().constData());
      std::fflush(stderr);
    }
    return response;
  }

  const ParsedUrl parsed = ParseUrl(request.url);
  if (!parsed.ok) {
    response.error = parsed.error;
    response.elapsed_ms = timer.elapsed();
    return response;
  }

  // T8.2：带凭据的请求只允许 api.bilibili.com:443 + HTTPS。失败即拒绝发送，
  // 不做“降级成匿名再发”的静默行为。
  if (!request.sessdata.isEmpty()) {
    if (!IsSafeSessdataValue(request.sessdata)) {
      response.error = QStringLiteral("凭据格式无效，请重新粘贴 SESSDATA");
      response.elapsed_ms = timer.elapsed();
      return response;
    }
    if (!IsApiCookieTarget(parsed)) {
      response.error =
          QStringLiteral("凭据只允许发送到 https://api.bilibili.com（HTTPS）");
      response.elapsed_ms = timer.elapsed();
      if (OnlineLogEnabled()) {
        std::fprintf(stderr, "T8.2 http_credential_blocked host=%s path=%s\n",
                     parsed.host.toUtf8().constData(),
                     parsed.log_path.toUtf8().constData());
        std::fflush(stderr);
      }
      return response;
    }
  }

  if (OnlineLogEnabled()) {
    // 请求日志只保留 host/path；不带查询串、headers、Cookie、超时或响应体。
    std::fprintf(stderr, "T8.1 http_request host=%s path=%s\n",
                 parsed.host.toUtf8().constData(), parsed.log_path.toUtf8().constData());
    std::fflush(stderr);
  }

  const int timeout_ms = request.timeout_ms > 0 ? request.timeout_ms : kDefaultTimeoutMs;
  auto* state = new WinHttpState();

  auto fail = [&](const QString& message, DWORD code) {
    response.error = message;
    response.win32_error = static_cast<int>(code);
    response.elapsed_ms = timer.elapsed();
    if (OnlineLogEnabled()) {
      std::fprintf(stderr, "T8.1 http_done host=%s status=0 elapsed_ms=%lld error=%s\n",
                   parsed.host.toUtf8().constData(),
                   static_cast<long long>(response.elapsed_ms),
                   message.toUtf8().constData());
      std::fflush(stderr);
    }
  };

  state->session = api.open(L"Lapian/0.1 (T8.1 online resolve)",
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
  if (state->session == nullptr) {
    const DWORD error = GetLastError();
    const QString message = MapWinHttpError(error);
    state->Release();
    fail(message, error);
    return response;
  }
  api.set_timeouts(state->session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

  state->connection = api.connect(state->session, parsed.host.toStdWString().c_str(),
                                  parsed.port, 0);
  if (state->connection == nullptr) {
    const DWORD error = GetLastError();
    CloseHandleIfPresent(api, &state->session);
    state->Release();
    fail(MapWinHttpError(error), error);
    return response;
  }

  state->request = api.open_request(
      state->connection, L"GET", parsed.object.toStdWString().c_str(), nullptr,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
      parsed.secure ? WINHTTP_FLAG_SECURE : 0);
  if (state->request == nullptr) {
    const DWORD error = GetLastError();
    CloseHandleIfPresent(api, &state->connection);
    CloseHandleIfPresent(api, &state->session);
    state->Release();
    fail(MapWinHttpError(error), error);
    return response;
  }

  // T8.2：绝不跟随重定向。Cookie 只允许 api.bilibili.com，若 30x 被跟随，
  // 下一次请求就可能把 Cookie 带到别的 host；这里在请求句柄上显式关掉。
  // 失败则拒绝发送（fail closed），不做“匿名重试”。
  DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  if (!api.set_option(state->request, WINHTTP_OPTION_REDIRECT_POLICY,
                      &redirect_policy, sizeof(redirect_policy))) {
    const DWORD error = GetLastError();
    CloseHandleIfPresent(api, &state->request);
    CloseHandleIfPresent(api, &state->connection);
    CloseHandleIfPresent(api, &state->session);
    state->Release();
    fail(QStringLiteral("无法关闭 HTTP 重定向，已拒绝发送请求"), error);
    return response;
  }

  // T8.1 明确匿名：WinHTTP 自己的 cookie jar 关掉，避免重定向/Set-Cookie
  // 偷偷带上服务端 Cookie。T8.2 的 SESSDATA 只手动加头。
  DWORD disable_cookies = WINHTTP_DISABLE_COOKIES;
  api.set_option(state->request, WINHTTP_OPTION_DISABLE_FEATURE, &disable_cookies,
                 sizeof(disable_cookies));
  api.add_request_headers(state->request, L"Accept: application/json",
                          static_cast<DWORD>(-1),
                          WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

  if (!request.sessdata.isEmpty()) {
    const std::wstring cookie_header =
        L"Cookie: SESSDATA=" + request.sessdata.toStdWString();
    if (!api.add_request_headers(state->request, cookie_header.c_str(),
                                 static_cast<DWORD>(-1),
                                 WINHTTP_ADDREQ_FLAG_ADD |
                                     WINHTTP_ADDREQ_FLAG_REPLACE)) {
      const DWORD error = GetLastError();
      CloseHandleIfPresent(api, &state->request);
      CloseHandleIfPresent(api, &state->connection);
      CloseHandleIfPresent(api, &state->session);
      state->Release();
      fail(QStringLiteral("无法添加凭据请求头，已拒绝发送请求"), error);
      return response;
    }
    response.request_cookie_sent = true;
  }

  if (api.set_status_callback(state->request, &WinHttpStatusCallback,
                              WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,
                              0) == WINHTTP_INVALID_STATUS_CALLBACK) {
    const DWORD error = GetLastError();
    CloseHandleIfPresent(api, &state->request);
    CloseHandleIfPresent(api, &state->connection);
    CloseHandleIfPresent(api, &state->session);
    state->Release();
    fail(QStringLiteral("无法注册网络回调"), error);
    return response;
  }
  state->AddRef();  // 回调引用；HANDLE_CLOSING 时释放

  if (!api.send_request(state->request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                        WINHTTP_NO_REQUEST_DATA, 0, 0,
                        reinterpret_cast<DWORD_PTR>(state))) {
    const DWORD error = GetLastError();
    state->Finish(false, MapWinHttpError(error), error);
  }

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    while (!state->done) {
      if (cancel.IsCancelled()) {
        state->cancelled = true;
        break;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        state->timed_out = true;
        break;
      }
      state->cv.wait_for(lock, std::chrono::milliseconds(kCancelPollMs));
    }
  }

  // 关闭请求句柄必须由持有请求的线程做；先与回调的 WinHTTP 调用串行化，
  // 再在锁外 WinHttpCloseHandle，等待 HANDLE_CLOSING 后再释放状态。
  if (!state->handle_closed.load()) {
    HINTERNET request_to_close = nullptr;
    {
      std::lock_guard<std::recursive_mutex> lock(state->api_mutex);
      if (!state->closing) {
        state->closing = true;
        request_to_close = state->request;
        state->request = nullptr;
      }
    }
    if (request_to_close != nullptr) {
      api.close(request_to_close);
    }
    std::unique_lock<std::mutex> lock(state->mutex);
    state->cv.wait_for(lock, std::chrono::milliseconds(kHandleCloseWaitMs),
                       [state]() { return state->handle_closed.load(); });
  }

  const bool handle_closed = state->handle_closed.load();
  if (handle_closed) {
    CloseHandleIfPresent(api, &state->connection);
    CloseHandleIfPresent(api, &state->session);
  } else if (OnlineLogEnabled()) {
    std::fprintf(stderr, "T8.1 http_handle_close_timeout host=%s\n",
                 parsed.host.toUtf8().constData());
    std::fflush(stderr);
  }

  response.http_status = state->http_status;
  response.body = state->body;
  response.win32_error = static_cast<int>(state->win32_error);
  response.elapsed_ms = timer.elapsed();
  if (state->cancelled || cancel.IsCancelled()) {
    response.cancelled = true;
    response.error = QStringLiteral("请求已取消");
  } else if (state->timed_out) {
    response.timed_out = true;
    response.error = QStringLiteral("网络超时，请重试");
  } else if (state->failed) {
    response.error = state->error.isEmpty()
                         ? QStringLiteral("网络请求失败，请重试")
                         : state->error;
  } else if (state->http_status >= 200 && state->http_status < 300) {
    response.ok = true;
  } else {
    response.error = QStringLiteral("HTTP %1").arg(state->http_status);
  }

  if (OnlineLogEnabled()) {
    std::fprintf(stderr, "T8.1 http_done host=%s status=%d elapsed_ms=%lld\n",
                 parsed.host.toUtf8().constData(), response.http_status,
                 static_cast<long long>(response.elapsed_ms));
    std::fflush(stderr);
  }

  state->Release();
  return response;
}

bool WinHttpClient::LibraryLoadedForTest() {
  return detail::WinHttpApiInstance().loaded();
}

QString WinHttpClient::ValidateUrlForTest(const QString& url) {
  const QUrl parsed(url);
  const QString scheme = parsed.scheme().toLower();
  if (!parsed.isValid() || (scheme != QStringLiteral("https") &&
                            scheme != QStringLiteral("http")) ||
      parsed.host().isEmpty()) {
    return QStringLiteral("invalid-url");
  }
  return QString();
}

bool WinHttpClient::CookieAllowedForTest(const QString& url) {
  const QUrl parsed(url);
  if (!parsed.isValid()) {
    return false;
  }
  return parsed.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 &&
         parsed.host().compare(QStringLiteral("api.bilibili.com"),
                               Qt::CaseInsensitive) == 0 &&
         parsed.port(443) == 443;
}

}  // namespace lapian::online
