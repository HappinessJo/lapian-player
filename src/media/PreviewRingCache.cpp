#include "media/PreviewRingCache.h"

#include <algorithm>
#include <utility>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace lapian::media {
namespace {

std::size_t SoftwareFrameBytes(const AVFrame* frame) {
  if (frame == nullptr || frame->width <= 0 || frame->height <= 0) {
    return 0;
  }
  AVPixelFormat format = static_cast<AVPixelFormat>(frame->format);
  if (format == AV_PIX_FMT_NONE) {
    format = AV_PIX_FMT_NV12;
  }
  const int size = av_image_get_buffer_size(format, frame->width, frame->height, 1);
  if (size > 0) {
    return static_cast<std::size_t>(size);
  }
  return static_cast<std::size_t>(frame->width) *
         static_cast<std::size_t>(frame->height) * 3u / 2u;
}

}  // namespace

PreviewRingCache::PreviewRingCache(std::size_t budget_bytes)
    : budget_bytes_(budget_bytes) {}

PreviewRingCache::~PreviewRingCache() { Clear(); }

void PreviewRingCache::EraseLocked(std::list<Entry>::iterator it) {
  if (it->frame != nullptr) {
    av_frame_free(&it->frame);
  }
  bytes_ -= std::min(bytes_, it->bytes);
  index_.erase(it->frame_index);
  entries_.erase(it);
  ++evictions_;
}

void PreviewRingCache::MoveToFrontLocked(std::list<Entry>::iterator it) {
  if (it == entries_.begin()) {
    return;
  }
  entries_.splice(entries_.begin(), entries_, it);
}

void PreviewRingCache::EvictLocked(std::size_t budget_bytes) {
  while ((bytes_ > budget_bytes || entries_.size() > kMaxEntries) &&
         entries_.size() > kMinEntries) {
    EraseLocked(std::prev(entries_.end()));
  }
}

void PreviewRingCache::Put(std::int64_t frame_index, std::int64_t pts_us,
                           AVFrame* frame) {
  if (frame == nullptr) {
    return;
  }
  const auto existing = index_.find(frame_index);
  if (existing != index_.end()) {
    EraseLocked(existing->second);
  }
  const std::size_t bytes = SoftwareFrameBytes(frame);
  entries_.push_front(Entry{frame_index, pts_us, frame, bytes, ++tick_});
  index_[frame_index] = entries_.begin();
  bytes_ += bytes;
  peak_bytes_ = std::max(peak_bytes_, bytes_);
  ++puts_;
  EvictLocked(budget_bytes_);
}

AVFrame* PreviewRingCache::Clone(std::int64_t frame_index) {
  const auto it = index_.find(frame_index);
  if (it == index_.end() || it->second->frame == nullptr) {
    return nullptr;
  }
  MoveToFrontLocked(it->second);
  it->second->tick = ++tick_;
  ++hits_;
  return av_frame_clone(it->second->frame);
}

bool PreviewRingCache::Contains(std::int64_t frame_index) const {
  const auto it = index_.find(frame_index);
  return it != index_.end() && it->second->frame != nullptr;
}

void PreviewRingCache::TrimToWindow(std::int64_t center, std::int64_t radius) {
  if (radius < 0) {
    return;
  }
  for (auto it = entries_.begin(); it != entries_.end();) {
    const std::int64_t distance =
        it->frame_index >= center ? it->frame_index - center : center - it->frame_index;
    if (distance > radius) {
      auto remove = it++;
      EraseLocked(remove);
    } else {
      ++it;
    }
  }
}

void PreviewRingCache::TrimToBudget(std::size_t budget_bytes) {
  if (budget_bytes < budget_bytes_) {
    budget_bytes_ = budget_bytes;
  }
  EvictLocked(budget_bytes_);
}

void PreviewRingCache::Clear() {
  for (Entry& entry : entries_) {
    if (entry.frame != nullptr) {
      av_frame_free(&entry.frame);
    }
  }
  entries_.clear();
  index_.clear();
  bytes_ = 0;
}

PreviewRingCache::Stats PreviewRingCache::stats() const {
  Stats result;
  result.entries = entries_.size();
  result.bytes = bytes_;
  result.peak_bytes = peak_bytes_;
  result.puts = puts_;
  result.hits = hits_;
  result.evictions = evictions_;
  return result;
}

