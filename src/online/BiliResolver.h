#pragma once

// T8.1 解析层：BV/URL 归一化、本地校验、view 元数据解析与错误码映射。
//
// 这一层不联网、不碰 UI、不解析 playurl/DASH。它只把用户输入变成
// “可以安全发请求的 bvid + 分P”，再把 view 的 JSON 变成回显确认所需的
// 标题/UP/时长/分P；网络与取消由 BiliResolveClient/IHttpClient 负责。
// 本地校验失败时调用方不应创建 HTTP 请求（O1-A2）。

#include <QByteArray>
#include <QString>
#include <QVector>

#include <cstdint>

namespace lapian::online {

// 分P。duration_seconds 与 view 的 pages[].duration 一致（秒）。
struct BiliPage {
  int page = 0;
  qint64 cid = 0;
  QString title;
  qint64 duration_seconds = 0;
};

// view 成功后的元数据。selected_index 指向 pages 里的当前分P（0 基）。
struct BiliVideo {
  QString bvid;
  qint64 aid = 0;
  QString title;
  QString owner;
  qint64 duration_seconds = 0;
  QVector<BiliPage> pages;
  int selected_index = 0;

  const BiliPage* selected_page() const {
    if (selected_index < 0 || selected_index >= pages.size()) {
      return nullptr;
    }
    return &pages[selected_index];
  }
};

enum class BiliErrorKind {
  kNone = 0,
  kInvalidInput,  // 本地校验失败：不发请求
  kApi,           // view 返回非 0 code
  kHttp,          // HTTP 状态异常（412/429/5xx 等）
  kNetwork,       // DNS/TLS/连接失败
  kTimeout,       // 超时
  kCancelled,     // 用户取消
  kParse,         // 响应不是预期 JSON
};

// 可读错误。message 直接给用户；detail 只进日志，不含 Cookie/签名 URL。
struct BiliResolveError {
  BiliErrorKind kind = BiliErrorKind::kNone;
  int api_code = 0;
  int http_status = 0;
  QString message;
  QString detail;
  QString api_message;

  bool ok() const { return kind == BiliErrorKind::kNone; }
};

// 归一化结果。page 只在 page_from_input=true 时有意义；否则默认 P1。
struct BiliInput {
  QString raw;
  QString bvid;
  int page = 1;
  bool page_from_input = false;
  bool ok = false;
  BiliResolveError error;
};

// 本地归一化：接受 BV 开头 12 位、bilibili.com/video/BV...（可带 ?p=）。
// 明确拒绝 av 号、b23.tv 短链、搜索词；非法输入在 100ms 内返回错误。
BiliInput NormalizeInput(const QString& text);

// 固定匿名 view 地址；不带 Cookie、不带签名参数。
QString BuildViewUrl(const QString& bvid);

// 解析 x/web-interface/view 的 JSON。成功时写 out 并返回 ok。
BiliResolveError ParseViewJson(const QByteArray& json, BiliVideo* out);

// 按 view 的 pages[].page 选分P（不是数组下标）。越界返回可读错误。
BiliResolveError SelectPage(BiliVideo* video, int page_number);

// 已知错误码映射；未知码只给通用可读提示，不编造含义。
BiliResolveError MapApiError(int code, const QString& api_message);

// 给日志/证据用的稳定名字，例如 invalid-input / api-400。
QString ErrorKindName(BiliErrorKind kind);

}  // namespace lapian::online
