#include "audio/AudioEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <QString>

#ifdef _WIN32
#include <windows.h>
#include <avrt.h>
#include <objbase.h>
#endif

#include "audio/AudioDecoder.h"
#include "audio/AudioFormat.h"
#include "audio/TimeStretch.h"
#include "audio/WasapiRenderer.h"
#include "online/GrowingSource.h"

namespace lapian::audio {
namespace {

constexpr std::size_t kDecodeChunkFrames = 1024;
constexpr int kTargetBufferMs = 80;
constexpr int kStartPrefillMs = 30;
constexpr int kMaxBufferMs = 150;
constexpr auto kDeviceRetryInterval = std::chrono::seconds(2);

bool AudioLogEnabled() {
  static const bool enabled = std::getenv("LAPIAN_AUDIO_LOG") != nullptr;
  return enabled;
}

void AudioLog(const char* format, ...) {
  if (!AudioLogEnabled()) {
    return;
  }
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
  std::fputc('\n', stderr);
  std::fflush(stderr);
}

// Hidden diagnostic/rollback switch: LAPIAN_PITCH_PRESERVE=0 forces the
// legacy resampling path (pitch changes with speed).
bool PitchPreserveFromEnv() {
  const char* value = std::getenv("LAPIAN_PITCH_PRESERVE");
  return value == nullptr || std::strcmp(value, "0") != 0;
}

}  // namespace

struct AudioEngine::Impl {
  std::string path;
  int stream_index = -1;
  double av_offset = 0.0;
  bool growing = false;
  std::shared_ptr<lapian::online::GrowingSource> growing_source;
  std::string growing_part_path;
  std::unique_ptr<lapian::online::GrowingFormat> growing_format;

  std::thread thread;
  std::mutex mutex;
  std::condition_variable cv;
  bool quit = false;
  bool open_requested = false;
  double play_target = 0.0;
  std::atomic<bool> want_play{false};
  std::atomic<std::uint64_t> seek_serial{0};
  std::atomic<double> requested_rate{1.0};
  std::atomic<std::uint64_t> rate_serial{0};
  std::atomic<int> volume{100};
  std::atomic<bool> muted{false};
  std::atomic<double> clock_seconds{0.0};
  std::atomic<bool> clock_valid{false};
  std::atomic<std::int64_t> last_pump_ns{0};
  std::atomic<bool> simulate_device_loss{false};

  mutable std::mutex status_mutex;
  AudioStatus status;

  AudioDecoder decoder;
  WasapiRenderer renderer;
  AudioRingBuffer ring;
  TimeStretch stretch;
  bool stretch_active = false;
  bool stretch_flushed = false;
  bool pitch_preserve = PitchPreserveFromEnv();
  std::vector<float> stretch_scratch;
  std::vector<float> scratch;
  std::vector<float> last_frame;
  std::size_t fade_in_remaining = 0;

  bool ready = false;
  bool playing = false;
  bool decoder_eof = false;
  bool device_retry = false;
  std::chrono::steady_clock::time_point next_retry;
  std::uint64_t handled_serial = 0;
  std::uint64_t handled_rate_serial = 0;
  double rate = 1.0;
  double anchor_seconds = 0.0;
  double position_seconds = 0.0;
  std::uint64_t frames_since_start = 0;
  std::uint64_t underruns = 0;
  std::uint64_t clock_base_frames = 0;
  std::uint64_t clock_frequency = 0;
  double clock_anchor_seconds = 0.0;
  int clock_failures = 0;
  std::chrono::steady_clock::time_point last_position_publish;

  int sample_rate() const { return renderer.device_info().format.sample_rate; }
  int channels() const { return renderer.device_info().format.channels; }

  // 0.5-2x: keep the device rate and time-stretch (pitch preserved).
  // 1x / 0.25x / fallback: legacy resampling device_rate / rate (pitch follows
  // speed). 0.25x intentionally keeps the old behavior.
  bool UseStretch() const {
    // 1x is a pure bypass: no SoundTouch work, no extra CPU.
    return pitch_preserve && rate >= 0.5 - 1e-9 &&
           std::abs(rate - 1.0) > 1e-6;
  }

