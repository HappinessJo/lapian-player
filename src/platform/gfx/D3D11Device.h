#pragma once

#include <windows.h>

#include <d3d11.h>

#include <string>

#include <wrl/client.h>

namespace lapian::gfx {

// 一次设备创建的观测结果，供状态栏与自检程序输出。
struct DeviceInfo {
  std::wstring adapter_name;
  D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_10_0;
  bool hardware = false;
  bool debug_layer = false;
};

// "11_1" / "11_0" / …；未知时返回 "unknown"。
const char* FeatureLevelName(D3D_FEATURE_LEVEL level);

// 拥有 ID3D11Device 与 ImmediateContext 的最小封装。
// T0.1 只负责创建与查询；解码、着色、设备丢失重建留给 T1.2/T1.3。
class D3D11Device {
 public:
  D3D11Device() = default;
  ~D3D11Device();

  D3D11Device(const D3D11Device&) = delete;
  D3D11Device& operator=(const D3D11Device&) = delete;

  // 优先硬件设备；失败回退 WARP。Debug 构建下尝试打开调试层，失败则关闭。
  bool Create(std::string* error);
  void Reset();

  bool IsValid() const { return device_ != nullptr; }
  ID3D11Device* device() const { return device_.Get(); }
  ID3D11DeviceContext* context() const { return context_.Get(); }
  const DeviceInfo& info() const { return info_; }

  static bool IsDeviceLost(HRESULT hr);

 private:
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  DeviceInfo info_;
};

}  // namespace lapian::gfx
