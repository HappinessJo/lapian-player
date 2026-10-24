#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

struct AVFrame;
struct ID3D11Device;

namespace lapian::media {

class FrameIndex;

// 预览帧元数据。frame 所有权归调用方，取走后需 av_frame_free(&frame)。
// frame_index/pts_us 用于丢弃“目标已回退、旧目标帧还在队列里”的过期预览。
struct PreviewFrame {
  AVFrame* frame = nullptr;
  std::int64_t pts_us = 0;
  std::int64_t frame_index = -1;
  std::uint64_t generation = 0;
};

// DP-1：拖动预览诊断。hw_active 只在预览解码器真的走 D3D11VA 时为 true；
// 打开失败/格式不符/上传失败都会回退软件并记录 hw_fallback_reason。
struct PreviewDiagnostics {
  bool open_attempted = false;
  bool open_ok = false;
  bool hw_requested = false;
  bool hw_active = false;
  std::string hw_fallback_reason;
  std::string open_error;
  int seeks = 0;
  int cache_hits = 0;
  int published = 0;
  int decoder_reopens = 0;
  std::size_t cache_frames = 0;
  std::int64_t target_frame = 0;
  std::int64_t last_published_frame = -1;
};

// T2.9 拖动预览解码器：独立线程、独立解封装/解码上下文。
// 只服务拖动预览：正向拖动从最近关键帧向前解码并发布经过帧；反向拖动只发布
// 目标帧，避免“重新从关键帧向前冲”造成频闪。不参与松手/步进/输入的精确落点。
// DP-1：有共享 D3D11 设备且源 ≤1080p 时优先硬解，硬件帧不经过 sws_scale、
// 直接交给 GfxWidget；失败自动回退软件 NV12（最长边约 1280）。硬件帧不进入
// 预览缓存，避免占满 D3D11VA 表面池/显存；软件缓存仍是 32 帧。4K/HEVC 超过
// 保证档时继续走软件预览并记录 source_too_large（不设 4K 通过线）。
class PreviewDecoder {
 public:
  PreviewDecoder();
  ~PreviewDecoder();

  PreviewDecoder(const PreviewDecoder&) = delete;
  PreviewDecoder& operator=(const PreviewDecoder&) = delete;

  // 在后台打开独立解码器；index 必须在预览器生命周期内保持有效。
  // device 由调用方（MainWindow/GfxWidget）拥有，必须保证预览 Close 早于设备销毁。
  // device 为空或 prefer_hardware=false 时走软件解码。
  // source_width/source_height 用于执行“硬解只覆盖保证档 ≤1080p”策略；0 表示未知。
  void Configure(const std::string& utf8_path, int video_stream_index,
                 const FrameIndex* index, ID3D11Device* device = nullptr,
                 bool prefer_hardware = true, int source_width = 0,
                 int source_height = 0);
  void Begin(std::int64_t frame);
  void UpdateTarget(std::int64_t frame);
  // 停止发布并等待预览线程退出当前解码切片（硬件路径会等，避免和松手后的
  // 主播放精确 seek 同时使用同一个 D3D11 视频上下文）；有界等待，不阻塞 UI。
  void End();
  void Close();

  // 取走最新预览帧；没有可用的新帧时 frame == nullptr。
  // 目标回退后，比当前目标更靠后的旧帧会被丢弃，不会返回。
  PreviewFrame TakeLatestFrame();

  bool IsReady() const;
  std::string last_error() const;
  PreviewDiagnostics diagnostics() const;
  // 当前打开时借用的设备是否仍是同一个；MainWindow 在设备丢失重建后据此重开。
  bool DeviceMatches(ID3D11Device* device) const;
  // UI 上传硬件帧失败时调用：下一次预览线程循环切到软件解码并记录原因。
  void ForceSoftwareFallback(const std::string& reason);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lapian::media
