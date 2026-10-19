#include "media/SeekEngine.h"

#include <chrono>
#include <cstdlib>
#include <limits>

namespace lapian::media {

bool SeekEngine::Open(const std::string& utf8_path, int video_stream_index, ID3D11Device* device,
                      bool prefer_hardware, std::string* error) {
  if (error == nullptr) {
    return false;
  }
  error->clear();
  Close();
  if (!decoder_.OpenWithD3D11Device(utf8_path, video_stream_index, device, prefer_hardware,
                                    error)) {
    return false;
  }
  generation_ = 0;
  cancel_.store(false);
  return true;
}

void SeekEngine::Close() {
  cancel_.store(true);
  decoder_.Close();
}

bool SeekEngine::DecodeFromKeyframe(std::int64_t keyframe, std::int64_t keyframe_pts,
                                    std::int64_t frame, DecodedFrameInfo* out,
                                    std::string* error) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);

  for (std::int64_t index = keyframe; index <= frame; ++index) {
    if (cancel_.load()) {
      *error = "seek 已取消";
      return false;
    }
    if (std::chrono::steady_clock::now() > deadline) {
      *error = "seek 解码超时";
      return false;
    }
    DecodedFrameInfo info;
    if (!decoder_.DecodeNextFrame(&info, error)) {
      return false;
    }
    if (index == keyframe) {
      const std::int64_t tolerance = info.duration_us > 0 ? info.duration_us / 2 : 1000;
      const std::int64_t deviation =
          info.pts_us > keyframe_pts ? info.pts_us - keyframe_pts : keyframe_pts - info.pts_us;
      if (deviation > tolerance) {
        *error = "seek 落点偏离关键帧";
        return false;
      }
    }
    if (index == frame) {
      *out = info;
      return true;
    }
  }
  *error = "seek 解码未到目标帧";
  return false;
}

bool SeekEngine::DecodeCountFallback(std::int64_t frame, const FrameIndex& index,
                                     DecodedFrameInfo* out, std::string* error) {
  const std::int64_t first_pts = index.PtsOfFrame(0);
  if (!decoder_.SeekToPts(first_pts, error)) {
    return false;
  }
  return DecodeFromKeyframe(0, first_pts, frame, out, error);
}

bool SeekEngine::SeekToFrame(std::int64_t frame, const FrameIndex& index, DecodedFrameInfo* out,
                             std::string* error) {
  if (error == nullptr || out == nullptr) {
    return false;
  }
  error->clear();
  if (!decoder_.IsOpen()) {
    *error = "seek 解码器未打开";
    return false;
  }
  if (!index.IsReady() || index.empty()) {
    *error = "PTS 索引未就绪";
    return false;
  }
  if (frame < 0 || static_cast<std::size_t>(frame) >= index.size()) {
    *error = "目标帧越界";
    return false;
  }

  ++generation_;
  cancel_.store(false);
  const std::int64_t target_pts = index.PtsOfFrame(frame);

  if (index.IsReliable()) {
    std::int64_t keyframe = index.KeyframeAtOrBefore(target_pts);
    if (keyframe < 0) {
      keyframe = 0;
    }
    const std::int64_t keyframe_pts = index.PtsOfFrame(keyframe);
    if (decoder_.SeekToPts(keyframe_pts, error)) {
      if (DecodeFromKeyframe(keyframe, keyframe_pts, frame, out, error)) {
        return true;
      }
    }
    if (cancel_.load()) {
      *error = "seek 已取消";
      return false;
    }
    // seek 落点/索引不可信时，降级从头解码计数；慢但帧号精确。
  }

  if (DecodeCountFallback(frame, index, out, error)) {
    return true;
  }
  return false;
}

}  // namespace lapian::media
