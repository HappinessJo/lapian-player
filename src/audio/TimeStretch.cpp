#include "audio/TimeStretch.h"

#include <algorithm>
#include <cstdint>
#include <exception>

#include "SoundTouch.h"

namespace lapian::audio {
namespace {

constexpr int kMaxChannels = 16;

}  // namespace

struct TimeStretch::Impl {
  soundtouch::SoundTouch st;
  int sample_rate = 0;
  int channels = 0;
  double tempo = 1.0;
  bool active = false;
  bool flushed = false;
};

TimeStretch::TimeStretch() : impl_(std::make_unique<Impl>()) {}

TimeStretch::~TimeStretch() = default;

bool TimeStretch::Reset(int sample_rate, int channels, double tempo,
                        std::string* error) {
  if (sample_rate <= 0 || channels <= 0 || channels > kMaxChannels ||
      !(tempo > 0.0) || !(tempo < 8.0)) {
    if (error != nullptr) {
      *error = "invalid time-stretch parameters";
    }
    impl_->active = false;
    return false;
  }
  try {
    impl_->st.setSampleRate(static_cast<std::uint32_t>(sample_rate));
    impl_->st.setChannels(static_cast<std::uint32_t>(channels));
    impl_->st.setPitch(1.0);
    impl_->st.setRate(1.0);
    impl_->st.setTempo(tempo);
    impl_->st.clear();
  } catch (const std::exception& e) {
    if (error != nullptr) {
      *error = e.what();
    }
    impl_->active = false;
    return false;
  }
  impl_->sample_rate = sample_rate;
  impl_->channels = channels;
  impl_->tempo = tempo;
  impl_->active = true;
  impl_->flushed = false;
  return true;
}

void TimeStretch::Clear() {
  impl_->st.clear();
  impl_->flushed = false;
}

bool TimeStretch::SetTempo(double tempo, std::string* error) {
  if (!impl_->active || !(tempo > 0.0) || !(tempo < 8.0)) {
    if (error != nullptr) {
      *error = "time-stretch is not active";
    }
    return false;
  }
  try {
    impl_->st.setTempo(tempo);
  } catch (const std::exception& e) {
    if (error != nullptr) {
      *error = e.what();
    }
    return false;
  }
  impl_->tempo = tempo;
  return true;
}

void TimeStretch::Feed(const float* interleaved, std::size_t frames) {
  if (!impl_->active || interleaved == nullptr || frames == 0) {
    return;
  }
  impl_->st.putSamples(interleaved, static_cast<std::uint32_t>(frames));
}

std::size_t TimeStretch::Receive(float* interleaved, std::size_t max_frames) {
  if (!impl_->active || interleaved == nullptr || max_frames == 0) {
    return 0;
  }
  return impl_->st.receiveSamples(interleaved,
                                  static_cast<std::uint32_t>(max_frames));
}

std::size_t TimeStretch::Available() const {
  return impl_->active ? impl_->st.numSamples() : 0;
}

void TimeStretch::Flush() {
  if (!impl_->active || impl_->flushed) {
    return;
  }
  impl_->st.flush();
  impl_->flushed = true;
}

double TimeStretch::tempo() const { return impl_->tempo; }

bool TimeStretch::active() const { return impl_->active; }

}  // namespace lapian::audio