  int OutputSampleRate() const {
    const int device_rate = sample_rate();
    if (device_rate <= 0) {
      return 0;
    }
    if (UseStretch()) {
      return device_rate;
    }
    const double divisor = rate > 0.0 ? rate : 1.0;
    return std::max(1, static_cast<int>(std::lround(
                           static_cast<double>(device_rate) / divisor)));
  }

  void Publish(AudioState state, std::string message) {
    AudioLog("audio state=%d message=%s", static_cast<int>(state), message.c_str());
    std::lock_guard<std::mutex> lock(status_mutex);
    status.state = state;
    status.message = std::move(message);
    status.has_audio = true;
    status.volume = volume.load();
    status.muted = muted.load();
    status.position_seconds = position_seconds;
    status.underruns = underruns;
    ++status.revision;
    last_position_publish = std::chrono::steady_clock::now();
  }

  void PublishPosition() {
    const auto now = std::chrono::steady_clock::now();
    if (now - last_position_publish < std::chrono::milliseconds(200)) {
      return;
    }
    last_position_publish = now;
    std::lock_guard<std::mutex> lock(status_mutex);
    status.position_seconds = position_seconds;
    status.underruns = underruns;
    ++status.revision;
  }

  bool OpenDeviceAndDecoder(std::string* error, bool* decoder_failed) {
    *decoder_failed = false;
    decoder.Close();
    growing_format.reset();
    renderer.Shutdown();
    if (!renderer.Initialize(error)) {
      return false;
    }
    const AudioFormat format = renderer.device_info().format;
    if (format.sample_rate <= 0 || format.channels <= 0) {
      *error = "音频设备格式无效";
      renderer.Shutdown();
      return false;
    }
    rate = requested_rate.load();
    stretch_active = UseStretch();
    AudioFormat output = format;
    output.sample_rate = OutputSampleRate();
    if (stretch_active) {
      std::string stretch_error;
      if (!stretch.Reset(format.sample_rate, format.channels, rate,
                         &stretch_error)) {
        AudioLog("audio pitch-preserve unavailable: %s; using legacy resampling",
                 stretch_error.c_str());
        pitch_preserve = false;
        stretch_active = false;
        output.sample_rate = OutputSampleRate();
      }
    }
    if (!stretch_active) {
      stretch.Clear();
    }
    stretch_scratch.resize(kDecodeChunkFrames * 4 *
                           static_cast<std::size_t>(format.channels));
    AudioLog("audio device: %d Hz %d ch %s buffer=%u clock_freq=%llu pitch_preserve=%d stretch=%d", format.sample_rate,
             format.channels,
             format.encoding == SampleEncoding::kFloat32 ? "float32" : "pcm16",
             renderer.device_info().buffer_frames,
             static_cast<unsigned long long>(renderer.device_info().clock_frequency),
             pitch_preserve ? 1 : 0, stretch_active ? 1 : 0);

    ring.Reset(static_cast<std::size_t>(format.sample_rate) *
                   static_cast<std::size_t>(kMaxBufferMs) / 1000,
               format.channels);
    scratch.resize(static_cast<std::size_t>(renderer.device_info().buffer_frames) *
                   static_cast<std::size_t>(format.channels));

    bool decoder_opened = false;
    if (growing) {
      growing_format = std::make_unique<lapian::online::GrowingFormat>();
      QString format_error;
      if (!growing_source ||
          !growing_format->Open(growing_source,
                                QString::fromUtf8(growing_part_path.c_str()),
                                &format_error, 0)) {
        *error = format_error.isEmpty() ? "无法打开可增长音频上下文"
                                        : format_error.toUtf8().constData();
      } else {
        decoder_opened = decoder.OpenWithFormat(
            growing_format->context(), stream_index, output, error);
      }
    } else {
      decoder_opened = decoder.Open(path, stream_index, output, error);
    }
    if (!decoder_opened) {
      *decoder_failed = true;
      growing_format.reset();
      renderer.Shutdown();
      return false;
    }
    AudioLog("audio decoder: %s %d Hz %d ch -> %d Hz", decoder.info().codec.c_str(),
             decoder.info().source_sample_rate, decoder.info().source_channels,
             output.sample_rate);
    return true;
  }

