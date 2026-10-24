#include "online/DownloadJob.h"

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>

#include <windows.h>

extern "C" {
#include <libavutil/error.h>
}

namespace lapian::online {
namespace {

using Clock = std::chrono::steady_clock;

std::int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             Clock::now().time_since_epoch())
      .count();
}

QString ToExtendedPath(const QString& path) {
  QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
  if (native.startsWith(QStringLiteral("\\\\?\\"))) {
    return native;
  }
  if (native.startsWith(QStringLiteral("\\\\"))) {
    return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
  }
  return QStringLiteral("\\\\?\\") + native;
}

QString Win32ErrorText(DWORD code) {
  wchar_t* buffer = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
  QString text;
  if (length > 0 && buffer != nullptr) {
    text = QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed();
  }
  if (buffer != nullptr) {
    LocalFree(buffer);
  }
  if (text.isEmpty()) {
    text = QStringLiteral("Win32 %1").arg(code);
  }
  return text;
}

int AvErrorFromWin32(DWORD code) {
  switch (code) {
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
      return AVERROR(ENOSPC);
    case ERROR_ACCESS_DENIED:
    case ERROR_WRITE_PROTECT:
      return AVERROR(EACCES);
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
      return AVERROR(ENOMEM);
    case ERROR_OPERATION_ABORTED:
      return AVERROR(ECANCELED);
    default:
      return AVERROR(EIO);
  }
}

}  // namespace

QString DownloadStateName(DownloadState state) {
  switch (state) {
    case DownloadState::kIdle:
      return QStringLiteral("idle");
    case DownloadState::kPreparing:
      return QStringLiteral("preparing");
    case DownloadState::kDownloading:
      return QStringLiteral("downloading");
    case DownloadState::kPaused:
      return QStringLiteral("paused");
    case DownloadState::kRetrying:
      return QStringLiteral("retrying");
    case DownloadState::kRefreshing:
      return QStringLiteral("refreshing");
    case DownloadState::kAwaitingFinalize:
      return QStringLiteral("awaiting_finalize");
    case DownloadState::kCompleted:
      return QStringLiteral("completed");
    case DownloadState::kFailed:
      return QStringLiteral("failed");
    case DownloadState::kCancelled:
      return QStringLiteral("cancelled");
  }
  return QStringLiteral("unknown");
}

class DownloadJob::PartSink final : public IDownloadSink {
 public:
  explicit PartSink(DownloadJob* job) : job_(job) {}
  bool Write(const std::uint8_t* data, std::size_t size,
             QString* error) override {
    return job_->WritePart(data, size, error);
  }

 private:
  DownloadJob* job_ = nullptr;
};

DownloadJob::DownloadJob(CacheStore* cache, CacheKey key,
                         std::shared_ptr<GrowingSource> source,
                         IDownloadTransport* transport, DownloadPlan plan,
                         DownloadRefreshCallback refresh, DownloadOptions options)
    : cache_(cache),
      key_(std::move(key)),
      source_(std::move(source)),
      transport_(transport),
      plan_(std::move(plan)),
      refresh_(std::move(refresh)),
      options_(std::move(options)) {}

DownloadJob::~DownloadJob() {
  Cancel();
  if (worker_.joinable()) {
    worker_.join();
  }
  CloseWriteHandle();
}

