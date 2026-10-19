#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct AVFrame;

namespace lapian::media {

class FrameIndex;

// T2.9 拖动预览解码器：独立线程、独立解封装/解码上下文。
// 只服务拖动预览：从目标之前最近关键帧向前解码，发布“目前最新的帧”；
// 不参与松手/步进/输入的精确落点（那些仍走 MediaSession 的精确解码器）。
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

  // 取走最新预览帧；返回的 AVFrame 由调用方 av_frame_free。
  // 没有新帧时返回 nullptr；End/Close 后不会返回旧帧。
  AVFrame* TakeLatestFrame();

  bool IsReady() const;
  std::string last_error() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lapian::media
