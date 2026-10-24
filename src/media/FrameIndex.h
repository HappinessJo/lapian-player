#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "core/FrameMath.h"

namespace lapian::media {

// 一个显示帧的 PTS（微秒，AV_TIME_BASE）与关键帧标志。
struct FrameIndexEntry {
  std::int64_t pts_us = 0;
  bool keyframe = false;
};

// T2.2 VFR/PTS 索引：只 demux、不解码；按 PTS 升序、重复 PTS 只留一帧。
// 索引不可靠时 T2.3 必须降级为解码计数，不能拿近似帧冒充精确帧。
class FrameIndex {
 public:
  // T7.1：cancel 非空时每读一个包检查一次；取消返回 false（索引不安装）。
  bool BuildFromFile(const std::string& utf8_path, int video_stream_index,
                     std::string* error, const std::atomic<bool>* cancel = nullptr);
  bool BuildFromEntries(std::vector<FrameIndexEntry> entries, std::string* error);
  void Reset();

  bool IsReady() const { return ready_; }
  bool IsReliable() const { return reliable_; }
  std::size_t size() const { return entries_.size(); }
  bool empty() const { return entries_.empty(); }

  std::int64_t PtsOfFrame(std::int64_t frame) const;
  // 最接近该 PTS 的帧号；空索引返回 -1。
  std::int64_t FrameOfPts(std::int64_t pts_us) const;
  bool FindExactPts(std::int64_t pts_us, std::int64_t* frame) const;
  // 目标 PTS 之前（含）最近的关键帧帧号；没有返回 -1。
  std::int64_t KeyframeAtOrBefore(std::int64_t pts_us) const;
  // DP-2：按帧号取关键帧，供缩略图滑动窗口/预取使用。
  std::int64_t KeyframeAtOrBeforeFrame(std::int64_t frame) const;
  std::int64_t PreviousKeyframe(std::int64_t frame) const;
  std::int64_t NextKeyframe(std::int64_t frame) const;
  const FrameIndexEntry* Entry(std::int64_t frame) const;

  lapian::core::TotalFrameCount Total() const;

  int duplicate_pts_count() const { return duplicate_pts_count_; }
  int missing_pts_count() const { return missing_pts_count_; }
  int multi_frame_packets() const { return multi_frame_packets_; }
  const std::string& warning() const { return warning_; }
  double build_ms() const { return build_ms_; }

  // T2.3 解码校验：偏差 > frame_duration_us 计一次；连续 3 次标记不可靠。
  bool ValidateDecodedPts(std::int64_t frame, std::int64_t decoded_pts_us,
                          std::int64_t frame_duration_us);

 private:
  void Normalize();
  void RebuildKeyframes();

  std::vector<FrameIndexEntry> entries_;
  std::vector<std::int64_t> keyframe_frames_;
  bool ready_ = false;
  bool reliable_ = false;
  int duplicate_pts_count_ = 0;
  int missing_pts_count_ = 0;
  int multi_frame_packets_ = 0;
  int consecutive_validation_failures_ = 0;
  double build_ms_ = 0.0;
  std::string warning_;
};

}  // namespace lapian::media
