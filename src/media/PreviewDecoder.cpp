#include "media/PreviewDecoder.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>

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
#include "media/PreviewRingCache.h"
#include "media/VideoDecoder.h"

namespace lapian::media {
namespace {

// 预览只在拖动期间使用；每次解码切片约 15ms，避免长时间占住一个目标。
constexpr auto kDecodeSlice = std::chrono::milliseconds(15);
// 正向拖动允许从当前位置连续向前解的上限；超过则 seek 到目标关键帧。
// T8.17：快速拖动会连续改目标，不能因为目标版本变化就中断；这个窗口只决定
// “继续解”还是“重新找关键帧”。1s GOP 的 2s 大跳应走连续解码。
constexpr std::int64_t kMaxContinueAheadUs = 5'000'000;
// 预览最长边约 1280；4K 降到 1280×720 左右再上传（仅软件帧；硬件帧不缩放）。
constexpr int kPreviewMaxSide = 1280;
// DP-2 保留 DP-1 的 32 帧软件热缓存（不扩大）；新增的有界环缓存单独记账。
constexpr std::size_t kMaxPreviewCacheFrames = 32;
// 热缓存再做一层字节上限，避免极端画幅下 32 帧把“合计 ≤256MiB”撑破。
constexpr std::size_t kMaxPreviewHotBytes = 48u * 1024u * 1024u;
// 预览环缓存：软件 NV12 降采样帧。32 帧热缓存约 44MiB，192+44 ≤256MiB。
constexpr std::size_t kPreviewRingBudgetBytes = 192u * 1024u * 1024u;
// 关键帧缩略图：320×180 NV12，滑动窗口 + LRU，预算单独 ≤64MiB。
constexpr std::size_t kPreviewThumbBudgetBytes = 64u * 1024u * 1024u;
constexpr int kThumbnailMaxSide = 320;
// 预览硬解表面池：不缓存硬件帧，不需要主播放 T7.11.1 的 32 张延后回拷预算。
constexpr int kPreviewExtraHwFrames = 8;
// End() 等待预览线程退出当前解码切片的上界；正常 <20ms，超时也继续松手精确。
constexpr auto kEndWait = std::chrono::milliseconds(100);
// End() 后把环缓存收窄，避免空闲播放时长期占用上百 MB；热缓存仍保留 32 帧。
constexpr std::size_t kPreviewRingIdleBytes = 32u * 1024u * 1024u;
constexpr std::size_t kPreviewThumbIdleBytes = 16u * 1024u * 1024u;
// 正向只有超大跳（≥300 帧）才先发粗帧；正向连续解码本身会发布中间帧。
// DP-3：阈值与 DragStrategy 共用（kDragFarJumpStep），不在这里写第二份。
constexpr std::int64_t kCoarseMinForwardJump = kDragFarJumpStep;
// 反向小步拖动时把整段解码结果回拷进缓存（最多一个 GOP），后续邻近目标可命中。
// 大步反向/正向不逐帧回拷，避免 GPU→CPU 回拷把 Present 拖慢。
// 环缓存只保留目标附近 ±60s（30fps）的帧；超出窗口的旧帧对反向/局部拖动无价值。
constexpr std::int64_t kPreviewCacheWindowFrames = 1800;
// 缩略图窗口：目标附近 ±12 个 GOP（按帧数折算），避免无限增长。
constexpr std::int64_t kThumbnailWindowFrames = 12 * 300;
// 预取一次只解一帧/一小段，且只在没有待处理目标时做；任何新目标都会取消。
constexpr int kPrefetchFramesPerIdle = 2;
constexpr std::int64_t kPrefetchMaxDistance = 450;
constexpr auto kIdleWait = std::chrono::milliseconds(2);

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

// 把任意软件帧转成最长边 ≤ max_side 的 NV12；已足够小时直接引用。
AVFrame* ScaleToNv12(const AVFrame* source, int max_side, SwsContext** context) {
  if (source == nullptr || source->data[0] == nullptr || source->width <= 0 ||
      source->height <= 0) {
    return nullptr;
  }
  if (source->format == AV_PIX_FMT_D3D11 ||
      source->hw_frames_ctx != nullptr) {
    return av_frame_clone(source);
  }
  const int longest = std::max(source->width, source->height);
  int destination_width = source->width;
  int destination_height = source->height;
  if (longest > max_side) {
    const double scale = static_cast<double>(max_side) / longest;
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
  if (destination_width == source->width &&
      destination_height == source->height) {
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
  *context = sws_getCachedContext(
      *context, source->width, source->height,
      static_cast<AVPixelFormat>(source->format), destination_width,
      destination_height, AV_PIX_FMT_NV12, SWS_FAST_BILINEAR, nullptr, nullptr,
      nullptr);
  if (*context == nullptr) {
    av_frame_free(&destination);
    return nullptr;
  }
  const int scaled = sws_scale(*context, source->data, source->linesize, 0,
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

}  // namespace


struct PreviewDecoder::Impl {
  mutable std::mutex mutex;
  std::condition_variable condition;
  // DP-1：End() 等待 worker 退出当前 Process，避免松手后的主播放精确 seek
  // 与预览硬解同时使用同一个 D3D11 视频上下文。
  std::condition_variable idle_condition;
  std::thread thread;

  std::string path;
  int stream_index = -1;
  const FrameIndex* index = nullptr;
  ID3D11Device* device = nullptr;
  bool prefer_hardware = true;
  // DP-1：硬解只覆盖保证档（≤1920×1088）；4K 继续软件预览，避免 4K 硬解
  // 每次 seek 解整个 GOP 把 Present 拖到数百 ms（DP-3/DP-4 再评估）。
  int source_width = 0;
  int source_height = 0;
  // 运行期回退后保持软件，直到 Configure 重新决定；避免每次拖动重试硬解。
  bool force_software = false;

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
  bool processing = false;

  bool open_attempted = false;
  bool open_ok = false;
  std::string error;
  std::string open_error;
  bool hardware_in_use = false;
  std::string hw_fallback_reason;
  AVFrame* latest = nullptr;
  std::int64_t latest_pts_us = 0;
  std::int64_t latest_frame_index = -1;
  std::uint64_t latest_generation = 0;
  // 已经交给 UI 的最后一帧。反向快拖时允许显示“比当前目标晚、但比上一张
  // 更早”的粗帧，避免每个完成帧都因为 != target 被丢弃而整段空住。
  std::int64_t last_delivered_frame = -1;
  bool last_delivered_coarse = false;
  // DP-2：32 帧软件热缓存（DP-1 语义/容量不变） + 独立记账的有界环缓存。
  std::deque<CachedPreview> cache;
  std::size_t cache_bytes = 0;
  std::size_t cache_peak_bytes = 0;
  PreviewRingCache ring_cache{kPreviewRingBudgetBytes};
  PreviewThumbnailCache thumbnail_cache{kPreviewThumbBudgetBytes};
  SwsContext* sws = nullptr;
  // worker 生成“当前 GOP 关键帧粗帧”用的独立 sws 上下文，避免和 1280 预览缩放
  // 共用上下文来回重建。
  SwsContext* coarse_sws = nullptr;
  std::int64_t last_pts_us = 0;
  bool position_valid = false;

  // 最新帧的类型标记：粗帧/缓存命中/硬件帧不能混为“精确帧”。
  bool latest_coarse = false;
  bool latest_from_cache = false;
  bool latest_hardware = false;

  // DP-3：FarJump 的“先粗后精”闸门。粗帧已发布但 UI 还没取走时，精确目标
  // 先暂存在这里；TakeLatestFrame 取走粗帧后，下一 tick 再把精确帧交给 UI。
  // 只服务远跳策略，快拖/慢拖/反向不改变 DP-2 的发布时序。
  AVFrame* deferred_exact = nullptr;
  std::int64_t deferred_exact_pts_us = 0;
  std::int64_t deferred_exact_frame_index = -1;
  bool deferred_exact_hardware = false;
  bool coarse_undelivered = false;
  DragMode drag_mode = DragMode::Exact;

  // 拖动会话统计；Begin 清零，诊断/探针在拖动中或松手前读取。
  std::atomic<int> seeks{0};
  std::atomic<int> cache_hits{0};
  std::atomic<int> cache_misses{0};
  std::atomic<int> coarse_hits{0};
  std::atomic<int> coarse_requests{0};
  std::atomic<int> coarse_published{0};
  std::atomic<int> prefetched_frames{0};
  std::atomic<int> readbacks{0};
  std::atomic<int> decoded_frames{0};
  std::atomic<int> published_count{0};
  std::atomic<int> decoder_reopens{0};
  std::atomic<std::int64_t> seek_us_total{0};
  std::atomic<std::int64_t> decode_us_total{0};
  std::atomic<std::int64_t> readback_us_total{0};
  std::atomic<std::int64_t> first_coarse_us{-1};
  std::atomic<std::int64_t> last_coarse_us{-1};
  std::atomic<std::int64_t> last_published_frame{-1};
  bool cache_enabled = true;
  bool prefetch_enabled = true;
  bool thumbnails_enabled = true;

  // 本次拖动最近一次目标变化，供回拷策略（小步反向才逐帧回拷）使用。
  std::int64_t last_target_step = 0;
  bool has_target_update = false;
  bool coarse_pending = false;
  std::chrono::steady_clock::time_point coarse_request_time{};

  // 关键帧缩略图线程：独立软件解码器/独立读层，低优先级，可取消。
  std::thread thumbnail_thread;
  std::condition_variable thumbnail_condition;
  bool thumbnail_stop = false;
  std::deque<std::int64_t> thumbnail_queue;
  std::int64_t thumbnail_request_keyframe = -1;
  std::int64_t thumbnail_request_target = -1;
  std::uint64_t thumbnail_request_session = 0;
  SwsContext* thumbnail_sws = nullptr;  // 只由缩略图线程使用

  // 空闲预取：只在没有待处理目标时解一到两帧；新目标立即取消。
  std::int64_t prefetch_target = -1;
  std::int64_t prefetch_start = -1;
  std::int64_t prefetch_end = -1;
  std::int64_t prefetch_cursor = -1;
  std::int64_t prefetch_anchor_keyframe = -1;
  std::int64_t prefetch_done_keyframe = -1;
  bool prefetch_seeked = false;
  int prefetch_remaining = 0;

  // DP-2：独立低优先级预取线程。慢速局部反向拖动时把“上一 GOP”解成 1280
  // NV12 软件帧写进环缓存，供下一次目标直接命中；不用硬件回拷，因此不会
  // 占 D3D11 表面池，也不会把当前目标的 Present 拖慢。
  std::thread prefetch_thread;
  std::condition_variable prefetch_condition;
  bool prefetch_stop = false;
  std::int64_t prefetch_request_target = -1;
  std::uint64_t prefetch_request_session = 0;
  std::int64_t prefetch_served_keyframe = -1;

  ~Impl() { Close(); }

  void Close() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stop = true;
      thumbnail_stop = true;
      prefetch_stop = true;
      active = false;
      ++generation;
      ++session;
      condition.notify_all();
      idle_condition.notify_all();
      thumbnail_condition.notify_all();
      prefetch_condition.notify_all();
    }
    if (thread.joinable()) {
      thread.join();
    }
    if (thumbnail_thread.joinable()) {
      thumbnail_thread.join();
    }
    if (prefetch_thread.joinable()) {
      prefetch_thread.join();
    }

    std::lock_guard<std::mutex> lock(mutex);
    if (deferred_exact != nullptr) {
      av_frame_free(&deferred_exact);
    }
    if (latest != nullptr) {
      av_frame_free(&latest);
    }
    ClearCacheLocked();
    if (sws != nullptr) {
      sws_freeContext(sws);
      sws = nullptr;
    }
    if (coarse_sws != nullptr) {
      sws_freeContext(coarse_sws);
      coarse_sws = nullptr;
    }
    if (thumbnail_sws != nullptr) {
      sws_freeContext(thumbnail_sws);
      thumbnail_sws = nullptr;
    }
    path.clear();
    index = nullptr;
    stream_index = -1;
    device = nullptr;
    prefer_hardware = true;
    source_width = 0;
    source_height = 0;
    force_software = false;
    open_attempted = false;
    open_ok = false;
    error.clear();
    open_error.clear();
    hardware_in_use = false;
    hw_fallback_reason.clear();
    target_frame = 0;
    target_direction = 0;
    generation = 0;
    handled_generation = 0;
    session = 0;
    active = false;
    stop = false;
    processing = false;
    latest_pts_us = 0;
    latest_frame_index = -1;
    latest_generation = 0;
    latest_coarse = false;
    latest_from_cache = false;
    latest_hardware = false;
    deferred_exact_pts_us = 0;
    deferred_exact_frame_index = -1;
    deferred_exact_hardware = false;
    coarse_undelivered = false;
    drag_mode = DragMode::Exact;
    last_delivered_frame = -1;
    last_delivered_coarse = false;
    last_pts_us = 0;
    position_valid = false;
    last_target_step = 0;
    has_target_update = false;
    coarse_pending = false;
    seeks = 0;
    cache_hits = 0;
    cache_misses = 0;
    coarse_hits = 0;
    coarse_requests = 0;
    coarse_published = 0;
    prefetched_frames = 0;
    readbacks = 0;
    decoded_frames = 0;
    published_count = 0;
    decoder_reopens = 0;
    seek_us_total = 0;
    decode_us_total = 0;
    readback_us_total = 0;
    first_coarse_us = -1;
    last_coarse_us = -1;
    last_published_frame = -1;
    cache_enabled = true;
    prefetch_enabled = true;
    thumbnails_enabled = true;
    cache_peak_bytes = 0;
    thumbnail_queue.clear();
    thumbnail_request_keyframe = -1;
    thumbnail_request_target = -1;
    thumbnail_request_session = 0;
    thumbnail_stop = false;
    prefetch_target = -1;
    prefetch_remaining = 0;
    prefetch_stop = false;
    prefetch_request_target = -1;
    prefetch_request_session = 0;
    prefetch_served_keyframe = -1;
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
    cache_bytes = 0;
    ring_cache.Clear();
    thumbnail_cache.Clear();
    thumbnail_queue.clear();
  }

  void UpdateCachePeakLocked() {
    const std::size_t total = cache_bytes + ring_cache.bytes();
    if (total > cache_peak_bytes) {
      cache_peak_bytes = total;
    }
  }

  void TrimHotLocked() {
    while (!cache.empty() &&
           (cache.size() > kMaxPreviewCacheFrames ||
            cache_bytes > kMaxPreviewHotBytes)) {
      CachedPreview& oldest = cache.front();
      if (oldest.frame != nullptr) {
        const std::size_t bytes =
            static_cast<std::size_t>(oldest.frame->width) *
            static_cast<std::size_t>(oldest.frame->height) * 3u / 2u;
        cache_bytes -= std::min(cache_bytes, bytes);
        av_frame_free(&oldest.frame);
      }
      cache.pop_front();
    }
  }

  AVFrame* CloneHotLocked(std::int64_t frame_index) {
    for (auto it = cache.rbegin(); it != cache.rend(); ++it) {
      if (it->frame_index == frame_index && it->frame != nullptr) {
        return av_frame_clone(it->frame);
      }
    }
    return nullptr;
  }

  // 精确命中（热缓存优先，其次环缓存）；调用方持有 mutex。不在这里计数：
  // 命中率的分母定义为“每次 Begin/UpdateTarget 的目标查询”，由调用方统一计。
  AVFrame* CloneExactLocked(std::int64_t frame_index,
                            std::uint64_t requested_session) {
    if (stop || !active || session != requested_session || !cache_enabled) {
      return nullptr;
    }
    if (AVFrame* hot = CloneHotLocked(frame_index)) {
      return hot;
    }
    return ring_cache.Clone(frame_index);
  }

  // 接管 frame；调用方持有 mutex。
  void CacheFrameLocked(AVFrame* frame, std::int64_t pts_us,
                        std::int64_t frame_index,
                        std::uint64_t requested_session) {
    if (frame == nullptr) {
      return;
    }
    if (stop || !active || session != requested_session || !cache_enabled) {
      av_frame_free(&frame);
      return;
    }
    cache.push_back(CachedPreview{pts_us, frame_index, frame});
    cache_bytes += static_cast<std::size_t>(frame->width) *
                   static_cast<std::size_t>(frame->height) * 3u / 2u;
    TrimHotLocked();
    UpdateCachePeakLocked();
  }

  // 环缓存只接收软件 NV12；硬件帧必须在调用前回拷，绝不持有 D3D11 帧。
  void PutRingLocked(AVFrame* frame, std::int64_t pts_us,
                     std::int64_t frame_index) {
    if (frame == nullptr) {
      return;
    }
    if (!cache_enabled) {
      av_frame_free(&frame);
      return;
    }
    ring_cache.Put(frame_index, pts_us, frame);
    UpdateCachePeakLocked();
  }

  // 调用方持有 mutex。DP-3 远跳闸门用：丢弃暂存的精确帧。
  void FreeDeferredExactLocked() {
    if (deferred_exact != nullptr) {
      av_frame_free(&deferred_exact);
    }
    deferred_exact_pts_us = 0;
    deferred_exact_frame_index = -1;
    deferred_exact_hardware = false;
  }

  // 调用方持有 mutex。粗帧被 UI 取走后，把暂存的精确目标提升为 latest；
  // 下一次 TakeLatestFrame 即可交给 UI，保证“先粗后精”且不丢精确帧。
  void PromoteDeferredExactLocked() {
    if (deferred_exact == nullptr) {
      return;
    }
    AVFrame* frame = deferred_exact;
    const std::int64_t pts = deferred_exact_pts_us;
    const std::int64_t index_frame = deferred_exact_frame_index;
    const bool hardware = deferred_exact_hardware;
    deferred_exact = nullptr;
    deferred_exact_pts_us = 0;
    deferred_exact_frame_index = -1;
    deferred_exact_hardware = false;
    ReplaceLatestLocked(frame, pts, index_frame, false, false, hardware);
  }

  // 接管 frame；调用方持有 mutex。
  void ReplaceLatestLocked(AVFrame* frame, std::int64_t pts_us,
                           std::int64_t frame_index, bool coarse,
                           bool from_cache, bool hardware) {
    if (stop || !active) {
      av_frame_free(&frame);
      return;
    }
    if (!coarse) {
      // 新的精确帧已经就位，之前暂存的精确帧不再有意义。
      FreeDeferredExactLocked();
    }
    if (latest != nullptr) {
      av_frame_free(&latest);
    }
    latest = frame;
    latest_pts_us = pts_us;
    latest_frame_index = frame_index;
    latest_generation = session;
    latest_coarse = coarse;
    latest_from_cache = from_cache;
    latest_hardware = hardware;
    coarse_undelivered = coarse;
    ++published_count;
    last_published_frame = frame_index;
    if (coarse) {
      ++coarse_published;
      if (!coarse_pending) {
        coarse_pending = true;
        coarse_request_time = std::chrono::steady_clock::now();
      }
    }
  }

  void Publish(AVFrame* frame, std::int64_t pts_us, std::int64_t frame_index,
               std::uint64_t requested_session, bool coarse = false,
               bool from_cache = false, bool hardware = false) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stop || !active || session != requested_session) {
      av_frame_free(&frame);
      return;
    }
    // DP-3 FarJump：粗帧还没被 UI 取走时，精确目标先暂存；等粗帧呈现后
    // 下一 tick 再替换。保证“先粗后精”而不是在同一 tick 内被精确帧覆盖。
    if (!coarse && drag_mode == DragMode::FarJump && coarse_undelivered &&
        latest != nullptr && latest_coarse) {
      FreeDeferredExactLocked();
      deferred_exact = frame;
      deferred_exact_pts_us = pts_us;
      deferred_exact_frame_index = frame_index;
      deferred_exact_hardware = hardware;
      return;
    }
    ReplaceLatestLocked(frame, pts_us, frame_index, coarse, from_cache,
                        hardware);
  }

