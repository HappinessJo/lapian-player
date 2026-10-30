#include "media/OnlinePreviewDecoder.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

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

#if defined(_WIN32)
#include <windows.h>
#endif

#include "media/FrameIndex.h"
#include "media/VideoDecoder.h"
#include "online/GrowingSource.h"

namespace lapian::media {
namespace {

// 预览解码主动让出 CPU 的切片；与本地 PreviewDecoder 相同。
constexpr auto kDecodeSlice = std::chrono::milliseconds(15);
// D2：默认预览最长边 1024 / 缓存预算 64MiB（与旧编译期常量一致；未调用
// Configure 的旧调用方行为不变）。16:9 一帧 1024x576 NV12 约 0.84MiB，
// 默认预算按公式约 75 帧；峰值只随实际缓存帧增长，不预分配。
constexpr int kDefaultPreviewMaxSide = 1024;
constexpr std::size_t kDefaultCacheBudgetBytes = 64u * 1024u * 1024u;
// D2：Configure 的硬边界（设置下限/内存 25% 夹取在 MainWindow/设置层完成，
// 解码器只保证自身不越界）。
constexpr int kMinPreviewMaxSide = 256;
constexpr int kMaxPreviewMaxSide = 1920;
constexpr std::size_t kMinCacheBudgetBytes = 8u * 1024u * 1024u;
constexpr std::size_t kMaxCacheBudgetBytes = 4096ull * 1024ull * 1024ull;
// 远跳粗帧最长边 320；只用于“先出一张画面”，随后必须精确目标或更近缓存。
constexpr int kCoarseMaxSide = 320;

// D2：16:9 估计帧高（偶数）与 NV12 帧字节；只用于算帧数上限，不参与分配。
int EstimatedFrameHeight(int max_side) {
  int height = max_side * 9 / 16;
  if (height < 2) {
    height = 2;
  }
  if ((height & 1) != 0) {
    ++height;
  }
  return height;
}

std::size_t EstimatedFrameBytes(int max_side) {
  if (max_side <= 0) {
    return 0;
  }
  return static_cast<std::size_t>(max_side) *
         static_cast<std::size_t>(EstimatedFrameHeight(max_side)) * 3u / 2u;
}

int FrameLimitFor(int max_side, std::size_t cache_budget_bytes) {
  const std::size_t frame_bytes = EstimatedFrameBytes(max_side);
  if (frame_bytes == 0) {
    return 8;
  }
  const std::size_t frames = cache_budget_bytes / frame_bytes;
  return static_cast<int>(std::max<std::size_t>(8, frames));
}

// 正向连续解码窗口；目标在该窗口内直接续解，不重新 seek。
constexpr std::int64_t kContinueWindowUs = 2'000'000;
// 目标在解码期间回退、当前目标先命中缓存时，把当前目标之后最多 10 帧
// 继续解完并缓存（不发布），避免下一次向前拖动缺帧重新 seek（长 GOP 冷启动
// 后缓存窗口上沿不完整）。
constexpr std::int64_t kCompletionLookaheadFrames = 10;
// 反向预取：只解“目标关键帧之前最近 30 帧”，覆盖连续反向拖动。
constexpr std::int64_t kBackwardPrefetchFrames = 30;
// 远跳阈值（与 DragStrategy FarJump 一致）：>=300 帧才先出粗帧。
constexpr std::int64_t kFarJumpFrames = 300;
// 一次 Process 最多服务多少个目标（避免目标抖动时无限循环）。
constexpr int kMaxProcessIterations = 6;
// 目标跨到解码起点另一侧超过该步长时，放弃当前解码重新服务新目标。
constexpr std::int64_t kOppositeSideStep = 8;

struct CachedPreview {
  std::int64_t pts_us = 0;
  std::int64_t frame_index = -1;
  AVFrame* frame = nullptr;
};

std::int64_t Distance(std::int64_t a, std::int64_t b) {
  return a >= b ? a - b : b - a;
}

bool LogEnabled() {
  static const bool enabled =
      std::getenv("LAPIAN_ONLINE_PREVIEW_LOG") != nullptr;
  return enabled;
}

void Log(const char* format, ...) {
  if (!LogEnabled()) {
    return;
  }
  static const auto log_start = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - log_start)
                           .count();
  va_list arguments;
  va_start(arguments, format);
  std::fprintf(stderr, "OPD t=%lldms ", static_cast<long long>(elapsed));
  std::vfprintf(stderr, format, arguments);
  std::fprintf(stderr, "\n");
  std::fflush(stderr);
  va_end(arguments);
}

// D4（仅测试）：最近创建的实例，供探针在真实会话里 dump 有界缓存帧。
// 生命周期跟随构造函数/析构函数；产品代码不读它。
std::atomic<OnlinePreviewDecoder*> g_latest_instance{nullptr};

}  // namespace

struct OnlinePreviewDecoder::Impl {
  // 一次目标/索引快照；worker 解码期间目标仍可变化，发布判定读最新值。
  struct TargetSnapshot {
    bool active = false;
    bool cached = false;
    std::int64_t target = 0;
    std::uint64_t generation = 0;
    int direction = 0;
    std::shared_ptr<const FrameIndex> index;
  };

  mutable std::mutex mutex;
  std::condition_variable condition;
  std::thread thread;
  std::thread prefetch_thread;

  bool running = false;
  bool stop = false;
  bool active = false;
  // Pause 时让 worker/预取线程退出并关闭 reader（不释缓存），Resume 重启线程。
  bool pause_requested = false;

  std::shared_ptr<lapian::online::GrowingSource> source;
  std::string path;
  int stream_index = -1;
  std::shared_ptr<const FrameIndex> index;
  std::shared_ptr<std::atomic<bool>> interrupt;

  std::int64_t target_frame = 0;
  bool target_cached = false;
  int target_direction = 0;
  std::uint64_t generation = 0;
  std::uint64_t handled_generation = 0;
  bool waiting = true;

  bool open_attempted = false;
  bool open_ok = false;
  std::string error;

  AVFrame* latest = nullptr;
  std::int64_t latest_pts_us = 0;
  std::int64_t latest_frame_index = -1;
  std::uint64_t latest_generation = 0;
  bool latest_coarse = false;
  bool latest_from_cache = false;

  // 上一次真正被 UI 取走的帧；发布判定以它为基准，保证单调靠近目标。
  std::int64_t last_delivered_frame = -1;
  bool last_delivered_coarse = false;

  std::deque<CachedPreview> cache;
  std::size_t cache_bytes = 0;
  std::size_t cache_peak_bytes = 0;
  // D2：运行时预览配置（Begin 前 Configure 生效；默认 1024/64MiB）。
  int max_side = kDefaultPreviewMaxSide;
  std::size_t cache_budget_bytes = kDefaultCacheBudgetBytes;
  bool begin_called = false;
  // 最近发布/缓存帧的实际尺寸（NV12），供探针证明清晰度配置真的生效。
  int preview_width = 0;
  int preview_height = 0;
  // 分配失败回退：先降预算 75% 重试一次，仍失败回退 1024/64MiB。
  bool alloc_fallback_used = false;
  bool alloc_budget_shrunk = false;
  std::uint64_t alloc_failures = 0;
  int forced_alloc_failures = 0;
  SwsContext* sws = nullptr;
  SwsContext* coarse_sws = nullptr;

  std::int64_t last_pts_us = 0;
  std::int64_t last_decoded_frame = -1;
  bool position_valid = false;

  // 反向预取请求：保证 [prefetch_from, prefetch_to] 进缓存（目标回退方向的
  // 下一段，通常是“缓存窗口下沿之前的 30 帧”）。
  std::condition_variable prefetch_condition;
  bool prefetch_stop = false;
  std::int64_t prefetch_from = -1;
  std::int64_t prefetch_to = -1;
  // 只在“请求区间/方向真正变化”时递增，避免 60Hz 目标更新反复取消
  // 正在预取的同一段。
  std::uint64_t prefetch_epoch = 0;
  std::int64_t prefetch_served_to = -1;

  std::uint64_t frames_published = 0;
  std::uint64_t stale_dropped = 0;
  std::uint64_t seek_count = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t cache_misses = 0;
  std::uint64_t coarse_published = 0;
  std::uint64_t prefetched_frames = 0;
  std::uint64_t decoded_frames = 0;
  std::int64_t last_published_frame = -1;

  // P0 度量（只读）：每次 Begin/Resume 重置；不参与任何发布/解码判定。
  DragStats drag_stats;
  std::chrono::steady_clock::time_point drag_stats_start{};
  bool drag_stats_started = false;

  // P0 seek 诊断（只读）：strict 关键帧 seek 失败回退、最近一次 seek 落点。
  std::uint64_t seek_fallbacks = 0;
  std::int64_t last_seek_keyframe = -1;
  std::int64_t last_seek_landing_frame = -1;
  std::int64_t last_seek_landing_offset = 0;
  bool seek_landing_pending = false;

  // P0 预热（Warm）：静默、低优先级；完成后线程/decoder/reader 保持 idle。
  bool warm_requested = false;
  bool warming = false;
  bool warm_idle = true;
  std::uint64_t warm_epoch = 0;
  std::int64_t warm_origin = -1;
  std::int64_t warm_target = -1;
  std::chrono::steady_clock::time_point warm_start{};
  bool warm_started = false;
  std::int64_t warm_ms = -1;
  std::int64_t warm_frames = 0;
  std::uint64_t warm_skipped = 0;
  std::uint64_t warm_failures = 0;

  // P0 PrimeFrame：线程未启动时暂存，Run 打开解码器后处理。
  struct PendingPrime {
    AVFrame* frame = nullptr;
    std::int64_t frame_index = -1;
  };
  std::deque<PendingPrime> pending_primes;
  bool prime_requested = false;
  std::uint64_t primed_frames = 0;

  ~Impl() { Close(); }

  void ResetDragStatsLocked() {
    drag_stats = DragStats{};
    drag_stats_start = std::chrono::steady_clock::now();
    drag_stats_started = true;
  }

