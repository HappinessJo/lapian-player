#pragma once

#include <string>
#include <vector>

#include "media/MediaInfo.h"

namespace lapian::media {

// 打开/探测失败的分类。UI（T1.4）负责把这些分类翻译成用户可读的中文提示；
// 本模块只返回稳定枚举与技术细节，不依赖 Qt。
enum class MediaProbeError {
  kNone = 0,
  kFileNotFound,
  kPermissionDenied,
  kUnsupportedFormat,
  kCorrupt,
  kNoVideoStream,
  kStreamInfoFailed,
  kOpenFailed,
};

const char* MediaProbeErrorName(MediaProbeError error);

// 把 FFmpeg 的 display matrix 逆时针角度规范到 [0,360)；非有限值返回 0。
// 单独暴露是为了让 T1.3 渲染与单测使用同一套归一化规则。
int NormalizeRotationDegrees(double degrees);

// 用相邻帧 PTS 间隔（秒）做 VFR 初判。纯函数，便于单测；
// 少于 8 个有效间隔时 checked=false，不误报。
VfrInfo ClassifyVfrIntervals(const std::vector<double>& intervals_seconds);

struct MediaProbeResult {
  bool ok = false;
  MediaProbeError error = MediaProbeError::kNone;
  std::string error_detail;  // UTF-8 技术细节，可能来自 av_strerror
  MediaInfo info;
  double probe_ms = 0.0;  // 打开 + 读取流信息 + VFR 初判的总墙钟时间
};

// 打开本地文件，读取第一条视频/音频流元数据；只读，不发网络请求。
// 失败时不抛异常，返回 ok=false 与错误分类。
MediaProbeResult ProbeMedia(const std::string& utf8_path);

}  // namespace lapian::media
