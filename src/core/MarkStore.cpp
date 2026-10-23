#include "core/MarkStore.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStandardPaths>
#include <QString>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

namespace lapian::core {
namespace {

QString FromUtf8(const std::string& text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

std::string ToUtf8(const QString& text) { return text.toUtf8().toStdString(); }

#ifdef _WIN32
// T7.5：ReplaceFileW/MoveFileExW 不会自动加长路径前缀。sidecar 位于 >MAX_PATH
// 的中文长目录时，Qt 能创建临时文件，但 Win32 替换会失败并误回退 AppData。
// 这里显式转成 `\\?\C:\...` / `\\?\UNC\server\share\...`。
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

// 绝对化 + 解析 . / .. + 正斜杠转反斜杠；存在的本地文件再解析 8.3/符号链接。
// UNC 不做文件系统解析，避免打开不存在/离线的网络路径时阻塞。
QString CleanAbsolutePath(const QString& input) {
  if (input.isEmpty()) {
    return {};
  }
  QString path = input;
  path.replace(QLatin1Char('/'), QLatin1Char('\\'));

  if (path.startsWith(QLatin1String("\\\\?\\UNC\\"), Qt::CaseInsensitive)) {
    path = QStringLiteral("\\\\") + path.mid(8);
  } else if (path.startsWith(QLatin1String("\\\\?\\"), Qt::CaseInsensitive)) {
    path = path.mid(4);
  }

  QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  absolute.replace(QLatin1Char('/'), QLatin1Char('\\'));

  const bool is_unc = absolute.startsWith(QLatin1String("\\\\"));
  if (!is_unc) {
    const QString canonical = QFileInfo(absolute).canonicalFilePath();
    if (!canonical.isEmpty()) {
      absolute = QDir::cleanPath(canonical);
      absolute.replace(QLatin1Char('/'), QLatin1Char('\\'));
    }
  }

  // canonicalFilePath/absoluteFilePath 也可能返回 \\?\ 前缀；统一去掉便于哈希与显示。
  if (absolute.startsWith(QLatin1String("\\\\?\\UNC\\"), Qt::CaseInsensitive)) {
    absolute = QStringLiteral("\\\\") + absolute.mid(8);
  } else if (absolute.startsWith(QLatin1String("\\\\?\\"), Qt::CaseInsensitive)) {
    absolute = absolute.mid(4);
  }

  while (absolute.size() > 3 && absolute.endsWith(QLatin1Char('\\'))) {
    absolute.chop(1);
  }
  if (absolute.size() == 2 && absolute.at(1) == QLatin1Char(':')) {
    absolute += QLatin1Char('\\');
  }
  return absolute;
}

QString DefaultAppDataMarksDir() {
  QString appdata = qEnvironmentVariable("APPDATA");
  if (appdata.isEmpty()) {
    appdata = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  }
  if (appdata.isEmpty()) {
    appdata = QDir::homePath() + QStringLiteral("/AppData/Roaming");
  }
  return QDir(appdata).filePath(QString::fromUtf8("拉片软件/marks"));
}

// T8.7：%APPDATA%\拉片软件\online（不叠加第二层“拉片软件”）。
QString DefaultAppDataOnlineDir() {
  QString appdata = qEnvironmentVariable("APPDATA");
  if (appdata.isEmpty()) {
    appdata = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  }
  if (appdata.isEmpty()) {
    appdata = QDir::homePath() + QStringLiteral("/AppData/Roaming");
  }
  return QDir(appdata).filePath(QString::fromUtf8("拉片软件/online"));
}

bool IsSafeOnlineBvid(const QString& bvid) {
  if (bvid.isEmpty() || bvid.size() > 32) {
    return false;
  }
  for (const QChar ch : bvid) {
    const ushort code = ch.unicode();
    const bool ascii_alnum =
        (code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z') ||
        (code >= 'a' && code <= 'z');
    if (!ascii_alnum) {
      return false;
    }
  }
  return true;
}

QString OnlineRecordPath(const MarkStoreOptions& options, const QString& bvid,
                         qint64 cid) {
  const QString root = options.appdata_online_dir.empty()
                           ? DefaultAppDataOnlineDir()
                           : FromUtf8(options.appdata_online_dir);
  return QDir(root).filePath(
      QStringLiteral("%1-%2.lapian.json").arg(bvid).arg(cid));
}

std::string NowIso8601() {
  return ToUtf8(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
}

QDateTime ParseTimestamp(const std::string& text) {
  if (text.empty()) {
    return {};
  }
  const QString value = FromUtf8(text);
  QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
  if (!parsed.isValid()) {
    parsed = QDateTime::fromString(value, Qt::ISODate);
  }
  if (parsed.isValid()) {
    parsed = parsed.toUTC();
  }
  return parsed;
}

bool IsNewer(const Mark& candidate, const Mark& current) {
  const QDateTime candidate_time = ParseTimestamp(candidate.updated_at);
  const QDateTime current_time = ParseTimestamp(current.updated_at);
  if (candidate_time.isValid() && current_time.isValid()) {
    return candidate_time > current_time;
  }
  if (candidate_time.isValid() != current_time.isValid()) {
    return candidate_time.isValid();
  }
  return candidate.updated_at > current.updated_at;
}

void SortMarks(std::vector<Mark>* marks) {
  std::sort(marks->begin(), marks->end(), [](const Mark& a, const Mark& b) {
    if (a.frame != b.frame) {
      return a.frame < b.frame;
    }
    if (a.pts_us != b.pts_us) {
      return a.pts_us < b.pts_us;
    }
    if (a.created_at != b.created_at) {
      return a.created_at < b.created_at;
    }
    return a.id < b.id;
  });
}

std::vector<Mark> DedupeMarks(const std::vector<Mark>& marks) {
  std::vector<Mark> result;
  for (const Mark& mark : marks) {
    auto existing = std::find_if(result.begin(), result.end(),
                                 [&](const Mark& item) { return item.id == mark.id; });
    if (existing == result.end()) {
      result.push_back(mark);
    } else if (IsNewer(mark, *existing)) {
      *existing = mark;
    }
  }
  return result;
}

bool JsonInt64(const QJsonValue& value, qint64* out) {
  if (!value.isDouble()) {
    return false;
  }
  const double number = value.toDouble();
  if (!std::isfinite(number) || std::floor(number) != number) {
    return false;
  }
  if (number < static_cast<double>(std::numeric_limits<qint64>::min()) ||
      number > static_cast<double>(std::numeric_limits<qint64>::max())) {
    return false;
  }
  *out = static_cast<qint64>(number);
  return true;
}

bool JsonDouble(const QJsonValue& value, double* out) {
  if (!value.isDouble()) {
    return false;
  }
  const double number = value.toDouble();
  if (!std::isfinite(number)) {
    return false;
  }
  *out = number;
  return true;
}

QJsonObject MarkToJson(const Mark& mark) {
  QJsonObject object;
  object.insert(QStringLiteral("id"), FromUtf8(mark.id));
  object.insert(QStringLiteral("frame"), static_cast<double>(mark.frame));
  object.insert(QStringLiteral("pts"), static_cast<double>(mark.pts_us));
  object.insert(QStringLiteral("label"), FromUtf8(mark.label));
  object.insert(QStringLiteral("note"), FromUtf8(mark.note));
  object.insert(QStringLiteral("created_at"), FromUtf8(mark.created_at));
  object.insert(QStringLiteral("updated_at"), FromUtf8(mark.updated_at));
  return object;
}

QJsonObject OnlineMarkToJson(const Mark& mark, const OnlineMarkMeta& meta) {
  QJsonObject object = MarkToJson(mark);
  object.insert(QStringLiteral("fps"), std::isfinite(meta.fps) ? meta.fps : 0.0);
  object.insert(QStringLiteral("qn"), meta.qn);
  object.insert(QStringLiteral("codecid"), meta.codecid);
  return object;
}

std::string JoinErrorMessage(const char* prefix, const std::string& detail) {
  return std::string(prefix) + detail;
}

struct LoadedFile {
  bool exists = false;
  bool loaded = false;
  bool read_failed = false;
  bool unsupported = false;
  std::vector<Mark> marks;
  VideoIdentity video;
};

bool BackupCorruptFile(const QString& path, QString* backup_path, std::string* error) {
  QString candidate = path + QStringLiteral(".bak");
  int suffix = 1;
  while (QFileInfo::exists(candidate)) {
    candidate = path + QStringLiteral(".bak.") + QString::number(suffix++);
  }
  if (QFile::rename(path, candidate)) {
    if (backup_path != nullptr) {
      *backup_path = candidate;
    }
    return true;
  }

  // rename 失败时退化为 copy+remove；两端同目录，正常不会走到这里。
  const QString rename_error = QStringLiteral("rename 失败");
  if (!QFile::copy(path, candidate)) {
    if (error != nullptr) {
      *error = ToUtf8(QStringLiteral("备份损坏文件失败（%1）：%2")
                          .arg(rename_error, path));
    }
    return false;
  }
  if (!QFile::remove(path)) {
    QFile::remove(candidate);
    if (error != nullptr) {
      *error = ToUtf8(QStringLiteral("已复制备份但无法移除损坏文件：%1").arg(path));
    }
    return false;
  }
  if (backup_path != nullptr) {
    *backup_path = candidate;
  }
  return true;
}

LoadedFile LoadOne(const QString& path, std::vector<MarkWarning>* warnings) {
  LoadedFile result;
  const QFileInfo info(path);
  if (!info.exists()) {
    return result;
  }
  result.exists = true;
  if (!info.isFile()) {
    // 路径被目录/其他对象占用：不是我们的附属文件，不能删也不能读；
    // 不阻塞保存，写 sidecar 失败后照常回退 AppData。
    result.exists = false;
    warnings->push_back({MarkWarningKind::kReadFailed,
                         "标记文件路径不是普通文件，已忽略", ToUtf8(path)});
    return result;
  }

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    warnings->push_back({MarkWarningKind::kReadFailed,
                         "无法读取标记文件：" + ToUtf8(file.errorString()),
                         ToUtf8(path)});
    result.read_failed = true;
    return result;
  }
  const QByteArray bytes = file.readAll();
  const bool read_error = file.error() != QFileDevice::NoError;
  const QString read_error_text = file.errorString();
  file.close();
  if (read_error) {
    warnings->push_back({MarkWarningKind::kReadFailed,
                         "读取标记文件失败：" + ToUtf8(read_error_text),
                         ToUtf8(path)});
    result.read_failed = true;
    return result;
  }

  std::string parse_error;
  bool unsupported = false;
  if (!ParseMarkFile(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())),
                     &result.marks, &result.video, &parse_error, &unsupported)) {
    result.marks.clear();
    result.video = {};
    if (unsupported) {
      result.unsupported = true;
      warnings->push_back({MarkWarningKind::kUnsupportedSchema,
                           "标记文件由更新版本创建，暂不读取且不会覆盖：" + parse_error,
                           ToUtf8(path)});
      return result;
    }

    QString backup_path;
    std::string backup_error;
    if (BackupCorruptFile(path, &backup_path, &backup_error)) {
      result.exists = false;
      warnings->push_back({MarkWarningKind::kCorruptBackup,
                           "标记文件损坏，已备份为 " + ToUtf8(backup_path) +
                               "；按无标记继续打开：" + parse_error,
                           ToUtf8(path)});
    } else {
      result.read_failed = true;
      warnings->push_back({MarkWarningKind::kReadFailed,
                           "标记文件损坏且备份失败，已阻止覆盖：" + backup_error +
                               "；" + parse_error,
                           ToUtf8(path)});
    }
    return result;
  }

  result.loaded = true;
  return result;
}

void CheckIdentity(const LoadedFile& loaded, const VideoIdentity& current,
                   const std::string& path, std::vector<MarkWarning>* warnings) {
  if (!loaded.loaded || loaded.video.empty() || loaded.marks.empty()) {
    return;
  }
  const bool path_diff =
      !loaded.video.path_key.empty() && loaded.video.path_key != current.path_key;
  const bool size_diff =
      loaded.video.size != 0 && current.size != 0 && loaded.video.size != current.size;
  const bool duration_diff =
      loaded.video.duration_us != 0 && current.duration_us != 0 &&
      std::llabs(loaded.video.duration_us - current.duration_us) > 1000;
  if (path_diff && (size_diff || duration_diff)) {
    warnings->push_back(
        {MarkWarningKind::kIdentityMismatch,
         "标记文件的视频标识与当前文件不一致（视频可能已被替换）；已按文件位置加载",
         path});
  }
}

bool RemoveRegularFile(const QString& path, const std::string& failure_message,
                       std::vector<MarkWarning>* warnings, std::string* error,
                       MarkWarningKind kind = MarkWarningKind::kSaveFailed) {
  if (path.isEmpty()) {
    return false;
  }
  const QFileInfo info(path);
  if (!info.exists()) {
    return false;
  }
  if (!info.isFile()) {
    // 目录/设备不是本模块创建的附属文件；只提示，不当作删除失败。
    if (warnings != nullptr) {
      warnings->push_back({kind, failure_message + "（路径不是普通文件，未删除）",
                           ToUtf8(path)});
    }
    return false;
  }
  if (QFile::remove(path)) {
    return true;
  }
  if (warnings != nullptr) {
    warnings->push_back({kind, failure_message, ToUtf8(path)});
  }
  if (error != nullptr && error->empty()) {
    *error = failure_message;
  }
  return false;
}

// 同目录临时文件 -> write -> flush -> FlushFileBuffers -> ReplaceFile/MoveFileEx。
// 任一步失败都删除临时文件，目标旧文件保持不变。
bool WriteAtomically(const QString& target, const QByteArray& bytes, std::string* error) {
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = ToUtf8(message);
    }
    return false;
  };