  std::int64_t DragStatsElapsedMsLocked() const {
    if (!drag_stats_started) {
      return -1;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - drag_stats_start)
        .count();
  }

  // P0：seek 到 frame 之前最近关键帧的 PTS。必须 strict 钉住关键帧；
  // fMP4 的宽松 av_seek_frame 会落到前一个 GOP（S1 实测早 150 帧）。
  bool SeekToPreviewKeyframe(VideoDecoder& decoder,
                             const FrameIndex& frame_index,
                             std::int64_t frame, std::string* error_text,
                             bool record_landing) {
    std::int64_t keyframe = frame_index.KeyframeAtOrBeforeFrame(frame);
    if (keyframe < 0) {
      keyframe = 0;
    }
    const std::int64_t pts = frame_index.PtsOfFrame(keyframe);
    bool strict = true;
    if (!decoder.SeekToKeyframePts(pts, error_text)) {
      strict = false;
      {
        std::lock_guard<std::mutex> lock(mutex);
        ++seek_fallbacks;
      }
      if (!decoder.SeekToPts(pts, error_text)) {
        return false;
      }
      // strict 失败的错误文本不能残留给后续 DecodeNextFrame 失败路径。
      if (error_text != nullptr) {
        error_text->clear();
      }
    }
    if (record_landing) {
      std::lock_guard<std::mutex> lock(mutex);
      last_seek_keyframe = keyframe;
      last_seek_landing_frame = -1;
      last_seek_landing_offset = 0;
      seek_landing_pending = true;
    }
    Log("seek keyframe=%lld pts=%lld strict=%d",
        static_cast<long long>(keyframe), static_cast<long long>(pts),
        strict ? 1 : 0);
    return true;
  }