  void DrainStretchToRing() {
    if (!stretch_active) {
      return;
    }
    while (ring.free_frames() > 0) {
      const std::size_t want =
          std::min<std::size_t>(ring.free_frames(), kDecodeChunkFrames * 4);
      const std::size_t got = stretch.Receive(stretch_scratch.data(), want);
      if (got == 0) {
        break;
      }
      ring.Push(stretch_scratch.data(), got);
    }
  }

  bool FillTo(std::size_t target_frames, std::string* error) {
    if (!decoder.IsOpen()) {
      *error = "音频解码器未打开";
      return false;
    }
    if (stretch_active) {
      DrainStretchToRing();
      while (ring.size_frames() < target_frames &&
             ring.free_frames() >= kDecodeChunkFrames) {
        if (decoder_eof) {
          if (!stretch_flushed) {
            stretch.Flush();
            stretch_flushed = true;
          }
          const std::size_t before = ring.size_frames();
          DrainStretchToRing();
          if (ring.size_frames() == before) {
            break;
          }
          continue;
        }
        AudioChunk chunk;
        const AudioDecodeResult result =
            decoder.DecodeChunk(kDecodeChunkFrames, &chunk, error);
        if (result == AudioDecodeResult::kEnd) {
          decoder_eof = true;
          continue;
        }
        if (result == AudioDecodeResult::kError) {
          return false;
        }
        if (chunk.frames > 0) {
          stretch.Feed(chunk.samples.data(), chunk.frames);
        }
        DrainStretchToRing();
      }
      return true;
    }
    while (!decoder_eof && ring.size_frames() < target_frames &&
           ring.free_frames() >= kDecodeChunkFrames) {
      AudioChunk chunk;
      const AudioDecodeResult result =
          decoder.DecodeChunk(kDecodeChunkFrames, &chunk, error);
      if (result == AudioDecodeResult::kEnd) {
        decoder_eof = true;
        break;
      }
      if (result == AudioDecodeResult::kError) {
        return false;
      }
      if (chunk.frames > 0) {
        const std::size_t accepted =
            ring.Push(chunk.samples.data(), chunk.frames);
        if (accepted == 0) {
          break;
        }
      }
    }
    return true;
  }

  void ApplyGain(float* samples, std::size_t sample_count) {
    const float gain = muted.load() ? 0.0f : VolumeToGain(volume.load());
    if (gain == 1.0f) {
      return;
    }
    if (gain == 0.0f) {
      std::fill(samples, samples + sample_count, 0.0f);
      return;
    }
    for (std::size_t i = 0; i < sample_count; ++i) {
      samples[i] *= gain;
    }
  }

  void UpdateClock() {
    if (!playing) {
      return;
    }
    std::uint64_t position = 0;
    if (!renderer.ClockPosition(&position)) {
      if (clock_valid.load()) {
        ++clock_failures;
        if (clock_failures >= 3) {
          clock_valid.store(false);
        }
      }
      return;
    }
    clock_failures = 0;

    // 第一次成功查询只用来建立基准：音频设备位置可能是流内位置，也可能是
    // 设备累计位置；不能假定 Start 后一定为 0。
    if (!clock_valid.load()) {
      clock_base_frames = position;
      clock_anchor_seconds = clock_seconds.load();
      clock_valid.store(true);
      return;
    }
    if (position < clock_base_frames) {
      clock_base_frames = position;
      return;
    }
    const double seconds =
        clock_anchor_seconds +
        (static_cast<double>(position - clock_base_frames) /
         static_cast<double>(clock_frequency > 0 ? clock_frequency : sample_rate())) *
            rate;

    // 设备时钟若发生秒级跳变（驱动重置/累计位置混用），重新锚定而不是让
    // 视频跳一大段；正常播放不会触发。
    if (seconds > clock_seconds.load() + 1.0) {
      AudioLog("audio clock jump: pos=%llu base=%llu seconds=%.3f current=%.3f",
               static_cast<unsigned long long>(position),
               static_cast<unsigned long long>(clock_base_frames), seconds,
               clock_seconds.load());
      clock_base_frames = position;
      clock_anchor_seconds = clock_seconds.load();
      return;
    }
    if (seconds > clock_seconds.load()) {
      clock_seconds.store(seconds);
    }
  }

