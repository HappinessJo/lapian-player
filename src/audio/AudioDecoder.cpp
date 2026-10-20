#include "audio/AudioDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace lapian::audio {
namespace {

constexpr std::int64_t kNoPts = std::numeric_limits<std::int64_t>::min();

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

}  // namespace

struct AudioDecoder::Impl {
  AVFormatContext* format = nullptr;
  AVCodecContext* codec = nullptr;
  AVStream* stream = nullptr;
  AVPacket* packet = nullptr;
  AVFrame* frame = nullptr;
  SwrContext* swr = nullptr;

  int stream_index = -1;
  AudioFormat output_format;
  AVChannelLayout input_layout{};
  bool has_input_layout = false;
  std::vector<float> output;
  std::int64_t discard_until_us = 0;
  std::int64_t next_pts_us = kNoPts;

  bool packet_valid = false;
  bool demux_eof = false;
  bool drain_sent = false;
  bool resampler_drained = false;

  bool IsOpen() const { return format != nullptr && codec != nullptr && stream != nullptr; }

  bool ConfigureOutput(const AudioFormat& desired, std::string* error) {
    if (!IsOpen() || !has_input_layout) {
      *error = "音频解码器未打开";
      return false;
    }
    if (!desired.valid()) {
      *error = "音频输出格式无效";
      return false;
    }

    swr_free(&swr);
    AVChannelLayout output_layout{};
    av_channel_layout_default(&output_layout, desired.channels);
    const int ret = swr_alloc_set_opts2(
        &swr, &output_layout, AV_SAMPLE_FMT_FLT, desired.sample_rate, &input_layout,
        codec->sample_fmt, codec->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&output_layout);
    if (ret < 0 || swr == nullptr) {
      *error = "创建音频重采样器失败: " + AvErrorText(ret);
      return false;
    }
    if (swr_init(swr) < 0) {
      *error = "初始化音频重采样器失败";
      swr_free(&swr);
      return false;
    }
    output_format = desired;
    return true;
  }

  void Close() {
    if (swr != nullptr) {
      swr_free(&swr);
    }
    if (has_input_layout) {
      av_channel_layout_uninit(&input_layout);
      has_input_layout = false;
    }
    if (frame != nullptr) {
      av_frame_free(&frame);
    }
    if (packet != nullptr) {
      av_packet_free(&packet);
    }
    if (codec != nullptr) {
      avcodec_free_context(&codec);
    }
    if (format != nullptr) {
      avformat_close_input(&format);
    }
    stream = nullptr;
    stream_index = -1;
    packet_valid = false;
    demux_eof = false;
    drain_sent = false;
    resampler_drained = false;
    next_pts_us = kNoPts;
    discard_until_us = 0;
    output.clear();
  }

  AudioDecodeResult DrainResampler(std::size_t max_frames, AudioChunk* chunk, std::string* error) {
    if (resampler_drained) {
      return AudioDecodeResult::kEnd;
    }
    if (max_frames == 0) {
      *error = "音频输出帧数上限为 0";
      return AudioDecodeResult::kError;
    }
    const int channels = output_format.channels;
    output.resize(max_frames * static_cast<std::size_t>(channels));
    std::uint8_t* out_data[1] = {reinterpret_cast<std::uint8_t*>(output.data())};
    const int converted =
        swr_convert(swr, out_data, static_cast<int>(max_frames), nullptr, 0);
    if (converted < 0) {
      *error = "刷新重采样器失败: " + AvErrorText(converted);
      return AudioDecodeResult::kError;
    }
    if (converted == 0) {
      resampler_drained = true;
      return AudioDecodeResult::kEnd;
    }

    chunk->frames = static_cast<std::size_t>(converted);
    chunk->samples.assign(output.begin(),
                          output.begin() + static_cast<std::ptrdiff_t>(chunk->frames *
                                                                      static_cast<std::size_t>(channels)));
    chunk->pts_seconds =
        next_pts_us == kNoPts ? 0.0 : static_cast<double>(next_pts_us) / 1.0e6;
    if (next_pts_us != kNoPts) {
      next_pts_us += static_cast<std::int64_t>(
          std::llround(static_cast<double>(converted) * 1.0e6 /
                       static_cast<double>(output_format.sample_rate)));
    }
    return AudioDecodeResult::kFrame;
  }

