#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <string>

namespace lapian::media {

// 与 FFmpeg 的 AV_NOPTS_VALUE 语义一致；公开头文件不依赖 FFmpeg 头。
inline constexpr std::int64_t kNoTimestamp = std::numeric_limits<std::int64_t>::min();

// 有理数帧率/时间基。num 与 den 都为正数时才有效。
struct Rational {
  int num = 0;
  int den = 0;

  bool valid() const { return num > 0 && den > 0; }

  double ToDouble() const {
    return valid() ? static_cast<double>(num) / static_cast<double>(den) : 0.0;
  }
};

// 第一条视频流。frame_rate 是展示用首选值（avg 优先、缺失回退 r），
// average/real 原样保留，供 T2.1 的帧映射与 VFR 判断使用。
struct VideoStreamInfo {
  int stream_index = -1;
  std::string codec;
  int width = 0;
  int height = 0;
  std::string pixel_format;
  Rational average_frame_rate;
  Rational real_frame_rate;
  Rational frame_rate;
  Rational time_base;
  std::int64_t start_time = kNoTimestamp;  // 视频流 time_base 单位
  std::int64_t duration = kNoTimestamp;    // 视频流 time_base 单位
  std::int64_t nb_frames = 0;              // 仅容器提示；0 表示未知
  bool nb_frames_known = false;
};

// 第一条音频流；没有音轨时 has_audio=false，其余字段保持默认值。
struct AudioStreamInfo {
  int stream_index = -1;
  std::string codec;
  int sample_rate = 0;
  int channels = 0;
};

// 容器 display matrix。matrix 原样保存 16.16 / 2.30 fixed-point 值，
// counterclockwise_degrees 是 FFmpeg 约定的逆时针角度，范围 [0,360)。
struct DisplayRotation {
  bool has_matrix = false;
  std::array<std::int32_t, 9> matrix{};
  int counterclockwise_degrees = 0;
};

// VFR 初判结果。T1.1 只采样有限包；T2.2 再做全片 PTS 索引与校验。
struct VfrInfo {
  bool checked = false;  // 是否采到足够间隔做判断
  bool suspect = false;  // 是否初判为可变帧率
  int sampled_frames = 0;
  double median_interval_seconds = 0.0;
  double deviation_ratio = 0.0;  // 偏离中位数 >5% 的间隔比例
};

struct MediaInfo {
  std::string path_utf8;
  std::string format_name;
  std::string format_long_name;
  std::int64_t duration_us = 0;  // 容器时长；0 表示未知
  bool duration_reliable = false;
  VideoStreamInfo video;
  bool has_audio = false;
  AudioStreamInfo audio;
  // T3.4：第一条音轨首包 PTS（含 priming）相对首帧视频 PTS 的全局偏移。
  // 正值表示音频时间轴晚于视频；波形映射与音频起播共用这个值。
  std::int64_t av_offset_us = 0;
  bool av_offset_estimated = false;
  DisplayRotation rotation;
  VfrInfo vfr;
};

}  // namespace lapian::media
