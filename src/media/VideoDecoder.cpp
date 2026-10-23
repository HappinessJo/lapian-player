#include "media/VideoDecoder.h"

#include <d3d11.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "platform/gfx/D3D11Context.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/buffer.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixdesc.h>
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace lapian::media {
namespace {

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

AVPixelFormat SelectHardwareFormat(AVCodecContext* context, const AVPixelFormat* formats) {
  (void)context;
  if (formats == nullptr) {
    return AV_PIX_FMT_NONE;
  }
  for (const AVPixelFormat* format = formats; *format != AV_PIX_FMT_NONE; ++format) {
    if (*format == AV_PIX_FMT_D3D11) {
      return *format;
    }
  }
  return formats[0];
}

int ReadBitDepth(const AVCodecParameters* parameters) {
  int depth = 8;
  if (parameters != nullptr) {
    const AVPixFmtDescriptor* description =
        av_pix_fmt_desc_get(static_cast<AVPixelFormat>(parameters->format));
    if (description != nullptr && description->comp[0].depth > 0) {
      depth = description->comp[0].depth;
    }
    if (parameters->bits_per_raw_sample > depth) {
      depth = parameters->bits_per_raw_sample;
    }
  }
  return depth;
}

struct FormatContextDeleter {
  void operator()(AVFormatContext* context) const {
    if (context != nullptr) {
      avformat_close_input(&context);
    }
  }
};

}  // namespace

struct VideoDecoder::Impl {
  AVFormatContext* format = nullptr;
  bool owns_format = true;
  AVCodecContext* decoder = nullptr;
  AVFrame* frame = nullptr;
  AVPacket* packet = nullptr;
  int video_stream = -1;
  int bit_depth = 8;
  bool hardware_active = false;
  bool eof_sent = false;
  bool packet_valid = false;
  std::string path;
  int requested_stream = -1;
  ID3D11Device* external_device = nullptr;

  ~Impl() { Close(); }

  void Close() {
    if (frame != nullptr) {
      av_frame_free(&frame);
    }
    if (packet != nullptr) {
      av_packet_free(&packet);
    }
    if (decoder != nullptr) {
      avcodec_free_context(&decoder);
    }
    if (format != nullptr && owns_format) {
      avformat_close_input(&format);
    }
    format = nullptr;
    owns_format = true;
    video_stream = -1;
    bit_depth = 8;
    hardware_active = false;
    eof_sent = false;
    packet_valid = false;
  }

  bool IsOpen() const { return format != nullptr && decoder != nullptr && frame != nullptr; }

  bool OpenInternal(const std::string& utf8_path, int stream_index, bool prefer_hardware,
                    ID3D11Device* shared_d3d11_device, std::string* error) {
    AVFormatContext* raw_format = nullptr;
    const int ret =
        avformat_open_input(&raw_format, utf8_path.c_str(), nullptr, nullptr);
    if (ret < 0) {
      *error = "打开文件失败: " + AvErrorText(ret);
      return false;
    }
    const int info_ret = avformat_find_stream_info(raw_format, nullptr);
    if (info_ret < 0) {
      *error = "读取流信息失败: " + AvErrorText(info_ret);
      avformat_close_input(&raw_format);
      return false;
    }
    if (OpenFormatInternal(raw_format, true, stream_index, prefer_hardware,
                           shared_d3d11_device, utf8_path, error)) {
      return true;
    }
    if (raw_format != nullptr) {
      avformat_close_input(&raw_format);
    }
    return false;
  }

  bool OpenFormatInternal(AVFormatContext* input_format, bool take_ownership,
                          int stream_index, bool prefer_hardware,
                          ID3D11Device* shared_d3d11_device,
                          const std::string& display_path, std::string* error) {
    if (input_format == nullptr) {
      *error = "媒体上下文为空";
      return false;
    }
    int selected = stream_index;
    if (selected < 0) {
      for (unsigned int i = 0; i < input_format->nb_streams; ++i) {
        if (input_format->streams[i]->codecpar != nullptr &&
            input_format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
          selected = static_cast<int>(i);
          break;
        }
      }
    }
    if (selected < 0 || selected >= static_cast<int>(input_format->nb_streams)) {
      *error = "找不到视频流";
      if (take_ownership) {
        avformat_close_input(&input_format);
      }
      return false;
    }

    AVStream* stream = input_format->streams[selected];
    AVCodecParameters* parameters = stream->codecpar;
    if (parameters == nullptr || parameters->codec_type != AVMEDIA_TYPE_VIDEO) {
      *error = "指定流不是视频流";
      return false;
    }
    const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
    if (codec == nullptr) {
      *error = "没有可用的视频解码器";
      return false;
    }

    auto create_context = [&](bool hardware) -> AVCodecContext* {
      AVCodecContext* context = avcodec_alloc_context3(codec);
      if (context == nullptr) {
        return nullptr;
      }
      if (avcodec_parameters_to_context(context, parameters) < 0) {
        avcodec_free_context(&context);
        return nullptr;
      }
      context->pkt_timebase = stream->time_base;

      if (hardware) {
        // T7.11.1：1080p 后退窗口的延后回拷最多同时持有 24 个硬解帧引用。
        // FFmpeg 的 D3D11VA 静态表面池默认很小（实测持 18 帧就耗尽），
        // 这里给 1080p 及以下预留窗口大小的额外表面；4K 仍走同步回拷，不加池。
        if (static_cast<std::int64_t>(parameters->width) * parameters->height <=
            1920 * 1088) {
          context->extra_hw_frames = 32;
        }
      }

      if (hardware) {
        AVBufferRef* device = nullptr;
        if (shared_d3d11_device != nullptr) {
          device = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
          if (device != nullptr) {
            auto* hw_device = reinterpret_cast<AVHWDeviceContext*>(device->data);
            auto* d3d11_device =
                reinterpret_cast<AVD3D11VADeviceContext*>(hw_device->hwctx);
            // FFmpeg 的 hwdevice 释放路径会 Release 这个设备；先加引用，
            // 避免把渲染器/窗口持有的设备提前释放。
            shared_d3d11_device->AddRef();
            d3d11_device->device = shared_d3d11_device;
            if (av_hwdevice_ctx_init(device) < 0) {
              av_buffer_unref(&device);
            }
          }
        } else {
          const int hw_ret =
              av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
          if (hw_ret < 0) {
            if (device != nullptr) {
              av_buffer_unref(&device);
            }
            device = nullptr;
          }
        }
        if (device == nullptr) {
          avcodec_free_context(&context);
          return nullptr;
        }
        context->hw_device_ctx = av_buffer_ref(device);
        av_buffer_unref(&device);
        if (context->hw_device_ctx == nullptr) {
          avcodec_free_context(&context);
          return nullptr;
        }
        context->get_format = SelectHardwareFormat;
      }

      if (avcodec_open2(context, codec, nullptr) < 0) {
        avcodec_free_context(&context);
        return nullptr;
      }
      return context;
    };

    AVCodecContext* decoder_holder = nullptr;
    bool hardware = false;
    // T7.4：测试专用强制软解开关（--force-software-decode 会设置该环境变量）。
    // 只影响本次进程打开的解码器；产品默认仍是 D3D11VA 优先、失败回退软解。
    const bool software_only = std::getenv("LAPIAN_FORCE_SOFTWARE") != nullptr;
    if (prefer_hardware && !software_only) {
      decoder_holder = create_context(true);
      hardware = decoder_holder != nullptr;
    }
    if (decoder_holder == nullptr) {
      decoder_holder = create_context(false);
      hardware = false;
    }
    if (decoder_holder == nullptr) {
      *error = "解码器打开失败";
      return false;
    }

    AVFrame* frame_holder = av_frame_alloc();
    AVPacket* packet_holder = av_packet_alloc();
    if (frame_holder == nullptr || packet_holder == nullptr) {
      if (frame_holder != nullptr) {
        av_frame_free(&frame_holder);
      }
      if (packet_holder != nullptr) {
        av_packet_free(&packet_holder);
      }
      avcodec_free_context(&decoder_holder);
      *error = "分配解码缓冲失败";
      return false;
    }

    Close();
    format = input_format;
    owns_format = take_ownership;
    decoder = decoder_holder;
    frame = frame_holder;
    packet = packet_holder;
    video_stream = selected;
    bit_depth = ReadBitDepth(parameters);
    hardware_active = hardware;
    eof_sent = false;
    packet_valid = false;
    path = display_path;
    requested_stream = selected;
    external_device = shared_d3d11_device;
    return true;
  }

  bool ReopenSoftware(std::string* error) {
    const std::string saved_path = path;
    const int saved_stream = requested_stream;
    AVFormatContext* saved_format = format;
    const bool saved_owns_format = owns_format;
    const bool had_format = saved_format != nullptr;
    Close();
    path = saved_path;
    requested_stream = saved_stream;
    if (had_format && !saved_owns_format) {
      // 可增长读层：format 由 MediaSession 拥有；软解回退只重建 codec，
      // 并在 DecodeInternal(true) 里重新 seek 到流起点。
      return OpenFormatInternal(saved_format, false, saved_stream, false,
                                external_device, saved_path, error);
    }
    return OpenInternal(saved_path, saved_stream, false, external_device, error);
  }

  bool FillInfo(DecodedFrameInfo* info) {
    if (frame == nullptr) {
      return false;
    }
    info->width = frame->width > 0 ? frame->width : decoder->width;
    info->height = frame->height > 0 ? frame->height : decoder->height;
    info->pixel_format = frame->format;
    const char* format_name =
        av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format));
    info->pixel_format_name = format_name != nullptr ? format_name : "unknown";
    info->bit_depth = bit_depth;
    info->pts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp
                                                               : frame->pts;
    if (info->pts == AV_NOPTS_VALUE) {
      info->pts = 0;
    }
    info->duration = frame->duration;
    const AVRational time_base = format->streams[video_stream]->time_base;
    info->pts_us = av_rescale_q(info->pts, time_base, AV_TIME_BASE_Q);
    info->duration_us = av_rescale_q(frame->duration, time_base, AV_TIME_BASE_Q);
    info->key_frame = (frame->flags & AV_FRAME_FLAG_KEY) != 0;
    info->hardware = frame->hw_frames_ctx != nullptr || frame->format == AV_PIX_FMT_D3D11;
    info->av_frame = frame;
    return true;
  }

  bool DecodeInternal(bool seek_first, DecodedFrameInfo* info, std::string* error) {
    if (!IsOpen()) {
      *error = "解码器未打开";
      return false;
    }
    if (info == nullptr) {
      *error = "输出参数为空";
      return false;
    }

    if (seek_first) {
      const std::int64_t start =
          format->streams[video_stream]->start_time != AV_NOPTS_VALUE
              ? format->streams[video_stream]->start_time
              : 0;
      // 失败也不致命：open 后通常已在文件开头，继续读即可。
      if (av_seek_frame(format, video_stream, start, AVSEEK_FLAG_BACKWARD) >= 0) {
        avcodec_flush_buffers(decoder);
      }
      eof_sent = false;
      packet_valid = false;
    }
    av_frame_unref(frame);

    for (int guard = 0; guard < 1'000'000; ++guard) {
      int ret = avcodec_receive_frame(decoder, frame);
      if (ret == 0) {
        return FillInfo(info);
      }
      if (ret == AVERROR_EOF) {
        *error = "到达文件末尾";
        return false;
      }
      if (ret != AVERROR(EAGAIN)) {
        *error = "解码失败: " + AvErrorText(ret);
        return false;
      }

      if (eof_sent && !packet_valid) {
        *error = "到达文件末尾";
        return false;
      }

      if (!packet_valid) {
        ret = av_read_frame(format, packet);
        if (ret < 0) {
          ret = avcodec_send_packet(decoder, nullptr);
          if (ret < 0 && ret != AVERROR_EOF) {
            *error = "刷新解码器失败: " + AvErrorText(ret);
            return false;
          }
          eof_sent = true;
          continue;
        }
        if (packet->stream_index != video_stream) {
          av_packet_unref(packet);
          continue;
        }
        packet_valid = true;
      }

      ret = avcodec_send_packet(decoder, packet);
      if (ret == 0) {
        av_packet_unref(packet);
        packet_valid = false;
        continue;
      }
      if (ret == AVERROR(EAGAIN)) {
        continue;  // 保留 packet，先收帧
      }
      if (ret == AVERROR_EOF) {
        av_packet_unref(packet);
        packet_valid = false;
        eof_sent = true;
        continue;
      }
      *error = "送入解码包失败: " + AvErrorText(ret);
      return false;
    }

    *error = "解码循环超过上限";
    return false;
  }

  bool SeekToPts(std::int64_t pts_us, std::string* error) {
    if (!IsOpen()) {
      *error = "解码器未打开";
      return false;
    }
    const AVRational time_base = format->streams[video_stream]->time_base;
    const std::int64_t target = av_rescale_q(pts_us, AV_TIME_BASE_Q, time_base);
    const int ret = av_seek_frame(format, video_stream, target, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
      *error = "seek 失败: " + AvErrorText(ret);
      return false;
    }
    avcodec_flush_buffers(decoder);
    av_frame_unref(frame);
    eof_sent = false;
    packet_valid = false;
    return true;
  }
};