  // 同时写热缓存（32 帧）和环缓存；两个参数所有权都转交（ring_frame 可为空）。
  void CachePreviewFrame(AVFrame* hot_frame, AVFrame* ring_frame,
                         std::int64_t pts_us, std::int64_t frame_index,
                         std::uint64_t requested_session) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stop || !active || session != requested_session || !cache_enabled) {
      if (hot_frame != nullptr) {
        av_frame_free(&hot_frame);
      }
      if (ring_frame != nullptr) {
        av_frame_free(&ring_frame);
      }
      return;
    }
    if (ring_frame != nullptr) {
      ring_cache.Put(frame_index, pts_us, ring_frame);
    }
    if (hot_frame != nullptr) {
      cache.push_back(CachedPreview{pts_us, frame_index, hot_frame});
      cache_bytes += static_cast<std::size_t>(hot_frame->width) *
                     static_cast<std::size_t>(hot_frame->height) * 3u / 2u;
      TrimHotLocked();
    }
    UpdateCachePeakLocked();
  }

  // UI 线程的缓存快路径：目标正好在缓存里时不必等 worker seek。调用方持有
  // mutex；返回 true 表示已把精确帧放进 latest。
  bool TryPublishCachedTargetLocked(std::int64_t frame,
                                    std::uint64_t requested_session) {
    AVFrame* cached = CloneExactLocked(frame, requested_session);
    if (cached == nullptr) {
      return false;
    }
    const std::int64_t pts =
        index != nullptr ? index->PtsOfFrame(frame) : 0;
    ReplaceLatestLocked(cached, pts, frame, false, true, false);
    return true;
  }

  bool CoarseEligibleLocked(std::int64_t old_target, std::int64_t target) const {
    if (!cache_enabled || !thumbnails_enabled) {
      return false;
    }
    // DP-3：慢拖/小步（Exact）不使用粗帧替换，避免关键帧粗帧打断精确目标；
    // 快拖/反向/远跳仍按 DP-2 的 ≥300 帧阈值先发粗帧。
    if (drag_mode == DragMode::Exact) {
      return false;
    }
    const std::int64_t distance =
        target >= old_target ? target - old_target : old_target - target;
    // 只有真正的远跳才先出粗帧；-20/-60 每 15.6ms 的快反向由精确路径处理
    // （硬件 45–66fps；软件 30fps 级是物理上限，粗帧追不上目标只会变慢）。
    return distance >= kCoarseMinForwardJump;
  }

  // 远跳/反向快拖：先发布 <= 目标的关键帧缩略图。找不到精确关键帧时退而用
  // 更早的最近缩略图（仍然只会向目标方向靠近，不会把未来帧当粗帧）。
  bool PublishCoarseLocked(std::int64_t target, std::uint64_t requested_session,
                           bool allow_nearest, std::int64_t max_distance = -1) {
    if (!active || session != requested_session || index == nullptr ||
        !thumbnails_enabled) {
      return false;
    }
    // DP-3：Exact（慢拖/小步）只用精确帧，不用粗帧占位。
    if (drag_mode == DragMode::Exact) {
      return false;
    }
    const std::int64_t keyframe =
        index->KeyframeAtOrBeforeFrame(target) >= 0
            ? index->KeyframeAtOrBeforeFrame(target)
            : 0;
    AVFrame* thumb = thumbnail_cache.Clone(keyframe);
    std::int64_t published_keyframe = keyframe;
    if (thumb == nullptr && allow_nearest) {
      thumb = thumbnail_cache.CloneNearestBelow(target, &published_keyframe);
    }
    if (thumb == nullptr) {
      return false;
    }
    if (max_distance >= 0 && target - published_keyframe > max_distance) {
      av_frame_free(&thumb);
      return false;
    }
    if (latest != nullptr && !latest_coarse &&
        latest_frame_index == target) {
      av_frame_free(&thumb);
      return false;
    }
    if (latest != nullptr && latest_coarse &&
        latest_frame_index >= published_keyframe) {
      av_frame_free(&thumb);
      return false;
    }
    const std::int64_t pts = index->PtsOfFrame(published_keyframe);
    ReplaceLatestLocked(thumb, pts, published_keyframe, true, true, false);
    return true;
  }

  // DP-2：慢速局部反向拖动请求预取上一 GOP（≤4 帧/目标的局部拖动）。快速
  // -20/-60 不预取：软件解码追不上，只会抢 CPU；它们已有硬件/粗帧路径。
  void RequestPrefetchLocked(std::int64_t target,
                             std::uint64_t requested_session) {
    if (!prefetch_enabled || !cache_enabled || !active) {
      prefetch_request_target = -1;
      return;
    }
    prefetch_request_target = target;
    prefetch_request_session = requested_session;
    prefetch_condition.notify_all();
  }

  void ClearPrefetchLocked() {
    prefetch_request_target = -1;
    prefetch_condition.notify_all();
  }

  // 请求当前目标所在 GOP 的关键帧缩略图。快速拖动时不在每次目标变化都排
  // 几十个关键帧（会把 CPU 抢走、拉大 Present 空档）；worker 自己的首帧粗帧
  // 已经覆盖当前目标，缩略图线程只做“事先缓存/空闲窗口”。
  void RequestThumbnailsLocked(std::int64_t target,
                               std::uint64_t requested_session) {
    if (!thumbnails_enabled || index == nullptr || !active) {
      return;
    }
    std::int64_t keyframe = index->KeyframeAtOrBeforeFrame(target);
    if (keyframe < 0) {
      keyframe = 0;
    }
    thumbnail_request_keyframe = keyframe;
    thumbnail_request_target = target;
    thumbnail_request_session = requested_session;
    auto enqueue = [this](std::int64_t frame) {
      if (frame < 0 ||
          std::find(thumbnail_queue.begin(), thumbnail_queue.end(), frame) !=
              thumbnail_queue.end()) {
        return;
      }
      if (thumbnail_queue.size() < 4) {
        thumbnail_queue.push_back(frame);
      }
    };
    if (index->Entry(keyframe) != nullptr && !thumbnail_cache.Contains(keyframe)) {
      enqueue(keyframe);
    }
    // 反向快拖时提前解“上一个/上上个 GOP”的关键帧：目标下一次跨 GOP 时，
    // 缩略图已经准备好，才能在目标更新时立即发粗帧。
    if (target_direction < 0) {
      const std::int64_t previous = index->PreviousKeyframe(keyframe);
      enqueue(previous);
      if (previous >= 0) {
        enqueue(index->PreviousKeyframe(previous));
      }
    } else {
      enqueue(index->NextKeyframe(keyframe));
    }
    if (!thumbnail_queue.empty()) {
      thumbnail_condition.notify_all();
    }
  }

  // 缩略图线程解好后，只在“当前目标仍落在该关键帧所在 GOP”时发布粗帧。
  void MaybePublishThumbnailLocked(std::int64_t keyframe,
                                   std::uint64_t requested_session) {
    const bool timing = std::getenv("LAPIAN_TIMING_LOG") != nullptr;
    if (timing) {
      std::fprintf(stderr,
                   "DP2 coarse_try keyframe=%lld target=%lld active=%d stop=%d "
                   "session=%llu req=%llu index=%p thumbs=%d\n",
                   static_cast<long long>(keyframe),
                   static_cast<long long>(target_frame), active ? 1 : 0,
                   stop ? 1 : 0, static_cast<unsigned long long>(session),
                   static_cast<unsigned long long>(requested_session),
                   static_cast<const void*>(index), thumbnails_enabled ? 1 : 0);
      std::fflush(stderr);
    }
    if (!active || stop || session != requested_session || index == nullptr ||
        !thumbnails_enabled) {
      return;
    }
    // DP-3：Exact（慢拖/小步）不允许缩略图线程把粗帧插到精确目标前后。
    if (drag_mode == DragMode::Exact) {
      if (timing) {
        std::fprintf(stderr, "DP3 coarse_skip exact_mode keyframe=%lld\n",
                     static_cast<long long>(keyframe));
      }
      return;
    }
    // 粗帧只能来自目标之前（或正好目标）的关键帧；目标已经继续后退时，
    // 刚解出的旧关键帧仍可能比上一张已显示帧更早，允许它作为粗帧继续后退。
    if (keyframe > target_frame) {
      if (timing) {
        std::fprintf(stderr, "DP2 coarse_skip future keyframe=%lld target=%lld\n",
                     static_cast<long long>(keyframe),
                     static_cast<long long>(target_frame));
      }
      return;
    }
    AVFrame* thumb = thumbnail_cache.Clone(keyframe);
    if (thumb == nullptr) {
      if (timing) {
        std::fprintf(stderr, "DP2 coarse_skip nocache keyframe=%lld\n",
                     static_cast<long long>(keyframe));
      }
      return;
    }
    if (latest != nullptr && !latest_coarse &&
        latest_frame_index == target_frame) {
      av_frame_free(&thumb);
      if (timing) {
        std::fprintf(stderr, "DP2 coarse_skip exact_already target=%lld\n",
                     static_cast<long long>(target_frame));
      }
      return;
    }
    if (latest != nullptr && latest_coarse &&
        latest_frame_index >= keyframe) {
      av_frame_free(&thumb);
      if (timing) {
        std::fprintf(stderr,
                     "DP2 coarse_skip newer_coarse latest=%lld keyframe=%lld\n",
                     static_cast<long long>(latest_frame_index),
                     static_cast<long long>(keyframe));
      }
      return;
    }
    const std::int64_t pts = index->PtsOfFrame(keyframe);
    ReplaceLatestLocked(thumb, pts, keyframe, true, true, false);
  }

  AVFrame* ScalePreviewFrame(const AVFrame* source) {
    return ScaleToNv12(source, kPreviewMaxSide, &sws);
  }

  // DP-1：硬件路径失败时重开软件解码。返回 false 表示软件也打不开。
  bool ApplySoftwareFallback(VideoDecoder& decoder, const std::string& reason,
                             std::string* error_text) {
    decoder.Close();
    position_valid = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      ClearCacheLocked();
      if (latest != nullptr) {
        av_frame_free(&latest);
        latest_pts_us = 0;
        latest_frame_index = -1;
        latest_generation = 0;
      }
      FreeDeferredExactLocked();
      coarse_undelivered = false;
      hardware_in_use = false;
      hw_fallback_reason = reason;
      ++decoder_reopens;
    }
    std::string fallback_error;
    if (!decoder.Open(path, stream_index, false, &fallback_error)) {
      if (error_text != nullptr) {
        *error_text = fallback_error.empty() ? "软解回退打开失败" : fallback_error;
      }
      return false;
    }
    return true;
  }

  // 返回值：true = 当前目标处理完成或已交给新目标；false = 解码/seek/帧格式
  // 失败，调用方决定是否回退软件（不能直接把 active 置 false，否则硬件失败
  // 后没有机会重试软解）。
  bool Process(VideoDecoder& decoder, const FrameIndex& frame_index,
               std::int64_t initial_target, int initial_direction,
               std::uint64_t requested_session, bool expect_hardware,
               std::string* error_text) {
    std::int64_t target = initial_target;
    int direction = initial_direction;
    if (!LatestTarget(requested_session, &target, &direction) || target < 0 ||
        static_cast<std::size_t>(target) >= frame_index.size()) {
      return true;
    }

    bool backward = direction < 0;
    std::int64_t target_pts = frame_index.PtsOfFrame(target);
    // 进入切片时的目标，用于判断“小步反向”还是“大步快拖”以及是否回拷缓存。
    const std::int64_t entry_target = target;
    // 反向拖动要“解完当前目标”再回来取最新目标：H.264 不能倒放，若每次目标
    // 变化都中途放弃，快速后退会反复 seek、一张都发不出来。解出的旧目标帧若
    // 仍早于上一张已显示帧，TakeLatestFrame 会作为粗帧接受。
    const bool pursue_backward = backward;
    const std::int64_t pursue_target = target;
    const std::int64_t pursue_pts = target_pts;
    const bool far_forward_jump =
        !pursue_backward && has_target_update &&
        (last_target_step >= kCoarseMinForwardJump ||
         -last_target_step >= kCoarseMinForwardJump);
    // DP-2：目标精确命中缓存时直接发布，不再 seek；正向、反向都查。
    // 注意：CloneExactLocked 要求持锁（会遍历热缓存 deque / 环缓存 list，
    // 而 UI 线程 Begin/End 与预取线程会并发增删）；裸调会和 av_frame_clone/
    // eviction 竞争，是本轮 0xC0000374/0xC0000005 的首要嫌疑。
    AVFrame* cached = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex);
      cached = CloneExactLocked(target, requested_session);
    }
    if (cached != nullptr) {
      Publish(cached, target_pts, target, requested_session, false, true, false);
      return true;
    }
    if (position_valid && target_pts == last_pts_us) {
      return true;
    }
    // 硬件预览解码单帧成本远低于软件：若仍按软件的 5s 窗口一路向前追，目标
    // 每秒前进数千帧时会解出成千上万帧、把 GPU/呈现拖住，而显示帧仍远远落后。
    // 硬件路径缩小“连续追”的窗口；目标跑远就回到关键帧重新 seek，保证每次
    // Present 的内容接近当前目标，而不是无限补旧帧。
    const std::int64_t max_continue_us =
        expect_hardware ? 1'000'000 : kMaxContinueAheadUs;
    const bool can_continue =
        position_valid && !backward && target_pts > last_pts_us &&
        target_pts - last_pts_us <= max_continue_us;
    bool seeked = false;
    bool coarse_sent_for_seek = false;
    std::string error_string;
    if (!can_continue) {
      std::int64_t keyframe = frame_index.KeyframeAtOrBefore(target_pts);
      if (keyframe < 0) {
        keyframe = 0;
      }
      ++seeks;
      const auto seek_start = std::chrono::steady_clock::now();
      const bool seek_ok =
          decoder.SeekToPts(frame_index.PtsOfFrame(keyframe), &error_string);
      seek_us_total.fetch_add(
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - seek_start)
              .count(),
          std::memory_order_relaxed);
      if (!seek_ok) {
        position_valid = false;
        *error_text = error_string.empty() ? "预览 seek 失败" : error_string;
        return false;
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
          return true;
        }
        if (latest_direction > 0) {
          // 用户改成向前拖了，交给 worker 走正向逻辑。
          return true;
        }
        target = pursue_target;
        target_pts = pursue_pts;
        direction = -1;
        backward = true;
      } else if (expect_hardware) {
        // DP-1 硬件路径：一个切片只服务“进入切片时的目标”。目标每 16ms
        // 前进 60 帧时，如果边解边追最新目标，会永远在补新帧、Present 变稀；
        // 固定目标解完就交回 worker，由下一次切片追最新目标。
        if (!IsSessionCurrent(requested_session)) {
          return true;
        }
        target = pursue_target;
        target_pts = pursue_pts;
        direction = initial_direction;
        backward = false;
      } else {
        if (!LatestTarget(requested_session, &target, &direction)) {
          return true;
        }
        if (target < 0 ||
            static_cast<std::size_t>(target) >= frame_index.size()) {
          return true;
        }
        backward = direction < 0;
        target_pts = frame_index.PtsOfFrame(target);
      }
      if (position_valid) {
        if (target_pts == last_pts_us) {
          return true;
        }
        if (backward) {
          // 已经解过当前目标：本条目标完成，交给 worker 取最新目标。
          if (last_pts_us > target_pts) {
            if (last_backward_preview != nullptr &&
                CoarseAllowed(last_backward_frame)) {
              Publish(last_backward_preview.release(), last_backward_pts,
                      last_backward_frame, requested_session,
                      last_backward_frame != target, false, false);
            }
            return true;
          }
        } else {
          // 目标被拖到当前解码位置之前：交给 worker 重新 seek。
          if (target_pts < last_pts_us) {
            return true;
          }
          // 刚 seek 过的目标即使还在 5s 外也要继续解完；否则普通远跳会在
          // “解到第一帧 → 发现还很远 → 再 seek”之间死循环。
          if (!seeked && target_pts - last_pts_us > max_continue_us) {
            return true;
          }
        }
      }

      if (!IsSessionCurrent(requested_session)) {
        return true;
      }
      DecodedFrameInfo info;
      const auto decode_start = std::chrono::steady_clock::now();
      const bool decode_ok = decoder.DecodeNextFrame(&info, &error_string);
      decode_us_total.fetch_add(
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - decode_start)
              .count(),
          std::memory_order_relaxed);
      if (!decode_ok) {
        position_valid = false;
        *error_text = error_string.empty() ? "预览解码失败" : error_string;
        return false;
      }
      ++decoded_frames;
      last_pts_us = info.pts_us;
      position_valid = true;
      if (!IsSessionCurrent(requested_session)) {
        return true;
      }
      // DP-1：不允许“假硬解”。要求硬解却收到软件帧时立即回退。
      if (expect_hardware && !info.hardware) {
        *error_text = "硬件预览收到了软件帧";
        return false;
      }

      // 反向拖动时关键帧到目标之间的中间帧不能发布：H.264 只能从关键帧向前解，
      // 逐帧发布会表现为“画面反复向前冲”的频闪。只在到达目标时发布一次。
      AVFrame* preview = ScalePreviewFrame(info.av_frame);
      if (preview == nullptr) {
        *error_text = "预览帧降采样失败";
        return false;
      }
      std::int64_t decoded_frame = frame_index.FrameOfPts(info.pts_us);
      if (backward && decoded_frame < 0) {
        decoded_frame = target;
      }
      // DP-2：远跳/快速反向在 seek 后的第一帧（关键帧）生成 320×180 粗帧并发布。
      // 软件快拖时精确目标往往要 50ms+，先给画面；硬件大步快拖不生成，避免
      // 回拷拖慢已经达标的 57–66fps。粗帧带 coarse 标记，精确帧到了会替换。
      if (seeked && !coarse_sent_for_seek && info.pts_us <= target_pts) {
        const std::int64_t step =
            has_target_update
                ? (last_target_step >= 0 ? last_target_step : -last_target_step)
                : 0;
        const bool worker_coarse =
            thumbnails_enabled && step >= kCoarseMinForwardJump;
        if (worker_coarse) {
          const bool timing = std::getenv("LAPIAN_TIMING_LOG") != nullptr;
          if (timing) {
            std::fprintf(stderr,
                         "DP2 worker_coarse step=%lld target=%lld seeked=%d "
                         "hw=%d pts=%lld target_pts=%lld\n",
                         static_cast<long long>(step),
                         static_cast<long long>(entry_target), seeked ? 1 : 0,
                         info.hardware ? 1 : 0,
                         static_cast<long long>(info.pts_us),
                         static_cast<long long>(target_pts));
            std::fflush(stderr);
          }
          AVFrame* thumbnail = nullptr;
          if (info.hardware) {
            // 硬件远跳：直接把刚解出的关键帧（全分辨率）作为粗帧发布，零回拷。
            // 粗帧标记为 coarse，精确目标随后照常解码；它不会占 D3D11 表面池，
            // 因为只是 clone 当前帧的引用，渲染器上传后立即释放。
            std::int64_t keyframe = frame_index.FrameOfPts(info.pts_us);
            if (keyframe < 0) {
              keyframe = frame_index.KeyframeAtOrBeforeFrame(entry_target);
            }
            if (keyframe < 0) {
              keyframe = 0;
            }
            AVFrame* coarse_frame = av_frame_clone(info.av_frame);
            if (coarse_frame != nullptr) {
              Publish(coarse_frame, info.pts_us, keyframe, requested_session,
                      true, false, true);
            }
          } else {
            thumbnail =
                ScaleToNv12(info.av_frame, kThumbnailMaxSide, &coarse_sws);
          }
          if (thumbnail != nullptr) {
            std::int64_t keyframe =
                frame_index.KeyframeAtOrBeforeFrame(entry_target);
            if (keyframe < 0) {
              keyframe = 0;
            }
            const std::int64_t thumbnail_pts =
                frame_index.PtsOfFrame(keyframe);
            {
              std::lock_guard<std::mutex> lock(mutex);
              if (!stop && active && session == requested_session) {
                thumbnail_cache.Put(keyframe, thumbnail_pts, thumbnail);
                MaybePublishThumbnailLocked(keyframe, requested_session);
              } else {
                av_frame_free(&thumbnail);
              }
            }
          }
        }
        coarse_sent_for_seek = true;
      }
      if (backward) {
        last_backward_preview.reset(av_frame_clone(preview));
        last_backward_pts = info.pts_us;
        last_backward_frame = decoded_frame;
      }
      const bool publish_frame =
          backward ? (info.pts_us >= target_pts)
                   : (!far_forward_jump || info.pts_us >= target_pts);
      AVFrame* publish_copy =
          publish_frame ? av_frame_clone(preview) : nullptr;
      // DP-1：软件帧照旧写 32 帧热缓存；硬件帧绝不直接缓存。DP-2 的环缓存
      // 只服务“已解过的帧会再被访问”的局部/慢速拖动；-20/-60 这种快反向每个
      // 目标都跨 GOP，再存 200MB 也命中不了，只会增加内存与拷贝开销。
      if (!info.hardware) {
        const std::int64_t step =
            has_target_update
                ? (last_target_step >= 0 ? last_target_step : -last_target_step)
                : 0;
        const bool ring_allowed = !has_target_update || step <= 8;
        // 反向拖动时把每个已解码帧再降采样成 320×180 粗帧存起来。软件路径本来
        // 就要解这些帧，额外成本只是一次小 sws；下一次反向目标可以先命中
        // 附近的粗帧（不必再等整段 seek+decode），这正是 -20 的关键。
        if (thumbnails_enabled && backward && step <= 8) {
          AVFrame* coarse =
              ScaleToNv12(preview, kThumbnailMaxSide, &coarse_sws);
          if (coarse != nullptr) {
            std::lock_guard<std::mutex> lock(mutex);
            if (!stop && active && session == requested_session) {
              thumbnail_cache.Put(decoded_frame, info.pts_us, coarse);
            } else {
              av_frame_free(&coarse);
            }
          }
        }
        AVFrame* ring_copy =
            (cache_enabled && ring_allowed) ? av_frame_clone(preview) : nullptr;
        CachePreviewFrame(preview, ring_copy, info.pts_us, decoded_frame,
                          requested_session);
      } else {
        av_frame_free(&preview);
      }
      if (publish_copy != nullptr) {
        Publish(publish_copy, info.pts_us, decoded_frame, requested_session,
                false, false, info.hardware);
      }

      if (info.pts_us >= target_pts) {
        // DP-1：硬件路径到目标就交回 worker 取最新目标；不在同一 slice 里继续
        // 追已经跑远的新目标，否则一次拖动会连续解几十帧、Present 反而变稀。
        if (expect_hardware) {
          return true;
        }
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
        return true;
      }
      if (++guard > 100000) {
        *error_text = "预览解码超过上限";
        return false;
      }
      if (std::chrono::steady_clock::now() - slice_start > kDecodeSlice) {
        slice_start = std::chrono::steady_clock::now();
        std::this_thread::yield();
      }
    }
  }

  // DP-2：独立低优先级预取线程。慢速局部反向拖动时解“上一 GOP”的软件帧
  // （1280 NV12）写入热/环缓存。与预览硬解线程分离，因此不会阻塞当前目标，
  // 也不接触 D3D11 表面池；目标跨 GOP/换会话时逐帧检查并取消。
  void RunPrefetch() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    VideoDecoder decoder;
    bool opened = false;
    SwsContext* prefetch_sws = nullptr;
    std::string decode_error;
    for (;;) {
      std::int64_t request_target = -1;
      std::uint64_t request_session = 0;
      std::string local_path;
      int local_stream = -1;
      const FrameIndex* local_index = nullptr;
      {
        std::unique_lock<std::mutex> lock(mutex);
        prefetch_condition.wait(lock, [this] {
          return prefetch_stop ||
                 (active && prefetch_enabled && cache_enabled &&
                  prefetch_request_target >= 0);
        });
        if (prefetch_stop) {
          break;
        }
        request_target = prefetch_request_target;
        request_session = prefetch_request_session;
        local_path = path;
        local_stream = stream_index;
        local_index = index;
      }
      if (local_index == nullptr || request_target < 0) {
        continue;
      }
      const std::int64_t current_keyframe =
          local_index->KeyframeAtOrBeforeFrame(request_target);
      const std::int64_t previous_keyframe =
          local_index->PreviousKeyframe(current_keyframe);
      if (previous_keyframe < 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        continue;
      }
      bool already_served = false;
      {
        std::lock_guard<std::mutex> lock(mutex);
        already_served = prefetch_served_keyframe == current_keyframe;
      }
      if (already_served) {
        // 本 GOP 已预取完；目标同 GOP 移动时不重复解码，也不空转。
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        continue;
      }
      if (!opened) {
        if (!decoder.Open(local_path, local_stream, false, &decode_error)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          continue;
        }
        opened = true;
      }
      if (!decoder.SeekToPts(local_index->PtsOfFrame(previous_keyframe),
                             &decode_error)) {
        decoder.Close();
        opened = false;
        continue;
      }
      int guard = 0;
      bool cancelled = false;
      for (;;) {
        {
          std::lock_guard<std::mutex> lock(mutex);
          if (prefetch_stop || !active || !prefetch_enabled || !cache_enabled ||
              prefetch_request_session != request_session ||
              prefetch_request_target < 0 ||
              local_index->KeyframeAtOrBeforeFrame(prefetch_request_target) !=
                  current_keyframe) {
            cancelled = true;
            break;
          }
        }
        if (++guard > 300) {
          break;
        }
        DecodedFrameInfo info;
        if (!decoder.DecodeNextFrame(&info, &decode_error)) {
          decoder.Close();
          opened = false;
          break;
        }
        const std::int64_t decoded_frame =
            local_index->FrameOfPts(info.pts_us);
        if (decoded_frame >= current_keyframe) {
          break;
        }
        if (decoded_frame < 0 || info.hardware) {
          continue;
        }
        AVFrame* preview =
            ScaleToNv12(info.av_frame, kPreviewMaxSide, &prefetch_sws);
        if (preview == nullptr) {
          continue;
        }
        AVFrame* ring_copy = av_frame_clone(preview);
        CachePreviewFrame(preview, ring_copy, info.pts_us, decoded_frame,
                          request_session);
        ++prefetched_frames;
      }
      if (!cancelled) {
        std::lock_guard<std::mutex> lock(mutex);
        if (active && prefetch_request_session == request_session &&
            local_index->KeyframeAtOrBeforeFrame(prefetch_request_target) ==
                current_keyframe) {
          prefetch_served_keyframe = current_keyframe;
        }
      }
    }
    if (prefetch_sws != nullptr) {
      sws_freeContext(prefetch_sws);
    }
    decoder.Close();
  }

  // DP-2：关键帧缩略图线程。独立软件解码器/独立读层，不碰主播放或预览硬解
  // 的 D3D11 上下文；只在拖动期间按队列解少量关键帧。
  void RunThumbnails() {
    VideoDecoder decoder;
    bool opened = false;
    SwsContext* thumb_sws = nullptr;
    std::string decode_error;
    for (;;) {
      std::int64_t keyframe = -1;
      std::uint64_t requested_session = 0;
      std::string local_path;
      int local_stream = -1;
      const FrameIndex* local_index = nullptr;
      {
        std::unique_lock<std::mutex> lock(mutex);
        thumbnail_condition.wait(lock, [this] {
          return thumbnail_stop || (active && thumbnails_enabled &&
                                    !thumbnail_queue.empty());
        });
        if (thumbnail_stop) {
          break;
        }
        keyframe = thumbnail_queue.front();
        thumbnail_queue.pop_front();
        requested_session = thumbnail_request_session;
        local_path = path;
        local_stream = stream_index;
        local_index = index;
      }
      if (local_index == nullptr || keyframe < 0) {
        continue;
      }
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (!active || stop || !thumbnails_enabled) {
          continue;
        }
        if (thumbnail_cache.Contains(keyframe)) {
          MaybePublishThumbnailLocked(keyframe, requested_session);
          continue;
        }
      }
      if (!opened) {
        if (!decoder.Open(local_path, local_stream, false, &decode_error)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          continue;
        }
        opened = true;
      }
      const std::int64_t keyframe_pts = local_index->PtsOfFrame(keyframe);
      if (keyframe_pts == std::numeric_limits<std::int64_t>::min()) {
        continue;
      }
      if (!decoder.SeekToPts(keyframe_pts, &decode_error)) {
        decoder.Close();
        opened = false;
        continue;
      }
      DecodedFrameInfo info;
      bool decoded = false;
      for (int attempt = 0; attempt < 4; ++attempt) {
        if (!decoder.DecodeNextFrame(&info, &decode_error)) {
          break;
        }
        if (info.pts_us >= keyframe_pts) {
          decoded = true;
          break;
        }
      }
      if (!decoded) {
        continue;
      }
      AVFrame* thumbnail =
          ScaleToNv12(info.av_frame, kThumbnailMaxSide, &thumb_sws);
      if (thumbnail == nullptr) {
        continue;
      }
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (thumbnail_stop || !active || stop) {
          av_frame_free(&thumbnail);
          break;
        }
        thumbnail_cache.Put(keyframe, keyframe_pts, thumbnail);
        MaybePublishThumbnailLocked(keyframe, requested_session);
      }
    }
    if (thumb_sws != nullptr) {
      sws_freeContext(thumb_sws);
    }
    decoder.Close();
  }

  // DP-2：空闲预取。只在没有待处理目标时走一小段；任何新目标都会在下一帧前
  // 取消。预取只写缓存，不发布；用完后把 position_valid 置 false，强制下个
  // 目标重新 seek，避免把预取位置误当成“已解到目标”。
  void PrefetchStep(VideoDecoder& decoder, const FrameIndex& frame_index,
                    std::int64_t requested_target, int requested_direction,
                    std::uint64_t requested_session, bool expect_hardware) {
    (void)requested_direction;
    (void)expect_hardware;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (!prefetch_enabled || !cache_enabled || !active || stop ||
          session != requested_session || generation != handled_generation ||
          target_frame != requested_target) {
        return;
      }
    }
    const std::int64_t current_keyframe =
        frame_index.KeyframeAtOrBeforeFrame(requested_target);
    if (current_keyframe < 0) {
      return;
    }
    if (prefetch_anchor_keyframe != current_keyframe) {
      prefetch_anchor_keyframe = current_keyframe;
      prefetch_done_keyframe = -1;
      prefetch_start = frame_index.PreviousKeyframe(current_keyframe);
      prefetch_end = current_keyframe - 1;
      prefetch_cursor = prefetch_start;
      prefetch_seeked = false;
    }
    if (prefetch_done_keyframe == current_keyframe) {
      return;
    }
    std::int64_t start = prefetch_start;
    std::int64_t end = prefetch_end;
    std::int64_t cursor = prefetch_cursor;
    if (start < 0 || end < 0 || cursor < 0 || cursor > end) {
      prefetch_done_keyframe = current_keyframe;
      return;
    }
    if (requested_target - start > kPrefetchMaxDistance) {
      prefetch_done_keyframe = current_keyframe;
      return;
    }
    if (!prefetch_seeked) {
      // 与 Process 的缓存命中一样：CloneExactLocked 必须持锁；裸调会和 UI/
      // 预取线程的 eviction 竞争（同上的堆/AVFrame 竞争窗口）。
      AVFrame* cached = nullptr;
      {
        std::lock_guard<std::mutex> lock(mutex);
        cached = CloneExactLocked(cursor, requested_session);
      }
      if (cached != nullptr) {
        av_frame_free(&cached);
        prefetch_cursor = cursor + 1;
        return;
      }
      if (!decoder.SeekToPts(frame_index.PtsOfFrame(start), nullptr)) {
        prefetch_seeked = false;
        return;
      }
      prefetch_seeked = true;
      position_valid = false;
    }
    for (int i = 0; i < kPrefetchFramesPerIdle; ++i) {
      if (cursor > end) {
        prefetch_done_keyframe = current_keyframe;
        break;
      }
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (stop || !active || session != requested_session ||
            generation != handled_generation || target_frame != requested_target) {
          position_valid = false;
          return;
        }
      }
      DecodedFrameInfo info;
      if (!decoder.DecodeNextFrame(&info, nullptr)) {
        position_valid = false;
        prefetch_seeked = false;
        prefetch_done_keyframe = current_keyframe;
        return;
      }
      const std::int64_t decoded_frame = frame_index.FrameOfPts(info.pts_us);
      if (decoded_frame < 0) {
        continue;
      }
      if (!info.hardware) {
        AVFrame* preview = ScalePreviewFrame(info.av_frame);
        if (preview != nullptr) {
          AVFrame* ring_copy = av_frame_clone(preview);
          CachePreviewFrame(preview, ring_copy, info.pts_us, decoded_frame,
                            requested_session);
          ++prefetched_frames;
        }
      }
      cursor = decoded_frame + 1;
      prefetch_cursor = cursor;
      if (decoded_frame >= end) {
        prefetch_done_keyframe = current_keyframe;
        break;
      }
    }
    prefetch_cursor = cursor;
    prefetch_seeked = true;
    position_valid = false;
  }

  void Run() {
    // 预览频繁 seek 会触发 H.264 “missing picture in access unit” 警告；
    // 只保留 error 级别，避免刷屏影响 UI/日志与测量。
    av_log_set_level(AV_LOG_ERROR);

    std::string local_path;
    int local_stream = -1;
    ID3D11Device* local_device = nullptr;
    bool local_prefer_hardware = true;
    int local_width = 0;
    int local_height = 0;
    {
      std::lock_guard<std::mutex> lock(mutex);
      local_path = path;
      local_stream = stream_index;
      local_device = device;
      local_prefer_hardware = prefer_hardware;
      local_width = source_width;
      local_height = source_height;
    }

    VideoDecoder decoder;
    std::string open_error_text;
    bool opened = false;
    bool hardware = false;
    std::string fallback_reason;
    const bool env_force_software =
        std::getenv("LAPIAN_PREVIEW_FORCE_SOFTWARE") != nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (force_software) {
        fallback_reason = hw_fallback_reason.empty() ? "forced_software"
                                                     : hw_fallback_reason;
      }
    }
    const bool source_supported =
        local_width <= 0 || local_height <= 0 ||
        (local_width <= 1920 && local_height <= 1088);
    if (fallback_reason.empty() && local_prefer_hardware && !source_supported) {
      fallback_reason = "source_too_large: " + std::to_string(local_width) + "x" +
                        std::to_string(local_height);
    }
    const bool want_hardware =
        local_prefer_hardware && local_device != nullptr && !env_force_software &&
        fallback_reason.empty() && source_supported;
    if (want_hardware) {
      std::string hardware_error;
      if (decoder.OpenWithD3D11Device(local_path, local_stream, local_device, true,
                                      &hardware_error, kPreviewExtraHwFrames)) {
        hardware = decoder.HardwareActive();
        opened = hardware;
        if (!hardware) {
          fallback_reason = "hardware_inactive";
          open_error_text = "硬件预览未激活";
          decoder.Close();
        }
      } else {
        fallback_reason = "hardware_open_failed";
        open_error_text = hardware_error.empty()
                              ? "硬件预览打开失败"
                              : "硬件预览打开失败: " + hardware_error;
        if (!hardware_error.empty()) {
          std::string detail = hardware_error;
          if (detail.size() > 160) {
            detail.resize(160);
          }
          fallback_reason += ": " + detail;
        }
      }
    } else if (fallback_reason.empty()) {
      if (env_force_software) {
        fallback_reason = "forced_software";
      } else if (!local_prefer_hardware) {
        fallback_reason = "software_requested";
      } else {
        fallback_reason = "device_unavailable";
      }
    }
    if (!opened) {
      // 软件路径：最长边 1280 降采样，保持 T2.9/T8.17 的对象缓存与反向行为。
      std::string software_error;
      opened = decoder.Open(local_path, local_stream, false, &software_error);
      hardware = false;
      if (!opened) {
        const std::string software_text =
            software_error.empty() ? "预览解码器打开失败" : software_error;
        if (open_error_text.empty()) {
          open_error_text = software_text;
        } else {
          open_error_text += "; 软解回退失败: " + software_text;
        }
        if (!fallback_reason.empty()) {
          fallback_reason = "software_open_failed: " + software_text;
        }
      }
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      open_attempted = true;
      open_ok = opened;
      hardware_in_use = hardware;
      open_error = opened ? std::string() : open_error_text;
      if (hardware) {
        hw_fallback_reason.clear();
      } else {
        hw_fallback_reason = fallback_reason;
      }
      if (!opened) {
        error = open_error_text.empty() ? "预览解码器打开失败" : open_error_text;
        active = false;
      }
    }

    for (;;) {
      std::uint64_t requested_generation = 0;
      std::uint64_t requested_session = 0;
      std::int64_t target = 0;
      int direction = 0;
      const FrameIndex* current_index = nullptr;
      bool can_decode = false;
      {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this] { return stop || active; });
        if (stop) {
          break;
        }
        if (generation == handled_generation) {
          // DP-2：没有待处理目标时用 2ms 空闲窗口预取上一 GOP；任何新目标都会
          // 打断，因此预取不会阻塞当前目标。
          const bool signaled = condition.wait_for(lock, kIdleWait, [this] {
            return stop || !active || generation != handled_generation;
          });
          if (stop) {
            break;
          }
          if (!signaled) {
            const std::int64_t prefetch_target_frame = target_frame;
            const int prefetch_direction = target_direction;
            const std::uint64_t prefetch_session = session;
            current_index = index;
            lock.unlock();
            if (current_index != nullptr) {
              PrefetchStep(decoder, *current_index, prefetch_target_frame,
                           prefetch_direction, prefetch_session, hardware);
            }
            continue;
          }
        }
        if (!active) {
          continue;
        }
        requested_generation = generation;
        requested_session = session;
        target = target_frame;
        direction = target_direction;
        current_index = index;
        can_decode = open_ok;
      }

      bool force_now = false;
      {
        std::lock_guard<std::mutex> lock(mutex);
        force_now = force_software && hardware;
      }
      if (force_now) {
        std::string fallback_error;
        if (ApplySoftwareFallback(decoder, "forced_software", &fallback_error)) {
          hardware = false;
        } else {
          SetError(fallback_error.empty() ? "软解回退失败" : fallback_error);
        }
      }

      bool retry = false;
      if (can_decode && current_index != nullptr) {
        {
          std::lock_guard<std::mutex> lock(mutex);
          processing = true;
        }
        std::string process_error;
        const bool ok =
            Process(decoder, *current_index, target, direction, requested_session,
                    hardware, &process_error);
        bool recovered = false;
        if (!ok) {
          if (hardware) {
            std::string fallback_error;
            std::string reason = "hardware_decode_failed";
            if (!process_error.empty()) {
              reason += ": " + process_error;
            }
            if (ApplySoftwareFallback(decoder, reason, &fallback_error)) {
              hardware = false;
              recovered = true;
            } else {
              SetError(process_error + "; 软解回退失败: " + fallback_error);
            }
          } else {
            SetError(process_error.empty() ? "预览解码失败" : process_error);
          }
        }
        {
          std::lock_guard<std::mutex> lock(mutex);
          processing = false;
          idle_condition.notify_all();
        }
        retry = recovered;
      }

      {
        std::lock_guard<std::mutex> lock(mutex);
        if (!retry) {
          // 用“进入 Process 时的版本”做 handled：如果目标在解码过程中变了，
          // 下一轮会再处理一次；若 Process 已经追到最新目标，这一轮会快速返回。
          handled_generation = requested_generation;
        }
      }
    }
  }
};