bool DownloadJob::Start(QString* error) {
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = DownloadState::kFailed;
    stats_.error = message;
    return false;
  };

  if (cache_ == nullptr || source_ == nullptr || transport_ == nullptr) {
    return fail(QStringLiteral("下载任务缺少缓存/读层/传输组件"));
  }
  if (!IsValidCacheKey(key_)) {
    return fail(QStringLiteral("缓存键无效，无法开始下载"));
  }
  if (plan_.total_bytes <= 0) {
    return fail(QStringLiteral("取流没有给出总大小，无法进入可增长播放"));
  }
  if (plan_.urls.isEmpty()) {
    return fail(QStringLiteral("取流没有返回可用的下载地址"));
  }
  if (source_->Size() != plan_.total_bytes) {
    return fail(QStringLiteral("可增长读层的总大小与取流信息不一致"));
  }
  total_ = plan_.total_bytes;

  QString cache_error;
  if (!cache_->Reserve(key_, total_, &cache_error)) {
    return fail(cache_error.isEmpty() ? QStringLiteral("无法创建缓存条目")
                                      : cache_error);
  }

  // 已完整缓存：T8.7 的缓存命中路径负责 OpenComplete；这里不重复下载。
  QString load_error;
  const std::optional<CacheEntry> entry = cache_->Load(key_, &load_error);
  if (entry.has_value() && entry->complete && !entry->broken) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = DownloadState::kCompleted;
    finished_ = true;
    stats_.total_bytes = total_;
    stats_.written_bytes = total_;
    stats_.already_complete = true;
    cv_.notify_all();
    return true;
  }

  QString open_error;
  if (!cache_->OpenPart(key_, &part_path_, &existing_, &open_error)) {
    return fail(open_error.isEmpty() ? QStringLiteral("无法打开缓存文件")
                                     : open_error);
  }
  if (existing_ < 0) {
    existing_ = 0;
  }
  if (existing_ > total_) {
    return fail(QStringLiteral(
                    "缓存文件比取流总大小还大（%1 > %2 字节），请重建缓存")
                    .arg(existing_)
                    .arg(total_));
  }

  const std::wstring wide = ToExtendedPath(part_path_).toStdWString();
  HANDLE handle =
      CreateFileW(wide.c_str(), GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    const DWORD code = GetLastError();
    return fail(QStringLiteral("无法打开缓存写入句柄：%1").arg(Win32ErrorText(code)));
  }
  LARGE_INTEGER file_size{};
  if (GetFileSizeEx(handle, &file_size) == FALSE) {
    const DWORD code = GetLastError();
    CloseHandle(handle);
    return fail(QStringLiteral("无法读取缓存文件长度：%1")
                    .arg(Win32ErrorText(code)));
  }
  if (file_size.QuadPart != existing_) {
    // .part 长度是唯一真相；OpenPart 与句柄看到不同长度时按句柄校正。
    existing_ = file_size.QuadPart;
    if (existing_ > total_) {
      CloseHandle(handle);
      return fail(QStringLiteral("缓存文件长度异常（%1 > %2 字节）")
                      .arg(existing_)
                      .arg(total_));
    }
  }
  LARGE_INTEGER offset{};
  offset.QuadPart = existing_;
  if (SetFilePointerEx(handle, offset, nullptr, FILE_BEGIN) == FALSE) {
    const DWORD code = GetLastError();
    CloseHandle(handle);
    return fail(QStringLiteral("无法定位缓存写入位置：%1")
                    .arg(Win32ErrorText(code)));
  }
  write_handle_ = reinterpret_cast<std::intptr_t>(handle);
  written_ = existing_;

  // playurl 元数据写 meta（失败不阻塞下载；meta 缺失可由 Load 自愈）。
  cache_->SetMetadata(key_, plan_.metadata, nullptr);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = DownloadState::kPreparing;
    stats_.total_bytes = total_;
    stats_.existing_bytes = existing_;
    stats_.written_bytes = existing_;
  }

  if (existing_ == total_) {
    CloseWriteHandle();
    source_->Publish(total_);
    cache_->UpdateProgress(key_, total_, true);
    if (options_.defer_finalize) {
      std::lock_guard<std::mutex> lock(mutex_);
      state_ = DownloadState::kAwaitingFinalize;
      finished_ = true;
      stats_.total_bytes = total_;
      stats_.written_bytes = total_;
      stats_.elapsed_ms = 1;
      cv_.notify_all();
      return true;
    }
    QString complete_error;
    if (!cache_->MarkComplete(key_, &complete_error)) {
      return fail(complete_error.isEmpty()
                      ? QStringLiteral("缓存完成失败（.part 长度已满）")
                      : complete_error);
    }
    QString mp4_path;
    QString open_complete_error;
    if (!cache_->OpenComplete(key_, &mp4_path, &open_complete_error)) {
      return fail(open_complete_error.isEmpty()
                      ? QStringLiteral("缓存完成后无法打开 .mp4")
                      : open_complete_error);
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      state_ = DownloadState::kCompleted;
      finished_ = true;
      stats_.written_bytes = total_;
      stats_.downloaded_bytes = 0;
      stats_.elapsed_ms = 1;
      cv_.notify_all();
    }
    return true;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    started_ = true;
  }
  try {
    worker_ = std::thread([this]() { Run(); });
  } catch (...) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      started_ = false;
      state_ = DownloadState::kFailed;
      stats_.error = QStringLiteral("无法创建下载线程");
    }
    CloseWriteHandle();
    return fail(QStringLiteral("无法创建下载线程"));
  }
  return true;
}

