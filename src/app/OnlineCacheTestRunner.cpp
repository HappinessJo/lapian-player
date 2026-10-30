#include "app/OnlineCacheTestRunner.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cstdio>

#include "core/AppSettings.h"
#include "online/CacheStore.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#endif

namespace lapian::app {
namespace {

using lapian::core::AppSettings;
using lapian::core::SettingsStore;
using lapian::online::CacheEntry;
using lapian::online::CacheKey;
using lapian::online::CacheMetadata;
using lapian::online::CacheStore;
using lapian::online::CleanupOptions;

constexpr qint64 kMb = 1024 * 1024;

int g_checks = 0;
int g_failures = 0;
QStringList g_failed;

bool Check(const QString& name, bool ok, const QString& detail = QString()) {
  ++g_checks;
  if (ok) {
    std::printf("[ OK ] %s\n", name.toUtf8().constData());
  } else {
    ++g_failures;
    g_failed << name;
    std::printf("[FAIL] %s%s%s\n", name.toUtf8().constData(),
                detail.isEmpty() ? "" : "：", detail.toUtf8().constData());
  }
  std::fflush(stdout);
  return ok;
}

bool EnsureParent(const QString& path) {
  return QDir().mkpath(QFileInfo(path).absolutePath());
}

bool MarkSparse(const QString& path) {
#ifdef _WIN32
  const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
  const std::wstring wide =
      (native.startsWith(QStringLiteral("\\\\?\\"))
           ? native
           : QStringLiteral("\\\\?\\") + native)
          .toStdWString();
  HANDLE handle = CreateFileW(wide.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD returned = 0;
  const BOOL ok = DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0,
                                  &returned, nullptr);
  CloseHandle(handle);
  return ok != 0;
#else
  return QFileInfo::exists(path);
#endif
}

bool CreateSizedFile(const QString& path, qint64 size) {
  if (!EnsureParent(path)) {
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  file.close();
  MarkSparse(path);  // 失败也继续；只影响物理占用，不影响逻辑大小
  if (!file.open(QIODevice::ReadWrite)) {
    return false;
  }
  const bool ok = file.resize(size);
  file.close();
  return ok;
}

QDateTime AgoSecs(qint64 seconds) {
  return QDateTime::currentDateTimeUtc().addSecs(-seconds);
}

bool CreateComplete(CacheStore* store, const CacheKey& key, qint64 bytes,
                    const QString& title) {
  QString error;
  if (!store->Reserve(key, bytes, &error)) {
    Check(QStringLiteral("setup-reserve-") + title, false, error);
    return false;
  }
  CacheMetadata metadata;
  metadata.title = title;
  metadata.owner = QStringLiteral("UP主");
  metadata.duration_ms = 111000;
  metadata.width = 1920;
  metadata.height = 1080;
  metadata.fps = 29.412;
  metadata.source_fps_hint = QStringLiteral("60");
  if (!store->SetMetadata(key, metadata, &error)) {
    Check(QStringLiteral("setup-metadata-") + title, false, error);
    return false;
  }
  if (!CreateSizedFile(store->PartPath(key), bytes)) {
    Check(QStringLiteral("setup-data-") + title, false);
    return false;
  }
  if (!store->MarkComplete(key, &error)) {
    Check(QStringLiteral("setup-complete-") + title, false, error);
    return false;
  }
  return true;
}

// 造一条完整条目（稀疏数据 + meta），用于 LRU 顺序控制。
bool CreateRawComplete(CacheStore* store, const CacheKey& key, qint64 bytes,
                       const QDateTime& last_access) {
  if (!CreateSizedFile(store->Mp4Path(key), bytes)) {
    return false;
  }
  QJsonObject root;
  root.insert(QStringLiteral("schema"), QStringLiteral("lapian-cache-1"));
  root.insert(QStringLiteral("bvid"), key.bvid);
  root.insert(QStringLiteral("cid"), QJsonValue(key.cid));
  root.insert(QStringLiteral("title"), QStringLiteral("LRU 合成条目"));
  root.insert(QStringLiteral("owner"), QStringLiteral("UP主"));
  root.insert(QStringLiteral("duration_ms"), QJsonValue(qint64{111000}));
  root.insert(QStringLiteral("qn"), key.qn);
  root.insert(QStringLiteral("codecid"), key.codecid);
  root.insert(QStringLiteral("width"), 1920);
  root.insert(QStringLiteral("height"), 1080);
  root.insert(QStringLiteral("fps"), 29.412);
  root.insert(QStringLiteral("total_bytes"), QJsonValue(bytes));
  root.insert(QStringLiteral("downloaded_bytes"), QJsonValue(bytes));
  root.insert(QStringLiteral("complete"), true);
  root.insert(QStringLiteral("created_at"),
              last_access.addSecs(-3600).toString(Qt::ISODateWithMs));
  root.insert(QStringLiteral("last_access_at"),
              last_access.toString(Qt::ISODateWithMs));
  root.insert(QStringLiteral("source_fps_hint"), QString());
  QFile meta(QDir(store->EntryDir(key)).filePath(QStringLiteral("meta.json")));
  if (!meta.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  const QByteArray bytes_json =
      QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';
  const bool ok = meta.write(bytes_json) == bytes_json.size();
  meta.close();
  return ok;
}

bool PatchLastAccess(const CacheStore& store, const CacheKey& key,
                     const QDateTime& when) {
  const QString path =
      QDir(store.EntryDir(key)).filePath(QStringLiteral("meta.json"));
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
  file.close();
  if (!document.isObject()) {
    return false;
  }
  QJsonObject root = document.object();
  root.insert(QStringLiteral("last_access_at"), when.toString(Qt::ISODateWithMs));
  QFile out(path);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';
  const bool ok = out.write(bytes) == bytes.size();
  out.close();
  return ok;
}

QString TrimmedLabel(const CacheKey& key) {
  return QStringLiteral("%1/%2/%3-%4")
      .arg(key.bvid.right(3))
      .arg(key.cid)
      .arg(key.qn)
      .arg(key.codecid);
}

QString LruOrder(const QVector<CacheEntry>& list) {
  QVector<CacheEntry> sorted = list;
  std::sort(sorted.begin(), sorted.end(),
            [](const CacheEntry& a, const CacheEntry& b) {
              return a.last_access_at < b.last_access_at;
            });
  QStringList labels;
  for (const CacheEntry& entry : sorted) {
    labels << TrimmedLabel(entry.key);
  }
  return labels.join(QLatin1Char('>'));
}

bool KeyPresent(const QVector<CacheEntry>& list, const CacheKey& key) {
  for (const CacheEntry& entry : list) {
    if (entry.key == key) {
      return true;
    }
  }
  return false;
}

int CountMetaSecrets(const QString& root, qint64* scanned) {
  int hits = 0;
  QDir dir(root);
  const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QFileInfo& info : dirs) {
    hits += CountMetaSecrets(info.absoluteFilePath(), scanned);
  }
  const QFileInfoList files = dir.entryInfoList(QDir::Files);
  const QStringList markers = {QStringLiteral("SESSDATA"), QStringLiteral("sessdata"),
                               QStringLiteral("Cookie"), QStringLiteral("cookie"),
                               QStringLiteral("http://"), QStringLiteral("https://"),
                               QStringLiteral("playurl"),
                               QStringLiteral("access_key")};
  for (const QFileInfo& file : files) {
    if (file.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) != 0) {
      continue;
    }
    QFile f(file.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly)) {
      continue;
    }
    const QByteArray bytes = f.readAll();
    if (scanned != nullptr) {
      *scanned += bytes.size();
    }
    for (const QString& marker : markers) {
      if (bytes.contains(marker.toUtf8())) {
        ++hits;
        std::printf("  明文命中：%s -> %s\n", file.absoluteFilePath().toUtf8().constData(),
                    marker.toUtf8().constData());
      }
    }
  }
  return hits;
}

QString UniqueKey(const QString& letter_seed, int index) {
  // 12 位合法 BV：BV + 1 + 8 个同字母 + 1 位序号。
  return QStringLiteral("BV1") + letter_seed.repeated(8) + QString::number(index);
}

}  // namespace

