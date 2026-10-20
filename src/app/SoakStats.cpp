#include "app/SoakStats.h"

#include <algorithm>
#include <cmath>

namespace lapian::app {
namespace {

double ClampNonNegative(double value) {
  return value > 0.0 ? value : 0.0;
}

}  // namespace

bool AnalyzeSoak(const std::vector<SoakSample>& samples, double warmup_s,
                 const SoakThresholds& thresholds, SoakAnalysis* out) {
  if (out == nullptr || samples.empty()) {
    return false;
  }
  *out = SoakAnalysis{};

  std::vector<SoakSample> sorted = samples;
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const SoakSample& a, const SoakSample& b) {
                     return a.t_s < b.t_s;
                   });

  out->total_samples = sorted.size();
  out->duration_s = ClampNonNegative(sorted.back().t_s - sorted.front().t_s);
  for (const SoakSample& sample : sorted) {
    out->ws_max_mb = std::max(out->ws_max_mb, sample.ws_mb);
    out->private_max_mb = std::max(out->private_max_mb, sample.private_mb);
    out->handles_max = std::max(out->handles_max, sample.handles);
    out->threads_max = std::max(out->threads_max, sample.threads);
    out->cache_max_entries = std::max(out->cache_max_entries, sample.cache_entries);
    if (sample.cache_entries > 0) {
      // 纯播放路径任何时刻都不应写帧缓存（含 warmup 前）。
      out->cache_nonempty = true;
    }
    if (sample.playing) {
      ++out->playing_samples;
    }
  }
  out->audio_underruns_end = sorted.back().audio_underruns;
  out->loop_jumps_end = sorted.back().loop_jumps;

  const double steady_start =
      sorted.front().t_s + ClampNonNegative(warmup_s);
  std::vector<const SoakSample*> steady;
  steady.reserve(sorted.size());
  for (const SoakSample& sample : sorted) {
    if (sample.t_s >= steady_start) {
      steady.push_back(&sample);
    }
  }
  out->steady_samples = steady.size();
  out->steady_start_s = steady.empty() ? sorted.back().t_s : steady.front()->t_s;

  if (!steady.empty()) {
    out->ws_start_mb = steady.front()->ws_mb;
    out->ws_final_mb = steady.back()->ws_mb;
    out->handles_start = steady.front()->handles;
    out->handles_end = steady.back()->handles;
    out->threads_start = steady.front()->threads;
    out->threads_end = steady.back()->threads;
    for (const SoakSample* sample : steady) {
      out->ui_gap_max_ms =
          std::max(out->ui_gap_max_ms, sample->ui_gap_max_ms);
    }
  }

  out->ws_growth_mb = out->ws_final_mb - out->ws_start_mb;

  // 稳态窗口的最小二乘斜率（x=小时，y=MB）。
  if (steady.size() >= 2) {
    double x_mean = 0.0;
    double y_mean = 0.0;
    for (const SoakSample* sample : steady) {
      x_mean += (sample->t_s - steady.front()->t_s) / 3600.0;
      y_mean += sample->ws_mb;
    }
    x_mean /= static_cast<double>(steady.size());
    y_mean /= static_cast<double>(steady.size());
    double numerator = 0.0;
    double denominator = 0.0;
    for (const SoakSample* sample : steady) {
      const double x = (sample->t_s - steady.front()->t_s) / 3600.0 - x_mean;
      const double y = sample->ws_mb - y_mean;
      numerator += x * y;
      denominator += x * x;
    }
    if (denominator > 0.0) {
      out->ws_slope_mb_per_hour = numerator / denominator;
    }
  }

  for (const SoakSample* sample : steady) {
    if (!sample->playing) {
      ++out->playback_stopped_events;
    }
    if (sample->ui_gap_max_ms > thresholds.ui_gap_stall_ms) {
      out->ui_stall = true;
    }
  }

  // 帧号连续不变（且没有发生 B→A 回绕）超过一个采样窗口，才算一次停顿。
  std::size_t consecutive_stalls = 0;
  for (std::size_t i = 1; i < sorted.size(); ++i) {
    const SoakSample& previous = sorted[i - 1];
    const SoakSample& current = sorted[i];
    const bool can_compare = previous.playing && current.playing &&
                             (current.t_s - previous.t_s) >= 5.0;
    if (can_compare && current.frame == previous.frame &&
        current.loop_jumps == previous.loop_jumps) {
      ++consecutive_stalls;
      ++out->frame_stall_events;
    } else {
      consecutive_stalls = 0;
    }
    if (consecutive_stalls >= 2) {
      out->frame_stall = true;
    }
  }

  out->memory_over_limit = out->ws_max_mb > thresholds.memory_limit_mb;
  out->memory_growth =
      steady.size() >= 3 &&
      out->ws_growth_mb >= thresholds.growth_min_delta_mb &&
      out->ws_slope_mb_per_hour > thresholds.growth_limit_mb_per_hour;
  out->handle_growth =
      steady.size() >= 2 &&
      out->handles_end > out->handles_start + thresholds.handle_growth_limit;
  out->thread_growth =
      steady.size() >= 2 &&
      out->threads_end > out->threads_start + thresholds.thread_growth_limit;
  out->playback_stopped = out->playback_stopped_events > 0;

  out->passed = out->total_samples >= 3 && out->steady_samples >= 3 &&
                !out->memory_over_limit && !out->memory_growth &&
                !out->handle_growth && !out->thread_growth &&
                !out->cache_nonempty && !out->playback_stopped &&
                !out->frame_stall && !out->ui_stall;
  return true;
}

}  // namespace lapian::app
