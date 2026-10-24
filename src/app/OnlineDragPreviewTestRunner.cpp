#include "app/OnlineDragPreviewTestRunner.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

#include "app/MainWindow.h"
#include "app/OnlineResolveDialog.h"

namespace lapian::app {
namespace {

void Print(const QString& text) {
  std::fprintf(stderr, "%s\n", text.toUtf8().constData());
  std::fflush(stderr);
}

double StreamFps(MainWindow* window) {
  double fps = window->OnlineStreamFpsForTest();
  if (!(fps > 0.0)) {
    fps = window->OnlineVideoFpsForTest();
  }
  if (!(fps > 0.0)) {
    fps = 30.0;
  }
  return fps;
}

}  // namespace

OnlineDragPreviewTestRunner::OnlineDragPreviewTestRunner(
    MainWindow* window, OnlineDragPreviewTestOptions options)
    : window_(window), options_(std::move(options)) {}

void OnlineDragPreviewTestRunner::Check(const char* name, bool ok,
                                        const QString& detail) {
  ++checks_;
  if (!ok) {
    ++failures_;
  }
  Print(QStringLiteral("online_drag_preview_check %1 ok=%2%3")
            .arg(QString::fromUtf8(name))
            .arg(ok ? 1 : 0)
            .arg(detail.isEmpty() ? QString()
                                  : QStringLiteral(" detail=") + detail));
}

void OnlineDragPreviewTestRunner::Start() {
  QTimer::singleShot(0, window_, [this]() { Run(); });
}

bool OnlineDragPreviewTestRunner::WaitUntil(
    const std::function<bool()>& predicate, int timeout_ms, const char* label) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  Print(QStringLiteral("online_drag_preview_wait_timeout label=%1 elapsed=%2ms")
            .arg(QString::fromUtf8(label))
            .arg(timer.elapsed()));
  return predicate();
}

void OnlineDragPreviewTestRunner::Pump(int ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
  }
}

bool OnlineDragPreviewTestRunner::OpenOnline(bool expect_cache_hit,
                                             QString* error) {
  if (options_.throttle_bytes_per_sec > 0) {
    window_->SetOnlineDownloadThrottleForTest(
        options_.throttle_bytes_per_sec);
  }
  window_->OpenOnlineDialogForTest();
  OnlineResolveDialog* dialog = window_->OnlineDialogForTest();
  if (dialog == nullptr) {
    *error = QStringLiteral("对话框未创建");
    return false;
  }
  QString input = options_.bvid;
  if (options_.page > 1) {
    input = QStringLiteral("https://www.bilibili.com/video/%1?p=%2")
                .arg(options_.bvid)
                .arg(options_.page);
  }
  dialog->SetInputForTest(input);
  dialog->StartResolveForTest();
  if (!dialog->WaitForResolveForTest(options_.timeout_ms)) {
    *error = QStringLiteral("解析超时");
    return false;
  }
  if (!dialog->HasResultForTest() || !dialog->ConfirmEnabledForTest()) {
    *error = dialog->ErrorTextForTest();
    if (error->isEmpty()) {
      *error = QStringLiteral("解析失败");
    }
    return false;
  }
  if (expect_cache_hit != dialog->CacheHitForTest()) {
    *error = QStringLiteral("缓存命中状态不符：expect=%1 actual=%2")
                 .arg(expect_cache_hit ? 1 : 0)
                 .arg(dialog->CacheHitForTest() ? 1 : 0);
    return false;
  }
  dialog->ClickConfirmForTest();
  if (!window_->WaitForOnlineReadyForTest(options_.timeout_ms)) {
    *error = window_->OnlineLastErrorForTest();
    if (error->isEmpty()) {
      *error = QStringLiteral("在线首帧超时");
    }
    return false;
  }
  return true;
}