  void RecordSeekLanding(std::int64_t decoded_frame) {
    std::int64_t keyframe = -1;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (!seek_landing_pending) {
        return;
      }
      seek_landing_pending = false;
      last_seek_landing_frame = decoded_frame;
      last_seek_landing_offset = decoded_frame - last_seek_keyframe;
      keyframe = last_seek_keyframe;
    }
    Log("seek_landing keyframe=%lld frame=%lld offset=%lld",
        static_cast<long long>(keyframe),
        static_cast<long long>(decoded_frame),
        static_cast<long long>(decoded_frame - keyframe));
  }

  std::int64_t ChooseBaselineLocked(std::int64_t displayed,
                                    std::int64_t fallback) const {
    if (displayed >= 0 && index != nullptr &&
        static_cast<std::size_t>(displayed) < index->size()) {
      return displayed;
    }
    return fallback >= 0 ? fallback : 0;
  }

  void ClearPendingPrimesLocked() {
    for (PendingPrime& pending : pending_primes) {
      if (pending.frame != nullptr) {
        av_frame_free(&pending.frame);
      }
    }
    pending_primes.clear();
    prime_requested = false;
  }

  void FinishWarmLocked(bool success) {
    if (warm_started) {
      warm_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - warm_start)
                    .count();
    }
    warming = false;
    warm_requested = false;
    warm_idle = true;
    warm_started = false;
    if (!success) {
      ++warm_failures;
    }
    condition.notify_all();
    Log("warm_done success=%d frames=%lld skipped=%llu failures=%llu ms=%lld",
        success ? 1 : 0, static_cast<long long>(warm_frames),
        static_cast<unsigned long long>(warm_skipped),
        static_cast<unsigned long long>(warm_failures),
        static_cast<long long>(warm_ms));
  }

  void DrainPendingPrimes() {
    for (;;) {
      AVFrame* frame = nullptr;
      std::int64_t frame_index = -1;
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (pending_primes.empty()) {
          prime_requested = false;
          return;
        }
        PendingPrime pending = pending_primes.front();
        pending_primes.pop_front();
        frame = pending.frame;
        frame_index = pending.frame_index;
      }
      if (frame == nullptr) {
        continue;
      }
      if (frame->hw_frames_ctx != nullptr) {
        av_frame_free(&frame);
        continue;
      }
      AVFrame* preview = ScalePreviewFrame(frame, &sws);
      av_frame_free(&frame);
      if (preview == nullptr) {
        continue;
      }
      std::lock_guard<std::mutex> lock(mutex);
      if (stop) {
        av_frame_free(&preview);
        return;
      }
      const std::int64_t pts =
          index != nullptr ? index->PtsOfFrame(frame_index) : 0;
      CacheFrameLocked(preview, pts, frame_index, target_frame);
      ++primed_frames;
    }
  }

  void ProcessWarm(VideoDecoder& decoder) {
    std::int64_t origin = 0;
    std::int64_t target = 0;
    std::int64_t window = 1;
    std::uint64_t epoch = 0;
    std::shared_ptr<const FrameIndex> current_index;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (!warm_requested || active || stop) {
        return;
      }
      origin = warm_origin;
      target = warm_target;
      window = CacheWindowLocked();
      epoch = warm_epoch;
      current_index = index;
      warming = true;
      warm_idle = false;
      if (!warm_started) {
        warm_start = std::chrono::steady_clock::now();
        warm_started = true;
      }
    }
    if (!current_index || origin < 0 || target < origin) {
      std::lock_guard<std::mutex> lock(mutex);
      if (warm_epoch == epoch && warm_requested) {
        FinishWarmLocked(false);
      }
      return;
    }
    // 已有缓存窗口且解码位置已到 target：不重复解码。
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (warm_epoch == epoch && warm_requested && position_valid &&
          last_decoded_frame >= target &&
          RangeCachedLocked(target - window, target)) {
        ++warm_skipped;
        FinishWarmLocked(true);
        return;
      }
      // 预热窗口 [target-window,target] 要完整；旧缓存若在窗口外会挤掉新帧。
      PruneCacheOutsideLocked(target - window, target);
    }
    std::string error_text;
    if (!SeekToPreviewKeyframe(decoder, *current_index, origin, &error_text,
                               true)) {
      std::lock_guard<std::mutex> lock(mutex);
      if (warm_epoch == epoch && warm_requested) {
        FinishWarmLocked(false);
      }
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      position_valid = false;
    }
    bool landing_recorded = false;
    int guard = 0;
    auto slice_start = std::chrono::steady_clock::now();
    for (;;) {
      if (++guard > 100000) {
        std::lock_guard<std::mutex> lock(mutex);
        position_valid = false;
        if (warm_epoch == epoch && warm_requested) {
          FinishWarmLocked(false);
        }
        return;
      }
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (stop || pause_requested || !warm_requested ||
            warm_epoch != epoch || active) {
          return;
        }
      }
      DecodedFrameInfo info;
      if (!decoder.DecodeNextFrame(&info, &error_text)) {
        std::lock_guard<std::mutex> lock(mutex);
        if (warm_epoch == epoch && warm_requested) {
          position_valid = false;
          FinishWarmLocked(false);
        }
        return;
      }
      std::int64_t decoded_frame = current_index->FrameOfPts(info.pts_us);
      if (decoded_frame < 0) {
        continue;
      }
      {
        std::lock_guard<std::mutex> lock(mutex);
        last_pts_us = info.pts_us;
        last_decoded_frame = decoded_frame;
        position_valid = true;
        ++warm_frames;
      }
      if (!landing_recorded) {
        RecordSeekLanding(decoded_frame);
        landing_recorded = true;
      }
      // 只对 [target-window, target] 做缩放/入缓存；更早的帧只解码不缓存。
      if (decoded_frame >= target - window) {
        AVFrame* preview = ScalePreviewFrame(info.av_frame, &sws);
        if (preview != nullptr) {
          std::lock_guard<std::mutex> lock(mutex);
          if (stop || !warm_requested || warm_epoch != epoch) {
            av_frame_free(&preview);
            return;
          }
          CacheFrameLocked(preview, info.pts_us, decoded_frame, target);
        }
      }
      if (decoded_frame >= target) {
        break;
      }
      if (std::chrono::steady_clock::now() - slice_start >
          std::chrono::milliseconds(15)) {
        slice_start = std::chrono::steady_clock::now();
        std::this_thread::yield();
      }
    }
    std::lock_guard<std::mutex> lock(mutex);
    if (warm_epoch == epoch && warm_requested) {
      FinishWarmLocked(true);
    }
  }

  bool IsActive() {
    std::lock_guard<std::mutex> lock(mutex);
    return active && !stop;
  }

  void FreeLatestLocked() {
    if (latest != nullptr) {
      av_frame_free(&latest);
    }
    latest_pts_us = 0;
    latest_frame_index = -1;
    latest_generation = 0;
    latest_coarse = false;
    latest_from_cache = false;
  }

  void ClearCacheLocked() {
    for (CachedPreview& cached : cache) {
      if (cached.frame != nullptr) {
        av_frame_free(&cached.frame);
      }
    }
    cache.clear();
    cache_bytes = 0;
  }

  // 预热前清掉窗口外的旧帧，保证整个缓存窗口预算全部留给 [from,to]。
  void PruneCacheOutsideLocked(std::int64_t from, std::int64_t to) {
    for (auto it = cache.begin(); it != cache.end();) {
      const bool outside = it->frame_index >= 0 &&
                           (it->frame_index < from || it->frame_index > to);
      if (outside) {
        cache_bytes -= FrameBytes(it->frame);
        if (it->frame != nullptr) {
          av_frame_free(&it->frame);
        }
        it = cache.erase(it);
      } else {
        ++it;
      }
    }
    UpdateCachePeakLocked();
  }

  static std::size_t FrameBytes(const AVFrame* frame) {
    if (frame == nullptr || frame->width <= 0 || frame->height <= 0) {
      return 0;
    }
    return static_cast<std::size_t>(frame->width) *
           static_cast<std::size_t>(frame->height) * 3u / 2u;
  }

  void UpdateCachePeakLocked() {
    cache_peak_bytes = std::max(cache_peak_bytes, cache_bytes);
  }

  int CachedFrameLimitLocked() const {
    return FrameLimitFor(max_side, cache_budget_bytes);
  }

  // 预热窗口宽度：缓存上限 - 1（旧实现写死 71）。
  std::int64_t CacheWindowLocked() const {
    return static_cast<std::int64_t>(
               std::max(1, CachedFrameLimitLocked() - 1));
  }

  // D2：分配失败回退（调用方持锁）。首次失败把预算降到 75% 并允许重试一次；
  // 再次失败回退到 1024 / 64MiB（旧默认）。已处于回退值时不重复降级，只计数，
  // 避免无限重试/反复抖动。降低预算后立即按当前目标淘汰超限帧。
  void NoteAllocationFailureLocked() {
    ++alloc_failures;
    const bool at_fallback = max_side <= kDefaultPreviewMaxSide &&
                             cache_budget_bytes <= kDefaultCacheBudgetBytes;
    if (!at_fallback && !alloc_budget_shrunk) {
      alloc_budget_shrunk = true;
      alloc_fallback_used = true;
      const std::size_t shrunk = cache_budget_bytes * 3u / 4u;
      cache_budget_bytes = std::max(kMinCacheBudgetBytes, shrunk);
    } else if (!at_fallback) {
      alloc_fallback_used = true;
      max_side = kDefaultPreviewMaxSide;
      cache_budget_bytes = kDefaultCacheBudgetBytes;
    }
    EvictCacheLocked(target_frame);
  }

  // D2：预览帧降采样 + 失败回退。每次分配最多两次尝试（失败后配置已降级再试），
  // 返回 nullptr 表示本次帧放弃；调用方按“跳过该帧”处理，不升级成预览不可用。
  AVFrame* ScalePreviewFrame(const AVFrame* source_frame, SwsContext** context) {
    for (int attempt = 0; attempt < 2; ++attempt) {
      int side = kDefaultPreviewMaxSide;
      bool forced = false;
      {
        std::lock_guard<std::mutex> lock(mutex);
        side = max_side;
        if (forced_alloc_failures > 0) {
          --forced_alloc_failures;
          forced = true;
        }
      }
      AVFrame* frame =
          forced ? nullptr : ScaleToNv12(source_frame, side, context);
      if (frame != nullptr) {
        return frame;
      }
      std::lock_guard<std::mutex> lock(mutex);
      NoteAllocationFailureLocked();
    }
    return nullptr;
  }

  // 按“距当前目标最远”淘汰，保证目标两侧都留帧（+-1 振荡/反向都靠缓存）。
  // D2：两个上限都来自运行时配置（帧数由预算/估计帧字节算出）。
  void EvictCacheLocked(std::int64_t anchor) {
    const std::size_t frame_limit =
        static_cast<std::size_t>(CachedFrameLimitLocked());
    while (!cache.empty() &&
           (cache.size() > frame_limit || cache_bytes > cache_budget_bytes)) {
      std::size_t victim = cache.size();
      std::int64_t victim_distance = -1;
      for (std::size_t i = 0; i < cache.size(); ++i) {
        const std::int64_t distance =
            cache[i].frame_index >= 0
                ? Distance(cache[i].frame_index, anchor)
                : std::numeric_limits<std::int64_t>::max();
        if (distance > victim_distance) {
          victim_distance = distance;
          victim = i;
        }
      }
      if (victim >= cache.size()) {
        break;
      }
      cache_bytes -= FrameBytes(cache[victim].frame);
      if (cache[victim].frame != nullptr) {
        av_frame_free(&cache[victim].frame);
      }
      cache.erase(cache.begin() + static_cast<std::ptrdiff_t>(victim));
    }
    UpdateCachePeakLocked();
  }

  // 接管 frame；重复帧替换旧副本，超预算按距 anchor 最远淘汰。
  void CacheFrameLocked(AVFrame* frame, std::int64_t pts_us,
                        std::int64_t frame_index, std::int64_t anchor) {
    if (frame == nullptr || frame_index < 0) {
      if (frame != nullptr) {
        av_frame_free(&frame);
      }
      return;
    }
    // D2：记录实际缓存帧尺寸（配置真的落到 NV12 帧上的证据）。
    if (frame->width > 0 && frame->height > 0) {
      preview_width = frame->width;
      preview_height = frame->height;
    }
    for (CachedPreview& cached : cache) {
      if (cached.frame_index == frame_index) {
        cache_bytes -= FrameBytes(cached.frame);
        if (cached.frame != nullptr) {
          av_frame_free(&cached.frame);
        }
        cached.pts_us = pts_us;
        cached.frame = frame;
        cache_bytes += FrameBytes(frame);
        EvictCacheLocked(anchor);
        return;
      }
    }
    cache.push_back(CachedPreview{pts_us, frame_index, frame});
    cache_bytes += FrameBytes(frame);
    EvictCacheLocked(anchor);
  }

  AVFrame* CloneExactLocked(std::int64_t frame_index) const {
    for (auto it = cache.rbegin(); it != cache.rend(); ++it) {
      if (it->frame_index == frame_index && it->frame != nullptr) {
        return av_frame_clone(it->frame);
      }
    }
    return nullptr;
  }

  bool HasExactLocked(std::int64_t frame_index) const {
    for (const CachedPreview& cached : cache) {
      if (cached.frame_index == frame_index && cached.frame != nullptr) {
        return true;
      }
    }
    return false;
  }

  bool RangeCachedLocked(std::int64_t from, std::int64_t to) const {
    if (from > to) {
      return true;
    }
    for (std::int64_t frame = from; frame <= to; ++frame) {
      if (!HasExactLocked(frame)) {
        return false;
      }
    }
    return true;
  }

  // 缓存里“比上一张更接近目标且不越过目标”的最近帧。用于目标未命中精确
  // 缓存时立即给一张更近的画面，而不是等整段解码。
  AVFrame* CloneClosestLocked(std::int64_t target,
                              std::int64_t* out_frame) const {
    if (last_delivered_frame < 0 || out_frame == nullptr) {
      return nullptr;
    }
    std::int64_t best = -1;
    std::int64_t best_distance = std::numeric_limits<std::int64_t>::max();
    for (const CachedPreview& cached : cache) {
      if (cached.frame == nullptr || cached.frame_index < 0) {
        continue;
      }
      bool in_range = false;
      if (target > last_delivered_frame) {
        in_range = cached.frame_index > last_delivered_frame &&
                   cached.frame_index < target;
      } else if (target < last_delivered_frame) {
        in_range = cached.frame_index < last_delivered_frame &&
                   cached.frame_index > target;
      }
      if (!in_range) {
        continue;
      }
      const std::int64_t distance = Distance(cached.frame_index, target);
      if (distance < best_distance) {
        best_distance = distance;
        best = cached.frame_index;
      }
    }
    if (best < 0) {
      return nullptr;
    }
    *out_frame = best;
    return CloneExactLocked(best);
  }

  // 已发布的 latest 是否仍对当前目标“有用”：精确、或比上一张更接近且未越过
  // 目标。目标 +-1 振荡时不要因为方向变化就清空 latest。
  bool LatestUsefulLocked(std::int64_t target) const {
    if (latest == nullptr) {
      return false;
    }
    if (last_delivered_frame < 0) {
      return latest_coarse || latest_frame_index == target;
    }
    if (latest_coarse) {
      if (target < last_delivered_frame) {
        return latest_frame_index < last_delivered_frame;
      }
      if (target > last_delivered_frame) {
        return latest_frame_index > last_delivered_frame;
      }
      return false;
    }
    if (latest_frame_index == target) {
      return true;
    }
    if (Distance(latest_frame_index, target) >=
        Distance(last_delivered_frame, target)) {
      return false;
    }
    if (target > last_delivered_frame) {
      return latest_frame_index > last_delivered_frame &&
             latest_frame_index < target;
    }
    if (target < last_delivered_frame) {
      return latest_frame_index < last_delivered_frame &&
             latest_frame_index > target;
    }
    return false;
  }

  // 统一的“发布更近帧”判定；调用方持锁。
  bool ShouldPublishRegularLocked(std::int64_t frame_index,
                                  std::int64_t target) const {
    if (frame_index == target) {
      return true;
    }
    // baseline 保证 last>=0；兜底允许首帧，方向/不越过目标规则仍由下面
    // reference 分支约束（reference<0 时无方向可言，只放行一张）。
    if (last_delivered_frame < 0) {
      return true;
    }
    std::int64_t reference = last_delivered_frame;
    if (latest != nullptr && !latest_coarse) {
      if (Distance(latest_frame_index, target) <= Distance(frame_index, target)) {
        return false;
      }
      reference = latest_frame_index;
    }
    if (Distance(frame_index, target) >= Distance(reference, target)) {
      return false;
    }
    if (reference < target) {
      return frame_index > reference && frame_index < target;
    }
    if (reference > target) {
      return frame_index < reference && frame_index > target;
    }
    return false;
  }

  // 接管 frame。
  void PublishLocked(AVFrame* frame, std::int64_t pts_us,
                     std::int64_t frame_index, std::uint64_t session,
                     bool coarse, bool from_cache) {
    if (stop || !active || frame == nullptr) {
      av_frame_free(&frame);
      return;
    }
    // 精确目标已经呈现：更远的帧不能覆盖它，粗帧更不行。
    if (last_delivered_frame == target_frame && !last_delivered_coarse &&
        !(frame_index == target_frame && !coarse)) {
      av_frame_free(&frame);
      return;
    }
    // 暂存的精确目标已就位时，不允许更远的普通帧覆盖。
    if (!coarse && latest != nullptr && !latest_coarse &&
        latest_frame_index == target_frame && frame_index != target_frame) {
      av_frame_free(&frame);
      return;
    }
    // 普通帧必须比 pending/last 更接近目标；粗帧只做方向性占位。
    if (!coarse && !ShouldPublishRegularLocked(frame_index, target_frame)) {
      av_frame_free(&frame);
      return;
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
    ++frames_published;
    last_published_frame = frame_index;
    ++drag_stats.published;
    if (drag_stats.first_publish_ms < 0) {
      drag_stats.first_publish_ms = DragStatsElapsedMsLocked();
    }
    if (coarse) {
      ++coarse_published;
    }
    Log("publish frame=%lld target=%lld last=%lld coarse=%d cache=%d",
        static_cast<long long>(frame_index),
        static_cast<long long>(target_frame),
        static_cast<long long>(last_delivered_frame), coarse ? 1 : 0,
        from_cache ? 1 : 0);
  }

  void PublishExactLocked(AVFrame* frame, std::int64_t frame_index,
                          std::uint64_t session, bool from_cache) {
    if (frame == nullptr) {
      return;
    }
    const std::int64_t pts =
        index != nullptr ? index->PtsOfFrame(frame_index) : 0;
    PublishLocked(frame, pts, frame_index, session, false, from_cache);
  }

  void SetWaiting(bool value) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stop || !active) {
      return;
    }
    waiting = value;
  }

  TargetSnapshot Snapshot() {
    TargetSnapshot snapshot;
    std::lock_guard<std::mutex> lock(mutex);
    snapshot.active = active && !stop;
    snapshot.cached = target_cached;
    snapshot.target = target_frame;
    snapshot.generation = generation;
    snapshot.direction = target_direction;
    snapshot.index = index;
    return snapshot;
  }

  AVFrame* ScaleToNv12(const AVFrame* source_frame, int max_side,
                       SwsContext** context) {
    if (source_frame == nullptr || source_frame->data[0] == nullptr ||
        source_frame->width <= 0 || source_frame->height <= 0) {
      return nullptr;
    }
    const int longest = std::max(source_frame->width, source_frame->height);
    int destination_width = source_frame->width;
    int destination_height = source_frame->height;
    if (longest > max_side) {
      const double scale = static_cast<double>(max_side) / longest;
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
    *context = sws_getCachedContext(
        *context, source_frame->width, source_frame->height,
        static_cast<AVPixelFormat>(source_frame->format), destination_width,
        destination_height, AV_PIX_FMT_NV12, SWS_FAST_BILINEAR, nullptr,
        nullptr, nullptr);
    if (*context == nullptr) {
      av_frame_free(&destination);
      return nullptr;
    }
    const int scaled =
        sws_scale(*context, source_frame->data, source_frame->linesize, 0,
                  source_frame->height, destination->data,
                  destination->linesize);
    if (scaled <= 0) {
      av_frame_free(&destination);
      return nullptr;
    }
    destination->pts = source_frame->pts;
    destination->best_effort_timestamp = source_frame->best_effort_timestamp;
    destination->sample_aspect_ratio = source_frame->sample_aspect_ratio;
    destination->colorspace = source_frame->colorspace;
    destination->color_range = source_frame->color_range;
    destination->color_primaries = source_frame->color_primaries;
    destination->color_trc = source_frame->color_trc;
    return destination;
  }

  // 远跳第一帧：生成 320x180 粗帧；失败不影响精确目标。
  void PublishCoarseFrom(const AVFrame* decoded, std::int64_t pts_us,
                         std::int64_t frame_index, std::uint64_t session) {
    AVFrame* coarse = ScaleToNv12(decoded, kCoarseMaxSide, &coarse_sws);
    if (coarse == nullptr) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    PublishLocked(coarse, pts_us, frame_index, session, true, false);
  }

  // 先按发布判定决定是否克隆，再缓存并发布；缓存接管 preview 所有权。
  // suppress_publish=true（远跳）：只缓存，等精确目标；中间帧不再逐帧
  // 发布，避免 5s GOP 下“关键帧幻灯片”占满 presented。
  void CacheAndMaybePublish(AVFrame* preview, std::int64_t pts_us,
                            std::int64_t frame_index, std::int64_t target,
                            std::uint64_t session, bool suppress_publish) {
    bool publish = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      publish = !stop && active && !suppress_publish &&
                ShouldPublishRegularLocked(frame_index, target);
    }
    AVFrame* copy = publish ? av_frame_clone(preview) : nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (stop || !active) {
        av_frame_free(&preview);
        av_frame_free(&copy);
        return;
      }
      if (publish && copy == nullptr) {
        // D2：发布用 clone 失败也计入分配失败回退（缓存帧本身不丢）。
        NoteAllocationFailureLocked();
      }
      CacheFrameLocked(preview, pts_us, frame_index, target_frame);
    }
    if (copy != nullptr) {
      std::lock_guard<std::mutex> lock(mutex);
      if (stop || !active) {
        av_frame_free(&copy);
        return;
      }
      PublishLocked(copy, pts_us, frame_index, session, false, false);
      waiting = false;
    }
  }

  bool PublishExactIfCached(std::int64_t target, std::uint64_t session) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stop || !active) {
      return false;
    }
    AVFrame* exact = CloneExactLocked(target);
    if (exact == nullptr) {
      return false;
    }
    PublishExactLocked(exact, target, session, true);
    waiting = false;
    return true;
  }

  // 解码到 pursue_target；不在每次目标变化时中断（否则 60Hz 目标会不断
  // 重 seek、缓存永远填不满）。中途只更新发布判定，完成后由 worker 再服务
  // 最新目标。返回 false 表示解码/seek 失败。
  bool DecodeToward(VideoDecoder& decoder, const FrameIndex& frame_index,
                    std::int64_t pursue_target, std::string* error_text) {
    const std::int64_t pursue_pts = frame_index.PtsOfFrame(pursue_target);
    if (pursue_pts == std::numeric_limits<std::int64_t>::min()) {
      return true;
    }
    bool seeked = false;
    std::int64_t seek_origin = -1;
    bool can_continue = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      can_continue = position_valid && pursue_pts > last_pts_us &&
                     pursue_pts - last_pts_us <= kContinueWindowUs;
    }
    if (!can_continue) {
      std::int64_t keyframe = frame_index.KeyframeAtOrBeforeFrame(pursue_target);
      if (keyframe < 0) {
        keyframe = 0;
      }
      seek_origin = keyframe;
      {
        std::lock_guard<std::mutex> lock(mutex);
        ++seek_count;
      }
      // P0：strict 关键帧 seek（fMP4 宽松 seek 会落到前一 GOP）。
      if (!SeekToPreviewKeyframe(decoder, frame_index, pursue_target,
                                 error_text, true)) {
        std::lock_guard<std::mutex> lock(mutex);
        position_valid = false;
        return false;
      }
      {
        std::lock_guard<std::mutex> lock(mutex);
        position_valid = false;
      }
      seeked = true;
      Log("seek target=%lld keyframe=%lld pursue=%lld can_continue=%d",
          static_cast<long long>(pursue_target),
          static_cast<long long>(keyframe),
          static_cast<long long>(pursue_target),
          can_continue ? 1 : 0);
    } else {
      Log("continue target=%lld last_pts=%lld", static_cast<long long>(pursue_target),
          static_cast<long long>(pursue_pts));
    }

    std::int64_t last_delivered = -1;
    {
      std::lock_guard<std::mutex> lock(mutex);
      last_delivered = last_delivered_frame;
    }
    const bool far_jump =
        last_delivered >= 0 &&
        Distance(pursue_target, last_delivered) >= kFarJumpFrames;
    bool coarse_sent = false;
    // 目标在解码期间回退时，当前目标可能先命中缓存而提前返回；把“当前目标
    // 之后 10 帧”继续解完并缓存（不发布），下一次向前拖动不会因为缺少
    // 66..75 而重新从关键帧 seek。
    std::int64_t complete_until = -1;
    const auto start_completion = [&](std::int64_t current) {
      const std::int64_t desired = std::max<std::int64_t>(
          pursue_target,
          std::min<std::int64_t>(
              static_cast<std::int64_t>(frame_index.size()) - 1,
              current + kCompletionLookaheadFrames));
      if (desired > current && desired - current <= 30) {
        complete_until = desired;
        Log("completion start current=%lld until=%lld pursue=%lld",
            static_cast<long long>(current),
            static_cast<long long>(complete_until),
            static_cast<long long>(pursue_target));
        return true;
      }
      return false;
    };
    auto slice_start = std::chrono::steady_clock::now();
    int guard = 0;
    for (;;) {
      if (!IsActive()) {
        return true;
      }
      const TargetSnapshot now = Snapshot();
      if (!now.active) {
        return true;
      }
      if (!now.cached || now.index == nullptr) {
        SetWaiting(true);
        return true;
      }
      const std::int64_t current_target = now.target;
      if (complete_until >= 0) {
        // 完成预解期间目标可能继续移动；这段最多 10 帧，先填完缓存再交回
        // worker 服务最新目标（目标更新期间 UI 仍可从已缓存帧发布）。
      } else {
        // 当前目标已经进缓存：向前多解 10 帧（或解到进入时的目标，取较大者）
        // 并缓存，再结束这次解码；下一次向前拖动不会缺帧重新 seek。
        if (PublishExactIfCached(current_target, now.generation)) {
          if (!start_completion(current_target)) {
            return true;
          }
        }
      }
      // 目标跨到解码起点另一侧（例如正在向前解 60→74，目标跳到 50）：
      // 放弃本次解码，worker 重新按新目标 seek。
      if (seeked && seek_origin >= 0 &&
          Distance(current_target, pursue_target) > kOppositeSideStep) {
        if (pursue_target >= seek_origin && current_target < seek_origin) {
          return true;
        }
        if (pursue_target < seek_origin && current_target >= seek_origin) {
          return true;
        }
      }

      DecodedFrameInfo info;
      const bool decoded_ok = decoder.DecodeNextFrame(&info, error_text);
      if (!decoded_ok) {
        std::lock_guard<std::mutex> lock(mutex);
        position_valid = false;
        return false;
      }
      std::int64_t decoded_frame = frame_index.FrameOfPts(info.pts_us);
      {
        std::lock_guard<std::mutex> lock(mutex);
        last_pts_us = info.pts_us;
        last_decoded_frame = decoded_frame;
        position_valid = true;
        ++decoded_frames;
      }
      if (decoded_frame < 0) {
        decoded_frame = pursue_target;
      } else {
        // P0：首个解码帧相对请求关键帧的偏差（负=落到更早 GOP）。
        RecordSeekLanding(decoded_frame);
      }
      AVFrame* preview = ScalePreviewFrame(info.av_frame, &sws);
      if (preview == nullptr) {
        // D2：分配失败已在 ScalePreviewFrame 里降预算/回退（有界、最多两次
        // 尝试）；不把一次分配失败升级成“预览不可用”，跳过该帧继续。若已解到
        // 进入时的目标则交回 worker，避免在同一目标上空转。
        if (pursue_pts <= info.pts_us) {
          return true;
        }
        if (++guard > 100000) {
          if (error_text != nullptr) {
            *error_text = "在线预览帧分配持续失败";
          }
          return false;
        }
        if (std::chrono::steady_clock::now() - slice_start > kDecodeSlice) {
          slice_start = std::chrono::steady_clock::now();
          std::this_thread::yield();
        }
        continue;
      }
      // 远跳：seek 后第一帧先出粗帧，保证“先有画面”；精确目标随后继续。
      if (seeked && !coarse_sent && far_jump) {
        PublishCoarseFrom(info.av_frame, info.pts_us, decoded_frame,
                          now.generation);
        coarse_sent = true;
      }
      if (complete_until >= 0) {
        // 完成预解：只缓存；但如果正好解到当前目标（正向连续拖动），仍要
        // 立即发布，不能等下一次目标更新。
        CacheAndMaybePublish(preview, info.pts_us, decoded_frame,
                             current_target, now.generation,
                             decoded_frame != current_target);
        if (decoded_frame == current_target) {
          SetWaiting(false);
        }
        if (decoded_frame >= complete_until) {
          Log("completion done frame=%lld until=%lld",
              static_cast<long long>(decoded_frame),
              static_cast<long long>(complete_until));
          return true;
        }
        if (++guard > 100000) {
          if (error_text != nullptr) {
            *error_text = "在线预览解码超过上限";
          }
          return false;
        }
        if (std::chrono::steady_clock::now() - slice_start > kDecodeSlice) {
          slice_start = std::chrono::steady_clock::now();
          std::this_thread::yield();
        }
        continue;
      }
      CacheAndMaybePublish(preview, info.pts_us, decoded_frame, current_target,
                           now.generation, far_jump);
      if (decoded_frame == current_target) {
        SetWaiting(false);
        // 已发布精确目标：继续向前缓存 10 帧再返回，保持下一次目标/向前
        // 拖动在缓存内（长 GOP 冷启动尤其重要）。
        if (start_completion(current_target)) {
          if (std::chrono::steady_clock::now() - slice_start > kDecodeSlice) {
            slice_start = std::chrono::steady_clock::now();
            std::this_thread::yield();
          }
          continue;
        }
        return true;
      }
      // 已经解到（或越过）进入时的目标：交回 worker 取最新目标。
      if (pursue_pts <= info.pts_us) {
        return true;
      }
      if (++guard > 100000) {
        if (error_text != nullptr) {
          *error_text = "在线预览解码超过上限";
        }
        return false;
      }
      if (std::chrono::steady_clock::now() - slice_start > kDecodeSlice) {
        slice_start = std::chrono::steady_clock::now();
        std::this_thread::yield();
      }
    }
  }

  void Run() {
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
      if (opened && drag_stats.open_ms < 0) {
        drag_stats.open_ms = DragStatsElapsedMsLocked();
      }
      should_stop = stop;
      if (!opened) {
        if (warm_requested && !active) {
          // P0：预热失败静默（不置 error/active），只记失败数，避免把
          // “后台预热失败”显示成拖动预览不可用。
          ++warm_failures;
          warm_requested = false;
          warming = false;
          warm_idle = true;
          warm_started = false;
        } else {
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
    }
    if (!opened || should_stop) {
      std::lock_guard<std::mutex> lock(mutex);
      running = false;
      return;
    }

    for (;;) {
      std::unique_lock<std::mutex> lock(mutex);
      condition.wait(lock, [this] {
        return stop || pause_requested ||
               (active && generation != handled_generation) || warm_requested ||
               prime_requested;
      });
      if (stop || pause_requested) {
        break;
      }
      const std::uint64_t observed_generation = generation;
      const bool do_process = active && generation != handled_generation;
      const bool do_warm = warm_requested && !active;
      const bool do_primes = prime_requested;
      lock.unlock();

      if (do_primes) {
        DrainPendingPrimes();
      }
      if (do_warm) {
        ProcessWarm(decoder);
      }
      if (do_process) {
        std::string error_text;
        Process(decoder, &error_text);
        // 正向由需求解码直接续解；不再额外预解 30 帧（会和需求解码/UI
        // 抢 CPU，实测把正向 presented 从 55 拉到 38）。
      }

      lock.lock();
      if (do_process) {
        handled_generation = observed_generation;
      }
    }

    decoder.Close();
    format.Close();
    std::lock_guard<std::mutex> lock(mutex);
    running = false;
  }

  void Process(VideoDecoder& decoder, std::string* error_text) {
    for (int iteration = 0; iteration < kMaxProcessIterations; ++iteration) {
      const TargetSnapshot snapshot = Snapshot();
      if (!snapshot.active) {
        return;
      }
      if (!snapshot.cached || snapshot.index == nullptr) {
        SetWaiting(true);
        return;
      }
      if (PublishExactIfCached(snapshot.target, snapshot.generation)) {
        return;
      }
      if (!DecodeToward(decoder, *snapshot.index, snapshot.target,
                        error_text)) {
        const std::string text =
            error_text != nullptr && !error_text->empty()
                ? *error_text
                : std::string("在线预览解码失败");
        std::lock_guard<std::mutex> lock(mutex);
        if (!stop && active) {
          error = text;
          active = false;
          waiting = false;
          ++generation;
          condition.notify_all();
        }
        return;
      }
      const TargetSnapshot now = Snapshot();
      if (!now.active) {
        return;
      }
      if (!now.cached) {
        SetWaiting(true);
        return;
      }
      if (PublishExactIfCached(now.target, now.generation)) {
        return;
      }
      if (now.target == snapshot.target) {
        // 已解到/越过目标但缓存里没有目标帧（索引不可靠/解码越界）：
        // 不再空转，等下一次目标或索引更新。
        return;
      }
      // 目标在解码期间变化：继续服务最新目标（通常命中缓存或只差几帧）。
    }
  }

  void RunPrefetch() {
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
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
    SwsContext* prefetch_sws = nullptr;
    bool opened = false;
    for (;;) {
      std::int64_t request_from = -1;
      std::int64_t request_to = -1;
      std::uint64_t request_epoch = 0;
      std::shared_ptr<const FrameIndex> current_index;
      {
        std::unique_lock<std::mutex> lock(mutex);
        prefetch_condition.wait(lock, [this] {
          return prefetch_stop || (active && prefetch_to >= 0);
        });
        if (prefetch_stop) {
          break;
        }
        request_from = prefetch_from;
        request_to = prefetch_to;
        request_epoch = prefetch_epoch;
        current_index = index;
      }
      if (current_index == nullptr || request_to < 0 || request_from < 0 ||
          request_from > request_to) {
        continue;
      }
      Log("prefetch range from=%lld to=%lld opened=%d",
          static_cast<long long>(request_from),
          static_cast<long long>(request_to), opened ? 1 : 0);
      bool already_cached = false;
      {
        std::lock_guard<std::mutex> lock(mutex);
        already_cached = RangeCachedLocked(request_from, request_to);
        if (already_cached && prefetch_epoch == request_epoch) {
          prefetch_to = -1;
        }
      }
      if (already_cached) {
        continue;
      }
      if (!opened) {
        if (!local_source || local_path.empty()) {
          break;
        }
        const QString qpath =
            QString::fromUtf8(local_path.c_str(),
                              static_cast<int>(local_path.size()));
        QString open_error;
        if (!format.Open(local_source, qpath, &open_error, 0)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          continue;
        }
        if (format.cursor() != nullptr && local_interrupt != nullptr) {
          format.cursor()->SetInterruptFlag(local_interrupt);
        }
        std::string decoder_error;
        if (!decoder.OpenWithFormat(format.context(), local_stream, nullptr,
                                    false, &decoder_error, 2)) {
          format.Close();
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          continue;
        }
        opened = true;
      }
      std::string error_text;
      // P0：strict seek 到 request_from 之前最近关键帧（不能用任意 frame 的
      // PTS 做 strict seek）；随后跳过 < from 的帧。
      if (!SeekToPreviewKeyframe(decoder, *current_index, request_from,
                                 &error_text, false)) {
        decoder.Close();
        format.Close();
        opened = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      bool cancelled = false;
      int guard = 0;
      for (;;) {
        {
          std::lock_guard<std::mutex> lock(mutex);
          if (prefetch_stop || !active || prefetch_to < 0 ||
              prefetch_epoch != request_epoch) {
            cancelled = true;
            break;
          }
        }
        if (++guard > 100000) {
          cancelled = true;
          break;
        }
        DecodedFrameInfo info;
        if (!decoder.DecodeNextFrame(&info, &error_text)) {
          decoder.Close();
          format.Close();
          opened = false;
          cancelled = true;
          break;
        }
        const std::int64_t decoded_frame =
            current_index->FrameOfPts(info.pts_us);
        if (decoded_frame < 0) {
          continue;
        }
        if (decoded_frame < request_from) {
          continue;
        }
        if (decoded_frame > request_to) {
          break;
        }
        AVFrame* preview = ScalePreviewFrame(info.av_frame, &prefetch_sws);
        if (preview == nullptr) {
          continue;
        }
        {
          std::lock_guard<std::mutex> lock(mutex);
          if (prefetch_stop || !active) {
            av_frame_free(&preview);
            cancelled = true;
            break;
          }
          CacheFrameLocked(preview, info.pts_us, decoded_frame, target_frame);
          ++prefetched_frames;
        }
      }
      if (!cancelled) {
        std::lock_guard<std::mutex> lock(mutex);
        if (active && prefetch_epoch == request_epoch) {
          prefetch_from = -1;
          prefetch_to = -1;
          prefetch_served_to = request_to;
        }
      }
    }
    if (prefetch_sws != nullptr) {
      sws_freeContext(prefetch_sws);
      prefetch_sws = nullptr;
    }
    decoder.Close();
    format.Close();
  }

  // 拖动结束但会话仍打开：关闭预览 reader（不与松手后的精确 seek/finalize
  // 抢 `.part`），保留已解码缓存；下一次 Resume 重启线程并复用缓存。
  void Pause() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (stop) {
        return;
      }
      pause_requested = true;
      active = false;
      waiting = false;
      // 预热随 Pause 取消；缓存保留。
      warm_requested = false;
      warming = false;
      warm_idle = false;
      ++warm_epoch;
      ++generation;
      if (interrupt != nullptr) {
        interrupt->store(true, std::memory_order_relaxed);
      }
      condition.notify_all();
      prefetch_stop = true;
      prefetch_condition.notify_all();
    }
    if (thread.joinable()) {
      thread.join();
    }
    if (prefetch_thread.joinable()) {
      prefetch_thread.join();
    }
    std::lock_guard<std::mutex> lock(mutex);
    pause_requested = false;
    prefetch_stop = false;
    running = false;
    position_valid = false;
    last_pts_us = 0;
    last_decoded_frame = -1;
    warm_started = false;
    warm_idle = false;
    seek_landing_pending = false;
    FreeLatestLocked();
    Log("pause cache_frames=%d", static_cast<int>(cache.size()));
  }

  void Resume(std::int64_t frame, bool cached,
              std::int64_t displayed_frame) {
    bool start_threads = false;
    bool join_stale = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (stop) {
        return;
      }
      const bool covered =
          index != nullptr && frame >= 0 &&
          static_cast<std::size_t>(frame) < index->size();
      if (!thread.joinable()) {
        start_threads = true;
      } else if (!running) {
        // 上次预热/打开失败后线程已返回但未 join；先回收再重启。
        start_threads = true;
        join_stale = true;
      }
      // P0 度量：记录 Resume 前旧值与本拖动 baseline。
      const std::int64_t previous_delivered = last_delivered_frame;
      ResetDragStatsLocked();
      drag_stats.last_delivered_before_resume = previous_delivered;
      const std::int64_t baseline =
          ChooseBaselineLocked(displayed_frame, frame);
      drag_stats.baseline_frame = baseline;
      if (!start_threads && open_ok) {
        drag_stats.open_ms = 0;
      }
      // 新拖动优先于预热：取消在途 warm，保留已有缓存/位置（线程复用时）。
      warm_requested = false;
      warming = false;
      warm_idle = true;
      ++warm_epoch;
      seek_landing_pending = false;
      active = true;
      target_frame = frame;
      target_cached = cached && covered;
      target_direction = 0;
      waiting = !target_cached;
      last_delivered_frame = baseline;
      last_delivered_coarse = false;
      FreeLatestLocked();
      prefetch_from = -1;
      prefetch_to = -1;
      if (join_stale) {
        // 打开失败时 Run 已返回，但 prefetch_thread 仍可能停在 wait；
        // 先唤醒并确保两个线程都 join，绝不能给 joinable 的 std::thread 赋值。
        prefetch_stop = true;
        prefetch_condition.notify_all();
      } else {
        prefetch_stop = false;
      }
      ++prefetch_epoch;
      if (start_threads) {
        // 只有线程确实重启时才重置解码位置；warm idle 复用保留位置。
        position_valid = false;
        last_pts_us = 0;
        last_decoded_frame = -1;
        interrupt = std::make_shared<std::atomic<bool>>(false);
        generation = 1;
        handled_generation = 0;
      } else {
        if (interrupt == nullptr) {
          interrupt = std::make_shared<std::atomic<bool>>(false);
        } else {
          interrupt->store(false, std::memory_order_relaxed);
        }
        ++generation;
      }
      running = true;
      condition.notify_all();
      prefetch_condition.notify_all();
      Log("resume target=%lld cached=%d cache_frames=%d start=%d "
          "baseline=%lld",
          static_cast<long long>(frame), target_cached ? 1 : 0,
          static_cast<int>(cache.size()), start_threads ? 1 : 0,
          static_cast<long long>(baseline));
    }
    if (join_stale) {
      if (thread.joinable()) {
        thread.join();
      }
      if (prefetch_thread.joinable()) {
        prefetch_thread.join();
      }
    }
    if (start_threads) {
      StartThreads();
    }
  }

  void StartThreads() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (thread.joinable() || prefetch_thread.joinable()) {
        // 防御：给 joinable 的 std::thread 赋值会 std::terminate。
        Log("start_threads skipped stale joinable thread");
        return;
      }
      prefetch_stop = false;
    }
    thread = std::thread([this] { Run(); });
    prefetch_thread = std::thread([this] { RunPrefetch(); });
  }

  void End() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stop = true;
      active = false;
      waiting = false;
      warm_requested = false;
      warming = false;
      warm_idle = false;
      ++warm_epoch;
      ++generation;
      if (interrupt != nullptr) {
        interrupt->store(true, std::memory_order_relaxed);
      }
      condition.notify_all();
      prefetch_stop = true;
      prefetch_condition.notify_all();
    }
    if (thread.joinable()) {
      thread.join();
    }
    if (prefetch_thread.joinable()) {
      prefetch_thread.join();
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      running = false;
      pause_requested = false;
      FreeLatestLocked();
      ClearCacheLocked();
      ClearPendingPrimesLocked();
      primed_frames = 0;
      if (sws != nullptr) {
        sws_freeContext(sws);
        sws = nullptr;
      }
      if (coarse_sws != nullptr) {
        sws_freeContext(coarse_sws);
        coarse_sws = nullptr;
      }
      position_valid = false;
      last_pts_us = 0;
      last_decoded_frame = -1;
      target_cached = false;
      target_frame = 0;
      target_direction = 0;
      generation = 0;
      handled_generation = 0;
      open_attempted = false;
      open_ok = false;
      interrupt.reset();
      prefetch_stop = false;
      prefetch_from = -1;
      prefetch_to = -1;
      prefetch_epoch = 0;
      prefetch_served_to = -1;
      last_delivered_frame = -1;
      last_delivered_coarse = false;
      seek_landing_pending = false;
      warm_started = false;
      warm_ms = -1;
      warm_frames = 0;
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
};

