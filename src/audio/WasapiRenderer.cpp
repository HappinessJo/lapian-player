#include "audio/WasapiRenderer.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <windows.h>
#include <objbase.h>

#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include "audio/AudioFormat.h"

namespace lapian::audio {
namespace {

using Microsoft::WRL::ComPtr;

std::string HresultText(HRESULT result) {
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "0x%08lX",
                static_cast<unsigned long>(result));
  return buffer;
}

bool IsDeviceLostHresult(HRESULT result) {
  return result == AUDCLNT_E_DEVICE_INVALIDATED ||
         result == AUDCLNT_E_RESOURCES_INVALIDATED;
}

bool ParseMixFormat(const WAVEFORMATEX* format, AudioFormat* out, std::string* error) {
  if (format == nullptr) {
    *error = "WASAPI 没有返回混合格式";
    return false;
  }
  if (format->nChannels <= 0 || format->nSamplesPerSec <= 0) {
    *error = "WASAPI 混合格式的声道数/采样率无效";
    return false;
  }

  bool is_float = false;
  bool is_pcm = false;
  if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
    const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
    if (IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
      is_float = true;
    } else if (IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
      is_pcm = true;
    }
  } else if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
    is_float = true;
  } else if (format->wFormatTag == WAVE_FORMAT_PCM) {
    is_pcm = true;
  }

  AudioFormat parsed;
  if (is_float && format->wBitsPerSample == 32) {
    parsed.encoding = SampleEncoding::kFloat32;
  } else if (is_pcm && format->wBitsPerSample == 16) {
    parsed.encoding = SampleEncoding::kInt16;
  } else {
    *error = "音频设备格式不支持（仅 float32 / PCM16）";
    return false;
  }
  parsed.sample_rate = static_cast<int>(format->nSamplesPerSec);
  parsed.channels = static_cast<int>(format->nChannels);
  if (format->nBlockAlign != parsed.bytes_per_frame()) {
    *error = "音频设备块对齐与声道/位深不匹配";
    return false;
  }

  *out = parsed;
  return true;
}

}  // namespace

struct WasapiRenderer::Impl {
  ComPtr<IMMDeviceEnumerator> enumerator;
  ComPtr<IMMDevice> device;
  ComPtr<IAudioClient> client;
  ComPtr<IAudioRenderClient> render_client;
  ComPtr<IAudioClock> audio_clock;
  WAVEFORMATEX* mix_format = nullptr;
  HANDLE event = nullptr;
  WasapiDeviceInfo info;
  bool started = false;
  mutable bool device_lost = false;

  bool Open(std::string* error) {
    Shutdown();

    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(enumerator.GetAddressOf()));
    if (FAILED(result)) {
      *error = "创建音频设备枚举器失败: " + HresultText(result);
      return false;
    }

    result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.GetAddressOf());
    if (FAILED(result)) {
      *error = "没有可用的默认音频输出设备: " + HresultText(result);
      return false;
    }

    result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(client.GetAddressOf()));
    if (FAILED(result)) {
      *error = "激活 WASAPI 音频客户端失败: " + HresultText(result);
      return false;
    }

    result = client->GetMixFormat(&mix_format);
    if (FAILED(result) || mix_format == nullptr) {
      *error = "读取音频设备混合格式失败: " + HresultText(result);
      return false;
    }
    if (!ParseMixFormat(mix_format, &info.format, error)) {
      return false;
    }

    auto initialize = [&](REFERENCE_TIME duration) {
      return client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                duration, 0, mix_format, nullptr);
    };
    result = initialize(0);
    if (result == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
      UINT32 buffer_frames = 0;
      result = client->GetBufferSize(&buffer_frames);
      if (FAILED(result) || buffer_frames == 0) {
        *error = "读取音频设备缓冲大小失败: " + HresultText(result);
        return false;
      }
      const REFERENCE_TIME duration = static_cast<REFERENCE_TIME>(
          10000.0 * 1000.0 * static_cast<double>(buffer_frames) /
              static_cast<double>(info.format.sample_rate) +
          0.5);
      client.Reset();
      result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(client.GetAddressOf()));
      if (FAILED(result)) {
        *error = "重建 WASAPI 音频客户端失败: " + HresultText(result);
        return false;
      }
      result = initialize(duration);
    }
    if (FAILED(result)) {
      if (IsDeviceLostHresult(result)) {
        device_lost = true;
      }
      *error = "初始化 WASAPI 音频流失败: " + HresultText(result);
      return false;
    }

    event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr) {
      *error = "创建音频渲染事件失败";
      return false;
    }
    result = client->SetEventHandle(event);
    if (FAILED(result)) {
      *error = "设置音频渲染事件失败: " + HresultText(result);
      return false;
    }

    result = client->GetService(IID_PPV_ARGS(render_client.GetAddressOf()));
    if (FAILED(result)) {
      *error = "获取音频渲染接口失败: " + HresultText(result);
      return false;
    }

    UINT32 buffer_frames = 0;
    result = client->GetBufferSize(&buffer_frames);
    if (FAILED(result) || buffer_frames == 0) {
      *error = "读取音频流缓冲大小失败: " + HresultText(result);
      return false;
    }
    info.buffer_frames = buffer_frames;
    info.device_name = "默认输出设备";

    // IAudioClock 只用于 T3.2 的主时钟；取不到不阻塞 T3.1 的音频输出。
    if (FAILED(client->GetService(IID_PPV_ARGS(audio_clock.GetAddressOf())))) {
      audio_clock.Reset();
    }
    if (audio_clock != nullptr) {
      UINT64 frequency = 0;
      if (SUCCEEDED(audio_clock->GetFrequency(&frequency)) && frequency > 0) {
        info.clock_frequency = frequency;
      }
    }
    device_lost = false;
    started = false;
    return true;
  }

  void Shutdown() {
    if (started && client != nullptr) {
      client->Stop();
      started = false;
    }
    audio_clock.Reset();
    render_client.Reset();
    client.Reset();
    device.Reset();
    enumerator.Reset();
    if (mix_format != nullptr) {
      CoTaskMemFree(mix_format);
      mix_format = nullptr;
    }
    if (event != nullptr) {
      CloseHandle(event);
      event = nullptr;
    }
    info = WasapiDeviceInfo{};
    device_lost = false;
  }
};

