#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "media/DragStrategy.h"

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
  // DP-2：精确帧/关键帧粗帧/缓存命中/硬件帧必须可区分；粗帧永远不是最终帧。
  bool coarse = false;
  bool from_cache = false;
  bool hardware = false;
};

// DP-1：拖动预览诊断。hw_active 只在预览解码器真的走 D3D11VA 时为 true；
// 打开失败/格式不符/上传失败都会回退软件并记录 hw_fallback_reason。
// DP-2：增加有界缓存/缩略图/预取/粗帧与分段耗时诊断。
struct PreviewDiagnostics {
  bool open_attempted = false;
  bool open_ok = false;
  bool hw_requested = false;
  bool hw_active = false;
  std::string hw_fallback_reason;
  std::string open_error;
  int seeks = 0;
  int cache_hits = 0;
  int cache_misses = 0;
  int cache_requests = 0;
  int cache_evictions = 0;
  int published = 0;
  int decoder_reopens = 0;
  std::size_t cache_frames = 0;
  std::size_t cache_bytes = 0;
  std::size_t cache_peak_bytes = 0;
  std::size_t thumbnail_frames = 0;
  std::size_t thumbnail_bytes = 0;
  std::size_t thumbnail_peak_bytes = 0;
  int coarse_hits = 0;
  int coarse_requests = 0;
  int coarse_published = 0;
  int prefetched_frames = 0;
  int readbacks = 0;
  int decoded_frames = 0;
  double seek_ms_avg = 0.0;
  double decode_ms_avg = 0.0;
  double readback_ms_avg = 0.0;
  double first_coarse_ms = -1.0;
  double last_coarse_ms = -1.0;
  bool cache_enabled = true;
  bool prefetch_enabled = true;
  bool thumbnails_enabled = true;
  // DP-3：调用方（MainWindow）每次目标更新写入的交互策略。Exact 模式关闭
  // 粗帧替换；FarJump 模式保证“先粗后精”（粗帧呈现后下一 tick 再交给精确帧）。
  DragMode drag_mode = DragMode::Exact;
  std::int64_t target_frame = 0;
  std::int64_t last_published_frame = -1;
};

// T2.9 拖动预览解码器：独立线程、独立解封装/解码上下文。
// 只服务拖动预览：正向拖动从最近关键帧向前解码并发布经过帧；反向拖动只发布
// 目标帧，避免“重新从关键帧向前冲”造成频闪。不参与松手/步进/输入的精确落点。
// DP-1：有共享 D3D11 设备且源 ≤1080p 时优先硬解，硬件帧不经过 sws_scale、
// 直接交给 GfxWidget；失败自动回退软件 NV12（最长边约 1280）。4K/HEVC 超过
// 保证档时继续走软件预览并记录 source_too_large（不设 4K 通过线）。
// DP-2：32 帧热缓存保留；新增有界环缓存（192MiB）与粗帧缓存（64MiB）。
// 反向目标先查精确缓存；局部/慢速反向由独立低优先级预取线程解“上一 GOP”的
// 软件 NV12 入环缓存；远跳先发粗帧（软件 320×180，硬件关键帧全分辨率）再精确。
// 硬件帧仍然不直接缓存，也不占 D3D11VA 表面池。
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
  // DP-3：设置本次拖动的交互策略（Exact/Fast/Reverse/FarJump）。必须在
  // Begin/UpdateTarget 之前调用；只影响粗帧替换与远跳“先粗后精”，不改变
  // 缓存预算、硬件帧不缓存、松手精确等 DP-1/DP-2 边界。
  void SetDragMode(DragMode mode);
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
