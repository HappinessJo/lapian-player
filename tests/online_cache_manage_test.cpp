// T8.3.1 缓存管理核心单测（无 GUI/网络）：默认根、跨会话目录/配额、清空保留
// 当前会话（含 bvid+cid 部分键）、在线记录在缓存根外、孤儿清理、可读错误。
//
// 不写真实 %LOCALAPPDATA%/%APPDATA%：根/记录/设置都用 QTemporaryDir。

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QVector>

#include <algorithm>
#include <cstdio>

#include "core/AppSettings.h"
#include "online/CacheManager.h"
#include "online/CacheStore.h"

namespace {

using lapian::core::AppSettings;
using lapian::core::SettingsStore;
using lapian::online::CacheEntry;
using lapian::online::CacheKey;
using lapian::online::CacheMaintenanceResult;
using lapian::online::CacheManager;
using lapian::online::CacheMetadata;
using lapian::online::CacheStore;
using lapian::online::CleanupOptions;

constexpr qint64 kKb = 1024;
constexpr qint64 kMb = 1024 * kKb;
constexpr qint64 kGb = 1024 * kMb;

int g_checks = 0;
int g_failures = 0;

bool Check(bool ok, const char* name, const QString& detail = QString()) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("[FAIL] %s%s%s\n", name, detail.isEmpty() ? "" : "：",
                detail.toUtf8().constData());
  }
  return ok;
}

bool CreateSizedFile(const QString& path, qint64 size) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  if (!file.resize(size)) {
    return false;
  }
  file.close();
  return true;
}

bool SeedComplete(CacheStore* store, const CacheKey& key, qint64 bytes) {
  QString error;
  if (!store->Reserve(key, bytes, &error)) {
    return false;
  }
  CacheMetadata metadata;
  metadata.title = QStringLiteral("单测");
  metadata.width = 1280;
  metadata.height = 720;
  if (!store->SetMetadata(key, metadata, &error)) {
    return false;
  }
  if (!CreateSizedFile(store->PartPath(key), bytes)) {
    return false;
  }
  return store->MarkComplete(key, &error);
}

bool SeedIncomplete(CacheStore* store, const CacheKey& key, qint64 total,
                    qint64 downloaded) {
  QString error;
  if (!store->Reserve(key, total, &error)) {
    return false;
  }
  return CreateSizedFile(store->PartPath(key), downloaded);
}

bool PatchLastAccess(CacheStore* store, const CacheKey& key,
                     const QDateTime& at) {
  const QString path =
      QDir(store->EntryDir(key)).filePath(QStringLiteral("meta.json"));
  QFile in(path);
  if (!in.open(QIODevice::ReadOnly)) {
    return false;
  }
  const QJsonDocument doc = QJsonDocument::fromJson(in.readAll());
  in.close();
  if (!doc.isObject()) {
    return false;
  }
  QJsonObject object = doc.object();
  object.insert(QStringLiteral("last_access_at"),
                at.toUTC().toString(Qt::ISODateWithMs));
  QFile out(path);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  out.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
  out.close();
  return true;
}

QString RecordPath(const QString& root, const QString& bvid, qint64 cid) {
  QDir().mkpath(root);
  return QDir(root).filePath(
      QStringLiteral("%1-%2.lapian.json").arg(bvid).arg(cid));
}

bool WriteAll(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return file.write(bytes) == bytes.size();
}

QString UniqueKey(const QString& seed, int index) {
  const QString number = QString::number(index);
  const int digits = std::clamp(static_cast<int>(number.size()), 1, 9);
  return QStringLiteral("BV1") + seed.repeated(9 - digits) +
         number.rightJustified(digits, QLatin1Char('0'));
}