OnlinePreviewDecoder::OnlinePreviewDecoder() : impl_(std::make_unique<Impl>()) {
  g_latest_instance.store(this, std::memory_order_release);
}

OnlinePreviewDecoder::~OnlinePreviewDecoder() {
  OnlinePreviewDecoder* expected = this;
  g_latest_instance.compare_exchange_strong(expected, nullptr,
                                            std::memory_order_acq_rel);
}

OnlinePreviewDecoder* OnlinePreviewDecoder::LatestInstanceForTest() {
  return g_latest_instance.load(std::memory_order_acquire);
}

bool OnlinePreviewDecoder::Configure(const Config& config) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  // Begin 之后配置已随缓存/线程生效，热切换会让已有缓存帧尺寸与预算/窗口
  // 语义不一致；按要求拒绝且不改动已生效配置。
  if (impl_->begin_called) {
    return false;
  }
  impl_->max_side = std::clamp(config.max_side, kMinPreviewMaxSide,
                               kMaxPreviewMaxSide);
  impl_->cache_budget_bytes =
      std::clamp<std::size_t>(config.cache_budget_bytes, kMinCacheBudgetBytes,
                              kMaxCacheBudgetBytes);
  return true;
}

OnlinePreviewDecoder::Config OnlinePreviewDecoder::config() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  Config config;
  config.max_side = impl_->max_side;
  config.cache_budget_bytes = impl_->cache_budget_bytes;
  return config;
}

