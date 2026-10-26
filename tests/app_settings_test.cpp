// T6.3 设置持久化单测：默认值、序列化/解析、非法值 clamp、损坏/未知 schema、
// 未知键忽略、Unicode 路径 roundtrip、原子覆盖写、不可读/不可写回退。
// D1：新增预览分辨率/缓存键（auto/1080p/720p、128–2048MiB）的解析、落盘夹取、
// 内存上限与 ResolvePreviewConfig 纯函数。
// 只依赖 Qt Core 与 lapian_settings；不碰 Qt Widgets / FFmpeg。

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdio>
#include <string>

#include "core/AppSettings.h"

namespace {

using lapian::core::AppSettings;
using lapian::core::ClampPreviewCacheMb;
using lapian::core::kPreviewCacheMinMb1080p;
using lapian::core::kPreviewCacheMinMb720p;
using lapian::core::MergeVolumeWaveform;
using lapian::core::ParseAppSettings;
using lapian::core::ParsePreviewResolutionKey;
using lapian::core::PreviewCacheMinMbForMaxSide;
using lapian::core::PreviewCacheMinMbForResolution;
using lapian::core::PreviewResolution;
using lapian::core::PreviewResolutionKey;
using lapian::core::ResolvedPreviewConfig;
using lapian::core::ResolvePreviewConfig;
using lapian::core::SerializeAppSettings;
using lapian::core::SettingsLoadResult;
using lapian::core::SettingsLoadStatus;
using lapian::core::SettingsLoadStatusName;
using lapian::core::SettingsStore;

int g_failures = 0;

void Check(bool condition, const std::string& what) {
  if (condition) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n", what.c_str());
    ++g_failures;
  }
}

void CheckEq(const std::string& actual, const std::string& expected,
             const std::string& what) {
  if (actual == expected) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n  实际：%s\n  期望：%s\n", what.c_str(), actual.c_str(),
                expected.c_str());
    ++g_failures;
  }
}

void CheckSettings(const SettingsLoadResult& result, SettingsLoadStatus status,
                   int volume, bool waveform, const std::string& what) {
  const bool ok = result.status == status && result.settings.volume == volume &&
                  result.settings.waveform_visible == waveform;
  if (ok) {
    std::printf("[ OK ] %s\n", what.c_str());
    return;
  }
  std::printf(
      "[FAIL] %s\n  实际：status=%s volume=%d waveform=%d\n"
      "  期望：status=%s volume=%d waveform=%d\n",
      what.c_str(), SettingsLoadStatusName(result.status).toUtf8().constData(),
      result.settings.volume, result.settings.waveform_visible ? 1 : 0,
      SettingsLoadStatusName(status).toUtf8().constData(), volume, waveform ? 1 : 0);
  ++g_failures;
}

// T8.3：缓存根/配额（空根 = 默认 %LOCALAPPDATA%\拉片软件\cache）。
void CheckCache(const SettingsLoadResult& result, const QString& cache_root,
                int cache_quota_gb, const std::string& what) {
  const bool ok = result.settings.cache_root == cache_root &&
                  result.settings.cache_quota_gb == cache_quota_gb;
  if (ok) {
    std::printf("[ OK ] %s\n", what.c_str());
    return;
  }
  std::printf("[FAIL] %s\n  实际：root=%s quota=%d\n  期望：root=%s quota=%d\n",
              what.c_str(), result.settings.cache_root.toUtf8().constData(),
              result.settings.cache_quota_gb, cache_root.toUtf8().constData(),
              cache_quota_gb);
  ++g_failures;
}

const char* PreviewResolutionLabel(PreviewResolution resolution) {
  switch (resolution) {
    case PreviewResolution::kAuto:
      return "auto";
    case PreviewResolution::k1080p:
      return "1080p";
    case PreviewResolution::k720p:
      return "720p";
    default:
      return "?";
  }
}

// D1：预览分辨率/缓存（MiB）；合法文件解析后 status 必须是 loaded。
void CheckPreview(const SettingsLoadResult& result, PreviewResolution resolution,
                  int cache_mb, const std::string& what) {
  const bool ok = result.status == SettingsLoadStatus::kLoaded &&
                  result.settings.preview_resolution == resolution &&
                  result.settings.preview_cache_mb == cache_mb;
  if (ok) {
    std::printf("[ OK ] %s\n", what.c_str());
    return;
  }
  std::printf(
      "[FAIL] %s\n  实际：status=%s resolution=%s cache_mb=%d\n"
      "  期望：status=loaded resolution=%s cache_mb=%d\n",
      what.c_str(), SettingsLoadStatusName(result.status).toUtf8().constData(),
      PreviewResolutionLabel(result.settings.preview_resolution),
      result.settings.preview_cache_mb, PreviewResolutionLabel(resolution),
      cache_mb);
  ++g_failures;
}

