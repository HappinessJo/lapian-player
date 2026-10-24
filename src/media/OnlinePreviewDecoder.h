#pragma once

// T8.6.1b 在线拖动预览解码器：只服务“增长中的 .part 会话”的拖动预览。
//
// 与本地 PreviewDecoder 的边界：
// - 本地/完整缓存命中继续走 PreviewDecoder（路径 + FrameIndex），本类不接管；
// - 本类用独立 GrowingSource::GrowingCursor/GrowingFormat 打开视频 `.part`，
//   不碰 MediaSession 的 decoder/reader，也不碰 T8.6.1a 的 online_seek 状态；
// - 只按“稳定前缀索引”预览已缓存区；目标超出稳定前缀时只标记 waiting，
//   绝不进入阻塞读，UI 线程不会等待。
//
// 线程/取消：
// - 每个实例一个预览线程；Begin 启动，End/Close 取消并 join（不在 UI 线程同步 seek）；
// - 目标频繁变化时只保留最新目标 + generation；旧 generation 的解码在切片点退出；
// - 同一时刻只有一个在途解码；最新帧只留 1 张，降采样帧缓存最多 kMaxCachedFrames 张。
//
// 帧所有权：OnlinePreviewFrame 的 frame 归调用方，取走后需 av_frame_free。
// 预览帧是降采样 NV12（最长边约 1280），不是精确目标帧；松手仍走 GoToFrame。

#include <cstdint>
#include <memory>
#include <string>

#include "media/PreviewDecoder.h"

namespace lapian::online {
class GrowingSource;
}

namespace lapian::media {

class FrameIndex;

class OnlinePreviewDecoder {
 public:
  OnlinePreviewDecoder();
  ~OnlinePreviewDecoder();

  OnlinePreviewDecoder(const OnlinePreviewDecoder&) = delete;
  OnlinePreviewDecoder& operator=(const OnlinePreviewDecoder&) = delete;

  // source/part_path 必须对应同一个 `.part`；index 是稳定的只读快照。
  // 调用后立即返回；打开/首帧在后台线程。
  void Begin(std::shared_ptr<lapian::online::GrowingSource> source,
             const std::string& part_path_utf8, int video_stream_index,
             std::shared_ptr<const FrameIndex> index,
             std::int64_t initial_frame, bool initial_cached);
  // 索引增长时替换快照（GUI 线程拷贝，worker 只持有 shared_ptr）。
  void UpdateIndex(std::shared_ptr<const FrameIndex> index);
  // frame 是拖动目标；cached=false 表示未缓存/未索引，只显示等待，不解码。
  void UpdateTarget(std::int64_t frame, bool cached);

  // 结束本次拖动：取消阻塞读、join 预览线程、释放帧缓存。
  void End();
  void Close();

  // 取走最新预览帧；没有可用的新帧时 frame == nullptr。
  // 旧 generation 或比当前目标更靠后的帧会被丢弃，不会返回。
  PreviewFrame TakeLatestFrame();

  bool IsReady() const;
  bool IsWaiting() const;
  bool IsThreadRunning() const;
  std::string last_error() const;

  // 诊断/探针：发布帧数、丢弃的过期帧、seek 次数、缓存帧数。
  std::uint64_t frames_published() const;
  std::uint64_t stale_dropped() const;
  std::uint64_t seek_count() const;
  int cached_frames() const;
  std::int64_t last_published_frame() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lapian::media
