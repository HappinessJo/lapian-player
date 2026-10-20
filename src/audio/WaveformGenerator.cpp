#include "audio/WaveformGenerator.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "audio/AudioDecoder.h"

namespace lapian::audio {
namespace {

constexpr int kOutputSampleRate = 48000;
constexpr std::size_t kSamplesPerBucket =
    static_cast<std::size_t>(kOutputSampleRate) /
    static_cast<std::size_t>(WaveformData::kBucketsPerSecond);

}  // namespace

struct WaveformGenerator::Impl {
  mutable std::mutex status_mutex;
  WaveformStatus status;
  std::shared_ptr<WaveformData> data;
  std::thread thread;
  std::atomic<bool> cancel{false};

  void Publish(WaveformState state, double progress, std::string message) {
    std::lock_guard<std::mutex> lock(status_mutex);
    status.state = state;
    status.progress = std::clamp(progress, 0.0, 1.0);
    status.message = std::move(message);
    ++status.revision;
  }

  void Run(std::string path, int stream_index) {
    AudioFormat format;
    format.sample_rate = kOutputSampleRate;
    format.channels = 1;

    AudioDecoder decoder;
    std::string error;
    if (!decoder.Open(path, stream_index, format, &error)) {
      const bool no_audio = error.find("没有音频流") != std::string::npos;
      Publish(no_audio ? WaveformState::kNoAudio : WaveformState::kFailed, 0.0,
              error);
      return;
    }

    const double duration =
        decoder.info().duration_us > 0
            ? static_cast<double>(decoder.info().duration_us) / 1.0e6
            : 0.0;
    data->SetDuration(duration);

    std::int64_t current_bucket = -1;
    std::int16_t bucket_min = 0;
    std::int16_t bucket_max = 0;
    bool bucket_has_data = false;

    auto flush_bucket = [&]() {
      if (current_bucket >= 0 && bucket_has_data) {
        data->SetBucket(static_cast<std::size_t>(current_bucket), bucket_min,
                        bucket_max);
      }
    };

    for (int guard = 0; guard < 10'000'000; ++guard) {
      if (cancel.load()) {
        Publish(WaveformState::kCancelled, 0.0, "已取消");
        return;
      }

      AudioChunk chunk;
      const AudioDecodeResult result = decoder.DecodeChunk(2048, &chunk, &error);
      if (result == AudioDecodeResult::kEnd) {
        break;
      }
      if (result == AudioDecodeResult::kError) {
        Publish(WaveformState::kFailed, 0.0, error);
        return;
      }
      if (chunk.frames == 0) {
        continue;
      }

      const std::int64_t base_sample = static_cast<std::int64_t>(
          std::llround(chunk.pts_seconds * static_cast<double>(kOutputSampleRate)));
      for (std::size_t i = 0; i < chunk.frames; ++i) {
        const std::int64_t sample_index =
            base_sample + static_cast<std::int64_t>(i);
        if (sample_index < 0) {
          continue;
        }
        const std::int64_t bucket =
            sample_index / static_cast<std::int64_t>(kSamplesPerBucket);
        if (bucket != current_bucket) {
          flush_bucket();
          current_bucket = bucket;
          bucket_min = 0;
          bucket_max = 0;
          bucket_has_data = false;
        }

        const float sample = std::clamp(chunk.samples[i], -1.0f, 1.0f);
        const long quantized = std::lround(static_cast<double>(sample) * 32767.0);
        const auto value = static_cast<std::int16_t>(
            std::clamp(quantized, static_cast<long>(-32768),
                       static_cast<long>(32767)));
        if (!bucket_has_data) {
          bucket_min = value;
          bucket_max = value;
          bucket_has_data = true;
        } else {
          bucket_min = std::min(bucket_min, value);
          bucket_max = std::max(bucket_max, value);
        }
      }

      const double decoded_seconds =
          duration > 0.0
              ? std::min(1.0,
                         (static_cast<double>(base_sample) +
                          static_cast<double>(chunk.frames)) /
                             static_cast<double>(kOutputSampleRate) / duration)
              : 0.0;
      Publish(WaveformState::kRunning, decoded_seconds, "生成中");
    }

    flush_bucket();
    if (data->bucket_count() > 0) {
      data->SetDuration(
          std::max(duration,
                   static_cast<double>(data->bucket_count()) /
                       static_cast<double>(WaveformData::kBucketsPerSecond)));
    }
    Publish(WaveformState::kReady, 1.0, "波形就绪");
  }
};

WaveformGenerator::WaveformGenerator() : impl_(std::make_unique<Impl>()) {}

WaveformGenerator::~WaveformGenerator() { Cancel(); }

bool WaveformGenerator::Start(const std::string& utf8_path, int stream_index,
                              double av_offset_seconds) {
  Cancel();
  impl_->cancel.store(false);
  impl_->data = std::make_shared<WaveformData>();
  impl_->data->Reset(av_offset_seconds);
  impl_->Publish(WaveformState::kRunning, 0.0, "生成中");

  try {
    impl_->thread = std::thread([this, utf8_path, stream_index]() {
      impl_->Run(utf8_path, stream_index);
    });
  } catch (const std::exception&) {
    impl_->Publish(WaveformState::kFailed, 0.0, "启动波形线程失败");
    return false;
  }
  return true;
}

void WaveformGenerator::Cancel() {
  impl_->cancel.store(true);
  if (impl_->thread.joinable()) {
    impl_->thread.join();
  }
  {
    std::lock_guard<std::mutex> lock(impl_->status_mutex);
    if (impl_->status.state == WaveformState::kRunning) {
      impl_->status.state = WaveformState::kCancelled;
      impl_->status.progress = 0.0;
      impl_->status.message = "已取消";
      ++impl_->status.revision;
    }
  }
}

WaveformStatus WaveformGenerator::status() const {
  std::lock_guard<std::mutex> lock(impl_->status_mutex);
  return impl_->status;
}

std::shared_ptr<const WaveformData> WaveformGenerator::data() const {
  return impl_->data;
}

}  // namespace lapian::audio
