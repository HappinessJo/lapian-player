#include "app/OnlineGrowingTestRunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
}

#include "app/ProcessMemory.h"
#include "online/CacheStore.h"
#include "online/GrowingSource.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#endif

namespace lapian::app {
namespace {

using lapian::online::CacheKey;
using lapian::online::CacheEntry;
using lapian::online::CacheStore;
using lapian::online::GrowingCursor;
using lapian::online::GrowingFormat;
using lapian::online::GrowingSource;
using lapian::online::GrowingWaitResult;

constexpr std::int64_t kKb = 1024;
constexpr std::int64_t kMb = 1024 * kKb;
constexpr std::int64_t kFirstFrameBytes = 300 * kKb;  // 307200
constexpr std::int64_t kSeekPrefixBytes = 8 * kMb;
constexpr int kDefaultStressSeconds = 10;
constexpr int kStressWaitTimeoutMs = 20000;

using Clock = std::chrono::steady_clock;

int g_checks = 0;
int g_failures = 0;

std::int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             Clock::now().time_since_epoch())
      .count();
}

std::int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             Clock::now().time_since_epoch())
      .count();
}

double WsMb() {
  ProcessMemorySample sample;
  if (!SampleProcessMemory(&sample)) {
    return 0.0;
  }
  return static_cast<double>(sample.working_set_bytes) / (1024.0 * 1024.0);
}

std::uint64_t ProcessCpuMs() {
  FILETIME create{};
  FILETIME exit{};
  FILETIME kernel{};
  FILETIME user{};
  if (GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user) ==
      FALSE) {
    return 0;
  }
  ULARGE_INTEGER k{};
  ULARGE_INTEGER u{};
  k.LowPart = kernel.dwLowDateTime;
  k.HighPart = kernel.dwHighDateTime;
  u.LowPart = user.dwLowDateTime;
  u.HighPart = user.dwHighDateTime;
  return (k.QuadPart + u.QuadPart) / 10000ULL;  // 100ns -> ms
}

void Check(const char* name, bool ok, const QString& detail = QString()) {
  ++g_checks;
  if (ok) {
    std::printf("[ OK ] T8.4 %s\n", name);
  } else {
    ++g_failures;
    std::printf("[FAIL] T8.4 %s%s%s\n", name, detail.isEmpty() ? "" : "：",
                detail.toUtf8().constData());
  }
  std::fflush(stdout);
}

QString Num(std::int64_t value) { return QString::number(value); }
QString Num(int value) { return QString::number(value); }
QString Num(double value) { return QString::number(value, 'f', 3); }

void PrintKV(const char* key, const QString& value) {
  std::printf("%s=%s\n", key, value.toUtf8().constData());
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

bool MarkSparse(const QString& path) {
  const std::wstring wide = ToExtendedPath(path).toStdWString();
  HANDLE handle = CreateFileW(wide.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD returned = 0;
  const BOOL ok = DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr,
                                  0, &returned, nullptr);
  CloseHandle(handle);
  return ok != 0;
}

bool CreateSizedFile(const QString& path, std::int64_t size) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  file.close();
  MarkSparse(path);
  if (!file.open(QIODevice::ReadWrite)) {
    return false;
  }
  const bool ok = file.resize(size);
  file.close();
  return ok;
}

bool WriteAt(const QString& path, std::int64_t offset, const std::uint8_t* data,
             std::int64_t size) {
  const std::wstring wide = ToExtendedPath(path).toStdWString();
  HANDLE handle = CreateFileW(wide.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  LARGE_INTEGER position{};
  position.QuadPart = offset;
  bool ok = SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) != FALSE;
  std::int64_t written = 0;
  while (ok && written < size) {
    const std::int64_t remaining = size - written;
    const DWORD chunk =
        static_cast<DWORD>(remaining > 64 * kKb ? 64 * kKb : remaining);
    DWORD wrote = 0;
    ok = WriteFile(handle, data + written, chunk, &wrote, nullptr) != FALSE &&
         wrote == chunk;
    if (!ok) {
      break;
    }
    written += wrote;
  }
  CloseHandle(handle);
  return ok;
}

bool WriteZeros(const QString& path, std::int64_t offset, std::int64_t size) {
  static const std::uint8_t zeros[64 * 1024] = {};
  std::int64_t written = 0;
  while (written < size) {
    const std::int64_t remaining = size - written;
    const std::int64_t chunk = remaining > 64 * kKb ? 64 * kKb : remaining;
    if (!WriteAt(path, offset + written, zeros, chunk)) {
      return false;
    }
    written += chunk;
  }
  return true;
}

bool ReadSampleAt(const QString& path, std::int64_t offset, std::uint8_t* data,
                  int size) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  if (!file.seek(offset)) {
    return false;
  }
  return file.read(reinterpret_cast<char*>(data), size) == size;
}

struct Mp4Boxes {
  std::int64_t moov_offset = -1;
  std::int64_t moov_size = 0;
  std::int64_t mdat_offset = -1;
};

