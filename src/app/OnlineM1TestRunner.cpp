#include "app/OnlineM1TestRunner.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

#include "app/MainWindow.h"
#include "app/OnlineResolveDialog.h"

namespace lapian::app {
namespace {

// T8.14：M2 门期间复现过一次未定位的 teardown 崩溃；探针进程记录符号化栈，
// 产品不安装这个过滤器。
#ifdef _WIN32
LONG WINAPI M1CrashFilter(EXCEPTION_POINTERS* info) {
  static LONG handled = 0;
  if (InterlockedExchange(&handled, 1) != 0) {
    return EXCEPTION_EXECUTE_HANDLER;
  }
  const DWORD code = info != nullptr && info->ExceptionRecord != nullptr
                         ? info->ExceptionRecord->ExceptionCode
                         : 0;
  std::fprintf(stderr, "online_m1_crash code=0x%08lx\n",
               static_cast<unsigned long>(code));
  void* frames[48] = {};
  const USHORT count = CaptureStackBackTrace(0, 48, frames, nullptr);
  HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  SymInitialize(process, nullptr, TRUE);
  for (USHORT i = 0; i < count; ++i) {
    char buffer[sizeof(SYMBOL_INFO) + 256] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    DWORD64 displacement = 0;
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD line_displacement = 0;
    const bool has_line = SymGetLineFromAddr64(
        process, reinterpret_cast<DWORD64>(frames[i]), &line_displacement,
        &line);
    if (SymFromAddr(process, reinterpret_cast<DWORD64>(frames[i]),
                    &displacement, symbol)) {
      std::fprintf(stderr, "online_m1_crash_frame %u %s+0x%llx %s:%lu\n", i,
                   symbol->Name,
                   static_cast<unsigned long long>(displacement),
                   has_line ? line.FileName : "", has_line ? line.LineNumber : 0);
    } else {
      std::fprintf(stderr, "online_m1_crash_frame %u addr=%p\n", i, frames[i]);
    }
    std::fflush(stderr);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void Print(const QString& text) {
  std::fprintf(stderr, "%s\n", text.toUtf8().constData());
  std::fflush(stderr);
}

qint64 Percentile(std::vector<qint64> samples, double p) {
  if (samples.empty()) {
    return -1;
  }
  std::sort(samples.begin(), samples.end());
  const std::size_t index = static_cast<std::size_t>(
      std::clamp(p, 0.0, 1.0) * static_cast<double>(samples.size() - 1));
  return samples[index];
}

}  // namespace

OnlineM1TestRunner::OnlineM1TestRunner(MainWindow* window,
                                       OnlineM1TestOptions options)
    : window_(window), options_(std::move(options)) {}

bool OnlineM1TestRunner::WaitUntil(const std::function<bool()>& predicate,
                                   int timeout_ms, const char* label) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    // 先让事件循环跑一次：缓存命中的本地 seek 是同步的，predicate 可能立刻为真；
    // 若直接返回，连续 40 次 seek 会占住 UI 线程，让心跳误报“UI 停顿”。
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  Print(QStringLiteral("online_m1_wait_timeout label=%1 elapsed=%2ms")
            .arg(QString::fromUtf8(label))
            .arg(timer.elapsed()));
  return predicate();
}

void OnlineM1TestRunner::Start() {
  QTimer::singleShot(0, window_, [this]() { Run(); });
}

void OnlineM1TestRunner::Run() {
#ifdef _WIN32
  SetUnhandledExceptionFilter(M1CrashFilter);
#endif
  auto check = [this](const char* name, bool ok, const QString& detail = {}) {
    ++checks_;
    if (!ok) {
      ++failures_;
    }
    Print(QStringLiteral("online_m1_check %1 ok=%2%3")
              .arg(QString::fromUtf8(name))
              .arg(ok ? 1 : 0)
              .arg(detail.isEmpty() ? QString()
                                    : QStringLiteral(" detail=") + detail));
  };

  Print(QStringLiteral("online_m1_sample tag=%1 bvid=%2 cid=%3 page=%4")
            .arg(options_.tag, options_.bvid)
            .arg(options_.cid)
            .arg(options_.page));
  // 键位自动化需要真实“激活窗口 + 视频区焦点”：QAction 的 WindowShortcut
  // 只在激活窗口生效。交付机/CI 从后台进程启动 GUI 时窗口可能拿不到前台，
  // 这里显式激活（等价于用户点一下窗口），否则 Space/Ctrl+M/S 会被吞掉。
  auto activate_window = [this]() {
    window_->raise();
    window_->activateWindow();
    window_->FocusVideoForTest();
  };
  activate_window();
  if (options_.throttle_bytes_per_sec > 0) {
    // T8.14 OP3：只节流视频流，让未缓存区在测试窗口内仍然存在。
    window_->SetOnlineDownloadThrottleForTest(options_.throttle_bytes_per_sec);
  }
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
  QElapsedTimer resolve_timer;
  resolve_timer.start();
  dialog->SetInputForTest(input);
  dialog->StartResolveForTest();
  const bool resolved = dialog->WaitForResolveForTest(options_.timeout_ms);
  const qint64 resolve_ms = resolve_timer.elapsed();
  check("resolve_completed", resolved);
  check("resolve_result", dialog->HasResultForTest(),
        dialog->ErrorTextForTest());
  check("confirm_enabled", dialog->ConfirmEnabledForTest());
  Print(QStringLiteral("online_m1_resolve ms=%1 title=%2 requests=%3")
            .arg(resolve_ms)
            .arg(dialog->TitleTextForTest())
            .arg(dialog->TotalHttpRequestsForTest()));
  if (!dialog->HasResultForTest() || !dialog->ConfirmEnabledForTest()) {
    QCoreApplication::exit(2);
    return;
  }

  if (options_.cancel_only) {
    dialog->ClickConfirmForTest();
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 80) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const QString before = window_->OnlineStateForTest();
    QElapsedTimer cancel_timer;
    cancel_timer.start();
    window_->CancelOnlineLoadingForTest();
    const qint64 cancel_ms = cancel_timer.elapsed();
    const QString status = window_->StatusMessageForTest();
    check("cancel_buffering",
          before.contains(QStringLiteral("state=buffering")) ||
              before.contains(QStringLiteral("state=preparing")),
          QStringLiteral("before=%1").arg(before));
    check("cancel_le_100ms", cancel_ms <= 100,
          QStringLiteral("ms=%1").arg(cancel_ms));
    check("cancel_status", status.contains(QStringLiteral("已取消")), status);
    check("cancel_returns_empty", !window_->OnlineModeForTest());
    Print(QStringLiteral("online_m1_cancel ms=%1 status=%2")
              .arg(cancel_ms)
              .arg(status));
    Print(QStringLiteral("online_m1_result checks=%1 failures=%2 verdict=%3")
              .arg(checks_)
              .arg(failures_)
              .arg(failures_ == 0 ? QStringLiteral("pass")
                                  : QStringLiteral("fail")));
    QCoreApplication::exit(failures_ == 0 ? 0 : 1);
    return;
  }

