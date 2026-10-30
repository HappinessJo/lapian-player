// T1.2 解码服务测试：条件集成测试，素材缺失时 CTest SKIP。

#include <cstdio>
#include <filesystem>
#include <string>

#include "media/VideoDecoder.h"

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
  // 源文件里的 path 是 UTF-8；Windows 的窄字符 path 会按 ANSI 解释，中文会查不到。
  const std::u8string utf8(reinterpret_cast<const char8_t*>(path.data()), path.size());
  return std::filesystem::exists(std::filesystem::path(utf8));
}

bool DecodeFirst(const std::string& path, bool prefer_hardware, bool expect_hardware,
                 lapian::media::DecodedFrameInfo* info) {
  if (!FileExists(path)) {
    Skip(path + " 不存在");
    return false;
  }
  lapian::media::VideoDecoder decoder;
  std::string error;
  if (!decoder.Open(path, -1, prefer_hardware, &error)) {
    if (prefer_hardware) {
      Skip(path + " 硬解打开失败（" + error + "）");
      return false;
    }
    Check(false, "打开 " + path + ": " + error);
    return false;
  }
  Check(decoder.IsOpen(), "解码器已打开: " + path);
  Check(decoder.BitDepth() >= 8, "位深 >=8: " + path);
  if (expect_hardware) {
    Check(decoder.HardwareActive(), "硬解已激活: " + path);
  }
  const bool ok = decoder.DecodeFirstFrame(info, &error);
  Check(ok, "解码首帧: " + path);
  if (!ok) {
    std::printf("       error=%s\n", error.c_str());
    return false;
  }
  Check(info->av_frame != nullptr, "首帧有 AVFrame: " + path);
  Check(info->width > 0 && info->height > 0, "首帧尺寸有效: " + path);
  return true;
}

void TestMissingFile() {
  lapian::media::VideoDecoder decoder;
  std::string error;
  Check(!decoder.Open("Z:\\definitely-missing\\missing.mp4", -1, false, &error),
        "不存在的文件打开失败");
  Check(!error.empty(), "不存在文件有错误信息");
}

void TestV9Software() {
  lapian::media::DecodedFrameInfo info;
  if (!DecodeFirst("media/V9.mp4", false, false, &info)) {
    return;
  }
  Check(info.width == 1920 && info.height == 1080, "V9 软件解码 1080p");
  Check(info.pixel_format_name.find("yuv420p") != std::string::npos, "V9 软件解码 yuv420p");
  Check(!info.hardware, "V9 软件解码不是硬解帧");
  Check(info.bit_depth == 8, "V9 位深 8");
  Check(info.pts >= 0, "V9 首帧 PTS 非负");
}

void TestV9Hardware() {
#ifdef _WIN32
  if (!FileExists("media/V9.mp4")) {
    Skip("media/V9.mp4 不存在，跳过硬解测试");
    return;
  }
  lapian::media::VideoDecoder decoder;
  std::string error;
  if (!decoder.Open("media/V9.mp4", -1, true, &error)) {
    Skip("V9 硬解打开失败（" + error + "）");
    return;
  }
  if (!decoder.HardwareActive()) {
    Skip("V9 硬解未激活（驱动/环境不支持）");
    return;
  }
  lapian::media::DecodedFrameInfo info;
  if (!decoder.DecodeFirstFrame(&info, &error)) {
    Check(false, "V9 硬解首帧失败: " + error);
    return;
  }
  Check(info.width == 1920 && info.height == 1080, "V9 硬解 1080p");
  Check(info.hardware, "V9 硬解帧标记为 hardware");
  Check(info.pixel_format_name.find("d3d11") != std::string::npos, "V9 硬解格式为 d3d11");
#else
  std::printf("[SKIP] D3D11 hardware decode is Windows-only\n");
#endif
}

void TestV9Sequence() {
  if (!FileExists("media/V9.mp4")) {
    Skip("media/V9.mp4 不存在，跳过连续解码测试");
    return;
  }
  lapian::media::VideoDecoder decoder;
  std::string error;
  if (!decoder.Open("media/V9.mp4", -1, false, &error)) {
    Check(false, "V9 连续解码打开失败: " + error);
    return;
  }
  lapian::media::DecodedFrameInfo previous;
  if (!decoder.DecodeFirstFrame(&previous, &error)) {
    Check(false, "V9 连续解码首帧失败: " + error);
    return;
  }
  bool monotonic = true;
  for (int i = 0; i < 4; ++i) {
    lapian::media::DecodedFrameInfo current;
    if (!decoder.DecodeNextFrame(&current, &error)) {
      Check(false, "V9 连续解码第 " + std::to_string(i + 1) + " 帧失败: " + error);
      return;
    }
    if (current.pts < previous.pts) {
      monotonic = false;
    }
    previous = current;
  }
  Check(monotonic, "V9 连续解码 PTS 单调不降");
}

void TestProxyV3() {
  lapian::media::DecodedFrameInfo info;
  if (!DecodeFirst("media/proxy/proxy-V3.mp4", false, false, &info)) {
    return;
  }
  Check(info.width == 1920 && info.height == 1080, "proxy-V3 软件解码 1080p");
  Check(info.pixel_format_name.find("yuv420p") != std::string::npos,
        "proxy-V3 软件解码 yuv420p");
}

void TestRealSample() {
  lapian::media::DecodedFrameInfo info;
  if (!DecodeFirst("media/测试.mp4", false, false, &info)) {
    return;
  }
  Check(info.width == 1920 && info.height == 1080, "真实 1080p30 素材首帧 1080p");
  Check(info.pts >= 0, "真实素材首帧 PTS 非负");
}

}  // namespace

int main() {
  std::printf("T1.2 视频解码测试\n");
  TestMissingFile();
  TestV9Software();
  TestV9Hardware();
  TestV9Sequence();
  TestProxyV3();
  TestRealSample();

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  if (g_skipped) {
    std::printf("部分素材/硬解跳过\n");
    return 77;
  }
  std::printf("全部通过\n");
  return 0;
}
