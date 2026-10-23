#include "app/OnlineCacheManageTestRunner.h"

#include <QCoreApplication>
#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPushButton>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "app/CacheSettingsDialog.h"
#include "app/MainWindow.h"
#include "core/AppSettings.h"
#include "online/CacheManager.h"
#include "online/CacheStore.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>

namespace lapian::app {
namespace {

using lapian::core::AppSettings;
using lapian::core::SettingsStore;
using lapian::online::CacheEntry;
using lapian::online::CacheKey;
using lapian::online::CacheMaintenanceResult;
using lapian::online::CacheManager;
using lapian::online::CacheMetadata;
using lapian::online::CacheRecordsSummary;
using lapian::online::CacheStore;
using lapian::online::CacheSummary;
using lapian::online::CleanupOptions;

constexpr qint64 kKb = 1024;
constexpr qint64 kMb = 1024 * kKb;
constexpr qint64 kGb = 1024 * kMb;

int g_checks = 0;
int g_failures = 0;
QStringList g_failed;

bool Check(const QString& name, bool ok, const QString& detail = QString()) {
  ++g_checks;
  if (ok) {
    std::printf("[ OK ] T8.3.1 %s\n", name.toUtf8().constData());
  } else {
    ++g_failures;
    g_failed << name;
    std::printf("[FAIL] T8.3.1 %s%s%s\n", name.toUtf8().constData(),
                detail.isEmpty() ? "" : "：", detail.toUtf8().constData());
  }
  std::fflush(stdout);
  return ok;
}

void PrintKV(const char* key, const QString& value) {
  std::printf("%s=%s\n", key, value.toUtf8().constData());
}

void PrintKI(const char* key, qint64 value) {
  std::printf("%s=%lld\n", key, static_cast<long long>(value));
}

QString UniqueKey(const QString& seed, int index) {
  // 12 位合法 BV：BV + 1 + 9 位 [字母/数字]；index 10+ 用两位数字，
  // 对应减少一个字母，保持总长固定。
  const QString number = QString::number(index);
  const int digits =
      std::clamp(static_cast<int>(number.size()), 1, 9);
  return QStringLiteral("BV1") + seed.repeated(9 - digits) +
         number.rightJustified(digits, QLatin1Char('0'));
}

QString IntegrityLevel() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    return QStringLiteral("unknown");
  }
  DWORD length = 0;
  GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &length);
  std::vector<BYTE> buffer(length);
  QString result = QStringLiteral("unknown");
  if (length > 0 &&
      GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), length,
                          &length)) {
    auto* label = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer.data());
    PSID sid = label->Label.Sid;
    const DWORD count = *GetSidSubAuthorityCount(sid);
    const DWORD rid = *GetSidSubAuthority(sid, count - 1);
    if (rid < SECURITY_MANDATORY_LOW_RID) {
      result = QStringLiteral("untrusted");
    } else if (rid < SECURITY_MANDATORY_MEDIUM_RID) {
      result = QStringLiteral("low");
    } else if (rid < SECURITY_MANDATORY_HIGH_RID) {
      result = QStringLiteral("medium");
    } else if (rid < SECURITY_MANDATORY_SYSTEM_RID) {
      result = QStringLiteral("high");
    } else {
      result = QStringLiteral("system");
    }
  }
  CloseHandle(token);
  return result;
}

bool MarkSparse(const QString& path) {
  const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
  const std::wstring wide =
      (native.startsWith(QStringLiteral("\\\\?\\"))
           ? native
           : QStringLiteral("\\\\?\\") + native)
          .toStdWString();
  HANDLE handle = CreateFileW(wide.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD returned = 0;
  const BOOL ok = DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0,
                                  &returned, nullptr);
  CloseHandle(handle);
  return ok != 0;
}

bool CreateSizedFile(const QString& path, qint64 size) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  file.close();
  MarkSparse(path);  // 失败只影响物理占用，不影响逻辑大小
  if (!file.open(QIODevice::ReadWrite)) {
    return false;
  }
  const bool ok = file.resize(size);
  file.close();
  return ok;
}

bool SeedComplete(CacheStore* store, const CacheKey& key, qint64 bytes,
                  const QString& title) {
  QString error;
  if (!store->Reserve(key, bytes, &error)) {
    Check(QStringLiteral("setup-reserve-%1").arg(title), false, error);
    return false;
  }
  CacheMetadata metadata;
  metadata.title = title;
  metadata.owner = QStringLiteral("UP主");
  metadata.duration_ms = 111000;
  metadata.width = 1280;
  metadata.height = 720;
  metadata.fps = 30.0;
  if (!store->SetMetadata(key, metadata, &error)) {
    Check(QStringLiteral("setup-meta-%1").arg(title), false, error);
    return false;
  }
  if (!CreateSizedFile(store->PartPath(key), bytes)) {
    Check(QStringLiteral("setup-data-%1").arg(title), false);
    return false;
  }
  if (!store->MarkComplete(key, &error)) {
    Check(QStringLiteral("setup-complete-%1").arg(title), false, error);
    return false;
  }
  return true;
}

