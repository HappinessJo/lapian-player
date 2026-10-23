#include "online/PlayUrl.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <iterator>

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

// playurl 响应外层：code/message/data。durl 与 DASH 两条解析共用，错误文本
// 与 T8.5 完全一致（不因 T8.9 改动 M1 的错误映射）。
BiliResolveError ParseEnvelope(const QByteArray& json, QJsonObject* data) {
  if (data == nullptr) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("取流解析没有输出位置"), "no-output");
  }
  QJsonParseError parse_error;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("取流响应格式无法解析，请稍后重试"), "json");
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
  *data = data_value.toObject();
  return {};
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

// ---------------------------------------------------------------- T8.9 DASH
// 解析辅助：DASH 字段类型松散（数字/字符串混用、snake/camel 两套键），
// 单条坏数据只跳过该条，不把整次解析判失败（“错误 qn/字段不崩”）。

double JsonToDouble(const QJsonValue& value) {
  if (value.isDouble()) {
    const double number = value.toDouble();
    return std::isfinite(number) ? number : 0.0;
  }
  if (value.isString()) {
    bool ok = false;
    const double number = value.toString().trimmed().toDouble(&ok);
    return ok && std::isfinite(number) ? number : 0.0;
  }
  return 0.0;
}

int JsonToIntLoose(const QJsonValue& value) {
  if (value.isDouble()) {
    return JsonToInt(value);
  }
  if (value.isString()) {
    bool ok = false;
    const int number = value.toString().trimmed().toInt(&ok);
    return ok ? number : 0;
  }
  return 0;
}

// "0-916" / "917-1244"：闭区间字节范围。
bool ParseDashRange(const QJsonValue& value, DashRange* range) {
  if (range == nullptr) {
    return false;
  }
  *range = DashRange{};
  if (!value.isString()) {
    return false;
  }
  const QString text = value.toString().trimmed();
  const int dash = text.indexOf(QLatin1Char('-'));
  if (dash <= 0 || dash >= text.size() - 1) {
    return false;
  }
  bool ok_start = false;
  bool ok_end = false;
  const qint64 start = text.left(dash).toLongLong(&ok_start);
  const qint64 end = text.mid(dash + 1).toLongLong(&ok_end);
  if (!ok_start || !ok_end || start < 0 || end < start) {
    return false;
  }
  if (end > (1LL << 52)) {
    return false;
  }
  range->start = start;
  range->end = end;
  return true;
}

QString PickString(const QJsonObject& object, const char* a, const char* b) {
  const QJsonValue first = object.value(QLatin1String(a));
  if (first.isString()) {
    return first.toString();
  }
  const QJsonValue second = object.value(QLatin1String(b));
  return second.isString() ? second.toString() : QString();
}

int PickInt(const QJsonObject& object, const char* a, const char* b) {
  const QJsonValue first = object.value(QLatin1String(a));
  if (!first.isUndefined() && !first.isNull()) {
    return JsonToIntLoose(first);
  }
  return JsonToIntLoose(object.value(QLatin1String(b)));
}

QStringList PickUrlArray(const QJsonObject& object, const char* a, const char* b) {
  QStringList urls;
  for (const char* key : {a, b}) {
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isArray()) {
      continue;
    }
    for (const QJsonValue& item : value.toArray()) {
      if (!item.isString()) {
        continue;
      }
      const QString url = item.toString();
      if (IsHttpsUrl(url) && !urls.contains(url)) {
        urls.append(url);
      }
    }
    if (!urls.isEmpty()) {
      break;
    }
  }
  return urls;
}

int InferCodecidFromCodecs(const QString& codecs) {
  const QString lower = codecs.toLower();
  if (lower.startsWith(QStringLiteral("avc")) ||
      lower.contains(QStringLiteral("avc1"))) {
    return 7;
  }
  if (lower.startsWith(QStringLiteral("hvc")) ||
      lower.startsWith(QStringLiteral("hev"))) {
    return 12;
  }
  if (lower.startsWith(QStringLiteral("av01")) ||
      lower.startsWith(QStringLiteral("av1"))) {
    return 13;
  }
  return 0;
}

int CodecRank(int codecid) {
  switch (codecid) {
    case 7:
      return 0;
    case 12:
      return 1;
    case 13:
      return 2;
    default:
      return 3;
  }
}

