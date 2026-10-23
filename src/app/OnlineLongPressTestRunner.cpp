#include "app/OnlineLongPressTestRunner.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cstdio>
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

QString TraceText(const std::vector<qint64>& trace) {
  QStringList parts;
  for (const qint64 frame : trace) {
    parts << QString::number(frame);
  }
  return parts.join(QLatin1Char(','));
}

}  // namespace

OnlineLongPressTestRunner::OnlineLongPressTestRunner(
    MainWindow* window, OnlineLongPressTestOptions options)
    : window_(window), options_(std::move(options)) {}

bool OnlineLongPressTestRunner::WaitUntil(
    const std::function<bool()>& predicate, int timeout_ms, const char* label) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    // 先跑事件循环：在线落地/连发/心跳都在事件队列里。
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  Print(QStringLiteral("online_longpress_wait_timeout label=%1 elapsed=%2ms")
            .arg(QString::fromUtf8(label))
            .arg(timer.elapsed()));
  return predicate();
}

bool OnlineLongPressTestRunner::WaitStepIdle(int timeout_ms, const char* label) {
  return WaitUntil([this]() { return window_->OnlineStepIdleForTest(); },
                   timeout_ms, label);
}

void OnlineLongPressTestRunner::HoldKey(int key, int hold_ms) {
  window_->FocusVideoForTest();
  window_->PressKeyDownForTest(key);
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < hold_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  window_->PressKeyUpForTest(key);
}

void OnlineLongPressTestRunner::Start() {
  QTimer::singleShot(0, window_, [this]() { Run(); });
}

