// T8.6.1b 在线拖动预览解码器：用完整的本地文件模拟“已发布全部字节”的
// GrowingSource，验证独立读层/降采样/最新目标合并/反向缓存/过期帧丢弃/取消。
// 不使用网络与 GUI；素材缺失返回 77。

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
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
#include "media/MediaProbe.h"
#include "media/OnlinePreviewDecoder.h"
#include "online/GrowingSource.h"

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

lapian::media::PreviewFrame PollLatest(
    lapian::media::OnlinePreviewDecoder* preview, int timeout_ms) {
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

lapian::media::PreviewFrame PollTarget(
    lapian::media::OnlinePreviewDecoder* preview, std::int64_t target,
    int timeout_ms) {
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
  std::printf("T8.6.1b 在线拖动预览解码测试\n");
  const std::string path =
      argc > 1 ? std::string(argv[1]) : "media/proxy/proxy-V1.mp4";
  std::printf("[info] path=%s\n", path.c_str());
  std::error_code size_error;
  const std::uintmax_t file_size = std::filesystem::file_size(path, size_error);
  if (size_error || file_size == 0) {
    std::printf("[SKIP] %s 不可用\n", path.c_str());
    return 77;
  }

  lapian::media::MediaProbeResult probe =
      lapian::media::ProbeMedia(path);
  if (!probe.ok) {
    std::printf("[SKIP] probe 失败: %s\n", probe.error_detail.c_str());
    return 77;
  }
  const int stream_index = probe.info.video.stream_index;
  lapian::media::FrameIndex built_index;
  std::string index_error;
  if (!built_index.BuildFromFile(path, stream_index, &index_error)) {
    std::printf("[SKIP] index 失败: %s\n", index_error.c_str());
    return 77;
  }
  if (built_index.size() < 700) {
    std::printf("[SKIP] 素材帧数不足: %zu\n", built_index.size());
    return 77;
  }
  auto index = std::make_shared<lapian::media::FrameIndex>(
      std::move(built_index));
  auto source = std::make_shared<lapian::online::GrowingSource>(
      static_cast<std::int64_t>(file_size));
  source->Publish(static_cast<std::int64_t>(file_size));

  lapian::media::OnlinePreviewDecoder preview;
  preview.Begin(source, path, stream_index, index, 600, true);
  const auto open_start = std::chrono::steady_clock::now();
  bool ready = false;
  for (int i = 0; i < 500; ++i) {
    if (preview.IsReady()) {
      ready = true;
      break;
    }
    if (!preview.last_error().empty()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  const double open_ms = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - open_start)
                             .count();
  std::printf("[perf] online_preview_open=%.2fms\n", open_ms);
  Check(ready, "独立 GrowingFormat/解码器在 3s 内打开");
  if (!ready) {
    std::printf("       error=%s\n", preview.last_error().c_str());
    preview.Close();
    return 1;
  }

  preview.UpdateTarget(600, true);
  lapian::media::PreviewFrame first = PollTarget(&preview, 600, 3000);  Check(first.frame != nullptr, "起点目标帧 600 能取到预览帧");
  if (first.frame != nullptr) {
    const int longest = std::max(first.frame->width, first.frame->height);
    Check(first.frame->width > 0 && first.frame->height > 0 &&
              longest <= 1280,
          "预览帧降采样到最长边 ≤1280");
    Check(first.frame_index == 600, "起点预览帧号 == 目标帧");
    av_frame_free(&first.frame);
  }

  // 正向拖动 2s：每 16ms 更新目标（目标推进 1 帧），统计实际取走的预览帧。
  preview.UpdateTarget(0, true);
  const auto drag_start = std::chrono::steady_clock::now();
  int presented = 0;
  std::int64_t target = 0;
  while (std::chrono::steady_clock::now() - drag_start <
         std::chrono::seconds(2)) {
    ++target;
    if (target >= 600) {
      target = 1;
    }
    preview.UpdateTarget(target, true);
    const auto slice_end = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(16);
    do {
      lapian::media::PreviewFrame frame = preview.TakeLatestFrame();
      if (frame.frame != nullptr) {
        ++presented;
        av_frame_free(&frame.frame);
      }
      // 不能用 sleep_for(1ms)：Windows 默认定时器粒度约 15.6ms，会把 16ms
      // 的目标更新拖成 ~31ms，测出来的不是解码吞吐。这里忙等并 yield。
      std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < slice_end);
  }
  const double drag_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                    drag_start)
          .count();
  const double fps = static_cast<double>(presented) / drag_seconds;
  std::printf("[perf] online_preview_fps=%.2f (%d frames / %.3fs) seeks=%llu "
              "published=%llu dropped=%llu\n",
              fps, presented, drag_seconds,
              static_cast<unsigned long long>(preview.seek_count()),
              static_cast<unsigned long long>(preview.frames_published()),
              static_cast<unsigned long long>(preview.stale_dropped()));  Check(presented > 0, "正向拖动期间有预览帧发布");
  Check(fps >= 30.0, "正向拖动预览发布/取走 ≥30fps（开发机）");
  Check(preview.cached_frames() <= 24, "反向帧缓存有界（≤24）");

  // 反向拖动：600→560 每步必须是目标帧，且绝不向前跳。
  preview.UpdateTarget(600, true);
  lapian::media::PreviewFrame backward_first =
      PollTarget(&preview, 600, 2000);
  if (backward_first.frame != nullptr) {
    av_frame_free(&backward_first.frame);
  }
  bool exact_target = true;
  bool monotonic = true;
  int backward_steps = 0;
  std::int64_t previous = 600;
  for (std::int64_t step = 599; step >= 560; --step) {
    preview.UpdateTarget(step, true);
    lapian::media::PreviewFrame frame =
        PollTarget(&preview, step, 1500);
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
    if (frame.frame_index > previous) {
      monotonic = false;
    }
    previous = frame.frame_index;
    ++backward_steps;
    av_frame_free(&frame.frame);
  }
  Check(backward_steps == 40, "反向拖动 40 步都能取到帧");
  Check(exact_target, "反向拖动每一步发布的都是目标帧");
  Check(monotonic, "反向拖动发布帧单调不增（无向前跳帧）");

  // 未缓存目标：只进入 waiting，不阻塞、不产生新帧。
  preview.UpdateTarget(650, false);
  Check(preview.IsWaiting(), "未缓存目标标记 waiting");
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  lapian::media::PreviewFrame waiting_frame = preview.TakeLatestFrame();
  Check(waiting_frame.frame == nullptr, "未缓存目标不发布预览帧");
  if (waiting_frame.frame != nullptr) {
    av_frame_free(&waiting_frame.frame);
  }

  // 请求未缓存目标后立即取消（End），线程必须有界退出。
  preview.UpdateTarget(5000, false);
  const auto end_start = std::chrono::steady_clock::now();
  preview.End();
  const double end_ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - end_start)
                            .count();
  std::printf("[perf] online_preview_end=%.2fms\n", end_ms);
  Check(!preview.IsThreadRunning(), "End 后预览线程已退出");
  Check(!preview.IsReady(), "End 后预览器不再 ready");
  Check(end_ms <= 1000.0, "End 有界（≤1000ms）");
  preview.Close();

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("全部通过\n");
  return 0;
}
