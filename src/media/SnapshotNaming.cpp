#include "media/SnapshotNaming.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryFile>

#include <algorithm>

#include "core/FrameMath.h"

namespace lapian::snapshot {
namespace {

constexpr int kMaxBaseNameUnits = 120;

bool IsReservedDeviceName(const QString& name) {
  const QString stem = name.section(QLatin1Char('.'), 0, 0).trimmed().toUpper();
  if (stem.isEmpty()) {
    return false;
  }
  static const QStringList kReserved = {
      QStringLiteral("CON"),   QStringLiteral("PRN"),    QStringLiteral("AUX"),
      QStringLiteral("NUL"),   QStringLiteral("CONIN$"), QStringLiteral("CONOUT$"),
      QStringLiteral("CLOCK$")};
  if (kReserved.contains(stem)) {
    return true;
  }
  if (stem.size() == 4 &&
      (stem.startsWith(QStringLiteral("COM")) || stem.startsWith(QStringLiteral("LPT")))) {
    const QChar digit = stem.at(3);
    return digit >= QLatin1Char('1') && digit <= QLatin1Char('9');
  }
  return false;
}

QString TrimTrailingDotsAndSpaces(QString text) {
  while (!text.isEmpty()) {
    const QChar last = text.back();
    if (last == QLatin1Char('.') || last == QLatin1Char(' ')) {
      text.chop(1);
    } else {
      break;
    }
  }
  return text;
}

bool IsWritableDirectory(const QString& path) {
  if (path.isEmpty()) {
    return false;
  }
  const QFileInfo info(path);
  if (!info.exists()) {
    if (!QDir().mkpath(path)) {
      return false;
    }
  } else if (!info.isDir()) {
    return false;
  }

  // 不用 QFileInfo::isWritable()：它只看只读属性，不反映 ACL/网络盘的真实写入权限。
  QTemporaryFile probe(QDir(path).filePath(QStringLiteral(".lapian-snapshot-probe-XXXXXX")));
  probe.setAutoRemove(true);
  if (!probe.open()) {
    return false;
  }
  if (probe.write("ok", 2) != 2) {
    return false;
  }
  return probe.flush();
}

}  // namespace

QString SanitizeSnapshotBaseName(const QString& video_base_name) {
  static const QString kInvalid = QStringLiteral("<>:\"/\\|?*");
  QString text = video_base_name;
  if (text.isEmpty()) {
    text = QStringLiteral("snapshot");
  }

  QString sanitized;
  sanitized.reserve(text.size());
  for (const QChar ch : text) {
    const ushort code = ch.unicode();
    if (code < 0x20 || kInvalid.contains(ch)) {
      sanitized.append(QLatin1Char('_'));
    } else {
      sanitized.append(ch);
    }
  }

  sanitized = TrimTrailingDotsAndSpaces(sanitized);
  if (sanitized.isEmpty() || sanitized == QStringLiteral(".") ||
      sanitized == QStringLiteral("..")) {
    sanitized = QStringLiteral("snapshot");
  }

  if (sanitized.size() > kMaxBaseNameUnits) {
    sanitized.truncate(kMaxBaseNameUnits);
    // 不要截断 UTF-16 代理对，避免生成非法文件名。
    if (!sanitized.isEmpty() && sanitized.back().isHighSurrogate()) {
      sanitized.chop(1);
    }
    sanitized = TrimTrailingDotsAndSpaces(sanitized);
    if (sanitized.isEmpty()) {
      sanitized = QStringLiteral("snapshot");
    }
  }

  if (IsReservedDeviceName(sanitized)) {
    sanitized.prepend(QLatin1Char('_'));
  }
  return sanitized;
}

QString FormatSnapshotTimecode(std::int64_t frame, int fps_num, int fps_den) {
  lapian::core::FrameRate rate(fps_num, fps_den);
  if (!rate.valid()) {
    rate = lapian::core::FrameRate(30, 1);
  }
  const lapian::core::TimecodeFormatter formatter(rate);
  const lapian::core::Timecode timecode =
      formatter.FromFrame(std::max<std::int64_t>(frame, 0));
  QString text = QString::fromStdString(timecode.ToString());
  text.replace(QLatin1Char(':'), QLatin1Char('-'));
  return text;
}

QString BuildSnapshotBaseName(const QString& video_path, std::int64_t frame,
                              int fps_num, int fps_den) {
  const QFileInfo info(video_path);
  const QString base = SanitizeSnapshotBaseName(info.completeBaseName());
  return QStringLiteral("%1_f%2_%3")
      .arg(base)
      .arg(static_cast<qlonglong>(frame))
      .arg(FormatSnapshotTimecode(frame, fps_num, fps_den));
}

QString SnapshotFileName(const QString& base_name, int sequence) {
  if (sequence <= 0) {
    return base_name + QStringLiteral(".png");
  }
  return QStringLiteral("%1_%2.png").arg(base_name).arg(sequence);
}

QString SnapshotFilePath(const QString& directory, const QString& base_name, int sequence) {
  return QDir(directory).filePath(SnapshotFileName(base_name, sequence));
}

SnapshotDirectory ResolveSnapshotDirectory(const QString& video_path,
                                           const QString& pictures_root,
                                           QString* error) {
  if (error != nullptr) {
    error->clear();
  }

  SnapshotDirectory result;
  const QString video_dir = QFileInfo(video_path).absolutePath();
  if (video_dir.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("无法确定视频所在目录");
    }
    return result;
  }

  const QString primary = QDir(video_dir).filePath(QStringLiteral("snapshots"));
  if (IsWritableDirectory(primary)) {
    result.path = primary;
    return result;
  }

  QString pictures = pictures_root;
  if (pictures.isEmpty()) {
    pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  }
  if (pictures.isEmpty()) {
    pictures = QDir::homePath() + QStringLiteral("/Pictures");
  }
  pictures = QFileInfo(pictures).absoluteFilePath();
  const QString fallback = QDir(pictures).filePath(QStringLiteral("拉片软件"));
  if (IsWritableDirectory(fallback)) {
    result.path = fallback;
    result.fallback = true;
    result.reason = QStringLiteral("视频目录不可写，已回退用户图片目录");
    return result;
  }

  if (error != nullptr) {
    *error = QStringLiteral("视频目录与用户图片目录都不可写：\n%1\n%2")
                 .arg(QDir::toNativeSeparators(primary),
                      QDir::toNativeSeparators(fallback));
  }
  return result;
}

}  // namespace lapian::snapshot
