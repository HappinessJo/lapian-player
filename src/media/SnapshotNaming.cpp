#include "media/SnapshotNaming.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryFile>

#include <algorithm>
#include <cmath>

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

QString DefaultAppDataOnlineRoot() {
  QString appdata = qEnvironmentVariable("APPDATA");
  if (appdata.isEmpty()) {
    appdata = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  }
  if (appdata.isEmpty()) {
    appdata = QDir::homePath() + QStringLiteral("/AppData/Roaming");
  }
  return QDir(appdata).filePath(QString::fromUtf8("拉片软件/online"));
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

OnlineSnapshotDirectory ResolveOnlineSnapshotDirectory(
    const QString& bvid, const QString& pictures_root,
    const QString& appdata_online_root, QString* error) {
  if (error != nullptr) {
    error->clear();
  }

  OnlineSnapshotDirectory result;
  const QString safe_bvid = SanitizeSnapshotBaseName(bvid);
  if (safe_bvid.isEmpty() || safe_bvid == QStringLiteral("snapshot")) {
    if (error != nullptr) {
      *error = QStringLiteral("在线截图缺少有效 bvid");
    }
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
  const QString primary =
      QDir(pictures).filePath(QStringLiteral("拉片软件/%1").arg(safe_bvid));
  if (IsWritableDirectory(primary)) {
    result.path = primary;
    return result;
  }

  const QString appdata = appdata_online_root.isEmpty()
                              ? DefaultAppDataOnlineRoot()
                              : appdata_online_root;
  const QString fallback = QDir(appdata).filePath(
      QStringLiteral("snapshots/%1").arg(safe_bvid));
  if (IsWritableDirectory(fallback)) {
    result.path = fallback;
    result.fallback = true;
    result.reason = QStringLiteral("图片目录不可写，已回退 %APPDATA% 在线目录");
    return result;
  }

  if (error != nullptr) {
    *error = QStringLiteral("在线截图目录不可写：\n%1\n%2")
                 .arg(QDir::toNativeSeparators(primary),
                      QDir::toNativeSeparators(fallback));
  }
  return result;
}

QString BuildOnlineSnapshotBaseName(const QString& bvid, std::int64_t frame,
                                    int fps_num, int fps_den) {
  const QString base = SanitizeSnapshotBaseName(bvid);
  return QStringLiteral("%1_f%2_%3")
      .arg(base)
      .arg(static_cast<qlonglong>(frame))
      .arg(FormatSnapshotTimecode(frame, fps_num, fps_den));
}

bool AppendOnlineSnapshotIndex(const QString& directory, const QString& bvid,
                               std::int64_t cid, int qn, int codecid, double fps,
                               std::int64_t frame, std::int64_t pts_us,
                               const QString& png_path, QString* error) {
  if (error != nullptr) {
    error->clear();
  }
  if (directory.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("在线截图目录为空");
    }
    return false;
  }
  QJsonObject object;
  object.insert(QStringLiteral("bvid"), bvid);
  object.insert(QStringLiteral("cid"), static_cast<double>(cid));
  object.insert(QStringLiteral("qn"), qn);
  object.insert(QStringLiteral("codecid"), codecid);
  object.insert(QStringLiteral("fps"), std::isfinite(fps) && fps > 0.0 ? fps : 0.0);
  object.insert(QStringLiteral("frame"), static_cast<double>(frame));
  object.insert(QStringLiteral("pts"), static_cast<double>(pts_us));
  object.insert(QStringLiteral("path"),
                QDir::toNativeSeparators(QFileInfo(png_path).absoluteFilePath()));
  object.insert(QStringLiteral("saved_at"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact);
  line.append('\n');

  const QString index_path = QDir(directory).filePath(QStringLiteral("index.jsonl"));
  QFile index(index_path);
  if (!index.open(QIODevice::ReadWrite | QIODevice::Append)) {
    if (error != nullptr) {
      *error = QStringLiteral("无法打开截图清单：%1").arg(index.errorString());
    }
    return false;
  }
  const qint64 before = index.size();
  const bool written = index.write(line) == line.size() && index.flush();
  if (!written) {
    const QString detail = index.errorString();
    index.resize(before);
    index.close();
    if (error != nullptr) {
      *error = QStringLiteral("写入截图清单失败：%1").arg(detail);
    }
    return false;
  }
  index.close();
  return true;
}

}  // namespace lapian::snapshot
