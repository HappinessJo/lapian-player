#include "online/DownloadTransport.h"

#include "online/WinHttpApi.h"

#include <QElapsedTimer>
#include <QRegularExpression>
#include <QString>
#include <QUrl>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include <windows.h>
#include <winhttp.h>

namespace lapian::online {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int kCancelPollMs = 20;
constexpr int kHandleCloseWaitMs = 400;
constexpr std::size_t kReadBufferBytes = 256 * 1024;

bool OnlineLogEnabled() {
  static const bool enabled = qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG");
  return enabled;
}

std::int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             Clock::now().time_since_epoch())
      .count();
}

QString MapWinHttpError(DWORD error) {
  switch (error) {
    case ERROR_WINHTTP_TIMEOUT:
      return QStringLiteral("下载超时，正在重试");
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
      return QStringLiteral("无法解析下载服务器地址（DNS）");
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR:
      return QStringLiteral("下载连接失败，正在切换线路");
    case ERROR_WINHTTP_SECURE_FAILURE:
      return QStringLiteral("TLS/证书校验失败，无法安全下载");
    case ERROR_WINHTTP_OPERATION_CANCELLED:
      return QStringLiteral("下载已取消");
    case ERROR_WINHTTP_INVALID_URL:
      return QStringLiteral("下载地址无效");
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
      return QStringLiteral("不支持的下载协议");
    default:
      return QStringLiteral("下载请求失败（WinHTTP 错误 %1）").arg(error);
  }
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
    parsed.error = QStringLiteral("下载地址格式无效");
    return parsed;
  }
  auto component = [](LPWSTR text, DWORD length) {
    if (text == nullptr || length == 0) {
      return QString();
    }
    return QString::fromWCharArray(text, static_cast<int>(length));
  };
  const QString scheme =
      component(components.lpszScheme, components.dwSchemeLength).toLower();
  // 只允许 HTTPS；CDN 的签名地址不支持明文 HTTP。
  if (scheme != QStringLiteral("https")) {
    parsed.error = QStringLiteral("下载只支持 HTTPS 地址");
    return parsed;
  }
  parsed.secure = true;
  parsed.host = component(components.lpszHostName, components.dwHostNameLength);
  if (parsed.host.isEmpty()) {
    parsed.error = QStringLiteral("下载地址缺少主机名");
    return parsed;
  }
  parsed.port = components.nPort;
  QString path = component(components.lpszUrlPath, components.dwUrlPathLength);
  if (path.isEmpty()) {
    path = QStringLiteral("/");
  }
  const QString extra =
      component(components.lpszExtraInfo, components.dwExtraInfoLength);
  parsed.object = path + extra;
  parsed.log_path = path;  // 日志不带查询串（签名参数）
  parsed.ok = true;
  return parsed;
}

struct TransferState {
  std::mutex mutex;
  std::condition_variable cv;
  // WinHTTP 异步回调可能在本机回环上同步重入，递归锁串行化 API 调用。
  std::recursive_mutex api_mutex;
  std::atomic<int> refs{1};

  HINTERNET session = nullptr;
  HINTERNET connection = nullptr;
  HINTERNET request = nullptr;

  std::atomic<bool> closing{false};
  std::atomic<bool> handle_closed{false};
  std::atomic<bool> cancelled{false};
  std::atomic<bool> timed_out{false};

  bool done = false;
  bool failed = false;
  bool response_requested = false;
  bool reading = false;

  DownloadTransportStatus status = DownloadTransportStatus::kOk;
  int http_status = 0;
  std::int64_t content_length = -1;
  std::int64_t content_range_start = -1;
  std::int64_t content_range_end = -1;
  std::int64_t content_range_total = -1;
  std::int64_t offset = 0;
  std::int64_t expected_total = -1;
  std::int64_t bytes_written = 0;
  std::int64_t first_byte_ns = 0;
  std::int64_t start_ns = 0;
  bool range_ignored = false;
  bool start_mismatch = false;
  bool total_mismatch = false;
  QString error;
  DWORD win32_error = 0;
  IDownloadSink* sink = nullptr;
  std::array<char, kReadBufferBytes> read_buffer{};