  if (target.isEmpty()) {
    return fail(QStringLiteral("写入路径为空"));
  }

  const QFileInfo target_info(target);
  QDir directory = target_info.absoluteDir();
  if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
    return fail(QStringLiteral("无法创建标记目录：") + directory.absolutePath());
  }

  const QString temp_path =
      target + QStringLiteral(".tmp-") +
      QUuid::createUuid().toString(QUuid::WithoutBraces);
  QFile temp_file(temp_path);
  if (!temp_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    return fail(QStringLiteral("无法创建临时文件：") + temp_file.errorString());
  }

  bool ok = true;
  QString write_error;
  if (temp_file.write(bytes) != bytes.size()) {
    ok = false;
    write_error = QStringLiteral("写入临时文件不完整：") + temp_file.errorString();
  }
  if (ok && !temp_file.flush()) {
    ok = false;
    write_error = QStringLiteral("刷新临时文件失败：") + temp_file.errorString();
  }
#ifdef _WIN32
  if (ok) {
    const intptr_t native = _get_osfhandle(temp_file.handle());
    if (native == -1 || !FlushFileBuffers(reinterpret_cast<HANDLE>(native))) {
      ok = false;
      write_error = QStringLiteral("落盘失败（Win32 %1）").arg(GetLastError());
    }
  }
