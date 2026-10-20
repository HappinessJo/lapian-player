#pragma once

#include <windows.h>

#include <d3d11.h>
#include <d3d11_3.h>

#include <string>

#include <wrl/client.h>

struct AVFrame;

namespace lapian::gfx {

// T1.3 视频帧渲染：NV12/P010 → BT.709 limited RGB，适应窗口，按 display matrix 旋转。
// 只负责把帧画到调用方给的 RTV；Present 由 SwapChain 负责。
// 非线程安全：只在 UI/渲染线程使用。
class VideoRenderer {
 public:
  VideoRenderer() = default;
  ~VideoRenderer();

  VideoRenderer(const VideoRenderer&) = delete;
  VideoRenderer& operator=(const VideoRenderer&) = delete;

  bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, std::string* error);
  // 帧可以是 D3D11 硬件帧或软件 NV12/P010/YUV420P；内部会复制到自有纹理。
  bool SetFrame(const AVFrame* frame, int rotation_counterclockwise_degrees, std::string* error);
  void ClearFrame();
  bool HasFrame() const { return has_frame_; }

  // 当前用于 fit 的显示尺寸（不是 D3D11 纹理分配尺寸）；回归用。
  int video_width() const { return video_width_; }
  int video_height() const { return video_height_; }

  // 清屏 + 画当前帧；rtv 来自 SwapChain。
  void Render(ID3D11RenderTargetView* render_target, UINT viewport_width, UINT viewport_height);

  void Reset();

 private:
  struct Vertex {
    float x;
    float y;
    float u;
    float v;
  };

  struct Params {
    float bit_depth = 0.0f;
    float padding[3] = {0.0f, 0.0f, 0.0f};
  };

  bool CreatePipeline(std::string* error);
  bool CreateTexture(UINT width, UINT height, DXGI_FORMAT format, std::string* error);
  bool CreateSoftwareTextures(UINT width, UINT height, int bit_depth, std::string* error);
  bool CreatePlaneViews(std::string* error);
  bool UploadSoftwareFrame(const AVFrame* frame, std::string* error);
  bool UploadHardwareFrame(const AVFrame* frame, std::string* error);
  void UpdateGeometry(UINT viewport_width, UINT viewport_height);

  ID3D11Device* device_ = nullptr;
  ID3D11DeviceContext* context_ = nullptr;
  bool initialized_ = false;

  Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer_;

  Microsoft::WRL::ComPtr<ID3D11Texture2D> video_texture_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> y_texture_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> uv_texture_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView1> y_view_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView1> uv_view_;
  DXGI_FORMAT texture_format_ = DXGI_FORMAT_UNKNOWN;
  UINT texture_width_ = 0;
  UINT texture_height_ = 0;
  bool separate_planes_ = false;

  int video_width_ = 0;
  int video_height_ = 0;
  int rotation_ = 0;
  int bit_depth_ = 8;
  bool has_frame_ = false;
};

}  // namespace lapian::gfx