void DownloadJob::Run() {
  run_start_ms_ = NowMs();
  last_progress_ms_ = run_start_ms_;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.last_progress_ms = run_start_ms_;
  }

  if (written_ > 0) {
    // 续传前缀在 Start 中已确认写盘长度；发布给 reader。
    source_->Publish(written_);
    cache_->UpdateProgress(key_, written_, true);
  }
  UpdateState(DownloadState::kDownloading);

  DownloadPlan active = plan_;
  int url_index = 0;
  int consecutive_failures = 0;
  int total_failures = 0;
  bool refreshed = false;
  QString last_error;

  PartSink sink(this);
  while (true) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (cancelled_) {
        FinishCancelled();
        return;
      }
    }
    if (IsPauseRequested()) {
      if (!WaitWhilePaused()) {
        FinishCancelled();
        return;
      }
      continue;
    }

    const std::int64_t current = [this]() {
      std::lock_guard<std::mutex> lock(mutex_);
      return written_;
    }();
    if (current >= total_) {
      break;
    }
    if (url_index < 0 || url_index >= active.urls.size()) {
      if (!refreshed && refresh_) {
        UpdateState(DownloadState::kRefreshing);
        DownloadPlan fresh;
        QString refresh_error;
        auto token = std::make_shared<HttpCancelToken>();
        {
          std::lock_guard<std::mutex> lock(mutex_);
          refresh_cancel_ = token;
        }
        const bool ok = refresh_(&fresh, *token, &refresh_error);
        {
          std::lock_guard<std::mutex> lock(mutex_);
          refresh_cancel_.reset();
        }
        if (IsCancelRequested()) {
          FinishCancelled();
          return;
        }
        if (ok && fresh.total_bytes == total_ && !fresh.urls.isEmpty()) {
          active.urls = fresh.urls;
          url_index = 0;
          consecutive_failures = 0;
          refreshed = true;
          SetStats([](DownloadStats* stats) { ++stats->refreshes; });
          UpdateState(DownloadState::kDownloading);
          continue;
        }
        last_error = ok ? QStringLiteral("刷新后的取流信息与缓存总大小不一致")
                        : refresh_error;
      }
      FinishFailed(AVERROR(EIO),
                   last_error.isEmpty() ? QStringLiteral("取流失败，没有可用线路")
                                        : last_error);
      return;
    }

    const QString url = active.urls.at(url_index);
    auto token = std::make_shared<HttpCancelToken>();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      attempt_cancel_ = token;
    }
    DownloadRequest request;
    request.url = url;
    request.offset = current;
    request.expected_total = total_;
    request.timeout_ms = options_.attempt_timeout_ms;
    request.user_agent = options_.user_agent;
    request.referer = options_.referer;
    SetStats([](DownloadStats* stats) { ++stats->range_requests; });
    // T8.13：每次尝试都重新计“无响应”窗口；连接/首字节等待也算在这个窗口内。
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stats_.last_progress_ms = NowMs();
      stalled_ = false;
    }
    const DownloadTransportResult result =
        transport_->GetRange(request, &sink, *token);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      attempt_cancel_.reset();
    }
    // T8.13：Poll 判定“连续 N 秒没有写盘”后中止本次尝试；不再重试/切备份，
    // 直接给可读错误（.part 保留，缓存区仍可播放，重新打开可续传）。
    bool stall_aborted = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stall_aborted = stalled_;
      stalled_ = false;
    }
    if (stall_aborted) {
      FinishFailed(
          AVERROR(ETIMEDOUT),
          QStringLiteral("网络无响应：连续 %1 秒没有收到下载数据，已暂停下载"
                         "（已缓存区仍可播放；重新打开同一视频可续传）")
              .arg(std::max(1, options_.stall_timeout_ms / 1000)));
      return;
    }

    const std::int64_t after = [this]() {
      std::lock_guard<std::mutex> lock(mutex_);
      return written_;
    }();
    SetStats([&](DownloadStats* stats) {
      if (result.http_status >= 200) {
        if (result.http_status == 206) {
          ++stats->http_206;
        } else if (result.http_status == 200) {
          ++stats->http_200;
        }
      }
      if (result.first_byte_ms >= 0 && stats->first_byte_ms < 0) {
        const std::int64_t elapsed = NowMs() - run_start_ms_;
        stats->first_byte_ms =
            std::max<std::int64_t>(0, elapsed - result.elapsed_ms +
                                           result.first_byte_ms);
      }
      stats->written_bytes = after;
      stats->downloaded_bytes = session_bytes_;
    });

    if (result.range_ignored) {
      // 服务端忽略 Range：必须从 0 重下，不能把全量追加在旧字节后。
      // 没有 reader 时 truncate；已有 reader 时原地覆盖（避免短读被当作 EIO）。
      SetStats([](DownloadStats* stats) { stats->range_ignored = true; });
      const bool no_readers = source_->Stats().live_cursors == 0;
      LARGE_INTEGER zero{};
      if (SetFilePointerEx(reinterpret_cast<HANDLE>(write_handle_), zero,
                           nullptr, FILE_BEGIN) == FALSE) {
        FinishFailed(AvErrorFromWin32(GetLastError()),
                     QStringLiteral("无法重置缓存写入位置"));
        return;
      }
      if (no_readers) {
        if (SetEndOfFile(reinterpret_cast<HANDLE>(write_handle_)) == FALSE) {
          FinishFailed(AvErrorFromWin32(GetLastError()),
                       QStringLiteral("无法截断缓存文件"));
          return;
        }
        SetStats([](DownloadStats* stats) {
          stats->range_ignored_truncated = true;
        });
      } else {
        SetStats([](DownloadStats* stats) {
          stats->range_ignored_overwrote = true;
        });
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        written_ = 0;
        stats_.written_bytes = 0;
      }
      consecutive_failures = 0;
      last_error = QStringLiteral("服务器不支持断点续传，已从头重新下载");
      UpdateState(DownloadState::kDownloading);
      continue;
    }

    if (result.start_mismatch) {
      FinishFailed(AVERROR(EIO),
                   QStringLiteral("服务器返回的续传起点不一致，已保留缓存"));
      return;
    }
    if (result.total_mismatch) {
      FinishFailed(AVERROR(EIO),
                   QStringLiteral("文件总大小与取流信息不一致，已保留缓存"));
      return;
    }
    if (result.status == DownloadTransportStatus::kSinkError) {
      FinishFailed(sink_av_error_ != 0 ? sink_av_error_ : AVERROR(EIO),
                   result.error.isEmpty() ? QStringLiteral("写入缓存失败")
                                          : result.error);
      return;
    }

    if (result.cancelled) {
      if (IsCancelRequested()) {
        FinishCancelled();
        return;
      }
      if (IsPauseRequested()) {
        if (!WaitWhilePaused()) {
          FinishCancelled();
          return;
        }
        consecutive_failures = 0;
        continue;
      }
      // token 只由 Pause/Cancel 置位；两者都未置位时按一次网络失败处理。
    }

    if (result.ok && after >= total_) {
      break;
    }

    const bool hard_failure =
        result.http_status == 400 || result.http_status == 401 ||
        result.http_status == 403 || result.http_status == 404 ||
        result.http_status == 410 || result.http_status == 416;
    const bool made_progress = after > current;
    if (result.ok) {
      last_error = QStringLiteral("服务器提前结束下载，正在续传");
    } else if (!result.error.isEmpty()) {
      last_error = result.error;
    }
    if (!result.ok) {
      ++total_failures;
      SetStats([](DownloadStats* stats) { ++stats->retries; });
    }

    if (!hard_failure && total_failures <= 50) {
      if (made_progress) {
        // 连接中断但已有新字节：当前 URL 直接续传，不等待退避。
        consecutive_failures = 0;
        UpdateState(DownloadState::kDownloading);
        continue;
      }
      if (consecutive_failures + 1 <
          std::max(1, options_.max_attempts_per_url)) {
        ++consecutive_failures;
        UpdateState(DownloadState::kRetrying);
        const int slot = std::min(consecutive_failures - 1, 2);
        if (!SleepInterruptible(options_.backoff_ms[slot])) {
          if (IsCancelRequested()) {
            FinishCancelled();
            return;
          }
          continue;  // paused：回到循环顶部等待
        }
        continue;
      }
    }

    consecutive_failures = 0;
    if (url_index + 1 < active.urls.size()) {
      ++url_index;
      SetStats([](DownloadStats* stats) { ++stats->backup_switches; });
      UpdateState(DownloadState::kRetrying);
      continue;
    }
    if (!refreshed && refresh_) {
      UpdateState(DownloadState::kRefreshing);
      DownloadPlan fresh;
      QString refresh_error;
      auto refresh_token = std::make_shared<HttpCancelToken>();
      {
        std::lock_guard<std::mutex> lock(mutex_);
        refresh_cancel_ = refresh_token;
      }
      const bool ok = refresh_(&fresh, *refresh_token, &refresh_error);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        refresh_cancel_.reset();
      }
      if (IsCancelRequested()) {
        FinishCancelled();
        return;
      }
      if (ok && fresh.total_bytes == total_ && !fresh.urls.isEmpty()) {
        active.urls = fresh.urls;
        url_index = 0;
        consecutive_failures = 0;
        refreshed = true;
        SetStats([](DownloadStats* stats) { ++stats->refreshes; });
        UpdateState(DownloadState::kDownloading);
        continue;
      }
      last_error = ok ? QStringLiteral("刷新后的取流信息与缓存总大小不一致")
                      : refresh_error;
    }
    FinishFailed(AVERROR(EIO), last_error);
    return;
  }

  FinishCompleted();
}