  AudioDecodeResult ConvertFrame(std::size_t max_frames, AudioChunk* chunk, std::string* error) {
    const int source_rate = frame->sample_rate > 0 ? frame->sample_rate : codec->sample_rate;
    const int source_samples = frame->nb_samples;
    if (source_rate <= 0 || source_samples <= 0) {
      av_frame_unref(frame);
      return AudioDecodeResult::kFrame;  // 不产出样本，继续取下一帧
    }

    std::int64_t timestamp = frame->best_effort_timestamp;
    if (timestamp == AV_NOPTS_VALUE) {
      timestamp = frame->pts;
    }
    std::int64_t frame_start_us = 0;
    if (timestamp != AV_NOPTS_VALUE) {
      frame_start_us = av_rescale_q(timestamp, stream->time_base, AV_TIME_BASE_Q);
    } else if (next_pts_us != kNoPts) {
      frame_start_us = next_pts_us;
    }

    int skip = 0;
    if (discard_until_us > frame_start_us) {
      const double skip_samples = static_cast<double>(discard_until_us - frame_start_us) *
                                  static_cast<double>(source_rate) / 1.0e6;
      skip = static_cast<int>(std::ceil(skip_samples));
      if (skip >= source_samples) {
        next_pts_us = frame_start_us + static_cast<std::int64_t>(std::llround(
                                           static_cast<double>(source_samples) * 1.0e6 /
                                           static_cast<double>(source_rate)));
        av_frame_unref(frame);
        return AudioDecodeResult::kFrame;  // 整帧都在目标之前
      }
    }

    const AVSampleFormat sample_format = static_cast<AVSampleFormat>(frame->format);
    const int planes = av_sample_fmt_is_planar(sample_format) > 0
                           ? frame->ch_layout.nb_channels
                           : 1;
    const int bytes_per_sample = av_get_bytes_per_sample(sample_format);
    if (planes <= 0 || bytes_per_sample <= 0 || frame->extended_data == nullptr) {
      *error = "音频帧样本格式无效";
      av_frame_unref(frame);
      return AudioDecodeResult::kError;
    }

    const std::uint8_t* input_data[AV_NUM_DATA_POINTERS] = {};
    for (int plane = 0; plane < planes && plane < AV_NUM_DATA_POINTERS; ++plane) {
      input_data[plane] = frame->extended_data[plane] +
                          static_cast<std::size_t>(skip) *
                              static_cast<std::size_t>(bytes_per_sample);
    }

    const int channels = output_format.channels;
    output.resize(max_frames * static_cast<std::size_t>(channels));
    std::uint8_t* out_data[1] = {reinterpret_cast<std::uint8_t*>(output.data())};
    const int converted = swr_convert(swr, out_data, static_cast<int>(max_frames), input_data,
                                      source_samples - skip);
    if (converted < 0) {
      *error = "重采样失败: " + AvErrorText(converted);
      av_frame_unref(frame);
      return AudioDecodeResult::kError;
    }

    if (converted > 0) {
      chunk->frames = static_cast<std::size_t>(converted);
      chunk->samples.assign(
          output.begin(),
          output.begin() + static_cast<std::ptrdiff_t>(chunk->frames *
                                                      static_cast<std::size_t>(channels)));
      chunk->pts_seconds = (static_cast<double>(frame_start_us) +
                            static_cast<double>(skip) * 1.0e6 /
                                static_cast<double>(source_rate)) /
                           1.0e6;
      discard_until_us = 0;
    }

    next_pts_us = frame_start_us + static_cast<std::int64_t>(std::llround(
                                       static_cast<double>(source_samples) * 1.0e6 /
                                       static_cast<double>(source_rate)));
    av_frame_unref(frame);
    return AudioDecodeResult::kFrame;
  }

