#include "audio/AudioFormat.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lapian::audio {

float VolumeToGain(int percent) {
  const int clamped = std::clamp(percent, 0, 100);
  const double normalized = static_cast<double>(clamped) / 100.0;
  return static_cast<float>(normalized * normalized);
}

void AudioRingBuffer::Reset(std::size_t capacity_frames, int channels) {
  channels_ = channels > 0 ? channels : 0;
  capacity_frames_ = (channels_ > 0) ? capacity_frames : 0;
  head_ = 0;
  size_ = 0;
  data_.assign(capacity_frames_ * static_cast<std::size_t>(channels_), 0.0f);
}

void AudioRingBuffer::Clear() {
  head_ = 0;
  size_ = 0;
}

std::size_t AudioRingBuffer::Push(const float* samples, std::size_t frames) {
  if (!valid() || samples == nullptr || frames == 0) {
    return 0;
  }
  const std::size_t accepted = std::min(frames, free_frames());
  if (accepted == 0) {
    return 0;
  }

  const std::size_t write_index = (head_ + size_) % capacity_frames_;
  const std::size_t first = std::min(accepted, capacity_frames_ - write_index);
  const std::size_t channels = static_cast<std::size_t>(channels_);
  std::memcpy(&data_[write_index * channels], samples,
              first * channels * sizeof(float));
  if (first < accepted) {
    std::memcpy(&data_[0], samples + first * channels,
                (accepted - first) * channels * sizeof(float));
  }
  size_ += accepted;
  return accepted;
}

std::size_t AudioRingBuffer::Pop(float* out, std::size_t frames) {
  if (!valid() || out == nullptr || frames == 0) {
    return 0;
  }
  const std::size_t taken = std::min(frames, size_);
  if (taken == 0) {
    return 0;
  }

  const std::size_t first = std::min(taken, capacity_frames_ - head_);
  const std::size_t channels = static_cast<std::size_t>(channels_);
  std::memcpy(out, &data_[head_ * channels], first * channels * sizeof(float));
  if (first < taken) {
    std::memcpy(out + first * channels, &data_[0],
                (taken - first) * channels * sizeof(float));
  }
  head_ = (head_ + taken) % capacity_frames_;
  size_ -= taken;
  if (size_ == 0) {
    head_ = 0;
  }
  return taken;
}

void ConvertFloatToS16(const float* input, std::int16_t* output, std::size_t samples) {
  if (input == nullptr || output == nullptr) {
    return;
  }
  for (std::size_t i = 0; i < samples; ++i) {
    const float clamped = std::clamp(input[i], -1.0f, 1.0f);
    const long rounded = std::lround(static_cast<double>(clamped) * 32767.0);
    output[i] = static_cast<std::int16_t>(
        std::clamp(rounded, static_cast<long>(-32768), static_cast<long>(32767)));
  }
}

void ConvertS16ToFloat(const std::int16_t* input, float* output, std::size_t samples) {
  if (input == nullptr || output == nullptr) {
    return;
  }
  for (std::size_t i = 0; i < samples; ++i) {
    output[i] = static_cast<float>(input[i]) / 32768.0f;
  }
}

}  // namespace lapian::audio
