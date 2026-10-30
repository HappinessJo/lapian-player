#include "audio/WasapiRenderer.h"

#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <vector>

namespace lapian::audio {

struct WasapiRenderer::Impl {
  AudioUnit output = nullptr;
  WasapiDeviceInfo info;
  bool open = false;
  bool started = false;
  bool lost = false;
  mutable std::mutex mutex;
  std::condition_variable cv;
  std::vector<float> queue;
  std::size_t read_offset = 0;
  std::uint64_t rendered_frames = 0;

  std::size_t BufferedFramesLocked() const {
    return (queue.size() - read_offset) / static_cast<std::size_t>(info.format.channels);
  }

  void ClearQueueLocked() {
    queue.clear();
    read_offset = 0;
  }

  static OSStatus RenderAudio(void* user_data, AudioUnitRenderActionFlags*,
                              const AudioTimeStamp*, UInt32, UInt32 frames,
                              AudioBufferList* data) {
    auto* impl = static_cast<Impl*>(user_data);
    return impl == nullptr ? noErr : impl->Render(frames, data);
  }

  OSStatus Render(UInt32 frames, AudioBufferList* data) {
    if (data == nullptr || data->mNumberBuffers == 0) {
      return noErr;
    }
    AudioBuffer& buffer = data->mBuffers[0];
    float* output_samples = static_cast<float*>(buffer.mData);
    const std::size_t requested_samples =
        static_cast<std::size_t>(frames) * static_cast<std::size_t>(info.format.channels);
    const std::size_t requested_bytes = requested_samples * sizeof(float);
    if (output_samples == nullptr) {
      return noErr;
    }
    std::memset(output_samples, 0, std::min<std::size_t>(buffer.mDataByteSize, requested_bytes));

    {
      std::lock_guard<std::mutex> lock(mutex);
      const std::size_t available_samples = queue.size() - read_offset;
      const std::size_t count = std::min(requested_samples, available_samples);
      if (count > 0) {
        std::memcpy(output_samples, queue.data() + read_offset, count * sizeof(float));
        read_offset += count;
        if (read_offset == queue.size()) {
          ClearQueueLocked();
        } else if (read_offset >= queue.size() / 2) {
          queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(read_offset));
          read_offset = 0;
        }
      }
    }
    rendered_frames += frames;
    cv.notify_all();
    return noErr;
  }

  void Shutdown() {
    if (output != nullptr) {
      if (started) {
        AudioOutputUnitStop(output);
      }
      AudioUnitUninitialize(output);
      AudioComponentInstanceDispose(output);
      output = nullptr;
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      ClearQueueLocked();
    }
    open = false;
    started = false;
    lost = false;
    rendered_frames = 0;
    info = WasapiDeviceInfo{};
    cv.notify_all();
  }
};

WasapiRenderer::WasapiRenderer() : impl_(std::make_unique<Impl>()) {}

WasapiRenderer::~WasapiRenderer() { impl_->Shutdown(); }

bool WasapiRenderer::Initialize(std::string* error) {
  if (error == nullptr) {
    return false;
  }
  error->clear();
  impl_->Shutdown();

  AudioComponentDescription description{};
  description.componentType = kAudioUnitType_Output;
  description.componentSubType = kAudioUnitSubType_DefaultOutput;
  description.componentManufacturer = kAudioUnitManufacturer_Apple;
  AudioComponent component = AudioComponentFindNext(nullptr, &description);
  if (component == nullptr) {
    *error = "没有可用的 CoreAudio 默认输出设备";
    return false;
  }
  OSStatus status = AudioComponentInstanceNew(component, &impl_->output);
  if (status != noErr) {
    *error = "创建 CoreAudio 输出失败（" + std::to_string(status) + "）";
    return false;
  }

  AURenderCallbackStruct callback{};
  callback.inputProc = &Impl::RenderAudio;
  callback.inputProcRefCon = impl_.get();
  status = AudioUnitSetProperty(impl_->output, kAudioUnitProperty_SetRenderCallback,
                                kAudioUnitScope_Input, 0, &callback, sizeof(callback));
  if (status != noErr) {
    *error = "配置 CoreAudio 回调失败（" + std::to_string(status) + "）";
    impl_->Shutdown();
    return false;
  }

  AudioStreamBasicDescription format{};
  format.mSampleRate = 48000;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  format.mFramesPerPacket = 1;
  format.mChannelsPerFrame = 2;
  format.mBitsPerChannel = 32;
  format.mBytesPerFrame = sizeof(float) * format.mChannelsPerFrame;
  format.mBytesPerPacket = format.mBytesPerFrame;
  status = AudioUnitSetProperty(impl_->output, kAudioUnitProperty_StreamFormat,
                                kAudioUnitScope_Input, 0, &format, sizeof(format));
  if (status == noErr) {
    status = AudioUnitInitialize(impl_->output);
  }
  if (status != noErr) {
    *error = "初始化 CoreAudio 输出失败（" + std::to_string(status) + "）";
    impl_->Shutdown();
    return false;
  }

  impl_->info.format.encoding = SampleEncoding::kFloat32;
  impl_->info.format.sample_rate = static_cast<int>(format.mSampleRate);
  impl_->info.format.channels = static_cast<int>(format.mChannelsPerFrame);
  impl_->info.buffer_frames = 4800;
  impl_->info.clock_frequency = static_cast<std::uint64_t>(format.mSampleRate);
  impl_->info.device_name = "CoreAudio 默认输出";
  impl_->open = true;
  return true;
}

