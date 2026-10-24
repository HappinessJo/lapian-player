#include "app/OnlineQualityTestRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

#include "app/MainWindow.h"
#include "online/BiliAuth.h"
#include "online/BiliResolveClient.h"
#include "online/QualityMenu.h"
#include "online/Sessdata.h"
#include "online/WinHttpClient.h"

namespace lapian::app {
namespace {

int g_checks = 0;
int g_failures = 0;

void Print(const QString& text) {
  std::fprintf(stderr, "%s\n", text.toUtf8().constData());
  std::fflush(stderr);
}

void PrintKV(const QString& key, const QString& value) {
  std::printf("%s=%s\n", key.toUtf8().constData(), value.toUtf8().constData());
  std::fflush(stdout);
}

void PrintKV(const char* key, const QString& value) {
  PrintKV(QString::fromUtf8(key), value);
}

QString Num(qint64 value) { return QString::number(value); }

QString OneLine(const QString& text) {
  QString one = text;
  one.replace(QLatin1Char('\n'), QStringLiteral(" | "));
  return one;
}

void Check(const QString& name, bool ok, const QString& detail = QString()) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
  }
  const QString line =
      QStringLiteral("online_quality_check %1 ok=%2%3")
          .arg(name)
          .arg(ok ? 1 : 0)
          .arg(detail.isEmpty() ? QString()
                                : QStringLiteral(" detail=") + OneLine(detail));
  std::printf("%s\n", line.toUtf8().constData());
  std::fflush(stdout);
}

// ---------------------------------------------------------------- credentials

struct SecretFile {
  bool ok = false;
  QString path;
  QString source;
  QString sessdata;
  QString error;
};

