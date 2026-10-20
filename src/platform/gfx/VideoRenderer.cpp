#include "platform/gfx/VideoRenderer.h"

#include <d3d11_3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
extern "C" {
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "platform/gfx/DxgiError.h"
#include "platform/gfx/VideoShaders.h"

namespace lapian::gfx {
namespace {

std::string AvErrorText(int error) {
  if (error >= 0) {
    return {};
  }
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  if (av_strerror(error, buffer, sizeof(buffer)) < 0) {
    return {};
  }
  return buffer;
}

int NormalizeRotation(int degrees) {
  int value = ((degrees % 360) + 360) % 360;
  const int snapped = static_cast<int>(std::lround(static_cast<double>(value) / 90.0)) * 90;
  return ((snapped % 360) + 360) % 360;
}

bool IsHardwareFrame(const AVFrame* frame) {
  return frame != nullptr &&
         (frame->format == AV_PIX_FMT_D3D11 || frame->hw_frames_ctx != nullptr);
}

int FrameBitDepth(const AVFrame* frame) {
  if (frame == nullptr) {
    return 8;
  }
  const AVPixFmtDescriptor* description =
      av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
  if (description != nullptr && description->comp[0].depth > 0) {
    return description->comp[0].depth;
  }
  return 8;
}

bool IsSupportedTextureFormat(DXGI_FORMAT format) {
  return format == DXGI_FORMAT_NV12 || format == DXGI_FORMAT_P010;
}

}  // namespace

VideoRenderer::~VideoRenderer() { Reset(); }

bool VideoRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
                               std::string* error) {
  if (device == nullptr || context == nullptr) {
    if (error != nullptr) {
      *error = "VideoRenderer 初始化失败：设备或上下文为空";
    }
    return false;
  }
  Reset();
  device_ = device;
  context_ = context;
  if (!CreatePipeline(error)) {
    Reset();
    return false;
  }
  initialized_ = true;
  return true;
}

bool VideoRenderer::CreatePipeline(std::string* error) {
  HRESULT hr = device_->CreateVertexShader(shaders::kVideoVS, shaders::kVideoVSSize, nullptr,
                                           vertex_shader_.GetAddressOf());
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "CreateVertexShader 失败：" + HResultText(hr);
    }
    return false;
  }
  hr = device_->CreatePixelShader(shaders::kVideoPS, shaders::kVideoPSSize, nullptr,
                                  pixel_shader_.GetAddressOf());
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "CreatePixelShader 失败：" + HResultText(hr);
    }
    return false;
  }

  const D3D11_INPUT_ELEMENT_DESC elements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  hr = device_->CreateInputLayout(elements, ARRAYSIZE(elements), shaders::kVideoVS,
                                  shaders::kVideoVSSize, input_layout_.GetAddressOf());
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "CreateInputLayout 失败：" + HResultText(hr);
    }
    return false;
  }

  D3D11_SAMPLER_DESC sampler_desc{};
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampler_desc.MinLOD = 0.0f;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
  hr = device_->CreateSamplerState(&sampler_desc, sampler_.GetAddressOf());
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "CreateSamplerState 失败：" + HResultText(hr);
    }
    return false;
  }

  D3D11_BUFFER_DESC constant_desc{};
  constant_desc.ByteWidth = sizeof(Params);
  constant_desc.Usage = D3D11_USAGE_DEFAULT;
  constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  hr = device_->CreateBuffer(&constant_desc, nullptr, constant_buffer_.GetAddressOf());
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "CreateBuffer(constant) 失败：" + HResultText(hr);
    }
    return false;
  }

  D3D11_BUFFER_DESC vertex_desc{};
  vertex_desc.ByteWidth = sizeof(Vertex) * 4;
  vertex_desc.Usage = D3D11_USAGE_DYNAMIC;
  vertex_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  vertex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = device_->CreateBuffer(&vertex_desc, nullptr, vertex_buffer_.GetAddressOf());
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "CreateBuffer(vertex) 失败：" + HResultText(hr);
    }
    return false;
  }
  return true;
}

