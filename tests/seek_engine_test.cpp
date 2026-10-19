// T2.3 精确 seek 测试：关键帧 backward + 解码到目标帧；索引不可靠时降级计数。

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/log.h>
}

#include "media/FrameIndex.h"
#include "media/SeekEngine.h"
#include "platform/gfx/D3D11Device.h"

namespace {

int g_failures = 0;
bool g_skipped = false;

void Check(bool condition, const std::string& what) {
  if (condition) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n", what.c_str());
    ++g_failures;
  }
}

void Skip(const std::string& what) {
  std::printf("[SKIP] %s\n", what.c_str());
  g_skipped = true;
}

bool FileExists(const std::string& path) {
  const std::u8string utf8(reinterpret_cast<const char8_t*>(path.data()), path.size());
  return std::filesystem::exists(std::filesystem::path(utf8));
}

bool CheckFrame(lapian::media::SeekEngine& engine, const lapian::media::FrameIndex& index,
                std::int64_t frame, const std::string& label) {
  lapian::media::DecodedFrameInfo info;
  std::string error;
  const auto started = std::chrono::steady_clock::now();
  const bool ok = engine.SeekToFrame(frame, index, &info, &error);
  const double elapsed =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
          .count();
  if (!ok) {
    Check(false, label + " seek 帧 " + std::to_string(frame) + ": " + error);
    return false;
  }
  const bool exact = info.pts_us == index.PtsOfFrame(frame);
  Check(exact, label + " seek 帧 " + std::to_string(frame) + " PTS 精确 (" +
                   std::to_string(elapsed) + "ms)");
  if (!exact) {
    std::printf("       got=%lld expected=%lld\n", static_cast<long long>(info.pts_us),
                static_cast<long long>(index.PtsOfFrame(frame)));
  }
  return exact;
}

void TestFile(lapian::gfx::D3D11Device& device, const std::string& path,
              const std::vector<std::int64_t>& frames, const std::string& label) {
  if (!FileExists(path)) {
    Skip(path + " 不存在");
    return;
  }
  lapian::media::FrameIndex index;
  std::string error;
  if (!index.BuildFromFile(path, -1, &error)) {
    Check(false, label + " 建索引: " + error);
    return;
  }
  Check(index.IsReliable(), label + " 索引可靠");

  lapian::media::SeekEngine engine;
  if (!engine.Open(path, -1, device.device(), true, &error)) {
    Check(false, label + " 打开 seek 解码器: " + error);
    return;
  }
  for (const std::int64_t frame : frames) {
    CheckFrame(engine, index, frame, label);
  }

  // 20 个随机帧，误差必须为 0。
  std::mt19937 rng(12345);
  std::uniform_int_distribution<std::int64_t> distribution(
      0, static_cast<std::int64_t>(index.size()) - 1);
  int exact = 0;
  double max_ms = 0.0;
  for (int i = 0; i < 20; ++i) {
    const std::int64_t frame = distribution(rng);
    lapian::media::DecodedFrameInfo info;
    const auto started = std::chrono::steady_clock::now();
    if (engine.SeekToFrame(frame, index, &info, &error) &&
        info.pts_us == index.PtsOfFrame(frame)) {
      ++exact;
    }
    const double elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    max_ms = std::max(max_ms, elapsed);
  }
  Check(exact == 20, label + " 20 随机帧误差 0");
  std::printf("       %s 20 随机帧 max=%.2fms\n", label.c_str(), max_ms);
}

void TestFallback(lapian::gfx::D3D11Device& device) {
  const std::string path = "media/V9.mp4";
  if (!FileExists(path)) {
    Skip("V9 不存在，跳过低可靠降级");
    return;
  }
  lapian::media::FrameIndex index;
  std::string error;
  if (!index.BuildFromFile(path, -1, &error)) {
    Check(false, "fallback 建索引: " + error);
    return;
  }
  index.ValidateDecodedPts(0, 999999, 1000);
  index.ValidateDecodedPts(0, 999999, 1000);
  index.ValidateDecodedPts(0, 999999, 1000);
  Check(!index.IsReliable(), "连续偏差后索引不可靠");

  lapian::media::SeekEngine engine;
  if (!engine.Open(path, -1, device.device(), true, &error)) {
    Check(false, "fallback 打开解码器: " + error);
    return;
  }
  CheckFrame(engine, index, 24, "降级计数");
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  av_log_set_level(AV_LOG_ERROR);
  std::printf("T2.3 精确 seek 测试\n");

  lapian::gfx::D3D11Device device;
  std::string error;
  if (!device.Create(&error)) {
    Skip("D3D11 设备创建失败: " + error);
    return 77;
  }

  TestFile(device, "media/V9.mp4", {0, 1, 24, 59, 100, 119}, "V9");
  TestFile(device, "media/proxy/proxy-V1.mp4", {0, 1, 24, 100, 359, 719}, "proxy-V1");
  TestFile(device, "media/测试.mp4", {0, 1, 100, 1000, 3000, 4572}, "real-1080p30");
  TestFallback(device);

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  if (g_skipped) {
    std::printf("部分素材跳过\n");
    return 77;
  }
  std::printf("全部通过\n");
  return 0;
}
