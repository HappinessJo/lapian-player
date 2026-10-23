#include "online/GrowingSource.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <limits>
#include <utility>

#include <QDir>
#include <QFileInfo>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

#include <windows.h>

namespace lapian::online {
namespace {

using Clock = std::chrono::steady_clock;

std::int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             Clock::now().time_since_epoch())
      .count();
}

QString InvalidReasonFor(std::int64_t size) {
  if (size > 0) {
    return QString();
  }
  return QStringLiteral(
             "总大小未知（%1 字节），不能进入可增长播放；"
             "需要 playurl/Content-Length 提供已知总大小")
      .arg(size);
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
    text = QStringLiteral("未知错误");
  }
  return text;
}

// 长路径统一加 \\?\；缓存路径可能超过 MAX_PATH（T8.3 已验 301+ 字符）。
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

}  // namespace

// ---------------------------------------------------------------- GrowingSource

GrowingSource::GrowingSource(std::int64_t total_size)
    : size_(total_size), invalid_reason_(InvalidReasonFor(total_size)) {
  created_ns_ = NowNs();
}

GrowingSource::~GrowingSource() {
  std::lock_guard<std::mutex> lock(mutex_);
  // 契约：读者先 Cancel + join，再销毁 GrowingSource。
  assert(active_waiters_ == 0);
  assert(live_cursors_ == 0);
}

bool GrowingSource::IsValid() const { return size_ > 0; }

QString GrowingSource::InvalidReason() const { return invalid_reason_; }

std::int64_t GrowingSource::Size() const { return size_; }

std::int64_t GrowingSource::Published() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return published_;
}

GrowingWaitResult GrowingSource::WaitFor(std::int64_t offset) {
  return WaitForImpl(offset, 0);
}

GrowingWaitResult GrowingSource::WaitForTimeout(std::int64_t offset,
                                                std::int64_t timeout_ms) {
  return WaitForImpl(offset, timeout_ms > 0 ? timeout_ms : 0);
}

GrowingWaitResult GrowingSource::WaitForImpl(std::int64_t offset,
                                             std::int64_t timeout_ms) {
  const std::int64_t start_ns = NowNs();
  std::unique_lock<std::mutex> lock(mutex_);

  auto note_cancel_latency = [&]() {
    if (cancel_ns_ > 0) {
      const std::int64_t latency = NowNs() - cancel_ns_;
      if (latency > cancel_latency_ns_) {
        cancel_latency_ns_ = latency;
      }
    }
  };

  if (cancelled_) {
    // 没有真正阻塞过：不计“取消延迟”（避免取消很久后的新调用污染统计）。
    return GrowingWaitResult::Cancelled;
  }
  if (failed_) {
    return GrowingWaitResult::Failed;
  }
  // offset >= Size() 由调用方按 EOF 处理；这里不伪装成可读。
  if (offset < 0 || offset >= size_ || offset < published_) {
    return GrowingWaitResult::Ready;
  }

  ++waits_;
  ++active_waiters_;
  const auto predicate = [&] {
    return cancelled_ || failed_ || published_ > offset;
  };

  bool satisfied = true;
  if (timeout_ms <= 0) {
    cv_.wait(lock, predicate);
  } else {
    satisfied = cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                             predicate);
  }
  --active_waiters_;
  wait_ns_ += NowNs() - start_ns;

  if (cancelled_) {
    note_cancel_latency();
    return GrowingWaitResult::Cancelled;
  }
  if (failed_) {
    return GrowingWaitResult::Failed;
  }
  if (!satisfied) {
    ++timeouts_;
    return GrowingWaitResult::TimedOut;
  }
  return GrowingWaitResult::Ready;
}

void GrowingSource::Publish(std::int64_t published_bytes) {
  bool notify = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++publish_calls_;
    // 取消/失败后发布没有意义：不推进、不唤醒（测试会断言）。
    if (cancelled_ || failed_) {
      return;
    }
    std::int64_t clamped = published_bytes;
    if (clamped > size_) {
      clamped = size_;
    }
    if (clamped <= published_) {
      return;
    }
    const std::int64_t now = NowNs();
    if (first_publish_ns_ == 0) {
      first_publish_ns_ = now;
    }
    last_publish_ns_ = now;
    published_ = clamped;
    ++publish_events_;
    ++notifications_;
    notify = true;
  }
  if (notify) {
    cv_.notify_all();
  }
}

