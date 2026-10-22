#pragma once

// T8.3 在线缓存存储与配额（delta §2.5 O5 / §4；plan §12.1 CacheStore）。
//
// 只做本地存储：目录 / meta.json / .part→.mp4 原子改名 / 配额与 LRU /
// 占用与清空 / 空间预检 / 续传长度 / 损坏检测与重建 / 孤儿清理。
//
// **不做**（留给后续任务）：
// - T8.4 可增长读层：本类不提供 GrowingSource/Cursor/AVIO/多读者/阻塞读；
// - T8.5 取流：本类不发 HTTP、不做 Range、不认识 playurl/CDN；
// - T8.6/T8.7 UI、缓存命中打开、在线记录。
//
// 目录结构（delta §4.1，qn/codecid 来自 playurl，不猜）：
//   <缓存根>\<bvid>\<cid>\<qn>-<codecid>\
//       meta.json     # 原子写；不含 Cookie / 签名 URL / 请求头
//       data.part     # 下载中；连续前缀
//       data.mp4      # 完成后由 .part 原子改名
//
// 线程模型：所有公开方法内部用一把互斥锁串行化；本类自身不创建线程。
// 时间语义：created_at/last_access_at 以 UTC 保存；LRU 只比较 last_access_at。
// 调用方约定：MarkComplete 前必须关闭自己持有的 .part 写句柄（本类只做改名）。

#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QVector>

#include <optional>

namespace lapian::online {

// 缓存键：bvid + cid + qn + codecid（delta §1.4）。
struct CacheKey {
  QString bvid;
  qint64 cid = 0;
  int qn = 0;
  int codecid = 0;

  bool operator==(const CacheKey& other) const = default;
};

// 只接受 "BV" + 10 位字母数字；用于目录名，必须先校验再拼路径（防穿越）。
bool IsValidCacheBvid(const QString& bvid);
bool IsValidCacheKey(const CacheKey& key);
// 日志/证据用标签：BV1BMnv6jE5S/12345/64-7（不拼进文件名）。
QString CacheKeyLabel(const CacheKey& key);

// playurl 成功后由 T8.5 写入的展示元数据；其余字段由存储层维护。
struct CacheMetadata {
  QString title;
  QString owner;
  qint64 duration_ms = 0;
  int width = 0;
  int height = 0;
  double fps = 0.0;
  QString source_fps_hint;
};

// 一条缓存记录（meta + 数据文件的实际状态）。
struct CacheEntry {
  CacheKey key;
  QString title;
  QString owner;
  qint64 duration_ms = 0;
  int width = 0;
  int height = 0;
  double fps = 0.0;
  QString source_fps_hint;
  qint64 total_bytes = 0;
  qint64 downloaded_bytes = 0;  // 已下载连续前缀（以数据文件实际长度为准）
  bool complete = false;
  QDateTime created_at;
  QDateTime last_access_at;

  // 加载/扫描时填充（不写进 meta 的派生字段）。
  QString dir;             // 条目目录（绝对路径）
  QString data_path;       // complete ? data.mp4 : data.part
  qint64 data_bytes = 0;   // data.part + data.mp4 实际字节
  qint64 meta_bytes = 0;   // meta.json 实际字节
  qint64 usage_bytes = 0;  // data_bytes + meta_bytes（配额口径）
  bool meta_present = false;
  bool broken = false;      // 损坏：不得当可播放；Rebuild 才能恢复
  bool evict_pending = false;  // 淘汰时文件仍被读取：标记待淘汰
  bool meta_dirty = false;     // 加载时发现校正（如 .part 长度）：需要回写
  QString broken_reason;
};

// 启动清理（delta §4.2 第 6 条）：可配置/可关。
struct CleanupOptions {
  bool enabled = true;
  int incomplete_max_age_hours = 24;
};

class CacheStore {
 public:
  // root 为空 = DefaultRoot()；quota_bytes <= 0 = DefaultQuotaBytes()。
  // 构造不碰磁盘（首次 Reserve/SetRoot 才建目录），便于纯存储单测。
  explicit CacheStore(QString root = QString(), qint64 quota_bytes = 0);
  ~CacheStore();

  CacheStore(const CacheStore&) = delete;
  CacheStore& operator=(const CacheStore&) = delete;

  // %LOCALAPPDATA%\拉片软件\cache（不叠加第二层“拉片软件”）。
  static QString DefaultRoot();
  // 纯函数版本：给定 %LOCALAPPDATA% 基路径返回缓存根；空则回退 GenericCacheLocation。
  // 单测用它验证“不叠两层”，不依赖修改进程环境变量。
  static QString DefaultRootFor(const QString& local_app_data);
  // 20 GiB（vision 已拍板 20GB；delta §4.3）。
  static qint64 DefaultQuotaBytes();

  static constexpr qint64 kDefaultQuotaBytes = 20LL * 1024 * 1024 * 1024;
  // 空间预检余量：可用空间 ≥ 预计总字节 + 512MB（delta §4.3）。
  static constexpr qint64 kSpaceHeadroomBytes = 512LL * 1024 * 1024;
  // 配额下限只做健全性保护；1–100GB 的产品范围由设置层 clamp（DEC-5 用 300MB 验收）。
  static constexpr qint64 kMinQuotaBytes = 1LL * 1024 * 1024;
  // 进度写盘节流（delta §4.2 第 2 条：可 1–2s 节流）。
  static constexpr qint64 kProgressWriteIntervalMs = 1000;
  // meta.json 防御性上限：异常文件不读进内存。
  static constexpr qint64 kMaxMetaBytes = 64 * 1024;