#endif
  temp_file.close();
  if (!ok) {
    QFile::remove(temp_path);
    return fail(write_error);
  }

#ifdef _WIN32
  const std::wstring temp_native = NativeLongPath(temp_path);
  const std::wstring target_native = NativeLongPath(target);
  bool replaced = false;
  if (QFileInfo::exists(target)) {
    replaced = ReplaceFileW(target_native.c_str(), temp_native.c_str(), nullptr,
                            REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) != 0;
  }
  if (!replaced) {
    replaced = MoveFileExW(temp_native.c_str(), target_native.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  }
  if (!replaced) {
    const DWORD last_error = GetLastError();
    QFile::remove(temp_path);
    return fail(QStringLiteral("替换标记文件失败（Win32 %1）").arg(last_error));
  }
#else
  if (QFileInfo::exists(target) && !QFile::remove(target)) {
    QFile::remove(temp_path);
    return fail(QStringLiteral("替换标记文件失败"));
  }
  if (!QFile::rename(temp_path, target)) {
    QFile::remove(temp_path);
    return fail(QStringLiteral("重命名临时文件失败"));
  }
#endif
  return true;
}

SaveResult SaveMarksInternal(const VideoIdentity& video, const std::vector<Mark>& marks,
                             const MarkStoreOptions& options,
                             std::vector<MarkWarning>* warnings) {
  SaveResult result;
  const MarkPaths paths = ResolveMarkPaths(video.path, options);
  result.path = paths.sidecar;
  if (paths.sidecar.empty() || paths.fallback.empty()) {
    result.ok = false;
    result.error = "视频路径为空，无法保存标记";
    return result;
  }

  if (marks.empty()) {
    bool any_existed = false;
    for (const std::string& path : {paths.sidecar, paths.fallback}) {
      const QFileInfo info(FromUtf8(path));
      if (info.exists() && info.isFile()) {
        any_existed = true;
      }
      std::string remove_error;
      if (RemoveRegularFile(FromUtf8(path), "删除附属标记文件失败", warnings,
                            &remove_error)) {
        result.removed = true;
      } else if (!remove_error.empty()) {
        result.ok = false;
        result.error = remove_error;
      }
    }
    if (result.ok && !any_existed) {
      result.removed = false;
    }
    return result;
  }

  const QByteArray bytes = QByteArray::fromStdString(SerializeMarkFile(video, marks));
  std::string sidecar_error;
  if (WriteAtomically(FromUtf8(paths.sidecar), bytes, &sidecar_error)) {
    result.ok = true;
    result.wrote = true;
    result.path = paths.sidecar;
    if (paths.fallback != paths.sidecar) {
      RemoveRegularFile(FromUtf8(paths.fallback),
                        "已写入 sidecar，但未能删除 AppData 回退文件（下次打开会合并）",
                        warnings, nullptr, MarkWarningKind::kMigrationFailed);
    }
    return result;
  }

  std::string fallback_error;
  if (WriteAtomically(FromUtf8(paths.fallback), bytes, &fallback_error)) {
    result.ok = true;
    result.wrote = true;
    result.path = paths.fallback;
    if (paths.fallback != paths.sidecar) {
      RemoveRegularFile(FromUtf8(paths.sidecar),
                        "已回退到 AppData，但未能删除旧 sidecar（下次打开会按 id+updated_at 合并）",
                        warnings, nullptr, MarkWarningKind::kMigrationFailed);
    }
    return result;
  }

  result.ok = false;
  result.error = JoinErrorMessage("sidecar 写入失败：", sidecar_error) +
                 "；AppData 回退失败：" + fallback_error;
  if (warnings != nullptr) {
    warnings->push_back(
        {MarkWarningKind::kSaveFailed, "标记保存失败：" + result.error, paths.sidecar});
  }
  return result;
}

}  // namespace

std::string NormalizeVideoPath(const std::string& path_utf8) {
  const QString path = CleanAbsolutePath(FromUtf8(path_utf8));
  if (path.isEmpty()) {
    return {};
  }
  return ToUtf8(path.toLower());
}

std::string MarkPathHash(const std::string& normalized_path) {
  if (normalized_path.empty()) {
    return {};
  }
  QString key = FromUtf8(normalized_path);
  key.replace(QLatin1Char('/'), QLatin1Char('\\'));
  key = key.toLower();
  return QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256)
      .toHex()
      .toStdString();
}

