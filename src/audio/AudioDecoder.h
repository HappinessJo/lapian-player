#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio/AudioFormat.h"

struct AVFormatContext;

namespace lapian::audio {

struct AudioStreamInfo {
  int stream_index = -1;
  std::string codec;
  int source_sample_rate = 0;
  int source_channels = 0;
  std::int64_t duration_us = 0;
  std::int64_t start_time_us = 0;
};

struct AudioChunk {
  std::vector<float> samples;  // 交错 float32
  std::size_t frames = 0;
  double pts_seconds = 0.0;  // 本块第一个样本的媒体时间
};

enum class AudioDecodeResult {
  kFrame,
  kEnd,
  kError,
};

// 第一条音轨的独立解码器：不共享视频的 AVFormatContext，只在一个音频线程内使用。
// 输出统一为交错 float32；采样率/声道由构造时传入的设备格式决定。
class AudioDecoder {
 public:
  AudioDecoder();
  ~AudioDecoder();

  AudioDecoder(const AudioDecoder&) = delete;
  AudioDecoder& operator=(const AudioDecoder&) = delete;

  // stream_index < 0 时选第一条音频流；显式索引无效时也回退第一条。
  bool Open(const std::string& utf8_path, int stream_index, const AudioFormat& output_format,
            std::string* error);
  // T8.6：使用调用方拥有的可增长 AVFormatContext；不关闭 context。
  bool OpenWithFormat(AVFormatContext* format, int stream_index,
                      const AudioFormat& output_format, std::string* error);
  void Close();
  bool IsOpen() const;

  const AudioStreamInfo& info() const { return info_; }

  // 定位到媒体秒；之后 DecodeChunk 返回的样本从目标时刻开始（样本级裁剪）。
  bool SeekTo(double seconds, std::string* error);

  // 最多输出 max_frames 帧；返回 kEnd 表示解码结束，kError 时 error 非空。
  AudioDecodeResult DecodeChunk(std::size_t max_frames, AudioChunk* chunk, std::string* error);

  // T3.3：保持文件/解码器打开，只重建 swresample 输出格式（变速用）。
  // 调用方应在之后 SeekTo 到当前媒体位置，再继续 DecodeChunk。
  bool SetOutputFormat(const AudioFormat& output_format, std::string* error);

  bool ended() const { return ended_; }

 private:
  bool OpenWithFormatInternal(AVFormatContext* format, bool take_ownership,
                              int stream_index, const AudioFormat& output_format,
                              std::string* error);

  struct Impl;
  std::unique_ptr<Impl> impl_;
  AudioStreamInfo info_;
  bool ended_ = false;
};

}  // namespace lapian::audio