  void ApplyFadeIn(float* samples, std::size_t frames, int channel_count) {
    if (fade_in_remaining == 0 || channel_count <= 0) {
      return;
    }
    const std::size_t total =
        static_cast<std::size_t>(sample_rate()) * 5 / 1000;  // 5ms
    if (total == 0) {
      fade_in_remaining = 0;
      return;
    }
    for (std::size_t frame = 0; frame < frames && fade_in_remaining > 0;
         ++frame) {
      const float gain =
          1.0f - static_cast<float>(fade_in_remaining) / static_cast<float>(total);
      for (int channel = 0; channel < channel_count; ++channel) {
        samples[frame * static_cast<std::size_t>(channel_count) +
                static_cast<std::size_t>(channel)] *= gain;
      }
      --fade_in_remaining;
    }
  }

  void StoreLastFrame(const float* samples, std::size_t frames, int channel_count) {
    if (samples == nullptr || frames == 0 || channel_count <= 0) {
      return;
    }
    last_frame.resize(static_cast<std::size_t>(channel_count));
    const std::size_t offset =
        (frames - 1) * static_cast<std::size_t>(channel_count);
    for (int channel = 0; channel < channel_count; ++channel) {
      last_frame[static_cast<std::size_t>(channel)] =
          samples[offset + static_cast<std::size_t>(channel)];
    }
  }

  // 变速/暂停/切文件前写入 5ms 衰减，降低截断波形造成的爆音；
  // 更完整的步进/拖动淡入淡出仍在 T3.7。
  void FadeOut() {
    if (!playing || !ready || !renderer.IsOpen() || last_frame.empty()) {
      return;
    }
    std::uint32_t padding = 0;
    std::string error;
    if (!renderer.Padding(&padding, &error)) {
      return;
    }
    const std::uint32_t buffer_frames = renderer.device_info().buffer_frames;
    const std::uint32_t space =
        buffer_frames > padding ? buffer_frames - padding : 0;
    const std::size_t fade_frames = std::min<std::size_t>(
        static_cast<std::size_t>(space),
        static_cast<std::size_t>(sample_rate()) * 5 / 1000);
    if (fade_frames < 2) {
      return;
    }
    const int channel_count = channels();
    scratch.resize(fade_frames * static_cast<std::size_t>(channel_count));
    for (std::size_t frame = 0; frame < fade_frames; ++frame) {
      const float gain =
          1.0f - static_cast<float>(frame) / static_cast<float>(fade_frames - 1);
      for (int channel = 0; channel < channel_count; ++channel) {
        scratch[frame * static_cast<std::size_t>(channel_count) +
                static_cast<std::size_t>(channel)] =
            last_frame[static_cast<std::size_t>(channel)] * gain;
      }
    }
    if (renderer.WriteFrames(scratch.data(), fade_frames, &error)) {
      frames_since_start += fade_frames;
      last_frame.assign(static_cast<std::size_t>(channel_count), 0.0f);
    }
  }

  void StopPlayback(bool publish_status) {
    if (playing && ready && renderer.IsOpen()) {
      FadeOut();
      std::uint32_t padding = 0;
      std::string error;
      if (renderer.Padding(&padding, &error) && frames_since_start >= padding) {
        position_seconds =
            anchor_seconds +
            (static_cast<double>(frames_since_start - padding) /
             static_cast<double>(sample_rate())) *
                rate;
      }
      renderer.Stop();
      renderer.Reset();
    }
    ring.Clear();
    stretch.Clear();
    stretch_active = false;
    stretch_flushed = false;
    decoder_eof = false;
    playing = false;
    fade_in_remaining = 0;
    clock_valid.store(false);
    if (publish_status) {
      Publish(AudioState::kPaused, "已暂停");
    }
  }

