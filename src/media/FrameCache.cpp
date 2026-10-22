#include "media/FrameCache.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <utility>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace lapian::media {
namespace {

constexpr std::size_t kMiB = 1024 * 1024;

int BitDepthOfFormat(int format) {
  switch (format) {
    case AV_PIX_FMT_P010:
    case AV_PIX_FMT_P016:
      return 10;
    default:
      return 8;
  }
}

}  // namespace

FrameCache::FrameCache() = default;

FrameCache::~FrameCache() { Clear(); }

void FrameCache::SetLimits(std::size_t max_bytes, std::size_t max_entries) {
  Clear();
  max_bytes_ = max_bytes;
  max_entries_ = max_entries;
}

void FrameCache::Clear() {
  for (Entry& entry : entries_) {
    FreeEntry(&entry);
  }
  entries_.clear();
  by_frame_.clear();
  bytes_ = 0;
  hits_ = 0;
  misses_ = 0;
  puts_ = 0;
  evictions_ = 0;
}

void FrameCache::FreeEntry(Entry* entry) {
  if (entry != nullptr && entry->frame != nullptr) {
    av_frame_free(&entry->frame);
  }
}

bool FrameCache::Put(const AVFrame* frame, std::int64_t frame_index, std::int64_t pts,
                     std::int64_t pts_us, std::int64_t duration_us, bool keyframe) {
  if (frame == nullptr || max_bytes_ == 0 || max_entries_ == 0) {
    return false;
  }

  const bool hardware =
      frame->hw_frames_ctx != nullptr || frame->format == AV_PIX_FMT_D3D11;
  const bool put_log = std::getenv("LAPIAN_PUT_LOG") != nullptr;
  const auto put_started = std::chrono::steady_clock::now();
  double transfer_ms = 0.0;
  AVFrame* copy = nullptr;
  if (hardware) {
    // 不长期持有 D3D11VA 解码表面：回拷到系统内存，缓存与驱动池解耦。
    copy = av_frame_alloc();
    if (copy == nullptr) {
      return false;
    }
    const auto transfer_started = std::chrono::steady_clock::now();
    if (av_hwframe_transfer_data(copy, frame, 0) < 0) {
      av_frame_free(&copy);
      return false;
    }
    transfer_ms = std::chrono::duration<double, std::milli>(
                      std::chrono::steady_clock::now() - transfer_started)
                      .count();
  } else {
    // 解码器输出是引用计数帧；av_frame_clone 只增加底层 AVBuffer 引用。
    copy = av_frame_clone(frame);
    if (copy == nullptr) {
      return false;
    }
  }

  const int bit_depth = BitDepthOfFormat(copy->format);
  int image_bytes = av_image_get_buffer_size(
      static_cast<AVPixelFormat>(copy->format), copy->width, copy->height, 32);
  std::size_t bytes = image_bytes > 0 ? static_cast<std::size_t>(image_bytes)
                                      : FrameBytesFor(copy->width, copy->height, bit_depth);
  if (bytes == 0 || bytes > max_bytes_) {
    av_frame_free(&copy);
    return false;
  }

  // 同一帧重复写入时替换旧条目，避免同一帧占两份预算。
  auto existing = by_frame_.find(frame_index);
  if (existing != by_frame_.end()) {
    Entry* entry = &(*existing->second);
    bytes_ -= entry->bytes;
    FreeEntry(entry);
    entries_.erase(existing->second);
    by_frame_.erase(existing);
  }

  Entry entry;
  entry.frame = copy;
  entry.frame_index = frame_index;
  entry.pts = pts;
  entry.pts_us = pts_us;
  entry.duration_us = duration_us;
  entry.keyframe = keyframe;
  entry.bytes = bytes;
  entries_.push_back(std::move(entry));
  by_frame_[frame_index] = std::prev(entries_.end());
  bytes_ += bytes;
  ++puts_;
  const std::uint64_t evictions_before = evictions_;
  EvictUntilWithinLimits();
  if (put_log) {
    const double total_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - put_started)
                                .count();
    std::fprintf(stderr,
                 "T7.4 put_log frame=%lld hw=%d bytes=%zu transfer_ms=%.2f "
                 "total_ms=%.2f evicted=%llu entries=%zu\n",
                 static_cast<long long>(frame_index), hardware ? 1 : 0, bytes,
                 transfer_ms, total_ms,
                 static_cast<unsigned long long>(evictions_ - evictions_before),
                 entries_.size());
    std::fflush(stderr);
  }
  // 新条目本身过大时会被立即淘汰；此时 Put 视为未缓存。
  return by_frame_.find(frame_index) != by_frame_.end();
}

