// T4.1 A/B 区间数据单测：入出点校验、I 晚于 B 清 B、循环开关、清除。
// 纯 C++，无 Qt/FFmpeg 依赖。

#include <cstdio>
#include <string>

#include "core/LoopRange.h"

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
  if (condition) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n", what.c_str());
    ++g_failures;
  }
}

void CheckResult(lapian::core::LoopEditResult actual,
                 lapian::core::LoopEditResult expected,
                 const std::string& what) {
  Check(actual == expected, what);
}

}  // namespace

int main() {
  using lapian::core::LoopEditResult;
  using lapian::core::LoopRange;

  std::printf("T4.1 A/B 区间测试\n");

  LoopRange range;
  Check(!range.has_in() && !range.has_out(), "初始没有 A/B");
  Check(!range.complete() && !range.enabled(), "初始不完整且循环关闭");
  CheckResult(range.Toggle(), LoopEditResult::kNeedsRange, "未设 A/B 时 L 被拒绝");
  Check(!range.enabled(), "拒绝后循环仍关闭");

  CheckResult(range.SetOut(160), LoopEditResult::kNeedsIn, "未设 A 时拒绝设 B");
  Check(!range.has_out(), "拒绝设 B 后仍无出点");

  CheckResult(range.SetIn(100), LoopEditResult::kSet, "设置入点 A=100");
  Check(range.has_in() && range.in() == 100, "A=100 已记录");
  Check(!range.complete(), "只有 A 时区间不完整");

  CheckResult(range.SetOut(90), LoopEditResult::kOutBeforeIn, "B=90 早于 A=100 被拒绝");
  Check(!range.has_out() && range.in() == 100, "拒绝后 A 不变且仍无 B");

  CheckResult(range.SetOut(100), LoopEditResult::kSet, "B=A=100 合法");
  Check(range.complete() && range.is_single_frame(), "A=B 是单帧区间");
  Check(!range.enabled(), "设置 B 不会自动开启循环");

  CheckResult(range.Toggle(), LoopEditResult::kSet, "完整 A/B 可以开启循环");
  Check(range.enabled(), "循环已开启");

  // F4-A8：循环开启时尝试 B < A，原区间与循环状态都不被破坏。
  CheckResult(range.SetOut(90), LoopEditResult::kOutBeforeIn, "循环中 B<A 仍被拒绝");
  Check(range.in() == 100 && range.out() == 100 && range.enabled(),
        "F4-A8 拒绝后原 A=B 与循环状态不变");

  CheckResult(range.SetOut(160), LoopEditResult::kSet, "循环中把 B 改为 160");
  Check(range.enabled() && range.out() == 160, "调整 B 后循环保持开启");

  CheckResult(range.SetIn(120), LoopEditResult::kSet, "循环中把 A 改为 120");
  Check(range.enabled() && range.in() == 120 && range.out() == 160,
        "A<=B 时调整 A 保持循环");

  Check(range.Contains(120) && range.Contains(160), "闭区间包含 A 与 B");
  Check(!range.Contains(119) && !range.Contains(161), "区间外帧不包含");

  // plan §10 第 1 条：I 晚于当前 B，接受 A 并清除 B、关闭循环。
  CheckResult(range.SetIn(200), LoopEditResult::kClearedOut,
              "I 晚于 B 时清除出点");
  Check(range.has_in() && range.in() == 200, "新入点 200 已生效");
  Check(!range.has_out() && !range.complete(), "出点已被清除");
  Check(!range.enabled(), "出点被清除后循环自动关闭");

  CheckResult(range.Toggle(), LoopEditResult::kNeedsRange, "A/B 不完整时不能重新开循环");
  Check(!range.enabled(), "仍保持循环关闭");

  CheckResult(range.SetOut(260), LoopEditResult::kSet, "重新设置 B=260");
  CheckResult(range.Toggle(), LoopEditResult::kSet, "重新开启循环");
  Check(range.enabled(), "循环重新开启");

  range.Clear();
  Check(!range.has_in() && !range.has_out(), "清除后无 A/B");
  Check(!range.complete() && !range.enabled(), "清除后循环关闭（F4-A4 数据面）");

  CheckResult(range.SetIn(-1), LoopEditResult::kInvalidFrame, "负入点被拒绝");
  CheckResult(range.SetOut(-1), LoopEditResult::kInvalidFrame, "负出点被拒绝");
  Check(!range.has_in() && !range.has_out(), "非法帧号不改变状态");

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("全部通过\n");
  return 0;
}