  AudioDecodeResult DecodeChunk(std::size_t max_frames, AudioChunk* chunk, std::string* error) {
    if (!IsOpen()) {
      *error = "音频解码器未打开";
      return AudioDecodeResult::kError;
    }
    if (chunk == nullptr || max_frames == 0) {
      *error = "音频输出参数无效";
      return AudioDecodeResult::kError;
    }

    chunk->samples.clear();
    chunk->frames = 0;
    chunk->pts_seconds = 0.0;

    // 上限防御：一次最多输出 16384 帧，避免异常容器把内存拉爆。
    max_frames = std::min<std::size_t>(max_frames, 16384);

    for (int guard = 0; guard < 1'000'000; ++guard) {
      const int receive = avcodec_receive_frame(codec, frame);
      if (receive == 0) {
        const AudioDecodeResult result = ConvertFrame(max_frames, chunk, error);
        if (result == AudioDecodeResult::kError) {
          return result;
        }
        if (chunk->frames > 0) {
          return AudioDecodeResult::kFrame;
        }
        continue;
      }
      if (receive == AVERROR_EOF) {
        return DrainResampler(max_frames, chunk, error);
      }
      if (receive != AVERROR(EAGAIN)) {
        *error = "读取解码帧失败: " + AvErrorText(receive);
        return AudioDecodeResult::kError;
      }

      if (demux_eof && drain_sent) {
        return DrainResampler(max_frames, chunk, error);
      }

      if (!packet_valid) {
        if (demux_eof) {
          const int sent = avcodec_send_packet(codec, nullptr);
          if (sent < 0 && sent != AVERROR_EOF) {
            *error = "刷新音频解码器失败: " + AvErrorText(sent);
            return AudioDecodeResult::kError;
          }
          drain_sent = true;
          continue;
        }

        const int read = av_read_frame(format, packet);
        if (read < 0) {
          if (read != AVERROR_EOF) {
            *error = "读取音频包失败: " + AvErrorText(read);
            return AudioDecodeResult::kError;
          }
          demux_eof = true;
          continue;
        }
        if (packet->stream_index != stream_index) {
          av_packet_unref(packet);
          continue;
        }
        packet_valid = true;
      }

      const int sent = avcodec_send_packet(codec, packet);
      if (sent == 0) {
        av_packet_unref(packet);
        packet_valid = false;
        continue;
      }
      if (sent == AVERROR(EAGAIN)) {
        continue;  // 保留 packet，先收帧
      }
      if (sent == AVERROR_EOF) {
        av_packet_unref(packet);
        packet_valid = false;
        drain_sent = true;
        continue;
      }
      *error = "送入音频包失败: " + AvErrorText(sent);
      return AudioDecodeResult::kError;
    }

    *error = "音频解码循环超过上限";
    return AudioDecodeResult::kError;
  }

  bool SeekTo(double seconds, std::string* error) {
    if (!IsOpen()) {
      *error = "音频解码器未打开";
      return false;
    }
    const std::int64_t target_us = std::max<std::int64_t>(
        0, static_cast<std::int64_t>(std::llround(seconds * 1.0e6)));
    const std::int64_t preroll_us = 200'000;
    const std::int64_t seek_us = std::max<std::int64_t>(0, target_us - preroll_us);
    const std::int64_t seek_ts = av_rescale_q(seek_us, AV_TIME_BASE_Q, stream->time_base);

    int ret = av_seek_frame(format, stream_index, seek_ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
      ret = av_seek_frame(format, -1, seek_us, AVSEEK_FLAG_BACKWARD);
    }
    if (ret < 0) {
      *error = "音频 seek 失败: " + AvErrorText(ret);
      return false;
    }

    avcodec_flush_buffers(codec);
    av_packet_unref(packet);
    av_frame_unref(frame);
    packet_valid = false;
    demux_eof = false;
    drain_sent = false;
    resampler_drained = false;
    next_pts_us = kNoPts;
    discard_until_us = target_us;

    // 冲掉 seek 前残留在重采样器里的样本，并回到干净状态。
    if (swr != nullptr) {
      swr_convert(swr, nullptr, 0, nullptr, 0);
      swr_init(swr);
    }
    return true;
  }
};

AudioDecoder::AudioDecoder() : impl_(std::make_unique<Impl>()) {}

AudioDecoder::~AudioDecoder() {
  if (impl_ != nullptr) {
    impl_->Close();
  }
}

