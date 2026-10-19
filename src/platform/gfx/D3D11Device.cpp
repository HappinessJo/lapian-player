#include "platform/gfx/D3D11Device.h"

#include <dxgi.h>

#include <wrl/client.h>

#include "platform/gfx/DxgiError.h"

namespace lapian::gfx {
namespace {

std::wstring QueryAdapterName(ID3D11Device* device) {
  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
  if (FAILED(device->QueryInterface(IID_PPV_ARGS(dxgi_device.GetAddressOf())))) {
    return L"(未知适配器)";
  }
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  if (FAILED(dxgi_device->GetAdapter(adapter.GetAddressOf()))) {
    return L"(未知适配器)";
  }
  DXGI_ADAPTER_DESC desc{};
  if (FAILED(adapter->GetDesc(&desc))) {
    return L"(未知适配器)";
  }
  return desc.Description;
}

}  // namespace

const char* FeatureLevelName(D3D_FEATURE_LEVEL level) {
  switch (level) {
    case D3D_FEATURE_LEVEL_11_1:
      return "11_1";
    case D3D_FEATURE_LEVEL_11_0:
      return "11_0";
    case D3D_FEATURE_LEVEL_10_1:
      return "10_1";
    case D3D_FEATURE_LEVEL_10_0:
      return "10_0";
    default:
      return "unknown";
  }
}

D3D11Device::~D3D11Device() { Reset(); }

void D3D11Device::Reset() {
  context_.Reset();
  device_.Reset();
  info_ = DeviceInfo{};
}

bool D3D11Device::Create(std::string* error) {
  Reset();

  static const D3D_FEATURE_LEVEL kFeatureLevels[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
  };

  UINT flags = 0;
#if defined(_DEBUG)
  flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_10_0;

  auto create_with = [&](D3D_DRIVER_TYPE driver, UINT create_flags) {
    return D3D11CreateDevice(nullptr, driver, nullptr, create_flags, kFeatureLevels,
                             ARRAYSIZE(kFeatureLevels), D3D11_SDK_VERSION,
                             device_.ReleaseAndGetAddressOf(), &obtained,
                             context_.ReleaseAndGetAddressOf());
  };

  HRESULT hr = create_with(D3D_DRIVER_TYPE_HARDWARE, flags);
  if (FAILED(hr) && flags != 0) {
    // 机器没装 D3D11 调试层时去掉 DEBUG 标志重试
    flags = 0;
    hr = create_with(D3D_DRIVER_TYPE_HARDWARE, flags);
  }

  bool hardware = true;
  if (FAILED(hr)) {
    hardware = false;
    hr = create_with(D3D_DRIVER_TYPE_WARP, 0);
  }

  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "D3D11CreateDevice 失败：" + HResultText(hr);
    }
    Reset();
    return false;
  }

  info_.feature_level = obtained;
  info_.hardware = hardware;
  info_.debug_layer = (flags & D3D11_CREATE_DEVICE_DEBUG) != 0;
  info_.adapter_name = QueryAdapterName(device_.Get());
  return true;
}

bool D3D11Device::IsDeviceLost(HRESULT hr) {
  return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET;
}

}  // namespace lapian::gfx
