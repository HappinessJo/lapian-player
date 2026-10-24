#include "media/DragStrategy.h"

namespace lapian::media {

DragDecision ClassifyDrag(const DragInput& input) {
  DragDecision decision;
  const std::int64_t step = input.step >= 0 ? input.step : -input.step;
  if (input.since_previous_ms > 0) {
    decision.speed_frames_per_s =
        static_cast<double>(step) * 1000.0 /
        static_cast<double>(input.since_previous_ms);
  }

  // 远跳优先于方向：向前/向后一次跳 ≥300 帧都先粗后精。
  if (step >= kDragFarJumpStep) {
    decision.mode = DragMode::FarJump;
    decision.reason = "far_jump_step_ge_300";
    return decision;
  }
  // 慢拖/小步（含 +1/-1/±3）：精确优先，关闭粗帧替换。
  if (step <= kDragExactMaxStep) {
    decision.mode = DragMode::Exact;
    decision.reason = input.direction == 0 ? "begin" : "small_step_le_4";
    return decision;
  }
  if (input.direction < 0) {
    decision.mode = DragMode::Reverse;
    decision.reason = input.same_direction_run >= 3 ? "reverse_sustained"
                                                     : "reverse_step_gt_4";
    return decision;
  }
  if (input.direction > 0) {
    decision.mode = DragMode::Fast;
    decision.reason = input.same_direction_run >= 3 ? "forward_sustained"
                                                     : "forward_step_gt_4";
    return decision;
  }
  // 方向未知但步长 >4：当作精确路径处理，避免没有依据地放粗帧。
  decision.mode = DragMode::Exact;
  decision.reason = "unknown_direction";
  return decision;
}

const char* DragModeName(DragMode mode) {
  switch (mode) {
    case DragMode::Exact:
      return "exact";
    case DragMode::Fast:
      return "fast";
    case DragMode::Reverse:
      return "reverse";
    case DragMode::FarJump:
      return "far_jump";
  }
  return "exact";
}

}  // namespace lapian::media
