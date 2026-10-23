#pragma once

// T8.6 在线部分索引：只 demux 已发布前缀，不把未下载区当成 EOF。
//
// - 每次从文件起点重建到当前 Published 前沿；前沿读用 GrowingCursor 的
//   100ms 等待超时结束，避免阻塞后台线程。
// - 只暴露“稳定前缀”（末尾留 tail_guard_frames 帧），下载增长时帧号前缀不变；
//   完整下载后才包含全部帧。
// - 同时返回每帧 packet 的 pos/size，供 UI 判断目标是否已缓存。
// - 本类不联网、不接 UI；每个 builder 自己打开独立 GrowingFormat/Cursor。

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "media/FrameIndex.h"

namespace lapian::online {
class GrowingSource;
}

namespace lapian::media {

struct OnlineFrameSpan {
  std::int64_t pos = -1;
  std::int64_t size = 0;
  bool keyframe = false;
};

struct OnlineIndexResult {
  bool ok = false;
  FrameIndex index;
  std::vector<OnlineFrameSpan> spans;  // 与 index 的帧号一一对应
  int stable_frames = 0;
  int parsed_frames = 0;
  bool complete = false;
  bool timed_out = false;
  double build_ms = 0.0;
  std::string error;
};

// tail_guard_frames：未完整下载时，末尾保留不暴露的帧数。0 表示不做保护
// （仅测试/完整文件使用）。cancel 可为空。
OnlineIndexResult BuildOnlineIndex(
    const std::shared_ptr<lapian::online::GrowingSource>& source,
    const std::string& part_path_utf8, int video_stream_index,
    int tail_guard_frames = 48,
    const std::atomic<bool>* cancel = nullptr);

}  // namespace lapian::media
