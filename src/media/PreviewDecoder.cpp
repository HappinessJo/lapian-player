#include "media/PreviewDecoder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
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
// 正向拖动允许从当前位置连续向前解的上限；超过则 seek 到目标关键帧。
// T8.17：快速拖动会连续改目标，不能因为目标版本变化就中断；这个窗口只决定
// “继续解”还是“重新找关键帧”。1s GOP 的 2s 大跳应走连续解码。
constexpr std::int64_t kMaxContinueAheadUs = 5'000'000;
// 预览最长边约 1280；4K 降到 1280×720 左右再上传。
constexpr int kPreviewMaxSide = 1280;
// 反向拖动的小缓存：只存已经降采样的预览帧，最多 32 帧（约 44MB @720p NV12）。
// 不是 T7.2 的正式 LRU 帧缓存；End/Close 会释放。
constexpr std::size_t kMaxPreviewCacheFrames = 32;

struct CachedPreview {
  std::int64_t pts_us = 0;
  std::int64_t frame_index = -1;
  AVFrame* frame = nullptr;
};

struct AvFrameDeleter {
  void operator()(AVFrame* frame) const {
    if (frame != nullptr) {
      av_frame_free(&frame);
    }
  }
};
using AvFramePtr = std::unique_ptr<AVFrame, AvFrameDeleter>;

}  // namespace

struct PreviewDecoder::Impl {
  mutable std::mutex mutex;
  std::condition_variable condition;
  std::thread thread;

  std::string path;
  int stream_index = -1;
  const FrameIndex* index = nullptr;

  std::int64_t target_frame = 0;
  // <0：目标在回退，只发布目标帧；>0：目标在前进，可连续发布中间帧。
  int target_direction = 0;
  // generation 只表示“目标版本”；session 表示“本次拖动”。
  // 快速拖动时目标会连续变化，不能让每次 UpdateTarget 都中断正在解码的线程。
  std::uint64_t generation = 0;
  std::uint64_t handled_generation = 0;
  std::uint64_t session = 0;
  bool active = false;
  bool stop = false;

