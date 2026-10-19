#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "media/FrameIndex.h"
#include "media/VideoDecoder.h"

struct ID3D11Device;

namespace lapian::media {

// T2.3 精确 seek：索引可靠时 seek 到前关键帧再解码到目标帧；
// 索引不可靠时降级为从头解码计数（慢但帧号精确）。
class SeekEngine {
 public:
  SeekEngine() = default;
  ~SeekEngine() = default;

  SeekEngine(const SeekEngine&) = delete;
  SeekEngine& operator=(const SeekEngine&) = delete;

  bool Open(const std::string& utf8_path, int video_stream_index, ID3D11Device* device,
            bool prefer_hardware, std::string* error);
  void Close();
  bool IsOpen() const { return decoder_.IsOpen(); }

  // frame 为 0 基显示帧号；成功时 out 是目标帧，error 为空。
  bool SeekToFrame(std::int64_t frame, const FrameIndex& index, DecodedFrameInfo* out,
                   std::string* error);

  // T2.4 的取消入口；当前同步调用，Cancel 可从其他线程置位。
  void Cancel() { cancel_.store(true); }
  std::uint64_t generation() const { return generation_; }

 private:
  bool DecodeCountFallback(std::int64_t frame, const FrameIndex& index, DecodedFrameInfo* out,
                           std::string* error);
  bool DecodeFromKeyframe(std::int64_t keyframe, std::int64_t keyframe_pts, std::int64_t frame,
                          DecodedFrameInfo* out, std::string* error);

  VideoDecoder decoder_;
  std::atomic<bool> cancel_{false};
  std::uint64_t generation_ = 0;
};

}  // namespace lapian::media
