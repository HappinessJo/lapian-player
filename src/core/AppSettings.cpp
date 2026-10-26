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

// T8.3：缓存配额 1–100GB；越界截断，非法值用默认。
int ClampCacheQuotaGb(int quota_gb) {
  return std::clamp(quota_gb, kCacheQuotaMinGb, kCacheQuotaMaxGb);
}

}  // namespace

QByteArray SerializeAppSettings(const AppSettings& settings) {
  QJsonObject root;
  root.insert(QStringLiteral("schema"), kSchemaVersion);
  root.insert(QStringLiteral("volume"), ClampVolume(settings.volume));
  root.insert(QStringLiteral("waveform"), settings.waveform_visible);
  // T8.3 新增键：旧版本读到会当未知键忽略（不改变 v1 键语义）。
  root.insert(QStringLiteral("cache_root"), settings.cache_root);
  root.insert(QStringLiteral("cache_quota_gb"), ClampCacheQuotaGb(settings.cache_quota_gb));
  // D1：预览两个键总是写；resolution 写小写规范键，cache 落盘前先夹到合法范围
  // （不按内存夹），旧版本读到同样当未知键忽略。
  root.insert(QStringLiteral("preview_resolution"),
              PreviewResolutionKey(settings.preview_resolution));
  root.insert(QStringLiteral("preview_cache_mb"),
              ClampPreviewCacheMb(settings.preview_cache_mb,
                                  settings.preview_resolution, 0, nullptr));
  return QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';
}

SettingsLoadResult ParseAppSettings(const QByteArray& bytes) {  SettingsLoadResult result;
  result.settings.volume = kDefaultVolume;
  result.settings.waveform_visible = kDefaultWaveformVisible;
  result.settings.cache_root.clear();
  result.settings.cache_quota_gb = kCacheQuotaDefaultGb;
  result.settings.preview_resolution = PreviewResolution::kAuto;
  result.settings.preview_cache_mb = kPreviewCacheDefaultMb;

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
  // T8.3：缓存根按原样保存（空/类型不对 → 默认根）；配额按 1–100GB clamp。
  const QJsonValue cache_root = root.value(QStringLiteral("cache_root"));
  if (cache_root.isString()) {
    result.settings.cache_root = cache_root.toString();
  }
  const QJsonValue cache_quota = root.value(QStringLiteral("cache_quota_gb"));
  if (cache_quota.isDouble()) {
    const double raw = cache_quota.toDouble();
    if (std::isfinite(raw)) {
      if (raw <= static_cast<double>(kCacheQuotaMinGb)) {
        result.settings.cache_quota_gb = kCacheQuotaMinGb;
      } else if (raw >= static_cast<double>(kCacheQuotaMaxGb)) {
        result.settings.cache_quota_gb = kCacheQuotaMaxGb;
      } else {
        result.settings.cache_quota_gb = static_cast<int>(std::lround(raw));
      }
    }
  }
  // D1：预览分辨率非字符串/未知字符串 → auto；缓存非数字/非有限值 → 512，
  // 数字越界先四舍五入再按当前分辨率的 [下限, 2048] 夹取。
  const QJsonValue preview_resolution = root.value(QStringLiteral("preview_resolution"));
  if (preview_resolution.isString()) {
    result.settings.preview_resolution =
        ParsePreviewResolutionKey(preview_resolution.toString());
  }
  const QJsonValue preview_cache = root.value(QStringLiteral("preview_cache_mb"));
  if (preview_cache.isDouble()) {
    const double raw = preview_cache.toDouble();
    if (std::isfinite(raw)) {
      const int min_mb = PreviewCacheMinMbForResolution(result.settings.preview_resolution);
      int rounded = min_mb;
      if (raw <= static_cast<double>(min_mb)) {
        // 先按范围截断再四舍五入，避免 1e300/-1e300 在 lround/cast 时溢出。
        rounded = min_mb;
      } else if (raw >= static_cast<double>(kPreviewCacheMaxMb)) {
        rounded = kPreviewCacheMaxMb;
      } else {
        rounded = static_cast<int>(std::lround(raw));
      }
      result.settings.preview_cache_mb =
          ClampPreviewCacheMb(rounded, result.settings.preview_resolution, 0, nullptr);
    }
  }

  // 未知键（含未来版本新增键）直接忽略。
  result.status = SettingsLoadStatus::kLoaded;
  return result;
}

