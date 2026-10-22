#include "online/BiliResolver.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>

namespace lapian::online {
namespace {

bool IsAsciiDigit(QChar c) {
  return c >= QLatin1Char('0') && c <= QLatin1Char('9');
}

bool IsAsciiAlpha(QChar c) {
  return (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
         (c >= QLatin1Char('a') && c <= QLatin1Char('z'));
}

bool IsAsciiAlnum(QChar c) { return IsAsciiDigit(c) || IsAsciiAlpha(c); }

bool HasBvPrefix(const QString& text) {
  return text.size() >= 2 && text.at(0).toUpper() == QLatin1Char('B') &&
         text.at(1).toUpper() == QLatin1Char('V');
}

// 本地只校验“BV + 10 位 ASCII 字母数字”。B 站校验位规则会随版本/接口变化，
// 这里不猜校验位：末位改动仍可能命中另一条视频，必须靠回显确认拦（O1-A4）。
bool IsValidBv(const QString& text) {
  if (text.size() != 12 || !HasBvPrefix(text)) {
    return false;
  }
  for (int i = 2; i < text.size(); ++i) {
    if (!IsAsciiAlnum(text.at(i))) {
      return false;
    }
  }
  return true;
}

QString NormalizeBvPrefix(const QString& text) {
  if (text.size() >= 2 && text.at(0).toUpper() == QLatin1Char('B') &&
      text.at(1).toUpper() == QLatin1Char('V')) {
    return QStringLiteral("BV") + text.mid(2);
  }
  return text;
}

QString BvFormatMessage(const QString& text) {
  if (text.size() != 12) {
    return QStringLiteral("BV 号格式不对：应为 BV 开头的 12 位字母数字（当前 %1 位）")
        .arg(text.size());
  }
  return QStringLiteral("BV 号含非法字符：只允许字母和数字");
}

bool IsAvNumber(const QString& text) {
  if (text.size() < 2 || text.at(0).toLower() != QLatin1Char('a') ||
      text.at(1).toLower() != QLatin1Char('v')) {
    return false;
  }
  if (text.size() == 2) {
    return true;  // 单独的 "av" 也按 av 号处理，给明确提示
  }
  for (int i = 2; i < text.size(); ++i) {
    if (!IsAsciiDigit(text.at(i))) {
      return false;
    }
  }
  return true;
}

BiliResolveError MakeError(BiliErrorKind kind, const QString& message,
                           const QString& detail = QString()) {
  BiliResolveError error;
  error.kind = kind;
  error.message = message;
  error.detail = detail;
  return error;
}

BiliInput MakeInputError(const QString& raw, const QString& message,
                         const QString& detail = QString()) {
  BiliInput input;
  input.raw = raw;
  input.ok = false;
  input.error = MakeError(BiliErrorKind::kInvalidInput, message, detail);
  return input;
}

BiliInput MakeInputOk(const QString& raw, const QString& bvid, int page,
                      bool page_from_input) {
  BiliInput input;
  input.raw = raw;
  input.bvid = bvid;
  input.page = page;
  input.page_from_input = page_from_input;
  input.ok = true;
  return input;
}

bool LooksLikeUrl(const QString& lower) {
  return lower.startsWith(QStringLiteral("http://")) ||
         lower.startsWith(QStringLiteral("https://")) ||
         lower.startsWith(QStringLiteral("//")) ||
         lower.startsWith(QStringLiteral("www.")) ||
         lower.startsWith(QStringLiteral("m.bilibili.com/")) ||
         lower.startsWith(QStringLiteral("bilibili.com/")) ||
         lower.startsWith(QStringLiteral("b23.tv/")) ||
         lower.contains(QStringLiteral("bilibili.com/"));
}

// 用户可能粘贴不带 scheme 的链接；请求固定升级到 https（只允许 HTTPS）。
QString WithScheme(const QString& text) {
  const QString lower = text.toLower();
  if (lower.startsWith(QStringLiteral("http://")) ||
      lower.startsWith(QStringLiteral("https://"))) {
    return text;
  }
  if (lower.startsWith(QStringLiteral("//"))) {
    return QStringLiteral("https:") + text;
  }
  return QStringLiteral("https://") + text;
}

bool ParsePositivePage(const QString& value, int* out) {
  if (value.isEmpty()) {
    return false;
  }
  for (const QChar c : value) {
    if (!IsAsciiDigit(c)) {
      return false;
    }
  }
  bool ok = false;
  const int page = value.toInt(&ok);
  if (!ok || page <= 0) {
    return false;
  }
  *out = page;
  return true;
}

BiliResolveError ParsePageFields(const QJsonObject& object, BiliPage* page) {
  const QJsonValue page_value = object.value(QStringLiteral("page"));
  if (!page_value.isDouble()) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("接口返回的分P缺少页码"));
  }
  const int page_number = page_value.toInt();
  if (page_number <= 0) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("接口返回的分P页码无效"));
  }

  const QJsonValue cid_value = object.value(QStringLiteral("cid"));
  if (!cid_value.isDouble()) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("接口返回的分P缺少 cid"));
  }
  const qint64 cid = cid_value.toVariant().toLongLong();
  if (cid <= 0) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("接口返回的分P cid 无效"));
  }

  const QJsonValue duration_value = object.value(QStringLiteral("duration"));
  if (!duration_value.isDouble()) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("接口返回的分P缺少时长"));
  }
  const qint64 duration = duration_value.toVariant().toLongLong();
  if (duration < 0) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("接口返回的分P时长无效"));
  }

  QString title = object.value(QStringLiteral("part")).toString().trimmed();
  if (title.isEmpty()) {
    title = QStringLiteral("P%1").arg(page_number);
  }

  page->page = page_number;
  page->cid = cid;
  page->title = title;
  page->duration_seconds = duration;
  return {};
}

}  // namespace

