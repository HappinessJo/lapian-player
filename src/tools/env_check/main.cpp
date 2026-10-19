// 环境自检：构建脚本在打包后运行，确认 Qt / FFmpeg / D3D11 三者都能用。
// 退出码：0 = 全部正常；2 = D3D11 不可用。stdout 供人读，脚本只看退出码。

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include <QtCore/QtGlobal>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "platform/gfx/D3D11Device.h"

namespace {

std::string WideToUtf8(const std::wstring& wide) {
  if (wide.empty()) {
    return std::string();
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                       nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    return std::string();
  }
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size,
                      nullptr, nullptr);
  return result;
}

}  // namespace

int main() {
  SetConsoleOutputCP(CP_UTF8);

  std::printf("lapian %s\n", LAPIAN_VERSION);
  std::printf("Qt 编译 %s / 运行 %s\n", QT_VERSION_STR, qVersion());
  std::printf("FFmpeg %s\n", av_version_info());
  std::printf("版本号 avformat=%u avcodec=%u avutil=%u swscale=%u swresample=%u\n",
              avformat_version(), avcodec_version(), avutil_version(), swscale_version(),
              swresample_version());

  const std::string configuration = avcodec_configuration();
  std::printf("configure: %s\n", configuration.c_str());
  if (configuration.find("--disable-network") != std::string::npos) {
    std::printf("网络协议: 已禁用（最小构建）\n");
  } else {
    std::printf("网络协议: 未在 configure 中显式禁用（阶段 A 预编译包；阶段 B 将替换为最小构建）\n");
  }

  lapian::gfx::D3D11Device device;
  std::string error;
  if (!device.Create(&error)) {
    std::printf("D3D11: 失败 — %s\n", error.c_str());
    return 2;
  }

  const lapian::gfx::DeviceInfo& info = device.info();
  std::printf("D3D11: %s · FL %s · %s%s\n", WideToUtf8(info.adapter_name).c_str(),
              lapian::gfx::FeatureLevelName(info.feature_level), info.hardware ? "硬件" : "WARP",
              info.debug_layer ? " · debug 层" : "");
  return 0;
}