bool FrameCache::Get(std::int64_t frame_index, AVFrame** out,
                     FrameCacheEntryInfo* info) {
  if (out == nullptr) {
    return false;
  }
  *out = nullptr;
  auto it = by_frame_.find(frame_index);
  if (it == by_frame_.end()) {
    ++misses_;
    return false;
  }
  // 命中即移到 MRU 尾部。
  entries_.splice(entries_.end(), entries_, it->second);
  AVFrame* clone = av_frame_clone(it->second->frame);
  if (clone == nullptr) {
    ++misses_;
    return false;
  }
  *out = clone;
  if (info != nullptr) {
    info->pts = it->second->pts;
    info->pts_us = it->second->pts_us;
    info->duration_us = it->second->duration_us;
    info->keyframe = it->second->keyframe;
    info->bytes = it->second->bytes;
  }
  ++hits_;
  return true;
}

bool FrameCache::Contains(std::int64_t frame_index) const {
  return by_frame_.find(frame_index) != by_frame_.end();
}

FrameCacheStats FrameCache::stats() const {
  FrameCacheStats result;
  result.entries = entries_.size();
  result.bytes = bytes_;
  result.max_bytes = max_bytes_;
  result.max_entries = max_entries_;
  result.hits = hits_;
  result.misses = misses_;
  result.puts = puts_;
  result.evictions = evictions_;
  return result;
}

void FrameCache::EvictUntilWithinLimits() {
  while (!entries_.empty() &&
         (entries_.size() > max_entries_ || bytes_ > max_bytes_)) {
    Entry& oldest = entries_.front();
    bytes_ -= oldest.bytes;
    by_frame_.erase(oldest.frame_index);
    FreeEntry(&oldest);
    entries_.pop_front();
    ++evictions_;
  }
}

FrameCacheLimits FrameCache::LimitsForFrame(int width, int height, int bit_depth) {
  FrameCacheLimits limits;
  if (width <= 0 || height <= 0) {
    return limits;
  }
  const std::int64_t pixels = static_cast<std::int64_t>(width) * height;
  if (width >= 3840 || height >= 2160 || pixels >= 3840LL * 2160) {
    // T7.4：4K 后退窗口从 8 帧扩到 20 帧。4K 每帧 NV12 约 11.9MiB，
    // 一次缓存回拷约 11ms/帧；8 帧窗口在 2s GOP 下每 8 步就重新从关键帧解码
    // （p95 253ms），20 帧窗口把 40 步扫描的未命中压到 2 次。
    // 仍只缓存后退步进的软件帧，播放/普通跳转不写缓存，不影响 P11。
    limits.max_bytes = 256 * kMiB;
    limits.max_entries = 20;
  } else if (width >= 1920 || height >= 1080 || pixels >= 1920LL * 1080) {
    limits.max_bytes = 64 * kMiB;
    limits.max_entries = 24;
  } else {
    limits.max_bytes = 32 * kMiB;
    limits.max_entries = 24;
  }
  // 单帧就超过预算时（例如 8K/10-bit）至少保留 1 帧，避免缓存永远为空；
  // 但调用方仍会因 bytes > max_bytes 拒绝。这里只保证预算至少容纳一帧。
  const std::size_t frame_bytes = FrameBytesFor(width, height, bit_depth);
  if (frame_bytes > 0 && limits.max_bytes < frame_bytes) {
    limits.max_bytes = frame_bytes;
    limits.max_entries = 1;
  }
  return limits;
}

std::size_t FrameCache::FrameBytesFor(int width, int height, int bit_depth) {
  if (width <= 0 || height <= 0) {
    return 0;
  }
  const AVPixelFormat format =
      bit_depth > 8 ? AV_PIX_FMT_P010 : AV_PIX_FMT_NV12;
  const int bytes = av_image_get_buffer_size(format, width, height, 32);
  if (bytes > 0) {
    return static_cast<std::size_t>(bytes);
  }
  const std::size_t pixels = static_cast<std::size_t>(width) * height;
  return bit_depth > 8 ? pixels * 3 : pixels * 3 / 2;
}

}  // namespace lapian::media
