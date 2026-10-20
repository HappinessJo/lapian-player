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
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>
#include <avrt.h>
#include <objbase.h>

#include "audio/AudioDecoder.h"
#include "audio/AudioFormat.h"
#include "audio/WasapiRenderer.h"

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

}  // namespace

struct AudioEngine::Impl {
  std::string path;
  int stream_index = -1;

  std::thread thread;
  std::mutex mutex;
  std::condition_variable cv;
  bool quit = false;
  bool open_requested = false;
  double play_target = 0.0;
  std::atomic<bool> want_play{false};
  std::atomic<std::uint64_t> seek_serial{0};
  std::atomic<int> volume{100};
  std::atomic<bool> muted{false};

  mutable std::mutex status_mutex;
  AudioStatus status;

  AudioDecoder decoder;
  WasapiRenderer renderer;
  AudioRingBuffer ring;
  std::vector<float> scratch;

  bool ready = false;
  bool playing = false;
  bool decoder_eof = false;
  bool device_retry = false;
  std::chrono::steady_clock::time_point next_retry;
  std::uint64_t handled_serial = 0;
  double anchor_seconds = 0.0;
  double position_seconds = 0.0;
  std::uint64_t frames_since_start = 0;
  std::uint64_t underruns = 0;
  std::chrono::steady_clock::time_point last_position_publish;

  int sample_rate() const { return renderer.device_info().format.sample_rate; }
  int channels() const { return renderer.device_info().format.channels; }

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
    AudioLog("audio device: %d Hz %d ch %s buffer=%u", format.sample_rate, format.channels,
             format.encoding == SampleEncoding::kFloat32 ? "float32" : "pcm16",
             renderer.device_info().buffer_frames);

    ring.Reset(static_cast<std::size_t>(format.sample_rate) *
                   static_cast<std::size_t>(kMaxBufferMs) / 1000,
               format.channels);
    scratch.resize(static_cast<std::size_t>(renderer.device_info().buffer_frames) *
                   static_cast<std::size_t>(format.channels));

    if (!decoder.Open(path, stream_index, format, error)) {
      *decoder_failed = true;
      renderer.Shutdown();
      return false;
    }
    AudioLog("audio decoder: %s %d Hz %d ch", decoder.info().codec.c_str(),
             decoder.info().source_sample_rate, decoder.info().source_channels);
    return true;
  }

  bool FillTo(std::size_t target_frames, std::string* error) {
    if (!decoder.IsOpen()) {
      *error = "音频解码器未打开";
      return false;
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

  void StopPlayback(bool publish_status) {
    if (playing && ready && renderer.IsOpen()) {
      std::uint32_t padding = 0;
      std::string error;
      if (renderer.Padding(&padding, &error) && frames_since_start >= padding) {
        position_seconds =
            anchor_seconds + static_cast<double>(frames_since_start - padding) /
                                 static_cast<double>(sample_rate());
      }
      renderer.Stop();
      renderer.Reset();
    }
    ring.Clear();
    decoder_eof = false;
    playing = false;
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

    std::string error;
    if (!decoder.SeekTo(target, &error)) {
      want_play.store(false);
      Publish(AudioState::kError, error);
      return false;
    }

    decoder_eof = false;
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
    playing = true;
    handled_serial = serial;
    AudioLog("audio start: target=%.3fs", target);
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
    if (!playing || !ready) {
      return;
    }

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
      if (decoder_eof) {
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
        anchor_seconds + static_cast<double>(played) / static_cast<double>(sample_rate());
    frames_since_start += frames;
    PublishPosition();
  }

  void Run() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) {
      AudioLog("audio COM init failed: 0x%08lX", static_cast<unsigned long>(com));
      Publish(AudioState::kError, "音频线程初始化 COM 失败");
      return;
    }
    DWORD mmcss_index = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &mmcss_index);

    while (true) {
      bool do_open = false;
      bool do_play = false;
      bool do_pause = false;
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
    if (mmcss != nullptr) {
      AvRevertMmThreadCharacteristics(mmcss);
    }
    CoUninitialize();
  }
};

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() { Close(); }

bool AudioEngine::Open(const std::string& utf8_path, int stream_index) {
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
    impl_->quit = false;
    impl_->open_requested = true;
    impl_->want_play.store(false);
    impl_->seek_serial.store(0);
    impl_->play_target = 0.0;
    impl_->ready = false;
    impl_->playing = false;
    impl_->decoder_eof = false;
    impl_->device_retry = false;
    impl_->handled_serial = 0;
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
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->play_target = media_seconds;
    impl_->seek_serial.fetch_add(1);
    impl_->want_play.store(true);
  }
  impl_->cv.notify_all();
}

void AudioEngine::Pause() {
  impl_->want_play.store(false);
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

}  // namespace lapian::audio