  void HandleDeviceLost() {
    StopPlayback(false);
    renderer.Shutdown();
    ready = false;
    device_retry = true;
    next_retry = std::chrono::steady_clock::now() + kDeviceRetryInterval;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (position_seconds > play_target) {
        play_target = position_seconds;
      }
    }
    AudioLog("audio device lost at %.3fs", position_seconds);
    Publish(AudioState::kSilent, "音频设备已断开，正在重连…");
  }

  bool StartPlayback(double target, std::uint64_t serial) {
    if (!ready) {
      return false;
    }
    StopPlayback(false);
    rate = requested_rate.load();
    if (!std::isfinite(rate) || rate <= 0.0) {
      rate = 1.0;
    }

    std::string error;
    stretch_active = UseStretch();
    AudioFormat output = renderer.device_info().format;
    output.sample_rate = OutputSampleRate();
    if (stretch_active) {
      std::string stretch_error;
      if (!stretch.Reset(sample_rate(), channels(), rate, &stretch_error)) {
        AudioLog("audio pitch-preserve unavailable: %s; using legacy resampling",
                 stretch_error.c_str());
        pitch_preserve = false;
        stretch_active = false;
        output.sample_rate = OutputSampleRate();
      }
    }
    if (!stretch_active) {
      stretch.Clear();
    }
    stretch_flushed = false;
    if (!decoder.SetOutputFormat(output, &error)) {
      want_play.store(false);
      Publish(AudioState::kError, error);
      return false;
    }
    if (!decoder.SeekTo(std::max(0.0, target + av_offset), &error)) {
      want_play.store(false);
      Publish(AudioState::kError, error);
      return false;
    }

    decoder_eof = false;
    stretch_flushed = false;
    const std::size_t prefill =
        static_cast<std::size_t>(sample_rate()) * kStartPrefillMs / 1000;
    if (!FillTo(prefill, &error)) {
      want_play.store(false);
      Publish(AudioState::kError, error);
      return false;
    }
    if (ring.size_frames() == 0 && decoder_eof) {
      want_play.store(false);
      Publish(AudioState::kPaused, "音频已到末尾");
      return false;
    }

    if (!renderer.Start(&error)) {
      HandleDeviceLost();
      return false;
    }

    anchor_seconds = target;
    position_seconds = target;
    frames_since_start = 0;
    clock_base_frames = 0;
    clock_frequency = renderer.device_info().clock_frequency;
    clock_anchor_seconds = target;
    clock_seconds.store(target);
    clock_valid.store(false);
    clock_failures = 0;
    playing = true;
    handled_serial = serial;
    fade_in_remaining = static_cast<std::size_t>(sample_rate()) * 5 / 1000;
    last_frame.assign(static_cast<std::size_t>(channels()), 0.0f);
    AudioLog("audio start: target=%.3fs rate=%.2fx pitch_preserve=%d stretch=%d clock=wait", target, rate, pitch_preserve ? 1 : 0, stretch_active ? 1 : 0);
    Publish(AudioState::kPlaying, "播放中");
    return true;
  }

  void HandleOpen() {
    Publish(AudioState::kOpening, "音频准备中…");
    std::string error;
    bool decoder_failed = false;
    if (!OpenDeviceAndDecoder(&error, &decoder_failed)) {
      ready = false;
      if (decoder_failed) {
        device_retry = false;
        want_play.store(false);
        Publish(AudioState::kError, error);
      } else {
        device_retry = true;
        next_retry = std::chrono::steady_clock::now();
        Publish(AudioState::kSilent, error);
      }
      return;
    }
    ready = true;
    device_retry = false;
    Publish(AudioState::kPaused, "音频就绪");
  }

  void RetryDevice() {
    if (simulate_device_loss.load()) {
      // 仍处于模拟拔出状态：保持静音，不反复打开真实设备。
      next_retry = std::chrono::steady_clock::now() + kDeviceRetryInterval;
      return;
    }
    std::string error;
    bool decoder_failed = false;
    if (!OpenDeviceAndDecoder(&error, &decoder_failed)) {
      next_retry = std::chrono::steady_clock::now() + kDeviceRetryInterval;
      if (decoder_failed) {
        device_retry = false;
        want_play.store(false);
        Publish(AudioState::kError, error);
      } else {
        Publish(AudioState::kSilent, error);
      }
      return;
    }
    ready = true;
    device_retry = false;
    AudioLog("audio device recovered at %.3fs", position_seconds);
    Publish(AudioState::kPaused, "音频设备已恢复");
  }