void TestDefaults(const QString& temp) {
  const QString default_root = CacheStore::DefaultRoot();
  const QString default_records = CacheManager::DefaultRecordsRoot();
  Check(CacheManager::IsPathInside(default_root, qEnvironmentVariable("LOCALAPPDATA")),
        "default-root-under-localappdata", default_root);
  Check(CacheManager::IsPathInside(default_records, qEnvironmentVariable("APPDATA")),
        "default-records-under-appdata", default_records);
  Check(!CacheManager::IsPathInside(default_root, QDir::currentPath()),
        "default-root-not-workspace", default_root);
  Check(!CacheManager::IsPathInside(default_records, QDir::currentPath()),
        "default-records-not-workspace", default_records);
  Check(!CacheManager::IsPathInside(default_root, temp), "default-root-not-temp");
  Check(CacheManager::IsPathInside(default_root + QStringLiteral("/x"), default_root),
        "path-inside-child");
  Check(!CacheManager::IsPathInside(default_root + QStringLiteral("x"), default_root),
        "path-inside-prefix-not-child");
  const QString sample = CacheStore::DefaultRootFor(QStringLiteral("C:/Users/x/AppData/Local"));
  Check(QDir::fromNativeSeparators(sample) ==
            QDir::fromNativeSeparators(
                QStringLiteral("C:/Users/x/AppData/Local/拉片软件/cache")),
        "default-root-no-double-app");
  const QString records_sample =
      CacheManager::DefaultRecordsRootFor(QStringLiteral("C:/Users/x/AppData/Roaming"));
  Check(QDir::fromNativeSeparators(records_sample) ==
            QDir::fromNativeSeparators(
                QStringLiteral("C:/Users/x/AppData/Roaming/拉片软件/online")),
        "default-records-no-double-app");
  Check(QDir::fromNativeSeparators(CacheManager::DefaultRecordsRootFor(
            QStringLiteral("C:/Users/x/AppData/Roaming/拉片软件"))) ==
            QDir::fromNativeSeparators(
                QStringLiteral("C:/Users/x/AppData/Roaming/拉片软件/online")),
        "default-records-already-app-named");
  Check(CacheManager::NormalizeRoot(QString()) == default_root,
        "normalize-empty-is-default");
  Check(CacheStore::DefaultQuotaBytes() == 20 * kGb, "default-quota-20gib");
}

void TestSettingsAcrossSessions(const QString& temp) {
  const QString settings_root = QDir(temp).filePath(QStringLiteral("settings"));
  const QString root_a = QDir(temp).filePath(QStringLiteral("cache-a"));
  const QString root_b = QDir(temp).filePath(QStringLiteral("cache-b"));
  SettingsStore settings(settings_root);
  AppSettings values;
  values.cache_root = root_b;
  values.cache_quota_gb = 7;
  Check(settings.Save(values), "settings-save");
  const lapian::core::SettingsLoadResult loaded = settings.Load();
  Check(loaded.status == lapian::core::SettingsLoadStatus::kLoaded,
        "settings-load-status");
  Check(QDir::cleanPath(loaded.settings.cache_root) == QDir::cleanPath(root_b),
        "settings-root-cross-session", loaded.settings.cache_root);
  Check(loaded.settings.cache_quota_gb == 7, "settings-quota-cross-session");

  CacheStore store(root_a, 300 * kMb);
  CacheManager manager(&store);
  QString error;
  Check(manager.ApplyRootAndQuota(loaded.settings.cache_root,
                                  7 * kGb, &error),
        "apply-loaded-root", error);
  Check(store.root().compare(root_b, Qt::CaseInsensitive) == 0,
        "apply-root-effective", store.root());
  Check(store.quota() == 7 * kGb, "apply-quota-effective");
  const CacheKey key{UniqueKey(QStringLiteral("a"), 1), 1, 64, 7};
  Check(SeedComplete(&store, key, 1 * kMb), "apply-new-root-write");
  CacheStore reopened(root_b, 7 * kGb);
  Check(reopened.UsageBytes() == store.UsageBytes(), "reopen-usage");
  Check(reopened.quota() == 7 * kGb, "reopen-quota");
  Check(!QFileInfo::exists(root_a) || QDir(root_a).isEmpty(),
        "root-switch-no-migration");
}