bool WriteAll(const QString& path, const QByteArray& bytes) {
  const QFileInfo info(path);
  if (!QDir().mkpath(info.absolutePath())) {
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  const bool ok = file.write(bytes) == bytes.size();
  file.close();
  return ok;
}

QByteArray ReadAll(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

void TestParse() {
  {
    const QByteArray bytes = SerializeAppSettings(AppSettings{37, false});
    const QJsonObject object = QJsonDocument::fromJson(bytes).object();
    Check(object.value(QStringLiteral("schema")).toInt() == 1, "serialize-schema");
    Check(object.value(QStringLiteral("volume")).toInt() == 37, "serialize-volume");
    Check(!object.value(QStringLiteral("waveform")).toBool(true), "serialize-waveform");
    // T8.3：缓存键写入但不改 v1 schema（仍是 1）。
    Check(object.value(QStringLiteral("cache_root")).isString(),
          "serialize-cache-root-present");
    Check(object.value(QStringLiteral("cache_root")).toString().isEmpty(),
          "serialize-cache-root-default-empty");
    Check(object.value(QStringLiteral("cache_quota_gb")).toInt() == 20,
          "serialize-cache-quota-default-20");
    CheckSettings(ParseAppSettings(bytes), SettingsLoadStatus::kLoaded, 37, false,
                  "parse-roundtrip");
  }
  {
    // 写入前 clamp：越界值不会落盘成非法状态。
    CheckSettings(ParseAppSettings(SerializeAppSettings(AppSettings{150, true})),
                  SettingsLoadStatus::kLoaded, 100, true, "serialize-clamp-high");
    CheckSettings(ParseAppSettings(SerializeAppSettings(AppSettings{-5, false})),
                  SettingsLoadStatus::kLoaded, 0, false, "serialize-clamp-low");
  }
  CheckSettings(ParseAppSettings(QByteArray()), SettingsLoadStatus::kMissing, 100, true,
                "parse-empty-missing");
  CheckSettings(ParseAppSettings(QByteArrayLiteral("  \n\t ")),
                SettingsLoadStatus::kMissing, 100, true, "parse-whitespace-missing");

  CheckSettings(ParseAppSettings(QByteArrayLiteral("{\"schema\":1}")),
                SettingsLoadStatus::kLoaded, 100, true, "parse-missing-fields-defaults");
  CheckSettings(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"volume\":40}")),
      SettingsLoadStatus::kLoaded, 40, true, "parse-missing-waveform-default");
  CheckSettings(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"waveform\":false}")),
      SettingsLoadStatus::kLoaded, 100, false, "parse-missing-volume-default");
  CheckSettings(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"volume\":\"loud\","
                                         "\"waveform\":0}")),
      SettingsLoadStatus::kLoaded, 100, true, "parse-invalid-types-default");
  CheckSettings(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"volume\":150,"
                                         "\"waveform\":false}")),
      SettingsLoadStatus::kLoaded, 100, false, "parse-clamp-high");
  CheckSettings(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"volume\":-20,"
                                         "\"waveform\":true}")),
      SettingsLoadStatus::kLoaded, 0, true, "parse-clamp-low");
  CheckSettings(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"volume\":1e300,"
                                         "\"waveform\":true}")),
      SettingsLoadStatus::kLoaded, 100, true, "parse-clamp-huge");
  CheckSettings(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"volume\":-1e300,"
                                         "\"waveform\":true}")),
      SettingsLoadStatus::kLoaded, 0, true, "parse-clamp-huge-negative");
  CheckSettings(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"volume\":1e-300,"
                                         "\"waveform\":true}")),
      SettingsLoadStatus::kLoaded, 0, true, "parse-clamp-tiny");
  CheckSettings(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"volume\":42.6,"
                                         "\"waveform\":true}")),
      SettingsLoadStatus::kLoaded, 43, true, "parse-round-fraction-up");
  CheckSettings(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"volume\":42.4,"
                                         "\"waveform\":true}")),
      SettingsLoadStatus::kLoaded, 42, true, "parse-round-fraction-down");

  CheckSettings(
      ParseAppSettings(QByteArrayLiteral(
          "{\"schema\":1,\"volume\":33,\"waveform\":false,"
          "\"unknown\":\"x\",\"future\":{\"nested\":[1,2,3]}}")),
      SettingsLoadStatus::kLoaded, 33, false, "parse-unknown-keys-ignored");

  CheckSettings(ParseAppSettings(QByteArrayLiteral("{")), SettingsLoadStatus::kCorrupt,
                100, true, "parse-corrupt-json");
  CheckSettings(ParseAppSettings(QByteArrayLiteral("[]")), SettingsLoadStatus::kCorrupt,
                100, true, "parse-corrupt-not-object");
  CheckSettings(ParseAppSettings(QByteArrayLiteral("null")),
                SettingsLoadStatus::kCorrupt, 100, true, "parse-corrupt-null");
  CheckSettings(ParseAppSettings(QByteArrayLiteral("{\"volume\":50}")),
                SettingsLoadStatus::kCorrupt, 100, true, "parse-missing-schema");
  // T8.3：缓存键缺失/非法类型 → 默认；越界 → clamp 1–100GB。
  CheckCache(ParseAppSettings(QByteArrayLiteral("{\"schema\":1}")),
             QString(), 20, "parse-cache-missing-defaults");
  CheckCache(
      ParseAppSettings(QByteArrayLiteral(
          "{\"schema\":1,\"cache_root\":\"D:/缓存 根\",\"cache_quota_gb\":7}")),
      QStringLiteral("D:/缓存 根"), 7, "parse-cache-roundtrip");
  CheckCache(
      ParseAppSettings(QByteArrayLiteral(
          "{\"schema\":1,\"cache_root\":42,\"cache_quota_gb\":\"big\"}")),
      QString(), 20, "parse-cache-invalid-types-default");
  CheckCache(ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"cache_quota_gb\":0}")),
             QString(), 1, "parse-cache-quota-clamp-low");
  CheckCache(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"cache_quota_gb\":150}")),
      QString(), 100, "parse-cache-quota-clamp-high");
  CheckCache(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"cache_quota_gb\":-1e300}")),
      QString(), 1, "parse-cache-quota-clamp-huge-negative");
  CheckCache(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"cache_quota_gb\":99.6}")),
      QString(), 100, "parse-cache-quota-round-up");
  CheckCache(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"cache_quota_gb\":1.4}")),
      QString(), 1, "parse-cache-quota-round-down");
  CheckSettings(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1.5,\"volume\":50}")),
      SettingsLoadStatus::kCorrupt, 100, true, "parse-fractional-schema");
  CheckSettings(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":2,\"volume\":10,"
                                         "\"waveform\":false}")),
      SettingsLoadStatus::kUnsupportedSchema, 100, true, "parse-future-schema-default");
}

