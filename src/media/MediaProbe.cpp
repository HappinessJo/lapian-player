#include "media/MediaProbe.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
// FFmpeg 7.1 的 common.h 在 /W4 下有 C4244 内联函数告警；不是我们的代码问题。
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/pixdesc.h>
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace lapian::media {
namespace {

struct FormatContextDeleter {
  void operator()(AVFormatContext* context) const {
    if (context != nullptr) {
      avformat_close_input(&context);
    }
  }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;

struct PacketDeleter {
  void operator()(AVPacket* packet) const { av_packet_free(&packet); }
};

using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

std::string AvErrorText(int error) {
  if (error >= 0) {
    return {};
  }
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  if (av_strerror(error, buffer, sizeof(buffer)) < 0) {
    return {};
  }
  return buffer;
}

MediaProbeError ClassifyOpenError(int error) {
  if (error == AVERROR(ENOENT)) {
    return MediaProbeError::kFileNotFound;
  }
  if (error == AVERROR(EACCES)) {
    return MediaProbeError::kPermissionDenied;
  }
  if (error == AVERROR_INVALIDDATA) {
    return MediaProbeError::kCorrupt;
  }
  if (error == AVERROR_DEMUXER_NOT_FOUND) {
    return MediaProbeError::kUnsupportedFormat;
  }
  return MediaProbeError::kOpenFailed;
}

MediaProbeResult MakeError(MediaProbeError error, std::string detail, double probe_ms) {
  MediaProbeResult result;
  result.ok = false;
  result.error = error;
  result.error_detail = std::move(detail);
  result.probe_ms = probe_ms;
  return result;
}

Rational ToRational(AVRational value) {
  Rational result;
  result.num = value.num;
  result.den = value.den;
  return result;
}

double ElapsedMs(std::chrono::steady_clock::time_point started) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
      .count();
}

// 只采样有限数量的视频帧；不读全片，避免把 T1.1 的打开耗时拖进 P2 预算。
// T3.4：顺带记录第一条视频/音频包的首个 PTS 与音频 skip_samples。
struct FirstPacketPts {
  bool video_seen = false;
  bool audio_seen = false;
  std::int64_t video_pts = 0;
  std::int64_t audio_pts = 0;
  int audio_skip_samples = 0;
};

VfrInfo DetectVfr(AVFormatContext* format, int video_index, int audio_index,
                  AVRational video_time_base, FirstPacketPts* first) {
  VfrInfo info;
  PacketPtr packet(av_packet_alloc());
  if (!packet) {
    return info;
  }

  constexpr int kMaxVideoFrames = 300;
  constexpr auto kSampleBudget = std::chrono::milliseconds(50);
  const auto started = std::chrono::steady_clock::now();
  std::vector<std::int64_t> pts_values;
  pts_values.reserve(kMaxVideoFrames);

  while (av_read_frame(format, packet.get()) >= 0) {
    if (packet->stream_index == video_index && packet->pts != AV_NOPTS_VALUE) {
      if (!first->video_seen) {
        first->video_seen = true;
        first->video_pts = packet->pts;
      }
      pts_values.push_back(packet->pts);
    } else if (audio_index >= 0 && packet->stream_index == audio_index &&
               packet->pts != AV_NOPTS_VALUE && !first->audio_seen) {
      first->audio_seen = true;
      first->audio_pts = packet->pts;
      size_t side_size = 0;
      const std::uint8_t* skip = av_packet_get_side_data(
          packet.get(), AV_PKT_DATA_SKIP_SAMPLES, &side_size);
      if (skip != nullptr && side_size >= sizeof(std::uint32_t)) {
        first->audio_skip_samples = static_cast<int>(AV_RL32(skip));
      }
    }
    av_packet_unref(packet.get());

    if (static_cast<int>(pts_values.size()) >= kMaxVideoFrames ||
        std::chrono::steady_clock::now() - started > kSampleBudget) {
      break;
    }
  }

  std::sort(pts_values.begin(), pts_values.end());
  pts_values.erase(std::unique(pts_values.begin(), pts_values.end()), pts_values.end());

  std::vector<double> intervals;
  intervals.reserve(pts_values.size() > 0 ? pts_values.size() - 1 : 0);
  const double seconds_per_tick = av_q2d(video_time_base);
  for (std::size_t i = 1; i < pts_values.size(); ++i) {
    const double interval =
        static_cast<double>(pts_values[i] - pts_values[i - 1]) * seconds_per_tick;
    if (std::isfinite(interval) && interval > 0.0) {
      intervals.push_back(interval);
    }
  }
  return ClassifyVfrIntervals(intervals);
}

void ReadRotation(const AVCodecParameters* parameters, DisplayRotation* rotation) {
  if (parameters == nullptr || parameters->coded_side_data == nullptr ||
      parameters->nb_coded_side_data <= 0) {
    return;
  }

  const AVPacketSideData* side_data = av_packet_side_data_get(
      parameters->coded_side_data, parameters->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
  if (side_data == nullptr || side_data->size < 9 * sizeof(std::int32_t)) {
    return;
  }

  std::memcpy(rotation->matrix.data(), side_data->data, 9 * sizeof(std::int32_t));
  rotation->has_matrix = true;

  const double degrees = av_display_rotation_get(rotation->matrix.data());
  if (std::isfinite(degrees)) {
    rotation->counterclockwise_degrees = NormalizeRotationDegrees(degrees);
  }
}

}  // namespace

const char* MediaProbeErrorName(MediaProbeError error) {
  switch (error) {
    case MediaProbeError::kNone:
      return "none";
    case MediaProbeError::kFileNotFound:
      return "file_not_found";
    case MediaProbeError::kPermissionDenied:
      return "permission_denied";
    case MediaProbeError::kUnsupportedFormat:
      return "unsupported_format";
    case MediaProbeError::kCorrupt:
      return "corrupt";
    case MediaProbeError::kNoVideoStream:
      return "no_video_stream";
    case MediaProbeError::kStreamInfoFailed:
      return "stream_info_failed";
    case MediaProbeError::kOpenFailed:
      return "open_failed";
  }
  return "unknown";
}

int NormalizeRotationDegrees(double degrees) {
  if (!std::isfinite(degrees)) {
    return 0;
  }
  double value = std::fmod(degrees, 360.0);
  if (value < 0.0) {
    value += 360.0;
  }
  int result = static_cast<int>(std::llround(value));
  if (result >= 360) {
    result -= 360;
  }
  return result;
}

VfrInfo ClassifyVfrIntervals(const std::vector<double>& intervals_seconds) {
  VfrInfo info;

  std::vector<double> valid;
  valid.reserve(intervals_seconds.size());
  for (double interval : intervals_seconds) {
    if (std::isfinite(interval) && interval > 0.0) {
      valid.push_back(interval);
    }
  }
  info.sampled_frames = static_cast<int>(valid.size());

  // 样本不足时宁可“未检查”，也不误报 VFR。
  if (valid.size() < 8) {
    return info;
  }

  std::sort(valid.begin(), valid.end());
  const std::size_t count = valid.size();
  const double median =
      (count % 2 == 1) ? valid[count / 2] : 0.5 * (valid[count / 2 - 1] + valid[count / 2]);

  std::size_t deviations = 0;
  for (double interval : valid) {
    if (std::fabs(interval - median) / median > 0.05) {
      ++deviations;
    }
  }

  info.checked = true;
  info.median_interval_seconds = median;
  info.deviation_ratio = static_cast<double>(deviations) / static_cast<double>(count);
  info.suspect = info.deviation_ratio > 0.05;
  return info;
}

MediaProbeResult ProbeMedia(const std::string& utf8_path) {
  const auto started = std::chrono::steady_clock::now();

  AVFormatContext* raw_format = nullptr;
  int ret = avformat_open_input(&raw_format, utf8_path.c_str(), nullptr, nullptr);
  if (ret < 0) {
    if (raw_format != nullptr) {
      avformat_close_input(&raw_format);
    }
    return MakeError(ClassifyOpenError(ret), AvErrorText(ret), ElapsedMs(started));
  }
  FormatContextPtr format(raw_format);

  ret = avformat_find_stream_info(format.get(), nullptr);
  if (ret < 0) {
    MediaProbeError error = ClassifyOpenError(ret);
    if (error == MediaProbeError::kOpenFailed) {
      error = MediaProbeError::kStreamInfoFailed;
    }
    return MakeError(error, AvErrorText(ret), ElapsedMs(started));
  }

  int video_index = -1;
  int audio_index = -1;
  for (unsigned int i = 0; i < format->nb_streams; ++i) {
    const AVCodecParameters* parameters = format->streams[i]->codecpar;
    if (parameters == nullptr) {
      continue;
    }
    if (video_index < 0 && parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_index = static_cast<int>(i);
    } else if (audio_index < 0 && parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
      audio_index = static_cast<int>(i);
    }
  }

  if (video_index < 0) {
    return MakeError(MediaProbeError::kNoVideoStream, "no video stream", ElapsedMs(started));
  }

  MediaProbeResult result;
  result.ok = true;
  result.info.path_utf8 = utf8_path;
  if (format->iformat != nullptr) {
    result.info.format_name = format->iformat->name != nullptr ? format->iformat->name : "";
    result.info.format_long_name =
        format->iformat->long_name != nullptr ? format->iformat->long_name : "";
  }
  if (format->duration != AV_NOPTS_VALUE && format->duration > 0) {
    result.info.duration_us = format->duration;
    result.info.duration_reliable = true;
  }

  AVStream* video_stream = format->streams[video_index];
  const AVCodecParameters* video_parameters = video_stream->codecpar;
  VideoStreamInfo& video = result.info.video;
  video.stream_index = video_index;
  const char* video_codec = avcodec_get_name(video_parameters->codec_id);
  video.codec = video_codec != nullptr ? video_codec : "unknown";

  const char* pixel_format = av_get_pix_fmt_name(static_cast<AVPixelFormat>(video_parameters->format));
  video.pixel_format = pixel_format != nullptr ? pixel_format : "unknown";
  // T6.5：优先用容器/编码参数里的位深；缺失时从像素格式描述推导。
  if (video_parameters->bits_per_raw_sample > 0) {
    video.bit_depth = video_parameters->bits_per_raw_sample;
  } else if (video_parameters->format != AV_PIX_FMT_NONE) {
    const AVPixFmtDescriptor* descriptor =
        av_pix_fmt_desc_get(static_cast<AVPixelFormat>(video_parameters->format));
    if (descriptor != nullptr && descriptor->comp[0].depth > 0) {
      video.bit_depth = descriptor->comp[0].depth;
    }
  }
  video.width = video_parameters->width;
  video.height = video_parameters->height;
  video.average_frame_rate = ToRational(video_stream->avg_frame_rate);
  video.real_frame_rate = ToRational(video_stream->r_frame_rate);
  video.frame_rate = video.average_frame_rate.valid() ? video.average_frame_rate
                                                     : video.real_frame_rate;
  if (!video.frame_rate.valid()) {
    video.frame_rate = ToRational(av_guess_frame_rate(format.get(), video_stream, nullptr));
  }
  video.time_base = ToRational(video_stream->time_base);
  video.start_time = video_stream->start_time;
  video.duration = video_stream->duration;
  if (video_stream->nb_frames > 0) {
    video.nb_frames = video_stream->nb_frames;
    video.nb_frames_known = true;
  }

  if (audio_index >= 0) {
    const AVCodecParameters* audio_parameters = format->streams[audio_index]->codecpar;
    result.info.has_audio = true;
    result.info.audio.stream_index = audio_index;
    const char* audio_codec = avcodec_get_name(audio_parameters->codec_id);
    result.info.audio.codec = audio_codec != nullptr ? audio_codec : "unknown";
    result.info.audio.sample_rate = audio_parameters->sample_rate;
    result.info.audio.channels = audio_parameters->ch_layout.nb_channels;
  }

  ReadRotation(video_parameters, &result.info.rotation);
  FirstPacketPts first;
  result.info.vfr = DetectVfr(format.get(), video_index, audio_index,
                              video_stream->time_base, &first);
  if (audio_index >= 0 && first.video_seen && first.audio_seen) {
    const AVStream* audio_stream = format->streams[audio_index];
    const std::int64_t video_us =
        av_rescale_q(first.video_pts, video_stream->time_base, AV_TIME_BASE_Q);
    const std::int64_t audio_us =
        av_rescale_q(first.audio_pts, audio_stream->time_base, AV_TIME_BASE_Q);
    const int sample_rate = result.info.audio.sample_rate;
    const std::int64_t priming_us =
        sample_rate > 0
            ? av_rescale(first.audio_skip_samples, 1'000'000, sample_rate)
            : 0;
    result.info.av_offset_us = audio_us + priming_us - video_us;
    result.info.av_offset_estimated = true;
  }
  result.probe_ms = ElapsedMs(started);
  return result;
}

}  // namespace lapian::media
