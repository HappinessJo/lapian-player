#include "app/SoakStats.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

  // 5 分钟分块下包络：取每块最小 WS，避免循环边界的瞬时分配尖峰/末端高点
  // 主导斜率。持续增长应表现为“每块最低水位”整体抬升。
  if (steady.size() >= 2 && thresholds.baseline_block_s > 0.0) {
    const double block_s = thresholds.baseline_block_s;
    const double origin = steady.front()->t_s;
    std::vector<double> block_min;
    std::vector<double> block_time;
    for (const SoakSample* sample : steady) {
      const double offset = sample->t_s - origin;
      const std::size_t index =
          static_cast<std::size_t>(offset / block_s);
      if (index >= block_min.size()) {
        block_min.resize(index + 1,
                         std::numeric_limits<double>::infinity());
        block_time.resize(
            index + 1,
            origin + static_cast<double>(index) * block_s);
      }
      block_min[index] = std::min(block_min[index], sample->ws_mb);
    }
    std::vector<double> envelope_time;
    std::vector<double> envelope_ws;
    envelope_time.reserve(block_min.size());
    envelope_ws.reserve(block_min.size());
    for (std::size_t i = 0; i < block_min.size(); ++i) {
      if (std::isfinite(block_min[i])) {
        envelope_time.push_back(block_time[i]);
        envelope_ws.push_back(block_min[i]);
      }
    }
    out->baseline_blocks = envelope_ws.size();
    if (envelope_ws.size() >= 2) {
      out->baseline_growth_mb = envelope_ws.back() - envelope_ws.front();
      double x_mean = 0.0;
      double y_mean = 0.0;
      for (std::size_t i = 0; i < envelope_ws.size(); ++i) {
        x_mean += (envelope_time[i] - envelope_time.front()) / 3600.0;
        y_mean += envelope_ws[i];
      }
      x_mean /= static_cast<double>(envelope_ws.size());
      y_mean /= static_cast<double>(envelope_ws.size());
      double numerator = 0.0;
      double denominator = 0.0;
      for (std::size_t i = 0; i < envelope_ws.size(); ++i) {
        const double x =
            (envelope_time[i] - envelope_time.front()) / 3600.0 - x_mean;
        const double y = envelope_ws[i] - y_mean;
        numerator += x * y;
        denominator += x * x;
      }
      if (denominator > 0.0) {
        out->baseline_slope_mb_per_hour = numerator / denominator;
      }
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
      out->baseline_blocks >= 3 &&
      out->baseline_growth_mb >= thresholds.baseline_growth_min_mb &&
      out->baseline_slope_mb_per_hour >
          thresholds.growth_limit_mb_per_hour;
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
