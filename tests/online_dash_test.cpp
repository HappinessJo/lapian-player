// T8.9 DASH 解析与档位单测（纯离线：合成 JSON + example.invalid URL，不联网）。
//
// 覆盖（对应 tasks-v2 T8.9 验收）：
// - playurl(fnval=4048) URL 构造；M1 durl URL（fnval=0）不变；
// - dash.video[]/audio[] 字段：qn/codecid/fps/带宽/size/init/index/备份 URL；
// - `quality` 字段不可信：档位只信 dash.video[]；
// - 默认 AVC；档位顺序 1080P60→720P60→1080P30→720P30→480P30；
// - 60fps/4K 只手动（不自动选；HEVC-only 60fps 不自动）；
// - 错误 qn/坏字段不崩（跳过单条，不整体失败）；无音轨可解析；
// - 档位菜单模型：只列示/置灰说明 T8.12、4K 标“可用档”、不把可选写成保证可达。
//
// 退出码：0 = 全部通过；1 = 失败。

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdio>

#include "online/PlayUrl.h"
#include "online/QualityMenu.h"

namespace {

using lapian::online::BiliErrorKind;
using lapian::online::BiliResolveError;
using lapian::online::DashAudioStream;
using lapian::online::DashCatalog;
using lapian::online::DashStream;
using lapian::online::QualityMenuContext;
using lapian::online::QualityMenuEntry;
using lapian::online::QualityMenuKind;
using lapian::online::QualityMenuModelText;
using lapian::online::QualityTier;

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* what) {
  ++g_checks;
  if (condition) {
    std::printf("[ OK ] %s\n", what);
  } else {
    std::printf("[FAIL] %s\n", what);
    ++g_failures;
  }
}

void CheckEq(const QString& actual, const QString& expected, const char* what) {
  ++g_checks;
  if (actual == expected) {
    std::printf("[ OK ] %s\n", what);
    return;
  }
  std::printf("[FAIL] %s\n  实际：%s\n  期望：%s\n", what,
              actual.toUtf8().constData(), expected.toUtf8().constData());
  ++g_failures;
}

void CheckEqI64(qint64 actual, qint64 expected, const char* what) {
  ++g_checks;
  if (actual == expected) {
    std::printf("[ OK ] %s\n", what);
    return;
  }
  std::printf("[FAIL] %s\n  实际：%lld\n  期望：%lld\n", what,
              static_cast<long long>(actual), static_cast<long long>(expected));
  ++g_failures;
}

// ---------------------------------------------------------------- fixtures

QString CodecsFor(int codecid) {
  switch (codecid) {
    case 7:
      return QStringLiteral("avc1.640028");
    case 12:
      return QStringLiteral("hvc1.1.6.L120.90");
    case 13:
      return QStringLiteral("av01.0.08M.08.0.110.01.01.01.0");
    default:
      return QStringLiteral("unknown");
  }
}

QJsonObject MakeVideoObject(int qn, int codecid, int width, int height,
                            const QJsonValue& frame_rate, qint64 bandwidth,
                            const QString& init, const QString& index,
                            int backup_count, bool camel_case = true) {
  QJsonObject object;
  object.insert(QStringLiteral("id"), qn);
  object.insert(camel_case ? QStringLiteral("baseUrl")
                           : QStringLiteral("base_url"),
                QStringLiteral("https://example.invalid/v-%1-%2.m4s")
                    .arg(qn)
                    .arg(codecid));
  QJsonArray backups;
  for (int i = 0; i < backup_count; ++i) {
    backups.append(QStringLiteral("https://example.invalid/b-%1-%2-%3.m4s")
                       .arg(qn)
                       .arg(codecid)
                       .arg(i));
  }
  object.insert(camel_case ? QStringLiteral("backupUrl")
                           : QStringLiteral("backup_url"),
                backups);
  object.insert(QStringLiteral("bandwidth"), bandwidth);
  object.insert(QStringLiteral("mimeType"), QStringLiteral("video/mp4"));
  object.insert(QStringLiteral("codecs"), CodecsFor(codecid));
  object.insert(QStringLiteral("width"), width);
  object.insert(QStringLiteral("height"), height);
  object.insert(camel_case ? QStringLiteral("frameRate")
                           : QStringLiteral("frame_rate"),
                frame_rate);
  object.insert(QStringLiteral("startWithSap"), 1);
  object.insert(QStringLiteral("codecid"), codecid);
  QJsonObject segment;
  segment.insert(QStringLiteral("Initialization"), init);
  segment.insert(QStringLiteral("indexRange"), index);
  object.insert(QStringLiteral("SegmentBase"), segment);
  return object;
}