VideoDecoder::VideoDecoder() : impl_(std::make_unique<Impl>()) {}

VideoDecoder::~VideoDecoder() = default;

bool VideoDecoder::Open(const std::string& utf8_path, int video_stream_index, bool prefer_hardware,
                        std::string* error) {
  if (error == nullptr) {
    return false;
  }
  // 只有硬解路径使用 D3D11 设备；软件预览解码不抢这把锁。
  std::unique_lock<std::recursive_mutex> device_lock(
      lapian::gfx::D3D11ContextMutex(), std::defer_lock);
  if (prefer_hardware || impl_->hardware_active) {
    device_lock.lock();
  }
  error->clear();
  impl_->Close();
  impl_->path = utf8_path;
  impl_->requested_stream = video_stream_index;
  impl_->external_device = nullptr;
  if (impl_->OpenInternal(utf8_path, video_stream_index, prefer_hardware, nullptr, error)) {
    return true;
  }
  impl_->Close();
  return false;
}

bool VideoDecoder::OpenWithD3D11Device(const std::string& utf8_path, int video_stream_index,
                                       ID3D11Device* d3d11_device, bool prefer_hardware,
                                       std::string* error) {
  if (error == nullptr) {
    return false;
  }
  std::unique_lock<std::recursive_mutex> device_lock(
      lapian::gfx::D3D11ContextMutex(), std::defer_lock);
  if (prefer_hardware || impl_->hardware_active) {
    device_lock.lock();
  }
  error->clear();
  if (prefer_hardware && d3d11_device == nullptr) {
    *error = "硬件解码需要与渲染器共用的 D3D11 设备";
    return false;
  }
  impl_->Close();
  impl_->path = utf8_path;
  impl_->requested_stream = video_stream_index;
  impl_->external_device = d3d11_device;
  if (impl_->OpenInternal(utf8_path, video_stream_index, prefer_hardware, d3d11_device, error)) {
    return true;
  }
  impl_->Close();
  return false;
}