void WasapiRenderer::Shutdown() { impl_->Shutdown(); }

bool WasapiRenderer::IsOpen() const { return impl_->open; }

const WasapiDeviceInfo& WasapiRenderer::device_info() const { return impl_->info; }

bool WasapiRenderer::Start(std::string* error) {
  if (error == nullptr) {
    return false;
  }
  error->clear();
  if (!impl_->open) {
    *error = "音频设备未打开";
    return false;
  }
  if (impl_->started) {
    return true;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ClearQueueLocked();
    impl_->rendered_frames = 0;
  }
  const OSStatus status = AudioOutputUnitStart(impl_->output);
  if (status != noErr) {
    *error = "启动 CoreAudio 输出失败（" + std::to_string(status) + "）";
    impl_->lost = true;
    return false;
  }
  impl_->started = true;
  return true;
}

void WasapiRenderer::Stop() {
  if (impl_->output != nullptr && impl_->started) {
    AudioOutputUnitStop(impl_->output);
  }
  impl_->started = false;
  impl_->cv.notify_all();
}

void WasapiRenderer::Reset() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->ClearQueueLocked();
  impl_->rendered_frames = 0;
  impl_->cv.notify_all();
}

bool WasapiRenderer::WaitForEvent(unsigned timeout_ms) {
  std::unique_lock<std::mutex> lock(impl_->mutex);
  return impl_->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]() {
    return !impl_->started || impl_->BufferedFramesLocked() < impl_->info.buffer_frames;
  });
}

bool WasapiRenderer::Padding(std::uint32_t* frames, std::string* error) const {
  if (frames == nullptr || error == nullptr) {
    return false;
  }
  error->clear();
  if (!impl_->open) {
    *error = "音频设备未打开";
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  *frames = static_cast<std::uint32_t>(impl_->BufferedFramesLocked());
  return true;
}

bool WasapiRenderer::WriteFrames(const float* samples, std::size_t frames,
                                 std::string* error) {
  if (error == nullptr) {
    return false;
  }
  error->clear();
  if (!impl_->open) {
    *error = "音频设备未打开";
    return false;
  }
  if (frames == 0) {
    return true;
  }
  if (samples == nullptr) {
    *error = "音频样本为空";
    return false;
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  const std::size_t free_frames = impl_->info.buffer_frames > impl_->BufferedFramesLocked()
                                      ? impl_->info.buffer_frames - impl_->BufferedFramesLocked()
                                      : 0;
  if (frames > free_frames) {
    *error = "CoreAudio 写入超过可用缓冲";
    return false;
  }
  const std::size_t count = frames * static_cast<std::size_t>(impl_->info.format.channels);
  impl_->queue.insert(impl_->queue.end(), samples, samples + count);
  return true;
}

bool WasapiRenderer::IsDeviceLost() const { return impl_->lost; }

bool WasapiRenderer::ClockPosition(std::uint64_t* position) const {
  if (position == nullptr || !impl_->started) {
    return false;
  }
  *position = impl_->rendered_frames;
  return true;
}

}  // namespace lapian::audio
