// T8.3 缓存存储与配额单测（纯本地存储，不联网、不需要 SESSDATA）。
//
// 覆盖（对应 tasks-v2 T8.3 验收）：
// - 默认根 %LOCALAPPDATA%\拉片软件\cache（不叠第二层）、默认配额 20 GiB；
// - 缓存键校验（bvid/cid/qn/codecid）与路径穿越防护；
// - meta.json 字段 roundtrip / 原子写 / 损坏/缺字段不崩；
// - .part → .mp4 原子改名、complete=true、幂等；
// - 续传：按 .part 实际长度校正，不拼接双份；
// - 300MB 配额下 LRU 顺序、当前会话不淘汰、总占用 ≤ 配额；
// - 20GB 记账/淘汰单测（稀疏文件，不真填满）；
// - 损坏检测/重建（只删该条目数据）；
// - 清空保留在线记录/截图（缓存根外的 sentinel 不动）；
// - 磁盘满预检 ≤5s 可读错误（用不可满足的 expected 走同一预检路径）；
// - 路径/配额跨会话（settings.json 新键）；
// - Unicode 路径与 >MAX_PATH 长路径的创建/改名/读取；
// - 缓存 meta 无明文 Cookie/签名 URL。
//
// 退出码：0 = 全部通过；1 = 有失败。

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

#include "core/AppSettings.h"
#include "online/CacheStore.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>

namespace {

using lapian::core::AppSettings;
using lapian::core::SettingsLoadStatus;
using lapian::core::SettingsStore;
using lapian::online::CacheEntry;
using lapian::online::CacheKey;
using lapian::online::CacheMetadata;
using lapian::online::CacheStore;
using lapian::online::CleanupOptions;

constexpr qint64 kKb = 1024;
constexpr qint64 kMb = 1024 * kKb;
constexpr qint64 kGb = 1024 * kMb;

int g_failures = 0;
int g_checks = 0;
int g_skips = 0;

void Check(bool condition, const std::string& what) {
  ++g_checks;
  if (condition) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n", what.c_str());
    ++g_failures;
  }
}

void CheckEq(const QString& actual, const QString& expected,
             const std::string& what) {
  ++g_checks;
  if (actual == expected) {
    std::printf("[ OK ] %s\n", what.c_str());
    return;
  }
  std::printf("[FAIL] %s\n  实际：%s\n  期望：%s\n", what.c_str(),
              actual.toUtf8().constData(), expected.toUtf8().constData());
  ++g_failures;
}

void CheckEqI64(qint64 actual, qint64 expected, const std::string& what) {
  ++g_checks;
  if (actual == expected) {
    std::printf("[ OK ] %s\n", what.c_str());
    return;
  }
  std::printf("[FAIL] %s\n  实际：%lld\n  期望：%lld\n", what.c_str(),
              static_cast<long long>(actual), static_cast<long long>(expected));
  ++g_failures;
}

void CheckGe(qint64 actual, qint64 expected, const std::string& what) {
  ++g_checks;
  if (actual >= expected) {
    std::printf("[ OK ] %s\n", what.c_str());
    return;
  }
  std::printf("[FAIL] %s\n  实际：%lld\n  期望至少：%lld\n", what.c_str(),
              static_cast<long long>(actual), static_cast<long long>(expected));
  ++g_failures;
}

void CheckLe(qint64 actual, qint64 expected, const std::string& what) {
  ++g_checks;
  if (actual <= expected) {
    std::printf("[ OK ] %s\n", what.c_str());
    return;
  }
  std::printf("[FAIL] %s\n  实际：%lld\n  期望最多：%lld\n", what.c_str(),
              static_cast<long long>(actual), static_cast<long long>(expected));
  ++g_failures;
}

void Skip(const std::string& what, const QString& reason) {
  ++g_skips;
  std::printf("[SKIP] %s：%s\n", what.c_str(), reason.toUtf8().constData());
}

// 12 位合法 BV：BV + 1 + 9 个同字母。
QString Bvid(QChar letter) { return QStringLiteral("BV1") + QString(9, letter); }

bool EnsureParent(const QString& path) {
  return QDir().mkpath(QFileInfo(path).absolutePath());
}