WasapiRenderer::WasapiRenderer() : impl_(std::make_unique<Impl>()) {}

WasapiRenderer::~WasapiRenderer() {
  if (impl_ != nullptr) {
    impl_->Shutdown();
  }
}

bool WasapiRenderer::Initialize(std::string* error) {
  if (error == nullptr) {
    return false;
  }
  error->clear();
  if (!impl_->Open(error)) {
    impl_->Shutdown();
    return false;
  }
  return true;
}

void WasapiRenderer::Shutdown() { impl_->Shutdown(); }

bool WasapiRenderer::IsOpen() const { return impl_->client != nullptr; }

const WasapiDeviceInfo& WasapiRenderer::device_info() const { return impl_->info; }

bool WasapiRenderer::Start(std::string* error) {
  if (error == nullptr) {
    return false;
  }
  error->clear();
  if (impl_->client == nullptr) {
    *error = "音频设备未打开";
    return false;
  }
  if (impl_->started) {
    return true;
  }
  if (impl_->event != nullptr) {
    ResetEvent(impl_->event);
  }
  const HRESULT result = impl_->client->Start();
  if (FAILED(result)) {
    if (IsDeviceLostHresult(result)) {
      impl_->device_lost = true;
    }
    *error = "启动音频流失败: " + HresultText(result);
    return false;
  }
  impl_->started = true;
  return true;
}

void WasapiRenderer::Stop() {
  if (impl_->client != nullptr && impl_->started) {
    const HRESULT result = impl_->client->Stop();
    if (IsDeviceLostHresult(result)) {
      impl_->device_lost = true;
    }
    impl_->started = false;
  }
}

void WasapiRenderer::Reset() {
  if (impl_->client != nullptr) {
    const HRESULT result = impl_->client->Reset();
    if (IsDeviceLostHresult(result)) {
      impl_->device_lost = true;
    }
  }
}

bool WasapiRenderer::WaitForEvent(unsigned timeout_ms) {
  if (impl_->event == nullptr) {
    return false;
  }
  return WaitForSingleObject(impl_->event, static_cast<DWORD>(timeout_ms)) ==
         WAIT_OBJECT_0;
}

bool WasapiRenderer::Padding(std::uint32_t* frames, std::string* error) const {
  if (frames == nullptr || error == nullptr) {
    return false;
  }
  error->clear();
  if (impl_->client == nullptr) {
    *error = "音频设备未打开";
    return false;
  }
  UINT32 padding = 0;
  const HRESULT result = impl_->client->GetCurrentPadding(&padding);
  if (FAILED(result)) {
    if (IsDeviceLostHresult(result)) {
      impl_->device_lost = true;
    }
    *error = "读取音频缓冲状态失败: " + HresultText(result);
    return false;
  }
  *frames = padding;
  return true;
}

bool WasapiRenderer::WriteFrames(const float* samples, std::size_t frames,
                                 std::string* error) {
  if (error == nullptr) {
    return false;
  }
  error->clear();
  if (impl_->render_client == nullptr || impl_->client == nullptr) {
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
  if (frames > impl_->info.buffer_frames) {
    *error = "音频写入超过设备缓冲大小";
    return false;
  }

  BYTE* buffer = nullptr;
  const UINT32 frame_count = static_cast<UINT32>(frames);
  HRESULT result = impl_->render_client->GetBuffer(frame_count, &buffer);
  if (FAILED(result)) {
    if (IsDeviceLostHresult(result)) {
      impl_->device_lost = true;
    }
    *error = "获取音频渲染缓冲失败: " + HresultText(result);
    return false;
  }
  if (buffer == nullptr) {
    *error = "音频渲染缓冲为空";
    return false;
  }

  const std::size_t sample_count = frames * static_cast<std::size_t>(impl_->info.format.channels);
  if (impl_->info.format.encoding == SampleEncoding::kFloat32) {
    std::memcpy(buffer, samples, sample_count * sizeof(float));
  } else {
    ConvertFloatToS16(samples, reinterpret_cast<std::int16_t*>(buffer), sample_count);
  }

  result = impl_->render_client->ReleaseBuffer(frame_count, 0);
  if (FAILED(result)) {
    if (IsDeviceLostHresult(result)) {
      impl_->device_lost = true;
    }
    *error = "提交音频渲染缓冲失败: " + HresultText(result);
    return false;
  }
  return true;
}

bool WasapiRenderer::IsDeviceLost() const { return impl_->device_lost; }

bool WasapiRenderer::ClockPosition(std::uint64_t* position) const {
  if (position == nullptr || impl_->audio_clock == nullptr) {
    return false;
  }
  UINT64 current = 0;
  const HRESULT result = impl_->audio_clock->GetPosition(&current, nullptr);
  if (FAILED(result)) {
    if (IsDeviceLostHresult(result)) {
      impl_->device_lost = true;
    }
    return false;
  }
  *position = current;
  return true;
}

}  // namespace lapian::audio
