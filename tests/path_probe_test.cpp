// T7.5 网络路径有界预检：UNC/映射盘识别 + 存在/缺失/超时的耗时上限。
// 不依赖 Qt/FFmpeg；超时用例只断言“等待被上界截断”，不依赖目标主机是否存在。

#include <chrono>
#include <cstdio>
#include <random>
#include <string>

#include "media/PathProbe.h"

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

double ElapsedMs(std::chrono::steady_clock::time_point started) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                    started)
      .count();
}

void TestNetworkDetection() {
  using lapian::media::IsNetworkPath;
  Check(IsNetworkPath("\\\\server\\share\\clip.mp4"), "UNC 反斜杠识别为网络");
  Check(IsNetworkPath("//server/share/clip.mp4"), "UNC 正斜杠识别为网络");
  Check(IsNetworkPath("\\\\?\\UNC\\server\\share\\clip.mp4"),
        "\\\\?\\UNC 识别为网络");
  Check(!IsNetworkPath("\\\\?\\C:\\media\\clip.mp4"),
        "扩展本地路径不是网络");
  Check(!IsNetworkPath("\\\\.\\pipe\\lapian"), "设备命名空间不是网络");
  Check(!IsNetworkPath("media\\V9.mp4"), "相对路径不是网络");
  Check(!IsNetworkPath(""), "空路径不是网络");
}

void TestLocalProbe() {
  using lapian::media::PathProbeResult;
  using lapian::media::PathProbeStatus;
  using lapian::media::ProbePathWithTimeout;

  const auto existing_started = std::chrono::steady_clock::now();
#ifdef _WIN32
  const std::string existing_path = "media\\V9.mp4";
  const std::string missing_path = "build\\t7.5-pathprobe-missing.mp4";
#else
  const std::string existing_path = "media/macos-smoke.mp4";
  const std::string missing_path = "build/t7.5-pathprobe-missing.mp4";
#endif
  const PathProbeResult existing = ProbePathWithTimeout(existing_path, 2000);
  const double existing_ms = ElapsedMs(existing_started);
  Check(existing.status == PathProbeStatus::kReachable, "本地已存在文件可达");
  Check(existing_ms < 2000.0, "本地已存在文件不等待超时");

  const PathProbeResult missing = ProbePathWithTimeout(missing_path, 2000);
  Check(missing.status == PathProbeStatus::kMissing, "本地缺失文件报告 missing");
  Check(missing.elapsed_ms < 2000.0, "本地缺失文件快速返回");
  Check(!missing.message.empty(), "缺失文件有可读提示");

  const PathProbeResult directory = ProbePathWithTimeout("media", 2000);
  Check(directory.status == PathProbeStatus::kReachable, "目录本身可被属性查询");
}

void TestBoundedTimeout() {
  using lapian::media::PathProbeResult;
  using lapian::media::PathProbeStatus;
  using lapian::media::ProbePathWithTimeout;

  // TEST-NET-3（203.0.113.0/24）通常不可达；关键是调用方等待被 400ms 截断，
  // 而不是跟随 SMB 的 ~20s 默认超时。随机末位避免 Windows SMB 负缓存。
  std::mt19937 rng(std::random_device{}());
  const int last_octet = std::uniform_int_distribution<int>(1, 254)(rng);
  const std::string dead = "\\\\203.0.113." + std::to_string(last_octet) +
                           "\\lapian-share\\clip.mp4";
  const auto started = std::chrono::steady_clock::now();
  const PathProbeResult result = ProbePathWithTimeout(dead, 400);
  const double elapsed = ElapsedMs(started);
  std::printf("[INFO] test-net probe status=%d elapsed_ms=%.1f\n",
              static_cast<int>(result.status), elapsed);
  Check(elapsed < 3000.0, "不可达 UNC 预检被超时上界截断");
  if (result.status == PathProbeStatus::kTimeout) {
    Check(!result.message.empty(), "超时有可读提示");
  } else {
    // 少数网络会立刻返回 host/net 错误；同样是“快速失败”，不判失败。
    std::printf("[INFO] test-net probe returned early (status=%d)\n",
                static_cast<int>(result.status));
  }
}

}  // namespace

int main() {
  TestNetworkDetection();
  TestLocalProbe();
  TestBoundedTimeout();
  if (g_failures == 0) {
    std::printf("[PASS] path_probe_test\n");
    return 0;
  }
  std::printf("[FAIL] path_probe_test failures=%d\n", g_failures);
  return 1;
}
