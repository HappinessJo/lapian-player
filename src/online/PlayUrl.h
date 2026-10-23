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
// qn 是“请求档位”，服务端可能降档；实际档位看 PlayUrlData::quality。
// fourk=1 只是把 4K 请求放行给服务端，不保证返回 4K（普通用户仍拿不到）。
// fnval 默认 0（durl 单文件，M1 语义不变）；T8.9 传 kDashFnval 请求 DASH。
QString BuildPlayUrlUrl(const QString& bvid, qint64 cid, int qn = 64,
                        int fourk = 0, int fnval = 0);

// 解析 playurl JSON。成功 → error.ok()；API 错误按 MapApiError 映射。
// 特例：data.durl 为空（只有 DASH）时 error.kind=kParse 且 detail="no-durl"，
// 但已解析出的 quality/format 仍写入 out，便于记录“实际拿到什么档位”。
BiliResolveError ParsePlayUrlJson(const QByteArray& json, PlayUrlData* out);

// 档位可读名（仅用于证据/提示，不参与判定）。
QString QualityName(int qn);
// M1 门要求 720P：qn>=64。只用于报告，不影响下载。
bool IsM1Quality(int qn);

// M1 产品路径统一“请求 1080P 上限”：qn=80，fourk=0。
// 实测（T8.8 矩阵）：OB-2 在 qn=64 时服务端只给 16（360P），qn=80/112/120 都给 80
// （1080P）；fourk=0/1 对实际档位无影响。请求 80 既拿到可用最高，又天然封顶
// 1080P（不请求 4K），普通用户/大会员账号都不会误触发 4K 下载。
// 服务端仍可能降档（OB-1/OB-3 给 64）；实际档位只信 PlayUrlData::quality。
inline constexpr int kM1RequestQn = 80;

// ---------------------------------------------------------------- T8.9 DASH
//
// playurl(fnval=4048) 的 DASH 段解析与档位目录。这一层只解析/排序/选档，
// **不做双输入播放**（T8.10）、不切档（T8.12）、不下载。
//
// 关键事实（T8.9 实测，3 条 OB 样本）：
// - `dash.video[].id` 才是实际档位（qn）；`data.quality` 是“服务端选中档”，
//   可能与 dash 列表不一致（匿名时请求 qn=116，quality=64，dash 只有 16/32），
//   因此档位判断只信 dash.video[]，`quality` 只作为证据记录。
// - 一次 fnval=4048 请求返回的是**账号允许的完整档位梯**（16/32/64/80 …
//   每个 qn 可能有 AVC/HEVC/AV1 多条），与请求 qn 基本无关；请求 qn 只影响
//   `data.quality`。所以档位菜单不需要逐档请求。
// - `dash.video[].size` 不存在（服务端不给总字节）；size=0 表示未知，
//   不得用带宽×时长伪造精确值（T8.10 用 Content-Length/末段 Range 对账）。
// - 每条流有主 URL + 2 个 backupUrl；`SegmentBase.Initialization` /
//   `indexRange` 是闭区间字节范围（"0-916" / "917-1244"）。
// - 普通用户（非大会员）3/3 只到 1080P30（AVC qn=80）；116/74/120 不出现。
inline constexpr int kDashFnval = 4048;  // 16(DASH)+64(HDR)+128(4K)+256(Dolby audio)
                                         // +512(Dolby vision)+1024(8K)+2048(AV1)
// DASH 目录/菜单的请求档位：与 M1 一致封顶 1080P，fourk=1 只让服务端把
// “账号允许的档位梯”返回全（含 4K 时仅在大会员账号出现），不自动选 4K。
inline constexpr int kDashRequestQn = 80;

// 闭区间字节范围（SegmentBase.Initialization / indexRange）。
struct DashRange {
  qint64 start = -1;
  qint64 end = -1;
  bool valid() const { return start >= 0 && end >= start; }
  qint64 bytes() const { return valid() ? end - start + 1 : 0; }
  QString ToString() const;  // "start-end"；无效时空串
};

// dash.video[] 的一条流（同一 qn 可能有 AVC/HEVC/AV1）。
struct DashStream {
  int qn = 0;        // dash.video[].id
  int codecid = 0;   // 7=AVC / 12=HEVC / 13=AV1；缺失时按 codecs 推断
  int width = 0;
  int height = 0;
  double fps = 0.0;         // frameRate / frame_rate
  qint64 bandwidth = 0;     // bps
  qint64 size = 0;          // dash 不返回 size：0=未知（不伪造）
  QString mime;
  QString codecs;
  bool start_with_sap = false;
  DashRange init_range;
  DashRange index_range;
  QString url;              // baseUrl/base_url；空=该条流没有可用地址
  QStringList backup_urls;  // 去空/去重/保序
  bool urls_ok = false;     // 主 URL 是 https 且 host 非空
  bool init_ok = false;     // init/index 范围都有效

  QStringList AllUrls() const;
};

// dash.audio[] 的一条流（id 是音频质量 id：30216/30232/30280…）。
struct DashAudioStream {
  int id = 0;
  int codecid = 0;  // 通常 0（响应里不给）；mime/codecs 区分 AAC
  qint64 bandwidth = 0;
  qint64 size = 0;  // 0=未知
  QString mime;
  QString codecs;
  bool start_with_sap = false;
  DashRange init_range;
  DashRange index_range;
  QString url;
  QStringList backup_urls;
  bool urls_ok = false;
  bool init_ok = false;

