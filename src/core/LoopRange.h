#pragma once

#include <cstdint>

namespace lapian::core {

// T4.1 A/B 循环区间（纯数据，不依赖 Qt）。
// 规则对齐 spec §3.4 与 plan §10：
// - 出点必须 >= 入点；早于入点的出点被拒绝，原区间不变（F4-A8）。
// - 入点晚于已设出点：接受入点并清除出点（prompt，plan §10 第 1 条），
//   不自动交换、不静默改成 A=B。
// - 只有 A、B 都设置后才能开启循环；清除 A/B 同时关闭循环（F4-A4）。
// - A=B 仍是合法区间；定帧播放行为在 T4.2 接入，T4.1 只负责数据与显示。
enum class LoopEditResult {
  kSet,           // 设置成功，或循环开关切换成功
  kClearedOut,    // 入点已接受，原出点因此被清除
  kOutBeforeIn,   // 出点早于入点，拒绝且不改变任何状态
  kNeedsIn,       // 尚未设置入点，拒绝设置出点
  kNeedsRange,    // A/B 不完整，拒绝开启循环
  kInvalidFrame,  // 负帧号，拒绝
};

class LoopRange {
 public:
  bool has_in() const { return has_in_; }
  bool has_out() const { return has_out_; }
  bool complete() const { return has_in_ && has_out_; }
  bool enabled() const { return enabled_; }
  bool is_single_frame() const { return complete() && in_ == out_; }

  std::int64_t in() const { return in_; }
  std::int64_t out() const { return out_; }

  // 设置入点（A）。若已有出点且新入点晚于出点，出点被清除并关闭循环。
  LoopEditResult SetIn(std::int64_t frame) {
    if (frame < 0) {
      return LoopEditResult::kInvalidFrame;
    }
    has_in_ = true;
    in_ = frame;
    if (has_out_ && out_ < in_) {
      has_out_ = false;
      out_ = 0;
      enabled_ = false;
      return LoopEditResult::kClearedOut;
    }
    return LoopEditResult::kSet;
  }

  // 设置出点（B）。必须先有入点；B < A 时拒绝，已有 A/B 与循环状态不变。
  LoopEditResult SetOut(std::int64_t frame) {
    if (frame < 0) {
      return LoopEditResult::kInvalidFrame;
    }
    if (!has_in_) {
      return LoopEditResult::kNeedsIn;
    }
    if (frame < in_) {
      return LoopEditResult::kOutBeforeIn;
    }
    has_out_ = true;
    out_ = frame;
    return LoopEditResult::kSet;
  }

  // L：只有完整 A/B 才能开启；不完整时拒绝且保持关闭。
  LoopEditResult Toggle() {
    if (!complete()) {
      enabled_ = false;
      return LoopEditResult::kNeedsRange;
    }
    enabled_ = !enabled_;
    return LoopEditResult::kSet;
  }

  // Shift+L：清除 A/B，并自动关闭循环（F4-A4）。
  void Clear() {
    has_in_ = false;
    has_out_ = false;
    enabled_ = false;
    in_ = 0;
    out_ = 0;
  }

  // T4.2 循环跳转使用；只有完整区间才包含帧。
  bool Contains(std::int64_t frame) const {
    return complete() && frame >= in_ && frame <= out_;
  }

 private:
  std::int64_t in_ = 0;
  std::int64_t out_ = 0;
  bool has_in_ = false;
  bool has_out_ = false;
  bool enabled_ = false;
};

}  // namespace lapian::core
