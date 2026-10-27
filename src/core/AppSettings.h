#pragma once

#include <QByteArray>
#include <QString>

#include <cstddef>

// T6.3 应用设置持久化：只保存 spec §3.8 要求的音量与波形开关。
// 循环、播放速度、静音都只在本次会话内保持，不写入这里。
// 解析/校验是纯函数（只依赖 Qt Core），产品与单测共用。
//
// T8.3：新增在线缓存两个键（cache_root / cache_quota_gb），只加不改——
// v1 的 volume/waveform 语义与 schema 仍是 1；旧文件缺键时用默认值，
// 未知键继续忽略。缓存根为空表示用 CacheStore::DefaultRoot()。
//
// D1：新增在线拖动预览两个键（preview_resolution / preview_cache_mb），
// 同样只加不改：schema 仍是 1，旧文件缺键用 auto/512，未知键继续忽略。
namespace lapian::core {

// 配额范围按 delta §4.3 建议（1–100GB，步进 1GB）；默认 20GB（vision 已拍板）。
constexpr int kCacheQuotaDefaultGb = 20;
constexpr int kCacheQuotaMinGb = 1;
constexpr int kCacheQuotaMaxGb = 100;

// D1：在线拖动预览清晰度/缓存（计划 §3）。auto 跟随源且最高 1080P，不向上放大。
enum class PreviewResolution { kAuto = 0, k1080p = 1, k720p = 2 };

constexpr int kPreviewCacheDefaultMb = 512;   // 默认
constexpr int kPreviewCacheMinMb720p = 128;   // 720P 下限
constexpr int kPreviewCacheMinMb1080p = 256;  // 1080P 下限
constexpr int kPreviewCacheMaxMb = 4096;      // 上限（硬）

struct AppSettings {
  int volume = 100;
  bool waveform_visible = true;
  QString cache_root;                    // 空 = 默认 %LOCALAPPDATA%\拉片软件\cache
  int cache_quota_gb = kCacheQuotaDefaultGb;
  // D1：放在旧字段之后，保持既有 AppSettings{volume, waveform} 聚合初始化兼容。
  PreviewResolution preview_resolution = PreviewResolution::kAuto;
  int preview_cache_mb = kPreviewCacheDefaultMb;

  bool operator==(const AppSettings& other) const = default;
};

enum class SettingsLoadStatus {
  kLoaded,             // 文件存在且 schema/JSON 合法
  kMissing,            // 没有文件（或空文件）：使用默认值
  kUnreadable,         // 存在但打不开/不是普通文件：使用默认值
  kCorrupt,            // JSON/结构损坏或超过大小上限：使用默认值
  kUnsupportedSchema,  // 来自更新版本的 schema：使用默认值
};

struct SettingsLoadResult {
  AppSettings settings;
  SettingsLoadStatus status = SettingsLoadStatus::kMissing;
};

// 纯解析：不读文件、不弹窗、不崩溃；字段非法时逐项回退默认值。
QByteArray SerializeAppSettings(const AppSettings& settings);
SettingsLoadResult ParseAppSettings(const QByteArray& bytes);
QString SettingsLoadStatusName(SettingsLoadStatus status);

// T8.3：产品保存音量/波形时保留其它键（cache_root/cache_quota_gb），
// 避免 T6.3 的保存路径把在线缓存设置冲回默认。纯函数，单测覆盖。
AppSettings MergeVolumeWaveform(const AppSettings& base, int volume,
                                bool waveform_visible);

// D1："auto" / "1080p" / "720p"（小写规范键）；解析大小写不敏感，非法回退 fallback。
QString PreviewResolutionKey(PreviewResolution resolution);
PreviewResolution ParsePreviewResolutionKey(
    const QString& key, PreviewResolution fallback = PreviewResolution::kAuto);

// 下限按“有效长边”给：<=1280 用 128，否则 256。
int PreviewCacheMinMbForMaxSide(int max_side);
// 下限按设置项给：720p→128；auto/1080p→256（auto 可能落到 1080P，取下限的保守值）。
int PreviewCacheMinMbForResolution(PreviewResolution resolution);

// 纯 clamp：先抬到下限，再夹到 kPreviewCacheMaxMb，再按可用内存 25% 夹
// （available_memory_mb<=0 表示未知、不夹）。若 25% 上限低于下限，保下限并通过
// limited_by_memory 报告冲突；limited_by_memory 可为 nullptr。
int ClampPreviewCacheMb(int mb, PreviewResolution resolution,
                        int available_memory_mb, bool* limited_by_memory);

// 一次在线会话实际使用的预览配置（D2/D3 从这里取值；不向上放大、不热切换）。
struct ResolvedPreviewConfig {
  int max_side = 1920;                 // 预览长边上限（1280/1920）
  int cache_mb = kPreviewCacheDefaultMb;
  std::size_t cache_bytes = 0;         // cache_mb * 1024 * 1024
  bool follow_source = true;           // auto
  bool source_limited = false;         // 选了 1080P 但源更小（不向上放大）
  bool cache_lifted_to_min = false;
  bool cache_limited_by_memory = false;
};

// source_max_side = max(源宽,源高)；<=0 表示未知，按 1920 处理（自动/1080p）。
// auto:  min(1920, 源)；1080p: min(1920, 源)；720p: min(1280, 源)。都不向上放大。
ResolvedPreviewConfig ResolvePreviewConfig(const AppSettings& settings,
                                           int source_max_side,
                                           int available_memory_mb);

// 配置根目录（不传时用 DefaultRootDir）。实际文件是 <root>/settings.json。
// 原子写失败、目录不可建等一律静默返回 false，由调用方决定是否记录日志。
class SettingsStore {
 public:
  explicit SettingsStore(QString root_dir = QString());

  // %APPDATA%\拉片软件（与 sidecar 回退目录同级），不写注册表。
  static QString DefaultRootDir();

  const QString& root_dir() const { return root_dir_; }
  QString file_path() const;

  SettingsLoadResult Load() const;
  bool Save(const AppSettings& settings) const;

  // 防御性上限：异常大/被替换的文件不读进内存，也不阻塞窗口显示。
  static constexpr qint64 kMaxSettingsBytes = 64 * 1024;

 private:
  QString root_dir_;
};

}  // namespace lapian::core