PreviewDecoder::PreviewDecoder() : impl_(std::make_unique<Impl>()) {}

PreviewDecoder::~PreviewDecoder() = default;

void PreviewDecoder::Configure(const std::string& utf8_path, int video_stream_index,
                               const FrameIndex* index, ID3D11Device* device,
                               bool prefer_hardware, int source_width,
                               int source_height) {
  impl_->Close();
  if (utf8_path.empty() || index == nullptr) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->path = utf8_path;
    impl_->stream_index = video_stream_index;
    impl_->index = index;
    impl_->device = device;
    impl_->prefer_hardware = prefer_hardware;
    impl_->source_width = source_width;
    impl_->source_height = source_height;
    impl_->force_software = false;
    impl_->hw_fallback_reason.clear();
    impl_->open_error.clear();
    impl_->hardware_in_use = false;
    impl_->stop = false;
    impl_->active = false;
    impl_->processing = false;
    impl_->target_frame = 0;
    impl_->generation = 0;
    impl_->handled_generation = 0;
    impl_->session = 0;
    impl_->open_attempted = false;
    impl_->open_ok = false;
    impl_->error.clear();
    impl_->seeks = 0;
    impl_->cache_hits = 0;
    impl_->cache_misses = 0;
    impl_->coarse_hits = 0;
    impl_->coarse_requests = 0;
    impl_->coarse_published = 0;
    impl_->prefetched_frames = 0;
    impl_->readbacks = 0;
    impl_->decoded_frames = 0;
    impl_->published_count = 0;
    impl_->decoder_reopens = 0;
    impl_->seek_us_total = 0;
    impl_->decode_us_total = 0;
    impl_->readback_us_total = 0;
    impl_->first_coarse_us = -1;
    impl_->last_coarse_us = -1;
    impl_->last_published_frame = -1;
    impl_->last_delivered_frame = -1;
    impl_->last_delivered_coarse = false;
    impl_->drag_mode = DragMode::Exact;
    impl_->coarse_undelivered = false;
    impl_->FreeDeferredExactLocked();
    impl_->last_target_step = 0;
    impl_->has_target_update = false;
    impl_->coarse_pending = false;
    impl_->prefetch_target = -1;
    impl_->prefetch_start = -1;
    impl_->prefetch_end = -1;
    impl_->prefetch_cursor = -1;
    impl_->prefetch_anchor_keyframe = -1;
    impl_->prefetch_done_keyframe = -1;
    impl_->prefetch_seeked = false;
    impl_->prefetch_remaining = 0;
    impl_->thumbnail_queue.clear();
    impl_->thumbnail_request_keyframe = -1;
    impl_->thumbnail_request_target = -1;
    impl_->thumbnail_request_session = 0;
    impl_->prefetch_stop = false;
    impl_->prefetch_request_target = -1;
    impl_->prefetch_request_session = 0;
    impl_->prefetch_served_keyframe = -1;
    impl_->cache_peak_bytes = 0;
    impl_->cache_enabled =
        std::getenv("LAPIAN_PREVIEW_DISABLE_CACHE") == nullptr;
    impl_->prefetch_enabled =
        std::getenv("LAPIAN_PREVIEW_DISABLE_PREFETCH") == nullptr;
    impl_->thumbnails_enabled =
        std::getenv("LAPIAN_PREVIEW_DISABLE_THUMBNAILS") == nullptr;
  }
  impl_->thread = std::thread([impl = impl_.get()] { impl->Run(); });
  impl_->thumbnail_thread =
      std::thread([impl = impl_.get()] { impl->RunThumbnails(); });
  impl_->prefetch_thread =
      std::thread([impl = impl_.get()] { impl->RunPrefetch(); });
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
  impl_->last_target_step = 0;
  impl_->has_target_update = false;
  impl_->coarse_pending = false;
  impl_->first_coarse_us = -1;
  impl_->last_coarse_us = -1;
  impl_->prefetch_start = -1;
  impl_->prefetch_end = -1;
  impl_->prefetch_cursor = -1;
  impl_->prefetch_anchor_keyframe = -1;
  impl_->prefetch_done_keyframe = -1;
  impl_->prefetch_seeked = false;
  impl_->prefetch_served_keyframe = -1;
  if (impl_->latest != nullptr) {
    av_frame_free(&impl_->latest);
  }
  impl_->FreeDeferredExactLocked();
  impl_->coarse_undelivered = false;
  impl_->latest_pts_us = 0;
  impl_->latest_frame_index = -1;
  impl_->latest_generation = 0;
  impl_->latest_coarse = false;
  impl_->latest_from_cache = false;
  impl_->latest_hardware = false;
  impl_->seeks = 0;
  impl_->cache_hits = 0;
  impl_->cache_misses = 0;
  impl_->coarse_hits = 0;
  impl_->coarse_requests = 0;
  impl_->coarse_published = 0;
  impl_->prefetched_frames = 0;
  impl_->readbacks = 0;
  impl_->decoded_frames = 0;
  impl_->published_count = 0;
  impl_->seek_us_total = 0;
  impl_->decode_us_total = 0;
  impl_->readback_us_total = 0;
  impl_->last_published_frame = -1;
  impl_->last_delivered_frame = frame + 1;
  impl_->last_delivered_coarse = false;
  // DP-2：环缓存跨拖动保留（有界），但只保留目标附近窗口；热缓存仍按 32 帧。
  impl_->ring_cache.TrimToWindow(frame, kPreviewCacheWindowFrames);
  impl_->thumbnail_cache.TrimToWindow(frame, kThumbnailWindowFrames);

  // 目标查询口径：每次 Begin/UpdateTarget 都记录一次命中/未命中，分母清晰。
  bool exact_hit = false;
  if (impl_->cache_enabled) {
    exact_hit = impl_->TryPublishCachedTargetLocked(frame, impl_->session);
    if (exact_hit) {
      ++impl_->cache_hits;
    } else {
      ++impl_->cache_misses;
    }
  }
  if (!exact_hit && impl_->drag_mode != DragMode::Exact) {
    ++impl_->coarse_requests;
    impl_->coarse_pending = true;
    impl_->coarse_request_time = std::chrono::steady_clock::now();
    // 拖动起点只接受“几乎正好”的粗帧（≤2 帧）；否则宁可不显示，避免从
    // 当前画面突然退回一个旧关键帧。远跳仍由 UpdateTarget/worker 出粗帧。
    if (impl_->PublishCoarseLocked(frame, impl_->session, true, 2)) {
      ++impl_->coarse_hits;
    }
  }
  impl_->RequestPrefetchLocked(frame, impl_->session);
  impl_->condition.notify_all();
}

