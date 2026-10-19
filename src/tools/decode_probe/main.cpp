// T1.2 解码探针（开发工具，不随产品分发）。
//
// 用法：
//   lapian_decode_probe --file media\V9.mp4 [--frames 1] [--sw] [--stream N]
//
// 只解码并打印帧元数据，不渲染、不写源文件、不发网络请求。

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "media/VideoDecoder.h"

namespace {

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

void PrintUsage() {
  std::printf("用法: lapian_decode_probe --file <视频> [--frames 1] [--sw] [--stream N]\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);

  std::string file;
  int frames = 1;
  bool software = false;
  int stream = -1;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = WideToUtf8(argv[i]);
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        PrintUsage();
        std::exit(2);
      }
      return WideToUtf8(argv[++i]);
    };
    if (arg == "--file") {
      file = next();
    } else if (arg == "--frames") {
      frames = std::atoi(next().c_str());
    } else if (arg == "--sw") {
      software = true;
    } else if (arg == "--stream") {
      stream = std::atoi(next().c_str());
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage();
      return 0;
    } else {
      PrintUsage();
      return 2;
    }
  }

  if (file.empty() || frames <= 0) {
    PrintUsage();
    return 2;
  }

  std::printf("FILE=%s\n", file.c_str());
  lapian::media::VideoDecoder decoder;
  std::string error;
  if (!decoder.Open(file, stream, !software, &error)) {
    std::printf("OK=0\nERROR=%s\n", error.c_str());
    return 3;
  }

  std::printf("OK=1\n");
  std::printf("HW_ACTIVE=%d\n", decoder.HardwareActive() ? 1 : 0);
  std::printf("BIT_DEPTH=%d\n", decoder.BitDepth());

  for (int i = 0; i < frames; ++i) {
    lapian::media::DecodedFrameInfo info;
    const bool first = i == 0;
    const bool ok = first ? decoder.DecodeFirstFrame(&info, &error)
                          : decoder.DecodeNextFrame(&info, &error);
    if (!ok) {
      std::printf("FRAME=%d\nDECODE_OK=0\nERROR=%s\n", i, error.c_str());
      return 4;
    }
    std::printf(
        "FRAME=%d PTS=%lld DURATION=%lld KEY=%d HW=%d FMT=%s W=%d H=%d BIT_DEPTH=%d HAS_FRAME=%d\n",
        i, static_cast<long long>(info.pts), static_cast<long long>(info.duration),
        info.key_frame ? 1 : 0, info.hardware ? 1 : 0, info.pixel_format_name.c_str(), info.width,
        info.height, info.bit_depth, info.av_frame != nullptr ? 1 : 0);
  }

  std::printf("DECODE_OK=1\n");
  return 0;
}
