#include "online/CacheStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/statvfs.h>
#include <unistd.h>
using DWORD = unsigned long;
#endif

namespace lapian::online {
namespace {

constexpr char kSchemaValue[] = "lapian-cache-1";
constexpr qint64 kMinCacheCid = 1;
constexpr qint64 kMaxCacheCid = 1'000'000'000'000'000LL;
constexpr int kMinQn = 1;
constexpr int kMaxQn = 100000;
constexpr int kMinCodecId = 1;
constexpr int kMaxCodecId = 100000;

const QString kMetaName = QStringLiteral("meta.json");
const QString kPartName = QStringLiteral("data.part");
const QString kMp4Name = QStringLiteral("data.mp4");

QString JoinPath(const QString& dir, const QString& name) {
  if (dir.isEmpty()) {
    return {};
  }
  return QDir(dir).filePath(name);
}

QString IsoNow() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QDateTime ParseTimestamp(const QString& text) {
  if (text.isEmpty()) {
    return {};
  }
  QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
  if (!parsed.isValid()) {
    parsed = QDateTime::fromString(text, Qt::ISODate);
  }
  return parsed.isValid() ? parsed.toUTC() : QDateTime();
}

QString SerializeTimestamp(const QDateTime& time) {
  if (!time.isValid()) {
    return {};
  }
  return time.toUTC().toString(Qt::ISODateWithMs);
}

bool IsIntegral(double value) {
  return std::isfinite(value) && std::floor(value) == value;
}

// Windows 长路径：MoveFileExW/CreateFileW/GetDiskFreeSpaceExW 不会自动加 \\?\，
// >MAX_PATH 的中文目录会失败。参考 core/MarkStore.cpp 的 NativeLongPath。
#ifdef _WIN32
std::wstring NativeLongPath(const QString& path) {
  QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
  if (native.startsWith(QStringLiteral("\\\\?\\"))) {
    return native.toStdWString();
  }
  if (native.startsWith(QStringLiteral("\\\\"))) {
    return (QStringLiteral("\\\\?\\UNC\\") + native.mid(2)).toStdWString();
  }
  return (QStringLiteral("\\\\?\\") + native).toStdWString();
}
#endif

bool RenameLongPath(const QString& from, const QString& to, QString* error) {
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (from.isEmpty() || to.isEmpty()) {
    return fail(QStringLiteral("缓存改名路径为空"));
  }
#ifdef _WIN32
  // 调用方必须已关闭 .part 写句柄；否则这里以共用冲突失败并保留 .part。
  if (MoveFileExW(NativeLongPath(from).c_str(), NativeLongPath(to).c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
    const DWORD code = GetLastError();
    if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION) {
      return fail(QStringLiteral("缓存文件仍在使用中（Win32 %1）").arg(code));
    }
    return fail(QStringLiteral("缓存文件改名失败（Win32 %1）").arg(code));
  }
  return true;
#else
  QFile::remove(to);
  if (!QFile::rename(from, to)) {
    return fail(QStringLiteral("缓存文件改名失败"));
  }
  return true;
#endif
}

enum class DeleteStatus { kDeleted, kMissing, kBusy, kFailed };

// 删除前先以 DELETE 独占打开，确认没有读句柄：
// - 读句柄不带 FILE_SHARE_DELETE（如 FFmpeg/CRT 的 open）→ 共用冲突 → kBusy；
// - 打开成功时用 FileDispositionInfo 标记删除，避免关闭后再删的竞态。
DeleteStatus DeleteFileChecked(const QString& path, DWORD* win32_error) {
  if (win32_error != nullptr) {
    *win32_error = 0;
  }
  if (path.isEmpty()) {
    return DeleteStatus::kMissing;
  }
#ifdef _WIN32
  const std::wstring native = NativeLongPath(path);
  HANDLE handle = CreateFileW(native.c_str(), DELETE, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
      return DeleteStatus::kMissing;
    }
    if (win32_error != nullptr) {
      *win32_error = error;
    }
    if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) {
      return DeleteStatus::kBusy;
    }
    return DeleteStatus::kFailed;
  }
  FILE_DISPOSITION_INFO disposition{};
  disposition.DeleteFile = TRUE;
  const BOOL marked =
      SetFileInformationByHandle(handle, FileDispositionInfo, &disposition,
                                 sizeof(disposition));
  const DWORD marked_error = marked ? 0 : GetLastError();
  CloseHandle(handle);
  if (marked) {
    return DeleteStatus::kDeleted;
  }
  if (marked_error == ERROR_FILE_NOT_FOUND) {
    return DeleteStatus::kMissing;
  }
  // 老系统/特殊卷不支持 FileDispositionInfo 时退回普通删除（文件已关闭）。
  if (DeleteFileW(native.c_str()) != 0) {
    return DeleteStatus::kDeleted;
  }
  const DWORD fallback_error = GetLastError();
  if (fallback_error == ERROR_FILE_NOT_FOUND) {
    return DeleteStatus::kMissing;
  }
  if (win32_error != nullptr) {
    *win32_error = fallback_error;
  }
  if (fallback_error == ERROR_SHARING_VIOLATION ||
      fallback_error == ERROR_LOCK_VIOLATION) {
    return DeleteStatus::kBusy;
  }
  return DeleteStatus::kFailed;
#else
  const QByteArray native = QFile::encodeName(path);
  const int handle = ::open(native.constData(), O_RDONLY);
  if (handle < 0) {
    return QFileInfo::exists(path) ? DeleteStatus::kFailed : DeleteStatus::kMissing;
  }
  if (::flock(handle, LOCK_EX | LOCK_NB) != 0) {
    const int code = errno;
    ::close(handle);
    if (code == EWOULDBLOCK) {
      return DeleteStatus::kBusy;
    }
    if (win32_error != nullptr) {
      *win32_error = static_cast<DWORD>(code);
    }
    return DeleteStatus::kFailed;
  }
  const bool removed = QFile::remove(path);
  ::close(handle);
  return removed ? DeleteStatus::kDeleted : DeleteStatus::kFailed;
#endif
}

QString Win32CodeText(DWORD code) {
  return code == 0 ? QStringLiteral("未知") : QString::number(code);
}

// 删除条目目录下的数据；delete_meta 时连 meta.json 一起删。
// 失败不中断其它文件的删除；busy=true 表示有文件仍被读取。
bool DeleteEntryFiles(const QString& dir, bool delete_meta, qint64* removed_bytes,
                      bool* busy, QString* error) {
  if (removed_bytes != nullptr) {
    *removed_bytes = 0;
  }
  if (busy != nullptr) {
    *busy = false;
  }
  bool ok = true;
  QString first_error;
  auto remove_one = [&](const QString& path) {
    if (path.isEmpty()) {
      return;
    }
    const QFileInfo info(path);
    const bool existed = info.exists();
    const qint64 bytes = existed && info.isFile() ? info.size() : 0;
    DWORD code = 0;
    const DeleteStatus status = DeleteFileChecked(path, &code);
    switch (status) {
      case DeleteStatus::kDeleted:
        if (removed_bytes != nullptr) {
          *removed_bytes += bytes;
        }
        break;
      case DeleteStatus::kMissing:
        break;
      case DeleteStatus::kBusy:
        if (busy != nullptr) {
          *busy = true;
        }
        if (ok) {
          ok = false;
          first_error = QStringLiteral("缓存文件正被读取，暂时无法删除：%1").arg(info.fileName());
        }
        break;
      case DeleteStatus::kFailed:
      default:
        if (ok) {
          ok = false;
          first_error =
              QStringLiteral("缓存文件删除失败：%1（Win32 %2）")
                  .arg(info.fileName(), Win32CodeText(code));
        }
        break;
    }
  };

  remove_one(JoinPath(dir, kPartName));
  remove_one(JoinPath(dir, kMp4Name));
  // 数据文件被读句柄占用（或删除失败）时保留 meta：条目仍可识别/重试，
  // 不会变成“半条数据 + 无 meta”的孤儿。
  if (delete_meta && ok) {
    remove_one(JoinPath(dir, kMetaName));
    // QSaveFile 崩溃残留：meta.json.<随机后缀>
    const QDir entry_dir(dir);
    const QStringList leftovers =
        entry_dir.entryList(QStringList{QStringLiteral("meta.json.*")}, QDir::Files);
    for (const QString& name : leftovers) {
      remove_one(JoinPath(dir, name));
    }
  }
  if (!ok && error != nullptr && error->isEmpty()) {
    *error = first_error;
  }
  return ok;
}