void PreviewDecoder::UpdateTarget(std::int64_t frame) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->active) {
    return;
  }
  const std::int64_t old_target = impl_->target_frame;
  if (frame == old_target) {
    return;
  }
  const std::int64_t step = frame >= old_target ? frame - old_target
                                                : old_target - frame;
  impl_->last_target_step = step;
  impl_->has_target_update = true;
  if (frame < old_target) {
    impl_->target_direction = -1;
  } else if (frame > old_target) {
    impl_->target_direction = 1;
  }
  impl_->target_frame = frame;
  // 目标已经变化，为上一目标暂存的精确帧立即作废（远跳闸门）。
  impl_->FreeDeferredExactLocked();

  // DP-2：每次目标更新先查精确缓存；命中就立即发布，不等 worker seek。
  bool exact_hit = false;
  if (impl_->cache_enabled) {
    exact_hit = impl_->TryPublishCachedTargetLocked(frame, impl_->session);
    if (exact_hit) {
      ++impl_->cache_hits;
    } else {
      ++impl_->cache_misses;
    }
  }
  if (!exact_hit && impl_->CoarseEligibleLocked(old_target, frame)) {
    ++impl_->coarse_requests;
    impl_->coarse_pending = true;
    impl_->coarse_request_time = std::chrono::steady_clock::now();
    if (impl_->PublishCoarseLocked(frame, impl_->session, true)) {
      ++impl_->coarse_hits;
    }
  }
  if (step > kPreviewCacheWindowFrames) {
    impl_->ring_cache.TrimToWindow(frame, kPreviewCacheWindowFrames);
    impl_->thumbnail_cache.TrimToWindow(frame, kThumbnailWindowFrames);
  }
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
      impl_->latest_coarse = false;
      impl_->latest_from_cache = false;
      impl_->latest_hardware = false;
    }
  }
  ++impl_->generation;
  if (step >= kCoarseMinForwardJump) {
    impl_->RequestThumbnailsLocked(frame, impl_->session);
  }
  if (step <= kDragExactMaxStep && impl_->target_direction < 0) {
    impl_->RequestPrefetchLocked(frame, impl_->session);
  } else {
    impl_->ClearPrefetchLocked();
  }
  impl_->condition.notify_all();
}