void TestClearAndRecords(const QString& temp) {
  const QString root = QDir(temp).filePath(QStringLiteral("clear-cache"));
  const QString records = QDir(temp).filePath(QStringLiteral("clear-records"));
  CacheStore store(root, 300 * kMb);
  CacheManager manager(&store, records);
  QString error;
  Check(manager.ApplyRootAndQuota(root, 300 * kMb, &error), "clear-apply-root",
        error);

  const CacheKey current{UniqueKey(QStringLiteral("b"), 2), 2, 64, 7};
  const CacheKey other{UniqueKey(QStringLiteral("c"), 3), 3, 80, 12};
  const CacheKey broken{UniqueKey(QStringLiteral("d"), 4), 4, 64, 7};
  const CacheKey orphan{UniqueKey(QStringLiteral("e"), 5), 5, 64, 7};
  Check(SeedIncomplete(&store, current, 4 * kMb, 1 * kMb), "clear-seed-current");
  Check(SeedComplete(&store, other, 1 * kMb), "clear-seed-other");
  Check(SeedComplete(&store, broken, 1 * kMb), "clear-seed-broken");
  Check(CreateSizedFile(store.PartPath(orphan), 16 * kKb), "clear-seed-orphan");
  {
    QFile file(store.Mp4Path(broken));
    Check(file.open(QIODevice::ReadWrite) && file.resize(512 * kKb),
          "clear-break-meta-size");
  }
  const QString record = RecordPath(records, current.bvid, current.cid);
  const QByteArray content = QByteArrayLiteral("{\"schema\":\"lapian-online-1\"}");
  Check(WriteAll(record, content), "clear-record-write");
  const int records_before = manager.Records().count;

  CacheKey partial = current;
  partial.qn = 0;
  partial.codecid = 0;
  qint64 removed = 0;
  int kept = 0;
  Check(manager.ClearAll(&partial, &error, &removed, &kept),
        "clear-with-current-ok", error);
  Check(kept == 1, "clear-kept-count");
  Check(store.Load(current).has_value(), "clear-current-kept");
  Check(!store.Load(other).has_value(), "clear-other-gone");
  Check(!store.Load(broken).has_value(), "clear-broken-gone");
  Check(!QFileInfo::exists(store.EntryDir(orphan)), "clear-orphan-gone");
  Check(QFileInfo::exists(record), "clear-record-kept");
  Check(manager.Records().count == records_before, "clear-record-count");
  {
    QFile file(record);
    Check(file.open(QIODevice::ReadOnly) && file.readAll() == content,
          "clear-record-content");
  }
  Check(manager.ClearAll(nullptr, &error, &removed, nullptr),
        "clear-all-ok", error);
  Check(store.UsageBytes() == 0, "clear-all-usage-zero");
  Check(QFileInfo(root).isDir(), "clear-root-kept");
  Check(QFileInfo::exists(record), "clear-all-record-kept");
}

