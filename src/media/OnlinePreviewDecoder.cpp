#include "media/OnlinePreviewDecoder.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#include <QString>

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
#include "online/GrowingSource.h"

namespace lapian::media {
namespace {

// 预览解码主动让出 CPU 的切片；与本地 PreviewDecoder 相同，避免一次目标
// 长时间占住预览线程而无法响应新目标/松手。
constexpr auto kDecodeSlice = std::chrono::milliseconds(15);
// 预览最长边约 1280；4K/1080p 都先降采样再上传，避免 UI 线程上传大帧。
constexpr int kPreviewMaxSide = 1280;
// 反向拖动最近帧缓存：只存已降采样的 NV12；24 帧约 33MB @720p，End/Close 释放。
constexpr std::size_t kMaxCachedFrames = 24;
// 正向小步拖动直接续解，避免每个目标都从关键帧重解。
constexpr std::int64_t kContinueWindowUs = 2'000'000;

struct CachedPreview {
  std::int64_t pts_us = 0;
  std::int64_t frame_index = -1;
  AVFrame* frame = nullptr;
};

}  // namespace

struct OnlinePreviewDecoder::Impl {
  mutable std::mutex mutex;
  std::condition_variable condition;
  std::thread thread;
  bool running = false;

  std::shared_ptr<lapian::online::GrowingSource> source;
  std::string path;
  int stream_index = -1;
  std::shared_ptr<const FrameIndex> index;
  std::shared_ptr<std::atomic<bool>> interrupt;

  std::int64_t target_frame = 0;
  bool target_cached = false;
  // <0：目标在回退，只发布目标帧；>0：目标在前进，可连续发布中间帧。
  int target_direction = 0;
  std::uint64_t generation = 0;
  std::uint64_t handled_generation = 0;
  bool active = false;
  bool stop = false;
  bool waiting = true;

  bool open_attempted = false;
  bool open_ok = false;
  std::string error;

  AVFrame* latest = nullptr;
  std::int64_t latest_pts_us = 0;
  std::int64_t latest_frame_index = -1;
  std::uint64_t latest_generation = 0;
  std::deque<CachedPreview> cache;
  SwsContext* sws = nullptr;
  std::int64_t last_pts_us = 0;
  bool position_valid = false;

  std::uint64_t frames_published = 0;
  std::uint64_t stale_dropped = 0;
  std::uint64_t seek_count = 0;
  std::int64_t last_published_frame = -1;

  ~Impl() { Close(); }

  void FreeLatestLocked() {
    if (latest != nullptr) {
      av_frame_free(&latest);
    }
    latest_pts_us = 0;
    latest_frame_index = -1;
    latest_generation = 0;
  }

  void ClearCacheLocked() {
    for (CachedPreview& cached : cache) {
      if (cached.frame != nullptr) {
        av_frame_free(&cached.frame);
      }
    }
    cache.clear();
  }