  void Pump() {
    last_pump_ns.store(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count(),
        std::memory_order_relaxed);
    if (simulate_device_loss.load()) {
      if (playing) {
        // 与真实设备失效同一条路径：停播、使时钟失效、2s 后重试。
        HandleDeviceLost();
      }
      return;
    }
    if (!playing || !ready) {
      return;
    }
    UpdateClock();

    std::string error;
    const std::size_t target =
        static_cast<std::size_t>(sample_rate()) * kTargetBufferMs / 1000;
    if (!FillTo(target, &error)) {
      want_play.store(false);
      StopPlayback(false);
      Publish(AudioState::kError, error);
      return;
    }

    std::uint32_t padding = 0;
    if (!renderer.Padding(&padding, &error)) {
      if (renderer.IsDeviceLost()) {
        HandleDeviceLost();
      } else {
        want_play.store(false);
        StopPlayback(false);
        Publish(AudioState::kError, error);
      }
      return;
    }

    const std::uint32_t buffer_frames = renderer.device_info().buffer_frames;
    const std::uint32_t space =
        buffer_frames > padding ? buffer_frames - padding : 0;
    if (space == 0) {
      return;
    }

    const std::size_t frames = std::min(ring.size_frames(),
                                       static_cast<std::size_t>(space));
    if (frames == 0) {
      if (decoder_eof && (!stretch_active || stretch.Available() == 0)) {
        want_play.store(false);
        StopPlayback(false);
        Publish(AudioState::kPaused, "音频结束");
      } else if (padding == 0) {
        ++underruns;
        AudioLog("audio underrun count=%llu", static_cast<unsigned long long>(underruns));
        PublishPosition();
      }
      return;
    }

    const int channel_count = channels();
    scratch.resize(frames * static_cast<std::size_t>(channel_count));
    const std::size_t popped = ring.Pop(scratch.data(), frames);
    if (popped != frames) {
      return;
    }
    ApplyGain(scratch.data(), frames * static_cast<std::size_t>(channel_count));
    ApplyFadeIn(scratch.data(), frames, channel_count);
    StoreLastFrame(scratch.data(), frames, channel_count);

    if (!renderer.WriteFrames(scratch.data(), frames, &error)) {
      if (renderer.IsDeviceLost()) {
        HandleDeviceLost();
      } else {
        want_play.store(false);
        StopPlayback(false);
        Publish(AudioState::kError, error);
      }
      return;
    }

    const std::uint64_t played =
        frames_since_start > padding ? frames_since_start - padding : 0;
    position_seconds =
        anchor_seconds +
        (static_cast<double>(played) / static_cast<double>(sample_rate())) * rate;
    frames_since_start += frames;
    UpdateClock();
    PublishPosition();
  }

  void Run() {
#ifdef _WIN32
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) {
      AudioLog("audio COM init failed: 0x%08lX", static_cast<unsigned long>(com));
      Publish(AudioState::kError, "音频线程初始化 COM 失败");
      return;
    }
    DWORD mmcss_index = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &mmcss_index);
#endif

    while (true) {
      bool do_open = false;
      bool do_play = false;
      bool do_pause = false;
      bool do_rate = false;
      double new_rate = 1.0;
      double target = 0.0;
      std::uint64_t serial = 0;

      {
        std::unique_lock<std::mutex> lock(mutex);
        if (quit) {
          break;
        }
        if (open_requested) {
          open_requested = false;
          do_open = true;
        }
        const std::uint64_t current_rate_serial = rate_serial.load();
        if (current_rate_serial != handled_rate_serial) {
          do_rate = true;
          new_rate = requested_rate.load();
          handled_rate_serial = current_rate_serial;
        }
        const bool want = want_play.load();
        const std::uint64_t current_serial = seek_serial.load();
        if (want) {
          if (!playing || current_serial != handled_serial) {
            do_play = true;
            target = play_target;
            serial = current_serial;
          }
        } else if (playing) {
          do_pause = true;
        }
      }

      if (do_open) {
        HandleOpen();
      }
      if (do_pause) {
        StopPlayback(true);
      }
      if (do_rate) {
        if (playing) {
          const double resume_target =
              clock_valid.load() ? clock_seconds.load() : position_seconds;
          rate = new_rate;
          StartPlayback(resume_target, handled_serial);
        } else {
          rate = new_rate;
        }
      }
      if (do_play && ready) {
        StartPlayback(target, serial);
      }

      if (!ready && device_retry && want_play.load() &&
          std::chrono::steady_clock::now() >= next_retry) {
        RetryDevice();
      }

      if (playing) {
        Pump();
        if (playing) {
          renderer.WaitForEvent(5);
        }
      } else {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_for(lock, std::chrono::milliseconds(20));
      }
    }

    renderer.Stop();
    renderer.Shutdown();
    decoder.Close();
    growing_format.reset();