int OnlinePreviewDecoder::cached_frame_limit() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->CachedFrameLimitLocked();
}

int OnlinePreviewDecoder::preview_width() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->preview_width;
}

int OnlinePreviewDecoder::preview_height() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->preview_height;
}

bool OnlinePreviewDecoder::alloc_fallback_used() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->alloc_fallback_used;
}

void OnlinePreviewDecoder::ForceAllocationFailureForTest(int n) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->forced_alloc_failures = std::max(0, n);
}

void OnlinePreviewDecoder::Begin(
    std::shared_ptr<lapian::online::GrowingSource> source,
    const std::string& part_path_utf8, int video_stream_index,
    std::shared_ptr<const FrameIndex> index, std::int64_t initial_frame,
    bool initial_cached, std::int64_t displayed_frame, bool warm_only) {
  impl_->Close();
  if (!source || part_path_utf8.empty() || video_stream_index < 0 || !index) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->error = "在线预览参数不完整";
    return;
  }
  const bool initial_covered =
      initial_frame >= 0 &&
      static_cast<std::size_t>(initial_frame) < index->size();
  std::int64_t initial_keyframe = -1;
  if (initial_covered) {
    initial_keyframe = index->KeyframeAtOrBeforeFrame(initial_frame);
  }
  const std::int64_t baseline =
      displayed_frame >= 0 &&
              static_cast<std::size_t>(displayed_frame) < index->size()
          ? displayed_frame
          : (initial_frame >= 0 ? initial_frame : 0);
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->begin_called = true;
    impl_->source = std::move(source);
    impl_->path = part_path_utf8;
    impl_->stream_index = video_stream_index;
    impl_->index = std::move(index);
    impl_->interrupt = std::make_shared<std::atomic<bool>>(false);
    impl_->stop = false;
    impl_->active = !warm_only;
    impl_->running = true;
    impl_->pause_requested = false;
    impl_->waiting =
        warm_only ? false : !(initial_cached && initial_covered);
    // generation 从 1 起步，保证 worker 立即处理初始目标（不必等第一次
    // UpdateTarget）。
    impl_->generation = 1;
    impl_->handled_generation = 0;
    impl_->target_frame = initial_frame;
    impl_->target_cached = !warm_only && initial_cached && initial_covered;
    impl_->target_direction = 0;
    impl_->open_attempted = false;
    impl_->open_ok = false;
    impl_->error.clear();
    impl_->frames_published = 0;
    impl_->stale_dropped = 0;
    impl_->seek_count = 0;
    impl_->cache_hits = 0;
    impl_->cache_misses = 0;
    impl_->coarse_published = 0;
    impl_->prefetched_frames = 0;
    impl_->decoded_frames = 0;
    impl_->cache_bytes = 0;
    impl_->cache_peak_bytes = 0;
    // 新会话：帧尺寸证据从 0 开始（配置/预算保留，不清回退记录）。
    impl_->preview_width = 0;
    impl_->preview_height = 0;
    impl_->last_published_frame = -1;
    // P0 度量：Begin 记录覆盖前旧值与本拖动 baseline（只读统计）。
    impl_->ResetDragStatsLocked();
    impl_->drag_stats.last_delivered_before_resume =
        impl_->last_delivered_frame;
    impl_->drag_stats.baseline_frame = baseline;
    impl_->last_delivered_frame = baseline;
    impl_->last_delivered_coarse = false;
    impl_->last_pts_us = 0;
    impl_->last_decoded_frame = -1;
    impl_->position_valid = false;
    impl_->prefetch_stop = false;
    impl_->prefetch_from = -1;
    impl_->prefetch_to = -1;
    impl_->prefetch_epoch = 0;
    impl_->prefetch_served_to = -1;
    impl_->seek_landing_pending = false;
    impl_->ClearPendingPrimesLocked();
    impl_->primed_frames = 0;
    if (warm_only) {
      impl_->warm_requested = true;
      impl_->warming = false;
      impl_->warm_idle = false;
      ++impl_->warm_epoch;
      const std::int64_t last_frame =
          static_cast<std::int64_t>(impl_->index->size()) - 1;
      const std::int64_t origin =
          std::max<std::int64_t>(0, std::min(initial_frame, last_frame));
      impl_->warm_origin =
          std::max<std::int64_t>(0, origin - impl_->CacheWindowLocked());
      impl_->warm_target =
          std::min<std::int64_t>(last_frame, origin + 10);
      impl_->warm_start = std::chrono::steady_clock::now();
      impl_->warm_started = true;
      impl_->warm_ms = -1;
      impl_->warm_frames = 0;
    } else {
      impl_->warm_requested = false;
      impl_->warming = false;
      impl_->warm_idle = true;
      impl_->warm_started = false;
      ++impl_->warm_epoch;
    }
  }
  impl_->thread = std::thread([impl = impl_.get()] { impl->Run(); });
  impl_->prefetch_thread =
      std::thread([impl = impl_.get()] { impl->RunPrefetch(); });
  Log("begin target=%lld cached=%d keyframe=%lld baseline=%lld warm_only=%d",
      static_cast<long long>(initial_frame), initial_cached ? 1 : 0,
      static_cast<long long>(initial_keyframe),
      static_cast<long long>(baseline), warm_only ? 1 : 0);
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
  const bool target_covered =
      impl_->target_frame >= 0 &&
      static_cast<std::size_t>(impl_->target_frame) < index->size();
  impl_->index = std::move(index);
  // 索引增长本身不需要 worker 重发同一目标帧（会导致每 30 帧一次重复
  // publish）；只有目标从“快照外”变成“已覆盖”时才唤醒重解。
  if (target_covered && !impl_->target_cached) {
    ++impl_->generation;
  }
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
  bool exact_hit = false;
  const std::int64_t previous_target = impl_->target_frame;
  const bool far_jump =
      previous_target >= 0 && Distance(frame, previous_target) >= kFarJumpFrames;
  if (frame < impl_->target_frame) {
    impl_->target_direction = -1;
  } else if (frame > impl_->target_frame) {
    impl_->target_direction = 1;
  }
  impl_->target_frame = frame;
  impl_->target_cached = cached && covered;
  impl_->waiting = !impl_->target_cached;
  ++impl_->generation;
  // 目标变化只丢弃“比上一张更远/越过目标”的旧帧；+-1 振荡不清空 latest。
  if (impl_->latest != nullptr && !impl_->LatestUsefulLocked(frame)) {
    impl_->FreeLatestLocked();
    ++impl_->stale_dropped;
  }
  if (impl_->target_cached) {
    // 先查精确缓存；命中立即发布，不依赖 worker seek/下一次 tick。
    if (AVFrame* exact = impl_->CloneExactLocked(frame)) {
      impl_->PublishExactLocked(exact, frame, impl_->generation, true);
      ++impl_->cache_hits;
      impl_->waiting = false;
      exact_hit = true;
    } else {
      ++impl_->cache_misses;
      // 没命中精确缓存：发布缓存里“更接近目标且不越过目标”的最近帧，
      // 让反向拖动在解码跟上前就持续有画面。远跳不发旧的最近缓存帧
      // （否则会被当成“粗帧”但其实离目标还很远），交给 worker 的关键帧
      // 粗帧 + 精确目标。
      std::int64_t closest = -1;
      if (!far_jump) {
        if (AVFrame* close = impl_->CloneClosestLocked(frame, &closest)) {
          const std::int64_t pts = impl_->index != nullptr
                                       ? impl_->index->PtsOfFrame(closest)
                                       : 0;
          impl_->PublishLocked(close, pts, closest, impl_->generation, false,
                               true);
        }
      }
    }
  } else {
    // P0：未缓存目标也发“last 与 target 之间最近”的缓存帧，waiting 保持 true；
    // 远跳也允许（没有可用帧时不发）。
    std::int64_t closest = -1;
    if (AVFrame* close = impl_->CloneClosestLocked(frame, &closest)) {
      const std::int64_t pts = impl_->index != nullptr
                                   ? impl_->index->PtsOfFrame(closest)
                                   : 0;
      impl_->PublishLocked(close, pts, closest, impl_->generation, false, true);
    }
  }
  // 反向：保证“缓存窗口下沿之前的 30 帧”进缓存，目标拖到边界时已经解好；
  // 正向：取消反向预取，由需求解码向前续解。+-1 振荡反复请求同一区间时
  // epoch 不变，预取线程继续而不会被反复取消。
  if (impl_->target_cached && impl_->index != nullptr) {
    if (impl_->target_direction < 0 && !impl_->cache.empty()) {
      std::int64_t cache_min = std::numeric_limits<std::int64_t>::max();
      for (const CachedPreview& entry : impl_->cache) {
        if (entry.frame != nullptr && entry.frame_index >= 0) {
          cache_min = std::min(cache_min, entry.frame_index);
        }
      }
      if (cache_min > 0) {
        const std::int64_t to = cache_min - 1;
        const std::int64_t from =
            std::max<std::int64_t>(0, to - kBackwardPrefetchFrames);
        if ((from != impl_->prefetch_from || to != impl_->prefetch_to) &&
            to != impl_->prefetch_served_to) {
          impl_->prefetch_from = from;
          impl_->prefetch_to = to;
          ++impl_->prefetch_epoch;
        }
      }
    } else if (impl_->target_direction > 0) {
      if (impl_->prefetch_to >= 0) {
        impl_->prefetch_from = -1;
        impl_->prefetch_to = -1;
        ++impl_->prefetch_epoch;
      }
    }
  }
  impl_->prefetch_condition.notify_all();
  impl_->condition.notify_all();
  Log("target frame=%lld cached=%d exact_hit=%d dir=%d cache_frames=%d",
      static_cast<long long>(frame), impl_->target_cached ? 1 : 0,
      exact_hit ? 1 : 0, impl_->target_direction,
      static_cast<int>(impl_->cache.size()));
}