bool DownloadJob::WritePart(const std::uint8_t* data, std::size_t size,
                            QString* error) {
  if (data == nullptr || size == 0) {
    return true;
  }
  std::int64_t offset = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancelled_) {
      if (error != nullptr) {
        *error = QStringLiteral("下载已取消");
      }
      sink_av_error_ = AVERROR(ECANCELED);
      return false;
    }
    offset = written_;
  }
  if (offset < 0 || offset + static_cast<std::int64_t>(size) > total_) {
    sink_av_error_ = AVERROR(EIO);
    if (error != nullptr) {
      *error = QStringLiteral("写入缓存的数据超过已知总大小");
    }
    return false;
  }
  HANDLE handle = reinterpret_cast<HANDLE>(write_handle_);
  if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
    sink_av_error_ = AVERROR(EIO);
    if (error != nullptr) {
      *error = QStringLiteral("缓存写入句柄不可用");
    }
    return false;
  }
  LARGE_INTEGER position{};
  position.QuadPart = offset;
  if (SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) == FALSE) {
    const DWORD code = GetLastError();
    sink_av_error_ = AvErrorFromWin32(code);
    if (error != nullptr) {
      *error = QStringLiteral("无法定位缓存写入位置：%1")
                   .arg(Win32ErrorText(code));
    }
    return false;
  }
  DWORD written = 0;
  if (WriteFile(handle, data, static_cast<DWORD>(size), &written, nullptr) ==
      FALSE) {
    const DWORD code = GetLastError();
    sink_av_error_ = AvErrorFromWin32(code);
    if (error != nullptr) {
      *error = QStringLiteral("写入缓存失败：%1").arg(Win32ErrorText(code));
    }
    return false;
  }
  if (written != size) {
    sink_av_error_ = AVERROR(EIO);
    if (error != nullptr) {
      *error = QStringLiteral("写入缓存不完整（%1/%2 字节）")
                   .arg(written)
                   .arg(size);
    }
    return false;
  }

  const std::int64_t updated = offset + static_cast<std::int64_t>(written);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    written_ = updated;
    session_bytes_ += static_cast<std::int64_t>(written);
    stats_.written_bytes = written_;
    stats_.downloaded_bytes = session_bytes_;
    // T8.13：任何一次成功写盘都刷新“无响应”窗口（不受 1s 进度写盘节流影响）。
    stats_.last_progress_ms = NowMs();
  }
  // T8.11：测试慢增长在写盘后、发布前节流；产品默认 0 不进入。
  if (options_.throttle_bytes_per_sec > 0) {
    ThrottleWrite(static_cast<std::size_t>(written));
  }
  // 必须先写盘再发布：reader 只读 Published 以内的字节。
  source_->Publish(updated);

  LARGE_INTEGER file_size{};
  if (GetFileSizeEx(handle, &file_size) == FALSE ||
      file_size.QuadPart < updated) {
    sink_av_error_ = AVERROR(EIO);
    if (error != nullptr) {
      *error = QStringLiteral("缓存文件长度小于已发布字节");
    }
    return false;
  }

  const std::int64_t now = NowMs();
  if (now - last_progress_ms_ >= options_.progress_interval_ms) {
    last_progress_ms_ = now;
    cache_->UpdateProgress(key_, updated, false);
  }
  return true;
}

