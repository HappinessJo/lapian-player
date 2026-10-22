#include "media/MediaSession.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace lapian::media {

namespace {

using Clock = std::chrono::steady_clock;

double ElapsedMs(Clock::time_point started) {
  return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

int BitDepthOfFormat(int format) {
  switch (format) {
    case AV_PIX_FMT_P010:
    case AV_PIX_FMT_P016:
      return 10;
    default:
      return 8;
  }
}

// T7.9：精确 seek 后紧接 PgUp 的一帧 handoff 只保留目标前一帧的 AVFrame 引用；
// 硬解帧不回拷，避免 av_hwframe_transfer_data 等 GPU 队列（实测 70–80ms），
// 否则会把 P6/P9 的余量吃掉。引用在下一次 seek/Close 时释放。

// T7.11.1：只对 1080p 及以下硬解后退窗口做延后回拷。4K 的 20 帧窗口若持有
// D3D11 表面约 250MB（显存或共享内存），继续走同步回拷，避免拖坏 4K 路径。
constexpr int kDeferredCacheMaxPixels = 1920 * 1088;

}  // namespace

MediaSession::MediaSession() = default;

MediaSession::~MediaSession() {
  if (last_hit_frame_ != nullptr) {
    av_frame_free(&last_hit_frame_);
  }
  ClearSeekHandoff();
  CancelPendingCacheWindow();
}

void MediaSession::ClearSeekHandoff() {
  if (seek_previous_frame_ != nullptr) {
    av_frame_free(&seek_previous_frame_);
  }
  seek_previous_index_ = -1;
  seek_previous_info_ = DecodedFrameInfo{};
}

bool MediaSession::Open(const std::string& utf8_path, ID3D11Device* device, std::string* error,
                        bool build_index) {
  if (error == nullptr) {
    return false;
  }
  error->clear();
  if (last_hit_frame_ != nullptr) {
    av_frame_free(&last_hit_frame_);
  }
  frame_cache_.Clear();
  ClearSeekHandoff();
  CancelPendingCacheWindow();
  decoder_stale_ = true;

  // T7.1：分段计时只用于冷启动报告；失败路径也记录，便于区分探测/解码/索引。
  const auto open_started = Clock::now();
  auto timings = MediaOpenTimings{};

  const auto probe_started = Clock::now();
  last_attempt_ = ProbeMedia(utf8_path);
  timings.probe_ms = ElapsedMs(probe_started);
  if (!last_attempt_.ok) {
    *error = last_attempt_.error_detail.empty() ? "无法打开文件" : last_attempt_.error_detail;
    timings.total_ms = ElapsedMs(open_started);
    last_open_timings_ = timings;
    return false;
  }

  const auto decoder_started = Clock::now();
  auto decoder = std::make_unique<VideoDecoder>();
  if (!decoder->OpenWithD3D11Device(utf8_path, last_attempt_.info.video.stream_index, device,
                                    true, error)) {
    timings.decoder_open_ms = ElapsedMs(decoder_started);
    timings.total_ms = ElapsedMs(open_started);
    last_open_timings_ = timings;
    return false;
  }
  timings.decoder_open_ms = ElapsedMs(decoder_started);

  const auto first_frame_started = Clock::now();
  DecodedFrameInfo first_frame;
  if (!decoder->DecodeFirstFrame(&first_frame, error)) {
    timings.first_frame_ms = ElapsedMs(first_frame_started);
    timings.total_ms = ElapsedMs(open_started);
    last_open_timings_ = timings;
    return false;
  }
  timings.first_frame_ms = ElapsedMs(first_frame_started);

  // 索引失败不阻塞打开；步进/输入在索引为空时禁用。
  // T7.1：异步打开时这里跳过，首帧先呈现，索引由后台 worker 建好后 InstallIndex。
  if (build_index) {
    const auto index_started = Clock::now();
    FrameIndex index;
    std::string index_error;
    if (!index.BuildFromFile(utf8_path, last_attempt_.info.video.stream_index, &index_error)) {
      index.Reset();
    } else {
      timings.index_ok = true;
      timings.index_frames = index.size();
    }
    timings.index_ms = ElapsedMs(index_started);
    index_ = std::move(index);
  } else {
    // 复用同一个 MediaSession 再次打开时必须丢掉旧索引，避免把上一个文件的
    // 帧号/PTS 带到新会话。
    index_.Reset();
  }
  timings.total_ms = ElapsedMs(open_started);
  last_open_timings_ = timings;

  // 只有探测/解码都成功才替换旧会话。
  probe_ = last_attempt_;
  decoder_ = std::move(decoder);
  first_frame_ = first_frame;
  open_ = true;
  // 首帧已解码，解码器下一次 DecodeNextFrame 返回第 1 帧。
  decoder_stale_ = false;
  sequential_frame_ = 1;
  // T7.2：预算随分辨率/位深固定，不随片长增长。缓存为空时不影响 P10。
  const lapian::media::FrameCacheLimits limits = FrameCache::LimitsForFrame(
      probe_.info.video.width, probe_.info.video.height,
      probe_.info.video.bit_depth);
  frame_cache_.SetLimits(limits.max_bytes, limits.max_entries);
  return true;
}

bool MediaSession::BuildIndexNow(std::string* error, const std::atomic<bool>* cancel) {
  if (!open_) {
    if (error != nullptr) {
      *error = "会话未打开";
    }
    return false;
  }
  FrameIndex index;
  const auto started = Clock::now();
  std::string build_error;
  if (!index.BuildFromFile(probe_.info.path_utf8, video_stream_index(), &build_error, cancel)) {
    if (error != nullptr) {
      *error = build_error;
    }
    return false;
  }
  const double build_ms = ElapsedMs(started);
  InstallIndex(std::move(index), build_ms);
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

void MediaSession::InstallIndex(FrameIndex index, double build_ms) {
  // 新索引可能改变帧号/PTS 映射，旧缓存必须丢弃。
  frame_cache_.Clear();
  ClearSeekHandoff();
  CancelPendingCacheWindow();
  decoder_stale_ = true;
  sequential_frame_ = -1;
  index_ = std::move(index);
  last_open_timings_.index_ok = HasIndex();
  last_open_timings_.index_frames = index_.size();
  last_open_timings_.index_ms = build_ms;
}

bool MediaSession::SeekToFrame(std::int64_t frame, DecodedFrameInfo* out,
                               std::string* error, FrameCacheMode cache_mode) {
  if (error == nullptr || out == nullptr) {
    return false;
  }
  error->clear();
  // T7.4 诊断：只在设置 LAPIAN_SEEK_LOG 时输出每次 seek 的命中/关键帧距离/分项耗时。
  const bool seek_log = std::getenv("LAPIAN_SEEK_LOG") != nullptr;
  const auto seek_started = Clock::now();
  // 上一帧的命中引用只保证到下一次 Seek；先释放，避免调用方看到悬空指针。
  if (last_hit_frame_ != nullptr) {
    av_frame_free(&last_hit_frame_);
  }
  decoder_stale_ = true;
  if (!open_ || decoder_ == nullptr) {
    *error = "会话未打开";
    return false;
  }
  if (!index_.IsReady() || index_.empty()) {
    *error = "PTS 索引未就绪";
    return false;
  }
  if (frame < 0 || static_cast<std::size_t>(frame) >= index_.size()) {
    *error = "目标帧越界";
    return false;
  }

  // T7.9：精确 seek 后的 handoff。目标正好是上一帧时直接返回引用，
  // 不再从关键帧解码；这是 spec P7“精确 seek 后紧接 PgUp”的单步路径。
  if (cache_mode == FrameCacheMode::kBackwardWindow &&
      seek_previous_index_ == frame && seek_previous_frame_ != nullptr) {
    AVFrame* clone = av_frame_clone(seek_previous_frame_);
    if (clone != nullptr) {
      last_hit_frame_ = clone;
      *out = seek_previous_info_;
      out->av_frame = clone;
      decoder_stale_ = true;
      sequential_frame_ = -1;
      if (seek_log) {
        std::fprintf(stderr,
                     "T7.4 seek_log frame=%lld hit=2 handoff=1 total_ms=%.2f\n",
                     static_cast<long long>(frame), ElapsedMs(seek_started));
        std::fflush(stderr);
      }
      return true;
    }
  }
  // 新的 seek 会让上一帧 handoff 失效。
  ClearSeekHandoff();

  // T7.11.1：回拷完成前，连续后退仍从待回拷窗口直接命中。命中只 clone 一次
  // AVFrame 引用（硬解帧保留纹理引用），不触发 GPU→CPU 回拷。
  if (cache_mode == FrameCacheMode::kBackwardWindow) {
    for (auto it = pending_cache_window_.rbegin(); it != pending_cache_window_.rend();
         ++it) {
      if (it->frame_index != frame) {
        continue;
      }
      AVFrame* clone = av_frame_clone(it->frame);
      if (clone != nullptr) {
        last_hit_frame_ = clone;
        *out = it->info;
        out->av_frame = clone;
        decoder_stale_ = true;
        sequential_frame_ = -1;
        if (seek_log) {
          std::fprintf(stderr,
                       "T7.4 seek_log frame=%lld hit=3 pending=1 total_ms=%.2f\n",
                       static_cast<long long>(frame), ElapsedMs(seek_started));
          std::fflush(stderr);
        }
        return true;
      }
      break;
    }
  }

  // 非后退操作（跳转/前进/起播）不使用后退窗口，进入解码前先释放引用。
  if (cache_mode != FrameCacheMode::kBackwardWindow &&
      !pending_cache_window_.empty()) {
    CancelPendingCacheWindow();
  }

  // T7.4：前进步进/精确 seek 后紧接 PgDn：解码器已经停在目标帧上时直接解一帧。
  // 旧实现每次前进都从最近关键帧重解到目标（4K 2s GOP 约 20–60 帧，p95 >200ms）。
  if (cache_mode == FrameCacheMode::kNone && sequential_frame_ == frame &&
      decoder_ != nullptr) {
    DecodedFrameInfo info;
    if (decoder_->DecodeNextFrame(&info, error)) {
      *out = info;
      sequential_frame_ = frame + 1;
      decoder_stale_ = false;
      if (seek_log) {
        std::fprintf(stderr,
                     "T7.4 seek_log frame=%lld hit=0 sequential=1 decoded=1 "
                     "total_ms=%.2f\n",
                     static_cast<long long>(frame), ElapsedMs(seek_started));
        std::fflush(stderr);
      }
      return true;
    }
    // 顺序位置失效（EOF 等）时退回关键帧路径，不改变既有帧精确语义。
    sequential_frame_ = -1;
  }

  if (cache_mode == FrameCacheMode::kBackwardWindow) {
    AVFrame* hit = nullptr;
    FrameCacheEntryInfo cache_info;
    if (frame_cache_.Get(frame, &hit, &cache_info)) {
      last_hit_frame_ = hit;
      out->width = hit->width;
      out->height = hit->height;
      out->pixel_format = hit->format;
      const char* format_name =
          av_get_pix_fmt_name(static_cast<AVPixelFormat>(hit->format));
      out->pixel_format_name = format_name != nullptr ? format_name : "unknown";
      out->bit_depth = BitDepthOfFormat(hit->format);
      out->pts = cache_info.pts;
      out->duration = 0;  // 缓存只保留微秒时长；调用方用 duration_us。
      out->pts_us = cache_info.pts_us;
      out->duration_us = cache_info.duration_us;
      out->key_frame = cache_info.keyframe;
      out->hardware = false;
      out->av_frame = hit;
      // 缓存命中没有移动解码器；起播前必须重新定位。
      decoder_stale_ = true;
      sequential_frame_ = -1;
      if (seek_log) {
        std::fprintf(stderr,
                     "T7.4 seek_log frame=%lld hit=1 total_ms=%.2f\n",
                     static_cast<long long>(frame), ElapsedMs(seek_started));
        std::fflush(stderr);
      }
      return true;
    }
  }

  // 后退窗口和 FrameCache 都没有目标帧，接下来要真正解码；先释放窗口引用，
  // 避免解码器申请表面时和这 24 帧的引用撞车。
  if (cache_mode == FrameCacheMode::kBackwardWindow &&
      !pending_cache_window_.empty()) {
    CancelPendingCacheWindow();
  }

  const std::size_t max_entries = frame_cache_.stats().max_entries;
  std::int64_t decoded_frames = 0;
  std::size_t deferred_frames = 0;
  double cache_ms = 0.0;
  double clone_ms = 0.0;
  auto decode_range = [&](std::int64_t start_frame) -> bool {
    std::int64_t cache_from = frame + 1;
    if (cache_mode == FrameCacheMode::kBackwardWindow && max_entries > 0) {
      cache_from = std::max(
          start_frame, frame - static_cast<std::int64_t>(max_entries) + 1);
    }
    AVFrame* previous_candidate = nullptr;
    DecodedFrameInfo previous_info;
    for (std::int64_t index = start_frame; index <= frame; ++index) {
      DecodedFrameInfo info;
      if (!decoder_->DecodeNextFrame(&info, error)) {
        if (previous_candidate != nullptr) {
          av_frame_free(&previous_candidate);
        }
        return false;
      }
      ++decoded_frames;
      if (cache_mode != FrameCacheMode::kNone && index >= cache_from) {
        // T7.11.1：1080p 硬解后退窗口先保留引用，回拷交给事件循环空闲时分块做。
        // 目标帧本身也在窗口内，clone 后再返回；调用方呈现完才开始回拷。
        const bool defer = cache_mode == FrameCacheMode::kBackwardWindow &&
                           info.hardware &&
                           static_cast<std::int64_t>(info.width) * info.height <=
                               kDeferredCacheMaxPixels;
        const auto clone_started = Clock::now();
        if (defer && AddPendingCacheFrame(info, index)) {
          ++deferred_frames;
          clone_ms += ElapsedMs(clone_started);
        } else {
          clone_ms += ElapsedMs(clone_started);
          const auto cache_started = Clock::now();
          frame_cache_.Put(info.av_frame, index, info.pts, info.pts_us,
                           info.duration_us, info.key_frame);
          cache_ms += ElapsedMs(cache_started);
        }
      }
      // T7.9：只保留目标前一帧的引用；目标帧本身直接交给调用方呈现。
      if (cache_mode == FrameCacheMode::kSeekHandoff && index == frame - 1) {
        if (previous_candidate != nullptr) {
          av_frame_free(&previous_candidate);
        }
        previous_candidate = av_frame_clone(info.av_frame);
        if (previous_candidate != nullptr) {
          previous_info = info;
          previous_info.av_frame = nullptr;
        }
      }
      if (index == frame) {
        *out = info;
        decoder_stale_ = false;
        sequential_frame_ = frame + 1;
        if (previous_candidate != nullptr) {
          ClearSeekHandoff();
          seek_previous_frame_ = previous_candidate;
          seek_previous_index_ = frame - 1;
          seek_previous_info_ = previous_info;
          previous_candidate = nullptr;
        }
        return true;
      }
    }
    if (previous_candidate != nullptr) {
      av_frame_free(&previous_candidate);
    }
    *error = "seek 解码未到目标帧";
    return false;
  };

  bool ok = false;
  std::int64_t keyframe = -1;
  double seek_ms = 0.0;
  // 一旦重新 seek，旧顺序位置失效。
  sequential_frame_ = -1;
  if (index_.IsReliable()) {
    const std::int64_t target_pts = index_.PtsOfFrame(frame);
    keyframe = index_.KeyframeAtOrBefore(target_pts);
    if (keyframe < 0) {
      keyframe = 0;
    }
    const auto seek_start = Clock::now();
    const bool seek_ok = decoder_->SeekToPts(index_.PtsOfFrame(keyframe), error);
    seek_ms += ElapsedMs(seek_start);
    if (seek_ok && decode_range(keyframe)) {
      ok = true;
    }
  }

  if (!ok) {
    // 索引不可靠或关键帧路径失败：从头解码计数。
    CancelPendingCacheWindow();
    deferred_frames = 0;
    decoded_frames = 0;
    cache_ms = 0.0;
    clone_ms = 0.0;
    const auto seek_start = Clock::now();
    const bool seek_ok = decoder_->SeekToPts(index_.PtsOfFrame(0), error);
    seek_ms += ElapsedMs(seek_start);
    keyframe = 0;
    ok = seek_ok && decode_range(0);
  }
  if (seek_log) {
    std::fprintf(stderr,
                 "T7.4 seek_log frame=%lld hit=0 keyframe=%lld decoded=%lld "
                 "seek_ms=%.2f decode_ms=%.2f clone_ms=%.2f cache_ms=%.2f "
                 "total_ms=%.2f ok=%d pending=%zu\n",
                 static_cast<long long>(frame), static_cast<long long>(keyframe),
                 static_cast<long long>(decoded_frames), seek_ms,
                 ElapsedMs(seek_started) - seek_ms - cache_ms - clone_ms, clone_ms,
                 cache_ms, ElapsedMs(seek_started), ok ? 1 : 0, deferred_frames);
    std::fflush(stderr);
  }
  if (!ok) {
    CancelPendingCacheWindow();
  }
  return ok;
}

bool MediaSession::AddPendingCacheFrame(const DecodedFrameInfo& info,
                                        std::int64_t frame_index) {
  if (info.av_frame == nullptr) {
    return false;
  }
  AVFrame* clone = av_frame_clone(info.av_frame);
  if (clone == nullptr) {
    return false;
  }
  PendingCacheFrame pending;
  pending.frame = clone;
  pending.frame_index = frame_index;
  pending.info = info;
  pending.info.av_frame = nullptr;
  pending_cache_window_.push_back(std::move(pending));
  return true;
}

void MediaSession::CancelPendingCacheWindow() {
  for (PendingCacheFrame& entry : pending_cache_window_) {
    if (entry.frame != nullptr) {
      av_frame_free(&entry.frame);
    }
  }
  pending_cache_window_.clear();
}

bool MediaSession::CopyPendingCacheFrames(std::size_t max_frames) {
  if (max_frames == 0) {
    return !pending_cache_window_.empty();
  }
  const bool seek_log = std::getenv("LAPIAN_SEEK_LOG") != nullptr;
  const auto started = Clock::now();
  std::size_t copied = 0;
  // 从窗口头部（更早的帧）开始回拷：FrameCache 的 64MiB 预算实际只能放 21 帧
  // （24×3.11MB > 64MiB），淘汰的是最早写入的条目；先写旧帧可以把新帧留在
  // 缓存里，正好覆盖“继续后退”的下一批目标。
  while (copied < max_frames && !pending_cache_window_.empty()) {
    PendingCacheFrame entry = std::move(pending_cache_window_.front());
    pending_cache_window_.erase(pending_cache_window_.begin());
    if (entry.frame != nullptr) {
      const bool put_ok =
          frame_cache_.Put(entry.frame, entry.frame_index, entry.info.pts,
                           entry.info.pts_us, entry.info.duration_us,
                           entry.info.key_frame);
      if (!put_ok && seek_log) {
        std::fprintf(stderr, "T7.11.1 cache_fill_put_failed frame=%lld\n",
                     static_cast<long long>(entry.frame_index));
        std::fflush(stderr);
      }
      av_frame_free(&entry.frame);
    }
    ++copied;
  }
  if (seek_log) {
    std::fprintf(stderr,
                 "T7.11.1 cache_fill_slice copied=%zu remaining=%zu total_ms=%.2f\n",
                 copied, pending_cache_window_.size(), ElapsedMs(started));
    std::fflush(stderr);
  }
  return !pending_cache_window_.empty();
}

bool MediaSession::EnsureSequentialPosition(std::int64_t frame, std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  // T7.11.1：起播要真正解码，先释放待回拷窗口占用的解码表面。
  CancelPendingCacheWindow();
  if (!open_ || decoder_ == nullptr) {
    if (error != nullptr) {
      *error = "会话未打开";
    }
    return false;
  }
  if (!decoder_stale_) {
    return true;
  }
  DecodedFrameInfo info;
  std::string seek_error;
  if (!SeekToFrame(frame, &info, &seek_error, FrameCacheMode::kNone)) {
    if (error != nullptr) {
      *error = seek_error;
    }
    return false;
  }
  return true;
}

void MediaSession::Close() {
  if (last_hit_frame_ != nullptr) {
    av_frame_free(&last_hit_frame_);
  }
  frame_cache_.Clear();
  ClearSeekHandoff();
  CancelPendingCacheWindow();
  decoder_stale_ = true;
  sequential_frame_ = -1;
  decoder_.reset();
  index_.Reset();
  probe_ = MediaProbeResult{};
  last_attempt_ = MediaProbeResult{};
  first_frame_ = DecodedFrameInfo{};
  open_ = false;
}

}  // namespace lapian::media
