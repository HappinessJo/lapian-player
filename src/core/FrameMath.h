#pragma once

#include <cstdint>
#include <string>

namespace lapian::core {

// CFR 帧率。用有理数保存，避免 23.976/29.97 的浮点漂移。
class FrameRate {
 public:
  FrameRate() = default;
  FrameRate(int num, int den);

  bool valid() const;
  int num() const { return num_; }
  int den() const { return den_; }
  double ToDouble() const;
  int NominalFps() const;  // NDF 时间码用：23.976→24、29.97→30、59.94→60
  bool IsNtsc() const;
  std::string ToString() const;  // "24" 或 "24000/1001"

  // 一帧的时长（微秒，四舍五入）。
  std::int64_t FrameDurationUs() const;

  // 第 N 帧的 PTS（AV_TIME_BASE 微秒）。N 必须非负；无效帧率返回 start_time_us。
  std::int64_t PtsOfFrame(std::int64_t frame, std::int64_t start_time_us = 0) const;

  // PTS 对应的最近帧号；可能为负（PTS 早于 start_time）。
  std::int64_t FrameOfPts(std::int64_t pts_us, std::int64_t start_time_us = 0) const;

  // 由时长估算总帧数（四舍五入）；无效帧率/时长返回 0。
  std::int64_t EstimateFrameCount(std::int64_t duration_us) const;

 private:
  int num_ = 0;
  int den_ = 0;
};

enum class TotalFramesState {
  kUnknown,
  kScanning,
  kEstimated,
  kExact,
};

// 总帧数三态；UI 分别显示 "—"、"扫描中"、"≈N"、"N"。
class TotalFrameCount {
 public:
  TotalFrameCount() = default;

  static TotalFrameCount Unknown();
  static TotalFrameCount Scanning();
  static TotalFrameCount Estimated(std::int64_t count);
  static TotalFrameCount Exact(std::int64_t count);

  // 容器 nb_frames 可信则精确；否则用时长×帧率估算。
  static TotalFrameCount FromContainer(std::int64_t nb_frames, std::int64_t duration_us,
                                       const FrameRate& rate);

  // 末帧 PTS 已知时的校正入口；实际探针由 T2.3/T2.6 调用。
  static TotalFrameCount FromLastFramePts(std::int64_t last_pts_us, std::int64_t start_time_us,
                                         const FrameRate& rate);

  TotalFramesState state() const { return state_; }
  bool has_value() const { return value_ >= 0; }
  std::int64_t value() const { return value_; }  // 无值返回 -1
  std::int64_t LastFrame() const;                // 无值返回 -1
  bool IsExact() const { return state_ == TotalFramesState::kExact; }
  bool IsEstimated() const { return state_ == TotalFramesState::kEstimated; }
  std::string Display() const;

 private:
  TotalFramesState state_ = TotalFramesState::kUnknown;
  std::int64_t value_ = -1;
};

struct Timecode {
  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  int frames = 0;

  std::string ToString() const;
};

// CFR 的 NDF 时间码格式化/解析；VFR 由 T2.2 用 PTS 处理。
class TimecodeFormatter {
 public:
  explicit TimecodeFormatter(FrameRate rate);

  bool valid() const { return rate_.valid(); }
  Timecode FromFrame(std::int64_t frame) const;
  bool Parse(const std::string& text, std::int64_t* frame) const;

 private:
  FrameRate rate_;
};

// 把帧号 clamp 到 [0, last_frame]；last_frame < 0 时返回 0。
std::int64_t ClampFrame(std::int64_t frame, std::int64_t last_frame);

}  // namespace lapian::core
