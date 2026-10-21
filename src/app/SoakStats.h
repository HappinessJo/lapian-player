#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lapian::app {

// T7.3：长时播放采样的纯数据分析，不依赖 Qt/Windows。
// 采样由 MemoryTestRunner 在 UI 线程产生；这里只做趋势/阈值判定，便于单测。
struct SoakSample {
  double t_s = 0.0;             // 相对浸泡开始的秒数
  double ws_mb = 0.0;           // WorkingSetSize（P14 口径）
  double private_mb = 0.0;      // PrivateUsage，诊断用
  double peak_ws_mb = 0.0;      // 进程启动以来峰值
  std::uint32_t handles = 0;    // GetProcessHandleCount
  std::uint32_t threads = 0;    // 当前进程线程数
  std::int64_t frame = 0;       // 当前帧号
  bool playing = false;         // 采样瞬间是否在播放
  int loop_jumps = 0;           // 累计 B→A 跳转次数
  std::uint64_t audio_underruns = 0;
  std::string audio_state;      // closed/opening/playing/paused/silent/error/none
  int cache_entries = 0;        // 纯播放路径应为 0
  double ui_gap_max_ms = 0.0;   // 距上一次 UI 心跳的最大间隔（本次采样窗口）
};

struct SoakThresholds {
  // P14：2h 1080p 连续播放 working set ≤1.2GB。
  double memory_limit_mb = 1200.0;
  // “无持续增长”的自动化口径：先用 5 分钟分块取 WS 最小值（下包络），
  // 再对包络做线性拟合。这样循环边界的瞬时分配尖峰不会把斜率抬高；
  // 原始斜率/首末增量仍保留在报告里，便于对照。
  double growth_limit_mb_per_hour = 16.0;
  double growth_min_delta_mb = 16.0;
  double baseline_block_s = 300.0;
  double baseline_growth_min_mb = 12.0;
  std::uint32_t handle_growth_limit = 64;
  std::uint32_t thread_growth_limit = 2;
  // P5 的“卡顿”口径是 >200ms；浸泡只看是否出现秒级 UI 冻结。
  double ui_gap_stall_ms = 2000.0;
};

struct SoakAnalysis {
  std::size_t total_samples = 0;
  std::size_t steady_samples = 0;  // 去掉 warmup 后的样本
  double duration_s = 0.0;
  double steady_start_s = 0.0;
  double ws_start_mb = 0.0;
  double ws_final_mb = 0.0;
  double ws_max_mb = 0.0;
  double ws_growth_mb = 0.0;
  double ws_slope_mb_per_hour = 0.0;
  std::size_t baseline_blocks = 0;
  double baseline_growth_mb = 0.0;
  double baseline_slope_mb_per_hour = 0.0;
  double private_max_mb = 0.0;
  std::uint32_t handles_start = 0;
  std::uint32_t handles_end = 0;
  std::uint32_t handles_max = 0;
  std::uint32_t threads_start = 0;
  std::uint32_t threads_end = 0;
  std::uint32_t threads_max = 0;
  std::uint64_t audio_underruns_end = 0;
  int cache_max_entries = 0;
  int loop_jumps_end = 0;
  std::size_t playing_samples = 0;
  std::size_t playback_stopped_events = 0;
  std::size_t frame_stall_events = 0;
  double ui_gap_max_ms = 0.0;
  bool memory_over_limit = false;
  bool memory_growth = false;
  bool handle_growth = false;
  bool thread_growth = false;
  bool cache_nonempty = false;
  bool playback_stopped = false;
  bool frame_stall = false;
  bool ui_stall = false;
  bool passed = false;
};

// samples 按 t_s 升序；warmup_s 从第一个样本起算。样本不足或 out 为空时返回 false。
bool AnalyzeSoak(const std::vector<SoakSample>& samples, double warmup_s,
                 const SoakThresholds& thresholds, SoakAnalysis* out);

}  // namespace lapian::app
