#pragma once

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>

#include <string>

#include <wrl/client.h>

namespace lapian::gfx {

// flip-model 交换链（B8G8R8A8、2 缓冲）。
// T0.1 只做清屏与呈现，用来验证“Qt 原生子窗口 + D3D11”这条路径；
// 视频帧的 NV12/P010 着色器留给 T1.3。
class SwapChain {
 public:
  SwapChain() = default;
  ~SwapChain();

  SwapChain(const SwapChain&) = delete;
  SwapChain& operator=(const SwapChain&) = delete;

  // hwnd 为窗口客户区；宽高为 0 时由交换链取窗口客户区尺寸。
  bool Create(ID3D11Device* device, HWND hwnd, std::string* error);
  bool Resize(UINT width, UINT height, std::string* error);

  // 清屏 + Present(1, 0)；返回 HRESULT，设备丢失由调用方处理。
  HRESULT Render();

  void Reset();

  bool IsValid() const { return swap_chain_ != nullptr; }
  UINT width() const { return width_; }
  UINT height() const { return height_; }

 private:
  bool CreateRenderTargetView(std::string* error);

  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_;
  UINT width_ = 0;
  UINT height_ = 0;
};

}  // namespace lapian::gfx
