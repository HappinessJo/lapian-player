#include "media/PreviewDecoder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/log.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "media/FrameIndex.h"
#include "media/VideoDecoder.h"

namespace lapian::media {
namespace {

// 预览只在拖动期间使用；每次解码切片约 15ms，避免长时间占住一个目标。
constexpr auto kDecodeSlice = std::chrono::milliseconds(15);
// 预览最长边约 1280；4K 降到 1280×720 左右再上传。
constexpr int kPreviewMaxSide = 1280;

}  // namespace

struct PreviewDecoder::Impl {
  mutable std::mutex mutex;
  std::condition_variable condition;
  std::thread thread;

  std::string path;
  int stream_index = -1;
  const FrameIndex* index = nullptr;

  std::int64_t target_frame = 0;
  std::uint64_t generation = 0;
  std::uint64_t handled_generation = 0;
  bool active = false;
  bool stop = false;

  bool open_attempted = false;
  bool open_ok = false;
  std::string error;
  AVFrame* latest = nullptr;
  SwsContext* sws = nullptr;
  std::int64_t last_pts_us = 0;
  bool position_valid = false;

  ~Impl() { Close(); }

  void Close() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stop = true;
      active = false;
      ++generation;
      condition.notify_all();
    }
    if (thread.joinable()) {
      thread.join();
    }

    std::lock_guard<std::mutex> lock(mutex);
    if (latest != nullptr) {
      av_frame_free(&latest);
    }
    if (sws != nullptr) {
      sws_freeContext(sws);
      sws = nullptr;
    }
    path.clear();
    index = nullptr;
    stream_index = -1;
    open_attempted = false;
    open_ok = false;
    error.clear();
    target_frame = 0;
    generation = 0;
    handled_generation = 0;
    active = false;
    stop = false;
    last_pts_us = 0;
    position_valid = false;
  }

  bool IsCurrent(std::uint64_t requested_generation) const {
    std::lock_guard<std::mutex> lock(mutex);
    return !stop && active && generation == requested_generation;
  }

  void SetError(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex);
    error = text;
    active = false;
    ++generation;
  }

  void Publish(AVFrame* frame, std::uint64_t requested_generation) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stop || !active || generation != requested_generation) {
      av_frame_free(&frame);
      return;
    }
    if (latest != nullptr) {
      av_frame_free(&latest);
    }
    latest = frame;
  }

  AVFrame* ScalePreviewFrame(const AVFrame* source) {
    if (source == nullptr || source->data[0] == nullptr || source->width <= 0 ||
        source->height <= 0) {
      return nullptr;
    }

    const int longest = std::max(source->width, source->height);
    int destination_width = source->width;
    int destination_height = source->height;
    if (longest > kPreviewMaxSide) {
      const double scale = static_cast<double>(kPreviewMaxSide) / longest;
      destination_width =
          std::max(2, static_cast<int>(std::lround(source->width * scale)));
      destination_height =
          std::max(2, static_cast<int>(std::lround(source->height * scale)));
    }
    destination_width &= ~1;
    destination_height &= ~1;
    if (destination_width < 2) {
      destination_width = 2;
    }
    if (destination_height < 2) {
      destination_height = 2;
    }

    // 已经足够小：直接引用解码帧，让渲染器做 YUV→NV12 转换。
    if (destination_width == source->width && destination_height == source->height) {
      return av_frame_clone(source);
    }

    AVFrame* destination = av_frame_alloc();
    if (destination == nullptr) {
      return nullptr;
    }
    destination->format = AV_PIX_FMT_NV12;
    destination->width = destination_width;
    destination->height = destination_height;
    if (av_frame_get_buffer(destination, 32) < 0) {
      av_frame_free(&destination);
      return nullptr;
    }

    sws = sws_getCachedContext(
        sws, source->width, source->height,
        static_cast<AVPixelFormat>(source->format), destination_width,
        destination_height, AV_PIX_FMT_NV12, SWS_FAST_BILINEAR, nullptr, nullptr,
        nullptr);
    if (sws == nullptr) {
      av_frame_free(&destination);
      return nullptr;
    }
    const int scaled = sws_scale(sws, source->data, source->linesize, 0,
                                 source->height, destination->data,
                                 destination->linesize);
    if (scaled <= 0) {
      av_frame_free(&destination);
      return nullptr;
    }
    destination->pts = source->pts;
    destination->best_effort_timestamp = source->best_effort_timestamp;
    destination->sample_aspect_ratio = source->sample_aspect_ratio;
    destination->colorspace = source->colorspace;
    destination->color_range = source->color_range;
    destination->color_primaries = source->color_primaries;
    destination->color_trc = source->color_trc;
    return destination;
  }

  void Process(VideoDecoder& decoder, const FrameIndex& frame_index,
               std::int64_t target, std::uint64_t requested_generation) {
    if (!IsCurrent(requested_generation) || target < 0 ||
        static_cast<std::size_t>(target) >= frame_index.size()) {
      return;
    }

    const std::int64_t target_pts = frame_index.PtsOfFrame(target);
    // 拖动通常是向前小步移动：解码器已经在目标之前且距离不远时继续向前解，
    // 避免每个鼠标事件都从关键帧重解。后退或远跳才重新 seek。
    const bool can_continue =
        position_valid && target_pts > last_pts_us &&
        target_pts - last_pts_us <= 2'000'000;
    std::string error_text;
    if (!can_continue) {
      std::int64_t keyframe = frame_index.KeyframeAtOrBefore(target_pts);
      if (keyframe < 0) {
        keyframe = 0;
      }
      if (!decoder.SeekToPts(frame_index.PtsOfFrame(keyframe), &error_text)) {
        position_valid = false;
        SetError(error_text.empty() ? "预览 seek 失败" : error_text);
        return;
      }
      position_valid = false;
    }

    auto slice_start = std::chrono::steady_clock::now();
    int guard = 0;
    for (;;) {
      if (!IsCurrent(requested_generation)) {
        return;
      }
      DecodedFrameInfo info;
      if (!decoder.DecodeNextFrame(&info, &error_text)) {
        position_valid = false;
        SetError(error_text.empty() ? "预览解码失败" : error_text);
        return;
      }
      last_pts_us = info.pts_us;
      position_valid = true;
      if (!IsCurrent(requested_generation)) {
        return;
      }

      AVFrame* preview = ScalePreviewFrame(info.av_frame);
      if (preview == nullptr) {
        SetError("预览帧降采样失败");
        return;
      }
      Publish(preview, requested_generation);

      if (info.pts_us >= target_pts) {
        return;
      }
      if (++guard > 100000) {
        SetError("预览解码超过上限");
        return;
      }
      if (std::chrono::steady_clock::now() - slice_start > kDecodeSlice) {
        slice_start = std::chrono::steady_clock::now();
        std::this_thread::yield();
      }
    }
  }

  void Run() {
    // 预览频繁 seek 会触发 H.264 “missing picture in access unit” 警告；
    // 只保留 error 级别，避免刷屏影响 UI/日志与测量。
    av_log_set_level(AV_LOG_ERROR);

    std::string local_path;
    int local_stream = -1;
    {
      std::lock_guard<std::mutex> lock(mutex);
      local_path = path;
      local_stream = stream_index;
    }

    VideoDecoder decoder;
    std::string open_error;
    bool opened = false;
    if (!local_path.empty()) {
      opened = decoder.Open(local_path, local_stream, false, &open_error);
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      open_attempted = true;
      open_ok = opened;
      if (!opened) {
        error = open_error.empty() ? "预览解码器打开失败" : open_error;
        active = false;
      }
    }

    for (;;) {
      std::unique_lock<std::mutex> lock(mutex);
      condition.wait(lock, [this] {
        return stop || (active && generation != handled_generation);
      });
      if (stop) {
        break;
      }
      const std::uint64_t requested_generation = generation;
      const std::int64_t target = target_frame;
      const FrameIndex* current_index = index;
      const bool can_decode = open_ok;
      lock.unlock();

      if (can_decode && current_index != nullptr) {
        Process(decoder, *current_index, target, requested_generation);
      }

      lock.lock();
      handled_generation = requested_generation;
    }
  }
};

