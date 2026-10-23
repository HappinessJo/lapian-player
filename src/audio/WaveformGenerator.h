#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "audio/WaveformData.h"

namespace lapian::online {
class GrowingSource;
}

namespace lapian::audio {

enum class WaveformState {
  kIdle,
  kRunning,
  kReady,
  kFailed,
  kNoAudio,
  kCancelled,
};

struct WaveformStatus {
  WaveformState state = WaveformState::kIdle;
  double progress = 0.0;
  std::string message;
  std::uint64_t revision = 0;
};

// 后台波形生成：独立 FFmpeg/AudioDecoder 上下文，不占 UI 线程、不占 WASAPI。
class WaveformGenerator {
 public:
  WaveformGenerator();
  ~WaveformGenerator();

  WaveformGenerator(const WaveformGenerator&) = delete;
  WaveformGenerator& operator=(const WaveformGenerator&) = delete;

  // 异步启动；返回 false 只表示线程启动失败。
  bool Start(const std::string& utf8_path, int stream_index,
             double av_offset_seconds);
  // T8.6：从可增长读层渐进生成；读到发布前沿会阻塞，随下载增长继续。
  // 关闭前调用方应先 source->Cancel() 唤醒阻塞读，再 Cancel()/join。
  bool StartGrowing(std::shared_ptr<lapian::online::GrowingSource> source,
                    const std::string& part_path_utf8, int stream_index,
                    double av_offset_seconds);
  void Cancel();

  WaveformStatus status() const;
  std::shared_ptr<const WaveformData> data() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lapian::audio