VideoIdentity MakeVideoIdentity(const std::string& path_utf8,
                                std::int64_t duration_us) {
  VideoIdentity identity;
  const QString canonical = CleanAbsolutePath(FromUtf8(path_utf8));
  if (canonical.isEmpty()) {
    return identity;
  }
  identity.path = ToUtf8(canonical);
  identity.path_key = ToUtf8(canonical.toLower());

  const QFileInfo info(canonical);
  if (info.exists() && info.isFile()) {
    identity.size = static_cast<std::uint64_t>(std::max<qint64>(0, info.size()));
  }
  identity.duration_us = duration_us > 0 ? duration_us : 0;
  return identity;
}

MarkPaths ResolveMarkPaths(const std::string& video_path_utf8,
                           const MarkStoreOptions& options) {
  MarkPaths paths;
  const VideoIdentity video = MakeVideoIdentity(video_path_utf8, 0);
  if (video.path.empty()) {
    return paths;
  }
  paths.sidecar = video.path + ".lapian.json";
  paths.path_hash = MarkPathHash(video.path_key);

  const QString root = options.appdata_marks_dir.empty()
                           ? DefaultAppDataMarksDir()
                           : FromUtf8(options.appdata_marks_dir);
  paths.fallback =
      ToUtf8(QDir(root).filePath(FromUtf8(paths.path_hash) + QStringLiteral(".json")));
  return paths;
}

std::vector<Mark> MergeMarks(const std::vector<Mark>& sidecar,
                             const std::vector<Mark>& fallback) {
  std::vector<Mark> merged = DedupeMarks(sidecar);
  for (const Mark& candidate : DedupeMarks(fallback)) {
    auto existing = std::find_if(merged.begin(), merged.end(),
                                 [&](const Mark& item) { return item.id == candidate.id; });
    if (existing == merged.end()) {
      merged.push_back(candidate);
    } else if (IsNewer(candidate, *existing)) {
      *existing = candidate;
    }
  }
  SortMarks(&merged);
  return merged;
}

void ReconcileMarks(std::vector<Mark>* marks, const FrameLookup& lookup,
                    std::vector<MarkWarning>* warnings) {
  if (marks == nullptr) {
    return;
  }
  if (!lookup.pts_of_frame && !lookup.frame_of_pts) {
    return;
  }

  for (Mark& mark : *marks) {
    const bool frame_in_range =
        lookup.frame_count <= 0 || (mark.frame >= 0 && mark.frame < lookup.frame_count);

    qint64 current_pts = 0;
    const bool have_current_pts =
        frame_in_range && lookup.pts_of_frame && lookup.pts_of_frame(mark.frame, &current_pts);
    if (have_current_pts && current_pts == mark.pts_us) {
      continue;
    }

    qint64 resolved_frame = -1;
    const bool have_resolved =
        lookup.frame_of_pts && lookup.frame_of_pts(mark.pts_us, &resolved_frame) &&
        resolved_frame >= 0 &&
        (lookup.frame_count <= 0 || resolved_frame < lookup.frame_count);

    if (have_resolved) {
      const qint64 old_frame = mark.frame;
      mark.frame = resolved_frame;
      if (lookup.pts_of_frame) {
        qint64 resolved_pts = 0;
        if (lookup.pts_of_frame(resolved_frame, &resolved_pts)) {
          mark.pts_us = resolved_pts;
        }
      }
      if (warnings != nullptr) {
        if (old_frame != resolved_frame) {
          warnings->push_back(
              {MarkWarningKind::kPtsMismatch,
               "标记 " + mark.id + "：帧号可能变化，已按 PTS 从第 " +
                   std::to_string(old_frame) + " 帧校正到第 " +
                   std::to_string(resolved_frame) + " 帧",
               {}});
        } else {
          warnings->push_back(
              {MarkWarningKind::kPtsMismatch,
               "标记 " + mark.id + "：PTS 与当前索引不一致，已按第 " +
                   std::to_string(resolved_frame) + " 帧更新 PTS",
               {}});
        }
      }
      continue;
    }

    if (have_current_pts) {
      mark.pts_us = current_pts;
      if (warnings != nullptr) {
        warnings->push_back(
            {MarkWarningKind::kPtsMismatch,
             "标记 " + mark.id + "：PTS 在当前索引中找不到，保留第 " +
                 std::to_string(mark.frame) + " 帧并更新 PTS",
             {}});
      }
    } else if (warnings != nullptr) {
      warnings->push_back(
          {MarkWarningKind::kPtsMismatch,
           "标记 " + mark.id + "：第 " + std::to_string(mark.frame) +
               " 帧与 PTS 均无法在当前索引中校验，保留原值",
           {}});
    }
  }
}

