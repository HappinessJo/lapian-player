#pragma once

#include <string>

namespace lapian::media {

// T7.5：打开前的有界路径预检。
//
// 背景：`avformat_open_input` 在 UI 线程同步调用。对断开的网络驱动器/不可达
// UNC 主机，Windows 的 SMB 连接超时可达 ~20s，直接违反 spec §4.4“未响应不得
// 超过 5s”。这里只对网络路径做一次带超时的 `GetFileAttributesW` 预检：
// 超时/不存在/无权限时直接给可读错误，不进入 FFmpeg 的阻塞 IO。
//
// 局限（已知，写入 T7.5 交接）：预检只判断“路径可达”；可访问但读写很慢的
// 网络盘仍可能在真正打开/解码时阻塞，彻底解决需要异步打开（未做）。
enum class PathProbeStatus {
  kReachable,
  kMissing,
  kPermissionDenied,
  kTimeout,
  kError,
};

struct PathProbeResult {
  PathProbeStatus status = PathProbeStatus::kError;
  double elapsed_ms = 0.0;
  // 可直接拼进错误提示的中文短语；不包含路径本身。
  std::string message;
};

// 判断路径是否可能落在网络位置：UNC（`\\server\share\...`、`\\?\UNC\...`）
// 或映射网络驱动器（`GetDriveTypeW == DRIVE_REMOTE`）。
// 只做本地字符串/盘符查询，不访问网络。
bool IsNetworkPath(const std::string& utf8_path);

// 有界可达性预检：工作线程调用 `GetFileAttributesW`，主线程最多等待
// timeout_ms。超时返回 kTimeout 并放弃等待工作线程（线程只持有自有的
// Win32 调用状态，不引用 Qt/FFmpeg 对象，进程退出时安全）。
// 传入本地路径也可用（用于单测），但产品只在网络路径上调用。
PathProbeResult ProbePathWithTimeout(const std::string& utf8_path, int timeout_ms);

}  // namespace lapian::media