void TestStore() {
  const QString default_root = SettingsStore::DefaultRootDir();
  Check(QFileInfo(default_root).isAbsolute(), "default-root-absolute");
  Check(default_root.endsWith(QStringLiteral("拉片软件")), "default-root-name");
  Check(QDir::fromNativeSeparators(default_root)
            .contains(QStringLiteral("AppData/Roaming"), Qt::CaseInsensitive),
        "default-root-under-appdata-roaming");
  Check(SettingsLoadStatusName(SettingsLoadStatus::kMissing) ==
            QStringLiteral("missing"),
        "status-name-missing");

  QTemporaryDir temp;
  Check(temp.isValid(), "temp-dir-valid");
  if (!temp.isValid()) {
    return;
  }
  const QString root = temp.path() + QStringLiteral("/中文 设置");
  const SettingsStore store(root);
  Check(store.root_dir() == root, "store-root");
  Check(store.file_path() == root + QStringLiteral("/settings.json"), "store-file-path");

  CheckSettings(store.Load(), SettingsLoadStatus::kMissing, 100, true,
                "store-missing-defaults");
  Check(!QFileInfo::exists(store.file_path()), "store-missing-no-file");
  // 读默认值不能顺手创建配置。
  Check(!QFileInfo::exists(root), "store-load-no-side-effects");

  Check(store.Save(AppSettings{37, false}), "store-save-ok");
  Check(QFileInfo::exists(store.file_path()), "store-save-creates-file");
  Check(QFileInfo(store.file_path()).size() > 0, "store-save-nonempty");
  Check(QFileInfo(store.file_path()).size() <= SettingsStore::kMaxSettingsBytes,
        "store-save-under-limit");
  CheckSettings(store.Load(), SettingsLoadStatus::kLoaded, 37, false,
                "store-roundtrip");
  CheckCache(store.Load(), QString(), 20, "store-roundtrip-cache-defaults");
  // 原子写不应留下临时文件。
  const QStringList leftovers =
      QDir(root).entryList(QDir::Files | QDir::Hidden | QDir::System);
  Check(leftovers.size() == 1 &&
            leftovers.first() == QStringLiteral("settings.json"),
        "store-no-temp-leftovers");

  Check(store.Save(AppSettings{90, true}), "store-overwrite-ok");
  CheckSettings(store.Load(), SettingsLoadStatus::kLoaded, 90, true,
                "store-overwrite-roundtrip");

  // T8.3：缓存根/配额跨会话（含中文路径）；v1 键仍各自生效。
  AppSettings with_cache{55, false};
  with_cache.cache_root = temp.path() + QStringLiteral("/缓存 根");
  with_cache.cache_quota_gb = 33;
  Check(store.Save(with_cache), "store-save-cache-keys");
  const SettingsLoadResult cache_loaded = store.Load();
  CheckSettings(cache_loaded, SettingsLoadStatus::kLoaded, 55, false,
                "store-cache-keeps-v1-keys");
  CheckCache(cache_loaded, with_cache.cache_root, 33, "store-cache-roundtrip");
  // 写入前 clamp：150 → 100。
  AppSettings clamped{10, true};
  clamped.cache_quota_gb = 150;
  Check(store.Save(clamped), "store-save-cache-clamp");
  CheckCache(store.Load(), QString(), 100, "store-cache-clamp-high");

  // T8.3：产品保存音量/波形时保留缓存键（MainWindow 走这个纯函数）。
  AppSettings base{10, true};
  base.cache_root = QStringLiteral("D:/自定义缓存 根");
  base.cache_quota_gb = 42;
  const AppSettings merged = MergeVolumeWaveform(base, 80, false);
  Check(merged.volume == 80 && !merged.waveform_visible, "merge-volume-waveform");
  CheckCache(SettingsLoadResult{merged, SettingsLoadStatus::kLoaded},
             QStringLiteral("D:/自定义缓存 根"), 42, "merge-preserves-cache-keys");
  const AppSettings merged_defaults = MergeVolumeWaveform(AppSettings{}, 300, true);
  Check(merged_defaults.volume == 100, "merge-clamps-volume");
  CheckCache(SettingsLoadResult{merged_defaults, SettingsLoadStatus::kLoaded},
             QString(), 20, "merge-defaults-cache-keys");

  // 损坏文件：回退默认，文件保持原样（只有下一次保存才覆盖）。
  Check(WriteAll(store.file_path(), QByteArrayLiteral("{oops")), "store-write-corrupt");
  CheckSettings(store.Load(), SettingsLoadStatus::kCorrupt, 100, true,
                "store-corrupt-defaults");
  Check(ReadAll(store.file_path()) == QByteArrayLiteral("{oops"),
        "store-corrupt-not-clobbered-by-load");

  // 非法值：逐项 clamp/回退，未知键忽略。
  Check(WriteAll(store.file_path(),
                 QByteArrayLiteral("{\"schema\":1,\"volume\":999,"
                                   "\"waveform\":\"no\",\"extra\":42}")),
        "store-write-invalid");
  CheckSettings(store.Load(), SettingsLoadStatus::kLoaded, 100, true,
                "store-invalid-values-default");

  // 空文件按缺省处理。
  Check(WriteAll(store.file_path(), QByteArray()), "store-write-empty");
  CheckSettings(store.Load(), SettingsLoadStatus::kMissing, 100, true,
                "store-empty-missing");

  // 超过大小上限：不读进内存，回退默认。
  Check(WriteAll(store.file_path(), QByteArray(SettingsStore::kMaxSettingsBytes + 1, 'x')),
        "store-write-oversize");
  CheckSettings(store.Load(), SettingsLoadStatus::kCorrupt, 100, true,
                "store-oversize-corrupt");

  // 路径被目录占用：按不可读回退；保存失败但不崩溃、不抛异常。
  Check(QFile::remove(store.file_path()), "store-remove-before-dir");
  Check(QDir().mkpath(store.file_path()), "store-make-dir-at-file");
  CheckSettings(store.Load(), SettingsLoadStatus::kUnreadable, 100, true,
                "store-dir-unreadable");
  Check(!store.Save(AppSettings{10, false}), "store-save-on-dir-fails-silently");
  QDir().rmdir(store.file_path());

  // 目录本身不可建：静默失败。
  const QString blocked_root = temp.path() + QStringLiteral("/blocked");
  Check(WriteAll(blocked_root, QByteArrayLiteral("not-a-directory")),
        "store-write-blocking-file");
  const SettingsStore blocked(blocked_root);
  Check(!blocked.Save(AppSettings{20, true}), "store-mkdir-failure-silent");
}