void GrowingSource::Fail(int av_error) {
  bool notify = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancelled_ || failed_) {
      return;
    }
    failed_ = true;
    if (av_error == 0) {
      error_code_ = AVERROR(EIO);
    } else if (av_error > 0) {
      error_code_ = AVERROR(av_error);
    } else {
      error_code_ = av_error;
    }
    ++notifications_;
    notify = true;
  }
  if (notify) {
    cv_.notify_all();
  }
}

void GrowingSource::Cancel() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancelled_) {
      return;
    }
    cancelled_ = true;
    cancel_ns_ = NowNs();
    ++notifications_;
  }
  cv_.notify_all();
}

bool GrowingSource::IsCancelled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cancelled_;
}

bool GrowingSource::HasFailed() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return failed_;
}

int GrowingSource::ErrorCode() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return error_code_;
}

GrowingSourceStats GrowingSource::Stats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  GrowingSourceStats stats;
  stats.size = size_;
  stats.published = published_;
  stats.publish_calls = publish_calls_;
  stats.publish_events = publish_events_;
  stats.waits = waits_;
  stats.wait_ns = wait_ns_;
  stats.timeouts = timeouts_;
  stats.notifications = notifications_;
  stats.cancel_latency_ns = cancel_latency_ns_;
  stats.first_publish_ns = first_publish_ns_;
  stats.last_publish_ns = last_publish_ns_;
  stats.active_waiters = active_waiters_;
  stats.live_cursors = live_cursors_;
  stats.cancelled = cancelled_;
  stats.failed = failed_;
  stats.error_code = error_code_;
  if (published_ > 0 && last_publish_ns_ > created_ns_) {
    stats.publish_bytes_per_sec =
        static_cast<double>(published_) * 1.0e9 /
        static_cast<double>(last_publish_ns_ - created_ns_);
  }
  return stats;
}

void GrowingSource::RegisterCursor() {
  std::lock_guard<std::mutex> lock(mutex_);
  ++live_cursors_;
}

void GrowingSource::UnregisterCursor() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (live_cursors_ > 0) {
    --live_cursors_;
  }
}

// ---------------------------------------------------------------- GrowingCursor

std::unique_ptr<GrowingCursor> GrowingCursor::Open(
    std::shared_ptr<GrowingSource> source, const QString& file_path,
    QString* error) {
  auto fail = [&](const QString& message) -> std::unique_ptr<GrowingCursor> {
    if (error != nullptr) {
      *error = message;
    }
    return nullptr;
  };

  if (!source) {
    return fail(QStringLiteral("可增长读层没有数据源"));
  }
  if (!source->IsValid()) {
    return fail(source->InvalidReason());
  }
  if (file_path.trimmed().isEmpty()) {
    return fail(QStringLiteral("可增长读层没有数据文件路径"));
  }

  const QString native = ToExtendedPath(file_path);
  const std::wstring wide = native.toStdWString();
  HANDLE handle = CreateFileW(
      wide.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    const DWORD code = GetLastError();
    return fail(QStringLiteral("无法打开缓存文件：%1（Win32 %2：%3）")
                    .arg(file_path, QString::number(code),
                         Win32ErrorText(code)));
  }

  LARGE_INTEGER file_size{};
  if (GetFileSizeEx(handle, &file_size) == FALSE) {
    const DWORD code = GetLastError();
    CloseHandle(handle);
    return fail(QStringLiteral("无法读取缓存文件长度：%1（Win32 %2：%3）")
                    .arg(file_path, QString::number(code),
                         Win32ErrorText(code)));
  }

  // Publish 必须在写盘之后：已发布前缀不得超前于实际文件长度。
  const std::int64_t published = source->Published();
  if (published > file_size.QuadPart) {
    CloseHandle(handle);
    return fail(QStringLiteral(
                    "可增长读层状态异常：已发布 %1 字节，但文件只有 %2 字节"
                    "（Publish 必须先写盘）")
                    .arg(published)
                    .arg(file_size.QuadPart));
  }

  auto* buffer = static_cast<unsigned char*>(av_malloc(kGrowingAvioBufferBytes));
  if (buffer == nullptr) {
    CloseHandle(handle);
    return fail(QStringLiteral("内存不足：无法分配 %1 字节 AVIO 缓冲")
                    .arg(kGrowingAvioBufferBytes));
  }

  auto cursor = std::unique_ptr<GrowingCursor>(
      new GrowingCursor(std::move(source),
                        reinterpret_cast<std::intptr_t>(handle), file_path));
  cursor->avio_ = avio_alloc_context(
      buffer, kGrowingAvioBufferBytes, 0, cursor.get(), &GrowingCursor::ReadThunk,
      nullptr, &GrowingCursor::SeekThunk);
  if (cursor->avio_ == nullptr) {
    av_free(buffer);
    // cursor 析构负责关闭句柄。
    return fail(QStringLiteral("无法创建 AVIOContext（可增长读层）"));
  }
  cursor->avio_->seekable = AVIO_SEEKABLE_NORMAL;
  cursor->source_->RegisterCursor();
  return cursor;
}

