#pragma once

// T8.5 durl 取流：playurl(fnval=0) 的 URL 构造、JSON 解析与错误映射。
//
// 这一层只做“一次 playurl GET + durl 单文件元数据”：
// - 不下载媒体、不碰缓存/读层/UI；
// - 不解析 DASH（M1 只走 durl；没有 durl 就报告不可用，不降级）；
// - 不猜档位：`quality` 是服务端**实际**返回，请求 qn 只作为输入；
// - 不打印签名 URL / Cookie（日志只记 host/path/状态/耗时）。

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

#include "online/BiliResolver.h"
#include "online/IHttpClient.h"

namespace lapian::online {

// durl 单文件。url/backup_url 都带签名与过期参数，只能用于当前会话，
// 绝不写日志/meta。
struct DurlSegment {
  QString url;
  QStringList backup_urls;
  qint64 size = 0;       // 服务端声明的总字节（必须与 Content-Range 对账）
  qint64 length_ms = 0;  // 时长（ms，辅助）
  int order = 0;

  // 主 URL + 有效备份 URL（去空、去重、保序）。
  QStringList AllUrls() const;
};

struct PlayUrlData {
  int quality = 0;       // 实际 qn（服务端返回，不是请求值）
  QString format;        // 例如 mp4720 / mp4
  int codecid = 0;       // video_codecid；durl 为 AVC 时通常是 7
  qint64 timelength_ms = 0;
  QVector<DurlSegment> durls;

  bool HasDurl() const { return !durls.isEmpty() && !durls.first().url.isEmpty(); }
  // durl[0].size；缺失/非法时为 0。
  qint64 TotalBytes() const;
};

// https://api.bilibili.com/x/player/playurl?bvid=...&cid=...&qn=64&fnval=0&fnver=0&fourk=0
// bvid 非法或 cid<=0 时返回空串（调用方不应发请求）。
QString BuildPlayUrlUrl(const QString& bvid, qint64 cid, int qn = 64);

// 解析 playurl JSON。成功 → error.ok()；API 错误按 MapApiError 映射。
// 特例：data.durl 为空（只有 DASH）时 error.kind=kParse 且 detail="no-durl"，
// 但已解析出的 quality/format 仍写入 out，便于记录“实际拿到什么档位”。
BiliResolveError ParsePlayUrlJson(const QByteArray& json, PlayUrlData* out);

// 档位可读名（仅用于证据/提示，不参与判定）。
QString QualityName(int qn);
// M1 门要求 720P：qn>=64。只用于报告，不影响下载。
bool IsM1Quality(int qn);

struct PlayUrlFetchOptions {
  int timeout_ms = 6000;
  // 可选 SESSDATA；非空时由 WinHttpClient 只发 api.bilibili.com。
  QString sessdata;
};

struct PlayUrlFetchResult {
  PlayUrlData data;
  BiliResolveError error;
  int http_requests = 0;
  int http_status = 0;
  qint64 elapsed_ms = 0;
  bool request_cookie_sent = false;
};

// 同步执行；调用方负责放到工作线程。cancel 可在其它线程置位。
PlayUrlFetchResult FetchPlayUrl(IHttpClient* http, const QString& bvid, qint64 cid,
                                int qn, const PlayUrlFetchOptions& options,
                                const HttpCancelToken& cancel);

}  // namespace lapian::online