// O6 的自动档顺序（保帧率优先）；112（1080P+，同分辨率更高码率）排在 80 之后，
// 避免默认选到更高码率。116/74/120 是手动档，列在这里只用于“存在性”判断。
constexpr int kQualityPreference[] = {116, 74, 80, 112, 64, 32, 16};

int PreferenceRank(int qn) {
  for (int i = 0; i < static_cast<int>(std::size(kQualityPreference)); ++i) {
    if (kQualityPreference[i] == qn) {
      return i;
    }
  }
  return 1000;
}

BiliResolveError ParseDashVideo(const QJsonObject& object, DashStream* stream) {
  if (stream == nullptr) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("DASH 解析没有输出位置"),
                     "no-output");
  }
  *stream = DashStream();
  const int qn = PickInt(object, "id", "id");
  if (qn <= 0 || qn > 10000) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("DASH 视频流缺少有效档位"), "video-qn");
  }
  stream->qn = qn;
  stream->codecs = PickString(object, "codecs", "codecs");
  stream->codecid = PickInt(object, "codecid", "codecid");
  if (stream->codecid <= 0) {
    stream->codecid = InferCodecidFromCodecs(stream->codecs);
  }
  if (stream->codecid <= 0) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("DASH 视频流缺少编码信息"), "video-codecid");
  }
  stream->width = PickInt(object, "width", "width");
  stream->height = PickInt(object, "height", "height");
  const QJsonValue frame_rate = object.value(QStringLiteral("frameRate"));
  stream->fps = !frame_rate.isUndefined() && !frame_rate.isNull()
                    ? JsonToDouble(frame_rate)
                    : JsonToDouble(object.value(QStringLiteral("frame_rate")));
  stream->bandwidth = JsonToI64(object.value(QStringLiteral("bandwidth")));
  stream->size = JsonToI64(object.value(QStringLiteral("size")));
  stream->mime = PickString(object, "mimeType", "mime_type");
  stream->start_with_sap =
      PickInt(object, "startWithSap", "start_with_sap") > 0;
  stream->url = PickString(object, "baseUrl", "base_url");
  stream->urls_ok = IsHttpsUrl(stream->url);
  stream->backup_urls = PickUrlArray(object, "backupUrl", "backup_url");
  const QJsonValue segment = !object.value(QStringLiteral("SegmentBase")).isUndefined()
                                 ? object.value(QStringLiteral("SegmentBase"))
                                 : object.value(QStringLiteral("segment_base"));
  if (segment.isObject()) {
    const QJsonObject base = segment.toObject();
    ParseDashRange(base.value(QStringLiteral("Initialization")),
                   &stream->init_range);
    if (!stream->init_range.valid()) {
      ParseDashRange(base.value(QStringLiteral("initialization")),
                     &stream->init_range);
    }
    ParseDashRange(base.value(QStringLiteral("indexRange")), &stream->index_range);
    if (!stream->index_range.valid()) {
      ParseDashRange(base.value(QStringLiteral("index_range")),
                     &stream->index_range);
    }
  }
  stream->init_ok = stream->init_range.valid() && stream->index_range.valid();
  return {};
}

BiliResolveError ParseDashAudio(const QJsonObject& object,
                                DashAudioStream* stream) {
  if (stream == nullptr) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("DASH 解析没有输出位置"),
                     "no-output");
  }
  *stream = DashAudioStream();
  const int id = PickInt(object, "id", "id");
  if (id <= 0) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("DASH 音频流缺少有效 id"), "audio-id");
  }
  stream->id = id;
  stream->codecid = PickInt(object, "codecid", "codecid");
  stream->bandwidth = JsonToI64(object.value(QStringLiteral("bandwidth")));
  stream->size = JsonToI64(object.value(QStringLiteral("size")));
  stream->mime = PickString(object, "mimeType", "mime_type");
  stream->codecs = PickString(object, "codecs", "codecs");
  stream->start_with_sap =
      PickInt(object, "startWithSap", "start_with_sap") > 0;
  stream->url = PickString(object, "baseUrl", "base_url");
  stream->urls_ok = IsHttpsUrl(stream->url);
  stream->backup_urls = PickUrlArray(object, "backupUrl", "backup_url");
  const QJsonValue segment = !object.value(QStringLiteral("SegmentBase")).isUndefined()
                                 ? object.value(QStringLiteral("SegmentBase"))
                                 : object.value(QStringLiteral("segment_base"));
  if (segment.isObject()) {
    const QJsonObject base = segment.toObject();
    ParseDashRange(base.value(QStringLiteral("Initialization")),
                   &stream->init_range);
    if (!stream->init_range.valid()) {
      ParseDashRange(base.value(QStringLiteral("initialization")),
                     &stream->init_range);
    }
    ParseDashRange(base.value(QStringLiteral("indexRange")), &stream->index_range);
    if (!stream->index_range.valid()) {
      ParseDashRange(base.value(QStringLiteral("index_range")),
                     &stream->index_range);
    }
  }
  stream->init_ok = stream->init_range.valid() && stream->index_range.valid();
  return {};
}

