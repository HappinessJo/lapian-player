#include "core/MediaClock.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace lapian::core {
namespace {

double SteadyNowSeconds() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration<double>(now).count();
}

double SanitizeRate(double rate) {
  if (!std::isfinite(rate) || rate <= 0.0) {
    return 1.0;
  }
  return std::clamp(rate, 0.25, 2.0);
}

}  // namespace

int PlaybackRateIndex(double rate) {
  if (!std::isfinite(rate) || rate <= 0.0) {
    return 3;  // 1.0
  }
  int best = 0;
  double best_distance = std::abs(kPlaybackRates[0] - rate);
  for (int i = 1; i < static_cast<int>(kPlaybackRates.size()); ++i) {
    const double distance = std::abs(kPlaybackRates[i] - rate);
    if (distance < best_distance) {
      best = i;
      best_distance = distance;
    }
  }
  return best;
}

double StepPlaybackRate(double rate, int delta) {
  const int index = PlaybackRateIndex(rate);
  const int next = std::clamp(index + delta, 0,
                              static_cast<int>(kPlaybackRates.size()) - 1);
  return kPlaybackRates[static_cast<std::size_t>(next)];
}

MediaClock::MediaClock() : now_(&SteadyNowSeconds) {}

MediaClock::MediaClock(NowFunction now_seconds)
    : now_(std::move(now_seconds)) {
  if (!now_) {
    now_ = &SteadyNowSeconds;
  }
}

double MediaClock::WallNow() const { return now_(); }

void MediaClock::Start(double media_seconds, double rate) {
  rate_ = SanitizeRate(rate);
  anchor_media_ = std::max(0.0, media_seconds);
  anchor_wall_ = WallNow();
  running_ = true;
}

void MediaClock::Pause() {
  if (!running_) {
    return;
  }
  anchor_media_ = NowSeconds();
  anchor_wall_ = WallNow();
  running_ = false;
}

void MediaClock::Seek(double media_seconds) {
  anchor_media_ = std::max(0.0, media_seconds);
  anchor_wall_ = WallNow();
}

void MediaClock::SetRate(double rate) {
  if (running_) {
    anchor_media_ = NowSeconds();
    anchor_wall_ = WallNow();
  }
  rate_ = SanitizeRate(rate);
}

double MediaClock::NowSeconds() const {
  if (!running_) {
    return anchor_media_;
  }
  const double elapsed = WallNow() - anchor_wall_;
  return std::max(0.0, anchor_media_ + elapsed * rate_);
}

}  // namespace lapian::core
