#pragma once

// T8.3.1 在线缓存管理（非门）：缓存目录/配额、占用/记录摘要、清空（保留当前
// 会话）、孤儿清理与配额回收。
//
// 本类只做 Qt Core 层的组合，不创建窗口、不联网、不读凭据：
// - 目录/配额应用仍走 CacheStore::SetRoot/SetQuotaBytes；
// - 清空/孤儿清理复用 CacheStore，在线记录/截图位于缓存根外，本类不碰；
// - 当前会话保护由调用方传入 CacheKey（可为 bvid+cid 部分键）。
//
// UI（设置对话框）与 `--online-cache-manage-test` 共用本类，避免把判断散在
// 窗口代码里；产品默认路径仍是 `%LOCALAPPDATA%\拉片软件\cache`。

#include <QString>

#include "online/CacheStore.h"

namespace lapian::online {

// 设置对话框/探针的只读摘要。
struct CacheSummary {
  QString root;
  qint64 quota_bytes = 0;
  qint64 usage_bytes = 0;
  qint64 free_bytes = -1;  // -1 = 查询失败
  int entries = 0;
  int complete_entries = 0;
  int incomplete_entries = 0;
  int broken_entries = 0;
  bool root_exists = false;
  bool root_writable = false;
  QString root_error;  // 目录不可用时的可读原因；不存在时为空
};

// 在线记录在缓存根外（%APPDATA%\拉片软件\online），清空缓存不得影响。
struct CacheRecordsSummary {
  QString root;
  bool exists = false;
  int count = 0;
  qint64 bytes = 0;
};

// 启动/定期清理结果。warning 不是硬失败（例如配额回收遇到读句柄）。
struct CacheMaintenanceResult {
  bool ok = false;
  bool changed = false;
  qint64 removed_bytes = 0;
  int removed_entries = 0;
  qint64 usage_after = 0;
  double elapsed_ms = 0.0;
  bool current_protected = false;
  QString error;
  QString warning;
};

class CacheManager {
 public:
  // cache 必须比本对象活得久；records_root 为空 = DefaultRecordsRoot()。
  explicit CacheManager(CacheStore* cache, QString records_root = QString());

  // %APPDATA%\拉片软件\online（与 T8.7 MarkStore 一致，不叠第二层）。
  static QString DefaultRecordsRoot();
  // 纯函数版本，单测用；appdata 为空时回退 QStandardPaths/家目录。
  static QString DefaultRecordsRootFor(const QString& appdata);

  // 空 = 默认根；否则绝对化 + cleanPath（不创建目录）。
  static QString NormalizeRoot(const QString& root);
  // path 是否等于/位于 dir 下（Windows 大小写不敏感）。
  static bool IsPathInside(const QString& path, const QString& dir);

  CacheStore* cache() const { return cache_; }
  QString records_root() const;

  // 扫描占用、条目数、损坏数、磁盘可用空间；可写性用一次性探测文件。
  CacheSummary Summary() const;
  // 只读扫描在线记录（*.lapian.json），目录不存在返回 count=0。
  CacheRecordsSummary Records() const;
  // 验证当前缓存根存在且可写；不改变 store 状态。
  bool CheckRoot(QString* error) const;

  // 应用新根/配额（创建目录并做可写探测）。根变化不迁移旧缓存。
  bool ApplyRootAndQuota(const QString& root, qint64 quota_bytes, QString* error);

  // 清空缓存；keep 非空时保留该条目（支持 bvid+cid 部分键）。
  bool ClearAll(const CacheKey* keep, QString* error, qint64* removed_bytes,
                int* kept_entries);

  // 无 meta 孤儿、>N 小时未完成、evict_pending；enforce_quota 时再按 LRU
  // 回收到配额内。当前会话永不清理。
  CacheMaintenanceResult CleanupOrphans(const CacheKey* keep,
                                        CleanupOptions options,
                                        bool enforce_quota);

 private:
  CacheStore* cache_ = nullptr;
  QString records_root_;
};

}  // namespace lapian::online