bool OnlineDragPreviewTestRunner::WaitForCachedWindow(qint64* start,
                                                      qint64* end,
                                                      qint64 desired_span,
                                                      int timeout_ms) {
  const bool cache_hit = window_->OnlineCacheHitForTest();
  const qint64 wanted_end = *start + desired_span;
  if (cache_hit) {
    const bool ready = WaitUntil(
        [this, wanted_end]() {
          return window_->IndexReadyForTest() &&
                 window_->LastFrameIndexForTest() >= wanted_end;
        },
        timeout_ms, "cache_hit_index_window");
    if (!ready) {
      return false;
    }
    *end = wanted_end;
    return true;
  }
  const bool covered = WaitUntil(
      [this, wanted_end]() {
        return window_->OnlineFrameCachedForTest(wanted_end);
      },
      timeout_ms, "growing_cached_window");
  if (!covered) {
    // 视频太短/下载太慢时退而求其次：取当前连续已缓存窗口。
    const qint64 last = window_->OnlineLastFrameForTest();
    qint64 discovered_end = *start;
    while (discovered_end + 1 <= last &&
           discovered_end - *start < desired_span * 2 &&
           window_->OnlineFrameCachedForTest(discovered_end + 1)) {
      ++discovered_end;
    }
    if (discovered_end - *start + 1 < 30) {
      return false;
    }
    *end = discovered_end;
    return true;
  }
  *end = wanted_end;
  return true;
}

