#include "app/OnlineOfflineTestRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <thread>

#include "app/MainWindow.h"
#include "app/OnlineSession.h"
#include "online/CacheStore.h"
#include "online/IHttpClient.h"

namespace lapian::app {
namespace {

using lapian::online::CacheEntry;
using lapian::online::CacheKey;
using lapian::online::CacheStore;

void Print(const QString& text) {
  std::fprintf(stderr, "%s\n", text.toUtf8().constData());
  std::fflush(stderr);
}

bool WaitUntil(const std::function<bool()>& predicate, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    if (predicate()) {
      return true;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return predicate();
}

// 只在缓存命中时不应被调用；一旦被调用就说明又在走 view/playurl。
class CountingHttpClient final : public lapian::online::IHttpClient {
 public:
  explicit CountingHttpClient(std::shared_ptr<std::atomic<int>> calls)
      : calls_(std::move(calls)) {}

  lapian::online::HttpResponse Get(
      const lapian::online::HttpRequest&,
      const lapian::online::HttpCancelToken&) override {
    calls_->fetch_add(1);
    lapian::online::HttpResponse response;
    response.error = QStringLiteral("T8.7 offline test: HTTP 不应被调用");
    return response;
  }

 private:
  std::shared_ptr<std::atomic<int>> calls_;
};

bool CopyFile(const QString& source, const QString& target, QString* error) {
  QFile input(source);
  if (!input.open(QIODevice::ReadOnly)) {
    if (error != nullptr) {
      *error = QStringLiteral("无法读取样本：%1").arg(input.errorString());
    }
    return false;
  }
  QFile output(target);
  if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error != nullptr) {
      *error = QStringLiteral("无法写缓存：%1").arg(output.errorString());
    }
    return false;
  }
  const QByteArray bytes = input.readAll();
  if (input.error() != QFileDevice::NoError || output.write(bytes) != bytes.size() ||
      !output.flush()) {
    if (error != nullptr) {
      *error = QStringLiteral("写缓存不完整");
    }
    return false;
  }
  output.close();
  return true;
}

bool SeedCache(const QString& root, const QString& bvid, qint64 cid,
               const QString& media, QString* error) {
  const qint64 total = QFileInfo(media).size();
  if (total <= 0) {
    if (error != nullptr) {
      *error = QStringLiteral("样本文件为空或不存在");
    }
    return false;
  }
  QDir(root).removeRecursively();
  if (!QDir().mkpath(root)) {
    if (error != nullptr) {
      *error = QStringLiteral("无法创建缓存根");
    }
    return false;
  }
  CacheStore store(root);
  CacheKey key;
  key.bvid = bvid;
  key.cid = cid;
  key.qn = 64;
  key.codecid = 7;
  if (!store.Reserve(key, total, error)) {
    return false;
  }
  if (!CopyFile(media, store.PartPath(key), error)) {
    return false;
  }
  return store.MarkComplete(key, error);
}

bool ReadJson(const QString& path, QJsonObject* object, QString* error) {
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

OnlineOfflineTestRunner::OnlineOfflineTestRunner(
    MainWindow* window, OnlineOfflineTestOptions options)
    : window_(window), options_(std::move(options)) {}

void OnlineOfflineTestRunner::Start() {
  QTimer::singleShot(0, window_, [this]() {
    auto check = [this](const char* name, bool ok, const QString& detail = {}) {
      ++checks_;
      if (!ok) {
        ++failures_;
      }
      Print(QStringLiteral("online_offline_check %1 ok=%2%3")
                .arg(QString::fromUtf8(name))
                .arg(ok ? 1 : 0)
                .arg(detail.isEmpty() ? QString()
                                      : QStringLiteral(" detail=") + detail));
    };

    const QString bvid = options_.bvid;
    const qint64 cid = options_.cid;
    const QString media = QFileInfo(options_.media_path).absoluteFilePath();
    const QFileInfo media_info(media);
    if (!media_info.exists() || !media_info.isFile()) {
      Print(QStringLiteral("online_offline_fail reason=missing_media path=%1")
                .arg(media));
      QCoreApplication::exit(2);
      return;
    }
    const QString cache_root =
        options_.use_default_paths
            ? CacheStore::DefaultRoot()
            : (options_.cache_root.isEmpty()
                   ? QDir::currentPath() +
                         QStringLiteral("/build/t8.7-online-cache")
                   : options_.cache_root);
    const QString pictures_root =
        options_.use_default_paths
            ? options_.pictures_root
            : (options_.pictures_root.isEmpty()
                   ? QDir::currentPath() +
                         QStringLiteral("/build/t8.7-online-pictures")
                   : options_.pictures_root);
    const QString records_root =
        options_.use_default_paths
            ? QString()
            : (options_.records_root.isEmpty()
                   ? QDir::currentPath() +
                         QStringLiteral("/build/t8.7-online-records")
                   : options_.records_root);
    // 默认 AppData 验收会写/改该 BV 的在线记录；先备份用户原文件，结束/失败时恢复。
    QString default_record_path;
    QString default_record_backup;
    QString default_record_bak_backup;
    bool default_record_backed_up = false;
    bool default_record_bak_backed_up = false;
    auto restore_default_record = [&]() {
      if (!options_.use_default_paths || default_record_path.isEmpty()) {
        return;
      }
      QFile::remove(default_record_path);
      if (default_record_bak_backed_up) {
        QFile::remove(default_record_path + QStringLiteral(".bak"));
      }
      if (default_record_backed_up &&
          !QFile::rename(default_record_backup, default_record_path)) {
        Print(QStringLiteral(
                  "online_offline_warning reason=restore_record_failed path=%1")
                  .arg(default_record_backup));
      }
      if (default_record_bak_backed_up &&
          !QFile::rename(default_record_bak_backup,
                         default_record_path + QStringLiteral(".bak"))) {
        Print(QStringLiteral(
                  "online_offline_warning reason=restore_record_bak_failed path=%1")
                  .arg(default_record_bak_backup));
      }
    };
    if (!options_.use_default_paths) {
      QDir(pictures_root).removeRecursively();
      QDir(records_root).removeRecursively();
      QDir().mkpath(pictures_root);
      QDir().mkpath(records_root);
      window_->SetOnlineCacheRootForTest(cache_root);
      window_->SetOnlineRecordsRootForTest(records_root);
      window_->SetSnapshotPicturesRootForTest(pictures_root);
    } else {
      // 默认路径：不删除用户缓存；先备份该 BV 的在线记录，测试结束再恢复。
      if (!pictures_root.isEmpty()) {
        window_->SetSnapshotPicturesRootForTest(pictures_root);
      }
      const QString appdata = qEnvironmentVariable("APPDATA");
      if (!appdata.isEmpty()) {
        default_record_path = QDir(appdata).filePath(
            QStringLiteral("拉片软件/online/%1-%2.lapian.json")
                .arg(bvid)
                .arg(cid));
        default_record_backup = default_record_path + QStringLiteral(".t87-backup");
        default_record_bak_backup =
            default_record_path + QStringLiteral(".bak.t87-backup");
        QFile::remove(default_record_backup);
        QFile::remove(default_record_bak_backup);
        if (QFileInfo::exists(default_record_path)) {
          default_record_backed_up =
              QFile::rename(default_record_path, default_record_backup);
        }
        if (QFileInfo::exists(default_record_path + QStringLiteral(".bak"))) {
          default_record_bak_backed_up = QFile::rename(
              default_record_path + QStringLiteral(".bak"),
              default_record_bak_backup);
        }
      }
    }

    QString error;
    if (options_.use_default_paths && !options_.keep_cache) {
      restore_default_record();
      Print(QStringLiteral(
          "online_offline_fail reason=default_paths_requires_keep_cache"));
      QCoreApplication::exit(2);
      return;
    }
    if (!options_.keep_cache) {
      check("seed_cache", SeedCache(cache_root, bvid, cid, media, &error),
            error);
    }
    CacheStore store(cache_root);
    CacheKey key;
    key.bvid = bvid;
    key.cid = cid;
    key.qn = 64;
    key.codecid = 7;
    const QString mp4 = store.Mp4Path(key);
    check("data_mp4_complete", QFileInfo::exists(mp4), mp4);
    const qint64 mp4_size = QFileInfo(mp4).size();
    check("mp4_size_matches", mp4_size == media_info.size(),
          QStringLiteral("mp4=%1 media=%2").arg(mp4_size).arg(media_info.size()));

    auto http_calls = std::make_shared<std::atomic<int>>(0);
    auto inject_offline_http = [&]() {
      window_->SetOnlineHttpClientForTest(
          std::make_unique<CountingHttpClient>(http_calls));
    };
    OnlineRequest request;
    request.bvid = bvid;
    request.cid = cid;
    request.page = 1;
    request.title = QStringLiteral("T8.7 offline cache hit");
    request.duration_ms = 30000;

    // 1) 完整缓存命中：0 请求、首帧 ≤2.5s、不创建 .part。
    inject_offline_http();
    QElapsedTimer open_timer;
    open_timer.start();
    const bool started = window_->StartOnlineSession(request);
    const bool ready = window_->WaitForOnlineReadyForTest(options_.timeout_ms);
    const qint64 first_frame_ms = open_timer.elapsed();
    check("cache_hit_started", started, window_->OnlineLastErrorForTest());
    check("cache_hit_ready", ready, window_->OnlineLastErrorForTest());
    check("cache_hit_flag", window_->OnlineCacheHitForTest());
    check("cache_hit_first_frame_le_2500ms", first_frame_ms <= 2500,
          QStringLiteral("ms=%1").arg(first_frame_ms));
    check("cache_hit_no_playurl", window_->OnlinePlayUrlRequestsForTest() == 0,
          QStringLiteral("playurl=%1").arg(window_->OnlinePlayUrlRequestsForTest()));
    check("cache_hit_no_http", http_calls->load() == 0,
          QStringLiteral("http=%1").arg(http_calls->load()));
    check("cache_hit_no_part", !QFileInfo::exists(store.PartPath(key)));
    check("cache_hit_mp4_unchanged", QFileInfo(mp4).size() == mp4_size);
    Print(QStringLiteral(
              "online_offline_cache_hit first_frame_ms=%1 playurl=%2 http=%3 "
              "path=%4")
              .arg(first_frame_ms)
              .arg(window_->OnlinePlayUrlRequestsForTest())
              .arg(http_calls->load())
              .arg(window_->OnlineCachePathForTest()));
    if (!ready) {
      restore_default_record();
      Print(QStringLiteral("online_offline_result checks=%1 failures=%2 verdict=fail")
                .arg(checks_)
                .arg(failures_));
      QCoreApplication::exit(2);
      return;
    }

    // 2) 标记/备注/截图：帧 100，记录写 %APPDATA%（测试根）。
    QString snapshot_dir_path;
    const bool indexed =
        WaitUntil([&]() { return window_->IndexReadyForTest(); },
                  options_.timeout_ms);
    check("cache_index_ready", indexed,
          QStringLiteral("frames=%1").arg(window_->IndexFrameCountForTest()));
    if (indexed && window_->IndexFrameCountForTest() > 100) {
      window_->GoToFrameForTest(100);
      WaitUntil([&]() { return window_->CurrentFrameForTest() == 100; }, 3000);
      check("goto_100", window_->CurrentFrameForTest() == 100,
            QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
      window_->AddMarkForTest();
      check("mark_added", window_->MarkCountForTest() == 1,
            QStringLiteral("count=%1").arg(window_->MarkCountForTest()));
      if (window_->MarkCountForTest() == 1) {
        window_->SetMarkNoteForTest(0, QStringLiteral("卡点"),
                                    QStringLiteral("T8.7 note 100"));
        window_->CommitNoteForTest();
        check("mark_note", window_->MarkTextForTest(0).contains(
                               QStringLiteral("T8.7 note 100")),
              window_->MarkTextForTest(0));
      }
      window_->CaptureSnapshotForTest();
      window_->DrainSnapshotsForTest();
      const QStringList saved = window_->SnapshotSavedPathsForTest();
      check("snapshot_saved", !saved.isEmpty(), window_->SnapshotStatusForTest());
      if (!saved.isEmpty()) {
        const QString path = saved.last();
        snapshot_dir_path = QFileInfo(path).absolutePath();
        check("snapshot_name",
              QFileInfo(path).fileName().contains(
                  QStringLiteral("%1_f100_").arg(bvid)),
              path);
        check("snapshot_dir",
              QDir::fromNativeSeparators(path).contains(
                  QStringLiteral("/拉片软件/%1/").arg(bvid)),
              path);
        const QString index_path = window_->OnlineSnapshotIndexPathForTest();
        check("snapshot_index_exists", QFileInfo::exists(index_path), index_path);
        QFile index(index_path);
        if (index.open(QIODevice::ReadOnly)) {
          const QList<QByteArray> lines = index.readAll().split('\n');
          QJsonObject last;
          for (const QByteArray& line : lines) {
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (doc.isObject()) {
              last = doc.object();
            }
          }
          check("snapshot_index_fields",
                last.value(QStringLiteral("bvid")).toString() == bvid &&
                    last.value(QStringLiteral("frame")).toInt() == 100 &&
                    last.value(QStringLiteral("qn")).toInt() == 64 &&
                    last.value(QStringLiteral("codecid")).toInt() == 7 &&
                    last.value(QStringLiteral("fps")).toDouble() > 0.0 &&
                    last.value(QStringLiteral("pts")).toDouble() >= 0.0 &&
                    !last.value(QStringLiteral("path")).toString().isEmpty() &&
                    !last.value(QStringLiteral("saved_at")).toString().isEmpty(),
                QString::fromUtf8(QJsonDocument(last).toJson(QJsonDocument::Compact)));
        }
      }
    }

    const QString record_path = window_->OnlineRecordPathForTest();
    check("record_path", !record_path.isEmpty() &&
                             record_path.contains(QStringLiteral("%1-%2").arg(bvid).arg(cid)),
          record_path);
    check("record_file_exists", QFileInfo::exists(record_path), record_path);
    QJsonObject record;
    if (ReadJson(record_path, &record, &error)) {
      const QJsonArray marks = record.value(QStringLiteral("marks")).toArray();
      check("record_schema",
            record.value(QStringLiteral("schema")).toString() ==
                QStringLiteral("lapian-online-1"),
            record.value(QStringLiteral("schema")).toString());
      check("record_key", record.value(QStringLiteral("bvid")).toString() == bvid &&
                              static_cast<qint64>(
                                  record.value(QStringLiteral("cid")).toDouble()) == cid);
      check("record_mark_count", marks.size() == 1,
            QStringLiteral("count=%1").arg(marks.size()));
      if (!marks.isEmpty()) {
        const QJsonObject mark = marks.at(0).toObject();
        check("record_mark_fields",
              mark.value(QStringLiteral("frame")).toInt() == 100 &&
                  !mark.value(QStringLiteral("id")).toString().isEmpty() &&
                  mark.value(QStringLiteral("note")).toString() ==
                      QStringLiteral("T8.7 note 100") &&
                  mark.value(QStringLiteral("qn")).toInt() == 64 &&
                  mark.value(QStringLiteral("codecid")).toInt() == 7 &&
                  mark.value(QStringLiteral("fps")).toDouble() > 0.0,
              QString::fromUtf8(QJsonDocument(mark).toJson(QJsonDocument::Compact)));
      }
    } else {
      check("record_parse", false, error);
    }

    // 3) 关窗重开：标记/备注/截图恢复。
    window_->CloseForTest();
    inject_offline_http();
    const bool reopen_started = window_->StartOnlineSession(request);
    const bool reopen_ready =
        window_->WaitForOnlineReadyForTest(options_.timeout_ms);
    check("reopen_started", reopen_started);
    check("reopen_ready", reopen_ready, window_->OnlineLastErrorForTest());
    check("reopen_mark_count", window_->MarkCountForTest() == 1,
          QStringLiteral("count=%1").arg(window_->MarkCountForTest()));
    if (window_->MarkCountForTest() == 1) {
      check("reopen_mark_frame", window_->MarkFrameAtForTest(0) == 100,
            QStringLiteral("frame=%1").arg(window_->MarkFrameAtForTest(0)));
      check("reopen_mark_note",
            window_->MarkTextForTest(0).contains(QStringLiteral("T8.7 note 100")),
            window_->MarkTextForTest(0));
    }
    const QString snapshot_index_dir =
        snapshot_dir_path.isEmpty()
            ? QDir(pictures_root)
                  .filePath(QStringLiteral("拉片软件/%1").arg(bvid))
            : snapshot_dir_path;
    check("reopen_snapshot_exists",
          QFileInfo::exists(QDir(snapshot_index_dir).filePath(
              QStringLiteral("index.jsonl"))));
    window_->CloseForTest();

    // 默认 AppData 路径验收不清理默认缓存（会删用户数据）；命中/记录/重开已验证。
    if (options_.use_default_paths) {
      restore_default_record();
      Print(QStringLiteral(
                "online_offline_result checks=%1 failures=%2 verdict=%3")
                .arg(checks_)
                .arg(failures_)
                .arg(failures_ == 0 ? QStringLiteral("pass")
                                    : QStringLiteral("fail")));
      QCoreApplication::exit(failures_ == 0 ? 0 : 1);
      return;
    }

    // 4) 清缓存：记录/截图保留；再次打开会回到网络路径（注入客户端证明）。
    qint64 removed = 0;
    QString clear_error;
    check("clear_cache", store.ClearAll(&clear_error, &removed), clear_error);
    check("clear_removed", removed > 0, QStringLiteral("bytes=%1").arg(removed));
    check("clear_mp4_gone", !QFileInfo::exists(mp4));
    check("clear_record_kept", QFileInfo::exists(record_path));
    check("clear_snapshot_kept",
          QFileInfo::exists(QDir(snapshot_index_dir).filePath(
              QStringLiteral("index.jsonl"))));
    inject_offline_http();
    const bool miss_started = window_->StartOnlineSession(request);
    const bool miss_ready = window_->WaitForOnlineReadyForTest(2000);
    check("cache_miss_goes_network", miss_started && !miss_ready &&
                                         http_calls->load() > 0,
          QStringLiteral("started=%1 ready=%2 http=%3 error=%4")
              .arg(miss_started ? 1 : 0)
              .arg(miss_ready ? 1 : 0)
              .arg(http_calls->load())
              .arg(window_->OnlineLastErrorForTest()));

    // 5) 重建缓存后记录回访。
    if (window_->OnlineModeForTest() || window_->OnlineReadyForTest()) {
      window_->CloseForTest();
    }
    check("reseed_cache", SeedCache(cache_root, bvid, cid, media, &error), error);
    inject_offline_http();
    const bool rebuild_started = window_->StartOnlineSession(request);
    const bool rebuild_ready =
        window_->WaitForOnlineReadyForTest(options_.timeout_ms);
    check("rebuild_started", rebuild_started);
    check("rebuild_ready", rebuild_ready, window_->OnlineLastErrorForTest());
    check("rebuild_record_restored", window_->MarkCountForTest() == 1 &&
                                          window_->MarkFrameAtForTest(0) == 100,
          QStringLiteral("count=%1 frame=%2")
              .arg(window_->MarkCountForTest())
              .arg(window_->MarkFrameAtForTest(0)));
    window_->CloseForTest();

    // 6) 损坏记录：备份后按空记录继续播放，不阻塞首帧。
    if (!options_.skip_corrupt) {
      QFile corrupt(record_path);
      check("corrupt_write",
            corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                corrupt.write("{not-json") == 9LL && corrupt.flush(),
            corrupt.errorString());
      corrupt.close();
      inject_offline_http();
      const bool corrupt_started = window_->StartOnlineSession(request);
      const bool corrupt_ready =
          window_->WaitForOnlineReadyForTest(options_.timeout_ms);
      check("corrupt_record_not_blocking",
            corrupt_started && corrupt_ready && window_->MarkCountForTest() == 0,
            QStringLiteral("started=%1 ready=%2 marks=%3 error=%4")
                .arg(corrupt_started ? 1 : 0)
                .arg(corrupt_ready ? 1 : 0)
                .arg(window_->MarkCountForTest())
                .arg(window_->OnlineLastErrorForTest()));
      check("corrupt_record_backup",
            QFileInfo::exists(record_path + QStringLiteral(".bak")));
      window_->CloseForTest();
    }

    Print(QStringLiteral("online_offline_result checks=%1 failures=%2 verdict=%3")
              .arg(checks_)
              .arg(failures_)
              .arg(failures_ == 0 ? QStringLiteral("pass")
                                  : QStringLiteral("fail")));
    QCoreApplication::exit(failures_ == 0 ? 0 : 1);
  });
}

}  // namespace lapian::app