// 删掉空条目目录及其空的 cid/bvid 父目录；不越过缓存根。
void RemoveEmptyParents(const QString& entry_dir, const QString& root) {
  if (entry_dir.isEmpty() || root.isEmpty()) {
    return;
  }
  const QString normalized_root = QDir::cleanPath(root);
  QString current = QDir::cleanPath(entry_dir);
  for (int depth = 0; depth < 3; ++depth) {
    if (current.isEmpty() || current == normalized_root) {
      return;
    }
    const QDir dir(current);
    if (!dir.exists()) {
      current = QFileInfo(current).absolutePath();
      continue;
    }
    const QFileInfoList children =
        dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                              QDir::System,
                          QDir::NoSort);
    if (!children.isEmpty()) {
      return;
    }
    const QString parent_path = QFileInfo(current).absolutePath();
    if (parent_path == current || parent_path.isEmpty()) {
      return;
    }
    if (!QDir(parent_path).rmdir(QFileInfo(current).fileName())) {
      return;
    }
    current = parent_path;
  }
}

bool IsValidDirSegment(const QString& name) {
  return !name.isEmpty() && name != QStringLiteral(".") &&
         name != QStringLiteral("..") && !name.contains(QLatin1Char('/')) &&
         !name.contains(QLatin1Char('\\'));
}

// T8.3.1：当前会话保护键。完整键按 qn/codecid 精确匹配；qn=0 表示
// playurl 还没返回档位，但 bvid+cid 已知——保护该视频的全部档位，
// 避免启动/定期清理在“已 Reserve、尚未 Ready”窗口误删当前 .part。
bool MatchesProtectedKey(const CacheKey& entry, const CacheKey& keep) {
  if (keep.bvid.isEmpty() || keep.cid <= 0) {
    return false;
  }
  if (entry.bvid != keep.bvid || entry.cid != keep.cid) {
    return false;
  }
  if (keep.qn <= 0) {
    return true;
  }
  // T8.10：当前会话以视频键传入；同 bvid+cid 的音频条目是同一个 DASH 播放
  // 会话的一部分，清空/清理/配额淘汰都必须一起保护，否则会把正在播放的
  // 音频抽走。
  if (entry.kind != keep.kind) {
    return keep.kind == CacheStreamKind::kVideo &&
           entry.kind == CacheStreamKind::kAudio;
  }
  return entry.qn == keep.qn && entry.codecid == keep.codecid;
}

bool ParsePositiveInt(const QString& text, int min_value, int max_value, int* out) {
  bool ok = false;
  const qint64 value = text.toLongLong(&ok);
  if (!ok || value < min_value || value > max_value) {
    return false;
  }
  *out = static_cast<int>(value);
  return true;
}

bool ParsePositiveInt64(const QString& text, qint64 min_value, qint64 max_value,
                        qint64* out) {
  bool ok = false;
  const qint64 value = text.toLongLong(&ok);
  if (!ok || value < min_value || value > max_value) {
    return false;
  }
  *out = value;
  return true;
}

}  // namespace

bool IsValidCacheBvid(const QString& bvid) {
  // BV + 10 位 [0-9A-Za-z]（S2/OB 样本均为 12 位）；不接受路径字符。
  static const QRegularExpression pattern(
      QStringLiteral("\\ABV[0-9A-Za-z]{10}\\z"));
  return pattern.match(bvid).hasMatch();
}

bool IsValidCacheKey(const CacheKey& key) {
  if (!IsValidCacheBvid(key.bvid) || key.cid < kMinCacheCid ||
      key.cid > kMaxCacheCid) {
    return false;
  }
  if (key.kind == CacheStreamKind::kAudio) {
    // T8.10：音频条目只按 audio_id 标识；qn/codecid 必须为 0，避免与视频键
    // 产生同目录/同标签歧义。
    return key.audio_id >= kMinCodecId && key.audio_id <= kMaxQn &&
           key.qn == 0 && key.codecid == 0;
  }
  if (key.audio_id != 0) {
    return false;
  }
  return key.qn >= kMinQn && key.qn <= kMaxQn &&
         key.codecid >= kMinCodecId && key.codecid <= kMaxCodecId;
}

QString CacheStreamKindName(CacheStreamKind kind) {
  return kind == CacheStreamKind::kAudio ? QStringLiteral("audio")
                                         : QStringLiteral("video");
}

QString CacheKeyLabel(const CacheKey& key) {
  if (key.kind == CacheStreamKind::kAudio) {
    return QStringLiteral("%1/%2/audio-%3")
        .arg(key.bvid)
        .arg(key.cid)
        .arg(key.audio_id);
  }
  return QStringLiteral("%1/%2/%3-%4")
      .arg(key.bvid)
      .arg(key.cid)
      .arg(key.qn)
      .arg(key.codecid);
}

QString CacheStore::DefaultRootFor(const QString& local_app_data) {
  QString local = local_app_data;
  if (local.isEmpty()) {
    // Windows 上 GenericCacheLocation 就是 %LOCALAPPDATA%；不用 AppCacheLocation，
    // 否则会再拼一层“拉片软件”。
    local = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
  }
  if (local.isEmpty()) {
    local = QDir::homePath() + QStringLiteral("/AppData/Local");
  }
  QDir base(local);
  if (base.dirName() == QStringLiteral("拉片软件")) {
    return base.filePath(QStringLiteral("cache"));
  }
  return QDir(base.filePath(QStringLiteral("拉片软件")))
      .filePath(QStringLiteral("cache"));
}

QString CacheStore::DefaultRoot() {
  return DefaultRootFor(qEnvironmentVariable("LOCALAPPDATA"));
}

qint64 CacheStore::DefaultQuotaBytes() { return kDefaultQuotaBytes; }

CacheStore::CacheStore(QString root, qint64 quota_bytes)
    : root_(root.isEmpty() ? DefaultRoot()
                           : QDir::cleanPath(QFileInfo(root).absoluteFilePath())),
      quota_(quota_bytes <= 0 ? kDefaultQuotaBytes : quota_bytes) {}

CacheStore::~CacheStore() = default;

QString CacheStore::root() const {
  QMutexLocker locker(&mutex_);
  return root_;
}

qint64 CacheStore::quota() const {
  QMutexLocker locker(&mutex_);
  return quota_;
}

QString CacheStore::FormatBytes(qint64 bytes) {
  constexpr qint64 kKb = 1024;
  constexpr qint64 kMb = 1024 * kKb;
  constexpr qint64 kGb = 1024 * kMb;
  constexpr qint64 kTb = 1024 * kGb;
  const double value = static_cast<double>(bytes);
  if (bytes >= kTb) {
    return QStringLiteral("%1 TB").arg(value / static_cast<double>(kTb), 0, 'f', 1);
  }
  if (bytes >= kGb) {
    return QStringLiteral("%1 GB").arg(value / static_cast<double>(kGb), 0, 'f', 1);
  }
  if (bytes >= kMb) {
    return QStringLiteral("%1 MB").arg(value / static_cast<double>(kMb), 0, 'f', 1);
  }
  if (bytes >= kKb) {
    return QStringLiteral("%1 KB").arg(value / static_cast<double>(kKb), 0, 'f', 1);
  }
  return QStringLiteral("%1 B").arg(bytes);
}

qint64 CacheStore::FreeDiskBytes(const QString& path, QString* error) {
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return qint64(-1);
  };
  if (path.isEmpty()) {
    return fail(QStringLiteral("缓存路径为空，无法读取磁盘可用空间"));
  }
  // 目录可能还没创建：向上找最近的已存在祖先。
  QString probe = QFileInfo(path).absoluteFilePath();
  for (int guard = 0; guard < 64; ++guard) {
    const QFileInfo info(probe);
    if (info.exists()) {
      if (!info.isDir()) {
        probe = info.absolutePath();
      }
      break;
    }
    const QString parent = info.absolutePath();
    if (parent.isEmpty() || parent == probe) {
      break;
    }
    probe = parent;
  }
  if (!QFileInfo::exists(probe)) {
    return fail(QStringLiteral("无法定位缓存目录所在的磁盘：%1").arg(path));
  }
