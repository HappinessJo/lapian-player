// T7.3 浸泡分析单测：稳态斜率、绝对增量门槛、句柄/线程增长、缓存非空、
// 帧停顿、播放中断、UI 心跳超时、P14 内存上限与样本不足。
// 纯 C++，不依赖 Qt/Windows/素材。

#include <cstdio>
#include <vector>

#include "app/SoakStats.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
  if (condition) {
    std::printf("[ OK ] %s\n", what);
  } else {
    std::printf("[FAIL] %s\n", what);
    ++g_failures;
  }
}

using lapian::app::AnalyzeSoak;
using lapian::app::SoakAnalysis;
using lapian::app::SoakSample;
using lapian::app::SoakThresholds;

std::vector<SoakSample> MakeSamples(int count, double interval_s = 30.0) {
  std::vector<SoakSample> samples;
  samples.reserve(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    SoakSample sample;
    sample.t_s = static_cast<double>(i) * interval_s;
    sample.ws_mb = 100.0;
    sample.private_mb = 130.0;
    sample.peak_ws_mb = 160.0;
    sample.handles = 500;
    sample.threads = 12;
    sample.loop_jumps = 0;
    sample.playing = true;
    sample.cache_entries = 0;
    sample.ui_gap_max_ms = 12.0;
    samples.push_back(sample);
  }
  return samples;
}

void SetPlaybackFrames(std::vector<SoakSample>* samples, double fps = 30.0,
                       double loop_frames = 4000.0) {
  for (SoakSample& sample : *samples) {
    const double frame_position = sample.t_s * fps;
    const int loops = static_cast<int>(frame_position / loop_frames);
    sample.loop_jumps = loops;
    sample.frame =
        static_cast<std::int64_t>(frame_position) -
        static_cast<std::int64_t>(loops * loop_frames);
  }
}

SoakAnalysis Analyze(const std::vector<SoakSample>& samples,
                     double warmup_s = 60.0) {
  SoakAnalysis analysis;
  SoakThresholds thresholds;
  if (!AnalyzeSoak(samples, warmup_s, thresholds, &analysis)) {
    std::printf("[FAIL] AnalyzeSoak 返回 false\n");
    ++g_failures;
  }
  return analysis;
}

}  // namespace