bool VideoRenderer::CreateTexture(UINT width, UINT height, DXGI_FORMAT format,
                                  std::string* error) {
  if (!initialized_ && device_ == nullptr) {
    if (error != nullptr) {
      *error = "VideoRenderer 未初始化";
    }
    return false;
  }
  if (width == 0 || height == 0 || !IsSupportedTextureFormat(format)) {
    if (error != nullptr) {
      *error = "不支持的视频纹理格式/尺寸";
    }
    return false;
  }
  if (video_texture_ && texture_width_ == width && texture_height_ == height &&
      texture_format_ == format) {
    return true;
  }

  y_view_.Reset();
  uv_view_.Reset();
  video_texture_.Reset();
  y_texture_.Reset();
  uv_texture_.Reset();
  separate_planes_ = false;

  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  HRESULT hr = device_->CreateTexture2D(&desc, nullptr, video_texture_.GetAddressOf());
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "CreateTexture2D(NV12/P010) 失败：" + HResultText(hr);
    }
    return false;
  }

  texture_format_ = format;
  texture_width_ = width;
  texture_height_ = height;
  return CreatePlaneViews(error);
}

bool VideoRenderer::CreateSoftwareTextures(UINT width, UINT height, int bit_depth,
                                           std::string* error) {
  if (width == 0 || height == 0 || width % 2 != 0 || height % 2 != 0) {
    if (error != nullptr) {
      *error = "软件帧尺寸无效";
    }
    return false;
  }
  if (separate_planes_ && texture_width_ == width && texture_height_ == height &&
      bit_depth_ == bit_depth && y_texture_ && uv_texture_) {
    return true;
  }

  Microsoft::WRL::ComPtr<ID3D11Device3> device3;
  HRESULT hr = device_->QueryInterface(IID_PPV_ARGS(device3.GetAddressOf()));
  if (FAILED(hr) || !device3) {
    if (error != nullptr) {
      *error = "创建软件帧纹理失败：拿不到 ID3D11Device3";
    }
    return false;
  }

  y_view_.Reset();
  uv_view_.Reset();
  y_texture_.Reset();
  uv_texture_.Reset();
  video_texture_.Reset();
  separate_planes_ = false;

  const bool ten_bit = bit_depth > 8;
  D3D11_TEXTURE2D_DESC y_desc{};
  y_desc.Width = width;
  y_desc.Height = height;
  y_desc.MipLevels = 1;
  y_desc.ArraySize = 1;
  y_desc.Format = ten_bit ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
  y_desc.SampleDesc.Count = 1;
  y_desc.Usage = D3D11_USAGE_DEFAULT;
  y_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  hr = device_->CreateTexture2D(&y_desc, nullptr, y_texture_.GetAddressOf());
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "CreateTexture2D(Y) 失败：" + HResultText(hr);
    }
    return false;
  }

  D3D11_TEXTURE2D_DESC uv_desc = y_desc;
  uv_desc.Width = width / 2;
  uv_desc.Height = height / 2;
  uv_desc.Format = ten_bit ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
  hr = device_->CreateTexture2D(&uv_desc, nullptr, uv_texture_.GetAddressOf());
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "CreateTexture2D(UV) 失败：" + HResultText(hr);
    }
    return false;
  }

  auto create_view = [&](ID3D11Texture2D* texture, DXGI_FORMAT format,
                         Microsoft::WRL::ComPtr<ID3D11ShaderResourceView1>* view) {
    D3D11_SHADER_RESOURCE_VIEW_DESC1 desc{};
    desc.Format = format;
    desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MostDetailedMip = 0;
    desc.Texture2D.MipLevels = 1;
    desc.Texture2D.PlaneSlice = 0;
    return device3->CreateShaderResourceView1(texture, &desc, view->GetAddressOf());
  };

  hr = create_view(y_texture_.Get(), y_desc.Format, &y_view_);
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "CreateShaderResourceView1(Y) 失败：" + HResultText(hr);
    }
    return false;
  }
  hr = create_view(uv_texture_.Get(), uv_desc.Format, &uv_view_);
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "CreateShaderResourceView1(UV) 失败：" + HResultText(hr);
    }
    return false;
  }

  separate_planes_ = true;
  texture_format_ = DXGI_FORMAT_UNKNOWN;
  texture_width_ = width;
  texture_height_ = height;
  bit_depth_ = bit_depth;
  return true;
}

