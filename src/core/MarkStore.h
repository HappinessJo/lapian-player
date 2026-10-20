#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace lapian::core {

// T5.1 标记/备注 sidecar 存储（ADR-006）。
// 本层只负责数据与文件，不依赖 MainWindow/Timeline：
// - 默认写视频同目录的 `<视频完整文件名>.lapian.json`（含扩展名，避免同目录
//   同名不同后缀的视频互相覆盖）；源目录不可写时回退
//   `%APPDATA%\拉片软件\marks\<规范化路径 SHA-256>.json`。
// - 原子写（同目录临时文件 -> flush -> ReplaceFile/rename），失败不破坏旧文件。
// - sidecar 与回退文件同时存在时按 mark id 合并，冲突取 updated_at 较新者。
// - 损坏 JSON 备份为 `.bak` 后按空处理，不阻塞打开；最后一个标记删除后删除附属文件。
// - 标记同时保存 frame 与 pts（微秒）；重开优先 frame，PTS 不一致时按 PTS 校正并返回提示。
// 不做：写视频元数据、SQLite、最近文件、跨进程锁。

inline constexpr int kMarkSchemaVersion = 1;
inline constexpr const char* kMarkSchemaName = "lapian.marks";

// 视频标识。path 保留磁盘大小写便于阅读；path_key 小写、反斜杠、绝对路径，
// 用于身份比较与回退文件哈希。
struct VideoIdentity {
  std::string path;
  std::string path_key;
  std::uint64_t size = 0;
  std::int64_t duration_us = 0;

  bool empty() const { return path.empty(); }
};

struct Mark {
  std::string id;  // UUID（无花括号）；合并键
  std::int64_t frame = 0;
  std::int64_t pts_us = 0;  // 媒体时间，AV_TIME_BASE 微秒
  std::string label;
  std::string note;
  std::string created_at;  // ISO-8601 UTC，毫秒
  std::string updated_at;

  bool operator==(const Mark&) const = default;
};

// sidecar 与 AppData 回退的完整路径（UTF-8）。
struct MarkPaths {
  std::string sidecar;
  std::string fallback;
  std::string path_hash;
};

struct MarkStoreOptions {
  // 空表示真实 `%APPDATA%\拉片软件\marks`；测试注入临时目录，避免污染用户数据。
  std::string appdata_marks_dir;
};

// T5.2 用 FrameIndex 提供；T5.1 只定义接口与纯函数，不依赖 media。
struct FrameLookup {
  std::int64_t frame_count = 0;  // <=0 表示未知
  std::function<bool(std::int64_t frame, std::int64_t* pts_us)> pts_of_frame;
  std::function<bool(std::int64_t pts_us, std::int64_t* frame)> frame_of_pts;
};

enum class MarkWarningKind {
  kCorruptBackup,     // 损坏 JSON 已备份为 .bak
  kReadFailed,        // 文件存在但读不了/不是普通文件
  kUnsupportedSchema, // schema_version 高于本版本；不覆盖
  kIdentityMismatch,  // 文件内视频标识与当前文件不一致
  kPtsMismatch,       // frame/pts 不一致，已按 PTS 校正或保留 frame
  kSaveFailed,        // 写 sidecar 与回退都失败，或删除附属文件失败
  kMigrationFailed,   // 写成功但未能清理旧副本
};

struct MarkWarning {
  MarkWarningKind kind = MarkWarningKind::kReadFailed;
  std::string message;  // 中文，可直接给 UI
  std::string path;     // 相关文件，可为空
};

struct SaveResult {
  bool ok = true;
  bool wrote = false;
  bool removed = false;
  std::string path;  // 实际写入/删除的目标（删除时为空标记的预测 sidecar）
  std::string error;
};

// 路径规范化与哈希（纯函数，便于单测）。
// NormalizeVideoPath 返回小写身份键；空输入返回空串。
std::string NormalizeVideoPath(const std::string& path_utf8);
std::string MarkPathHash(const std::string& normalized_path);
VideoIdentity MakeVideoIdentity(const std::string& path_utf8, std::int64_t duration_us);
MarkPaths ResolveMarkPaths(const std::string& video_path_utf8,
                           const MarkStoreOptions& options = {});

// 按 id 合并；同一 id 取 updated_at 较新者，时间相同保留 sidecar 版本。
// 结果按 frame、pts、created_at、id 排序。
std::vector<Mark> MergeMarks(const std::vector<Mark>& sidecar,
                             const std::vector<Mark>& fallback);

// VFR 双键校正：优先 frame；帧对应 PTS 不一致时按 PTS 找回帧号并更新 pts。
// lookup 为空时不做任何事。警告写入 warnings。
void ReconcileMarks(std::vector<Mark>* marks, const FrameLookup& lookup,
                    std::vector<MarkWarning>* warnings);

// JSON 序列化/解析（schema v1）。Parse 失败时 marks/stored_video 清空；
// unsupported_schema 为 true 表示版本更高，调用方不得覆盖原文件。
std::string SerializeMarkFile(const VideoIdentity& video, const std::vector<Mark>& marks);
bool ParseMarkFile(const std::string& json_utf8, std::vector<Mark>* marks,
                   VideoIdentity* stored_video, std::string* error,
                   bool* unsupported_schema = nullptr);

class MarkStore {
 public:
  MarkStore() = default;

  // 读取、备份损坏文件、合并 sidecar/回退、做 frame+pts 校正，并尽量迁移到
  // 当前规范位置（sidecar 优先）。永不抛异常；失败通过 warnings/storage_path 反映。
  static MarkStore Open(const std::string& video_path_utf8, std::int64_t duration_us,
                        const MarkStoreOptions& options = {},
                        const FrameLookup& lookup = {});

  bool valid() const { return valid_; }
  const VideoIdentity& video() const { return video_; }
  const MarkPaths& paths() const { return paths_; }
  const std::vector<Mark>& marks() const { return marks_; }
  const std::vector<MarkWarning>& warnings() const { return warnings_; }
  const std::string& storage_path() const { return storage_path_; }
  bool using_fallback() const { return using_fallback_; }

  const Mark* FindMark(const std::string& id) const;

  // 新标记使用当前 UTC 时间与 UUID；label 为空时用“帧 N”。
  bool AddMark(std::int64_t frame, std::int64_t pts_us, const std::string& label,
               const std::string& note, std::string* out_id = nullptr);
  // 备注/标签编辑（T5.3）；内容未变时不更新 updated_at。
  bool SetMarkText(const std::string& id, const std::string& label,
                   const std::string& note);
  bool RemoveMark(const std::string& id);

  // 空标记会删除 sidecar 与回退文件（F6-A6）；先尝试 sidecar，失败回退 AppData。
  SaveResult Save();

 private:
  VideoIdentity video_;
  MarkPaths paths_;
  std::vector<Mark> marks_;
  std::vector<MarkWarning> warnings_;
  MarkStoreOptions options_;
  std::string storage_path_;
  bool using_fallback_ = false;
  bool valid_ = false;
  bool block_save_ = false;  // 读到更高 schema/读失败时禁止覆盖
};

}  // namespace lapian::core
