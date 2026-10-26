#include "app/OnlineDragPreviewTestRunner.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

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

double Percentile(std::vector<double> values, double fraction) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  std::size_t index = static_cast<std::size_t>(
      static_cast<double>(values.size() - 1) * fraction);
  if (index >= values.size()) {
    index = values.size() - 1;
  }
  return values[index];
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
  // P0：连续多少个目标更新没有新画面（只打印）。
  int presented_at_last_target = presented;
  int missing_run = 0;
  int max_missing_run = 0;
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
    if (i > 0) {
      // P0：上一个目标窗口内没有新呈现 → 连续缺失计数。
      if (presented_now() == presented_at_last_target) {
        ++missing_run;
        max_missing_run = std::max(max_missing_run, missing_run);
      } else {
        missing_run = 0;
      }
    }
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
    presented_at_last_target = current_presented;

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
  result.max_missing_run = max_missing_run;
  const OnlineDragStatsSnapshot drag_stats = window_->OnlineDragStatsForTest();
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
  const qint64 p0_warm_skipped =
      static_cast<qint64>(window_->OnlinePreviewWarmSkippedForTest());
  const qint64 p0_warm_failures =
      static_cast<qint64>(window_->OnlinePreviewWarmFailuresForTest());
  const qint64 p0_seek_fallbacks =
      static_cast<qint64>(window_->OnlinePreviewSeekFallbacksForTest());
  const qint64 p0_primed =
      static_cast<qint64>(window_->OnlinePreviewPrimedFramesForTest());
  Print(QStringLiteral(
            "online_drag_p0_drag scenario=%1 mode=online first_publish_ms=%2 "
            "first_delivered_ms=%3 open_ms=%4 last_before_resume=%5 "
            "published_before_release=%6 decoder_published=%7 "
            "decoder_delivered=%8 max_missing_run=%9 baseline=%10 "
            "warm_ms=%11 warm_frames=%12 warm_skipped=%13 "
            "warm_failures=%14 seek_fallbacks=%15 seek_keyframe=%16 "
            "seek_offset=%17 primed=%18")
            .arg(label)
            .arg(drag_stats.first_publish_ms)
            .arg(drag_stats.first_delivered_ms)
            .arg(drag_stats.open_ms)
            .arg(drag_stats.last_delivered_before_resume)
            .arg(drag_stats.published_before_release)
            .arg(drag_stats.published)
            .arg(drag_stats.delivered)
            .arg(result.max_missing_run)
            .arg(drag_stats.baseline_frame)
            .arg(window_->OnlinePreviewWarmMsForTest())
            .arg(window_->OnlinePreviewWarmFramesForTest())
            .arg(p0_warm_skipped)
            .arg(p0_warm_failures)
            .arg(p0_seek_fallbacks)
            .arg(window_->OnlinePreviewLastSeekKeyframeForTest())
            .arg(window_->OnlinePreviewLastSeekLandingOffsetForTest())
            .arg(p0_primed));
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

  // 拖回已缓存区：旧目标取消，预览恢复。回到的帧可能仍停留在屏幕上
  // （未缓存目标期间画面保持），此时不重复呈现同一帧也算恢复；以
  // waiting=false + target/显示帧都回到 cached_start 为准。
  const int presented_before = window_->OnlinePreviewPresentedForTest();
  window_->ScrubTargetForTest(cached_start);
  const bool resumed = WaitUntil(
      [this, cached_start]() {
        return !window_->OnlinePreviewWaitingForTest() &&
               window_->OnlinePreviewTargetForTest() == cached_start &&
               window_->OnlinePreviewLastFrameForTest() == cached_start;
      },
      3000, "uncached_back_to_cached");
  Check("uncached_back_to_cached_not_waiting",
        !window_->OnlinePreviewWaitingForTest(),
        window_->OnlinePreviewStateForTest());
  Check("uncached_back_to_cached_preview_resumes", resumed,
        QStringLiteral("before=%1 after=%2 last=%3")
            .arg(presented_before)
            .arg(window_->OnlinePreviewPresentedForTest())
            .arg(window_->OnlinePreviewLastFrameForTest()));
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

int OnlineDragPreviewTestRunner::PresentedNow() const {
  return options_.local_only ? window_->PreviewPresentedForTest()
                             : window_->OnlinePreviewPresentedForTest();
}

qint64 OnlineDragPreviewTestRunner::DisplayedNow() const {
  if (options_.local_only) {
    return window_->PreviewDiagnosticsForTest().displayed_frame;
  }
  return window_->OnlinePreviewLastFrameForTest();
}

bool OnlineDragPreviewTestRunner::CoarseNow() const {
  if (options_.local_only) {
    return window_->PreviewDiagnosticsForTest().displayed_coarse;
  }
  return window_->OnlinePreviewLastCoarseForTest();
}

bool OnlineDragPreviewTestRunner::CacheNow() const {
  if (options_.local_only) {
    return window_->PreviewDiagnosticsForTest().displayed_from_cache;
  }
  return window_->OnlinePreviewLastFromCacheForTest();
}

int OnlineDragPreviewTestRunner::StaleNow() const {
  if (options_.local_only) {
    return 0;
  }
  return static_cast<int>(window_->OnlinePreviewStaleDroppedForTest());
}

bool OnlineDragPreviewTestRunner::ParityLanding(qint64 target,
                                                ParityResult* result) {
  QElapsedTimer release_timer;
  release_timer.start();
  window_->ScrubFinishForTest(target);
  result->release_ms = release_timer.elapsed();
  QElapsedTimer landing_timer;
  landing_timer.start();
  const bool local = options_.local_only;
  const bool landed = WaitUntil(
      [this, local, target]() {
        if (window_->CurrentFrameForTest() != target) {
          return false;
        }
        return local || window_->OnlineSeekIdleForTest();
      },
      5000, "parity_exact_landing");
  result->landing_ms = landing_timer.elapsed();
  result->landing_frame_ok = landed;
  result->landing_frame = window_->CurrentFrameForTest();
  result->landing_pts = window_->CurrentPtsUsForTest();
  // P0：本次拖动的只读度量快照（OnScrubFinished 在 Pause 前保存）。
  const OnlineDragStatsSnapshot drag_stats = window_->OnlineDragStatsForTest();
  result->open_ms = drag_stats.open_ms;
  result->first_publish_ms = drag_stats.first_publish_ms;
  result->first_delivered_ms = drag_stats.first_delivered_ms;
  result->last_delivered_before_resume =
      drag_stats.last_delivered_before_resume;
  result->published_before_release = drag_stats.published_before_release;
  result->decoder_published = drag_stats.published;
  result->decoder_delivered = drag_stats.delivered;
  result->baseline_frame = drag_stats.baseline_frame;
  result->warm_ms = window_->OnlinePreviewWarmMsForTest();
  result->warm_frames = window_->OnlinePreviewWarmFramesForTest();
  result->warm_skipped =
      static_cast<qint64>(window_->OnlinePreviewWarmSkippedForTest());
  result->warm_failures =
      static_cast<qint64>(window_->OnlinePreviewWarmFailuresForTest());
  result->seek_fallbacks =
      static_cast<qint64>(window_->OnlinePreviewSeekFallbacksForTest());
  result->seek_keyframe = window_->OnlinePreviewLastSeekKeyframeForTest();
  result->seek_offset = window_->OnlinePreviewLastSeekLandingOffsetForTest();
  result->primed_frames =
      static_cast<qint64>(window_->OnlinePreviewPrimedFramesForTest());
  if (local) {
    result->landing_pts_ok = landed;
  } else {
    result->landing_pts_ok =
        landed &&
        window_->CurrentPtsUsForTest() == window_->OnlineFramePtsForTest(target);
  }
  return landed && result->landing_pts_ok;
}

