// T3.1 音频链路开发探针：控制台播放 N 秒并打印状态/位置/underrun。
// 不渲染视频、不入分发；用于开发机手测与 docs/perf 记录。

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <windows.h>

#include "audio/AudioEngine.h"

namespace {

const char* StateName(lapian::audio::AudioState state) {
  using lapian::audio::AudioState;
  switch (state) {
    case AudioState::kClosed:
      return "closed";
    case AudioState::kOpening:
      return "opening";
    case AudioState::kPlaying:
      return "playing";
    case AudioState::kPaused:
      return "paused";
    case AudioState::kSilent:
      return "silent";
    case AudioState::kError:
      return "error";
  }
  return "unknown";
}

}  // namespace

int main(int argc, char* argv[]) {
  SetConsoleOutputCP(CP_UTF8);
  if (argc < 2) {
    std::printf("用法: lapian_audio_probe <file> [seconds] [--mute] [--volume N]\n");
    return 2;
  }

  double seconds = 3.0;
  double start_seconds = 0.0;
  double rate = 1.0;
  int volume = 100;
  bool mute = false;
  for (int i = 2; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--mute") {
      mute = true;
    } else if (argument == "--seek" && i + 1 < argc) {
      start_seconds = std::atof(argv[++i]);
    } else if (argument == "--rate" && i + 1 < argc) {
      rate = std::atof(argv[++i]);
    } else if (argument == "--volume" && i + 1 < argc) {
      volume = std::atoi(argv[++i]);
    } else {
      seconds = std::atof(argument.c_str());
    }
  }
  if (seconds < 0.0) {
    seconds = 0.0;
  }
  if (start_seconds < 0.0) {
    start_seconds = 0.0;
  }
  if (!std::isfinite(rate) || rate <= 0.0) {
    rate = 1.0;
  }
  rate = std::clamp(rate, 0.25, 2.0);

  lapian::audio::AudioEngine engine;
  engine.SetVolume(volume);
  engine.SetMute(mute);
  engine.SetRate(rate);
  if (!engine.Open(argv[1], -1)) {
    std::printf("启动音频线程失败\n");
    return 1;
  }

  const auto started = std::chrono::steady_clock::now();
  std::printf("rate=%.2fx\n", rate);
  engine.PlayFrom(start_seconds);
  while (std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() <
         seconds) {
    const lapian::audio::AudioStatus status = engine.status();
    double clock_seconds = 0.0;
    const bool has_clock = engine.ClockSeconds(&clock_seconds);
    std::printf("state=%s pos=%.3f clock=%.3f has_clock=%d underruns=%llu\n",
                StateName(status.state), status.position_seconds, clock_seconds,
                has_clock ? 1 : 0, static_cast<unsigned long long>(status.underruns));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  engine.Pause();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const lapian::audio::AudioStatus final_status = engine.status();
  std::printf("final state=%s pos=%.3f underruns=%llu message=%s\n",
              StateName(final_status.state), final_status.position_seconds,
              static_cast<unsigned long long>(final_status.underruns),
              final_status.message.c_str());
  engine.Close();
  return final_status.state == lapian::audio::AudioState::kError ? 1 : 0;
}
