// T8.6 在线媒体层单测（无 GUI、无网络）：
// - 0.3MB 发布前缀上 OpenGrowing/ProbeFormat/首帧；
// - 部分索引只覆盖已发布前缀，帧号/落点与完整文件一致；
// - 已缓存精确 seek；
// - 未缓存 seek 阻塞时可被单读者 interrupt 中断（≤100ms）；
// - GrowingSource Cancel/Fail 不会把未完成区伪装成 EOF。
//
// 素材优先用 build/test-media/faststart-720p.mp4（T8.4 生成），其次用
// .deps 的 T8.5 OB-1 缓存，最后用 media/V9.mp4。缺失时返回 77（SKIP）。

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "app/OnlineSession.h"
#include "media/MediaSession.h"
#include "media/OnlineIndexBuilder.h"
#include "online/GrowingSource.h"

namespace {

int failures = 0;
int checks = 0;

void Check(bool ok, const char* name, const std::string& detail = {}) {
  ++checks;
  if (!ok) {
    ++failures;
  }
  std::fprintf(stderr, "online_m1_check %s ok=%d%s%s\n", name, ok ? 1 : 0,
               detail.empty() ? "" : " detail=", detail.c_str());
  std::fflush(stderr);
}

QString FindMedia() {
  const QString root = QDir::currentPath();
  const QStringList candidates = {
      root + QStringLiteral("/build/test-media/faststart-720p.mp4"),
      root + QStringLiteral(
                 "/.deps/tmp/fetch-ob1/BV1BMnv6jE5S/42915922022/64-7/data.mp4"),
      root + QStringLiteral("/media/V9.mp4")};
  for (const QString& candidate : candidates) {
    const QFileInfo info(candidate);
    if (info.exists() && info.isFile() && info.size() > 0) {
      return info.absoluteFilePath();
    }
  }
  return QString();
}

// 离线 fake transport：从本地文件按 Range 顺序读给 DownloadJob，不联网。
class FileTransport final : public lapian::online::IDownloadTransport {
 public:
  explicit FileTransport(QString path) : path_(std::move(path)) {}

  lapian::online::DownloadTransportResult GetRange(
      const lapian::online::DownloadRequest& request,
      lapian::online::IDownloadSink* sink,
      const lapian::online::HttpCancelToken& cancel) override {
    lapian::online::DownloadTransportResult result;
    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly)) {
      result.status = lapian::online::DownloadTransportStatus::kNetwork;
      result.error = QStringLiteral("fake transport 无法读取源文件");
      return result;
    }
    if (!file.seek(request.offset)) {
      result.status = lapian::online::DownloadTransportStatus::kSinkError;
      result.error = QStringLiteral("fake transport seek 失败");
      return result;
    }
    const std::int64_t total = file.size();
    if (request.expected_total > 0 && request.expected_total != total) {
      result.status = lapian::online::DownloadTransportStatus::kTotalMismatch;
      result.total_mismatch = true;
      return result;
    }
    result.ok = true;
    result.http_status = request.offset == 0 ? 200 : 206;
    result.content_range_start = request.offset;
    result.content_range_total = total;
    const QByteArray chunk = file.read(256 * 1024);
    if (!chunk.isEmpty()) {
      if (!sink->Write(reinterpret_cast<const std::uint8_t*>(chunk.constData()),
                       static_cast<std::size_t>(chunk.size()),
                       &result.error)) {
        result.ok = false;
        result.status = lapian::online::DownloadTransportStatus::kSinkError;
        return result;
      }
      result.bytes_written = chunk.size();
    }
    if (cancel.IsCancelled()) {
      result.ok = false;
      result.cancelled = true;
      result.status = lapian::online::DownloadTransportStatus::kCancelled;
    }
    return result;
  }

 private:
  QString path_;
};

