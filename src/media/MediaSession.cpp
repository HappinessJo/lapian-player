#include "media/MediaSession.h"

#include <memory>
#include <string>
#include <utility>

namespace lapian::media {

MediaSession::MediaSession() = default;

MediaSession::~MediaSession() = default;

bool MediaSession::Open(const std::string& utf8_path, ID3D11Device* device, std::string* error) {
  if (error == nullptr) {
    return false;
  }
  error->clear();

  last_attempt_ = ProbeMedia(utf8_path);
  if (!last_attempt_.ok) {
    *error = last_attempt_.error_detail.empty() ? "无法打开文件" : last_attempt_.error_detail;
    return false;
  }

  auto decoder = std::make_unique<VideoDecoder>();
  if (!decoder->OpenWithD3D11Device(utf8_path, last_attempt_.info.video.stream_index, device,
                                    true, error)) {
    return false;
  }

  DecodedFrameInfo first_frame;
  if (!decoder->DecodeFirstFrame(&first_frame, error)) {
    return false;
  }

  // 索引失败不阻塞打开；步进/输入在索引为空时禁用。
  FrameIndex index;
  std::string index_error;
  if (!index.BuildFromFile(utf8_path, last_attempt_.info.video.stream_index, &index_error)) {
    index.Reset();
  }

  // 只有探测/解码都成功才替换旧会话。
  probe_ = last_attempt_;
  decoder_ = std::move(decoder);
  first_frame_ = first_frame;
  index_ = std::move(index);
  open_ = true;
  return true;
}

bool MediaSession::SeekToFrame(std::int64_t frame, DecodedFrameInfo* out, std::string* error) {
  if (error == nullptr || out == nullptr) {
    return false;
  }
  error->clear();
  if (!open_ || decoder_ == nullptr) {
    *error = "会话未打开";
    return false;
  }
  if (!index_.IsReady() || index_.empty()) {
    *error = "PTS 索引未就绪";
    return false;
  }
  if (frame < 0 || static_cast<std::size_t>(frame) >= index_.size()) {
    *error = "目标帧越界";
    return false;
  }

  auto decode_range = [&](std::int64_t start_frame) -> bool {
    for (std::int64_t index = start_frame; index <= frame; ++index) {
      DecodedFrameInfo info;
      if (!decoder_->DecodeNextFrame(&info, error)) {
        return false;
      }
      if (index == frame) {
        *out = info;
        return true;
      }
    }
    *error = "seek 解码未到目标帧";
    return false;
  };

  if (index_.IsReliable()) {
    const std::int64_t target_pts = index_.PtsOfFrame(frame);
    std::int64_t keyframe = index_.KeyframeAtOrBefore(target_pts);
    if (keyframe < 0) {
      keyframe = 0;
    }
    if (decoder_->SeekToPts(index_.PtsOfFrame(keyframe), error) &&
        decode_range(keyframe)) {
      return true;
    }
  }

  // 索引不可靠或关键帧路径失败：从头解码计数。
  if (!decoder_->SeekToPts(index_.PtsOfFrame(0), error)) {
    return false;
  }
  return decode_range(0);
}

void MediaSession::Close() {
  decoder_.reset();
  index_.Reset();
  probe_ = MediaProbeResult{};
  last_attempt_ = MediaProbeResult{};
  first_frame_ = DecodedFrameInfo{};
  open_ = false;
}

}  // namespace lapian::media