bool VideoRenderer::CreatePlaneViews(std::string* error) {
  if (!video_texture_) {
    if (error != nullptr) {
      *error = "创建平面 SRV 失败：纹理为空";
    }
    return false;
  }

  Microsoft::WRL::ComPtr<ID3D11Device3> device3;
  HRESULT hr = device_->QueryInterface(IID_PPV_ARGS(device3.GetAddressOf()));
  if (FAILED(hr) || !device3) {
    if (error != nullptr) {
      *error = "创建平面 SRV 失败：拿不到 ID3D11Device3";
    }
    return false;
  }

  const bool ten_bit = texture_format_ == DXGI_FORMAT_P010;
  D3D11_SHADER_RESOURCE_VIEW_DESC1 y_desc{};
  y_desc.Format = ten_bit ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
  y_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  y_desc.Texture2D.MostDetailedMip = 0;
  y_desc.Texture2D.MipLevels = 1;
  y_desc.Texture2D.PlaneSlice = 0;

  D3D11_SHADER_RESOURCE_VIEW_DESC1 uv_desc{};
  uv_desc.Format = ten_bit ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
  uv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  uv_desc.Texture2D.MostDetailedMip = 0;
  uv_desc.Texture2D.MipLevels = 1;
  uv_desc.Texture2D.PlaneSlice = 1;

  hr = device3->CreateShaderResourceView1(video_texture_.Get(), &y_desc,
                                          y_view_.GetAddressOf());
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "CreateShaderResourceView1(Y) 失败：" + HResultText(hr);
    }
    return false;
  }
  hr = device3->CreateShaderResourceView1(video_texture_.Get(), &uv_desc,
                                          uv_view_.GetAddressOf());
  if (FAILED(hr)) {
    if (error != nullptr) {
      *error = "CreateShaderResourceView1(UV) 失败：" + HResultText(hr);
    }
    return false;
  }
  return true;
}

bool VideoRenderer::UploadSoftwareFrame(const AVFrame* frame, std::string* error) {
  if (frame == nullptr) {
    if (error != nullptr) {
      *error = "软件帧为空";
    }
    return false;
  }

  const int bit_depth = FrameBitDepth(frame);
  const AVPixelFormat destination_format =
      bit_depth > 8 ? AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12;
  const AVFrame* source = frame;
  AVFrame* converted = nullptr;
  SwsContext* swscale = nullptr;

  if (static_cast<AVPixelFormat>(frame->format) != destination_format) {
    converted = av_frame_alloc();
    if (converted == nullptr) {
      if (error != nullptr) {
        *error = "分配转换帧失败";
      }
      return false;
    }
    converted->format = destination_format;
    converted->width = frame->width;
    converted->height = frame->height;
    if (av_frame_get_buffer(converted, 32) < 0) {
      av_frame_free(&converted);
      if (error != nullptr) {
        *error = "分配转换帧缓冲失败";
      }
      return false;
    }
    swscale = sws_getCachedContext(nullptr, frame->width, frame->height,
                                   static_cast<AVPixelFormat>(frame->format), frame->width,
                                   frame->height, destination_format, SWS_BILINEAR, nullptr,
                                   nullptr, nullptr);
    if (swscale == nullptr) {
      av_frame_free(&converted);
      if (error != nullptr) {
        *error = "创建 swscale 上下文失败";
      }
      return false;
    }
    const int scaled = sws_scale(swscale, frame->data, frame->linesize, 0, frame->height,
                                 converted->data, converted->linesize);
    sws_freeContext(swscale);
    if (scaled <= 0) {
      av_frame_free(&converted);
      if (error != nullptr) {
        *error = "YUV 转换失败";
      }
      return false;
    }
    source = converted;
  }

  if (!CreateSoftwareTextures(static_cast<UINT>(source->width),
                              static_cast<UINT>(source->height), bit_depth, error)) {
    av_frame_free(&converted);
    return false;
  }

  // 软件帧用独立的 Y/UV 纹理，避免 P010/NV12 平面 Subresource 在不同驱动上的差异。
  context_->UpdateSubresource(y_texture_.Get(), 0, nullptr, source->data[0],
                              static_cast<UINT>(source->linesize[0]), 0);
  context_->UpdateSubresource(uv_texture_.Get(), 0, nullptr, source->data[1],
                              static_cast<UINT>(source->linesize[1]), 0);

  video_width_ = source->width;
  video_height_ = source->height;
  bit_depth_ = bit_depth;
  av_frame_free(&converted);
  return true;
}

