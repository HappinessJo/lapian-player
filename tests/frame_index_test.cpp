// T2.2 PTS 索引单测与真实文件集成测试。

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "media/FrameIndex.h"

// T7.2：索引内存按“每帧 16B + keyframe 向量”预算，结构不能悄悄变大。
static_assert(sizeof(lapian::media::FrameIndexEntry) == 16,
              "FrameIndexEntry 必须是 16 字节（T7.2 内存预算）");

namespace {

int g_failures = 0;
bool g_skipped = false;

void Check(bool condition, const std::string& what) {
  if (condition) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n", what.c_str());
    ++g_failures;
  }
}

void Skip(const std::string& what) {
  std::printf("[SKIP] %s\n", what.c_str());
  g_skipped = true;
}

bool FileExists(const std::string& path) {
  const std::u8string utf8(reinterpret_cast<const char8_t*>(path.data()), path.size());
  return std::filesystem::exists(std::filesystem::path(utf8));
}

void TestEntries() {
  std::vector<lapian::media::FrameIndexEntry> entries = {
      {0, true}, {100, false}, {100, false}, {200, true}, {300, false}, {50, false}};
  lapian::media::FrameIndex index;
  std::string error;
  Check(index.BuildFromEntries(entries, &error), "BuildFromEntries");
  Check(index.size() == 5, "重复 PTS 去重后 5 帧");
  Check(index.duplicate_pts_count() == 1, "重复 PTS 计数 1");
  Check(index.IsReliable(), "纯索引可靠");
  Check(index.Total().IsExact() && index.Total().value() == 5, "总帧数精确 5");

  Check(index.PtsOfFrame(0) == 0, "帧 0 PTS=0");
  Check(index.PtsOfFrame(1) == 50, "排序后帧 1 PTS=50");
  Check(index.PtsOfFrame(2) == 100, "帧 2 PTS=100");
  Check(index.PtsOfFrame(99) == std::numeric_limits<std::int64_t>::min(),
        "越界 PTS 返回哨兵");

  Check(index.FrameOfPts(0) == 0, "FrameOfPts(0)=0");
  Check(index.FrameOfPts(120) == 2, "FrameOfPts(120)=2");
  Check(index.FrameOfPts(150) == 2, "FrameOfPts(150) 取较近的 2");
  Check(index.FrameOfPts(-50) == 0, "早于首帧 clamp 到 0");
  Check(index.FrameOfPts(999) == 4, "晚于末帧 clamp 到末帧");

  std::int64_t frame = -1;
  Check(index.FindExactPts(100, &frame) && frame == 2, "精确 PTS 100 -> 帧 2");
  Check(!index.FindExactPts(101, &frame), "PTS 101 无精确匹配");

  Check(index.KeyframeAtOrBefore(0) == 0, "PTS 0 关键帧 0");
  Check(index.KeyframeAtOrBefore(150) == 0, "PTS 150 之前最近关键帧 0");
  Check(index.KeyframeAtOrBefore(200) == 3, "PTS 200 关键帧 3");
  Check(index.KeyframeAtOrBefore(-1) == -1, "首帧前无关键帧");

  Check(index.ValidateDecodedPts(2, 100, 40), "解码 PTS 匹配");
  Check(!index.ValidateDecodedPts(2, 500, 40), "第一次偏差");
  Check(!index.ValidateDecodedPts(2, 500, 40), "第二次偏差");
  Check(!index.ValidateDecodedPts(2, 500, 40), "第三次偏差");
  Check(!index.IsReliable(), "连续 3 次偏差后不可靠");
  Check(index.Total().IsEstimated(), "不可靠后总帧数降级估算");
}

void TestVfrFile() {
  const std::string path = "build/test-media/vfr.mp4";
  if (!FileExists(path)) {
    Skip(path + " 不存在（可选 VFR 合成素材）");
    return;
  }
  lapian::media::FrameIndex index;
  std::string error;
  if (!index.BuildFromFile(path, -1, &error)) {
    Check(false, "VFR 索引: " + error);
    return;
  }
  Check(index.size() >= 10, "VFR 帧数有效");
  Check(index.IsReliable(), "VFR 索引可靠");
  Check(index.Total().IsExact(), "VFR 总帧数精确");

  bool variable = false;
  for (std::size_t i = 2; i < index.size(); ++i) {
    const std::int64_t a = index.PtsOfFrame(static_cast<std::int64_t>(i)) -
                           index.PtsOfFrame(static_cast<std::int64_t>(i - 1));
    const std::int64_t b = index.PtsOfFrame(static_cast<std::int64_t>(i - 1)) -
                           index.PtsOfFrame(static_cast<std::int64_t>(i - 2));
    if (a != b) {
      variable = true;
      break;
    }
  }
  Check(variable, "VFR 帧间隔确实变化");
}

void TestFile(const std::string& path, std::int64_t expected_size) {
  if (!FileExists(path)) {
    Skip(path + " 不存在");
    return;
  }
  lapian::media::FrameIndex index;
  std::string error;
  const bool built = index.BuildFromFile(path, -1, &error);
  Check(built, "索引 " + path);
  if (!built) {
    std::printf("       error=%s\n", error.c_str());
    return;
  }
  Check(index.size() == static_cast<std::size_t>(expected_size),
        path + " 帧数 " + std::to_string(index.size()));
  Check(index.IsReliable(), path + " 索引可靠");
  Check(index.Total().IsExact(), path + " 总帧数精确");
  Check(index.build_ms() >= 0.0, path + " 构建耗时非负");

  const std::int64_t last = static_cast<std::int64_t>(index.size()) - 1;
  Check(index.PtsOfFrame(0) == 0, path + " 首帧 PTS=0");
  Check(index.FrameOfPts(index.PtsOfFrame(last)) == last, path + " 末帧 PTS 往返");
  Check(index.KeyframeAtOrBefore(index.PtsOfFrame(0)) == 0, path + " 首帧是关键帧");
}

void TestCancel() {
  const std::string path = "media/V9.mp4";
  if (!FileExists(path)) {
    Skip(path + " 不存在（可选取消测试素材）");
    return;
  }
  std::atomic<bool> cancel{true};
  lapian::media::FrameIndex index;
  std::string error;
  Check(!index.BuildFromFile(path, -1, &error, &cancel), "取消时 BuildFromFile 返回 false");
  Check(!index.IsReady() && index.empty(), "取消后不留下半成品索引");
  Check(error == "索引已取消", "取消错误信息");
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("T2.2 PTS 索引测试\n");
  TestEntries();
  TestCancel();
  TestFile("media/V9.mp4", 120);
  TestFile("media/proxy/proxy-V1.mp4", 720);
  TestVfrFile();

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  if (g_skipped) {
    std::printf("部分素材跳过\n");
    return 77;
  }
  std::printf("全部通过\n");
  return 0;
}