BiliResolveError NetworkError(const HttpResponse& response) {  BiliResolveError error;
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

QString BuildPlayUrlUrl(const QString& bvid, qint64 cid, int qn, int fourk,
                        int fnval) {
  if (!IsValidBvid(bvid) || cid <= 0) {
    return QString();
  }
  const int quality = qn > 0 ? qn : kDefaultQn;
  QUrl url(QStringLiteral("https://api.bilibili.com/x/player/playurl"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("bvid"), bvid);
  query.addQueryItem(QStringLiteral("cid"), QString::number(cid));
  query.addQueryItem(QStringLiteral("qn"), QString::number(quality));
  query.addQueryItem(QStringLiteral("fnval"),
                     QString::number(fnval > 0 ? fnval : 0));
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

  QJsonObject data;
  BiliResolveError envelope = ParseEnvelope(json, &data);
  if (!envelope.ok()) {
    return envelope;
  }
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
    case 127:
      return QStringLiteral("8K");
    case 126:
      return QStringLiteral("杜比视界");
    case 125:
      return QStringLiteral("HDR");
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

// ---------------------------------------------------------------- T8.9 DASH

QString DashRange::ToString() const {
  if (!valid()) {
    return QString();
  }
  return QStringLiteral("%1-%2").arg(start).arg(end);
}

QStringList DashStream::AllUrls() const {
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

QStringList DashAudioStream::AllUrls() const {
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

bool QualityTier::HasCodec(int codecid) const {
  return Stream(codecid) != nullptr;
}

const DashStream* QualityTier::Stream(int codecid) const {
  for (const DashStream& stream : streams) {
    if (stream.codecid == codecid) {
      return &stream;
    }
  }
  return nullptr;
}

const DashStream* QualityTier::PreferredStream() const {
  for (int codecid : {7, 12, 13}) {
    const DashStream* stream = Stream(codecid);
    if (stream != nullptr) {
      return stream;
    }
  }
  return streams.isEmpty() ? nullptr : &streams.first();
}

int DashCatalog::MaxQn() const {
  int max_qn = 0;
  for (const QualityTier& tier : tiers) {
    max_qn = std::max(max_qn, tier.qn);
  }
  return max_qn;
}

const QualityTier* DashCatalog::FindTier(int qn) const {
  for (const QualityTier& tier : tiers) {
    if (tier.qn == qn) {
      return &tier;
    }
  }
  return nullptr;
}

const DashStream* DashCatalog::FindStream(int qn, int codecid) const {
  const QualityTier* tier = FindTier(qn);
  if (tier == nullptr) {
    return nullptr;
  }
  return tier->Stream(codecid);
}

const DashAudioStream* DashCatalog::BestAudio() const {
  const DashAudioStream* best = nullptr;
  for (const DashAudioStream& audio : audios) {
    if (best == nullptr || audio.bandwidth > best->bandwidth) {
      best = &audio;
    }
  }
  return best;
}

bool DashCatalog::ClaimedQualityInTiers() const {
  return claimed_quality > 0 && FindTier(claimed_quality) != nullptr;
}

int DashCatalog::DefaultQn() const {
  // 1) O6 顺序里的第一个“实际存在 + 非手动 + 有 AVC”档。
  for (int qn : kQualityPreference) {
    const QualityTier* tier = FindTier(qn);
    if (tier == nullptr || IsManualOnlyQuality(qn)) {
      continue;
    }
    if (tier->has_avc()) {
      return qn;
    }
  }
  // 2) 没有 AVC 可自动选时，回退最高的非手动档（HEVC-only 可播，但不选 60/4K）。
  for (const QualityTier& tier : tiers) {
    if (!IsManualOnlyQuality(tier.qn)) {
      return tier.qn;
    }
  }
  return 0;
}

const DashStream* DashCatalog::DefaultStream() const {
  const QualityTier* tier = FindTier(DefaultQn());
  return tier == nullptr ? nullptr : tier->PreferredStream();
}

QString DashCatalog::Summary() const {
  QStringList tier_texts;
  for (const QualityTier& tier : tiers) {
    QString text = QStringLiteral("%1(%2").arg(tier.qn).arg(tier.width);
    text += QStringLiteral("x%1").arg(tier.height);
    if (tier.fps > 0.0) {
      text += QStringLiteral("@%1").arg(QString::number(tier.fps, 'f', 3));
    }
    QStringList codecs;
    for (const DashStream& stream : tier.streams) {
      codecs.append(CodecName(stream.codecid));
    }
    text += QStringLiteral(":%1)").arg(codecs.join(QLatin1Char('/')));
    tier_texts.append(text);
  }
  QStringList audio_texts;
  for (const DashAudioStream& audio : audios) {
    audio_texts.append(QStringLiteral("%1:%2k").arg(audio.id).arg(
        audio.bandwidth / 1000));
  }
  return QStringLiteral(
             "tiers=%1 audio=[%2] duration_ms=%3 claimed_qn=%4 "
             "claimed_in_tiers=%5 skipped=%6/%7")
      .arg(tier_texts.isEmpty() ? QStringLiteral("-") : tier_texts.join(','),
           audio_texts.isEmpty() ? QStringLiteral("-") : audio_texts.join(','))
      .arg(duration_ms)
      .arg(claimed_quality)
      .arg(ClaimedQualityInTiers() ? 1 : 0)
      .arg(skipped_video_entries)
      .arg(skipped_audio_entries);
}

bool IsManualOnlyQuality(int qn) {
  // 60fps 档（116/74）与 4K/HDR/杜比/8K（>=120）只允许手动选择（O6/O11）。
  return qn == 116 || qn == 74 || qn >= 120;
}

QString CodecName(int codecid) {
  switch (codecid) {
    case 7:
      return QStringLiteral("AVC");
    case 12:
      return QStringLiteral("HEVC");
    case 13:
      return QStringLiteral("AV1");
    default:
      break;
  }
  return QStringLiteral("codec=%1").arg(codecid);
}

QString QualityTierLabel(const QualityTier& tier) {
  QString text = QualityName(tier.qn);
  const DashStream* stream = tier.PreferredStream();
  if (stream != nullptr) {
    text += QStringLiteral(" · %1").arg(CodecName(stream->codecid));
  }
  if (tier.fps > 0.0) {
    text += QStringLiteral(" · %1fps").arg(QString::number(tier.fps, 'f', 1));
  }
  if (tier.width > 0 && tier.height > 0) {
    text += QStringLiteral(" · %1x%2").arg(tier.width).arg(tier.height);
  }
  return text;
}

QString BuildDashPlayUrlUrl(const QString& bvid, qint64 cid, int qn, int fourk) {
  return BuildPlayUrlUrl(bvid, cid, qn, fourk, kDashFnval);
}

BiliResolveError ParseDashPlayUrlJson(const QByteArray& json, DashCatalog* out) {
  if (out == nullptr) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("DASH 解析没有输出位置"), "no-output");
  }
  *out = DashCatalog();
  QElapsedTimer timer;
  timer.start();

  QJsonObject data;
  BiliResolveError envelope = ParseEnvelope(json, &data);
  if (!envelope.ok()) {
    return envelope;
  }
  // data.quality / accept_quality 只记录：实测会与 dash 列表不一致，
  // 档位判断只信 dash.video[]。
  out->claimed_quality = JsonToInt(data.value(QStringLiteral("quality")));
  out->claimed_format = data.value(QStringLiteral("format")).toString();
  const QJsonValue accept_quality = data.value(QStringLiteral("accept_quality"));
  if (accept_quality.isArray()) {
    for (const QJsonValue& value : accept_quality.toArray()) {
      const int qn = JsonToIntLoose(value);
      if (qn > 0) {
        out->claimed_accept_quality.append(qn);
      }
    }
  }

  const QJsonValue dash_value = data.value(QStringLiteral("dash"));
  if (!dash_value.isObject()) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("取流响应没有 DASH 段（该请求可能只返回 durl）"),
                     "no-dash");
  }
  const QJsonObject dash = dash_value.toObject();
  // dash.duration 是秒；data.timelength 是毫秒。优先 dash.duration（允许小数秒）。
  const double dash_duration =
      JsonToDouble(dash.value(QStringLiteral("duration")));
  if (dash_duration > 0.0) {
    out->duration_ms = static_cast<qint64>(dash_duration * 1000.0 + 0.5);
  }
  if (out->duration_ms <= 0) {
    out->duration_ms = JsonToI64(data.value(QStringLiteral("timelength")));
  }

  const QJsonValue video_value = dash.value(QStringLiteral("video"));
  if (!video_value.isArray() || video_value.toArray().isEmpty()) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("DASH 响应没有视频流，无法列档"), "no-video");
  }
  for (const QJsonValue& value : video_value.toArray()) {
    if (!value.isObject()) {
      ++out->skipped_video_entries;
      continue;
    }
    DashStream stream;
    if (!ParseDashVideo(value.toObject(), &stream).ok()) {
      ++out->skipped_video_entries;
      continue;
    }
    out->videos.append(stream);
  }
  if (out->videos.isEmpty()) {
    return MakeError(BiliErrorKind::kParse,
                     QStringLiteral("DASH 视频流字段无效，无法列档"), "bad-video");
  }

  const QJsonValue audio_value = dash.value(QStringLiteral("audio"));
  if (audio_value.isArray()) {
    for (const QJsonValue& value : audio_value.toArray()) {
      if (!value.isObject()) {
        ++out->skipped_audio_entries;
        continue;
      }
      DashAudioStream stream;
      if (!ParseDashAudio(value.toObject(), &stream).ok()) {
        ++out->skipped_audio_entries;
        continue;
      }
      out->audios.append(stream);
    }
  }

  // 按 qn 归并为档位；同一 qn 的 AVC/HEVC/AV1 都在 streams 里。
  RebuildDashTiers(out);
  out->has_dash = true;
  out->parse_elapsed_ms = timer.elapsed();
  return {};
}

