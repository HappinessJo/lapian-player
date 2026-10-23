#include "online/CacheManager.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QVector>

#include <algorithm>

namespace lapian::online {
namespace {

QString FromUtf8Path(const QString& path) {
  return QDir::fromNativeSeparators(QDir::cleanPath(path));
}

QString AppDataFallback() {
  QString appdata = qEnvironmentVariable("APPDATA");
  if (appdata.isEmpty()) {
    appdata = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  }
  if (appdata.isEmpty()) {
    appdata = QDir::homePath() + QStringLiteral("/AppData/Roaming");
  }
  return appdata;
}

}  // namespace

CacheManager::CacheManager(CacheStore* cache, QString records_root)
    : cache_(cache),
      records_root_(records_root.isEmpty() ? DefaultRecordsRoot()
                                           : NormalizeRoot(records_root)) {}

QString CacheManager::DefaultRecordsRootFor(const QString& appdata) {
  QString base = appdata;
  if (base.isEmpty()) {
    base = AppDataFallback();
  }
  const QDir dir(base);
  if (dir.dirName() == QStringLiteral("拉片软件")) {
    return dir.filePath(QStringLiteral("online"));
  }
  return QDir(base).filePath(QStringLiteral("拉片软件/online"));
}

QString CacheManager::DefaultRecordsRoot() {
  return DefaultRecordsRootFor(qEnvironmentVariable("APPDATA"));
}

QString CacheManager::NormalizeRoot(const QString& root) {
  const QString trimmed = root.trimmed();
  if (trimmed.isEmpty()) {
    return CacheStore::DefaultRoot();
  }
  return QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
}

bool CacheManager::IsPathInside(const QString& path, const QString& dir) {
  if (path.isEmpty() || dir.isEmpty()) {
    return false;
  }
  QString child = FromUtf8Path(path);
  QString parent = FromUtf8Path(dir);
  if (child.isEmpty() || parent.isEmpty()) {
    return false;
  }
#ifdef _WIN32
  child = child.toLower();
  parent = parent.toLower();
#endif
  if (parent.endsWith(QLatin1Char('/'))) {
    parent.chop(1);
  }
  return child == parent || child.startsWith(parent + QLatin1Char('/'));
}

QString CacheManager::records_root() const { return records_root_; }

CacheSummary CacheManager::Summary() const {
  CacheSummary summary;
  if (cache_ == nullptr) {
    summary.root_error = QStringLiteral("在线缓存未初始化");
    return summary;
  }
  summary.root = cache_->root();
  summary.quota_bytes = cache_->quota();
  const QFileInfo root_info(summary.root);
  summary.root_exists = root_info.exists() && root_info.isDir();
  if (!summary.root_exists) {
    summary.free_bytes = CacheStore::FreeDiskBytes(summary.root);
    return summary;
  }
  QVector<CacheEntry> entries;
  QVector<CacheEntry> broken;
  QString scan_error;
  if (!cache_->Scan(&entries, &broken, &scan_error)) {
    summary.root_error = scan_error.isEmpty() ? QStringLiteral("无法扫描缓存目录")
                                              : scan_error;
    return summary;
  }
  for (const CacheEntry& entry : entries) {
    summary.usage_bytes += entry.usage_bytes;
    ++summary.entries;
    if (entry.complete) {
      ++summary.complete_entries;
    } else {
      ++summary.incomplete_entries;
    }
  }
  for (const CacheEntry& entry : broken) {
    summary.usage_bytes += entry.usage_bytes;
    ++summary.entries;
    ++summary.broken_entries;
  }
  summary.free_bytes = CacheStore::FreeDiskBytes(summary.root);
  QString write_error;
  summary.root_writable = CacheStore::CheckDirectoryWritable(summary.root, &write_error);
  if (!summary.root_writable) {
    summary.root_error = write_error;
  }
  return summary;
}

CacheRecordsSummary CacheManager::Records() const {
  CacheRecordsSummary summary;
  summary.root = records_root_;
  const QDir dir(summary.root);
  if (!dir.exists()) {
    return summary;
  }
  summary.exists = true;
  const QFileInfoList files =
      dir.entryInfoList(QStringList{QStringLiteral("*.lapian.json")},
                        QDir::Files | QDir::Hidden | QDir::System, QDir::Name);
  for (const QFileInfo& file : files) {
    ++summary.count;
    summary.bytes += file.size();
  }
  return summary;
}

bool CacheManager::CheckRoot(QString* error) const {
  if (cache_ == nullptr) {
    if (error != nullptr) {
      *error = QStringLiteral("在线缓存未初始化");
    }
    return false;
  }
  return CacheStore::CheckDirectoryWritable(cache_->root(), error);
}

bool CacheManager::ApplyRootAndQuota(const QString& root, qint64 quota_bytes,
                                     QString* error) {
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (cache_ == nullptr) {
    return fail(QStringLiteral("在线缓存未初始化"));
  }
  if (quota_bytes < CacheStore::kMinQuotaBytes) {
    return fail(QStringLiteral("缓存配额过小：%1 字节（至少 %2）")
                    .arg(quota_bytes)
                    .arg(CacheStore::FormatBytes(CacheStore::kMinQuotaBytes)));
  }
  const QString normalized = NormalizeRoot(root);
  if (normalized.isEmpty()) {
    return fail(QStringLiteral("缓存目录为空"));
  }
  if (!cache_->SetRoot(normalized, error)) {
    return false;
  }
  return cache_->SetQuotaBytes(quota_bytes, error);
}

bool CacheManager::ClearAll(const CacheKey* keep, QString* error,
                            qint64* removed_bytes, int* kept_entries) {
  if (cache_ == nullptr) {
    if (error != nullptr) {
      *error = QStringLiteral("在线缓存未初始化");
    }
    if (removed_bytes != nullptr) {
      *removed_bytes = 0;
    }
    if (kept_entries != nullptr) {
      *kept_entries = 0;
    }
    return false;
  }
  return cache_->ClearAll(error, removed_bytes, keep, kept_entries);
}

CacheMaintenanceResult CacheManager::CleanupOrphans(const CacheKey* keep,
                                                    CleanupOptions options,
                                                    bool enforce_quota) {
  CacheMaintenanceResult result;
  if (cache_ == nullptr) {
    result.error = QStringLiteral("在线缓存未初始化");
    return result;
  }
  QElapsedTimer timer;
  timer.start();
  const CacheKey current = keep != nullptr ? *keep : CacheKey{};
  result.current_protected = keep != nullptr && !keep->bvid.isEmpty();
  QString error;
  qint64 removed = 0;
  int removed_entries = 0;
  if (!cache_->CleanupOrphans(current, &error, options, &removed,
                              &removed_entries)) {
    result.error = error.isEmpty() ? QStringLiteral("缓存清理失败") : error;
    result.elapsed_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
    return result;
  }
  result.ok = true;
  result.changed = removed > 0 || removed_entries > 0;
  result.removed_bytes = removed;
  result.removed_entries = removed_entries;
  if (enforce_quota) {
    QString quota_error;
    if (!cache_->EvictToQuota(current, &quota_error)) {
      result.warning = quota_error;
    }
  }
  result.usage_after = cache_->UsageBytes();
  result.elapsed_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  return result;
}

}  // namespace lapian::online