bool DownloadJob::IsCancelRequested() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cancelled_;
}

bool DownloadJob::IsPauseRequested() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return paused_;
}

bool DownloadJob::SleepInterruptible(int ms) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (cv_.wait_for(lock, std::chrono::milliseconds(ms),
                   [this]() { return cancelled_ || paused_; })) {
    return false;
  }
  return true;
}

void DownloadJob::ThrottleWrite(std::size_t size) {
  const std::int64_t rate = options_.throttle_bytes_per_sec;
  if (rate <= 0 || size == 0) {
    return;
  }
  const std::int64_t now = NowMs();
  if (throttle_start_ms_ == 0) {
    throttle_start_ms_ = now;
  }
  throttle_bytes_ += static_cast<std::int64_t>(size);
  const std::int64_t target_ms =
      throttle_start_ms_ + throttle_bytes_ * 1000 / rate;
  while (!IsCancelRequested()) {
    const std::int64_t remaining = target_ms - NowMs();
    if (remaining <= 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(
        std::min<std::int64_t>(remaining, 20)));
  }
}

bool DownloadJob::WaitWhilePaused() {
  const std::int64_t current = [this]() {
    std::lock_guard<std::mutex> lock(mutex_);
    return written_;
  }();
  cache_->UpdateProgress(key_, current, true);
  std::unique_lock<std::mutex> lock(mutex_);
  if (!paused_) {
    return !cancelled_;
  }
  state_ = DownloadState::kPaused;
  cv_.wait(lock, [this]() { return !paused_ || cancelled_; });
  if (cancelled_) {
    return false;
  }
  state_ = DownloadState::kDownloading;
  // T8.13：暂停可能持续很久，恢复后重新计“无响应”窗口，避免一恢复就被看门狗中止。
  stats_.last_progress_ms = NowMs();
  return true;
}