void TestCleanup(const QString& temp) {
  const QString root = QDir(temp).filePath(QStringLiteral("cleanup-cache"));
  const QString records = QDir(temp).filePath(QStringLiteral("cleanup-records"));
  CacheStore store(root, 300 * kMb);
  CacheManager manager(&store, records);
  QString error;
  Check(manager.ApplyRootAndQuota(root, 300 * kMb, &error), "cleanup-apply-root",
        error);

  const CacheKey current{UniqueKey(QStringLiteral("f"), 6), 6, 64, 7};
  const CacheKey stale{UniqueKey(QStringLiteral("g"), 7), 7, 64, 7};
  const CacheKey fresh{UniqueKey(QStringLiteral("h"), 8), 8, 64, 7};
  const CacheKey complete{UniqueKey(QStringLiteral("i"), 9), 9, 64, 7};
  const CacheKey orphan{UniqueKey(QStringLiteral("j"), 10), 10, 64, 7};
  Check(SeedIncomplete(&store, current, 2 * kMb, 1 * kKb), "cleanup-seed-current");
  Check(SeedIncomplete(&store, stale, 2 * kMb, 1 * kKb), "cleanup-seed-stale");
  Check(SeedIncomplete(&store, fresh, 2 * kMb, 1 * kKb), "cleanup-seed-fresh");
  Check(SeedComplete(&store, complete, 1 * kMb), "cleanup-seed-complete");
  Check(CreateSizedFile(store.PartPath(orphan), 8 * kKb), "cleanup-seed-orphan");
  const QDateTime now = QDateTime::currentDateTimeUtc();
  Check(PatchLastAccess(&store, current, now.addSecs(-48 * 3600)),
        "cleanup-age-current");
  Check(PatchLastAccess(&store, stale, now.addSecs(-25 * 3600)),
        "cleanup-age-stale");
  Check(PatchLastAccess(&store, fresh, now.addSecs(-23 * 3600)),
        "cleanup-age-fresh");
  const QString record = RecordPath(records, current.bvid, current.cid);
  Check(WriteAll(record, QByteArrayLiteral("{\"schema\":\"lapian-online-1\"}")),
        "cleanup-record-write");
  const int records_before = manager.Records().count;

  CacheKey partial = current;
  partial.qn = 0;
  partial.codecid = 0;
  const CacheMaintenanceResult result =
      manager.CleanupOrphans(&partial, CleanupOptions{}, false);
  Check(result.ok, "cleanup-ok", result.error);
  Check(result.removed_entries >= 2, "cleanup-removed-entries",
        QString::number(result.removed_entries));
  Check(result.current_protected, "cleanup-current-protected");
  Check(QFileInfo::exists(store.PartPath(current)), "cleanup-current-kept");
  Check(!QFileInfo::exists(store.EntryDir(stale)), "cleanup-stale-removed");
  Check(QFileInfo::exists(store.PartPath(fresh)), "cleanup-fresh-kept");
  Check(QFileInfo::exists(store.Mp4Path(complete)), "cleanup-complete-kept");
  Check(!QFileInfo::exists(store.EntryDir(orphan)), "cleanup-orphan-removed");
  Check(QFileInfo::exists(record), "cleanup-record-kept");
  Check(manager.Records().count == records_before, "cleanup-record-count");

  CleanupOptions disabled;
  disabled.enabled = false;
  const CacheMaintenanceResult skipped =
      manager.CleanupOrphans(nullptr, disabled, false);
  Check(skipped.ok && skipped.removed_entries == 0, "cleanup-disabled");
}

void TestErrors(const QString& temp) {
  const QString root = QDir(temp).filePath(QStringLiteral("error-cache"));
  CacheStore store(root, 300 * kMb);
  CacheManager manager(&store);
  QString error;
  Check(manager.ApplyRootAndQuota(root, 300 * kMb, &error), "error-apply-root",
        error);

  const QString file_root = QDir(temp).filePath(QStringLiteral("file-root"));
  Check(WriteAll(file_root, "x"), "error-file-root-create");
  CacheStore invalid(file_root);
  Check(!invalid.SetRoot(file_root, &error), "error-file-root-fails");
  Check(error.contains(QStringLiteral("不是目录")), "error-file-root-readable",
        error);
  Check(!manager.ApplyRootAndQuota(file_root, 300 * kMb, &error),
        "error-apply-file-root");
  Check(store.root().compare(root, Qt::CaseInsensitive) == 0,
        "error-apply-keeps-root");

  const CacheKey huge{UniqueKey(QStringLiteral("k"), 11), 11, 64, 7};
  const bool reserved = store.Reserve(huge, 2'000'000'000'000'000LL, &error);
  Check(!reserved, "error-disk-full-blocked");
  Check(error.startsWith(QStringLiteral("磁盘空间不足：需要")) &&
            error.contains(QStringLiteral("可用")),
        "error-disk-full-readable", error);
  Check(!store.SetQuotaBytes(0, &error), "error-quota-zero-fails");
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("拉片软件"));
  QCoreApplication::setApplicationName(QStringLiteral("拉片软件"));
  QTemporaryDir temp;
  if (!temp.isValid()) {
    std::printf("T8.3.1 online_cache_manage_test: 临时目录不可用\n");
    return 2;
  }
  TestDefaults(temp.path());
  TestSettingsAcrossSessions(temp.path());
  TestClearAndRecords(temp.path());
  TestCleanup(temp.path());
  TestErrors(temp.path());
  std::printf("checks=%d failures=%d\n", g_checks, g_failures);
  if (g_failures == 0) {
    std::printf("T8.3.1 online_cache_manage_test: all checks passed\n");
    return 0;
  }
  return 1;
}