PreviewDecoder::PreviewDecoder() : impl_(std::make_unique<Impl>()) {}

PreviewDecoder::~PreviewDecoder() = default;

void PreviewDecoder::Configure(const std::string& utf8_path, int video_stream_index,
                                const FrameIndex* index) {
  impl_->Close();
  if (utf8_path.empty() || index == nullptr) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->path = utf8_path;
    impl_->stream_index = video_stream_index;
    impl_->index = index;
    impl_->stop = false;
    impl_->active = false;
    impl_->target_frame = 0;
    impl_->generation = 0;
    impl_->handled_generation = 0;
    impl_->open_attempted = false;
    impl_->open_ok = false;
    impl_->error.clear();
  }
  impl_->thread = std::thread([impl = impl_.get()] { impl->Run(); });
}

void PreviewDecoder::Begin(std::int64_t frame) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->active = true;
  impl_->target_frame = frame;
  ++impl_->generation;
  if (impl_->latest != nullptr) {
    av_frame_free(&impl_->latest);
  }
  impl_->condition.notify_all();
}

void PreviewDecoder::UpdateTarget(std::int64_t frame) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->active) {
    return;
  }
  impl_->target_frame = frame;
  ++impl_->generation;
  impl_->condition.notify_all();
}

void PreviewDecoder::End() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->active = false;
  ++impl_->generation;
  if (impl_->latest != nullptr) {
    av_frame_free(&impl_->latest);
  }
  impl_->condition.notify_all();
}

void PreviewDecoder::Close() { impl_->Close(); }

AVFrame* PreviewDecoder::TakeLatestFrame() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  AVFrame* frame = impl_->latest;
  impl_->latest = nullptr;
  return frame;
}

bool PreviewDecoder::IsReady() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->open_ok;
}

std::string PreviewDecoder::last_error() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->error;
}

}  // namespace lapian::media