void RebuildDashTiers(DashCatalog* catalog) {
  if (catalog == nullptr) {
    return;
  }
  catalog->tiers.clear();
  for (const DashStream& stream : catalog->videos) {
    QualityTier* tier = nullptr;
    for (QualityTier& candidate : catalog->tiers) {
      if (candidate.qn == stream.qn) {
        tier = &candidate;
        break;
      }
    }
    if (tier == nullptr) {
      QualityTier fresh;
      fresh.qn = stream.qn;
      catalog->tiers.append(fresh);
      tier = &catalog->tiers.last();
    }
    tier->streams.append(stream);
  }
  for (QualityTier& tier : catalog->tiers) {
    std::sort(tier.streams.begin(), tier.streams.end(),
              [](const DashStream& a, const DashStream& b) {
                return CodecRank(a.codecid) < CodecRank(b.codecid);
              });
    const DashStream* preferred = tier.PreferredStream();
    if (preferred != nullptr) {
      tier.width = preferred->width;
      tier.height = preferred->height;
      tier.fps = preferred->fps;
      tier.bandwidth = preferred->bandwidth;
    }
  }
  std::sort(catalog->tiers.begin(), catalog->tiers.end(),
            [](const QualityTier& a, const QualityTier& b) {
              if (a.qn != b.qn) {
                return a.qn > b.qn;
              }
              return PreferenceRank(a.qn) < PreferenceRank(b.qn);
            });
  catalog->has_dash = catalog->has_dash || !catalog->videos.isEmpty();
}

DashFetchResult FetchDashCatalog(IHttpClient* http, const QString& bvid,
                                 qint64 cid, const DashFetchOptions& options,
                                 const HttpCancelToken& cancel) {
  QElapsedTimer timer;
  timer.start();
  DashFetchResult result;

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
  request.url = BuildDashPlayUrlUrl(
      bvid, cid, options.qn > 0 ? options.qn : kDashRequestQn, options.fourk);
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
  result.error = ParseDashPlayUrlJson(response.body, &result.catalog);
  return result;
}

}  // namespace lapian::online
