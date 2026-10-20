#include "core/AppSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringLiteral>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace lapian::core {
namespace {

constexpr int kSchemaVersion = 1;
constexpr int kDefaultVolume = 100;
constexpr bool kDefaultWaveformVisible = true;

int ClampVolume(int volume) { return std::clamp(volume, 0, 100); }

}  // namespace

QByteArray SerializeAppSettings(const AppSettings& settings) {
  QJsonObject root;
  root.insert(QStringLiteral("schema"), kSchemaVersion);
  root.insert(QStringLiteral("volume"), ClampVolume(settings.volume));
  root.insert(QStringLiteral("waveform"), settings.waveform_visible);
  return QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';
}

SettingsLoadResult ParseAppSettings(const QByteArray& bytes) {
  SettingsLoadResult result;
  result.settings.volume = kDefaultVolume;
  result.settings.waveform_visible = kDefaultWaveformVisible;

  const QByteArray trimmed = bytes.trimmed();
  if (trimmed.isEmpty()) {
    result.status = SettingsLoadStatus::kMissing;
    return result;
  }

  QJsonParseError error{};
  const QJsonDocument document = QJsonDocument::fromJson(trimmed, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    result.status = SettingsLoadStatus::kCorrupt;
    return result;
  }

  const QJsonObject root = document.object();
  const QJsonValue schema = root.value(QStringLiteral("schema"));
  if (!schema.isDouble()) {
    result.status = SettingsLoadStatus::kCorrupt;
    return result;
  }
  const double schema_value = schema.toDouble();
  if (!std::isfinite(schema_value) || std::floor(schema_value) != schema_value) {
    result.status = SettingsLoadStatus::kCorrupt;
    return result;
  }
  if (schema_value != static_cast<double>(kSchemaVersion)) {
    result.status = SettingsLoadStatus::kUnsupportedSchema;
    return result;
  }

  // 字段缺失或类型不对时保留默认值；数值越界按 0–100 clamp。
  const QJsonValue volume = root.value(QStringLiteral("volume"));
  if (volume.isDouble()) {
    const double raw = volume.toDouble();
    if (std::isfinite(raw)) {
      // 先按范围截断再四舍五入，避免 1e300 这类值在 lround/cast 时溢出。
      if (raw <= 0.0) {
        result.settings.volume = 0;
      } else if (raw >= 100.0) {
        result.settings.volume = 100;
      } else {
        result.settings.volume = static_cast<int>(std::lround(raw));
      }
    }
  }
  const QJsonValue waveform = root.value(QStringLiteral("waveform"));
  if (waveform.isBool()) {
    result.settings.waveform_visible = waveform.toBool();
  }

  // 未知键（含未来版本新增键）直接忽略。
  result.status = SettingsLoadStatus::kLoaded;
  return result;
}

QString SettingsLoadStatusName(SettingsLoadStatus status) {
  switch (status) {
    case SettingsLoadStatus::kLoaded:
      return QStringLiteral("loaded");
    case SettingsLoadStatus::kMissing:
      return QStringLiteral("missing");
    case SettingsLoadStatus::kUnreadable:
      return QStringLiteral("unreadable");
    case SettingsLoadStatus::kCorrupt:
      return QStringLiteral("corrupt");
    case SettingsLoadStatus::kUnsupportedSchema:
      return QStringLiteral("unsupported_schema");
    default:
      return QStringLiteral("unknown");
  }
}

SettingsStore::SettingsStore(QString root_dir)
    : root_dir_(root_dir.isEmpty() ? DefaultRootDir() : std::move(root_dir)) {}

QString SettingsStore::DefaultRootDir() {
  // 与 MarkStore 的 AppData 回退保持同一父目录：%APPDATA%\拉片软件。
  QString appdata = qEnvironmentVariable("APPDATA");
  if (appdata.isEmpty()) {
    appdata = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  }
  if (appdata.isEmpty()) {
    appdata = QDir::homePath() + QStringLiteral("/AppData/Roaming");
  }
  QDir base(appdata);
  if (base.dirName() == QStringLiteral("拉片软件")) {
    return base.absolutePath();
  }
  return base.filePath(QStringLiteral("拉片软件"));
}

QString SettingsStore::file_path() const {
  return QDir(root_dir_).filePath(QStringLiteral("settings.json"));
}

SettingsLoadResult SettingsStore::Load() const {
  SettingsLoadResult result;
  const QFileInfo info(file_path());
  if (!info.exists()) {
    result.status = SettingsLoadStatus::kMissing;
    return result;
  }
  // 路径被目录/设备占用时按不可读处理，不尝试读取。
  if (!info.isFile() || info.size() > kMaxSettingsBytes) {
    result.status = info.isFile() ? SettingsLoadStatus::kCorrupt
                                  : SettingsLoadStatus::kUnreadable;
    return result;
  }

  QFile file(info.absoluteFilePath());
  if (!file.open(QIODevice::ReadOnly)) {
    result.status = SettingsLoadStatus::kUnreadable;
    return result;
  }
  const QByteArray bytes = file.read(kMaxSettingsBytes + 1);
  if (bytes.size() > kMaxSettingsBytes) {
    result.status = SettingsLoadStatus::kCorrupt;
    return result;
  }
  return ParseAppSettings(bytes);
}

bool SettingsStore::Save(const AppSettings& settings) const {
  if (root_dir_.isEmpty()) {
    return false;
  }
  if (!QDir().mkpath(root_dir_)) {
    if (qEnvironmentVariableIsSet("LAPIAN_SETTINGS_LOG")) {
      const QFileInfo root_info(root_dir_);
      std::fprintf(stderr,
                   "T6.3 settings_mkpath_failed root=%s parent=%s parent_exists=%d "
                   "root_exists=%d\n",
                   root_dir_.toUtf8().constData(),
                   root_info.absolutePath().toUtf8().constData(),
                   QFileInfo(root_info.absolutePath()).isDir() ? 1 : 0,
                   root_info.exists() ? 1 : 0);
      std::fflush(stderr);
    }
    return false;
  }
  QSaveFile file(file_path());
  // 不使用 direct write fallback：要么完整替换，要么保留旧文件。
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) {
    if (qEnvironmentVariableIsSet("LAPIAN_SETTINGS_LOG")) {
      std::fprintf(stderr, "T6.3 settings_open_failed path=%s error=%s\n",
                   file.fileName().toUtf8().constData(),
                   file.errorString().toUtf8().constData());
      std::fflush(stderr);
    }
    return false;
  }
  const QByteArray bytes = SerializeAppSettings(settings);
  if (file.write(bytes) != bytes.size()) {
    if (qEnvironmentVariableIsSet("LAPIAN_SETTINGS_LOG")) {
      std::fprintf(stderr, "T6.3 settings_write_failed error=%s\n",
                   file.errorString().toUtf8().constData());
      std::fflush(stderr);
    }
    file.cancelWriting();
    return false;
  }
  if (!file.commit()) {
    if (qEnvironmentVariableIsSet("LAPIAN_SETTINGS_LOG")) {
      std::fprintf(stderr, "T6.3 settings_commit_failed path=%s error=%s\n",
                   file.fileName().toUtf8().constData(),
                   file.errorString().toUtf8().constData());
      std::fflush(stderr);
    }
    return false;
  }
  return true;
}

}  // namespace lapian::core
