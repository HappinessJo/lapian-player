#include "online/PlayUrl.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QUrl>
#include <QUrlQuery>

#include <cmath>

namespace lapian::online {
namespace {

constexpr int kDefaultQn = 64;  // 720P

BiliResolveError MakeError(BiliErrorKind kind, const QString& message,
                           const QString& detail = QString()) {
  BiliResolveError error;
  error.kind = kind;
  error.message = message;
  error.detail = detail;
  return error;
}

bool IsValidBvid(const QString& bvid) {
  if (bvid.size() != 12 || !bvid.startsWith(QStringLiteral("BV"))) {
    return false;
  }
  for (int i = 2; i < bvid.size(); ++i) {
    const QChar ch = bvid.at(i);
    if (!ch.isLetterOrNumber() || ch.unicode() > 0x7f) {
      return false;
    }
  }
  return true;
}

qint64 JsonToI64(const QJsonValue& value) {
  if (!value.isDouble()) {
    return 0;
  }
  const double number = value.toDouble();
  if (!std::isfinite(number) || number < 0.0 || number > 9.0e18) {
    return 0;
  }
  return static_cast<qint64>(number);
}

int JsonToInt(const QJsonValue& value) {
  const qint64 number = JsonToI64(value);
  if (number > 2147483647LL) {
    return 0;
  }
  return static_cast<int>(number);
}

bool IsHttpsUrl(const QString& url) {
  if (url.isEmpty() || url.size() > 8192) {
    return false;
  }
  const QUrl parsed(url);
  return parsed.isValid() &&
         parsed.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) ==
             0 &&
         !parsed.host().isEmpty();
}

BiliResolveError ParseDurl(const QJsonValue& value, DurlSegment* segment) {
  if (!value.isObject()) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("取流响应里的 durl 段格式无效"), "durl-shape");
  }
  const QJsonObject object = value.toObject();
  const QString url = object.value(QStringLiteral("url")).toString();
  if (!IsHttpsUrl(url)) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("取流响应里的播放地址无效"), "durl-url");
  }
  segment->url = url;
  segment->size = JsonToI64(object.value(QStringLiteral("size")));
  segment->length_ms = JsonToI64(object.value(QStringLiteral("length")));
  segment->order = JsonToInt(object.value(QStringLiteral("order")));
  const QJsonValue backups = object.value(QStringLiteral("backup_url"));
  if (backups.isArray()) {
    for (const QJsonValue& backup : backups.toArray()) {
      const QString backup_url = backup.toString();
      if (IsHttpsUrl(backup_url) && !segment->backup_urls.contains(backup_url)) {
        segment->backup_urls.append(backup_url);
      }
    }
  }
  return {};
}

BiliResolveError NetworkError(const HttpResponse& response) {
  BiliResolveError error;
  if (response.http_status == 412 || response.http_status == 429) {
    error.kind = BiliErrorKind::kHttp;
    error.http_status = response.http_status;
    error.message = QStringLiteral("取流请求被拒绝，请稍后重试（HTTP %1）")
                        .arg(response.http_status);
    return error;
  }
  if (response.http_status >= 400) {
    error.kind = BiliErrorKind::kHttp;
    error.http_status = response.http_status;
    error.message = QStringLiteral("取流请求失败（HTTP %1），请稍后重试")
                        .arg(response.http_status);
    return error;
  }
  error.kind = BiliErrorKind::kNetwork;
  error.message = response.error.isEmpty()
                      ? QStringLiteral("取流网络请求失败，请检查网络后重试")
                      : response.error;
  return error;
}

}  // namespace

QStringList DurlSegment::AllUrls() const {
  QStringList urls;
  if (!url.isEmpty()) {
    urls.append(url);
  }
  for (const QString& backup : backup_urls) {
    if (!backup.isEmpty() && !urls.contains(backup)) {
      urls.append(backup);
    }
  }
  return urls;
}

qint64 PlayUrlData::TotalBytes() const {
  if (durls.isEmpty()) {
    return 0;
  }
  return durls.first().size;
}