  bool open_attempted = false;
  bool open_ok = false;
  std::string error;
  AVFrame* latest = nullptr;
  std::int64_t latest_pts_us = 0;
  std::int64_t latest_frame_index = -1;
  std::uint64_t latest_generation = 0;
  // 已经交给 UI 的最后一帧。反向快拖时允许显示“比当前目标晚、但比上一张
  // 更早”的粗帧，避免每个完成帧都因为 != target 被丢弃而整段空住。
  std::int64_t last_delivered_frame = -1;
  std::deque<CachedPreview> cache;
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
      ++session;
      condition.notify_all();
    }
    if (thread.joinable()) {
      thread.join();
    }

    std::lock_guard<std::mutex> lock(mutex);
    if (latest != nullptr) {
      av_frame_free(&latest);
    }
    ClearCacheLocked();
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
    target_direction = 0;
    generation = 0;
    handled_generation = 0;
    session = 0;
    active = false;
    stop = false;
    latest_pts_us = 0;
    latest_frame_index = -1;
    latest_generation = 0;
    last_delivered_frame = -1;
    last_pts_us = 0;
    position_valid = false;
  }

  bool IsSessionCurrent(std::uint64_t requested_session) const {
    std::lock_guard<std::mutex> lock(mutex);
    return !stop && active && session == requested_session;
  }

  // 读取“当前最新目标”。拖动过程中目标会被连续改写；Process 每解一帧后都来
  // 取一次，向前移动时直接追新目标，而不是因为目标版本变化就中断重 seek。
  bool LatestTarget(std::uint64_t requested_session, std::int64_t* target,
                    int* direction) const {
    std::lock_guard<std::mutex> lock(mutex);
    if (stop || !active || session != requested_session || target == nullptr ||
        direction == nullptr) {
      return false;
    }
    *target = target_frame;
    *direction = target_direction;
    return true;
  }

  // 反向快拖的“粗帧”条件：帧仍比上一张已显示帧更早，画面只会继续后退。
  bool CoarseAllowed(std::int64_t frame_index) const {
    std::lock_guard<std::mutex> lock(mutex);
    return !stop && active && last_delivered_frame >= 0 &&
           frame_index < last_delivered_frame;
  }

  void SetError(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex);
    error = text;
    active = false;
    ++generation;
  }

  // 调用方必须持有 mutex。
  void ClearCacheLocked() {
    for (CachedPreview& cached : cache) {
      if (cached.frame != nullptr) {
        av_frame_free(&cached.frame);
      }
    }
    cache.clear();
  }

  AVFrame* CloneCached(std::int64_t frame_index,
                       std::uint64_t requested_session) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stop || !active || session != requested_session) {
      return nullptr;
    }
    for (auto it = cache.rbegin(); it != cache.rend(); ++it) {
      if (it->frame_index == frame_index && it->frame != nullptr) {
        return av_frame_clone(it->frame);
      }
    }
    return nullptr;
  }

  void CacheFrame(AVFrame* frame, std::int64_t pts_us,
                  std::int64_t frame_index, std::uint64_t requested_session) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stop || !active || session != requested_session) {
      av_frame_free(&frame);
      return;
    }
    cache.push_back(CachedPreview{pts_us, frame_index, frame});
    while (cache.size() > kMaxPreviewCacheFrames) {
      CachedPreview& oldest = cache.front();
      if (oldest.frame != nullptr) {
        av_frame_free(&oldest.frame);
      }
      cache.pop_front();
    }
  }

  void Publish(AVFrame* frame, std::int64_t pts_us, std::int64_t frame_index,
               std::uint64_t requested_session) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stop || !active || session != requested_session) {
      av_frame_free(&frame);
      return;
    }
    if (latest != nullptr) {
      av_frame_free(&latest);
    }
    latest = frame;
    latest_pts_us = pts_us;
    latest_frame_index = frame_index;
    latest_generation = requested_session;
  }

  AVFrame* ScalePreviewFrame(const AVFrame* source) {
    if (source == nullptr || source->data[0] == nullptr || source->width <= 0 ||
        source->height <= 0) {
      return nullptr;
    }
    // 硬件帧（D3D11 纹理）不能走 sws_scale；直接交给渲染器按显示尺寸缩放。
    if (source->format == AV_PIX_FMT_D3D11 ||
        source->hw_frames_ctx != nullptr) {
      return av_frame_clone(source);
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
               std::int64_t initial_target, int initial_direction,
               std::uint64_t requested_session) {
    std::int64_t target = initial_target;
    int direction = initial_direction;
    if (!LatestTarget(requested_session, &target, &direction) || target < 0 ||
        static_cast<std::size_t>(target) >= frame_index.size()) {
      return;
    }

    bool backward = direction < 0;
    std::int64_t target_pts = frame_index.PtsOfFrame(target);
    // 反向拖动要“解完当前目标”再回来取最新目标：H.264 不能倒放，若每次目标
    // 变化都中途放弃，快速后退会反复 seek、一张都发不出来。解出的旧目标帧若
    // 仍早于上一张已显示帧，TakeLatestFrame 会作为粗帧接受。
    const bool pursue_backward = backward;
    const std::int64_t pursue_target = target;
    const std::int64_t pursue_pts = target_pts;
    // 反向小步拖动优先命中刚解码过的预览帧，避免每个鼠标事件都从关键帧重解。
    if (backward) {
      AVFrame* cached = CloneCached(target, requested_session);
      if (cached != nullptr) {
        Publish(cached, target_pts, target, requested_session);
        return;
      }
    }
    if (position_valid && target_pts == last_pts_us) {
      return;
    }
    // 拖动通常是向前移动：解码器已经在目标之前且距离不远时继续向前解，
    // 避免每个鼠标事件都从关键帧重解。后退或远跳才重新 seek。
    const bool can_continue =
        position_valid && !backward && target_pts > last_pts_us &&
        target_pts - last_pts_us <= kMaxContinueAheadUs;
    bool seeked = false;
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
      seeked = true;
    }

    auto slice_start = std::chrono::steady_clock::now();
    int guard = 0;
    // 反向快拖时保留“最后解出的一帧”，目标被继续拖早时作为粗帧发布，
    // 而不是直接丢弃导致整段空住。所有权交给 Publish 或本函数结束时释放。
    AvFramePtr last_backward_preview;
    std::int64_t last_backward_pts = 0;
    std::int64_t last_backward_frame = -1;
    for (;;) {
      // 关键变化：向前拖动时只把“最新目标”读进来，不因为目标版本变化就退出；
      // 快速拖动时目标每 16ms 变一次，若每次中断重 seek，画面会整段空住。
      // 反向拖动则坚持解完 pursue_target，再由 worker 取最新目标。
      if (pursue_backward) {
        std::int64_t latest_target = target;
        int latest_direction = direction;
        if (!LatestTarget(requested_session, &latest_target,
                          &latest_direction)) {
          return;
        }
        if (latest_direction > 0) {
          // 用户改成向前拖了，交给 worker 走正向逻辑。
          return;
        }
        target = pursue_target;
        target_pts = pursue_pts;
        direction = -1;
        backward = true;
      } else {
        if (!LatestTarget(requested_session, &target, &direction)) {
          return;
        }
        if (target < 0 ||
            static_cast<std::size_t>(target) >= frame_index.size()) {
          return;
        }
        backward = direction < 0;
        target_pts = frame_index.PtsOfFrame(target);
      }
      if (position_valid) {
        if (target_pts == last_pts_us) {
          return;
        }
        if (backward) {
          // 已经解过当前目标：本条目标完成，交给 worker 取最新目标。
          if (last_pts_us > target_pts) {
            if (last_backward_preview != nullptr &&
                CoarseAllowed(last_backward_frame)) {
              Publish(last_backward_preview.release(), last_backward_pts,
                      last_backward_frame, requested_session);
            }
            return;
          }
        } else {
          // 目标被拖到当前解码位置之前：交给 worker 重新 seek。
          if (target_pts < last_pts_us) {
            return;
          }
          // 刚 seek 过的目标即使还在 5s 外也要继续解完；否则普通远跳会在
          // “解到第一帧 → 发现还很远 → 再 seek”之间死循环。
          if (!seeked && target_pts - last_pts_us > kMaxContinueAheadUs) {
            return;
          }
        }
      }

      if (!IsSessionCurrent(requested_session)) {
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
      if (!IsSessionCurrent(requested_session)) {
        return;
      }

      // 反向拖动时关键帧到目标之间的中间帧不能发布：H.264 只能从关键帧向前解，
      // 逐帧发布会表现为“画面反复向前冲”的频闪。只在到达目标时发布一次。
      AVFrame* preview = ScalePreviewFrame(info.av_frame);
      if (preview == nullptr) {
        SetError("预览帧降采样失败");
        return;
      }
      std::int64_t decoded_frame = frame_index.FrameOfPts(info.pts_us);
      if (backward && decoded_frame < 0) {
        decoded_frame = target;
      }
      if (backward) {
        last_backward_preview.reset(av_frame_clone(preview));
        last_backward_pts = info.pts_us;
        last_backward_frame = decoded_frame;
      }
      const bool publish_frame = !backward || info.pts_us >= target_pts;
      AVFrame* publish_copy =
          publish_frame ? av_frame_clone(preview) : nullptr;
      // 预览缓存只保留降采样帧；正向/反向都填，反向下一步可直接命中。
      CacheFrame(preview, info.pts_us, decoded_frame, requested_session);
      if (publish_copy != nullptr) {
        Publish(publish_copy, info.pts_us, decoded_frame, requested_session);
      }

      if (info.pts_us >= target_pts) {
        // 刚解到目标时目标可能又前进了：直接追最新目标，避免多一次 seek。
        std::int64_t latest_target = target;
        int latest_direction = direction;
        if (LatestTarget(requested_session, &latest_target, &latest_direction) &&
            latest_direction > 0) {
          const std::int64_t latest_pts =
              frame_index.PtsOfFrame(latest_target);
          if (latest_pts > info.pts_us) {
            target = latest_target;
            direction = latest_direction;
            continue;
          }
        }
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
      const std::uint64_t requested_session = session;
      const std::int64_t target = target_frame;
      const int direction = target_direction;
      const FrameIndex* current_index = index;
      const bool can_decode = open_ok;
      lock.unlock();

      if (can_decode && current_index != nullptr) {
        Process(decoder, *current_index, target, direction, requested_session);
      }

      lock.lock();
      // 用“进入 Process 时的版本”做 handled：如果目标在解码过程中变了，
      // 下一轮会再处理一次；若 Process 已经追到最新目标，这一轮会快速返回。
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
  // 新一次拖动先只显示起点目标帧；等用户向前移动后再发布中间帧。
  // 这样也不会在按下时从关键帧向前“冲”一段。
  impl_->target_direction = -1;
  ++impl_->generation;
  ++impl_->session;
  impl_->ClearCacheLocked();
  if (impl_->latest != nullptr) {
    av_frame_free(&impl_->latest);
  }
  impl_->latest_pts_us = 0;
  impl_->latest_frame_index = -1;
  impl_->latest_generation = 0;
  // 虚拟的“上一帧”上界：反向快拖时允许起点目标帧作为粗帧先显示；
  // 否则第一个目标会在每次 16ms 更新中过期，等不到“正好等于 target”的时机。
  impl_->last_delivered_frame = frame + 1;
  impl_->condition.notify_all();
}

void PreviewDecoder::UpdateTarget(std::int64_t frame) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->active) {
    return;
  }
  if (frame < impl_->target_frame) {
    impl_->target_direction = -1;
  } else if (frame > impl_->target_frame) {
    impl_->target_direction = 1;
  }
  impl_->target_frame = frame;
  // 目标已经回退时，旧目标留下的“更靠后”帧通常立即作废；但若它仍比已显示的
  // 上一帧更早，可作为快速反向拖动的粗帧保留，避免整段空住。
  if (impl_->latest != nullptr && impl_->latest_frame_index > frame) {
    const bool coarse =
        impl_->target_direction < 0 && impl_->last_delivered_frame >= 0 &&
        impl_->latest_frame_index < impl_->last_delivered_frame;
    if (!coarse) {
      av_frame_free(&impl_->latest);
      impl_->latest_pts_us = 0;
      impl_->latest_frame_index = -1;
      impl_->latest_generation = 0;
    }
  }
  ++impl_->generation;
  impl_->condition.notify_all();
}

void PreviewDecoder::End() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->active = false;
  impl_->target_direction = 0;
  ++impl_->generation;
  ++impl_->session;
  if (impl_->latest != nullptr) {
    av_frame_free(&impl_->latest);
  }
  impl_->ClearCacheLocked();
  impl_->latest_pts_us = 0;
  impl_->latest_frame_index = -1;
  impl_->latest_generation = 0;
  impl_->last_delivered_frame = -1;
  impl_->condition.notify_all();
}