#ifdef _WIN32
  ULARGE_INTEGER free_bytes{};
  if (!GetDiskFreeSpaceExW(NativeLongPath(probe).c_str(), &free_bytes, nullptr,
                           nullptr)) {
    return fail(QStringLiteral("无法读取磁盘可用空间（Win32 %1）").arg(GetLastError()));
  }
  return static_cast<qint64>(free_bytes.QuadPart);
#else
  const QByteArray native = QFile::encodeName(probe);
  struct statvfs info {};
  if (::statvfs(native.constData(), &info) != 0) {
    return fail(QStringLiteral("无法读取磁盘可用空间（%1）")
                    .arg(QString::fromLocal8Bit(std::strerror(errno))));
  }
  const unsigned long long free_bytes =
      static_cast<unsigned long long>(info.f_bavail) * info.f_frsize;
  return free_bytes > static_cast<unsigned long long>(std::numeric_limits<qint64>::max())
             ? std::numeric_limits<qint64>::max()
             : static_cast<qint64>(free_bytes);
#endif
}

QString CacheStore::EntryDirLocked(const CacheKey& key) const {
  if (!IsValidCacheKey(key) || root_.isEmpty()) {
    return {};
  }
  const QString bvid_dir = JoinPath(root_, key.bvid);
  const QString cid_dir = JoinPath(bvid_dir, QString::number(key.cid));
  if (key.kind == CacheStreamKind::kAudio) {
    return JoinPath(cid_dir,
                    QStringLiteral("audio-%1").arg(key.audio_id));
  }
  return JoinPath(cid_dir,
                  QStringLiteral("%1-%2").arg(key.qn).arg(key.codecid));
}

QString CacheStore::PartPathLocked(const CacheKey& key) const {
  const QString dir = EntryDirLocked(key);
  return dir.isEmpty() ? QString() : JoinPath(dir, kPartName);
}

QString CacheStore::Mp4PathLocked(const CacheKey& key) const {
  const QString dir = EntryDirLocked(key);
  return dir.isEmpty() ? QString() : JoinPath(dir, kMp4Name);
}

QString CacheStore::EntryDir(const CacheKey& key) const {
  QMutexLocker locker(&mutex_);
  return EntryDirLocked(key);
}

QString CacheStore::PartPath(const CacheKey& key) const {
  QMutexLocker locker(&mutex_);
  return PartPathLocked(key);
}

QString CacheStore::Mp4Path(const CacheKey& key) const {
  QMutexLocker locker(&mutex_);
  return Mp4PathLocked(key);
}

bool CacheStore::EnsureRootLocked(QString* error) {
  if (root_.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("缓存根为空");
    }
    return false;
  }
  const QFileInfo info(root_);
  if (info.exists() && !info.isDir()) {
    if (error != nullptr) {
      *error = QStringLiteral("缓存根不是目录：%1").arg(root_);
    }
    return false;
  }
  if (!QDir().mkpath(root_)) {
    if (error != nullptr) {
      *error = QStringLiteral("无法创建缓存目录：%1").arg(root_);
    }
    return false;
  }
  return true;
}

bool CacheStore::CheckDirectoryWritable(const QString& dir, QString* error) {
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (dir.isEmpty()) {
    return fail(QStringLiteral("缓存根为空"));
  }
  const QFileInfo info(dir);
  if (!info.exists()) {
    return fail(QStringLiteral("缓存目录不存在：%1").arg(dir));
  }
  if (!info.isDir()) {
    return fail(QStringLiteral("缓存根不是目录：%1").arg(dir));
  }
  // 可写探测：用一次性文件，失败时不改当前根（比等到下载才失败更早）。
  static std::atomic<quint64> probe_seq{0};
  const quint64 sequence = probe_seq.fetch_add(1, std::memory_order_relaxed);
  const QString probe =
      JoinPath(dir, QStringLiteral(".lapian-probe-%1-%2.tmp")
                        .arg(QDateTime::currentMSecsSinceEpoch())
                        .arg(sequence));
  QFile probe_file(probe);
  if (!probe_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    return fail(QStringLiteral("缓存目录不可写（只读或权限不足）：%1（%2）")
                    .arg(dir, probe_file.errorString()));
  }
  probe_file.write("lapian");
  probe_file.close();
  QFile::remove(probe);
  return true;
}

bool CacheStore::SetRoot(const QString& root, QString* error) {
  QMutexLocker locker(&mutex_);
  if (root.trimmed().isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("缓存根不能为空");
    }
    return false;
  }
  const QString cleaned =
      QDir::cleanPath(QFileInfo(root.trimmed()).absoluteFilePath());
  if (cleaned.isEmpty() || cleaned.size() < 3) {
    if (error != nullptr) {
      *error = QStringLiteral("缓存根路径不合法：%1").arg(root);
    }
    return false;
  }
  const QFileInfo info(cleaned);
  if (info.exists() && !info.isDir()) {
    if (error != nullptr) {
      *error = QStringLiteral("缓存根不是目录：%1").arg(cleaned);
    }
    return false;
  }
  if (!QDir().mkpath(cleaned)) {
    if (error != nullptr) {
      *error = QStringLiteral("无法创建缓存目录：%1").arg(cleaned);
    }
    return false;
  }
  if (!CheckDirectoryWritable(cleaned, error)) {
    return false;
  }
  // 契约：路径变化不迁移旧缓存，旧根的文件继续留在旧根（delta §4.1）。
  root_ = cleaned;
  last_progress_write_ms_.clear();
  return true;
}

bool CacheStore::SetQuotaBytes(qint64 quota_bytes, QString* error) {
  QMutexLocker locker(&mutex_);
  if (quota_bytes < kMinQuotaBytes) {
    if (error != nullptr) {
      *error = QStringLiteral("缓存配额过小：%1 字节（至少 %2）")
                   .arg(quota_bytes)
                   .arg(FormatBytes(kMinQuotaBytes));
    }
    return false;
  }
  quota_ = quota_bytes;
  return true;
}

bool CacheStore::WriteMetaLocked(CacheEntry& entry, QString* error) {
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (entry.dir.isEmpty()) {
    return fail(QStringLiteral("缓存条目路径为空"));
  }
  if (!QDir().mkpath(entry.dir)) {
    return fail(QStringLiteral("无法创建缓存条目目录：%1").arg(entry.dir));
  }

  QJsonObject root;
  root.insert(QStringLiteral("schema"), QString::fromLatin1(kSchemaValue));
  root.insert(QStringLiteral("bvid"), entry.key.bvid);
  root.insert(QStringLiteral("cid"), QJsonValue(entry.key.cid));
  root.insert(QStringLiteral("title"), entry.title);
  root.insert(QStringLiteral("owner"), entry.owner);
  root.insert(QStringLiteral("duration_ms"), QJsonValue(entry.duration_ms));
  root.insert(QStringLiteral("qn"), entry.key.qn);
  root.insert(QStringLiteral("codecid"), entry.key.codecid);
  // T8.10：音频条目显式标 kind/audio_id；旧视频条目保持原字段（不写 kind），
  // 旧缓存读路径完全不变。
  if (entry.key.kind == CacheStreamKind::kAudio) {
    root.insert(QStringLiteral("kind"), QStringLiteral("audio"));
    root.insert(QStringLiteral("audio_id"), entry.key.audio_id);
  } else if (entry.dash) {
    // DASH 视频单流：音频在同 bvid+cid 的 audio-<id> 条目里。
    root.insert(QStringLiteral("container"), QStringLiteral("dash"));
    root.insert(QStringLiteral("audio_id"), entry.audio_id);
  }
  root.insert(QStringLiteral("width"), entry.width);
  root.insert(QStringLiteral("height"), entry.height);
  root.insert(QStringLiteral("fps"), entry.fps);
  root.insert(QStringLiteral("total_bytes"), QJsonValue(entry.total_bytes));
  root.insert(QStringLiteral("downloaded_bytes"),
              QJsonValue(entry.downloaded_bytes));
  root.insert(QStringLiteral("complete"), entry.complete);
  root.insert(QStringLiteral("created_at"),
              SerializeTimestamp(entry.created_at.isValid()
                                     ? entry.created_at
                                     : QDateTime::currentDateTimeUtc()));
  root.insert(QStringLiteral("last_access_at"), IsoNow());
  root.insert(QStringLiteral("source_fps_hint"), entry.source_fps_hint);
  if (entry.evict_pending) {
    root.insert(QStringLiteral("evict_pending"), true);
  }
  const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';
  if (bytes.size() > kMaxMetaBytes) {
    return fail(QStringLiteral("meta.json 内容过大：%1 字节").arg(bytes.size()));
  }

  QSaveFile file(JoinPath(entry.dir, kMetaName));
  // 要么完整替换，要么保留旧文件（与 T6.3/T8.2 一致）。
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) {
    return fail(QStringLiteral("无法写入 meta.json：%1").arg(file.errorString()));
  }
  if (file.write(bytes) != bytes.size()) {
    file.cancelWriting();
    return fail(QStringLiteral("meta.json 写入不完整：%1").arg(file.errorString()));
  }
  if (!file.commit()) {
    return fail(QStringLiteral("meta.json 保存失败：%1").arg(file.errorString()));
  }
  entry.meta_present = true;
  entry.meta_bytes = bytes.size();
  entry.usage_bytes = entry.data_bytes + entry.meta_bytes;
  entry.meta_dirty = false;
  return true;
}