OnlineDragPreviewTestRunner::DragResult
OnlineDragPreviewTestRunner::RunDragPass(const QString& label, qint64 start,
                                         qint64 end, int samples,
                                         bool cache_hit, bool check_snapshot) {
  DragResult result;
  result.start_frame = start;
  result.end_frame = end;
  result.span_frames = end - start;
  result.samples = samples;
  const auto presented_now = [this, cache_hit]() {
    return cache_hit ? window_->PreviewPresentedForTest()
                     : window_->OnlinePreviewPresentedForTest();
  };
  const auto preview_waiting = [this, cache_hit]() {
    return cache_hit ? false : window_->OnlinePreviewWaitingForTest();
  };

  window_->ResetPreviewStatsForTest();
  const int duration_ms = std::max(200, options_.drag_seconds * 1000);
  QElapsedTimer drag_timer;
  drag_timer.start();
  window_->ScrubStartForTest(start);
  Pump(60);
  result.preview_active_during = window_->PreviewActiveForTest();
  int presented = presented_now();
  int previous_presented = presented;
  qint64 previous_frame = start;
  bool monotonic = true;
  bool snapshot_checked = false;
  qint64 snapshot_paths_before = 0;
  if (check_snapshot) {
    snapshot_paths_before = window_->SnapshotSavedPathsForTest().size();
  }

  for (int i = 0; i < samples; ++i) {
    const qint64 due =
        samples <= 1 ? duration_ms
                     : static_cast<qint64>(duration_ms) * i / (samples - 1);
    while (drag_timer.elapsed() < due) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
      // sleep_for(1ms) 在 Windows 上会睡 ~15.6ms，60 个样本会被拖成 ~31Hz；
      // 这里忙等，保证探针模拟的是 60Hz 鼠标目标而不是定时器粒度。
      std::this_thread::yield();
    }
    const double ratio =
        samples <= 1 ? 1.0 : static_cast<double>(i) / (samples - 1);
    const qint64 frame =
        start + static_cast<qint64>(std::llround((end - start) * ratio));
    QElapsedTimer target_timer;
    target_timer.start();
    const int before_target = presented_now();
    window_->ScrubTargetForTest(frame);
    const qint64 target_ms = target_timer.elapsed();
    if (target_ms > 100) {
      Print(QStringLiteral("online_drag_preview_target_slow label=%1 frame=%2 "
                           "ms=%3")
                .arg(label)
                .arg(frame)
                .arg(target_ms));
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 2);

    const int current_presented = presented_now();
    if (current_presented > previous_presented) {
      result.presented += current_presented - previous_presented;
      if (result.first_present_ms < 0) {
        result.first_present_ms = drag_timer.elapsed();
      }
    }
    // stale 只统计“更新目标之后新呈现的帧”。更新前时钟 tick 呈现的上一目标
    // 帧不是新呈现，不能算到当前目标头上。
    if (!cache_hit && current_presented > before_target) {
      const qint64 shown = window_->OnlinePreviewLastFrameForTest();
      if (result.span_frames >= 0) {
        if (shown > frame) {
          ++result.stale_presented;
        }
        if (shown < previous_frame) {
          monotonic = false;
        }
      } else {
        // 反向拖动：屏幕仍可能停留在上一个目标的帧（落后），这不是“过期
        // 超前帧”；只把比当前目标更靠后的帧记为 stale。真正向前跳另记
        // monotonic=false。
        if (shown > frame) {
          ++result.stale_presented;
        }
        if (shown > previous_frame) {
          monotonic = false;
        }
      }
      previous_frame = shown;
    }
    previous_presented = current_presented;

    if (check_snapshot && !snapshot_checked && i >= samples / 2) {
      snapshot_checked = true;
      window_->FocusVideoForTest();
      window_->CaptureSnapshotForTest();
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      const bool saved_same =
          window_->SnapshotSavedPathsForTest().size() == snapshot_paths_before;
      const bool guarded =
          window_->StatusMessageForTest().contains(
              QStringLiteral("拖动预览中"));
      Check("snapshot_blocked_during_drag", saved_same && guarded,
            window_->StatusMessageForTest());
    }
  }
  while (drag_timer.elapsed() < duration_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  result.presented += presented_now() - previous_presented;
  if (result.presented > 0 && result.first_present_ms < 0) {
    result.first_present_ms = 0;
  }
  const qint64 drag_ms = drag_timer.elapsed();
  const qint64 release_start = drag_timer.elapsed();
  window_->ScrubFinishForTest(end);
  result.release_ms = drag_timer.elapsed() - release_start;
  result.preview_stopped_after = !window_->PreviewActiveForTest();
  result.dropped =
      cache_hit ? 0 : window_->OnlinePreviewDroppedForTest();
  result.ui_gap_max_ms = heartbeat_max_ms_;
  result.ui_gap_over_200 = heartbeat_over_200_;

  const qint64 landing_start = drag_timer.elapsed();
  const bool landed = WaitUntil(
      [this, end]() {
        return window_->CurrentFrameForTest() == end &&
               window_->OnlineSeekIdleForTest();
      },
      std::max(5000, options_.timeout_ms), "exact_landing");
  result.landing_ms = drag_timer.elapsed() - landing_start;
  result.exact_frame = landed;
  result.exact_pts =
      landed && window_->CurrentPtsUsForTest() ==
                    window_->OnlineFramePtsForTest(end);
  result.fps_total =
      drag_ms > 0 ? static_cast<double>(result.presented) * 1000.0 / drag_ms
                  : 0.0;
  if (result.first_present_ms >= 0 && drag_ms > result.first_present_ms + 100) {
    result.fps_active =
        result.presented * 1000.0 /
        static_cast<double>(drag_ms - result.first_present_ms);
  } else {
    result.fps_active = result.fps_total;
  }
  result.ok = landed && result.exact_frame && result.exact_pts &&
              result.stale_presented == 0 && monotonic &&
              result.preview_stopped_after;
  result.monotonic = monotonic;
  Print(QStringLiteral(
            "online_drag_preview_pass label=%1 cache_hit=%2 start=%3 end=%4 "
            "span=%5 samples=%6 presented=%7 fps_total=%8 fps_active=%9 "
            "first_ms=%10 release_ms=%11 landing_ms=%12 stale=%13 dropped=%14 "
            "ui_gap_max_ms=%15 ui_gap_over_200=%16 exact_frame=%17 exact_pts=%18 "
            "preview_active=%19 stopped=%20")
            .arg(label)
            .arg(cache_hit ? 1 : 0)
            .arg(start)
            .arg(end)
            .arg(result.span_frames)
            .arg(samples)
            .arg(result.presented)
            .arg(QString::number(result.fps_total, 'f', 2))
            .arg(QString::number(result.fps_active, 'f', 2))
            .arg(result.first_present_ms)
            .arg(result.release_ms)
            .arg(result.landing_ms)
            .arg(result.stale_presented)
            .arg(result.dropped)
            .arg(result.ui_gap_max_ms)
            .arg(result.ui_gap_over_200)
            .arg(result.exact_frame ? 1 : 0)
            .arg(result.exact_pts ? 1 : 0)
            .arg(result.preview_active_during ? 1 : 0)
            .arg(result.preview_stopped_after ? 1 : 0));
  return result;
}