void PreviewDecoder::SetDragMode(DragMode mode) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->drag_mode = mode;
  if (mode == DragMode::Exact && impl_->latest != nullptr &&
      impl_->latest_coarse) {
    // 从快拖/远跳切回慢拖：尚未呈现的粗帧立即作废，避免关键帧粗帧打断
    // 精确目标；若已有暂存的精确帧，则直接提升。
    av_frame_free(&impl_->latest);
    impl_->latest_pts_us = 0;
    impl_->latest_frame_index = -1;
    impl_->latest_generation = 0;
    impl_->latest_coarse = false;
    impl_->latest_from_cache = false;
    impl_->latest_hardware = false;
    impl_->coarse_undelivered = false;
    impl_->PromoteDeferredExactLocked();
  }
}

void PreviewDecoder::End() {
  bool hardware = false;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->active = false;
    impl_->target_direction = 0;
    ++impl_->generation;
    ++impl_->session;
    hardware = impl_->hardware_in_use;
    if (impl_->latest != nullptr) {
      av_frame_free(&impl_->latest);
    }
    impl_->FreeDeferredExactLocked();
    impl_->coarse_undelivered = false;
    // DP-2：松手后保留有界缓存供下一次拖动命中，但收窄到空闲预算；
    // 热缓存仍保留 32 帧（DP-1 容量不变）。
    impl_->ring_cache.TrimToBudget(kPreviewRingIdleBytes);
    impl_->thumbnail_cache.TrimToBudget(kPreviewThumbIdleBytes);
    impl_->thumbnail_queue.clear();
    impl_->latest_pts_us = 0;
    impl_->latest_frame_index = -1;
    impl_->latest_generation = 0;
    impl_->latest_coarse = false;
    impl_->latest_from_cache = false;
    impl_->latest_hardware = false;
    impl_->coarse_pending = false;
    impl_->last_delivered_frame = -1;
    impl_->last_delivered_coarse = false;
    impl_->target_direction = 0;
    impl_->condition.notify_all();
    impl_->thumbnail_condition.notify_all();
    impl_->prefetch_request_target = -1;
    impl_->prefetch_condition.notify_all();
    impl_->UpdateCachePeakLocked();
  }
  // DP-1：预览硬解与主播放硬解共用同一个 D3D11 视频上下文；松手后马上要
  // GoToFrame 精确解码，必须等预览退出当前解码切片。有界等待，超时继续。
  if (hardware) {
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->idle_condition.wait_for(
        lock, kEndWait, [this] { return !impl_->processing || impl_->stop; });
  }
}