void OnlineDragPreviewTestRunner::PrintParity(const ParityResult& result) {
  Print(QStringLiteral(
            "online_drag_parity scenario=%1 mode=%2 start=%3 end=%4 "
            "updates=%5 presented=%6 first_response_ms=%7 "
            "first_from_start_ms=%8 response_p95_ms=%9 max_response_ms=%10 "
            "max_gap_ms=%11 active_fps=%12 target_lag_p95=%13 "
            "stale_dropped=%14 coarse_presented=%15 cache_presented=%16 "
            "max_missing=%17 flicker=%18 release_ms=%19 landing_ms=%20 "
            "landing_frame=%21 landing_pts=%22 landing_frame_ok=%23 "
            "landing_pts_ok=%24 ui_gap_ms=%25 exact_ms=%26 ok=%27 "
            "open_ms=%28 first_publish_ms=%29 first_delivered_ms=%30 "
            "last_before_resume=%31 published_before_release=%32 "
            "decoder_published=%33 decoder_delivered=%34 "
            "baseline=%35 warm_ms=%36 warm_frames=%37 warm_skipped=%38 "
            "warm_failures=%39 seek_fallbacks=%40 seek_keyframe=%41 "
            "seek_offset=%42 primed=%43")
            .arg(result.label)
            .arg(result.mode)
            .arg(result.start_frame)
            .arg(result.end_frame)
            .arg(result.updates)
            .arg(result.presented)
            .arg(QString::number(result.first_response_ms, 'f', 2))
            .arg(QString::number(result.first_from_start_ms, 'f', 2))
            .arg(QString::number(result.response_p95_ms, 'f', 2))
            .arg(QString::number(result.max_response_ms, 'f', 2))
            .arg(QString::number(result.max_gap_ms, 'f', 2))
            .arg(QString::number(result.active_fps, 'f', 2))
            .arg(QString::number(result.target_lag_p95, 'f', 2))
            .arg(result.stale_dropped)
            .arg(result.coarse_presented)
            .arg(result.cache_presented)
            .arg(result.max_consecutive_missing)
            .arg(result.flicker)
            .arg(result.release_ms)
            .arg(result.landing_ms)
            .arg(result.landing_frame)
            .arg(result.landing_pts)
            .arg(result.landing_frame_ok ? 1 : 0)
            .arg(result.landing_pts_ok ? 1 : 0)
            .arg(result.ui_gap_ms)
            .arg(result.exact_ms)
            .arg(result.ok ? 1 : 0)
            .arg(result.open_ms)
            .arg(result.first_publish_ms)
            .arg(result.first_delivered_ms)
            .arg(result.last_delivered_before_resume)
            .arg(result.published_before_release)
            .arg(result.decoder_published)
            .arg(result.decoder_delivered)
            .arg(result.baseline_frame)
            .arg(result.warm_ms)
            .arg(result.warm_frames)
            .arg(result.warm_skipped)
            .arg(result.warm_failures)
            .arg(result.seek_fallbacks)
            .arg(result.seek_keyframe)
            .arg(result.seek_offset)
            .arg(result.primed_frames));
  parity_ui_gap_max_ms_ = std::max(parity_ui_gap_max_ms_, result.ui_gap_ms);
}

OnlineDragPreviewTestRunner::ParityResult
OnlineDragPreviewTestRunner::RunParityFrames(
    const QString& label, qint64 start, qint64 end,
    const std::vector<qint64>& targets, const std::vector<int>& due_ms,
    bool forward, int gap_limit_ms, int initial_pump_ms) {
  ParityResult result;
  result.label = label;
  result.mode = options_.local_only ? QStringLiteral("local")
                                    : QStringLiteral("online");
  result.start_frame = start;
  result.end_frame = end;
  result.updates = static_cast<int>(targets.size());
  heartbeat_max_ms_ = 0;
  heartbeat_over_200_ = 0;
  window_->ResetPreviewStatsForTest();
  const int stale_base = StaleNow();
  QElapsedTimer drag_clock;
  drag_clock.start();
  window_->ScrubStartForTest(start);
  Pump(initial_pump_ms);
  const int base_presented = PresentedNow();
  int last_presented = base_presented;
  int presented_at_last_target = base_presented;
  int unanswered = 0;
  int missing_run = 0;
  qint64 first_target_time = -1;
  qint64 last_present_time = -1;
  qint64 last_displayed = -1;
  std::vector<qint64> target_times;
  std::vector<double> lags;

  const auto poll = [&](qint64 current_target) {
    const int now_presented = PresentedNow();
    if (now_presented <= last_presented) {
      return;
    }
    const qint64 now = drag_clock.elapsed();
    const int delta = now_presented - last_presented;
    for (int i = 0; i < delta; ++i) {
      if (unanswered < static_cast<int>(target_times.size())) {
        const double latency =
            static_cast<double>(now - target_times[unanswered]);
        result.responses.push_back(latency);
        result.max_response_ms = std::max(result.max_response_ms, latency);
        if (result.first_response_ms < 0.0 && unanswered == 0) {
          result.first_response_ms = latency;
          result.first_from_start_ms = static_cast<double>(now);
        }
        ++unanswered;
      }
      if (last_present_time >= 0) {
        result.max_gap_ms = std::max(
            result.max_gap_ms, static_cast<double>(now - last_present_time));
      }
      last_present_time = now;
      const qint64 displayed = DisplayedNow();
      if (displayed >= 0) {
        lags.push_back(static_cast<double>(
            displayed >= current_target ? displayed - current_target
                                        : current_target - displayed));
        if (last_displayed >= 0) {
          if (forward && displayed < last_displayed) {
            ++result.flicker;
          }
          if (!forward && displayed > last_displayed) {
            ++result.flicker;
          }
        }
        last_displayed = displayed;
      }
      if (CoarseNow()) {
        ++result.coarse_presented;
      }
      if (CacheNow()) {
        ++result.cache_presented;
      }
    }
    last_presented = now_presented;
  };

  for (std::size_t i = 0; i < targets.size(); ++i) {
    if (i > 0) {
      const qint64 due = first_target_time + due_ms[i];
      while (drag_clock.elapsed() < due) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        poll(targets[i - 1]);
        std::this_thread::yield();
      }
      if (PresentedNow() == presented_at_last_target) {
        ++missing_run;
        result.max_consecutive_missing =
            std::max(result.max_consecutive_missing, missing_run);
      } else {
        missing_run = 0;
      }
    } else {
      first_target_time = drag_clock.elapsed();
    }
    window_->ScrubTargetForTest(targets[i]);
    target_times.push_back(drag_clock.elapsed());
    presented_at_last_target = PresentedNow();
    if (i == 0) {
      last_present_time = target_times.front();
    }
    const qint64 next_due =
        (i + 1 < targets.size())
            ? first_target_time + due_ms[i + 1]
            : drag_clock.elapsed() + std::max<int>(150, gap_limit_ms + 80);
    while (drag_clock.elapsed() < next_due) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
      poll(targets[i]);
      std::this_thread::yield();
    }
  }
  const qint64 trailing_until =
      drag_clock.elapsed() + std::max<int>(120, gap_limit_ms + 60);
  while (drag_clock.elapsed() < trailing_until) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    poll(targets.empty() ? start : targets.back());
    std::this_thread::yield();
  }

  result.presented = PresentedNow() - base_presented;
  result.response_p95_ms = Percentile(result.responses, 0.95);
  result.target_lag_p95 = Percentile(lags, 0.95);
  result.stale_dropped = StaleNow() - stale_base;
  result.ui_gap_ms = heartbeat_max_ms_;
  if (result.first_from_start_ms < 0.0 && result.presented > 0) {
    result.first_from_start_ms = 0.0;
  }
  if (result.presented >= 2 && last_present_time > first_target_time &&
      first_target_time >= 0) {
    result.active_fps =
        static_cast<double>(result.presented) * 1000.0 /
        static_cast<double>(last_present_time - first_target_time);
  } else if (result.presented > 0 && drag_clock.elapsed() > 0) {
    result.active_fps =
        static_cast<double>(result.presented) * 1000.0 /
        static_cast<double>(drag_clock.elapsed());
  }
  const bool landed = ParityLanding(end, &result);
  result.ok = landed && result.first_response_ms >= 0.0 &&
              result.first_response_ms <= 100.0 &&
              result.response_p95_ms <= 100.0 &&
              result.max_gap_ms <= static_cast<double>(gap_limit_ms) &&
              result.max_consecutive_missing <= 1 && result.flicker == 0;
  PrintParity(result);
  Print(QStringLiteral(
            "online_drag_p0_drag scenario=%1 mode=%2 first_publish_ms=%3 "
            "first_delivered_ms=%4 open_ms=%5 last_before_resume=%6 "
            "published_before_release=%7 decoder_published=%8 "
            "decoder_delivered=%9 max_missing_run=%10 baseline=%11 "
            "warm_ms=%12 warm_frames=%13 warm_skipped=%14 "
            "warm_failures=%15 seek_fallbacks=%16 seek_keyframe=%17 "
            "seek_offset=%18 primed=%19")
            .arg(result.label)
            .arg(result.mode)
            .arg(result.first_publish_ms)
            .arg(result.first_delivered_ms)
            .arg(result.open_ms)
            .arg(result.last_delivered_before_resume)
            .arg(result.published_before_release)
            .arg(result.decoder_published)
            .arg(result.decoder_delivered)
            .arg(result.max_consecutive_missing)
            .arg(result.baseline_frame)
            .arg(result.warm_ms)
            .arg(result.warm_frames)
            .arg(result.warm_skipped)
            .arg(result.warm_failures)
            .arg(result.seek_fallbacks)
            .arg(result.seek_keyframe)
            .arg(result.seek_offset)
            .arg(result.primed_frames));
  return result;
}

