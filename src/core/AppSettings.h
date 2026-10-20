#pragma once

#include <QByteArray>
#include <QString>

// T6.3 应用设置持久化：只保存 spec §3.8 要求的音量与波形开关。
// 循环、播放速度、静音都只在本次会话内保持，不写入这里。
// 解析/校验是纯函数（只依赖 Qt Core），产品与单测共用。
namespace lapian::core {

struct AppSettings {
  int volume = 100;
  bool waveform_visible = true;

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