bool WriteAll(const QString& path, const QByteArray& bytes) {
  if (!EnsureParent(path)) {
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

// 标记稀疏：20GB 记账测试只造逻辑大小，不真占盘。
bool MarkSparse(const QString& path) {
  const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
  const std::wstring wide =
      (native.startsWith(QStringLiteral("\\\\?\\"))
           ? native
           : QStringLiteral("\\\\?\\") + native)
          .toStdWString();
  HANDLE handle = CreateFileW(wide.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD returned = 0;
  const BOOL ok = DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0,
                                  &returned, nullptr);
  CloseHandle(handle);
  return ok != 0;
}

bool ResizeFile(const QString& path, qint64 size) {
  if (!EnsureParent(path)) {
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadWrite)) {
    return false;
  }
  const bool ok = file.resize(size);
  file.close();
  return ok;
}

// 造一个逻辑大小为 size 的数据文件（稀疏；不写内容）。
bool CreateSizedFile(const QString& path, qint64 size) {
  if (!EnsureParent(path)) {
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  file.close();
  MarkSparse(path);  // 失败也继续：NTFS 的 SetEndOfFile 通常是惰性分配
  return ResizeFile(path, size);
}

QDateTime AgoSecs(qint64 seconds) {
  return QDateTime::currentDateTimeUtc().addSecs(-seconds);
}

QString IsoText(const QDateTime& time) {
  return time.toUTC().toString(Qt::ISODateWithMs);
}

// 直接写 meta.json（模拟旧记录/损坏/旧 last_access_at；不走 CacheStore）。
bool WriteRawMeta(const CacheStore& store, const CacheKey& key, qint64 total_bytes,
                  qint64 downloaded_bytes, bool complete,
                  const QDateTime& created_at, const QDateTime& last_access_at,
                  const QJsonObject& extra = QJsonObject()) {
  QJsonObject root;
  root.insert(QStringLiteral("schema"), QStringLiteral("lapian-cache-1"));
  root.insert(QStringLiteral("bvid"), key.bvid);
  root.insert(QStringLiteral("cid"), QJsonValue(key.cid));
  root.insert(QStringLiteral("title"), QStringLiteral("测试标题"));
  root.insert(QStringLiteral("owner"), QStringLiteral("测试UP"));
  root.insert(QStringLiteral("duration_ms"), QJsonValue(qint64{111000}));
  root.insert(QStringLiteral("qn"), key.qn);
  root.insert(QStringLiteral("codecid"), key.codecid);
  root.insert(QStringLiteral("width"), 1920);
  root.insert(QStringLiteral("height"), 1080);
  root.insert(QStringLiteral("fps"), 29.412);
  root.insert(QStringLiteral("total_bytes"), QJsonValue(total_bytes));
  root.insert(QStringLiteral("downloaded_bytes"), QJsonValue(downloaded_bytes));
  root.insert(QStringLiteral("complete"), complete);
  root.insert(QStringLiteral("created_at"), IsoText(created_at));
  root.insert(QStringLiteral("last_access_at"), IsoText(last_access_at));
  root.insert(QStringLiteral("source_fps_hint"), QString());
  for (auto it = extra.begin(); it != extra.end(); ++it) {
    root.insert(it.key(), it.value());
  }
  const QString dir = store.EntryDir(key);
  if (dir.isEmpty()) {
    return false;
  }
  return WriteAll(QDir(dir).filePath(QStringLiteral("meta.json")),
                  QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n');
}

// 造一条完整条目（数据文件 + meta），用于 LRU/记账测试，绕开 Reserve 的空间预检。
bool CreateRawCompleteEntry(const CacheStore& store, const CacheKey& key,
                            qint64 total_bytes, const QDateTime& last_access_at) {
  if (!CreateSizedFile(store.Mp4Path(key), total_bytes)) {
    return false;
  }
  return WriteRawMeta(store, key, total_bytes, total_bytes, true,
                      last_access_at.addSecs(-3600), last_access_at);
}

bool CreateCompleteEntry(CacheStore* store, const CacheKey& key, qint64 total_bytes,
                         const QString& title = QStringLiteral("缓存标题")) {
  QString error;
  if (!store->Reserve(key, total_bytes, &error)) {
    std::printf("  Reserve 失败：%s\n", error.toUtf8().constData());
    return false;
  }
  CacheMetadata metadata;
  metadata.title = title;
  metadata.owner = QStringLiteral("UP主");
  metadata.duration_ms = 111000;
  metadata.width = 1920;
  metadata.height = 1080;
  metadata.fps = 29.412;
  metadata.source_fps_hint = QStringLiteral("60");
  if (!store->SetMetadata(key, metadata, &error)) {
    std::printf("  SetMetadata 失败：%s\n", error.toUtf8().constData());
    return false;
  }
  if (!CreateSizedFile(store->PartPath(key), total_bytes)) {
    return false;
  }
  if (!store->MarkComplete(key, &error)) {
    std::printf("  MarkComplete 失败：%s\n", error.toUtf8().constData());
    return false;
  }
  return true;
}

// 部分下载：Reserve 后把 .part 扩到 downloaded 字节并校正 meta。
bool CreateIncompleteEntry(CacheStore* store, const CacheKey& key, qint64 total_bytes,
                           qint64 downloaded_bytes) {
  QString error;
  if (!store->Reserve(key, total_bytes, &error)) {
    std::printf("  Reserve 失败：%s\n", error.toUtf8().constData());
    return false;
  }
  if (downloaded_bytes > 0 &&
      !CreateSizedFile(store->PartPath(key), downloaded_bytes)) {
    return false;
  }
  QString part_path;
  qint64 existing = 0;
  if (!store->OpenPart(key, &part_path, &existing, &error)) {
    std::printf("  OpenPart 失败：%s\n", error.toUtf8().constData());
    return false;
  }
  return true;
}

QStringList Labels(const QVector<CacheEntry>& list) {
  QStringList out;
  out.reserve(list.size());
  for (const CacheEntry& entry : list) {
    out << lapian::online::CacheKeyLabel(entry.key);
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool HasKey(const QVector<CacheEntry>& list, const CacheKey& key) {
  for (const CacheEntry& entry : list) {
    if (entry.key == key) {
      return true;
    }
  }
  return false;
}

// 非本工具布局的目录/文件：清空/清理都不该碰。
bool CreateForeignPaths(const QString& root) {
  const bool a = WriteAll(QDir(root).filePath(QStringLiteral("readme.txt")),
                          QByteArrayLiteral("not-a-cache-entry"));
  const bool b =
      WriteAll(QDir(root).filePath(QStringLiteral("notabvid/1/64-7/data.part")),
               QByteArrayLiteral("x"));
  const bool c = WriteAll(
      QDir(root).filePath(QStringLiteral("BV1BMnv6jE5S/abc/64-7/data.part")),
      QByteArrayLiteral("x"));
  const bool d = WriteAll(
      QDir(root).filePath(QStringLiteral("BV1BMnv6jE5S/123/64-7-8/data.part")),
      QByteArrayLiteral("x"));
  return a && b && c && d;
}

bool PatchMeta(const CacheStore& store, const CacheKey& key,
               const QJsonObject& overrides) {
  const QString dir = store.EntryDir(key);
  if (dir.isEmpty()) {
    return false;
  }
  const QString path = QDir(dir).filePath(QStringLiteral("meta.json"));
  const QJsonDocument document = QJsonDocument::fromJson(ReadAll(path));
  if (!document.isObject()) {
    return false;
  }
  QJsonObject root = document.object();
  for (auto it = overrides.begin(); it != overrides.end(); ++it) {
    root.insert(it.key(), it.value());
  }
  return WriteAll(path, QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n');
}

qint64 MetaBytes(const CacheStore& store, const CacheKey& key) {
  return QFileInfo(QDir(store.EntryDir(key)).filePath(QStringLiteral("meta.json"))).size();
}

// ---------------------------------------------------------------- 默认值与键

void TestDefaultsAndKeys() {
  QTemporaryDir local;
  Check(local.isValid(), "default-root-temp-dir");
  // “不叠第二层 拉片软件”是纯函数行为；不依赖修改进程环境变量。
  CheckEq(QDir::fromNativeSeparators(
              CacheStore::DefaultRootFor(QStringLiteral("C:/Users/x/AppData/Local"))),
          QStringLiteral("C:/Users/x/AppData/Local/拉片软件/cache"),
          "default-root-localappdata");
  CheckEq(QDir::fromNativeSeparators(
              CacheStore::DefaultRootFor(QStringLiteral("C:/Users/x/AppData/Local/拉片软件"))),
          QStringLiteral("C:/Users/x/AppData/Local/拉片软件/cache"),
          "default-root-already-app-named");
  Check(!QDir::fromNativeSeparators(
             CacheStore::DefaultRootFor(QStringLiteral("C:/tmp/x")))
             .contains(QStringLiteral("拉片软件/拉片软件")),
        "default-root-no-double-app-dir");
  Check(QFileInfo(CacheStore::DefaultRoot()).isAbsolute(), "default-root-absolute");
  Check(CacheStore::DefaultRoot().contains(QStringLiteral("拉片软件")),
        "default-root-has-app-dir");
  const QString real_local_app_data = qEnvironmentVariable("LOCALAPPDATA");
  if (!real_local_app_data.isEmpty()) {
    CheckEq(QDir::fromNativeSeparators(CacheStore::DefaultRoot()),
            QDir::fromNativeSeparators(
                CacheStore::DefaultRootFor(real_local_app_data)),
            "default-root-uses-localappdata-env");
  } else {
    Skip("default-root-uses-localappdata-env",
         QStringLiteral("本机没有 LOCALAPPDATA（回退 GenericCacheLocation）"));
  }
  CheckEqI64(CacheStore::DefaultQuotaBytes(), 20 * kGb, "default-quota-20gib");
  CheckEqI64(CacheStore::kSpaceHeadroomBytes, 512 * kMb, "space-headroom-512mb");
  Check(CacheStore::kMinQuotaBytes <= 300 * kMb, "min-quota-allows-300mb-test");

  const CacheKey good{QStringLiteral("BV1BMnv6jE5S"), 123456, 64, 7};
  Check(lapian::online::IsValidCacheKey(good), "key-valid");
  Check(!lapian::online::IsValidCacheKey({QString(), 1, 1, 1}), "key-invalid-empty-bvid");
  Check(!lapian::online::IsValidCacheKey({QStringLiteral("BV1"), 1, 1, 1}),
        "key-invalid-short-bvid");
  Check(!lapian::online::IsValidCacheKey({QStringLiteral("BV1BMnv6jE5"), 1, 1, 1}),
        "key-invalid-11-char-bvid");
  Check(!lapian::online::IsValidCacheKey({QStringLiteral("BV1BMnv6jE5SS"), 1, 1, 1}),
        "key-invalid-13-char-bvid");
  Check(!lapian::online::IsValidCacheKey({QStringLiteral("bv1BMnv6jE5S"), 1, 1, 1}),
        "key-invalid-lowercase");
  Check(!lapian::online::IsValidCacheKey({QStringLiteral("../BV1BMnv6jE5"), 1, 1, 1}),
        "key-invalid-dotdot");
  Check(!lapian::online::IsValidCacheKey({QStringLiteral("BV1BMnv6jE5/"), 1, 1, 1}),
        "key-invalid-slash");
  Check(!lapian::online::IsValidCacheKey({QStringLiteral("BV1BMnv6jE5\\"), 1, 1, 1}),
        "key-invalid-backslash");
  Check(!lapian::online::IsValidCacheKey({QStringLiteral("BV1BMnv6jE5."), 1, 1, 1}),
        "key-invalid-dot");
  Check(!lapian::online::IsValidCacheKey({good.bvid, 0, 64, 7}), "key-invalid-cid-zero");
  Check(!lapian::online::IsValidCacheKey({good.bvid, -5, 64, 7}),
        "key-invalid-cid-negative");
  Check(!lapian::online::IsValidCacheKey({good.bvid, 1, 0, 7}), "key-invalid-qn-zero");
  Check(!lapian::online::IsValidCacheKey({good.bvid, 1, -64, 7}), "key-invalid-qn-negative");
  Check(!lapian::online::IsValidCacheKey({good.bvid, 1, 64, 0}),
        "key-invalid-codecid-zero");

  CheckEq(lapian::online::CacheKeyLabel(good), QStringLiteral("BV1BMnv6jE5S/123456/64-7"),
          "key-label");

  QTemporaryDir temp;
  CacheStore store(temp.path() + QStringLiteral("/cache"), 300 * kMb);
  const QString expected_dir = QDir(QDir(QDir(QDir(temp.path() + QStringLiteral("/cache"))
                                                  .filePath(good.bvid))
                                             .filePath(QStringLiteral("123456")))
                                        .filePath(QStringLiteral("64-7")))
                                   .absolutePath();
  CheckEq(QDir::fromNativeSeparators(store.EntryDir(good)),
          QDir::fromNativeSeparators(expected_dir), "entry-dir-layout");
  CheckEq(QDir::fromNativeSeparators(store.PartPath(good)),
          QDir::fromNativeSeparators(
              QDir(store.EntryDir(good)).filePath(QStringLiteral("data.part"))),
          "entry-part-path");
  CheckEq(QDir::fromNativeSeparators(store.Mp4Path(good)),
          QDir::fromNativeSeparators(
              QDir(store.EntryDir(good)).filePath(QStringLiteral("data.mp4"))),
          "entry-mp4-path");
  Check(store.EntryDir(CacheKey{QStringLiteral("../evil"), 1, 1, 1}).isEmpty(),
        "entry-dir-invalid-empty");

  QString error;
  Check(!store.Reserve(CacheKey{QStringLiteral("../evil"), 1, 1, 1}, 1024, &error),
        "reserve-rejects-invalid-key");
  Check(!error.isEmpty(), "reserve-invalid-key-error-readable");
  Check(!QFileInfo::exists(temp.path() + QStringLiteral("/cache/../evil")),
        "reserve-invalid-key-no-traversal");
}

// ---------------------------------------------------------------- 生命周期

void TestLifecycle() {
  QTemporaryDir temp;
  const QString root = temp.path() + QStringLiteral("/cache");
  CacheStore store(root, 300 * kMb);
  const CacheKey key{QStringLiteral("BV1BMnv6jE5S"), 1001, 64, 7};

  QString error;
  Check(!store.Load(key).has_value(), "load-missing-nullopt");
  Check(store.UsageBytes() == 0, "usage-missing-zero");

  Check(store.Reserve(key, 10 * kMb, &error), "reserve-ok");
  Check(QFileInfo::exists(store.EntryDir(key)), "reserve-creates-entry-dir");
  Check(QFileInfo::exists(QDir(store.EntryDir(key)).filePath(QStringLiteral("meta.json"))),
        "reserve-creates-meta");
  const QFileInfo part_info(store.PartPath(key));
  Check(part_info.exists(), "reserve-creates-part");
  CheckEqI64(part_info.size(), 0, "reserve-part-empty");
  CheckEqI64(store.UsageBytes(), MetaBytes(store, key), "reserve-usage-is-meta-bytes");

  std::optional<CacheEntry> entry = store.Load(key, &error);
  Check(entry.has_value(), "load-after-reserve");
  if (entry.has_value()) {
    Check(!entry->complete, "load-incomplete");
    Check(!entry->broken, "load-not-broken");
    CheckEqI64(entry->total_bytes, 10 * kMb, "load-total-bytes");
    CheckEqI64(entry->downloaded_bytes, 0, "load-downloaded-zero");
    CheckEq(entry->data_path, store.PartPath(key), "load-data-path-part");
    Check(entry->created_at.isValid(), "load-created-at");
    Check(entry->last_access_at.isValid(), "load-last-access-at");
  }

  // 幂等 Reserve：不新建、不截断已有 .part。
  Check(store.Reserve(key, 10 * kMb, &error), "reserve-idempotent");
  Check(QFileInfo(store.PartPath(key)).size() == 0, "reserve-idempotent-keeps-part");

  // 元数据 roundtrip。
  CacheMetadata metadata;
  metadata.title = QStringLiteral("中文标题 🎬");
  metadata.owner = QStringLiteral("UP 主");
  metadata.duration_ms = 111000;
  metadata.width = 1920;
  metadata.height = 1080;
  metadata.fps = 29.412;
  metadata.source_fps_hint = QStringLiteral("60fps 源");
  Check(store.SetMetadata(key, metadata, &error), "set-metadata-ok");
  entry = store.Load(key, &error);
  Check(entry.has_value(), "load-after-metadata");
  if (entry.has_value()) {
    CheckEq(entry->title, metadata.title, "metadata-title-unicode");
    CheckEq(entry->owner, metadata.owner, "metadata-owner");
    CheckEqI64(entry->duration_ms, metadata.duration_ms, "metadata-duration");
    Check(entry->width == 1920 && entry->height == 1080, "metadata-size");
    Check(std::abs(entry->fps - 29.412) < 1e-9, "metadata-fps");
    CheckEq(entry->source_fps_hint, metadata.source_fps_hint, "metadata-fps-hint");
  }
  const CacheKey missing_key{QStringLiteral("BV1BMnv6jE5S"), 9, 64, 7};
  Check(!store.SetMetadata(missing_key, metadata, &error),
        "set-metadata-missing-entry-fails");
  CheckEqI64(store.UsageBytes(), part_info.size() + MetaBytes(store, key),
             "usage-meta-plus-data");

  // 总字节变化：需要重建，不静默拼接。
  Check(!store.Reserve(key, 20 * kMb, &error), "reserve-total-mismatch-fails");
  Check(error.contains(QStringLiteral("不一致")), "reserve-total-mismatch-readable");

  // OpenPart 返回路径与已有长度。
  QString part_path;
  qint64 existing = -1;
  Check(store.OpenPart(key, &part_path, &existing, &error), "open-part-ok");
  CheckEq(QDir::fromNativeSeparators(part_path),
          QDir::fromNativeSeparators(store.PartPath(key)), "open-part-path");
  CheckEqI64(existing, 0, "open-part-existing-zero");
  CheckEqI64(store.UsageBytes(), MetaBytes(store, key), "usage-after-open-part");
}

// ---------------------------------------------------------------- 进度与完成

void TestProgressAndComplete() {
  QTemporaryDir temp;
  const QString root = temp.path() + QStringLiteral("/cache");
  CacheStore store(root, 300 * kMb);
  const CacheKey key{QStringLiteral("BV1mA411m7pS"), 2002, 80, 12};
  QString error;

  Check(store.Reserve(key, 8 * kMb, &error), "complete-reserve");
  QString part_path;
  qint64 existing = 0;
  Check(store.OpenPart(key, &part_path, &existing, &error), "complete-open-part");
  const QString meta_path =
      QDir(store.EntryDir(key)).filePath(QStringLiteral("meta.json"));

  // 第一次 force 写 3MB（同时建立节流时间戳）。
  Check(ResizeFile(part_path, 3 * kMb), "complete-grow-part-3mb");
  Check(store.UpdateProgress(key, 3 * kMb, true), "progress-forced-3mb-ok");
  QJsonObject meta = QJsonDocument::fromJson(ReadAll(meta_path)).object();
  CheckEqI64(static_cast<qint64>(
                 meta.value(QStringLiteral("downloaded_bytes")).toDouble()),
             3 * kMb, "progress-forced-3mb-written");

  // 1s 内的非 force 写被节流：文件已 4MB，但 meta 仍是 3MB。
  Check(ResizeFile(part_path, 4 * kMb), "complete-grow-part-4mb");
  Check(store.UpdateProgress(key, 4 * kMb, false), "progress-throttled-ok");
  meta = QJsonDocument::fromJson(ReadAll(meta_path)).object();
  CheckEqI64(static_cast<qint64>(
                 meta.value(QStringLiteral("downloaded_bytes")).toDouble()),
             3 * kMb, "progress-throttled-not-written");

  // force 立即写；随后更小的值不回退。
  Check(store.UpdateProgress(key, 4 * kMb, true), "progress-forced-4mb-ok");
  meta = QJsonDocument::fromJson(ReadAll(meta_path)).object();
  CheckEqI64(static_cast<qint64>(
                 meta.value(QStringLiteral("downloaded_bytes")).toDouble()),
             4 * kMb, "progress-forced-4mb-written");
  Check(store.UpdateProgress(key, 1 * kMb, true), "progress-smaller-ok");
  meta = QJsonDocument::fromJson(ReadAll(meta_path)).object();
  CheckEqI64(static_cast<qint64>(
                 meta.value(QStringLiteral("downloaded_bytes")).toDouble()),
             4 * kMb, "progress-monotonic");

  // 不完整时不能完成：保留 .part，给可读错误。
  Check(!store.MarkComplete(key, &error), "complete-rejects-short-part");
  Check(error.contains(QStringLiteral("不完整")), "complete-short-part-readable");
  Check(QFileInfo::exists(store.PartPath(key)), "complete-short-part-kept");
  Check(!store.OpenComplete(key, nullptr, &error), "open-complete-incomplete-fails");

  // 写满后完成：.part → .mp4 原子改名 + complete=true。
  Check(ResizeFile(part_path, 8 * kMb), "complete-grow-to-total");
  Check(store.MarkComplete(key, &error), "complete-mark-ok");
  Check(!QFileInfo::exists(store.PartPath(key)), "complete-part-gone");
  Check(QFileInfo::exists(store.Mp4Path(key)), "complete-mp4-exists");
  CheckEqI64(QFileInfo(store.Mp4Path(key)).size(), 8 * kMb, "complete-mp4-size");

  std::optional<CacheEntry> entry = store.Load(key, &error);
  Check(entry.has_value() && entry->complete, "complete-load-complete");
  if (entry.has_value()) {
    CheckEqI64(entry->downloaded_bytes, 8 * kMb, "complete-downloaded-total");
    CheckEq(entry->data_path, store.Mp4Path(key), "complete-data-path-mp4");
  }
  QString mp4_path;
  Check(store.OpenComplete(key, &mp4_path, &error), "open-complete-ok");
  CheckEq(QDir::fromNativeSeparators(mp4_path),
          QDir::fromNativeSeparators(store.Mp4Path(key)), "open-complete-path");
  Check(store.MarkComplete(key, &error), "complete-idempotent");
  Check(!store.OpenPart(key, &part_path, &existing, &error),
        "open-part-on-complete-fails");

  // 已完整缓存时 Reserve 直接命中，不重建 .part。
  Check(store.Reserve(key, 8 * kMb, &error), "reserve-complete-hit");
  Check(!QFileInfo::exists(store.PartPath(key)), "reserve-complete-no-part");
  CheckEqI64(QFileInfo(store.Mp4Path(key)).size(), 8 * kMb, "reserve-complete-keeps-mp4");
  Check(!store.UpdateProgress(key, 1, true), "progress-on-complete-rejected");

  // 缺条目：OpenPart/UpdateProgress/MarkComplete 都返回可读失败，不崩。
  const CacheKey missing{QStringLiteral("BV1zz411c7mD"), 7, 64, 7};
  Check(!store.OpenPart(missing, &part_path, &existing, &error),
        "open-part-missing-fails");
  Check(!store.UpdateProgress(missing, 1, true), "progress-missing-fails");
  Check(!store.MarkComplete(missing, &error), "complete-missing-fails");
  Check(!store.OpenComplete(missing, &mp4_path, &error), "open-complete-missing-fails");
}

// ---------------------------------------------------------------- 续传

void TestResume() {
  QTemporaryDir temp;
  const QString root = temp.path() + QStringLiteral("/cache");
  const CacheKey key{QStringLiteral("BV1GJ411x7h7"), 3003, 64, 7};
  QString error;

  {
    CacheStore store(root, 300 * kMb);
    Check(store.Reserve(key, 20 * kMb, &error), "resume-reserve");
    Check(CreateSizedFile(store.PartPath(key), 5 * kMb), "resume-write-5mb");
    QString part_path;
    qint64 existing = 0;
    Check(store.OpenPart(key, &part_path, &existing, &error), "resume-open-part");
    CheckEqI64(existing, 5 * kMb, "resume-existing-5mb");
  }

  // 进程重启（新 CacheStore，同一根）。
  CacheStore reopened(root, 300 * kMb);
  QString part_path;
  qint64 existing = 0;
  Check(reopened.OpenPart(key, &part_path, &existing, &error), "resume-reopen-open-part");
  CheckEqI64(existing, 5 * kMb, "resume-reopen-length");
  CheckEqI64(QFileInfo(reopened.PartPath(key)).size(), 5 * kMb,
             "resume-no-double-append");

  // meta 的 downloaded_bytes 与文件长度不一致时，按文件长度校正并回写。
  Check(PatchMeta(reopened, key, QJsonObject{{QStringLiteral("downloaded_bytes"), 1}}),
        "resume-corrupt-downloaded");
  std::optional<CacheEntry> entry = reopened.Load(key, &error);
  Check(entry.has_value() && entry->downloaded_bytes == 5 * kMb,
        "resume-load-corrects-length");
  const QJsonObject repaired = QJsonDocument::fromJson(
                                   ReadAll(QDir(reopened.EntryDir(key))
                                              .filePath(QStringLiteral("meta.json"))))
                                   .object();
  CheckEqI64(
      static_cast<qint64>(repaired.value(QStringLiteral("downloaded_bytes")).toDouble()),
      5 * kMb, "resume-meta-rewritten");

  // 续传 Reserve 不截断 .part；再续到 8MB。
  Check(reopened.Reserve(key, 20 * kMb, &error), "resume-reserve-again");
  CheckEqI64(QFileInfo(reopened.PartPath(key)).size(), 5 * kMb,
             "resume-reserve-keeps-part");
  Check(ResizeFile(reopened.PartPath(key), 8 * kMb), "resume-grow-to-8mb");
  Check(reopened.OpenPart(key, &part_path, &existing, &error), "resume-open-8mb");
  CheckEqI64(existing, 8 * kMb, "resume-existing-8mb");

  // .part 超过 total：损坏，不当可播放。
  Check(ResizeFile(reopened.PartPath(key), 21 * kMb), "resume-oversize-part");
  entry = reopened.Load(key, &error);
  Check(entry.has_value() && entry->broken, "resume-oversize-broken");
  Check(error.contains(QStringLiteral("超过总字节")), "resume-oversize-readable");
}

// ---------------------------------------------------------------- 损坏/重建

void TestCorruption() {
  QTemporaryDir temp;
  const QString root = temp.path() + QStringLiteral("/cache");
  CacheStore store(root, 300 * kMb);
  const CacheKey a{QStringLiteral("BV1BMnv6jE5S"), 101, 64, 7};
  const CacheKey b{QStringLiteral("BV1mA411m7pS"), 102, 80, 12};
  const CacheKey c{QStringLiteral("BV1GJ411x7h7"), 103, 32, 7};
  QString error;

  Check(CreateCompleteEntry(&store, a, 1 * kMb), "corrupt-setup-a");
  Check(CreateCompleteEntry(&store, b, 1 * kMb), "corrupt-setup-b");
  Check(CreateCompleteEntry(&store, c, 1 * kMb), "corrupt-setup-c");
  Check(CreateIncompleteEntry(&store, CacheKey{QStringLiteral("BV1Q541167Qh"), 104, 64, 7},
                              1 * kMb, 1024),
        "corrupt-setup-incomplete");

  // 缓存根外的“在线记录/截图”：重建/清空都不能删。
  const QString records_path =
      temp.path() + QStringLiteral("/online/BV1BMnv6jE5S-101.lapian.json");
  const QString shot_path = temp.path() + QStringLiteral("/pictures/shot.png");
  Check(WriteAll(records_path, QByteArrayLiteral("{\"marks\":[]}")),
        "corrupt-records-sentinel");
  Check(WriteAll(shot_path, QByteArrayLiteral("png")), "corrupt-shot-sentinel");

  auto scan_counts = [&](int* entry_count, int* broken_count) {
    QVector<CacheEntry> list;
    QVector<CacheEntry> bad;
    store.Scan(&list, &bad, nullptr);
    *entry_count = list.size();
    *broken_count = bad.size();
  };

  // (a) 非法 JSON。
  Check(WriteAll(QDir(store.EntryDir(a)).filePath(QStringLiteral("meta.json")),
                 QByteArrayLiteral("{oops")),
        "corrupt-a-write-bad-json");
  const std::optional<CacheEntry> broken_a = store.Load(a, &error);
  Check(broken_a.has_value() && broken_a->broken, "corrupt-a-load-broken");
  Check(error.contains(QStringLiteral("JSON")), "corrupt-a-reason-json");
  Check(!store.OpenComplete(a, nullptr, &error), "corrupt-a-not-playable");
  Check(!store.Reserve(a, 1 * kMb, &error), "corrupt-a-reserve-needs-rebuild");

  int entries = 0;
  int broken = 0;
  scan_counts(&entries, &broken);
  CheckEqI64(entries, 3, "corrupt-scan-entries-with-one-broken");
  CheckEqI64(broken, 1, "corrupt-scan-broken-count");

  Check(store.Rebuild(a, &error), "corrupt-a-rebuild");
  Check(!QFileInfo::exists(store.EntryDir(a)), "corrupt-a-rebuild-removes-dir");
  Check(!store.Load(a, &error).has_value(), "corrupt-a-load-after-rebuild");
  Check(QFileInfo::exists(records_path), "corrupt-a-records-kept");
  Check(QFileInfo::exists(shot_path), "corrupt-a-shot-kept");

  // (b) complete=true 但 data.mp4 被截断。
  Check(ResizeFile(store.Mp4Path(b), 512 * kKb), "corrupt-b-truncate");
  const std::optional<CacheEntry> broken_b = store.Load(b, &error);
  Check(broken_b.has_value() && broken_b->broken, "corrupt-b-broken");
  Check(error.contains(QStringLiteral("大小不符")), "corrupt-b-reason-size");
  Check(!store.OpenComplete(b, nullptr, &error), "corrupt-b-not-playable");
  Check(store.Rebuild(b, &error), "corrupt-b-rebuild");

  // (c) complete=true 但缺 data.mp4。
  Check(QFile::remove(store.Mp4Path(c)), "corrupt-c-remove-mp4");
  const std::optional<CacheEntry> broken_c = store.Load(c, &error);
  Check(broken_c.has_value() && broken_c->broken, "corrupt-c-broken");
  Check(error.contains(QStringLiteral("data.mp4")), "corrupt-c-reason-missing-mp4");
  Check(store.Rebuild(c, &error), "corrupt-c-rebuild");

  // (d) meta 与目录键不一致（串片/复制错）。
  const CacheKey d{QStringLiteral("BV1Q541167Qh"), 105, 64, 7};
  Check(CreateCompleteEntry(&store, d, 1 * kMb), "corrupt-setup-d");
  Check(PatchMeta(store, d,
                  QJsonObject{{QStringLiteral("bvid"), QStringLiteral("BV1mA411m7pS")}}),
        "corrupt-d-patch-bvid");
  const std::optional<CacheEntry> broken_d = store.Load(d, &error);
  Check(broken_d.has_value() && broken_d->broken, "corrupt-d-broken");
  Check(error.contains(QStringLiteral("不一致")), "corrupt-d-reason-mismatch");
  Check(store.Rebuild(d, &error), "corrupt-d-rebuild");

  // (e) 不支持的 schema。
  const CacheKey e{QStringLiteral("BV1Q541167Qh"), 106, 64, 7};
  Check(CreateCompleteEntry(&store, e, 1 * kMb), "corrupt-setup-e");
  Check(PatchMeta(store, e,
                  QJsonObject{{QStringLiteral("schema"),
                               QStringLiteral("lapian-cache-2")}}),
        "corrupt-e-patch-schema");
  const std::optional<CacheEntry> broken_e = store.Load(e, &error);
  Check(broken_e.has_value() && broken_e->broken, "corrupt-e-broken");
  Check(error.contains(QStringLiteral("schema")), "corrupt-e-reason-schema");
  Check(store.Rebuild(e, &error), "corrupt-e-rebuild");

  // (f) 缺 meta.json（只有 data.mp4）：孤儿，不当可播放；Rebuild 清掉。
  const CacheKey f{QStringLiteral("BV1Q541167Qh"), 107, 64, 7};
  Check(CreateCompleteEntry(&store, f, 1 * kMb), "corrupt-setup-f");
  Check(QFile::remove(QDir(store.EntryDir(f)).filePath(QStringLiteral("meta.json"))),
        "corrupt-f-remove-meta");
  const std::optional<CacheEntry> broken_f = store.Load(f, &error);
  Check(broken_f.has_value() && broken_f->broken, "corrupt-f-broken");
  Check(error.contains(QStringLiteral("meta.json")), "corrupt-f-reason-missing-meta");
  Check(!store.OpenComplete(f, nullptr, &error), "corrupt-f-not-playable");
  Check(store.Rebuild(f, &error), "corrupt-f-rebuild");
  Check(!QFileInfo::exists(store.EntryDir(f)), "corrupt-f-rebuild-removes-dir");

  // (g) meta.json 过大：不读进内存。
  const CacheKey g{QStringLiteral("BV1Q541167Qh"), 108, 64, 7};
  Check(CreateCompleteEntry(&store, g, 1 * kMb), "corrupt-setup-g");
  Check(WriteAll(QDir(store.EntryDir(g)).filePath(QStringLiteral("meta.json")),
                 QByteArray(static_cast<int>(CacheStore::kMaxMetaBytes + 1), 'x')),
        "corrupt-g-oversize-meta");
  const std::optional<CacheEntry> broken_g = store.Load(g, &error);
  Check(broken_g.has_value() && broken_g->broken, "corrupt-g-broken");
  Check(error.contains(QStringLiteral("过大")), "corrupt-g-reason-oversize");
  Check(store.Rebuild(g, &error), "corrupt-g-rebuild");

  // (h) 崩溃在改名之后、meta 落盘之前：按 data.mp4 修正为完成。
  const CacheKey h{QStringLiteral("BV1Q541167Qh"), 109, 64, 7};
  Check(store.Reserve(h, 1 * kMb, &error), "corrupt-h-reserve");
  Check(CreateSizedFile(store.Mp4Path(h), 1 * kMb), "corrupt-h-make-mp4");
  Check(QFile::remove(store.PartPath(h)), "corrupt-h-remove-empty-part");
  const std::optional<CacheEntry> healed = store.Load(h, &error);
  Check(healed.has_value() && healed->complete, "corrupt-h-healed-complete");
  Check(store.OpenComplete(h, nullptr, &error), "corrupt-h-open-complete");
  const QJsonObject healed_meta =
      QJsonDocument::fromJson(
          ReadAll(QDir(store.EntryDir(h)).filePath(QStringLiteral("meta.json"))))
          .object();
  Check(healed_meta.value(QStringLiteral("complete")).toBool(),
        "corrupt-h-meta-rewritten-complete");

  // 非本工具布局：扫描忽略、不删除。
  Check(CreateForeignPaths(root), "corrupt-foreign-paths");
  scan_counts(&entries, &broken);
  Check(entries >= 2, "corrupt-scan-ignores-foreign-layout");
  QVector<CacheEntry> entries_list;
  QVector<CacheEntry> broken_list;
  Check(store.Scan(&entries_list, &broken_list, nullptr), "scan-returns-true");
  const QStringList entry_labels = Labels(entries_list);
  Check(!entry_labels.contains(QStringLiteral("notabvid/1/64-7")),
        "scan-skips-foreign-bvid");
  Check(QFileInfo::exists(root + QStringLiteral("/readme.txt")), "scan-foreign-file-kept");
}

// ---------------------------------------------------------------- 300MB LRU

void TestLru300Mb() {
  QTemporaryDir temp;
  const QString root = temp.path() + QStringLiteral("/cache");
  const qint64 quota = 300 * kMb;
  CacheStore store(root, quota);
  QString error;

  const CacheKey e1{Bvid(QLatin1Char('a')), 1, 64, 7};
  const CacheKey e2{Bvid(QLatin1Char('b')), 2, 64, 7};
  const CacheKey e3{Bvid(QLatin1Char('c')), 3, 64, 7};
  const CacheKey current{Bvid(QLatin1Char('d')), 4, 64, 7};
  const CacheKey e5{Bvid(QLatin1Char('e')), 5, 64, 7};

  Check(CreateRawCompleteEntry(store, e1, 90 * kMb, AgoSecs(4 * 3600)), "lru-setup-e1");
  Check(CreateRawCompleteEntry(store, e2, 90 * kMb, AgoSecs(3 * 3600)), "lru-setup-e2");
  Check(CreateRawCompleteEntry(store, e3, 90 * kMb, AgoSecs(2 * 3600)), "lru-setup-e3");
  const qint64 usage_before = store.UsageBytes();
  CheckGe(usage_before, 270 * kMb, "lru-usage-before-reserve");
  CheckLe(usage_before, quota, "lru-usage-under-300mb-before");
  std::printf("  lru300_usage_before=%lld quota=%lld\n",
              static_cast<long long>(usage_before),
              static_cast<long long>(quota));

  // 新条目 60MB：270 + 60 = 330 > 300 → 淘汰最旧的 e1（90MB）后 ≈240 ≤ 300。
  Check(store.Reserve(current, 60 * kMb, &error), "lru-reserve-evicts");
  std::printf("  lru300_usage_after=%lld\n",
              static_cast<long long>(store.UsageBytes()));
  CheckLe(store.UsageBytes(), quota, "lru-usage-under-quota-after");
  Check(!store.Load(e1).has_value(), "lru-evicts-oldest");
  Check(store.Load(e2).has_value(), "lru-keeps-second");
  Check(store.Load(e3).has_value(), "lru-keeps-third");
  Check(store.Load(current).has_value(), "lru-keeps-current");
  Check(!QFileInfo::exists(store.EntryDir(e1)), "lru-evicted-dir-removed");

  // 第二段：补满 current 并再加 80MB，再淘汰当时最旧的 e2。
  Check(CreateSizedFile(store.PartPath(current), 60 * kMb), "lru-current-grow");
  Check(store.MarkComplete(current, &error), "lru-current-complete");
  Check(store.Reserve(e5, 80 * kMb, &error), "lru-second-reserve");
  CheckLe(store.UsageBytes(), quota, "lru-second-usage-under-quota");
  Check(!store.Load(e2).has_value(), "lru-second-evicts-e2");
  Check(store.Load(e3).has_value(), "lru-second-keeps-e3");
  Check(store.Load(current).has_value(), "lru-second-keeps-current");
  Check(store.Load(e5).has_value(), "lru-second-keeps-new");

  // 最旧优先：e3 比 current 旧，再加 80MB 时先淘汰 e3。
  Check(ResizeFile(store.PartPath(e5), 80 * kMb), "lru-e5-grow");
  Check(store.MarkComplete(e5, &error), "lru-e5-complete");
  const CacheKey f6{Bvid(QLatin1Char('f')), 6, 64, 7};
  Check(store.Reserve(f6, 80 * kMb, &error), "lru-third-reserve");
  Check(!store.Load(e3).has_value(), "lru-third-evicts-oldest-remaining");
  Check(store.Load(current).has_value(), "lru-third-keeps-current");
  Check(store.Load(f6).has_value(), "lru-third-keeps-new");
  CheckLe(store.UsageBytes(), quota, "lru-third-usage-under-quota");

  // EvictToQuota 幂等：已在配额内不动。
  const qint64 usage_now = store.UsageBytes();
  Check(store.EvictToQuota(current, &error), "lru-evict-to-quota-noop");
  CheckEqI64(store.UsageBytes(), usage_now, "lru-evict-to-quota-keeps-usage");
}

// ---------------------------------------------------------------- 当前条目不淘汰

void TestCurrentProtected() {
  QTemporaryDir temp;
  const QString root = temp.path() + QStringLiteral("/cache");
  const CacheKey current{QStringLiteral("BV1BMnv6jE5S"), 77, 64, 7};
  const CacheKey other{QStringLiteral("BV1mA411m7pS"), 78, 64, 7};
  QString error;

  CacheStore store(root, 40 * kMb);
  Check(CreateCompleteEntry(&store, current, 5 * kMb), "protect-setup-current");
  Check(store.SetQuotaBytes(1 * kMb, &error), "protect-set-quota-1mb");
  CheckEqI64(store.quota(), 1 * kMb, "protect-quota-value");

  // 只有当前条目、且自身超额：无可淘汰 → 失败但不删当前条目。
  Check(!store.EvictToQuota(current, &error), "protect-evict-fails-current-exceeds");
  Check(store.Load(current).has_value(), "protect-current-kept");
  Check(error.contains(QStringLiteral("配额不足")), "protect-error-readable");

  // 换一个 current：原当前条目可以被淘汰（先放回大配额创建新条目）。
  Check(store.SetQuotaBytes(40 * kMb, &error), "protect-restore-quota");
  Check(CreateCompleteEntry(&store, other, 5 * kMb), "protect-setup-other");
  Check(store.SetQuotaBytes(1 * kMb, &error), "protect-set-quota-again");
  Check(!store.EvictToQuota(other, &error), "protect-evict-other-still-over");
  Check(store.Load(other).has_value(), "protect-other-kept");
  Check(!store.Load(current).has_value(), "protect-old-current-evicted");
  CheckLe(store.UsageBytes(), 6 * kMb, "protect-usage-now-only-other");

  // 配额过小：Reserve 直接拒绝（不留下半条记录）。
  CacheStore tiny(temp.path() + QStringLiteral("/tiny"), 1 * kMb);
  const CacheKey big{QStringLiteral("BV1GJ411x7h7"), 1, 64, 7};
  Check(!tiny.Reserve(big, 5 * kMb, &error), "protect-reserve-over-quota-fails");
  Check(error.contains(QStringLiteral("配额")), "protect-reserve-over-quota-readable");
  Check(!QFileInfo::exists(tiny.EntryDir(big)), "protect-reserve-over-quota-no-entry");

  // 配额参数校验。
  Check(!store.SetQuotaBytes(0, &error), "quota-zero-rejected");
  Check(!store.SetQuotaBytes(-1, &error), "quota-negative-rejected");
  Check(!store.SetQuotaBytes(CacheStore::kMinQuotaBytes - 1, &error),
        "quota-too-small-rejected");
  CheckEqI64(store.quota(), 1 * kMb, "quota-unchanged-after-reject");
}

// ---------------------------------------------------------------- 读句柄/待淘汰

void TestEvictBusy() {
  QTemporaryDir temp;
  const QString root = temp.path() + QStringLiteral("/cache");
  const CacheKey locked{QStringLiteral("BV1BMnv6jE5S"), 55, 64, 7};
  const CacheKey current{QStringLiteral("BV1mA411m7pS"), 56, 64, 7};
  QString error;

  CacheStore store(root, 40 * kMb);
  Check(CreateCompleteEntry(&store, locked, 10 * kMb), "busy-setup-locked");
  Check(CreateCompleteEntry(&store, current, 10 * kMb), "busy-setup-current");
  Check(store.SetQuotaBytes(12 * kMb, &error), "busy-set-quota");

  // 独占读句柄（不带 FILE_SHARE_DELETE）模拟解码器正在读。
  const QString mp4 = store.Mp4Path(locked);
  const std::wstring native = mp4.toStdWString();
  HANDLE reader = CreateFileW(native.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  Check(reader != INVALID_HANDLE_VALUE, "busy-open-reader-handle");
  Check(!store.EvictToQuota(current, &error), "busy-evict-reports-busy");
  Check(error.contains(QStringLiteral("正被读取")), "busy-error-readable");
  Check(QFileInfo::exists(mp4), "busy-data-kept");
  const std::optional<CacheEntry> pending = store.Load(locked);
  Check(pending.has_value() && pending->evict_pending, "busy-marked-pending");

  // 有句柄时 CleanupOrphans 删不掉（下一轮再试），但不会崩。
  qint64 removed = 0;
  Check(store.CleanupOrphans(current, &error, CleanupOptions{}, &removed),
        "busy-cleanup-with-handle");
  Check(QFileInfo::exists(mp4), "busy-cleanup-keeps-open-file");

  CloseHandle(reader);
  Check(store.CleanupOrphans(current, &error, CleanupOptions{}, &removed),
        "busy-cleanup-after-close");
  Check(!QFileInfo::exists(mp4), "busy-cleanup-removes-pending");
  CheckLe(store.UsageBytes(), 12 * kMb, "busy-usage-under-quota");
  Check(store.Load(current).has_value(), "busy-current-kept");
}

// ---------------------------------------------------------------- 磁盘空间

void TestDiskSpace() {
  QTemporaryDir temp;
  const QString root = temp.path() + QStringLiteral("/cache");
  CacheStore store(root, 300 * kMb);
  const CacheKey cached{QStringLiteral("BV1BMnv6jE5S"), 21, 64, 7};
  const CacheKey huge{QStringLiteral("BV1mA411m7pS"), 22, 64, 7};
  QString error;

  Check(CreateCompleteEntry(&store, cached, 1 * kMb), "space-setup-cached");
  CheckGe(CacheStore::FreeDiskBytes(root, &error), 0, "space-free-bytes-read");
  Check(CacheStore::FreeDiskBytes(temp.path() + QStringLiteral("/not/created/yet"),
                                  &error) >= 0,
        "space-free-bytes-uses-ancestor");
  Check(CacheStore::FreeDiskBytes(QString(), &error) < 0, "space-free-bytes-empty-fails");

  // 2 PB 在任何测试机上都不可能满足：走的是同一预检路径（不是真写满磁盘）。
  QElapsedTimer timer;
  timer.start();
  const bool reserved = store.Reserve(huge, 2'000'000'000'000'000LL, &error);
  const double elapsed_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  Check(!reserved, "space-reserve-huge-fails");
  Check(error.startsWith(QStringLiteral("磁盘空间不足：需要")), "space-error-prefix");
  Check(error.contains(QStringLiteral("可用")), "space-error-has-available");
  Check(elapsed_ms <= 5000.0, "space-error-under-5s");
  std::printf("  space_error_ms=%.3f\n", elapsed_ms);
  Check(!QFileInfo::exists(store.EntryDir(huge)), "space-failure-no-entry-dir");
  Check(!QFileInfo::exists(store.PartPath(huge)), "space-failure-no-part");

  // 已缓存仍可播。
  QString mp4_path;
  Check(store.OpenComplete(cached, &mp4_path, &error), "space-cached-still-playable");
  Check(QFileInfo::exists(mp4_path), "space-cached-path-exists");
}

// ---------------------------------------------------------------- 20GB 记账

void TestAccounting20Gb() {
  QTemporaryDir temp;
  const QString root = temp.path() + QStringLiteral("/cache");
  const qint64 quota = 20 * kGb;
  CacheStore store(root, quota);
  QString error;

  const CacheKey a{Bvid(QLatin1Char('a')), 1, 80, 7};
  const CacheKey b{Bvid(QLatin1Char('b')), 2, 80, 7};
  const CacheKey c{Bvid(QLatin1Char('c')), 3, 80, 7};
  Check(CreateRawCompleteEntry(store, a, 5 * kGb, AgoSecs(3 * 24 * 3600)), "acct-a-5gb");
  Check(CreateRawCompleteEntry(store, b, 2 * kGb, AgoSecs(2 * 24 * 3600)), "acct-b-2gb");
  Check(CreateRawCompleteEntry(store, c, 14 * kGb, AgoSecs(1 * 24 * 3600)), "acct-c-14gb");

  const qint64 meta_bytes =
      MetaBytes(store, a) + MetaBytes(store, b) + MetaBytes(store, c);
  const qint64 usage_before = store.UsageBytes();
  CheckEqI64(usage_before, 21 * kGb + meta_bytes, "acct-usage-sums-logical-sizes");
  Check(usage_before > quota, "acct-over-20gb");

  // 配额 20GB：先淘汰最旧的 5GB → 16GB（含 meta）≤ 20GB。
  const qint64 a_usage = 5 * kGb + MetaBytes(store, a);
  Check(store.EvictToQuota(c, &error), "acct-evict-ok");
  CheckLe(store.UsageBytes(), quota, "acct-usage-under-20gb");
  CheckEqI64(store.UsageBytes(), usage_before - a_usage, "acct-eviction-debits-exactly");
  Check(!store.Load(a).has_value(), "acct-oldest-evicted");
  Check(store.Load(b).has_value() && store.Load(c).has_value(), "acct-others-kept");

  // 再加 10GB：25GB → 淘汰 2GB（24）→ 再淘汰 14GB（10）→ ≤ 20GB。
  const CacheKey d{Bvid(QLatin1Char('d')), 4, 80, 7};
  Check(CreateRawCompleteEntry(store, d, 10 * kGb, QDateTime::currentDateTimeUtc()),
        "acct-d-10gb");
  Check(store.UsageBytes() > quota, "acct-over-again");
  Check(store.EvictToQuota(d, &error), "acct-evict-again-ok");
  CheckLe(store.UsageBytes(), quota, "acct-usage-under-20gb-again");
  Check(!store.Load(b).has_value(), "acct-evicts-b");
  Check(!store.Load(c).has_value(), "acct-evicts-c");
  Check(store.Load(d).has_value(), "acct-keeps-current");
  CheckEqI64(store.UsageBytes(), 10 * kGb + MetaBytes(store, d),
             "acct-final-usage-exact");
  std::printf("  quota_20gb=%lld usage_after=%lld\n", static_cast<long long>(quota),
              static_cast<long long>(store.UsageBytes()));
}

// ---------------------------------------------------------------- 清空

void TestClearAll() {
  QTemporaryDir temp;
  const QString root = temp.path() + QStringLiteral("/cache");
  CacheStore store(root, 300 * kMb);
  QString error;

  const CacheKey a{QStringLiteral("BV1BMnv6jE5S"), 31, 64, 7};
  const CacheKey b{QStringLiteral("BV1mA411m7pS"), 32, 80, 12};
  const CacheKey broken_key{QStringLiteral("BV1GJ411x7h7"), 33, 64, 7};
  const CacheKey incomplete{QStringLiteral("BV1Q541167Qh"), 34, 64, 7};
  Check(CreateCompleteEntry(&store, a, 2 * kMb), "clear-setup-a");
  Check(CreateCompleteEntry(&store, b, 2 * kMb), "clear-setup-b");
  Check(CreateCompleteEntry(&store, broken_key, 2 * kMb), "clear-setup-broken");
  Check(CreateIncompleteEntry(&store, incomplete, 2 * kMb, 1 * kMb),
        "clear-setup-incomplete");
  Check(WriteAll(QDir(store.EntryDir(broken_key)).filePath(QStringLiteral("meta.json")),
                 QByteArrayLiteral("{bad")),
        "clear-break-meta");
  Check(CreateForeignPaths(root), "clear-foreign-paths");

  const QString record_file = temp.path() +
                              QStringLiteral("/online/BV1BMnv6jE5S-31.lapian.json");
  const QString shot_file =
      temp.path() + QStringLiteral("/pictures/BV1BMnv6jE5S_f100.png");
  Check(WriteAll(record_file, QByteArrayLiteral("{\"schema\":\"lapian-online-1\"}")),
        "clear-record-sentinel");
  Check(WriteAll(shot_file, QByteArrayLiteral("png-bytes")), "clear-shot-sentinel");
  const QByteArray record_before = ReadAll(record_file);

  qint64 removed = 0;
  Check(store.ClearAll(&error, &removed), "clear-all-ok");
  CheckGe(removed, 7 * kMb, "clear-removed-bytes");
  CheckEqI64(store.UsageBytes(), 0, "clear-usage-zero");
  Check(QFileInfo::exists(root), "clear-root-kept");
  Check(!store.Load(a).has_value(), "clear-a-gone");
  Check(!store.Load(incomplete).has_value(), "clear-incomplete-gone");
  Check(!QFileInfo::exists(store.EntryDir(b)), "clear-entry-dir-gone");
  Check(QFileInfo::exists(record_file), "clear-records-kept");
  Check(ReadAll(record_file) == record_before, "clear-records-unchanged");
  Check(QFileInfo::exists(shot_file), "clear-shots-kept");
  Check(QFileInfo::exists(root + QStringLiteral("/readme.txt")), "clear-foreign-kept");

  QVector<CacheEntry> entries;
  QVector<CacheEntry> broken;
  Check(store.Scan(&entries, &broken, nullptr), "clear-scan-ok");
  Check(entries.isEmpty() && broken.isEmpty(), "clear-scan-empty");
  Check(store.ClearAll(&error, &removed), "clear-idempotent");
  CheckEqI64(removed, 0, "clear-idempotent-removed-zero");
}

// ---------------------------------------------------------------- 孤儿清理

void TestCleanupOrphans() {
  QTemporaryDir temp;
  const QString root = temp.path() + QStringLiteral("/cache");
  CacheStore store(root, 300 * kMb);
  QString error;

  const CacheKey current{QStringLiteral("BV1BMnv6jE5S"), 41, 64, 7};
  const CacheKey stale{QStringLiteral("BV1BMnv6jE5S"), 42, 64, 7};
  const CacheKey fresh{QStringLiteral("BV1BMnv6jE5S"), 43, 64, 7};
  const CacheKey old_complete{QStringLiteral("BV1BMnv6jE5S"), 44, 64, 7};
  const CacheKey orphan_part{QStringLiteral("BV1BMnv6jE5S"), 45, 64, 7};
  const CacheKey orphan_mp4{QStringLiteral("BV1BMnv6jE5S"), 46, 64, 7};

  Check(CreateIncompleteEntry(&store, current, 2 * kMb, 1024), "cleanup-current");
  Check(CreateIncompleteEntry(&store, stale, 2 * kMb, 1024), "cleanup-stale");
  Check(CreateIncompleteEntry(&store, fresh, 2 * kMb, 1024), "cleanup-fresh");
  Check(CreateCompleteEntry(&store, old_complete, 1 * kMb), "cleanup-old-complete");
  Check(PatchMeta(store, current,
                  QJsonObject{{QStringLiteral("last_access_at"),
                               IsoText(AgoSecs(48 * 3600))}}),
        "cleanup-current-old-timestamp");
  Check(PatchMeta(store, stale,
                  QJsonObject{{QStringLiteral("last_access_at"),
                               IsoText(AgoSecs(25 * 3600))}}),
        "cleanup-stale-old-timestamp");
  Check(PatchMeta(store, fresh,
                  QJsonObject{{QStringLiteral("last_access_at"),
                               IsoText(AgoSecs(23 * 3600))}}),
        "cleanup-fresh-recent-timestamp");
  Check(PatchMeta(store, old_complete,
                  QJsonObject{{QStringLiteral("last_access_at"),
                               IsoText(AgoSecs(400LL * 24 * 3600))}}),
        "cleanup-complete-old-timestamp");

  // 无 meta 的孤儿数据（.part / .mp4）。
  Check(CreateSizedFile(store.PartPath(orphan_part), 4096),
        "cleanup-orphan-part-create");
  Check(CreateSizedFile(store.Mp4Path(orphan_mp4), 4096), "cleanup-orphan-mp4-create");
  Check(CreateForeignPaths(root), "cleanup-foreign-paths");

  // 关闭清理：什么都不动。
  qint64 removed = 0;
  CleanupOptions disabled;
  disabled.enabled = false;
  Check(store.CleanupOrphans(current, &error, disabled, &removed),
        "cleanup-disabled-ok");
  CheckEqI64(removed, 0, "cleanup-disabled-removed-zero");
  Check(QFileInfo::exists(store.PartPath(stale)), "cleanup-disabled-keeps-stale");

  // 开启清理：孤儿 + 25h 未访问未完成；保留当前/23h/完成的。
  Check(store.CleanupOrphans(current, &error, CleanupOptions{}, &removed),
        "cleanup-enabled-ok");
  CheckGe(removed, 0, "cleanup-removed-bytes");
  Check(QFileInfo::exists(store.PartPath(current)), "cleanup-keeps-current");
  Check(!QFileInfo::exists(store.EntryDir(stale)), "cleanup-removes-stale");
  Check(QFileInfo::exists(store.PartPath(fresh)), "cleanup-keeps-fresh");
  Check(QFileInfo::exists(store.Mp4Path(old_complete)), "cleanup-keeps-complete");
  Check(!QFileInfo::exists(store.EntryDir(orphan_part)), "cleanup-removes-orphan-part");
  Check(!QFileInfo::exists(store.EntryDir(orphan_mp4)), "cleanup-removes-orphan-mp4");

  // 自定义阈值：0 小时内未访问即清理。
  CleanupOptions aggressive;
  aggressive.incomplete_max_age_hours = 0;
  Check(store.CleanupOrphans(current, &error, aggressive, &removed),
        "cleanup-aggressive-ok");
  Check(!QFileInfo::exists(store.EntryDir(fresh)), "cleanup-aggressive-removes-fresh");
  Check(QFileInfo::exists(store.PartPath(current)), "cleanup-aggressive-keeps-current");
  Check(QFileInfo::exists(store.Mp4Path(old_complete)),
        "cleanup-aggressive-keeps-complete");
  Check(QFileInfo::exists(root + QStringLiteral("/readme.txt")),
        "cleanup-foreign-untouched");
}

// ---------------------------------------------------------------- 跨会话

void TestCrossSession() {
  QTemporaryDir temp;
  const QString settings_root = temp.path() + QStringLiteral("/settings");
  const QString cache_a = temp.path() + QStringLiteral("/cache-a");
  const QString cache_b = temp.path() + QStringLiteral("/cache-b");

  {
    SettingsStore settings(settings_root);
    AppSettings values;
    values.volume = 66;
    values.waveform_visible = false;
    values.cache_root = cache_a;
    values.cache_quota_gb = 7;
    Check(settings.Save(values), "cross-save-settings");
  }

  // 新进程：从 settings.json 读回缓存根/配额。
  SettingsStore settings(settings_root);
  const lapian::core::SettingsLoadResult loaded = settings.Load();
  Check(loaded.status == SettingsLoadStatus::kLoaded, "cross-settings-loaded");
  CheckEq(loaded.settings.cache_root, cache_a, "cross-settings-cache-root");
  CheckEqI64(loaded.settings.cache_quota_gb, 7, "cross-settings-quota-gb");

  CacheStore store(loaded.settings.cache_root,
                   static_cast<qint64>(loaded.settings.cache_quota_gb) * kGb);
  CheckEq(QDir::fromNativeSeparators(store.root()),
          QDir::fromNativeSeparators(cache_a), "cross-store-root");
  CheckEqI64(store.quota(), 7 * kGb, "cross-store-quota");
  const CacheKey key{QStringLiteral("BV1BMnv6jE5S"), 51, 64, 7};
  QString error;
  Check(CreateCompleteEntry(&store, key, 1 * kMb), "cross-create-entry");
  const qint64 usage = store.UsageBytes();
  CheckGe(usage, 1 * kMb, "cross-usage");

  // 再开一个进程：同一根/配额，占用仍在。
  CacheStore reopened(cache_a, 7 * kGb);
  CheckEqI64(reopened.UsageBytes(), usage, "cross-usage-persists");
  Check(reopened.Load(key).has_value(), "cross-entry-persists");

  // 改根：不迁移旧缓存；旧根仍在，新根为空。
  QString root_error;
  Check(reopened.SetRoot(cache_b, &root_error), "cross-set-root-ok");
  CheckEq(QDir::fromNativeSeparators(reopened.root()),
          QDir::fromNativeSeparators(cache_b), "cross-root-updated");
  CheckEqI64(reopened.UsageBytes(), 0, "cross-new-root-empty");
  Check(QFileInfo::exists(
            QDir(cache_a).filePath(QStringLiteral("BV1BMnv6jE5S/51/64-7/data.mp4"))),
        "cross-old-root-kept");
  Check(QFileInfo::exists(cache_a), "cross-old-root-exists");

  // 改配额：新进程仍生效。
  Check(reopened.SetQuotaBytes(3 * kGb, &root_error), "cross-set-quota");
  CacheStore quota_store(cache_b, 3 * kGb);
  CheckEqI64(quota_store.quota(), 3 * kGb, "cross-quota-new-instance");

  // 旧 settings.json（无新键）：默认根/配额，不崩。
  const QString legacy_root = temp.path() + QStringLiteral("/legacy-settings");
  Check(WriteAll(QDir(legacy_root).filePath(QStringLiteral("settings.json")),
                 QByteArrayLiteral("{\"schema\":1,\"volume\":42,\"waveform\":true}")),
        "cross-legacy-settings-write");
  SettingsStore legacy(legacy_root);
  const lapian::core::SettingsLoadResult legacy_loaded = legacy.Load();
  Check(legacy_loaded.status == SettingsLoadStatus::kLoaded, "cross-legacy-loaded");
  Check(legacy_loaded.settings.cache_root.isEmpty(), "cross-legacy-default-root");
  CheckEqI64(legacy_loaded.settings.cache_quota_gb, 20, "cross-legacy-default-quota");
  CheckEqI64(legacy_loaded.settings.volume, 42, "cross-legacy-volume-preserved");

  // SetRoot 校验。
  Check(!reopened.SetRoot(QString(), &root_error), "cross-set-root-empty-fails");
  const QString file_as_root = temp.path() + QStringLiteral("/not-a-dir");
  Check(WriteAll(file_as_root, QByteArrayLiteral("x")), "cross-write-file-for-root");
  Check(!reopened.SetRoot(file_as_root, &root_error), "cross-set-root-file-fails");
  Check(!root_error.isEmpty(), "cross-set-root-error-readable");
  CheckEq(QDir::fromNativeSeparators(reopened.root()),
          QDir::fromNativeSeparators(cache_b), "cross-root-unchanged-on-error");
}

// ---------------------------------------------------------------- Unicode/长路径

void TestLongPath() {
  QTemporaryDir temp;
  QString long_root = QDir(temp.path()).filePath(QStringLiteral("缓存根"));
  const QString segment = QStringLiteral("很长的中文目录名-");
  int index = 0;
  while (long_root.size() < 280) {
    long_root += QLatin1Char('/') + segment + QString::number(index++);
  }
  const CacheKey key{QStringLiteral("BV1BMnv6jE5S"), 61, 64, 7};
  CacheStore store(long_root, 300 * kMb);
  QString error;
  if (!store.SetRoot(long_root, &error)) {
    Skip("long-path", QStringLiteral("无法创建长路径缓存根（%1 字符）：%2")
                           .arg(long_root.size())
                           .arg(error));
    return;
  }
  const QString entry_dir = store.EntryDir(key);
  Check(entry_dir.size() > 260, "long-path-entry-over-max-path");
  std::printf("  long_root_len=%lld entry_len=%lld\n",
              static_cast<long long>(long_root.size()),
              static_cast<long long>(entry_dir.size()));

  Check(store.Reserve(key, 2 * kMb, &error), "long-path-reserve");
  Check(CreateSizedFile(store.PartPath(key), 2 * kMb), "long-path-part-write");
  Check(store.MarkComplete(key, &error), "long-path-movefile-longpath");
  Check(QFileInfo::exists(store.Mp4Path(key)), "long-path-mp4-exists");
  CheckEqI64(QFileInfo(store.Mp4Path(key)).size(), 2 * kMb, "long-path-mp4-size");
  QString mp4_path;
  Check(store.OpenComplete(key, &mp4_path, &error), "long-path-open-complete");
  std::optional<CacheEntry> entry = store.Load(key, &error);
  Check(entry.has_value() && entry->complete, "long-path-load-complete");
  CheckGe(store.UsageBytes(), 2 * kMb, "long-path-usage");
  Check(store.Rebuild(key, &error), "long-path-rebuild");
  Check(!QFileInfo::exists(entry_dir), "long-path-rebuild-removes-dir");
}

// ---------------------------------------------------------------- 无明文

void ScanMetaForSecrets(const QString& root, QVector<QString>* offenders) {
  QDir dir(root);
  const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QFileInfo& info : dirs) {
    ScanMetaForSecrets(info.absoluteFilePath(), offenders);
  }
  const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::NoSort);
  const QStringList markers = {QStringLiteral("SESSDATA"),
                               QStringLiteral("sessdata"),
                               QStringLiteral("Cookie"),
                               QStringLiteral("cookie"),
                               QStringLiteral("http://"),
                               QStringLiteral("https://"),
                               QStringLiteral("playurl"),
                               QStringLiteral("access_key"),
                               QStringLiteral("token")};
  for (const QFileInfo& file : files) {
    if (file.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) != 0) {
      continue;
    }
    const QByteArray bytes = ReadAll(file.absoluteFilePath());
    for (const QString& marker : markers) {
      if (bytes.contains(marker.toUtf8())) {
        offenders->push_back(file.absoluteFilePath() + QStringLiteral(" -> ") + marker);
      }
    }
  }
}

void TestNoPlaintext() {
  QTemporaryDir temp;
  const QString root = temp.path() + QStringLiteral("/cache");
  CacheStore store(root, 300 * kMb);
  const CacheKey key{QStringLiteral("BV1BMnv6jE5S"), 71, 64, 7};
  Check(CreateCompleteEntry(&store, key, 1 * kMb, QStringLiteral("标题 ✓")),
        "plaintext-setup");
  QVector<QString> offenders;
  ScanMetaForSecrets(root, &offenders);
  Check(offenders.isEmpty(), "plaintext-no-cookie-or-url-in-meta");
  for (const QString& offender : offenders) {
    std::printf("  命中：%s\n", offender.toUtf8().constData());
  }
  const QByteArray meta =
      ReadAll(QDir(store.EntryDir(key)).filePath(QStringLiteral("meta.json")));
  Check(meta.contains("\"schema\":\"lapian-cache-1\""), "plaintext-schema-value");
  Check(!meta.contains("\"url\""), "plaintext-no-url-key");
}

void TestAudioEntries() {
  const QString bvid = QStringLiteral("BV1BMnv6jE5S");
  const qint64 cid = 42915922022LL;
  CacheKey video{bvid, cid, 80, 7};
  CacheKey audio;
  audio.bvid = bvid;
  audio.cid = cid;
  audio.kind = lapian::online::CacheStreamKind::kAudio;
  audio.audio_id = 30280;
  audio.qn = 0;
  audio.codecid = 0;

  Check(lapian::online::IsValidCacheKey(video), "audio-key-video-valid");
  Check(lapian::online::IsValidCacheKey(audio), "audio-key-audio-valid");
  Check(lapian::online::CacheKeyLabel(audio).contains(QStringLiteral("audio-30280")),
        "audio-key-label");
  CacheKey bad_audio = audio;
  bad_audio.qn = 80;
  Check(!lapian::online::IsValidCacheKey(bad_audio),
        "audio-key-rejects-qn");
  bad_audio = audio;
  bad_audio.audio_id = 0;
  Check(!lapian::online::IsValidCacheKey(bad_audio),
        "audio-key-rejects-zero-id");
  CacheKey bad_video = video;
  bad_video.audio_id = 30280;
  Check(!lapian::online::IsValidCacheKey(bad_video),
        "audio-key-video-rejects-audio-id");

  QTemporaryDir temp;
  const QString root = temp.path() + QStringLiteral("/cache");
  CacheStore store(root, 300 * kMb);
  Check(store.EntryDir(audio).contains(QStringLiteral("audio-30280")),
        "audio-entry-dir");
  Check(CreateCompleteEntry(&store, video, 1 * kMb, QStringLiteral("视频")),
        "audio-setup-video");
  Check(CreateCompleteEntry(&store, audio, 128 * kKb, QStringLiteral("音频")),
        "audio-setup-audio");

  // Scan 必须同时返回视频与音频条目，且 kind/audio_id 原样保留。
  QVector<CacheEntry> entries;
  QVector<CacheEntry> broken;
  QString error;
  Check(store.Scan(&entries, &broken, &error), "audio-scan-ok");
  Check(entries.size() == 2 && broken.isEmpty(),
        "audio-scan-count");
  bool video_found = false;
  bool audio_found = false;
  for (const CacheEntry& entry : entries) {
    if (entry.key == video) {
      video_found = true;
      Check(!entry.dash, "audio-video-not-dash");
    } else if (entry.key == audio) {
      audio_found = true;
      Check(entry.complete && entry.total_bytes == 128 * kKb,
            "audio-entry-complete");
      Check(entry.key.kind == lapian::online::CacheStreamKind::kAudio,
            "audio-entry-kind");
    }
  }
  Check(video_found && audio_found, "audio-scan-both-streams");

  // meta.json 明确标 kind/audio_id；不把音频塞进 qn/codecid。
  const QByteArray meta =
      ReadAll(QDir(store.EntryDir(audio)).filePath(QStringLiteral("meta.json")));
  Check(meta.contains("\"kind\":\"audio\""), "audio-meta-kind");
  Check(meta.contains("\"audio_id\":30280"), "audio-meta-audio-id");
  Check(meta.contains("\"qn\":0") && meta.contains("\"codecid\":0"),
        "audio-meta-qn-codecid-zero");

  // 完整音频条目可 OpenComplete；外部删除 .mp4 后必须判损坏而不是当可播。
  QString mp4;
  Check(store.OpenComplete(audio, &mp4, &error), "audio-open-complete");
  Check(!mp4.isEmpty() && QFileInfo::exists(mp4), "audio-open-complete-path");

  // 当前视频会话保护：清空必须同时保留视频与配套音频（DASH 双输入）。
  CacheKey other = video;
  other.cid = cid + 1;
  Check(CreateCompleteEntry(&store, other, 256 * kKb, QStringLiteral("其它")),
        "audio-setup-other");
  qint64 removed = 0;
  int kept = 0;
  Check(store.ClearAll(&error, &removed, &video, &kept), "audio-clear-all");
  Check(kept == 2, "audio-clear-keeps-video-and-audio");
  entries.clear();
  broken.clear();
  Check(store.Scan(&entries, &broken, &error), "audio-scan-after-clear");
  Check(HasKey(entries, video) && HasKey(entries, audio) &&
            !HasKey(entries, other),
        "audio-clear-protected-set");

  // 配额淘汰也保护当前 bvid+cid 的音频；没有可淘汰候选时必须显式失败。
  Check(store.SetQuotaBytes(1 * kMb, &error), "audio-quota-set");
  Check(!store.EvictToQuota(video, &error), "audio-evict-protected-fails");
  Check(QFileInfo::exists(store.Mp4Path(audio)), "audio-evict-keeps-audio");

  // 音频 meta 的 kind 与目录键不一致 → 损坏，不当可播放。
  Check(WriteRawMeta(store, audio, 128 * kKb, 128 * kKb, true,
                     QDateTime::currentDateTimeUtc().addSecs(-60),
                     QDateTime::currentDateTimeUtc(),
                     QJsonObject{{QStringLiteral("kind"), QStringLiteral("video")},
                                 {QStringLiteral("audio_id"), 30280}}),
        "audio-write-bad-kind-meta");
  const std::optional<CacheEntry> bad = store.Load(audio, &error);
  Check(bad.has_value() && bad->broken, "audio-bad-kind-broken");
}

}  // namespace

int main() {
  std::printf("T8.3 online_cache_test\n");
  TestDefaultsAndKeys();
  TestLifecycle();
  TestProgressAndComplete();
  TestResume();
  TestCorruption();
  TestLru300Mb();
  TestCurrentProtected();
  TestEvictBusy();
  TestDiskSpace();
  TestAccounting20Gb();
  TestClearAll();
  TestAudioEntries();
  TestCleanupOrphans();
  TestCrossSession();
  TestLongPath();
  TestNoPlaintext();

  std::printf("checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
  if (g_failures == 0) {
    std::printf("T8.3 online_cache_test: all checks passed\n");
    return 0;
  }
  std::printf("T8.3 online_cache_test: %d failure(s)\n", g_failures);
  return 1;
}