void OnlinePreviewDecoder::End() { impl_->End(); }

void OnlinePreviewDecoder::Close() { impl_->Close(); }

void OnlinePreviewDecoder::Pause() { impl_->Pause(); }

void OnlinePreviewDecoder::Resume(std::int64_t frame, bool cached,
                                  std::int64_t displayed_frame) {
  impl_->Resume(frame, cached, displayed_frame);
}

void OnlinePreviewDecoder::Warm(std::int64_t frame) {
  bool start_threads = false;
  bool join_stale = false;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->stop || frame < 0 || impl_->index == nullptr ||
        impl_->index->empty()) {
      return;
    }
    if (impl_->active) {
      // 拖动进行中不预热；Resume 会取消在途 warm。
      return;
    }
    const std::int64_t limit =
        static_cast<std::int64_t>(impl_->index->size()) - 1;
    const std::int64_t origin = std::min(frame, limit);
    const std::int64_t target =
        std::min<std::int64_t>(limit, origin + 10);
    impl_->warm_origin =
        std::max<std::int64_t>(0, origin - impl_->CacheWindowLocked());
    impl_->warm_target = target;
    ++impl_->warm_epoch;
    impl_->warm_start = std::chrono::steady_clock::now();
    impl_->warm_started = true;
    impl_->warm_ms = -1;
    impl_->warm_frames = 0;
    const std::int64_t window = impl_->CacheWindowLocked();
    // 已有缓存窗口且解码位置已到 target：直接 idle，不重复解码。
    if (impl_->position_valid && impl_->last_decoded_frame >= target &&
        impl_->RangeCachedLocked(target - window, target)) {
      ++impl_->warm_skipped;
      impl_->warm_requested = false;
      impl_->warming = false;
      impl_->warm_idle = true;
      impl_->warm_started = false;
      impl_->warm_ms = 0;
      Log("warm_skip target=%lld cache=%d",
          static_cast<long long>(target),
          static_cast<int>(impl_->cache.size()));
      return;
    }
    impl_->warm_requested = true;
    impl_->warming = true;
    impl_->warm_idle = false;
    if (!impl_->thread.joinable()) {
      start_threads = true;
    } else if (!impl_->running) {
      start_threads = true;
      join_stale = true;
    }
    if (start_threads) {
      impl_->position_valid = false;
      impl_->last_pts_us = 0;
      impl_->last_decoded_frame = -1;
      impl_->interrupt = std::make_shared<std::atomic<bool>>(false);
      impl_->generation = 1;
      impl_->handled_generation = 0;
      impl_->pause_requested = false;
      impl_->stop = false;
      if (join_stale) {
        // 先唤醒 wait 中的 prefetch_thread；unlock 后 join 两个线程。
        impl_->prefetch_stop = true;
        impl_->prefetch_condition.notify_all();
      } else {
        impl_->prefetch_stop = false;
      }
      impl_->running = true;
    }
    impl_->condition.notify_all();
    Log("warm target=%lld origin=%lld cache=%d start=%d",
        static_cast<long long>(target), static_cast<long long>(origin),
        static_cast<int>(impl_->cache.size()), start_threads ? 1 : 0);
  }
  if (join_stale) {
    if (impl_->thread.joinable()) {
      impl_->thread.join();
    }
    if (impl_->prefetch_thread.joinable()) {
      impl_->prefetch_thread.join();
    }
  }
  if (start_threads) {
    impl_->StartThreads();
  }
}

