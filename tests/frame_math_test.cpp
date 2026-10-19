// T2.1 CFR 帧映射/总帧数/NDF 时间码单测。零依赖，不链接 Qt/FFmpeg。

#include <cmath>
#include <cstdio>
#include <string>

#include "core/FrameMath.h"

namespace {

int g_failures = 0;

using lapian::core::ClampFrame;
using lapian::core::FrameRate;
using lapian::core::Timecode;
using lapian::core::TimecodeFormatter;
using lapian::core::TotalFrameCount;

void Check(bool condition, const std::string& what) {
  if (condition) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n", what.c_str());
    ++g_failures;
  }
}

void CheckEqual(std::int64_t actual, std::int64_t expected, const std::string& what) {
  if (actual == expected) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s (actual=%lld expected=%lld)\n", what.c_str(),
                static_cast<long long>(actual), static_cast<long long>(expected));
    ++g_failures;
  }
}

void CheckTimecode(const Timecode& actual, const std::string& expected, const std::string& what) {
  Check(actual.ToString() == expected, what + " = " + expected);
}

void TestFrameRateBasics() {
  const FrameRate fps24(24, 1);
  Check(fps24.valid(), "24fps 有效");
  CheckEqual(fps24.NominalFps(), 24, "24fps 标称 24");
  Check(!fps24.IsNtsc(), "24fps 不是 NTSC");
  Check(fps24.ToString() == "24", "24fps ToString");
  CheckEqual(fps24.FrameDurationUs(), 41667, "24fps 帧时长 41667us");

  const FrameRate ntsc(24000, 1001);
  Check(ntsc.valid(), "24000/1001 有效");
  Check(ntsc.IsNtsc(), "24000/1001 是 NTSC");
  CheckEqual(ntsc.NominalFps(), 24, "24000/1001 标称 24");
  Check(ntsc.ToString() == "24000/1001", "24000/1001 ToString");
  CheckEqual(ntsc.FrameDurationUs(), 41708, "24000/1001 帧时长 41708us");

  const FrameRate invalid(0, 1001);
  Check(!invalid.valid(), "0/1001 无效");
  CheckEqual(invalid.FrameDurationUs(), 0, "无效帧率帧时长 0");
  CheckEqual(invalid.EstimateFrameCount(5'000'000), 0, "无效帧率估算 0");
  Check(invalid.ToString() == "invalid", "无效帧率 ToString");
}