#ifdef _WIN32
    if (mmcss != nullptr) {
      AvRevertMmThreadCharacteristics(mmcss);
    }
    CoUninitialize();
#endif
  }
};

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() { Close(); }

bool AudioEngine::Open(const std::string& utf8_path, int stream_index,
                       double av_offset_seconds) {
  Close();
  {
    std::lock_guard<std::mutex> lock(impl_->status_mutex);
    impl_->status = AudioStatus{};
    impl_->status.state = AudioState::kOpening;
    impl_->status.has_audio = true;
    impl_->status.volume = impl_->volume.load();
    impl_->status.muted = impl_->muted.load();
    ++impl_->status.revision;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->path = utf8_path;
    impl_->stream_index = stream_index;
    impl_->av_offset = std::isfinite(av_offset_seconds) ? av_offset_seconds : 0.0;
    impl_->growing = false;
    impl_->growing_source.reset();
    impl_->growing_part_path.clear();
    impl_->quit = false;
    impl_->open_requested = true;
    impl_->want_play.store(false);
    impl_->seek_serial.store(0);
    impl_->play_target = 0.0;
    impl_->ready = false;
    impl_->playing = false;
    impl_->decoder_eof = false;
    impl_->device_retry = false;
    impl_->simulate_device_loss.store(false);
    impl_->handled_serial = 0;
    impl_->handled_rate_serial = 0;
    impl_->rate = impl_->requested_rate.load();
    impl_->frames_since_start = 0;
    impl_->position_seconds = 0.0;
    impl_->underruns = 0;
  }
  try {
    impl_->thread = std::thread([this] { impl_->Run(); });
  } catch (const std::exception&) {
    std::lock_guard<std::mutex> lock(impl_->status_mutex);
    impl_->status.state = AudioState::kError;
    impl_->status.message = "启动音频线程失败";
    ++impl_->status.revision;
    return false;
  }
  return true;
}

bool AudioEngine::OpenGrowing(
    std::shared_ptr<lapian::online::GrowingSource> source,
    const std::string& part_path_utf8, int stream_index,
    double av_offset_seconds) {
  Close();
  if (!source || !source->IsValid() || part_path_utf8.empty()) {
    std::lock_guard<std::mutex> lock(impl_->status_mutex);
    impl_->status = AudioStatus{};
    impl_->status.state = AudioState::kError;
    impl_->status.message = "可增长音频源无效";
    ++impl_->status.revision;
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->status_mutex);
    impl_->status = AudioStatus{};
    impl_->status.state = AudioState::kOpening;
    impl_->status.has_audio = true;
    impl_->status.volume = impl_->volume.load();
    impl_->status.muted = impl_->muted.load();
    ++impl_->status.revision;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->path.clear();
    impl_->stream_index = stream_index;
    impl_->av_offset =
        std::isfinite(av_offset_seconds) ? av_offset_seconds : 0.0;
    impl_->growing = true;
    impl_->growing_source = std::move(source);
    impl_->growing_part_path = part_path_utf8;
    impl_->quit = false;
    impl_->open_requested = true;
    impl_->want_play.store(false);
    impl_->seek_serial.store(0);
    impl_->play_target = 0.0;
    impl_->ready = false;
    impl_->playing = false;
    impl_->decoder_eof = false;
    impl_->device_retry = false;
    impl_->simulate_device_loss.store(false);
    impl_->handled_serial = 0;
    impl_->handled_rate_serial = 0;
    impl_->rate = impl_->requested_rate.load();
    impl_->frames_since_start = 0;
    impl_->position_seconds = 0.0;
    impl_->underruns = 0;
  }
  try {
    impl_->thread = std::thread([this] { impl_->Run(); });
  } catch (const std::exception&) {
    std::lock_guard<std::mutex> lock(impl_->status_mutex);
    impl_->status.state = AudioState::kError;
    impl_->status.message = "启动音频线程失败";
    ++impl_->status.revision;
    return false;
  }
  return true;
}

