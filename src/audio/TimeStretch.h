#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace lapian::audio {

// Tempo change without pitch change, backed by SoundTouch.
//
// Audio-thread only: no locks, no UI-thread calls, no per-chunk allocation
// beyond SoundTouch's internal FIFO. The wrapper is intentionally small so
// AudioEngine can fall back to the legacy resampling path if Reset() fails.
class TimeStretch {
 public:
  TimeStretch();
  ~TimeStretch();

  TimeStretch(const TimeStretch&) = delete;
  TimeStretch& operator=(const TimeStretch&) = delete;

  // sample_rate/channels must match the interleaved float32 stream.
  // tempo is the playback rate (0.5 = half speed, 2.0 = double speed).
  bool Reset(int sample_rate, int channels, double tempo, std::string* error);
  void Clear();

  bool SetTempo(double tempo, std::string* error);

  // interleaved float32; frames = samples per channel.
  void Feed(const float* interleaved, std::size_t frames);
  std::size_t Receive(float* interleaved, std::size_t max_frames);
  std::size_t Available() const;

  // Drain the tail at end-of-stream. Call once, then Receive until empty.
  void Flush();

  double tempo() const;
  bool active() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lapian::audio
