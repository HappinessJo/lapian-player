// T8.7 缓存命中与在线记录单测（纯本地，不联网、不需要 GUI/SESSDATA）。
//
// 覆盖（对应 tasks-v2 T8.7 / delta O5-A2、O10）：
// - 完整缓存命中：OnlineSession 直接返回 data.mp4，不发起 playurl、不创建 .part；
// - 记录文件：bvid+cid → %APPDATA%\拉片软件\online\<bvid>-<cid>.lapian.json，
//   schema lapian-online-1，字段 id/frame/pts/fps/qn/codecid/label/note/时间；
// - 原子写、空记录删除、损坏备份且不阻塞；
// - 清空缓存不删除记录；重建缓存后记录可回访；
// - 截图目录/命名/`index.jsonl`（bvid/frame/fps/qn/codecid/pts/path/saved_at）。
//
// 退出码：0 = 全部通过；1 = 有失败。

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "app/OnlineSession.h"
#include "core/MarkStore.h"
#include "media/SnapshotNaming.h"
#include "online/CacheStore.h"
#include "online/PlayUrl.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const std::string& what, const std::string& detail = {}) {
  ++g_checks;
  if (condition) {
    std::printf("[ OK ] %s\n", what.c_str());
    return;
  }
  ++g_failures;
  std::printf("[FAIL] %s%s%s\n", what.c_str(), detail.empty() ? "" : "：",
              detail.c_str());
}

void Pump(int ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(5);
  }
}

bool WriteFile(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return file.write(bytes) == bytes.size() && file.flush();
}