void AudioEngine::Close() {
  if (!impl_) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->quit = true;
  }
  impl_->cv.notify_all();
  if (impl_->thread.joinable()) {
    impl_->thread.join();
  }
  impl_->clock_valid.store(false);
  impl_->growing_source.reset();
  impl_->growing = false;
  impl_->growing_part_path.clear();
  {
    std::lock_guard<std::mutex> lock(impl_->status_mutex);
    impl_->status = AudioStatus{};
    impl_->status.state = AudioState::kClosed;
    ++impl_->status.revision;
  }
}

void AudioEngine::PlayFrom(double media_seconds) {
  if (!std::isfinite(media_seconds) || media_seconds < 0.0) {
    media_seconds = 0.0;
  }
  impl_->clock_seconds.store(media_seconds);
  impl_->clock_valid.store(false);
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->play_target = media_seconds;
    impl_->seek_serial.fetch_add(1);
    impl_->want_play.store(true);
  }
  impl_->cv.notify_all();
}

void AudioEngine::Pause() {
  impl_->clock_valid.store(false);
  impl_->want_play.store(false);
  impl_->cv.notify_all();
}

void AudioEngine::SetRate(double rate) {
  if (!std::isfinite(rate) || rate <= 0.0) {
    rate = 1.0;
  }
  const double clamped = std::clamp(rate, 0.25, 2.0);
  impl_->requested_rate.store(clamped);
  impl_->rate_serial.fetch_add(1);
  impl_->clock_valid.store(false);
  AudioLog("audio rate=%.2fx", clamped);
  impl_->cv.notify_all();
}

void AudioEngine::SetVolume(int percent) {
  const int clamped = std::clamp(percent, 0, 100);
  impl_->volume.store(clamped);
  AudioLog("audio volume=%d", clamped);
  {
    std::lock_guard<std::mutex> lock(impl_->status_mutex);
    impl_->status.volume = clamped;
    ++impl_->status.revision;
  }
}

void AudioEngine::SetMute(bool muted) {
  impl_->muted.store(muted);
  AudioLog("audio mute=%d", muted ? 1 : 0);
  {
    std::lock_guard<std::mutex> lock(impl_->status_mutex);
    impl_->status.muted = muted;
    ++impl_->status.revision;
  }
}

AudioStatus AudioEngine::status() const {
  std::lock_guard<std::mutex> lock(impl_->status_mutex);
  return impl_->status;
}

bool AudioEngine::ClockSeconds(double* seconds) const {
  if (seconds == nullptr || !impl_->clock_valid.load()) {
    return false;
  }
  // T8.6：音频线程若阻塞在可增长源 frontier（FillTo → DecodeChunk），Pump 不再
  // 推进；此时设备时钟不再代表媒体进度。用“最后一次 Pump 墙钟”判定失速，
  // 让 PlaybackController 进入缓冲暂停，而不是拿旧 clock 把画面冻住/跳帧。
  const std::int64_t last_pump = impl_->last_pump_ns.load(std::memory_order_relaxed);
  if (last_pump > 0) {
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    if (now - last_pump > 400'000'000LL) {
      return false;
    }
  }
  const double value = impl_->clock_seconds.load();
  if (!std::isfinite(value)) {
    return false;
  }
  *seconds = value;
  return true;
}

void AudioEngine::SimulateDeviceLossForTest(bool active) {
  impl_->simulate_device_loss.store(active);
  impl_->cv.notify_all();
}

}  // namespace lapian::audio