void TestPreviewParse() {
  // D1 缺省：新文件缺 preview 键 → auto/512（schema 仍是 1）。
  CheckPreview(ParseAppSettings(QByteArrayLiteral("{\"schema\":1}")),
               PreviewResolution::kAuto, 512, "preview-parse-missing-defaults");

  // D1 roundtrip：四个代表值序列化→解析一致；两个键总是写、resolution 小写规范键。
  struct RoundtripCase {
    PreviewResolution resolution;
    int cache_mb;
    const char* tag;
  };
  const RoundtripCase roundtrips[] = {
      {PreviewResolution::kAuto, 512, "auto/512"},
      {PreviewResolution::k1080p, 1024, "1080p/1024"},
      {PreviewResolution::k720p, 128, "720p/128"},
      {PreviewResolution::k1080p, 2048, "1080p/2048"},
  };
  for (const RoundtripCase& item : roundtrips) {
    AppSettings settings;
    settings.preview_resolution = item.resolution;
    settings.preview_cache_mb = item.cache_mb;
    const QByteArray bytes = SerializeAppSettings(settings);
    const QJsonObject object = QJsonDocument::fromJson(bytes).object();
    const QJsonValue resolution_value = object.value(QStringLiteral("preview_resolution"));
    const QString key = resolution_value.toString();
    Check(resolution_value.isString() &&
              object.value(QStringLiteral("preview_cache_mb")).isDouble(),
          std::string("preview-serialize-keys-present ") + item.tag);
    Check(key == PreviewResolutionKey(item.resolution) && key == key.toLower(),
          std::string("preview-serialize-key-canonical-lowercase ") + item.tag);
    CheckPreview(ParseAppSettings(bytes), item.resolution, item.cache_mb,
                 std::string("preview-roundtrip ") + item.tag);
  }

  // D1 旧 settings（合成 Unicode 路径，不碰用户真实路径）：缺 preview 键 → auto/512，
  // 既有字段不变。
  const SettingsLoadResult legacy = ParseAppSettings(QByteArrayLiteral(
      "{\"schema\":1,\"volume\":46,\"waveform\":false,"
      "\"cache_root\":\"D:/缓存 根\",\"cache_quota_gb\":20}"));
  CheckPreview(legacy, PreviewResolution::kAuto, 512,
               "preview-legacy-missing-keys-default");
  CheckSettings(legacy, SettingsLoadStatus::kLoaded, 46, false,
                "preview-legacy-keeps-v1-keys");
  CheckCache(legacy, QStringLiteral("D:/缓存 根"), 20,
             "preview-legacy-keeps-cache-keys");

  // D1 非法 resolution：未知字符串/数字/null → auto；大小写不敏感。
  CheckPreview(ParseAppSettings(QByteArrayLiteral(
                   "{\"schema\":1,\"preview_resolution\":\"4k\"}")),
               PreviewResolution::kAuto, 512, "preview-resolution-unknown-auto");
  CheckPreview(ParseAppSettings(QByteArrayLiteral(
                   "{\"schema\":1,\"preview_resolution\":1080}")),
               PreviewResolution::kAuto, 512, "preview-resolution-number-auto");
  CheckPreview(ParseAppSettings(QByteArrayLiteral(
                   "{\"schema\":1,\"preview_resolution\":null}")),
               PreviewResolution::kAuto, 512, "preview-resolution-null-auto");
  CheckPreview(ParseAppSettings(QByteArrayLiteral(
                   "{\"schema\":1,\"preview_resolution\":\"1080P\"}")),
               PreviewResolution::k1080p, 512,
               "preview-resolution-case-insensitive");

  // D1 非法 cache（类型不对 → 默认 512，不按类型做 clamp）。
  CheckPreview(ParseAppSettings(QByteArrayLiteral(
                   "{\"schema\":1,\"preview_cache_mb\":\"512\"}")),
               PreviewResolution::kAuto, 512, "preview-cache-string-default");
  CheckPreview(ParseAppSettings(QByteArrayLiteral(
                   "{\"schema\":1,\"preview_cache_mb\":true}")),
               PreviewResolution::kAuto, 512, "preview-cache-bool-default");
  CheckPreview(ParseAppSettings(QByteArrayLiteral(
                   "{\"schema\":1,\"preview_cache_mb\":null}")),
               PreviewResolution::kAuto, 512, "preview-cache-null-default");

  // D1 越界 cache：先四舍五入再按 [下限, 2048] 夹取。
  CheckPreview(ParseAppSettings(QByteArrayLiteral(
                   "{\"schema\":1,\"preview_cache_mb\":100}")),
               PreviewResolution::kAuto, 256, "preview-cache-clamp-low-100");
  CheckPreview(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"preview_cache_mb\":0}")),
      PreviewResolution::kAuto, 256, "preview-cache-clamp-zero");
  CheckPreview(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"preview_cache_mb\":-1}")),
      PreviewResolution::kAuto, 256, "preview-cache-clamp-negative");
  CheckPreview(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"preview_cache_mb\":2048}")),
      PreviewResolution::kAuto, 2048, "preview-cache-clamp-at-max");
  CheckPreview(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"preview_cache_mb\":4096}")),
      PreviewResolution::kAuto, 2048, "preview-cache-clamp-high-4096");
  CheckPreview(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"preview_cache_mb\":1e300}")),
      PreviewResolution::kAuto, 2048, "preview-cache-clamp-huge");
  CheckPreview(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"preview_cache_mb\":-1e300}")),
      PreviewResolution::kAuto, 256, "preview-cache-clamp-huge-negative");
  CheckPreview(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,\"preview_cache_mb\":512.6}")),
      PreviewResolution::kAuto, 513, "preview-cache-round-fraction");
  CheckPreview(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,"
                                         "\"preview_resolution\":\"720p\","
                                         "\"preview_cache_mb\":128}")),
      PreviewResolution::k720p, 128, "preview-cache-720p-at-min");
  CheckPreview(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,"
                                         "\"preview_resolution\":\"720p\","
                                         "\"preview_cache_mb\":64}")),
      PreviewResolution::k720p, 128, "preview-cache-720p-clamp-low");
  CheckPreview(
      ParseAppSettings(QByteArrayLiteral("{\"schema\":1,"
                                         "\"preview_resolution\":\"1080p\","
                                         "\"preview_cache_mb\":128}")),
      PreviewResolution::k1080p, 256, "preview-cache-1080p-clamp-low");

  // D1 未知键（含未知 preview_* 键）继续忽略，不影响新键解析。
  CheckPreview(ParseAppSettings(QByteArrayLiteral(
                   "{\"schema\":1,\"preview_unknown\":1,"
                   "\"preview_resolution\":\"720p\",\"preview_cache_mb\":128,"
                   "\"future\":{\"nested\":[1,2]}}")),
               PreviewResolution::k720p, 128, "preview-unknown-keys-ignored");

  // D1 MergeVolumeWaveform 整体拷贝 base：新键保留，v1 键被更新。
  AppSettings base{10, true};
  base.preview_resolution = PreviewResolution::k1080p;
  base.preview_cache_mb = 1024;
  const AppSettings merged = MergeVolumeWaveform(base, 80, false);
  Check(merged.volume == 80 && !merged.waveform_visible,
        "preview-merge-updates-volume-waveform");
  Check(merged.preview_resolution == PreviewResolution::k1080p &&
            merged.preview_cache_mb == 1024,
        "preview-merge-preserves-preview-keys");

  // D1 规范键/解析：小写规范键、大小写不敏感、非法回退。
  Check(PreviewResolutionKey(PreviewResolution::kAuto) == QStringLiteral("auto") &&
            PreviewResolutionKey(PreviewResolution::k1080p) ==
                QStringLiteral("1080p") &&
            PreviewResolutionKey(PreviewResolution::k720p) == QStringLiteral("720p"),
        "preview-key-canonical");
  Check(ParsePreviewResolutionKey(QStringLiteral("AUTO")) ==
                PreviewResolution::kAuto &&
            ParsePreviewResolutionKey(QStringLiteral("1080P")) ==
                PreviewResolution::k1080p &&
            ParsePreviewResolutionKey(QStringLiteral("720p")) ==
                PreviewResolution::k720p,
        "preview-parse-key-case-insensitive");
  Check(ParsePreviewResolutionKey(QStringLiteral("4k")) ==
                PreviewResolution::kAuto &&
            ParsePreviewResolutionKey(QString()) == PreviewResolution::kAuto,
        "preview-parse-key-invalid-fallback");
  Check(ParsePreviewResolutionKey(QStringLiteral("bogus"),
                                 PreviewResolution::k720p) ==
            PreviewResolution::k720p,
        "preview-parse-key-custom-fallback");

  // D1 clamp：下限 → 2048 → 可用内存 25%；25% 低于下限时保下限并报告冲突。
  bool limited = false;
  Check(ClampPreviewCacheMb(2048, PreviewResolution::k1080p, 4096, &limited) == 1024 &&
            limited,
        "preview-clamp-memory-quarter");
  limited = false;
  Check(ClampPreviewCacheMb(2048, PreviewResolution::k1080p, 800, &limited) ==
                kPreviewCacheMinMb1080p &&
            limited,
        "preview-clamp-memory-below-min");
  limited = false;
  Check(ClampPreviewCacheMb(2048, PreviewResolution::k1080p, 0, &limited) == 2048 &&
            !limited,
        "preview-clamp-memory-unknown");
  Check(ClampPreviewCacheMb(2048, PreviewResolution::k1080p, 4096, nullptr) == 1024,
        "preview-clamp-null-limited-pointer");
  Check(ClampPreviewCacheMb(100, PreviewResolution::k720p, 0, nullptr) ==
            kPreviewCacheMinMb720p,
        "preview-clamp-720p-min");
  Check(PreviewCacheMinMbForMaxSide(1280) == kPreviewCacheMinMb720p &&
            PreviewCacheMinMbForMaxSide(1920) == kPreviewCacheMinMb1080p,
        "preview-min-mb-by-max-side");
  Check(PreviewCacheMinMbForResolution(PreviewResolution::k720p) ==
                kPreviewCacheMinMb720p &&
            PreviewCacheMinMbForResolution(PreviewResolution::kAuto) ==
                kPreviewCacheMinMb1080p &&
            PreviewCacheMinMbForResolution(PreviewResolution::k1080p) ==
                kPreviewCacheMinMb1080p,
        "preview-min-mb-by-resolution");
}