OnlineDragPreviewTestRunner::ParityResult
OnlineDragPreviewTestRunner::RunParitySequence(
    const QString& label, qint64 start, qint64 end, int samples,
    int duration_ms, bool forward, int gap_limit_ms, bool check_snapshot,
    int initial_pump_ms) {
  (void)check_snapshot;
  std::vector<qint64> targets;
  std::vector<int> dues;
  targets.reserve(static_cast<std::size_t>(std::max(1, samples)));
  dues.reserve(static_cast<std::size_t>(std::max(1, samples)));
  for (int i = 0; i < samples; ++i) {
    const double ratio =
        samples <= 1 ? 1.0 : static_cast<double>(i) / (samples - 1);
    targets.push_back(
        start + static_cast<qint64>(std::llround((end - start) * ratio)));
    dues.push_back(samples <= 1 ? 0 : duration_ms * i / (samples - 1));
  }
  return RunParityFrames(label, start, end, targets, dues, forward,
                         gap_limit_ms, initial_pump_ms);
}

OnlineDragPreviewTestRunner::ParityResult
OnlineDragPreviewTestRunner::RunParityZoom(const QString& label, qint64 start,
                                           qint64 step, int steps,
                                           int step_ms) {
  std::vector<qint64> targets;
  std::vector<int> dues;
  for (int i = 0; i < steps; ++i) {
    targets.push_back(start + step * i);
    dues.push_back(i * step_ms);
  }
  const qint64 end = start + step * (steps - 1);
  return RunParityFrames(label, start, end, targets, dues, step > 0, 150);
}

bool OnlineDragPreviewTestRunner::EnsureLocalReady(QString* error) {
  if (options_.local_file.isEmpty()) {
    *error = QStringLiteral("缺少 --file");
    return false;
  }
  window_->OpenFile(options_.local_file);
  const bool ready = WaitUntil(
      [this]() {
        return window_->HasMediaForTest() && window_->IndexReadyForTest() &&
               window_->LastFrameIndexForTest() > 300;
      },
      std::max(10000, options_.timeout_ms), "online_drag_parity_local_ready");
  if (!ready) {
    *error = QStringLiteral("本地打开/索引超时");
    return false;
  }
  window_->ClearFocusForTest();
  window_->FocusVideoForTest();
  return true;
}

bool OnlineDragPreviewTestRunner::OpenLocal(QString* error) {
  return EnsureLocalReady(error);
}

OnlineDragPreviewTestRunner::ParityResult
OnlineDragPreviewTestRunner::RunParityFar(const QString& label, qint64 start,
                                          qint64 target, bool forward) {
  ParityResult result;
  result.label = label;
  result.mode = options_.local_only ? QStringLiteral("local")
                                    : QStringLiteral("online");
  result.start_frame = start;
  result.end_frame = target;
  result.updates = 1;
  (void)forward;
  heartbeat_max_ms_ = 0;
  heartbeat_over_200_ = 0;
  window_->ResetPreviewStatsForTest();
  const int stale_base = StaleNow();
  window_->ScrubStartForTest(start);
  Pump(120);
  const int base_presented = PresentedNow();
  QElapsedTimer far_clock;
  far_clock.start();
  window_->ScrubTargetForTest(target);
  int last_presented = base_presented;
  int presented = 0;
  int coarse_count = 0;
  int cache_count = 0;
  qint64 first_ms = -1;
  qint64 exact_ms = -1;
  bool first_coarse = true;
  bool exact_seen = false;
  bool coarse_after_exact = false;
  const auto poll = [&]() {
    const int now = PresentedNow();
    if (now <= last_presented) {
      return;
    }
    const qint64 t = far_clock.elapsed();
    const int delta = now - last_presented;
    for (int i = 0; i < delta; ++i) {
      ++presented;
      if (first_ms < 0) {
        first_ms = t;
        first_coarse = CoarseNow();
      }
      if (CoarseNow()) {
        ++coarse_count;
      }
      if (CacheNow()) {
        ++cache_count;
      }
      if (!exact_seen) {
        if (DisplayedNow() == target) {
          exact_seen = true;
          exact_ms = t;
        }
      } else if (CoarseNow()) {
        coarse_after_exact = true;
      }
    }
    last_presented = now;
  };
  const qint64 first_deadline = far_clock.elapsed() + 800;
  while (far_clock.elapsed() < first_deadline && first_ms < 0) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    poll();
    std::this_thread::yield();
  }
  const qint64 exact_deadline = far_clock.elapsed() + 3000;
  while (far_clock.elapsed() < exact_deadline && !exact_seen) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    poll();
    std::this_thread::yield();
  }
  const qint64 settle_deadline = far_clock.elapsed() + 150;
  while (far_clock.elapsed() < settle_deadline) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    poll();
    std::this_thread::yield();
  }
  result.presented = presented;
  result.exact_ms = exact_ms;
  result.first_response_ms =
      first_ms >= 0 ? static_cast<double>(first_ms) : -1.0;
  result.first_from_start_ms = result.first_response_ms;
  result.max_response_ms = result.first_response_ms;
  result.response_p95_ms = result.first_response_ms;
  result.max_gap_ms = result.first_response_ms;
  result.active_fps =
      far_clock.elapsed() > 0
          ? static_cast<double>(presented) * 1000.0 /
                static_cast<double>(far_clock.elapsed())
          : 0.0;
  result.coarse_presented = coarse_count;
  result.cache_presented = cache_count;
  result.stale_dropped = StaleNow() - stale_base;
  result.ui_gap_ms = heartbeat_max_ms_;
  const bool landed = ParityLanding(target, &result);
  result.ok = landed && first_ms >= 0 && first_ms <= 600 &&
              exact_seen && !coarse_after_exact;
  PrintParity(result);
  if (first_coarse) {
    result.coarse_presented = std::max(1, result.coarse_presented);
  }
  return result;
}