QString BuildPlayUrlUrl(const QString& bvid, qint64 cid, int qn, int fourk) {
  if (!IsValidBvid(bvid) || cid <= 0) {
    return QString();
  }
  const int quality = qn > 0 ? qn : kDefaultQn;
  QUrl url(QStringLiteral("https://api.bilibili.com/x/player/playurl"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("bvid"), bvid);
  query.addQueryItem(QStringLiteral("cid"), QString::number(cid));
  query.addQueryItem(QStringLiteral("qn"), QString::number(quality));
  query.addQueryItem(QStringLiteral("fnval"), QStringLiteral("0"));
  query.addQueryItem(QStringLiteral("fnver"), QStringLiteral("0"));
  query.addQueryItem(QStringLiteral("fourk"), fourk > 0 ? QStringLiteral("1")
                                                       : QStringLiteral("0"));
  url.setQuery(query);
  return url.toString();
}

BiliResolveError ParsePlayUrlJson(const QByteArray& json, PlayUrlData* out) {
  if (out == nullptr) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("取流解析没有输出位置"), "no-output");
  }
  *out = PlayUrlData();

  QJsonParseError parse_error;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("取流响应格式无法解析，请稍后重试"),
                     "json");
  }
  const QJsonObject root = document.object();
  const QJsonValue code_value = root.value(QStringLiteral("code"));
  if (!code_value.isDouble()) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("取流响应缺少错误码"), "no-code");
  }
  const int api_code = code_value.toInt();
  const QString api_message = root.value(QStringLiteral("message")).toString();
  if (api_code != 0) {
    return MapApiError(api_code, api_message);
  }

  const QJsonValue data_value = root.value(QStringLiteral("data"));
  if (!data_value.isObject()) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("取流响应缺少 data"), "no-data");
  }
  const QJsonObject data = data_value.toObject();
  out->quality = JsonToInt(data.value(QStringLiteral("quality")));
  out->format = data.value(QStringLiteral("format")).toString();
  out->timelength_ms = JsonToI64(data.value(QStringLiteral("timelength")));
  out->codecid = JsonToInt(data.value(QStringLiteral("video_codecid")));

  // codecid 缺失时，从 support_formats 里匹配实际 quality；durl 是单文件 MP4，
  // 兜底按 AVC(7) 记录（不参与档位判断）。
  if (out->codecid <= 0) {
    const QJsonValue formats = data.value(QStringLiteral("support_formats"));
    if (formats.isArray()) {
      for (const QJsonValue& value : formats.toArray()) {
        const QJsonObject format = value.toObject();
        if (JsonToInt(format.value(QStringLiteral("quality"))) == out->quality) {
          out->codecid = JsonToInt(format.value(QStringLiteral("codecid")));
          if (out->codecid > 0) {
            break;
          }
        }
      }
    }
  }
  if (out->codecid <= 0) {
    out->codecid = 7;
  }

  const QJsonValue durl_value = data.value(QStringLiteral("durl"));
  if (!durl_value.isArray() || durl_value.toArray().isEmpty()) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("当前样本没有 durl 单文件地址（M1 不做 DASH）"),
                     "no-durl");
  }
  const QJsonArray durls = durl_value.toArray();
  for (const QJsonValue& value : durls) {
    DurlSegment segment;
    BiliResolveError error = ParseDurl(value, &segment);
    if (!error.ok()) {
      return error;
    }
    out->durls.append(segment);
  }
  if (out->durls.first().size <= 0) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("取流响应没有给出可用的总大小，无法安全续传"),
                     "no-size");
  }
  if (out->quality <= 0) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("取流响应没有给出实际清晰度"), "no-quality");
  }
  return {};
}

QString QualityName(int qn) {
  switch (qn) {
    case 120:
      return QStringLiteral("4K");
    case 116:
      return QStringLiteral("1080P60");
    case 112:
      return QStringLiteral("1080P+");
    case 80:
      return QStringLiteral("1080P");
    case 74:
      return QStringLiteral("720P60");
    case 64:
      return QStringLiteral("720P");
    case 32:
      return QStringLiteral("480P");
    case 16:
      return QStringLiteral("360P");
    default:
      break;
  }
  return QStringLiteral("qn=%1").arg(qn);
}

bool IsM1Quality(int qn) { return qn >= 64; }

PlayUrlFetchResult FetchPlayUrl(IHttpClient* http, const QString& bvid, qint64 cid,
                                int qn, const PlayUrlFetchOptions& options,
                                const HttpCancelToken& cancel) {
  QElapsedTimer timer;
  timer.start();
  PlayUrlFetchResult result;

  if (!IsValidBvid(bvid)) {
    result.error = MakeError(BiliErrorKind::kInvalidInput,
                             QStringLiteral("BV 号格式不对，应为 BV 开头的 12 位"),
                             "invalid-bvid");
    result.elapsed_ms = timer.elapsed();
    return result;
  }
  if (cid <= 0) {
    result.error = MakeError(BiliErrorKind::kInvalidInput,
                             QStringLiteral("缺少有效的 cid，无法取流"), "invalid-cid");
    result.elapsed_ms = timer.elapsed();
    return result;
  }
  if (cancel.IsCancelled()) {
    result.error = MakeError(BiliErrorKind::kCancelled, QStringLiteral("取流已取消"));
    result.elapsed_ms = timer.elapsed();
    return result;
  }
  if (http == nullptr) {
    result.error = MakeError(BiliErrorKind::kNetwork,
                             QStringLiteral("网络客户端未初始化"), "no-http");
    result.elapsed_ms = timer.elapsed();
    return result;
  }

  ++result.http_requests;
  HttpRequest request;
  request.url = BuildPlayUrlUrl(bvid, cid, qn, options.fourk);
  request.timeout_ms = options.timeout_ms > 0 ? options.timeout_ms : 6000;
  request.sessdata = options.sessdata;
  const HttpResponse response = http->Get(request, cancel);

  result.http_status = response.http_status;
  result.request_cookie_sent = response.request_cookie_sent;
  result.elapsed_ms = timer.elapsed();
  if (cancel.IsCancelled() || response.cancelled) {
    result.error = MakeError(BiliErrorKind::kCancelled, QStringLiteral("取流已取消"));
    return result;
  }
  if (response.timed_out) {
    result.error = MakeError(BiliErrorKind::kTimeout,
                             QStringLiteral("取流请求超时，请重试"), "timeout");
    return result;
  }
  if (!response.ok) {
    result.error = NetworkError(response);
    return result;
  }
  result.error = ParsePlayUrlJson(response.body, &result.data);
  return result;
}

}  // namespace lapian::online
