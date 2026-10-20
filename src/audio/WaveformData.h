#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace lapian::audio {

// 波形峰值包络：每个 bucket 保存该时间窗的 min/max（int16 量化）。
// bucket 时间轴是“音频时间轴”；显示时用 offset_seconds 映射到帧格。
struct WaveformBucket {
  std::int16_t min = 0;
  std::int16_t max = 0;
};

class WaveformData {
 public:
  static constexpr int kBucketsPerSecond = 200;
  // T7.2：硬上限 4 小时（约 11.5MB @ 4 字节/桶），远高于 V10 的 2 小时；
  // 超过部分不再分配，避免异常长片把波形内存拖成无界。
  static constexpr std::size_t kMaxBuckets =
      static_cast<std::size_t>(4 * 60 * 60 * kBucketsPerSecond);

  void Reset(double offset_seconds);
  void SetDuration(double duration_seconds);
  void SetBucket(std::size_t index, std::int16_t min_value, std::int16_t max_value);

  std::size_t bucket_count() const;
  std::size_t byte_size() const;
  WaveformBucket BucketAt(std::size_t index) const;
  WaveformBucket MinMaxRange(std::size_t begin, std::size_t end) const;
  void CopyRange(std::size_t begin, std::size_t end,
                 std::vector<WaveformBucket>* out) const;

  double offset_seconds() const;
  double duration_seconds() const;

 private:
  mutable std::mutex mutex_;
  std::vector<WaveformBucket> buckets_;
  double offset_seconds_ = 0.0;
  double duration_seconds_ = 0.0;
};

}  // namespace lapian::audio