void OnlineLongPressTestRunner::Run() {
  auto check = [this](const char* name, bool ok, const QString& detail = {}) {
    ++checks_;
    if (!ok) {
      ++failures_;
    }
    Print(QStringLiteral("online_longpress_check %1 ok=%2%3")
              .arg(QString::fromUtf8(name))
              .arg(ok ? 1 : 0)
              .arg(detail.isEmpty() ? QString()
                                    : QStringLiteral(" detail=") + detail));
  };
  auto activate_window = [this]() {
    window_->raise();
    window_->activateWindow();
    window_->FocusVideoForTest();
  };
  auto pump_for = [](int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  };

  Print(QStringLiteral(
            "online_longpress_sample tag=%1 bvid=%2 cid=%3 page=%4 hold_ms=%5")
            .arg(options_.tag, options_.bvid)
            .arg(options_.cid)
            .arg(options_.page)
            .arg(options_.hold_ms));

  activate_window();
  window_->OpenOnlineDialogForTest();
  OnlineResolveDialog* dialog = window_->OnlineDialogForTest();
  if (dialog == nullptr) {
    check("dialog_created", false);
    QCoreApplication::exit(2);
    return;
  }
  QString input = options_.bvid;
  if (options_.page > 1) {
    input = QStringLiteral("https://www.bilibili.com/video/%1?p=%2")
                .arg(options_.bvid)
                .arg(options_.page);
  }
  dialog->SetInputForTest(input);
  dialog->StartResolveForTest();
  const bool resolved = dialog->WaitForResolveForTest(options_.timeout_ms);
  check("resolve_completed", resolved);
  check("resolve_result", dialog->HasResultForTest(),
        dialog->ErrorTextForTest());
  if (!dialog->HasResultForTest() || !dialog->ConfirmEnabledForTest()) {
    Print(QStringLiteral(
              "online_longpress_result checks=%1 failures=%2 verdict=fail")
              .arg(checks_)
              .arg(failures_));
    QCoreApplication::exit(2);
    return;
  }

  QElapsedTimer first_frame_timer;
  first_frame_timer.start();
  dialog->ClickConfirmForTest();
  const bool ready = window_->WaitForOnlineReadyForTest(options_.timeout_ms);
  const qint64 first_frame_ms = first_frame_timer.elapsed();
  quality_ = window_->OnlineQualityForTest();
  check("online_ready", ready, window_->OnlineLastErrorForTest());
  if (!ready) {
    Print(QStringLiteral(
              "online_longpress_result checks=%1 failures=%2 verdict=fail")
              .arg(checks_)
              .arg(failures_));
    QCoreApplication::exit(2);
    return;
  }
  check("growing_session", !window_->OnlineCacheHitForTest(),
        QStringLiteral("cache_hit=%1")
            .arg(window_->OnlineCacheHitForTest() ? 1 : 0));
  Print(QStringLiteral("online_longpress_first_frame ms=%1 qn=%2")
            .arg(first_frame_ms)
            .arg(quality_));
  check("default_paused", !window_->IsPlayingForTest());
  activate_window();
  check("window_active",
        WaitUntil([this]() { return window_->isActiveWindow(); }, 1500,
                  "window_active"));

  const bool indexed =
      WaitUntil([this]() { return window_->OnlineStableFramesForTest() >= 60; },
                options_.timeout_ms, "index_prefix");
  check("index_prefix", indexed,
        QStringLiteral("stable=%1 container=%2")
            .arg(window_->OnlineStableFramesForTest())
            .arg(window_->OnlineContainerFramesForTest()));
  if (!indexed) {
    Print(QStringLiteral(
              "online_longpress_result checks=%1 failures=%2 verdict=fail")
              .arg(checks_)
              .arg(failures_));
    QCoreApplication::exit(1);
    return;
  }
  const std::int64_t last = window_->OnlineLastFrameForTest();
  check("last_frame_known", last > 100,
        QStringLiteral("last=%1").arg(static_cast<qint64>(last)));

  // UI 心跳：16ms 定时器；>200ms 记为 UI 停顿（P5/OP2 口径）。
  QElapsedTimer heartbeat_clock;
  heartbeat_clock.start();
  qint64 heartbeat_last = 0;
  qint64 heartbeat_max = 0;
  QTimer heartbeat;
  heartbeat.setInterval(16);
  heartbeat.setTimerType(Qt::PreciseTimer);
  QObject::connect(&heartbeat, &QTimer::timeout, [&]() {
    const qint64 now = heartbeat_clock.elapsed();
    if (heartbeat_last > 0) {
      const qint64 gap = now - heartbeat_last;
      if (gap > 200) {
        Print(QStringLiteral("online_longpress_ui_gap_event gap_ms=%1 at_ms=%2")
                  .arg(gap)
                  .arg(now));
      }
      heartbeat_max = std::max(heartbeat_max, gap);
    }
    heartbeat_last = now;
  });
  heartbeat.start();

  // ---- 未缓存区步进：提示 + 取消（先做，保证下载还没追上远端目标）----
  std::int64_t uncached_target = last - 120;
  if (uncached_target < 1) {
    uncached_target = 1;
  }
  if (window_->OnlineFrameCachedForTest(uncached_target)) {
    uncached_target = std::max<std::int64_t>(1, last - 5);
  }
  const bool uncached_available =
      !window_->OnlineFrameCachedForTest(uncached_target);
  check("uncached_available", uncached_available,
        QStringLiteral("target=%1 cached=%2")
            .arg(static_cast<qint64>(uncached_target))
            .arg(window_->OnlineFrameCachedForTest(uncached_target) ? 1 : 0));
  if (uncached_available) {
    QElapsedTimer jump_timer;
    jump_timer.start();
    window_->GoToFrameForTest(uncached_target);
    const qint64 jump_ui_ms = jump_timer.elapsed();
    const QString jump_status = window_->StatusMessageForTest();
    check("uncached_jump_prompt",
          jump_status.contains(QStringLiteral("缓冲中（目标帧")) &&
              jump_status.contains(QString::number(uncached_target)),
          jump_status);
    check("uncached_jump_ui_le_100ms", jump_ui_ms <= 100,
          QStringLiteral("ms=%1").arg(jump_ui_ms));

    // 步进落在未缓存目标：从“已请求的目标帧”+1，并取消旧等待。
    const std::int64_t step_target = uncached_target + 1;
    QElapsedTimer step_timer;
    step_timer.start();
    window_->PressKeyForTest(Qt::Key_PageDown);
    const qint64 step_ui_ms = step_timer.elapsed();
    const QString step_status = window_->StatusMessageForTest();
    check("uncached_step_prompt",
          step_status.contains(QStringLiteral("缓冲中（目标帧")) &&
              step_status.contains(QString::number(step_target)),
          step_status);
    check("uncached_step_ui_le_100ms", step_ui_ms <= 100,
          QStringLiteral("ms=%1").arg(step_ui_ms));

    // 长按继续推进：提示跟随最新目标，UI 不停顿。
    const QString before_hold_status = window_->StatusMessageForTest();
    const int retarget_before = window_->OnlineStepRetargetCountForTest();
    HoldKey(Qt::Key_PageDown, 600);
    const QString after_hold_status = window_->StatusMessageForTest();
    check("uncached_hold_released", !window_->StepRepeatActiveForTest());
    check("uncached_hold_status_updates",
          after_hold_status != before_hold_status,
          QStringLiteral("before=%1 after=%2")
              .arg(before_hold_status, after_hold_status));
    check("uncached_hold_no_retarget",
          window_->OnlineStepRetargetCountForTest() == retarget_before,
          QStringLiteral("before=%1 after=%2")
              .arg(retarget_before)
              .arg(window_->OnlineStepRetargetCountForTest()));

    // 取消：跳回已缓存区，旧等待取消 ≤100ms，落点精确。
    QElapsedTimer cancel_timer;
    cancel_timer.start();
    window_->GoToFrameForTest(options_.start_frame);
    const qint64 cancel_ui_ms = cancel_timer.elapsed();
    check("uncached_cancel_ui_le_100ms", cancel_ui_ms <= 100,
          QStringLiteral("ms=%1").arg(cancel_ui_ms));
    const bool back_cached = WaitUntil(
        [this]() {
          return window_->OnlineStepIdleForTest() &&
                 window_->CurrentFrameForTest() == options_.start_frame;
        },
        10000, "back_to_cached");
    const qint64 back_ms = cancel_timer.elapsed();
    check("back_to_cached_exact", back_cached,
          QStringLiteral("frame=%1")
              .arg(window_->CurrentFrameForTest()));
    check("back_to_cached_le_1500ms", back_ms <= 1500,
          QStringLiteral("ms=%1").arg(back_ms));
    check("uncached_hold_no_retarget_after_cancel",
          window_->OnlineStepRetargetCountForTest() == retarget_before,
          window_->OnlineStepStateForTest());
    Print(QStringLiteral(
              "online_longpress_uncached target=%1 jump_ui_ms=%2 step_ui_ms=%3 "
              "cancel_ui_ms=%4 back_ms=%5 retargets=%6 landed_frame=%7")
              .arg(static_cast<qint64>(uncached_target))
              .arg(jump_ui_ms)
              .arg(step_ui_ms)
              .arg(cancel_ui_ms)
              .arg(back_ms)
              .arg(window_->OnlineStepRetargetCountForTest())
              .arg(window_->CurrentFrameForTest()));
  }

  // 回到缓存区后等索引仍在（重建会话必须保留部分索引）。
  const bool cached_start =
      WaitUntil([this]() { return window_->OnlineFrameCachedForTest(
                               options_.start_frame); },
                options_.timeout_ms, "cached_start");
  check("cached_start", cached_start,
        QStringLiteral("frame=%1 stable=%2")
            .arg(window_->CurrentFrameForTest())
            .arg(window_->OnlineStableFramesForTest()));

  // ---- 已缓存区：长按前进 2s ----
  const std::int64_t forward_start = window_->CurrentFrameForTest();
  window_->ResetOnlineStepTraceForTest();
  window_->FocusVideoForTest();
  window_->PressKeyDownForTest(Qt::Key_PageDown);
  const bool forward_active_before = window_->StepRepeatActiveForTest();
  QElapsedTimer forward_timer;
  forward_timer.start();
  while (forward_timer.elapsed() < options_.hold_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const bool forward_active_during = window_->StepRepeatActiveForTest();
  // 物理长按会混入 OS 自动重复的 KeyPress/KeyRelease；连发不能被它停掉。
  window_->PressAutoRepeatKeyForTest(Qt::Key_PageDown);
  window_->PressAutoRepeatReleaseForTest(Qt::Key_PageDown);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  const bool forward_active_after_auto = window_->StepRepeatActiveForTest();
  window_->PressKeyUpForTest(Qt::Key_PageDown);
  check("forward_repeat_started", forward_active_before);
  check("forward_autorepeat_keeps_repeat", forward_active_after_auto);
  check("forward_repeat_active_2s", forward_active_during);
  check("forward_release_stops", !window_->StepRepeatActiveForTest());
  WaitStepIdle(5000, "forward_idle");
  std::vector<qint64> forward_trace = window_->OnlineStepTraceForTest();
  int forward_bad = 0;
  qint64 forward_prev = forward_start;
  for (const qint64 frame : forward_trace) {
    if (frame != forward_prev + 1) {
      ++forward_bad;
    }
    forward_prev = frame;
  }
  check("forward_trace_consecutive", forward_bad == 0 && !forward_trace.empty(),
        QStringLiteral("bad=%1 trace=%2").arg(forward_bad).arg(TraceText(forward_trace)));
  check("forward_first_step", !forward_trace.empty() &&
                                  forward_trace.front() == forward_start + 1,
        QStringLiteral("first=%1 start=%2")
            .arg(forward_trace.empty() ? -1 : forward_trace.front())
            .arg(forward_start));
  check("forward_count_ge_20", forward_trace.size() >= 20,
        QStringLiteral("count=%1").arg(static_cast<qint64>(forward_trace.size())));
  check("forward_frame_matches",
        !forward_trace.empty() &&
            window_->CurrentFrameForTest() == forward_trace.back(),
        QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
  Print(QStringLiteral(
            "online_longpress_cached dir=forward start=%1 end=%2 frames=%3 "
            "bad=%4 hold_ms=%5")
            .arg(forward_start)
            .arg(window_->CurrentFrameForTest())
            .arg(static_cast<qint64>(forward_trace.size()))
            .arg(forward_bad)
            .arg(options_.hold_ms));

  // ---- 已缓存区：长按后退 2s ----
  const std::int64_t backward_start = window_->CurrentFrameForTest();
  window_->ResetOnlineStepTraceForTest();
  window_->FocusVideoForTest();
  window_->PressKeyDownForTest(Qt::Key_PageUp);
  const bool backward_active_before = window_->StepRepeatActiveForTest();
  QElapsedTimer backward_timer;
  backward_timer.start();
  while (backward_timer.elapsed() < options_.hold_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const bool backward_active_during = window_->StepRepeatActiveForTest();
  window_->PressKeyUpForTest(Qt::Key_PageUp);
  check("backward_repeat_started", backward_active_before);
  check("backward_repeat_active_2s", backward_active_during);
  check("backward_release_stops", !window_->StepRepeatActiveForTest());
  WaitStepIdle(5000, "backward_idle");
  std::vector<qint64> backward_trace = window_->OnlineStepTraceForTest();
  int backward_bad = 0;
  qint64 backward_prev = backward_start;
  for (const qint64 frame : backward_trace) {
    if (frame != backward_prev - 1) {
      ++backward_bad;
    }
    backward_prev = frame;
  }
  check("backward_trace_consecutive",
        backward_bad == 0 && !backward_trace.empty(),
        QStringLiteral("bad=%1 trace=%2")
            .arg(backward_bad)
            .arg(TraceText(backward_trace)));
  check("backward_first_step", !backward_trace.empty() &&
                                   backward_trace.front() == backward_start - 1,
        QStringLiteral("first=%1 start=%2")
            .arg(backward_trace.empty() ? -1 : backward_trace.front())
            .arg(backward_start));
  check("backward_count_ge_20", backward_trace.size() >= 20,
        QStringLiteral("count=%1")
            .arg(static_cast<qint64>(backward_trace.size())));
  check("backward_no_boundary_hit",
        !backward_trace.empty() &&
            *std::min_element(backward_trace.begin(), backward_trace.end()) > 0,
        QStringLiteral("min=%1")
            .arg(backward_trace.empty()
                     ? -1
                     : *std::min_element(backward_trace.begin(),
                                         backward_trace.end())));
  Print(QStringLiteral(
            "online_longpress_cached dir=backward start=%1 end=%2 frames=%3 "
            "bad=%4 hold_ms=%5")
            .arg(backward_start)
            .arg(window_->CurrentFrameForTest())
            .arg(static_cast<qint64>(backward_trace.size()))
            .arg(backward_bad)
            .arg(options_.hold_ms));

  // ---- 伪 KeyRelease：物理键仍按住时，不能停连发 ----
  // Qt/Windows 在极少数时序下会在键仍按下时补一个非 autoRepeat 的 release；
  // 该场景无法用普通 QKeyEvent 表示“物理键仍按下”，用测试钩子模拟。
  window_->SetStepKeyPhysicalDownForTest(1);
  window_->FocusVideoForTest();
  window_->ResetOnlineStepTraceForTest();
  const std::int64_t spurious_start = window_->CurrentFrameForTest();
  window_->PressKeyDownForTest(Qt::Key_PageDown);
  QElapsedTimer spurious_timer;
  spurious_timer.start();
  while (spurious_timer.elapsed() < 400) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  window_->PressKeyUpForTest(Qt::Key_PageDown);  // 伪 release：物理键仍按住
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  const bool spurious_survives = window_->StepRepeatActiveForTest();
  while (spurious_timer.elapsed() < 800) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const int spurious_frames =
      static_cast<int>(window_->OnlineStepTraceForTest().size());
  window_->SetStepKeyPhysicalDownForTest(0);
  window_->PressKeyUpForTest(Qt::Key_PageDown);  // 真正的 keyup
  const bool spurious_stopped = !window_->StepRepeatActiveForTest();
  window_->SetStepKeyPhysicalDownForTest(-1);
  WaitStepIdle(5000, "spurious_release_idle");
  check("spurious_release_keeps_repeat", spurious_survives,
        QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
  check("spurious_release_still_steps", spurious_frames >= 1,
        QStringLiteral("frames=%1 start=%2")
            .arg(spurious_frames)
            .arg(spurious_start));
  check("physical_release_stops", spurious_stopped);

  // ---- 短按回归：单次 ±1、松开不走帧、快速双击不丢 ----
  const std::int64_t short_base = window_->CurrentFrameForTest();
  window_->PressKeyForTest(Qt::Key_PageDown);
  const bool short_one = WaitUntil(
      [this, short_base]() {
        return window_->OnlineStepIdleForTest() &&
               window_->CurrentFrameForTest() == short_base + 1;
      },
      3000, "short_one");
  check("short_press_one_frame", short_one,
        QStringLiteral("frame=%1 expected=%2")
            .arg(window_->CurrentFrameForTest())
            .arg(short_base + 1));
  pump_for(350);
  check("short_press_no_residual",
        window_->CurrentFrameForTest() == short_base + 1,
        QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));

  const std::int64_t double_base = window_->CurrentFrameForTest();
  window_->PressKeyForTest(Qt::Key_PageDown);
  window_->PressKeyForTest(Qt::Key_PageDown);
  const bool double_forward = WaitUntil(
      [this, double_base]() {
        return window_->OnlineStepIdleForTest() &&
               window_->CurrentFrameForTest() == double_base + 2;
      },
      4000, "double_forward");
  check("short_double_press_queue", double_forward,
        QStringLiteral("frame=%1 expected=%2")
            .arg(window_->CurrentFrameForTest())
            .arg(double_base + 2));
  const std::int64_t double_back_base = window_->CurrentFrameForTest();
  window_->PressKeyForTest(Qt::Key_PageUp);
  window_->PressKeyForTest(Qt::Key_PageUp);
  const bool double_backward = WaitUntil(
      [this, double_back_base]() {
        return window_->OnlineStepIdleForTest() &&
               window_->CurrentFrameForTest() == double_back_base - 2;
      },
      4000, "double_backward");
  check("short_double_back_queue", double_backward,
        QStringLiteral("frame=%1 expected=%2")
            .arg(window_->CurrentFrameForTest())
            .arg(double_back_base - 2));
  Print(QStringLiteral(
            "online_longpress_short_press base=%1 after_single=%2 "
            "after_double=%3")
            .arg(short_base)
            .arg(short_base + 1)
            .arg(double_base + 2));

  // ---- 首帧边界：0 处按住 PgUp，不启动连发、不动、不 seek ----
  window_->GoToFrameForTest(0);
  const bool at_zero = WaitUntil(
      [this]() {
        return window_->OnlineStepIdleForTest() &&
               window_->CurrentFrameForTest() == 0;
      },
      5000, "goto_zero");
  check("goto_zero", at_zero,
        QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
  const int boundary_before = window_->OnlineStepBoundaryStopsForTest();
  window_->ResetOnlineStepTraceForTest();
  window_->FocusVideoForTest();
  window_->PressKeyDownForTest(Qt::Key_PageUp);
  const bool lower_stopped = !window_->StepRepeatActiveForTest();
  check("lower_boundary_stops_repeat", lower_stopped);
  pump_for(400);
  window_->PressKeyUpForTest(Qt::Key_PageUp);
  check("lower_boundary_frame_zero", window_->CurrentFrameForTest() == 0,
        QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
  check("lower_boundary_no_seek", window_->OnlineStepTraceForTest().empty());
  check("lower_boundary_count",
        window_->OnlineStepBoundaryStopsForTest() > boundary_before,
        QStringLiteral("before=%1 after=%2")
            .arg(boundary_before)
            .arg(window_->OnlineStepBoundaryStopsForTest()));

  // ---- 末帧边界：先请求末帧附近（可能未缓存），再按住 PgDn ----
  const std::int64_t high_last = window_->OnlineLastFrameForTest();
  const std::int64_t near_last = std::max<std::int64_t>(0, high_last - 2);
  window_->GoToFrameForTest(near_last);
  pump_for(60);  // 让等待提示/worker 启动，不要求落地
  const int high_boundary_before = window_->OnlineStepBoundaryStopsForTest();
  window_->FocusVideoForTest();
  window_->PressKeyDownForTest(Qt::Key_PageDown);
  pump_for(800);
  const bool upper_stopped = !window_->StepRepeatActiveForTest();
  window_->PressKeyUpForTest(Qt::Key_PageDown);
  check("upper_boundary_stops_repeat", upper_stopped,
        window_->OnlineStepStateForTest());
  check("upper_boundary_frame_within_last",
        window_->CurrentFrameForTest() <= high_last,
        QStringLiteral("frame=%1 last=%2")
            .arg(window_->CurrentFrameForTest())
            .arg(high_last));
  check("upper_boundary_count",
        window_->OnlineStepBoundaryStopsForTest() > high_boundary_before,
        QStringLiteral("before=%1 after=%2")
            .arg(high_boundary_before)
            .arg(window_->OnlineStepBoundaryStopsForTest()));
  Print(QStringLiteral(
            "online_longpress_boundary low=0 high=%1 boundary_stops=%2")
            .arg(high_last)
            .arg(window_->OnlineStepBoundaryStopsForTest()));

  heartbeat.stop();
  const qint64 tail_gap = heartbeat_clock.elapsed() - heartbeat_last;
  if (tail_gap > 0) {
    heartbeat_max = std::max(heartbeat_max, tail_gap);
  }
  check("ui_gap_le_200ms", heartbeat_max <= 200,
        QStringLiteral("max_gap_ms=%1").arg(heartbeat_max));
  Print(QStringLiteral("online_longpress_ui_gap max_ms=%1").arg(heartbeat_max));
  Print(QStringLiteral("online_longpress_state %1")
            .arg(window_->OnlineStepStateForTest()));

  // 关窗：可能在未缓存等待中，必须 ≤1s。
  QElapsedTimer close_timer;
  close_timer.start();
  const bool closed = window_->CloseForTest();
  const qint64 close_ms = close_timer.elapsed();
  check("close", closed);
  check("close_le_1000ms", close_ms <= 1000,
        QStringLiteral("ms=%1").arg(close_ms));
  Print(QStringLiteral("online_longpress_close ms=%1").arg(close_ms));
  Print(QStringLiteral(
            "online_longpress_result checks=%1 failures=%2 verdict=%3")
            .arg(checks_)
            .arg(failures_)
            .arg(failures_ == 0 ? QStringLiteral("pass")
                                : QStringLiteral("fail")));
  QCoreApplication::exit(failures_ == 0 ? 0 : 1);
}

}  // namespace lapian::app
