#include "app/OnlineBufferTestRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "app/MainWindow.h"
#include "app/OnlineSession.h"
#include "online/DownloadTransport.h"

namespace lapian::app {
namespace {

void Print(const QString& text) {
  std::fprintf(stderr, "%s\n", text.toUtf8().constData());
  std::fflush(stderr);
}

// 前 600KB 正常返回；下一次请求先停 3s（可取消），再把余量补齐。
class StallingTransport final : public lapian::online::IDownloadTransport {
 public:
  explicit StallingTransport(QString path) : path_(std::move(path)) {}

  lapian::online::DownloadTransportResult GetRange(
      const lapian::online::DownloadRequest& request,
      lapian::online::IDownloadSink* sink,
      const lapian::online::HttpCancelToken& cancel) override {
    lapian::online::DownloadTransportResult result;
    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly)) {
      result.status = lapian::online::DownloadTransportStatus::kNetwork;
      result.error = QStringLiteral("buffer fake transport 无法读取源文件");
      return result;
    }
    const qint64 total = file.size();
    if (request.expected_total > 0 && request.expected_total != total) {
      result.status = lapian::online::DownloadTransportStatus::kTotalMismatch;
      result.total_mismatch = true;
      return result;
    }
    file.seek(request.offset);
    result.ok = true;
    result.http_status = request.offset == 0 ? 200 : 206;
    result.content_range_start = request.offset;
    result.content_range_total = total;
    result.bytes_written = 0;

    auto write_bytes = [&](qint64 count) -> bool {
      while (count > 0 && !cancel.IsCancelled()) {
        const qint64 chunk = std::min<qint64>(count, 128 * 1024);
        const QByteArray data = file.read(chunk);
        if (data.isEmpty()) {
          break;
        }
        if (!sink->Write(
                reinterpret_cast<const std::uint8_t*>(data.constData()),
                static_cast<std::size_t>(data.size()), &result.error)) {
          result.ok = false;
          result.status = lapian::online::DownloadTransportStatus::kSinkError;
          return false;
        }
        result.bytes_written += data.size();
        count -= data.size();
      }
      return true;
    };

    if (request.offset == 0) {
      if (!write_bytes(1536 * 1024)) {
        return result;
      }
      return result;
    }

