// T1.3 离屏渲染测试：不建窗口，把首帧画到 RTV，回读像素确认不是空屏。
// 素材缺失时 CTest SKIP；硬件路径不可用时只测软件路径。

#include <windows.h>

#include <d3d11.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <wrl/client.h>

#include "media/VideoDecoder.h"
#include "platform/gfx/D3D11Device.h"
#include "platform/gfx/VideoRenderer.h"

namespace {

int g_failures = 0;
bool g_skipped = false;

using Microsoft::WRL::ComPtr;

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

struct RenderTarget {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11RenderTargetView> view;
  ComPtr<ID3D11Texture2D> staging;
};

bool CreateRenderTarget(ID3D11Device* device, UINT width, UINT height, RenderTarget* target,
                        std::string* error) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, target->texture.GetAddressOf()))) {
    *error = "CreateTexture2D(RTV) 失败";
    return false;
  }
  if (FAILED(device->CreateRenderTargetView(target->texture.Get(), nullptr,
                                            target->view.GetAddressOf()))) {
    *error = "CreateRenderTargetView 失败";
    return false;
  }

  D3D11_TEXTURE2D_DESC staging_desc = desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (FAILED(device->CreateTexture2D(&staging_desc, nullptr, target->staging.GetAddressOf()))) {
    *error = "CreateTexture2D(staging) 失败";
    return false;
  }
  return true;
}

bool HasNonBackgroundPixel(ID3D11DeviceContext* context, const RenderTarget& target) {
  context->CopyResource(target.staging.Get(), target.texture.Get());
  D3D11_TEXTURE2D_DESC staging_desc{};
  target.staging->GetDesc(&staging_desc);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
    return false;
  }
  const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData);
  bool found = false;
  for (UINT y = 0; y < staging_desc.Height && !found; y += 8) {
    const auto* row = pixels + static_cast<std::size_t>(y) * mapped.RowPitch;
    for (UINT x = 0; x < staging_desc.Width; x += 8) {
      const std::uint8_t b = row[x * 4 + 0];
      const std::uint8_t g = row[x * 4 + 1];
      const std::uint8_t r = row[x * 4 + 2];
      // 背景约为 (23,23,26)；只要有一块明显不同的像素，就说明画出了视频。
      if (std::max({r, g, b}) > 60 || std::min({r, g, b}) < 8) {
        found = true;
        break;
      }
    }
  }
  context->Unmap(target.staging.Get(), 0);
  return found;
}

void TestFrame(const std::string& path, bool prefer_hardware, const std::string& label) {
  if (!FileExists(path)) {
    Skip(path + " 不存在");
    return;
  }

  lapian::gfx::D3D11Device device;
  std::string error;
  if (!device.Create(&error)) {
    Skip("D3D11 设备创建失败: " + error);
    return;
  }

  lapian::media::VideoDecoder decoder;
  if (!decoder.OpenWithD3D11Device(path, -1, device.device(), prefer_hardware, &error)) {
    if (prefer_hardware) {
      Skip(label + " 硬解打开失败: " + error);
    } else {
      Check(false, label + " 软解打开失败: " + error);
    }
    return;
  }
  lapian::media::DecodedFrameInfo frame;
  if (!decoder.DecodeFirstFrame(&frame, &error)) {
    Check(false, label + " 首帧解码失败: " + error);
    return;
  }

  lapian::gfx::VideoRenderer renderer;
  if (!renderer.Initialize(device.device(), device.context(), &error)) {
    Check(false, label + " VideoRenderer 初始化失败: " + error);
    return;
  }
  if (!renderer.SetFrame(frame.av_frame, 0, &error)) {
    Check(false, label + " SetFrame 失败: " + error);
    return;
  }

  RenderTarget target;
  if (!CreateRenderTarget(device.device(), 640, 360, &target, &error)) {
    Check(false, label + " " + error);
    return;
  }
  renderer.Render(target.view.Get(), 640, 360);
  Check(HasNonBackgroundPixel(device.context(), target),
        label + " 首帧画出了非背景像素");
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("T1.3 离屏渲染测试\n");

  TestFrame("media/proxy/proxy-V1.mp4", false, "proxy-V1 软解");
  TestFrame("media/proxy/proxy-V3.mp4", false, "proxy-V3 软解");
  TestFrame("media/proxy/proxy-V1.mp4", true, "proxy-V1 硬解");
  TestFrame("media/proxy/proxy-V4.mp4", true, "proxy-V4 4K 硬解");
  TestFrame("build/test-media/10bit.mp4", false, "10-bit 软解");

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