void OnlinePreviewDecoder::PrimeFrame(const AVFrame* frame,
                                      std::int64_t frame_index) {
  if (frame == nullptr || frame_index < 0 ||
      frame->hw_frames_ctx != nullptr) {
    return;
  }
  AVFrame* clone = av_frame_clone(frame);
  if (clone == nullptr) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->stop) {
      av_frame_free(&clone);
      return;
    }
    impl_->pending_primes.push_back(
        Impl::PendingPrime{clone, frame_index});
    impl_->prime_requested = true;
    if (impl_->thread.joinable()) {
      impl_->condition.notify_all();
    }
  }
}

PreviewFrame OnlinePreviewDecoder::TakeLatestFrame() {
  PreviewFrame result;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->active || impl_->latest == nullptr) {
    return result;
  }
  const std::int64_t target = impl_->target_frame;
  const std::int64_t latest_frame = impl_->latest_frame_index;
  const bool latest_coarse = impl_->latest_coarse;
  const std::int64_t last = impl_->last_delivered_frame;
  bool accept = false;
  if (latest_coarse) {
    if (last < 0) {
      accept = true;
    } else if (target < last) {
      accept = latest_frame < last;
    } else if (target > last) {
      accept = latest_frame > last;
    } else {
      accept = false;
    }
    // 精确目标已呈现后，粗帧不得再覆盖它。
    if (last == target && !impl_->last_delivered_coarse) {
      accept = false;
    }
  } else {
    if (latest_frame == target) {
      accept = true;
    } else if (last < 0) {
      // baseline 正常不会走到这里；兜底允许首帧，方向规则无法判断。
      accept = latest_frame >= 0;
    } else if (target > last) {
      accept = latest_frame > last && latest_frame < target;
    } else if (target < last) {
      accept = latest_frame < last && latest_frame > target;
    } else {
      accept = false;
    }
    // 同一帧不重复呈现（避免 presented 注水）。
    if (latest_frame == last) {
      accept = false;
    }
  }
  if (!accept) {
    av_frame_free(&impl_->latest);
    impl_->latest_pts_us = 0;
    impl_->latest_frame_index = -1;
    impl_->latest_generation = 0;
    impl_->latest_coarse = false;
    impl_->latest_from_cache = false;
    ++impl_->stale_dropped;
    return result;
  }
  result.frame = impl_->latest;
  result.pts_us = impl_->latest_pts_us;
  result.frame_index = impl_->latest_frame_index;
  result.generation = impl_->latest_generation;
  result.coarse = impl_->latest_coarse;
  result.from_cache = impl_->latest_from_cache;
  result.hardware = false;
  impl_->last_delivered_frame = impl_->latest_frame_index;
  impl_->last_delivered_coarse = impl_->latest_coarse;
  ++impl_->drag_stats.delivered;
  impl_->latest = nullptr;
  impl_->latest_pts_us = 0;
  impl_->latest_frame_index = -1;
  impl_->latest_generation = 0;
  impl_->latest_coarse = false;
  impl_->latest_from_cache = false;
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

