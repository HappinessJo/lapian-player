// T2.9 拖动预览解码器：预热、首帧时间、2 秒拖动目标更新下的发布帧率。
// 这是开发机探针，不是 spec 基线机的正式 P3/P4 验收。

#include <algorithm>
#include <chrono>
#include <cstdint>
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

lapian::media::PreviewFrame PollLatest(
    lapian::media::PreviewDecoder* preview, int timeout_ms) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    lapian::media::PreviewFrame frame = preview->TakeLatestFrame();
    if (frame.frame != nullptr) {
      return frame;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return {};
}

// 正向拖动允许发布中间帧；等待目标帧时丢弃中间帧，验证最终能到目标。
lapian::media::PreviewFrame PollTargetFrame(
    lapian::media::PreviewDecoder* preview, std::int64_t target, int timeout_ms) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    lapian::media::PreviewFrame frame = preview->TakeLatestFrame();
    if (frame.frame == nullptr) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    if (frame.frame_index == target) {
      return frame;
    }
    av_frame_free(&frame.frame);
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("T2.9 拖动预览解码测试\n");
  const std::string path =
      argc > 1 ? std::string(argv[1]) : "media/proxy/proxy-V1.mp4";
  std::printf("[info] path=%s\n", path.c_str());

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
  lapian::media::PreviewFrame first = PollTargetFrame(&preview, 600, 2000);
  const double first_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - first_start)
          .count();
  std::printf("[perf] first_preview=%.3fms\n", first_ms);
  Check(first.frame != nullptr, "拖动开始时能取到首张预览帧");
  // 反向拖动起点现在只发布目标帧：长 GOP 下要等解码到目标，当前画面仍由精确帧保持。
  // 短 GOP 素材通常 <100ms；这里用 1000ms 作为长 GOP 不空等的上限。
  Check(first_ms <= 1000.0, "预热后首张目标预览 ≤1000ms（开发机）");
  if (first.frame != nullptr) {
    const int longest = std::max(first.frame->width, first.frame->height);
    Check(first.frame->width > 0 && first.frame->height > 0 && longest <= 1280,
          "预览帧降采样到最长边 ≤1280");
    Check(first.frame_index == 600, "起点预览==目标帧 600");
    av_frame_free(&first.frame);
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
      lapian::media::PreviewFrame frame = preview.TakeLatestFrame();
      if (frame.frame != nullptr) {
        ++published;
        av_frame_free(&frame.frame);
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

  // 反向拖动：600→560 每步等目标帧；必须单调不增、每步正好是目标帧、绝不前跳。
  const auto backward_start = std::chrono::steady_clock::now();
  preview.Begin(600);
  lapian::media::PreviewFrame backward_first = PollTargetFrame(&preview, 600, 2000);
  Check(backward_first.frame != nullptr, "反向拖动起点能取到预览帧");
  if (backward_first.frame != nullptr) {
    Check(backward_first.frame_index == 600, "反向拖动起点==600");
    av_frame_free(&backward_first.frame);
  }

  std::int64_t previous_frame = 600;
  int backward_steps = 0;
  bool exact_target = true;
  bool monotonic = true;
  for (std::int64_t step = 599; step >= 560; --step) {
    preview.UpdateTarget(step);
    lapian::media::PreviewFrame frame = PollTargetFrame(&preview, step, 1000);
    if (frame.frame == nullptr) {
      exact_target = false;
      break;
    }
    if (frame.frame_index != step) {
      exact_target = false;
      std::printf("[info] backward target=%lld got=%lld\n",
                  static_cast<long long>(step),
                  static_cast<long long>(frame.frame_index));
    }
    if (frame.frame_index > previous_frame) {
      monotonic = false;
    }
    previous_frame = frame.frame_index;
    ++backward_steps;
    av_frame_free(&frame.frame);
  }
  const double backward_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - backward_start)
          .count();
  std::printf("[perf] backward_steps=%d total=%.3fms avg=%.3fms\n",
              backward_steps, backward_ms,
              backward_steps > 0 ? backward_ms / backward_steps : 0.0);
  Check(backward_steps == 40, "反向拖动 40 步都能取到帧");
  Check(exact_target, "反向拖动每一步发布的都是目标帧");
  Check(monotonic, "反向拖动发布帧单调不增（无向前跳帧）");

  // 反向后小幅向前：应恢复正向发布，且不残留旧目标帧。
  preview.UpdateTarget(561);
  lapian::media::PreviewFrame forward_again =
      PollTargetFrame(&preview, 561, 2000);
  Check(forward_again.frame != nullptr, "反向拖动后向前一步仍能到目标 561");
  if (forward_again.frame != nullptr) {
    av_frame_free(&forward_again.frame);
  }

  // 快速反向拖动：目标每 16ms 回退 1 帧；UI 可能跳过目标，但发布帧绝不能向前跳。
  preview.Begin(600);
  lapian::media::PreviewFrame rapid_start_frame =
      PollTargetFrame(&preview, 600, 2000);
  if (rapid_start_frame.frame != nullptr) {
    av_frame_free(&rapid_start_frame.frame);
  }
  std::int64_t rapid_target = 599;
  std::int64_t rapid_previous = 600;
  int rapid_frames = 0;
  bool rapid_monotonic = true;
  const auto rapid_start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - rapid_start <
         std::chrono::milliseconds(900)) {
    preview.UpdateTarget(rapid_target);
    const auto slice_end =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
    do {
      lapian::media::PreviewFrame frame = preview.TakeLatestFrame();
      if (frame.frame != nullptr) {
        ++rapid_frames;
        if (frame.frame_index > rapid_previous) {
          rapid_monotonic = false;
        }
        rapid_previous = frame.frame_index;
        av_frame_free(&frame.frame);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < slice_end);
    if (rapid_target > 500) {
      --rapid_target;
    }
  }
  std::printf("[perf] rapid_backward_frames=%d\n", rapid_frames);
  Check(rapid_frames > 0, "快速反向拖动有预览帧发布");
  Check(rapid_monotonic, "快速反向拖动发布帧不向前跳");

  preview.End();
  preview.Close();
  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("全部通过\n");
  return 0;
}
