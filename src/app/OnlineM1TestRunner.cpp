#include "app/OnlineM1TestRunner.h"

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

#include "app/MainWindow.h"
#include "app/OnlineResolveDialog.h"

namespace lapian::app {
namespace {

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
  window_->FocusVideoForTest();
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
  Print(QStringLiteral("online_m1_first_frame ms=%1 state=%2")
            .arg(first_frame_ms)
            .arg(window_->OnlineStateForTest()));
  const bool cache_hit = window_->OnlineCacheHitForTest();
  Print(QStringLiteral("online_m1_cache_hit cache_hit=%1 path=%2")
            .arg(cache_hit ? 1 : 0)
            .arg(window_->OnlineCachePathForTest()));
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
    for (int i = 0; i < 20 && range > 1; ++i) {
      const qint64 target = 5 + (static_cast<qint64>(i) * 37) % (range - 4);
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
  if (!cache_hit && container_frames > 120) {
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