void OnlineDragPreviewTestRunner::RunParityUncached() {
  if (window_->OnlineCacheCompleteForTest()) {
    Check("parity_uncached_available", false,
          QStringLiteral("cache_complete=1"));
    return;
  }
  const qint64 last = window_->OnlineLastFrameForTest();
  qint64 target = std::max<qint64>(1, last - 1);
  int guard = 0;
  while (target > 0 && window_->OnlineFrameCachedForTest(target) &&
         guard++ < 5000) {
    --target;
  }
  const bool uncached_available = !window_->OnlineFrameCachedForTest(target);
  Check("parity_uncached_available", uncached_available,
        QStringLiteral("target=%1 stable=%2 last=%3")
            .arg(target)
            .arg(window_->OnlineStableFramesForTest())
            .arg(last));
  if (!uncached_available) {
    return;
  }
  const qint64 cached_start = 40;
  Check("parity_uncached_start_cached",
        window_->OnlineFrameCachedForTest(cached_start),
        QStringLiteral("frame=%1").arg(cached_start));
  window_->ResetPreviewStatsForTest();
  window_->ScrubStartForTest(cached_start);
  Pump(80);
  const int before = PresentedNow();
  QElapsedTimer target_timer;
  target_timer.start();
  window_->ScrubTargetForTest(target);
  const qint64 ui_ms = target_timer.elapsed();
  const QString status = window_->StatusMessageForTest();
  Check("parity_uncached_prompt",
        status.contains(QStringLiteral("缓冲中（预览）")) &&
            status.contains(QString::number(target)),
        status);
  Check("parity_uncached_ui_le_100ms", ui_ms <= 100,
        QStringLiteral("ms=%1").arg(ui_ms));
  Pump(200);
  Check("parity_uncached_waiting", window_->OnlinePreviewWaitingForTest(),
        window_->OnlinePreviewStateForTest());
  window_->ScrubTargetForTest(cached_start + 5);
  const bool resumed = WaitUntil(
      [this, before]() {
        return !window_->OnlinePreviewWaitingForTest() &&
               PresentedNow() > before;
      },
      3000, "parity_uncached_resume");
  Check("parity_uncached_resume", resumed,
        window_->OnlinePreviewStateForTest());
  ParityResult result;
  result.label = QStringLiteral("uncached");
  result.mode = QStringLiteral("online");
  result.start_frame = cached_start;
  result.end_frame = cached_start + 5;
  result.updates = 1;
  result.first_response_ms = static_cast<double>(ui_ms);
  result.first_from_start_ms = static_cast<double>(ui_ms);
  result.ui_gap_ms = heartbeat_max_ms_;
  result.presented = std::max(0, PresentedNow() - before);
  const bool landed = ParityLanding(cached_start + 5, &result);
  result.ok = landed;
  PrintParity(result);
  Check("parity_uncached_release_landing", landed,
        QStringLiteral("frame=%1 pts=%2 expected=%3")
            .arg(window_->CurrentFrameForTest())
            .arg(window_->CurrentPtsUsForTest())
            .arg(window_->OnlineFramePtsForTest(cached_start + 5)));
}

void OnlineDragPreviewTestRunner::RunParity() { RunOnlineParity(); }

void OnlineDragPreviewTestRunner::RunLocalParity() {
  parity_ui_gap_max_ms_ = 0;
  window_->raise();
  window_->activateWindow();
  window_->FocusVideoForTest();
  QElapsedTimer heartbeat_clock;
  heartbeat_clock.start();
  qint64 heartbeat_last = 0;
  QTimer heartbeat;
  heartbeat.setInterval(16);
  heartbeat.setTimerType(Qt::PreciseTimer);
  QObject::connect(&heartbeat, &QTimer::timeout,
                   [this, &heartbeat_clock, &heartbeat_last]() {
                     const qint64 now = heartbeat_clock.elapsed();
                     if (heartbeat_last > 0) {
                       const qint64 gap = now - heartbeat_last;
                       if (gap > 200) {
                         ++heartbeat_over_200_;
                       }
                       heartbeat_max_ms_ = std::max(heartbeat_max_ms_, gap);
                     }
                     heartbeat_last = now;
                   });
  heartbeat.start();

  QString error;
  const bool opened = OpenLocal(&error);
  Check("online_drag_parity_local_open", opened, error);
  if (!opened) {
    heartbeat.stop();
    Print(QStringLiteral(
              "online_drag_parity_result mode=local checks=%1 failures=%2 "
              "verdict=fail")
              .arg(checks_)
              .arg(failures_));
    QCoreApplication::exit(2);
    return;
  }
  const qint64 last = window_->LastFrameIndexForTest();
  const qint64 far_target = std::min<qint64>(3000, last - 1);
  const ParityResult forward = RunParitySequence(
      QStringLiteral("local_forward"), 15, 75, 60, 1000, true, 50, false);
  const ParityResult backward = RunParitySequence(
      QStringLiteral("local_backward"), 75, 15, 60, 1000, false, 50, false);
  const ParityResult zoom_forward = RunParityZoom(
      QStringLiteral("local_zoom_forward"), 40, 1, 10, 60);
  const ParityResult zoom_backward = RunParityZoom(
      QStringLiteral("local_zoom_backward"), 50, -1, 10, 60);
  const ParityResult far_forward = RunParityFar(
      QStringLiteral("local_far_forward"), 15, far_target, true);
  const ParityResult far_backward = RunParityFar(
      QStringLiteral("local_far_backward"), far_target, 15, false);
  Check("local_forward_presented", forward.presented > 0,
        QStringLiteral("presented=%1 fps=%2")
            .arg(forward.presented)
            .arg(QString::number(forward.active_fps, 'f', 2)));
  Check("local_forward_landing",
        forward.landing_frame_ok && forward.landing_pts_ok,
        QStringLiteral("frame=%1").arg(forward.landing_frame_ok ? 1 : 0));
  Check("local_backward_presented", backward.presented > 0,
        QStringLiteral("presented=%1 fps=%2")
            .arg(backward.presented)
            .arg(QString::number(backward.active_fps, 'f', 2)));
  Check("local_backward_landing",
        backward.landing_frame_ok && backward.landing_pts_ok);
  Check("local_zoom_forward_steps", zoom_forward.max_consecutive_missing <= 1,
        QStringLiteral("max_missing=%1 max_response=%2")
            .arg(zoom_forward.max_consecutive_missing)
            .arg(QString::number(zoom_forward.max_response_ms, 'f', 2)));
  Check("local_zoom_backward_steps",
        zoom_backward.max_consecutive_missing <= 1,
        QStringLiteral("max_missing=%1 max_response=%2")
            .arg(zoom_backward.max_consecutive_missing)
            .arg(QString::number(zoom_backward.max_response_ms, 'f', 2)));
  Check("local_far_forward_exact",
        far_forward.ok || (far_forward.landing_frame_ok &&
                           far_forward.landing_pts_ok),
        QStringLiteral("first_ms=%1 coarse=%2")
            .arg(QString::number(far_forward.first_response_ms, 'f', 2))
            .arg(far_forward.coarse_presented));
  Check("local_far_backward_exact",
        far_backward.ok || (far_backward.landing_frame_ok &&
                            far_backward.landing_pts_ok),
        QStringLiteral("first_ms=%1 coarse=%2")
            .arg(QString::number(far_backward.first_response_ms, 'f', 2))
            .arg(far_backward.coarse_presented));
  heartbeat.stop();
  Print(QStringLiteral(
            "online_drag_parity_result mode=local checks=%1 failures=%2 "
            "forward_fps=%3 backward_fps=%4 forward_gap=%5 backward_gap=%6 "
            "forward_first=%7 backward_first=%8 verdict=%9")
            .arg(checks_)
            .arg(failures_)
            .arg(QString::number(forward.active_fps, 'f', 2))
            .arg(QString::number(backward.active_fps, 'f', 2))
            .arg(QString::number(forward.max_gap_ms, 'f', 2))
            .arg(QString::number(backward.max_gap_ms, 'f', 2))
            .arg(QString::number(forward.first_response_ms, 'f', 2))
            .arg(QString::number(backward.first_response_ms, 'f', 2))
            .arg(failures_ == 0 ? QStringLiteral("pass")
                                : QStringLiteral("fail")));
  QCoreApplication::exit(failures_ == 0 ? 0 : 1);
}