  QStringList AllUrls() const;
};

// 一个 qn 档位（同 qn 的 AVC/HEVC/AV1 变体）。展示字段优先取 AVC：
// 默认只选 AVC（O6），备选按 codecid 7/12/13 排序。
struct QualityTier {
  int qn = 0;
  int width = 0;
  int height = 0;
  double fps = 0.0;
  qint64 bandwidth = 0;
  QVector<DashStream> streams;

  bool HasCodec(int codecid) const;
  const DashStream* Stream(int codecid) const;
  const DashStream* PreferredStream() const;  // AVC > HEVC > AV1 > 第一条
  bool has_avc() const { return HasCodec(7); }
  bool has_hevc() const { return HasCodec(12); }
  bool has_av1() const { return HasCodec(13); }
};

// DASH 目录：来自一次 playurl(fnval=4048) 的完整返回。
struct DashCatalog {
  QVector<DashStream> videos;         // 服务端原始顺序（仅有效条目）
  QVector<DashAudioStream> audios;    // 服务端原始顺序
  QVector<QualityTier> tiers;         // 按 qn 降序去重（解析末尾构建）
  qint64 duration_ms = 0;             // dash.duration 秒 → ms；缺失回退 timelength
  int claimed_quality = 0;            // data.quality：**不可信**，只记录
  QString claimed_format;             // data.format
  QVector<int> claimed_accept_quality;  // accept_quality：不可信，只记录
  int skipped_video_entries = 0;      // 字段无效被跳过的条数（不崩）
  int skipped_audio_entries = 0;
  qint64 parse_elapsed_ms = 0;

  bool has_dash = false;
  bool HasAudio() const { return !audios.isEmpty(); }
  bool HasVideo() const { return !videos.isEmpty(); }
  int MaxQn() const;
  const QualityTier* FindTier(int qn) const;
  const DashStream* FindStream(int qn, int codecid) const;
  const DashAudioStream* BestAudio() const;  // 最高带宽；无音频返回 nullptr
  bool ClaimedQualityInTiers() const;        // 证据：quality 字段是否撒谎
  // 默认档：AVC、非“只手动”、按 O6 顺序 1080P60→720P60→1080P30→720P30→480P30。
  // 没有 AVC 时回退“最高非手动档”（HEVC-only 可播但不自动选 60fps/4K）。
  // 只有手动档/没有视频时返回 0。
  int DefaultQn() const;
  const DashStream* DefaultStream() const;  // DefaultQn 的 AVC/首选变体
  bool HasAutoSelectableTier() const { return DefaultQn() > 0; }
  QString Summary() const;  // 证据单行（不含 URL）
};

// 60fps 档（116/74）与 4K/HDR/杜比/8K（>=120）只允许手动选择（O6/O11）。
bool IsManualOnlyQuality(int qn);
QString CodecName(int codecid);
QString QualityTierLabel(const QualityTier& tier);  // "1080P · AVC · 30.0fps"

// 由 videos 重建 tiers（按 qn 降序去重、变体按 AVC/HEVC/AV1 排序、展示字段取
// AVC）。ParseDashPlayUrlJson 末尾调用；合成目录（缓存命中 meta / 测试夹具）
// 也用它，避免“两套档位归并逻辑”。
void RebuildDashTiers(DashCatalog* catalog);

// 请求 DASH：fnval=4048、qn=kDashRequestQn、fourk=1（只影响服务端返回哪些档，
// 不自动选 4K）。bvid/cid 非法时不发请求。
QString BuildDashPlayUrlUrl(const QString& bvid, qint64 cid,
                            int qn = kDashRequestQn, int fourk = 1);

// 解析 DASH playurl JSON。成功 → error.ok()；API 错误按 MapApiError 映射。
// 只依赖 dash.video[]/dash.audio[]；`data.quality` 只写入 claimed_quality。
// 单条字段无效会被跳过并计数（skipped_*），不因一条坏数据整体失败；
// dash 缺失/视频为空才返回 kParse。
BiliResolveError ParseDashPlayUrlJson(const QByteArray& json, DashCatalog* out);

struct DashFetchOptions {
  int timeout_ms = 6000;
  QString sessdata;  // 可选；非空时由 WinHttpClient 只发 api.bilibili.com
  int qn = kDashRequestQn;
  int fourk = 1;
};

struct DashFetchResult {
  DashCatalog catalog;
  BiliResolveError error;
  int http_requests = 0;
  int http_status = 0;
  qint64 elapsed_ms = 0;
  bool request_cookie_sent = false;
};

// 同步执行；调用方负责放到工作线程。cancel 可在其它线程置位。
DashFetchResult FetchDashCatalog(IHttpClient* http, const QString& bvid,
                                 qint64 cid, const DashFetchOptions& options,
                                 const HttpCancelToken& cancel);

struct PlayUrlFetchOptions {
  int timeout_ms = 6000;
  // 可选 SESSDATA；非空时由 WinHttpClient 只发 api.bilibili.com。
  QString sessdata;
  // 透传给 playurl 的 fourk；0 保持 M1 旧行为（不请求 4K）。
  int fourk = 0;
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