void TestPtsMapping() {
  const FrameRate fps24(24, 1);
  CheckEqual(fps24.PtsOfFrame(0), 0, "24fps 第 0 帧 PTS");
  CheckEqual(fps24.PtsOfFrame(1), 41667, "24fps 第 1 帧 PTS");
  CheckEqual(fps24.PtsOfFrame(24), 1'000'000, "24fps 第 24 帧 PTS");
  CheckEqual(fps24.FrameOfPts(1'000'000), 24, "24fps PTS 1s 对应第 24 帧");
  CheckEqual(fps24.FrameOfPts(1'000'000, 1'000'000), 0, "带 start_time 的 PTS 映射");
  CheckEqual(fps24.PtsOfFrame(24, 1'000'000), 2'000'000, "带 start_time 的第 24 帧 PTS");
  CheckEqual(fps24.FrameOfPts(0, 1'000'000), -24, "PTS 早于 start_time 得到负帧号");
  CheckEqual(fps24.EstimateFrameCount(5'000'000), 120, "24fps 5s 估算 120 帧");

  const FrameRate ntsc(24000, 1001);
  CheckEqual(ntsc.PtsOfFrame(24), 1'001'000, "23.976 第 24 帧 PTS=1001000us");
  CheckEqual(ntsc.FrameOfPts(1'001'000), 24, "23.976 PTS 1001000us 回到第 24 帧");
  CheckEqual(ntsc.EstimateFrameCount(5'000'000), 120, "23.976 5s 估算 120 帧");

  const FrameRate ntsc30(30000, 1001);
  CheckEqual(ntsc30.NominalFps(), 30, "29.97 标称 30");
  CheckEqual(ntsc30.PtsOfFrame(30), 1'001'000, "29.97 第 30 帧 PTS=1001000us");
  CheckEqual(ntsc30.FrameOfPts(1'001'000), 30, "29.97 PTS 1001000us 回到第 30 帧");

  const FrameRate ntsc60(60000, 1001);
  CheckEqual(ntsc60.NominalFps(), 60, "59.94 标称 60");
  CheckEqual(ntsc60.PtsOfFrame(60), 1'001'000, "59.94 第 60 帧 PTS=1001000us");

  const std::int64_t samples[] = {0, 1, 23, 24, 100, 1000, 100000};
  for (const std::int64_t frame : samples) {
    const std::int64_t pts = ntsc.PtsOfFrame(frame);
    CheckEqual(ntsc.FrameOfPts(pts), frame, "23.976 往返 frame=" + std::to_string(frame));
  }

  CheckEqual(fps24.PtsOfFrame(5, 123), 208'333 + 123, "无效 start 仍保留偏移");
  Check(FrameRate(48, 2).ToString() == "24", "48/2 约分为 24");
}

void TestTimecode() {
  const TimecodeFormatter tc24(FrameRate(24, 1));
  CheckTimecode(tc24.FromFrame(0), "00:00:00:00", "24fps 第 0 帧时间码");
  CheckTimecode(tc24.FromFrame(24), "00:00:01:00", "24fps 第 24 帧时间码");
  CheckTimecode(tc24.FromFrame(137), "00:00:05:17", "24fps 第 137 帧时间码");
  CheckTimecode(tc24.FromFrame(86400), "01:00:00:00", "24fps 1 小时时间码");

  std::int64_t frame = -1;
  Check(tc24.Parse("00:00:05:12", &frame), "24fps 解析 00:00:05:12");
  CheckEqual(frame, 132, "24fps 00:00:05:12 = 第 132 帧");
  Check(tc24.Parse("00:00:05:17", &frame), "24fps 解析 137 的时间码");
  CheckEqual(frame, 137, "24fps 时间码回到 137");
  Check(tc24.Parse(" 00:00:05:12 ", &frame), "时间码允许首尾空白");
  CheckEqual(frame, 132, "去空白后仍是 132");

  const TimecodeFormatter tcNtsc(FrameRate(24000, 1001));
  CheckTimecode(tcNtsc.FromFrame(24), "00:00:01:00", "23.976 NDF 第 24 帧=1s");
  Check(tcNtsc.Parse("00:00:01:00", &frame), "23.976 解析 NDF 00:00:01:00");
  CheckEqual(frame, 24, "23.976 NDF 1s = 第 24 帧");

  const TimecodeFormatter tc2997(FrameRate(30000, 1001));
  CheckTimecode(tc2997.FromFrame(30), "00:00:01:00", "29.97 NDF 第 30 帧=1s");

  const TimecodeFormatter tc60(FrameRate(60, 1));
  CheckTimecode(tc60.FromFrame(59), "00:00:00:59", "60fps 第 59 帧");
  CheckTimecode(tc60.FromFrame(60), "00:00:01:00", "60fps 第 60 帧");
  Check(tc60.Parse("00:00:00:59", &frame), "60fps 解析 00:00:00:59");
  CheckEqual(frame, 59, "60fps 59 帧");

  const char* invalid[] = {
      "00:00:05:24",  // FF 越界
      "00:60:00:00",  // MM 越界
      "00:00:60:00",  // SS 越界
      "00:00:05",     // 缺字段
      "00:00:05:12:00",  // 多字段
      "aa:00:00:00",  // 非数字
      "00:00:05:-1",  // 负数
      "",              // 空
  };
  for (const char* text : invalid) {
    Check(!tc24.Parse(text, &frame), std::string("24fps 拒绝非法时间码: ") + text);
  }

  Check(!FrameRate(0, 0).valid(), "无效帧率不能格式化时间码");
  CheckTimecode(tc24.FromFrame(-1), "00:00:00:00", "负帧号时间码归零");
}

void TestTotalFrames() {
  Check(!TotalFrameCount::Unknown().has_value(), "Unknown 无值");
  Check(TotalFrameCount::Unknown().Display() == "—", "Unknown 显示 —");
  CheckEqual(TotalFrameCount::Unknown().LastFrame(), -1, "Unknown 末帧 -1");
  Check(!TotalFrameCount::Scanning().has_value(), "Scanning 无值");
  Check(TotalFrameCount::Scanning().Display() == "扫描中", "Scanning 显示 扫描中");

  const TotalFrameCount estimated = TotalFrameCount::Estimated(120);
  Check(estimated.IsEstimated(), "Estimated 状态");
  CheckEqual(estimated.value(), 120, "Estimated 值 120");
  CheckEqual(estimated.LastFrame(), 119, "Estimated 末帧 119");
  Check(estimated.Display() == "≈120", "Estimated 显示 ≈120");

  const TotalFrameCount exact = TotalFrameCount::Exact(120);
  Check(exact.IsExact(), "Exact 状态");
  Check(exact.Display() == "120", "Exact 显示 120");

  const FrameRate fps24(24, 1);
  const TotalFrameCount from_container = TotalFrameCount::FromContainer(120, 5'000'000, fps24);
  Check(from_container.IsExact(), "nb_frames 可信时精确");
  CheckEqual(from_container.value(), 120, "nb_frames=120");

  const TotalFrameCount from_duration = TotalFrameCount::FromContainer(0, 5'000'000, fps24);
  Check(from_duration.IsEstimated(), "无 nb_frames 时用时长估算");
  CheckEqual(from_duration.value(), 120, "5s@24 估算 120");

  const TotalFrameCount no_duration = TotalFrameCount::FromContainer(0, 0, fps24);
  Check(!no_duration.has_value(), "无 nb_frames/时长时 Unknown");

  const TotalFrameCount from_last =
      TotalFrameCount::FromLastFramePts(4'958'333, 0, fps24);
  Check(from_last.IsExact(), "末帧 PTS 校正为 Exact");
  CheckEqual(from_last.value(), 120, "末帧 PTS 4.958333s -> 120 帧");

  CheckEqual(ClampFrame(-5, 119), 0, "clamp 负帧到 0");
  CheckEqual(ClampFrame(0, 119), 0, "clamp 0");
  CheckEqual(ClampFrame(50, 119), 50, "clamp 中间");
  CheckEqual(ClampFrame(119, 119), 119, "clamp 末帧");
  CheckEqual(ClampFrame(120, 119), 119, "clamp 越界到末帧");
  CheckEqual(ClampFrame(5, -1), 0, "末帧未知时 clamp 到 0");
}

}  // namespace

int main() {
  std::printf("T2.1 CFR 帧映射/时间码测试\n");
  TestFrameRateBasics();
  TestPtsMapping();
  TestTimecode();
  TestTotalFrames();

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("全部通过\n");
  return 0;
}