void DownloadJob::UpdateState(DownloadState state) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!finished_) {
    state_ = state;
  }
}

void DownloadJob::SetStats(const std::function<void(DownloadStats*)>& update) {
  std::lock_guard<std::mutex> lock(mutex_);
  update(&stats_);
}

void DownloadJob::CloseWriteHandle() {
  if (write_handle_ == -1) {
    return;
  }
  HANDLE handle = reinterpret_cast<HANDLE>(write_handle_);
  write_handle_ = -1;
  FlushFileBuffers(handle);
  CloseHandle(handle);
}

void DownloadJob::FinishCompleted() {
  std::int64_t written = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    written = written_;
  }
  if (written != total_) {
    FinishFailed(AVERROR(EIO), QStringLiteral("下载结束但缓存长度不足"));
    return;
  }
  CloseWriteHandle();
  source_->Publish(total_);
  cache_->UpdateProgress(key_, total_, true);

  if (options_.defer_finalize) {
    // T8.6：数据已完整发布，但 .part 仍被正在播放的 reader 打开。
    // 不 rename；由 OnlineSession 停 reader 后调用 FinalizeCompleted()。
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = DownloadState::kAwaitingFinalize;
    finished_ = true;
    stats_.total_bytes = total_;
    stats_.written_bytes = total_;
    stats_.downloaded_bytes = session_bytes_;
    stats_.elapsed_ms = std::max<std::int64_t>(1, NowMs() - run_start_ms_);
    stats_.bps = static_cast<double>(session_bytes_) * 1000.0 /
                 static_cast<double>(stats_.elapsed_ms);
    cv_.notify_all();
    return;
  }

  QString complete_error;
  if (!cache_->MarkComplete(key_, &complete_error)) {
    FinishFailed(AVERROR(EIO),
                 complete_error.isEmpty()
                     ? QStringLiteral("缓存完成失败，已保留 .part")
                     : complete_error);
    return;
  }
  QString mp4_path;
  QString open_error;
  if (!cache_->OpenComplete(key_, &mp4_path, &open_error)) {
    FinishFailed(AVERROR(EIO),
                 open_error.isEmpty() ? QStringLiteral("缓存完成后无法打开 .mp4")
                                      : open_error);
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  state_ = DownloadState::kCompleted;
  finished_ = true;
  stats_.total_bytes = total_;
  stats_.written_bytes = total_;
  stats_.downloaded_bytes = session_bytes_;
  stats_.elapsed_ms = std::max<std::int64_t>(1, NowMs() - run_start_ms_);
  stats_.bps = static_cast<double>(session_bytes_) * 1000.0 /
               static_cast<double>(stats_.elapsed_ms);
  cv_.notify_all();
}