AppSettings MergeVolumeWaveform(const AppSettings& base, int volume,
                                bool waveform_visible) {
  AppSettings merged = base;
  merged.volume = ClampVolume(volume);
  merged.waveform_visible = waveform_visible;
  merged.cache_quota_gb = ClampCacheQuotaGb(base.cache_quota_gb);
  return merged;
}

QString PreviewResolutionKey(PreviewResolution resolution) {
  switch (resolution) {
    case PreviewResolution::k1080p:
      return QStringLiteral("1080p");
    case PreviewResolution::k720p:
      return QStringLiteral("720p");
    case PreviewResolution::kAuto:
    default:
      return QStringLiteral("auto");
  }
}

PreviewResolution ParsePreviewResolutionKey(const QString& key,
                                            PreviewResolution fallback) {
  if (key.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0) {
    return PreviewResolution::kAuto;
  }
  if (key.compare(QStringLiteral("1080p"), Qt::CaseInsensitive) == 0) {
    return PreviewResolution::k1080p;
  }
  if (key.compare(QStringLiteral("720p"), Qt::CaseInsensitive) == 0) {
    return PreviewResolution::k720p;
  }
  return fallback;
}

int PreviewCacheMinMbForMaxSide(int max_side) {
  return max_side <= 1280 ? kPreviewCacheMinMb720p : kPreviewCacheMinMb1080p;
}

int PreviewCacheMinMbForResolution(PreviewResolution resolution) {
  // auto 可能落到 1080P，取下限的保守值 256。
  return resolution == PreviewResolution::k720p ? kPreviewCacheMinMb720p
                                                : kPreviewCacheMinMb1080p;
}

int ClampPreviewCacheMb(int mb, PreviewResolution resolution,
                        int available_memory_mb, bool* limited_by_memory) {
  if (limited_by_memory != nullptr) {
    *limited_by_memory = false;
  }
  const int min_mb = PreviewCacheMinMbForResolution(resolution);
  int clamped = std::clamp(mb, min_mb, kPreviewCacheMaxMb);
  if (available_memory_mb > 0) {
    const int memory_cap_mb = available_memory_mb / 4;  // 可用内存 25%
    if (memory_cap_mb < min_mb) {
      // 可用内存太小：保下限，冲突通过 limited_by_memory 报告。
      clamped = min_mb;
      if (limited_by_memory != nullptr) {
        *limited_by_memory = true;
      }
    } else if (clamped > memory_cap_mb) {
      clamped = memory_cap_mb;
      if (limited_by_memory != nullptr) {
        *limited_by_memory = true;
      }
    }
  }
  return clamped;
}

ResolvedPreviewConfig ResolvePreviewConfig(const AppSettings& settings,
                                           int source_max_side,
                                           int available_memory_mb) {
  ResolvedPreviewConfig config;
  // 源未知（<=0）按 1920 处理；任何档位都不向上放大。
  const int source_side = source_max_side > 0 ? source_max_side : 1920;
  const int cap_side =
      settings.preview_resolution == PreviewResolution::k720p ? 1280 : 1920;
  config.max_side = std::min(cap_side, source_side);
  config.follow_source = settings.preview_resolution == PreviewResolution::kAuto;
  config.source_limited = settings.preview_resolution == PreviewResolution::k1080p &&
                          source_side < 1920;
  const int min_mb = PreviewCacheMinMbForResolution(settings.preview_resolution);
  config.cache_mb = ClampPreviewCacheMb(settings.preview_cache_mb,
                                        settings.preview_resolution,
                                        available_memory_mb,
                                        &config.cache_limited_by_memory);
  config.cache_lifted_to_min = settings.preview_cache_mb < min_mb;
  config.cache_bytes = static_cast<std::size_t>(config.cache_mb) * 1024u * 1024u;
  return config;
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