std::optional<CacheEntry> CacheStore::LoadEntryLocked(const CacheKey& key,
                                                      QString* error,
                                                      bool correct_downloaded) const {
  auto set_error = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
  };
  if (!IsValidCacheKey(key)) {
    set_error(QStringLiteral("缓存键不合法：%1").arg(CacheKeyLabel(key)));
    return std::nullopt;
  }
  CacheEntry entry;
  entry.key = key;
  entry.dir = EntryDirLocked(key);
  const QString meta_path = JoinPath(entry.dir, kMetaName);
  const QString part_path = JoinPath(entry.dir, kPartName);
  const QString mp4_path = JoinPath(entry.dir, kMp4Name);
  const QFileInfo meta_info(meta_path);
  const QFileInfo part_info(part_path);
  const QFileInfo mp4_info(mp4_path);
  const bool has_meta = meta_info.exists() && meta_info.isFile();
  const bool has_part = part_info.exists() && part_info.isFile();
  const bool has_mp4 = mp4_info.exists() && mp4_info.isFile();
  if (!has_meta && !has_part && !has_mp4) {
    return std::nullopt;
  }

  entry.meta_present = has_meta;
  entry.meta_bytes = has_meta ? meta_info.size() : 0;
  entry.data_bytes =
      (has_part ? part_info.size() : 0) + (has_mp4 ? mp4_info.size() : 0);
  entry.usage_bytes = entry.data_bytes + entry.meta_bytes;
  const QDateTime file_time = has_meta ? meta_info.lastModified().toUTC()
                                       : (has_part ? part_info.lastModified().toUTC()
                                                   : mp4_info.lastModified().toUTC());
  entry.created_at = file_time;
  entry.last_access_at = file_time;
  entry.broken = false;

  auto mark_broken = [&](const QString& reason) {
    entry.broken = true;
    entry.broken_reason = reason;
    entry.data_path = has_mp4 ? mp4_path : (has_part ? part_path : QString());
    set_error(reason);
    return entry;
  };

  if (!has_meta) {
    // 只有数据文件没有 meta：孤儿（CleanupOrphans 会处理）；不得当可播放。
    return mark_broken(QStringLiteral("缺少 meta.json（只有数据文件）"));
  }
  if (meta_info.size() > kMaxMetaBytes) {
    return mark_broken(
        QStringLiteral("meta.json 过大：%1 字节").arg(meta_info.size()));
  }
  QFile file(meta_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return mark_broken(QStringLiteral("meta.json 无法读取：%1").arg(file.errorString()));
  }
  const QByteArray bytes = file.read(kMaxMetaBytes + 1);
  file.close();
  if (bytes.size() > kMaxMetaBytes) {
    return mark_broken(QStringLiteral("meta.json 过大"));
  }

  QJsonParseError parse_error{};
  const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    return mark_broken(QStringLiteral("meta.json 不是合法 JSON"));
  }
  const QJsonObject root = document.object();
  const QJsonValue schema = root.value(QStringLiteral("schema"));
  if (!schema.isString() ||
      schema.toString() != QString::fromLatin1(kSchemaValue)) {
    return mark_broken(schema.isString()
                           ? QStringLiteral("不支持的缓存 schema：%1").arg(schema.toString())
                           : QStringLiteral("meta.json 缺少 schema"));
  }

  const QJsonValue bvid = root.value(QStringLiteral("bvid"));
  const QJsonValue cid = root.value(QStringLiteral("cid"));
  const QJsonValue qn = root.value(QStringLiteral("qn"));
  const QJsonValue codecid = root.value(QStringLiteral("codecid"));
  if (!bvid.isString() || !cid.isDouble() || !qn.isDouble() || !codecid.isDouble() ||
      !IsIntegral(cid.toDouble()) || !IsIntegral(qn.toDouble()) ||
      !IsIntegral(codecid.toDouble())) {
    return mark_broken(QStringLiteral("meta.json 的键字段缺失或类型不对"));
  }
  if (bvid.toString() != key.bvid || static_cast<qint64>(cid.toDouble()) != key.cid) {
    return mark_broken(QStringLiteral("meta.json 与目录键不一致"));
  }
  // T8.10：kind 缺省=视频（旧 meta）；音频条目必须显式 kind=audio。
  const QJsonValue kind_value = root.value(QStringLiteral("kind"));
  CacheStreamKind meta_kind = CacheStreamKind::kVideo;
  if (kind_value.isString()) {
    const QString kind_text = kind_value.toString();
    if (kind_text == QStringLiteral("audio")) {
      meta_kind = CacheStreamKind::kAudio;
    } else if (kind_text != QStringLiteral("video")) {
      return mark_broken(QStringLiteral("meta.json 的 kind 不支持：%1")
                             .arg(kind_text));
    }
  } else if (!kind_value.isUndefined()) {
    return mark_broken(QStringLiteral("meta.json 的 kind 类型不对"));
  }
  if (meta_kind != key.kind) {
    return mark_broken(QStringLiteral("meta.json 的 kind 与目录键不一致"));
  }
  if (meta_kind == CacheStreamKind::kAudio) {
    const QJsonValue audio_id = root.value(QStringLiteral("audio_id"));
    if (!audio_id.isDouble() || !IsIntegral(audio_id.toDouble()) ||
        static_cast<int>(audio_id.toDouble()) != key.audio_id) {
      return mark_broken(QStringLiteral("meta.json 的 audio_id 缺失或与目录键不一致"));
    }
    if (static_cast<int>(qn.toDouble()) != 0 ||
        static_cast<int>(codecid.toDouble()) != 0) {
      return mark_broken(QStringLiteral("音频条目的 qn/codecid 必须为 0"));
    }
  } else {
    if (static_cast<int>(qn.toDouble()) != key.qn ||
        static_cast<int>(codecid.toDouble()) != key.codecid) {
      return mark_broken(QStringLiteral("meta.json 与目录键不一致"));
    }
    const QJsonValue container = root.value(QStringLiteral("container"));
    if (container.isString()) {
      if (container.toString() == QStringLiteral("dash")) {
        entry.dash = true;
      } else if (container.toString() != QStringLiteral("durl")) {
        return mark_broken(QStringLiteral("meta.json 的 container 不支持：%1")
                               .arg(container.toString()));
      }
    } else if (!container.isUndefined()) {
      return mark_broken(QStringLiteral("meta.json 的 container 类型不对"));
    }
    const QJsonValue audio_id = root.value(QStringLiteral("audio_id"));
    if (audio_id.isDouble() && IsIntegral(audio_id.toDouble()) &&
        audio_id.toDouble() >= 0.0 && audio_id.toDouble() <= 100000.0) {
      entry.audio_id = static_cast<int>(audio_id.toDouble());
    }
  }

  const QJsonValue total_bytes = root.value(QStringLiteral("total_bytes"));
  const QJsonValue downloaded_bytes = root.value(QStringLiteral("downloaded_bytes"));
  const QJsonValue complete = root.value(QStringLiteral("complete"));
  if (!total_bytes.isDouble() || !IsIntegral(total_bytes.toDouble()) ||
      total_bytes.toDouble() <= 0.0 ||
      total_bytes.toDouble() > 9.0e15) {
    return mark_broken(QStringLiteral("meta.json 的 total_bytes 缺失或非法"));
  }
  if (!complete.isBool()) {
    return mark_broken(QStringLiteral("meta.json 缺少 complete"));
  }
  entry.total_bytes = static_cast<qint64>(total_bytes.toDouble());
  entry.complete = complete.toBool();
  if (downloaded_bytes.isDouble() && IsIntegral(downloaded_bytes.toDouble()) &&
      downloaded_bytes.toDouble() >= 0.0) {
    entry.downloaded_bytes = static_cast<qint64>(downloaded_bytes.toDouble());
  }

  const QJsonValue title = root.value(QStringLiteral("title"));
  if (title.isString()) {
    entry.title = title.toString();
  }
  const QJsonValue owner = root.value(QStringLiteral("owner"));
  if (owner.isString()) {
    entry.owner = owner.toString();
  }
  const QJsonValue duration = root.value(QStringLiteral("duration_ms"));
  if (duration.isDouble() && IsIntegral(duration.toDouble()) &&
      duration.toDouble() >= 0.0 && duration.toDouble() < 9.0e15) {
    entry.duration_ms = static_cast<qint64>(duration.toDouble());
  }
  const QJsonValue width = root.value(QStringLiteral("width"));
  if (width.isDouble() && IsIntegral(width.toDouble()) && width.toDouble() >= 0.0 &&
      width.toDouble() <= 100000.0) {
    entry.width = static_cast<int>(width.toDouble());
  }
  const QJsonValue height = root.value(QStringLiteral("height"));
  if (height.isDouble() && IsIntegral(height.toDouble()) &&
      height.toDouble() >= 0.0 && height.toDouble() <= 100000.0) {
    entry.height = static_cast<int>(height.toDouble());
  }
  const QJsonValue fps = root.value(QStringLiteral("fps"));
  if (fps.isDouble() && std::isfinite(fps.toDouble()) && fps.toDouble() >= 0.0 &&
      fps.toDouble() <= 10000.0) {
    entry.fps = fps.toDouble();
  }
  const QJsonValue hint = root.value(QStringLiteral("source_fps_hint"));
  if (hint.isString()) {
    entry.source_fps_hint = hint.toString();
  }
  const QDateTime created =
      ParseTimestamp(root.value(QStringLiteral("created_at")).toString());
  if (created.isValid()) {
    entry.created_at = created;
  }
  const QDateTime accessed =
      ParseTimestamp(root.value(QStringLiteral("last_access_at")).toString());
  if (accessed.isValid()) {
    entry.last_access_at = accessed;
  } else if (entry.created_at.isValid()) {
    entry.last_access_at = entry.created_at;
  }
  const QJsonValue evict_pending = root.value(QStringLiteral("evict_pending"));
  entry.evict_pending = evict_pending.isBool() && evict_pending.toBool();

  // 数据文件一致性：complete 必须是 data.mp4 且大小匹配；否则走 .part 前缀。
  if (entry.complete) {
    if (!has_mp4) {
      return mark_broken(QStringLiteral("complete=true 但缺少 data.mp4"));
    }
    if (mp4_info.size() != entry.total_bytes) {
      return mark_broken(QStringLiteral("缓存大小不符：data.mp4 为 %1，meta 记录 %2")
                             .arg(mp4_info.size())
                             .arg(entry.total_bytes));
    }
    entry.data_path = mp4_path;
    entry.downloaded_bytes = entry.total_bytes;
    return entry;
  }

  if (has_part) {
    const qint64 part_bytes = part_info.size();
    if (part_bytes > entry.total_bytes) {
      return mark_broken(QStringLiteral("缓存前缀超过总字节：%1 > %2")
                             .arg(part_bytes)
                             .arg(entry.total_bytes));
    }
    entry.data_path = part_path;
    if (correct_downloaded && entry.downloaded_bytes != part_bytes) {
      // 崩溃后以 .part 实际长度为准（delta §4.4）；回写校正，不拼接双份。
      // 下载进度写盘走 UpdateProgress（correct_downloaded=false），由它节流写入。
      entry.downloaded_bytes = part_bytes;
      entry.meta_dirty = true;
    }
    return entry;
  }

  if (has_mp4 && mp4_info.size() == entry.total_bytes) {
    // 崩溃发生在改名成功、meta 落盘之前：按 data.mp4 修正为完成。
    entry.complete = true;
    entry.data_path = mp4_path;
    entry.downloaded_bytes = entry.total_bytes;
    entry.meta_dirty = true;
    return entry;
  }
  if (has_mp4) {
    return mark_broken(QStringLiteral("缺少 data.part，且 data.mp4 大小不符（%1 / %2）")
                           .arg(mp4_info.size())
                           .arg(entry.total_bytes));
  }
  return mark_broken(QStringLiteral("缺少 data.part"));
}

