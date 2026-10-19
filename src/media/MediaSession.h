#pragma once

#include <memory>
#include <string>

#include "media/FrameIndex.h"
#include "media/MediaInfo.h"
#include "media/MediaProbe.h"
#include "media/VideoDecoder.h"

struct ID3D11Device;

namespace lapian::media {

// 一条已打开视频的媒体会话：探测结果 + 解码器 + 首帧。
// T1.4 用它切换文件；T1.5 在后面继续用同一个解码器做播放。
class MediaSession {
 public:
  MediaSession();
  ~MediaSession();

  MediaSession(const MediaSession&) = delete;
  MediaSession& operator=(const MediaSession&) = delete;

  // 成功后才替换当前会话；失败时保持旧会话不变。
  bool Open(const std::string& utf8_path, ID3D11Device* device, std::string* error);
  void Close();

  // T2.4：用同一个解码器 seek 到目标帧；成功后解码器停在目标帧之后，可继续播放。
  bool SeekToFrame(std::int64_t frame, DecodedFrameInfo* out, std::string* error);

  bool IsOpen() const { return open_; }
  const MediaInfo& info() const { return probe_.info; }
  const MediaProbeResult& probe() const { return probe_; }
  // 最近一次尝试的探测结果（失败时也可读，用于 UI 错误分类）。
  const MediaProbeResult& last_attempt() const { return last_attempt_; }
  const DecodedFrameInfo& first_frame() const { return first_frame_; }
  const FrameIndex& index() const { return index_; }
  VideoDecoder& decoder() { return *decoder_; }
  const std::string& path_utf8() const { return probe_.info.path_utf8; }
  bool IsVfrSuspect() const { return probe_.info.vfr.suspect; }

 private:
  MediaProbeResult probe_;
  MediaProbeResult last_attempt_;
  FrameIndex index_;
  std::unique_ptr<VideoDecoder> decoder_;
  DecodedFrameInfo first_frame_;
  bool open_ = false;
};

}  // namespace lapian::media
