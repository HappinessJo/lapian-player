// T6.3 设置持久化单测：默认值、序列化/解析、非法值 clamp、损坏/未知 schema、
// 未知键忽略、Unicode 路径 roundtrip、原子覆盖写、不可读/不可写回退。
// 只依赖 Qt Core 与 lapian_settings；不碰 Qt Widgets / FFmpeg。

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdio>
#include <string>

#include "core/AppSettings.h"

namespace {

using lapian::core::AppSettings;
using lapian::core::MergeVolumeWaveform;
using lapian::core::ParseAppSettings;
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

}  // namespace

int main() {
  TestParse();
  TestStore();
  if (g_failures == 0) {
    std::printf("T6.3 app_settings_test: all checks passed\n");
    return 0;
  }
  std::printf("T6.3 app_settings_test: %d failure(s)\n", g_failures);
  return 1;
}
