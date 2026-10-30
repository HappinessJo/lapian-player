#include "online/CurlHttpClient.h"

#include <QUrl>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>

#include <curl/curl.h>

#include "online/Sessdata.h"

namespace lapian::online {
namespace {

std::atomic<bool> g_used{false};
std::once_flag g_curl_once;

struct RequestState {
  const HttpCancelToken* cancel = nullptr;
  HttpResponse* response = nullptr;
  bool passport = false;
};

size_t WriteBody(char* data, size_t size, size_t count, void* user_data) {
  auto* state = static_cast<RequestState*>(user_data);
  const std::size_t bytes = size * count;
  if (state->cancel->IsCancelled()) {
    return 0;
  }
  state->response->body.append(data, static_cast<qsizetype>(bytes));
  return bytes;
}

size_t ReadHeader(char* data, size_t size, size_t count, void* user_data) {
  auto* state = static_cast<RequestState*>(user_data);
  const QString line = QString::fromUtf8(data, static_cast<qsizetype>(size * count)).trimmed();
  if (state->passport && line.startsWith(QStringLiteral("Set-Cookie:"), Qt::CaseInsensitive)) {
    const QString cookie = line.mid(line.indexOf(':') + 1).trimmed();
    const int end = cookie.indexOf(';');
    const SessdataNormalization normalized = NormalizeSessdata(cookie.left(end));
    if (normalized.ok) {
      state->response->response_sessdata = normalized.value;
    }
  }
  return size * count;
}

int Progress(void* user_data, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  const auto* state = static_cast<const RequestState*>(user_data);
  return state->cancel->IsCancelled() ? 1 : 0;
}

QString ValidateUrl(const QString& text) {
  const QUrl url(text);
  if (!url.isValid() || url.host().isEmpty()) {
    return QStringLiteral("URL 无效");
  }
  if (url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
    return QStringLiteral("只允许 HTTPS URL");
  }
  if (!url.userInfo().isEmpty()) {
    return QStringLiteral("URL 不得包含用户信息");
  }
  return {};
}

}  // namespace

HttpResponse CurlHttpClient::Get(const HttpRequest& request,
                                 const HttpCancelToken& cancel) {
  HttpResponse response;
  const auto started = std::chrono::steady_clock::now();
  const QUrl url(request.url);
  const bool has_sessdata = !request.sessdata.isEmpty();
  if (has_sessdata && !CookieAllowedForTest(request.url)) {
    response.error = QStringLiteral("只允许向 api.bilibili.com 发送凭据");
    return response;
  }
  response.error = ValidateUrl(request.url);
  if (!response.error.isEmpty()) {
    return response;
  }
  const SessdataNormalization normalized = NormalizeSessdata(request.sessdata);
  if (has_sessdata && !normalized.ok) {
    response.error = QStringLiteral("凭据格式无效：") + normalized.error;
    return response;
  }

  g_used.store(true);
  std::call_once(g_curl_once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
  CURL* handle = curl_easy_init();
  if (handle == nullptr) {
    response.error = QStringLiteral("无法初始化 HTTP 客户端");
    return response;
  }

  RequestState state{&cancel, &response,
                     url.host().compare(QStringLiteral("passport.bilibili.com"),
                                        Qt::CaseInsensitive) == 0};
  curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Accept: application/json");
  if (has_sessdata) {
    const QByteArray cookie = "Cookie: SESSDATA=" + normalized.value.toUtf8();
    headers = curl_slist_append(headers, cookie.constData());
    response.request_cookie_sent = true;
  }

  const QByteArray encoded = url.toEncoded(QUrl::FullyEncoded);
  curl_easy_setopt(handle, CURLOPT_URL, encoded.constData());
  curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, WriteBody);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &state);
  curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, ReadHeader);
  curl_easy_setopt(handle, CURLOPT_HEADERDATA, &state);
  curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, Progress);
  curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &state);
  curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS,
                   static_cast<long>(request.timeout_ms > 0 ? request.timeout_ms : 6000));
  curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
  curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "https");
  curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);

  const CURLcode code = curl_easy_perform(handle);
  long status = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
  response.http_status = static_cast<int>(status);
  if (code != CURLE_OK) {
    response.cancelled = cancel.IsCancelled() || code == CURLE_ABORTED_BY_CALLBACK;
    response.timed_out = code == CURLE_OPERATION_TIMEDOUT;
    response.win32_error = static_cast<int>(code);
    response.error = response.cancelled
                         ? QStringLiteral("请求已取消")
                         : (response.timed_out ? QStringLiteral("请求超时")
                                               : QStringLiteral("网络请求失败：") +
                                                     QString::fromLatin1(curl_easy_strerror(code)));
  } else {
    response.ok = status >= 200 && status < 300;
    if (!response.ok) {
      response.error = QStringLiteral("HTTP 状态错误（%1）").arg(status);
    }
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(handle);
  response.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started)
                            .count();
  return response;
}

bool CurlHttpClient::LibraryLoadedForTest() { return g_used.load(); }

QString CurlHttpClient::ValidateUrlForTest(const QString& url) { return ValidateUrl(url); }

bool CurlHttpClient::CookieAllowedForTest(const QString& text) {
  const QUrl url(text);
  return ValidateUrl(text).isEmpty() &&
         url.host().compare(QStringLiteral("api.bilibili.com"), Qt::CaseInsensitive) == 0 &&
         url.port(443) == 443;
}

}  // namespace lapian::online