std::optional<CacheEntry> CacheStore::LoadLocked(const CacheKey& key,
                                                 QString* error,
                                                 bool correct_downloaded) {
  std::optional<CacheEntry> entry =
      LoadEntryLocked(key, error, correct_downloaded);
  if (entry && entry->meta_dirty && !entry->broken) {
    // 校正落盘失败不影响读取（下次还会再校正）；不把旧值当错误。
    entry->meta_dirty = false;
    WriteMetaLocked(*entry, nullptr);
  }
  return entry;
}

std::optional<CacheEntry> CacheStore::Load(const CacheKey& key, QString* error) {
  QMutexLocker locker(&mutex_);
  return LoadLocked(key, error);
}

void CacheStore::ScanLocked(QVector<CacheEntry>* out, QVector<CacheEntry>* broken,
                            QString* error) const {
  if (out != nullptr) {
    out->clear();
  }
  if (broken != nullptr) {
    broken->clear();
  }
  if (root_.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("缓存根为空");
    }
    return;
  }
  QDir root_dir(root_);
  if (!root_dir.exists()) {
    return;  // 还没建缓存：空扫描不是错误
  }
  const QFileInfoList bvid_dirs =
      root_dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QFileInfo& bvid_info : bvid_dirs) {
    const QString bvid = bvid_info.fileName();
    if (!IsValidCacheBvid(bvid)) {
      continue;  // 不是本工具的布局：不碰
    }
    QDir bvid_dir(bvid_info.absoluteFilePath());
    const QFileInfoList cid_dirs =
        bvid_dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& cid_info : cid_dirs) {
      qint64 cid = 0;
      if (!ParsePositiveInt64(cid_info.fileName(), kMinCacheCid, kMaxCacheCid,
                              &cid)) {
        continue;
      }
      QDir cid_dir(cid_info.absoluteFilePath());
      const QFileInfoList entry_dirs =
          cid_dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
      for (const QFileInfo& entry_info : entry_dirs) {
        const QString name = entry_info.fileName();
        if (!IsValidDirSegment(name)) {
          continue;
        }
        CacheKey key;
        key.bvid = bvid;
        key.cid = cid;
        if (name.startsWith(QStringLiteral("audio-"))) {
          // T8.10 DASH 音频条目：audio-<audio_id>。
          int audio_id = 0;
          if (!ParsePositiveInt(name.mid(6), kMinCodecId, kMaxQn, &audio_id)) {
            continue;
          }
          key.kind = CacheStreamKind::kAudio;
          key.audio_id = audio_id;
          key.qn = 0;
          key.codecid = 0;
        } else {
          const int dash = name.indexOf(QLatin1Char('-'));
          int qn = 0;
          int codecid = 0;
          if (dash <= 0 ||
              !ParsePositiveInt(name.left(dash), kMinQn, kMaxQn, &qn) ||
              !ParsePositiveInt(name.mid(dash + 1), kMinCodecId, kMaxCodecId,
                                &codecid)) {
            continue;
          }
          key.qn = qn;
          key.codecid = codecid;
        }
        const std::optional<CacheEntry> entry = LoadEntryLocked(key, nullptr);
        if (!entry.has_value()) {
          continue;
        }
        if (entry->broken) {
          if (broken != nullptr) {
            broken->push_back(*entry);
          }
        } else if (out != nullptr) {
          out->push_back(*entry);
        }
      }
    }
  }

  auto sort_entries = [](QVector<CacheEntry>* list) {
    if (list == nullptr) {
      return;
    }
    std::sort(list->begin(), list->end(), [](const CacheEntry& a, const CacheEntry& b) {
      if (a.key.bvid != b.key.bvid) {
        return a.key.bvid < b.key.bvid;
      }
      if (a.key.cid != b.key.cid) {
        return a.key.cid < b.key.cid;
      }
      if (a.key.kind != b.key.kind) {
        return static_cast<int>(a.key.kind) < static_cast<int>(b.key.kind);
      }
      if (a.key.kind == CacheStreamKind::kAudio &&
          a.key.audio_id != b.key.audio_id) {
        return a.key.audio_id < b.key.audio_id;
      }
      if (a.key.qn != b.key.qn) {
        return a.key.qn < b.key.qn;
      }
      return a.key.codecid < b.key.codecid;
    });
  };
  sort_entries(out);
  sort_entries(broken);
}

