// T1.1 媒体探测单测 + 条件集成测试。
//
// 不依赖 Catch2：T1.1 的测试量还小，先用零依赖断言，避免把构建期网络
// 变成 T1.1 的阻塞项。媒体素材缺失时返回 77（CTest SKIP_RETURN_CODE）。

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "media/MediaProbe.h"

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

void Skip(const std::string& what) {
  std::printf("[SKIP] %s\n", what.c_str());
  g_skipped = true;
}

std::string PathToUtf8(const std::filesystem::path& path) {
  const std::u8string utf8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

class TempDir {
 public:
  TempDir() {
    static int counter = 0;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("lapian_t1_1_" + std::to_string(stamp) + "_" + std::to_string(counter++));
    std::error_code error;
    std::filesystem::create_directories(path_, error);
  }

  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

bool WriteBytes(const std::filesystem::path& path, const std::string& data) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(data.data(), static_cast<std::streamsize>(data.size()));
  return output.good();
}

bool ProbeFixture(const std::string& relative, lapian::media::MediaProbeResult* result) {
  if (!std::filesystem::exists(relative)) {
    Skip(relative + " 不存在");
    return false;
  }
  *result = lapian::media::ProbeMedia(PathToUtf8(relative));
  Check(result->ok, "打开 " + relative);
  if (!result->ok) {
    std::printf("       error=%s detail=%s\n",
                lapian::media::MediaProbeErrorName(result->error),
                result->error_detail.c_str());
  }
  return result->ok;
}

void TestRational() {
  const lapian::media::Rational ntsc{24000, 1001};
  Check(ntsc.valid(), "24000/1001 是有效帧率");
  CheckNear(ntsc.ToDouble(), 23.976023976, 0.000001, "24000/1001 转 double");

  const lapian::media::Rational invalid{0, 1001};
  Check(!invalid.valid(), "0/1001 是无效帧率");
  CheckNear(invalid.ToDouble(), 0.0, 0.0, "无效帧率转 double 为 0");
}

void TestRotationNormalization() {
  using lapian::media::NormalizeRotationDegrees;
  Check(NormalizeRotationDegrees(0.0) == 0, "旋转 0°");
  Check(NormalizeRotationDegrees(90.0) == 90, "旋转 90°");
  Check(NormalizeRotationDegrees(180.0) == 180, "旋转 180°");
  Check(NormalizeRotationDegrees(-90.0) == 270, "旋转 -90° 归一化到 270");
  Check(NormalizeRotationDegrees(450.0) == 90, "旋转 450° 归一化到 90");
  Check(NormalizeRotationDegrees(-450.0) == 270, "旋转 -450° 归一化到 270");
  Check(NormalizeRotationDegrees(359.6) == 0, "旋转 359.6° 四舍五入到 0");
  Check(NormalizeRotationDegrees(std::nan("")) == 0, "NaN 旋转角归零");
}

void TestVfrClassifier() {
  using lapian::media::ClassifyVfrIntervals;
  using lapian::media::VfrInfo;

  std::vector<double> cfr(300, 1.0 / 24.0);
  const VfrInfo cfr_result = ClassifyVfrIntervals(cfr);
  Check(cfr_result.checked, "CFR 间隔样本足够");
  Check(!cfr_result.suspect, "CFR 不误报 VFR");
  Check(cfr_result.sampled_frames == 300, "CFR 采样数 300");
  CheckNear(cfr_result.median_interval_seconds, 1.0 / 24.0, 1e-9, "CFR 中位间隔");
  CheckNear(cfr_result.deviation_ratio, 0.0, 1e-9, "CFR 偏离比例 0");

  std::vector<double> vfr;
  vfr.reserve(300);
  for (int i = 0; i < 300; ++i) {
    vfr.push_back(i % 2 == 0 ? 0.02 : 0.06);
  }
  const VfrInfo vfr_result = ClassifyVfrIntervals(vfr);
  Check(vfr_result.checked, "VFR 间隔样本足够");
  Check(vfr_result.suspect, "交替长短间隔判为 VFR");
  Check(vfr_result.deviation_ratio > 0.99, "VFR 偏离比例接近 1");

  const VfrInfo few_result = ClassifyVfrIntervals(std::vector<double>(4, 1.0 / 24.0));
  Check(!few_result.checked, "少于 8 个间隔不检查");
  Check(!few_result.suspect, "样本不足不误报");

  std::vector<double> invalid{0.0, -1.0, std::nan(""), 1.0 / 24.0, 1.0 / 24.0, 1.0 / 24.0,
                              1.0 / 24.0, 1.0 / 24.0, 1.0 / 24.0, 1.0 / 24.0, 1.0 / 24.0};
  const VfrInfo invalid_result = ClassifyVfrIntervals(invalid);
  Check(invalid_result.checked, "过滤非法间隔后仍可检查");
  Check(invalid_result.sampled_frames == 8, "非法间隔不计入采样数");
  Check(!invalid_result.suspect, "过滤后 CFR 不误报");
}

void TestErrorCases() {
  TempDir temp;

  const auto missing = temp.path() / "missing.mp4";
  const auto missing_result = lapian::media::ProbeMedia(PathToUtf8(missing));
  Check(!missing_result.ok, "不存在的文件探测失败");
  Check(missing_result.error == lapian::media::MediaProbeError::kFileNotFound,
        "不存在的文件归类为 file_not_found");

  const auto garbage = temp.path() / "garbage.mp4";
  Check(WriteBytes(garbage, "this is not a video"), "写入垃圾文件");
  const auto garbage_result = lapian::media::ProbeMedia(PathToUtf8(garbage));
  Check(!garbage_result.ok, "垃圾文件探测失败");
  Check(garbage_result.error != lapian::media::MediaProbeError::kNone, "垃圾文件有错误分类");

  const auto empty = temp.path() / "empty.mp4";
  Check(WriteBytes(empty, ""), "写入空文件");
  const auto empty_result = lapian::media::ProbeMedia(PathToUtf8(empty));
  Check(!empty_result.ok, "空文件探测失败");

  const auto directory_result = lapian::media::ProbeMedia(PathToUtf8(temp.path()));
  Check(!directory_result.ok, "目录路径探测失败而不是崩溃");
}

void TestV9() {
  lapian::media::MediaProbeResult result;
  if (!ProbeFixture("media/V9.mp4", &result)) {
    return;
  }

  Check(result.info.video.width == 1920, "V9 宽 1920");
  Check(result.info.video.height == 1080, "V9 高 1080");
  Check(result.info.video.codec.find("h264") != std::string::npos, "V9 编码为 H.264");
  CheckNear(result.info.video.frame_rate.ToDouble(), 24.0, 0.01, "V9 帧率 24");
  Check(result.info.has_audio, "V9 有音轨");
  Check(result.info.audio.sample_rate == 48000, "V9 音频 48kHz");
  Check(result.info.av_offset_estimated, "V9 估出 A/V 偏移");
  Check(std::abs(result.info.av_offset_us) <= 42000,
        "V9 首包 PTS+priming 与视频对齐（≤1 帧）");
  Check(result.info.rotation.counterclockwise_degrees == 0, "V9 无旋转");
  Check(result.info.video.bit_depth == 8, "V9 位深 8-bit");
  Check(!result.info.vfr.suspect, "V9 不误报 VFR");
  Check(result.info.duration_us >= 4000000 && result.info.duration_us <= 6000000,
        "V9 时长约 5 秒");
}

void TestProxies() {
  lapian::media::MediaProbeResult v1;
  if (ProbeFixture("media/proxy/proxy-V1.mp4", &v1)) {
    Check(v1.info.video.width == 1920 && v1.info.video.height == 1080, "proxy-V1 1080p");
    CheckNear(v1.info.video.frame_rate.ToDouble(), 24.0, 0.01, "proxy-V1 24fps");
    Check(v1.info.video.codec.find("h264") != std::string::npos, "proxy-V1 H.264");
    Check(v1.info.has_audio, "proxy-V1 有音轨");
  }

  lapian::media::MediaProbeResult v3;
  if (ProbeFixture("media/proxy/proxy-V3.mp4", &v3)) {
    Check(v3.info.video.width == 1920 && v3.info.video.height == 1080, "proxy-V3 1080p");
    CheckNear(v3.info.video.frame_rate.ToDouble(), 60.0, 0.01, "proxy-V3 60fps");
    Check(v3.info.video.codec.find("hevc") != std::string::npos, "proxy-V3 HEVC");
    Check(v3.info.has_audio, "proxy-V3 有音轨");
  }

  lapian::media::MediaProbeResult v4;
  if (ProbeFixture("media/proxy/proxy-V4.mp4", &v4)) {
    Check(v4.info.video.width == 3840 && v4.info.video.height == 2160, "proxy-V4 4K");
    CheckNear(v4.info.video.frame_rate.ToDouble(), 30.0, 0.01, "proxy-V4 30fps");
    Check(v4.info.video.codec.find("h264") != std::string::npos, "proxy-V4 H.264");
    Check(v4.info.has_audio, "proxy-V4 有音轨");
  }
}

void TestHighBitDepthFixture() {
  lapian::media::MediaProbeResult result;
  if (!ProbeFixture("build/test-media/10bit.mp4", &result)) {
    return;
  }
  Check(result.info.video.pixel_format.find("10") != std::string::npos,
        "10bit 素材像素格式带 10");
  Check(result.info.video.bit_depth == 10, "T6.5 探测到 10-bit 位深");
  Check(!result.info.has_audio, "10bit 素材无音轨（同时覆盖无音频提示）");
}

void TestUnicodePath() {
  if (!std::filesystem::exists("media/V9.mp4")) {
    Skip("media/V9.mp4 不存在，跳过中文路径测试");
    return;
  }

  TempDir temp;
  const std::filesystem::path unicode_name(std::u8string(u8"拉片 测试 V9.mp4"));
  const auto destination = temp.path() / unicode_name;
  std::error_code error;
  std::filesystem::copy_file("media/V9.mp4", destination,
                             std::filesystem::copy_options::overwrite_existing, error);
  Check(!error, "复制 V9 到中文/空格路径");
  if (error) {
    return;
  }

  const auto result = lapian::media::ProbeMedia(PathToUtf8(destination));
  Check(result.ok, "中文/空格路径可打开");
  if (result.ok) {
    Check(result.info.video.width == 1920 && result.info.video.height == 1080,
          "中文路径文件仍是 1080p");
  }
}

}  // namespace

int main() {
  std::printf("T1.1 媒体探测测试\n");

  TestRational();
  TestRotationNormalization();
  TestVfrClassifier();
  TestErrorCases();
  TestV9();
  TestProxies();
  TestHighBitDepthFixture();
  TestUnicodePath();

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