QJsonObject MakeAudioObject(int id, qint64 bandwidth, bool camel_case = true) {
  QJsonObject object;
  object.insert(QStringLiteral("id"), id);
  object.insert(camel_case ? QStringLiteral("baseUrl")
                           : QStringLiteral("base_url"),
                QStringLiteral("https://example.invalid/a-%1.m4s").arg(id));
  QJsonArray backups;
  backups.append(
      QStringLiteral("https://example.invalid/a-%1-b1.m4s").arg(id));
  object.insert(camel_case ? QStringLiteral("backupUrl")
                           : QStringLiteral("backup_url"),
                backups);
  object.insert(QStringLiteral("bandwidth"), bandwidth);
  object.insert(QStringLiteral("mimeType"), QStringLiteral("audio/mp4"));
  object.insert(QStringLiteral("codecs"), QStringLiteral("mp4a.40.2"));
  object.insert(QStringLiteral("codecid"), 0);
  object.insert(QStringLiteral("startWithSap"), 1);
  QJsonObject segment;
  segment.insert(QStringLiteral("Initialization"), QStringLiteral("0-836"));
  segment.insert(QStringLiteral("indexRange"), QStringLiteral("837-1152"));
  object.insert(QStringLiteral("SegmentBase"), segment);
  return object;
}

