#include "core/FrameMath.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <vector>

namespace lapian::core {
namespace {

// 仅覆盖首期片长/帧号范围（2h×60fps≈43 万帧），乘积在 int64 内。
// 负数只出现在“PTS 早于 start_time”的边界，按对称方式四舍五入。
std::int64_t MulDivRound(std::int64_t value, std::int64_t mul, std::int64_t div) {
  if (div == 0) {
    return 0;
  }
  const std::int64_t product = value * mul;
  if (product >= 0) {
    return (product + div / 2) / div;
  }
  return -((-product + div / 2) / div);
}

bool ParseNonNegativeInt(const std::string& text, int* value) {
  if (text.empty()) {
    return false;
  }
  int parsed = 0;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc() || result.ptr != end || parsed < 0) {
    return false;
  }
  *value = parsed;
  return true;
}

std::string TrimAscii(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
    --end;
  }
  return text.substr(begin, end - begin);
}

}  // namespace

FrameRate::FrameRate(int num, int den) {
  if (num <= 0 || den <= 0) {
    return;
  }
  const int divisor = std::gcd(num, den);
  num_ = num / divisor;
  den_ = den / divisor;
}

bool FrameRate::valid() const { return num_ > 0 && den_ > 0; }

double FrameRate::ToDouble() const {
  return valid() ? static_cast<double>(num_) / static_cast<double>(den_) : 0.0;
}

bool FrameRate::IsNtsc() const {
  if (!valid()) {
    return false;
  }
  if (den_ == 1001 && (num_ == 24000 || num_ == 30000 || num_ == 60000)) {
    return true;
  }
  const double fps = ToDouble();
  return std::fabs(fps - 23.976) < 0.01 || std::fabs(fps - 29.97) < 0.01 ||
         std::fabs(fps - 59.94) < 0.01;
}

int FrameRate::NominalFps() const {
  if (!valid()) {
    return 0;
  }
  return static_cast<int>(std::llround(ToDouble()));
}

std::string FrameRate::ToString() const {
  if (!valid()) {
    return "invalid";
  }
  if (den_ == 1) {
    return std::to_string(num_);
  }
  return std::to_string(num_) + "/" + std::to_string(den_);
}

std::int64_t FrameRate::FrameDurationUs() const {
  if (!valid()) {
    return 0;
  }
  return MulDivRound(1'000'000, den_, num_);
}

std::int64_t FrameRate::PtsOfFrame(std::int64_t frame, std::int64_t start_time_us) const {
  if (!valid() || frame < 0) {
    return start_time_us;
  }
  return start_time_us + MulDivRound(frame, 1'000'000LL * den_, num_);
}

std::int64_t FrameRate::FrameOfPts(std::int64_t pts_us, std::int64_t start_time_us) const {
  if (!valid()) {
    return 0;
  }
  return MulDivRound(pts_us - start_time_us, num_, 1'000'000LL * den_);
}

std::int64_t FrameRate::EstimateFrameCount(std::int64_t duration_us) const {
  if (!valid() || duration_us <= 0) {
    return 0;
  }
  return MulDivRound(duration_us, num_, 1'000'000LL * den_);
}

TotalFrameCount TotalFrameCount::Unknown() { return {}; }

TotalFrameCount TotalFrameCount::Scanning() {
  TotalFrameCount result;
  result.state_ = TotalFramesState::kScanning;
  return result;
}

TotalFrameCount TotalFrameCount::Estimated(std::int64_t count) {
  if (count <= 0) {
    return Unknown();
  }
  TotalFrameCount result;
  result.state_ = TotalFramesState::kEstimated;
  result.value_ = count;
  return result;
}

TotalFrameCount TotalFrameCount::Exact(std::int64_t count) {
  TotalFrameCount result;
  result.state_ = TotalFramesState::kExact;
  result.value_ = count >= 0 ? count : 0;
  return result;
}