void OnlineDragPreviewTestRunner::RunOnlineParity() {
  parity_ui_gap_max_ms_ = 0;
  window_->raise();
  window_->activateWindow();
  window_->FocusVideoForTest();
  QElapsedTimer heartbeat_clock;
  heartbeat_clock.start();
  qint64 heartbeat_last = 0;
  QTimer heartbeat;
  heartbeat.setInterval(16);
  heartbeat.setTimerType(Qt::PreciseTimer);
  QObject::connect(&heartbeat, &QTimer::timeout,
                   [this, &heartbeat_clock, &heartbeat_last]() {
                     const qint64 now = heartbeat_clock.elapsed();
                     if (heartbeat_last > 0) {
                       const qint64 gap = now - heartbeat_last;
                       if (gap > 200) {
                         ++heartbeat_over_200_;
                       }
                       heartbeat_max_ms_ = std::max(heartbeat_max_ms_, gap);
                     }
                     heartbeat_last = now;
                   });
  heartbeat.start();

  QString error;
  const bool opened = OpenOnline(false, &error);
  Check("online_drag_parity_open", opened, error);
  if (!opened) {
    heartbeat.stop();
    Print(QStringLiteral(
              "online_drag_parity_result mode=online checks=%1 failures=%2 "
              "verdict=fail")
              .arg(checks_)
              .arg(failures_));
    QCoreApplication::exit(2);
    return;
  }
  Check("online_drag_parity_growing_session",
        !window_->OnlineCacheHitForTest(),
        QStringLiteral("cache_hit=%1")
            .arg(window_->OnlineCacheHitForTest() ? 1 : 0));
  qint64 start = 15;
  qint64 end = 75;
  const bool window_ready = WaitForCachedWindow(
      &start, &end, 60, std::max(10000, options_.timeout_ms));
  Check("online_drag_parity_cached_window", window_ready,
        QStringLiteral("start=%1 end=%2 stable=%3")
            .arg(start)
            .arg(end)
            .arg(window_->OnlineStableFramesForTest()));
  if (!window_ready) {
    heartbeat.stop();
    QCoreApplication::exit(1);
    return;
  }

  // 冷启动（5s GOP）只记录：在线预览解码器从关键帧解到目标，首帧可能
  // 超过 100ms；这是长 GOP 冷启动残余，不作为丝滑门槛。反向冷启动最能
  // 暴露“从关键帧解 75 帧”的成本（75 在 5s GOP 内，关键帧为 0）。
  const ParityResult cold = RunParitySequence(
      QStringLiteral("cold_backward"), 75, 65, 10, 300, false, 3000, false);
  Check("cold_backward_landing",
        cold.landing_frame_ok && cold.landing_pts_ok,
        QStringLiteral("first=%1")
            .arg(QString::number(cold.first_response_ms, 'f', 2)));
  Check("cold_first_publish_le_100",
        cold.first_publish_ms >= 0 && cold.first_publish_ms <= 100,
        QStringLiteral("first_publish=%1 baseline=%2")
            .arg(cold.first_publish_ms)
            .arg(cold.baseline_frame));
  Check("cold_first_delivery_le_100",
        cold.first_delivered_ms >= 0 && cold.first_delivered_ms <= 100,
        QStringLiteral("first_delivered=%1")
            .arg(cold.first_delivered_ms));
  Check("cold_first_response_le_100",
        cold.first_response_ms >= 0 && cold.first_response_ms <= 100.0,
        QStringLiteral("first_response=%1")
            .arg(QString::number(cold.first_response_ms, 'f', 2)));
  Check("cold_no_two_missing", cold.max_consecutive_missing <= 1,
        QStringLiteral("max_missing=%1")
            .arg(cold.max_consecutive_missing));
  // 预热一次短拖动：Pause/Resume 保留有界帧缓存，之后的拖动从缓存命中。
  RunParityZoom(QStringLiteral("warmup"), 15, 1, 6, 60);

  const ParityResult forward = RunParitySequence(
      QStringLiteral("forward"), 15, 75, 60, 1000, true, 50, false);
  const ParityResult backward = RunParitySequence(
      QStringLiteral("backward"), 75, 15, 60, 1000, false, 50, false);
  Check("forward_first_response_le_100",
        forward.first_response_ms >= 0 && forward.first_response_ms <= 100.0,
        QStringLiteral("first=%1 from_start=%2")
            .arg(QString::number(forward.first_response_ms, 'f', 2))
            .arg(QString::number(forward.first_from_start_ms, 'f', 2)));
  Check("forward_response_p95_le_100", forward.response_p95_ms <= 100.0,
        QStringLiteral("p95=%1")
            .arg(QString::number(forward.response_p95_ms, 'f', 2)));
  Check("forward_max_gap_le_50", forward.max_gap_ms <= 50.0,
        QStringLiteral("max_gap=%1")
            .arg(QString::number(forward.max_gap_ms, 'f', 2)));
  Check("forward_active_fps_ge_42", forward.active_fps >= 42.0,
        QStringLiteral("fps=%1 presented=%2")
            .arg(QString::number(forward.active_fps, 'f', 2))
            .arg(forward.presented));
  Check("forward_no_consecutive_missing",
        forward.max_consecutive_missing <= 1,
        QStringLiteral("max_missing=%1")
            .arg(forward.max_consecutive_missing));
  Check("forward_no_flicker", forward.flicker == 0,
        QStringLiteral("flicker=%1").arg(forward.flicker));
  Check("forward_landing",
        forward.landing_frame_ok && forward.landing_pts_ok);
  Check("backward_first_response_le_100",
        backward.first_response_ms >= 0 && backward.first_response_ms <= 100.0,
        QStringLiteral("first=%1 from_start=%2")
            .arg(QString::number(backward.first_response_ms, 'f', 2))
            .arg(QString::number(backward.first_from_start_ms, 'f', 2)));
  Check("backward_response_p95_le_100", backward.response_p95_ms <= 100.0,
        QStringLiteral("p95=%1")
            .arg(QString::number(backward.response_p95_ms, 'f', 2)));
  Check("backward_max_gap_le_50", backward.max_gap_ms <= 50.0,
        QStringLiteral("max_gap=%1")
            .arg(QString::number(backward.max_gap_ms, 'f', 2)));
  Check("backward_active_fps_ge_42", backward.active_fps >= 42.0,
        QStringLiteral("fps=%1 presented=%2")
            .arg(QString::number(backward.active_fps, 'f', 2))
            .arg(backward.presented));
  Check("backward_no_consecutive_missing",
        backward.max_consecutive_missing <= 1,
        QStringLiteral("max_missing=%1")
            .arg(backward.max_consecutive_missing));
  Check("backward_no_flicker", backward.flicker == 0,
        QStringLiteral("flicker=%1").arg(backward.flicker));
  Check("backward_landing",
        backward.landing_frame_ok && backward.landing_pts_ok);
  const double presented_max =
      std::max(1, std::max(forward.presented, backward.presented));
  const double presented_diff =
      std::abs(forward.presented - backward.presented) / presented_max;
  Check("parity_presented_diff_le_10pct", presented_diff <= 0.10,
        QStringLiteral("forward=%1 backward=%2 diff=%3")
            .arg(forward.presented)
            .arg(backward.presented)
            .arg(QString::number(presented_diff, 'f', 3)));

  const ParityResult zoom_forward = RunParityZoom(
      QStringLiteral("zoom_forward"), 40, 1, 10, 60);
  const ParityResult zoom_backward = RunParityZoom(
      QStringLiteral("zoom_backward"), 50, -1, 10, 60);
  for (const ParityResult* zoom : {&zoom_forward, &zoom_backward}) {
    Check(zoom->label == QStringLiteral("zoom_forward")
              ? "zoom_forward_each_step_le_100"
              : "zoom_backward_each_step_le_100",
          zoom->max_response_ms <= 100.0,
          QStringLiteral("max_response=%1")
              .arg(QString::number(zoom->max_response_ms, 'f', 2)));
    Check(zoom->label == QStringLiteral("zoom_forward")
              ? "zoom_forward_gap_le_150"
              : "zoom_backward_gap_le_150",
          zoom->max_gap_ms <= 150.0,
          QStringLiteral("max_gap=%1")
              .arg(QString::number(zoom->max_gap_ms, 'f', 2)));
    Check(zoom->label == QStringLiteral("zoom_forward")
              ? "zoom_forward_no_two_missing"
              : "zoom_backward_no_two_missing",
          zoom->max_consecutive_missing <= 1,
          QStringLiteral("max_missing=%1")
              .arg(zoom->max_consecutive_missing));
    Check(zoom->label == QStringLiteral("zoom_forward")
              ? "zoom_forward_no_flicker"
              : "zoom_backward_no_flicker",
          zoom->flicker == 0, QStringLiteral("flicker=%1").arg(zoom->flicker));
    Check(zoom->label == QStringLiteral("zoom_forward")
              ? "zoom_forward_landing"
              : "zoom_backward_landing",
          zoom->landing_frame_ok && zoom->landing_pts_ok);
  }

  RunParityUncached();

  const bool far_ready = WaitUntil(
      [this]() { return window_->OnlineFrameCachedForTest(3000); },
      150000, "online_drag_parity_far_cached");
  Check("online_drag_parity_far_cached", far_ready,
        QStringLiteral("stable=%1 last=%2")
            .arg(window_->OnlineStableFramesForTest())
            .arg(window_->OnlineLastFrameForTest()));
  if (far_ready) {
    const ParityResult far_forward = RunParityFar(
        QStringLiteral("far_forward"), 15, 3000, true);
    const ParityResult far_backward = RunParityFar(
        QStringLiteral("far_backward"), 3000, 15, false);
    Check("far_forward_coarse_le_100",
          far_forward.first_response_ms >= 0 &&
              far_forward.first_response_ms <= 100.0,
          QStringLiteral("first=%1")
              .arg(QString::number(far_forward.first_response_ms, 'f', 2)));
    Check("far_forward_exact_after_coarse", far_forward.ok,
          QStringLiteral("coarse=%1 presented=%2")
              .arg(far_forward.coarse_presented)
              .arg(far_forward.presented));
    Check("far_forward_landing",
          far_forward.landing_frame_ok && far_forward.landing_pts_ok);
    Check("far_backward_coarse_le_100",
          far_backward.first_response_ms >= 0 &&
              far_backward.first_response_ms <= 100.0,
          QStringLiteral("first=%1")
              .arg(QString::number(far_backward.first_response_ms, 'f', 2)));
    Check("far_backward_exact_after_coarse", far_backward.ok,
          QStringLiteral("coarse=%1 presented=%2")
              .arg(far_backward.coarse_presented)
              .arg(far_backward.presented));
    Check("far_backward_landing",
          far_backward.landing_frame_ok && far_backward.landing_pts_ok);
  }

  // P0 门禁：seek 落地 + 预热完成后，第一次拖动必须首帧 ≤100ms、无连续
  // 两个目标无画面。1575/1605 分别给反向/正向场景。
  ParityResult post_seek_backward;
  ParityResult post_seek_forward;
  bool post_seek_backward_ready = false;
  bool post_seek_forward_ready = false;
  const bool p0_post_seek_ready = WaitUntil(
      [this]() { return window_->OnlineFrameCachedForTest(1620); }, 120000,
      "p0_post_seek_cached");
  if (!p0_post_seek_ready) {
    Print(QStringLiteral("online_drag_p0_post_seek skip cached_1620=0"));
  } else {
    const auto prepare_exact = [this](qint64 frame) {
      window_->GoToFrameForTest(frame);
      const bool landed = WaitUntil(
          [this, frame]() {
            return window_->CurrentFrameForTest() == frame &&
                   window_->OnlineSeekIdleForTest();
          },
          std::max(5000, options_.timeout_ms), "p0_post_seek_landing");
      if (!landed) {
        return false;
      }
      // 等待 seek 落地后调度的 Warm 完成；S2 实测 warm 约 0.5s，留 3s。
      const bool warm_idle = WaitUntil(
          [this]() { return window_->OnlinePreviewWarmIdleForTest(); }, 3000,
          "p0_post_seek_warm_idle");
      Print(QStringLiteral(
                "online_drag_p0_post_seek_ready frame=%1 warm_idle=%2 "
                "warm_ms=%3 seek_offset=%4")
                .arg(frame)
                .arg(warm_idle ? 1 : 0)
                .arg(window_->OnlinePreviewWarmMsForTest())
                .arg(window_->OnlinePreviewLastSeekLandingOffsetForTest()));
      Pump(50);
      return true;
    };
    if (prepare_exact(1575)) {
      post_seek_backward = RunParitySequence(
          QStringLiteral("post_seek_backward"), 1575, 1565, 10, 300, false,
          3000, false, /*initial_pump_ms=*/0);
      post_seek_backward_ready = true;
    } else {
      Print(QStringLiteral(
          "online_drag_p0_post_seek skip scenario=post_seek_backward"));
    }
    if (prepare_exact(1605)) {
      post_seek_forward = RunParitySequence(
          QStringLiteral("post_seek_forward"), 1605, 1615, 10, 300, true,
          3000, false, /*initial_pump_ms=*/0);
      post_seek_forward_ready = true;
    } else {
      Print(QStringLiteral(
          "online_drag_p0_post_seek skip scenario=post_seek_forward"));
    }
  }
  Check("post_seek_backward_first_publish_le_100",
        post_seek_backward_ready && post_seek_backward.first_publish_ms >= 0 &&
            post_seek_backward.first_publish_ms <= 100,
        QStringLiteral("first_publish=%1 baseline=%2")
            .arg(post_seek_backward.first_publish_ms)
            .arg(post_seek_backward.baseline_frame));
  Check("post_seek_backward_first_delivery_le_100",
        post_seek_backward_ready &&
            post_seek_backward.first_delivered_ms >= 0 &&
            post_seek_backward.first_delivered_ms <= 100,
        QStringLiteral("first_delivered=%1")
            .arg(post_seek_backward.first_delivered_ms));
  Check("post_seek_backward_first_response_le_100",
        post_seek_backward_ready &&
            post_seek_backward.first_response_ms >= 0.0 &&
            post_seek_backward.first_response_ms <= 100.0,
        QStringLiteral("first_response=%1")
            .arg(QString::number(post_seek_backward.first_response_ms, 'f', 2)));
  Check("post_seek_backward_no_two_missing",
        post_seek_backward_ready &&
            post_seek_backward.max_consecutive_missing <= 1,
        QStringLiteral("max_missing=%1")
            .arg(post_seek_backward.max_consecutive_missing));
  Check("post_seek_backward_landing",
        post_seek_backward_ready && post_seek_backward.landing_frame_ok &&
            post_seek_backward.landing_pts_ok);
  Check("post_seek_backward_no_flicker",
        post_seek_backward_ready && post_seek_backward.flicker == 0,
        QStringLiteral("flicker=%1").arg(post_seek_backward.flicker));
  Check("post_seek_forward_first_publish_le_100",
        post_seek_forward_ready && post_seek_forward.first_publish_ms >= 0 &&
            post_seek_forward.first_publish_ms <= 100,
        QStringLiteral("first_publish=%1 baseline=%2")
            .arg(post_seek_forward.first_publish_ms)
            .arg(post_seek_forward.baseline_frame));
  Check("post_seek_forward_first_delivery_le_100",
        post_seek_forward_ready &&
            post_seek_forward.first_delivered_ms >= 0 &&
            post_seek_forward.first_delivered_ms <= 100,
        QStringLiteral("first_delivered=%1")
            .arg(post_seek_forward.first_delivered_ms));
  Check("post_seek_forward_first_response_le_100",
        post_seek_forward_ready &&
            post_seek_forward.first_response_ms >= 0.0 &&
            post_seek_forward.first_response_ms <= 100.0,
        QStringLiteral("first_response=%1")
            .arg(QString::number(post_seek_forward.first_response_ms, 'f', 2)));
  Check("post_seek_forward_no_two_missing",
        post_seek_forward_ready &&
            post_seek_forward.max_consecutive_missing <= 1,
        QStringLiteral("max_missing=%1")
            .arg(post_seek_forward.max_consecutive_missing));
  Check("post_seek_forward_landing",
        post_seek_forward_ready && post_seek_forward.landing_frame_ok &&
            post_seek_forward.landing_pts_ok);
  Check("post_seek_forward_no_flicker",
        post_seek_forward_ready && post_seek_forward.flicker == 0,
        QStringLiteral("flicker=%1").arg(post_seek_forward.flicker));

  const qint64 cache_peak =
      static_cast<qint64>(window_->OnlinePreviewCachePeakBytesForTest());
  Check("online_drag_parity_cache_peak_le_64mib",
        cache_peak <= 64LL * 1024LL * 1024LL,
        QStringLiteral("peak=%1").arg(cache_peak));
  Check("online_drag_parity_cache_frames_le_72",
        window_->OnlinePreviewCachedFramesForTest() <= 72,
        QStringLiteral("frames=%1")
            .arg(window_->OnlinePreviewCachedFramesForTest()));

  // P0 诚实性诊断（只打印、不门禁）：丢弃预览器后 seek 落地 0ms 立即再拖，
  // 测的是“全新预览解码器”的冷启动；warm timer 会被 ScrubStart 取消。
  const bool immediate_cached = WaitUntil(
      [this]() { return window_->OnlineFrameCachedForTest(1665); }, 120000,
      "p0_post_seek_immediate_cached");
  if (!immediate_cached) {
    Print(QStringLiteral(
        "online_drag_p0_post_seek_immediate skip cached_1665=0"));
  } else {
    window_->StopPreviewForTest();
    window_->GoToFrameForTest(1655);
    const bool landed = WaitUntil(
        [this]() {
          return window_->CurrentFrameForTest() == 1655 &&
                 window_->OnlineSeekIdleForTest();
        },
        std::max(5000, options_.timeout_ms), "p0_immediate_landing");
    if (!landed) {
      Print(QStringLiteral(
          "online_drag_p0_post_seek_immediate skip landing=0"));
    } else {
      // 不等待 WarmIdle；warm timer 会由随后的 ScrubStart 取消。
      Pump(10);
      const ParityResult immediate = RunParitySequence(
          QStringLiteral("post_seek_immediate"), 1655, 1645, 5, 150, false,
          3000, false, /*initial_pump_ms=*/0);
      Print(QStringLiteral(
                "online_drag_p0_post_seek_immediate first_publish=%1 "
                "first_delivered=%2 first_response=%3 max_missing=%4 "
                "max_response=%5 max_gap=%6 baseline=%7 seek_offset=%8")
                .arg(immediate.first_publish_ms)
                .arg(immediate.first_delivered_ms)
                .arg(QString::number(immediate.first_response_ms, 'f', 2))
                .arg(immediate.max_consecutive_missing)
                .arg(QString::number(immediate.max_response_ms, 'f', 2))
                .arg(QString::number(immediate.max_gap_ms, 'f', 2))
                .arg(immediate.baseline_frame)
                .arg(immediate.seek_offset));
    }
  }

  heartbeat.stop();
  Print(QStringLiteral(
            "online_drag_parity_ui_gap max_ms=%1 over_200=%2 cache_peak=%3 "
            "cache_frames=%4")
            .arg(parity_ui_gap_max_ms_)
            .arg(heartbeat_over_200_)
            .arg(cache_peak)
            .arg(window_->OnlinePreviewCachedFramesForTest()));
  Print(QStringLiteral(
            "online_drag_parity_result mode=online checks=%1 failures=%2 "
            "cold_first=%3 forward_fps=%4 backward_fps=%5 "
            "forward_gap=%6 backward_gap=%7 presented_diff=%8 verdict=%9 "
            "post_b_first=%10 post_b_delivered=%11 post_b_missing=%12 "
            "post_f_first=%13 post_f_delivered=%14 post_f_missing=%15 "
            "warm_ms=%16 seek_offset=%17")
            .arg(checks_)
            .arg(failures_)
            .arg(QString::number(cold.first_response_ms, 'f', 2))
            .arg(QString::number(forward.active_fps, 'f', 2))
            .arg(QString::number(backward.active_fps, 'f', 2))
            .arg(QString::number(forward.max_gap_ms, 'f', 2))
            .arg(QString::number(backward.max_gap_ms, 'f', 2))
            .arg(QString::number(presented_diff, 'f', 3))
            .arg(failures_ == 0 ? QStringLiteral("pass")
                                : QStringLiteral("fail"))
            .arg(post_seek_backward.first_publish_ms)
            .arg(post_seek_backward.first_delivered_ms)
            .arg(post_seek_backward.max_consecutive_missing)
            .arg(post_seek_forward.first_publish_ms)
            .arg(post_seek_forward.first_delivered_ms)
            .arg(post_seek_forward.max_consecutive_missing)
            .arg(post_seek_backward.warm_ms)
            .arg(post_seek_backward.seek_offset));
  QCoreApplication::exit(failures_ == 0 ? 0 : 1);
}