bool VideoRenderer::UploadHardwareFrame(const AVFrame* frame, std::string* error) {
  if (frame == nullptr || frame->data[0] == nullptr) {
    if (error != nullptr) {
      *error = "硬件帧为空";
    }
    return false;
  }

  auto* source = reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
  Microsoft::WRL::ComPtr<ID3D11Device> source_device;
  source->GetDevice(source_device.GetAddressOf());
  if (!source_device || source_device.Get() != device_) {
    if (error != nullptr) {
      *error = "硬件帧来自不同的 D3D11 设备";
    }
    return false;
  }
  const auto subresource = static_cast<UINT>(reinterpret_cast<std::uintptr_t>(frame->data[1]));
  D3D11_TEXTURE2D_DESC source_desc{};
  source->GetDesc(&source_desc);
  if (!IsSupportedTextureFormat(source_desc.Format)) {
    if (error != nullptr) {
      *error = "硬件帧不是 NV12/P010";
    }
    return false;
  }

  // T7.2.1：D3D11VA 纹理可能按编码尺寸分配（1080p 常见 1920x1088），
  // 而 AVFrame 的 width/height 是显示尺寸（1920x1080）。fit 和纹理都只取
  // 有效显示区域，避免硬解帧与缓存软件帧的画幅不一致。
  const UINT display_width =
      frame->width > 0 ? static_cast<UINT>(frame->width) : source_desc.Width;
  const UINT display_height =
      frame->height > 0 ? static_cast<UINT>(frame->height) : source_desc.Height;
  const UINT active_width = std::min(display_width, source_desc.Width);
  const UINT active_height = std::min(display_height, source_desc.Height);
  if (active_width == 0 || active_height == 0) {
    if (error != nullptr) {
      *error = "硬件帧显示尺寸无效";
    }
    return false;
  }
  if (!CreateTexture(active_width, active_height, source_desc.Format, error)) {
    return false;
  }

  D3D11_BOX active_box{};
  active_box.left = 0;
  active_box.top = 0;
  active_box.front = 0;
  active_box.right = active_width;
  active_box.bottom = active_height;
  active_box.back = 1;
  context_->CopySubresourceRegion(video_texture_.Get(), 0, 0, 0, 0, source, subresource,
                                  &active_box);
  video_width_ = static_cast<int>(active_width);
  video_height_ = static_cast<int>(active_height);
  bit_depth_ = source_desc.Format == DXGI_FORMAT_P010 ? 10 : 8;
  return true;
}

bool VideoRenderer::SetFrame(const AVFrame* frame, int rotation_counterclockwise_degrees,
                             std::string* error) {
  if (!initialized_) {
    if (error != nullptr) {
      *error = "VideoRenderer 未初始化";
    }
    return false;
  }
  if (frame == nullptr) {
    ClearFrame();
    return true;
  }

  const int rotation = NormalizeRotation(rotation_counterclockwise_degrees);
  bool uploaded = false;
  if (IsHardwareFrame(frame)) {
    uploaded = UploadHardwareFrame(frame, error);
    if (!uploaded) {
      // 硬解纹理不可用时，转成 CPU 帧再上传。
      AVFrame* software = av_frame_alloc();
      if (software != nullptr &&
          av_hwframe_transfer_data(software, frame, 0) >= 0) {
        uploaded = UploadSoftwareFrame(software, error);
      }
      av_frame_free(&software);
    }
  } else {
    uploaded = UploadSoftwareFrame(frame, error);
  }

  if (!uploaded) {
    return false;
  }
  rotation_ = rotation;
  has_frame_ = true;
  return true;
}

void VideoRenderer::ClearFrame() {
  has_frame_ = false;
  video_width_ = 0;
  video_height_ = 0;
}

