#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "audio/AudioFormat.h"

namespace lapian::audio {

struct WasapiDeviceInfo {
  AudioFormat format;
  std::uint32_t buffer_frames = 0;
  std::string device_name;
};

// WASAPI shared mode 默认渲染端点。必须在已 CoInitializeEx 的线程内使用；
// 不拥有线程/COM 生命周期。
class WasapiRenderer {
 public:
  WasapiRenderer();
  ~WasapiRenderer();

  WasapiRenderer(const WasapiRenderer&) = delete;
  WasapiRenderer& operator=(const WasapiRenderer&) = delete;

  bool Initialize(std::string* error);
  void Shutdown();
  bool IsOpen() const;

  const WasapiDeviceInfo& device_info() const;

  bool Start(std::string* error);
  void Stop();
  void Reset();

  // 等待渲染事件；超时返回 false。设备失效通过 IsDeviceLost() 暴露。
  bool WaitForEvent(unsigned timeout_ms);

  bool Padding(std::uint32_t* frames, std::string* error) const;
  // frames 必须不超过当前可写空间；调用方用 Padding 计算。
  bool WriteFrames(const float* samples, std::size_t frames, std::string* error);

  bool IsDeviceLost() const;
  bool PositionFrames(std::uint64_t* frames) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lapian::audio