void PreviewDecoder::Close() { impl_->Close(); }

PreviewFrame PreviewDecoder::TakeLatestFrame() {
  PreviewFrame result;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->active || impl_->latest == nullptr) {
    return result;
  }
  const bool ahead_of_target = impl_->latest_frame_index > impl_->target_frame;
  const bool behind_target = impl_->latest_frame_index < impl_->target_frame;
  const bool backward_mismatch =
      impl_->target_direction < 0 &&
      impl_->latest_frame_index != impl_->target_frame;
  const bool duplicate_exact =
      !impl_->latest_coarse && !impl_->last_delivered_coarse &&
      impl_->latest_frame_index == impl_->last_delivered_frame;
  bool discard = duplicate_exact;
  if (!discard) {
    if (impl_->latest_coarse) {
      // DP-2 粗帧：只允许“目标之前、且不倒退”的缩略图/降采样帧；反向时还必须
      // 比上一张已显示帧更早，正向时不能比上一张更早（否则会向后跳）。
      if (ahead_of_target) {
        discard = true;
      } else if (impl_->target_direction < 0 &&
                 impl_->last_delivered_frame >= 0 &&
                 impl_->latest_frame_index >= impl_->last_delivered_frame) {
        discard = true;
      } else if (impl_->target_direction > 0 &&
                 impl_->last_delivered_frame >= 0 &&
                 impl_->latest_frame_index <= impl_->last_delivered_frame) {
        discard = true;
      }
      // DP-3：精确目标已经呈现后，任何粗帧都不得再替换它（尤其是远跳反向
      // 落定后缩略图线程补发的更早关键帧，会把画面拉回目标之前）。
      if (impl_->last_delivered_frame == impl_->target_frame &&
          !impl_->last_delivered_coarse) {
        discard = true;
      }
    } else {
      // 1) 目标回退后，比当前目标更靠后的旧帧通常丢弃；
      // 2) 快速反向拖动时，解码线程完成的往往是“上一目标”的帧：它比当前目标晚，
      //    却比已经显示过的上一帧早。允许这种粗帧，画面才会持续后退而不是整段空住。
      bool accept_coarse = false;
      if (ahead_of_target) {
        accept_coarse = impl_->target_direction < 0 &&
                        impl_->last_delivered_frame >= 0 &&
                        impl_->latest_frame_index < impl_->last_delivered_frame;
      }
      if ((ahead_of_target && !accept_coarse) ||
          (backward_mismatch && behind_target)) {
        discard = true;
      }
    }
  }
  if (discard) {
    av_frame_free(&impl_->latest);
    impl_->latest_pts_us = 0;
    impl_->latest_frame_index = -1;
    impl_->latest_generation = 0;
    impl_->latest_coarse = false;
    impl_->latest_from_cache = false;
    impl_->latest_hardware = false;
    impl_->coarse_undelivered = false;
    // 被丢弃的可能是过期粗帧；为当前目标暂存的精确帧仍然有效，立即提升。
    impl_->PromoteDeferredExactLocked();
    return result;
  }
  const bool delivered_coarse = impl_->latest_coarse;
  result.frame = impl_->latest;
  result.pts_us = impl_->latest_pts_us;
  result.frame_index = impl_->latest_frame_index;
  result.generation = impl_->latest_generation;
  result.coarse = impl_->latest_coarse;
  result.from_cache = impl_->latest_from_cache;
  result.hardware = impl_->latest_hardware;
  impl_->last_delivered_frame = impl_->latest_frame_index;
  impl_->last_delivered_coarse = impl_->latest_coarse;
  if (impl_->latest_coarse) {
    if (impl_->coarse_pending) {
      const std::int64_t elapsed_us = static_cast<std::int64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - impl_->coarse_request_time)
              .count());
      if (impl_->first_coarse_us.load() < 0) {
        impl_->first_coarse_us = elapsed_us;
      }
      impl_->last_coarse_us = elapsed_us;
      impl_->coarse_pending = false;
    }
  } else if (impl_->coarse_pending) {
    // 精确帧先到，粗帧不再需要；不要把它算成“首张粗帧”。
    impl_->coarse_pending = false;
  }
  impl_->latest = nullptr;
  impl_->latest_frame_index = -1;
  impl_->latest_coarse = false;
  impl_->latest_from_cache = false;
  impl_->latest_hardware = false;
  impl_->coarse_undelivered = false;
  // DP-3：粗帧已经交给 UI，下一 tick 把暂存的精确目标提升为 latest，
  // 保证“先粗后精”而不是让精确帧在同一 tick 覆盖粗帧。
  if (delivered_coarse) {
    impl_->PromoteDeferredExactLocked();
  } else {
    impl_->FreeDeferredExactLocked();
  }
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

