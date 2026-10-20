#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "media/FrameCache.h"
#include "media/FrameIndex.h"
#include "media/MediaInfo.h"
#include "media/MediaProbe.h"
#include "media/VideoDecoder.h"

struct AVFrame;
struct ID3D11Device;

namespace lapian::media {

// T7.1：一次打开的分段耗时（毫秒），只用于冷启动测量，不参与逻辑。
struct MediaOpenTimings {
  double probe_ms = 0.0;
  double decoder_open_ms = 0.0;
  double first_frame_ms = 0.0;
  double index_ms = 0.0;
  double total_ms = 0.0;
  std::size_t index_frames = 0;
  bool index_ok = false;
};

// 一条已打开视频的媒体会话：探测结果 + 解码器 + 首帧。
// T1.4 用它切换文件；T1.5 在后面继续用同一个解码器做播放。
class MediaSession {
 public:
  MediaSession();
  ~MediaSession();

  MediaSession(const MediaSession&) = delete;
  MediaSession& operator=(const MediaSession&) = delete;

  // 成功后才替换当前会话；失败时保持旧会话不变。
  // T7.1：build_index=false 时只做探测/解码首帧，索引由调用方在后台建好后
  // 通过 InstallIndex 安装；默认 true 保持既有同步语义（回归测试用）。
  bool Open(const std::string& utf8_path, ID3D11Device* device, std::string* error,
            bool build_index = true);
  void Close();

  // 用当前会话的文件/流建索引；失败不影响已打开的会话（索引保持为空）。
  bool BuildIndexNow(std::string* error, const std::atomic<bool>* cancel = nullptr);
  // 安装后台建好的索引（调用方保证在 GUI 线程、且会话仍是同一个）。
  void InstallIndex(FrameIndex index, double build_ms);
  bool HasIndex() const { return index_.IsReady() && !index_.empty(); }
  int video_stream_index() const { return probe_.info.video.stream_index; }
  // T7.2：普通跳转/播放循环默认 kNone（保证解码器停在目标帧之后）；
  // 后退步进传 kBackwardWindow 预热最近若干帧，后续连续后退可直接命中。
  bool SeekToFrame(std::int64_t frame, DecodedFrameInfo* out, std::string* error,
                   FrameCacheMode cache_mode = FrameCacheMode::kNone);
  // 缓存命中后解码器可能不在目标帧之后；起播前调用一次，必要时真正 seek。
  bool EnsureSequentialPosition(std::int64_t frame, std::string* error);

  bool IsOpen() const { return open_; }
  const MediaInfo& info() const { return probe_.info; }
  const MediaProbeResult& probe() const { return probe_; }
  // 最近一次尝试的探测结果（失败时也可读，用于 UI 错误分类）。
  const MediaProbeResult& last_attempt() const { return last_attempt_; }
  const DecodedFrameInfo& first_frame() const { return first_frame_; }
  const FrameIndex& index() const { return index_; }
  // T7.2：帧缓存上限/命中统计（日志与测试用）。
  const FrameCache& frame_cache() const { return frame_cache_; }
  // 最近一次 Open 的分段耗时（T7.1 冷启动报告）。
  const MediaOpenTimings& last_open_timings() const { return last_open_timings_; }
  VideoDecoder& decoder() { return *decoder_; }
  const std::string& path_utf8() const { return probe_.info.path_utf8; }
  bool IsVfrSuspect() const { return probe_.info.vfr.suspect; }

 private:
  MediaProbeResult probe_;
  MediaProbeResult last_attempt_;
  FrameIndex index_;
  FrameCache frame_cache_;
  std::unique_ptr<VideoDecoder> decoder_;
  DecodedFrameInfo first_frame_;
  MediaOpenTimings last_open_timings_;
  // 缓存命中时返回的软件帧，保持到下一次 Seek/Close，沿用解码器帧的生命周期约定。
  AVFrame* last_hit_frame_ = nullptr;
  // true 表示解码器未必停在“最后返回帧的下一帧”，起播前需要重新定位。
  bool decoder_stale_ = true;
  bool open_ = false;
};

}  // namespace lapian::media
