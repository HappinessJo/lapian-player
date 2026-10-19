#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct AVFrame;

namespace lapian::media {

// 解码出的一帧元数据；av_frame 由 VideoDecoder 持有，只在下次 Decode/Close 前有效。
struct DecodedFrameInfo {
  int width = 0;
  int height = 0;
  int pixel_format = -1;  // AVPixelFormat
  std::string pixel_format_name;
  int bit_depth = 8;
  std::int64_t pts = 0;
  std::int64_t duration = 0;
  bool key_frame = false;
  bool hardware = false;
  const AVFrame* av_frame = nullptr;
};

// T1.2 视频解码服务：D3D11VA 硬解优先，失败回退软解。
// 非线程安全：一个实例只在一个解码线程内使用。
class VideoDecoder {
 public:
  VideoDecoder();
  ~VideoDecoder();

  VideoDecoder(const VideoDecoder&) = delete;
  VideoDecoder& operator=(const VideoDecoder&) = delete;

  // video_stream_index < 0 时选第一条视频流。失败返回 false + 可读错误。
  bool Open(const std::string& utf8_path, int video_stream_index, bool prefer_hardware,
            std::string* error);
  void Close();

  bool DecodeFirstFrame(DecodedFrameInfo* info, std::string* error);
  bool DecodeNextFrame(DecodedFrameInfo* info, std::string* error);

  bool IsOpen() const;
  bool HardwareActive() const;
  int BitDepth() const;
  bool IsHighBitDepth() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lapian::media