void OnlineDragPreviewTestRunner::RunCacheSwitch() {
  Print(QStringLiteral(
            "cache_switch_sample bvid=%1 cid=%2 tag=%3")
            .arg(options_.bvid)
            .arg(options_.cid)
            .arg(options_.tag));
  window_->raise();
  window_->activateWindow();
  window_->FocusVideoForTest();
  QString error;
  const bool opened = OpenOnline(false, &error);
  Check("cache_switch_open", opened, error);
  if (!opened) {
    Print(QStringLiteral(
              "cache_switch_result checks=%1 failures=%2 verdict=fail")
              .arg(checks_)
              .arg(failures_));
    QCoreApplication::exit(2);
    return;
  }
  Check("cache_switch_growing", window_->OnlineModeForTest() &&
                                    !window_->OnlineCacheHitForTest(),
        QStringLiteral("online=%1 cache_hit=%2")
            .arg(window_->OnlineModeForTest() ? 1 : 0)
            .arg(window_->OnlineCacheHitForTest() ? 1 : 0));
  const QString old_root = window_->OnlineCacheRootForTest();
  const QString part_before = window_->OnlinePartPathForTest();
  Check("cache_switch_part_under_old_root",
        !part_before.isEmpty() &&
            QDir::cleanPath(part_before)
                .startsWith(QDir::cleanPath(old_root), Qt::CaseInsensitive),
        QStringLiteral("part=%1 root=%2").arg(part_before, old_root));

  // 正在播放：改缓存设置不能停播/换 store。
  window_->StartPlaybackForTest();
  const int presented_before = window_->PresentedFrameCountForTest();
  Pump(800);
  Check("cache_switch_playing_before_save", window_->IsPlayingForTest());
  Check("cache_switch_presented_before_save",
        window_->PresentedFrameCountForTest() > presented_before);

  const QString new_root =
      QDir::currentPath() + QStringLiteral("/build/hotfix-p1-cache-new");
  QDir(new_root).removeRecursively();
  QString apply_error;
  const bool applied =
      window_->ApplyCacheSettingsForTest(new_root, 9, &apply_error);
  Check("cache_switch_apply_during_online", applied, apply_error);
  Check("cache_switch_root_unchanged",
        QDir::cleanPath(window_->OnlineCacheRootForTest())
                .compare(QDir::cleanPath(old_root), Qt::CaseInsensitive) == 0,
        window_->OnlineCacheRootForTest());
  Check("cache_switch_pending", window_->CacheSettingsPendingForTest());
  Check("cache_switch_session_alive",
        window_->OnlineModeForTest() && window_->OnlineReadyForTest());
  Check("cache_switch_part_unchanged",
        window_->OnlinePartPathForTest() == part_before &&
            QFileInfo::exists(part_before),
        window_->OnlinePartPathForTest());
  const int presented_at_save = window_->PresentedFrameCountForTest();
  Pump(1000);
  Check("cache_switch_still_playing", window_->IsPlayingForTest());
  Check("cache_switch_still_presenting",
        window_->PresentedFrameCountForTest() > presented_at_save,
        QStringLiteral("presented=%1->%2")
            .arg(presented_at_save)
            .arg(window_->PresentedFrameCountForTest()));

  // 本次会话结束 → idle 后应用新 store；下一次会话必须落在新根。
  window_->ShutdownOnlineForTest();
  Check("cache_switch_session_closed", !window_->OnlineModeForTest());
  window_->ApplyPendingCacheStoreForTest();
  Check("cache_switch_new_root_active",
        QDir::cleanPath(window_->OnlineCacheRootForTest())
                .compare(QDir::cleanPath(new_root), Qt::CaseInsensitive) == 0,
        window_->OnlineCacheRootForTest());
  Check("cache_switch_old_part_kept", QFileInfo::exists(part_before),
        part_before);
  const bool reopened = OpenOnline(false, &error);
  Check("cache_switch_reopen_online", reopened, error);
  if (reopened) {
    const QString part_after = window_->OnlinePartPathForTest();
    Check("cache_switch_next_session_new_root",
          !part_after.isEmpty() &&
              QDir::cleanPath(part_after)
                  .startsWith(QDir::cleanPath(new_root), Qt::CaseInsensitive),
          QStringLiteral("part=%1 root=%2").arg(part_after, new_root));
    window_->ShutdownOnlineForTest();
  }
  Print(QStringLiteral(
            "cache_switch_result checks=%1 failures=%2 verdict=%3")
            .arg(checks_)
            .arg(failures_)
            .arg(failures_ == 0 ? QStringLiteral("pass")
                                : QStringLiteral("fail")));
  QCoreApplication::exit(failures_ == 0 ? 0 : 1);
}