QString ResolveSecretPath(const QString& explicit_path, QString* source) {
  if (!explicit_path.isEmpty()) {
    *source = QStringLiteral("arg");
    return explicit_path;
  }
  const QString env = qEnvironmentVariable("LAPIAN_SESSDATA_FILE");
  if (!env.isEmpty()) {
    *source = QStringLiteral("env");
    return env;
  }
  *source = QStringLiteral("default");
  const QStringList candidates = {
      QDir::current().filePath(QStringLiteral(".secrets/bilibili_sessdata.txt")),
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral(".secrets/bilibili_sessdata.txt")),
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("../.secrets/bilibili_sessdata.txt")),
  };
  for (const QString& candidate : candidates) {
    if (QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return candidates.first();
}

SecretFile ReadSecret(const QString& explicit_path) {
  SecretFile secret;
  secret.path = ResolveSecretPath(explicit_path, &secret.source);
  const QFileInfo info(secret.path);
  if (!info.exists() || !info.isFile()) {
    secret.error = QStringLiteral("missing_sessdata_file");
    return secret;
  }
  if (info.size() <= 0 || info.size() > 8 * 1024) {
    secret.error = QStringLiteral("invalid_sessdata_file_size");
    return secret;
  }
  QFile file(secret.path);
  if (!file.open(QIODevice::ReadOnly)) {
    secret.error = QStringLiteral("unreadable_sessdata_file");
    return secret;
  }
  const QByteArray raw = file.readAll();
  file.close();
  const lapian::online::SessdataNormalization normalized =
      lapian::online::NormalizeSessdata(QString::fromUtf8(raw));
  if (!normalized.ok) {
    secret.error = QStringLiteral("invalid_sessdata");
    return secret;
  }
  secret.ok = true;
  secret.sessdata = normalized.value;
  return secret;
}

// ---------------------------------------------------------------- fixtures

lapian::online::DashStream MakeStream(int qn, int codecid, int width, int height,
                                      double fps, qint64 bandwidth) {
  lapian::online::DashStream stream;
  stream.qn = qn;
  stream.codecid = codecid;
  stream.width = width;
  stream.height = height;
  stream.fps = fps;
  stream.bandwidth = bandwidth;
  // 合成夹具只用于菜单模型断言：不打印、不联网、不是真实签名 URL。
  stream.url = QStringLiteral("https://example.invalid/dash-%1-%2.m4s")
                   .arg(qn)
                   .arg(codecid);
  stream.backup_urls = {QStringLiteral("https://example.invalid/backup.m4s")};
  stream.urls_ok = true;
  stream.init_range = {0, 900};
  stream.index_range = {901, 1200};
  stream.init_ok = true;
  switch (codecid) {
    case 7:
      stream.codecs = QStringLiteral("avc1.640028");
      break;
    case 12:
      stream.codecs = QStringLiteral("hvc1.1.6.L120.90");
      break;
    case 13:
      stream.codecs = QStringLiteral("av01.0.08M.08");
      break;
    default:
      break;
  }
  return stream;
}

lapian::online::DashCatalog MakeCatalog(
    const QVector<lapian::online::DashStream>& videos, bool with_audio) {
  lapian::online::DashCatalog catalog;
  catalog.videos = videos;
  if (with_audio) {
    lapian::online::DashAudioStream audio;
    audio.id = 30280;
    audio.bandwidth = 100000;
    audio.url = QStringLiteral("https://example.invalid/audio.m4s");
    audio.urls_ok = true;
    audio.init_range = {0, 800};
    audio.index_range = {801, 1000};
    audio.init_ok = true;
    audio.codecs = QStringLiteral("mp4a.40.2");
    catalog.audios.append(audio);
  }
  lapian::online::RebuildDashTiers(&catalog);
  return catalog;
}

const lapian::online::QualityMenuEntry* FindEntry(
    const QVector<lapian::online::QualityMenuEntry>& entries, int qn) {
  for (const lapian::online::QualityMenuEntry& entry : entries) {
    if (entry.kind == lapian::online::QualityMenuKind::kTier &&
        entry.qn == qn) {
      return &entry;
    }
  }
  return nullptr;
}

// 离线菜单模型断言：大会员档只手动、4K 只手动 + 可用档、HEVC-only 60fps 不自动、
// 空目录不伪造档位。不联网。
void RunMenuModelChecks() {
  using lapian::online::BuildQualityMenuModel;
  using lapian::online::DashCatalog;
  using lapian::online::DashStream;
  using lapian::online::QualityMenuContext;
  using lapian::online::QualityMenuModelText;

  // 1) 大会员目录：1080P30 AVC + 1080P60 HEVC-only + 4K HEVC。
  const DashCatalog vip = MakeCatalog(
      {MakeStream(80, 7, 1920, 1080, 30.0, 2200000),
       MakeStream(116, 12, 1920, 1080, 60.0, 1500000),
       MakeStream(120, 12, 3840, 2160, 60.0, 3000000)},
      true);
  Check("menu_vip_default_is_avc_1080p", vip.DefaultQn() == 80,
        QStringLiteral("default=%1").arg(vip.DefaultQn()));
  Check("menu_vip_1080p60_not_auto", vip.DefaultQn() != 116);
  QualityMenuContext ctx;
  ctx.current_qn = 80;
  ctx.current_codecid = 7;
  const auto vip_entries = BuildQualityMenuModel(vip, ctx);
  const auto* tier80 = FindEntry(vip_entries, 80);
  const auto* tier116 = FindEntry(vip_entries, 116);
  const auto* tier120 = FindEntry(vip_entries, 120);
  Check("menu_vip_tier80_checked_enabled",
        tier80 != nullptr && tier80->checked && tier80->enabled);
  Check("menu_vip_1080p60_in_menu_manual",
        tier116 != nullptr && tier116->manual_only && !tier116->enabled &&
            tier116->text.contains(QStringLiteral("仅手动")) &&
            tier116->text.contains(QStringLiteral("HEVC")));
  Check("menu_vip_4k_in_menu_manual_available",
        tier120 != nullptr && tier120->manual_only && !tier120->enabled &&
            tier120->text.contains(QStringLiteral("仅手动")) &&
            tier120->text.contains(QStringLiteral("可用档")));
  Check("menu_vip_4k_reason_t8_12",
        tier120 != nullptr &&
            tier120->disabled_reason.contains(QStringLiteral("T8.12")));
  const QString vip_text = QualityMenuModelText(vip_entries);
  Check("menu_vip_note_no_guarantee",
        vip_text.contains(QStringLiteral("菜单可选不等于保证可达")),
        OneLine(vip_text));
  PrintKV("menu_vip_model", OneLine(vip_text));

  // 2) HEVC-only 60fps：不自动选（DefaultQn=0），菜单标手动 + HEVC。
  const DashCatalog hevc_only =
      MakeCatalog({MakeStream(116, 12, 1920, 1080, 60.0, 1500000)}, true);
  Check("menu_hevc_only_60fps_not_auto", hevc_only.DefaultQn() == 0,
        QStringLiteral("default=%1").arg(hevc_only.DefaultQn()));
  const auto hevc_entries = BuildQualityMenuModel(hevc_only, ctx);
  const auto* hevc116 = FindEntry(hevc_entries, 116);
  Check("menu_hevc_only_60fps_manual",
        hevc116 != nullptr && hevc116->manual_only && !hevc116->enabled &&
            hevc116->text.contains(QStringLiteral("HEVC")) &&
            hevc116->text.contains(QStringLiteral("1080P60")) &&
            hevc116->text.contains(QStringLiteral("fps")));
  PrintKV("menu_hevc_only_model",
          OneLine(QualityMenuModelText(hevc_entries)));

  // 3) 空目录/失败：不伪造档位。
  QualityMenuContext empty_ctx;
  empty_ctx.error = QStringLiteral("取流请求超时（测试）");
  const auto empty_entries =
      BuildQualityMenuModel(lapian::online::DashCatalog{}, empty_ctx);
  Check("menu_empty_no_fake_tier",
        empty_entries.size() == 2 &&
            empty_entries.last().text.contains(QStringLiteral("档位获取失败")));

  // 4) 缓存命中：目录来自 meta，标“未联网”；无音频时明确说明。
  const DashCatalog cached =
      MakeCatalog({MakeStream(80, 7, 1920, 1080, 30.0, 2200000)}, false);
  QualityMenuContext cache_ctx;
  cache_ctx.current_qn = 80;
  cache_ctx.current_codecid = 7;
  cache_ctx.current_from_cache = true;
  const auto cache_entries = BuildQualityMenuModel(cached, cache_ctx);
  const QString cache_text = QualityMenuModelText(cache_entries);
  Check("menu_cache_header",
        cache_text.contains(QStringLiteral("缓存命中档位（未联网")));
  Check("menu_cache_no_audio_note",
        cache_text.contains(QStringLiteral("没有音频流")));
}

// ---------------------------------------------------------------- sample run

enum class SampleOutcome { kPass = 0, kFail, kBlocked };

void PrintTierStreams(const QString& tag, const lapian::online::DashCatalog& c) {
  for (const lapian::online::QualityTier& tier : c.tiers) {
    for (const lapian::online::DashStream& stream : tier.streams) {
      std::printf(
          "online_quality_stream tag=%s qn=%d codecid=%d w=%d h=%d fps=%.3f "
          "bw=%lld size=%lld init=%s index=%s backups=%d urls_ok=%d\n",
          tag.toUtf8().constData(), stream.qn, stream.codecid, stream.width,
          stream.height, stream.fps,
          static_cast<long long>(stream.bandwidth),
          static_cast<long long>(stream.size),
          stream.init_range.ToString().toUtf8().constData(),
          stream.index_range.ToString().toUtf8().constData(),
          static_cast<int>(stream.backup_urls.size()),
          stream.urls_ok ? 1 : 0);
    }
  }
  for (const lapian::online::DashAudioStream& audio : c.audios) {
    std::printf(
        "online_quality_audio tag=%s id=%d codecid=%d bw=%lld init=%s "
        "index=%s backups=%d urls_ok=%d codecs=%s\n",
        tag.toUtf8().constData(), audio.id, audio.codecid,
        static_cast<long long>(audio.bandwidth),
        audio.init_range.ToString().toUtf8().constData(),
        audio.index_range.ToString().toUtf8().constData(),
        static_cast<int>(audio.backup_urls.size()), audio.urls_ok ? 1 : 0,
        audio.codecs.toUtf8().constData());
  }
  std::fflush(stdout);
}

SampleOutcome RunSample(const OnlineQualityTestOptions& options,
                        const QString& sessdata, const QString& secret_source) {
  using namespace lapian::online;
  const QString tag = options.tag.isEmpty() ? options.bvid : options.tag;
  const int checks_before = g_checks;
  const int failures_before = g_failures;
  std::printf(
      "T8.9 online_quality sample tag=%s bvid=%s cid=%lld secret_source=%s\n",
      tag.toUtf8().constData(), options.bvid.toUtf8().constData(),
      static_cast<long long>(options.cid), secret_source.toUtf8().constData());
  std::fflush(stdout);

  WinHttpClient http;
  HttpCancelToken cancel;
  bool blocked = false;
  bool normal_user = false;

  if (!sessdata.isEmpty()) {
    const AuthResult auth = CheckNav(sessdata, &http, cancel, 4000);
    Check("nav_login", auth.is_login, auth.message);
    PrintKV("nav_api_code", Num(auth.api_code));
    PrintKV("nav_vip_status", Num(auth.vip_status));
    PrintKV("nav_elapsed_ms", Num(auth.elapsed_ms));
    PrintKV("nav_cookie_sent", Num(auth.request_cookie_sent ? 1 : 0));
    if (!auth.is_login) {
      // 凭据失效：不把匿名结果当“登录普通用户 1080P30”。
      PrintKV("sample_block", QStringLiteral("login_unavailable"));
      blocked = true;
    } else if (auth.vip_status != 0) {
      // T8.9 的 MVP 档位矩阵只认普通用户；大会员档是 T8.15 条件验收。
      PrintKV("sample_block", QStringLiteral("vip_account_not_normal_user"));
      blocked = true;
    } else {
      normal_user = true;
      PrintKV("account", QStringLiteral("normal"));
    }
    if (blocked) {
      PrintKV("online_quality_sample_result",
              QStringLiteral("tag=%1 checks=%2 failures=%3 verdict=blocked")
                  .arg(tag)
                  .arg(g_checks - checks_before)
                  .arg(g_failures - failures_before));
      return SampleOutcome::kBlocked;
    }
  } else {
    PrintKV("account", QStringLiteral("anonymous"));
  }

  // view：标题/时长/cid 对账（只用于证据与错误定位）。
  BiliResolveClient resolver(&http);
  const BiliResolveResult resolved = resolver.Resolve(options.bvid, cancel);
  Check("view_ok", resolved.error.ok(), resolved.error.message);
  qint64 duration_ms = 0;
  if (resolved.error.ok()) {
    PrintKV("title", resolved.video.title);
    PrintKV("view_requests", Num(resolved.http_requests));
    PrintKV("view_elapsed_ms", Num(resolved.elapsed_ms));
    if (!resolved.video.pages.isEmpty()) {
      const BiliPage& page = resolved.video.pages.first();
      Check("view_cid_matches", page.cid == options.cid,
            QStringLiteral("view_cid=%1").arg(page.cid));
      duration_ms = page.duration_seconds * 1000;
    }
  }

  // DASH 目录：一次 fnval=4048 请求返回账号允许的完整档位梯。
  DashFetchOptions dash_options;
  dash_options.timeout_ms = options.timeout_ms;
  dash_options.sessdata = sessdata;
  dash_options.qn = kDashRequestQn;
  dash_options.fourk = 1;
  const DashFetchResult dash =
      FetchDashCatalog(&http, options.bvid, options.cid, dash_options, cancel);
  lapian::online::ClearSecret(&dash_options.sessdata);
  PrintKV("dash_requests", Num(dash.http_requests));
  PrintKV("dash_http_status", Num(dash.http_status));
  PrintKV("dash_elapsed_ms", Num(dash.elapsed_ms));
  PrintKV("dash_cookie_sent", Num(dash.request_cookie_sent ? 1 : 0));
  Check("dash_ok", dash.error.ok(), dash.error.message);
  if (!dash.error.ok()) {
    PrintKV("online_quality_sample_result",
            QStringLiteral("tag=%1 checks=%2 failures=%3 verdict=fail")
                .arg(tag)
                .arg(g_checks - checks_before)
                .arg(g_failures - failures_before));
    return SampleOutcome::kFail;
  }
  const DashCatalog& catalog = dash.catalog;
  PrintKV("dash_summary", catalog.Summary());
  PrintKV("dash_claimed_quality", Num(catalog.claimed_quality));
  PrintKV("dash_claimed_quality_name", QualityName(catalog.claimed_quality));
  PrintKV("dash_claimed_in_tiers", Num(catalog.ClaimedQualityInTiers() ? 1 : 0));
  PrintKV("dash_claimed_format", catalog.claimed_format);
  QStringList accept;
  for (int qn : catalog.claimed_accept_quality) {
    accept.append(QString::number(qn));
  }
  PrintKV("dash_accept_quality", accept.join(QLatin1Char('/')));
  PrintKV("dash_default_qn", Num(catalog.DefaultQn()));
  PrintKV("dash_default_codecid",
          Num(catalog.DefaultStream() != nullptr
                  ? catalog.DefaultStream()->codecid
                  : 0));
  PrintTierStreams(tag, catalog);

  Check("dash_has_video", catalog.HasVideo());
  Check("dash_audio_present", catalog.HasAudio(),
        QStringLiteral("audio=%1").arg(catalog.audios.size()));
  Check("dash_tiers_sorted_desc", [&catalog]() {
    for (int i = 1; i < catalog.tiers.size(); ++i) {
      if (catalog.tiers[i - 1].qn <= catalog.tiers[i].qn) {
        return false;
      }
    }
    return true;
  }());
  if (duration_ms > 0) {
    Check("dash_duration_matches_view",
          qAbs(catalog.duration_ms - duration_ms) <= 2000,
          QStringLiteral("dash=%1 view=%2")
              .arg(catalog.duration_ms)
              .arg(duration_ms));
  }
  const QualityTier* tier1080 = catalog.FindTier(80);
  if (normal_user) {
    Check("dash_tier_1080p_present", tier1080 != nullptr);
    Check("dash_1080p_avc_1920x1080",
          tier1080 != nullptr && tier1080->has_avc() &&
              tier1080->Stream(7)->width == 1920 &&
              tier1080->Stream(7)->height == 1080,
          tier1080 != nullptr ? QualityTierLabel(*tier1080) : QString());
    Check("dash_default_is_avc_1080p",
          catalog.DefaultQn() == 80 &&
              catalog.DefaultStream() != nullptr &&
              catalog.DefaultStream()->codecid == 7);
  } else {
    // 匿名控制组：不应冒充登录档位；只记录实际能拿到什么（≤480P）。
    Check("anon_only_max_le_480p", catalog.MaxQn() <= 32,
          QStringLiteral("max_qn=%1").arg(catalog.MaxQn()));
    Check("anon_only_default_le_480p", catalog.DefaultQn() <= 32,
          QStringLiteral("default=%1").arg(catalog.DefaultQn()));
    Check("anon_only_no_1080p", tier1080 == nullptr);
  }
  Check("dash_all_urls_https",
        std::all_of(catalog.videos.begin(), catalog.videos.end(),
                    [](const DashStream& stream) { return stream.urls_ok; }));
  Check("dash_backups_present",
        std::all_of(catalog.videos.begin(), catalog.videos.end(),
                    [](const DashStream& stream) {
                      return stream.backup_urls.size() >= 1;
                    }));
  Check("dash_init_index_valid",
        std::all_of(catalog.videos.begin(), catalog.videos.end(),
                    [](const DashStream& stream) { return stream.init_ok; }) &&
            std::all_of(catalog.audios.begin(), catalog.audios.end(),
                        [](const DashAudioStream& audio) {
                          return audio.init_ok;
                        }));
  Check("dash_size_unknown_not_fabricated",
        std::all_of(catalog.videos.begin(), catalog.videos.end(),
                    [](const DashStream& stream) {
                      return stream.size == 0;
                    }) &&
            std::all_of(catalog.audios.begin(), catalog.audios.end(),
                        [](const DashAudioStream& stream) {
                          return stream.size == 0;
                        }));
  if (normal_user) {
    Check("dash_tiers_le_1080p", catalog.MaxQn() <= 80,
          QStringLiteral("max_qn=%1").arg(catalog.MaxQn()));
    Check("dash_1080p60_not_available",
          catalog.FindTier(116) == nullptr && catalog.FindTier(74) == nullptr);
    Check("dash_4k_not_available", catalog.FindTier(120) == nullptr);
  }

  // 菜单模型（真实目录）：T8.10 已能播放 DASH，但切档仍属 T8.12，档位置灰并写明。
  QualityMenuContext menu_ctx;
  // 当前播放是 durl 档（T8.9 仍走 durl），不是 DASH 默认档。
  PlayUrlFetchOptions durl_options;
  durl_options.timeout_ms = options.timeout_ms;
  durl_options.sessdata = sessdata;
  const PlayUrlFetchResult durl = FetchPlayUrl(
      &http, options.bvid, options.cid, kM1RequestQn, durl_options, cancel);
  lapian::online::ClearSecret(&durl_options.sessdata);
  Check("durl_ok", durl.error.ok(), durl.error.message);
  if (durl.error.ok()) {
    PrintKV("durl_quality", Num(durl.data.quality));
    PrintKV("durl_quality_name", QualityName(durl.data.quality));
    PrintKV("durl_codecid", Num(durl.data.codecid));
    PrintKV("durl_format", durl.data.format);
    PrintKV("durl_size", Num(durl.data.TotalBytes()));
    PrintKV("durl_requests", Num(durl.http_requests));
    PrintKV("durl_elapsed_ms", Num(durl.elapsed_ms));
    menu_ctx.current_qn = durl.data.quality;
    menu_ctx.current_codecid = durl.data.codecid;
  }
  const QVector<QualityMenuEntry> entries =
      BuildQualityMenuModel(catalog, menu_ctx);
  const QString menu_text = QualityMenuModelText(entries);
  PrintKV("menu_real_model", OneLine(menu_text));
  if (normal_user) {
    Check("menu_real_lists_1080p", FindEntry(entries, 80) != nullptr);
    Check("menu_real_no_4k", FindEntry(entries, 120) == nullptr);
    Check("menu_real_no_60fps",
          FindEntry(entries, 116) == nullptr &&
              FindEntry(entries, 74) == nullptr);
  } else {
    Check("menu_anon_no_1080p", FindEntry(entries, 80) == nullptr);
  }
  Check("menu_real_t8_12_note",
        menu_text.contains(QStringLiteral("T8.12")), OneLine(menu_text));
  if (menu_ctx.current_qn > 0) {
    const QualityMenuEntry* current = FindEntry(entries, menu_ctx.current_qn);
    // 当前 durl 档不在 DASH 目录时（匿名 720P vs DASH 480P），模型必须显式说明，
    // 而不是伪造一个勾选项。
    const bool explained_absent =
        current == nullptr &&
        menu_text.contains(QStringLiteral("不在 DASH 目录里"));
    Check("menu_real_current_checked",
          (current != nullptr && current->checked && current->enabled) ||
              explained_absent,
          QStringLiteral("current_qn=%1").arg(menu_ctx.current_qn));
  }

  // 匿名对照：无凭据时 DASH 只有 480P/360P；quality 字段可能撒谎。
  if (!sessdata.isEmpty()) {
    DashFetchOptions anon_options;
    anon_options.timeout_ms = options.timeout_ms;
    anon_options.qn = kDashRequestQn;
    anon_options.fourk = 1;
    const DashFetchResult anon =
        FetchDashCatalog(&http, options.bvid, options.cid, anon_options, cancel);
    Check("anon_dash_ok", anon.error.ok(), anon.error.message);
    if (anon.error.ok()) {
      PrintKV("anon_max_qn", Num(anon.catalog.MaxQn()));
      PrintKV("anon_default_qn", Num(anon.catalog.DefaultQn()));
      PrintKV("anon_claimed_quality", Num(anon.catalog.claimed_quality));
      PrintKV("anon_claimed_in_tiers",
              Num(anon.catalog.ClaimedQualityInTiers() ? 1 : 0));
      Check("anon_max_le_480p", anon.catalog.MaxQn() <= 32,
            QStringLiteral("max_qn=%1").arg(anon.catalog.MaxQn()));
      Check("anon_default_le_480p", anon.catalog.DefaultQn() <= 32,
            QStringLiteral("default=%1").arg(anon.catalog.DefaultQn()));
    }
  }

  // 错误 qn：116/0/9999 不崩、仍能解析；实际档位只信 dash.video[]。
  if (options.odd_qn) {
    for (int qn : {116, 0, 9999}) {
      DashFetchOptions odd_options;
      odd_options.timeout_ms = options.timeout_ms;
      odd_options.sessdata = sessdata;
      odd_options.qn = qn;
      odd_options.fourk = 1;
      const DashFetchResult odd = FetchDashCatalog(
          &http, options.bvid, options.cid, odd_options, cancel);
      lapian::online::ClearSecret(&odd_options.sessdata);
      Check(QStringLiteral("odd_qn_%1_ok").arg(qn), odd.error.ok(),
            odd.error.message);
      if (odd.error.ok()) {
        PrintKV(QStringLiteral("odd_qn_%1_claimed").arg(qn),
                Num(odd.catalog.claimed_quality));
        PrintKV(QStringLiteral("odd_qn_%1_default").arg(qn),
                Num(odd.catalog.DefaultQn()));
        Check(QStringLiteral("odd_qn_%1_has_video").arg(qn),
              odd.catalog.HasVideo());
      }
    }
  }

  const int sample_checks = g_checks - checks_before;
  const int sample_failures = g_failures - failures_before;
  PrintKV("online_quality_sample_result",
          QStringLiteral("tag=%1 checks=%2 failures=%3 verdict=%4")
              .arg(tag)
              .arg(sample_checks)
              .arg(sample_failures)
              .arg(sample_failures == 0 ? QStringLiteral("pass")
                                        : QStringLiteral("fail")));
  return sample_failures == 0 ? SampleOutcome::kPass : SampleOutcome::kFail;
}

}  // namespace