PreviewDiagnostics PreviewDecoder::diagnostics() const {
  PreviewDiagnostics result;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  result.open_attempted = impl_->open_attempted;
  result.open_ok = impl_->open_ok;
  result.hw_requested = impl_->prefer_hardware;
  result.hw_active = impl_->hardware_in_use;
  result.hw_fallback_reason = impl_->hw_fallback_reason;
  result.open_error = impl_->open_error;
  result.seeks = impl_->seeks.load();
  result.cache_hits = impl_->cache_hits.load();
  result.cache_misses = impl_->cache_misses.load();
  result.cache_requests = result.cache_hits + result.cache_misses;
  result.coarse_hits = impl_->coarse_hits.load();
  result.coarse_requests = impl_->coarse_requests.load();
  result.coarse_published = impl_->coarse_published.load();
  result.prefetched_frames = impl_->prefetched_frames.load();
  result.readbacks = impl_->readbacks.load();
  result.decoded_frames = impl_->decoded_frames.load();
  result.published = impl_->published_count.load();
  result.decoder_reopens = impl_->decoder_reopens.load();
  const PreviewRingCache::Stats ring = impl_->ring_cache.stats();
  result.cache_evictions = ring.evictions;
  result.cache_frames = impl_->cache.size() + ring.entries;
  result.cache_bytes = impl_->cache_bytes + ring.bytes;
  result.cache_peak_bytes = impl_->cache_peak_bytes;
  const PreviewThumbnailCache::Stats thumbs = impl_->thumbnail_cache.stats();
  result.thumbnail_frames = thumbs.entries;
  result.thumbnail_bytes = thumbs.bytes;
  result.thumbnail_peak_bytes = thumbs.peak_bytes;
  const int seeks = result.seeks;
  const int decoded = result.decoded_frames;
  const int readbacks = result.readbacks;
  result.seek_ms_avg =
      seeks > 0 ? static_cast<double>(impl_->seek_us_total.load()) / seeks /
                      1000.0
                : 0.0;
  result.decode_ms_avg =
      decoded > 0
          ? static_cast<double>(impl_->decode_us_total.load()) / decoded / 1000.0
          : 0.0;
  result.readback_ms_avg =
      readbacks > 0
          ? static_cast<double>(impl_->readback_us_total.load()) / readbacks /
                1000.0
          : 0.0;
  const std::int64_t first_coarse = impl_->first_coarse_us.load();
  const std::int64_t last_coarse = impl_->last_coarse_us.load();
  result.first_coarse_ms =
      first_coarse >= 0 ? static_cast<double>(first_coarse) / 1000.0 : -1.0;
  result.last_coarse_ms =
      last_coarse >= 0 ? static_cast<double>(last_coarse) / 1000.0 : -1.0;
  result.cache_enabled = impl_->cache_enabled;
  result.prefetch_enabled = impl_->prefetch_enabled;
  result.thumbnails_enabled = impl_->thumbnails_enabled;
  result.drag_mode = impl_->drag_mode;
  result.target_frame = impl_->target_frame;
  result.last_published_frame = impl_->last_published_frame.load();
  return result;
}

bool PreviewDecoder::DeviceMatches(ID3D11Device* device) const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->device == device;
}

void PreviewDecoder::ForceSoftwareFallback(const std::string& reason) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->force_software) {
    return;
  }
  impl_->force_software = true;
  if (!reason.empty()) {
    impl_->hw_fallback_reason = reason;
  }
  ++impl_->generation;
  impl_->condition.notify_all();
}

}  // namespace lapian::media
