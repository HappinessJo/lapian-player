#include "platform/gfx/SwapChain.h"

#include "platform/gfx/DxgiError.h"

namespace lapian::gfx {

SwapChain::~SwapChain() { Reset(); }

void SwapChain::Reset() {
  render_target_.Reset();
  swap_chain_.Reset();
  context_.Reset();
  device_.Reset();
  width_ = 0;
  height_ = 0;
}

bool SwapChain::Create(ID3D11Device* device, HWND hwnd, std::string* error) {
  Reset();

  if (device == nullptr || hwnd == nullptr) {
    if (error != nullptr) {
      *error = "创建交换链失败：设备或窗口为空";
    }
    return false;
  }

  device_ = device;
  device->GetImmediateContext(context_.ReleaseAndGetAddressOf());
  if (!context_) {
    if (error != nullptr) {
      *error = "创建交换链失败：拿不到 D3D11 立即上下文";
    }
    Reset();
    return false;
  }

  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
  HRESULT hr = device->QueryInterface(IID_PPV_ARGS(dxgi_device.GetAddressOf()));
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "创建交换链失败（QueryInterface IDXGIDevice）：" + HResultText(hr);
    }
    Reset();
    return false;
  }

  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  hr = dxgi_device->GetAdapter(adapter.GetAddressOf());
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "创建交换链失败（GetAdapter）：" + HResultText(hr);
    }
    Reset();
    return false;
  }

  Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
  hr = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "创建交换链失败（GetParent IDXGIFactory2）：" + HResultText(hr);
    }
    Reset();
    return false;
  }

  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Width = 0;   // 0 = 使用窗口客户区
  desc.Height = 0;  // 0 = 使用窗口客户区
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.Stereo = FALSE;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  desc.Flags = 0;

  hr = factory->CreateSwapChainForHwnd(device, hwnd, &desc, nullptr, nullptr,
                                      swap_chain_.GetAddressOf());
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "CreateSwapChainForHwnd 失败：" + HResultText(hr);
    }
    Reset();
    return false;
  }

  DXGI_SWAP_CHAIN_DESC1 actual{};
  swap_chain_->GetDesc1(&actual);
  width_ = actual.Width;
  height_ = actual.Height;

  return CreateRenderTargetView(error);
}

bool SwapChain::CreateRenderTargetView(std::string* error) {
  if (!swap_chain_ || !device_) {
    if (error != nullptr) {
      *error = "创建 RTV 失败：交换链未初始化";
    }
    return false;
  }

  Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
  HRESULT hr = swap_chain_->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf()));
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "交换链 GetBuffer(0) 失败：" + HResultText(hr);
    }
    return false;
  }

  DXGI_SWAP_CHAIN_DESC1 desc{};
  swap_chain_->GetDesc1(&desc);

  D3D11_RENDER_TARGET_VIEW_DESC rtv_desc{};
  rtv_desc.Format = desc.Format;
  rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

  hr = device_->CreateRenderTargetView(back_buffer.Get(), &rtv_desc,
                                      render_target_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "CreateRenderTargetView 失败：" + HResultText(hr);
    }
    return false;
  }
  return true;
}

bool SwapChain::Resize(UINT width, UINT height, std::string* error) {
  if (!swap_chain_) {
    return false;
  }
  if (width == 0 || height == 0) {
    return true;  // 最小化：保留交换链，下次恢复尺寸时再调整
  }
  if (width == width_ && height == height_) {
    return true;
  }

  context_->OMSetRenderTargets(0, nullptr, nullptr);
  render_target_.Reset();

  const HRESULT hr = swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "ResizeBuffers 失败：" + HResultText(hr);
    }
    return false;
  }

  width_ = width;
  height_ = height;
  return CreateRenderTargetView(error);
}

HRESULT SwapChain::Render() {
  if (!swap_chain_ || !render_target_ || !context_) {
    return E_FAIL;
  }

  // 深灰底：T0.1 的画面就是“窗口活着”的证明，不做任何业务内容。
  static const float kClearColor[4] = {0.09f, 0.09f, 0.10f, 1.0f};
  context_->ClearRenderTargetView(render_target_.Get(), kClearColor);

  ID3D11RenderTargetView* views[] = {render_target_.Get()};
  context_->OMSetRenderTargets(1, views, nullptr);

  return swap_chain_->Present(1, 0);
}

HRESULT SwapChain::Present() {
  if (!swap_chain_) {
    return E_FAIL;
  }
  return swap_chain_->Present(1, 0);
}

}  // namespace lapian::gfx