bool CacheStore::Scan(QVector<CacheEntry>* out, QVector<CacheEntry>* broken,
                      QString* error) {
  QMutexLocker locker(&mutex_);
  ScanLocked(out, broken, error);
  // 回写加载时发现的校正（.part 长度、崩溃后 .mp4 已完成）。
  auto repair = [this](QVector<CacheEntry>* list) {
    if (list == nullptr) {
      return;
    }
    for (CacheEntry& entry : *list) {
      if (!entry.meta_dirty || entry.broken) {
        continue;
      }
      entry.meta_dirty = false;
      WriteMetaLocked(entry, nullptr);
    }
  };
  repair(out);
  repair(broken);
  return true;
}

qint64 CacheStore::UsageLocked() const {
  QVector<CacheEntry> entries;
  QVector<CacheEntry> broken;
  ScanLocked(&entries, &broken, nullptr);
  qint64 total = 0;
  for (const CacheEntry& entry : entries) {
    total += entry.usage_bytes;
  }
  for (const CacheEntry& entry : broken) {
    total += entry.usage_bytes;
  }
  return total;
}

qint64 CacheStore::UsageBytes() const {
  QMutexLocker locker(&mutex_);
  return UsageLocked();
}

bool CacheStore::Reserve(const CacheKey& key, qint64 expected_bytes,
                         QString* error) {
  QMutexLocker locker(&mutex_);
  return ReserveLocked(key, expected_bytes, error);
}

bool CacheStore::ReserveLocked(const CacheKey& key, qint64 expected_bytes,
                               QString* error) {
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (!IsValidCacheKey(key)) {
    return fail(QStringLiteral("缓存键不合法（只接受合法 bvid/cid/qn/codecid）：%1")
                    .arg(CacheKeyLabel(key)));
  }
  if (expected_bytes <= 0 || expected_bytes > 9'000'000'000'000'000LL) {
    return fail(QStringLiteral("预计缓存大小无效：%1 字节").arg(expected_bytes));
  }
  if (quota_ < kMinQuotaBytes) {
    return fail(QStringLiteral("缓存配额未设置"));
  }
  if (!EnsureRootLocked(error)) {
    return false;
  }

  QString load_error;
  const std::optional<CacheEntry> existing = LoadLocked(key, &load_error);
  if (existing.has_value() && existing->broken) {
    return fail(QStringLiteral("缓存已损坏，需要重建：%1").arg(existing->broken_reason));
  }
  if (existing.has_value() && existing->complete) {
    // 缓存命中：不重新下载、不重新预检；只更新 LRU 访问时间。
    CacheEntry hit = *existing;
    TouchLocked(hit, nullptr);
    return true;
  }
  if (existing.has_value() && existing->total_bytes > 0 &&
      existing->total_bytes != expected_bytes) {
    return fail(
        QStringLiteral("已缓存记录的总字节与本次不一致（缓存 %1，本次 %2），需要重建缓存")
            .arg(FormatBytes(existing->total_bytes), FormatBytes(expected_bytes)));
  }

  const qint64 already = existing.has_value()
                             ? std::min(existing->downloaded_bytes, expected_bytes)
                             : 0;
  const qint64 additional = expected_bytes - already;

  // 1) 空间预检：可用空间 ≥ 预计总字节 + 512MB；不足直接失败，不开始下载。
  QString disk_error;
  const qint64 free_bytes = FreeDiskBytes(root_, &disk_error);
  if (free_bytes < 0) {
    return fail(disk_error.isEmpty() ? QStringLiteral("无法读取磁盘可用空间")
                                     : disk_error);
  }
  const qint64 required = expected_bytes + kSpaceHeadroomBytes;
  if (free_bytes < required) {
    return fail(QStringLiteral("磁盘空间不足：需要 %1，可用 %2")
                    .arg(FormatBytes(required), FormatBytes(free_bytes)));
  }

  // 2) 配额：超出时按 last_access_at 从旧到新淘汰非当前条目。
  const qint64 usage = UsageLocked();
  if (usage + additional > quota_) {
    const qint64 target = quota_ - additional;
    if (target < 0) {
      return fail(QStringLiteral("缓存配额不足：本次需要 %1，配额 %2")
                      .arg(FormatBytes(expected_bytes), FormatBytes(quota_)));
    }
    if (!EvictUntilLocked(target, key, error)) {
      return false;
    }
  }

  // 3) 建目录与空 .part，再写 meta（顺序：数据文件先、meta 后；崩溃留孤儿可清理）。
  const QString dir = EntryDirLocked(key);
  if (dir.isEmpty() || !QDir().mkpath(dir)) {
    return fail(QStringLiteral("无法创建缓存目录：%1").arg(dir));
  }
  const QString part_path = PartPathLocked(key);
  {
    QFile part(part_path);
    if (!part.open(QIODevice::WriteOnly | QIODevice::Append)) {
      return fail(QStringLiteral("无法创建缓存数据文件：%1").arg(part.errorString()));
    }
    part.close();
  }

  CacheEntry entry;
  if (existing.has_value()) {
    entry = *existing;
  }
  entry.key = key;
  entry.dir = dir;
  entry.data_path = part_path;
  entry.total_bytes = expected_bytes;
  entry.complete = false;
  entry.broken = false;
  entry.meta_present = true;
  entry.data_bytes = QFileInfo(part_path).size();
  entry.downloaded_bytes = entry.data_bytes;
  if (!entry.created_at.isValid()) {
    entry.created_at = QDateTime::currentDateTimeUtc();
  }
  entry.last_access_at = QDateTime::currentDateTimeUtc();
  if (!WriteMetaLocked(entry, error)) {
    return false;
  }
  return true;
}

bool CacheStore::OpenPart(const CacheKey& key, QString* part_path,
                          qint64* existing_bytes, QString* error) {
  QMutexLocker locker(&mutex_);
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (!IsValidCacheKey(key)) {
    return fail(QStringLiteral("缓存键不合法：%1").arg(CacheKeyLabel(key)));
  }
  const std::optional<CacheEntry> entry = LoadLocked(key, error);
  if (!entry.has_value()) {
    return fail(QStringLiteral("缓存条目不存在，请先 Reserve：%1").arg(CacheKeyLabel(key)));
  }
  if (entry->broken) {
    return fail(entry->broken_reason.isEmpty()
                    ? QStringLiteral("缓存已损坏，需要重建")
                    : entry->broken_reason);
  }
  if (entry->complete) {
    return fail(QStringLiteral("缓存已完成（data.mp4），请使用 OpenComplete"));
  }

  const QString part = PartPathLocked(key);
  if (!QFileInfo::exists(part)) {
    QFile create(part);
    if (!create.open(QIODevice::WriteOnly | QIODevice::Append)) {
      return fail(QStringLiteral("无法创建缓存数据文件：%1").arg(create.errorString()));
    }
    create.close();
  }
  const qint64 size = QFileInfo(part).size();
  CacheEntry updated = *entry;
  updated.data_path = part;
  updated.data_bytes = size;
  updated.downloaded_bytes = size;
  updated.last_access_at = QDateTime::currentDateTimeUtc();
  if (!WriteMetaLocked(updated, error)) {
    return false;
  }
  if (part_path != nullptr) {
    *part_path = part;
  }
  if (existing_bytes != nullptr) {
    *existing_bytes = size;
  }
  return true;
}

