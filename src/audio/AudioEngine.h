#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace lapian::online {
class GrowingSource;
}

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

  // stream_index < 0 时选第一条音频流。av_offset_seconds 是音频首包相对视频
  // 首帧的全局偏移（T3.4）；返回 false 只表示线程启动失败。
  bool Open(const std::string& utf8_path, int stream_index,
            double av_offset_seconds = 0.0);
  // T8.6：从可增长读层打开（独立 GrowingFormat/AudioDecoder，音频线程持有）。
  // 关闭前调用方应先 source->Cancel() 唤醒可能阻塞在未发布区的读。
  bool OpenGrowing(std::shared_ptr<lapian::online::GrowingSource> source,
                   const std::string& part_path_utf8, int stream_index,
                   double av_offset_seconds = 0.0);
  void Close();

  // 从媒体秒开始播放；音频时钟/精确同步留 T3.2。
  void PlayFrom(double media_seconds);
  void Pause();
  // T3.3：0.25–2x。播放中会重配重采样比并从当前媒体位置续播。
  void SetRate(double rate);

  void SetVolume(int percent);
  void SetMute(bool muted);

  // T3.2：IAudioClock 推算的当前媒体秒；不可用时返回 false（调用方回退 QPC）。
  // 只在播放中有效；暂停、设备失效、音频结束时失效。
  bool ClockSeconds(double* seconds) const;

  AudioStatus status() const;

  // T7.5 测试专用：模拟渲染设备丢失/恢复（USB 声卡拔出无法在自动化里物理复现）。
  // active=true 时音频线程走与 AUDCLNT_E_DEVICE_INVALIDATED 相同的
  // HandleDeviceLost 路径：静音 + 时钟失效（视频回退 QPC）+ 2s 重试；
  // active=false 后下一次重试重新打开设备并恢复播放。产品默认不调用。
  void SimulateDeviceLossForTest(bool active);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lapian::audio