void TestPreviewResolve() {
  AppSettings auto_settings;  // 默认 auto/512
  AppSettings settings_720;
  settings_720.preview_resolution = PreviewResolution::k720p;
  settings_720.preview_cache_mb = 128;
  AppSettings settings_1080;
  settings_1080.preview_resolution = PreviewResolution::k1080p;
  settings_1080.preview_cache_mb = 1024;

  // 源 3840 + auto → 1920；4K 只到 1080P，不向上放大。
  const ResolvedPreviewConfig auto_4k = ResolvePreviewConfig(auto_settings, 3840, 0);
  Check(auto_4k.max_side == 1920 && auto_4k.follow_source &&
            auto_4k.cache_mb == 512 && !auto_4k.source_limited &&
            !auto_4k.cache_lifted_to_min && !auto_4k.cache_limited_by_memory,
        "preview-resolve-auto-4k-source");

  // 源 1280 + auto → 1280；cache_bytes = cache_mb * 1024 * 1024。
  const ResolvedPreviewConfig auto_720 = ResolvePreviewConfig(auto_settings, 1280, 0);
  Check(auto_720.max_side == 1280 && auto_720.follow_source &&
            auto_720.cache_bytes ==
                static_cast<std::size_t>(auto_720.cache_mb) * 1024u * 1024u,
        "preview-resolve-auto-720p-source-cache-bytes");

  // 源 1920 + 720p → 1280（降采样）。
  const ResolvedPreviewConfig downscale = ResolvePreviewConfig(settings_720, 1920, 0);
  Check(downscale.max_side == 1280 && !downscale.follow_source &&
            downscale.cache_mb == 128,
        "preview-resolve-720p-downscales-1080p-source");

  // 源 1920 + 1080p → 1920，不算 source_limited。
  const ResolvedPreviewConfig native_1080 =
      ResolvePreviewConfig(settings_1080, 1920, 0);
  Check(native_1080.max_side == 1920 && !native_1080.source_limited &&
            native_1080.cache_mb == 1024 &&
            native_1080.cache_bytes == static_cast<std::size_t>(1024) * 1024u * 1024u,
        "preview-resolve-1080p-native");

  // 源 1280 + 1080p → 1280，source_limited=true（不向上放大）。
  const ResolvedPreviewConfig limited_source =
      ResolvePreviewConfig(settings_1080, 1280, 0);
  Check(limited_source.max_side == 1280 && limited_source.source_limited,
        "preview-resolve-1080p-source-limited");

  // 源未知（0）+ auto → 1920。
  const ResolvedPreviewConfig unknown_source = ResolvePreviewConfig(auto_settings, 0, 0);
  Check(unknown_source.max_side == 1920 && unknown_source.cache_mb == 512,
        "preview-resolve-unknown-source");

  // 可用内存 25% 夹：申请 2048MiB + 4GiB 可用 → 1024MiB。
  AppSettings memory_settings;
  memory_settings.preview_resolution = PreviewResolution::k1080p;
  memory_settings.preview_cache_mb = 2048;
  const ResolvedPreviewConfig memory_limited =
      ResolvePreviewConfig(memory_settings, 1920, 4096);
  Check(memory_limited.cache_mb == 1024 && memory_limited.cache_limited_by_memory,
        "preview-resolve-memory-limited");

  // 低于下限：100 → 256 并标记 cache_lifted_to_min。
  AppSettings lifted;
  lifted.preview_resolution = PreviewResolution::k1080p;
  lifted.preview_cache_mb = 100;
  const ResolvedPreviewConfig lifted_config = ResolvePreviewConfig(lifted, 1920, 0);
  Check(lifted_config.cache_mb == kPreviewCacheMinMb1080p &&
            lifted_config.cache_lifted_to_min,
        "preview-resolve-lifted-to-min");
}