QString ParseMp4Boxes(const QString& path, Mp4Boxes* boxes) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QStringLiteral("无法读取样本：%1").arg(path);
  }
  const QByteArray head = file.read(1024 * 1024);
  auto be32 = [&](int at) -> std::uint32_t {
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(head[at])) << 24) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(head[at + 1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(head[at + 2])) << 8) |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(head[at + 3]));
  };
  auto be64 = [&](int at) -> std::uint64_t {
    return (static_cast<std::uint64_t>(be32(at)) << 32) | be32(at + 4);
  };

  std::int64_t offset = 0;
  while (offset + 8 <= head.size()) {
    std::uint32_t size32 = be32(static_cast<int>(offset));
    const QByteArray type = head.mid(static_cast<int>(offset) + 4, 4);
    std::int64_t size = size32;
    std::int64_t header = 8;
    if (size32 == 1) {
      if (offset + 16 > head.size()) {
        break;
      }
      size = static_cast<std::int64_t>(be64(static_cast<int>(offset) + 8));
      header = 16;
    }
    if (size < header) {
      break;
    }
    if (type == "moov") {
      boxes->moov_offset = offset;
      boxes->moov_size = size;
    } else if (type == "mdat") {
      boxes->mdat_offset = offset;
      break;
    }
    if (offset > 64 * 1024 * 1024) {
      break;
    }
    offset += size;
  }
  if (boxes->moov_offset < 0 || boxes->mdat_offset < 0) {
    return QStringLiteral("样本不是 faststart（缺 moov 或 mdat）");
  }
  if (boxes->moov_offset > boxes->mdat_offset) {
    return QStringLiteral("样本 moov 在 mdat 之后（非 faststart）");
  }
  return QString();
}

// ---------------------------------------------------------------- 假下载器

class PartPublisher {
 public:
  PartPublisher(QString sample_path, QString part_path,
                std::shared_ptr<GrowingSource> source)
      : sample_path_(std::move(sample_path)),
        part_path_(std::move(part_path)),
        source_(std::move(source)) {}

  ~PartPublisher() { StopAndJoin(nullptr); }

  PartPublisher(const PartPublisher&) = delete;
  PartPublisher& operator=(const PartPublisher&) = delete;

  bool Start(std::int64_t target, std::int64_t burst_bytes, double bytes_per_sec,
             std::int64_t chunk_bytes, QString* error) {
    size_ = source_->Size();
    if (size_ <= 0) {
      if (error != nullptr) {
        *error = QStringLiteral("可增长数据源总大小未知");
      }
      return false;
    }
    sample_.setFileName(sample_path_);
    if (!sample_.open(QIODevice::ReadOnly)) {
      if (error != nullptr) {
        *error = QStringLiteral("无法读取样本：%1").arg(sample_path_);
      }
      return false;
    }
    const std::wstring wide = ToExtendedPath(part_path_).toStdWString();
    part_ = CreateFileW(wide.c_str(), GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (part_ == INVALID_HANDLE_VALUE) {
      sample_.close();
      if (error != nullptr) {
        *error = QStringLiteral("无法打开 .part 写入：%1").arg(part_path_);
      }
      return false;
    }
    LARGE_INTEGER existing{};
    if (GetFileSizeEx(part_, &existing) == FALSE || existing.QuadPart != 0) {
      CloseHandle(part_);
      part_ = INVALID_HANDLE_VALUE;
      sample_.close();
      if (error != nullptr) {
        *error = QStringLiteral(".part 非空，探针只从 0 开始：%1").arg(part_path_);
      }
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      target_ = ClampTarget(target);
      rate_ = bytes_per_sec > 0.0 ? bytes_per_sec : 0.0;
      chunk_ = chunk_bytes > 0 ? chunk_bytes : 64 * kKb;
      stopped_ = false;
    }

    const std::int64_t burst =
        std::min<std::int64_t>(burst_bytes, target_);
    std::int64_t written = 0;
    while (written < burst) {
      const std::int64_t chunk =
          std::min<std::int64_t>(chunk_, burst - written);
      if (!WriteChunk(chunk)) {
        CloseHandles();
        if (error != nullptr) {
          *error = error_;
        }
        return false;
      }
      written += chunk;
    }

    thread_ = std::thread([this] { Run(); });
    return true;
  }

  void Retarget(std::int64_t target, double bytes_per_sec,
                std::int64_t chunk_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    target_ = ClampTarget(target);
    rate_ = bytes_per_sec > 0.0 ? bytes_per_sec : 0.0;
    if (chunk_bytes > 0) {
      chunk_ = chunk_bytes;
    }
    cv_.notify_all();
  }

  // 幂等；可重复调用。join_ms 只统计第一次调用的耗时。
  void StopAndJoin(std::int64_t* join_ms) {
    const std::int64_t start = NowMs();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopped_) {
        if (join_ms != nullptr) {
          *join_ms = 0;
        }
        return;
      }
      stopped_ = true;
      cv_.notify_all();
    }
    if (thread_.joinable()) {
      thread_.join();
    }
    CloseHandles();
    if (join_ms != nullptr) {
      *join_ms = NowMs() - start;
    }
  }

  QString Error() const { return error_; }
  int OverFileViolations() const { return over_file_; }

 private:
  std::int64_t ClampTarget(std::int64_t target) const {
    if (target < 0) {
      return 0;
    }
    if (target > size_) {
      return size_;
    }
    return target;
  }

  bool WriteChunk(std::int64_t size) {
    if (size <= 0) {
      return true;
    }
    if (write_pos_ + size > size_) {
      size = size_ - write_pos_;
    }
    std::vector<std::uint8_t> buffer(static_cast<size_t>(size));
    const qint64 got =
        sample_.read(reinterpret_cast<char*>(buffer.data()), size);
    if (got != size) {
      error_ = QStringLiteral("读取样本失败：%1/%2").arg(got).arg(size);
      return false;
    }
    LARGE_INTEGER position{};
    position.QuadPart = write_pos_;
    if (SetFilePointerEx(part_, position, nullptr, FILE_BEGIN) == FALSE) {
      error_ = QStringLiteral("SetFilePointer 失败（Win32 %1）").arg(GetLastError());
      return false;
    }
    DWORD wrote = 0;
    if (WriteFile(part_, buffer.data(), static_cast<DWORD>(size), &wrote,
                  nullptr) == FALSE ||
        wrote != static_cast<DWORD>(size)) {
      error_ = QStringLiteral("写入 .part 失败（Win32 %1）").arg(GetLastError());
      source_->Fail(AVERROR(EIO));
      return false;
    }
    write_pos_ += size;
    source_->Publish(write_pos_);

    // Published 不得超前实际文件长度（known pitfall §10.6）。
    LARGE_INTEGER actual{};
    if (GetFileSizeEx(part_, &actual) != FALSE &&
        write_pos_ > actual.QuadPart) {
      ++over_file_;
      error_ = QStringLiteral("Published %1 超过文件长度 %2")
                   .arg(write_pos_)
                   .arg(actual.QuadPart);
      source_->Fail(AVERROR(EIO));
      return false;
    }
    return true;
  }

  void Run() {
    for (;;) {
      std::int64_t chunk = 0;
      double delay_s = 0.0;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&] { return stopped_ || write_pos_ < target_; });
        if (stopped_) {
          return;
        }
        if (write_pos_ >= target_) {
          continue;
        }
        chunk = std::min<std::int64_t>(chunk_, target_ - write_pos_);
        chunk = std::min<std::int64_t>(chunk, size_ - write_pos_);
        delay_s = rate_ > 0.0 ? static_cast<double>(chunk) / rate_ : 0.0;
      }
      if (chunk <= 0) {
        return;
      }
      if (!WriteChunk(chunk)) {
        return;
      }
      if (delay_s > 0.0) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::duration<double>(delay_s),
                     [&] { return stopped_; });
      }
    }
  }

  void CloseHandles() {
    if (part_ != INVALID_HANDLE_VALUE) {
      CloseHandle(part_);
      part_ = INVALID_HANDLE_VALUE;
    }
    if (sample_.isOpen()) {
      sample_.close();
    }
  }

  QString sample_path_;
  QString part_path_;
  std::shared_ptr<GrowingSource> source_;
  std::int64_t size_ = 0;
  QFile sample_;
  HANDLE part_ = INVALID_HANDLE_VALUE;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::int64_t target_ = 0;
  std::int64_t write_pos_ = 0;  // 只由 publisher 线程访问
  std::int64_t chunk_ = 64 * kKb;
  double rate_ = 0.0;
  bool stopped_ = false;
  QString error_;      // 线程写、StopAndJoin 后读
  int over_file_ = 0;  // 同上
};

