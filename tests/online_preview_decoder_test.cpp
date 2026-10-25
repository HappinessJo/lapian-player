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
  // baseline=599：起点目标 600 比显示帧新一帧，可正常取到。
  preview.Begin(source, path, stream_index, index, 600, true, 599);
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
              longest <= 1024,
          "预览帧降采样到最长边 ≤1024（64MiB 预算内可缓存 72 帧）");
    Check(first.frame_index == 600, "起点预览帧号 == 目标帧");
    av_frame_free(&first.frame);
  }
  Check(preview.drag_stats().baseline_frame == 599,
        "Begin 记录 displayed baseline");
  Check(preview.drag_stats().last_delivered_before_resume == -1,
        "Begin 记录覆盖前旧值 -1");
  Check(preview.last_seek_keyframe() >= 0 &&
            preview.last_seek_landing_offset() == 0,
        "strict seek 落点 offset=0（不再前一个 GOP）");

  // P0 baseline：目标 650 时先发布 baseline(600) 与 target 之间的中间帧，
  // 不再只等精确目标。
  preview.UpdateTarget(650, true);
  lapian::media::PreviewFrame intermediate = PollLatest(&preview, 3000);
  Check(intermediate.frame != nullptr, "baseline 下首帧中间帧可发布");
  if (intermediate.frame != nullptr) {
    Check(intermediate.frame_index > 600 && intermediate.frame_index < 650,
          "首张中间帧在 baseline 与 target 之间且非 target");
    av_frame_free(&intermediate.frame);
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
  Check(preview.cached_frames() <= 72, "缓存帧数有界（≤72）");
  Check(preview.cache_bytes() <= 64u * 1024u * 1024u,
        "缓存字节数 ≤64MiB");
  Check(preview.cache_peak_bytes() <= 64u * 1024u * 1024u,
        "缓存峰值 ≤64MiB");

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

  // 反向 60Hz 连续拖动：与正向同一目标节奏（每 16ms -1 帧），统计实际
  // 取走的帧；缓存已暖时不允许冻住，且帧号单调不增。
  preview.UpdateTarget(600, true);
  lapian::media::PreviewFrame reverse_warm = PollTarget(&preview, 600, 2000);
  if (reverse_warm.frame != nullptr) {
    av_frame_free(&reverse_warm.frame);
  }
  int reverse_presented = 0;
  bool reverse_monotonic = true;
  std::int64_t reverse_previous = 601;
  const auto reverse_start = std::chrono::steady_clock::now();
  for (int step = 0; step < 60; ++step) {
    const std::int64_t reverse_target = 599 - step;
    preview.UpdateTarget(reverse_target, true);
    const auto slice_end = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(16);
    do {
      lapian::media::PreviewFrame frame = preview.TakeLatestFrame();
      if (frame.frame != nullptr) {
        ++reverse_presented;
        if (frame.frame_index > reverse_previous) {
          reverse_monotonic = false;
        }
        reverse_previous = frame.frame_index;
        av_frame_free(&frame.frame);
      }
      std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < slice_end);
  }
  const double reverse_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                    reverse_start)
          .count();
  const double reverse_fps =
      reverse_presented / std::max(0.001, reverse_seconds);
  std::printf("[perf] online_preview_reverse_fps=%.2f (%d frames / %.3fs)\n",
              reverse_fps, reverse_presented, reverse_seconds);
  Check(reverse_presented >= 30, "反向 60Hz 拖动 presented ≥30（不冻住）");
  Check(reverse_fps >= 30.0, "反向 60Hz 拖动 presented ≥30fps");
  Check(reverse_monotonic, "反向 60Hz 拖动发布帧单调不增");

  // Pause/Resume：松手后保留有界缓存，下一次拖动直接命中；End 释放。
  const int cached_before_pause = preview.cached_frames();
  preview.Pause();
  Check(cached_before_pause > 0 && preview.cached_frames() == cached_before_pause,
        "Pause 保留有界缓存");
  Check(!preview.IsWaiting(), "Pause 后不处于 waiting");
  Check(!preview.IsThreadRunning(), "Pause 后预览线程已退出");
  Check(!preview.IsWarmIdle(), "Pause 后 warm_idle=false");
  preview.Resume(540, true, 539);
  const auto resume_start = std::chrono::steady_clock::now();
  lapian::media::PreviewFrame resumed = PollTarget(&preview, 540, 2000);
  const double resume_ms = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - resume_start)
                               .count();
  Check(resumed.frame != nullptr, "Resume 后目标帧可立即命中缓存");
  AVFrame* prime_source = nullptr;
  if (resumed.frame != nullptr) {
    Check(resumed.from_cache, "Resume 目标帧来自缓存（from_cache=true）");
    prime_source = av_frame_clone(resumed.frame);
  }
  std::printf("[perf] online_preview_resume=%.2fms\n", resume_ms);
  if (resumed.frame != nullptr) {
    av_frame_free(&resumed.frame);
  }
  Check(resume_ms <= 200.0, "Resume 命中缓存首帧 ≤200ms");
  Check(preview.drag_stats().baseline_frame == 539,
        "Resume 记录 displayed baseline");
  Check(preview.drag_stats().last_delivered_before_resume >= 0,
        "Resume 记录覆盖前旧值");

  // P0 PrimeFrame：暂停后暂存，下一次 Resume 打开解码器时异步入缓存；
  // 目标精确命中 from_cache。
  preview.Pause();
  if (prime_source != nullptr) {
    preview.PrimeFrame(prime_source, 650);
    preview.Resume(650, true, 640);
    const auto prime_start = std::chrono::steady_clock::now();
    lapian::media::PreviewFrame primed = PollTarget(&preview, 650, 2000);
    const double prime_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - prime_start)
                                .count();
    Check(primed.frame != nullptr, "PrimeFrame 后目标帧可发布");
    if (primed.frame != nullptr) {
      Check(primed.from_cache, "PrimeFrame 后目标精确命中 from_cache");
      av_frame_free(&primed.frame);
    }
    Check(prime_ms <= 200.0, "PrimeFrame 后目标命中 ≤200ms");
    std::printf("[perf] online_preview_prime=%.2fms primed=%llu\n", prime_ms,
                static_cast<unsigned long long>(preview.primed_frames()));
    Check(preview.primed_frames() > 0, "PrimeFrame 计数 >0");
    av_frame_free(&prime_source);
  } else {
    preview.Resume(650, true, 640);
    Check(false, "保留一张帧用于 PrimeFrame");
  }

  // P0 Warm：静默解到 frame+10，缓存窗口覆盖 [frame-61, frame+10]；
  // 完成后线程/decoder/reader 保持 idle，Resume 复用并命中缓存。
  preview.Pause();
  const auto warm_start = std::chrono::steady_clock::now();
  preview.Warm(600);
  bool warm_idle = false;
  for (int i = 0; i < 1500; ++i) {
    if (preview.IsWarmIdle() && !preview.IsWarming()) {
      warm_idle = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  const double warm_wait_ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - warm_start)
                                  .count();
  std::printf("[perf] online_preview_warm=%.2fms reported=%lld frames=%lld\n",
              warm_wait_ms, static_cast<long long>(preview.warm_ms()),
              static_cast<long long>(preview.warm_frames()));
  Check(warm_idle, "Warm 完成后 IsWarmIdle");
  Check(preview.warm_ms() >= 0, "Warm 记录 warm_ms");
  Check(preview.warm_frames() > 0, "Warm 解码帧数 >0");
  Check(preview.IsFrameCached(539) && preview.IsFrameCached(610),
        "Warm 缓存覆盖 [frame-61, frame+10]");
  // 已覆盖时再次 Warm 应直接 idle 并记 warm_skipped。
  const std::uint64_t skipped_before = preview.warm_skipped();
  preview.Warm(600);
  Check(preview.IsWarmIdle() && !preview.IsWarming(),
        "Warm 已覆盖时直接 idle");
  Check(preview.warm_skipped() == skipped_before + 1,
        "Warm 已覆盖时记 warm_skipped");
  lapian::media::PreviewFrame warm_silent = preview.TakeLatestFrame();
  Check(warm_silent.frame == nullptr, "Warm 静默不发布帧");
  if (warm_silent.frame != nullptr) {
    av_frame_free(&warm_silent.frame);
  }
  preview.Resume(601, true, 600);
  const auto warm_hit_start = std::chrono::steady_clock::now();
  lapian::media::PreviewFrame warm_hit = PollTarget(&preview, 601, 2000);
  const double warm_hit_ms = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() -
                                 warm_hit_start)
                                 .count();
  Check(warm_hit.frame != nullptr, "Warm 后 Resume 目标帧可发布");
  if (warm_hit.frame != nullptr) {
    Check(warm_hit.from_cache, "Warm 后 Resume 目标命中 from_cache");
    av_frame_free(&warm_hit.frame);
  }
  std::printf("[perf] online_preview_warm_hit=%.2fms\n", warm_hit_ms);
  Check(warm_hit_ms <= 100.0, "Warm 后 Resume 命中缓存 ≤100ms");
  Check(preview.drag_stats().open_ms == 0,
        "Warm idle 复用线程 open_ms=0");
  Check(preview.drag_stats().baseline_frame == 600,
        "Warm 后 Resume baseline 正确");

  // 未缓存目标：发 last 与 target 之间最近缓存帧，waiting 保持 true。
  preview.UpdateTarget(650, false);
  Check(preview.IsWaiting(), "未缓存目标标记 waiting");
  lapian::media::PreviewFrame waiting_frame = PollLatest(&preview, 1000);
  Check(waiting_frame.frame != nullptr, "未缓存目标发布最近缓存帧");
  if (waiting_frame.frame != nullptr) {
    Check(waiting_frame.frame_index > 601 && waiting_frame.frame_index < 650,
          "最近缓存帧在 last 与 target 之间");
    Check(waiting_frame.from_cache, "未缓存目标最近帧来自缓存");
    av_frame_free(&waiting_frame.frame);
  }
  Check(preview.IsWaiting(), "未缓存 waiting 保持 true");

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
  Check(preview.cached_frames() == 0, "End 释放帧缓存");
  Check(preview.cache_bytes() == 0, "End 释放缓存字节");
  Check(end_ms <= 1000.0, "End 有界（≤1000ms）");
  preview.Close();

  // 追加 API：Begin(warm_only=true) 直接创建预热器；不发布、不 waiting，
  // 完成后 idle，Resume 复用并命中缓存。
  {
    lapian::media::OnlinePreviewDecoder warm_preview;
    warm_preview.Begin(source, path, stream_index, index, 600, true, 600,
                       /*warm_only=*/true);
    bool begin_warm_idle = false;
    for (int i = 0; i < 1500; ++i) {
      if (warm_preview.IsReady() && warm_preview.IsWarmIdle() &&
          !warm_preview.IsWarming()) {
        begin_warm_idle = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Check(begin_warm_idle, "warm_only Begin 完成后 IsWarmIdle");
    Check(!warm_preview.IsWaiting(), "warm_only Begin 不 waiting");
    Check(warm_preview.IsFrameCached(539) && warm_preview.IsFrameCached(610),
          "warm_only Begin 缓存覆盖 [frame-61, frame+10]");
    lapian::media::PreviewFrame silent = warm_preview.TakeLatestFrame();
    Check(silent.frame == nullptr, "warm_only Begin 静默不发布");
    if (silent.frame != nullptr) {
      av_frame_free(&silent.frame);
    }
    warm_preview.Resume(601, true, 600);
    const auto begin_hit_start = std::chrono::steady_clock::now();
    lapian::media::PreviewFrame begin_hit =
        PollTarget(&warm_preview, 601, 2000);
    const double begin_hit_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - begin_hit_start)
            .count();
    Check(begin_hit.frame != nullptr, "warm_only 后 Resume 目标帧可发布");
    if (begin_hit.frame != nullptr) {
      Check(begin_hit.from_cache, "warm_only 后 Resume 命中 from_cache");
      av_frame_free(&begin_hit.frame);
    }
    std::printf("[perf] online_preview_warm_only_hit=%.2fms\n",
                begin_hit_ms);
    Check(begin_hit_ms <= 100.0, "warm_only 后命中 ≤100ms");
    Check(warm_preview.drag_stats().open_ms == 0,
          "warm_only 后 Resume 复用线程 open_ms=0");
    warm_preview.End();
    Check(!warm_preview.IsThreadRunning(),
          "warm_only End 后线程已退出");
    Check(warm_preview.cached_frames() == 0,
          "warm_only End 释放缓存");
  }

  // P0 线程生命周期回归：open 失败后 Run 返回，但 prefetch_thread 仍在 wait；
  // 旧代码在 Resume/Warm 重启时对 joinable 的 prefetch_thread 赋值会
  // std::terminate。这里要求不崩、End 后线程退出。
  {
    auto failing_source = std::make_shared<lapian::online::GrowingSource>(0);
    failing_source->Publish(0);
    lapian::media::OnlinePreviewDecoder broken;
    broken.Begin(failing_source, "media/p0-does-not-exist.mp4", stream_index,
                 index, 600, true, 599);
    bool run_exited = false;
    for (int i = 0; i < 300; ++i) {
      if (!broken.IsThreadRunning()) {
        run_exited = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Check(run_exited, "open 失败后 Run 线程退出");
    broken.Resume(600, true, 599);
    broken.Warm(600);
    broken.End();
    Check(!broken.IsThreadRunning(),
          "open 失败后 Resume/Warm/End 不崩溃且线程退出");
  }

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("全部通过\n");
  return 0;
}