bool OnlinePreviewDecoder::IsWarmIdle() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->warm_idle;
}

bool OnlinePreviewDecoder::IsWarming() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->warming;
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

std::size_t OnlinePreviewDecoder::cache_bytes() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->cache_bytes;
}

std::size_t OnlinePreviewDecoder::cache_peak_bytes() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->cache_peak_bytes;
}

std::uint64_t OnlinePreviewDecoder::cache_hits() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->cache_hits;
}

std::uint64_t OnlinePreviewDecoder::cache_misses() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->cache_misses;
}

std::uint64_t OnlinePreviewDecoder::coarse_published() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->coarse_published;
}

std::uint64_t OnlinePreviewDecoder::prefetched_frames() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->prefetched_frames;
}

std::uint64_t OnlinePreviewDecoder::decoded_frames() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->decoded_frames;
}

OnlinePreviewDecoder::DragStats OnlinePreviewDecoder::drag_stats() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->drag_stats;
}

std::uint64_t OnlinePreviewDecoder::seek_fallbacks() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->seek_fallbacks;
}

std::int64_t OnlinePreviewDecoder::last_seek_keyframe() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->last_seek_keyframe;
}

std::int64_t OnlinePreviewDecoder::last_seek_landing_frame() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->last_seek_landing_frame;
}

std::int64_t OnlinePreviewDecoder::last_seek_landing_offset() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->last_seek_landing_offset;
}

std::int64_t OnlinePreviewDecoder::warm_ms() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->warm_ms;
}

std::int64_t OnlinePreviewDecoder::warm_frames() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->warm_frames;
}

std::uint64_t OnlinePreviewDecoder::warm_skipped() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->warm_skipped;
}

std::uint64_t OnlinePreviewDecoder::warm_failures() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->warm_failures;
}

std::uint64_t OnlinePreviewDecoder::primed_frames() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->primed_frames;
}

bool OnlinePreviewDecoder::IsFrameCached(std::int64_t frame) const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->HasExactLocked(frame);
}

bool OnlinePreviewDecoder::DumpCachedFrameNv12ForTest(
    std::int64_t frame_index, const std::string& path) const {
  if (frame_index < 0 || path.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const AVFrame* frame = nullptr;
  for (const CachedPreview& cached : impl_->cache) {
    if (cached.frame_index == frame_index && cached.frame != nullptr) {
      frame = cached.frame;
      break;
    }
  }
  if (frame == nullptr || frame->width <= 0 || frame->height <= 0 ||
      (frame->height & 1) != 0 || frame->data[0] == nullptr ||
      frame->linesize[0] < frame->width) {
    return false;
  }
  // 缓存帧格式随路径不同：降采样路径是 NV12；max_side 等于源尺寸时
  // ScaleToNv12 直接 av_frame_clone 源帧（软件解码常见 yuv420p），不做转换。
  // dump 统一写“NV12 布局”（Y w*h + UV w*h/2），yuv420p 在这里把 U/V 交织，
  // 只影响证据文件，不改缓存里的帧本身。
  const bool nv12 = frame->format == AV_PIX_FMT_NV12;
  const bool yuv420p = frame->format == AV_PIX_FMT_YUV420P ||
                       frame->format == AV_PIX_FMT_YUVJ420P;
  if (!nv12 && !yuv420p) {
    return false;
  }
  if (nv12 && (frame->data[1] == nullptr ||
               frame->linesize[1] < frame->width)) {
    return false;
  }
  if (yuv420p && (frame->data[1] == nullptr || frame->data[2] == nullptr ||
                  frame->linesize[1] < frame->width / 2 ||
                  frame->linesize[2] < frame->width / 2)) {
    return false;
  }
  const int width = frame->width;
  const int height = frame->height;
  FILE* file = nullptr;
#if defined(_MSC_VER)
  if (fopen_s(&file, path.c_str(), "wb") != 0 || file == nullptr) {
    return false;
  }
#else
  file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    return false;
  }
#endif
  bool ok = true;
  std::vector<unsigned char> row(static_cast<std::size_t>(width));
  // Y 平面：w*h，逐行去掉 linesize padding。
  for (int y = 0; y < height && ok; ++y) {
    std::memcpy(row.data(),
                frame->data[0] + static_cast<std::ptrdiff_t>(y) *
                                     frame->linesize[0],
                static_cast<std::size_t>(width));
    ok = std::fwrite(row.data(), 1, static_cast<std::size_t>(width), file) ==
         static_cast<std::size_t>(width);
  }
  // UV 平面（NV12 交织）：w*(h/2)。
  const int uv_height = height / 2;
  for (int y = 0; y < uv_height && ok; ++y) {
    if (nv12) {
      std::memcpy(row.data(),
                  frame->data[1] + static_cast<std::ptrdiff_t>(y) *
                                       frame->linesize[1],
                  static_cast<std::size_t>(width));
    } else {
      const unsigned char* u = frame->data[1] +
                               static_cast<std::ptrdiff_t>(y) *
                                   frame->linesize[1];
      const unsigned char* v = frame->data[2] +
                               static_cast<std::ptrdiff_t>(y) *
                                   frame->linesize[2];
      const int chroma_width = width / 2;
      for (int x = 0; x < chroma_width; ++x) {
        row[static_cast<std::size_t>(2 * x)] = u[x];
        row[static_cast<std::size_t>(2 * x + 1)] = v[x];
      }
    }
    ok = std::fwrite(row.data(), 1, static_cast<std::size_t>(width), file) ==
         static_cast<std::size_t>(width);
  }
  std::fclose(file);
  return ok;
}

}  // namespace lapian::media