PreviewThumbnailCache::PreviewThumbnailCache(std::size_t budget_bytes)
    : budget_bytes_(budget_bytes) {}

PreviewThumbnailCache::~PreviewThumbnailCache() { Clear(); }

void PreviewThumbnailCache::EvictLocked(std::size_t budget_bytes) {
  while ((bytes_ > budget_bytes || entries_.size() > kMaxEntries) &&
         !entries_.empty()) {
    auto oldest = entries_.begin();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->second.tick < oldest->second.tick) {
        oldest = it;
      }
    }
    if (oldest->second.frame != nullptr) {
      av_frame_free(&oldest->second.frame);
    }
    bytes_ -= std::min(bytes_, oldest->second.bytes);
    entries_.erase(oldest);
    ++evictions_;
  }
}

void PreviewThumbnailCache::Put(std::int64_t keyframe_frame,
                                std::int64_t pts_us, AVFrame* frame) {
  if (frame == nullptr) {
    return;
  }
  const auto existing = entries_.find(keyframe_frame);
  if (existing != entries_.end()) {
    if (existing->second.frame != nullptr) {
      av_frame_free(&existing->second.frame);
    }
    bytes_ -= std::min(bytes_, existing->second.bytes);
    entries_.erase(existing);
  }
  const std::size_t bytes = SoftwareFrameBytes(frame);
  Entry entry;
  entry.pts_us = pts_us;
  entry.frame = frame;
  entry.bytes = bytes;
  entry.tick = ++tick_;
  entries_.emplace(keyframe_frame, entry);
  bytes_ += bytes;
  peak_bytes_ = std::max(peak_bytes_, bytes_);
  ++puts_;
  EvictLocked(budget_bytes_);
}

AVFrame* PreviewThumbnailCache::Clone(std::int64_t keyframe_frame) {
  const auto it = entries_.find(keyframe_frame);
  if (it == entries_.end() || it->second.frame == nullptr) {
    return nullptr;
  }
  it->second.tick = ++tick_;
  ++hits_;
  return av_frame_clone(it->second.frame);
}

AVFrame* PreviewThumbnailCache::CloneNearestBelow(std::int64_t target_frame,
                                                  std::int64_t* keyframe_frame) {
  if (entries_.empty()) {
    return nullptr;
  }
  auto it = entries_.upper_bound(target_frame);
  if (it == entries_.begin()) {
    return nullptr;
  }
  --it;
  if (it->second.frame == nullptr) {
    return nullptr;
  }
  it->second.tick = ++tick_;
  ++hits_;
  if (keyframe_frame != nullptr) {
    *keyframe_frame = it->first;
  }
  return av_frame_clone(it->second.frame);
}

bool PreviewThumbnailCache::Contains(std::int64_t keyframe_frame) const {
  const auto it = entries_.find(keyframe_frame);
  return it != entries_.end() && it->second.frame != nullptr;
}

void PreviewThumbnailCache::TrimToWindow(std::int64_t center_frame,
                                         std::int64_t radius_frames) {
  if (radius_frames < 0) {
    return;
  }
  for (auto it = entries_.begin(); it != entries_.end();) {
    const std::int64_t distance =
        it->first >= center_frame ? it->first - center_frame
                                  : center_frame - it->first;
    if (distance > radius_frames) {
      if (it->second.frame != nullptr) {
        av_frame_free(&it->second.frame);
      }
      bytes_ -= std::min(bytes_, it->second.bytes);
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
}

void PreviewThumbnailCache::TrimToBudget(std::size_t budget_bytes) {
  if (budget_bytes < budget_bytes_) {
    budget_bytes_ = budget_bytes;
  }
  EvictLocked(budget_bytes_);
}

void PreviewThumbnailCache::Clear() {
  for (auto& pair : entries_) {
    if (pair.second.frame != nullptr) {
      av_frame_free(&pair.second.frame);
    }
  }
  entries_.clear();
  bytes_ = 0;
}

PreviewThumbnailCache::Stats PreviewThumbnailCache::stats() const {
  Stats result;
  result.entries = entries_.size();
  result.bytes = bytes_;
  result.peak_bytes = peak_bytes_;
  result.puts = puts_;
  result.hits = hits_;
  result.evictions = evictions_;
  return result;
}

}  // namespace lapian::media