// ---------------------------------------------------------------- 首帧

struct FirstFrameResult {
  bool ok = false;
  bool timed_out = false;
  QString error;
  double open_ms = 0.0;
  double first_frame_ms = 0.0;
  std::int64_t consumed_bytes = 0;
  int width = 0;
  int height = 0;
  double fps = 0.0;
  double pts_s = -1.0;
};

FirstFrameResult DecodeFirstFrame(const std::shared_ptr<GrowingSource>& source,
                                  const QString& part_path) {
  FirstFrameResult result;
  const std::int64_t start = NowMs();
  GrowingFormat format;
  if (!format.Open(source, part_path, &result.error)) {
    return result;
  }
  result.open_ms = static_cast<double>(NowMs() - start);

  AVFormatContext* ctx = format.context();
  const int video = av_find_best_stream(ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (video < 0) {
    result.error = QStringLiteral("没有视频流");
    return result;
  }
  AVStream* stream = ctx->streams[video];
  const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (codec == nullptr) {
    result.error = QStringLiteral("找不到视频解码器");
    return result;
  }
  AVCodecContext* decoder = avcodec_alloc_context3(codec);
  if (decoder == nullptr) {
    result.error = QStringLiteral("无法创建解码器上下文");
    return result;
  }
  bool ok = avcodec_parameters_to_context(decoder, stream->codecpar) >= 0;
  decoder->thread_count = 1;
  ok = ok && avcodec_open2(decoder, codec, nullptr) >= 0;
  if (!ok) {
    result.error = QStringLiteral("打开视频解码器失败");
    avcodec_free_context(&decoder);
    return result;
  }

  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  bool got = false;
  int last_error = 0;
  for (int packets = 0; packets < 300 && !got; ++packets) {
    last_error = av_read_frame(ctx, packet);
    if (last_error < 0) {
      break;
    }
    if (packet->stream_index == video) {
      const int send = avcodec_send_packet(decoder, packet);
      if (send >= 0) {
        while (avcodec_receive_frame(decoder, frame) == 0) {
          result.width = frame->width;
          result.height = frame->height;
          result.fps = av_q2d(stream->avg_frame_rate);
          if (frame->pts != AV_NOPTS_VALUE) {
            result.pts_s = frame->pts * av_q2d(stream->time_base);
          }
          got = true;
          av_frame_unref(frame);
          break;
        }
      }
    }
    av_packet_unref(packet);
  }

  result.ok = got;
  result.first_frame_ms = static_cast<double>(NowMs() - start);
  if (!got) {
    result.error = last_error < 0
                       ? lapian::online::AvErrorText(last_error)
                       : QStringLiteral("解码 300 个包仍未得到首帧");
  }
  if (format.cursor() != nullptr) {
    result.consumed_bytes = format.cursor()->Stats().max_read_end;
  }
  av_packet_free(&packet);
  av_frame_free(&frame);
  avcodec_free_context(&decoder);
  format.Close();
  return result;
}

// ---------------------------------------------------------------- 4 读者压测

struct ReaderOutcome {
  std::atomic<std::int64_t> ops{0};
  std::atomic<int> error{0};
  std::atomic<std::int64_t> last_progress_ms{0};
  std::atomic<bool> opened{false};
  lapian::online::GrowingCursorStats cursor_stats;
};

struct StressOutcome {
  int readers = 0;
  int seconds = 0;
  std::int64_t ops = 0;
  std::int64_t waits = 0;
  std::int64_t wait_ns = 0;
  std::int64_t timeouts = 0;
  int errors = 0;
  int opened = 0;
  bool deadlock = false;
  std::int64_t close_ms = 0;
  double peak_ws_mb = 0.0;
  double baseline_ws_mb = 0.0;
  std::int64_t published = 0;
};

StressOutcome RunStress(const std::shared_ptr<GrowingSource>& source,
                        const QString& part_path, int readers, int seconds,
                        PartPublisher* publisher) {
  StressOutcome outcome;
  outcome.readers = readers;
  outcome.seconds = seconds;

  std::array<ReaderOutcome, 8> outcomes;
  std::array<std::thread, 8> threads;
  outcome.baseline_ws_mb = WsMb();
  double peak = outcome.baseline_ws_mb;

  for (int i = 0; i < readers; ++i) {
    outcomes[i].ops = 0;
    outcomes[i].error = 0;
    outcomes[i].opened = false;
    outcomes[i].last_progress_ms = NowMs();
    threads[i] = std::thread([&, i] {
      ReaderOutcome& out = outcomes[i];
      GrowingFormat format;
      QString error;
      if (!format.Open(source, part_path, &error)) {
        out.error = -1;
        return;
      }
      out.opened = true;
      format.cursor()->SetWaitTimeoutMs(kStressWaitTimeoutMs);

      AVFormatContext* ctx = format.context();
      const int video =
          av_find_best_stream(ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
      if (video < 0) {
        out.error = -2;
        out.cursor_stats = format.cursor()->Stats();
        format.Close();
        return;
      }
      AVPacket* packet = av_packet_alloc();
      const double duration_s =
          ctx->duration > 0 ? static_cast<double>(ctx->duration) / 1e6 : 60.0;
      const double time_base = av_q2d(ctx->streams[video]->time_base);
      std::mt19937_64 rng(static_cast<std::uint64_t>(i) + 7919);
      while (!source->IsCancelled()) {
        int ret = 0;
        const std::uint64_t roll = rng();
        // 99.8% 顺序读：读者会真正读到发布前沿并阻塞等待（每包约 25KB，
        // 20% 的缓存余量约 1.6MB，需要几十个包）；0.2% seek 覆盖并发 seek。
        if ((roll % 1000) < 998) {
          ret = av_read_frame(ctx, packet);
          if (ret >= 0) {
            av_packet_unref(packet);
          } else if (ret == AVERROR_EOF) {
            ret = av_seek_frame(ctx, video, 0, AVSEEK_FLAG_BACKWARD);
            if (ret >= 0) {
              ret = 0;
            }
          }
        } else {
          const double published_fraction =
              source->Size() > 0
                  ? static_cast<double>(source->Published()) /
                        static_cast<double>(source->Size())
                  : 0.0;
          const double max_seconds = duration_s * published_fraction * 0.8;
          const double target_seconds =
              max_seconds * static_cast<double>(rng() % 1000) / 1000.0;
          const std::int64_t timestamp =
              time_base > 0.0
                  ? static_cast<std::int64_t>(target_seconds / time_base)
                  : 0;
          ret = av_seek_frame(ctx, video, timestamp, AVSEEK_FLAG_BACKWARD);
          if (ret >= 0) {
            ret = av_read_frame(ctx, packet);
            if (ret >= 0) {
              av_packet_unref(packet);
            }
          }
        }
        if (source->IsCancelled()) {
          break;
        }
        if (ret < 0) {
          if (ret == AVERROR_EOF) {
            continue;
          }
          out.error = ret;
          break;
        }
        ++out.ops;
        out.last_progress_ms = NowMs();
      }
      av_packet_free(&packet);
      out.cursor_stats = format.cursor()->Stats();
      format.Close();
    });
  }

  const std::int64_t stress_start = NowMs();
  std::int64_t next_check = stress_start + 10000;
  while (NowMs() - stress_start < static_cast<std::int64_t>(seconds) * 1000) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    peak = std::max(peak, WsMb());
    if (NowMs() >= next_check) {
      next_check += 10000;
      std::int64_t oldest = NowMs();
      int progress_readers = 0;
      for (int i = 0; i < readers; ++i) {
        if (outcomes[i].opened.load()) {
          ++progress_readers;
          oldest = std::min(oldest, outcomes[i].last_progress_ms.load());
        }
      }
      if (progress_readers == readers && NowMs() - oldest > 120000) {
        outcome.deadlock = true;
        break;
      }
    }
  }

  const std::int64_t close_start = NowMs();
  source->Cancel();
  for (int i = 0; i < readers; ++i) {
    if (threads[i].joinable()) {
      threads[i].join();
    }
  }
  if (publisher != nullptr) {
    publisher->StopAndJoin(nullptr);
  }
  outcome.close_ms = NowMs() - close_start;
  outcome.peak_ws_mb = peak;

  for (int i = 0; i < readers; ++i) {
    outcome.ops += outcomes[i].ops.load();
    if (outcomes[i].opened.load()) {
      ++outcome.opened;
    }
    if (outcomes[i].error.load() != 0) {
      ++outcome.errors;
    }
    outcome.waits += outcomes[i].cursor_stats.waits;
    outcome.wait_ns += outcomes[i].cursor_stats.wait_ns;
    outcome.timeouts += outcomes[i].cursor_stats.wait_timeouts;
  }
  outcome.published = source->Published();
  return outcome;
}

// ---------------------------------------------------------------- 独立小场景

bool TestRateWait(const QString& media, const QString& root,
                  double* wait_ms, double* expected_ms, QString* error) {
  const QString part = root + QStringLiteral("/rate.part");
  const std::int64_t raw_size = 32 * kMb;
  // .part 是连续前缀：起始必须是空文件（下载线程按顺序写入）。
  if (!CreateSizedFile(part, 0)) {
    *error = QStringLiteral("无法创建 rate.part");
    return false;
  }
  auto source = std::make_shared<GrowingSource>(raw_size);
  PartPublisher publisher(media, part, source);
  // 1MB 一块、4MB/s：Windows 定时器粒度 ~15.6ms，块太小会让“配置速率”明显偏高，
  // 这里用大块让实际速率接近 4MB/s，等待预期才可复现。
  if (!publisher.Start(raw_size, 4 * kMb, 4.0 * static_cast<double>(kMb), 1 * kMb,
                       error)) {
    return false;
  }

  QString cursor_error;
  auto cursor = GrowingCursor::Open(source, part, &cursor_error);
  if (!cursor) {
    *error = cursor_error;
    publisher.StopAndJoin(nullptr);
    return false;
  }
  const std::int64_t target = 12 * kMb;
  const std::int64_t published_at_request = source->Published();
  const std::int64_t expected =
      static_cast<std::int64_t>(
          static_cast<double>(target - published_at_request) /
          (4.0 * static_cast<double>(kMb)) * 1000.0);
  cursor->Seek(target, SEEK_SET);
  std::vector<std::uint8_t> buffer(64 * kKb);
  const std::int64_t start = NowMs();
  const int read = cursor->Read(buffer.data(), static_cast<int>(buffer.size()));
  const std::int64_t actual = NowMs() - start;

  std::vector<std::uint8_t> expected_bytes(64 * kKb);
  const bool content_ok =
      ReadSampleAt(media, target, expected_bytes.data(),
                   static_cast<int>(expected_bytes.size())) &&
      std::memcmp(buffer.data(), expected_bytes.data(), expected_bytes.size()) == 0;

  *wait_ms = static_cast<double>(actual);
  *expected_ms = static_cast<double>(expected);
  *error = QString();
  if (read != static_cast<int>(buffer.size())) {
    *error = QStringLiteral("未缓存读返回 %1（期望 %2）")
                 .arg(read)
                 .arg(buffer.size());
  } else if (!content_ok) {
    *error = QStringLiteral("未缓存读内容与样本偏移不一致");
  }
  cursor->Close();
  publisher.StopAndJoin(nullptr);
  return read == static_cast<int>(buffer.size()) && content_ok;
}

bool TestCancel(const QString& root, double* cancel_ms, double* open_cancel_ms,
                QString* error) {
  const QString part = root + QStringLiteral("/cancel.part");
  if (!CreateSizedFile(part, 1 * kMb)) {
    *error = QStringLiteral("无法创建 cancel.part");
    return false;
  }
  auto source = std::make_shared<GrowingSource>(1 * kMb);
  auto cursor = GrowingCursor::Open(source, part, error);
  if (!cursor) {
    return false;
  }
  std::atomic<int> read_result{0};
  std::thread reader([&] {
    std::uint8_t buffer[64] = {};
    read_result = cursor->Read(buffer, 64);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  const std::int64_t start = NowNs();
  source->Cancel();
  reader.join();
  *cancel_ms = static_cast<double>(NowNs() - start) / 1e6;
  const bool blocked = cursor->Stats().waits == 1;
  cursor->Close();

  // 打开阶段（FFmpeg 在等 moov）也要可取消。
  const QString open_part = root + QStringLiteral("/open-cancel.part");
  if (!CreateSizedFile(open_part, 2 * kMb)) {
    *error = QStringLiteral("无法创建 open-cancel.part");
    return false;
  }
  auto open_source = std::make_shared<GrowingSource>(2 * kMb);
  std::atomic<bool> done{false};
  std::atomic<bool> open_ok{true};
  QString open_error;
  std::thread opener([&] {
    GrowingFormat format;
    open_ok = format.Open(open_source, open_part, &open_error);
    format.Close();
    done = true;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const std::int64_t open_start = NowNs();
  open_source->Cancel();
  opener.join();
  *open_cancel_ms = static_cast<double>(NowNs() - open_start) / 1e6;
  const bool open_blocked = open_source->Stats().waits > 0;

  return read_result.load() == AVERROR_EXIT && blocked &&
         *open_cancel_ms <= 100.0 && !open_ok.load() && open_blocked;
}

bool TestEofEioExit(const QString& root, QString* error) {
  const QString path = root + QStringLiteral("/eof.part");
  if (!CreateSizedFile(path, 64 * kKb)) {
    *error = QStringLiteral("无法创建 eof.part");
    return false;
  }
  std::uint8_t buffer[64] = {};

  auto eof_source = std::make_shared<GrowingSource>(64 * kKb);
  eof_source->Publish(64 * kKb);
  auto eof_cursor = GrowingCursor::Open(eof_source, path, error);
  if (!eof_cursor ||
      eof_cursor->Read(buffer, sizeof(buffer)) != static_cast<int>(sizeof(buffer)) ||
      eof_cursor->Seek(64 * kKb, SEEK_SET) != 64 * kKb ||
      eof_cursor->Read(buffer, sizeof(buffer)) != AVERROR_EOF) {
    *error = QStringLiteral("EOF 语义不成立");
    return false;
  }
  eof_cursor->Close();

  auto eio_source = std::make_shared<GrowingSource>(64 * kKb);
  eio_source->Fail(AVERROR(EIO));
  auto eio_cursor = GrowingCursor::Open(eio_source, path, error);
  if (!eio_cursor || eio_cursor->Read(buffer, sizeof(buffer)) != AVERROR(EIO)) {
    *error = QStringLiteral("EIO 语义不成立（可能被当成 EOF）");
    return false;
  }
  eio_cursor->Close();

  auto exit_source = std::make_shared<GrowingSource>(64 * kKb);
  exit_source->Cancel();
  auto exit_cursor = GrowingCursor::Open(exit_source, path, error);
  if (!exit_cursor || exit_cursor->Read(buffer, sizeof(buffer)) != AVERROR_EXIT) {
    *error = QStringLiteral("EXIT 语义不成立");
    return false;
  }
  exit_cursor->Close();
  *error = QString();
  return true;
}

bool TestEviction(const QString& root, QString* error) {
  const QString cache_root = root + QStringLiteral("/evict-cache");
  CacheStore store(cache_root, 40 * kMb);
  const CacheKey locked{QStringLiteral("BV1BMnv6jE5S"), 2101, 64, 7};
  const CacheKey current{QStringLiteral("BV1mA411m7pS"), 2102, 64, 7};
  if (!store.Reserve(locked, 2 * kMb, error) ||
      !store.Reserve(current, 2 * kMb, error)) {
    return false;
  }
  QString locked_part;
  QString current_part;
  qint64 existing = 0;
  if (!store.OpenPart(locked, &locked_part, &existing, error) ||
      !store.OpenPart(current, &current_part, &existing, error)) {
    return false;
  }
  if (!WriteZeros(locked_part, 0, 2 * kMb) ||
      !WriteZeros(current_part, 0, 2 * kMb)) {
    *error = QStringLiteral("无法写入淘汰测试文件");
    return false;
  }
  if (!store.SetQuotaBytes(3 * kMb, error)) {
    return false;
  }

  auto source = std::make_shared<GrowingSource>(2 * kMb);
  source->Publish(2 * kMb);
  auto cursor = GrowingCursor::Open(source, locked_part, error);
  if (!cursor) {
    return false;
  }
  const bool busy = !store.EvictToQuota(current, error);
  const auto pending = store.Load(locked);
  const bool marked = pending.has_value() && pending->evict_pending;
  cursor->Close();
  const bool after_close = store.EvictToQuota(current, error);
  const bool removed = !QFileInfo::exists(locked_part);
  const bool current_kept = QFileInfo::exists(current_part);
  if (!(busy && marked && after_close && removed && current_kept)) {
    *error = QStringLiteral("淘汰 busy 语义不成立（busy=%1 marked=%2 after=%3 "
                            "removed=%4 current=%5）")
                 .arg(busy ? 1 : 0)
                 .arg(marked ? 1 : 0)
                 .arg(after_close ? 1 : 0)
                 .arg(removed ? 1 : 0)
                 .arg(current_kept ? 1 : 0);
    return false;
  }
  *error = QString();
  return true;
}

}  // namespace

int RunOnlineGrowingTest(const OnlineGrowingTestOptions& options) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const std::int64_t probe_start = NowMs();
  QString media = options.media;
  if (media.isEmpty()) {
    media = QStringLiteral("build/test-media/faststart-60s.mp4");
  }
  const QFileInfo media_info(media);
  if (!media_info.isFile() || media_info.size() <= 0) {
    std::printf("T8.4 growing_test [FAIL] sample-missing：%s\n",
                media.toUtf8().constData());
    std::printf("verdict=fail\n");
    return 2;
  }
  const std::int64_t sample_bytes = media_info.size();

  Mp4Boxes boxes;
  const QString box_error = ParseMp4Boxes(media, &boxes);
  if (!box_error.isEmpty()) {
    std::printf("T8.4 growing_test [FAIL] sample-faststart：%s\n",
                box_error.toUtf8().constData());
    std::printf("verdict=fail\n");
    return 2;
  }

  QTemporaryDir temp_dir;
  if (!temp_dir.isValid()) {
    std::printf("T8.4 growing_test [FAIL] temp-dir\n");
    std::printf("verdict=fail\n");
    return 2;
  }
  if (options.keep_root) {
    temp_dir.setAutoRemove(false);
  }
  QString root = options.root;
  if (root.isEmpty()) {
    root = temp_dir.path() + QStringLiteral("/t8.4-growing");
  }
  if (!QDir().mkpath(root)) {
    std::printf("T8.4 growing_test [FAIL] root-create：%s\n",
                root.toUtf8().constData());
    std::printf("verdict=fail\n");
    return 2;
  }

  const int readers = std::max(1, std::min(options.readers, 8));
  const int stress_seconds =
      options.minutes > 0 ? options.minutes * 60 : kDefaultStressSeconds;

  // 用真实 CacheStore 的 .part 作为唯一合法起点。
  CacheStore store(root + QStringLiteral("/cache"), sample_bytes * 2 + 64 * kMb);
  const CacheKey key{QStringLiteral("BV1BMnv6jE5S"), 42915922022LL, 64, 7};
  QString part_path;
  qint64 existing = 0;
  QString error;
  if (!store.Reserve(key, sample_bytes, &error) ||
      !store.OpenPart(key, &part_path, &existing, &error)) {
    std::printf("T8.4 growing_test [FAIL] cache-part：%s\n",
                error.toUtf8().constData());
    std::printf("verdict=fail\n");
    return 2;
  }
  Check("cache-open-part-empty", existing == 0, error);
  if (existing != 0) {
    std::printf("verdict=fail\n");
    return 1;
  }

  auto source = std::make_shared<GrowingSource>(sample_bytes);
  PartPublisher publisher(media, part_path, source);
  if (!publisher.Start(kFirstFrameBytes, kFirstFrameBytes, 0.0, 64 * kKb,
                       &error)) {
    std::printf("T8.4 growing_test [FAIL] publisher-start：%s\n",
                error.toUtf8().constData());
    std::printf("verdict=fail\n");
    return 1;
  }
  Check("first-frame-published-0.3mb",
        source->Published() == kFirstFrameBytes,
        Num(source->Published()));

  // DoD 1：只发布前 307200 字节，打开 + 解码首帧。
  FirstFrameResult first;
  std::atomic<bool> first_done{false};
  std::thread first_worker([&] {
    first = DecodeFirstFrame(source, part_path);
    first_done = true;
  });
  const std::int64_t first_deadline = NowMs() + 5000;
  while (!first_done.load() && NowMs() < first_deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!first_done.load()) {
    first.timed_out = true;
    source->Cancel();
  }
  first_worker.join();
  Check("first-frame-0.3mb", !first.timed_out && first.ok,
        first.timed_out ? QStringLiteral("等待 >5s")
                        : first.error);
  if (!first.ok) {
    publisher.StopAndJoin(nullptr);
    std::printf("first_frame_published=%lld\n",
                static_cast<long long>(kFirstFrameBytes));
    std::printf("first_frame_ok=0\n");
    std::printf("first_frame_ms=%s\n", Num(first.first_frame_ms).toUtf8().constData());
    std::printf("first_frame_consumed=%lld\n",
                static_cast<long long>(first.consumed_bytes));
    std::printf("verdict=fail\n");
    return 1;
  }

  // DoD 2/3：发布 8MB 前缀后做 100 次已缓存 seek；等待计数不增加。
  publisher.Retarget(kSeekPrefixBytes, 0.0, 64 * kKb);
  const GrowingWaitResult prefix_wait =
      source->WaitForTimeout(kSeekPrefixBytes - 1, 30000);
  Check("prefix-8mb-published", prefix_wait == GrowingWaitResult::Ready,
        Num(static_cast<std::int64_t>(prefix_wait)));

  GrowingFormat seek_format;
  if (!seek_format.Open(source, part_path, &error)) {
    Check("seek-format-open", false, error);
    publisher.StopAndJoin(nullptr);
    std::printf("verdict=fail\n");
    return 1;
  }
  AVIOContext* pb = seek_format.cursor()->avio();
  const std::int64_t waits_before = source->Stats().waits;
  int seek_misses = 0;
  const std::int64_t seek_start = NowMs();
  std::mt19937_64 rng(20261023);
  std::uint8_t seek_buffer[64] = {};
  for (int i = 0; i < 100; ++i) {
    const std::int64_t upper = kSeekPrefixBytes - 64 * kKb;
    const std::int64_t target =
        static_cast<std::int64_t>(rng() % static_cast<std::uint64_t>(upper));
    if (avio_seek(pb, target, SEEK_SET) != target) {
      ++seek_misses;
      continue;
    }
    if (avio_read(pb, seek_buffer, sizeof(seek_buffer)) !=
        static_cast<int>(sizeof(seek_buffer))) {
      ++seek_misses;
    }
  }
  const std::int64_t seek_ms = NowMs() - seek_start;
  const std::int64_t cached_seek_waits = source->Stats().waits - waits_before;
  Check("seek-100-landing-zero", seek_misses == 0, Num(seek_misses));
  Check("seek-100-cached-zero-wait", cached_seek_waits == 0,
        Num(cached_seek_waits));
  seek_format.Close();

  // DoD 4：未缓存等待 ≈ 剩余字节 / 速率。
  double wait_ms = 0.0;
  double expected_wait_ms = 0.0;
  QString rate_error;
  const bool rate_ok =
      TestRateWait(media, root, &wait_ms, &expected_wait_ms, &rate_error);
  const double wait_err_pct =
      expected_wait_ms > 0.0
          ? (wait_ms - expected_wait_ms) / expected_wait_ms * 100.0
          : 0.0;
  Check("uncached-wait-approx-rate",
        rate_ok && wait_err_pct <= 30.0 && wait_err_pct >= -30.0,
        rate_error.isEmpty() ? QStringLiteral("err=%1%").arg(Num(wait_err_pct))
                             : rate_error);

  // DoD 5：取消 ≤100ms（阻塞读 + 打开阶段）。
  double cancel_ms = 0.0;
  double open_cancel_ms = 0.0;
  QString cancel_error;
  const bool cancel_ok =
      TestCancel(root, &cancel_ms, &open_cancel_ms, &cancel_error);
  Check("cancel-under-100ms", cancel_ok && cancel_ms <= 100.0 &&
                                   open_cancel_ms <= 100.0,
        cancel_error.isEmpty()
            ? QStringLiteral("read=%1ms open=%2ms")
                  .arg(Num(cancel_ms))
                  .arg(Num(open_cancel_ms))
            : cancel_error);

  // DoD 6/7/8/9：4 读者压测（短版 10s；--minutes 10 跑真实 600s）。
  // 短版也把发布速率压到 256KB/s，保证读者真的会在未缓存区等待（不是全在缓存里跑）。
  const double remaining_bytes =
      static_cast<double>(sample_bytes - kSeekPrefixBytes);
  const double natural_rate = remaining_bytes / static_cast<double>(stress_seconds);
  const double publish_rate = std::min(natural_rate, 256.0 * 1024.0);
  publisher.Retarget(sample_bytes, publish_rate, 16 * kKb);
  const StressOutcome stress = RunStress(source, part_path, readers,
                                         stress_seconds, &publisher);
  const double ws_delta = std::max(0.0, stress.peak_ws_mb - stress.baseline_ws_mb);
  Check("stress-no-errors", stress.errors == 0, Num(stress.errors));
  Check("stress-no-lost-wakeups", stress.timeouts == 0, Num(stress.timeouts));
  Check("stress-no-deadlock", !stress.deadlock);
  Check("stress-readers-opened", stress.opened == readers, Num(stress.opened));
  Check("stress-had-waits", stress.waits > 0, Num(stress.waits));
  Check("close-under-1s", stress.close_ms <= 1000, Num(stress.close_ms));
  Check("memory-bounded-64mb", ws_delta <= 64.0, Num(ws_delta));
  Check("publisher-published-not-ahead",
        publisher.OverFileViolations() == 0, Num(publisher.OverFileViolations()));
  Check("publisher-no-error", publisher.Error().isEmpty(), publisher.Error());

  // DoD 9：等待 5s 的 CPU 时间 <0.2s（无 busy loop）。
  const QString busy_part = root + QStringLiteral("/busy.part");
  std::int64_t busy_cpu_ms = 0;
  int busy_notifications_before_cancel = 0;
  if (!CreateSizedFile(busy_part, 1 * kMb)) {
    Check("busy-loop-setup", false, QStringLiteral("无法创建 busy.part"));
  } else {
    auto busy_source = std::make_shared<GrowingSource>(1 * kMb);
    auto busy_cursor = GrowingCursor::Open(busy_source, busy_part, &error);
    if (!busy_cursor) {
      Check("busy-loop-setup", false, error);
    } else {
      std::atomic<int> busy_result{0};
      std::thread busy_reader([&] {
        std::uint8_t buffer[64] = {};
        busy_result = busy_cursor->Read(buffer, 64);
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      const std::uint64_t cpu_start = ProcessCpuMs();
      std::this_thread::sleep_for(std::chrono::seconds(5));
      const std::uint64_t cpu_end = ProcessCpuMs();
      busy_cpu_ms = static_cast<std::int64_t>(cpu_end - cpu_start);
      busy_notifications_before_cancel =
          static_cast<int>(busy_source->Stats().notifications);
      busy_source->Cancel();
      busy_reader.join();
      Check("busy-loop-no-spin", busy_cpu_ms < 200, Num(busy_cpu_ms));
      Check("busy-loop-was-waiting", busy_cursor->Stats().waits == 1,
            Num(busy_cursor->Stats().waits));
      Check("busy-loop-cancel-exit", busy_result.load() == AVERROR_EXIT,
            Num(busy_result.load()));
      busy_cursor->Close();
    }
  }

  // DoD 10：EOF / EIO / EXIT 三分。
  QString eof_error;
  const bool eof_ok = TestEofEioExit(root, &eof_error);
  Check("eof-eio-exit", eof_ok, eof_error);

  // DoD 11：持读者句柄时淘汰 busy + evict_pending；释放后可删。
  QString evict_error;
  const bool evict_ok = TestEviction(root, &evict_error);
  Check("evict-busy", evict_ok, evict_error);

  // ---------------------------------------------------------------- 汇总
  PrintKV("root", root);
  PrintKV("sample", media);
  PrintKV("sample_bytes", Num(sample_bytes));
  PrintKV("moov_offset", Num(boxes.moov_offset));
  PrintKV("moov_bytes", Num(boxes.moov_size));
  PrintKV("mdat_offset", Num(boxes.mdat_offset));
  PrintKV("first_frame_published", Num(kFirstFrameBytes));
  PrintKV("first_frame_ok", QStringLiteral("1"));
  PrintKV("first_frame_open_ms", Num(first.open_ms));
  PrintKV("first_frame_ms", Num(first.first_frame_ms));
  PrintKV("first_frame_consumed", Num(first.consumed_bytes));
  PrintKV("first_frame_size",
          QStringLiteral("%1x%2").arg(first.width).arg(first.height));
  PrintKV("first_frame_pts_s", Num(first.pts_s));
  PrintKV("seek_100_miss", Num(seek_misses));
  PrintKV("seek_100_ms", Num(seek_ms));
  PrintKV("cached_seek_waits", Num(cached_seek_waits));
  PrintKV("wait_ms", Num(wait_ms));
  PrintKV("wait_expected_ms", Num(expected_wait_ms));
  PrintKV("wait_err_pct", Num(wait_err_pct));
  PrintKV("cancel_ms", Num(cancel_ms));
  PrintKV("open_cancel_ms", Num(open_cancel_ms));
  PrintKV("readers", Num(readers));
  PrintKV("minutes", Num(options.minutes));
  PrintKV("stress_seconds", Num(stress_seconds));
  PrintKV("stress_ops", Num(stress.ops));
  PrintKV("stress_waits", Num(stress.waits));
  PrintKV("stress_wait_ms", Num(static_cast<double>(stress.wait_ns) / 1e6));
  PrintKV("stress_published", Num(stress.published));
  PrintKV("stress_errors", Num(stress.errors));
  PrintKV("deadlocks", stress.deadlock ? QStringLiteral("1") : QStringLiteral("0"));
  PrintKV("lost_wakeups", Num(stress.timeouts));
  PrintKV("busy_loops", busy_cpu_ms >= 200 ? QStringLiteral("1")
                                           : QStringLiteral("0"));
  PrintKV("busy_cpu_ms", Num(busy_cpu_ms));
  PrintKV("busy_notifications_before_cancel",
          Num(busy_notifications_before_cancel));
  PrintKV("close_ms", Num(stress.close_ms));
  PrintKV("baseline_ws_mb", Num(stress.baseline_ws_mb));
  PrintKV("peak_ws_mb", Num(stress.peak_ws_mb));
  PrintKV("ws_delta_mb", Num(ws_delta));
  PrintKV("eof_eio_exit", eof_ok ? QStringLiteral("pass") : QStringLiteral("fail"));
  PrintKV("evict_busy", evict_ok ? QStringLiteral("1") : QStringLiteral("0"));
  PrintKV("evict_after_close", evict_ok ? QStringLiteral("1") : QStringLiteral("0"));
  PrintKV("checks", Num(g_checks));
  PrintKV("failures", Num(g_failures));
  PrintKV("elapsed_ms", Num(NowMs() - probe_start));
  PrintKV("verdict", g_failures == 0 ? QStringLiteral("pass")
                                     : QStringLiteral("fail"));
  return g_failures == 0 ? 0 : 1;
}

}  // namespace lapian::app