bool DownloadJob::FinalizeCompleted(QString* error) {
  auto fail = [&](const QString& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = DownloadState::kFailed;
    stats_.error = message;
    stats_.av_error = AVERROR(EIO);
    if (error != nullptr) {
      *error = message;
    }
    cv_.notify_all();
    return false;
  };
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DownloadState::kAwaitingFinalize) {
      const QString message = QStringLiteral("下载尚未进入可完成状态");
      if (error != nullptr) {
        *error = message;
      }
      return false;
    }
  }
  QString complete_error;
  if (!cache_->MarkComplete(key_, &complete_error)) {
    return fail(complete_error.isEmpty()
                    ? QStringLiteral("缓存完成失败，已保留 .part")
                    : complete_error);
  }
  QString mp4_path;
  QString open_error;
  if (!cache_->OpenComplete(key_, &mp4_path, &open_error)) {
    return fail(open_error.isEmpty() ? QStringLiteral("缓存完成后无法打开 .mp4")
                                     : open_error);
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = DownloadState::kCompleted;
    stats_.error.clear();
    stats_.av_error = 0;
  }
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

void DownloadJob::FinishFailed(int av_error, const QString& error) {
  CloseWriteHandle();
  const std::int64_t current = [this]() {
    std::lock_guard<std::mutex> lock(mutex_);
    return written_;
  }();
  if (current > 0) {
    cache_->UpdateProgress(key_, current, true);
  }
  source_->Fail(av_error == 0 ? AVERROR(EIO) : av_error);
  std::lock_guard<std::mutex> lock(mutex_);
  state_ = DownloadState::kFailed;
  finished_ = true;
  stats_.error = error;
  stats_.av_error = av_error == 0 ? AVERROR(EIO) : av_error;
  stats_.written_bytes = written_;
  stats_.downloaded_bytes = session_bytes_;
  stats_.elapsed_ms = std::max<std::int64_t>(1, NowMs() - run_start_ms_);
  stats_.bps = static_cast<double>(session_bytes_) * 1000.0 /
               static_cast<double>(stats_.elapsed_ms);
  cv_.notify_all();
}

