#include "app/MemoryTestRunner.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QSize>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

#include "app/MainWindow.h"
#include "app/ProcessMemory.h"

namespace lapian::app {
namespace {

constexpr std::size_t kMiB = 1024 * 1024;
constexpr double kBytesPerMiB = 1024.0 * 1024.0;

std::string Utf8(const QString& text) { return text.toUtf8().toStdString(); }

}  // namespace

MemoryTestRunner::MemoryTestRunner(MainWindow* window, QString mode, QString tag,
                                   int seconds, int steps, int switch_cycles,
                                   QString file_a, QString file_b, QObject* parent)
    : QObject(parent == nullptr ? window : parent),
      window_(window),
      mode_text_(std::move(mode)),
      tag_(std::move(tag)),
      seconds_(seconds > 0 ? seconds : 60),
      steps_(steps > 0 ? steps : 40),
      switch_cycles_(switch_cycles > 0 ? switch_cycles : 3),
      file_a_(std::move(file_a)),
      file_b_(std::move(file_b)) {
  if (!ParseMode(mode_text_)) {
    mode_ = Mode::kPause;
  }
}

void MemoryTestRunner::Start() {
  QTimer::singleShot(0, this, &MemoryTestRunner::Begin);
}

bool MemoryTestRunner::ParseMode(const QString& text) {
  if (text == QStringLiteral("empty")) {
    mode_ = Mode::kEmpty;
    return true;
  }
  if (text == QStringLiteral("pause")) {
    mode_ = Mode::kPause;
    return true;
  }
  if (text == QStringLiteral("play")) {
    mode_ = Mode::kPlay;
    return true;
  }
  if (text == QStringLiteral("step")) {
    mode_ = Mode::kStep;
    return true;
  }
  if (text == QStringLiteral("switch")) {
    mode_ = Mode::kSwitch;
    return true;
  }
  return false;
}

void MemoryTestRunner::Begin() {
  // 打开失败会弹模态框；测量模式自动关掉，避免挂死。
  modal_watchdog_ = new QTimer(this);
  modal_watchdog_->setInterval(250);
  connect(modal_watchdog_, &QTimer::timeout, this, []() {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (box != nullptr) {
      box->done(QMessageBox::Cancel);
    }
  });
  modal_watchdog_->start();

  std::fprintf(stderr, "T7.2 memory_begin mode=%s tag=%s seconds=%d\n",
               mode_text_.toUtf8().constData(), tag_.toUtf8().constData(), seconds_);
  std::fflush(stderr);

  if (mode_ == Mode::kEmpty) {
    phase_ = QStringLiteral("empty");
    phase_timer_.start();
    Sample(QStringLiteral("start"));
    ScheduleMilestones(seconds_, [this]() { SummaryAndExit(); });
    return;
  }
  if (mode_ == Mode::kSwitch && file_b_.isEmpty()) {
    Fail(QStringLiteral("switch 模式需要 --file-b"));
    return;
  }
  if (file_a_.isEmpty()) {
    Fail(QStringLiteral("缺少 --file"));
    return;
  }
  phase_ = QStringLiteral("a");
  OpenAndWait(file_a_, QStringLiteral("a"), [this]() { OnReady(); });
}

void MemoryTestRunner::OpenAndWait(const QString& path, const QString& phase,
                                   std::function<void()> next) {
  phase_ = phase;
  open_poll_count_ = 0;
  const QString expected_name = QFileInfo(path).fileName();
  window_->OpenFile(path);
  PollOpen(expected_name, std::move(next));
}

void MemoryTestRunner::PollOpen(const QString& expected_name,
                                std::function<void()> next) {
  if (failed_) {
    return;
  }
  if (!window_->HasMediaForTest() ||
      window_->InfoFileNameForTest() != expected_name) {
    if (++open_poll_count_ > 100) {
      Fail(QStringLiteral("打开失败或超时：") + expected_name);
      return;
    }
    QTimer::singleShot(50, this, [this, expected_name, next]() {
      PollOpen(expected_name, next);
    });
    return;
  }
  if (!window_->IndexReadyForTest()) {
    if (++open_poll_count_ > 300) {
      Fail(QStringLiteral("索引超时：") + expected_name);
      return;
    }
    QTimer::singleShot(50, this, [this, expected_name, next]() {
      PollOpen(expected_name, next);
    });
    return;
  }
  phase_timer_.restart();
  Sample(phase_ + QStringLiteral("-steady"));
  next();
}

void MemoryTestRunner::OnReady() {
  phase_timer_.restart();
  Sample(QStringLiteral("steady-start"));

  if (mode_ == Mode::kPlay) {
    window_->StartPlaybackForTest();
    Sample(QStringLiteral("play-start"));
    ScheduleMilestones(seconds_, [this]() { SummaryAndExit(); });
    return;
  }

  if (mode_ == Mode::kStep) {
    const qint64 last = window_->LastFrameIndexForTest();
    const qint64 start = std::min<qint64>(300, std::max<qint64>(0, last));
    window_->GoToInputForTest(QString::number(start));
    Sample(QStringLiteral("step-start"));
    RunSteps(true, steps_, [this]() {
      Sample(QStringLiteral("after-backward"));
      RunSteps(false, steps_, [this]() {
        Sample(QStringLiteral("after-forward"));
        SummaryAndExit();
      });
    });
    return;
  }

  if (mode_ == Mode::kSwitch) {
    auto cycle = std::make_shared<std::function<void()>>();
    *cycle = [this, cycle]() {
      // 一个“cycle”= A→B→A 两次打开；cycles=3 时共 6 次打开，最终回到 A。
      if (switch_done_ >= switch_cycles_ * 2) {
        Sample(QStringLiteral("switch-final"));
        SummaryAndExit();
        return;
      }
      ++switch_done_;
      const bool to_b = (switch_done_ % 2 == 1);
      const QString phase =
          (to_b ? QStringLiteral("b-") : QStringLiteral("a-")) +
          QString::number(switch_done_);
      OpenAndWait(to_b ? file_b_ : file_a_, phase, [cycle]() { (*cycle)(); });
    };
    (*cycle)();
    return;
  }

  // pause：索引就绪后保持暂停，观察 60s 稳态。
  ScheduleMilestones(seconds_, [this]() { SummaryAndExit(); });
}

void MemoryTestRunner::ScheduleMilestones(int seconds, std::function<void()> done) {
  const int milestones[] = {5, 10, 20, 30, 45, 60};
  for (int at : milestones) {
    if (at >= seconds) {
      break;
    }
    QTimer::singleShot(at * 1000, this, [this, at]() {
      Sample(QStringLiteral("t%1").arg(at));
    });
  }
  QTimer::singleShot(seconds * 1000, this, [this, done]() {
    if (window_->IsPlayingForTest()) {
      window_->TogglePlaybackForTest();
    }
    Sample(QStringLiteral("final"));
    done();
  });
}

void MemoryTestRunner::Sample(const QString& phase) {
  ProcessMemorySample sample;
  if (!SampleProcessMemory(&sample) || !sample.valid) {
    std::fprintf(stderr, "T7.2 memory phase=%s invalid\n",
                 phase.toUtf8().constData());
    std::fflush(stderr);
    return;
  }
  max_ws_bytes_ = std::max(max_ws_bytes_, sample.working_set_bytes);
  max_private_bytes_ = std::max(max_private_bytes_, sample.private_bytes);
  last_ws_bytes_ = sample.working_set_bytes;
  last_private_bytes_ = sample.private_bytes;
  const double t_s = phase_timer_.isValid()
                         ? static_cast<double>(phase_timer_.elapsed()) / 1000.0
                         : 0.0;
  const std::string cache = Utf8(window_->FrameCacheStateForTest());
  std::fprintf(stderr,
               "T7.2 memory mode=%s tag=%s phase=%s t_s=%.1f ws_mb=%.1f "
               "private_mb=%.1f peak_ws_mb=%.1f handles=%u playing=%d cache=%s\n",
               mode_text_.toUtf8().constData(), tag_.toUtf8().constData(),
               phase.toUtf8().constData(), t_s,
               static_cast<double>(sample.working_set_bytes) / kBytesPerMiB,
               static_cast<double>(sample.private_bytes) / kBytesPerMiB,
               static_cast<double>(sample.peak_working_set_bytes) / kBytesPerMiB,
               sample.handle_count, window_->IsPlayingForTest() ? 1 : 0,
               cache.c_str());
  std::fflush(stderr);
}

void MemoryTestRunner::RunSteps(bool backward, int count, std::function<void()> done) {
  window_->ClearFocusForTest();
  window_->FocusVideoForTest();
  auto remaining = std::make_shared<int>(count);
  auto samples = std::make_shared<std::vector<double>>();
  samples->reserve(static_cast<std::size_t>(count));
  auto tick = std::make_shared<std::function<void()>>();
  *tick = [this, backward, remaining, samples, tick, done, count]() {
    if (*remaining <= 0) {
      std::vector<double> sorted = *samples;
      std::sort(sorted.begin(), sorted.end());
      const double p50 = sorted.empty() ? 0.0 : sorted[sorted.size() / 2];
      const std::size_t p95_index =
          sorted.empty() ? 0 : static_cast<std::size_t>(
                                     static_cast<double>(sorted.size() - 1) * 0.95);
      const double p95 = sorted.empty() ? 0.0 : sorted[p95_index];
      const double max_ms = sorted.empty() ? 0.0 : sorted.back();
      std::fprintf(stderr,
                   "T7.2 step_stats direction=%s n=%zu p50_ms=%.2f p95_ms=%.2f "
                   "max_ms=%.2f cache=%s\n",
                   backward ? "backward" : "forward", sorted.size(), p50, p95,
                   max_ms, Utf8(window_->FrameCacheStateForTest()).c_str());
      std::fflush(stderr);
      done();
      return;
    }
    QElapsedTimer timer;
    timer.start();
    window_->PressKeyForTest(backward ? Qt::Key_PageUp : Qt::Key_PageDown, 0);
    samples->push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
    --(*remaining);
    const int done_steps = count - *remaining;
    if (done_steps % 20 == 0) {
      Sample(QStringLiteral("step-%1").arg(done_steps));
    }
    QTimer::singleShot(0, this, [tick]() { (*tick)(); });
  };
  (*tick)();
}

int MemoryTestRunner::LimitMiBForMode() const {
  const QSize display = window_->SnapshotDisplaySizeForTest();
  const bool is_4k = display.width() >= 3840 || display.height() >= 2160;
  if (mode_ == Mode::kEmpty) {
    return 250;  // P10 空态基线
  }
  if (mode_ == Mode::kPause) {
    // P10 的口径是 V1（1080p）打开暂停；4K 暂停不是 P10 断言。
    return is_4k ? 0 : 250;
  }
  return is_4k ? 1536 : 800;
}

void MemoryTestRunner::SummaryAndExit() {
  const int limit_mb = LimitMiBForMode();
  const bool pass =
      limit_mb <= 0 || max_ws_bytes_ <= static_cast<std::uint64_t>(limit_mb) * kMiB;
  const QSize display = window_->SnapshotDisplaySizeForTest();
  std::fprintf(stderr,
               "T7.2 memory_summary mode=%s tag=%s display=%dx%d limit_mb=%d "
               "pass=%d max_ws_mb=%.1f max_private_mb=%.1f final_ws_mb=%.1f "
               "final_private_mb=%.1f cache=%s\n",
               mode_text_.toUtf8().constData(), tag_.toUtf8().constData(),
               display.width(), display.height(), limit_mb, pass ? 1 : 0,
               static_cast<double>(max_ws_bytes_) / kBytesPerMiB,
               static_cast<double>(max_private_bytes_) / kBytesPerMiB,
               static_cast<double>(last_ws_bytes_) / kBytesPerMiB,
               static_cast<double>(last_private_bytes_) / kBytesPerMiB,
               Utf8(window_->FrameCacheStateForTest()).c_str());
  std::fflush(stderr);
  if (modal_watchdog_ != nullptr) {
    modal_watchdog_->stop();
  }
  QCoreApplication::exit(0);
}

void MemoryTestRunner::Fail(const QString& reason) {
  failed_ = true;
  std::fprintf(stderr, "T7.2 memory_fail mode=%s tag=%s reason=%s\n",
               mode_text_.toUtf8().constData(), tag_.toUtf8().constData(),
               reason.toUtf8().constData());
  std::fflush(stderr);
  if (modal_watchdog_ != nullptr) {
    modal_watchdog_->stop();
  }
  QCoreApplication::exit(2);
}

}  // namespace lapian::app