    if (!stalled_.exchange(true)) {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(8);
      while (std::chrono::steady_clock::now() < deadline) {
        if (cancel.IsCancelled()) {
          result.ok = false;
          result.cancelled = true;
          result.status = lapian::online::DownloadTransportStatus::kCancelled;
          return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    }
    if (!write_bytes(total - request.offset)) {
      return result;
    }
    return result;
  }

 private:
  QString path_;
  std::atomic<bool> stalled_{false};
};

}  // namespace

OnlineBufferTestRunner::OnlineBufferTestRunner(
    MainWindow* window, OnlineBufferTestOptions options)
    : window_(window), options_(std::move(options)) {}

void OnlineBufferTestRunner::Start() {
  QTimer::singleShot(0, window_, [this]() { Run(); });
}

void OnlineBufferTestRunner::Run() {
  auto check = [this](const char* name, bool ok, const QString& detail = {}) {
    ++checks_;
    if (!ok) {
      ++failures_;
    }
    Print(QStringLiteral("online_buffer_check %1 ok=%2%3")
              .arg(QString::fromUtf8(name))
              .arg(ok ? 1 : 0)
              .arg(detail.isEmpty() ? QString()
                                    : QStringLiteral(" detail=") + detail));
  };
  const QFileInfo media(options_.media_path);
  if (!media.exists() || !media.isFile()) {
    Print(QStringLiteral("online_buffer_fail reason=missing_media path=%1")
              .arg(options_.media_path));
    QCoreApplication::exit(2);
    return;
  }
  const QString root =
      QDir::currentPath() + QStringLiteral("/build/t8.6-buffer-cache");
  QDir(root).removeRecursively();
  QDir().mkpath(root);
  window_->SetOnlineCacheRootForTest(root);

  lapian::online::PlayUrlData playurl;
  playurl.quality = 64;
  playurl.codecid = 7;
  playurl.format = QStringLiteral("mp4");
  playurl.timelength_ms = 30000;
  lapian::online::DurlSegment segment;
  segment.url = QStringLiteral("https://example.invalid/buffer.mp4");
  segment.size = media.size();
  segment.length_ms = 30000;
  playurl.durls.push_back(segment);
  window_->SetOnlinePlayUrlForTest(playurl);
  window_->SetOnlineTransportForTest(
      std::make_unique<StallingTransport>(media.absoluteFilePath()));
  window_->SetOnlineReadyThresholdForTest(300 * 1024);

  OnlineRequest request;
  request.bvid = QStringLiteral("BV1BMnv6jE5S");
  request.cid = 42915922022;
  request.page = 1;
  request.title = QStringLiteral("T8.6 buffer test");
  request.duration_ms = 30000;
  const bool ready = window_->StartOnlineSession(request) &&
                     window_->WaitForOnlineReadyForTest(options_.timeout_ms);
  check("ready", ready, window_->OnlineLastErrorForTest());
  if (!ready) {
    Print(QStringLiteral("online_buffer_result checks=%1 failures=%2 verdict=fail")
              .arg(checks_)
              .arg(failures_));
    QCoreApplication::exit(2);
    return;
  }
  check("default_paused", !window_->IsPlayingForTest());

  window_->FocusVideoForTest();
  window_->PressSpaceKeyForTest();
  const auto wait_play = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - wait_play <
             std::chrono::seconds(2) &&
         !window_->IsPlayingForTest()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  check("playing", window_->IsPlayingForTest());

  // 600KB 缓冲在 3s 停流期间耗尽；PlaybackController 应自动暂停并提示。
  bool buffering = false;
  const qint64 frame_before_buffer = window_->CurrentFrameForTest();
  qint64 frame_at_buffer = frame_before_buffer;
  Print(QStringLiteral("online_buffer_after_play frame=%1 %2")
            .arg(frame_before_buffer)
            .arg(window_->OnlineGateDebugForTest()));
  QElapsedTimer debug_timer;
  debug_timer.start();
  const auto wait_buffer = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - wait_buffer <
             std::chrono::seconds(12) &&
         !buffering) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    buffering = window_->IsOnlineBufferingForTest();
    if (buffering) {
      frame_at_buffer = window_->CurrentFrameForTest();
    }
    if (debug_timer.elapsed() >= 1000) {
      debug_timer.restart();
      Print(QStringLiteral("online_buffer_wait frame=%1 %2")
                .arg(window_->CurrentFrameForTest())
                .arg(window_->OnlineGateDebugForTest()));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  check("buffer_exhausted", buffering,
        QStringLiteral("frame=%1").arg(frame_at_buffer));
  check("buffer_message",
        window_->StatusMessageForTest().contains(QStringLiteral("缓冲中")),
        window_->StatusMessageForTest());
  Print(QStringLiteral("online_buffer_exhausted frame=%1 state=%2")
            .arg(frame_at_buffer)
            .arg(window_->OnlineStateForTest()));

  // 3s 后余量补齐；从当前帧自动恢复，不黑屏/不跳帧。
  bool resumed = false;
  const auto wait_resume = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - wait_resume <
             std::chrono::seconds(16) &&
         !resumed) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    resumed = !window_->IsOnlineBufferingForTest();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const qint64 frame_at_resume = window_->CurrentFrameForTest();
  check("buffer_resumed", resumed,
        QStringLiteral("frame=%1").arg(frame_at_resume));
  check("no_frame_jump",
        frame_at_resume >= frame_at_buffer &&
            frame_at_resume <= frame_at_buffer + 60,
        QStringLiteral("at_buffer=%1 at_resume=%2")
            .arg(frame_at_buffer)
            .arg(frame_at_resume));
  QElapsedTimer after_resume;
  after_resume.start();
  while (after_resume.elapsed() < 1000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  check("resume_advances", window_->CurrentFrameForTest() > frame_at_resume,
        QStringLiteral("from=%1 to=%2")
            .arg(frame_at_resume)
            .arg(window_->CurrentFrameForTest()));

  QElapsedTimer close_timer;
  close_timer.start();
  const bool closed = window_->CloseForTest();
  check("close", closed);
  check("close_le_1000ms", close_timer.elapsed() <= 1000,
        QStringLiteral("ms=%1").arg(close_timer.elapsed()));
  Print(QStringLiteral("online_buffer_result checks=%1 failures=%2 verdict=%3")
            .arg(checks_)
            .arg(failures_)
            .arg(failures_ == 0 ? QStringLiteral("pass")
                                : QStringLiteral("fail")));
  QCoreApplication::exit(failures_ == 0 ? 0 : 1);
}

}  // namespace lapian::app