BiliInput NormalizeInput(const QString& text) {
  const QString raw = text;
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    return MakeInputError(raw, QStringLiteral("请输入 BV 号或 B 站视频链接"));
  }

  const QString lower = trimmed.toLower();

  // 1) 明确的 av 号（不在本任务承诺范围；要报错，不能当搜索词发出去）。
  if (IsAvNumber(trimmed)) {
    return MakeInputError(
        raw,
        QStringLiteral("不支持 av 号：请输入 BV 号，或粘贴 bilibili.com/video/BV... 链接"),
        QStringLiteral("av-number"));
  }

  // 2) 裸 BV 号（不是 URL）。只接受 BV + 10 位字母数字。
  if (HasBvPrefix(trimmed) && !trimmed.contains(QLatin1Char('/')) &&
      !trimmed.contains(QLatin1Char('?')) && !trimmed.contains(QLatin1Char('#'))) {
    if (!IsValidBv(trimmed)) {
      return MakeInputError(raw, BvFormatMessage(trimmed), QStringLiteral("bv-format"));
    }
    return MakeInputOk(raw, NormalizeBvPrefix(trimmed), 1, false);
  }

  // 3) 短链不在本任务接受范围；给用户可执行的下一步，而不是猜短链目标。
  if (lower.contains(QStringLiteral("b23.tv"))) {
    return MakeInputError(
        raw,
        QStringLiteral("不支持 b23.tv 短链：请展开为 bilibili.com/video/BV... 链接后再试"),
        QStringLiteral("short-link"));
  }

  // 4) B 站视频 URL。
  if (LooksLikeUrl(lower)) {
    if (trimmed.contains(QLatin1Char(' ')) || trimmed.contains(QLatin1Char('\t')) ||
        trimmed.contains(QLatin1Char('\n'))) {
      return MakeInputError(raw, QStringLiteral("链接里不能有空格，请重新复制视频链接"),
                            QStringLiteral("url-space"));
    }
    QUrl url(WithScheme(trimmed));
    if (!url.isValid()) {
      return MakeInputError(raw, QStringLiteral("视频链接格式不对，请重新复制 bilibili.com/video/BV... 链接"),
                            QStringLiteral("url-invalid"));
    }
    const QString host = url.host().toLower();
    if (host == QStringLiteral("b23.tv") || host.endsWith(QStringLiteral(".b23.tv"))) {
      return MakeInputError(
          raw,
          QStringLiteral("不支持 b23.tv 短链：请展开为 bilibili.com/video/BV... 链接后再试"),
          QStringLiteral("short-link"));
    }
    if (host != QStringLiteral("bilibili.com") &&
        !host.endsWith(QStringLiteral(".bilibili.com"))) {
      return MakeInputError(
          raw,
          QStringLiteral("只支持 bilibili.com 的视频链接（当前链接不是 B 站视频页）"),
          QStringLiteral("url-host"));
    }

    QString path = url.path();
    if (!path.startsWith(QStringLiteral("/video/"), Qt::CaseInsensitive)) {
      return MakeInputError(
          raw,
          QStringLiteral("链接里没有视频 BV 号：请使用 bilibili.com/video/BV... 形式"),
          QStringLiteral("url-path"));
    }
    QString segment = path.mid(7);
    while (segment.endsWith(QLatin1Char('/'))) {
      segment.chop(1);
    }
    if (segment.isEmpty() || segment.contains(QLatin1Char('/'))) {
      return MakeInputError(
          raw,
          QStringLiteral("链接里没有视频 BV 号：请使用 bilibili.com/video/BV... 形式"),
          QStringLiteral("url-path"));
    }
    if (IsAvNumber(segment)) {
      return MakeInputError(
          raw,
          QStringLiteral("不支持 av 号：请输入 BV 号，或粘贴 bilibili.com/video/BV... 链接"),
          QStringLiteral("av-number"));
    }
    if (!IsValidBv(segment)) {
      return MakeInputError(raw, BvFormatMessage(segment), QStringLiteral("bv-format"));
    }

    int page = 1;
    bool page_from_input = false;
    const QUrlQuery query(url);
    if (query.hasQueryItem(QStringLiteral("p"))) {
      if (!ParsePositivePage(query.queryItemValue(QStringLiteral("p")), &page)) {
        return MakeInputError(raw, QStringLiteral("分P 参数 p 必须是正整数"),
                              QStringLiteral("page-param"));
      }
      page_from_input = true;
    }
    return MakeInputOk(raw, NormalizeBvPrefix(segment), page, page_from_input);
  }

  // 5) 其它输入（搜索词/分享文案/不认识的链接）明确拒绝，不静默发请求。
  return MakeInputError(
      raw, QStringLiteral("只支持 BV 号或 B 站视频链接，不支持搜索词"),
      QStringLiteral("unsupported-input"));
}