std::string SerializeMarkFile(const VideoIdentity& video,
                              const std::vector<Mark>& marks) {
  QJsonObject root;
  root.insert(QStringLiteral("schema"), QString::fromLatin1(kMarkSchemaName));
  root.insert(QStringLiteral("schema_version"), kMarkSchemaVersion);

  QJsonObject video_object;
  video_object.insert(QStringLiteral("path"), FromUtf8(video.path));
  video_object.insert(QStringLiteral("path_key"), FromUtf8(video.path_key));
  video_object.insert(QStringLiteral("size"), static_cast<double>(video.size));
  video_object.insert(QStringLiteral("duration_us"),
                      static_cast<double>(video.duration_us));
  root.insert(QStringLiteral("video"), video_object);

  QJsonArray mark_array;
  for (const Mark& mark : marks) {
    mark_array.append(MarkToJson(mark));
  }
  root.insert(QStringLiteral("marks"), mark_array);

  const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
  return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

bool ParseMarkFile(const std::string& json_utf8, std::vector<Mark>* marks,
                   VideoIdentity* stored_video, std::string* error,
                   bool* unsupported_schema) {
  if (marks != nullptr) {
    marks->clear();
  }
  if (stored_video != nullptr) {
    *stored_video = {};
  }
  if (unsupported_schema != nullptr) {
    *unsupported_schema = false;
  }

  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = ToUtf8(message);
    }
    return false;
  };

  QJsonParseError parse_error;
  const QJsonDocument document =
      QJsonDocument::fromJson(QByteArray::fromStdString(json_utf8), &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    return fail(QStringLiteral("JSON 解析失败：") + parse_error.errorString());
  }
  if (!document.isObject()) {
    return fail(QStringLiteral("JSON 根不是对象"));
  }
  const QJsonObject root = document.object();
  if (root.value(QStringLiteral("schema")).toString() !=
      QString::fromLatin1(kMarkSchemaName)) {
    return fail(QStringLiteral("schema 不是 lapian.marks"));
  }

  qint64 version = 0;
  if (!JsonInt64(root.value(QStringLiteral("schema_version")), &version)) {
    return fail(QStringLiteral("schema_version 缺失或不是整数"));
  }
  if (version > kMarkSchemaVersion) {
    if (unsupported_schema != nullptr) {
      *unsupported_schema = true;
    }
    return fail(QStringLiteral("schema_version %1 高于支持的 %2")
                    .arg(version)
                    .arg(kMarkSchemaVersion));
  }
  if (version != kMarkSchemaVersion) {
    return fail(QStringLiteral("schema_version 不受支持：") + QString::number(version));
  }

  const QJsonValue video_value = root.value(QStringLiteral("video"));
  if (!video_value.isObject()) {
    return fail(QStringLiteral("video 缺失或不是对象"));
  }
  const QJsonObject video_object = video_value.toObject();
  VideoIdentity parsed_video;
  parsed_video.path = video_object.value(QStringLiteral("path")).toString().toUtf8().toStdString();
  if (parsed_video.path.empty()) {
    return fail(QStringLiteral("video.path 缺失"));
  }
  parsed_video.path_key = NormalizeVideoPath(parsed_video.path);
  qint64 size = 0;
  if (!JsonInt64(video_object.value(QStringLiteral("size")), &size) || size < 0) {
    return fail(QStringLiteral("video.size 缺失或不是非负整数"));
  }
  parsed_video.size = static_cast<std::uint64_t>(size);
  qint64 duration_us = 0;
  if (!JsonInt64(video_object.value(QStringLiteral("duration_us")), &duration_us) ||
      duration_us < 0) {
    return fail(QStringLiteral("video.duration_us 缺失或不是非负整数"));
  }
  parsed_video.duration_us = duration_us;

  const QJsonValue marks_value = root.value(QStringLiteral("marks"));
  if (!marks_value.isArray()) {
    return fail(QStringLiteral("marks 缺失或不是数组"));
  }
  const QJsonArray mark_array = marks_value.toArray();
  std::vector<Mark> parsed_marks;
  parsed_marks.reserve(static_cast<std::size_t>(mark_array.size()));
  for (int index = 0; index < mark_array.size(); ++index) {
    const QJsonValue item = mark_array.at(index);
    if (!item.isObject()) {
      return fail(QStringLiteral("marks[%1] 不是对象").arg(index));
    }
    const QJsonObject object = item.toObject();
    Mark mark;
    mark.id = object.value(QStringLiteral("id")).toString().toUtf8().toStdString();
    if (mark.id.empty()) {
      return fail(QStringLiteral("marks[%1].id 缺失").arg(index));
    }
    if (!JsonInt64(object.value(QStringLiteral("frame")), &mark.frame) ||
        mark.frame < 0) {
      return fail(QStringLiteral("marks[%1].frame 缺失或不是非负整数").arg(index));
    }
    if (!JsonInt64(object.value(QStringLiteral("pts")), &mark.pts_us)) {
      return fail(QStringLiteral("marks[%1].pts 缺失或不是整数").arg(index));
    }
    mark.label = object.value(QStringLiteral("label")).toString().toUtf8().toStdString();
    mark.note = object.value(QStringLiteral("note")).toString().toUtf8().toStdString();
    mark.created_at =
        object.value(QStringLiteral("created_at")).toString().toUtf8().toStdString();
    mark.updated_at =
        object.value(QStringLiteral("updated_at")).toString().toUtf8().toStdString();
    parsed_marks.push_back(std::move(mark));
  }

  if (marks != nullptr) {
    *marks = std::move(parsed_marks);
  }
  if (stored_video != nullptr) {
    *stored_video = std::move(parsed_video);
  }
  return true;
}