bool VideoDecoder::OpenWithFormat(AVFormatContext* format, int video_stream_index,
                                  ID3D11Device* d3d11_device, bool prefer_hardware,
                                  std::string* error) {
  if (error == nullptr) {
    return false;
  }
  std::unique_lock<std::recursive_mutex> device_lock(
      lapian::gfx::D3D11ContextMutex(), std::defer_lock);
  if (prefer_hardware || impl_->hardware_active) {
    device_lock.lock();
  }
  error->clear();
  if (format == nullptr) {
    *error = "可增长媒体上下文为空";
    return false;
  }
  if (prefer_hardware && d3d11_device == nullptr) {
    // 软件路径/自动化可以用空设备；只有明确要求硬解才拒绝。
    prefer_hardware = false;
  }
  impl_->external_device = d3d11_device;
  impl_->requested_stream = video_stream_index;
  if (impl_->OpenFormatInternal(format, false, video_stream_index, prefer_hardware,
                                d3d11_device, std::string(), error)) {
    return true;
  }
  impl_->Close();
  return false;
}

void VideoDecoder::Close() {
  std::unique_lock<std::recursive_mutex> device_lock(
      lapian::gfx::D3D11ContextMutex(), std::defer_lock);
  if (impl_->hardware_active) {
    device_lock.lock();
  }
  impl_->Close();
}