bool CacheStore::UpdateProgress(const CacheKey& key, qint64 downloaded_bytes,
                                bool force) {
  QMutexLocker locker(&mutex_);
  if (!IsValidCacheKey(key) || downloaded_bytes < 0) {
    return false;
  }
  const std::optional<CacheEntry> entry = LoadLocked(key, nullptr, false);
  if (!entry.has_value() || entry->broken || entry->complete) {
    return false;
  }
  qint64 value = std::min(downloaded_bytes, entry->total_bytes);
  if (value < entry->downloaded_bytes) {
    value = entry->downloaded_bytes;  // 单调：旧值不回退
  }
  const QString label = CacheKeyLabel(key);
  const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
  const auto it = last_progress_write_ms_.constFind(label);
  if (!force && it != last_progress_write_ms_.constEnd() &&
      now_ms - it.value() < kProgressWriteIntervalMs) {
    return true;  // 节流：本次不写盘
  }
  CacheEntry updated = *entry;
  updated.downloaded_bytes = value;
  updated.last_access_at = QDateTime::currentDateTimeUtc();
  if (!WriteMetaLocked(updated, nullptr)) {
    return false;
  }
  last_progress_write_ms_.insert(label, now_ms);
  return true;
}

bool CacheStore::TouchLocked(CacheEntry& entry, QString* error) {
  entry.last_access_at = QDateTime::currentDateTimeUtc();
  return WriteMetaLocked(entry, error);
}

bool CacheStore::Touch(const CacheKey& key, QString* error) {
  QMutexLocker locker(&mutex_);
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (!IsValidCacheKey(key)) {
    return fail(QStringLiteral("缓存键不合法：%1").arg(CacheKeyLabel(key)));
  }
  const std::optional<CacheEntry> entry = LoadLocked(key, error);
  if (!entry.has_value()) {
    return fail(QStringLiteral("缓存条目不存在：%1").arg(CacheKeyLabel(key)));
  }
  if (entry->broken) {
    return fail(entry->broken_reason);
  }
  CacheEntry updated = *entry;
  return TouchLocked(updated, error);
}

bool CacheStore::SetMetadata(const CacheKey& key, const CacheMetadata& metadata,
                             QString* error) {
  QMutexLocker locker(&mutex_);
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (!IsValidCacheKey(key)) {
    return fail(QStringLiteral("缓存键不合法：%1").arg(CacheKeyLabel(key)));
  }
  if (metadata.duration_ms < 0 || metadata.width < 0 || metadata.height < 0 ||
      !std::isfinite(metadata.fps) || metadata.fps < 0.0 ||
      metadata.audio_id < 0 || metadata.audio_id > 100000) {
    return fail(QStringLiteral("缓存元数据不合法"));
  }
  const std::optional<CacheEntry> entry = LoadLocked(key, error);
  if (!entry.has_value()) {
    return fail(QStringLiteral("缓存条目不存在：%1").arg(CacheKeyLabel(key)));
  }
  if (entry->broken) {
    return fail(entry->broken_reason);
  }
  CacheEntry updated = *entry;
  updated.title = metadata.title;
  updated.owner = metadata.owner;
  updated.duration_ms = metadata.duration_ms;
  updated.width = metadata.width;
  updated.height = metadata.height;
  updated.fps = metadata.fps;
  updated.source_fps_hint = metadata.source_fps_hint;
  updated.dash = metadata.dash;
  updated.audio_id = metadata.audio_id;
  updated.last_access_at = QDateTime::currentDateTimeUtc();
  return WriteMetaLocked(updated, error);
}

bool CacheStore::MarkComplete(const CacheKey& key, QString* error) {
  QMutexLocker locker(&mutex_);
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (!IsValidCacheKey(key)) {
    return fail(QStringLiteral("缓存键不合法：%1").arg(CacheKeyLabel(key)));
  }
  const std::optional<CacheEntry> entry = LoadLocked(key, error);
  if (!entry.has_value()) {
    return fail(QStringLiteral("缓存条目不存在：%1").arg(CacheKeyLabel(key)));
  }
  if (entry->broken) {
    return fail(entry->broken_reason);
  }
  if (entry->complete) {
    return true;  // 幂等
  }
  const QString part = PartPathLocked(key);
  const QString mp4 = Mp4PathLocked(key);
  const QFileInfo part_info(part);
  if (!part_info.exists() || !part_info.isFile()) {
    return fail(QStringLiteral("缺少 data.part，无法完成缓存"));
  }
  if (part_info.size() != entry->total_bytes) {
    return fail(QStringLiteral("缓存数据不完整：已下载 %1，需要 %2")
                    .arg(FormatBytes(part_info.size()), FormatBytes(entry->total_bytes)));
  }
  QString rename_error;
  if (!RenameLongPath(part, mp4, &rename_error)) {
    return fail(QStringLiteral("缓存文件改名失败：%1（.part 已保留）").arg(rename_error));
  }
  CacheEntry updated = *entry;
  updated.complete = true;
  updated.data_path = mp4;
  updated.data_bytes = entry->total_bytes;
  updated.downloaded_bytes = entry->total_bytes;
  updated.last_access_at = QDateTime::currentDateTimeUtc();
  if (!WriteMetaLocked(updated, error)) {
    const QString detail = error != nullptr ? *error : QString();
    return fail(QStringLiteral("data.mp4 已就绪，但 meta.json 写入失败：%1").arg(detail));
  }
  return true;
}

bool CacheStore::OpenComplete(const CacheKey& key, QString* mp4_path,
                              QString* error) {
  QMutexLocker locker(&mutex_);
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (!IsValidCacheKey(key)) {
    return fail(QStringLiteral("缓存键不合法：%1").arg(CacheKeyLabel(key)));
  }
  const std::optional<CacheEntry> entry = LoadLocked(key, error);
  if (!entry.has_value()) {
    return fail(QStringLiteral("缓存条目不存在：%1").arg(CacheKeyLabel(key)));
  }
  if (entry->broken) {
    return fail(entry->broken_reason);
  }
  if (!entry->complete) {
    return fail(QStringLiteral("缓存尚未完成：已下载 %1 / %2")
                    .arg(FormatBytes(entry->downloaded_bytes),
                         FormatBytes(entry->total_bytes)));
  }
  const QFileInfo mp4(entry->data_path);
  if (!mp4.exists() || !mp4.isFile()) {
    return fail(QStringLiteral("缓存文件不存在：data.mp4"));
  }
  if (mp4.size() != entry->total_bytes) {
    return fail(QStringLiteral("缓存大小不符：data.mp4 为 %1，meta 记录 %2")
                    .arg(FormatBytes(mp4.size()), FormatBytes(entry->total_bytes)));
  }
  CacheEntry touched = *entry;
  TouchLocked(touched, nullptr);
  if (mp4_path != nullptr) {
    *mp4_path = entry->data_path;
  }
  return true;
}

bool CacheStore::DeleteEntryDataLocked(const CacheEntry& entry, bool delete_meta,
                                       qint64* removed_bytes, bool* busy,
                                       QString* error) {
  return DeleteEntryFiles(entry.dir, delete_meta, removed_bytes, busy, error);
}

