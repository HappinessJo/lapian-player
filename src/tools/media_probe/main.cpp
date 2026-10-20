// T1.1 媒体探测开发工具（不随产品分发）。
//
// 输出稳定的 key=value 行，便于手测 V1–V8 与脚本采集：
//   lapian_media_probe media\V1.mp4
//
// 只读取元数据，不解码、不渲染、不写源文件、不发网络请求。

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "media/MediaProbe.h"

namespace {

using lapian::media::MediaProbeError;
using lapian::media::MediaProbeResult;
using lapian::media::Rational;

std::string WideToUtf8(const std::wstring& wide) {
  if (wide.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                       nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size,
                      nullptr, nullptr);
  return result;
}

const char* ErrorLabel(MediaProbeError error) {
  switch (error) {
    case MediaProbeError::kNone:
      return "无错误";
    case MediaProbeError::kFileNotFound:
      return "文件不存在";
    case MediaProbeError::kPermissionDenied:
      return "没有读取权限";
    case MediaProbeError::kUnsupportedFormat:
      return "不支持的容器/格式";
    case MediaProbeError::kCorrupt:
      return "文件损坏或无法解析";
    case MediaProbeError::kNoVideoStream:
      return "没有视频流";
    case MediaProbeError::kStreamInfoFailed:
      return "读取流信息失败";
    case MediaProbeError::kOpenFailed:
      return "打开失败";
  }
  return "未知错误";
}

void PrintRational(const char* name, const Rational& value) {
  std::printf("%s=%d/%d", name, value.num, value.den);
  if (value.valid()) {
    std::printf(" (%.6f)", value.ToDouble());
  }
  std::printf("\n");
}

void PrintResult(const MediaProbeResult& result) {
  std::printf("OK=%d\n", result.ok ? 1 : 0);
  std::printf("PROBE_MS=%.2f\n", result.probe_ms);
  if (!result.ok) {
    std::printf("ERROR=%s\n", lapian::media::MediaProbeErrorName(result.error));
    std::printf("ERROR_LABEL=%s\n", ErrorLabel(result.error));
    std::printf("DETAIL=%s\n", result.error_detail.c_str());
    return;
  }

  const auto& info = result.info;
  const auto& video = info.video;
  std::printf("FORMAT=%s\n", info.format_name.c_str());
  std::printf("FORMAT_LONG=%s\n", info.format_long_name.c_str());
  std::printf("DURATION_US=%lld\n", static_cast<long long>(info.duration_us));
  std::printf("DURATION_RELIABLE=%d\n", info.duration_reliable ? 1 : 0);
  std::printf("VIDEO_INDEX=%d\n", video.stream_index);
  std::printf("CODEC=%s\n", video.codec.c_str());
  std::printf("SIZE=%dx%d\n", video.width, video.height);
  std::printf("PIX_FMT=%s\n", video.pixel_format.c_str());
  PrintRational("FPS_AVG", video.average_frame_rate);
  PrintRational("FPS_REAL", video.real_frame_rate);
  PrintRational("FPS", video.frame_rate);
  PrintRational("TIME_BASE", video.time_base);
  std::printf("START_TIME=%lld\n", static_cast<long long>(video.start_time));
  std::printf("STREAM_DURATION=%lld\n", static_cast<long long>(video.duration));
  std::printf("NB_FRAMES=%lld\n", static_cast<long long>(video.nb_frames));
  std::printf("NB_FRAMES_KNOWN=%d\n", video.nb_frames_known ? 1 : 0);
  std::printf("HAS_DISPLAY_MATRIX=%d\n", info.rotation.has_matrix ? 1 : 0);
  std::printf("ROTATION_CCW_DEGREES=%d\n", info.rotation.counterclockwise_degrees);
  if (info.rotation.has_matrix) {
    std::printf("DISPLAY_MATRIX=");
    for (std::size_t i = 0; i < info.rotation.matrix.size(); ++i) {
      std::printf("%s%d", i == 0 ? "" : ",", info.rotation.matrix[i]);
    }
    std::printf("\n");
  }
  std::printf("VFR_CHECKED=%d\n", info.vfr.checked ? 1 : 0);
  std::printf("VFR_SUSPECT=%d\n", info.vfr.suspect ? 1 : 0);
  std::printf("VFR_SAMPLED=%d\n", info.vfr.sampled_frames);
  std::printf("VFR_MEDIAN_INTERVAL_S=%.9f\n", info.vfr.median_interval_seconds);
  std::printf("VFR_DEVIATION_RATIO=%.6f\n", info.vfr.deviation_ratio);
  std::printf("HAS_AUDIO=%d\n", info.has_audio ? 1 : 0);
  if (info.has_audio) {
    std::printf("AUDIO_INDEX=%d\n", info.audio.stream_index);
    std::printf("AUDIO_CODEC=%s\n", info.audio.codec.c_str());
    std::printf("AUDIO_RATE=%d\n", info.audio.sample_rate);
    std::printf("AUDIO_CHANNELS=%d\n", info.audio.channels);
    std::printf("AV_OFFSET_US=%lld\n", static_cast<long long>(info.av_offset_us));
    std::printf("AV_OFFSET_ESTIMATED=%d\n", info.av_offset_estimated ? 1 : 0);
  }
}

int Run(const std::string& path) {
  std::printf("FILE=%s\n", path.c_str());
  const MediaProbeResult result = lapian::media::ProbeMedia(path);
  PrintResult(result);
  return result.ok ? 0 : 3;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);
  if (argc < 2) {
    std::printf("用法: lapian_media_probe <视频文件>\n");
    return 2;
  }
  return Run(WideToUtf8(argv[1]));
}
