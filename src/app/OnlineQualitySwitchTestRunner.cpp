#include "app/OnlineQualitySwitchTestRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>
#include <utility>

#include "app/MainWindow.h"
#include "online/BiliAuth.h"
#include "online/BiliResolveClient.h"
#include "online/CredentialStore.h"
#include "online/Sessdata.h"
#include "online/WinHttpClient.h"

namespace lapian::app {
namespace {

using lapian::online::AuthResult;
using lapian::online::BiliResolveClient;
using lapian::online::BiliResolveResult;

void Print(const QString& text) {
  std::printf("%s\n", text.toUtf8().constData());
  std::fflush(stdout);
}

void PrintKV(const QString& key, const QString& value) {
  Print(QStringLiteral("%1=%2").arg(key, value));
}

QString OneLine(QString text) {
  text.replace(QLatin1Char('\n'), QStringLiteral(" | "));
  return text;
}

QString Num(qint64 value) { return QString::number(value); }

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
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  return predicate();
}

struct Credential {
  bool ok = false;
  QString source;
  QString sessdata;
  QString error;
};

Credential LoadCredential(const QString& explicit_file) {
  Credential result;
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
    const auto normalized =
        lapian::online::NormalizeSessdata(QString::fromUtf8(file.readAll()));
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
  // 默认 DPAPI 凭据（%APPDATA%\拉片软件\credentials.dat）。只读；不打印。
  lapian::online::CredentialStore store;
  const lapian::online::CredentialLoadResult loaded = store.Load();
  if (!loaded.ok) {
    result.error = loaded.missing
                       ? QStringLiteral("missing_default_credential")
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

struct CacheMetaRow {
  QString rel;
  QString kind;
  QString container;
  int qn = 0;
  int codecid = 0;
  int audio_id = 0;
  bool complete = false;
  qint64 bytes = 0;

  bool is_audio() const { return kind == QStringLiteral("audio"); }
};

QVector<CacheMetaRow> ScanCacheMeta(const QString& root, const QString& bvid,
                                    qint64 cid) {
  QVector<CacheMetaRow> rows;
  if (root.isEmpty() || !QDir(root).exists()) {
    return rows;
  }
  QDirIterator it(root, QStringList{QStringLiteral("meta.json")}, QDir::Files,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString path = it.next();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      continue;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
      continue;
    }
    const QJsonObject meta = document.object();
    if (meta.value(QStringLiteral("bvid")).toString() != bvid ||
        static_cast<qint64>(meta.value(QStringLiteral("cid")).toDouble()) != cid) {
      continue;
    }
    CacheMetaRow row;
    row.rel = QDir(root).relativeFilePath(QFileInfo(path).absolutePath());
    row.kind = meta.value(QStringLiteral("kind")).toString();
    row.container = meta.value(QStringLiteral("container")).toString();
    row.qn = static_cast<int>(meta.value(QStringLiteral("qn")).toDouble());
    row.codecid =
        static_cast<int>(meta.value(QStringLiteral("codecid")).toDouble());
    row.audio_id =
        static_cast<int>(meta.value(QStringLiteral("audio_id")).toDouble());
    row.complete = meta.value(QStringLiteral("complete")).toBool();
    row.bytes =
        static_cast<qint64>(meta.value(QStringLiteral("total_bytes")).toDouble());
    rows.push_back(row);
  }
  std::sort(rows.begin(), rows.end(),
            [](const CacheMetaRow& a, const CacheMetaRow& b) {
              return a.rel < b.rel;
            });
  return rows;
}

bool HasCompleteVideo(const QVector<CacheMetaRow>& rows, int qn, int codecid) {
  for (const CacheMetaRow& row : rows) {
    if (!row.is_audio() && row.complete && row.qn == qn &&
        row.codecid == codecid) {
      return true;
    }
  }
  return false;
}

bool HasVideoEntry(const QVector<CacheMetaRow>& rows, int qn, int codecid) {
  for (const CacheMetaRow& row : rows) {
    if (!row.is_audio() && row.qn == qn && row.codecid == codecid &&
        row.bytes > 0) {
      return true;
    }
  }
  return false;
}

bool HasCompleteAudio(const QVector<CacheMetaRow>& rows, int audio_id = 0) {
  for (const CacheMetaRow& row : rows) {
    if (row.is_audio() && row.complete &&
        (audio_id <= 0 || row.audio_id == audio_id)) {
      return true;
    }
  }
  return false;
}

bool HasAudioEntry(const QVector<CacheMetaRow>& rows, int audio_id = 0) {
  for (const CacheMetaRow& row : rows) {
    if (row.is_audio() && row.bytes > 0 &&
        (audio_id <= 0 || row.audio_id == audio_id)) {
      return true;
    }
  }
  return false;
}

int AnyAudioId(const QVector<CacheMetaRow>& rows) {
  for (const CacheMetaRow& row : rows) {
    if (row.is_audio()) {
      return row.audio_id;
    }
  }
  return 0;
}

}  // namespace

OnlineQualitySwitchUiTestRunner::OnlineQualitySwitchUiTestRunner(
    MainWindow* window, OnlineQualitySwitchTestOptions options)
    : window_(window), options_(std::move(options)) {}

void OnlineQualitySwitchUiTestRunner::Start() {
  QTimer::singleShot(0, window_, [this]() { Run(); });
}

void OnlineQualitySwitchUiTestRunner::Check(const QString& name, bool ok,
                                            const QString& detail) {
  ++checks_;
  if (!ok) {
    ++failures_;
  }
  Print(QStringLiteral("quality_switch_check %1 ok=%2%3")
            .arg(name)
            .arg(ok ? 1 : 0)
            .arg(detail.isEmpty() ? QString()
                                  : QStringLiteral(" detail=") + OneLine(detail)));
}

void OnlineQualitySwitchUiTestRunner::PrintCache(const QString& phase) {
  const QString root = window_->OnlineCacheRootForTest();
  const QVector<CacheMetaRow> rows =
      ScanCacheMeta(root, options_.bvid, options_.cid);
  Print(QStringLiteral("quality_switch_cache phase=%1 entries=%2")
            .arg(phase)
            .arg(rows.size()));
  for (const CacheMetaRow& row : rows) {
    Print(QStringLiteral("quality_switch_cache phase=%1 rel=%2 kind=%3 qn=%4 "
                         "codecid=%5 audio_id=%6 container=%7 complete=%8 "
                         "bytes=%9")
              .arg(phase, row.rel,
                   row.is_audio() ? QStringLiteral("audio")
                                  : QStringLiteral("video"))
              .arg(row.qn)
              .arg(row.codecid)
              .arg(row.audio_id)
              .arg(row.container.isEmpty() ? QStringLiteral("-") : row.container)
              .arg(row.complete ? 1 : 0)
              .arg(row.bytes));
  }
}

void OnlineQualitySwitchUiTestRunner::Finish() {
  if (finished_) {
    return;
  }
  finished_ = true;
  Print(QStringLiteral(
            "quality_switch_result checks=%1 failures=%2 verdict=%3")
            .arg(checks_)
            .arg(failures_)
            .arg(failures_ == 0 ? QStringLiteral("pass")
                                : QStringLiteral("fail")));
  QCoreApplication::exit(failures_ == 0 ? 0 : 1);
}

void OnlineQualitySwitchUiTestRunner::Run() {
  const QString tag =
      options_.tag.isEmpty() ? options_.bvid : options_.tag;
  Print(QStringLiteral(
            "T8.17 quality_switch begin tag=%1 bvid=%2 cid=%3 high_qn=%4 "
            "back_qn=%5")
            .arg(tag, options_.bvid)
            .arg(options_.cid)
            .arg(options_.high_qn)
            .arg(options_.back_qn));

  if (options_.bvid.isEmpty() || options_.cid <= 0) {
    PrintKV(QStringLiteral("quality_switch_block"),
            QStringLiteral("missing_bvid_or_cid"));
    PrintKV(QStringLiteral("quality_switch_verdict"),
            QStringLiteral("blocked"));
    QCoreApplication::exit(2);
    return;
  }

  Credential credential = LoadCredential(options_.sessdata_file);
  if (!credential.ok) {
    PrintKV(QStringLiteral("quality_switch_block"), credential.error);
    PrintKV(QStringLiteral("quality_switch_verdict"),
            QStringLiteral("blocked"));
    QCoreApplication::exit(2);
    return;
  }
  PrintKV(QStringLiteral("credential_source"), credential.source);

  lapian::online::WinHttpClient http;
  lapian::online::HttpCancelToken cancel;
  const AuthResult auth =
      lapian::online::CheckNav(credential.sessdata, &http, cancel, 4000);
  Check(QStringLiteral("nav_login"), auth.is_login, auth.message);
  Check(QStringLiteral("nav_vip_status_1"), auth.vip_status == 1,
        QStringLiteral("vip_status=%1").arg(auth.vip_status));
  if (!auth.is_login || auth.vip_status != 1) {
    lapian::online::ClearSecret(&credential.sessdata);
    PrintKV(QStringLiteral("quality_switch_block"),
            QStringLiteral("credential_not_vip"));
    PrintKV(QStringLiteral("quality_switch_verdict"),
            QStringLiteral("blocked"));
    QCoreApplication::exit(2);
    return;
  }

  BiliResolveClient resolver(&http);
  const BiliResolveResult resolved =
      resolver.Resolve(options_.bvid, cancel);
  Check(QStringLiteral("view_ok"), resolved.error.ok(), resolved.error.message);
  if (!resolved.error.ok()) {
    lapian::online::ClearSecret(&credential.sessdata);
    PrintKV(QStringLiteral("quality_switch_verdict"),
            QStringLiteral("blocked"));
    QCoreApplication::exit(2);
    return;
  }
  const QString title = resolved.video.title;
  const QString owner = resolved.video.owner;
  qint64 duration_ms = resolved.video.duration_seconds * 1000;
  for (const lapian::online::BiliPage& page : resolved.video.pages) {
    if (page.cid == options_.cid) {
      duration_ms = page.duration_seconds * 1000;
      break;
    }
  }

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
  request.cid = options_.cid;
  request.page = 1;
  request.title = title;
  request.owner = owner;
  request.duration_ms = duration_ms;
  request.sessdata = credential.sessdata;
  const bool started = window_->StartOnlineSession(request);
  lapian::online::ClearSecret(&credential.sessdata);
  Check(QStringLiteral("session_started"), started,
        window_->OnlineLastErrorForTest());
  if (!started) {
    Finish();
    return;
  }

  const bool ready = window_->WaitForOnlineReadyForTest(options_.timeout_ms);
  Check(QStringLiteral("ready"), ready,
        QStringLiteral("%1 | %2")
            .arg(window_->OnlineLastErrorForTest(),
                 window_->OnlineStateForTest()));
  if (!ready) {
    Finish();
    return;
  }
  const bool quality = window_->WaitForOnlineQualityForTest(options_.timeout_ms);
  Check(QStringLiteral("quality_catalog"), quality,
        window_->OnlineQualityErrorForTest());

  const int initial_qn = window_->OnlineQualityForTest();
  const int initial_codecid = window_->OnlineCodecidForTest();
  Print(QStringLiteral(
            "quality_switch_session phase=initial qn=%1 codecid=%2 fps=%3 "
            "cache_hit=%4 dash=%5 dash_requests=%6 playurl_requests=%7")
            .arg(initial_qn)
            .arg(initial_codecid)
            .arg(QString::number(window_->OnlineStreamFpsForTest(), 'f', 3))
            .arg(window_->OnlineCacheHitForTest() ? 1 : 0)
            .arg(window_->OnlineDashForTest() ? 1 : 0)
            .arg(window_->OnlineDashRequestsForTest())
            .arg(window_->OnlinePlayUrlRequestsForTest()));
  if (initial_qn != 80 || initial_codecid != 7) {
    PrintKV(QStringLiteral("quality_switch_block"),
            QStringLiteral("initial_not_1080p qn=%1 codecid=%2")
                .arg(initial_qn)
                .arg(initial_codecid));
    PrintKV(QStringLiteral("quality_switch_verdict"),
            QStringLiteral("blocked"));
    window_->ShutdownOnlineForTest();
    Finish();
    return;
  }
  const QString menu = window_->OnlineQualityMenuTextForTest();
  if (!menu.contains(QStringLiteral("qn=%1").arg(options_.high_qn))) {
    PrintKV(QStringLiteral("quality_switch_block"),
            QStringLiteral("high_tier_absent qn=%1").arg(options_.high_qn));
    PrintKV(QStringLiteral("quality_switch_verdict"),
            QStringLiteral("blocked"));
    window_->ShutdownOnlineForTest();
    Finish();
    return;
  }

  const bool cache_complete =
      window_->WaitForOnlineCacheCompleteForTest(options_.wait_complete_ms);
  Check(QStringLiteral("initial_cache_complete"), cache_complete,
        QStringLiteral("state=%1").arg(window_->OnlineStateForTest()));
  const bool cache_finalizable = WaitUntil(
      [this]() { return window_->OnlineCacheFinalizableForTest(); }, 5000);
  Check(QStringLiteral("initial_cache_finalizable"), cache_finalizable);
  PrintCache(QStringLiteral("initial"));
  const QVector<CacheMetaRow> initial_rows =
      ScanCacheMeta(window_->OnlineCacheRootForTest(), options_.bvid,
                    options_.cid);
  const int initial_audio_id = AnyAudioId(initial_rows);
  // 播放中两路仍是 `.part`，要等切档关闭 reader 后才 finalize；这里只验证
  // 实际键条目已存在且已完整发布（complete 由上面 progress/finalizable 断言）。
  Check(QStringLiteral("initial_video_80_present"),
        HasVideoEntry(initial_rows, 80, 7));
  Check(QStringLiteral("initial_audio_present"),
        HasAudioEntry(initial_rows, initial_audio_id),
        QStringLiteral("audio_id=%1").arg(initial_audio_id));

  const bool index_ready =
      window_->WaitForOnlineIndexFramesForTest(120, options_.timeout_ms);
  Check(QStringLiteral("index_before_switch"), index_ready,
        QStringLiteral("frames=%1")
            .arg(window_->OnlineIndexFramesForTest()));
  window_->GoToFrameForTest(30);
  const bool frame30 = WaitUntil(
      [this]() {
        return window_->OnlineSeekIdleForTest() &&
               qAbs(window_->CurrentFrameForTest() - 30) <= 2;
      },
      30000);
  Check(QStringLiteral("frame30_before_switch"), frame30,
        QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
  const qint64 old_pts = window_->CurrentPtsUsForTest();
  window_->AddMarkForTest();
  const int old_marks = window_->MarkCountForTest();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  const QString record_path = window_->OnlineRecordPathForTest();
  const bool record_before =
      !record_path.isEmpty() && QFileInfo::exists(record_path);
  Check(QStringLiteral("mark_before_switch"), old_marks >= 1,
        QStringLiteral("marks=%1").arg(old_marks));
  Check(QStringLiteral("record_before_switch"), record_before, record_path);
  window_->CaptureSnapshotForTest();
  const bool snapped = window_->DrainSnapshotsForTest();
  const QString snapshot_path = window_->SnapshotLastPathForTest();
  Check(QStringLiteral("snapshot_before_switch"),
        snapped && QFileInfo::exists(snapshot_path), snapshot_path);

  Print(QStringLiteral(
            "quality_switch_before qn=%1 codecid=%2 frame=%3 pts=%4 marks=%5 "
            "record=%6 snapshot=%7 cache_dash_requests=%8 cache_playurl=%9")
            .arg(initial_qn)
            .arg(initial_codecid)
            .arg(window_->CurrentFrameForTest())
            .arg(old_pts)
            .arg(old_marks)
            .arg(record_before ? 1 : 0)
            .arg(!snapshot_path.isEmpty() ? 1 : 0)
            .arg(window_->OnlineDashRequestsForTest())
            .arg(window_->OnlinePlayUrlRequestsForTest()));

  // ---------------------------------------------------------------- 80 -> 112
  QString status_one;
  const bool triggered_one =
      window_->TriggerOnlineQualityEntryForTest(options_.high_qn, &status_one);
  Check(QStringLiteral("switch_high_triggered"), triggered_one, status_one);
  const bool landed_one = window_->WaitForOnlineQualitySwitchForTest(
      options_.high_qn, std::max(120000, options_.timeout_ms * 2));
  switch_one_error_ = window_->OnlineLastErrorForTest();
  Check(QStringLiteral("switch_high_landed"), landed_one,
        QStringLiteral("%1 | state=%2 error=%3")
            .arg(window_->OnlineQualitySwitchStatusForTest(),
                 window_->OnlineStateForTest(), switch_one_error_));
  Print(QStringLiteral(
            "quality_switch_high landed=%1 qn=%2 codecid=%3 fps=%4 error=%5")
            .arg(landed_one ? 1 : 0)
            .arg(window_->OnlineQualityForTest())
            .arg(window_->OnlineCodecidForTest())
            .arg(QString::number(window_->OnlineStreamFpsForTest(), 'f', 3))
            .arg(OneLine(switch_one_error_)));
  if (!landed_one) {
    PrintCache(QStringLiteral("high_failed"));
    PrintKV(QStringLiteral("quality_switch_verdict"), QStringLiteral("fail"));
    window_->ShutdownOnlineForTest();
    Finish();
    return;
  }

  Check(QStringLiteral("switch_high_qn"),
        window_->OnlineQualityForTest() == options_.high_qn,
        QStringLiteral("actual=%1 expected=%2")
            .arg(window_->OnlineQualityForTest())
            .arg(options_.high_qn));
  Check(QStringLiteral("switch_high_codecid"),
        window_->OnlineCodecidForTest() == 7,
        QStringLiteral("actual=%1").arg(window_->OnlineCodecidForTest()));
  Check(QStringLiteral("switch_high_pts_mapped"),
        old_pts > 0 &&
            std::llabs(window_->CurrentPtsUsForTest() - old_pts) <= 100000,
        QStringLiteral("old=%1 new=%2")
            .arg(old_pts)
            .arg(window_->CurrentPtsUsForTest()));
  Check(QStringLiteral("switch_high_marks"),
        window_->MarkCountForTest() == old_marks,
        QStringLiteral("before=%1 after=%2")
            .arg(old_marks)
            .arg(window_->MarkCountForTest()));
  Check(QStringLiteral("switch_high_record_preserved"),
        !record_path.isEmpty() && QFileInfo::exists(record_path), record_path);
  Check(QStringLiteral("switch_high_snapshot_preserved"),
        !snapshot_path.isEmpty() && QFileInfo::exists(snapshot_path),
        snapshot_path);
  PrintCache(QStringLiteral("high"));

  const bool high_complete =
      window_->WaitForOnlineCacheCompleteForTest(options_.wait_complete_ms);
  const bool high_finalizable = WaitUntil(
      [this]() { return window_->OnlineCacheFinalizableForTest(); }, 5000);
  Check(QStringLiteral("high_cache_complete"), high_complete);
  Check(QStringLiteral("high_cache_finalizable"), high_finalizable);
  PrintCache(QStringLiteral("high_complete"));
  const QVector<CacheMetaRow> high_rows =
      ScanCacheMeta(window_->OnlineCacheRootForTest(), options_.bvid,
                    options_.cid);
  Check(QStringLiteral("high_video_present"),
        HasVideoEntry(high_rows, options_.high_qn, 7));
  Check(QStringLiteral("high_audio_complete"),
        HasCompleteAudio(high_rows, initial_audio_id));

  // ---------------------------------------------------------------- 112 -> 80
  QString status_two;
  const bool triggered_two =
      window_->TriggerOnlineQualityEntryForTest(options_.back_qn, &status_two);
  Check(QStringLiteral("switch_back_triggered"), triggered_two, status_two);
  const bool landed_two = window_->WaitForOnlineQualitySwitchForTest(
      options_.back_qn, std::max(120000, options_.timeout_ms * 2));
  switch_two_error_ = window_->OnlineLastErrorForTest();
  Check(QStringLiteral("switch_back_landed"), landed_two,
        QStringLiteral("%1 | state=%2 error=%3")
            .arg(window_->OnlineQualitySwitchStatusForTest(),
                 window_->OnlineStateForTest(), switch_two_error_));
  const int dash_after_back = window_->OnlineDashRequestsForTest();
  const int playurl_after_back = window_->OnlinePlayUrlRequestsForTest();
  Print(QStringLiteral(
            "quality_switch_back landed=%1 qn=%2 codecid=%3 fps=%4 "
            "cache_hit=%5 dash_requests=%6 playurl_requests=%7 error=%8")
            .arg(landed_two ? 1 : 0)
            .arg(window_->OnlineQualityForTest())
            .arg(window_->OnlineCodecidForTest())
            .arg(QString::number(window_->OnlineStreamFpsForTest(), 'f', 3))
            .arg(window_->OnlineCacheHitForTest() ? 1 : 0)
            .arg(dash_after_back)
            .arg(playurl_after_back)
            .arg(OneLine(switch_two_error_)));
  if (!landed_two) {
    PrintCache(QStringLiteral("back_failed"));
    PrintKV(QStringLiteral("quality_switch_verdict"), QStringLiteral("fail"));
    window_->ShutdownOnlineForTest();
    Finish();
    return;
  }

  Check(QStringLiteral("switch_back_qn"),
        window_->OnlineQualityForTest() == options_.back_qn,
        QStringLiteral("actual=%1 expected=%2")
            .arg(window_->OnlineQualityForTest())
            .arg(options_.back_qn));
  Check(QStringLiteral("switch_back_codecid"),
        window_->OnlineCodecidForTest() == 7,
        QStringLiteral("actual=%1").arg(window_->OnlineCodecidForTest()));
  Check(QStringLiteral("switch_back_cache_hit"),
        window_->OnlineCacheHitForTest());
  Check(QStringLiteral("switch_back_zero_dash_requests"),
        dash_after_back == 0, QStringLiteral("requests=%1").arg(dash_after_back));
  Check(QStringLiteral("switch_back_zero_playurl_requests"),
        playurl_after_back == 0,
        QStringLiteral("requests=%1").arg(playurl_after_back));
  Check(QStringLiteral("switch_back_pts_mapped"),
        old_pts > 0 &&
            std::llabs(window_->CurrentPtsUsForTest() - old_pts) <= 100000,
        QStringLiteral("old=%1 new=%2")
            .arg(old_pts)
            .arg(window_->CurrentPtsUsForTest()));
  Check(QStringLiteral("switch_back_marks"),
        window_->MarkCountForTest() == old_marks,
        QStringLiteral("before=%1 after=%2")
            .arg(old_marks)
            .arg(window_->MarkCountForTest()));
  Check(QStringLiteral("switch_back_record_preserved"),
        !record_path.isEmpty() && QFileInfo::exists(record_path), record_path);
  Check(QStringLiteral("switch_back_snapshot_preserved"),
        !snapshot_path.isEmpty() && QFileInfo::exists(snapshot_path),
        snapshot_path);
  PrintCache(QStringLiteral("back"));

  const QVector<CacheMetaRow> back_rows =
      ScanCacheMeta(window_->OnlineCacheRootForTest(), options_.bvid,
                    options_.cid);
  Check(QStringLiteral("back_video_80_complete"),
        HasCompleteVideo(back_rows, 80, 7));
  Check(QStringLiteral("back_audio_complete"),
        HasCompleteAudio(back_rows, initial_audio_id));
  Check(QStringLiteral("back_old_high_tier_kept"),
        HasCompleteVideo(back_rows, options_.high_qn, 7));
  Check(QStringLiteral("no_misleading_complete_error"),
        !switch_one_error_.contains(QStringLiteral("已完整缓存")) &&
            !switch_two_error_.contains(QStringLiteral("已完整缓存")),
        QStringLiteral("high=%1 back=%2")
            .arg(OneLine(switch_one_error_), OneLine(switch_two_error_)));

  Print(QStringLiteral(
            "quality_switch_after frame=%1 pts=%2 marks=%3 record=%4 "
            "snapshot=%5 cache_entries=%6")
            .arg(window_->CurrentFrameForTest())
            .arg(window_->CurrentPtsUsForTest())
            .arg(window_->MarkCountForTest())
            .arg(!record_path.isEmpty() && QFileInfo::exists(record_path) ? 1 : 0)
            .arg(!snapshot_path.isEmpty() && QFileInfo::exists(snapshot_path)
                     ? 1
                     : 0)
            .arg(back_rows.size()));
  window_->ShutdownOnlineForTest();
  Finish();
}

}  // namespace lapian::app