  // UI 心跳：20ms 定时器，记录事件循环最大间隔；>200ms 视为 UI 停顿。
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
        Print(QStringLiteral("online_m1_ui_gap_event gap_ms=%1 at_ms=%2")
                  .arg(gap)
                  .arg(now));
      }
      heartbeat_max = std::max(heartbeat_max, gap);
    }
    heartbeat_last = now;
  });
  heartbeat.start();

  QElapsedTimer first_frame_timer;
  first_frame_timer.start();
  dialog->ClickConfirmForTest();
  const bool ready =
      window_->WaitForOnlineReadyForTest(options_.timeout_ms);
  const qint64 first_frame_ms = first_frame_timer.elapsed();
  online_ready_ = ready;
  quality_ = window_->OnlineQualityForTest();
  check("online_ready", ready, window_->OnlineLastErrorForTest());
  if (!ready) {
    Print(QStringLiteral("online_m1_result checks=%1 failures=%2 verdict=fail")
              .arg(checks_)
              .arg(failures_));
    QCoreApplication::exit(2);
    return;
  }
  const bool first_frame_dash = window_->OnlineDashForTest();
  const double first_frame_fps = window_->OnlineDashFpsForTest() > 0.0
                                     ? window_->OnlineDashFpsForTest()
                                     : window_->FrameRateForTest();
  Print(QStringLiteral(
            "online_m1_first_frame ms=%1 state=%2 dash=%3 fps=%4")
            .arg(first_frame_ms)
            .arg(window_->OnlineStateForTest())
            .arg(first_frame_dash ? 1 : 0)
            .arg(first_frame_fps, 0, 'f', 3));
  {
    // T8.14 OP1：首帧后 1s 采样真实下载路径的持续下行，证明测试时段
    // 满足 DEC-2 的 ≥1MB/s 条件（不打印签名 URL/Cookie）。
    const qint64 rate_start_bytes = window_->OnlinePublishedBytesForTest();
    QElapsedTimer rate_timer;
    rate_timer.start();
    while (rate_timer.elapsed() < 1000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const qint64 rate_ms = std::max<qint64>(1, rate_timer.elapsed());
    const qint64 rate_bytes =
        window_->OnlinePublishedBytesForTest() - rate_start_bytes;
    const double rate_mbps =
        static_cast<double>(rate_bytes) / 1048576.0 /
        (static_cast<double>(rate_ms) / 1000.0);
    Print(QStringLiteral("online_m1_rate bytes=%1 ms=%2 mbps=%3")
              .arg(rate_bytes)
              .arg(rate_ms)
              .arg(rate_mbps, 0, 'f', 3));
  }
  const bool cache_hit = window_->OnlineCacheHitForTest();
  Print(QStringLiteral("online_m1_cache_hit cache_hit=%1 path=%2")
            .arg(cache_hit ? 1 : 0)
            .arg(window_->OnlineCachePathForTest()));
  {
    // 键位自动化的前置条件：窗口是激活窗口，且焦点不在文本框里；否则
    // QAction 快捷键（Space/Ctrl+M/S）会被窗口/文本让位规则吞掉。
    // activateWindow() 是异步的：从后台进程启动时，必须让事件循环跑一拍，
    // 等 Qt 真正收到激活事件后再按第一个键（否则第一下 Space 会丢）。
    activate_window();
    const bool window_active =
        WaitUntil([this]() { return window_->isActiveWindow(); }, 1500,
                  "window_active");
    check("window_active", window_active);
    QWidget* focus = QApplication::focusWidget();
    Print(QStringLiteral("online_m1_focus widget=%1 active=%2")
              .arg(focus == nullptr
                       ? QStringLiteral("null")
                       : QString::fromLatin1(focus->metaObject()->className()))
              .arg(window_->isActiveWindow() ? 1 : 0));
  }
  check("default_paused", !window_->IsPlayingForTest());
  check("frontend_quality", quality_ > 0,
        QStringLiteral("qn=%1").arg(quality_));

  // Space：状态变化 ≤100ms。
  initial_frame_ = window_->CurrentFrameForTest();
  QElapsedTimer space_timer;
  space_timer.start();
  window_->PressSpaceKeyForTest();
  const bool playing =
      WaitUntil([this]() { return window_->IsPlayingForTest(); }, 500,
                "space_play");
  const qint64 space_ms = space_timer.elapsed();
  check("space_play", playing);
  check("space_state_le_100ms", space_ms <= 100,
        QStringLiteral("ms=%1").arg(space_ms));
  QElapsedTimer play_timer;
  play_timer.start();
  while (play_timer.elapsed() < 1200) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const qint64 played_frame = window_->CurrentFrameForTest();
  check("play_advanced", played_frame > initial_frame_,
        QStringLiteral("from=%1 to=%2").arg(initial_frame_).arg(played_frame));
  window_->PressSpaceKeyForTest();
  check("space_pause",
        WaitUntil([this]() { return !window_->IsPlayingForTest(); }, 500,
                  "space_pause"));

  // 部分索引至少覆盖 2s 以上，做 20 次 PgUp/PgDn 与 20 次随机 seek。
  // 完整缓存命中走本地全量索引（IndexReadyForTest），不是在线部分索引。
  const bool indexed =
      cache_hit
          ? WaitUntil([this]() { return window_->IndexReadyForTest(); },
                      options_.timeout_ms, "cache_index")
          : WaitUntil(
                [this]() { return window_->OnlineStableFramesForTest() >= 60; },
                options_.timeout_ms, "online_index");
  check("index_prefix", indexed,
        QStringLiteral("stable=%1 index=%2")
            .arg(window_->OnlineStableFramesForTest())
            .arg(window_->IndexFrameCountForTest()));
  qint64 seek_target = 60;
  if (indexed) {
    window_->GoToFrameForTest(20);
    WaitUntil([this]() {
      return window_->OnlineSeekIdleForTest() &&
             window_->CurrentFrameForTest() == 20;
    }, 3000, "p7_start");
    qint64 current = window_->CurrentFrameForTest();
    std::vector<qint64> step_samples;
    int step_failures = 0;
    for (int i = 0; i < 20; ++i) {
      const bool forward = (i % 2) == 0;
      const qint64 expected = current + (forward ? 1 : -1);
      if (expected < 0) {
        continue;
      }
      QElapsedTimer timer;
      timer.start();
      window_->FocusVideoForTest();
      window_->PressKeyForTest(forward ? Qt::Key_PageDown : Qt::Key_PageUp);
      const bool ok =
          WaitUntil([this, expected]() {
            return window_->OnlineSeekIdleForTest() &&
                   window_->CurrentFrameForTest() == expected;
          }, 2000, forward ? "pgdn_step" : "pgup_step");
      const qint64 ms = timer.elapsed();
      step_samples.push_back(ms);
      if (!ok || window_->CurrentFrameForTest() != expected) {
        ++step_failures;
      }
      current = window_->CurrentFrameForTest();
    }
    const qint64 p50 = Percentile(step_samples, 0.50);
    const qint64 p95 = Percentile(step_samples, 0.95);
    check("p7_strict_pm1", step_failures == 0,
          QStringLiteral("failures=%1").arg(step_failures));
    check("p7_p95_le_80ms", p95 >= 0 && p95 <= 80,
          QStringLiteral("p95=%1").arg(p95));
    Print(QStringLiteral(
              "online_m1_p7 n=%1 failures=%2 p50=%3 p95=%4 max=%5")
              .arg(step_samples.size())
              .arg(step_failures)
              .arg(p50)
              .arg(p95)
              .arg(*std::max_element(step_samples.begin(), step_samples.end())));

    std::vector<qint64> seek_samples;
    int seek_failures = 0;
    const qint64 available_frames =
        cache_hit ? window_->IndexFrameCountForTest()
                  : window_->OnlineStableFramesForTest();
    const qint64 range = std::min<qint64>(200, available_frames - 2);
    // T8.10：DASH 1080P 的 GOP 是 5s，随机 seek 可能超出 P9 的“距前关键帧
    // ≤2s”口径。与 M2/OP3 一致，只选落在 2s 窗口内的目标，否则 p95 会把
    // 长 GOP 的物理成本算进 200ms 门槛。
    const double fps = window_->FrameRateForTest() > 0.0
                           ? window_->FrameRateForTest()
                           : 30.0;
    const qint64 max_keyframe_distance =
        std::max<qint64>(1, static_cast<qint64>(std::llround(fps * 2.0)));
    std::vector<qint64> seek_targets;
    for (int attempt = 0; attempt < 400 &&
                          seek_targets.size() < 20; ++attempt) {
      const qint64 candidate =
          5 + (static_cast<qint64>(attempt) * 37) % std::max<qint64>(1, range - 4);
      const qint64 distance =
          window_->FrameDistanceFromKeyframeForTest(candidate);
      if (distance >= 0 && distance <= max_keyframe_distance) {
        seek_targets.push_back(candidate);
      }
    }
    for (const qint64 target : seek_targets) {
      QElapsedTimer timer;
      timer.start();
      window_->GoToFrameForTest(target);
      const bool ok =
          WaitUntil([this, target]() {
            return window_->OnlineSeekIdleForTest() &&
                   window_->CurrentFrameForTest() == target;
          }, 3000, "cached_seek");
      seek_samples.push_back(timer.elapsed());
      if (!ok || window_->CurrentFrameForTest() != target) {
        ++seek_failures;
      }
      seek_target = target;
    }
    const qint64 seek_p50 = Percentile(seek_samples, 0.50);
    const qint64 seek_p95 = Percentile(seek_samples, 0.95);
    check("cached_seek_landing_zero", seek_failures == 0,
          QStringLiteral("failures=%1").arg(seek_failures));
    check("cached_seek_p95_le_200ms", seek_p95 >= 0 && seek_p95 <= 200,
          QStringLiteral("p95=%1").arg(seek_p95));
    Print(QStringLiteral(
              "online_m1_cached_seek n=%1 failures=%2 p50=%3 p95=%4")
              .arg(seek_samples.size())
              .arg(seek_failures)
              .arg(seek_p50)
              .arg(seek_p95));
  }

  // 未缓存 seek：立即显示“缓冲中（目标帧 N）”；改 seek 时旧等待被取消。
  // 完整缓存命中是全本地文件，没有“未缓存区”，跳过该专项。
  const qint64 container_frames = window_->OnlineContainerFramesForTest();
  if (!cache_hit && container_frames > 120 &&
      !window_->OnlineCacheCompleteForTest()) {
    const qint64 uncached_target = container_frames - 120;
    QElapsedTimer wait_ui_timer;
    wait_ui_timer.start();
    window_->GoToFrameForTest(uncached_target);
    const qint64 wait_ui_ms = wait_ui_timer.elapsed();
    const QString wait_status = window_->StatusMessageForTest();
    check("uncached_target_shown",
          wait_status.contains(QStringLiteral("缓冲中（目标帧")) &&
              wait_status.contains(QString::number(uncached_target)),
          wait_status);
    check("uncached_target_le_100ms", wait_ui_ms <= 100,
          QStringLiteral("ms=%1").arg(wait_ui_ms));
    Print(QStringLiteral("online_m1_uncached target=%1 ui_ms=%2")
              .arg(uncached_target)
              .arg(wait_ui_ms));

    // 改回到已缓存的帧：旧的未缓存等待必须被取消，UI 立即响应新目标。
    QElapsedTimer cancel_timer;
    cancel_timer.start();
    window_->GoToFrameForTest(seek_target);
    const qint64 cancel_ui_ms = cancel_timer.elapsed();
    check("old_wait_cancelled_ui_le_100ms", cancel_ui_ms <= 100,
          QStringLiteral("ms=%1").arg(cancel_ui_ms));
    const bool back_cached =
        WaitUntil([this, seek_target]() {
          return window_->OnlineSeekIdleForTest() &&
                 window_->CurrentFrameForTest() == seek_target;
        }, 5000, "back_to_cached");
    check("back_to_cached_exact", back_cached,
          QStringLiteral("frame=%1").arg(window_->CurrentFrameForTest()));
    Print(QStringLiteral("online_m1_cancel_wait ui_ms=%1 back_ok=%2")
              .arg(cancel_ui_ms)
              .arg(back_cached ? 1 : 0));

    // OP3 落点：再跳一次未缓存目标，等下载追上，验证“目标帧 == 显示帧”（误差 0）。
    // 只在 --uncached-landing 时执行，避免 20 次冷启动测量被整片下载拖慢。
    if (options_.uncached_landing) {
      window_->GoToFrameForTest(uncached_target);
      QElapsedTimer landing_timer;
      landing_timer.start();
      const bool landed =
          WaitUntil([this, uncached_target]() {
            return window_->OnlineSeekIdleForTest() &&
                   window_->CurrentFrameForTest() == uncached_target;
          }, options_.timeout_ms, "uncached_landing");
      Print(QStringLiteral(
                "online_m1_uncached_landing target=%1 frame=%2 ms=%3 ok=%4")
                .arg(uncached_target)
                .arg(window_->CurrentFrameForTest())
                .arg(landing_timer.elapsed())
                .arg(landed ? 1 : 0));
      check("uncached_landing_exact", landed,
            QStringLiteral("target=%1 frame=%2")
                .arg(uncached_target)
                .arg(window_->CurrentFrameForTest()));
    }
  } else if (!cache_hit && container_frames > 120) {
    // T8.10：DASH 1080P 下载在测试跑到这里前已经完整（31MB/2.7s），没有
    // “未缓存区”可测。记录跳过而不是把“已缓存”误判成未缓存提示。
    check("uncached_skipped_complete_cache", true,
          QStringLiteral("download complete before uncached test"));
    Print(QStringLiteral("online_m1_uncached skipped=complete_cache frames=%1")
              .arg(container_frames));
  }

  // 波形状态：有音频时至少 running/ready；不阻塞步进（上面已测）。
  const QString waveform = window_->WaveformStateForTest();
  Print(QStringLiteral("online_m1_waveform state=%1").arg(waveform));
  check("waveform_started",
        waveform != QStringLiteral("none") &&
            waveform != QStringLiteral("failed") &&
            waveform != QStringLiteral("cancelled"),
        waveform);

  // Ctrl+M：真实键事件。
  Print(QStringLiteral("online_m1_phase ctrl_m_begin at_ms=%1")
            .arg(heartbeat_clock.elapsed()));
  window_->FocusVideoForTest();
  window_->PressKeyForTest(Qt::Key_M, Qt::ControlModifier);
  const bool mark_added =
      WaitUntil([this]() { return window_->MarkCountForTest() > 0; }, 1000,
                "ctrl_m");
  check("ctrl_m", mark_added,
        QStringLiteral("marks=%1").arg(window_->MarkCountForTest()));
  Print(QStringLiteral("online_m1_phase ctrl_m_end at_ms=%1")
            .arg(heartbeat_clock.elapsed()));

  // S：真实键事件 + 后台编码；PNG 尺寸=当前流分辨率、无 UI。
  Print(QStringLiteral("online_m1_phase snapshot_begin at_ms=%1")
            .arg(heartbeat_clock.elapsed()));
  window_->PressSnapshotKeyForTest();
  window_->DrainSnapshotsForTest();
  Print(QStringLiteral("online_m1_phase snapshot_end at_ms=%1")
            .arg(heartbeat_clock.elapsed()));
  const QStringList saved = window_->SnapshotSavedPathsForTest();
  check("snapshot_saved", !saved.isEmpty(), window_->SnapshotStatusForTest());
  if (!saved.isEmpty()) {
    check("snapshot_file_exists", QFileInfo::exists(saved.last()), saved.last());
    check("snapshot_display_size",
          !window_->SnapshotDisplaySizeForTest().isEmpty(),
          QStringLiteral("%1x%2")
              .arg(window_->SnapshotDisplaySizeForTest().width())
              .arg(window_->SnapshotDisplaySizeForTest().height()));
  }

  heartbeat.stop();
  heartbeat_max = std::max(heartbeat_max, heartbeat_clock.elapsed() - heartbeat_last);
  check("ui_gap_le_200ms", heartbeat_max <= 200,
        QStringLiteral("max_gap_ms=%1").arg(heartbeat_max));
  Print(QStringLiteral("online_m1_ui_gap max_ms=%1").arg(heartbeat_max));

  // 关窗 ≤1s；下载/等待/播放中都必须有界。
  QElapsedTimer close_timer;
  close_timer.start();
  const bool closed = window_->CloseForTest();
  const qint64 close_ms = close_timer.elapsed();
  check("close", closed);
  check("close_le_1000ms", close_ms <= 1000,
        QStringLiteral("ms=%1").arg(close_ms));
  Print(QStringLiteral("online_m1_close ms=%1").arg(close_ms));
  Print(QStringLiteral("online_m1_quality qn=%1 gate=%2")
            .arg(quality_)
            .arg(quality_ >= 64 ? QStringLiteral("m1-720p")
                                : QStringLiteral("blocked-quality")));
  Print(QStringLiteral("online_m1_result checks=%1 failures=%2 verdict=%3")
            .arg(checks_)
            .arg(failures_)
            .arg(failures_ == 0 ? QStringLiteral("pass")
                                : QStringLiteral("fail")));
  QCoreApplication::exit(failures_ == 0 ? 0 : 1);
}

}  // namespace lapian::app
