#pragma once

#include <QString>

#include <cstdint>

namespace lapian::snapshot {

// T5.4 截图文件命名与目录选择（纯函数，便于单测）。
// 命名口径见 spec §3.6：{视频名}_f{帧号}_{HH-MM-SS-FF}.png；重名加 _1、_2…
// 视频目录下的 snapshots/ 不可写时回退用户图片目录下的“拉片软件/”。

// 只保留文件名安全的字符：Windows 非法字符、控制字符替换为 '_'，
// 去掉尾部的点/空格，避开 CON/PRN/AUX/NUL/COM1-9/LPT1-9 等设备名。
QString SanitizeSnapshotBaseName(const QString& video_base_name);

// 第 frame 帧的 NDF 时间码（按标称帧率），输出 HH-MM-SS-FF。
// 帧率无效时按 30fps 处理，保证文件名可生成。
QString FormatSnapshotTimecode(std::int64_t frame, int fps_num, int fps_den);

// {sanitize(视频完整文件名去扩展名)}_f{帧号}_{HH-MM-SS-FF}
QString BuildSnapshotBaseName(const QString& video_path, std::int64_t frame,
                              int fps_num, int fps_den);

// UI v2 3C：从截图文件名反解帧号（纯函数，截图网格扫历史目录用）。
// 只认 BuildSnapshotBaseName/SnapshotFileName 生成的形态：
//   {base}_f{帧号}_{HH-MM-SS-FF}[_序号].png
// 不匹配（含任意其他 png）返回 -1；帧号溢出 std::int64_t 也返回 -1。
std::int64_t ParseSnapshotFrameFromName(const QString& file_name);

// sequence=0 时不加序号；>0 时为 {base}_{sequence}.png。
QString SnapshotFileName(const QString& base_name, int sequence);
QString SnapshotFilePath(const QString& directory, const QString& base_name, int sequence);

struct SnapshotDirectory {
  QString path;     // 空表示主目录与回退目录都不可写
  bool fallback = false;
  QString reason;   // 回退原因，可直接用于状态栏提示
};

// 依次尝试 <视频目录>/snapshots 与 <pictures_root>/拉片软件。
// pictures_root 为空时使用系统图片目录；测试可注入临时目录。
// 可写性用同目录临时文件探测，避免 Windows 只读属性/ACL 的误判。
SnapshotDirectory ResolveSnapshotDirectory(const QString& video_path,
                                           const QString& pictures_root,
                                           QString* error);

// T8.7 在线截图（delta O10）：
// 目录 %USERPROFILE%\Pictures\拉片软件\<bvid>\；不可写时回退
// %APPDATA%\拉片软件\online\snapshots\<bvid>\。
// appdata_online_root 为空时使用 %APPDATA%\拉片软件\online（测试可注入）。
struct OnlineSnapshotDirectory {
  QString path;     // 空表示主目录与回退目录都不可写
  bool fallback = false;
  QString reason;   // 回退原因，可直接用于状态栏提示
};

OnlineSnapshotDirectory ResolveOnlineSnapshotDirectory(
    const QString& bvid, const QString& pictures_root,
    const QString& appdata_online_root, QString* error);

// <bvid>_f<frame>_<HH-MM-SS-FF>（重名加 _1、_2…，由 SnapshotFilePath 负责）。
QString BuildOnlineSnapshotBaseName(const QString& bvid, std::int64_t frame,
                                    int fps_num, int fps_den);

// 截图目录内 index.jsonl 追加一行；编码/写盘全部成功后才调用。
// 失败时回滚到追加前的文件长度，不留下半条 JSON。
bool AppendOnlineSnapshotIndex(const QString& directory, const QString& bvid,
                               std::int64_t cid, int qn, int codecid, double fps,
                               std::int64_t frame, std::int64_t pts_us,
                               const QString& png_path, QString* error);

}  // namespace lapian::snapshot