int RunOnlineQualityTest(const OnlineQualityTestOptions& options) {
  std::printf(
      "T8.9 online_quality begin bvid=%s cid=%lld tag=%s anonymous=%d "
      "odd_qn=%d timeout_ms=%d\n",
      options.bvid.toUtf8().constData(), static_cast<long long>(options.cid),
      options.tag.toUtf8().constData(), options.anonymous ? 1 : 0,
      options.odd_qn ? 1 : 0, options.timeout_ms);
  std::fflush(stdout);

  if (options.bvid.isEmpty() || options.cid <= 0) {
    PrintKV("error", QStringLiteral("missing_bvid_cid"));
    PrintKV("verdict", QStringLiteral("blocked"));
    return 2;
  }

  SecretFile secret;
  if (options.anonymous) {
    secret.ok = true;
    secret.source = QStringLiteral("none");
  } else {
    secret = ReadSecret(options.sessdata_file);
  }
  if (!secret.ok) {
    PrintKV("error", secret.error);
    PrintKV("verdict", QStringLiteral("blocked"));
    return 2;
  }
  PrintKV("secret_source", secret.source);

  // 先把不依赖网络的菜单模型/合成大会员档断言跑完（可离线复跑）。
  RunMenuModelChecks();

  const SampleOutcome outcome = RunSample(
      options, options.anonymous ? QString() : secret.sessdata, secret.source);
  lapian::online::ClearSecret(&secret.sessdata);

  PrintKV("checks", Num(g_checks));
  PrintKV("failures", Num(g_failures));
  QString verdict;
  int code = 0;
  if (g_failures > 0 || outcome == SampleOutcome::kFail) {
    verdict = QStringLiteral("fail");
    code = 1;
  } else if (outcome == SampleOutcome::kBlocked) {
    verdict = QStringLiteral("blocked");
    code = 2;
  } else {
    verdict = QStringLiteral("pass");
  }
  PrintKV("verdict", verdict);
  PrintKV("m2_1080p30_sample",
          outcome == SampleOutcome::kPass && !options.anonymous
              ? QStringLiteral("pass_for_sample")
              : QStringLiteral("not_evaluated"));
  std::printf("T8.9 online_quality end verdict=%s checks=%d failures=%d\n",
              verdict.toUtf8().constData(), g_checks, g_failures);
  std::fflush(stdout);
  return code;
}