QString BuildViewUrl(const QString& bvid) {
  return QStringLiteral("https://api.bilibili.com/x/web-interface/view?bvid=") + bvid;
}

BiliResolveError ParseViewJson(const QByteArray& json, BiliVideo* out) {
  if (out == nullptr) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("解析结果没有输出位置"));
  }

  QJsonParseError parse_error;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("接口返回格式无法解析，请稍后重试"),
                     parse_error.errorString());
  }

  const QJsonObject root = document.object();
  const QJsonValue code_value = root.value(QStringLiteral("code"));
  if (!code_value.isDouble()) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("接口返回缺少错误码"));
  }
  const int code = code_value.toInt();
  const QString api_message = root.value(QStringLiteral("message")).toString();
  if (code != 0) {
    return MapApiError(code, api_message);
  }

  const QJsonValue data_value = root.value(QStringLiteral("data"));
  if (!data_value.isObject()) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("接口没有返回视频信息"));
  }
  const QJsonObject data = data_value.toObject();

  const QString bvid = data.value(QStringLiteral("bvid")).toString().trimmed();
  if (!IsValidBv(bvid)) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("接口返回的 BV 号无效"));
  }

  const QString title = data.value(QStringLiteral("title")).toString().trimmed();
  if (title.isEmpty()) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("接口没有返回视频标题"));
  }

  QString owner;
  const QJsonValue owner_value = data.value(QStringLiteral("owner"));
  if (owner_value.isObject()) {
    owner = owner_value.toObject().value(QStringLiteral("name")).toString().trimmed();
  }
  if (owner.isEmpty()) {
    owner = QStringLiteral("未知UP主");
  }

  const QJsonValue duration_value = data.value(QStringLiteral("duration"));
  if (!duration_value.isDouble()) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("接口没有返回视频时长"));
  }
  const qint64 duration = duration_value.toVariant().toLongLong();
  if (duration < 0) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("接口返回的视频时长无效"));
  }

  const QJsonValue pages_value = data.value(QStringLiteral("pages"));
  if (!pages_value.isArray()) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("接口没有返回分P列表"));
  }
  const QJsonArray pages_array = pages_value.toArray();
  if (pages_array.isEmpty()) {
    return MakeError(BiliErrorKind::kParse, QStringLiteral("接口返回的分P列表为空"));
  }

  BiliVideo video;
  video.bvid = bvid;
  const QJsonValue aid_value = data.value(QStringLiteral("aid"));
  if (aid_value.isDouble()) {
    video.aid = aid_value.toVariant().toLongLong();
  }
  video.title = title;
  video.owner = owner;
  video.duration_seconds = duration;

  for (const QJsonValue& page_value : pages_array) {
    if (!page_value.isObject()) {
      return MakeError(BiliErrorKind::kParse, QStringLiteral("接口返回的分P格式无效"));
    }
    BiliPage page;
    const BiliResolveError page_error = ParsePageFields(page_value.toObject(), &page);
    if (!page_error.ok()) {
      return page_error;
    }
    video.pages.push_back(page);
  }

  video.selected_index = 0;
  *out = video;
  return {};
}