void PreviewDecoder::Close() { impl_->Close(); }

PreviewFrame PreviewDecoder::TakeLatestFrame() {
  PreviewFrame result;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->active || impl_->latest == nullptr) {
    return result;
  }
  // 1) 目标回退后，比当前目标更靠后的旧帧通常丢弃；
  // 2) 但快速反向拖动时，解码线程完成的往往是“上一目标”的帧：它比当前目标晚，
  //    却比已经显示过的上一帧早。允许这种粗帧，画面才会持续后退而不是整段空住。
  const bool ahead_of_target = impl_->latest_frame_index > impl_->target_frame;
  const bool behind_target = impl_->latest_frame_index < impl_->target_frame;
  const bool backward_mismatch =
      impl_->target_direction < 0 &&
      impl_->latest_frame_index != impl_->target_frame;
  bool accept_coarse = false;
  if (ahead_of_target) {
    accept_coarse = impl_->target_direction < 0 &&
                    impl_->last_delivered_frame >= 0 &&
                    impl_->latest_frame_index < impl_->last_delivered_frame;
  }
  if ((ahead_of_target && !accept_coarse) ||
      (backward_mismatch && behind_target)) {
    av_frame_free(&impl_->latest);
    impl_->latest_pts_us = 0;
    impl_->latest_frame_index = -1;
    impl_->latest_generation = 0;
    return result;
  }
  result.frame = impl_->latest;
  result.pts_us = impl_->latest_pts_us;
  result.frame_index = impl_->latest_frame_index;
  result.generation = impl_->latest_generation;
  impl_->last_delivered_frame = impl_->latest_frame_index;
  impl_->latest = nullptr;
  impl_->latest_frame_index = -1;
  return result;
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