// ---------------------------------------------------------------- UI (`--ui`)

OnlineQualityUiTestRunner::OnlineQualityUiTestRunner(
    MainWindow* window, OnlineQualityTestOptions options)
    : window_(window), options_(std::move(options)) {}

void OnlineQualityUiTestRunner::Start() {
  QTimer::singleShot(0, window_, [this]() { Run(); });
}

void OnlineQualityUiTestRunner::Run() {
  auto check = [this](const QString& name, bool ok,
                      const QString& detail = {}) {
    ++checks_;
    if (!ok) {
      ++failures_;
    }
    const QString line =
        QStringLiteral("online_quality_ui_check %1 ok=%2%3")
            .arg(name)
            .arg(ok ? 1 : 0)
            .arg(detail.isEmpty() ? QString()
                                  : QStringLiteral(" detail=") + OneLine(detail));
    std::printf("%s\n", line.toUtf8().constData());
    std::fflush(stdout);
  };
  auto finish = [this]() {
    std::printf("online_quality_ui_result checks=%d failures=%d verdict=%s\n",
                checks_, failures_, failures_ == 0 ? "pass" : "fail");
    std::fflush(stdout);
    QCoreApplication::exit(failures_ == 0 ? 0 : 1);
  };

  const QString tag = options_.tag.isEmpty() ? options_.bvid : options_.tag;
  std::printf("T8.9 online_quality ui begin tag=%s bvid=%s cid=%lld\n",
              tag.toUtf8().constData(), options_.bvid.toUtf8().constData(),
              static_cast<long long>(options_.cid));
  std::fflush(stdout);

  SecretFile secret = ReadSecret(options_.sessdata_file);
  if (!secret.ok) {
    check(QStringLiteral("ui_secret"), false, secret.error);
    lapian::online::ClearSecret(&secret.sessdata);
    finish();
    return;
  }

  lapian::online::WinHttpClient http;
  lapian::online::HttpCancelToken cancel;
  const lapian::online::AuthResult auth =
      lapian::online::CheckNav(secret.sessdata, &http, cancel, 4000);
  check(QStringLiteral("ui_login"), auth.is_login, auth.message);
  check(QStringLiteral("ui_normal_user"), auth.vip_status == 0,
        QStringLiteral("vip_status=%1").arg(auth.vip_status));
  if (!auth.is_login || auth.vip_status != 0) {
    lapian::online::ClearSecret(&secret.sessdata);
    finish();
    return;
  }

  lapian::online::BiliResolveClient resolver(&http);
  const lapian::online::BiliResolveResult resolved =
      resolver.Resolve(options_.bvid, cancel);
  check(QStringLiteral("ui_view"), resolved.error.ok(), resolved.error.message);
  if (!resolved.error.ok()) {
    lapian::online::ClearSecret(&secret.sessdata);
    finish();
    return;
  }

  window_->raise();
  window_->activateWindow();
  window_->FocusVideoForTest();

  lapian::app::OnlineRequest request;
  request.bvid = options_.bvid;
  request.cid = options_.cid;
  request.page = 1;
  request.title = resolved.video.title;
  request.duration_ms =
      resolved.video.pages.isEmpty()
          ? 0
          : resolved.video.pages.first().duration_seconds * 1000;
  request.sessdata = secret.sessdata;
  lapian::online::ClearSecret(&secret.sessdata);

  const bool started = window_->StartOnlineSession(request);
  check(QStringLiteral("ui_session_started"), started,
        window_->OnlineLastErrorForTest());
  const bool ready = window_->WaitForOnlineReadyForTest(options_.timeout_ms);
  check(QStringLiteral("ui_ready"), ready, window_->OnlineLastErrorForTest());
  const bool quality = window_->WaitForOnlineQualityForTest(options_.timeout_ms);
  check(QStringLiteral("ui_quality_received"), quality,
        window_->OnlineQualityErrorForTest());
  if (!ready || !quality) {
    window_->CancelOnlineLoadingForTest();
    finish();
    return;
  }

  const int current_qn = window_->OnlineQualityForTest();
  Print(QStringLiteral("online_quality_ui_state current_qn=%1 quality=%2")
            .arg(current_qn)
            .arg(window_->OnlineQualitySummaryForTest()));
  check(QStringLiteral("ui_dash_ok"),
        window_->OnlineQualitySummaryForTest().contains(QStringLiteral("ok=1")),
        window_->OnlineQualitySummaryForTest());
  check(QStringLiteral("ui_normal_user_1080p_available"),
        window_->OnlineQualitySummaryForTest().contains(
            QStringLiteral("80(1920x1080")),
        window_->OnlineQualitySummaryForTest());

  const QString menu = window_->OnlineQualityMenuTextForTest();
  Print(QStringLiteral("online_quality_ui_menu %1").arg(OneLine(menu)));
  check(QStringLiteral("ui_button_visible"),
        window_->OnlineQualityButtonVisibleForTest());
  check(QStringLiteral("ui_menu_lists_1080p"),
        menu.contains(QStringLiteral("qn=80")));
  check(QStringLiteral("ui_menu_no_4k"),
        !menu.contains(QStringLiteral("qn=120")));
  check(QStringLiteral("ui_menu_current_checked"),
        menu.contains(QStringLiteral("qn=%1").arg(current_qn)) &&
            menu.contains(QStringLiteral("checked=1")));
  check(QStringLiteral("ui_menu_actions"),
        !window_->OnlineQualityMenuActionsForTest().isEmpty());

  QString status;
  const bool current_triggered =
      window_->TriggerOnlineQualityEntryForTest(current_qn, &status);
  check(QStringLiteral("ui_trigger_current"), current_triggered, status);
  check(QStringLiteral("ui_current_status"),
        status.contains(QStringLiteral("已经是当前播放档位")), status);

  int other_qn = 0;
  for (int qn : {16, 32, 64, 112}) {
    if (qn != current_qn && menu.contains(QStringLiteral("qn=%1").arg(qn))) {
      other_qn = qn;
      break;
    }
  }
  if (other_qn > 0) {
    QString other_status;
    const bool other_triggered =
        window_->TriggerOnlineQualityEntryForTest(other_qn, &other_status);
    check(QStringLiteral("ui_other_tier_disabled"), !other_triggered,
          other_status);
    check(QStringLiteral("ui_other_tier_reason"),
          other_status.contains(QStringLiteral("T8.12")), other_status);
  }
  check(QStringLiteral("ui_quality_requests_one"),
        window_->OnlineQualityRequestsForTest() == 1,
        QStringLiteral("requests=%1")
            .arg(window_->OnlineQualityRequestsForTest()));
  Print(QStringLiteral("online_quality_ui_requests durl=%1 quality=%2")
            .arg(window_->OnlinePlayUrlRequestsForTest())
            .arg(window_->OnlineQualityRequestsForTest()));

  // 结束真实会话（保留 .part；测试根由 --online-cache-root 指定）。
  window_->CancelOnlineLoadingForTest();

  // 合成大会员目录：证明 1080P60/4K 进菜单、只手动、置灰（不假装可达）。
  lapian::app::OnlineQualityCatalog vip;
  vip.ok = true;
  vip.fetched = true;
  vip.catalog = MakeCatalog(
      {MakeStream(80, 7, 1920, 1080, 30.0, 2200000),
       MakeStream(116, 12, 1920, 1080, 60.0, 1500000),
       MakeStream(120, 12, 3840, 2160, 60.0, 3000000)},
      true);
  window_->SetOnlineQualityForTest(vip);
  const QStringList actions = window_->OnlineQualityMenuActionsForTest();
  const QString action_text = actions.join(QStringLiteral(" || "));
  Print(QStringLiteral("online_quality_ui_actions %1").arg(OneLine(action_text)));
  check(QStringLiteral("ui_vip_1080p60_menu"),
        action_text.contains(QStringLiteral("1080P60")) &&
            action_text.contains(QStringLiteral("仅手动")) &&
            action_text.contains(QStringLiteral("enabled=0")));
  check(QStringLiteral("ui_vip_4k_menu_manual"),
        action_text.contains(QStringLiteral("4K")) &&
            action_text.contains(QStringLiteral("可用档")) &&
            action_text.contains(QStringLiteral("enabled=0")));
  check(QStringLiteral("ui_vip_menu_no_guarantee"),
        action_text.contains(QStringLiteral("菜单可选不等于保证可达")));
  finish();
}

}  // namespace lapian::app