  void AddRef() { refs.fetch_add(1, std::memory_order_relaxed); }

  void Release() {
    if (refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }

  void Finish(bool success, DownloadTransportStatus result_status,
              const QString& message, DWORD code) {
    std::lock_guard<std::mutex> lock(mutex);
    if (done) {
      return;
    }
    done = true;
    failed = !success;
    status = result_status;
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

QString QueryHeaderString(detail::WinHttpApi& api, HINTERNET request,
                          LPCWSTR name) {
  DWORD size = 0;
  api.query_headers(request, WINHTTP_QUERY_CUSTOM, name, nullptr, &size,
                    WINHTTP_NO_HEADER_INDEX);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size < 2) {
    return QString();
  }
  std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
  if (!api.query_headers(request, WINHTTP_QUERY_CUSTOM, name, buffer.data(),
                         &size, WINHTTP_NO_HEADER_INDEX)) {
    return QString();
  }
  return QString::fromWCharArray(buffer.data()).trimmed();
}

std::int64_t QueryContentLength(detail::WinHttpApi& api, HINTERNET request) {
  DWORD64 length = 0;
  DWORD size = sizeof(length);
  if (!api.query_headers(
          request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &length, &size,
          WINHTTP_NO_HEADER_INDEX)) {
    return -1;
  }
  return static_cast<std::int64_t>(length);
}

void ParseContentRange(const QString& text, std::int64_t* start,
                       std::int64_t* end, std::int64_t* total) {
  static const QRegularExpression pattern(
      QStringLiteral("^bytes\\s+(\\d+)-(\\d+)/(\\d+|\\*)$"),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch match = pattern.match(text.trimmed());
  if (!match.hasMatch()) {
    return;
  }
  bool ok = false;
  const qlonglong parsed_start = match.captured(1).toLongLong(&ok);
  if (!ok) {
    return;
  }
  const qlonglong parsed_end = match.captured(2).toLongLong(&ok);
  if (!ok) {
    return;
  }
  *start = parsed_start;
  *end = parsed_end;
  if (match.captured(3) != QStringLiteral("*")) {
    const qlonglong parsed_total = match.captured(3).toLongLong(&ok);
    if (ok) {
      *total = parsed_total;
    }
  }
}

void StartRead(TransferState* state, HINTERNET request) {
  detail::WinHttpApi& api = detail::WinHttpApiInstance();
  state->reading = true;
  if (!api.read_data(request, state->read_buffer.data(),
                     static_cast<DWORD>(state->read_buffer.size()), nullptr)) {
    state->reading = false;
    const DWORD error = GetLastError();
    state->Finish(false, DownloadTransportStatus::kNetwork,
                  MapWinHttpError(error), error);
  }
}

void OnHeaders(TransferState* state, HINTERNET request) {
  detail::WinHttpApi& api = detail::WinHttpApiInstance();
  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (!api.query_headers(request,
                         WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                         WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                         WINHTTP_NO_HEADER_INDEX)) {
    const DWORD error = GetLastError();
    state->Finish(false, DownloadTransportStatus::kNetwork,
                  MapWinHttpError(error), error);
    return;
  }
  state->http_status = static_cast<int>(status);
  state->content_length = QueryContentLength(api, request);
  const QString content_range =
      QueryHeaderString(api, request, L"Content-Range");
  ParseContentRange(content_range, &state->content_range_start,
                    &state->content_range_end, &state->content_range_total);

  if (state->http_status >= 400) {
    state->Finish(false, DownloadTransportStatus::kHttpStatus,
                  QStringLiteral("取流失败（HTTP %1）").arg(state->http_status),
                  0);
    return;
  }
  if (state->http_status == 200) {
    if (state->offset > 0) {
      // 服务端忽略 Range：不读 body，交给 DownloadJob 从头重下；绝不能 append。
      state->range_ignored = true;
      state->Finish(false, DownloadTransportStatus::kRangeIgnored,
                    QStringLiteral("服务器不支持断点续传（已返回全量）"), 0);
      return;
    }
    if (state->expected_total > 0 && state->content_length >= 0 &&
        state->content_length != state->expected_total) {
      state->total_mismatch = true;
      state->Finish(false, DownloadTransportStatus::kTotalMismatch,
                    QStringLiteral("下载总大小与取流信息不一致"), 0);
      return;
    }
    StartRead(state, request);
    return;
  }
  if (state->http_status == 206) {
    if (state->content_range_start < 0 || state->content_range_total < 0) {
      state->Finish(false, DownloadTransportStatus::kHttpStatus,
                    QStringLiteral("服务器返回的 Range 信息不完整"), 0);
      return;
    }
    if (state->content_range_start != state->offset) {
      state->start_mismatch = true;
      state->Finish(false, DownloadTransportStatus::kRangeStartMismatch,
                    QStringLiteral("服务器返回的续传起点与请求不一致"), 0);
      return;
    }
    if (state->expected_total > 0 &&
        state->content_range_total != state->expected_total) {
      state->total_mismatch = true;
      state->Finish(false, DownloadTransportStatus::kTotalMismatch,
                    QStringLiteral("续传文件总大小与取流信息不一致"), 0);
      return;
    }
    StartRead(state, request);
    return;
  }
  state->Finish(false, DownloadTransportStatus::kHttpStatus,
                QStringLiteral("取流返回了不支持的状态（HTTP %1）")
                    .arg(state->http_status),
                0);
}

void OnRead(TransferState* state, HINTERNET request, DWORD length) {
  state->reading = false;
  if (length == 0) {
    state->Finish(true, DownloadTransportStatus::kOk, QString(), 0);
    return;
  }
  if (state->closing.load()) {
    return;
  }
  if (state->first_byte_ns == 0) {
    state->first_byte_ns = NowNs();
  }
  const std::int64_t chunk = static_cast<std::int64_t>(length);
  if (state->expected_total > 0 &&
      state->offset + state->bytes_written + chunk > state->expected_total) {
    state->total_mismatch = true;
    state->Finish(false, DownloadTransportStatus::kTotalMismatch,
                  QStringLiteral("服务器返回的数据超过已知总大小"), 0);
    return;
  }
  QString sink_error;
  if (state->sink == nullptr ||
      !state->sink->Write(
          reinterpret_cast<const std::uint8_t*>(state->read_buffer.data()),
          static_cast<std::size_t>(length), &sink_error)) {
    state->Finish(false, DownloadTransportStatus::kSinkError,
                  sink_error.isEmpty() ? QStringLiteral("写入缓存文件失败")
                                       : sink_error,
                  0);
    return;
  }
  state->bytes_written += chunk;
  StartRead(state, request);
}

void HandleCallback(HINTERNET handle, DWORD_PTR context, DWORD code, void* info,
                    DWORD length) {
  auto* state = reinterpret_cast<TransferState*>(context);
  if (state == nullptr) {
    return;
  }
  if (code == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->handle_closed = true;
    }
    state->cv.notify_all();
    state->Release();
    return;
  }
  if (code == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
    DWORD error = ERROR_WINHTTP_CONNECTION_ERROR;
    if (info != nullptr) {
      error = static_cast<DWORD>(
          static_cast<const WINHTTP_ASYNC_RESULT*>(info)->dwError);
    }
    if (!state->closing.load()) {
      state->Finish(false, DownloadTransportStatus::kNetwork,
                    MapWinHttpError(error), error);
    }
    return;
  }
  if (code == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE) {
    if (!state->closing.load()) {
      state->Finish(false, DownloadTransportStatus::kNetwork,
                    QStringLiteral("TLS/证书校验失败，无法安全下载"),
                    ERROR_WINHTTP_SECURE_FAILURE);
    }
    return;
  }

  detail::WinHttpApi& api = detail::WinHttpApiInstance();
  std::lock_guard<std::recursive_mutex> api_lock(state->api_mutex);
  if (state->closing.load()) {
    return;
  }
  switch (code) {
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
      if (!state->response_requested) {
        state->response_requested = true;
        if (!api.receive_response(handle, nullptr)) {
          const DWORD error = GetLastError();
          state->Finish(false, DownloadTransportStatus::kNetwork,
                        MapWinHttpError(error), error);
        }
      }
      break;
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
      if (!state->reading) {
        OnHeaders(state, handle);
      }
      break;
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
      OnRead(state, handle, length);
      break;
    default:
      break;
  }
}

void CALLBACK WinHttpStatusCallback(HINTERNET handle, DWORD_PTR context,
                                    DWORD code, void* info, DWORD length) {
  HandleCallback(handle, context, code, info, length);
}

}  // namespace

QString DownloadTransportStatusName(DownloadTransportStatus status) {
  switch (status) {
    case DownloadTransportStatus::kOk:
      return QStringLiteral("ok");
    case DownloadTransportStatus::kCancelled:
      return QStringLiteral("cancelled");
    case DownloadTransportStatus::kTimeout:
      return QStringLiteral("timeout");
    case DownloadTransportStatus::kNetwork:
      return QStringLiteral("network");
    case DownloadTransportStatus::kHttpStatus:
      return QStringLiteral("http");
    case DownloadTransportStatus::kRangeIgnored:
      return QStringLiteral("range-ignored");
    case DownloadTransportStatus::kRangeStartMismatch:
      return QStringLiteral("range-start-mismatch");
    case DownloadTransportStatus::kTotalMismatch:
      return QStringLiteral("total-mismatch");
    case DownloadTransportStatus::kInvalidUrl:
      return QStringLiteral("invalid-url");
    case DownloadTransportStatus::kSinkError:
      return QStringLiteral("sink-error");
  }
  return QStringLiteral("unknown");
}

DownloadTransportResult WinHttpDownloadTransport::GetRange(
    const DownloadRequest& request, IDownloadSink* sink,
    const HttpCancelToken& cancel) {
  DownloadTransportResult result;
  QElapsedTimer timer;
  timer.start();

  if (cancel.IsCancelled()) {
    result.status = DownloadTransportStatus::kCancelled;
    result.cancelled = true;
    result.error = QStringLiteral("下载已取消");
    result.elapsed_ms = timer.elapsed();
    return result;
  }
  if (request.offset < 0 || request.url.isEmpty() || sink == nullptr) {
    result.status = DownloadTransportStatus::kInvalidUrl;
    result.error = QStringLiteral("下载请求参数无效");
    result.elapsed_ms = timer.elapsed();
    return result;
  }

  detail::WinHttpApi& api = detail::WinHttpApiInstance();
  QString load_error;
  if (!api.Load(&load_error)) {
    result.status = DownloadTransportStatus::kNetwork;
    result.error = QStringLiteral("系统网络组件不可用：") + load_error;
    result.elapsed_ms = timer.elapsed();
    return result;
  }
  const ParsedUrl parsed = ParseUrl(request.url);
  if (!parsed.ok) {
    result.status = DownloadTransportStatus::kInvalidUrl;
    result.error = parsed.error;
    result.elapsed_ms = timer.elapsed();
    return result;
  }

  if (OnlineLogEnabled()) {
    std::fprintf(stderr, "T8.5 cdn_request host=%s path=%s offset=%lld\n",
                 parsed.host.toUtf8().constData(),
                 parsed.log_path.toUtf8().constData(),
                 static_cast<long long>(request.offset));
    std::fflush(stderr);
  }

  auto* state = new TransferState();
  state->offset = request.offset;
  state->expected_total = request.expected_total;
  state->sink = sink;
  state->start_ns = NowNs();

  const int timeout_ms = request.timeout_ms > 0 ? request.timeout_ms : 20000;
  auto fail = [&](const QString& message, DWORD code,
                  DownloadTransportStatus status) {
    result.status = status;
    result.error = message;
    result.win32_error = static_cast<int>(code);
    result.elapsed_ms = timer.elapsed();
    if (OnlineLogEnabled()) {
      std::fprintf(stderr,
                   "T8.5 cdn_done host=%s status=0 elapsed_ms=%lld error=%s\n",
                   parsed.host.toUtf8().constData(),
                   static_cast<long long>(result.elapsed_ms),
                   message.toUtf8().constData());
      std::fflush(stderr);
    }
  };

  state->session = api.open(L"Lapian/0.1 (T8.5 online fetch)",
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                            WINHTTP_FLAG_ASYNC);
  if (state->session == nullptr) {
    const DWORD error = GetLastError();
    state->Release();
    fail(MapWinHttpError(error), error, DownloadTransportStatus::kNetwork);
    return result;
  }
  api.set_timeouts(state->session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
  state->connection =
      api.connect(state->session, parsed.host.toStdWString().c_str(),
                  parsed.port, 0);
  if (state->connection == nullptr) {
    const DWORD error = GetLastError();
    CloseHandleIfPresent(api, &state->session);
    state->Release();
    fail(MapWinHttpError(error), error, DownloadTransportStatus::kNetwork);
    return result;
  }
  state->request = api.open_request(
      state->connection, L"GET", parsed.object.toStdWString().c_str(), nullptr,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (state->request == nullptr) {
    const DWORD error = GetLastError();
    CloseHandleIfPresent(api, &state->connection);
    CloseHandleIfPresent(api, &state->session);
    state->Release();
    fail(MapWinHttpError(error), error, DownloadTransportStatus::kNetwork);
    return result;
  }

  DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  if (!api.set_option(state->request, WINHTTP_OPTION_REDIRECT_POLICY,
                      &redirect_policy, sizeof(redirect_policy))) {
    const DWORD error = GetLastError();
    CloseHandleIfPresent(api, &state->request);
    CloseHandleIfPresent(api, &state->connection);
    CloseHandleIfPresent(api, &state->session);
    state->Release();
    fail(QStringLiteral("无法关闭 HTTP 重定向，已拒绝下载"), error,
         DownloadTransportStatus::kNetwork);
    return result;
  }
  DWORD disable_cookies = WINHTTP_DISABLE_COOKIES;
  api.set_option(state->request, WINHTTP_OPTION_DISABLE_FEATURE,
                 &disable_cookies, sizeof(disable_cookies));

  // CDN 请求绝不带 Cookie：这里只加 Range/UA/Referer/Accept-Encoding。
  std::wstring range = L"Range: bytes=" + std::to_wstring(request.offset);
  if (request.range_end >= request.offset) {
    // T8.10：DASH 总字节探测用 bytes=offset-end（通常 0-0）。
    range += L"-" + std::to_wstring(request.range_end);
  } else {
    range += L"-";
  }
  api.add_request_headers(state->request, range.c_str(),
                          static_cast<DWORD>(-1),
                          WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
  api.add_request_headers(state->request, L"Accept-Encoding: identity",
                          static_cast<DWORD>(-1),
                          WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
  const QString user_agent =
      request.user_agent.isEmpty()
          ? QStringLiteral(
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/120.0 Safari/537.36")
          : request.user_agent;
  api.add_request_headers(state->request, user_agent.toStdWString().c_str(),
                          static_cast<DWORD>(-1),
                          WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
  const QString referer = request.referer.isEmpty()
                              ? QStringLiteral("https://www.bilibili.com/")
                              : request.referer;
  const std::wstring referer_header = L"Referer: " + referer.toStdWString();
  api.add_request_headers(state->request, referer_header.c_str(),
                          static_cast<DWORD>(-1),
                          WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

  if (api.set_status_callback(state->request, &WinHttpStatusCallback,
                              WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,
                              0) == WINHTTP_INVALID_STATUS_CALLBACK) {
    const DWORD error = GetLastError();
    CloseHandleIfPresent(api, &state->request);
    CloseHandleIfPresent(api, &state->connection);
    CloseHandleIfPresent(api, &state->session);
    state->Release();
    fail(QStringLiteral("无法注册下载回调"), error,
         DownloadTransportStatus::kNetwork);
    return result;
  }
  state->AddRef();  // 回调引用；HANDLE_CLOSING 时释放

  if (!api.send_request(state->request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                        WINHTTP_NO_REQUEST_DATA, 0, 0,
                        reinterpret_cast<DWORD_PTR>(state))) {
    const DWORD error = GetLastError();
    state->Finish(false, DownloadTransportStatus::kNetwork,
                  MapWinHttpError(error), error);
  }

  const auto deadline =
      Clock::now() + std::chrono::milliseconds(timeout_ms);
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    while (!state->done) {
      if (cancel.IsCancelled()) {
        state->cancelled.store(true);
        break;
      }
      if (Clock::now() >= deadline) {
        state->timed_out.store(true);
        break;
      }
      state->cv.wait_for(lock, std::chrono::milliseconds(kCancelPollMs));
    }
  }

  // 关闭请求句柄；调用方持有 api_mutex 再置 closing，保证回调不会在
  // sink 生命周期结束后继续写盘（sink 属于 DownloadJob）。
  if (!state->handle_closed.load()) {
    HINTERNET request_to_close = nullptr;
    {
      std::lock_guard<std::recursive_mutex> lock(state->api_mutex);
      if (!state->closing.exchange(true)) {
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
  if (state->handle_closed.load()) {
    CloseHandleIfPresent(api, &state->connection);
    CloseHandleIfPresent(api, &state->session);
  } else {
    // 极少数驱动/网络组件不回调 HANDLE_CLOSING：仍关闭会话，避免句柄泄漏；
    // state 由引用计数保护，closing 已置位后回调不再碰 sink。
    CloseHandleIfPresent(api, &state->connection);
    CloseHandleIfPresent(api, &state->session);
  }

  result.http_status = state->http_status;
  result.content_length = state->content_length;
  result.content_range_start = state->content_range_start;
  result.content_range_end = state->content_range_end;
  result.content_range_total = state->content_range_total;
  result.bytes_written = state->bytes_written;
  result.range_ignored = state->range_ignored;
  result.start_mismatch = state->start_mismatch;
  result.total_mismatch = state->total_mismatch;
  if (state->first_byte_ns > 0) {
    result.first_byte_ms =
        (state->first_byte_ns - state->start_ns) / (1000 * 1000);
  }
  result.elapsed_ms = timer.elapsed();

  if (state->cancelled.load() || cancel.IsCancelled()) {
    result.status = DownloadTransportStatus::kCancelled;
    result.cancelled = true;
    result.error = QStringLiteral("下载已取消");
  } else if (state->timed_out.load()) {
    result.status = DownloadTransportStatus::kTimeout;
    result.timed_out = true;
    result.error = QStringLiteral("下载超时");
  } else if (state->failed) {
    result.status = state->status;
    result.error = state->error.isEmpty()
                       ? QStringLiteral("下载失败，请重试")
                       : state->error;
    result.win32_error = static_cast<int>(state->win32_error);
  } else {
    result.status = DownloadTransportStatus::kOk;
    result.ok = true;
  }

  if (OnlineLogEnabled()) {
    std::fprintf(stderr,
                 "T8.5 cdn_done host=%s path=%s status=%d elapsed_ms=%lld "
                 "bytes=%lld range_ignored=%d error=%s\n",
                 parsed.host.toUtf8().constData(),
                 parsed.log_path.toUtf8().constData(), result.http_status,
                 static_cast<long long>(result.elapsed_ms),
                 static_cast<long long>(result.bytes_written),
                 result.range_ignored ? 1 : 0,
                 result.error.toUtf8().constData());
    std::fflush(stderr);
  }

  state->Release();
  return result;
}

bool WinHttpDownloadTransport::LibraryLoadedForTest() {
  return detail::WinHttpApiInstance().loaded();
}

}  // namespace lapian::online