GrowingCursor::GrowingCursor(std::shared_ptr<GrowingSource> source,
                             std::intptr_t handle, QString path)
    : source_(std::move(source)), handle_(handle), path_(std::move(path)) {}

GrowingCursor::~GrowingCursor() { Close(); }

AVIOContext* GrowingCursor::avio() const { return avio_; }

std::int64_t GrowingCursor::Pos() const { return pos_; }

int GrowingCursor::ReadThunk(void* opaque, std::uint8_t* buffer,
                             int buffer_size) {
  auto* cursor = static_cast<GrowingCursor*>(opaque);
  if (cursor == nullptr) {
    return AVERROR(EINVAL);
  }
  return cursor->Read(buffer, buffer_size);
}

std::int64_t GrowingCursor::SeekThunk(void* opaque, std::int64_t offset,
                                      int whence) {
  auto* cursor = static_cast<GrowingCursor*>(opaque);
  if (cursor == nullptr) {
    return AVERROR(EINVAL);
  }
  return cursor->Seek(offset, whence);
}

int GrowingCursor::Read(std::uint8_t* buffer, int buffer_size) {
  if (avio_ == nullptr || handle_ == -1) {
    return AVERROR(EINVAL);
  }
  if (buffer == nullptr || buffer_size <= 0) {
    return AVERROR(EINVAL);
  }
  // 取消是终态：即使读的是已发布区也立刻退出（关窗/换 seek 的路径）。
  if (source_->IsCancelled()) {
    return AVERROR_EXIT;
  }
  // T8.6：单读者中断。只影响本 cursor，不取消共享 source（其它读者继续）。
  if (interrupt_ != nullptr && interrupt_->load(std::memory_order_relaxed)) {
    return AVERROR_EXIT;
  }

  const std::int64_t size = source_->Size();
  if (size <= 0) {
    return AVERROR(EIO);
  }
  std::int64_t pos = pos_;
  if (pos >= size) {
    return AVERROR_EOF;
  }

  // 本次请求最多能读多少（不超过 Size）：读回调要么返回这么多字节，要么返回错误；
  // 不在 frontiers 返回“半包”，避免 mov/h264 把短包当损坏（av_get_packet 对短读敏感）。
  const std::int64_t cap =
      std::min<std::int64_t>(buffer_size, size - pos);
  std::int64_t published = source_->Published();
  if (published - pos < cap) {
    const bool after_seek = after_uncached_seek_;
    after_uncached_seek_ = false;
    const std::int64_t wait_start = NowNs();
    GrowingWaitResult result = GrowingWaitResult::Ready;
    bool interrupted_by_cursor = false;
    if (interrupt_ != nullptr) {
      // T8.6：可中断等待。25ms 粒度把“改 seek/关窗”的退出延迟压到 ≤100ms，
      // 同时不像 1ms 轮询那样空转；每次醒来只做两个原子读。
      std::int64_t waited_ms = 0;
      while (true) {
        result = source_->WaitForTimeout(pos + cap - 1, 25);
        if (result != GrowingWaitResult::TimedOut) {
          break;
        }
        waited_ms += 25;
        if (interrupt_->load(std::memory_order_relaxed)) {
          interrupted_by_cursor = true;
          break;
        }
        if (wait_timeout_ms_ > 0 && waited_ms >= wait_timeout_ms_) {
          break;
        }
      }
    } else {
      result = wait_timeout_ms_ > 0
                   ? source_->WaitForTimeout(pos + cap - 1, wait_timeout_ms_)
                   : source_->WaitFor(pos + cap - 1);
    }
    const std::int64_t waited_ns = NowNs() - wait_start;

    ++stats_.waits;
    stats_.wait_ns += waited_ns;
    if (!stats_.first_wait_recorded) {
      stats_.first_wait_recorded = true;
      stats_.first_wait_ns = waited_ns;
    }
    if (after_seek) {
      ++stats_.seek_waits;
      stats_.seek_wait_ns += waited_ns;
    }

    if (interrupted_by_cursor) {
      return AVERROR_EXIT;
    }
    if (result == GrowingWaitResult::Cancelled) {
      return AVERROR_EXIT;
    }
    if (result == GrowingWaitResult::Failed) {
      // 下载失败不能伪装 EOF：FFmpeg 会把截断文件当成正常结束。
      return AVERROR(EIO);
    }
    if (result == GrowingWaitResult::TimedOut) {
      ++stats_.wait_timeouts;
      return AVERROR(ETIMEDOUT);
    }
    published = source_->Published();
  } else {
    after_uncached_seek_ = false;
  }

  const std::int64_t limit = std::min<std::int64_t>(published, size);
  if (pos + cap > limit) {
    // 等待返回后又被 Cancel/Fail（极窄竞态）；按终态返回，绝不当作 EOF。
    if (source_->IsCancelled()) {
      return AVERROR_EXIT;
    }
    if (source_->HasFailed()) {
      return AVERROR(EIO);
    }
    return AVERROR(EIO);
  }

  const std::int64_t want = cap;
  LARGE_INTEGER offset{};
  offset.QuadPart = pos;
  if (SetFilePointerEx(reinterpret_cast<HANDLE>(handle_), offset, nullptr,
                       FILE_BEGIN) == FALSE) {
    return AVERROR(EIO);
  }
  DWORD read = 0;
  if (ReadFile(reinterpret_cast<HANDLE>(handle_), buffer,
               static_cast<DWORD>(want), &read, nullptr) == FALSE) {
    return AVERROR(EIO);
  }
  if (read != static_cast<DWORD>(want)) {
    // 未到 Size() 却读到短/0 字节：文件被截断/状态异常，不能返回 EOF 或半包。
    return AVERROR(EIO);
  }

  pos_ += static_cast<std::int64_t>(read);
  ++stats_.reads;
  stats_.read_bytes += static_cast<std::int64_t>(read);
  if (pos_ > stats_.max_read_end) {
    stats_.max_read_end = pos_;
  }
  return static_cast<int>(read);
}