std::string SerializeOnlineMarkFile(
    const std::string& bvid, std::int64_t cid, double fps, int qn, int codecid,
    const std::vector<Mark>& marks,
    const std::map<std::string, OnlineMarkMeta>& meta) {
  QJsonObject root;
  root.insert(QStringLiteral("schema"),
              QString::fromLatin1(kOnlineMarkSchemaName));
  root.insert(QStringLiteral("schema_version"), kOnlineMarkSchemaVersion);
  root.insert(QStringLiteral("bvid"), FromUtf8(bvid));
  root.insert(QStringLiteral("cid"), static_cast<double>(cid));
  root.insert(QStringLiteral("qn"), qn);
  root.insert(QStringLiteral("codecid"), codecid);
  root.insert(QStringLiteral("fps"), std::isfinite(fps) ? fps : 0.0);
  root.insert(QStringLiteral("updated_at"), FromUtf8(NowIso8601()));

  QJsonArray mark_array;
  for (const Mark& mark : marks) {
    OnlineMarkMeta mark_meta;
    mark_meta.fps = fps;
    mark_meta.qn = qn;
    mark_meta.codecid = codecid;
    const auto found = meta.find(mark.id);
    if (found != meta.end()) {
      mark_meta = found->second;
    }
    mark_array.append(OnlineMarkToJson(mark, mark_meta));
  }
  root.insert(QStringLiteral("marks"), mark_array);

  const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
  return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

bool ParseOnlineMarkFile(
    const std::string& json_utf8, const std::string& expected_bvid,
    std::int64_t expected_cid, std::vector<Mark>* marks,
    std::map<std::string, OnlineMarkMeta>* meta, double* stored_fps,
    int* stored_qn, int* stored_codecid, std::string* error,
    bool* unsupported_schema) {
  if (marks != nullptr) {
    marks->clear();
  }
  if (meta != nullptr) {
    meta->clear();
  }
  if (stored_fps != nullptr) {
    *stored_fps = 0.0;
  }
  if (stored_qn != nullptr) {
    *stored_qn = 0;
  }
  if (stored_codecid != nullptr) {
    *stored_codecid = 0;
  }
  if (unsupported_schema != nullptr) {
    *unsupported_schema = false;
  }

  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = ToUtf8(message);
    }
    return false;
  };

  QJsonParseError parse_error;
  const QJsonDocument document =
      QJsonDocument::fromJson(QByteArray::fromStdString(json_utf8), &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    return fail(QStringLiteral("JSON 解析失败：") + parse_error.errorString());
  }
  if (!document.isObject()) {
    return fail(QStringLiteral("JSON 根不是对象"));
  }
  const QJsonObject root = document.object();
  if (root.value(QStringLiteral("schema")).toString() !=
      QString::fromLatin1(kOnlineMarkSchemaName)) {
    return fail(QStringLiteral("schema 不是 lapian-online-1"));
  }

  qint64 version = 0;
  if (!JsonInt64(root.value(QStringLiteral("schema_version")), &version)) {
    return fail(QStringLiteral("schema_version 缺失或不是整数"));
  }
  if (version > kOnlineMarkSchemaVersion) {
    if (unsupported_schema != nullptr) {
      *unsupported_schema = true;
    }
    return fail(QStringLiteral("schema_version %1 高于支持的 %2")
                    .arg(version)
                    .arg(kOnlineMarkSchemaVersion));
  }
  if (version != kOnlineMarkSchemaVersion) {
    return fail(QStringLiteral("schema_version 不受支持：") + QString::number(version));
  }

  const QString bvid = root.value(QStringLiteral("bvid")).toString();
  qint64 cid = 0;
  if (!JsonInt64(root.value(QStringLiteral("cid")), &cid) || cid <= 0) {
    return fail(QStringLiteral("cid 缺失或不是正整数"));
  }
  if (bvid != FromUtf8(expected_bvid) || cid != expected_cid) {
    return fail(QStringLiteral("bvid/cid 与记录文件名不一致（%1 / %2）")
                    .arg(bvid)
                    .arg(cid));
  }

  double fps = 0.0;
  if (root.contains(QStringLiteral("fps"))) {
    if (!JsonDouble(root.value(QStringLiteral("fps")), &fps) || fps < 0.0) {
      return fail(QStringLiteral("fps 不是非负数字"));
    }
  }
  qint64 qn = 0;
  if (root.contains(QStringLiteral("qn"))) {
    if (!JsonInt64(root.value(QStringLiteral("qn")), &qn) || qn < 0) {
      return fail(QStringLiteral("qn 不是非负整数"));
    }
  }
  qint64 codecid = 0;
  if (root.contains(QStringLiteral("codecid"))) {
    if (!JsonInt64(root.value(QStringLiteral("codecid")), &codecid) ||
        codecid < 0) {
      return fail(QStringLiteral("codecid 不是非负整数"));
    }
  }

  const QJsonValue marks_value = root.value(QStringLiteral("marks"));
  if (!marks_value.isArray()) {
    return fail(QStringLiteral("marks 缺失或不是数组"));
  }
  const QJsonArray mark_array = marks_value.toArray();
  std::vector<Mark> parsed_marks;
  std::map<std::string, OnlineMarkMeta> parsed_meta;
  std::map<std::string, std::size_t> index_by_id;
  parsed_marks.reserve(static_cast<std::size_t>(mark_array.size()));
  for (int i = 0; i < mark_array.size(); ++i) {
    const QJsonValue item = mark_array.at(i);
    if (!item.isObject()) {
      return fail(QStringLiteral("marks[%1] 不是对象").arg(i));
    }
    const QJsonObject object = item.toObject();
    Mark mark;
    mark.id = object.value(QStringLiteral("id")).toString().toUtf8().toStdString();
    if (mark.id.empty()) {
      return fail(QStringLiteral("marks[%1].id 缺失").arg(i));
    }
    if (!JsonInt64(object.value(QStringLiteral("frame")), &mark.frame) ||
        mark.frame < 0) {
      return fail(QStringLiteral("marks[%1].frame 缺失或不是非负整数").arg(i));
    }
    if (!JsonInt64(object.value(QStringLiteral("pts")), &mark.pts_us)) {
      return fail(QStringLiteral("marks[%1].pts 缺失或不是整数").arg(i));
    }
    mark.label = object.value(QStringLiteral("label")).toString().toUtf8().toStdString();
    mark.note = object.value(QStringLiteral("note")).toString().toUtf8().toStdString();
    mark.created_at =
        object.value(QStringLiteral("created_at")).toString().toUtf8().toStdString();
    mark.updated_at =
        object.value(QStringLiteral("updated_at")).toString().toUtf8().toStdString();

    OnlineMarkMeta mark_meta;
    mark_meta.fps = fps;
    mark_meta.qn = static_cast<int>(qn);
    mark_meta.codecid = static_cast<int>(codecid);
    double mark_fps = 0.0;
    if (object.contains(QStringLiteral("fps")) &&
        JsonDouble(object.value(QStringLiteral("fps")), &mark_fps) &&
        mark_fps >= 0.0) {
      mark_meta.fps = mark_fps;
    }
    qint64 mark_qn = 0;
    if (object.contains(QStringLiteral("qn")) &&
        JsonInt64(object.value(QStringLiteral("qn")), &mark_qn) && mark_qn >= 0) {
      mark_meta.qn = static_cast<int>(mark_qn);
    }
    qint64 mark_codecid = 0;
    if (object.contains(QStringLiteral("codecid")) &&
        JsonInt64(object.value(QStringLiteral("codecid")), &mark_codecid) &&
        mark_codecid >= 0) {
      mark_meta.codecid = static_cast<int>(mark_codecid);
    }

    const auto found = index_by_id.find(mark.id);
    if (found == index_by_id.end()) {
      index_by_id.emplace(mark.id, parsed_marks.size());
      parsed_marks.push_back(std::move(mark));
      parsed_meta[parsed_marks.back().id] = mark_meta;
    } else if (IsNewer(mark, parsed_marks[found->second])) {
      parsed_marks[found->second] = std::move(mark);
      parsed_meta[parsed_marks[found->second].id] = mark_meta;
    }
  }
  SortMarks(&parsed_marks);

  if (marks != nullptr) {
    *marks = std::move(parsed_marks);
  }
  if (meta != nullptr) {
    *meta = std::move(parsed_meta);
  }
  if (stored_fps != nullptr) {
    *stored_fps = fps;
  }
  if (stored_qn != nullptr) {
    *stored_qn = static_cast<int>(qn);
  }
  if (stored_codecid != nullptr) {
    *stored_codecid = static_cast<int>(codecid);
  }
  return true;
}