bool ReadObject(const QString& path, QJsonObject* object, QString* error = nullptr) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    return false;
  }
  QJsonParseError parse_error;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    if (error != nullptr) {
      *error = parse_error.errorString();
    }
    return false;
  }
  *object = document.object();
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  QTemporaryDir temp;
  if (!temp.isValid()) {
    std::printf("[FAIL] 无法创建临时目录\n");
    return 1;
  }
  const QString root = temp.filePath(QStringLiteral("cache"));
  const QString bvid = QStringLiteral("BV1BMnv6jE5S");
  const qint64 cid = 42915922022;

  lapian::online::CacheStore store(root);
  lapian::online::CacheKey key;
  key.bvid = bvid;
  key.cid = cid;
  key.qn = 64;
  key.codecid = 7;
  constexpr qint64 kBytes = 4096;
  QString error;
  Check(store.Reserve(key, kBytes, &error), "reserve", error.toStdString());
  QByteArray payload(static_cast<int>(kBytes), 'x');
  Check(WriteFile(store.PartPath(key), payload), "write_part");
  Check(store.MarkComplete(key, &error), "mark_complete", error.toStdString());
  const QString mp4_path = store.Mp4Path(key);
  Check(QFileInfo::exists(mp4_path), "data_mp4_exists");

  // 模拟旧路径：注入 playurl + 同 key，当前实现会在 DownloadJob 发现 complete 后报
  // “缓存命中打开将在 T8.7 接入”。T8.7 应在 playurl/DownloadJob 之前直接命中。
  lapian::online::PlayUrlData playurl;
  playurl.quality = 64;
  playurl.codecid = 7;
  playurl.format = QStringLiteral("mp4");
  playurl.timelength_ms = 1000;
  lapian::online::DurlSegment segment;
  segment.url = QStringLiteral("https://example.invalid/t87.mp4");
  segment.size = kBytes;
  segment.length_ms = 1000;
  playurl.durls.push_back(segment);

  lapian::app::OnlineRequest request;
  request.bvid = bvid;
  request.cid = cid;
  request.page = 1;
  request.title = QStringLiteral("T8.7 cache hit");
  request.duration_ms = 1000;

  lapian::app::OnlineSession session(&store);
  session.SetPlayUrlDataForTest(playurl);
  bool ready = false;
  QString ready_path;
  bool error_called = false;
  session.SetCallbacks(
      [](lapian::app::OnlineState, const QString&) {},
      [](const lapian::app::OnlineProgress&) {},
      [&](const lapian::app::OnlineReadyInfo& info) {
        ready = true;
        ready_path = info.part_path;
      },
      [&](const QString&) { error_called = true; });

  QString start_error;
  const bool started = session.Start(request, &start_error);
  Pump(300);

  Check(started, "start", start_error.toStdString());
  Check(ready, "ready_callback",
        QStringLiteral("state=%1 error=%2")
            .arg(lapian::app::OnlineStateName(session.state()),
                 session.LastErrorForTest())
            .toStdString());
  Check(!error_called, "no_error_callback", session.LastErrorForTest().toStdString());
  Check(ready_path == mp4_path, "ready_path_is_data_mp4",
        ready_path.toStdString());
  Check(session.PlayUrlRequestsForTest() == 0, "no_playurl_request",
        QString::number(session.PlayUrlRequestsForTest()).toStdString());
  Check(!QFileInfo::exists(store.PartPath(key)), "no_part_created");
  Check(QFileInfo(store.Mp4Path(key)).size() == kBytes, "mp4_unchanged");

  session.Close();

  // -------------------------------------------------------------------------
  // 在线记录 lapian-online-1：bvid+cid、逐条 fps/qn/codecid、原子写/空删除/损坏备份
  // -------------------------------------------------------------------------
  const QString records_root = temp.filePath(QStringLiteral("records"));
  QDir().mkpath(records_root);
  lapian::core::MarkStoreOptions record_options;
  record_options.appdata_online_dir =
      QDir::toNativeSeparators(records_root).toUtf8().toStdString();
  const std::string bvid_std = bvid.toStdString();
  lapian::core::MarkStore records = lapian::core::MarkStore::OpenOnline(
      bvid_std, cid, 29.97, 64, 7, record_options);
  Check(records.valid() && records.online(), "online_store_valid");
  const QString record_path = QString::fromStdString(records.storage_path());
  Check(record_path.endsWith(QStringLiteral("%1-%2.lapian.json").arg(bvid).arg(cid)),
        "record_path_key", record_path.toStdString());
  Check(!QFileInfo::exists(record_path), "empty_record_no_file");

  std::string mark_id;
  Check(records.AddMark(100, 3333333, "卡点", "备注 A", &mark_id),
        "record_add_mark");
  const lapian::core::OnlineMarkMeta* meta = records.OnlineMetaForMark(mark_id);
  Check(meta != nullptr && meta->qn == 64 && meta->codecid == 7 &&
            meta->fps > 29.9 && meta->fps < 30.1,
        "record_mark_meta");
  const lapian::core::SaveResult save1 = records.Save();
  Check(save1.ok && save1.wrote, "record_save", save1.error);
  Check(QFileInfo::exists(record_path), "record_file_created");

  QJsonObject record_json;
  QString json_error;
  Check(ReadObject(record_path, &record_json, &json_error), "record_json",
        json_error.toStdString());
  Check(record_json.value(QStringLiteral("schema")).toString() ==
            QStringLiteral("lapian-online-1"),
        "record_schema");
  Check(record_json.value(QStringLiteral("bvid")).toString() == bvid &&
            static_cast<qint64>(
                record_json.value(QStringLiteral("cid")).toDouble()) == cid,
        "record_key");
  const QJsonArray record_marks =
      record_json.value(QStringLiteral("marks")).toArray();
  Check(record_marks.size() == 1, "record_one_mark");
  if (!record_marks.isEmpty()) {
    const QJsonObject mark = record_marks.at(0).toObject();
    Check(mark.value(QStringLiteral("frame")).toInt() == 100 &&
              mark.value(QStringLiteral("pts")).toDouble() == 3333333.0 &&
              mark.value(QStringLiteral("fps")).toDouble() > 29.9 &&
              mark.value(QStringLiteral("qn")).toInt() == 64 &&
              mark.value(QStringLiteral("codecid")).toInt() == 7 &&
              mark.value(QStringLiteral("label")).toString() ==
                  QStringLiteral("卡点") &&
              mark.value(QStringLiteral("note")).toString() ==
                  QStringLiteral("备注 A") &&
              !mark.value(QStringLiteral("created_at")).toString().isEmpty() &&
              !mark.value(QStringLiteral("updated_at")).toString().isEmpty(),
          "record_mark_fields");
  }

  // 换档：记录来自 29.97fps，当前 30fps → 提示并保留；标记仍按 bvid+cid。
  lapian::core::MarkStore reread = lapian::core::MarkStore::OpenOnline(
      bvid_std, cid, 30.0, 64, 7, record_options);
  Check(reread.marks().size() == 1 && reread.marks().front().frame == 100,
        "record_reload_mark");
  bool fps_warning = false;
  for (const lapian::core::MarkWarning& warning : reread.warnings()) {
    if (warning.kind == lapian::core::MarkWarningKind::kPtsMismatch) {
      fps_warning = true;
    }
  }
  Check(fps_warning, "record_fps_warning");
  Check(reread.SetMarkText(mark_id, "卡点2", "备注 B"), "record_edit_note");
  Check(reread.Save().ok, "record_edit_save");
  lapian::core::MarkStore reread2 = lapian::core::MarkStore::OpenOnline(
      bvid_std, cid, 30.0, 64, 7, record_options);
  Check(reread2.marks().size() == 1 &&
            reread2.marks().front().label == "卡点2" &&
            reread2.marks().front().note == "备注 B",
        "record_edit_reload");

  // 换档映射（O10-A5）：按 PTS 找当前流帧号，不静默复用旧帧号。
  lapian::core::FrameLookup remap_lookup;
  remap_lookup.frame_count = 300;
  remap_lookup.pts_of_frame = [](std::int64_t frame, std::int64_t* pts) {
    *pts = frame * 100000;  // 当前 10fps 测试时间轴
    return true;
  };
  remap_lookup.frame_of_pts = [](std::int64_t pts, std::int64_t* frame) {
    if (pts % 100000 != 0) {
      return false;
    }
    *frame = pts / 100000;
    return true;
  };
  lapian::core::MarkStore remap = lapian::core::MarkStore::OpenOnline(
      bvid_std, cid + 1, 30.0, 64, 7, record_options);
  std::string remap_id;
  Check(remap.AddMark(99, 1000000, "换档", "", &remap_id),
        "record_remap_add");
  remap.SetOnlineContext(10.0, 80, 12);
  bool remap_changed = false;
  remap.ReconcileFrames(remap_lookup, nullptr, &remap_changed);
  Check(remap_changed && remap.marks().size() == 1 &&
            remap.marks().front().frame == 10 &&
            remap.marks().front().pts_us == 1000000,
        "record_pts_remap_frame");
  const lapian::core::OnlineMarkMeta* remap_meta =
      remap.OnlineMetaForMark(remap_id);
  Check(remap_meta != nullptr && remap_meta->qn == 80 &&
            remap_meta->codecid == 12,
        "record_pts_remap_meta");

  // 清缓存保留在线记录（记录在缓存根外）。
  QString clear_error;
  qint64 removed_bytes = 0;
  Check(store.ClearAll(&clear_error, &removed_bytes), "clear_cache_ok",
        clear_error.toStdString());
  Check(QFileInfo::exists(record_path), "clear_cache_keeps_record");

  // 空记录删除文件（O10-A2）。
  Check(reread2.RemoveMark(mark_id), "record_remove_mark");
  const lapian::core::SaveResult remove_save = reread2.Save();
  Check(remove_save.ok && remove_save.removed && !QFileInfo::exists(record_path),
        "record_empty_file_removed", remove_save.error);

  // 损坏记录：备份 .bak、按空记录继续；不阻塞。
  lapian::core::MarkStore corrupt_source = lapian::core::MarkStore::OpenOnline(
      bvid_std, cid, 30.0, 64, 7, record_options);
  std::string corrupt_id;
  Check(corrupt_source.AddMark(7, 233333, "坏", "", &corrupt_id),
        "corrupt_add_mark");
  Check(corrupt_source.Save().ok, "corrupt_save");
  Check(QFileInfo::exists(record_path), "corrupt_file_exists");
  Check(WriteFile(record_path, QByteArray("{not-json")), "corrupt_write");
  lapian::core::MarkStore corrupt_read = lapian::core::MarkStore::OpenOnline(
      bvid_std, cid, 30.0, 64, 7, record_options);
  Check(corrupt_read.valid() && corrupt_read.marks().empty(),
        "corrupt_back_to_empty");
  Check(QFileInfo::exists(record_path + QStringLiteral(".bak")),
        "corrupt_backup_created");

  // 更高 schema：不覆盖。
  QJsonObject future;
  future.insert(QStringLiteral("schema"), QStringLiteral("lapian-online-1"));
  future.insert(QStringLiteral("schema_version"), 99);
  Check(WriteFile(record_path,
                  QJsonDocument(future).toJson(QJsonDocument::Compact)),
        "future_schema_write");
  lapian::core::MarkStore future_read = lapian::core::MarkStore::OpenOnline(
      bvid_std, cid, 30.0, 64, 7, record_options);
  const lapian::core::SaveResult future_save = future_read.Save();
  Check(!future_save.ok &&
            QFileInfo(record_path).size() ==
                QJsonDocument(future).toJson(QJsonDocument::Compact).size(),
        "future_schema_not_overwritten", future_save.error);

  // -------------------------------------------------------------------------
  // 在线截图：Pictures/拉片软件/<bvid>/、命名、index.jsonl 字段、AppData 回退
  // -------------------------------------------------------------------------
  const QString pictures_root = temp.filePath(QStringLiteral("pictures"));
  QDir().mkpath(pictures_root);
  const lapian::snapshot::OnlineSnapshotDirectory snap_dir =
      lapian::snapshot::ResolveOnlineSnapshotDirectory(
          bvid, pictures_root, records_root, &json_error);
  Check(!snap_dir.path.isEmpty() && !snap_dir.fallback,
        "online_snapshot_dir", snap_dir.path.toStdString());
  Check(QDir::fromNativeSeparators(snap_dir.path) ==
            QDir::fromNativeSeparators(
                QDir(pictures_root)
                    .filePath(QStringLiteral("拉片软件/%1").arg(bvid))),
        "online_snapshot_dir_layout");
  const QString base_name = lapian::snapshot::BuildOnlineSnapshotBaseName(
      bvid, 100, 30, 1);
  Check(base_name == QStringLiteral("%1_f100_00-00-03-10").arg(bvid),
        "online_snapshot_name", base_name.toStdString());
  const QString png_path = lapian::snapshot::SnapshotFilePath(snap_dir.path, base_name, 0);
  Check(WriteFile(png_path, QByteArray("not-a-real-png")), "snapshot_write");
  QString index_error;
  Check(lapian::snapshot::AppendOnlineSnapshotIndex(
            snap_dir.path, bvid, cid, 64, 7, 30.0, 100, 3333333, png_path,
            &index_error),
        "snapshot_index_append", index_error.toStdString());
  QFile index_file(QDir(snap_dir.path).filePath(QStringLiteral("index.jsonl")));
  Check(index_file.open(QIODevice::ReadOnly), "snapshot_index_open");
  QJsonObject index_line;
  for (const QByteArray& line : index_file.readAll().split('\n')) {
    const QJsonDocument document = QJsonDocument::fromJson(line);
    if (document.isObject()) {
      index_line = document.object();
    }
  }
  Check(index_line.value(QStringLiteral("bvid")).toString() == bvid &&
            static_cast<qint64>(
                index_line.value(QStringLiteral("cid")).toDouble()) == cid &&
            index_line.value(QStringLiteral("qn")).toInt() == 64 &&
            index_line.value(QStringLiteral("codecid")).toInt() == 7 &&
            index_line.value(QStringLiteral("fps")).toDouble() == 30.0 &&
            index_line.value(QStringLiteral("frame")).toInt() == 100 &&
            index_line.value(QStringLiteral("pts")).toDouble() == 3333333.0 &&
            QFileInfo(index_line.value(QStringLiteral("path")).toString())
                .absoluteFilePath() == QFileInfo(png_path).absoluteFilePath() &&
            !index_line.value(QStringLiteral("saved_at")).toString().isEmpty(),
        "snapshot_index_fields",
        QString::fromUtf8(QJsonDocument(index_line).toJson(QJsonDocument::Compact))
            .toStdString());

  const QString pictures_file = temp.filePath(QStringLiteral("pictures-is-file"));
  Check(WriteFile(pictures_file, QByteArray("x")), "pictures_file_write");
  const lapian::snapshot::OnlineSnapshotDirectory fallback_dir =
      lapian::snapshot::ResolveOnlineSnapshotDirectory(
          bvid, pictures_file, records_root, &json_error);
  Check(fallback_dir.fallback &&
            QDir::fromNativeSeparators(fallback_dir.path).startsWith(
                QDir::fromNativeSeparators(
                    QDir(records_root).filePath(QStringLiteral("snapshots")))),
        "online_snapshot_fallback", fallback_dir.path.toStdString());

  std::printf("online_records_result checks=%d failures=%d verdict=%s\n", g_checks,
              g_failures, g_failures == 0 ? "pass" : "fail");
  return g_failures == 0 ? 0 : 1;
}