BiliResolveError SelectPage(BiliVideo* video, int page_number) {
  if (video == nullptr) {
    return MakeError(BiliErrorKind::kInvalidInput, QStringLiteral("没有可切换的分P"));
  }
  if (page_number <= 0) {
    return MakeError(BiliErrorKind::kInvalidInput, QStringLiteral("分P 必须从 1 开始"),
                     QStringLiteral("page-number"));
  }
  for (int index = 0; index < video->pages.size(); ++index) {
    if (video->pages.at(index).page == page_number) {
      video->selected_index = index;
      return {};
    }
  }
  return MakeError(
      BiliErrorKind::kInvalidInput,
      QStringLiteral("该视频没有 P%1（共 %2 个分P）")
          .arg(page_number)
          .arg(video->pages.size()),
      QStringLiteral("page-out-of-range"));
}

BiliResolveError MapApiError(int code, const QString& api_message) {
  BiliResolveError error;
  error.kind = BiliErrorKind::kApi;
  error.api_code = code;
  error.api_message = api_message.left(200);
  switch (code) {
    case -400:
      error.message = QStringLiteral("请求参数错误（-400）：请检查 BV 号或链接后重试");
      break;
    case -404:
      error.message = QStringLiteral("稿件不存在或已删除（-404）");
      break;
    case 62002:
      error.message = QStringLiteral("稿件不可见（私密/审核/删除，62002）");
      break;
    case -101:
      error.message =
          QStringLiteral("登录不可用/凭据失效（-101）；将按未登录清晰度处理");
      break;
    case -412:
    case -509:
    case -799:
      error.message = QStringLiteral("请求被拒绝，请稍后重试（%1）").arg(code);
      break;
    default:
      // 未实测的码只给通用提示，不编造含义（delta §5）。
      error.message = QStringLiteral("解析失败（错误码 %1），请稍后重试").arg(code);
      break;
  }
  error.detail = QStringLiteral("code=%1").arg(code);
  if (!error.api_message.isEmpty()) {
    error.detail += QStringLiteral(" message=") + error.api_message;
  }
  return error;
}

QString ErrorKindName(BiliErrorKind kind) {
  switch (kind) {
    case BiliErrorKind::kNone:
      return QStringLiteral("none");
    case BiliErrorKind::kInvalidInput:
      return QStringLiteral("invalid-input");
    case BiliErrorKind::kApi:
      return QStringLiteral("api");
    case BiliErrorKind::kHttp:
      return QStringLiteral("http");
    case BiliErrorKind::kNetwork:
      return QStringLiteral("network");
    case BiliErrorKind::kTimeout:
      return QStringLiteral("timeout");
    case BiliErrorKind::kCancelled:
      return QStringLiteral("cancelled");
    case BiliErrorKind::kParse:
      return QStringLiteral("parse");
  }
  return QStringLiteral("unknown");
}

}  // namespace lapian::online