void OnlineDragPreviewTestRunner::RunShortEdgePass() {
  // 首帧 + 极短拖动：0→0，按下后立即松手；必须仍能精确落回 0。
  window_->ScrubStartForTest(0);
  Pump(40);
  const bool active = window_->PreviewActiveForTest();
  window_->ScrubTargetForTest(0);
  window_->ScrubFinishForTest(0);
  const bool landed = WaitUntil(
      [this]() {
        return window_->OnlineSeekIdleForTest() &&
               window_->CurrentFrameForTest() == 0;
      },
      options_.timeout_ms, "first_frame_short_drag");
  const bool pts =
      window_->CurrentPtsUsForTest() == window_->OnlineFramePtsForTest(0);
  Check("first_frame_short_drag", active && landed && pts,
        QStringLiteral("active=%1 frame=%2 pts=%3 expected=%4")
            .arg(active ? 1 : 0)
            .arg(window_->CurrentFrameForTest())
            .arg(window_->CurrentPtsUsForTest())
            .arg(window_->OnlineFramePtsForTest(0)));

  // 极短反向：0→1→0，立即松手；不允许停在中间帧。
  window_->ScrubStartForTest(0);
  window_->ScrubTargetForTest(1);
  window_->ScrubTargetForTest(0);
  window_->ScrubFinishForTest(0);
  const bool reverse_landed = WaitUntil(
      [this]() {
        return window_->OnlineSeekIdleForTest() &&
               window_->CurrentFrameForTest() == 0;
      },
      options_.timeout_ms, "very_short_reverse");
  Check("very_short_reverse_drag", reverse_landed,
        QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
}

void OnlineDragPreviewTestRunner::RunUncachedPass() {
  if (window_->OnlineCacheCompleteForTest()) {
    Print(QStringLiteral(
        "online_drag_preview_uncached skipped=1 reason=cache_complete"));
    Check("uncached_skipped_cache_complete", true);
    return;
  }
  const qint64 stable = window_->OnlineStableFramesForTest();
  const qint64 last = window_->OnlineLastFrameForTest();
  if (stable <= 1 || last <= 1) {
    Check("uncached_available", false,
          QStringLiteral("stable=%1 last=%2").arg(stable).arg(last));
    return;
  }
  // 选容器估算末帧作为未缓存目标：它一定在稳定前缀之外（除非整片已下载
  // 完成），不会因为下载在 150ms 内追上 stable+30 而误判“已缓存”。
  qint64 uncached_target = last > 1 ? last - 1 : 1;
  if (uncached_target < 1) {
    uncached_target = 1;
  }
  int guard = 0;
  while (uncached_target > 0 &&
         window_->OnlineFrameCachedForTest(uncached_target) && guard++ < 1000) {
    --uncached_target;
  }
  const bool uncached_available =
      !window_->OnlineFrameCachedForTest(uncached_target);
  Check("uncached_available", uncached_available,
        QStringLiteral("target=%1 stable=%2 last=%3")
            .arg(uncached_target)
            .arg(stable)
            .arg(last));
  if (!uncached_available) {
    Print(QStringLiteral(
        "online_drag_preview_uncached skipped=1 reason=no_uncached_target"));
    return;
  }

  const qint64 capped_start = std::min<std::int64_t>(stable - 1, 15);
  qint64 cached_start = std::max<std::int64_t>(0, capped_start);
  if (!window_->OnlineFrameCachedForTest(cached_start)) {
    cached_start = std::max<std::int64_t>(0, stable - 5);
  }
  Check("uncached_cached_start", window_->OnlineFrameCachedForTest(cached_start),
        QStringLiteral("frame=%1").arg(cached_start));

  window_->ResetPreviewStatsForTest();
  window_->ScrubStartForTest(cached_start);
  Pump(60);
  QElapsedTimer target_timer;
  target_timer.start();
  window_->ScrubTargetForTest(uncached_target);
  const qint64 target_ui_ms = target_timer.elapsed();
  const QString status = window_->StatusMessageForTest();
  Check("uncached_preview_prompt",
        status.contains(QStringLiteral("缓冲中（预览）")) &&
            status.contains(QString::number(uncached_target)),
        status);
  Check("uncached_preview_ui_le_100ms", target_ui_ms <= 100,
        QStringLiteral("ms=%1").arg(target_ui_ms));
  Pump(150);
  Check("uncached_preview_waiting", window_->OnlinePreviewWaitingForTest(),
        window_->OnlinePreviewStateForTest());

  // 拖回已缓存区：旧目标取消，预览恢复。
  const int presented_before = window_->OnlinePreviewPresentedForTest();
  window_->ScrubTargetForTest(cached_start);
  const bool resumed = WaitUntil(
      [this, presented_before]() {
        return !window_->OnlinePreviewWaitingForTest() &&
               window_->OnlinePreviewPresentedForTest() > presented_before;
      },
      3000, "uncached_back_to_cached");
  Check("uncached_back_to_cached_not_waiting",
        !window_->OnlinePreviewWaitingForTest(),
        window_->OnlinePreviewStateForTest());
  Check("uncached_back_to_cached_preview_resumes", resumed,
        QStringLiteral("before=%1 after=%2")
            .arg(presented_before)
            .arg(window_->OnlinePreviewPresentedForTest()));
  window_->ScrubFinishForTest(cached_start);
  const bool back_landed = WaitUntil(
      [this, cached_start]() {
        return window_->OnlineSeekIdleForTest() &&
               window_->CurrentFrameForTest() == cached_start;
      },
      options_.timeout_ms, "uncached_back_landing");
  Check("uncached_back_landing", back_landed,
        QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));

  // 未缓存目标松手：只发精确 seek 请求，UI 立即返回；随后用新目标取消在途 seek。
  window_->ScrubStartForTest(cached_start);
  Pump(60);
  window_->ScrubTargetForTest(uncached_target);
  QElapsedTimer release_timer;
  release_timer.start();
  window_->ScrubFinishForTest(uncached_target);
  const qint64 release_ms = release_timer.elapsed();
  Check("uncached_release_ui_le_200ms", release_ms <= 200,
        QStringLiteral("ms=%1").arg(release_ms));
  Check("uncached_release_preview_stopped", !window_->PreviewActiveForTest());
  QElapsedTimer cancel_timer;
  cancel_timer.start();
  window_->GoToFrameForTest(cached_start);
  const qint64 cancel_ui_ms = cancel_timer.elapsed();
  Check("uncached_cancel_ui_le_200ms", cancel_ui_ms <= 200,
        QStringLiteral("ms=%1").arg(cancel_ui_ms));
  const bool cancel_landed = WaitUntil(
      [this, cached_start]() {
        return window_->OnlineSeekIdleForTest() &&
               window_->CurrentFrameForTest() == cached_start;
      },
      options_.timeout_ms, "uncached_cancel_landing");
  Check("uncached_cancel_landing", cancel_landed,
        QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
  Print(QStringLiteral(
            "online_drag_preview_uncached target=%1 cached_start=%2 "
            "target_ui_ms=%3 release_ms=%4 cancel_ui_ms=%5")
            .arg(uncached_target)
            .arg(cached_start)
            .arg(target_ui_ms)
            .arg(release_ms)
            .arg(cancel_ui_ms));
}

void OnlineDragPreviewTestRunner::RunCloseDuringDrag() {
  const qint64 start = window_->CurrentFrameForTest();
  window_->ScrubStartForTest(start);
  Pump(80);
  window_->ScrubTargetForTest(start + 5);
  Pump(80);
  Check("close_drag_preview_active", window_->PreviewActiveForTest(),
        window_->OnlinePreviewStateForTest());
  QElapsedTimer close_timer;
  close_timer.start();
  const bool closed = window_->CloseForTest();
  const qint64 close_ms = close_timer.elapsed();
  Check("close_during_drag", closed);
  Check("close_during_drag_le_1000ms", close_ms <= 1000,
        QStringLiteral("ms=%1").arg(close_ms));
  Check("close_during_drag_preview_stopped",
        !window_->PreviewActiveForTest(),
        window_->OnlinePreviewStateForTest());
  Print(QStringLiteral("online_drag_preview_close ms=%1").arg(close_ms));
}

void OnlineDragPreviewTestRunner::Run() {
  Print(QStringLiteral(
            "online_drag_preview_sample tag=%1 bvid=%2 cid=%3 page=%4 "
            "drag_seconds=%5 samples=%6 cache_hit_only=%7 throttle_bps=%8")
            .arg(options_.tag, options_.bvid)
            .arg(options_.cid)
            .arg(options_.page)
            .arg(options_.drag_seconds)
            .arg(options_.drag_samples)
            .arg(options_.cache_hit_only ? 1 : 0)
            .arg(options_.throttle_bytes_per_sec));

  window_->raise();
  window_->activateWindow();
  window_->FocusVideoForTest();

  // UI 心跳：16ms 精确定时器；>200ms 记一次停顿（OP2/P5 口径）。
  QElapsedTimer heartbeat_clock;
  heartbeat_clock.start();
  qint64 heartbeat_last = 0;
  QTimer heartbeat;
  heartbeat.setInterval(16);
  heartbeat.setTimerType(Qt::PreciseTimer);
  QObject::connect(&heartbeat, &QTimer::timeout, [this, &heartbeat_clock,
                                                 &heartbeat_last]() {
    const qint64 now = heartbeat_clock.elapsed();
    if (heartbeat_last > 0) {
      const qint64 gap = now - heartbeat_last;
      if (gap > 200) {
        ++heartbeat_over_200_;
        Print(QStringLiteral("online_drag_preview_ui_gap_event gap_ms=%1")
                  .arg(gap));
      }
      heartbeat_max_ms_ = std::max(heartbeat_max_ms_, gap);
    }
    heartbeat_last = now;
  });
  heartbeat.start();

  QString open_error;
  const bool opened =
      OpenOnline(options_.cache_hit_only, &open_error);
  Check("online_ready", opened, open_error);
  if (!opened) {
    heartbeat.stop();
    Print(QStringLiteral(
              "online_drag_preview_result checks=%1 failures=%2 verdict=fail")
              .arg(checks_)
              .arg(failures_));
    QCoreApplication::exit(2);
    return;
  }
  Check("cache_hit_matches_mode",
        window_->OnlineCacheHitForTest() == options_.cache_hit_only,
        QStringLiteral("cache_hit=%1 expect=%2")
            .arg(window_->OnlineCacheHitForTest() ? 1 : 0)
            .arg(options_.cache_hit_only ? 1 : 0));
  Check("default_paused", !window_->IsPlayingForTest());
  const double fps = StreamFps(window_);
  const qint64 desired_span = std::max<std::int64_t>(
      60, static_cast<std::int64_t>(std::ceil(fps * 2.0 *
                                             options_.drag_seconds)));
  qint64 start = options_.start_frame;
  qint64 end = start + desired_span;
  const bool window_ready = WaitForCachedWindow(
      &start, &end, desired_span, std::max(10000, options_.timeout_ms));
  Check("cached_window", window_ready,
        QStringLiteral("start=%1 end=%2 desired=%3 last=%4 stable=%5")
            .arg(start)
            .arg(end)
            .arg(desired_span)
            .arg(window_->OnlineLastFrameForTest())
            .arg(window_->OnlineStableFramesForTest()));
  if (!window_ready) {
    heartbeat.stop();
    Print(QStringLiteral(
              "online_drag_preview_result checks=%1 failures=%2 verdict=fail")
              .arg(checks_)
              .arg(failures_));
    QCoreApplication::exit(1);
    return;
  }

  const DragResult forward = RunDragPass(
      QStringLiteral("forward"), start, end, options_.drag_samples,
      options_.cache_hit_only, true);
  Check("forward_preview_active", forward.preview_active_during,
        window_->OnlinePreviewStateForTest());
  Check("forward_preview_presented", forward.presented > 0,
        QStringLiteral("presented=%1").arg(forward.presented));
  Check("forward_preview_fps_ge_30", forward.fps_active >= 30.0,
        QStringLiteral("fps_active=%1 fps_total=%2")
            .arg(QString::number(forward.fps_active, 'f', 2))
            .arg(QString::number(forward.fps_total, 'f', 2)));
  Check("forward_ui_gap_le_200ms", forward.ui_gap_max_ms <= 200,
        QStringLiteral("max_gap_ms=%1 over=%2")
            .arg(forward.ui_gap_max_ms)
            .arg(forward.ui_gap_over_200));
  Check("forward_no_stale_frame", forward.stale_presented == 0,
        QStringLiteral("stale=%1 dropped=%2")
            .arg(forward.stale_presented)
            .arg(forward.dropped));
  Check("forward_exact_landing",
        forward.exact_frame && forward.exact_pts,
        QStringLiteral("frame=%1 pts=%2 expected_pts=%3")
            .arg(window_->CurrentFrameForTest())
            .arg(window_->CurrentPtsUsForTest())
            .arg(window_->OnlineFramePtsForTest(end)));
  Check("forward_preview_stopped", forward.preview_stopped_after);
  Check("forward_no_preview_error",
        window_->OnlinePreviewErrorsForTest() == 0,
        QStringLiteral("errors=%1").arg(window_->OnlinePreviewErrorsForTest()));

  // 反向拖动：只检查单调/精确落帧；帧率不设 ≥30 的硬门槛。
  const qint64 backward_start = end;
  const qint64 backward_end = std::max<std::int64_t>(0, end - 30);
  const DragResult backward = RunDragPass(
      QStringLiteral("backward"), backward_start, backward_end,
      std::min(30, options_.drag_samples), options_.cache_hit_only, false);
  Check("backward_exact_landing",
        backward.exact_frame && backward.exact_pts,
        QStringLiteral("frame=%1 pts=%2")
            .arg(window_->CurrentFrameForTest())
            .arg(window_->CurrentPtsUsForTest()));
  Check("backward_no_stale_frame", backward.stale_presented == 0,
        QStringLiteral("stale=%1").arg(backward.stale_presented));
  Check("backward_monotonic", backward.monotonic);
  Check("backward_preview_stopped", backward.preview_stopped_after);

  if (!options_.cache_hit_only) {
    RunUncachedPass();
  } else {
    Check("cache_hit_no_playurl", window_->OnlinePlayUrlRequestsForTest() == 0,
          QStringLiteral("playurl=%1")
              .arg(window_->OnlinePlayUrlRequestsForTest()));
    Check("cache_hit_no_dash", window_->OnlineDashRequestsForTest() == 0,
          QStringLiteral("dash=%1")
              .arg(window_->OnlineDashRequestsForTest()));
  }

  RunShortEdgePass();
  RunCloseDuringDrag();
  heartbeat.stop();

  Print(QStringLiteral(
            "online_drag_preview_ui_gap max_ms=%1 over_200=%2")
            .arg(heartbeat_max_ms_)
            .arg(heartbeat_over_200_));
  Print(QStringLiteral(
            "online_drag_preview_result checks=%1 failures=%2 verdict=%3")
            .arg(checks_)
            .arg(failures_)
            .arg(failures_ == 0 ? QStringLiteral("pass")
                                : QStringLiteral("fail")));
  QCoreApplication::exit(failures_ == 0 ? 0 : 1);
}

}  // namespace lapian::app
