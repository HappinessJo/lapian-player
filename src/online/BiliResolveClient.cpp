#include "online/BiliResolveClient.h"

#include <QElapsedTimer>

#include <cstdio>

namespace lapian::online {
namespace {

bool OnlineLogEnabled() {
  static const bool enabled = qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG");
  return enabled;
}

BiliResolveError CancelledError() {
  BiliResolveError error;
  error.kind = BiliErrorKind::kCancelled;
  error.message = QStringLiteral("解析已取消");
  return error;
}

BiliResolveError NetworkError(const HttpResponse& response) {
  BiliResolveError error;
  if (response.http_status == 412 || response.http_status == 429) {
    error.kind = BiliErrorKind::kHttp;
    error.http_status = response.http_status;
    error.message = QStringLiteral("请求被拒绝，请稍后重试（HTTP %1）")
                        .arg(response.http_status);
    return error;
  }
  if (response.http_status >= 400) {
    error.kind = BiliErrorKind::kHttp;
    error.http_status = response.http_status;
    error.message =
        QStringLiteral("请求失败（HTTP %1），请稍后重试").arg(response.http_status);
    return error;
  }
  error.kind = BiliErrorKind::kNetwork;
  error.message = response.error.isEmpty()
                      ? QStringLiteral("网络请求失败，请检查网络后重试")
                      : response.error;
  return error;
}

void LogResult(const BiliInput& input, const BiliResolveResult& result) {
  if (!OnlineLogEnabled()) {
    return;
  }
  if (result.error.ok()) {
    const BiliPage* page = result.video.selected_page();
    std::fprintf(stderr,
                 "T8.1 resolve_ok bvid=%s page=%d cid=%lld requests=%d "
                 "elapsed_ms=%lld title=%s\n",
                 result.video.bvid.toUtf8().constData(),
                 page == nullptr ? 0 : page->page,
                 page == nullptr ? 0LL : static_cast<long long>(page->cid),
                 result.http_requests, static_cast<long long>(result.elapsed_ms),
                 result.video.title.toUtf8().constData());
  } else {
    std::fprintf(stderr,
                 "T8.1 resolve_error bvid=%s page=%d kind=%s api_code=%d "
                 "http_status=%d requests=%d elapsed_ms=%lld message=%s\n",
                 input.bvid.toUtf8().constData(), input.page,
                 ErrorKindName(result.error.kind).toUtf8().constData(),
                 result.error.api_code, result.error.http_status,
                 result.http_requests, static_cast<long long>(result.elapsed_ms),
                 result.error.message.toUtf8().constData());
  }
  std::fflush(stderr);
}

}  // namespace

BiliResolveResult BiliResolveClient::Resolve(const QString& input_text,
                                             const HttpCancelToken& cancel) {
  QElapsedTimer timer;
  timer.start();

  BiliResolveResult result;
  const BiliInput input = NormalizeInput(input_text);
  if (!input.ok) {
    result.error = input.error;
    result.elapsed_ms = timer.elapsed();
    LogResult(input, result);
    return result;
  }

  if (cancel.IsCancelled()) {
    result.error = CancelledError();
    result.elapsed_ms = timer.elapsed();
    LogResult(input, result);
    return result;
  }

  if (http_ == nullptr) {
    result.error.kind = BiliErrorKind::kNetwork;
    result.error.message = QStringLiteral("网络客户端未初始化");
    result.elapsed_ms = timer.elapsed();
    LogResult(input, result);
    return result;
  }

  ++request_count_;
  HttpRequest request;
  request.url = BuildViewUrl(input.bvid);
  request.timeout_ms = 6000;
  const HttpResponse response = http_->Get(request, cancel);

  result.http_requests = request_count_;
  if (cancel.IsCancelled() || response.cancelled) {
    result.error = CancelledError();
    result.elapsed_ms = timer.elapsed();
    LogResult(input, result);
    return result;
  }
  if (response.timed_out) {
    result.error.kind = BiliErrorKind::kTimeout;
    result.error.message = QStringLiteral("网络超时，请重试");
    result.elapsed_ms = timer.elapsed();
    LogResult(input, result);
    return result;
  }
  if (!response.ok) {
    result.error = NetworkError(response);
    result.elapsed_ms = timer.elapsed();
    LogResult(input, result);
    return result;
  }

  BiliVideo video;
  result.error = ParseViewJson(response.body, &video);
  if (!result.error.ok()) {
    result.elapsed_ms = timer.elapsed();
    LogResult(input, result);
    return result;
  }

  // 无 ?p= 时默认 P1；带 ?p= 时按 view 的 pages[].page 精确选，越界给可读错误。
  const int requested_page = input.page_from_input ? input.page : 1;
  result.error = SelectPage(&video, requested_page);
  if (!result.error.ok()) {
    result.elapsed_ms = timer.elapsed();
    LogResult(input, result);
    return result;
  }

  result.video = video;
  result.elapsed_ms = timer.elapsed();
  LogResult(input, result);
  return result;
}

}  // namespace lapian::online