bool SeedIncomplete(CacheStore* store, const CacheKey& key, qint64 total_bytes,
                    qint64 downloaded_bytes) {
  QString error;
  if (!store->Reserve(key, total_bytes, &error)) {
    return false;
  }
  return CreateSizedFile(store->PartPath(key), downloaded_bytes);
}

bool PatchLastAccess(CacheStore* store, const CacheKey& key,
                     const QDateTime& last_access) {
  const QString path =
      QDir(store->EntryDir(key)).filePath(QStringLiteral("meta.json"));
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const QByteArray bytes = file.readAll();
  file.close();
  QJsonParseError parse_error{};
  const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse_error);
  if (!document.isObject()) {
    return false;
  }
  QJsonObject object = document.object();
  object.insert(QStringLiteral("last_access_at"),
                last_access.toUTC().toString(Qt::ISODateWithMs));
  QFile out(path);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  out.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
  out.close();
  return true;
}

QString WriteRecord(const QString& records_root, const QString& bvid, qint64 cid,
                    const QByteArray& content) {
  QDir().mkpath(records_root);
  const QString path = QDir(records_root)
                           .filePath(QStringLiteral("%1-%2.lapian.json")
                                         .arg(bvid)
                                         .arg(cid));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return {};
  }
  file.write(content);
  file.close();
  return path;
}

QByteArray RecordContent(const QString& bvid, qint64 cid) {
  return QByteArray("{\"schema\":\"lapian-online-1\",\"bvid\":\"") +
         bvid.toUtf8() + QByteArray("\",\"cid\":") + QByteArray::number(cid) +
         QByteArray(",\"marks\":[]}");
}

QDateTime AgoHours(int hours) {
  return QDateTime::currentDateTimeUtc().addSecs(-static_cast<qint64>(hours) * 3600);
}

int CountEntriesAtRoot(const QString& root) {
  CacheStore store(root);
  QVector<CacheEntry> entries;
  QVector<CacheEntry> broken;
  store.Scan(&entries, &broken, nullptr);
  return entries.size() + broken.size();
}

}  // namespace

