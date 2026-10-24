#pragma once

#include <cstdint>

namespace lapian::media {

// DP-3：本地拖动预览的显式交互策略。拖动不是一种行为，而是四种：
//
//   Exact   慢拖/小步：优先精确目标帧；允许等待精确帧；不使用粗帧干扰。
//   Fast    快拖/大跳：允许中间帧/缓存/粗帧；允许目标滞后；不冻住 UI。
//   Reverse 反向：缓存命中优先；未命中仍走 DP-2 的精确/远跳粗帧路径。
//   FarJump 远跳：目标一次跳 ≥300 帧，先粗帧后精确；粗帧永远不是最终帧。
//
// 判定只看可测量输入：本次目标步长（abs(step)）、方向、距上次目标的时间、
// 连续同向次数。阈值是常量，探针/诊断日志原样输出，避免“感觉流畅”。
enum class DragMode {
  Exact = 0,
  Fast = 1,
  Reverse = 2,
  FarJump = 3,
};

// 每次目标更新步长 ≤4 帧视为“慢拖/小步”：硬件路径单步解码成本远低于
// 一次 tick，优先精确帧；不因为粗帧/中间帧打断精确目标。
constexpr std::int64_t kDragExactMaxStep = 4;
// 与 DP-2 远跳粗帧阈值一致：≥300 帧先发粗帧，再解精确目标。
constexpr std::int64_t kDragFarJumpStep = 300;

struct DragInput {
  std::int64_t target = 0;
  std::int64_t previous_target = 0;
  // abs(target - previous_target)；Begin 时为 0。
  std::int64_t step = 0;
  // +1 向前（帧号增大）、-1 向后、0 起点/未知。
  int direction = 0;
  // 距上一次目标更新的时间；0 表示起点/未知。
  std::int64_t since_previous_ms = 0;
  // 连续同向的目标更新次数（含本次）；方向变化/起点为 1。
  int same_direction_run = 1;
};

struct DragDecision {
  DragMode mode = DragMode::Exact;
  // 静态字符串，直接写进诊断/探针；不分配、不在 UI 线程拼接。
  const char* reason = "begin";
  // step / since_previous_ms；起点或同目标更新为 0。
  double speed_frames_per_s = 0.0;
};

// 纯函数：不依赖 Qt/FFmpeg/线程，产品与单测共用。阈值变化必须同步更新
// docs/perf/drag-preview-dp3.md 与探针输出，不允许静默改。
DragDecision ClassifyDrag(const DragInput& input);

const char* DragModeName(DragMode mode);

}  // namespace lapian::media