void DownloadJob::FinishCancelled() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelled_ = true;
  }
  CloseWriteHandle();
  const std::int64_t current = [this]() {
    std::lock_guard<std::mutex> lock(mutex_);
    return written_;
  }();
  if (current > 0) {
    cache_->UpdateProgress(key_, current, true);
  }
  source_->Cancel();
  std::lock_guard<std::mutex> lock(mutex_);
  state_ = DownloadState::kCancelled;
  finished_ = true;
  stats_.written_bytes = written_;
  stats_.downloaded_bytes = session_bytes_;
  stats_.elapsed_ms = std::max<std::int64_t>(1, NowMs() - run_start_ms_);
  stats_.bps = static_cast<double>(session_bytes_) * 1000.0 /
               static_cast<double>(stats_.elapsed_ms);
  cv_.notify_all();
}

void DownloadJob::Pause() {
  std::shared_ptr<HttpCancelToken> token;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_ || finished_) {
      return;
    }
    paused_ = true;
    token = attempt_cancel_;
  }
  if (token != nullptr) {
    token->Cancel();
  }
}

void DownloadJob::Resume() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!paused_ || finished_) {
      return;
    }
    paused_ = false;
    stats_.last_progress_ms = NowMs();
  }
  cv_.notify_all();
}

void DownloadJob::Cancel() {
  std::shared_ptr<HttpCancelToken> attempt;
  std::shared_ptr<HttpCancelToken> refresh;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelled_ = true;
    paused_ = false;
    attempt = attempt_cancel_;
    refresh = refresh_cancel_;
  }
  if (attempt != nullptr) {
    attempt->Cancel();
  }
  if (refresh != nullptr) {
    refresh->Cancel();
  }
  cv_.notify_all();
}

void DownloadJob::AbortStalledAttempt() {
  std::shared_ptr<HttpCancelToken> attempt;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_ || finished_ || cancelled_ || stalled_) {
      return;
    }
    stalled_ = true;
    attempt = attempt_cancel_;
  }
  if (attempt != nullptr) {
    attempt->Cancel();
  }
  cv_.notify_all();
}

bool DownloadJob::StalledAborted() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return stalled_;
}

bool DownloadJob::Wait(int timeout_ms) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (finished_) {
    return true;
  }
  if (timeout_ms < 0) {
    cv_.wait(lock, [this]() { return finished_; });
  } else {
    cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                 [this]() { return finished_; });
  }
  return finished_;
}

DownloadState DownloadJob::state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

DownloadStats DownloadJob::stats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return stats_;
}

QString DownloadJob::part_path() const { return part_path_; }

CacheKey DownloadJob::key() const { return key_; }

}  // namespace lapian::online
