#include "audio/WaveformData.h"

#include <algorithm>

namespace lapian::audio {

void WaveformData::Reset(double offset_seconds) {
  std::lock_guard<std::mutex> lock(mutex_);
  buckets_.clear();
  offset_seconds_ = offset_seconds;
  duration_seconds_ = 0.0;
}

void WaveformData::SetDuration(double duration_seconds) {
  std::lock_guard<std::mutex> lock(mutex_);
  duration_seconds_ = std::max(0.0, duration_seconds);
}

void WaveformData::SetBucket(std::size_t index, std::int16_t min_value,
                             std::int16_t max_value) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= buckets_.size()) {
    buckets_.resize(index + 1);
  }
  WaveformBucket& bucket = buckets_[index];
  bucket.min = min_value;
  bucket.max = max_value;
}

std::size_t WaveformData::bucket_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return buckets_.size();
}

WaveformBucket WaveformData::BucketAt(std::size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return index < buckets_.size() ? buckets_[index] : WaveformBucket{};
}

WaveformBucket WaveformData::MinMaxRange(std::size_t begin,
                                         std::size_t end) const {
  std::lock_guard<std::mutex> lock(mutex_);
  begin = std::min(begin, buckets_.size());
  end = std::min(end, buckets_.size());
  WaveformBucket result;
  bool any = false;
  for (std::size_t i = begin; i < end; ++i) {
    if (!any) {
      result = buckets_[i];
      any = true;
    } else {
      result.min = std::min(result.min, buckets_[i].min);
      result.max = std::max(result.max, buckets_[i].max);
    }
  }
  return result;
}

void WaveformData::CopyRange(std::size_t begin, std::size_t end,
                             std::vector<WaveformBucket>* out) const {
  if (out == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  begin = std::min(begin, buckets_.size());
  end = std::min(end, buckets_.size());
  if (end <= begin) {
    out->clear();
    return;
  }
  out->assign(buckets_.begin() + static_cast<std::ptrdiff_t>(begin),
              buckets_.begin() + static_cast<std::ptrdiff_t>(end));
}

double WaveformData::offset_seconds() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return offset_seconds_;
}

double WaveformData::duration_seconds() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return duration_seconds_;
}

}  // namespace lapian::audio
