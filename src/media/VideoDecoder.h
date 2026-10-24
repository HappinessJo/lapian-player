#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct AVFormatContext;
struct AVFrame;
struct ID3D11Device;

namespace lapian::media {

// 解码出的一帧元数据；av_frame 由 VideoDecoder 持有，只在下次 Decode/Close 前有效。
struct DecodedFrameInfo {
  int width = 0;
  int height = 0;
  int pixel_format = -1;  // AVPixelFormat
  std::string pixel_format_name;
  int bit_depth = 8;
  std::int64_t pts = 0;         // 流 time_base
  std::int64_t duration = 0;    // 流 time_base
  std::int64_t pts_us = 0;      // AV_TIME_BASE 微秒
  std::int64_t duration_us = 0; // AV_TIME_BASE 微秒
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
  // T1.3：硬解必须和渲染器共用同一个 D3D11 设备，否则纹理不能跨设备拷贝。
  // DP-1：extra_hw_frames >= 0 时覆盖默认表面池额外帧数（-1 = 按分辨率默认）；
  // 预览不缓存硬件帧，用小池避免占显存。
  bool OpenWithD3D11Device(const std::string& utf8_path, int video_stream_index,
                           ID3D11Device* d3d11_device, bool prefer_hardware,
                           std::string* error, int extra_hw_frames = -1);
  // T8.6：使用调用方拥有的（可增长读层）AVFormatContext；Decoder 不关闭它。
  // 调用方必须保证 context 的生命周期长于本对象，且同一时刻只有一个线程使用。
  bool OpenWithFormat(AVFormatContext* format, int video_stream_index,
                      ID3D11Device* d3d11_device, bool prefer_hardware,
                      std::string* error);
  void Close();

  bool DecodeFirstFrame(DecodedFrameInfo* info, std::string* error);
  bool DecodeNextFrame(DecodedFrameInfo* info, std::string* error);
  // T2.3：seek 到目标 PTS 之前最近关键帧；之后用 DecodeNextFrame 向前解码。
  bool SeekToPts(std::int64_t pts_us, std::string* error);
  // T8.10：精确 seek 到“已知关键帧 PTS”。fMP4（DASH）用 av_seek_frame 会落到
  // 前一个关键帧（实测每个 GOP 早一格），这里用 avformat_seek_file 的
  // min_ts=ts=max_ts=关键帧 PTS 约束落点；失败返回 false（调用方退回 SeekToPts）。
  bool SeekToKeyframePts(std::int64_t pts_us, std::string* error);

  bool IsOpen() const;
  bool HardwareActive() const;
  int BitDepth() const;
  bool IsHighBitDepth() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lapian::media