void OnlineDragPreviewTestRunner::RunScrubSpace() {
  const bool local = options_.scrub_space_local;
  Print(QStringLiteral(
            "scrub_space_sample mode=%1 bvid=%2 cid=%3 repeats=%4 start=%5 "
            "target=%6")
            .arg(local ? QStringLiteral("local") : QStringLiteral("online"),
                 options_.bvid)
            .arg(options_.cid)
            .arg(options_.scrub_space_repeats)
            .arg(options_.start_frame)
            .arg(options_.start_frame + 60));
  window_->raise();
  window_->activateWindow();
  window_->FocusVideoForTest();

  QString open_error;
  const bool opened =
      local ? EnsureLocalReady(&open_error) : OpenOnline(false, &open_error);
  Check(local ? "scrub_space_local_open" : "scrub_space_online_open", opened,
        open_error);
  if (!opened) {
    Print(QStringLiteral(
              "scrub_space_result mode=%1 checks=%2 failures=%3 verdict=fail")
              .arg(local ? QStringLiteral("local") : QStringLiteral("online"))
              .arg(checks_)
              .arg(failures_));
    QCoreApplication::exit(2);
    return;
  }
  Check("scrub_space_default_paused", !window_->IsPlayingForTest());
  if (!local) {
    Check("scrub_space_online_growing",
          window_->OnlineModeForTest() && !window_->OnlineCacheHitForTest(),
          QStringLiteral("online=%1 cache_hit=%2")
              .arg(window_->OnlineModeForTest() ? 1 : 0)
              .arg(window_->OnlineCacheHitForTest() ? 1 : 0));
    const qint64 need_frame = options_.start_frame + 60;
    const bool indexed = WaitUntil(
        [this, need_frame]() {
          return window_->OnlineLastFrameForTest() >= need_frame &&
                 window_->OnlineSeekIdleForTest();
        },
        std::max(10000, options_.timeout_ms), "scrub_space_index");
    Check("scrub_space_online_index", indexed,
          QStringLiteral("last=%1 need=%2")
              .arg(window_->OnlineLastFrameForTest())
              .arg(need_frame));
    if (!indexed) {
      Print(QStringLiteral(
                "scrub_space_result mode=online checks=%1 failures=%2 "
                "verdict=fail")
                .arg(checks_)
                .arg(failures_));
      QCoreApplication::exit(2);
      return;
    }
  }

  const qint64 start = options_.start_frame;
  const qint64 target = start + 60;
  const qint64 middle = start + 30;
  const int repeats = std::max(1, options_.scrub_space_repeats);
  for (int i = 0; i < repeats; ++i) {
    // 上一轮播放先停下；等上一次异步 seek 落地，避免把两轮混在一起。
    window_->PausePlaybackForTest();
    WaitUntil([this]() { return window_->OnlineSeekIdleForTest(); }, 5000,
              "scrub_space_idle_before");
    window_->ScrubStartForTest(start);
    Pump(40);
    window_->ScrubTargetForTest(middle);
    Pump(30);
    window_->ScrubFinishForTest(target);
    const int presented_before = window_->PresentedFrameCountForTest();
    // 用户动作：松手后立刻 Space（不 Pump，保证落在 seek 落地之前）。
    window_->PressSpaceKeyForTest();
    const bool pending = window_->OnlinePendingPlayForTest();
    Pump(1000);
    const int presented_after = window_->PresentedFrameCountForTest();
    const bool playing = window_->IsPlayingForTest();
    const bool idle = window_->OnlineSeekIdleForTest();
    const std::string suffix = std::to_string(i);
    const QString detail =
        QStringLiteral("iter=%1 pending=%2 presented=%3->%4 seek_idle=%5")
            .arg(i)
            .arg(pending ? 1 : 0)
            .arg(presented_before)
            .arg(presented_after)
            .arg(idle ? 1 : 0);
    Check((std::string("scrub_space_playing_") + suffix).c_str(), playing,
          detail);
    Check((std::string("scrub_space_presented_") + suffix).c_str(),
          presented_after > presented_before, detail);
    Check((std::string("scrub_space_seek_idle_") + suffix).c_str(), idle,
          detail);
  }
  Print(QStringLiteral(
            "scrub_space_result mode=%1 checks=%2 failures=%3 verdict=%4")
            .arg(local ? QStringLiteral("local") : QStringLiteral("online"))
            .arg(checks_)
            .arg(failures_)
            .arg(failures_ == 0 ? QStringLiteral("pass")
                                : QStringLiteral("fail")));
  QCoreApplication::exit(failures_ == 0 ? 0 : 1);
}

void OnlineDragPreviewTestRunner::Run() {
  if (options_.cache_switch) {
    RunCacheSwitch();
    return;
  }
  if (options_.scrub_space) {
    RunScrubSpace();
    return;
  }
  if (options_.local_only) {
    RunLocalParity();
    return;
  }
  if (options_.parity) {
    RunOnlineParity();
    return;
  }
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