  void End() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stop = true;
      active = false;
      waiting = false;
      ++generation;
      if (interrupt != nullptr) {
        interrupt->store(true, std::memory_order_relaxed);
      }
      condition.notify_all();
    }
    if (thread.joinable()) {
      thread.join();
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      running = false;
      FreeLatestLocked();
      ClearCacheLocked();
      if (sws != nullptr) {
        sws_freeContext(sws);
        sws = nullptr;
      }
      position_valid = false;
      last_pts_us = 0;
      target_cached = false;
      target_frame = 0;
      target_direction = 0;
      generation = 0;
      handled_generation = 0;
      open_attempted = false;
      open_ok = false;
      interrupt.reset();
    }
  }

  void Close() {
    End();
    std::lock_guard<std::mutex> lock(mutex);
    source.reset();
    path.clear();
    stream_index = -1;
    index.reset();
    error.clear();
    waiting = true;
  }

  bool IsCurrent(std::uint64_t requested_generation) const {
    std::lock_guard<std::mutex> lock(mutex);
    return !stop && active && generation == requested_generation;
  }

  void SetError(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stop) {
      return;
    }
    error = text;
    active = false;
    waiting = false;
    ++generation;
  }

  void SetWaiting(bool value, std::uint64_t requested_generation) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stop || !active || generation != requested_generation) {
      return;
    }
    waiting = value;
  }

  AVFrame* CloneCached(std::int64_t frame_index,
                       std::uint64_t requested_generation) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stop || !active) {
      return nullptr;
    }
    (void)requested_generation;
    for (auto it = cache.rbegin(); it != cache.rend(); ++it) {
      if (it->frame_index == frame_index && it->frame != nullptr) {
        return av_frame_clone(it->frame);
      }
    }
    return nullptr;
  }

  void CacheFrame(AVFrame* frame, std::int64_t pts_us,
                  std::int64_t frame_index,
                  std::uint64_t requested_generation) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stop || !active) {
      av_frame_free(&frame);
      return;
    }
    (void)requested_generation;
    cache.push_back(CachedPreview{pts_us, frame_index, frame});
    while (cache.size() > kMaxCachedFrames) {
      CachedPreview& oldest = cache.front();
      if (oldest.frame != nullptr) {
        av_frame_free(&oldest.frame);
      }
      cache.pop_front();
    }
  }

  void Publish(AVFrame* frame, std::int64_t pts_us, std::int64_t frame_index,
               std::uint64_t requested_generation) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stop || !active) {
      av_frame_free(&frame);
      return;
    }
    if (latest != nullptr) {
      av_frame_free(&latest);
    }
    latest = frame;
    latest_pts_us = pts_us;
    latest_frame_index = frame_index;
    latest_generation = requested_generation;
    ++frames_published;
    last_published_frame = frame_index;
  }

  AVFrame* ScalePreviewFrame(const AVFrame* source_frame) {
    if (source_frame == nullptr || source_frame->data[0] == nullptr ||
        source_frame->width <= 0 || source_frame->height <= 0) {
      return nullptr;
    }
    const int longest = std::max(source_frame->width, source_frame->height);
    int destination_width = source_frame->width;
    int destination_height = source_frame->height;
    if (longest > kPreviewMaxSide) {
      const double scale = static_cast<double>(kPreviewMaxSide) / longest;
      destination_width = std::max(
          2, static_cast<int>(std::lround(source_frame->width * scale)));
      destination_height = std::max(
          2, static_cast<int>(std::lround(source_frame->height * scale)));
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
    if (destination_width == source_frame->width &&
        destination_height == source_frame->height) {
      return av_frame_clone(source_frame);
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
        sws, source_frame->width, source_frame->height,
        static_cast<AVPixelFormat>(source_frame->format), destination_width,
        destination_height, AV_PIX_FMT_NV12, SWS_FAST_BILINEAR, nullptr,
        nullptr, nullptr);
    if (sws == nullptr) {
      av_frame_free(&destination);
      return nullptr;
    }
    const int scaled =
        sws_scale(sws, source_frame->data, source_frame->linesize, 0,
                  source_frame->height, destination->data,
                  destination->linesize);
    if (scaled <= 0) {
      av_frame_free(&destination);
      return nullptr;
    }
    destination->pts = source_frame->pts;
    destination->best_effort_timestamp =
        source_frame->best_effort_timestamp;
    destination->sample_aspect_ratio = source_frame->sample_aspect_ratio;
    destination->colorspace = source_frame->colorspace;
    destination->color_range = source_frame->color_range;
    destination->color_primaries = source_frame->color_primaries;
    destination->color_trc = source_frame->color_trc;
    return destination;
  }

  void Process(VideoDecoder& decoder, const FrameIndex& frame_index,
               std::int64_t target, int direction,
               std::uint64_t requested_generation) {
    if (!IsCurrent(requested_generation) || target < 0 ||
        static_cast<std::size_t>(target) >= frame_index.size()) {
      SetWaiting(true, requested_generation);
      return;
    }

    const bool backward = direction < 0;
    const std::int64_t target_pts = frame_index.PtsOfFrame(target);
    // 反向拖动优先命中刚解码过的降采样帧，避免每一步都从关键帧重解。
    if (backward) {
      AVFrame* cached = CloneCached(target, requested_generation);
      if (cached != nullptr) {
        Publish(cached, target_pts, target, requested_generation);
        SetWaiting(false, requested_generation);
        return;
      }
    }
    // 正向小步拖动继续解码；后退/远跳才重新 seek 到最近关键帧。
    const bool can_continue =
        position_valid && target_pts > last_pts_us &&
        target_pts - last_pts_us <= kContinueWindowUs;
    std::string error_text;
    if (!can_continue) {
      std::int64_t keyframe = frame_index.KeyframeAtOrBefore(target_pts);
      if (keyframe < 0) {
        keyframe = 0;
      }
      {
        std::lock_guard<std::mutex> lock(mutex);
        ++seek_count;
      }
      if (!decoder.SeekToPts(frame_index.PtsOfFrame(keyframe), &error_text)) {
        position_valid = false;
        SetError(error_text.empty() ? "在线预览 seek 失败" : error_text);
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
        SetError(error_text.empty() ? "在线预览解码失败" : error_text);
        return;
      }
      last_pts_us = info.pts_us;
      position_valid = true;

      AVFrame* preview = ScalePreviewFrame(info.av_frame);
      if (preview == nullptr) {
        SetError("在线预览帧降采样失败");
        return;
      }
      std::int64_t decoded_frame = frame_index.FrameOfPts(info.pts_us);
      if (decoded_frame < 0) {
        decoded_frame = target;
      }
      // 反向拖动不发布关键帧到目标之间的中间帧，避免“画面向前冲”的频闪；
      // 正向拖动允许连续发布中间帧，画面才是连续更新而不是只跳帧号。
      const bool publish_frame = !backward || info.pts_us >= target_pts;
      AVFrame* publish_copy =
          publish_frame ? av_frame_clone(preview) : nullptr;
      CacheFrame(preview, info.pts_us, decoded_frame, requested_generation);
      if (publish_copy != nullptr) {
        Publish(publish_copy, info.pts_us, decoded_frame,
                requested_generation);
      }
      if (info.pts_us >= target_pts) {
        SetWaiting(false, requested_generation);
        return;
      }
      // 新目标到达后，先把刚解出的这一帧发布/缓存，再退出；否则每次目标
      // 更新都会丢掉一帧，慢拖只能到 ~34fps。TakeLatestFrame 会按当前目标
      // 丢弃后退/超前帧，不会把旧目标帧当新目标帧。
      if (!IsCurrent(requested_generation)) {
        return;
      }
      if (++guard > 100000) {
        SetError("在线预览解码超过上限");
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

    std::shared_ptr<lapian::online::GrowingSource> local_source;
    std::string local_path;
    int local_stream = -1;
    std::shared_ptr<std::atomic<bool>> local_interrupt;
    {
      std::lock_guard<std::mutex> lock(mutex);
      local_source = source;
      local_path = path;
      local_stream = stream_index;
      local_interrupt = interrupt;
    }

    lapian::online::GrowingFormat format;
    VideoDecoder decoder;
    QString open_error;
    bool opened = false;
    if (local_source && !local_path.empty()) {
      const QString qpath =
          QString::fromUtf8(local_path.c_str(),
                            static_cast<int>(local_path.size()));
      opened = format.Open(local_source, qpath, &open_error, 0);
      if (opened && format.cursor() != nullptr && local_interrupt != nullptr) {
        format.cursor()->SetInterruptFlag(local_interrupt);
      }
      if (opened) {
        std::string decoder_error;
        opened = decoder.OpenWithFormat(format.context(), local_stream, nullptr,
                                        false, &decoder_error);
        if (!opened && decoder_error.size() > 0) {
          open_error = QString::fromStdString(decoder_error);
        }
      }
    } else {
      open_error = QStringLiteral("在线预览缺少数据源/路径");
    }

    bool should_stop = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      open_attempted = true;
      open_ok = opened;
      should_stop = stop;
      if (!opened) {
        if (open_error.isEmpty()) {
          open_error = QStringLiteral("在线预览解码器打开失败");
        }
        error = open_error.toStdString();
        active = false;
        waiting = false;
        ++generation;
        condition.notify_all();
      }
    }
    if (!opened || should_stop) {
      std::lock_guard<std::mutex> lock(mutex);
      running = false;
      return;
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
      const bool cached = target_cached;
      const int direction = target_direction;
      const bool active_now = active;
      const std::shared_ptr<const FrameIndex> current_index = index;
      lock.unlock();

      if (active_now && cached && current_index != nullptr) {
        Process(decoder, *current_index, target, direction,
                requested_generation);
      } else if (active_now) {
        SetWaiting(true, requested_generation);
      }

      lock.lock();
      handled_generation = requested_generation;
    }

    decoder.Close();
    format.Close();
    std::lock_guard<std::mutex> lock(mutex);
    running = false;
  }
};

OnlinePreviewDecoder::OnlinePreviewDecoder() : impl_(std::make_unique<Impl>()) {}

OnlinePreviewDecoder::~OnlinePreviewDecoder() = default;

void OnlinePreviewDecoder::Begin(
    std::shared_ptr<lapian::online::GrowingSource> source,
    const std::string& part_path_utf8, int video_stream_index,
    std::shared_ptr<const FrameIndex> index, std::int64_t initial_frame,
    bool initial_cached) {
  impl_->Close();
  if (!source || part_path_utf8.empty() || video_stream_index < 0 || !index) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->error = "在线预览参数不完整";
    return;
  }
  const bool initial_covered =
      initial_frame >= 0 &&
      static_cast<std::size_t>(initial_frame) < index->size();
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->source = std::move(source);
    impl_->path = part_path_utf8;
    impl_->stream_index = video_stream_index;
    impl_->index = std::move(index);
    impl_->interrupt = std::make_shared<std::atomic<bool>>(false);
    impl_->stop = false;
    impl_->active = true;
    impl_->running = true;
    impl_->waiting = !(initial_cached && initial_covered);
    impl_->handled_generation = 0;
    impl_->generation = 0;
    impl_->target_frame = initial_frame;
    impl_->target_cached = initial_cached && initial_covered;
    // 按下时先只显示起点目标帧，避免从关键帧向前“冲”一段。
    impl_->target_direction = -1;
    impl_->open_attempted = false;
    impl_->open_ok = false;
    impl_->error.clear();
    impl_->frames_published = 0;
    impl_->stale_dropped = 0;
    impl_->seek_count = 0;
    impl_->last_published_frame = -1;
  }
  impl_->thread = std::thread([impl = impl_.get()] { impl->Run(); });
}

