// T2.9 拖动预览解码器：预热、首帧时间、2 秒拖动目标更新下的发布帧率。
// 这是开发机探针，不是 spec 基线机的正式 P3/P4 验收。

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
extern "C" {
#include <libavutil/frame.h>
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "media/FrameIndex.h"
#include "media/PreviewDecoder.h"

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

void Skip(const std::string& what) {
  std::printf("[SKIP] %s\n", what.c_str());
}

AVFrame* PollLatest(lapian::media::PreviewDecoder* preview, int timeout_ms) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    AVFrame* frame = preview->TakeLatestFrame();
    if (frame != nullptr) {
      return frame;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return nullptr;
}

}  // namespace

int main() {
  std::printf("T2.9 拖动预览解码测试\n");
  const std::string path = "media/proxy/proxy-V1.mp4";

  lapian::media::FrameIndex index;
  std::string error;
  if (!index.BuildFromFile(path, -1, &error)) {
    Skip(path + " 不可用: " + error);
    return 77;
  }

  lapian::media::PreviewDecoder preview;
  const auto configure_start = std::chrono::steady_clock::now();
  preview.Configure(path, -1, &index);

  bool ready = false;
  for (int i = 0; i < 200; ++i) {
    if (preview.IsReady()) {
      ready = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  const double open_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - configure_start)
          .count();
  std::printf("[perf] preview_open=%.3fms\n", open_ms);
  Check(ready, "预览解码器在 2s 内后台打开");
  if (!ready) {
    std::printf("       error=%s\n", preview.last_error().c_str());
    preview.Close();
    return 1;
  }

  const auto first_start = std::chrono::steady_clock::now();
  preview.Begin(600);
  AVFrame* first = PollLatest(&preview, 2000);
  const double first_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - first_start)
          .count();
  std::printf("[perf] first_preview=%.3fms\n", first_ms);
  Check(first != nullptr, "拖动开始时能取到首张预览帧");
  Check(first_ms <= 100.0, "预热后首张预览 ≤100ms（开发机）");
  if (first != nullptr) {
    const int longest = std::max(first->width, first->height);
    Check(first->width > 0 && first->height > 0 && longest <= 1280,
          "预览帧降采样到最长边 ≤1280");
    av_frame_free(&first);
  }

  // 模拟 2 秒拖动：每 16ms 更新目标帧，并在 16ms 窗口内取走最新预览帧。
  preview.Begin(0);
  const auto drag_start = std::chrono::steady_clock::now();
  int published = 0;
  std::int64_t target = 0;
  while (std::chrono::steady_clock::now() - drag_start <
         std::chrono::seconds(2)) {
    target = (target + 7) % 720;
    preview.UpdateTarget(target);
    const auto slice_end =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
    do {
      AVFrame* frame = preview.TakeLatestFrame();
      if (frame != nullptr) {
        ++published;
        av_frame_free(&frame);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < slice_end);
  }
  const double drag_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - drag_start)
          .count();
  const double fps = static_cast<double>(published) / drag_seconds;
  std::printf("[perf] preview_fps=%.2f (%d frames / %.3fs)\n", fps, published,
              drag_seconds);
  Check(published > 0, "拖动期间有预览帧发布");
  Check(fps >= 30.0, "拖动预览发布 ≥30fps（开发机）");

  preview.End();
  preview.Close();
  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("全部通过\n");
  return 0;
}