bool AudioDecoder::Open(const std::string& utf8_path, int stream_index,
                        const AudioFormat& output_format, std::string* error) {
  if (error == nullptr) {
    return false;
  }
  error->clear();
  impl_->Close();
  ended_ = false;
  info_ = AudioStreamInfo{};

  if (!output_format.valid()) {
    *error = "音频输出格式无效";
    return false;
  }
  impl_->output_format = output_format;

  int ret = avformat_open_input(&impl_->format, utf8_path.c_str(), nullptr, nullptr);
  if (ret < 0) {
    *error = "打开音频文件失败: " + AvErrorText(ret);
    impl_->Close();
    return false;
  }
  ret = avformat_find_stream_info(impl_->format, nullptr);
  if (ret < 0) {
    *error = "读取音频流信息失败: " + AvErrorText(ret);
    impl_->Close();
    return false;
  }

  int selected = -1;
  if (stream_index >= 0 && stream_index < static_cast<int>(impl_->format->nb_streams)) {
    const AVStream* candidate = impl_->format->streams[stream_index];
    if (candidate != nullptr && candidate->codecpar != nullptr &&
        candidate->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      selected = stream_index;
    }
  }
  if (selected < 0) {
    for (unsigned int i = 0; i < impl_->format->nb_streams; ++i) {
      const AVStream* candidate = impl_->format->streams[i];
      if (candidate != nullptr && candidate->codecpar != nullptr &&
          candidate->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        selected = static_cast<int>(i);
        break;
      }
    }
  }
  if (selected < 0) {
    *error = "文件没有音频流";
    impl_->Close();
    return false;
  }

  impl_->stream_index = selected;
  impl_->stream = impl_->format->streams[selected];
  const AVCodecParameters* parameters = impl_->stream->codecpar;
  const AVCodec* decoder = avcodec_find_decoder(parameters->codec_id);
  if (decoder == nullptr) {
    *error = "没有可用的音频解码器";
    impl_->Close();
    return false;
  }

  impl_->codec = avcodec_alloc_context3(decoder);
  if (impl_->codec == nullptr) {
    *error = "创建音频解码器失败";
    impl_->Close();
    return false;
  }
  ret = avcodec_parameters_to_context(impl_->codec, parameters);
  if (ret < 0) {
    *error = "复制音频解码参数失败: " + AvErrorText(ret);
    impl_->Close();
    return false;
  }
  impl_->codec->pkt_timebase = impl_->stream->time_base;
  ret = avcodec_open2(impl_->codec, decoder, nullptr);
  if (ret < 0) {
    *error = "打开音频解码器失败: " + AvErrorText(ret);
    impl_->Close();
    return false;
  }

  AVChannelLayout input_layout{};
  if (impl_->codec->ch_layout.nb_channels > 0) {
    av_channel_layout_copy(&input_layout, &impl_->codec->ch_layout);
  } else {
    av_channel_layout_default(&input_layout, parameters->ch_layout.nb_channels > 0
                                                 ? parameters->ch_layout.nb_channels
                                                 : 1);
  }
  impl_->input_layout = input_layout;
  impl_->has_input_layout = true;
  if (!impl_->ConfigureOutput(output_format, error)) {
    impl_->Close();
    return false;
  }

  impl_->packet = av_packet_alloc();
  impl_->frame = av_frame_alloc();
  if (impl_->packet == nullptr || impl_->frame == nullptr) {
    *error = "分配音频解码缓冲失败";
    impl_->Close();
    return false;
  }

  info_.stream_index = selected;
  const char* codec_name = avcodec_get_name(parameters->codec_id);
  info_.codec = codec_name != nullptr ? codec_name : "unknown";
  info_.source_sample_rate = impl_->codec->sample_rate;
  info_.source_channels = impl_->codec->ch_layout.nb_channels;
  if (impl_->stream->duration != AV_NOPTS_VALUE && impl_->stream->duration > 0) {
    info_.duration_us =
        av_rescale_q(impl_->stream->duration, impl_->stream->time_base, AV_TIME_BASE_Q);
  } else if (impl_->format->duration != AV_NOPTS_VALUE && impl_->format->duration > 0) {
    info_.duration_us = impl_->format->duration;
  }
  if (impl_->stream->start_time != AV_NOPTS_VALUE) {
    info_.start_time_us =
        av_rescale_q(impl_->stream->start_time, impl_->stream->time_base, AV_TIME_BASE_Q);
  }

  impl_->next_pts_us = kNoPts;
  impl_->discard_until_us = 0;
  return true;
}

void AudioDecoder::Close() {
  if (impl_ != nullptr) {
    impl_->Close();
  }
  info_ = AudioStreamInfo{};
  ended_ = false;
}

bool AudioDecoder::IsOpen() const { return impl_ != nullptr && impl_->IsOpen(); }

bool AudioDecoder::SeekTo(double seconds, std::string* error) {
  if (error == nullptr) {
    return false;
  }
  error->clear();
  ended_ = false;
  return impl_->SeekTo(seconds, error);
}

AudioDecodeResult AudioDecoder::DecodeChunk(std::size_t max_frames, AudioChunk* chunk,
                                            std::string* error) {
  if (error == nullptr) {
    return AudioDecodeResult::kError;
  }
  error->clear();
  const AudioDecodeResult result = impl_->DecodeChunk(max_frames, chunk, error);
  if (result == AudioDecodeResult::kEnd) {
    ended_ = true;
  }
  return result;
}

bool AudioDecoder::SetOutputFormat(const AudioFormat& output_format, std::string* error) {
  if (error == nullptr) {
    return false;
  }
  error->clear();
  if (impl_ == nullptr || !impl_->IsOpen()) {
    *error = "音频解码器未打开";
    return false;
  }
  if (!impl_->ConfigureOutput(output_format, error)) {
    return false;
  }
  av_packet_unref(impl_->packet);
  av_frame_unref(impl_->frame);
  impl_->packet_valid = false;
  impl_->demux_eof = false;
  impl_->drain_sent = false;
  impl_->resampler_drained = false;
  impl_->next_pts_us = kNoPts;
  impl_->discard_until_us = 0;
  ended_ = false;
  return true;
}

}  // namespace lapian::audio
