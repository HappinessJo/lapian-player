// T3.3b: tempo change without pitch change (SoundTouch wrapper).
// A 440 Hz sine must stay 440 Hz at 0.5x and 2x; output length must follow 1/tempo.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "audio/TimeStretch.h"

using lapian::audio::TimeStretch;

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kInputFrames = kSampleRate;  // 1 second
constexpr double kPi = 3.14159265358979323846;
constexpr double kToneHz = 440.0;

int g_checks = 0;
int g_failures = 0;

void Check(const char* name, bool ok, const std::string& detail) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
  }
  std::printf("time_stretch_check %s ok=%d detail=%s\n", name, ok ? 1 : 0,
              detail.c_str());
}

std::vector<float> MakeTone() {
  std::vector<float> samples(static_cast<std::size_t>(kInputFrames) * kChannels);
  for (int i = 0; i < kInputFrames; ++i) {
    const float value =
        static_cast<float>(0.5 * std::sin(2.0 * kPi * kToneHz * i / kSampleRate));
    samples[static_cast<std::size_t>(i) * kChannels] = value;
    samples[static_cast<std::size_t>(i) * kChannels + 1] = value;
  }
  return samples;
}

double DominantFrequency(const std::vector<float>& interleaved, int channels,
                         int sample_rate) {
  const std::size_t frames = interleaved.size() / static_cast<std::size_t>(channels);
  if (frames < 100) {
    return 0.0;
  }
  const std::size_t begin = frames / 5;
  const std::size_t end = frames - frames / 5;
  int crossings = 0;
  float previous = interleaved[begin * static_cast<std::size_t>(channels)];
  for (std::size_t i = begin + 1; i < end; ++i) {
    const float current = interleaved[i * static_cast<std::size_t>(channels)];
    if (previous <= 0.0f && current > 0.0f) {
      ++crossings;
    }
    previous = current;
  }
  const std::size_t analyzed = end - begin;
  return analyzed == 0 ? 0.0
                       : static_cast<double>(crossings) * sample_rate /
                             static_cast<double>(analyzed);
}

void RunTempoCase(const char* name, double tempo) {
  TimeStretch stretch;
  std::string error;
  const bool reset = stretch.Reset(kSampleRate, kChannels, tempo, &error);
  Check((std::string(name) + "_reset").c_str(), reset, error);
  if (!reset) {
    return;
  }
  const std::vector<float> input = MakeTone();
  std::vector<float> output;
  output.reserve(input.size() * 2);

  constexpr std::size_t kFeedFrames = 1024;
  std::vector<float> chunk(kFeedFrames * kChannels);
  std::vector<float> drained(kFeedFrames * 4 * kChannels);
  for (std::size_t offset = 0; offset < static_cast<std::size_t>(kInputFrames);
       offset += kFeedFrames) {
    const std::size_t frames =
        std::min<std::size_t>(kFeedFrames, kInputFrames - offset);
    std::copy(input.begin() + static_cast<std::ptrdiff_t>(offset * kChannels),
              input.begin() + static_cast<std::ptrdiff_t>((offset + frames) * kChannels),
              chunk.begin());
    stretch.Feed(chunk.data(), frames);
    std::size_t got = 0;
    while ((got = stretch.Receive(drained.data(), kFeedFrames * 4)) > 0) {
      output.insert(output.end(), drained.begin(),
                    drained.begin() + static_cast<std::ptrdiff_t>(got * kChannels));
    }
  }
  stretch.Flush();
  std::size_t got = 0;
  while ((got = stretch.Receive(drained.data(), kFeedFrames * 4)) > 0) {
    output.insert(output.end(), drained.begin(),
                  drained.begin() + static_cast<std::ptrdiff_t>(got * kChannels));
  }

  const double output_frames =
      static_cast<double>(output.size()) / static_cast<double>(kChannels);
  const double ratio = output_frames / static_cast<double>(kInputFrames);
  const double expected_ratio = 1.0 / tempo;
  const double frequency = DominantFrequency(output, kChannels, kSampleRate);
  bool finite = true;
  for (float sample : output) {
    if (!std::isfinite(sample)) {
      finite = false;
      break;
    }
  }
  char detail[256];
  std::snprintf(detail, sizeof(detail),
                "tempo=%.2f frames=%.0f ratio=%.4f expected=%.4f freq=%.2f",
                tempo, output_frames, ratio, expected_ratio, frequency);
  Check((std::string(name) + "_length").c_str(),
        std::abs(ratio - expected_ratio) <= expected_ratio * 0.10, detail);
  Check((std::string(name) + "_pitch").c_str(),
        frequency > 0.0 && std::abs(frequency - kToneHz) <= 5.0, detail);
  Check((std::string(name) + "_finite").c_str(), finite, detail);
}

}  // namespace

int main() {
  std::printf("T3.3b tempo/pitch test (SoundTouch)\n");
  RunTempoCase("tempo_0_5", 0.5);
  RunTempoCase("tempo_1_0", 1.0);
  RunTempoCase("tempo_2_0", 2.0);

  TimeStretch invalid;
  std::string error;
  Check("invalid_rate", !invalid.Reset(0, kChannels, 1.0, &error), error);
  Check("invalid_tempo", !invalid.Reset(kSampleRate, kChannels, 0.0, &error), error);

  TimeStretch clear_test;
  Check("clear_reset",
        clear_test.Reset(kSampleRate, kChannels, 1.0, &error), error);
  clear_test.Clear();
  Check("clear_available", clear_test.Available() == 0,
        std::to_string(clear_test.Available()));

  std::printf("time_stretch_result checks=%d failures=%d\n", g_checks,
              g_failures);
  return g_failures == 0 ? 0 : 1;
}
