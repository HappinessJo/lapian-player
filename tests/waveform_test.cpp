// T3.5 波形生成单测：WaveformData 边界 + V9 后台生成/峰值位置。
// 媒体素材缺失时返回 77（CTest SKIP_RETURN_CODE）。

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "audio/WaveformData.h"
#include "audio/WaveformGenerator.h"

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

void CheckNear(double actual, double expected, double epsilon, const std::string& what) {
  const bool ok = std::fabs(actual - expected) <= epsilon;
  if (ok) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s (actual=%.9f expected=%.9f)\n", what.c_str(), actual, expected);
    ++g_failures;
  }
}

void TestData() {
  using lapian::audio::WaveformData;
  WaveformData data;
  data.Reset(0.125);
  CheckNear(data.offset_seconds(), 0.125, 1e-9, "WaveformData offset");
  Check(data.bucket_count() == 0, "初始 bucket 数为 0");

  data.SetBucket(5, -100, 200);
  Check(data.bucket_count() == 6, "SetBucket 自动扩展");
  const auto bucket = data.BucketAt(5);
  Check(bucket.min == -100 && bucket.max == 200, "bucket min/max 正确");
  Check(data.BucketAt(4).min == 0 && data.BucketAt(4).max == 0, "空洞 bucket 为 0");

  std::vector<lapian::audio::WaveformBucket> range;
  data.CopyRange(3, 6, &range);
  Check(range.size() == 3, "CopyRange 返回请求范围");
  Check(range[2].max == 200, "CopyRange 末项正确");
  data.CopyRange(10, 20, &range);
  Check(range.empty(), "越界 CopyRange 为空");

  data.SetDuration(12.5);
  CheckNear(data.duration_seconds(), 12.5, 1e-9, "duration 设置");
}

void TestV9() {
  if (!std::filesystem::exists("media/V9.mp4")) {
    std::printf("[SKIP] media/V9.mp4 不存在\n");
    g_skipped = true;
    return;
  }

  lapian::audio::WaveformGenerator generator;
  Check(generator.Start("media/V9.mp4", 1, 0.0), "启动 V9 波形生成");
  auto status = generator.status();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while ((status.state == lapian::audio::WaveformState::kRunning ||
          status.state == lapian::audio::WaveformState::kIdle) &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    status = generator.status();
  }

  Check(status.state == lapian::audio::WaveformState::kReady, "V9 波形生成完成");
  const auto data = generator.data();
  Check(data != nullptr, "V9 波形数据存在");
  if (data == nullptr) {
    return;
  }
  CheckNear(data->offset_seconds(), 0.0, 1e-9, "V9 波形 offset 为 0");
  const std::size_t count = data->bucket_count();
  Check(count >= 950 && count <= 1050, "V9 5s @ 200 bucket/s 约 1000 桶");
  Check(data->duration_seconds() >= 4.9 && data->duration_seconds() <= 5.2,
        "V9 波形时长约 5s");

  int peak = 0;
  std::size_t peak_bucket = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const auto bucket = data->BucketAt(i);
    const int magnitude =
        std::max(std::abs(static_cast<int>(bucket.min)),
                 std::abs(static_cast<int>(bucket.max)));
    if (magnitude > peak) {
      peak = magnitude;
      peak_bucket = i;
    }
  }
  Check(peak > 500, "V9 波形有 click 峰值");
  const std::size_t expected_bucket = static_cast<std::size_t>(100.0 / 24.0 * 200.0);
  Check(peak_bucket + 5 >= expected_bucket && peak_bucket <= expected_bucket + 5,
        "V9 click 峰值落在 frame100 对应 bucket ±5");
  std::printf("       V9 buckets=%zu peak=%d bucket=%zu expected=%zu\n", count, peak,
              peak_bucket, expected_bucket);
}

}  // namespace

int main() {
  std::printf("T3.5 波形测试\n");
  TestData();
  TestV9();

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  if (g_skipped) {
    std::printf("部分媒体素材缺失，测试跳过\n");
    return 77;
  }
  std::printf("全部通过\n");
  return 0;
}