  QString root() const;
  qint64 quota() const;

  // 建立/校验缓存根（可写、是目录）。失败不改当前根，返回可读错误。
  bool SetRoot(const QString& root, QString* error);
  // 只接受 >= kMinQuotaBytes；1–100GB 的 clamp 在设置层，测试可直接设 300MB。
  bool SetQuotaBytes(qint64 quota_bytes, QString* error);

  // 配额口径的总占用（data.mp4 + data.part + meta.json 实际字节；含损坏条目）。
  qint64 UsageBytes() const;

  // 写入前：空间预检（可用 ≥ 预计总字节 + 512MB）→ 配额/LRU → 建目录/空 .part/meta。
  // 已完整缓存时直接返回 true（缓存命中路径不重新下载）。
  bool Reserve(const CacheKey& key, qint64 expected_bytes, QString* error);

  // 续传：返回 .part 路径与文件实际长度；meta 的 downloaded_bytes 按文件长度校正。
  // 不请求网络（T8.5 自己决定 Range 起点）。
  bool OpenPart(const CacheKey& key, QString* part_path, qint64* existing_bytes,
                QString* error);

  // 下载进度：默认节流 1s 写盘；force=true 立即写；数值单调不回退。
  bool UpdateProgress(const CacheKey& key, qint64 downloaded_bytes, bool force);

  // 完成：关闭写句柄后调用；校验长度 → MoveFileExW 原子改名 → meta complete=true。
  // 失败保留 .part 并返回可读错误；重复调用幂等。
  bool MarkComplete(const CacheKey& key, QString* error);

  // T8.7 缓存命中：仅当 complete 且大小匹配时返回 data.mp4 路径；不联网。
  bool OpenComplete(const CacheKey& key, QString* mp4_path, QString* error);

  // 读取单条：不存在返回 nullopt（*error 为空）；存在但损坏返回 broken 条目。
  // 会回写校正（.part 长度、崩溃后 .mp4 已在但 meta 未落盘）。
  std::optional<CacheEntry> Load(const CacheKey& key, QString* error = nullptr);

  // 全量扫描：不因单条损坏中断；out/broken 都按 bvid/cid/qn/codecid 排序。
  bool Scan(QVector<CacheEntry>* out, QVector<CacheEntry>* broken,
            QString* error = nullptr);

  // 淘汰到配额内：最旧 last_access_at 优先；current 永不淘汰。
  // 文件仍被读取时标记 evict_pending 并跳过（返回 false 并说明）。
  bool EvictToQuota(const CacheKey& current, QString* error);

  // 重建：只删该条目的 data.part/data.mp4/meta.json（含 meta.json 临时残留），
  // 不动在线记录/截图（它们不在缓存根下）。
  bool Rebuild(const CacheKey& key, QString* error);

  // 清空：只删缓存根下的条目数据；不删在线记录/截图；幂等。
  bool ClearAll(QString* error, qint64* removed_bytes = nullptr);

  // 启动清理：无 meta 的孤儿数据、超过 N 小时未访问的未完成条目、evict_pending；
  // 当前会话 key 永不清理。
  bool CleanupOrphans(const CacheKey& current, QString* error,
                      CleanupOptions options = CleanupOptions(),
                      qint64* removed_bytes = nullptr);

  // 记录最近访问（LRU）；条目损坏/不存在时返回 false。
  bool Touch(const CacheKey& key, QString* error);

  // T8.5：playurl 元数据写进 meta（不改变 total_bytes/complete）。
  bool SetMetadata(const CacheKey& key, const CacheMetadata& metadata, QString* error);

  // 路径（不创建）。key 非法时返回空串。
  QString EntryDir(const CacheKey& key) const;
  QString PartPath(const CacheKey& key) const;
  QString Mp4Path(const CacheKey& key) const;

  // 磁盘可用字节（缓存根所在卷，找不到目录时用最近的已存在祖先）；失败返回 -1。
  static qint64 FreeDiskBytes(const QString& path, QString* error = nullptr);

  // 人类可读字节：用于“磁盘空间不足：需要 X，可用 Y”。
  static QString FormatBytes(qint64 bytes);

 private:
  bool ReserveLocked(const CacheKey& key, qint64 expected_bytes, QString* error);
  std::optional<CacheEntry> LoadLocked(const CacheKey& key, QString* error,
                                       bool correct_downloaded = true);
  std::optional<CacheEntry> LoadEntryLocked(const CacheKey& key, QString* error,
                                            bool correct_downloaded = true) const;
  void ScanLocked(QVector<CacheEntry>* out, QVector<CacheEntry>* broken,
                  QString* error) const;
  qint64 UsageLocked() const;
  bool WriteMetaLocked(CacheEntry& entry, QString* error);
  bool TouchLocked(CacheEntry& entry, QString* error);
  bool EnsureRootLocked(QString* error);
  bool EvictUntilLocked(qint64 target_usage, const CacheKey& current, QString* error);
  bool DeleteEntryDataLocked(const CacheEntry& entry, bool delete_meta,
                             qint64* removed_bytes, bool* busy, QString* error);
  QString EntryDirLocked(const CacheKey& key) const;
  QString PartPathLocked(const CacheKey& key) const;
  QString Mp4PathLocked(const CacheKey& key) const;

  mutable QMutex mutex_;
  QString root_;
  qint64 quota_ = kDefaultQuotaBytes;
  QHash<QString, qint64> last_progress_write_ms_;
};

}  // namespace lapian::online