void OnlinePreviewDecoder::UpdateIndex(
    std::shared_ptr<const FrameIndex> index) {
  if (!index) {
    return;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->stop) {
    return;
  }
  impl_->index = std::move(index);
  ++impl_->generation;
  impl_->condition.notify_all();
}

void OnlinePreviewDecoder::UpdateTarget(std::int64_t frame, bool cached) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->stop || !impl_->active) {
    return;
  }
  const bool covered =
      impl_->index != nullptr && frame >= 0 &&
      static_cast<std::size_t>(frame) < impl_->index->size();
  if (frame < impl_->target_frame) {
    impl_->target_direction = -1;
  } else if (frame > impl_->target_frame) {
    impl_->target_direction = 1;
  }
  impl_->target_frame = frame;
  impl_->target_cached = cached && covered;
  impl_->waiting = !impl_->target_cached;
  // 目标回退时，旧目标留下的“更靠后”帧立即作废，下一次 tick 不会显示。
  if (impl_->latest != nullptr &&
      impl_->latest_frame_index > impl_->target_frame) {
    impl_->FreeLatestLocked();
    ++impl_->stale_dropped;
  }
  ++impl_->generation;
  impl_->condition.notify_all();
}

void OnlinePreviewDecoder::End() { impl_->End(); }

