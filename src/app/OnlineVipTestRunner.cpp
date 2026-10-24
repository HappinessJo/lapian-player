#include "app/OnlineVipTestRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "app/MainWindow.h"
#include "online/BiliAuth.h"
#include "online/BiliResolveClient.h"
#include "online/CredentialStore.h"
#include "online/FrameRatePolicy.h"
#include "online/PlayUrl.h"
#include "online/QualityMenu.h"
#include "online/Sessdata.h"
#include "online/WinHttpClient.h"

namespace lapian::app {
namespace {

using lapian::online::AuthResult;
using lapian::online::BiliResolveClient;
using lapian::online::BiliResolveResult;
using lapian::online::DashCatalog;
using lapian::online::DashFetchResult;
using lapian::online::DashStream;
using lapian::online::QualityMenuContext;
using lapian::online::QualityMenuEntry;
using lapian::online::QualityTier;

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
      QStringLiteral("vip_check %1 ok=%2%3")
          .arg(name)
          .arg(ok ? 1 : 0)
          .arg(detail.isEmpty() ? QString()
                                : QStringLiteral(" detail=") + OneLine(detail));
  std::printf("%s\n", line.toUtf8().constData());
  std::fflush(stdout);
}

void PrintItem(const QString& id, const QString& value,
               const QString& detail = QString()) {
  QString line =
      QStringLiteral("vip_item %1=%2").arg(id).arg(value);
  if (!detail.isEmpty()) {
    line += QStringLiteral(" detail=") + OneLine(detail);
  }
  std::printf("%s\n", line.toUtf8().constData());
  std::fflush(stdout);
}

double Percentile(std::vector<qint64> values, double percentile) {
  if (values.empty()) {
    return -1.0;
  }
  std::sort(values.begin(), values.end());
  const double rank = percentile * (values.size() - 1);
  const std::size_t low = static_cast<std::size_t>(std::floor(rank));
  const std::size_t high = static_cast<std::size_t>(std::ceil(rank));
  if (low == high) {
    return static_cast<double>(values[low]);
  }
  return static_cast<double>(values[low]) +
         (static_cast<double>(values[high]) - static_cast<double>(values[low])) *
             (rank - static_cast<double>(low));
}

// ---------------------------------------------------------------- credentials

struct VipCredential {
  bool ok = false;
  QString source;
  QString sessdata;
  QString error;
};

VipCredential LoadVipCredential(const QString& explicit_file) {
  VipCredential result;
  if (!explicit_file.isEmpty()) {
    const QFileInfo info(explicit_file);
    if (!info.exists() || !info.isFile()) {
      result.error = QStringLiteral("missing_sessdata_file");
      return result;
    }
    if (info.size() <= 0 || info.size() > 8 * 1024) {
      result.error = QStringLiteral("invalid_sessdata_file_size");
      return result;
    }
    QFile file(explicit_file);
    if (!file.open(QIODevice::ReadOnly)) {
      result.error = QStringLiteral("unreadable_sessdata_file");
      return result;
    }
    const auto normalized = lapian::online::NormalizeSessdata(
        QString::fromUtf8(file.readAll()));
    file.close();
    if (!normalized.ok) {
      result.error = QStringLiteral("invalid_sessdata");
      return result;
    }
    result.ok = true;
    result.source = QStringLiteral("explicit_file");
    result.sessdata = normalized.value;
    return result;
  }
  // 默认 DPAPI 凭据（%APPDATA%\拉片软件\credentials.dat）。只读；不保存、不打印。
  lapian::online::CredentialStore store;
  const lapian::online::CredentialLoadResult loaded = store.Load();
  if (!loaded.ok) {
    result.error = loaded.missing ? QStringLiteral("missing_default_credential")
                                  : (loaded.corrupt
                                         ? QStringLiteral("corrupt_default_credential")
                                         : QStringLiteral("unreadable_default_credential"));
    return result;
  }
  result.ok = true;
  result.source = QStringLiteral("dpapi_default");
  result.sessdata = loaded.sessdata;
  return result;
}

// ---------------------------------------------------------------- catalog probe

struct VipProbe {
  bool blocked = false;
  QString block_reason;
  bool login = false;
  int vip_status = -1;
  qint64 nav_ms = 0;
  bool view_ok = false;
  QString title;
  QString owner;
  qint64 cid = 0;
  qint64 duration_ms = 0;
  int pages = 0;
  bool cid_in_view = false;
  DashFetchResult dash80;   // 产品自动路径的请求（qn=80）
  DashFetchResult dash120;  // 最大请求（qn=120），只用于列实际档位
};

const QualityTier* FindTier(const DashCatalog& catalog, int qn) {
  return catalog.FindTier(qn);
}

QString Presence(const DashCatalog& catalog, int qn) {
  const QualityTier* tier = catalog.FindTier(qn);
  if (tier == nullptr) {
    return QStringLiteral("-");
  }
  QStringList codecs;
  for (const DashStream& stream : tier->streams) {
    codecs.append(lapian::online::CodecName(stream.codecid));
  }
  return QStringLiteral("%1x%2@%3fps[%4]")
      .arg(tier->width)
      .arg(tier->height)
      .arg(QString::number(tier->fps, 'f', 3))
      .arg(codecs.join(QLatin1Char('/')));
}

QString DefaultText(const DashCatalog& catalog) {
  const DashStream* stream = catalog.DefaultStream();
  if (stream == nullptr) {
    return QStringLiteral("qn=0");
  }
  const QualityTier* tier = catalog.FindTier(stream->qn);
  return QStringLiteral("qn=%1 codecid=%2 codec=%3 fps=%4 manual=%5")
      .arg(stream->qn)
      .arg(stream->codecid)
      .arg(lapian::online::CodecName(stream->codecid))
      .arg(QString::number(stream->fps, 'f', 3))
      .arg(tier != nullptr && lapian::online::IsManualOnlyTier(*tier) ? 1 : 0);
}

void PrintCatalog(const QString& phase, const DashFetchResult& result) {
  PrintKV(QStringLiteral("vip_catalog_%1").arg(phase),
          QStringLiteral("ok=%1 http=%2 requests=%3 elapsed_ms=%4 videos=%5 "
                         "audios=%6 duration_ms=%7 claimed_quality=%8 "
                         "claimed_in_tiers=%9 error=%10")
              .arg(result.error.ok() ? 1 : 0)
              .arg(result.http_status)
              .arg(result.http_requests)
              .arg(result.elapsed_ms)
              .arg(result.catalog.videos.size())
              .arg(result.catalog.audios.size())
              .arg(result.catalog.duration_ms)
              .arg(result.catalog.claimed_quality)
              .arg(result.catalog.ClaimedQualityInTiers() ? 1 : 0)
              .arg(result.error.ok()
                       ? QStringLiteral("-")
                       : OneLine(result.error.message)));
  if (!result.error.ok()) {
    return;
  }
  for (const DashStream& stream : result.catalog.videos) {
    const QualityTier* tier = result.catalog.FindTier(stream.qn);
    std::printf(
        "vip_stream phase=%s qn=%d codecid=%d codec=%s fps=%.3f w=%d h=%d "
        "bw=%lld manual=%d codecs=%s\n",
        phase.toUtf8().constData(), stream.qn, stream.codecid,
        lapian::online::CodecName(stream.codecid).toUtf8().constData(),
        stream.fps, stream.width, stream.height,
        static_cast<long long>(stream.bandwidth),
        tier != nullptr && lapian::online::IsManualOnlyTier(*tier) ? 1 : 0,
        stream.codecs.toUtf8().constData());
  }
  for (const lapian::online::DashAudioStream& audio : result.catalog.audios) {
    std::printf(
        "vip_audio phase=%s id=%d codecs=%s bw=%lld init_ok=%d urls_ok=%d\n",
        phase.toUtf8().constData(), audio.id, audio.codecs.toUtf8().constData(),
        static_cast<long long>(audio.bandwidth), audio.init_ok ? 1 : 0,
        audio.urls_ok ? 1 : 0);
  }
  PrintKV(QStringLiteral("vip_default_%1").arg(phase),
          DefaultText(result.catalog));
  PrintKV(QStringLiteral("vip_presence_%1").arg(phase),
          QStringLiteral("116=%1 74=%2 120=%3")
              .arg(Presence(result.catalog, 116),
                   Presence(result.catalog, 74),
                   Presence(result.catalog, 120)));
  std::fflush(stdout);
}

QString CatalogMenuText(const DashCatalog& catalog, const QString& title) {
  QualityMenuContext context;
  context.logged_in = true;
  context.dash_playback_available = true;
  const DashStream* stream = catalog.DefaultStream();
  context.actual_fps = stream != nullptr ? stream->fps : 0.0;
  context.source_fps_hint = lapian::online::InferSourceFpsHintFromText(title);
  return lapian::online::QualityMenuModelText(
      lapian::online::BuildQualityMenuModel(catalog, context));
}

const QualityMenuEntry* FindEntry(const QVector<QualityMenuEntry>& entries,
                                  int qn) {
  for (const QualityMenuEntry& entry : entries) {
    if (entry.kind == lapian::online::QualityMenuKind::kTier &&
        entry.qn == qn) {
      return &entry;
    }
  }
  return nullptr;
}

VipProbe RunVipProbe(const OnlineVipTestOptions& options,
                     const QString& sessdata, lapian::online::WinHttpClient* http,
                     const lapian::online::HttpCancelToken& cancel) {
  VipProbe probe;
  const QString tag = options.tag.isEmpty() ? options.bvid : options.tag;
  std::printf("T8.15 vip_probe begin tag=%s bvid=%s cid_hint=%lld\n",
              tag.toUtf8().constData(), options.bvid.toUtf8().constData(),
              static_cast<long long>(options.cid));
  std::fflush(stdout);

  const AuthResult auth =
      lapian::online::CheckNav(sessdata, http, cancel, 4000);
  probe.login = auth.is_login;
  probe.vip_status = auth.vip_status;
  probe.nav_ms = auth.elapsed_ms;
  PrintKV("vip_nav",
          QStringLiteral("login=%1 vip_status=%2 api_code=%3 http=%4 "
                         "elapsed_ms=%5 cookie_sent=%6 message=%7")
              .arg(auth.is_login ? 1 : 0)
              .arg(auth.vip_status)
              .arg(auth.api_code)
              .arg(auth.http_status)
              .arg(auth.elapsed_ms)
              .arg(auth.request_cookie_sent ? 1 : 0)
              .arg(auth.message.isEmpty() ? QStringLiteral("-")
                                          : OneLine(auth.message)));
  if (!auth.is_login) {
    probe.blocked = true;
    probe.block_reason = QStringLiteral("login_unavailable");
    return probe;
  }
  if (auth.vip_status != 1) {
    probe.blocked = true;
    probe.block_reason =
        QStringLiteral("not_vip vip_status=%1").arg(auth.vip_status);
    return probe;
  }

  BiliResolveClient resolver(http);
  const BiliResolveResult view = resolver.Resolve(options.bvid, cancel);
  probe.view_ok = view.error.ok();
  PrintKV("vip_view",
          QStringLiteral("ok=%1 requests=%2 elapsed_ms=%3 title=%4 owner=%5 "
                         "duration_s=%6 pages=%7 error=%8")
              .arg(view.error.ok() ? 1 : 0)
              .arg(view.http_requests)
              .arg(view.elapsed_ms)
              .arg(view.video.title.isEmpty() ? QStringLiteral("-")
                                              : OneLine(view.video.title))
              .arg(view.video.owner.isEmpty() ? QStringLiteral("-")
                                              : OneLine(view.video.owner))
              .arg(view.video.duration_seconds)
              .arg(view.video.pages.size())
              .arg(view.error.ok() ? QStringLiteral("-")
                                   : OneLine(view.error.message)));
  if (!probe.view_ok) {
    probe.blocked = true;
    probe.block_reason = QStringLiteral("view_failed");
    return probe;
  }
  probe.title = view.video.title;
  probe.owner = view.video.owner;
  probe.pages = view.video.pages.size();
  probe.duration_ms = view.video.duration_seconds * 1000;
  probe.cid = options.cid > 0
                  ? options.cid
                  : (view.video.pages.isEmpty() ? 0
                                                : view.video.pages.first().cid);
  for (const lapian::online::BiliPage& page : view.video.pages) {
    if (page.cid == probe.cid) {
      probe.cid_in_view = true;
      probe.duration_ms = page.duration_seconds * 1000;
      break;
    }
  }
  PrintKV("vip_cid",
          QStringLiteral("cid=%1 in_view=%2 pages=%3 duration_ms=%4")
              .arg(probe.cid)
              .arg(probe.cid_in_view ? 1 : 0)
              .arg(probe.pages)
              .arg(probe.duration_ms));
  if (probe.cid <= 0) {
    probe.blocked = true;
    probe.block_reason = QStringLiteral("missing_cid");
    return probe;
  }

  // phase auto：产品 requested_qn=0 时实际发出的请求（qn=80、fourk=1）。
  {
    lapian::online::DashFetchOptions dash_options;
    dash_options.timeout_ms = options.timeout_ms;
    dash_options.sessdata = sessdata;
    dash_options.qn = lapian::online::kDashRequestQn;
    dash_options.fourk = 1;
    probe.dash80 = lapian::online::FetchDashCatalog(
        http, options.bvid, probe.cid, dash_options, cancel);
    lapian::online::ClearSecret(&dash_options.sessdata);
    PrintCatalog(QStringLiteral("auto80"), probe.dash80);
  }
  // phase max：显式请求最高档，用来判断账号在这条视频上实际能拿到 116/74/120。
  {
    lapian::online::DashFetchOptions dash_options;
    dash_options.timeout_ms = options.timeout_ms;
    dash_options.sessdata = sessdata;
    dash_options.qn = 120;
    dash_options.fourk = 1;
    probe.dash120 = lapian::online::FetchDashCatalog(
        http, options.bvid, probe.cid, dash_options, cancel);
    lapian::online::ClearSecret(&dash_options.sessdata);
    PrintCatalog(QStringLiteral("max120"), probe.dash120);
  }
  return probe;
}

const DashCatalog& PreferredCatalog(const VipProbe& probe) {
  if (probe.dash120.error.ok() && probe.dash120.catalog.HasVideo()) {
    return probe.dash120.catalog;
  }
  return probe.dash80.catalog;
}

bool CatalogAvailable(const VipProbe& probe) {
  return probe.view_ok && probe.cid > 0 &&
         ((probe.dash80.error.ok() && probe.dash80.catalog.HasVideo()) ||
          (probe.dash120.error.ok() && probe.dash120.catalog.HasVideo()));
}

using CheckReporter =
    std::function<void(const QString&, bool, const QString&)>;

// 目录/菜单模型检查（无 GUI 与 GUI 共用）。返回 60fps 档是否 AVC。
void EmitCatalogChecks(const OnlineVipTestOptions& options,
                       const VipProbe& probe, const CheckReporter& emit) {
  const DashCatalog& full = PreferredCatalog(probe);
  const QualityTier* tier116 = full.FindTier(116);
  const QualityTier* tier74 = full.FindTier(74);
  const QualityTier* tier120 = full.FindTier(120);
  const bool has60 = tier116 != nullptr || tier74 != nullptr;
  const bool has4k = tier120 != nullptr;
  const bool has60_avc =
      (tier116 != nullptr && tier116->has_avc()) ||
      (tier74 != nullptr && tier74->has_avc());
  const bool has60_hevc_av1 =
      has60 && !has60_avc &&
      ((tier116 != nullptr && (tier116->has_hevc() || tier116->has_av1())) ||
       (tier74 != nullptr && (tier74->has_hevc() || tier74->has_av1())));
  PrintKV("vip_availability",
          QStringLiteral("has_60=%1 has_60_avc=%2 has_60_hevc_av1=%3 has_4k=%4 "
                         "auto80_has_60=%5 auto80_has_4k=%6")
              .arg(has60 ? 1 : 0)
              .arg(has60_avc ? 1 : 0)
              .arg(has60_hevc_av1 ? 1 : 0)
              .arg(has4k ? 1 : 0)
              .arg(probe.dash80.catalog.FindTier(116) != nullptr ||
                           probe.dash80.catalog.FindTier(74) != nullptr
                       ? 1
                       : 0)
              .arg(probe.dash80.catalog.FindTier(120) != nullptr ? 1 : 0));

  const QString menu = CatalogMenuText(full, probe.title);
  PrintKV("vip_menu_model", OneLine(menu));
  const QVector<QualityMenuEntry> entries =
      lapian::online::BuildQualityMenuModel(full, [] {
        QualityMenuContext context;
        context.logged_in = true;
        context.dash_playback_available = true;
        return context;
      }());
  const QualityMenuEntry* entry116 = FindEntry(entries, 116);
  const QualityMenuEntry* entry74 = FindEntry(entries, 74);
  const QualityMenuEntry* entry120 = FindEntry(entries, 120);
  if (has60) {
    const QualityMenuEntry* entry =
        entry116 != nullptr ? entry116 : entry74;
    emit(QStringLiteral("menu_lists_60fps"), entry != nullptr,
         entry == nullptr ? QStringLiteral("-") : entry->text);
    if (entry != nullptr) {
      emit(QStringLiteral("menu_60fps_marks_rate"),
           entry->text.contains(QStringLiteral("60fps")), entry->text);
      if (has60_avc) {
        emit(QStringLiteral("menu_avc60_auto"),
             entry->text.contains(QStringLiteral("AVC")) &&
                 entry->text.contains(QStringLiteral("自动优先")),
             entry->text);
      } else {
        emit(QStringLiteral("menu_hevc_av1_60_manual"),
             entry->manual_only &&
                 !entry->text.contains(QStringLiteral("自动优先")) &&
                 entry->text.contains(QStringLiteral("手动")),
             entry->text);
      }
    }
  } else {
    PrintKV("vip_menu_60", QStringLiteral("absent"));
  }
  if (has4k) {
    emit(QStringLiteral("menu_lists_4k"), entry120 != nullptr,
         entry120 == nullptr ? QStringLiteral("-") : entry120->text);
    if (entry120 != nullptr) {
      emit(QStringLiteral("menu_4k_manual_available"),
           entry120->manual_only &&
               entry120->text.contains(QStringLiteral("仅手动")) &&
               entry120->text.contains(QStringLiteral("可用档")),
           entry120->text);
    }
  } else {
    PrintKV("vip_menu_4k", QStringLiteral("absent"));
  }
  emit(QStringLiteral("menu_notes_no_guarantee"),
       menu.contains(QStringLiteral("菜单可选不等于保证可达")), OneLine(menu));
  PrintKV("vip_codec_mode",
          has60 ? (has60_avc ? QStringLiteral("avc60")
                             : (has60_hevc_av1 ? QStringLiteral("hevc_av1_60")
                                               : QStringLiteral("unknown_60")))
                : QStringLiteral("no_60"));
  (void)options;
}

// ---------------------------------------------------------------- step audit

struct StepAudit {
  int n = 0;
  int failures = 0;
  int consecutive_failures = 0;
  std::vector<qint64> samples;
  qint64 stable_frames = 0;
  qint64 start_frame = 0;
  qint64 boundary_margin_frames = 0;
  double fps = 0.0;
  bool hardware = false;
};

bool WaitUntil(const std::function<bool()>& predicate, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return predicate();
}

StepAudit RunStepAudit(MainWindow* window, int seeks, double fps,
                       int start_frame, int per_step_timeout_ms) {
  StepAudit audit;
  audit.start_frame = start_frame;
  audit.fps = fps;
  audit.hardware = window->HardwareDecodeForTest();
  const double frames_per_second = fps > 0.5 ? fps : 30.0;
  const qint64 need_frames =
      start_frame + static_cast<qint64>(std::ceil(2.0 * frames_per_second)) + 10;
  const bool cached_prefix = WaitUntil(
      [window, need_frames]() {
        return window->OnlineIndexCompleteForTest() ||
               window->OnlineStableFramesForTest() >= need_frames;
      },
      90000);
  PrintKV("vip_step_cache_wait",
          QStringLiteral("ok=%1 stable=%2 need=%3 complete=%4")
              .arg(cached_prefix ? 1 : 0)
              .arg(window->OnlineStableFramesForTest())
              .arg(need_frames)
              .arg(window->OnlineIndexCompleteForTest() ? 1 : 0));

  window->GoToFrameForTest(start_frame);
  const bool start_ok = WaitUntil(
      [window, start_frame]() {
        return window->OnlineSeekIdleForTest() &&
               window->CurrentFrameForTest() == start_frame;
      },
      30000);
  PrintKV("vip_step_start",
          QStringLiteral("ok=%1 frame=%2 stable=%3")
              .arg(start_ok ? 1 : 0)
              .arg(window->CurrentFrameForTest())
              .arg(window->OnlineStableFramesForTest()));
  if (!start_ok) {
    audit.failures = seeks;
    audit.n = seeks;
    return audit;
  }
  audit.stable_frames = window->OnlineStableFramesForTest();
  audit.boundary_margin_frames = audit.stable_frames - start_frame - 1;
  qint64 current = window->CurrentFrameForTest();
  for (int i = 0; i < seeks; ++i) {
    const bool forward = (i % 2) == 0;
    const qint64 expected = current + (forward ? 1 : -1);
    if (expected < 0) {
      continue;
    }
    QElapsedTimer timer;
    timer.start();
    window->FocusVideoForTest();
    window->PressKeyForTest(forward ? Qt::Key_PageDown : Qt::Key_PageUp);
    const bool ok = WaitUntil(
        [window, expected]() {
          return window->OnlineStepIdleForTest() &&
                 window->CurrentFrameForTest() == expected;
        },
        per_step_timeout_ms);
    const qint64 ms = timer.elapsed();
    audit.samples.push_back(ms);
    ++audit.n;
    if (!ok || window->CurrentFrameForTest() != expected) {
      ++audit.failures;
      ++audit.consecutive_failures;
      if (audit.consecutive_failures >= 3) {
        PrintKV("vip_step_abort",
                QStringLiteral("consecutive=%1 i=%2 frame=%3 state=%4")
                    .arg(audit.consecutive_failures)
                    .arg(i)
                    .arg(window->CurrentFrameForTest())
                    .arg(window->OnlineStepStateForTest()));
        break;
      }
    } else {
      audit.consecutive_failures = 0;
    }
    current = window->CurrentFrameForTest();
  }
  const double p50 = Percentile(audit.samples, 0.50);
  const double p95 = Percentile(audit.samples, 0.95);
  const double max =
      audit.samples.empty()
          ? -1.0
          : static_cast<double>(*std::max_element(audit.samples.begin(),
                                                  audit.samples.end()));
  PrintKV("vip_p7",
          QStringLiteral("n=%1 failures=%2 p50_ms=%3 p95_ms=%4 max_ms=%5 "
                         "hw=%6 stable=%7 start=%8 margin_frames=%9 fps=%10")
              .arg(audit.n)
              .arg(audit.failures)
              .arg(QString::number(p50, 'f', 1))
              .arg(QString::number(p95, 'f', 1))
              .arg(QString::number(max, 'f', 1))
              .arg(audit.hardware ? 1 : 0)
              .arg(audit.stable_frames)
              .arg(audit.start_frame)
              .arg(audit.boundary_margin_frames)
              .arg(QString::number(audit.fps, 'f', 3)));
  return audit;
}

// ---------------------------------------------------------------- cache stats

struct CacheStats {
  int files = 0;
  qint64 bytes = 0;
};

CacheStats ScanCache(const QString& root) {
  CacheStats stats;
  if (root.isEmpty() || !QDir(root).exists()) {
    return stats;
  }
  QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString path = it.next();
    ++stats.files;
    stats.bytes += QFileInfo(path).size();
  }
  return stats;
}

