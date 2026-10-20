#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct AVFrame;

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

// T2.9 拖动预览解码器：独立线程、独立解封装/解码上下文。
// 只服务拖动预览：正向拖动从最近关键帧向前解码并发布经过帧；反向拖动只发布
// 目标帧，避免“重新从关键帧向前冲”造成频闪。不参与松手/步进/输入的精确落点。
// 预览帧为软件 NV12，并按最长边约 1280 降采样，避免 4K 全分辨率上传卡 UI。
class PreviewDecoder {
 public:
  PreviewDecoder();
  ~PreviewDecoder();

  PreviewDecoder(const PreviewDecoder&) = delete;
  PreviewDecoder& operator=(const PreviewDecoder&) = delete;

  // 在后台打开独立解码器；index 必须在预览器生命周期内保持有效。
  // 打开失败时 Begin 不会发布帧，last_error() 给出原因。
  void Configure(const std::string& utf8_path, int video_stream_index,
                 const FrameIndex* index);
  void Begin(std::int64_t frame);
  void UpdateTarget(std::int64_t frame);
  void End();
  void Close();

  // 取走最新预览帧；没有可用的新帧时 frame == nullptr。
  // 目标回退后，比当前目标更靠后的旧帧会被丢弃，不会返回。
  PreviewFrame TakeLatestFrame();

  bool IsReady() const;
  std::string last_error() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lapian::media