int main() {
  // 1. 2h 稳态：ws 小幅抖动、帧持续推进，应通过。
  {
    auto samples = MakeSamples(121);
    SetPlaybackFrames(&samples);
    for (std::size_t i = 0; i < samples.size(); ++i) {
      samples[i].ws_mb = 100.0 + ((i % 4 == 0) ? 0.4 : -0.3);
    }
    const SoakAnalysis analysis = Analyze(samples);
    Check(analysis.total_samples == 121, "2h 样本数 121");
    Check(analysis.steady_samples >= 100, "warmup 后样本 >=100");
    Check(analysis.duration_s > 3599.0, "时长约 3600s");
    Check(!analysis.memory_over_limit, "2h 稳态未超 P14 上限");
    Check(!analysis.memory_growth, "2h 稳态无持续增长");
    Check(!analysis.handle_growth, "2h 稳态句柄不增长");
    Check(!analysis.thread_growth, "2h 稳态线程不增长");
    Check(!analysis.cache_nonempty, "纯播放帧缓存为空");
    Check(!analysis.frame_stall, "帧号持续推进");
    Check(!analysis.ui_stall, "UI 心跳正常");
    Check(analysis.passed, "2h 稳态判定通过");
  }

  // 2. 线性泄漏 40MB/h：斜率与绝对增量都超限。
  {
    auto samples = MakeSamples(121);
    SetPlaybackFrames(&samples);
    for (SoakSample& sample : samples) {
      sample.ws_mb = 100.0 + 40.0 * (sample.t_s / 3600.0);
    }
    const SoakAnalysis analysis = Analyze(samples);
    Check(analysis.memory_growth, "40MB/h 泄漏被识别");
    Check(analysis.ws_slope_mb_per_hour > 35.0,
          "泄漏斜率 >35MB/h");
    Check(!analysis.passed, "泄漏样本判定失败");
  }

  // 3. 绝对增量低于 16MB：即使短窗口斜率偏大也不判泄漏。
  {
    auto samples = MakeSamples(13, 30.0);
    SetPlaybackFrames(&samples);
    for (SoakSample& sample : samples) {
      sample.ws_mb = 100.0 + 2.0 * (sample.t_s / 360.0);
    }
    const SoakAnalysis analysis = Analyze(samples, 30.0);
    Check(analysis.ws_slope_mb_per_hour >= 16.0,
          "短窗口斜率超过阈值");
    Check(analysis.ws_growth_mb < 16.0, "绝对增量低于门槛");
    Check(!analysis.memory_growth, "绝对增量门槛抑制误报");
    Check(analysis.passed, "小幅一次性增量不判失败");
  }

  // 4. 句柄增长超过 64。
  {
    auto samples = MakeSamples(121);
    SetPlaybackFrames(&samples);
    for (std::size_t i = 0; i < samples.size(); ++i) {
      samples[i].handles = 500 + static_cast<std::uint32_t>(i);
    }
    const SoakAnalysis analysis = Analyze(samples);
    Check(analysis.handle_growth, "句柄单调增长被识别");
    Check(!analysis.passed, "句柄增长判定失败");
  }

  // 5. 线程增长超过 2。
  {
    auto samples = MakeSamples(121);
    SetPlaybackFrames(&samples);
    for (std::size_t i = 0; i < samples.size(); ++i) {
      samples[i].threads = 12 + static_cast<std::uint32_t>(i / 40);
    }
    const SoakAnalysis analysis = Analyze(samples);
    Check(analysis.thread_growth, "线程增长被识别");
    Check(!analysis.passed, "线程增长判定失败");
  }

  // 6. 纯播放路径出现缓存条目。
  {
    auto samples = MakeSamples(121);
    SetPlaybackFrames(&samples);
    samples[10].cache_entries = 2;
    const SoakAnalysis analysis = Analyze(samples);
    Check(analysis.cache_nonempty, "播放路径缓存非空被识别");
    Check(analysis.cache_max_entries == 2, "缓存条目峰值记录");
    Check(!analysis.passed, "缓存非空判定失败");
  }

  // 7. 超过 P14 1.2GB。
  {
    auto samples = MakeSamples(121);
    SetPlaybackFrames(&samples);
    for (SoakSample& sample : samples) {
      sample.ws_mb = 1300.0;
    }
    const SoakAnalysis analysis = Analyze(samples);
    Check(analysis.memory_over_limit, "超过 1.2GB 被识别");
    Check(!analysis.passed, "超内存上限判定失败");
  }

  // 8. 播放中帧号连续 2 个采样窗口不变。
  {
    auto samples = MakeSamples(121);
    SetPlaybackFrames(&samples);
    for (int i = 20; i < 25; ++i) {
      samples[static_cast<std::size_t>(i)].frame = samples[19].frame;
      samples[static_cast<std::size_t>(i)].loop_jumps = samples[19].loop_jumps;
    }
    const SoakAnalysis analysis = Analyze(samples);
    Check(analysis.frame_stall_events >= 2, "帧停顿事件被记录");
    Check(analysis.frame_stall, "帧停顿被识别");
    Check(!analysis.passed, "帧停顿判定失败");
  }

  // 9. 播放意外停止。
  {
    auto samples = MakeSamples(121);
    SetPlaybackFrames(&samples);
    samples[60].playing = false;
    const SoakAnalysis analysis = Analyze(samples);
    Check(analysis.playback_stopped_events == 1, "播放停止事件记录");
    Check(analysis.playback_stopped, "播放停止被识别");
    Check(!analysis.passed, "播放停止判定失败");
  }

  // 10. UI 心跳超过 2000ms。
  {
    auto samples = MakeSamples(121);
    SetPlaybackFrames(&samples);
    samples[30].ui_gap_max_ms = 5000.0;
    const SoakAnalysis analysis = Analyze(samples);
    Check(analysis.ui_stall, "UI 心跳超时被识别");
    Check(!analysis.passed, "UI 心跳超时判定失败");
  }

  // 11. 样本不足。
  {
    auto samples = MakeSamples(3);
    SetPlaybackFrames(&samples);
    const SoakAnalysis analysis = Analyze(samples, 60.0);
    Check(analysis.steady_samples == 1, "warmup 后仅 1 个样本");
    Check(!analysis.passed, "样本不足判定失败");
  }

  // 12. 乱序输入仍按时间排序分析。
  {
    auto samples = MakeSamples(121);
    SetPlaybackFrames(&samples);
    std::vector<SoakSample> reversed(samples.rbegin(), samples.rend());
    const SoakAnalysis analysis = Analyze(reversed);
    Check(analysis.duration_s > 3599.0, "乱序样本按时间重排");
    Check(analysis.passed, "乱序稳态样本仍通过");
  }

  // 13. 循环边界周期性 +20MB 尖峰、基线下包络平稳：不判持续增长。
  {
    auto samples = MakeSamples(121);
    SetPlaybackFrames(&samples);
    for (std::size_t i = 0; i < samples.size(); ++i) {
      samples[i].ws_mb = 100.0 + 5.0 * (samples[i].t_s / 3600.0);
      if (i % 10 == 0) {
        samples[i].ws_mb += 20.0;
      }
    }
    const SoakAnalysis analysis = Analyze(samples);
    Check(analysis.ws_growth_mb > 12.0, "尖峰使原始首末增量变大");
    Check(analysis.baseline_blocks >= 10, "下包络分块 >=10");
    Check(analysis.baseline_growth_mb < 12.0, "下包络增长低于门槛");
    Check(!analysis.memory_growth, "周期性尖峰不判持续增长");
    Check(analysis.passed, "有界锯齿判定通过");
  }

  // 14. 20MB/h 慢泄漏：下包络也持续抬升。
  {
    auto samples = MakeSamples(121);
    SetPlaybackFrames(&samples);
    for (SoakSample& sample : samples) {
      sample.ws_mb = 100.0 + 20.0 * (sample.t_s / 3600.0);
    }
    const SoakAnalysis analysis = Analyze(samples);
    Check(analysis.baseline_slope_mb_per_hour > 16.0,
          "慢泄漏下包络斜率超限");
    Check(analysis.baseline_growth_mb >= 12.0,
          "慢泄漏下包络增量超限");
    Check(analysis.memory_growth, "20MB/h 慢泄漏被识别");
    Check(!analysis.passed, "慢泄漏判定失败");
  }

  if (g_failures == 0) {
    std::printf("[ OK ] T7.3 soak_stats 全部通过\n");
    return 0;
  }
  std::printf("[FAIL] T7.3 soak_stats 失败 %d 项\n", g_failures);
  return 1;
}