MarkStore MarkStore::Open(const std::string& video_path_utf8,
                          std::int64_t duration_us,
                          const MarkStoreOptions& options,
                          const FrameLookup& lookup) {
  MarkStore store;
  store.options_ = options;
  store.video_ = MakeVideoIdentity(video_path_utf8, duration_us);
  if (store.video_.empty()) {
    store.warnings_.push_back({MarkWarningKind::kReadFailed,
                               "视频路径为空或无法规范化，标记不会保存", {}});
    return store;
  }
  store.valid_ = true;
  store.paths_ = ResolveMarkPaths(video_path_utf8, options);

  LoadedFile sidecar = LoadOne(FromUtf8(store.paths_.sidecar), &store.warnings_);
  LoadedFile fallback = LoadOne(FromUtf8(store.paths_.fallback), &store.warnings_);

  if (sidecar.unsupported || fallback.unsupported) {
    store.block_save_ = true;
    store.storage_path_ = sidecar.exists ? store.paths_.sidecar : store.paths_.fallback;
    store.using_fallback_ = !sidecar.exists;
    return store;
  }

  if (sidecar.read_failed || fallback.read_failed) {
    store.block_save_ = true;
  }

  CheckIdentity(sidecar, store.video_, store.paths_.sidecar, &store.warnings_);
  CheckIdentity(fallback, store.video_, store.paths_.fallback, &store.warnings_);

  std::vector<Mark> merged;
  if (sidecar.loaded && fallback.loaded) {
    merged = MergeMarks(sidecar.marks, fallback.marks);
  } else if (sidecar.loaded) {
    merged = sidecar.marks;
  } else if (fallback.loaded) {
    merged = fallback.marks;
  }
  SortMarks(&merged);

  const std::vector<Mark> before_reconcile = merged;
  ReconcileMarks(&merged, lookup, &store.warnings_);
  const bool changed = merged != before_reconcile;
  store.marks_ = std::move(merged);

  const bool any_file = sidecar.exists || fallback.exists;
  if (store.marks_.empty()) {
    if (any_file && !store.block_save_) {
      std::string remove_error;
      RemoveRegularFile(FromUtf8(store.paths_.sidecar), "删除空的 sidecar 失败",
                        &store.warnings_, &remove_error);
      RemoveRegularFile(FromUtf8(store.paths_.fallback), "删除空的回退文件失败",
                        &store.warnings_, &remove_error);
    }
    store.storage_path_ = store.paths_.sidecar;
    store.using_fallback_ = false;
    return store;
  }

  const bool both_loaded = sidecar.loaded && fallback.loaded;
  const bool only_fallback = !sidecar.loaded && fallback.loaded;
  const bool needs_write = changed || both_loaded || only_fallback;

  if (!needs_write || store.block_save_) {
    store.storage_path_ = sidecar.loaded ? store.paths_.sidecar : store.paths_.fallback;
    store.using_fallback_ = !sidecar.loaded;
    return store;
  }

  const SaveResult save =
      SaveMarksInternal(store.video_, store.marks_, options, &store.warnings_);
  if (save.ok) {
    store.storage_path_ = save.path;
    store.using_fallback_ = save.path == store.paths_.fallback;
  } else {
    store.storage_path_ = sidecar.loaded ? store.paths_.sidecar : store.paths_.fallback;
    store.using_fallback_ = !sidecar.loaded;
  }
  return store;
}

MarkStore MarkStore::OpenOnline(const std::string& bvid, std::int64_t cid,
                                double fps, int qn, int codecid,
                                const MarkStoreOptions& options,
                                const FrameLookup& lookup) {
  MarkStore store;
  store.options_ = options;
  store.online_ = true;
  store.online_bvid_ = bvid;
  store.online_cid_ = cid;
  store.online_context_.fps = std::isfinite(fps) && fps > 0.0 ? fps : 0.0;
  store.online_context_.qn = qn;
  store.online_context_.codecid = codecid;
  store.video_.path = bvid + "-" + std::to_string(cid);
  store.video_.path_key = store.video_.path;

  const QString bvid_text = FromUtf8(bvid);
  if (!IsSafeOnlineBvid(bvid_text) || cid <= 0) {
    store.warnings_.push_back(
        {MarkWarningKind::kReadFailed,
         "在线记录键无效（bvid/cid），标记只在当前会话内可用", {}});
    return store;
  }
  store.valid_ = true;
  const QString path = OnlineRecordPath(options, bvid_text, cid);
  store.storage_path_ = ToUtf8(path);

  const QFileInfo info(path);
  if (!info.exists()) {
    return store;
  }
  if (!info.isFile()) {
    store.block_save_ = true;
    store.warnings_.push_back(
        {MarkWarningKind::kReadFailed, "在线记录路径不是普通文件，已忽略",
         store.storage_path_});
    return store;
  }

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    store.block_save_ = true;
    store.warnings_.push_back(
        {MarkWarningKind::kReadFailed,
         "无法读取在线记录：" + ToUtf8(file.errorString()), store.storage_path_});
    return store;
  }
  const QByteArray bytes = file.readAll();
  const bool read_error = file.error() != QFileDevice::NoError;
  const QString read_error_text = file.errorString();
  file.close();
  if (read_error) {
    store.block_save_ = true;
    store.warnings_.push_back(
        {MarkWarningKind::kReadFailed,
         "读取在线记录失败：" + ToUtf8(read_error_text), store.storage_path_});
    return store;
  }

  std::vector<Mark> parsed_marks;
  std::map<std::string, OnlineMarkMeta> parsed_meta;
  double stored_fps = 0.0;
  int stored_qn = 0;
  int stored_codecid = 0;
  std::string parse_error;
  bool unsupported = false;
  if (!ParseOnlineMarkFile(
          std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())),
          bvid, cid, &parsed_marks, &parsed_meta, &stored_fps, &stored_qn,
          &stored_codecid, &parse_error, &unsupported)) {
    if (unsupported) {
      store.block_save_ = true;
      store.warnings_.push_back(
          {MarkWarningKind::kUnsupportedSchema,
           "在线记录由更新版本创建，暂不读取且不会覆盖：" + parse_error,
           store.storage_path_});
      return store;
    }

    QString backup_path;
    std::string backup_error;
    if (BackupCorruptFile(path, &backup_path, &backup_error)) {
      store.warnings_.push_back(
          {MarkWarningKind::kCorruptBackup,
           "在线记录损坏，已备份为 " + ToUtf8(backup_path) +
               "；按无标记继续播放：" + parse_error,
           store.storage_path_});
    } else {
      store.block_save_ = true;
      store.warnings_.push_back(
          {MarkWarningKind::kReadFailed,
           "在线记录损坏且备份失败，已阻止覆盖：" + backup_error + "；" +
               parse_error,
           store.storage_path_});
    }
    return store;
  }

  store.marks_ = std::move(parsed_marks);
  store.online_meta_ = std::move(parsed_meta);
  if (stored_fps > 0.0 && store.online_context_.fps > 0.0 &&
      std::fabs(stored_fps - store.online_context_.fps) > 0.01) {
    store.warnings_.push_back(
        {MarkWarningKind::kPtsMismatch,
         QStringLiteral("在线记录来自 %1fps，当前流为 %2fps；标记按 PTS 映射并提示")
             .arg(QString::number(stored_fps, 'f', 3),
                  QString::number(store.online_context_.fps, 'f', 3))
             .toUtf8()
             .toStdString(),
         store.storage_path_});
  }
  // 若调用方已经有索引（同步打开），在加载时完成一次 PTS 校正。
  const std::vector<Mark> before_reconcile = store.marks_;
  ReconcileMarks(&store.marks_, lookup, &store.warnings_);
  if (store.marks_ != before_reconcile) {
    for (const Mark& mark : store.marks_) {
      store.online_meta_[mark.id] = store.online_context_;
    }
  }

  if (store.marks_.empty() && !store.block_save_) {
    std::string remove_error;
    RemoveRegularFile(path, "删除空的在线记录失败", &store.warnings_, &remove_error);
  }
  return store;
}

