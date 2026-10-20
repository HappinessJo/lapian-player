// T3.5 波形生成探针：控制台运行后台波形，打印耗时/桶数/峰值。
// 不渲染、不入分发；用于 P13 与 docs/perf 记录。

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <windows.h>

#include "audio/WaveformGenerator.h"

int main(int argc, char* argv[]) {
  SetConsoleOutputCP(CP_UTF8);
  if (argc < 2) {
    std::printf("用法: lapian_waveform_probe <file> [stream_index]\n");
    return 2;
  }
  const int stream_index = argc >= 3 ? std::atoi(argv[2]) : -1;

  lapian::audio::WaveformGenerator generator;
  const auto started = std::chrono::steady_clock::now();
  if (!generator.Start(argv[1], stream_index, 0.0)) {
    std::printf("启动波形线程失败\n");
    return 1;
  }

  lapian::audio::WaveformStatus status = generator.status();
  while (status.state == lapian::audio::WaveformState::kRunning ||
         status.state == lapian::audio::WaveformState::kIdle) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    status = generator.status();
  }

  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  const auto data = generator.data();
  std::size_t bucket_count = 0;
  int peak = 0;
  std::size_t peak_bucket = 0;
  if (data != nullptr) {
    bucket_count = data->bucket_count();
    for (std::size_t i = 0; i < bucket_count; ++i) {
      const auto bucket = data->BucketAt(i);
      const int magnitude =
          std::max(std::abs(static_cast<int>(bucket.min)),
                   std::abs(static_cast<int>(bucket.max)));
      if (magnitude > peak) {
        peak = magnitude;
        peak_bucket = i;
      }
    }
  }

  std::printf("state=%d elapsed_ms=%.1f buckets=%zu peak=%d peak_bucket=%zu\n",
              static_cast<int>(status.state), elapsed * 1000.0, bucket_count, peak,
              peak_bucket);
  return status.state == lapian::audio::WaveformState::kReady ? 0 : 1;
}