int RunOnlineCacheManageTest(const OnlineCacheManageTestOptions& options) {
  QElapsedTimer total;
  total.start();
  const QString integrity = IntegrityLevel();
  PrintKV("integrity", integrity);
  if (options.default_paths && integrity != QStringLiteral("medium")) {
    std::printf(
        "[FAIL] T8.3.1 default-paths 需要 Medium integrity 副本，当前=%s\n",
        integrity.toUtf8().constData());
    std::printf("verdict=fail\n");
    return 2;
  }

  const QString app_dir = QCoreApplication::applicationDirPath();
  const qint64 pid = static_cast<qint64>(QCoreApplication::applicationPid());
  const QString local_app_data = qEnvironmentVariable("LOCALAPPDATA");
  const QString roaming_app_data = qEnvironmentVariable("APPDATA");
  const QString tag = QStringLiteral("t8.3.1-test-%1").arg(pid);

  QString scratch = options.root;
  if (scratch.isEmpty()) {
    scratch = QDir(app_dir).filePath(QStringLiteral("../t8.3.1-cache-manage-%1").arg(pid));
  }
  scratch = QDir::cleanPath(QFileInfo(scratch).absoluteFilePath());

  QString records_root = options.records_root;
  if (records_root.isEmpty()) {
    records_root = QDir(QFileInfo(scratch).absolutePath())
                       .filePath(QStringLiteral("t8.3.1-records-%1").arg(pid));
  }
  records_root = QDir::cleanPath(QFileInfo(records_root).absoluteFilePath());

  QString settings_root = options.settings_root;
  if (settings_root.isEmpty()) {
    settings_root = QDir(QFileInfo(scratch).absolutePath())
                        .filePath(QStringLiteral("t8.3.1-settings-%1").arg(pid));
  }
  settings_root = QDir::cleanPath(QFileInfo(settings_root).absoluteFilePath());

  QString root = scratch;
  if (options.default_paths) {
    root = QDir(local_app_data)
               .filePath(QStringLiteral("拉片软件/cache-%1").arg(tag));
    records_root = QDir(roaming_app_data)
                       .filePath(QStringLiteral("拉片软件/online-%1").arg(tag));
    settings_root = QDir(roaming_app_data)
                        .filePath(QStringLiteral("拉片软件/settings-%1").arg(tag));
  }
  root = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
  records_root = QDir::cleanPath(QFileInfo(records_root).absoluteFilePath());
  settings_root = QDir::cleanPath(QFileInfo(settings_root).absoluteFilePath());

  // 1) 默认根/记录根必须是 AppData，不能是工作区。真实默认根只读扫描。
  const QString default_root = CacheStore::DefaultRoot();
  const QString default_records_root = CacheManager::DefaultRecordsRoot();
  const QString sample_local = QStringLiteral("C:/Users/x/AppData/Local");
  const QString sample_roaming = QStringLiteral("C:/Users/x/AppData/Roaming");
  const QString sample_root = CacheStore::DefaultRootFor(sample_local);
  const QString sample_records = CacheManager::DefaultRecordsRootFor(sample_roaming);
  Check(QStringLiteral("default-root-exact"),
        QDir::fromNativeSeparators(sample_root) ==
            QDir::fromNativeSeparators(sample_local +
                                       QStringLiteral("/拉片软件/cache")),
        sample_root);
  Check(QStringLiteral("default-records-exact"),
        QDir::fromNativeSeparators(sample_records) ==
            QDir::fromNativeSeparators(sample_roaming +
                                       QStringLiteral("/拉片软件/online")),
        sample_records);
  Check(QStringLiteral("default-root-under-localappdata"),
        CacheManager::IsPathInside(default_root, local_app_data), default_root);
  Check(QStringLiteral("default-records-under-appdata"),
        CacheManager::IsPathInside(default_records_root, roaming_app_data),
        default_records_root);
  Check(QStringLiteral("default-root-not-workspace"),
        !CacheManager::IsPathInside(default_root, QDir::currentPath()),
        default_root);
  Check(QStringLiteral("default-records-not-workspace"),
        !CacheManager::IsPathInside(default_records_root, QDir::currentPath()),
        default_records_root);
  CacheStore default_read_store;
  QVector<CacheEntry> default_entries;
  QVector<CacheEntry> default_broken;
  default_read_store.Scan(&default_entries, &default_broken, nullptr);
  CacheManager default_read_manager(&default_read_store, default_records_root);
  const CacheRecordsSummary default_records = default_read_manager.Records();
  PrintKV("default_cache_root", default_root);
  PrintKV("default_records_root", default_records_root);
  PrintKI("default_cache_entries", default_entries.size() + default_broken.size());
  PrintKI("default_cache_usage", default_read_store.UsageBytes());
  PrintKI("default_records_count", default_records.count);
  PrintKV("default_paths", options.default_paths ? QStringLiteral("appdata-test-subdir")
                                                 : QStringLiteral("temp"));

  if (options.default_paths) {
    Check(QStringLiteral("appdata-test-root-under-localappdata"),
          CacheManager::IsPathInside(root, local_app_data), root);
    Check(QStringLiteral("appdata-test-records-under-appdata"),
          CacheManager::IsPathInside(records_root, roaming_app_data),
          records_root);
    Check(QStringLiteral("appdata-test-root-not-default"),
          root.compare(default_root, Qt::CaseInsensitive) != 0, root);
  }

  // 临时根清理（只删自己的唯一目录；默认 AppData 测试子目录也带 pid）。
  QDir(scratch).removeRecursively();
  QDir(records_root).removeRecursively();
  QDir(settings_root).removeRecursively();
  if (!QDir().mkpath(scratch)) {
    std::printf("[FAIL] T8.3.1 临时根无法创建：%s\n", scratch.toUtf8().constData());
    std::printf("verdict=fail\n");
    return 2;
  }
  QDir().mkpath(records_root);

  CacheStore store(root, 300 * kMb);
  CacheManager manager(&store, records_root);
  QString error;
  Check(QStringLiteral("apply-root"), manager.ApplyRootAndQuota(root, 300 * kMb, &error),
        error);
  Check(QStringLiteral("apply-quota"), store.quota() == 300 * kMb);

  const QString record_file = WriteRecord(
      records_root, UniqueKey(QStringLiteral("r"), 1), 101,
      RecordContent(UniqueKey(QStringLiteral("r"), 1), 101));
  Check(QStringLiteral("record-sentinel-created"), !record_file.isEmpty());
  // 在线截图在缓存根外（Pictures\拉片软件），清空缓存不得碰；这里用同目录
  // 结构的 sentinel PNG 验证“只删缓存布局内的数据”。
  const QString pictures_root = QDir(scratch).filePath(QStringLiteral("pictures"));
  const QString shot_file =
      QDir(pictures_root).filePath(QStringLiteral("shot-sentinel.png"));
  QDir().mkpath(pictures_root);
  {
    QFile shot(shot_file);
    Check(QStringLiteral("shot-sentinel-created"),
          shot.open(QIODevice::WriteOnly) && shot.write("png") == 3);
  }
  const QByteArray record_before =
      [&] {
        QFile file(record_file);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
      }();
  const int records_before = manager.Records().count;

  const CacheKey complete_a{UniqueKey(QStringLiteral("a"), 1), 101, 64, 7};
  const CacheKey complete_b{UniqueKey(QStringLiteral("b"), 2), 102, 80, 12};
  const CacheKey broken_key{UniqueKey(QStringLiteral("c"), 3), 103, 64, 7};
  const CacheKey incomplete_key{UniqueKey(QStringLiteral("d"), 4), 104, 64, 7};
  const CacheKey orphan_part{UniqueKey(QStringLiteral("e"), 5), 105, 64, 7};
  Check(QStringLiteral("seed-a"), SeedComplete(&store, complete_a, 2 * kMb,
                                               QStringLiteral("A")));
  Check(QStringLiteral("seed-b"), SeedComplete(&store, complete_b, 3 * kMb,
                                               QStringLiteral("B")));
  Check(QStringLiteral("seed-broken"),
        SeedComplete(&store, broken_key, 1 * kMb, QStringLiteral("C")));
  Check(QStringLiteral("seed-incomplete"),
        SeedIncomplete(&store, incomplete_key, 4 * kMb, 1 * kMb));
  Check(QStringLiteral("seed-orphan-part"),
        CreateSizedFile(store.PartPath(orphan_part), 64 * kKb));
  {
    QFile file(QDir(root).filePath(QStringLiteral("readme.txt")));
    Check(QStringLiteral("seed-foreign"),
          file.open(QIODevice::WriteOnly) && file.write("keep") > 0);
  }
  {
    QFile file(store.Mp4Path(broken_key));
    Check(QStringLiteral("break-meta-size"),
          file.open(QIODevice::ReadWrite) && file.resize(512 * kKb));
  }

  const CacheSummary summary = manager.Summary();
  Check(QStringLiteral("summary-root-exists"), summary.root_exists);
  Check(QStringLiteral("summary-root-writable"), summary.root_writable,
        summary.root_error);
  Check(QStringLiteral("summary-usage-positive"), summary.usage_bytes > 0);
  Check(QStringLiteral("summary-entries"),
        summary.entries >= 4 && summary.complete_entries >= 2 &&
            summary.incomplete_entries >= 1 && summary.broken_entries >= 1,
        QStringLiteral("entries=%1 complete=%2 incomplete=%3 broken=%4")
            .arg(summary.entries)
            .arg(summary.complete_entries)
            .arg(summary.incomplete_entries)
            .arg(summary.broken_entries));
  Check(QStringLiteral("summary-free-disk"), summary.free_bytes >= 0);
  Check(QStringLiteral("summary-records"), manager.Records().count == records_before);
  PrintKI("summary_usage", summary.usage_bytes);
  PrintKI("summary_entries", summary.entries);
  PrintKI("summary_broken", summary.broken_entries);

  // 2) 清空：当前会话（部分键：只有 bvid+cid）保留；其它条目/损坏/孤儿删除；
  // 在线记录在缓存根外，逐字节不变。
  CacheKey partial_current = incomplete_key;
  partial_current.qn = 0;
  partial_current.codecid = 0;
  qint64 cleared = 0;
  int kept = 0;
  Check(QStringLiteral("clear-with-current-ok"),
        manager.ClearAll(&partial_current, &error, &cleared, &kept), error);
  Check(QStringLiteral("clear-keeps-current"), kept == 1 && store.Load(incomplete_key).has_value(),
        QStringLiteral("kept=%1").arg(kept));
  Check(QStringLiteral("clear-removes-other-complete"),
        !store.Load(complete_a).has_value() && !store.Load(complete_b).has_value());
  Check(QStringLiteral("clear-removes-broken"), !store.Load(broken_key).has_value());
  Check(QStringLiteral("clear-removes-orphan"),
        !QFileInfo::exists(store.EntryDir(orphan_part)));
  Check(QStringLiteral("clear-usage-current-only"),
        store.UsageBytes() > 0 && store.UsageBytes() < summary.usage_bytes);
  Check(QStringLiteral("clear-records-kept"),
        QFileInfo::exists(record_file) && manager.Records().count == records_before);
  {
    QFile file(record_file);
    const QByteArray after = file.open(QIODevice::ReadOnly) ? file.readAll()
                                                            : QByteArray();
    Check(QStringLiteral("clear-records-unchanged"), after == record_before);
  }
  PrintKI("clear_removed_bytes", cleared);
  PrintKI("clear_kept_entries", kept);

  Check(QStringLiteral("clear-all-ok"),
        manager.ClearAll(nullptr, &error, &cleared, nullptr), error);
  Check(QStringLiteral("clear-all-usage-zero"), store.UsageBytes() == 0);
  Check(QStringLiteral("clear-all-root-kept"), QFileInfo(root).isDir());
  Check(QStringLiteral("clear-all-foreign-kept"),
        QFileInfo::exists(QDir(root).filePath(QStringLiteral("readme.txt"))));
  Check(QStringLiteral("clear-all-records-kept"),
        QFileInfo::exists(record_file) && manager.Records().count == records_before);
  Check(QStringLiteral("clear-all-shots-kept"), QFileInfo::exists(shot_file));
  PrintKI("clear_all_removed_bytes", cleared);
  PrintKV("records_kept", QFileInfo::exists(record_file) ? QStringLiteral("1")
                                                         : QStringLiteral("0"));
  PrintKV("shots_kept", QFileInfo::exists(shot_file) ? QStringLiteral("1")
                                                     : QStringLiteral("0"));

  // 3) 孤儿/过期清理：当前会话（部分键）48h 也不删；25h 未完成、无 meta 孤儿删除；
  // 23h 未完成与完成条目保留。
  const CacheKey current_f{UniqueKey(QStringLiteral("f"), 6), 106, 64, 7};
  const CacheKey stale_g{UniqueKey(QStringLiteral("g"), 7), 107, 64, 7};
  const CacheKey fresh_h{UniqueKey(QStringLiteral("h"), 8), 108, 64, 7};
  const CacheKey complete_i{UniqueKey(QStringLiteral("i"), 9), 109, 64, 7};
  const CacheKey orphan_j{UniqueKey(QStringLiteral("j"), 10), 110, 64, 7};
  Check(QStringLiteral("cleanup-seed-current"),
        SeedIncomplete(&store, current_f, 2 * kMb, 1 * kKb));
  Check(QStringLiteral("cleanup-seed-stale"),
        SeedIncomplete(&store, stale_g, 2 * kMb, 1 * kKb));
  Check(QStringLiteral("cleanup-seed-fresh"),
        SeedIncomplete(&store, fresh_h, 2 * kMb, 1 * kKb));
  Check(QStringLiteral("cleanup-seed-complete"),
        SeedComplete(&store, complete_i, 1 * kMb, QStringLiteral("I")));
  Check(QStringLiteral("cleanup-seed-orphan"),
        CreateSizedFile(store.PartPath(orphan_j), 4 * kKb));
  Check(QStringLiteral("cleanup-patch-current"),
        PatchLastAccess(&store, current_f, AgoHours(48)));
  Check(QStringLiteral("cleanup-patch-stale"),
        PatchLastAccess(&store, stale_g, AgoHours(25)));
  Check(QStringLiteral("cleanup-patch-fresh"),
        PatchLastAccess(&store, fresh_h, AgoHours(23)));
  const QByteArray cleanup_record = RecordContent(current_f.bvid, current_f.cid);
  const QString cleanup_record_file =
      WriteRecord(records_root, current_f.bvid, current_f.cid, cleanup_record);
  const int records_cleanup_before = manager.Records().count;
  CacheKey partial_f = current_f;
  partial_f.qn = 0;
  partial_f.codecid = 0;
  const CacheMaintenanceResult cleanup =
      manager.CleanupOrphans(&partial_f, CleanupOptions{}, false);
  Check(QStringLiteral("cleanup-ok"), cleanup.ok, cleanup.error);
  Check(QStringLiteral("cleanup-current-kept"),
        store.Load(current_f).has_value() &&
            QFileInfo::exists(store.PartPath(current_f)));
  Check(QStringLiteral("cleanup-stale-removed"),
        !QFileInfo::exists(store.EntryDir(stale_g)));
  Check(QStringLiteral("cleanup-fresh-kept"),
        QFileInfo::exists(store.PartPath(fresh_h)));
  Check(QStringLiteral("cleanup-complete-kept"),
        QFileInfo::exists(store.Mp4Path(complete_i)));
  Check(QStringLiteral("cleanup-orphan-removed"),
        !QFileInfo::exists(store.EntryDir(orphan_j)));
  Check(QStringLiteral("cleanup-records-kept"),
        QFileInfo::exists(cleanup_record_file) &&
            manager.Records().count == records_cleanup_before);
  Check(QStringLiteral("cleanup-shots-kept"), QFileInfo::exists(shot_file));
  PrintKI("cleanup_removed_bytes", cleanup.removed_bytes);
  PrintKI("cleanup_removed_entries", cleanup.removed_entries);
  PrintKI("cleanup_usage_after", cleanup.usage_after);
  PrintKV("cleanup_current_protected",
          cleanup.current_protected ? QStringLiteral("1") : QStringLiteral("0"));

  // 4) 非法/只读路径：可读错误，且不改变当前根。
  const QString file_root = QDir(scratch).filePath(QStringLiteral("not-a-dir"));
  {
    QFile file(file_root);
    Check(QStringLiteral("invalid-root-create-file"),
          file.open(QIODevice::WriteOnly) && file.write("x") > 0);
  }
  {
    CacheStore invalid(file_root);
    QString invalid_error;
    Check(QStringLiteral("invalid-root-set-root-fails"),
          !invalid.SetRoot(file_root, &invalid_error));
    Check(QStringLiteral("invalid-root-error-readable"),
          invalid_error.contains(QStringLiteral("不是目录")) ||
              invalid_error.contains(QStringLiteral("无法创建")),
          invalid_error);
    CacheManager invalid_manager(&store, records_root);
    QString apply_error;
    Check(QStringLiteral("invalid-root-apply-fails"),
          !invalid_manager.ApplyRootAndQuota(file_root, 300 * kMb, &apply_error));
    Check(QStringLiteral("invalid-root-keeps-current"),
          store.root().compare(root, Qt::CaseInsensitive) == 0, store.root());
  }
  QString readonly_state = QStringLiteral("not-run");
  if (!options.readonly_root.isEmpty()) {
    const QString readonly_root = QDir::cleanPath(
        QFileInfo(options.readonly_root).absoluteFilePath());
    CacheStore readonly_store(readonly_root, 300 * kMb);
    QString readonly_error;
    const bool readonly_set = readonly_store.SetRoot(readonly_root, &readonly_error);
    Check(QStringLiteral("readonly-set-root-fails"), !readonly_set, readonly_error);
    Check(QStringLiteral("readonly-error-readable"),
          readonly_error.contains(QStringLiteral("不可写")) ||
              readonly_error.contains(QStringLiteral("只读")) ||
              readonly_error.contains(QStringLiteral("权限")),
          readonly_error);
    CacheManager readonly_manager(&store, records_root);
    QString readonly_apply_error;
    Check(QStringLiteral("readonly-apply-fails"),
          !readonly_manager.ApplyRootAndQuota(readonly_root, 300 * kMb,
                                              &readonly_apply_error));
    readonly_state = (!readonly_set &&
                      (readonly_error.contains(QStringLiteral("不可写")) ||
                       readonly_error.contains(QStringLiteral("只读")) ||
                       readonly_error.contains(QStringLiteral("权限"))))
                         ? QStringLiteral("pass")
                         : QStringLiteral("fail");
    PrintKV("readonly_error", readonly_error);
  }
  PrintKV("readonly", readonly_state);

  // 5) 设置跨会话 + 改目录不迁移旧缓存。
  const QString root2 = options.default_paths
                            ? QDir(local_app_data)
                                  .filePath(QStringLiteral("拉片软件/cache-%1-2").arg(tag))
                            : QDir(scratch).filePath(QStringLiteral("cache-2"));
  {
    SettingsStore settings(settings_root);
    AppSettings values;
    values.cache_root = root2;
    values.cache_quota_gb = 7;
    Check(QStringLiteral("settings-save-cross-session"), settings.Save(values));
  }
  bool settings_ok = false;
  qint64 quota2 = 0;
  QString loaded_root;
  {
    SettingsStore settings(settings_root);
    const lapian::core::SettingsLoadResult loaded = settings.Load();
    loaded_root = loaded.settings.cache_root;
    quota2 = static_cast<qint64>(loaded.settings.cache_quota_gb) * kGb;
    settings_ok = loaded.status == lapian::core::SettingsLoadStatus::kLoaded &&
                  loaded.settings.cache_root == root2 &&
                  loaded.settings.cache_quota_gb == 7;
  }
  Check(QStringLiteral("settings-load-cross-session"), settings_ok, loaded_root);
  const qint64 old_usage = store.UsageBytes();
  Check(QStringLiteral("root-switch-apply"),
        manager.ApplyRootAndQuota(loaded_root, quota2, &error), error);
  Check(QStringLiteral("root-switch-store-root"),
        store.root().compare(root2, Qt::CaseInsensitive) == 0, store.root());
  Check(QStringLiteral("root-switch-old-cache-preserved"),
        CountEntriesAtRoot(root) > 0 && old_usage > 0,
        QStringLiteral("old_usage=%1").arg(old_usage));
  Check(QStringLiteral("root-switch-new-root-empty"), store.UsageBytes() == 0);
  const CacheKey cross_key{UniqueKey(QStringLiteral("k"), 11), 111, 64, 7};
  Check(QStringLiteral("cross-session-seed"),
        SeedComplete(&store, cross_key, 1 * kMb, QStringLiteral("K")));
  Check(QStringLiteral("cross-session-usage"), store.UsageBytes() > 0);
  {
    CacheStore reopened(root2, quota2);
    Check(QStringLiteral("cross-session-reopen-quota"), reopened.quota() == quota2);
    Check(QStringLiteral("cross-session-reopen-usage"),
          reopened.UsageBytes() == store.UsageBytes());
    Check(QStringLiteral("cross-session-summary-quota"),
          manager.Summary().quota_bytes == quota2);
  }
  PrintKV("settings_cross_session", settings_ok ? QStringLiteral("pass")
                                                 : QStringLiteral("fail"));
  PrintKV("root_switch_no_migration",
          CountEntriesAtRoot(root) > 0 ? QStringLiteral("pass")
                                       : QStringLiteral("fail"));
  PrintKI("quota_cross_session_bytes", quota2);

  // 6) 磁盘满：不可满足的 expected 走同一预检路径（不是真写满磁盘）。
  const CacheKey huge{UniqueKey(QStringLiteral("l"), 12), 112, 64, 7};
  QElapsedTimer space_timer;
  space_timer.start();
  const bool huge_reserved = store.Reserve(huge, 2'000'000'000'000'000LL, &error);
  const double space_ms = static_cast<double>(space_timer.nsecsElapsed()) / 1.0e6;
  Check(QStringLiteral("disk-full-blocked"), !huge_reserved);
  Check(QStringLiteral("disk-full-readable"),
        error.startsWith(QStringLiteral("磁盘空间不足：需要")) &&
            error.contains(QStringLiteral("可用")),
        error);
  Check(QStringLiteral("disk-full-under-5s"), space_ms <= 5000.0);
  PrintKV("disk_full_error", error);
  std::printf("space_error_ms=%.3f\n", space_ms);

  // 7) UI smoke：设置对话框状态 + 清空；MainWindow 的启动/定期清理入口。
  QString ui_state = QStringLiteral("skip");
  if (options.ui_smoke) {
    QString ui_error;
    CacheManager ui_manager(&store, records_root);
    lapian::app::CacheSettingsDialog::Callbacks callbacks;
    callbacks.apply = [&ui_manager](const QString& path, int quota_gb,
                                    QString* apply_error) {
      return ui_manager.ApplyRootAndQuota(
          path, static_cast<qint64>(quota_gb) * kGb, apply_error);
    };
    callbacks.clear = [&ui_manager](QString* clear_error, qint64* removed,
                                    int* kept) {
      return ui_manager.ClearAll(nullptr, clear_error, removed, kept);
    };
    callbacks.cleanup = [&ui_manager](QString* cleanup_error, qint64* removed,
                                      int* entries) {
      const CacheMaintenanceResult result =
          ui_manager.CleanupOrphans(nullptr, CleanupOptions{}, false);
      if (!result.ok) {
        if (cleanup_error != nullptr) {
          *cleanup_error = result.error;
        }
        return false;
      }
      if (removed != nullptr) {
        *removed = result.removed_bytes;
      }
      if (entries != nullptr) {
        *entries = result.removed_entries;
      }
      return true;
    };
    callbacks.open_directory = [](QString* open_error) {
      Q_UNUSED(open_error);
      return true;
    };
    lapian::app::CacheSettingsDialog dialog(&store, records_root, false, QString(),
                                            callbacks, nullptr);
    const QString usage_text = dialog.UsageTextForTest();
    Check(QStringLiteral("ui-root-text"),
          dialog.RootTextForTest().contains(QStringLiteral("cache-2")) ||
              dialog.RootTextForTest().contains(QStringLiteral("cache-%1-2").arg(tag)),
          dialog.RootTextForTest());
    Check(QStringLiteral("ui-quota-7"), dialog.QuotaGbForTest() == 7,
          QString::number(dialog.QuotaGbForTest()));
    Check(QStringLiteral("ui-usage-visible"),
          usage_text.contains(QStringLiteral("当前占用")), usage_text);
    Check(QStringLiteral("ui-records-visible"),
          dialog.RecordsTextForTest().contains(QStringLiteral("在线记录")),
          dialog.RecordsTextForTest());
    Check(QStringLiteral("ui-save-enabled"), dialog.SaveEnabledForTest());
    Check(QStringLiteral("ui-clear-enabled"), dialog.ClearEnabledForTest());
    dialog.SetSessionActive(true, QStringLiteral("测试会话"));
    Check(QStringLiteral("ui-active-disables-save"), !dialog.SaveEnabledForTest());
    Check(QStringLiteral("ui-active-disables-quota"), !dialog.QuotaEnabledForTest());
    Check(QStringLiteral("ui-active-keeps-clear"), dialog.ClearEnabledForTest());
    dialog.SetSessionActive(false);
    Check(QStringLiteral("ui-inactive-enables-save"), dialog.SaveEnabledForTest());
    dialog.ClearWithoutConfirmForTest();
    Check(QStringLiteral("ui-clear-status"),
          dialog.StatusTextForTest().contains(QStringLiteral("已清空缓存")),
          dialog.StatusTextForTest());
    Check(QStringLiteral("ui-clear-usage-zero"), store.UsageBytes() == 0);
    Check(QStringLiteral("ui-clear-records-kept"),
          QFileInfo::exists(cleanup_record_file) || QFileInfo::exists(record_file));
    dialog.CleanupForTest();
    Check(QStringLiteral("ui-cleanup-status"),
          dialog.StatusTextForTest().contains(QStringLiteral("已清理")),
          dialog.StatusTextForTest());
    dialog.OpenDirectoryForTest();
    Check(QStringLiteral("ui-open-status"),
          dialog.StatusTextForTest().contains(QStringLiteral("已打开")),
          dialog.StatusTextForTest());
    Check(QStringLiteral("ui-apply"),
          dialog.ApplyForTest(&ui_error), ui_error);
    ui_state = g_failures == 0 ? QStringLiteral("pass") : QStringLiteral("fail");
  }
  PrintKV("ui", ui_state);

  // 8) MainWindow 启动/定期清理入口：同一 RunCacheMaintenance 路径。
  QString maintenance_state = QStringLiteral("skip");
  if (options.ui_smoke) {
    const int failures_before = g_failures;
    const CacheKey main_orphan{UniqueKey(QStringLiteral("m"), 13), 113, 64, 7};
    Check(QStringLiteral("maintenance-seed-orphan"),
          CreateSizedFile(store.PartPath(main_orphan), 8 * kKb));
    MainWindow window(nullptr, settings_root, false);
    QString window_error;
    Check(QStringLiteral("maintenance-window-settings-button"),
          window.findChild<QPushButton*>(QStringLiteral("cacheSettingsButton")) !=
              nullptr);
    Check(QStringLiteral("maintenance-window-settings-action"),
          window.findChild<QAction*>(QStringLiteral("cacheSettingsAction")) !=
              nullptr);
    Check(QStringLiteral("maintenance-window-apply-settings"),
          window.ApplyCacheSettingsForTest(root2, 9, &window_error),
          window_error);
    Check(QStringLiteral("maintenance-window-root"),
          QDir::cleanPath(window.OnlineCacheRootForTest())
                  .compare(QDir::cleanPath(root2), Qt::CaseInsensitive) == 0,
          window.OnlineCacheRootForTest());
    {
      SettingsStore check_settings(settings_root);
      const lapian::core::SettingsLoadResult reloaded = check_settings.Load();
      Check(QStringLiteral("maintenance-window-settings-persisted"),
            reloaded.status == lapian::core::SettingsLoadStatus::kLoaded &&
                reloaded.settings.cache_quota_gb == 9 &&
                QDir::cleanPath(reloaded.settings.cache_root)
                        .compare(QDir::cleanPath(root2), Qt::CaseInsensitive) == 0,
            QStringLiteral("quota=%1 root=%2")
                .arg(reloaded.settings.cache_quota_gb)
                .arg(reloaded.settings.cache_root));
    }
    window.SetOnlineRecordsRootForTest(records_root);
    window.SetCacheMaintenanceEnabledForTest(true);
    window.RunCacheMaintenanceForTest();
    Check(QStringLiteral("maintenance-window-ready"), window.CacheRootReadyForTest(),
          window.CacheRootErrorForTest());
    Check(QStringLiteral("maintenance-reason"),
          window.CacheLastMaintenanceReasonForTest() == QStringLiteral("test"),
          window.CacheLastMaintenanceReasonForTest());
    Check(QStringLiteral("maintenance-removed-orphan"),
          window.CacheLastMaintenanceRemovedEntriesForTest() >= 1,
          QString::number(window.CacheLastMaintenanceRemovedEntriesForTest()));
    Check(QStringLiteral("maintenance-orphan-gone"),
          !QFileInfo::exists(store.EntryDir(main_orphan)));
    maintenance_state =
        g_failures == failures_before ? QStringLiteral("pass")
                                      : QStringLiteral("fail");
  }
  PrintKV("maintenance", maintenance_state);

  const qint64 final_usage = store.UsageBytes();
  PrintKV("root", root);
  PrintKI("quota_bytes", store.quota());
  PrintKI("usage_bytes", final_usage);
  PrintKI("checks", g_checks);
  PrintKI("failures", g_failures);
  const double elapsed_ms = static_cast<double>(total.nsecsElapsed()) / 1.0e6;
  std::printf("elapsed_ms=%.1f\n", elapsed_ms);
  if (!options.keep_root) {
    QDir(scratch).removeRecursively();
    QDir(records_root).removeRecursively();
    QDir(settings_root).removeRecursively();
    if (options.default_paths) {
      QDir(root).removeRecursively();
      QDir(root2).removeRecursively();
    }
  }
  if (g_failures != 0) {
    std::printf("failed=%s\n", g_failed.join(QLatin1Char(',')).toUtf8().constData());
    std::printf("verdict=fail\n");
    return 1;
  }
  std::printf("verdict=pass\n");
  return 0;
}

}  // namespace lapian::app
