#include "online/CurlDownloadTransport.h"

#include <QRegularExpression>
#include <QUrl>

#include <atomic>
#include <chrono>
#include <mutex>

#include <curl/curl.h>

namespace lapian::online {
namespace {

std::atomic<bool> g_used{false};
std::once_flag g_curl_once;

struct TransferState {
  const DownloadRequest* request = nullptr;
  IDownloadSink* sink = nullptr;
  const HttpCancelToken* cancel = nullptr;
  DownloadTransportResult* result = nullptr;
  QString sink_error;
  bool html_response = false;
  bool stopped_for_headers = false;
  std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
};

size_t WriteTransfer(char* data, size_t size, size_t count, void* user_data) {
  auto* state = static_cast<TransferState*>(user_data);
  const std::size_t bytes = size * count;
  if (state->result->first_byte_ms < 0) {
    state->result->first_byte_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - state->started)
                                       .count();
  }
  if (state->cancel->IsCancelled()) {
    return 0;
  }
  if (state->html_response) {
    return 0;
  }
  if (state->request->headers_only) {
    state->stopped_for_headers = true;
    return 0;
  }
  if (state->sink == nullptr ||
      !state->sink->Write(reinterpret_cast<const std::uint8_t*>(data), bytes,
                          &state->sink_error)) {
    if (state->sink_error.isEmpty()) {
      state->sink_error = QStringLiteral("写入缓存文件失败");
    }
    return 0;
  }
  state->result->bytes_written += static_cast<std::int64_t>(bytes);
  return bytes;
}

size_t ReadTransferHeader(char* data, size_t size, size_t count, void* user_data) {
  auto* state = static_cast<TransferState*>(user_data);
  auto* result = state->result;
  const QString line = QString::fromLatin1(data, static_cast<qsizetype>(size * count)).trimmed();
  const int colon = line.indexOf(':');
  if (colon > 0) {
    const QString name = line.left(colon).trimmed();
    const QString value = line.mid(colon + 1).trimmed();
    if (name.compare(QStringLiteral("Content-Type"), Qt::CaseInsensitive) == 0 &&
        value.contains(QStringLiteral("text/html"), Qt::CaseInsensitive)) {
      state->html_response = true;
    } else if (name.compare(QStringLiteral("Content-Length"), Qt::CaseInsensitive) == 0) {
      result->content_length = value.toLongLong();
    } else if (name.compare(QStringLiteral("Content-Range"), Qt::CaseInsensitive) == 0) {
      static const QRegularExpression pattern(
          QStringLiteral("^bytes\\s+(\\d+)-(\\d+)/(\\d+|\\*)$"),
          QRegularExpression::CaseInsensitiveOption);
      const QRegularExpressionMatch match = pattern.match(value);
      if (match.hasMatch()) {
        result->content_range_start = match.captured(1).toLongLong();
        result->content_range_end = match.captured(2).toLongLong();
        if (match.captured(3) != QStringLiteral("*")) {
          result->content_range_total = match.captured(3).toLongLong();
        }
      }
    }
  }
  return size * count;
}

int TransferProgress(void* user_data, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  const auto* state = static_cast<const TransferState*>(user_data);
  return state->cancel->IsCancelled() ? 1 : 0;
}

bool ValidateHttps(const QString& text) {
  const QUrl url(text);
  return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty() &&
         url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0;
}

DownloadTransportStatus HttpStatusError(DownloadTransportResult* result) {
  result->error = QStringLiteral("HTTP 状态错误（%1）").arg(result->http_status);
  return DownloadTransportStatus::kHttpStatus;
}

}  // namespace