std::int64_t GrowingCursor::Seek(std::int64_t offset, int whence) {
  ++stats_.seeks;
  whence &= ~AVSEEK_FORCE;
  if (whence == AVSEEK_SIZE) {
    return source_->Size();
  }

  const std::int64_t size = source_->Size();
  std::int64_t base = 0;
  if (whence == SEEK_SET) {
    base = 0;
  } else if (whence == SEEK_CUR) {
    base = pos_;
  } else if (whence == SEEK_END) {
    base = size;
  } else {
    return AVERROR(EINVAL);
  }

  if (offset > 0 && base > std::numeric_limits<std::int64_t>::max() - offset) {
    return AVERROR(EINVAL);
  }
  if (offset < 0 && base < std::numeric_limits<std::int64_t>::min() - offset) {
    return AVERROR(EINVAL);
  }

  std::int64_t target = base + offset;
  // 不做静默 clamp：avio_seek() 会把 callback 的返回值当成“已到请求位置”，
  // clamp 会让 AVIOContext 的 pos 与实际文件偏移不一致；越界按 EINVAL 上抛。
  if (target < 0 || target > size) {
    return AVERROR(EINVAL);
  }
  pos_ = target;
  // 只有 seek 到未发布区后的第一次阻塞读才计入 “seek 等待”。
  after_uncached_seek_ = target >= source_->Published();
  return target;
}