bool VideoDecoder::DecodeFirstFrame(DecodedFrameInfo* info, std::string* error) {
  if (error == nullptr) {
    return false;
  }
  std::unique_lock<std::recursive_mutex> device_lock(
      lapian::gfx::D3D11ContextMutex(), std::defer_lock);
  if (impl_->hardware_active) {
    device_lock.lock();
  }
  error->clear();
  if (!impl_->IsOpen()) {
    *error = "解码器未打开";
    return false;
  }

  if (impl_->DecodeInternal(true, info, error)) {
    return true;
  }

  // 硬解在首帧解码阶段失败时，自动重开软解再试一次。
  if (impl_->hardware_active) {
    const std::string hardware_error = *error;
    std::string fallback_error;
    if (impl_->ReopenSoftware(&fallback_error) &&
        impl_->DecodeInternal(true, info, error)) {
      return true;
    }
    *error = hardware_error + "; 软解回退失败: " + fallback_error;
  }
  return false;
}

bool VideoDecoder::DecodeNextFrame(DecodedFrameInfo* info, std::string* error) {
  if (error == nullptr) {
    return false;
  }
  std::unique_lock<std::recursive_mutex> device_lock(
      lapian::gfx::D3D11ContextMutex(), std::defer_lock);
  if (impl_->hardware_active) {
    device_lock.lock();
  }
  error->clear();
  return impl_->DecodeInternal(false, info, error);
}

bool VideoDecoder::SeekToPts(std::int64_t pts_us, std::string* error) {
  if (error == nullptr) {
    return false;
  }
  std::unique_lock<std::recursive_mutex> device_lock(
      lapian::gfx::D3D11ContextMutex(), std::defer_lock);
  if (impl_->hardware_active) {
    device_lock.lock();
  }
  error->clear();
  return impl_->SeekToPts(pts_us, error);
}

bool VideoDecoder::IsOpen() const { return impl_->IsOpen(); }

bool VideoDecoder::HardwareActive() const { return impl_->hardware_active; }

int VideoDecoder::BitDepth() const { return impl_->bit_depth; }

bool VideoDecoder::IsHighBitDepth() const { return impl_->bit_depth > 8; }

}  // namespace lapian::media