MarkStore MarkStore::OpenSessionOnly(const std::string& video_path_utf8,
                                     std::int64_t duration_us) {
  MarkStore store;
  store.video_ = MakeVideoIdentity(video_path_utf8, duration_us);
  store.valid_ = !store.video_.empty();
  store.session_only_ = true;
  store.storage_path_.clear();
  if (!store.valid_) {
    store.warnings_.push_back({MarkWarningKind::kReadFailed,
                               "在线会话标识为空，标记只在当前会话内可用", {}});
  }
  return store;
}

const Mark* MarkStore::FindMark(const std::string& id) const {
  for (const Mark& mark : marks_) {
    if (mark.id == id) {
      return &mark;
    }
  }
  return nullptr;
}

bool MarkStore::AddMark(std::int64_t frame, std::int64_t pts_us,
                        const std::string& label, const std::string& note,
                        std::string* out_id) {
  if (!valid_ || frame < 0) {
    return false;
  }
  Mark mark;
  mark.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
  mark.frame = frame;
  mark.pts_us = pts_us;
  mark.label = label.empty() ? "帧 " + std::to_string(frame) : label;
  mark.note = note;
  const std::string now = NowIso8601();
  mark.created_at = now;
  mark.updated_at = now;
  marks_.push_back(mark);
  SortMarks(&marks_);
  if (online_) {
    online_meta_[mark.id] = online_context_;
  }
  if (out_id != nullptr) {
    *out_id = mark.id;
  }
  return true;
}

bool MarkStore::SetMarkText(const std::string& id, const std::string& label,
                            const std::string& note) {
  for (Mark& mark : marks_) {
    if (mark.id != id) {
      continue;
    }
    if (mark.label == label && mark.note == note) {
      return true;
    }
    mark.label = label;
    mark.note = note;
    mark.updated_at = NowIso8601();
    return true;
  }
  return false;
}

bool MarkStore::RemoveMark(const std::string& id) {
  const auto removed =
      std::erase_if(marks_, [&](const Mark& mark) { return mark.id == id; });
  if (removed > 0) {
    online_meta_.erase(id);
  }
  return removed > 0;
}

const OnlineMarkMeta* MarkStore::OnlineMetaForMark(const std::string& id) const {
  const auto found = online_meta_.find(id);
  return found == online_meta_.end() ? nullptr : &found->second;
}

void MarkStore::SetOnlineContext(double fps, int qn, int codecid) {
  if (!online_) {
    return;
  }
  online_context_.fps = std::isfinite(fps) && fps > 0.0 ? fps : 0.0;
  online_context_.qn = qn;
  online_context_.codecid = codecid;
}

bool MarkStore::ReconcileFrames(const FrameLookup& lookup,
                                std::vector<MarkWarning>* warnings,
                                bool* changed) {
  if (!online_) {
    if (changed != nullptr) {
      *changed = false;
    }
    return false;
  }
  if (!lookup.pts_of_frame && !lookup.frame_of_pts) {
    if (changed != nullptr) {
      *changed = false;
    }
    return false;
  }
  const std::vector<Mark> before = marks_;
  std::vector<MarkWarning> local_warnings;
  ReconcileMarks(&marks_, lookup, &local_warnings);
  const bool did_change = marks_ != before;
  if (did_change) {
    for (const Mark& mark : marks_) {
      online_meta_[mark.id] = online_context_;
    }
  }
  if (warnings != nullptr) {
    warnings->insert(warnings->end(), local_warnings.begin(), local_warnings.end());
  } else {
    warnings_.insert(warnings_.end(), local_warnings.begin(), local_warnings.end());
  }
  if (changed != nullptr) {
    *changed = did_change;
  }
  return did_change;
}

SaveResult MarkStore::Save() {
  SaveResult result;
  if (!valid_) {
    result.ok = false;
    result.error = "没有可保存的视频";
    return result;
  }
  if (block_save_) {
    result.ok = false;
    result.error = "标记文件读取或 schema 版本异常，已阻止覆盖";
    warnings_.push_back({MarkWarningKind::kSaveFailed, result.error, storage_path_});
    return result;
  }
  if (session_only_) {
    // T8.6：在线会话内标记不落盘（完整在线记录是 T8.7）。
    result.ok = true;
    result.wrote = false;
    result.path.clear();
    return result;
  }
  if (online_) {
    result.path = storage_path_;
    if (marks_.empty()) {
      const QFileInfo info(FromUtf8(storage_path_));
      if (!info.exists()) {
        result.ok = true;
        return result;
      }
      if (!info.isFile()) {
        result.ok = false;
        result.error = "在线记录路径不是普通文件，未删除";
        warnings_.push_back(
            {MarkWarningKind::kSaveFailed, result.error, storage_path_});
        return result;
      }
      if (QFile::remove(FromUtf8(storage_path_))) {
        result.ok = true;
        result.removed = true;
        return result;
      }
      result.ok = false;
      result.error = "删除空的在线记录失败";
      warnings_.push_back(
          {MarkWarningKind::kSaveFailed, result.error, storage_path_});
      return result;
    }
    if (storage_path_.empty()) {
      result.ok = false;
      result.error = "在线记录路径为空";
      return result;
    }
    const QByteArray bytes = QByteArray::fromStdString(SerializeOnlineMarkFile(
        online_bvid_, online_cid_, online_context_.fps, online_context_.qn,
        online_context_.codecid, marks_, online_meta_));
    std::string write_error;
    if (!WriteAtomically(FromUtf8(storage_path_), bytes, &write_error)) {
      result.ok = false;
      result.error = write_error.empty() ? "在线记录写入失败" : write_error;
      warnings_.push_back(
          {MarkWarningKind::kSaveFailed, "在线记录保存失败：" + result.error,
           storage_path_});
      return result;
    }
    result.ok = true;
    result.wrote = true;
    return result;
  }

  result = SaveMarksInternal(video_, marks_, options_, &warnings_);
  if (result.ok) {
    storage_path_ = result.path;
    using_fallback_ = result.path == paths_.fallback;
  }
  return result;
}

}  // namespace lapian::core