TotalFrameCount TotalFrameCount::FromContainer(std::int64_t nb_frames, std::int64_t duration_us,
                                               const FrameRate& rate) {
  if (nb_frames > 0) {
    return Exact(nb_frames);
  }
  if (duration_us > 0 && rate.valid()) {
    const std::int64_t estimated = rate.EstimateFrameCount(duration_us);
    if (estimated > 0) {
      return Estimated(estimated);
    }
  }
  return Unknown();
}

TotalFrameCount TotalFrameCount::FromLastFramePts(std::int64_t last_pts_us,
                                                  std::int64_t start_time_us,
                                                  const FrameRate& rate) {
  if (!rate.valid() || last_pts_us < start_time_us) {
    return Unknown();
  }
  const std::int64_t last_frame = rate.FrameOfPts(last_pts_us, start_time_us);
  if (last_frame < 0) {
    return Unknown();
  }
  return Exact(last_frame + 1);
}

std::int64_t TotalFrameCount::LastFrame() const {
  if (!has_value() || value_ <= 0) {
    return -1;
  }
  return value_ - 1;
}

std::string TotalFrameCount::Display() const {
  switch (state_) {
    case TotalFramesState::kUnknown:
      return "—";
    case TotalFramesState::kScanning:
      return "扫描中";
    case TotalFramesState::kEstimated:
      return "≈" + std::to_string(value_);
    case TotalFramesState::kExact:
      return std::to_string(value_);
  }
  return "—";
}

std::string Timecode::ToString() const {
  char buffer[48] = {};
  std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d:%02d", hours, minutes, seconds, frames);
  return buffer;
}

TimecodeFormatter::TimecodeFormatter(FrameRate rate) : rate_(rate) {}

Timecode TimecodeFormatter::FromFrame(std::int64_t frame) const {
  const int nominal = rate_.NominalFps();
  if (!rate_.valid() || nominal <= 0 || frame < 0) {
    return {};
  }

  std::int64_t remainder = frame;
  Timecode result;
  result.frames = static_cast<int>(remainder % nominal);
  remainder /= nominal;
  result.seconds = static_cast<int>(remainder % 60);
  remainder /= 60;
  result.minutes = static_cast<int>(remainder % 60);
  remainder /= 60;
  if (remainder > std::numeric_limits<int>::max()) {
    return {};
  }
  result.hours = static_cast<int>(remainder);
  return result;
}

bool TimecodeFormatter::Parse(const std::string& text, std::int64_t* frame) const {
  if (frame == nullptr || !rate_.valid()) {
    return false;
  }
  const int nominal = rate_.NominalFps();
  if (nominal <= 0) {
    return false;
  }

  const std::string trimmed = TrimAscii(text);
  std::vector<std::string> parts;
  std::size_t begin = 0;
  while (begin <= trimmed.size()) {
    const std::size_t separator = trimmed.find(':', begin);
    if (separator == std::string::npos) {
      parts.push_back(trimmed.substr(begin));
      break;
    }
    parts.push_back(trimmed.substr(begin, separator - begin));
    begin = separator + 1;
  }
  if (parts.size() != 4) {
    return false;
  }

  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  int frames = 0;
  if (!ParseNonNegativeInt(parts[0], &hours) || !ParseNonNegativeInt(parts[1], &minutes) ||
      !ParseNonNegativeInt(parts[2], &seconds) || !ParseNonNegativeInt(parts[3], &frames)) {
    return false;
  }
  if (minutes >= 60 || seconds >= 60 || frames >= nominal || hours > 9999) {
    return false;
  }

  const std::int64_t total = ((static_cast<std::int64_t>(hours) * 60 + minutes) * 60 + seconds) *
                                 nominal +
                             frames;
  *frame = total;
  return true;
}

std::int64_t ClampFrame(std::int64_t frame, std::int64_t last_frame) {
  if (last_frame < 0 || frame < 0) {
    return 0;
  }
  if (frame > last_frame) {
    return last_frame;
  }
  return frame;
}

}  // namespace lapian::core
