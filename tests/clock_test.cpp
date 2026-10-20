// T3.2 媒体时钟单测：注入墙上时间，验证 start/pause/seek/rate 的锚点语义。
// 纯 C++，无 Qt/FFmpeg 依赖。

#include <cmath>
#include <cstdio>
#include <string>

#include "core/MediaClock.h"

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
  if (condition) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n", what.c_str());
    ++g_failures;
  }
}

void CheckNear(double actual, double expected, double epsilon, const std::string& what) {
  const bool ok = std::fabs(actual - expected) <= epsilon;
  if (ok) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s (actual=%.9f expected=%.9f)\n", what.c_str(), actual, expected);
    ++g_failures;
  }
}

}  // namespace

int main() {
  std::printf("T3.2 媒体时钟测试\n");

  double fake_now = 0.0;
  lapian::core::MediaClock clock([&fake_now]() { return fake_now; });

  Check(!clock.IsRunning(), "初始未运行");
  CheckNear(clock.NowSeconds(), 0.0, 1e-9, "初始媒体时间为 0");

  clock.Start(10.0, 1.0);
  Check(clock.IsRunning(), "Start 后运行");
  CheckNear(clock.NowSeconds(), 10.0, 1e-9, "Start 锚定媒体时间");

  fake_now = 1.0;
  CheckNear(clock.NowSeconds(), 11.0, 1e-9, "1x 经过 1s 到 11s");

  clock.SetRate(2.0);
  CheckNear(clock.rate(), 2.0, 1e-9, "速率切到 2x");
  CheckNear(clock.NowSeconds(), 11.0, 1e-9, "切换速率不跳变");
  fake_now = 2.0;
  CheckNear(clock.NowSeconds(), 13.0, 1e-9, "2x 经过 1s 前进 2s");

  clock.Pause();
  Check(!clock.IsRunning(), "Pause 后停止");
  fake_now = 5.0;
  CheckNear(clock.NowSeconds(), 13.0, 1e-9, "暂停期间时间冻结");

  clock.Seek(100.0);
  CheckNear(clock.NowSeconds(), 100.0, 1e-9, "暂停时 Seek 直接到目标");
  fake_now = 6.0;
  CheckNear(clock.NowSeconds(), 100.0, 1e-9, "暂停时 Seek 后仍冻结");

  clock.Start(100.0, 1.0);
  fake_now = 7.0;
  CheckNear(clock.NowSeconds(), 101.0, 1e-9, "从 100s 重新起播");

  clock.SetRate(0.5);
  fake_now = 9.0;
  CheckNear(clock.NowSeconds(), 102.0, 1e-9, "0.5x 经过 2s 前进 1s");

  clock.SetRate(0.0);
  CheckNear(clock.rate(), 1.0, 1e-9, "非法速率回退 1x");
  clock.SetRate(9.0);
  CheckNear(clock.rate(), 2.0, 1e-9, "速率上限 2x");

  clock.Seek(-5.0);
  CheckNear(clock.NowSeconds(), 0.0, 1e-9, "负 Seek clamp 到 0");

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("全部通过\n");
  return 0;
}