void GrowingCursor::SetWaitTimeoutMs(int timeout_ms) {
  wait_timeout_ms_ = timeout_ms > 0 ? timeout_ms : 0;
}

int GrowingCursor::WaitTimeoutMs() const { return wait_timeout_ms_; }

void GrowingCursor::SetInterruptFlag(std::shared_ptr<std::atomic<bool>> flag) {
  interrupt_ = std::move(flag);
}

bool GrowingCursor::IsInterrupted() const {
  return interrupt_ != nullptr && interrupt_->load(std::memory_order_relaxed);
}

GrowingCursorStats GrowingCursor::Stats() const { return stats_; }

void GrowingCursor::Close() {
  if (avio_ != nullptr) {
    // libavformat 可能替换过 buffer；当前 buffer 由 av_malloc 分配，需自行释放。
    av_freep(&avio_->buffer);
    avio_context_free(&avio_);
  }
  if (handle_ != -1) {
    CloseHandle(reinterpret_cast<HANDLE>(handle_));
    handle_ = -1;
  }
  if (source_) {
    source_->UnregisterCursor();
    source_.reset();
  }
}

// ---------------------------------------------------------------- GrowingFormat

GrowingFormat::GrowingFormat() = default;

GrowingFormat::~GrowingFormat() { Close(); }

bool GrowingFormat::Open(std::shared_ptr<GrowingSource> source,
                         const QString& file_path, QString* error,
                         int wait_timeout_ms) {
  Close();
  source_ = source;

  QString cursor_error;
  cursor_ = GrowingCursor::Open(std::move(source), file_path, &cursor_error);
  if (!cursor_) {
    if (error != nullptr) {
      *error = cursor_error;
    }
    source_.reset();
    return false;
  }

  cursor_->SetWaitTimeoutMs(wait_timeout_ms);
  AVFormatContext* format = avformat_alloc_context();
  if (format == nullptr) {
    if (error != nullptr) {
      *error = QStringLiteral("内存不足：无法创建 AVFormatContext");
    }
    cursor_.reset();
    source_.reset();
    return false;
  }
  format->pb = cursor_->avio();
  format->flags |= AVFMT_FLAG_CUSTOM_IO;

  int ret = avformat_open_input(&format, nullptr, nullptr, nullptr);
  if (ret < 0) {
    const bool cancelled = source_ && source_->IsCancelled();
    // 文档约定失败时会释放并置 NULL；这里只做防御性清理，避免泄漏。
    if (format != nullptr) {
      avformat_free_context(format);
    }
    cursor_.reset();
    source_.reset();
    if (error != nullptr) {
      *error = QStringLiteral("打开可增长媒体失败：%1%2")
                   .arg(AvErrorText(ret),
                        cancelled ? QStringLiteral("（已取消）") : QString());
    }
    return false;
  }
  format_ = format;

  ret = avformat_find_stream_info(format_, nullptr);
  if (ret < 0) {
    const bool cancelled = source_ && source_->IsCancelled();
    const bool failed = source_ && source_->HasFailed();
    const QString text = QStringLiteral("读取可增长媒体流信息失败：%1%2%3")
                             .arg(AvErrorText(ret),
                                  cancelled ? QStringLiteral("（已取消）")
                                            : QString(),
                                  failed ? QStringLiteral("（下载失败）")
                                         : QString());
    Close();
    if (error != nullptr) {
      *error = text;
    }
    return false;
  }
  return true;
}

void GrowingFormat::Close() {
  if (format_ != nullptr) {
    // AVFMT_FLAG_CUSTOM_IO：avformat_close_input 不关闭/释放 pb，由 cursor 释放。
    avformat_close_input(&format_);
  }
  cursor_.reset();
  source_.reset();
}

bool GrowingFormat::IsOpen() const { return format_ != nullptr; }

AVFormatContext* GrowingFormat::context() const { return format_; }

GrowingCursor* GrowingFormat::cursor() const { return cursor_.get(); }

QString AvErrorText(int av_error) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  if (av_strerror(av_error, buffer, sizeof(buffer)) < 0) {
    return QStringLiteral("FFmpeg 错误 %1").arg(av_error);
  }
  return QString::fromUtf8(buffer);
}

}  // namespace lapian::online