DownloadTransportResult CurlDownloadTransport::GetRange(
    const DownloadRequest& request, IDownloadSink* sink,
    const HttpCancelToken& cancel) {
  g_used.store(true);
  DownloadTransportResult result;
  const auto started = std::chrono::steady_clock::now();
  const auto finish = [&](DownloadTransportStatus status) {
    result.status = status;
    result.ok = status == DownloadTransportStatus::kOk;
    result.cancelled = status == DownloadTransportStatus::kCancelled;
    result.timed_out = status == DownloadTransportStatus::kTimeout;
    result.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started)
                            .count();
    return result;
  };

  if (!ValidateHttps(request.url)) {
    result.error = QStringLiteral("只允许有效的 HTTPS URL");
    return finish(DownloadTransportStatus::kInvalidUrl);
  }
  if (request.offset < 0 || (request.range_end >= 0 && request.range_end < request.offset)) {
    result.error = QStringLiteral("Range 范围无效");
    return finish(DownloadTransportStatus::kInvalidUrl);
  }

  std::call_once(g_curl_once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
  CURL* handle = curl_easy_init();
  if (handle == nullptr) {
    result.error = QStringLiteral("无法初始化下载客户端");
    return finish(DownloadTransportStatus::kNetwork);
  }

  TransferState state{&request, sink, &cancel, &result};
  curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Accept-Encoding: identity");
  const QString user_agent =
      request.user_agent.isEmpty()
          ? QStringLiteral(
                "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/120.0 Safari/537.36")
          : request.user_agent;
  headers = curl_slist_append(headers, "User-Agent: " + user_agent.toUtf8());
  const QString referer = request.referer.isEmpty()
                              ? QStringLiteral("https://www.bilibili.com/")
                              : request.referer;
  headers = curl_slist_append(headers, "Referer: " + referer.toUtf8());

  const QByteArray url = request.url.toUtf8();
  const QByteArray range = request.range_end >= 0
                               ? QByteArray::number(request.offset) + "-" +
                                     QByteArray::number(request.range_end)
                               : QByteArray::number(request.offset) + "-";
  curl_easy_setopt(handle, CURLOPT_URL, url.constData());
  curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(handle, CURLOPT_RANGE, range.constData());
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, WriteTransfer);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &state);
  curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, ReadTransferHeader);
  curl_easy_setopt(handle, CURLOPT_HEADERDATA, &state);
  curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, TransferProgress);
  curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &state);
  curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS,
                   static_cast<long>(request.timeout_ms > 0 ? request.timeout_ms : 20000));
  curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
  curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "https");
  curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);

  const CURLcode code = curl_easy_perform(handle);
  long status = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
  result.http_status = static_cast<int>(status);
  curl_slist_free_all(headers);
  curl_easy_cleanup(handle);

  if (state.html_response) {
    result.error = QStringLiteral("CDN 返回 HTML 错误页，未写入媒体缓存");
    return finish(DownloadTransportStatus::kNetwork);
  }
  if (code != CURLE_OK && !state.stopped_for_headers) {
    result.win32_error = static_cast<int>(code);
    if (cancel.IsCancelled() || code == CURLE_ABORTED_BY_CALLBACK) {
      result.error = QStringLiteral("下载已取消");
      return finish(DownloadTransportStatus::kCancelled);
    }
    if (code == CURLE_OPERATION_TIMEDOUT) {
      result.error = QStringLiteral("下载超时");
      return finish(DownloadTransportStatus::kTimeout);
    }
    if (!state.sink_error.isEmpty()) {
      result.error = state.sink_error;
      return finish(DownloadTransportStatus::kSinkError);
    }
    result.error = QStringLiteral("网络下载失败：") +
                   QString::fromLatin1(curl_easy_strerror(code));
    return finish(DownloadTransportStatus::kNetwork);
  }
  if (status < 200 || status >= 300) {
    return finish(HttpStatusError(&result));
  }
  if (request.offset > 0 && status == 200) {
    result.range_ignored = true;
    result.error = QStringLiteral("服务端忽略了 Range 续传");
    return finish(DownloadTransportStatus::kRangeIgnored);
  }
  if (request.offset > 0 && status != 206) {
    result.error = QStringLiteral("续传响应不是 206");
    return finish(DownloadTransportStatus::kHttpStatus);
  }
  if (status == 206 && result.content_range_start != request.offset) {
    result.start_mismatch = true;
    result.error = QStringLiteral("Content-Range 起点不一致");
    return finish(DownloadTransportStatus::kRangeStartMismatch);
  }

  const std::int64_t total = result.content_range_total >= 0
                                 ? result.content_range_total
                                 : (status == 200 ? result.content_length : -1);
  if (request.expected_total > 0 && total >= 0 && total != request.expected_total) {
    result.total_mismatch = true;
    result.error = QStringLiteral("响应总长度与预期不一致");
    return finish(DownloadTransportStatus::kTotalMismatch);
  }
  return finish(DownloadTransportStatus::kOk);
}

bool CurlDownloadTransport::LibraryLoadedForTest() { return g_used.load(); }

}  // namespace lapian::online