int RunOnlineCacheTest(const OnlineCacheTestOptions& options) {
  QElapsedTimer total;
  total.start();

  const QString app_dir = QCoreApplication::applicationDirPath();
  const qint64 pid = static_cast<qint64>(QCoreApplication::applicationPid());
  QString scratch = options.root;
  if (scratch.isEmpty()) {
    scratch = QDir(app_dir).filePath(QStringLiteral("../t8.3-cache-%1").arg(pid));
  }
  scratch = QDir::cleanPath(scratch);
  const QString records_root =
      QDir(QFileInfo(scratch).absolutePath())
          .filePath(QStringLiteral("t8.3-online-%1").arg(pid));

  if (QFileInfo::exists(scratch) && !QDir(scratch).removeRecursively()) {
    std::printf("[FAIL] 临时根无法清理：%s\n", scratch.toUtf8().constData());
    std::printf("verdict=fail\n");
    return 2;
  }
  if (!QDir().mkpath(scratch)) {
    std::printf("[FAIL] 临时根无法创建：%s\n", scratch.toUtf8().constData());
    std::printf("verdict=fail\n");
    return 2;
  }
  QDir(scratch).removeRecursively();  // 让 CacheStore 自己建
  if (QFileInfo::exists(records_root)) {
    QDir(records_root).removeRecursively();
  }
  QDir().mkpath(records_root);

  const QString cache_root = QDir(scratch).filePath(QStringLiteral("cache"));
  const qint64 quota = 300 * kMb;
  CacheStore store(cache_root, quota);

  // 1) 默认根：%LOCALAPPDATA%\拉片软件\cache（不叠第二层），不写入真实目录。
  const QString default_root = CacheStore::DefaultRoot();
  const QString sample_base = QStringLiteral("C:/Users/x/AppData/Local");
  const QString sample_root = CacheStore::DefaultRootFor(sample_base);
  Check(QStringLiteral("default-root-localappdata"),
        QDir::fromNativeSeparators(sample_root) ==
            QDir::fromNativeSeparators(sample_base +
                                       QStringLiteral("/拉片软件/cache")),
        sample_root);
  Check(QStringLiteral("default-root-no-double-app-dir"),
        !QDir::fromNativeSeparators(sample_root)
             .contains(QStringLiteral("拉片软件/拉片软件")));
  Check(QStringLiteral("default-root-already-app-named"),
        QDir::fromNativeSeparators(CacheStore::DefaultRootFor(
            sample_base + QStringLiteral("/拉片软件"))) ==
            QDir::fromNativeSeparators(sample_base +
                                       QStringLiteral("/拉片软件/cache")));
  Check(QStringLiteral("default-root-real-under-localappdata"),
        QFileInfo(default_root).isAbsolute() &&
            default_root.contains(QStringLiteral("拉片软件")) &&
            QDir::fromNativeSeparators(default_root).endsWith(QStringLiteral("/cache")),
        default_root);
  Check(QStringLiteral("default-quota-20gib"),
        CacheStore::DefaultQuotaBytes() == 20LL * 1024 * 1024 * 1024);
  std::printf("default_root=%s\n", default_root.toUtf8().constData());

  // 2) 300MB 配额 + 3 条 90MB（最旧 e1，其次 e2，再次 e3）。
  const CacheKey e1{UniqueKey(QStringLiteral("a"), 1), 101, 64, 7};
  const CacheKey e2{UniqueKey(QStringLiteral("b"), 2), 102, 64, 7};
  const CacheKey e3{UniqueKey(QStringLiteral("c"), 3), 103, 80, 12};
  Check(QStringLiteral("setup-e1"), CreateRawComplete(&store, e1, 90 * kMb,
                                                      AgoSecs(4 * 3600)));
  Check(QStringLiteral("setup-e2"), CreateRawComplete(&store, e2, 90 * kMb,
                                                      AgoSecs(3 * 3600)));
  Check(QStringLiteral("setup-e3"), CreateRawComplete(&store, e3, 90 * kMb,
                                                      AgoSecs(2 * 3600)));

  QVector<CacheEntry> entries;
  QVector<CacheEntry> broken;
  store.Scan(&entries, &broken, nullptr);
  const int entries_before = entries.size();
  const qint64 usage_before = store.UsageBytes();
  const QString lru_before = LruOrder(entries);
  QStringList before_keys;
  before_keys.reserve(entries.size());
  for (const CacheEntry& entry : entries) {
    before_keys << TrimmedLabel(entry.key);
  }
  std::printf("entries_before=%d\n", entries_before);
  std::printf("usage_before=%lld\n", static_cast<long long>(usage_before));

  // 3) 新条目 60MB：270+60 > 300 → 淘汰最旧 e1；当前条目不淘汰。
  const CacheKey current{UniqueKey(QStringLiteral("d"), 4), 104, 64, 7};
  QString error;
  const bool reserved = store.Reserve(current, 60 * kMb, &error);
  Check(QStringLiteral("lru-reserve-evicts"), reserved, error);
  store.Scan(&entries, &broken, nullptr);
  const QString lru_after = LruOrder(entries);
  const bool e1_gone = !store.Load(e1).has_value();
  const int evicted = entries_before + 1 - entries.size();
  QStringList after_keys;
  after_keys.reserve(entries.size());
  for (const CacheEntry& entry : entries) {
    after_keys << TrimmedLabel(entry.key);
  }
  QStringList evicted_keys = before_keys;
  for (const QString& key : after_keys) {
    evicted_keys.removeAll(key);
  }
  const qint64 usage_after = store.UsageBytes();
  Check(QStringLiteral("lru-evicts-oldest"), e1_gone);
  Check(QStringLiteral("lru-keeps-current"), KeyPresent(entries, current));
  Check(QStringLiteral("lru-usage-under-quota"), usage_after <= quota,
        QStringLiteral("usage=%1 quota=%2").arg(usage_after).arg(quota));
  std::printf("lru_before=%s\n", lru_before.toUtf8().constData());
  std::printf("lru_after=%s\n", lru_after.toUtf8().constData());
  std::printf("evicted_keys=%s\n", evicted_keys.join(QLatin1Char(',')).toUtf8().constData());
  std::printf("evicted=%d\n", evicted);
  std::printf("protected=%d\n", KeyPresent(entries, current) ? 1 : 0);
  std::printf("usage_after=%lld\n", static_cast<long long>(usage_after));

  // 4) 续传：.part 5MB → 新实例按实际长度返回（不请求网络）。
  const CacheKey resume_key{UniqueKey(QStringLiteral("e"), 5), 105, 64, 7};
  Check(QStringLiteral("resume-reserve"), store.Reserve(resume_key, 20 * kMb, &error),
        error);
  Check(QStringLiteral("resume-write-part"),
        CreateSizedFile(store.PartPath(resume_key), 5 * kMb));
  qint64 resume_bytes = 0;
  {
    CacheStore reopened(cache_root, quota);
    QString part_path;
    Check(QStringLiteral("resume-open-part"),
          reopened.OpenPart(resume_key, &part_path, &resume_bytes, &error), error);
  }
  Check(QStringLiteral("resume-length-5mb"), resume_bytes == 5 * kMb);
  std::printf("resume_bytes=%lld\n", static_cast<long long>(resume_bytes));

  // 5) 损坏检测 + 重建：截断 data.mp4。
  const CacheKey corrupt_key = e3;
  Check(QStringLiteral("corrupt-truncate"),
        QFileInfo(store.Mp4Path(corrupt_key)).exists() &&
            [&] {
              QFile file(store.Mp4Path(corrupt_key));
              if (!file.open(QIODevice::ReadWrite)) {
                return false;
              }
              const bool ok = file.resize(512 * 1024);
              file.close();
              return ok;
            }());
  store.Scan(&entries, &broken, nullptr);
  Check(QStringLiteral("corrupt-detected"), broken.size() >= 1,
        QStringLiteral("broken=%1").arg(broken.size()));
  const bool rebuilt = store.Rebuild(corrupt_key, &error);
  Check(QStringLiteral("corrupt-rebuild"), rebuilt, error);
  Check(QStringLiteral("corrupt-not-playable-after-rebuild"),
        !store.Load(corrupt_key).has_value());
  std::printf("corrupt=%d\n", static_cast<int>(broken.size()));
  std::printf("rebuild=%d\n", rebuilt ? 1 : 0);

  // 6) 磁盘满预检（不可满足的 expected，走同一预检路径，不是真写满磁盘）。
  const CacheKey huge{UniqueKey(QStringLiteral("f"), 6), 106, 64, 7};
  QElapsedTimer space_timer;
  space_timer.start();
  const bool huge_reserved =
      store.Reserve(huge, 2'000'000'000'000'000LL, &error);
  const double space_error_ms =
      static_cast<double>(space_timer.nsecsElapsed()) / 1.0e6;
  Check(QStringLiteral("space-precheck-blocks"), !huge_reserved);
  Check(QStringLiteral("space-error-readable"),
        error.startsWith(QStringLiteral("磁盘空间不足：需要")) && error.contains("可用"),
        error);
  Check(QStringLiteral("space-error-under-5s"), space_error_ms <= 5000.0);
  std::printf("space_error_ms=%.3f\n", space_error_ms);

  // 7) 清空缓存保留在线记录（sentinel 在缓存根外）。
  const QString record_file = QDir(records_root).filePath(
      QStringLiteral("BV1BMnv6jE5S-101.lapian.json"));
  {
    QFile record(record_file);
    Check(QStringLiteral("records-sentinel-create"),
          record.open(QIODevice::WriteOnly) &&
              record.write("{\"schema\":\"lapian-online-1\"}") > 0);
  }
  qint64 cleared_bytes = 0;
  const bool cleared = store.ClearAll(&error, &cleared_bytes);
  Check(QStringLiteral("clear-ok"), cleared, error);
  Check(QStringLiteral("clear-usage-zero"), store.UsageBytes() == 0);
  const bool records_kept = QFileInfo::exists(record_file);
  Check(QStringLiteral("clear-records-kept"), records_kept);
  std::printf("clear=%lld\n", static_cast<long long>(cleared_bytes));
  std::printf("records_kept=%d\n", records_kept ? 1 : 0);

  // 8) 跨会话：settings.json 的 cache_root/cache_quota_gb 新进程仍生效。
  const QString settings_root = QDir(scratch).filePath(QStringLiteral("settings"));
  const QString cache_root_2 = QDir(scratch).filePath(QStringLiteral("cache-2"));
  {
    SettingsStore settings(settings_root);
    AppSettings values;
    values.cache_root = cache_root_2;
    values.cache_quota_gb = 7;
    Check(QStringLiteral("cross-save-settings"), settings.Save(values));
  }
  bool cross_ok = false;
  {
    SettingsStore settings(settings_root);
    const lapian::core::SettingsLoadResult loaded = settings.Load();
    CacheStore store2(loaded.settings.cache_root,
                      static_cast<qint64>(loaded.settings.cache_quota_gb) * 1024 * kMb);
    const CacheKey key{UniqueKey(QStringLiteral("g"), 7), 107, 64, 7};
    const bool created2 = CreateComplete(&store2, key, 1 * kMb,
                                         QStringLiteral("跨会话条目"));
    const qint64 usage2 = store2.UsageBytes();
    CacheStore reopened(loaded.settings.cache_root,
                        static_cast<qint64>(loaded.settings.cache_quota_gb) * 1024 * kMb);
    cross_ok = loaded.status == lapian::core::SettingsLoadStatus::kLoaded &&
               loaded.settings.cache_root == cache_root_2 &&
               loaded.settings.cache_quota_gb == 7 && created2 && usage2 > 0 &&
               reopened.UsageBytes() == usage2 && reopened.quota() == 7 * 1024 * kMb;
  }
  Check(QStringLiteral("cross-session-root-quota"), cross_ok);
  std::printf("cross_session=%s\n", cross_ok ? "pass" : "fail");

  // 9) Unicode + >MAX_PATH 长路径。
  QString long_root = QDir(scratch).filePath(QStringLiteral("长路径 缓存根"));
  const QString segment = QStringLiteral("很长的中文目录名-");
  int index = 0;
  while (long_root.size() < 280) {
    long_root += QLatin1Char('/') + segment + QString::number(index++);
  }
  QString long_path_state = QStringLiteral("skip");
  {
    CacheStore long_store(long_root, quota);
    QString long_error;
    if (!long_store.SetRoot(long_root, &long_error)) {
      std::printf("long_path=skip（%s）\n", long_error.toUtf8().constData());
    } else {
      const CacheKey key{UniqueKey(QStringLiteral("h"), 8), 108, 64, 7};
      const QString entry_dir = long_store.EntryDir(key);
      const bool ok = long_store.Reserve(key, 2 * kMb, &long_error) &&
                      CreateSizedFile(long_store.PartPath(key), 2 * kMb) &&
                      long_store.MarkComplete(key, &long_error) &&
                      long_store.Load(key).has_value() &&
                      long_store.OpenComplete(key, nullptr, &long_error) &&
                      long_store.Rebuild(key, &long_error);
      long_path_state = ok ? QStringLiteral("pass") : QStringLiteral("fail");
      Check(QStringLiteral("long-path-entry-over-max-path"),
            entry_dir.size() > 260,
            QStringLiteral("len=%1").arg(entry_dir.size()));
      Check(QStringLiteral("long-path-create-rename-read"), ok, long_error);
      std::printf("long_path_len=%lld\n", static_cast<long long>(entry_dir.size()));
    }
  }
  std::printf("long_path=%s\n", long_path_state.toUtf8().constData());

  // 10) meta.json 无明文 Cookie/签名 URL（只扫 JSON，不扫数据文件）。
  qint64 scanned = 0;
  const int secret_hits = CountMetaSecrets(cache_root, &scanned);
  Check(QStringLiteral("plaintext-no-secrets"), secret_hits == 0,
        QStringLiteral("hits=%1").arg(secret_hits));
  std::printf("plaintext=%d\n", secret_hits);

  // 11) 汇总（先读主缓存根的最终状态，再清理临时目录）。
  QVector<CacheEntry> final_entries;
  QVector<CacheEntry> final_broken;
  store.Scan(&final_entries, &final_broken, nullptr);
  const qint64 final_usage = store.UsageBytes();
  const int final_entry_count = static_cast<int>(final_entries.size());
  if (!options.keep_root) {
    QDir(scratch).removeRecursively();
    QDir(records_root).removeRecursively();
  }
  const double elapsed_ms = static_cast<double>(total.nsecsElapsed()) / 1.0e6;
  std::printf("root=%s\n", cache_root.toUtf8().constData());
  std::printf("quota_bytes=%lld\n", static_cast<long long>(quota));
  std::printf("usage_bytes=%lld\n", static_cast<long long>(final_usage));
  std::printf("entries=%d\n", final_entry_count);
  std::printf("checks=%d\n", g_checks);
  std::printf("failures=%d\n", g_failures);
  for (const QString& failed : g_failed) {
    std::printf("failed_check=%s\n", failed.toUtf8().constData());
  }
  std::printf("elapsed_ms=%.1f\n", elapsed_ms);
  std::printf("verdict=%s\n", g_failures == 0 ? "pass" : "fail");
  std::fflush(stdout);
  return g_failures == 0 ? 0 : 1;
}

}  // namespace lapian::app
