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

  // 只有探测/解码都成功才替换旧会话。
  probe_ = last_attempt_;
  decoder_ = std::move(decoder);
  first_frame_ = first_frame;
  open_ = true;
  return true;
}

void MediaSession::Close() {
  decoder_.reset();
  probe_ = MediaProbeResult{};
  last_attempt_ = MediaProbeResult{};
  first_frame_ = DecodedFrameInfo{};
  open_ = false;
}

}  // namespace lapian::media