QByteArray MakeDashJson(int claimed_quality,
                        const QVector<QJsonObject>& videos,
                        const QVector<QJsonObject>& audios, int duration_seconds,
                        qint64 timelength_ms = -1) {
  QJsonObject data;
  data.insert(QStringLiteral("quality"), claimed_quality);
  data.insert(QStringLiteral("format"), QStringLiteral("flv720"));
  data.insert(QStringLiteral("timelength"),
              timelength_ms >= 0 ? timelength_ms : duration_seconds * 1000);
  data.insert(QStringLiteral("video_codecid"), 7);
  QJsonArray accept;
  for (int qn : {116, 80, 64, 32, 16}) {
    accept.append(qn);
  }
  data.insert(QStringLiteral("accept_quality"), accept);
  QJsonObject dash;
  if (duration_seconds >= 0) {
    dash.insert(QStringLiteral("duration"), duration_seconds);
  }
  QJsonArray video_array;
  for (const QJsonObject& video : videos) {
    video_array.append(video);
  }
  dash.insert(QStringLiteral("video"), video_array);
  QJsonArray audio_array;
  for (const QJsonObject& audio : audios) {
    audio_array.append(audio);
  }
  dash.insert(QStringLiteral("audio"), audio_array);
  data.insert(QStringLiteral("dash"), dash);
  QJsonObject root;
  root.insert(QStringLiteral("code"), 0);
  root.insert(QStringLiteral("message"), QStringLiteral("0"));
  root.insert(QStringLiteral("data"), data);
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// 普通用户实测形态：16/32/64/80 × AVC/HEVC/AV1 + 3 条 AAC。
QByteArray MakeNormalUserDash(int claimed_quality = 80) {
  QVector<QJsonObject> videos;
  videos.append(MakeVideoObject(80, 7, 1920, 1080, QStringLiteral("30.000"),
                                2249181, QStringLiteral("0-917"),
                                QStringLiteral("918-1245"), 2));
  videos.append(MakeVideoObject(80, 12, 1920, 1080, QStringLiteral("30.000"),
                                741000, QStringLiteral("0-1083"),
                                QStringLiteral("1084-1411"), 2));
  videos.append(MakeVideoObject(80, 13, 1920, 1080, QStringLiteral("30.000"),
                                830000, QStringLiteral("0-978"),
                                QStringLiteral("979-1306"), 2));
  videos.append(MakeVideoObject(64, 7, 1280, 720, QStringLiteral("30.000"),
                                1200000, QStringLiteral("0-900"),
                                QStringLiteral("901-1200"), 2));
  videos.append(MakeVideoObject(32, 7, 852, 480, QStringLiteral("30.000"),
                                480554, QStringLiteral("0-916"),
                                QStringLiteral("917-1244"), 2));
  videos.append(MakeVideoObject(16, 7, 640, 360, QStringLiteral("30.000"),
                                326621, QStringLiteral("0-916"),
                                QStringLiteral("917-1244"), 2));
  QVector<QJsonObject> audios;
  audios.append(MakeAudioObject(30280, 93657));
  audios.append(MakeAudioObject(30216, 65740));
  audios.append(MakeAudioObject(30232, 93657));
  return MakeDashJson(claimed_quality, videos, audios, 111);
}

const QualityTier* FindTier(const DashCatalog& catalog, int qn) {
  return catalog.FindTier(qn);
}

const QualityMenuEntry* FindEntry(const QVector<QualityMenuEntry>& entries,
                                  int qn) {
  for (const QualityMenuEntry& entry : entries) {
    if (entry.kind == QualityMenuKind::kTier && entry.qn == qn) {
      return &entry;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------- tests

void TestBuildUrls() {
  using lapian::online::BuildDashPlayUrlUrl;
  using lapian::online::BuildPlayUrlUrl;
  const QString bvid = QStringLiteral("BV1BMnv6jE5S");
  const qint64 cid = 42915922022LL;

  const QString m1 = BuildPlayUrlUrl(bvid, cid, 80, 0);
  Check(m1.contains(QStringLiteral("qn=80")), "M1 durl URL 仍带 qn=80");
  Check(m1.contains(QStringLiteral("fnval=0")), "M1 durl URL 仍是 fnval=0");
  Check(m1.contains(QStringLiteral("fourk=0")), "M1 durl URL 仍是 fourk=0");
  Check(BuildPlayUrlUrl(bvid, cid, 80, 1, 4048).contains(
            QStringLiteral("fnval=4048")),
        "BuildPlayUrlUrl 支持 fnval 参数");

  const QString dash = BuildDashPlayUrlUrl(bvid, cid);
  Check(dash.contains(QStringLiteral("fnval=4048")),
        "DASH URL 带 fnval=4048");
  Check(dash.contains(QStringLiteral("qn=80")), "DASH URL 默认 qn=80");
  Check(dash.contains(QStringLiteral("fourk=1")),
        "DASH URL 默认 fourk=1（只请求档位梯，不自动选 4K）");
  Check(dash.contains(QStringLiteral("bvid=BV1BMnv6jE5S")),
        "DASH URL 带 bvid");
  Check(dash.contains(QStringLiteral("cid=42915922022")), "DASH URL 带 cid");
  Check(BuildDashPlayUrlUrl(QStringLiteral("BV1"), 1).isEmpty(),
        "非法 bvid 不发请求（DASH）");
  Check(BuildDashPlayUrlUrl(bvid, 0).isEmpty(), "cid<=0 不发请求（DASH）");
}

void TestParseNormalUser() {
  DashCatalog catalog;
  const BiliResolveError error =
      lapian::online::ParseDashPlayUrlJson(MakeNormalUserDash(), &catalog);
  Check(error.ok(), "普通用户 DASH JSON 解析成功");
  Check(catalog.has_dash, "has_dash");
  CheckEqI64(catalog.duration_ms, 111000, "dash.duration 秒 → ms");
  CheckEqI64(catalog.claimed_quality, 80, "quality 字段记录（80）");
  CheckEqI64(catalog.claimed_accept_quality.size(), 5, "accept_quality 记录 5 项");
  Check(catalog.HasVideo(), "有视频流");
  Check(catalog.HasAudio(), "有音频流（音轨存在）");
  CheckEqI64(catalog.videos.size(), 6, "6 条视频流");
  CheckEqI64(catalog.audios.size(), 3, "3 条音频流");
  CheckEqI64(catalog.skipped_video_entries, 0, "没有跳过视频条数");
  CheckEqI64(catalog.skipped_audio_entries, 0, "没有跳过音频条数");

  CheckEqI64(catalog.tiers.size(), 4, "4 个档位（80/64/32/16）");
  CheckEqI64(catalog.tiers[0].qn, 80, "档位按 qn 降序：首项 80");
  CheckEqI64(catalog.tiers[1].qn, 64, "档位按 qn 降序：次项 64");
  CheckEqI64(catalog.tiers[3].qn, 16, "档位按 qn 降序：末项 16");

  const QualityTier* tier80 = FindTier(catalog, 80);
  Check(tier80 != nullptr, "存在 1080P 档");
  Check(tier80 != nullptr && tier80->streams.size() == 3,
        "1080P 有 AVC/HEVC/AV1 三个变体");
  Check(tier80 != nullptr && tier80->has_avc(), "1080P 有 AVC");
  Check(tier80 != nullptr && tier80->has_hevc(), "1080P 有 HEVC");
  Check(tier80 != nullptr && tier80->has_av1(), "1080P 有 AV1");
  CheckEqI64(tier80 != nullptr ? tier80->streams[0].codecid : 0, 7,
             "变体排序：AVC 第一");
  CheckEqI64(tier80 != nullptr ? tier80->width : 0, 1920, "1080P 宽 1920");
  CheckEqI64(tier80 != nullptr ? tier80->height : 0, 1080, "1080P 高 1080");
  Check(tier80 != nullptr && tier80->fps > 29.9 && tier80->fps < 30.1,
        "1080P fps=30.000");
  CheckEqI64(tier80 != nullptr ? tier80->bandwidth : 0, 2249181,
             "1080P 带宽取 AVC");

  const DashStream* avc = catalog.FindStream(80, 7);
  Check(avc != nullptr, "FindStream(80, AVC)");
  Check(avc != nullptr && avc->urls_ok, "主 URL 是 https");
  Check(avc != nullptr && avc->backup_urls.size() == 2, "2 个备份 URL");
  Check(avc != nullptr && avc->AllUrls().size() == 3, "主 + 2 备份 = 3 个 URL");
  Check(avc != nullptr && avc->init_range.ToString() == QStringLiteral("0-917"),
        "init range 0-917");
  Check(avc != nullptr &&
            avc->index_range.ToString() == QStringLiteral("918-1245"),
        "index range 918-1245");
  CheckEqI64(avc != nullptr ? avc->init_range.bytes() : 0, 918,
             "init range 字节数 = end-start+1");
  CheckEqI64(avc != nullptr ? avc->size : -1, 0,
             "DASH 不返回 size：0=未知，不伪造");
  Check(avc != nullptr && avc->init_ok, "init/index 范围有效");
  Check(avc != nullptr && avc->start_with_sap, "startWithSap 记录");

  const DashAudioStream* best = catalog.BestAudio();
  Check(best != nullptr, "BestAudio 非空");
  CheckEqI64(best != nullptr ? best->id : 0, 30280, "最高带宽音频 30280");
  CheckEqI64(best != nullptr ? best->bandwidth : 0, 93657, "音频带宽记录");
  Check(best != nullptr && best->init_ok, "音频 init/index 有效");

  CheckEqI64(catalog.MaxQn(), 80, "MaxQn=80");
  CheckEqI64(catalog.DefaultQn(), 80, "默认档 1080P（AVC）");
  Check(catalog.DefaultStream() != nullptr &&
            catalog.DefaultStream()->codecid == 7,
        "默认流是 AVC");
  Check(catalog.ClaimedQualityInTiers(), "quality=80 在档位列表里");
  Check(catalog.Summary().contains(QStringLiteral("tiers=80(1920x1080")),
        "Summary 列出档位（不含 URL）");
  Check(!catalog.Summary().contains(QStringLiteral("example.invalid")),
        "Summary 不含 URL");
}

void TestQualityFieldUntrusted() {
  // 匿名实测形态：请求 qn=116 返回 quality=64，但 dash 只有 16/32。
  QVector<QJsonObject> videos;
  videos.append(MakeVideoObject(32, 7, 852, 480, QStringLiteral("30.000"),
                                480554, QStringLiteral("0-916"),
                                QStringLiteral("917-1244"), 2));
  videos.append(MakeVideoObject(16, 7, 640, 360, QStringLiteral("30.000"),
                                326621, QStringLiteral("0-916"),
                                QStringLiteral("917-1244"), 2));
  QVector<QJsonObject> audios;
  audios.append(MakeAudioObject(30280, 93657));
  const QByteArray json = MakeDashJson(64, videos, audios, 111);

  DashCatalog catalog;
  const BiliResolveError error =
      lapian::online::ParseDashPlayUrlJson(json, &catalog);
  Check(error.ok(), "quality 撒谎的响应仍可解析");
  CheckEqI64(catalog.claimed_quality, 64, "claimed_quality 记录 64");
  Check(!catalog.ClaimedQualityInTiers(),
        "quality=64 不在 dash 档位里（证明 quality 不可信）");
  CheckEqI64(catalog.tiers.size(), 2, "档位只有 16/32");
  CheckEqI64(catalog.DefaultQn(), 32, "默认档由 dash.video[] 决定：480P");
  Check(FindTier(catalog, 64) == nullptr, "不存在 720P 档（不信 quality）");

  QualityMenuContext context;
  context.current_qn = 64;  // 服务端声称的档位
  const QVector<QualityMenuEntry> entries =
      lapian::online::BuildQualityMenuModel(catalog, context);
  Check(FindEntry(entries, 64) == nullptr, "菜单不因 quality 字段伪造 720P");
  Check(FindEntry(entries, 32) != nullptr, "菜单列出实际 480P");
  Check(QualityMenuModelText(entries).contains(
            QStringLiteral("当前播放 720P 不在 DASH 目录里")),
        "菜单明确说明当前 durl 档不在 DASH 目录");
}

void TestParseErrorsAndRobustness() {
  using lapian::online::BiliErrorKind;
  DashCatalog catalog;

  // API 错误码。
  const QByteArray api_error =
      QByteArray(R"json({"code":-404,"message":"啥都木有","data":null})json");
  BiliResolveError error =
      lapian::online::ParseDashPlayUrlJson(api_error, &catalog);
  Check(!error.ok() && error.kind == BiliErrorKind::kApi &&
            error.api_code == -404,
        "API 错误码 → kApi/-404");

  // 只有 durl（没有 dash）。
  const QByteArray no_dash = QByteArray(
      R"json({"code":0,"message":"0","data":{"quality":64,"durl":[{"url":"https://example.invalid/x.mp4","size":1}]}})json");
  error = lapian::online::ParseDashPlayUrlJson(no_dash, &catalog);
  Check(!error.ok() && error.detail == QStringLiteral("no-dash"),
        "没有 dash 段 → no-dash（不猜）");

  // dash 存在但视频为空。
  const QByteArray empty_video = QByteArray(
      R"json({"code":0,"message":"0","data":{"quality":64,"dash":{"duration":10,"video":[],"audio":[]}}})json");
  error = lapian::online::ParseDashPlayUrlJson(empty_video, &catalog);
  Check(!error.ok() && error.detail == QStringLiteral("no-video"),
        "dash.video 为空 → no-video");

  // 坏条目跳过；snake_case；codecid 由 codecs 推断；坏 URL 保留但 urls_ok=false；
  // 坏 range 保留但 init_ok=false；音频缺失仍可解析。
  QVector<QJsonObject> videos;
  QJsonObject missing_id =
      MakeVideoObject(64, 7, 1280, 720, QStringLiteral("30.000"), 1000000,
                      QStringLiteral("0-900"), QStringLiteral("901-1200"), 1);
  missing_id.remove(QStringLiteral("id"));
  videos.append(missing_id);
  QJsonObject inferred =
      MakeVideoObject(64, 12, 1280, 720, 30.0, 900000,
                      QStringLiteral("0-900"), QStringLiteral("901-1200"), 1,
                      /*camel_case=*/false);
  inferred.remove(QStringLiteral("codecid"));
  videos.append(inferred);
  QJsonObject bad_url =
      MakeVideoObject(32, 7, 852, 480, QStringLiteral("30.000"), 480000,
                      QStringLiteral("0-916"), QStringLiteral("917-1244"), 0);
  bad_url.insert(QStringLiteral("baseUrl"),
                 QStringLiteral("http://insecure.example.invalid/v.m4s"));
  videos.append(bad_url);
  QJsonObject bad_range =
      MakeVideoObject(16, 7, 640, 360, QStringLiteral("30.000"), 320000,
                      QStringLiteral("abc"), QStringLiteral(""), 0);
  videos.append(bad_range);
  const QByteArray json = MakeDashJson(64, videos, {}, 10);
  error = lapian::online::ParseDashPlayUrlJson(json, &catalog);
  Check(error.ok(), "坏条目不影响整体解析");
  CheckEqI64(catalog.videos.size(), 3, "跳过缺少 id 的条目（3/4 保留）");
  CheckEqI64(catalog.skipped_video_entries, 1, "记录跳过 1 条");
  Check(!catalog.HasAudio(), "无 dash.audio → HasAudio=false（可解析）");
  const DashStream* hevc = catalog.FindStream(64, 12);
  Check(hevc != nullptr, "snake_case + codecid 缺失按 codecs 推断 HEVC");
  Check(hevc != nullptr && hevc->fps > 29.9 && hevc->fps < 30.1,
        "frameRate 为数字也能解析");
  const DashStream* insecure = catalog.FindStream(32, 7);
  Check(insecure != nullptr && !insecure->urls_ok,
        "非 https 主 URL：条目保留但 urls_ok=false");
  const DashStream* bad = catalog.FindStream(16, 7);
  Check(bad != nullptr && !bad->init_ok, "坏 range：条目保留但 init_ok=false");

  // duration 缺失时回退 data.timelength。
  QVector<QJsonObject> one;
  one.append(MakeVideoObject(32, 7, 852, 480, QStringLiteral("30.000"), 480000,
                             QStringLiteral("0-916"), QStringLiteral("917-1244"),
                             1));
  const QByteArray no_duration =
      MakeDashJson(32, one, {MakeAudioObject(30280, 90000)}, -1, 96000);
  error = lapian::online::ParseDashPlayUrlJson(no_duration, &catalog);
  Check(error.ok() && catalog.duration_ms == 96000,
        "dash.duration 缺失 → timelength 毫秒");
}

void TestSelection() {
  using lapian::online::IsManualOnlyQuality;
  Check(!IsManualOnlyQuality(80), "1080P30 可自动");
  Check(!IsManualOnlyQuality(64), "720P 可自动");
  Check(!IsManualOnlyQuality(32), "480P 可自动");
  Check(!IsManualOnlyQuality(16), "360P 可自动");
  Check(!IsManualOnlyQuality(112), "1080P+ 不是只手动");
  Check(!IsManualOnlyQuality(116), "1080P60 不能只凭 qn 判只手动（看编码）");
  Check(IsManualOnlyQuality(120), "4K 只手动");
  Check(IsManualOnlyQuality(125), "HDR 只手动");
  Check(IsManualOnlyQuality(126), "杜比视界只手动");
  Check(IsManualOnlyQuality(127), "8K 只手动");

  auto catalog_with = [](const QVector<DashStream>& streams) {
    DashCatalog catalog;
    catalog.videos = streams;
    lapian::online::RebuildDashTiers(&catalog);
    return catalog;
  };
  auto stream = [](int qn, int codecid, int width, int height, double fps) {
    DashStream value;
    value.qn = qn;
    value.codecid = codecid;
    value.width = width;
    value.height = height;
    value.fps = fps;
    value.bandwidth = 1000000;
    return value;
  };

  // O6/T8.12：1080P60 有 AVC 时可自动（保帧率优先）。
  DashCatalog vip = catalog_with({stream(80, 7, 1920, 1080, 30.0),
                                  stream(116, 7, 1920, 1080, 60.0),
                                  stream(74, 7, 1280, 720, 60.0)});
  CheckEqI64(vip.DefaultQn(), 116, "1080P60 AVC 可用 → 自动选 116");
  Check(vip.DefaultStream() != nullptr && vip.DefaultStream()->codecid == 7,
        "自动 60fps 仍是 AVC");

  // HEVC-only 60fps：只手动，不自动（DefaultQn=0）。
  DashCatalog hevc60 = catalog_with({stream(116, 12, 1920, 1080, 60.0)});
  CheckEqI64(hevc60.DefaultQn(), 0, "HEVC-only 60fps 不自动选");
  Check(lapian::online::IsManualOnlyTier(hevc60.tiers.first()),
        "HEVC-only 60fps 档是只手动");

  // 720P60 AVC + 1080P30 AVC：按 O6 顺序先 720P60。
  DashCatalog ladder60 = catalog_with({stream(80, 7, 1920, 1080, 30.0),
                                       stream(74, 7, 1280, 720, 60.0)});
  CheckEqI64(ladder60.DefaultQn(), 74, "720P60 AVC 优先于 1080P30");

  // 只有 720P30 + 1080P30：选 1080P30。
  DashCatalog ladder = catalog_with({stream(64, 7, 1280, 720, 30.0),
                                     stream(80, 7, 1920, 1080, 30.0)});
  CheckEqI64(ladder.DefaultQn(), 80, "1080P30 优先于 720P30");

  // 只有 4K：不自动选。
  DashCatalog only4k = catalog_with({stream(120, 12, 3840, 2160, 60.0)});
  CheckEqI64(only4k.DefaultQn(), 0, "只有 4K 时不自动选");

  // HEVC-only 1080P30：可自动但默认不是 AVC。
  DashCatalog hevc1080 = catalog_with({stream(64, 12, 1280, 720, 30.0),
                                       stream(80, 12, 1920, 1080, 30.0)});
  CheckEqI64(hevc1080.DefaultQn(), 80, "HEVC-only 1080P30 回退最高非手动档");
  Check(hevc1080.DefaultStream() != nullptr &&
            hevc1080.DefaultStream()->codecid == 12,
        "上述默认流是 HEVC（没有 AVC 可选）");

  // 480P AVC + 4K：仍选 480P（不自动 4K）。
  DashCatalog mixed = catalog_with({stream(32, 7, 852, 480, 30.0),
                                    stream(120, 12, 3840, 2160, 60.0)});
  CheckEqI64(mixed.DefaultQn(), 32, "480P + 4K：不自动 4K");

  // AV1-only 720P：可自动（AV1 不是手动档）。
  DashCatalog av1 = catalog_with({stream(64, 13, 1280, 720, 30.0)});
  CheckEqI64(av1.DefaultQn(), 64, "AV1-only 720P 可自动（T8.10 决定可播）");

  // 112 排在 80 之后：有 80 时不默认更高码率。
  DashCatalog high = catalog_with({stream(80, 7, 1920, 1080, 30.0),
                                   stream(112, 7, 1920, 1080, 30.0)});
  CheckEqI64(high.DefaultQn(), 80, "同分辨率时默认 80 而不是 112");
}

void TestMenuModel() {
  DashCatalog catalog;
  lapian::online::ParseDashPlayUrlJson(MakeNormalUserDash(64), &catalog);

  // 当前播放 durl 720P（OB-1/OB-3 实测）：720P 勾选可点，其余 DASH 档置灰。
  QualityMenuContext context;
  context.current_qn = 64;
  context.current_codecid = 7;
  const QVector<QualityMenuEntry> entries =
      lapian::online::BuildQualityMenuModel(catalog, context);

  Check(!entries.isEmpty() &&
            entries.first().kind == QualityMenuKind::kHeader &&
            entries.first().text.contains(QStringLiteral("playurl 返回为准")),
        "菜单首项是说明 header");
  const QualityMenuEntry* tier80 = FindEntry(entries, 80);
  const QualityMenuEntry* tier64 = FindEntry(entries, 64);
  Check(tier80 != nullptr && !tier80->enabled && !tier80->checked,
        "非当前档位（1080P）置灰");
  Check(tier80 != nullptr &&
            tier80->disabled_reason.contains(QStringLiteral("不能重新取流")),
        "1080P 置灰原因：当前会话不能重新取流");
  Check(tier80 != nullptr &&
            tier80->text.contains(QStringLiteral("当前不可切换")),
        "1080P 文案标注当前不可切换");
  Check(tier64 != nullptr && tier64->checked && tier64->enabled,
        "当前 durl 720P 勾选且可点（只提示已是当前档）");
  const QString text = QualityMenuModelText(entries);
  Check(text.contains(QStringLiteral("菜单只列 playurl 实际返回")),
        "菜单说明只列 playurl 实际返回");
  Check(text.contains(QStringLiteral("菜单可选不等于保证可达")),
        "菜单不把可选写成保证可达");
  Check(FindEntry(entries, 116) == nullptr, "普通用户菜单没有 1080P60");
  Check(FindEntry(entries, 120) == nullptr, "普通用户菜单没有 4K");

  // T8.12：会话就绪（dash_playback_available=true）后非当前档可手动切；
  // 60fps AVC 是自动档，但当前不是它时仍可手动选择。
  QualityMenuContext switchable = context;
  switchable.dash_playback_available = true;
  const QVector<QualityMenuEntry> switch_entries =
      BuildQualityMenuModel(catalog, switchable);
  const QualityMenuEntry* switch80 = FindEntry(switch_entries, 80);
  Check(switch80 != nullptr && switch80->enabled && !switch80->checked,
        "就绪后 1080P 可手动切");
  Check(switch80 != nullptr &&
            switch80->tooltip.contains(QStringLiteral("重新取流")),
        "就绪后提示会重新取流");

  // 未登录：明确写出最高约 480P30，不承诺 720/1080。
  QualityMenuContext anon;
  anon.logged_in = false;
  anon.current_qn = 32;
  anon.current_from_cache = true;
  DashCatalog anon_catalog;
  anon_catalog.videos = {[] {
    DashStream value;
    value.qn = 32;
    value.codecid = 7;
    value.width = 852;
    value.height = 480;
    value.fps = 30.0;
    return value;
  }()};
  lapian::online::RebuildDashTiers(&anon_catalog);
  const QString anon_text =
      QualityMenuModelText(BuildQualityMenuModel(anon_catalog, anon));
  Check(anon_text.contains(QStringLiteral("未登录：最高约 480P30")),
        "未登录显示最高约 480P30");
  Check(anon_text.contains(QStringLiteral("以实际返回为准")),
        "未登录提示以实际返回为准");

  // 大会员合成目录：1080P60 / 720P60 / 4K 进菜单、只手动、置灰；默认仍 1080P30。
  DashCatalog vip_catalog;
  vip_catalog.videos = {[] {
                          DashStream value;
                          value.qn = 80;
                          value.codecid = 7;
                          value.width = 1920;
                          value.height = 1080;
                          value.fps = 30.0;
                          return value;
                        }(),
                        [] {
                          DashStream value;
                          value.qn = 116;
                          value.codecid = 12;
                          value.width = 1920;
                          value.height = 1080;
                          value.fps = 60.0;
                          return value;
                        }(),
                        [] {
                          DashStream value;
                          value.qn = 74;
                          value.codecid = 12;
                          value.width = 1280;
                          value.height = 720;
                          value.fps = 60.0;
                          return value;
                        }(),
                        [] {
                          DashStream value;
                          value.qn = 120;
                          value.codecid = 12;
                          value.width = 3840;
                          value.height = 2160;
                          value.fps = 60.0;
                          return value;
                        }()};
  lapian::online::RebuildDashTiers(&vip_catalog);
  CheckEqI64(vip_catalog.DefaultQn(), 80, "大会员目录默认 1080P30（不自动 60/4K）");

  QualityMenuContext vip_context;
  vip_context.current_qn = 80;
  vip_context.current_codecid = 7;
  const QVector<QualityMenuEntry> vip_entries =
      lapian::online::BuildQualityMenuModel(vip_catalog, vip_context);
  const QualityMenuEntry* vip116 = FindEntry(vip_entries, 116);
  const QualityMenuEntry* vip74 = FindEntry(vip_entries, 74);
  const QualityMenuEntry* vip120 = FindEntry(vip_entries, 120);
  Check(vip116 != nullptr && vip116->manual_only,
        "HEVC-only 1080P60 进菜单且标只手动");
  Check(vip116 != nullptr &&
            vip116->text.contains(QStringLiteral("60fps / HEVC（手动）")),
        "HEVC-only 1080P60 文案含“60fps / HEVC（手动）”");
  Check(vip74 != nullptr && vip74->manual_only,
        "HEVC-only 720P60 进菜单且标只手动");
  Check(vip120 != nullptr && vip120->manual_only, "4K 进菜单且标只手动");
  Check(vip120 != nullptr &&
            vip120->text.contains(QStringLiteral("可用档")) &&
            vip120->text.contains(QStringLiteral("性能/颜色不保证")),
        "4K 文案标可用档/性能/颜色不保证");
  Check(vip120 != nullptr && !vip120->enabled, "未就绪时 4K 置灰（不假装可达）");

  // T8.10/T8.12 接入后（dash_playback_available=true）：非当前档可手动切；
  // 只手动档仍不自动选。
  QualityMenuContext future = vip_context;
  future.dash_playback_available = true;
  const QVector<QualityMenuEntry> future_entries =
      lapian::online::BuildQualityMenuModel(vip_catalog, future);
  const QualityMenuEntry* future116 = FindEntry(future_entries, 116);
  const QualityMenuEntry* future120 = FindEntry(future_entries, 120);
  Check(future116 != nullptr && future116->enabled && future116->manual_only,
        "就绪后可手动切 HEVC-only 1080P60（manual_only 仍为 true）");
  Check(future120 != nullptr && future120->enabled && future120->manual_only,
        "就绪后可手动切 4K（只在手动菜单）");

  // AVC 60fps：默认自动，不标“仅手动”；无 AVC 时才是手动档。
  DashCatalog avc60_catalog;
  avc60_catalog.videos = {[] {
                            DashStream value;
                            value.qn = 80;
                            value.codecid = 7;
                            value.width = 1920;
                            value.height = 1080;
                            value.fps = 30.0;
                            return value;
                          }(),
                          [] {
                            DashStream value;
                            value.qn = 116;
                            value.codecid = 7;
                            value.width = 1920;
                            value.height = 1080;
                            value.fps = 60.0;
                            return value;
                          }()};
  lapian::online::RebuildDashTiers(&avc60_catalog);
  CheckEqI64(avc60_catalog.DefaultQn(), 116, "AVC 1080P60 自动选 116");
  const QVector<QualityMenuEntry> avc60_entries =
      lapian::online::BuildQualityMenuModel(avc60_catalog, vip_context);
  const QualityMenuEntry* avc116 = FindEntry(avc60_entries, 116);
  Check(avc116 != nullptr && !avc116->manual_only &&
            avc116->text.contains(QStringLiteral("自动优先")),
        "AVC 1080P60 不标仅手动，标自动优先");

  // 空目录/失败：不伪造档位。
  QualityMenuContext empty;
  empty.error = QStringLiteral("取流请求超时");
  const QVector<QualityMenuEntry> empty_entries =
      lapian::online::BuildQualityMenuModel(DashCatalog{}, empty);
  Check(empty_entries.size() == 2 &&
            empty_entries.last().text.contains(QStringLiteral("档位获取失败")),
        "空目录显示可读失败，不伪造档位");

  // 缓存命中：meta 单档 header 说明未联网；拿到服务端目录后不再谎称未联网。
  QualityMenuContext cache;
  cache.current_qn = 80;
  cache.current_codecid = 7;
  cache.current_from_cache = true;
  DashCatalog cache_meta_catalog;
  cache_meta_catalog.videos = {[] {
    DashStream value;
    value.qn = 80;
    value.codecid = 7;
    value.width = 1920;
    value.height = 1080;
    value.fps = 30.0;
    return value;
  }()};
  lapian::online::RebuildDashTiers(&cache_meta_catalog);
  const QVector<QualityMenuEntry> cache_entries =
      lapian::online::BuildQualityMenuModel(cache_meta_catalog, cache);
  Check(!cache_entries.isEmpty() &&
            cache_entries.first().text.contains(QStringLiteral("缓存命中档位")),
        "缓存 meta 单档 header 说明未联网");
  // B：同一缓存命中会话拿到服务端完整目录后，标题改成服务端口径。
  QualityMenuContext cache_server = cache;
  cache_server.catalog_from_server = true;
  const QVector<QualityMenuEntry> server_entries =
      lapian::online::BuildQualityMenuModel(catalog, cache_server);
  Check(!server_entries.isEmpty() &&
            server_entries.first().text.contains(QStringLiteral("实际可用档位")) &&
            !server_entries.first().text.contains(QStringLiteral("未联网")),
        "服务端目录到达后不谎称未联网");
}

}  // namespace

int main() {
  std::printf("T8.9 DASH 解析/档位单测\n");
  TestBuildUrls();
  TestParseNormalUser();
  TestQualityFieldUntrusted();
  TestParseErrorsAndRobustness();
  TestSelection();
  TestMenuModel();
  std::printf("T8.9 online_dash_test checks=%d failures=%d\n", g_checks,
              g_failures);
  return g_failures == 0 ? 0 : 1;
}