void VideoRenderer::UpdateGeometry(UINT viewport_width, UINT viewport_height) {
  if (viewport_width == 0 || viewport_height == 0 || video_width_ <= 0 || video_height_ <= 0) {
    return;
  }

  const int rotation = NormalizeRotation(rotation_);
  const bool swapped = rotation == 90 || rotation == 270;
  const float rotated_width = static_cast<float>(swapped ? video_height_ : video_width_);
  const float rotated_height = static_cast<float>(swapped ? video_width_ : video_height_);
  const float scale = std::min(static_cast<float>(viewport_width) / rotated_width,
                               static_cast<float>(viewport_height) / rotated_height);
  const float half_width = rotated_width * scale / static_cast<float>(viewport_width);
  const float half_height = rotated_height * scale / static_cast<float>(viewport_height);

  Vertex vertices[4]{};
  switch (rotation) {
    case 90:
      vertices[0] = {-half_width, half_height, 1.0f, 0.0f};
      vertices[1] = {half_width, half_height, 1.0f, 1.0f};
      vertices[2] = {-half_width, -half_height, 0.0f, 0.0f};
      vertices[3] = {half_width, -half_height, 0.0f, 1.0f};
      break;
    case 180:
      vertices[0] = {-half_width, half_height, 1.0f, 1.0f};
      vertices[1] = {half_width, half_height, 0.0f, 1.0f};
      vertices[2] = {-half_width, -half_height, 1.0f, 0.0f};
      vertices[3] = {half_width, -half_height, 0.0f, 0.0f};
      break;
    case 270:
      vertices[0] = {-half_width, half_height, 0.0f, 1.0f};
      vertices[1] = {half_width, half_height, 0.0f, 0.0f};
      vertices[2] = {-half_width, -half_height, 1.0f, 1.0f};
      vertices[3] = {half_width, -half_height, 1.0f, 0.0f};
      break;
    default:
      vertices[0] = {-half_width, half_height, 0.0f, 0.0f};
      vertices[1] = {half_width, half_height, 1.0f, 0.0f};
      vertices[2] = {-half_width, -half_height, 0.0f, 1.0f};
      vertices[3] = {half_width, -half_height, 1.0f, 1.0f};
      break;
  }

  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (SUCCEEDED(context_->Map(vertex_buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    std::memcpy(mapped.pData, vertices, sizeof(vertices));
    context_->Unmap(vertex_buffer_.Get(), 0);
  }
}

void VideoRenderer::Render(ID3D11RenderTargetView* render_target, UINT viewport_width,
                           UINT viewport_height) {
  if (!initialized_ || render_target == nullptr) {
    return;
  }

  static const float kBackground[4] = {0.09f, 0.09f, 0.10f, 1.0f};
  context_->ClearRenderTargetView(render_target, kBackground);
  if (!has_frame_ || !y_view_ || !uv_view_) {
    return;
  }

  UpdateGeometry(viewport_width, viewport_height);

  D3D11_VIEWPORT viewport{};
  viewport.Width = static_cast<float>(viewport_width);
  viewport.Height = static_cast<float>(viewport_height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  context_->RSSetViewports(1, &viewport);
  context_->OMSetRenderTargets(1, &render_target, nullptr);

  const UINT stride = sizeof(Vertex);
  const UINT offset = 0;
  ID3D11Buffer* vertex_buffers[] = {vertex_buffer_.Get()};
  context_->IASetVertexBuffers(0, 1, vertex_buffers, &stride, &offset);
  context_->IASetInputLayout(input_layout_.Get());
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
  context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);

  ID3D11ShaderResourceView* views[] = {y_view_.Get(), uv_view_.Get()};
  context_->PSSetShaderResources(0, 2, views);
  context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());

  Params params;
  params.bit_depth = bit_depth_ > 8 ? 1.0f : 0.0f;
  context_->UpdateSubresource(constant_buffer_.Get(), 0, nullptr, &params, 0, 0);
  context_->VSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
  context_->PSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());

  context_->Draw(4, 0);

  ID3D11ShaderResourceView* null_views[] = {nullptr, nullptr};
  context_->PSSetShaderResources(0, 2, null_views);
}

void VideoRenderer::Reset() {
  has_frame_ = false;
  initialized_ = false;
  y_view_.Reset();
  uv_view_.Reset();
  video_texture_.Reset();
  y_texture_.Reset();
  uv_texture_.Reset();
  separate_planes_ = false;
  vertex_buffer_.Reset();
  constant_buffer_.Reset();
  sampler_.Reset();
  input_layout_.Reset();
  pixel_shader_.Reset();
  vertex_shader_.Reset();
  device_ = nullptr;
  context_ = nullptr;
  texture_format_ = DXGI_FORMAT_UNKNOWN;
  texture_width_ = 0;
  texture_height_ = 0;
  video_width_ = 0;
  video_height_ = 0;
  rotation_ = 0;
  bit_depth_ = 8;
}

}  // namespace lapian::gfx