QString CacheTierDir(const QString& root, const QString& bvid, qint64 cid,
                     int qn, int codecid) {
  return QDir(root).filePath(QStringLiteral("%1/%2/%3-%4")
                                 .arg(bvid)
                                 .arg(cid)
                                 .arg(qn)
                                 .arg(codecid));
}

QString CacheStatsText(const CacheStats& stats) {
  return QStringLiteral("files=%1 bytes=%2").arg(stats.files).arg(stats.bytes);
}

}  // namespace

// ---------------------------------------------------------------- catalog test

int RunOnlineVipCatalogTest(const OnlineVipTestOptions& options) {
  std::printf(
      "T8.15 vip_catalog begin bvid=%s cid=%lld tag=%s timeout_ms=%d\n",
      options.bvid.toUtf8().constData(),
      static_cast<long long>(options.cid),
      (options.tag.isEmpty() ? options.bvid : options.tag).toUtf8().constData(),
      options.timeout_ms);
  std::fflush(stdout);

  if (options.bvid.isEmpty()) {
    PrintKV("error", QStringLiteral("missing_bvid"));
    PrintKV("verdict", QStringLiteral("blocked"));
    return 2;
  }
  const VipCredential credential = LoadVipCredential(options.sessdata_file);
  if (!credential.ok) {
    PrintKV("error", credential.error);
    PrintKV("verdict", QStringLiteral("blocked"));
    return 2;
  }
  PrintKV("credential_source", credential.source);
  lapian::online::WinHttpClient http;
  lapian::online::HttpCancelToken cancel;
  VipProbe probe =
      RunVipProbe(options, credential.sessdata, &http, cancel);
  QString secret = credential.sessdata;
  lapian::online::ClearSecret(&secret);

  Check(QStringLiteral("nav_login"), probe.login);
  Check(QStringLiteral("nav_vip_status_1"), probe.vip_status == 1,
        QStringLiteral("vip_status=%1").arg(probe.vip_status));
  if (probe.blocked) {
    PrintKV("vip_block", probe.block_reason);
    PrintKV("checks", Num(g_checks));
    PrintKV("failures", Num(g_failures));
    PrintKV("verdict", QStringLiteral("blocked"));
    return 2;
  }
  Check(QStringLiteral("view_ok"), probe.view_ok, probe.title);
  Check(QStringLiteral("dash_auto80_ok"), probe.dash80.error.ok(),
        probe.dash80.error.message);
  Check(QStringLiteral("dash_max120_ok"), probe.dash120.error.ok(),
        probe.dash120.error.message);
  if (!CatalogAvailable(probe)) {
    PrintKV("vip_block", QStringLiteral("no_catalog"));
    PrintKV("checks", Num(g_checks));
    PrintKV("failures", Num(g_failures));
    PrintKV("verdict", QStringLiteral("blocked"));
    return 2;
  }
  const DashCatalog& full = PreferredCatalog(probe);
  Check(QStringLiteral("dash_has_video"), full.HasVideo());
  Check(QStringLiteral("dash_has_audio"), full.HasAudio(),
        QStringLiteral("audios=%1").arg(full.audios.size()));
  EmitCatalogChecks(
      options, probe,
      [](const QString& name, bool ok, const QString& detail) {
        Check(name, ok, detail);
      });

  const bool has60 = full.FindTier(116) != nullptr ||
                     full.FindTier(74) != nullptr;
  const bool has4k = full.FindTier(120) != nullptr;
  PrintItem(QStringLiteral("A1"),
            g_failures == 0 ? QStringLiteral("pass") : QStringLiteral("fail"),
            has60 || has4k ? QStringLiteral("menu_model_only")
                           : QStringLiteral("no_116_74_120"));
  PrintItem(QStringLiteral("A2"), QStringLiteral("unverified"),
            QStringLiteral("catalog_only_no_playback"));
  PrintItem(QStringLiteral("A3"),
            !has60 ? QStringLiteral("unverified")
                   : QStringLiteral("unverified"),
            QStringLiteral("catalog_only_no_manual_selection"));
  PrintItem(QStringLiteral("A4"),
            !has4k ? QStringLiteral("unverified")
                   : QStringLiteral("unverified"),
            QStringLiteral("catalog_only_no_playback"));
  PrintItem(QStringLiteral("A5"), QStringLiteral("unverified"),
            QStringLiteral("catalog_only"));
  PrintItem(QStringLiteral("A6"), QStringLiteral("report_only"),
            QStringLiteral("no_mvp_pass"));
  PrintItem(QStringLiteral("A7"), QStringLiteral("report_only"),
            QStringLiteral("m1_m2_unchanged"));

  const QString verdict =
      g_failures == 0 ? QStringLiteral("pass") : QStringLiteral("fail");
  PrintKV("checks", Num(g_checks));
  PrintKV("failures", Num(g_failures));
  PrintKV("vip_availability_summary",
          QStringLiteral("has_60=%1 has_4k=%2")
              .arg(has60 ? 1 : 0)
              .arg(has4k ? 1 : 0));
  PrintKV("verdict", verdict);
  std::printf("T8.15 vip_catalog end verdict=%s checks=%d failures=%d\n",
              verdict.toUtf8().constData(), g_checks, g_failures);
  std::fflush(stdout);
  return g_failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------- UI test

OnlineVipUiTestRunner::OnlineVipUiTestRunner(MainWindow* window,
                                             OnlineVipTestOptions options)
    : window_(window), options_(std::move(options)) {}

void OnlineVipUiTestRunner::Start() {
  QTimer::singleShot(0, window_, [this]() { Run(); });
}

void OnlineVipUiTestRunner::Run() {
  auto check = [this](const QString& name, bool ok,
                      const QString& detail = {}) {
    ++checks_;
    if (!ok) {
      ++failures_;
    }
    const QString line =
        QStringLiteral("vip_check %1 ok=%2%3")
            .arg(name)
            .arg(ok ? 1 : 0)
            .arg(detail.isEmpty() ? QString()
                                  : QStringLiteral(" detail=") + OneLine(detail));
    std::printf("%s\n", line.toUtf8().constData());
    std::fflush(stdout);
  };
  auto finish = [this]() {
    std::printf("vip_result checks=%d failures=%d verdict=%s\n", checks_,
                failures_, failures_ == 0 ? "pass" : "fail");
    std::fflush(stdout);
    QCoreApplication::exit(failures_ == 0 ? 0 : 1);
  };
  const QString tag = options_.tag.isEmpty() ? options_.bvid : options_.tag;
  std::printf(
      "T8.15 vip_ui begin tag=%s bvid=%s cid=%lld requested_qn=%d "
      "requested_codecid=%d expect_auto=%d play_seconds=%d seeks=%d "
      "switch_qn=%d\n",
      tag.toUtf8().constData(), options_.bvid.toUtf8().constData(),
      static_cast<long long>(options_.cid), options_.qn, options_.codecid,
      options_.expect_auto, options_.play_seconds, options_.seeks,
      options_.switch_qn);
  std::fflush(stdout);

  VipCredential credential = LoadVipCredential(options_.sessdata_file);
  if (!credential.ok) {
    PrintKV("vip_block", credential.error);
    PrintItem(QStringLiteral("A1"), QStringLiteral("unverified"),
              QStringLiteral("no_credential"));
    PrintItem(QStringLiteral("A6"), QStringLiteral("report_only"),
              QStringLiteral("no_mvp_pass"));
    PrintItem(QStringLiteral("A7"), QStringLiteral("report_only"),
              QStringLiteral("m1_m2_unchanged"));
    finish();
    return;
  }
  PrintKV("credential_source", credential.source);

  lapian::online::WinHttpClient http;
  lapian::online::HttpCancelToken cancel;
  VipProbe probe =
      RunVipProbe(options_, credential.sessdata, &http, cancel);
  QString session_secret = credential.sessdata;
  credential.sessdata.clear();
  check(QStringLiteral("nav_login"), probe.login);
  check(QStringLiteral("nav_vip_status_1"), probe.vip_status == 1,
        QStringLiteral("vip_status=%1").arg(probe.vip_status));
  if (probe.blocked) {
    PrintKV("vip_block", probe.block_reason);
    lapian::online::ClearSecret(&session_secret);
    finish();
    return;
  }
  check(QStringLiteral("view_ok"), probe.view_ok, probe.title);
  check(QStringLiteral("dash_auto80_ok"), probe.dash80.error.ok(),
        probe.dash80.error.message);
  check(QStringLiteral("dash_max120_ok"), probe.dash120.error.ok(),
        probe.dash120.error.message);
  if (!CatalogAvailable(probe)) {
    PrintKV("vip_block", QStringLiteral("no_catalog"));
    lapian::online::ClearSecret(&session_secret);
    finish();
    return;
  }
  EmitCatalogChecks(options_, probe,
                    [&check](const QString& name, bool ok,
                             const QString& detail) { check(name, ok, detail); });

  const DashCatalog& full = PreferredCatalog(probe);
  const DashCatalog& auto_catalog =
      probe.dash80.error.ok() && probe.dash80.catalog.HasVideo()
          ? probe.dash80.catalog
          : full;
  const QualityTier* requested_tier =
      options_.qn > 0 ? full.FindTier(options_.qn) : nullptr;
  const bool has60 = full.FindTier(116) != nullptr ||
                     full.FindTier(74) != nullptr;
  const bool has4k = full.FindTier(120) != nullptr;
  const bool has60_avc =
      (full.FindTier(116) != nullptr && full.FindTier(116)->has_avc()) ||
      (full.FindTier(74) != nullptr && full.FindTier(74)->has_avc());
  const bool has60_hevc_av1 =
      has60 && !has60_avc &&
      (((full.FindTier(116) != nullptr) &&
        (full.FindTier(116)->has_hevc() || full.FindTier(116)->has_av1())) ||
       ((full.FindTier(74) != nullptr) &&
        (full.FindTier(74)->has_hevc() || full.FindTier(74)->has_av1())));

  if (options_.qn > 0 && requested_tier == nullptr) {
    PrintKV("vip_block",
            QStringLiteral("requested_qn_absent qn=%1").arg(options_.qn));
    PrintItem(options_.qn >= 120 ? QStringLiteral("A4")
                                 : QStringLiteral("A3"),
              QStringLiteral("unverified"),
              QStringLiteral("no_tier qn=%1").arg(options_.qn));
    PrintItem(QStringLiteral("A6"), QStringLiteral("report_only"),
              QStringLiteral("no_mvp_pass"));
    PrintItem(QStringLiteral("A7"), QStringLiteral("report_only"),
              QStringLiteral("m1_m2_unchanged"));
    lapian::online::ClearSecret(&session_secret);
    finish();
    return;
  }
  if (options_.qn == 0 && !has60) {
    PrintKV("vip_block", QStringLiteral("no_60fps_tier"));
    PrintItem(QStringLiteral("A2"), QStringLiteral("unverified"),
              QStringLiteral("no_60fps_tier"));
    PrintItem(QStringLiteral("A3"), QStringLiteral("unverified"),
              QStringLiteral("no_60fps_tier"));
    PrintItem(QStringLiteral("A6"), QStringLiteral("report_only"),
              QStringLiteral("no_mvp_pass"));
    PrintItem(QStringLiteral("A7"), QStringLiteral("report_only"),
              QStringLiteral("m1_m2_unchanged"));
    lapian::online::ClearSecret(&session_secret);
    finish();
    return;
  }

  // 测试根：显式传入；不改真实默认缓存/记录/截图。
  window_->raise();
  window_->activateWindow();
  window_->FocusVideoForTest();
  if (!options_.cache_root.isEmpty()) {
    window_->SetOnlineCacheRootForTest(options_.cache_root);
  }
  if (!options_.records_root.isEmpty()) {
    window_->SetOnlineRecordsRootForTest(options_.records_root);
  }
  if (!options_.pictures_root.isEmpty()) {
    window_->SetSnapshotPicturesRootForTest(options_.pictures_root);
  }

  lapian::app::OnlineRequest request;
  request.bvid = options_.bvid;
  request.cid = probe.cid;
  request.page = 1;
  request.title = probe.title;
  request.owner = probe.owner;
  request.duration_ms = probe.duration_ms;
  request.requested_qn = options_.qn;
  request.requested_codecid = options_.codecid;
  request.sessdata = session_secret;
  const bool started = window_->StartOnlineSession(request);
  lapian::online::ClearSecret(&request.sessdata);
  lapian::online::ClearSecret(&session_secret);
  check(QStringLiteral("session_started"), started,
        window_->OnlineLastErrorForTest());
  if (!started) {
    finish();
    return;
  }
  const int ready_timeout =
      std::max(options_.timeout_ms, options_.qn >= 120 ? 90000 : 60000);
  const bool ready = window_->WaitForOnlineReadyForTest(ready_timeout);
  check(QStringLiteral("ready"), ready,
        QStringLiteral("%1 | %2")
            .arg(window_->OnlineLastErrorForTest(),
                 window_->OnlineStateForTest()));
  if (!ready) {
    // 手动档明确失败时按“无样本/未验证”处理，不伪造通过。
    if (options_.qn > 0) {
      PrintItem(options_.qn >= 120 ? QStringLiteral("A4")
                                   : QStringLiteral("A3"),
                QStringLiteral("unverified"),
                QStringLiteral("open_failed"));
    } else {
      PrintItem(QStringLiteral("A2"), QStringLiteral("unverified"),
                QStringLiteral("open_failed"));
    }
    window_->CancelOnlineLoadingForTest();
    finish();
    return;
  }
  const bool quality = window_->WaitForOnlineQualityForTest(options_.timeout_ms);
  check(QStringLiteral("quality_catalog"), quality,
        window_->OnlineQualityErrorForTest());

  const int actual_qn = window_->OnlineQualityForTest();
  const int actual_codecid = window_->OnlineCodecidForTest();
  const double actual_fps = window_->OnlineStreamFpsForTest() > 0.0
                                ? window_->OnlineStreamFpsForTest()
                                : window_->OnlineVideoFpsForTest();
  const double container_fps = window_->OnlineVideoFpsForTest();
  const double source_hint = window_->OnlineSourceFpsHintForTest();
  const QString menu = window_->OnlineQualityMenuTextForTest();
  const QString actions =
      window_->OnlineQualityMenuActionsForTest().join(QStringLiteral(" || "));
  PrintKV("vip_session",
          QStringLiteral("ready=%1 cache_hit=%2 dash=%3 actual_qn=%4 "
                         "actual_codecid=%5 actual_fps=%6 container_fps=%7 "
                         "source_fps_hint=%8 drop=%9 banner=%10 hw=%11 "
                         "button_visible=%12 part=%13")
              .arg(ready ? 1 : 0)
              .arg(window_->OnlineCacheHitForTest() ? 1 : 0)
              .arg(window_->OnlineDashForTest() ? 1 : 0)
              .arg(actual_qn)
              .arg(actual_codecid)
              .arg(QString::number(actual_fps, 'f', 3))
              .arg(QString::number(container_fps, 'f', 3))
              .arg(QString::number(source_hint, 'f', 0))
              .arg(window_->OnlineFrameRateDropForTest() ? 1 : 0)
              .arg(window_->OnlineFrameRateBannerVisibleForTest() ? 1 : 0)
              .arg(window_->HardwareDecodeForTest() ? 1 : 0)
              .arg(window_->OnlineQualityButtonVisibleForTest() ? 1 : 0)
              .arg(window_->OnlinePartPathForTest()));
  PrintKV("vip_status_detail", OneLine(window_->OnlineStatusDetailForTest()));
  PrintKV("vip_menu", OneLine(menu));
  PrintKV("vip_menu_actions", OneLine(actions));
  PrintKV("vip_quality_summary", OneLine(window_->OnlineQualitySummaryForTest()));

  const int expected_qn =
      options_.qn > 0
          ? options_.qn
          : auto_catalog.DefaultQn();
  int expected_codecid = options_.codecid;
  if (expected_codecid <= 0) {
    const DashStream* expected_stream =
        options_.qn > 0
            ? (requested_tier != nullptr ? requested_tier->PreferredStream()
                                         : nullptr)
            : auto_catalog.DefaultStream();
    expected_codecid = expected_stream != nullptr ? expected_stream->codecid : 0;
  }
  const DashStream* actual_stream =
      full.FindStream(actual_qn, actual_codecid);
  const double catalog_fps =
      actual_stream != nullptr ? actual_stream->fps : 0.0;
  PrintKV("vip_expect",
          QStringLiteral("expected_qn=%1 expected_codecid=%2 "
                         "auto_default_qn=%3 auto_default_codecid=%4")
              .arg(expected_qn)
              .arg(expected_codecid)
              .arg(auto_catalog.DefaultQn())
              .arg(auto_catalog.DefaultStream() != nullptr
                       ? auto_catalog.DefaultStream()->codecid
                       : 0));
  check(QStringLiteral("actual_qn_matches"), actual_qn == expected_qn,
        QStringLiteral("actual=%1 expected=%2").arg(actual_qn).arg(expected_qn));
  check(QStringLiteral("actual_codecid_matches"),
        expected_codecid <= 0 || actual_codecid == expected_codecid,
        QStringLiteral("actual=%1 expected=%2")
            .arg(actual_codecid)
            .arg(expected_codecid));
  check(QStringLiteral("actual_fps_known"), actual_fps > 0.0,
        QStringLiteral("fps=%1").arg(actual_fps));
  if (catalog_fps > 0.0 && actual_fps > 0.0) {
    check(QStringLiteral("actual_fps_matches_catalog"),
          std::abs(actual_fps - catalog_fps) <= 0.6,
          QStringLiteral("actual=%1 catalog=%2")
              .arg(actual_fps)
              .arg(catalog_fps));
  }
  check(QStringLiteral("menu_button_visible"),
        window_->OnlineQualityButtonVisibleForTest());
  const QString actual_tier_line =
      QStringLiteral("qn=%1").arg(actual_qn);
  const QualityTier* menu_tier = full.FindTier(actual_qn);
  const bool menu_preferred_codec =
      menu_tier != nullptr && menu_tier->PreferredStream() != nullptr &&
      menu_tier->PreferredStream()->codecid == actual_codecid;
  if (menu_preferred_codec) {
    check(QStringLiteral("menu_lists_actual_tier"),
          menu.contains(actual_tier_line) &&
              menu.contains(QStringLiteral("checked=1")),
          QStringLiteral("actual=%1 menu=%2")
              .arg(actual_qn)
              .arg(OneLine(menu)));
  } else {
    // 菜单是 tier 级（每个 qn 只列首选编码，产品也只按首选编码切档）；
    // 测试钩子强制非首选 codecid 时只要求档位存在，不要求 checked=1。
    check(QStringLiteral("menu_lists_actual_tier"),
          menu.contains(actual_tier_line),
          QStringLiteral("tier_level_only actual=%1 codecid=%2")
              .arg(actual_qn)
              .arg(actual_codecid));
    PrintKV("vip_menu_note", QStringLiteral("non_preferred_codecid"));
  }

  const bool manual_60 = options_.qn == 116 || options_.qn == 74;
  const bool manual_4k = options_.qn >= 120;
  const bool auto_mode = options_.qn == 0;
  bool a1_ok = true;
  bool a2_ok = true;
  bool a3_ok = true;
  bool a4_ok = false;
  bool a5_executed = false;
  bool a5_ok = false;
  bool p7_executed = false;
  bool p7_pass = false;
  StepAudit p7_audit;

  // A1：档位菜单标注（60fps/codec/可用档）。
  const QualityTier* actual_tier = full.FindTier(actual_qn);
  if (actual_tier != nullptr) {
    const bool is60 = actual_qn == 116 || actual_qn == 74;
    const bool is4k = actual_qn >= 120;
    if (is60) {
      const bool tier_line_ok =
          menu.contains(QStringLiteral("qn=%1").arg(actual_qn)) &&
          menu.contains(QStringLiteral("60fps")) &&
          (menu.contains(QStringLiteral("AVC")) ||
           menu.contains(QStringLiteral("HEVC")) ||
           menu.contains(QStringLiteral("AV1")));
      check(QStringLiteral("a1_menu_60fps_codec"), tier_line_ok,
            OneLine(menu));
      a1_ok = a1_ok && tier_line_ok;
    }
    if (is4k) {
      const bool tier_line_ok =
          menu.contains(QStringLiteral("qn=%1").arg(actual_qn)) &&
          menu.contains(QStringLiteral("仅手动")) &&
          menu.contains(QStringLiteral("可用档"));
      check(QStringLiteral("a1_menu_4k_manual_available"), tier_line_ok,
            OneLine(menu));
      a1_ok = a1_ok && tier_line_ok;
    }
  }
  check(QStringLiteral("a1_menu_guarantee_note"),
        menu.contains(QStringLiteral("菜单可选不等于保证可达")), OneLine(menu));
  a1_ok = a1_ok && menu.contains(QStringLiteral("菜单可选不等于保证可达"));

  // 真实 60fps：不显示降帧横幅；菜单标 fps。
  if (actual_fps >= 59.5) {
    check(QStringLiteral("true60_no_frame_drop"),
          !window_->OnlineFrameRateDropForTest(),
          QStringLiteral("fps=%1").arg(actual_fps));
    check(QStringLiteral("true60_no_drop_banner"),
          !window_->OnlineFrameRateBannerVisibleForTest(),
          window_->BannerStateForTest());
    a2_ok = a2_ok && !window_->OnlineFrameRateDropForTest() &&
            !window_->OnlineFrameRateBannerVisibleForTest();
  }

  // A2：requested_qn=0 自动路径。
  if (auto_mode) {
    if (has60 && has60_avc) {
      const bool auto_selected_60 = actual_qn == 116 || actual_qn == 74;
      check(QStringLiteral("a2_auto_selects_avc60"), auto_selected_60,
            QStringLiteral("actual=%1 default=%2")
                .arg(actual_qn)
                .arg(auto_catalog.DefaultQn()));
      check(QStringLiteral("a2_actual_fps_ge_59_5"), actual_fps >= 59.5,
            QStringLiteral("fps=%1").arg(actual_fps));
      check(QStringLiteral("a2_no_frame_drop"),
            !window_->OnlineFrameRateDropForTest(),
            QStringLiteral("fps=%1").arg(actual_fps));
      a2_ok = a2_ok && auto_selected_60 && actual_fps >= 59.5 &&
              !window_->OnlineFrameRateDropForTest();
    } else if (has60_hevc_av1) {
      // A3 自动侧：不自动选 60，且必须提示有更高帧率档可手动切换。
      const bool auto_selected_60 = actual_qn == 116 || actual_qn == 74;
      check(QStringLiteral("a3_auto_not_select_hevc_av1_60"),
            !auto_selected_60,
            QStringLiteral("actual=%1").arg(actual_qn));
      const QString hint_text = window_->StatusMessageForTest() + QLatin1Char(' ') +
                                window_->BannerStateForTest() + QLatin1Char(' ') +
                                menu;
      const bool hint = hint_text.contains(QStringLiteral("更高帧率"));
      check(QStringLiteral("a3_higher_fps_manual_hint"), hint,
            OneLine(hint_text));
      const bool menu_manual =
          menu.contains(QStringLiteral("60fps")) &&
          menu.contains(QStringLiteral("手动"));
      check(QStringLiteral("a3_menu_60fps_manual"), menu_manual,
            OneLine(menu));
      a3_ok = a3_ok && !auto_selected_60 && hint && menu_manual;
      if (options_.expect_auto == 1) {
        check(QStringLiteral("expect_auto_60"), auto_selected_60,
              QStringLiteral("actual=%1").arg(actual_qn));
      } else if (options_.expect_auto == 0) {
        check(QStringLiteral("expect_not_auto_60"), !auto_selected_60,
              QStringLiteral("actual=%1").arg(actual_qn));
      }
    } else {
      PrintKV("vip_auto", QStringLiteral("no_60fps_tier"));
    }
  }

  // A3 手动侧：HEVC/AV1-only 60fps 手动选中后的实际编码/帧率。
  if (manual_60) {
    const QualityTier* tier = full.FindTier(options_.qn);
    const bool tier_exists = tier != nullptr;
    const bool selected = actual_qn == options_.qn;
    const bool codec_ok =
        options_.codecid <= 0 || actual_codecid == options_.codecid;
    const int catalog_codecid =
        tier != nullptr && tier->PreferredStream() != nullptr
            ? tier->PreferredStream()->codecid
            : 0;
    const bool hevc_av1_tier =
        tier != nullptr && !tier->has_avc() &&
        (tier->has_hevc() || tier->has_av1());
    const QString codec_name =
        lapian::online::CodecName(actual_codecid);
    const bool menu_marks =
        menu.contains(QStringLiteral("qn=%1").arg(options_.qn)) &&
        menu.contains(QStringLiteral("checked=1")) &&
        menu.contains(QStringLiteral("60fps")) &&
        (menu.contains(codec_name));
    check(QStringLiteral("a3_manual_selected"), selected,
          QStringLiteral("actual=%1 requested=%2").arg(actual_qn).arg(options_.qn));
    check(QStringLiteral("a3_manual_codecid"), codec_ok,
          QStringLiteral("actual=%1 catalog=%2").arg(actual_codecid).arg(
              catalog_codecid));
    check(QStringLiteral("a3_manual_fps_ge_59_5"), actual_fps >= 59.5,
          QStringLiteral("fps=%1").arg(actual_fps));
    check(QStringLiteral("a3_manual_menu_codec_60fps"), menu_marks,
          OneLine(menu));
    a3_ok = a3_ok && selected && codec_ok && actual_fps >= 59.5 && menu_marks &&
            tier_exists && hevc_av1_tier;
    if (options_.expect_auto == 1) {
      check(QStringLiteral("expect_auto_60"), actual_fps >= 59.5,
            QStringLiteral("fps=%1").arg(actual_fps));
    }
  }

  // P7：60fps 档按 OP9 条件；4K 只记录不设门槛。
  if (manual_60 || (auto_mode && (actual_qn == 116 || actual_qn == 74))) {
    p7_executed = true;
    p7_audit = RunStepAudit(window_, options_.seeks, actual_fps, 30, 8000);
    const bool margin_ok =
        p7_audit.stable_frames <= 0 ||
        p7_audit.boundary_margin_frames >=
            static_cast<qint64>(std::ceil(2.0 * (actual_fps > 0.5 ? actual_fps
                                                                  : 60.0)));
    check(QStringLiteral("p7_strict_pm1"), p7_audit.failures == 0,
          QStringLiteral("failures=%1 n=%2")
              .arg(p7_audit.failures)
              .arg(p7_audit.n));
    check(QStringLiteral("p7_boundary_ge_2s"), margin_ok,
          QStringLiteral("margin_frames=%1 fps=%2")
              .arg(p7_audit.boundary_margin_frames)
              .arg(actual_fps));
    const double p95 = Percentile(p7_audit.samples, 0.95);
    const bool p95_ok = p95 >= 0.0 && p95 <= 80.0;
    check(QStringLiteral("p7_p95_le_80ms"), p95_ok,
          QStringLiteral("p95=%1").arg(p95, 0, 'f', 1));
    check(QStringLiteral("p7_hardware_decode"), p7_audit.hardware);
    p7_pass = p7_audit.failures == 0 && margin_ok && p95_ok;
    if (!p7_audit.hardware) {
      // 软解：OP9 要求硬解；只写“可用档/partial”。
      PrintKV("vip_p7_note", QStringLiteral("software_decode"));
    }
    if (auto_mode && has60_avc) {
      a2_ok = a2_ok && p7_pass;
    }
    if (manual_60) {
      a3_ok = a3_ok && p7_pass;
    }
  }

  // A4：4K 只手动；能打开、播放、已缓存逐帧不崩；不承诺 P7/P9。
  if (manual_4k) {
    check(QStringLiteral("a4_menu_manual_only"),
          menu.contains(QStringLiteral("仅手动")) &&
              menu.contains(QStringLiteral("可用档")),
          OneLine(menu));
    window_->FocusVideoForTest();
    window_->PressSpaceKeyForTest();
    const bool playing = WaitUntil(
        [this]() { return window_->OnlinePlayingForTest(); }, 2000);
    const qint64 play_start = window_->CurrentFrameForTest();
    QElapsedTimer play_timer;
    play_timer.start();
    while (play_timer.elapsed() < options_.play_seconds * 1000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const qint64 play_end = window_->CurrentFrameForTest();
    window_->PressSpaceKeyForTest();
    check(QStringLiteral("a4_playing"), playing);
    check(QStringLiteral("a4_play_advanced"), play_end > play_start,
          QStringLiteral("from=%1 to=%2").arg(play_start).arg(play_end));
    PrintKV("vip_4k_play",
            QStringLiteral("seconds=%1 from=%2 to=%3")
                .arg(options_.play_seconds)
                .arg(play_start)
                .arg(play_end));
    if (options_.seeks > 0) {
      p7_executed = true;
      p7_audit = RunStepAudit(window_, options_.seeks, actual_fps, 10, 20000);
      PrintKV("vip_4k_steps",
              QStringLiteral("n=%1 failures=%2 p50_ms=%3 p95_ms=%4 max_ms=%5 "
                             "hw=%6 stable=%7")
                  .arg(p7_audit.n)
                  .arg(p7_audit.failures)
                  .arg(QString::number(Percentile(p7_audit.samples, 0.50), 'f', 1))
                  .arg(QString::number(Percentile(p7_audit.samples, 0.95), 'f', 1))
                  .arg(p7_audit.samples.empty()
                           ? QStringLiteral("-1")
                           : QString::number(
                                 static_cast<double>(*std::max_element(
                                     p7_audit.samples.begin(),
                                     p7_audit.samples.end())),
                                 'f', 1))
                  .arg(p7_audit.hardware ? 1 : 0)
                  .arg(p7_audit.stable_frames));
      check(QStringLiteral("a4_steps_land_exact"), p7_audit.failures == 0,
            QStringLiteral("failures=%1 n=%2")
                .arg(p7_audit.failures)
                .arg(p7_audit.n));
      a4_ok = playing && play_end > play_start && p7_audit.failures == 0;
    } else {
      a4_ok = playing && play_end > play_start;
    }
    // 4K 不设性能门槛：上面的 p95 只记录，不判 ≤80ms。
    check(QStringLiteral("a4_no_p7_p9_claim"), true,
          QStringLiteral("op10_no_threshold"));
  }

  // A5：高档 → 1080P30；缓存/记录/标记/PTS 映射。
  if ((actual_qn == 74 || actual_qn == 116 || actual_qn >= 120) &&
      options_.switch_qn > 0 && actual_qn != options_.switch_qn &&
      full.FindTier(options_.switch_qn) != nullptr) {
    a5_executed = true;
    const bool index_ready =
        window_->WaitForOnlineIndexFramesForTest(120, 60000);
    check(QStringLiteral("a5_index_ready"), index_ready,
          QStringLiteral("frames=%1").arg(window_->OnlineIndexFramesForTest()));
    window_->GoToFrameForTest(30);
    const bool frame30 = WaitUntil(
        [this]() {
          return window_->OnlineSeekIdleForTest() &&
                 window_->CurrentFrameForTest() == 30;
        },
        30000);
    check(QStringLiteral("a5_frame30_before_switch"), frame30,
          QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
    const qint64 old_pts = window_->CurrentPtsUsForTest();
    window_->AddMarkForTest();
    const int old_marks = window_->MarkCountForTest();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const QString record_path = window_->OnlineRecordPathForTest();
    const bool record_before =
        !record_path.isEmpty() && QFileInfo::exists(record_path);
    const QString cache_root = window_->OnlineCacheRootForTest();
    const CacheStats cache_before = ScanCache(cache_root);
    const QString old_tier_dir = CacheTierDir(
        cache_root, options_.bvid, probe.cid, actual_qn, actual_codecid);
    PrintKV("vip_a5_before",
            QStringLiteral("qn=%1 mark_count=%2 frame=%3 pts=%4 record=%5 "
                           "cache=%6 old_tier_dir=%7")
                .arg(actual_qn)
                .arg(old_marks)
                .arg(window_->CurrentFrameForTest())
                .arg(old_pts)
                .arg(record_before ? 1 : 0)
                .arg(CacheStatsText(cache_before))
                .arg(QDir(old_tier_dir).exists() ? 1 : 0));

    QString switch_status;
    QElapsedTimer switch_timer;
    switch_timer.start();
    const bool triggered =
        window_->TriggerOnlineQualityEntryForTest(options_.switch_qn,
                                                  &switch_status);
    const qint64 click_ms = switch_timer.elapsed();
    check(QStringLiteral("a5_switch_triggered"), triggered, switch_status);
    PrintKV("vip_a5_switch_click",
            QStringLiteral("ms=%1 status=%2")
                .arg(click_ms)
                .arg(OneLine(switch_status)));
    const bool landed = window_->WaitForOnlineQualitySwitchForTest(
        options_.switch_qn, std::max(120000, options_.timeout_ms * 3));
    check(QStringLiteral("a5_switch_landed"), landed,
          QStringLiteral("%1 | state=%2 error=%3")
              .arg(window_->OnlineQualitySwitchStatusForTest(),
                   window_->OnlineStateForTest(),
                   window_->OnlineLastErrorForTest()));
    if (!landed) {
      PrintKV("vip_a5_fail",
              QStringLiteral("state=%1 error=%2 summary=%3")
                  .arg(window_->OnlineStateForTest(),
                       window_->OnlineLastErrorForTest(),
                       OneLine(window_->OnlineQualitySummaryForTest())));
    }
    if (landed) {
      window_->WaitForOnlineQualityForTest(options_.timeout_ms);
      check(QStringLiteral("a5_actual_qn"),
            window_->OnlineQualityForTest() == options_.switch_qn,
            QStringLiteral("actual=%1 expected=%2")
                .arg(window_->OnlineQualityForTest())
                .arg(options_.switch_qn));
      const QString switch_notice =
          window_->OnlineQualitySwitchStatusForTest();
      const bool notice_ok =
          switch_notice.contains(QStringLiteral("PTS 映射")) ||
          switch_notice.contains(QStringLiteral("帧号可能变化")) ||
          window_->StatusMessageForTest().contains(QStringLiteral("PTS 映射"));
      check(QStringLiteral("a5_pts_mapping_notice"), notice_ok, switch_notice);
      check(QStringLiteral("a5_marks_preserved"),
            window_->MarkCountForTest() == old_marks,
            QStringLiteral("before=%1 after=%2")
                .arg(old_marks)
                .arg(window_->MarkCountForTest()));
      bool mark_pts_found = false;
      for (int i = 0; i < window_->MarkCountForTest(); ++i) {
        const qint64 mark_frame = window_->MarkFrameAtForTest(i);
        const qint64 mark_pts = window_->OnlineFramePtsForTest(mark_frame);
        if (mark_pts >= 0 && std::llabs(mark_pts - old_pts) <= 100000) {
          mark_pts_found = true;
        }
      }
      check(QStringLiteral("a5_mark_pts_mapping"), mark_pts_found,
            QStringLiteral("old_pts=%1 marks=%2")
                .arg(old_pts)
                .arg(window_->MarkCountForTest()));
      check(QStringLiteral("a5_position_pts_preserved"),
            old_pts > 0 &&
                std::llabs(window_->CurrentPtsUsForTest() - old_pts) <= 100000,
            QStringLiteral("old=%1 new=%2")
                .arg(old_pts)
                .arg(window_->CurrentPtsUsForTest()));
      const bool record_after =
          !record_path.isEmpty() && QFileInfo::exists(record_path);
      check(QStringLiteral("a5_record_preserved"), record_after, record_path);
      const CacheStats cache_after = ScanCache(cache_root);
      const bool old_tier_kept = QDir(old_tier_dir).exists();
      check(QStringLiteral("a5_cache_preserved"),
            cache_after.files >= cache_before.files &&
                cache_after.bytes >= cache_before.bytes && old_tier_kept,
            QStringLiteral("before=%1 after=%2 old_tier=%3")
                .arg(CacheStatsText(cache_before))
                .arg(CacheStatsText(cache_after))
                .arg(old_tier_kept ? QStringLiteral("1")
                                   : QStringLiteral("0")));
      PrintKV("vip_a5_after",
              QStringLiteral("qn=%1 mark_count=%2 frame=%3 pts=%4 record=%5 "
                             "cache=%6 old_tier_dir=%7 notice=%8")
                  .arg(window_->OnlineQualityForTest())
                  .arg(window_->MarkCountForTest())
                  .arg(window_->CurrentFrameForTest())
                  .arg(window_->CurrentPtsUsForTest())
                  .arg(record_after ? 1 : 0)
                  .arg(CacheStatsText(cache_after))
                  .arg(old_tier_kept ? 1 : 0)
                  .arg(OneLine(switch_notice)));
      a5_ok = triggered && landed && notice_ok && mark_pts_found &&
              record_after && cache_after.files >= cache_before.files &&
              old_tier_kept;
    } else {
      a5_ok = false;
    }
  }

  // 逐项结论（本次样本能覆盖的部分；其余由文档按多次运行合并）。
  const QString a1_value =
      a1_ok && quality ? QStringLiteral("pass")
                       : (quality ? QStringLiteral("partial")
                                  : QStringLiteral("fail"));
  PrintItem(QStringLiteral("A1"), a1_value,
            QStringLiteral("sample=%1 actual_qn=%2").arg(tag).arg(actual_qn));
  if (auto_mode) {
    QString a2_value;
    QString a2_detail;
    if (!has60) {
      a2_value = QStringLiteral("unverified");
      a2_detail = QStringLiteral("no_60fps_tier");
    } else if (!has60_avc) {
      a2_value = QStringLiteral("unverified");
      a2_detail = QStringLiteral("hevc_av1_only_60");
    } else if (a2_ok) {
      a2_value = QStringLiteral("pass");
    } else if (actual_qn == 116 || actual_qn == 74) {
      a2_value = QStringLiteral("partial");
    } else {
      a2_value = QStringLiteral("fail");
    }
    PrintItem(QStringLiteral("A2"), a2_value,
              a2_detail.isEmpty()
                  ? QStringLiteral("sample=%1 qn=%2 fps=%3 p95=%4")
                        .arg(tag)
                        .arg(actual_qn)
                        .arg(QString::number(actual_fps, 'f', 3))
                        .arg(p7_executed
                                 ? QString::number(
                                       Percentile(p7_audit.samples, 0.95), 'f', 1)
                                 : QStringLiteral("-"))
                  : a2_detail);
  } else {
    PrintItem(QStringLiteral("A2"), QStringLiteral("unverified"),
              QStringLiteral("manual_run"));
  }
  if (manual_60 || (auto_mode && has60_hevc_av1)) {
    const QString a3_value =
        a3_ok ? QStringLiteral("pass")
              : (actual_qn == options_.qn || (auto_mode && has60_hevc_av1)
                     ? QStringLiteral("partial")
                     : QStringLiteral("fail"));
    PrintItem(QStringLiteral("A3"), a3_value,
              QStringLiteral("sample=%1 mode=%2 qn=%3 codecid=%4 fps=%5")
                  .arg(tag)
                  .arg(auto_mode ? QStringLiteral("auto")
                                 : QStringLiteral("manual"))
                  .arg(actual_qn)
                  .arg(actual_codecid)
                  .arg(QString::number(actual_fps, 'f', 3)));
  } else {
    PrintItem(QStringLiteral("A3"), QStringLiteral("unverified"),
              QStringLiteral("no_hevc_av1_60_sample"));
  }
  if (manual_4k) {
    PrintItem(QStringLiteral("A4"), a4_ok ? QStringLiteral("pass")
                                          : QStringLiteral("partial"),
              QStringLiteral("sample=%1 qn=%2 fps=%3 steps_failures=%4 "
                             "op10_no_threshold")
                  .arg(tag)
                  .arg(actual_qn)
                  .arg(QString::number(actual_fps, 'f', 3))
                  .arg(p7_audit.failures));
  } else {
    PrintItem(QStringLiteral("A4"), has4k ? QStringLiteral("unverified")
                                          : QStringLiteral("unverified"),
              QStringLiteral("no_4k_run"));
  }
  PrintItem(QStringLiteral("A5"),
            a5_executed ? (a5_ok ? QStringLiteral("pass")
                                 : QStringLiteral("partial"))
                        : QStringLiteral("unverified"),
            a5_executed
                ? QStringLiteral("sample=%1 from_qn=%2 to_qn=%3")
                      .arg(tag)
                      .arg(actual_qn)
                      .arg(options_.switch_qn)
                : QStringLiteral("no_high_tier_switch"));
  PrintItem(QStringLiteral("A6"), QStringLiteral("report_only"),
            QStringLiteral("conditional_pass_partial_fail_unverified"));
  PrintItem(QStringLiteral("A7"), QStringLiteral("report_only"),
            QStringLiteral("m1_m2_unchanged"));

  window_->CancelOnlineLoadingForTest();
  std::printf(
      "T8.15 vip_ui end checks=%d failures=%d actual_qn=%d fps=%.3f "
      "p7_executed=%d\n",
      checks_, failures_, actual_qn, actual_fps, p7_executed ? 1 : 0);
  std::fflush(stdout);
  finish();
}

}  // namespace lapian::app
