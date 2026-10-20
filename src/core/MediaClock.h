#pragma once

#include <array>
#include <functional>

namespace lapian::core {

// spec F4 的固定速度档位。
inline constexpr std::array<double, 7> kPlaybackRates{
    0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0};

// 最近档位；无效输入返回 1.0 的下标。
int PlaybackRateIndex(double rate);
// 在档位表中移动 delta 格并 clamp；无效输入先落到 1.0。
double StepPlaybackRate(double rate, int delta);

// 媒体时钟：把“墙上时间”换算成媒体时间。T3.2 用作 QPC 回退，
// T3.3 的变速也复用同一套 rate 语义。时间基准可注入，便于单测。
class MediaClock {
 public:
  using NowFunction = std::function<double()>;

  MediaClock();
  explicit MediaClock(NowFunction now_seconds);

  // rate 是媒体时间 / 墙上时间的倍率（1.0 为正常速度）。
  void Start(double media_seconds, double rate = 1.0);
  void Pause();
  void Seek(double media_seconds);
  void SetRate(double rate);

  bool IsRunning() const { return running_; }
  double NowSeconds() const;
  double rate() const { return rate_; }

 private:
  double WallNow() const;

  NowFunction now_;
  double anchor_media_ = 0.0;
  double anchor_wall_ = 0.0;
  double rate_ = 1.0;
  bool running_ = false;
};

}  // namespace lapian::core