bool CacheStore::EvictUntilLocked(qint64 target_usage, const CacheKey& current,
                                  QString* error) {
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  QVector<CacheEntry> entries;
  QVector<CacheEntry> broken;
  ScanLocked(&entries, &broken, nullptr);
  QVector<CacheEntry> candidates;
  candidates.reserve(entries.size() + broken.size());
  for (const CacheEntry& entry : entries) {
    if (!MatchesProtectedKey(entry.key, current)) {
      candidates.push_back(entry);
    }
  }
  for (const CacheEntry& entry : broken) {
    if (!MatchesProtectedKey(entry.key, current)) {
      candidates.push_back(entry);
    }
  }
  // LRU：last_access_at 最旧优先；同一时间按标签稳定排序。
  std::sort(candidates.begin(), candidates.end(),
            [](const CacheEntry& a, const CacheEntry& b) {
              if (a.last_access_at != b.last_access_at) {
                return a.last_access_at < b.last_access_at;
              }
              return CacheKeyLabel(a.key) < CacheKeyLabel(b.key);
            });

  qint64 usage = 0;
  for (const CacheEntry& entry : entries) {
    usage += entry.usage_bytes;
  }
  for (const CacheEntry& entry : broken) {
    usage += entry.usage_bytes;
  }

  int busy_count = 0;
  QString first_busy;
  for (CacheEntry& candidate : candidates) {
    if (usage <= target_usage) {
      break;
    }
    qint64 removed = 0;
    bool busy = false;
    QString delete_error;
    DeleteEntryDataLocked(candidate, true, &removed, &busy, &delete_error);
    usage -= removed;
    if (busy) {
      ++busy_count;
      if (first_busy.isEmpty()) {
        first_busy = CacheKeyLabel(candidate.key);
      }
      // 标记待淘汰：会话结束后（下次 Evict/Cleanup）再删。
      if (!candidate.meta_present || !candidate.broken) {
        CacheEntry pending = candidate;
        pending.evict_pending = true;
        WriteMetaLocked(pending, nullptr);
      }
      continue;
    }
    if (!delete_error.isEmpty()) {
      continue;  // 单个条目删除失败不中断；总占用不达标时会报错
    }
    RemoveEmptyParents(candidate.dir, root_);
  }

  if (usage > target_usage) {
    if (busy_count > 0) {
      return fail(QStringLiteral("缓存配额不足：有 %1 个条目正被读取（已标记待淘汰，"
                                 "首个 %2）；当前占用 %3，配额 %4")
                      .arg(busy_count)
                      .arg(first_busy, FormatBytes(usage), FormatBytes(quota_)));
    }
    return fail(QStringLiteral("缓存配额不足：当前占用 %1，配额 %2（可淘汰的条目已用尽）")
                    .arg(FormatBytes(usage), FormatBytes(quota_)));
  }
  return true;
}

bool CacheStore::EvictToQuota(const CacheKey& current, QString* error) {
  QMutexLocker locker(&mutex_);
  if (quota_ < kMinQuotaBytes) {
    if (error != nullptr) {
      *error = QStringLiteral("缓存配额未设置");
    }
    return false;
  }
  if (UsageLocked() <= quota_) {
    return true;
  }
  return EvictUntilLocked(quota_, current, error);
}

bool CacheStore::Rebuild(const CacheKey& key, QString* error) {
  QMutexLocker locker(&mutex_);
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (!IsValidCacheKey(key)) {
    return fail(QStringLiteral("缓存键不合法：%1").arg(CacheKeyLabel(key)));
  }
  const std::optional<CacheEntry> entry = LoadEntryLocked(key, nullptr);
  if (!entry.has_value()) {
    return true;  // 不存在 = 已重建
  }
  qint64 removed = 0;
  bool busy = false;
  QString delete_error;
  const bool ok = DeleteEntryDataLocked(*entry, true, &removed, &busy, &delete_error);
  if (!ok) {
    Q_UNUSED(removed);
    return fail(delete_error.isEmpty() ? QStringLiteral("缓存重建失败") : delete_error);
  }
  RemoveEmptyParents(entry->dir, root_);
  return true;
}

bool CacheStore::ClearAll(QString* error, qint64* removed_bytes,
                          const CacheKey* keep, int* kept_entries) {
  QMutexLocker locker(&mutex_);
  if (removed_bytes != nullptr) {
    *removed_bytes = 0;
  }
  if (kept_entries != nullptr) {
    *kept_entries = 0;
  }
  QVector<CacheEntry> entries;
  QVector<CacheEntry> broken;
  ScanLocked(&entries, &broken, nullptr);
  // 缓存根必须存在；不存在时清空是幂等的成功。
  qint64 removed_total = 0;
  int kept_total = 0;
  bool ok = true;
  QString first_error;
  auto clear_list = [&](const QVector<CacheEntry>& list) {
    for (const CacheEntry& entry : list) {
      // T8.3.1：当前在线会话（含 bvid+cid 部分键）不删，避免清空把正在播放/
      // 正在下载的条目抽走；其它条目照常删除。
      if (keep != nullptr && MatchesProtectedKey(entry.key, *keep)) {
        ++kept_total;
        continue;
      }
      qint64 removed = 0;
      bool busy = false;
      QString delete_error;
      const bool cleared =
          DeleteEntryDataLocked(entry, true, &removed, &busy, &delete_error);
      removed_total += removed;
      if (!cleared) {
        ok = false;
        if (first_error.isEmpty()) {
          first_error = delete_error;
        }
      }
      RemoveEmptyParents(entry.dir, root_);
    }
  };
  clear_list(entries);
  clear_list(broken);
  if (removed_bytes != nullptr) {
    *removed_bytes = removed_total;
  }
  if (kept_entries != nullptr) {
    *kept_entries = kept_total;
  }
  if (!ok) {
    if (error != nullptr) {
      *error = first_error.isEmpty() ? QStringLiteral("清空缓存失败") : first_error;
    }
    return false;
  }
  return true;
}

bool CacheStore::CleanupOrphans(const CacheKey& current, QString* error,
                                CleanupOptions options, qint64* removed_bytes,
                                int* removed_entries) {
  QMutexLocker locker(&mutex_);
  Q_UNUSED(error);  // 清理是按条目尽力而为：读句柄占用留到下一轮，不算失败
  if (removed_bytes != nullptr) {
    *removed_bytes = 0;
  }
  if (removed_entries != nullptr) {
    *removed_entries = 0;
  }
  if (!options.enabled) {
    return true;  // 可关闭
  }
  QVector<CacheEntry> entries;
  QVector<CacheEntry> broken;
  ScanLocked(&entries, &broken, nullptr);
  const int hours = std::max(0, options.incomplete_max_age_hours);
  const QDateTime cutoff =
      QDateTime::currentDateTimeUtc().addSecs(-static_cast<qint64>(hours) * 3600);
  qint64 removed_total = 0;
  int removed_count = 0;

  auto cleanup_list = [&](const QVector<CacheEntry>& list) {
    for (const CacheEntry& entry : list) {
      if (MatchesProtectedKey(entry.key, current)) {
        continue;  // 当前会话永不清理（支持 bvid+cid 部分键）
      }
      bool should_remove = false;
      if (!entry.meta_present) {
        should_remove = true;  // 无 meta 的孤儿数据（.part/.mp4）
      } else if (entry.evict_pending) {
        should_remove = true;  // 上次因读句柄未淘汰
      } else if (!entry.complete && entry.last_access_at.isValid() &&
                 entry.last_access_at < cutoff) {
        should_remove = true;  // 超过 N 小时未访问的未完成条目
      }
      if (!should_remove) {
        continue;
      }
      qint64 removed = 0;
      bool busy = false;
      QString delete_error;
      const bool cleared = DeleteEntryDataLocked(entry, true, &removed, &busy,
                                                 &delete_error);
      removed_total += removed;
      if (!cleared) {
        // 仍被读取：下一轮再试；不把整个清理判失败。
        continue;
      }
      ++removed_count;
      RemoveEmptyParents(entry.dir, root_);
    }
  };
  cleanup_list(entries);
  cleanup_list(broken);
  if (removed_bytes != nullptr) {
    *removed_bytes = removed_total;
  }
  if (removed_entries != nullptr) {
    *removed_entries = removed_count;
  }
  return true;
}

}  // namespace lapian::online