void TestPreviewStore() {
  QTemporaryDir temp;
  Check(temp.isValid(), "preview-temp-dir-valid");
  if (!temp.isValid()) {
    return;
  }
  // 合成 Unicode 路径：不读写用户 %APPDATA%\拉片软件。
  const QString root = temp.path() + QStringLiteral("/预览 设置");
  const SettingsStore store(root);

  AppSettings settings{46, false};
  settings.cache_root = QStringLiteral("D:/缓存 根");
  settings.cache_quota_gb = 20;
  settings.preview_resolution = PreviewResolution::k1080p;
  settings.preview_cache_mb = 1024;
  Check(store.Save(settings), "preview-store-save-ok");
  const SettingsLoadResult loaded = store.Load();
  CheckPreview(loaded, PreviewResolution::k1080p, 1024, "preview-store-roundtrip");
  CheckSettings(loaded, SettingsLoadStatus::kLoaded, 46, false,
                "preview-store-keeps-v1-keys");
  CheckCache(loaded, QStringLiteral("D:/缓存 根"), 20,
             "preview-store-keeps-cache-keys");
  const QJsonObject saved = QJsonDocument::fromJson(ReadAll(store.file_path())).object();
  Check(saved.value(QStringLiteral("preview_resolution")).toString() ==
            QStringLiteral("1080p"),
        "preview-store-writes-lowercase-key");
  Check(saved.value(QStringLiteral("preview_cache_mb")).toInt() == 1024,
        "preview-store-writes-cache-mb");

  // 旧文件（无 preview 键）→ auto/512；旧字段保持。
  Check(WriteAll(store.file_path(),
                 QByteArrayLiteral("{\"schema\":1,\"volume\":46,"
                                   "\"waveform\":false,"
                                   "\"cache_root\":\"D:/缓存 根\","
                                   "\"cache_quota_gb\":20}")),
        "preview-store-write-legacy");
  const SettingsLoadResult legacy = store.Load();
  CheckPreview(legacy, PreviewResolution::kAuto, 512,
               "preview-store-legacy-defaults");
  CheckSettings(legacy, SettingsLoadStatus::kLoaded, 46, false,
                "preview-store-legacy-keeps-v1-keys");

  // 产品路径保存音量/波形（MergeVolumeWaveform）：preview 键必须保留。
  Check(store.Save(MergeVolumeWaveform(legacy.settings, 80, true)),
        "preview-store-save-merge");
  const SettingsLoadResult merged_loaded = store.Load();
  CheckPreview(merged_loaded, PreviewResolution::kAuto, 512,
               "preview-store-merge-keeps-preview");
  CheckSettings(merged_loaded, SettingsLoadStatus::kLoaded, 80, true,
                "preview-store-merge-updates-v1-keys");

  // 落盘不留非法值：720p + 10MiB 先抬到 128 再写。
  AppSettings invalid_cache;
  invalid_cache.preview_resolution = PreviewResolution::k720p;
  invalid_cache.preview_cache_mb = 10;
  Check(store.Save(invalid_cache), "preview-store-save-invalid-cache");
  Check(QJsonDocument::fromJson(ReadAll(store.file_path()))
                .object()
                .value(QStringLiteral("preview_cache_mb"))
                .toInt() == 128,
        "preview-store-clamps-before-write");
  CheckPreview(store.Load(), PreviewResolution::k720p, 128,
               "preview-store-clamped-roundtrip");
}

}  // namespace

int main() {
  TestParse();
  TestStore();
  TestPreviewParse();
  TestPreviewResolve();
  TestPreviewStore();
  if (g_failures == 0) {
    std::printf("T6.3 app_settings_test: all checks passed\n");
    return 0;
  }
  std::printf("T6.3 app_settings_test: %d failure(s)\n", g_failures);
  return 1;
}