void OnlinePreviewDecoder::Close() { impl_->Close(); }

PreviewFrame OnlinePreviewDecoder::TakeLatestFrame() {
  PreviewFrame result;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->active || impl_->latest == nullptr) {
    return result;
  }
  // 目标回退后，比当前目标更靠后的旧帧一律丢弃；反向拖动只接受正好是
  // 当前目标的那一帧，避免显示关键帧到目标之间的中间帧。
  const bool ahead_of_target =
      impl_->latest_frame_index > impl_->target_frame;
  const bool backward_mismatch =
      impl_->target_direction < 0 &&
      impl_->latest_frame_index != impl_->target_frame;
  if (ahead_of_target || backward_mismatch) {
    impl_->FreeLatestLocked();
    ++impl_->stale_dropped;
    return result;
  }
  result.frame = impl_->latest;
  result.pts_us = impl_->latest_pts_us;
  result.frame_index = impl_->latest_frame_index;
  result.generation = impl_->latest_generation;
  impl_->latest = nullptr;
  impl_->latest_frame_index = -1;
  return result;
}

bool OnlinePreviewDecoder::IsReady() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->open_ok && !impl_->stop;
}

bool OnlinePreviewDecoder::IsWaiting() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->active && impl_->waiting;
}

bool OnlinePreviewDecoder::IsThreadRunning() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->running;
}

std::string OnlinePreviewDecoder::last_error() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->error;
}

std::uint64_t OnlinePreviewDecoder::frames_published() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->frames_published;
}

std::uint64_t OnlinePreviewDecoder::stale_dropped() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->stale_dropped;
}

std::uint64_t OnlinePreviewDecoder::seek_count() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->seek_count;
}

int OnlinePreviewDecoder::cached_frames() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return static_cast<int>(impl_->cache.size());
}

std::int64_t OnlinePreviewDecoder::last_published_frame() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->last_published_frame;
}

}  // namespace lapian::media
