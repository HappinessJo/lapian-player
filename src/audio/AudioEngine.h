#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace lapian::audio {

enum class AudioState {
  kClosed,
  kOpening,
  kPlaying,
  kPaused,
  kSilent,
  kError,
};

struct AudioStatus {
  AudioState state = AudioState::kClosed;
  std::string message;
  bool has_audio = false;
  bool muted = false;
  int volume = 100;
  double position_seconds = 0.0;
  std::uint64_t underruns = 0;
  std::uint64_t revision = 0;
};

// 一条视频的音频输出：独立工作线程解码 + WASAPI 渲染。
// 打开是异步的；UI 通过 status() 轮询状态，不阻塞事件循环。
class AudioEngine {
 public:
  AudioEngine();
  ~AudioEngine();

  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  // stream_index < 0 时选第一条音频流。返回 false 只表示线程启动失败。
  bool Open(const std::string& utf8_path, int stream_index);
  void Close();

  // 从媒体秒开始播放；音频时钟/精确同步留 T3.2。
  void PlayFrom(double media_seconds);
  void Pause();

  void SetVolume(int percent);
  void SetMute(bool muted);

  // T3.2：IAudioClock 推算的当前媒体秒；不可用时返回 false（调用方回退 QPC）。
  // 只在播放中有效；暂停、设备失效、音频结束时失效。
  bool ClockSeconds(double* seconds) const;

  AudioStatus status() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lapian::audio