class FailingTransport final : public lapian::online::IDownloadTransport {
 public:
  lapian::online::DownloadTransportResult GetRange(
      const lapian::online::DownloadRequest&,
      lapian::online::IDownloadSink*,
      const lapian::online::HttpCancelToken&) override {
    lapian::online::DownloadTransportResult result;
    result.status = lapian::online::DownloadTransportStatus::kTotalMismatch;
    result.total_mismatch = true;
    result.error = QStringLiteral("fake total mismatch");
    return result;
  }
};

std::int64_t HalfFrameUs(const lapian::media::MediaInfo& info) {
  const double fps = info.video.frame_rate.ToDouble();
  return fps > 0.0 ? static_cast<std::int64_t>(500000.0 / fps) : 20000;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("拉片软件"));
  QCoreApplication::setApplicationName(QStringLiteral("拉片软件-test"));
  const QString media = FindMedia();
  if (media.isEmpty()) {
    std::fprintf(stderr, "online_m1_skip reason=missing_media\n");
    return 77;
  }
  const QByteArray native = QDir::toNativeSeparators(media).toUtf8();
  const std::string path(native.constData(), static_cast<std::size_t>(native.size()));
  const qint64 total = QFileInfo(media).size();
  const qint64 threshold = std::min<qint64>(300 * 1024, total);
  std::fprintf(stderr, "online_m1_media path=%s total=%lld threshold=%lld\n",
               path.c_str(), static_cast<long long>(total),
               static_cast<long long>(threshold));

  // ---------------------------------------------------------- 0.3MB 首帧
  auto source = std::make_shared<lapian::online::GrowingSource>(total);
  source->Publish(threshold);
  lapian::media::MediaSession session;
  std::string error;
  const auto open_started = std::chrono::steady_clock::now();
  const bool opened = session.OpenGrowing(source, path, "BV1TEST-online.mp4",
                                          nullptr, &error, false);
  const double open_ms = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - open_started)
                             .count();
  Check(opened, "open_growing_03mb", error);
  if (!opened) {
    return 1;
  }
  Check(session.IsGrowing(), "is_growing");
  Check(session.first_frame().av_frame != nullptr, "first_frame_present");
  Check(session.info().video.width > 0 && session.info().video.height > 0,
        "first_frame_dimensions");
  Check(session.last_open_timings().probe_ms >= 0.0 &&
            session.last_open_timings().first_frame_ms >= 0.0,
        "open_timings");
  std::fprintf(stderr,
               "online_m1_first_frame open_ms=%.2f probe_ms=%.2f decoder_ms=%.2f "
               "frame_ms=%.2f %dx%d\n",
               open_ms, session.last_open_timings().probe_ms,
               session.last_open_timings().decoder_open_ms,
               session.last_open_timings().first_frame_ms,
               session.info().video.width, session.info().video.height);

  // ---------------------------------------------------------- 部分索引
  auto prefix = lapian::media::BuildOnlineIndex(source, path,
                                                session.video_stream_index(), 0);
  Check(prefix.ok, "partial_index_ok", prefix.error);
  if (!prefix.ok) {
    return 1;
  }
  Check(prefix.index.size() > 0, "partial_index_frames");
  Check(prefix.spans.size() == prefix.index.size(), "partial_spans_aligned");
  Check(prefix.stable_frames == static_cast<int>(prefix.index.size()),
        "partial_stable_all_when_guard0");
  const std::size_t prefix_frames = prefix.index.size();
  std::fprintf(stderr, "online_m1_index frames=%zu complete=%d timed_out=%d\n",
               prefix_frames, prefix.complete ? 1 : 0,
               prefix.timed_out ? 1 : 0);

  // 前沿之外不可读：保持首帧画面，CanDecodeAhead 在低余量时为 false。
  Check(!source->IsCancelled() && !source->HasFailed(), "source_alive_after_index");
  Check(!session.CanDecodeAhead(256 * 1024), "decode_gate_low_at_frontier");

  // ---------------------------------------------------------- 完整下载 + 精确 seek
  source->Publish(total);
  Check(session.GrowingComplete(), "source_complete");
  Check(session.CanDecodeAhead(256 * 1024), "decode_gate_open_after_complete");

  auto full = lapian::media::BuildOnlineIndex(source, path,
                                               session.video_stream_index(), 0);
  Check(full.ok, "full_index_ok", full.error);
  if (!full.ok || full.index.size() <= prefix_frames) {
    Check(false, "full_index_larger_than_prefix");
    return 1;
  }
  Check(full.index.size() > prefix_frames, "full_index_larger_than_prefix");
  session.InstallOnlineIndex(std::move(full.index), full.build_ms);
  Check(session.HasIndex(), "index_installed");

  const std::int64_t target = std::min<std::int64_t>(
      120, static_cast<std::int64_t>(session.index().size()) - 1);
  lapian::media::DecodedFrameInfo seek_info;
  std::string seek_error;
  const bool seek_ok =
      session.SeekToFrame(target, &seek_info, &seek_error,
                          lapian::media::FrameCacheMode::kSeekHandoff);
  Check(seek_ok, "cached_seek_ok", seek_error);
  if (seek_ok) {
    Check(seek_info.pts_us == session.index().PtsOfFrame(target),
          "cached_seek_exact_pts");
    Check(std::llabs(seek_info.pts_us - session.index().PtsOfFrame(target)) <=
              HalfFrameUs(session.info()),
          "cached_seek_within_half_frame");
  }

  // ---------------------------------------------------------- 未缓存 seek 中断
  auto blocked_source = std::make_shared<lapian::online::GrowingSource>(total);
  blocked_source->Publish(threshold);
  lapian::media::MediaSession blocked;
  std::string blocked_error;
  const bool blocked_opened =
      blocked.OpenGrowing(blocked_source, path, "BV1TEST-blocked.mp4", nullptr,
                         &blocked_error, false);
  Check(blocked_opened, "blocked_session_open", blocked_error);
  if (!blocked_opened) {
    return 1;
  }

  std::atomic<bool> started{false};
  std::thread seek_thread([&]() {
    started.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    lapian::media::DecodedFrameInfo info;
    std::string error;
    // 60s 远大于已发布前缀；读会在 frontier 阻塞。
    blocked.SeekToEstimatedPts(60'000'000, 1800, &info, &error);
  });
  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  const auto interrupt_started = std::chrono::steady_clock::now();
  blocked.InterruptGrowingReads();
  seek_thread.join();
  const double interrupt_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - interrupt_started)
          .count();
  std::fprintf(stderr, "online_m1_interrupt join_ms=%.2f\n", interrupt_ms);
  Check(blocked.GrowingInterrupted(), "blocked_session_interrupted");
  Check(interrupt_ms <= 100.0, "interrupt_within_100ms");
  blocked.Close();
  Check(blocked_source->Stats().live_cursors == 0, "interrupt_cursor_released");

  session.Close();
  Check(source->Stats().live_cursors == 0, "session_cursor_released");

  // ---------------------------------------------------------- OnlineSession + finalize
  const QString root =
      QDir::currentPath() + QStringLiteral("/build/t8.6-online-session");
  QDir(root).removeRecursively();
  QDir().mkpath(root);
  lapian::online::CacheStore cache(root, 2LL * 1024 * 1024 * 1024);
  lapian::app::OnlineSession online(&cache);
  lapian::online::PlayUrlData playurl;
  playurl.quality = 64;
  playurl.codecid = 7;
  playurl.format = QStringLiteral("mp4");
  playurl.timelength_ms = 110000;
  lapian::online::DurlSegment segment;
  segment.url = QStringLiteral("https://example.invalid/fake.mp4");
  segment.size = total;
  segment.length_ms = 110000;
  playurl.durls.push_back(segment);
  online.SetPlayUrlDataForTest(playurl);
  online.SetDownloadTransportForTest(std::make_unique<FileTransport>(media));
  online.SetReadyThresholdForTest(300 * 1024);

  bool ready = false;
  bool failed = false;
  QString online_error;
  std::shared_ptr<lapian::online::GrowingSource> online_source;
  QString online_part;
  std::unique_ptr<lapian::online::GrowingCursor> reader;
  online.SetCallbacks(
      [](lapian::app::OnlineState, const QString&) {},
      [](const lapian::app::OnlineProgress&) {},
      [&](const lapian::app::OnlineReadyInfo& info) {
        ready = true;
        online_source = info.source;
        online_part = info.part_path;
        QString cursor_error;
        reader = lapian::online::GrowingCursor::Open(info.source, info.part_path,
                                                     &cursor_error);
        if (!reader) {
          failed = true;
          online_error = cursor_error;
        }
      },
      [&](const QString& message) {
        failed = true;
        online_error = message;
      });

  lapian::app::OnlineRequest request;
  request.bvid = QStringLiteral("BV1BMnv6jE5S");
  request.cid = 42915922022;
  request.page = 1;
  request.title = QStringLiteral("T8.6 fake online");
  request.owner = QStringLiteral("test");
  request.duration_ms = 110000;
  QString start_error;
  const bool online_started = online.Start(request, &start_error);
  Check(online_started, "online_session_start", start_error.toStdString());
  const auto wait_started = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - wait_started <
             std::chrono::seconds(5) &&
         !ready && !failed) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  Check(ready, "online_session_ready", online_error.toStdString());
  Check(reader != nullptr, "online_fake_reader_open");
  Check(!online_part.isEmpty() && QFileInfo::exists(online_part),
        "online_part_exists");
  if (online_source) {
    Check(online_source->Published() >= 300 * 1024,
          "online_published_threshold");
  }

  bool waiting_finalize = false;
  const auto finalize_wait_start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - finalize_wait_start <
             std::chrono::seconds(10) &&
         !waiting_finalize) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    waiting_finalize = online.HasPendingFinalize();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  Check(waiting_finalize, "online_awaits_finalize");
  Check(QFileInfo::exists(online_part), "part_retained_before_finalize");
  if (!online_part.isEmpty()) {
    const QString mp4 = online_part.left(online_part.size() - 5) + ".mp4";
    Check(!QFileInfo::exists(mp4), "mp4_not_renamed_while_reader_open");
  }

  // reader 关闭后才 finalize；内部走 CacheStore::MarkComplete。
  if (reader) {
    reader->Close();
  }
  QString finalize_error;
  const bool finalized = online.FinalizeAfterReadersStopped(&finalize_error);
  Check(finalized, "online_finalize_after_reader_close",
        finalize_error.toStdString());
  if (finalized && !online_part.isEmpty()) {
    const QString mp4 = online_part.left(online_part.size() - 5) + ".mp4";
    Check(QFileInfo::exists(mp4), "mp4_after_finalize");
    Check(!QFileInfo::exists(online_part), "part_removed_after_finalize");
  }
  const auto entry = cache.Load(
      lapian::online::CacheKey{QStringLiteral("BV1BMnv6jE5S"), 42915922022, 64, 7},
      nullptr);
  Check(entry.has_value() && entry->complete, "cache_meta_complete");
  online.Close();

  // 产品关窗顺序：先 Cancel（唤醒 reader/下载），停 reader 后再 Finalize。
  const QString root2 =
      QDir::currentPath() + QStringLiteral("/build/t8.6-online-session-cancel");
  QDir(root2).removeRecursively();
  QDir().mkpath(root2);
  lapian::online::CacheStore cache2(root2, 2LL * 1024 * 1024 * 1024);
  lapian::app::OnlineSession online2(&cache2);
  online2.SetPlayUrlDataForTest(playurl);
  online2.SetDownloadTransportForTest(std::make_unique<FileTransport>(media));
  online2.SetReadyThresholdForTest(300 * 1024);
  bool ready2 = false;
  QString part2;
  std::unique_ptr<lapian::online::GrowingCursor> reader2;
  online2.SetCallbacks(
      [](lapian::app::OnlineState, const QString&) {},
      [](const lapian::app::OnlineProgress&) {},
      [&](const lapian::app::OnlineReadyInfo& info) {
        ready2 = true;
        part2 = info.part_path;
        QString cursor_error;
        reader2 = lapian::online::GrowingCursor::Open(info.source, info.part_path,
                                                      &cursor_error);
      },
      [](const QString&) {});
  const bool started2 = online2.Start(request, &start_error);
  Check(started2, "online2_start");
  const auto wait2 = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - wait2 < std::chrono::seconds(5) &&
         !ready2) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  Check(ready2, "online2_ready");
  bool pending2 = false;
  const auto wait2b = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - wait2b < std::chrono::seconds(10) &&
         !pending2) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    pending2 = online2.HasPendingFinalize();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  Check(pending2, "online2_awaits_finalize");
  online2.Cancel();
  Check(online2.state() == lapian::app::OnlineState::kClosed,
        "cancel_marks_closed");
  if (reader2) {
    reader2->Close();
  }
  QString finalize2_error;
  Check(online2.FinalizeAfterReadersStopped(&finalize2_error),
        "finalize_after_cancel", finalize2_error.toStdString());
  if (!part2.isEmpty()) {
    const QString mp4 = part2.left(part2.size() - 5) + ".mp4";
    Check(QFileInfo::exists(mp4), "cancel_finalize_mp4");
  }
  online2.Close();

  // 错误路径：下载不可恢复失败 → 可读 error，≤5s；.part 保留；不崩。
  const QString root3 =
      QDir::currentPath() + QStringLiteral("/build/t8.6-online-session-error");
  QDir(root3).removeRecursively();
  QDir().mkpath(root3);
  lapian::online::CacheStore cache3(root3, 2LL * 1024 * 1024 * 1024);
  lapian::app::OnlineSession online3(&cache3);
  online3.SetPlayUrlDataForTest(playurl);
  online3.SetDownloadTransportForTest(std::make_unique<FailingTransport>());
  bool error3 = false;
  QString error3_text;
  const auto error_start = std::chrono::steady_clock::now();
  online3.SetCallbacks(
      [](lapian::app::OnlineState, const QString&) {},
      [](const lapian::app::OnlineProgress&) {},
      [](const lapian::app::OnlineReadyInfo&) {},
      [&](const QString& message) {
        error3 = true;
        error3_text = message;
      });
  QString start3_error;
  Check(online3.Start(request, &start3_error), "error_session_start",
        start3_error.toStdString());
  while (!error3 && std::chrono::steady_clock::now() - error_start <
                        std::chrono::seconds(5)) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  const qint64 error_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - error_start)
                              .count();
  Check(error3, "error_callback");
  Check(error_ms <= 5000, "error_le_5s");
  Check(!error3_text.isEmpty(), "error_readable");
  Check(online3.state() == lapian::app::OnlineState::kError,
        "error_state");
  const auto error_entry = cache3.Load(
      lapian::online::CacheKey{QStringLiteral("BV1BMnv6jE5S"), 42915922022, 64, 7},
      nullptr);
  Check(error_entry.has_value() && !error_entry->complete,
        "error_part_retained");
  std::fprintf(stderr, "online_m1_error text=%s ms=%lld\n",
               error3_text.toUtf8().constData(),
               static_cast<long long>(error_ms));
  online3.Close();

  std::fprintf(stderr, "online_m1_result checks=%d failures=%d\n", checks,
               failures);
  return failures == 0 ? 0 : 1;
}
