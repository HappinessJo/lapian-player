// T8.4 可增长读层单测（纯本地：临时目录 + 稀疏文件，不联网、不需要 SESSDATA、无 GUI）。
//
// 覆盖（对应 tasks-v2 T8.4 验收 / next-session-prompt §5）：
// - 总大小未知（<=0）拒绝进入可增长播放，错误可读；
// - Publish 单调、clamp、统计；已发布不得超前文件长度；
// - EOF / EIO / EXIT 三分（失败绝不伪装 EOF，已发布前缀仍可读）；
// - 已缓存读零等待；未缓存读阻塞后由 Publish 唤醒；
// - Cancel 唤醒阻塞读 ≤100ms，取消后 Publish 不再推进；
// - seek：SEEK_SET/CUR/END、AVSEEK_SIZE、边界 clamp、溢出拒绝、只改 pos 不阻塞；
// - 4 读者短压测：无死锁/丢唤醒，Cancel + join ≤1s，每读者独立 AVIO/句柄；
// - 4GiB−1 稀疏文件上的 int64 算术；
// - 与 CacheStore 的边界：OpenPart 路径可读、持读者句柄时淘汰 busy + evict_pending，
//   关闭后可删；读层内部不调用 CacheStore。
//
// 退出码：0 = 全部通过；1 = 有失败。

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/error.h>
}

#include "online/CacheStore.h"
#include "online/GrowingSource.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>

namespace {

using lapian::online::CacheKey;
using lapian::online::CacheStore;
using lapian::online::GrowingCursor;
using lapian::online::GrowingFormat;
using lapian::online::GrowingSource;
using lapian::online::GrowingSourceStats;
using lapian::online::GrowingWaitResult;

constexpr std::int64_t kKb = 1024;
constexpr std::int64_t kMb = 1024 * kKb;

int g_checks = 0;
int g_failures = 0;

using Clock = std::chrono::steady_clock;

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

void Check(bool condition, const std::string& what) {
  ++g_checks;
  if (condition) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n", what.c_str());
    ++g_failures;
  }
}

void CheckEqI64(std::int64_t actual, std::int64_t expected,
                const std::string& what) {
  ++g_checks;
  if (actual == expected) {
    std::printf("[ OK ] %s\n", what.c_str());
    return;
  }
  std::printf("[FAIL] %s\n  实际：%lld\n  期望：%lld\n", what.c_str(),
              static_cast<long long>(actual),
              static_cast<long long>(expected));
  ++g_failures;
}

void CheckLe(std::int64_t actual, std::int64_t max, const std::string& what) {
  ++g_checks;
  if (actual <= max) {
    std::printf("[ OK ] %s (%lld <= %lld)\n", what.c_str(),
                static_cast<long long>(actual), static_cast<long long>(max));
    return;
  }
  std::printf("[FAIL] %s\n  实际：%lld\n  期望最多：%lld\n", what.c_str(),
              static_cast<long long>(actual), static_cast<long long>(max));
  ++g_failures;
}

void CheckGe(std::int64_t actual, std::int64_t min, const std::string& what) {
  ++g_checks;
  if (actual >= min) {
    std::printf("[ OK ] %s (%lld >= %lld)\n", what.c_str(),
                static_cast<long long>(actual), static_cast<long long>(min));
    return;
  }
  std::printf("[FAIL] %s\n  实际：%lld\n  期望至少：%lld\n", what.c_str(),
              static_cast<long long>(actual), static_cast<long long>(min));
  ++g_failures;
}

void CheckContains(const QString& text, const QString& needle,
                   const std::string& what) {
  ++g_checks;
  if (text.contains(needle)) {
    std::printf("[ OK ] %s\n", what.c_str());
    return;
  }
  std::printf("[FAIL] %s\n  实际：%s\n  期望包含：%s\n", what.c_str(),
              text.toUtf8().constData(), needle.toUtf8().constData());
  ++g_failures;
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
  MarkSparse(path);  // 失败也继续：只影响物理占用
  if (!file.open(QIODevice::ReadWrite)) {
    return false;
  }
  const bool ok = file.resize(size);
  file.close();
  return ok;
}

// 在已有文件上写一段内容（share read|write、不带 delete，模拟下载线程）。
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
    const DWORD chunk = static_cast<DWORD>(
        remaining > 64 * kKb ? 64 * kKb : remaining);
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

bool WritePattern(const QString& path, std::int64_t offset, std::int64_t size) {
  std::vector<std::uint8_t> buffer(static_cast<size_t>(size));
  for (std::int64_t i = 0; i < size; ++i) {
    buffer[static_cast<size_t>(i)] =
        static_cast<std::uint8_t>(((offset + i) * 31 + 7) & 0xFF);
  }
  return WriteAt(path, offset, buffer.data(), size);
}

bool WriteZeros(const QString& path, std::int64_t offset, std::int64_t size) {
  static const std::uint8_t zeros[64 * 1024] = {};
  std::int64_t written = 0;
  while (written < size) {
    const std::int64_t remaining = size - written;
    const std::int64_t chunk =
        remaining > 64 * kKb ? 64 * kKb : remaining;
    if (!WriteAt(path, offset + written, zeros, chunk)) {
      return false;
    }
    written += chunk;
  }
  return true;
}

std::shared_ptr<GrowingSource> MakeSource(std::int64_t size) {
  return std::make_shared<GrowingSource>(size);
}

// ---------------------------------------------------------------- 总大小未知/打开错误

void TestInvalidAndOpenErrors() {
  QTemporaryDir temp;
  Check(temp.isValid(), "invalid-temp-dir");
  const QString empty = temp.path() + QStringLiteral("/empty.part");
  Check(CreateSizedFile(empty, 0), "invalid-create-empty-file");

  auto zero = MakeSource(0);
  Check(!zero->IsValid(), "invalid-size-zero-rejected");
  CheckContains(zero->InvalidReason(), QStringLiteral("总大小未知"),
                "invalid-size-error-readable");

  auto negative = MakeSource(-1);
  Check(!negative->IsValid(), "invalid-size-negative-rejected");

  QString error;
  auto bad_cursor = GrowingCursor::Open(zero, empty, &error);
  Check(bad_cursor == nullptr, "invalid-open-zero-returns-null");
  CheckContains(error, QStringLiteral("总大小未知"),
                "invalid-open-zero-error-readable");

  auto small = MakeSource(1024);
  auto missing = GrowingCursor::Open(
      small, temp.path() + QStringLiteral("/does-not-exist.part"), &error);
  Check(missing == nullptr, "invalid-open-missing-file");
  CheckContains(error, QStringLiteral("无法打开缓存文件"),
                "invalid-open-missing-error-readable");

  // Publish 必须先写盘：文件 0 字节却发布 1024 字节，打开器必须拒绝。
  small->Publish(1024);
  CheckEqI64(small->Published(), 1024, "invalid-published-before-write");
  auto ahead = GrowingCursor::Open(small, empty, &error);
  Check(ahead == nullptr, "invalid-published-ahead-rejected");
  CheckContains(error, QStringLiteral("Publish 必须先写盘"),
                "invalid-published-ahead-error-readable");
}

// ---------------------------------------------------------------- Publish/统计

void TestPublishStats() {
  auto source = MakeSource(1 * kMb);
  Check(source->IsValid(), "publish-source-valid");
  CheckEqI64(source->Size(), 1 * kMb, "publish-size");

  source->Publish(0);
  CheckEqI64(source->Published(), 0, "publish-zero");

  source->Publish(64 * kKb);
  CheckEqI64(source->Published(), 64 * kKb, "publish-first-prefix");

  source->Publish(32 * kKb);
  CheckEqI64(source->Published(), 64 * kKb, "publish-monotonic-ignores-older");

  source->Publish(2 * kMb);
  CheckEqI64(source->Published(), 1 * kMb, "publish-clamped-to-size");

  const GrowingSourceStats stats = source->Stats();
  CheckEqI64(stats.publish_calls, 4, "publish-calls-counted");
  CheckEqI64(stats.publish_events, 2, "publish-events-counted");
  CheckEqI64(stats.published, 1 * kMb, "publish-stats-published");
  Check(stats.publish_bytes_per_sec > 0.0, "publish-rate-positive");
  CheckEqI64(stats.live_cursors, 0, "publish-no-live-cursors");
}

// ---------------------------------------------------------------- 读/EOF

void TestReadAndEof() {
  QTemporaryDir temp;
  const QString path = temp.path() + QStringLiteral("/read.part");
  Check(CreateSizedFile(path, 128 * kKb), "read-create-file");
  Check(WritePattern(path, 0, 128 * kKb), "read-write-pattern");

  auto source = MakeSource(128 * kKb);
  source->Publish(128 * kKb);
  QString error;
  auto cursor = GrowingCursor::Open(source, path, &error);
  Check(cursor != nullptr, "read-cursor-open");
  if (!cursor) {
    return;
  }

  std::uint8_t buffer[4096] = {};
  const int first = cursor->Read(buffer, 1024);
  CheckEqI64(first, 1024, "read-first-1024");
  CheckEqI64(buffer[0], static_cast<std::int64_t>((0 * 31 + 7) & 0xFF),
             "read-first-byte-pattern");
  CheckEqI64(buffer[1023], static_cast<std::int64_t>((1023 * 31 + 7) & 0xFF),
             "read-last-byte-pattern");

  const std::int64_t last = 128 * kKb - 100;
  CheckEqI64(cursor->Seek(last, SEEK_SET), last, "read-seek-to-tail");
  const int tail = cursor->Read(buffer, 200);
  CheckEqI64(tail, 100, "read-tail-length");
  CheckEqI64(buffer[99],
             static_cast<std::int64_t>(((last + 99) * 31 + 7) & 0xFF),
             "read-tail-byte-pattern");
  CheckEqI64(cursor->Read(buffer, 16), AVERROR_EOF, "read-eof-after-size");

  CheckEqI64(cursor->Seek(128 * kKb, SEEK_SET), 128 * kKb, "read-seek-exact-size");
  CheckEqI64(cursor->Read(buffer, 16), AVERROR_EOF, "read-eof-at-size");

  const auto stats = cursor->Stats();
  CheckEqI64(stats.reads, 2, "read-stat-reads");
  CheckEqI64(stats.read_bytes, 1024 + 100, "read-stat-bytes");
  CheckEqI64(stats.max_read_end, 128 * kKb, "read-stat-max-end");
  cursor->Close();
  CheckEqI64(source->Stats().live_cursors, 0, "read-cursor-unregistered");
}

// ---------------------------------------------------------------- 等待/唤醒

void TestWaitWake() {
  QTemporaryDir temp;
  const QString path = temp.path() + QStringLiteral("/wait.part");
  Check(CreateSizedFile(path, 1 * kMb), "wait-create-file");

  auto source = MakeSource(1 * kMb);
  QString error;
  auto cursor = GrowingCursor::Open(source, path, &error);
  Check(cursor != nullptr, "wait-cursor-open");
  if (!cursor) {
    return;
  }

  std::atomic<int> read_result{0};
  std::atomic<std::int64_t> elapsed_ms{0};
  std::thread reader([&] {
    std::uint8_t buffer[64] = {};
    const std::int64_t start = NowMs();
    read_result = cursor->Read(buffer, 64);
    elapsed_ms = NowMs() - start;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  Check(WriteZeros(path, 0, 64 * kKb), "wait-write-bytes");
  source->Publish(64 * kKb);
  reader.join();

  CheckEqI64(read_result.load(), 64, "wait-read-woken");
  CheckGe(elapsed_ms.load(), 100, "wait-blocks-until-publish");
  CheckLe(elapsed_ms.load(), 3000, "wait-completes-after-publish");

  const GrowingSourceStats source_stats = source->Stats();
  CheckEqI64(source_stats.waits, 1, "wait-source-wait-count");
  CheckGe(source_stats.wait_ns, 100 * 1000 * 1000LL, "wait-source-wait-ns");
  const auto cursor_stats = cursor->Stats();
  CheckEqI64(cursor_stats.waits, 1, "wait-cursor-wait-count");
  Check(cursor_stats.first_wait_recorded, "wait-first-wait-recorded");
  CheckGe(cursor_stats.first_wait_ns, 100 * 1000 * 1000LL,
          "wait-first-wait-ns");
}

// ---------------------------------------------------------------- 已缓存读零等待

void TestCachedNoWait() {
  QTemporaryDir temp;
  const QString path = temp.path() + QStringLiteral("/cached.part");
  Check(CreateSizedFile(path, 1 * kMb), "cached-create-file");
  Check(WriteZeros(path, 0, 256 * kKb), "cached-write-prefix");

  auto source = MakeSource(1 * kMb);
  source->Publish(256 * kKb);
  QString error;
  auto cursor = GrowingCursor::Open(source, path, &error);
  Check(cursor != nullptr, "cached-cursor-open");
  if (!cursor) {
    return;
  }

  AVIOContext* pb = cursor->avio();
  std::uint8_t buffer[256] = {};
  const std::int64_t waits_before = source->Stats().waits;
  int misses = 0;
  for (int i = 0; i < 100; ++i) {
    const std::int64_t target = (i * 2500) % (240 * kKb);
    if (avio_seek(pb, target, SEEK_SET) != target) {
      ++misses;
      continue;
    }
    if (avio_read(pb, buffer, sizeof(buffer)) != 256) {
      ++misses;
    }
  }
  CheckEqI64(misses, 0, "cached-100-seeks-reads");
  CheckEqI64(source->Stats().waits - waits_before, 0,
             "cached-seek-zero-blocking");
  CheckEqI64(cursor->Stats().waits, 0, "cached-cursor-zero-waits");
}

// ---------------------------------------------------------------- seek 矩阵

void TestSeekMatrix() {
  QTemporaryDir temp;
  const QString path = temp.path() + QStringLiteral("/seek.part");
  Check(CreateSizedFile(path, 2 * kMb), "seek-create-file");
  Check(WriteZeros(path, 0, 1 * kMb), "seek-write-prefix");

  auto source = MakeSource(2 * kMb);
  source->Publish(1 * kMb);
  QString error;
  auto cursor = GrowingCursor::Open(source, path, &error);
  Check(cursor != nullptr, "seek-cursor-open");
  if (!cursor) {
    return;
  }
  AVIOContext* pb = cursor->avio();
  const std::int64_t waits_before = source->Stats().waits;

  CheckEqI64(avio_size(pb), 2 * kMb, "seek-avseek-size");
  CheckEqI64(avio_seek(pb, 123456, SEEK_SET), 123456, "seek-set");
  CheckEqI64(avio_seek(pb, 100, SEEK_CUR), 123556, "seek-cur");
  // avio_seek() 本身只接受 SET/CUR（FFmpeg 会把 SEEK_END 直接当 EINVAL）；
  // END 语义由我们给 avio_size() 的 fallback / 直接 callback 覆盖。
  CheckEqI64(avio_seek(pb, 3 * kMb, SEEK_SET), AVERROR(EINVAL),
             "seek-past-end-einval");
  CheckEqI64(avio_seek(pb, -1, SEEK_SET), AVERROR(EINVAL),
             "seek-negative-einval");
  CheckEqI64(avio_seek(pb, 2 * kMb, SEEK_SET), 2 * kMb, "seek-exact-end");

  // 直接调用 callback：END / 非法 whence / int64 溢出。
  Check(cursor->Seek(0, 0x7F) < 0, "seek-invalid-whence");
  CheckEqI64(cursor->Seek(0, SEEK_END), 2 * kMb, "seek-end-zero");
  CheckEqI64(cursor->Seek(-100, SEEK_END), 2 * kMb - 100, "seek-end-offset");
  Check(cursor->Seek(std::numeric_limits<std::int64_t>::max(), SEEK_CUR) < 0,
        "seek-overflow-rejected");
  Check(cursor->Seek(-2 * kMb - 1, SEEK_END) < 0, "seek-end-negative-rejected");

  CheckEqI64(source->Stats().waits - waits_before, 0,
             "seek-does-not-block");
}

// ---------------------------------------------------------------- 取消

void TestCancel() {
  QTemporaryDir temp;
  const QString path = temp.path() + QStringLiteral("/cancel.part");
  Check(CreateSizedFile(path, 1 * kMb), "cancel-create-file");

  auto source = MakeSource(1 * kMb);
  QString error;
  auto cursor = GrowingCursor::Open(source, path, &error);
  Check(cursor != nullptr, "cancel-cursor-open");
  if (!cursor) {
    return;
  }

  std::atomic<int> read_result{0};
  std::thread reader([&] {
    std::uint8_t buffer[64] = {};
    read_result = cursor->Read(buffer, 64);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  const std::int64_t cancel_start = NowMs();
  source->Cancel();
  reader.join();
  const std::int64_t cancel_ms = NowMs() - cancel_start;

  CheckEqI64(read_result.load(), AVERROR_EXIT, "cancel-read-exit");
  CheckEqI64(cursor->Stats().waits, 1, "cancel-reader-was-waiting");
  CheckLe(cancel_ms, 100, "cancel-under-100ms");
  const GrowingSourceStats stats = source->Stats();
  Check(stats.cancel_latency_ns > 0, "cancel-latency-recorded");
  CheckLe(stats.cancel_latency_ns, 100 * 1000 * 1000LL,
          "cancel-latency-under-100ms");
  CheckEqI64(stats.active_waiters, 0, "cancel-no-active-waiters");

  std::uint8_t buffer[64] = {};
  CheckEqI64(cursor->Read(buffer, 64), AVERROR_EXIT, "cancel-terminal-read");

  source->Publish(64 * kKb);
  CheckEqI64(source->Published(), 0, "cancel-publish-noop");
  CheckEqI64(static_cast<std::int64_t>(source->WaitFor(0)),
             static_cast<std::int64_t>(GrowingWaitResult::Cancelled),
             "cancel-waitfor-cancelled");
  CheckEqI64(static_cast<std::int64_t>(source->WaitForTimeout(0, 50)),
             static_cast<std::int64_t>(GrowingWaitResult::Cancelled),
             "cancel-waitfor-timeout-cancelled");
  source->Cancel();  // 幂等
  Check(source->IsCancelled(), "cancel-idempotent");
}

// ---------------------------------------------------------------- 失败 ≠ EOF

void TestFailSemantics() {
  QTemporaryDir temp;
  const QString path = temp.path() + QStringLiteral("/fail.part");
  Check(CreateSizedFile(path, 1 * kMb), "fail-create-file");
  Check(WriteZeros(path, 0, 128 * kKb), "fail-write-prefix");

  auto source = MakeSource(1 * kMb);
  source->Publish(128 * kKb);
  QString error;
  auto cursor = GrowingCursor::Open(source, path, &error);
  Check(cursor != nullptr, "fail-cursor-open");
  if (!cursor) {
    return;
  }

  std::atomic<int> blocked_result{0};
  std::atomic<std::int64_t> seek_result{0};
  std::thread waiter([&] {
    std::uint8_t buffer[64] = {};
    seek_result = cursor->Seek(512 * kKb, SEEK_SET);
    blocked_result = cursor->Read(buffer, 64);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  source->Fail(AVERROR(EIO));
  waiter.join();

  CheckEqI64(seek_result.load(), 512 * kKb, "fail-seek-uncached");
  Check(source->HasFailed(), "fail-state");
  CheckEqI64(source->ErrorCode(), AVERROR(EIO), "fail-error-code");
  CheckEqI64(blocked_result.load(), AVERROR(EIO), "fail-wait-eio-not-eof");

  std::uint8_t buffer[64] = {};
  CheckEqI64(cursor->Seek(0, SEEK_SET), 0, "fail-seek-cached");
  CheckEqI64(cursor->Read(buffer, 64), 64, "fail-cached-prefix-still-readable");
  CheckEqI64(cursor->Seek(128 * kKb, SEEK_SET), 128 * kKb,
             "fail-seek-at-published");
  CheckEqI64(cursor->Read(buffer, 64), AVERROR(EIO), "fail-at-published-eio");
  CheckEqI64(cursor->Seek(1 * kMb, SEEK_SET), 1 * kMb, "fail-seek-size");
  CheckEqI64(cursor->Read(buffer, 64), AVERROR_EOF, "fail-at-size-still-eof");

  auto normalized = MakeSource(1 * kMb);
  normalized->Fail(0);
  Check(normalized->HasFailed(), "fail-zero-normalized-state");
  CheckEqI64(normalized->ErrorCode(), AVERROR(EIO),
             "fail-zero-normalized-eio");
  CheckEqI64(static_cast<std::int64_t>(normalized->WaitFor(0)),
             static_cast<std::int64_t>(GrowingWaitResult::Failed),
             "fail-wait-result");
}

// ---------------------------------------------------------------- 4 读者短压测

void TestFourReaders() {
  QTemporaryDir temp;
  const QString path = temp.path() + QStringLiteral("/multi.part");
  constexpr std::int64_t kFileBytes = 2 * kMb;
  Check(CreateSizedFile(path, kFileBytes), "multi-create-file");

  auto source = MakeSource(kFileBytes);
  const std::int64_t wait_timeout_ms = 5000;

  std::atomic<bool> stop{false};
  std::atomic<bool> publish_error{false};
  std::int64_t published_by_writer = 0;

  std::thread publisher([&] {
    static const std::uint8_t zeros[64 * 1024] = {};
    constexpr std::int64_t kChunk = 64 * kKb;
    while (!stop.load() && published_by_writer < kFileBytes &&
           !source->IsCancelled()) {
      const std::int64_t remaining = kFileBytes - published_by_writer;
      const std::int64_t chunk = remaining > kChunk ? kChunk : remaining;
      if (!WriteAt(path, published_by_writer, zeros, chunk)) {
        publish_error = true;
        source->Fail(AVERROR(EIO));
        return;
      }
      published_by_writer += chunk;
      source->Publish(published_by_writer);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });

  constexpr int kReaders = 4;
  std::vector<std::thread> readers;
  std::array<std::atomic<std::int64_t>, kReaders> ops;
  std::array<std::atomic<int>, kReaders> errors;
  for (int i = 0; i < kReaders; ++i) {
    ops[i] = 0;
    errors[i] = 0;
    readers.emplace_back([&, i] {
      auto cursor = GrowingCursor::Open(source, path, nullptr);
      if (!cursor) {
        errors[i] = 1000;
        return;
      }
      cursor->SetWaitTimeoutMs(wait_timeout_ms);
      AVIOContext* pb = cursor->avio();
      std::mt19937_64 rng(static_cast<std::uint64_t>(i) + 12345);
      std::uint8_t buffer[64 * 1024] = {};
      while (!source->IsCancelled()) {
        const std::int64_t published = source->Published();
        const std::int64_t upper =
            std::min<std::int64_t>(published + 64 * kKb, kFileBytes - 64 * kKb);
        const std::int64_t target =
            upper > 0 ? static_cast<std::int64_t>(rng() % static_cast<std::uint64_t>(upper))
                      : 0;
        if (avio_seek(pb, target, SEEK_SET) != target) {
          errors[i] = 2000;
          return;
        }
        const int read = avio_read(pb, buffer, sizeof(buffer));
        if (read < 0) {
          if (read != AVERROR_EXIT) {
            errors[i] = read;
          }
          return;
        }
        ++ops[i];
      }
    });
  }

  std::this_thread::sleep_for(std::chrono::seconds(3));
  const std::int64_t close_start = NowMs();
  source->Cancel();
  for (auto& reader : readers) {
    reader.join();
  }
  const std::int64_t close_ms = NowMs() - close_start;
  stop = true;
  publisher.join();

  Check(!publish_error.load(), "multi-publisher-no-error");
  CheckLe(close_ms, 1000, "multi-close-under-1s");
  std::int64_t total_ops = 0;
  for (int i = 0; i < kReaders; ++i) {
    CheckEqI64(errors[i].load(), 0, "multi-reader-no-error-" + std::to_string(i));
    CheckGe(ops[i].load(), 1, "multi-reader-progress-" + std::to_string(i));
    total_ops += ops[i].load();
  }
  CheckGe(total_ops, 20, "multi-total-ops");
  const GrowingSourceStats stats = source->Stats();
  CheckEqI64(stats.timeouts, 0, "multi-no-lost-wakeups");
  CheckEqI64(stats.active_waiters, 0, "multi-no-active-waiters");
  Check(stats.waits > 0, "multi-had-blocking-waits");
  CheckGe(stats.published, 1 * kMb, "multi-published-enough");
}

// ---------------------------------------------------------------- 4GiB−1 稀疏文件

void TestSparse4GiB() {
  QTemporaryDir temp;
  const QString path = temp.path() + QStringLiteral("/sparse.part");
  const std::int64_t size = 4LL * 1024 * 1024 * 1024 - 1;
  Check(CreateSizedFile(path, size), "sparse-create-4gib-minus-1");
  const QFileInfo info(path);
  CheckEqI64(info.size(), size, "sparse-logical-size");

  auto source = MakeSource(size);
  QString error;
  auto cursor = GrowingCursor::Open(source, path, &error);
  Check(cursor != nullptr, "sparse-cursor-open");
  if (!cursor) {
    return;
  }
  AVIOContext* pb = cursor->avio();
  CheckEqI64(avio_size(pb), size, "sparse-avseek-size-int64");
  // 只调用 seek callback（不在 4GiB 稀疏文件上做会触发 fill_buffer 的 avio_seek）。
  CheckEqI64(cursor->Seek(size - 2, SEEK_SET), size - 2,
             "sparse-seek-near-end");
  CheckEqI64(cursor->Seek(-1, SEEK_CUR), size - 3, "sparse-seek-cur-int64");
  CheckEqI64(cursor->Seek(0, SEEK_END), size, "sparse-seek-end");
  CheckEqI64(cursor->Seek(size + 100, SEEK_SET), AVERROR(EINVAL),
             "sparse-seek-past-end-einval");
  CheckEqI64(cursor->Seek(-size - 1, SEEK_END), AVERROR(EINVAL),
             "sparse-seek-negative-einval");
  Check(cursor->Seek(std::numeric_limits<std::int64_t>::max(), SEEK_CUR) < 0,
        "sparse-overflow-rejected");
  CheckEqI64(cursor->Seek(size, SEEK_SET), size, "sparse-seek-exact-end");

  std::uint8_t buffer[8] = {};
  CheckEqI64(cursor->Read(buffer, sizeof(buffer)), AVERROR_EOF,
             "sparse-read-at-size-eof");
  cursor->Close();
}

// ---------------------------------------------------------------- CacheStore 边界

CacheKey Key(char letter, std::int64_t cid) {
  return CacheKey{QStringLiteral("BV1") + QString(9, QLatin1Char(letter)), cid,
                  64, 7};
}

void TestCacheStoreIntegration() {
  QTemporaryDir temp;
  const QString root = temp.path() + QStringLiteral("/cache");
  CacheStore store(root, 64 * kMb);
  const CacheKey key = Key('A', 1001);

  QString error;
  Check(store.Reserve(key, 64 * kKb, &error), "cache-reserve");
  QString part_path;
  qint64 existing = -1;
  Check(store.OpenPart(key, &part_path, &existing, &error), "cache-open-part");
  CheckEqI64(existing, 0, "cache-open-part-existing-zero");
  Check(WritePattern(part_path, 0, 64 * kKb), "cache-write-part");

  auto source = MakeSource(64 * kKb);
  source->Publish(64 * kKb);
  auto cursor = GrowingCursor::Open(source, part_path, &error);
  Check(cursor != nullptr, "cache-growing-cursor-open");
  if (!cursor) {
    return;
  }
  std::uint8_t buffer[1024] = {};
  CheckEqI64(cursor->Read(buffer, sizeof(buffer)), 1024, "cache-read-part");
  CheckEqI64(buffer[0], 7, "cache-read-part-pattern");
  CheckEqI64(cursor->Seek(64 * kKb, SEEK_SET), 64 * kKb, "cache-seek-end");
  CheckEqI64(cursor->Read(buffer, 16), AVERROR_EOF, "cache-read-eof");
  cursor->Close();

  Check(store.MarkComplete(key, &error), "cache-mark-complete");
  QString mp4_path;
  Check(store.OpenComplete(key, &mp4_path, &error), "cache-open-complete");
  Check(QFileInfo::exists(mp4_path), "cache-mp4-exists");
  const auto entry = store.Load(key);
  Check(entry.has_value() && entry->complete, "cache-entry-complete");
}

void TestEvictionBusy() {
  QTemporaryDir temp;
  const QString root = temp.path() + QStringLiteral("/cache");
  CacheStore store(root, 40 * kMb);
  const CacheKey locked = Key('B', 2002);
  const CacheKey current = Key('C', 3003);
  QString error;

  Check(store.Reserve(locked, 2 * kMb, &error), "evict-reserve-locked");
  Check(store.Reserve(current, 2 * kMb, &error), "evict-reserve-current");
  QString locked_part;
  QString current_part;
  qint64 existing = 0;
  Check(store.OpenPart(locked, &locked_part, &existing, &error),
        "evict-open-part-locked");
  Check(store.OpenPart(current, &current_part, &existing, &error),
        "evict-open-part-current");
  Check(WriteZeros(locked_part, 0, 2 * kMb), "evict-write-locked");
  Check(WriteZeros(current_part, 0, 2 * kMb), "evict-write-current");
  Check(store.SetQuotaBytes(3 * kMb, &error), "evict-set-quota");

  auto source = MakeSource(2 * kMb);
  source->Publish(2 * kMb);
  auto cursor = GrowingCursor::Open(source, locked_part, &error);
  Check(cursor != nullptr, "evict-reader-open");
  if (!cursor) {
    return;
  }

  Check(!store.EvictToQuota(current, &error), "evict-busy-returns-false");
  CheckContains(error, QStringLiteral("正被读取"), "evict-busy-error-readable");
  const auto pending = store.Load(locked);
  Check(pending.has_value() && pending->evict_pending,
        "evict-busy-marked-pending");
  Check(QFileInfo::exists(locked_part), "evict-busy-keeps-file");

  cursor->Close();
  Check(store.EvictToQuota(current, &error), "evict-after-close-succeeds");
  Check(!QFileInfo::exists(locked_part), "evict-after-close-removes-file");
  Check(QFileInfo::exists(current_part), "evict-keeps-current");
  CheckLe(store.UsageBytes(), 3 * kMb, "evict-usage-under-quota");
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("T8.4 growing_source_test\n");
  TestInvalidAndOpenErrors();
  TestPublishStats();
  TestReadAndEof();
  TestWaitWake();
  TestCachedNoWait();
  TestSeekMatrix();
  TestCancel();
  TestFailSemantics();
  TestFourReaders();
  TestSparse4GiB();
  TestCacheStoreIntegration();
  TestEvictionBusy();

  std::printf("checks=%d failures=%d\n", g_checks, g_failures);
  if (g_failures == 0) {
    std::printf("T8.4 growing_source_test: all checks passed\n");
    return 0;
  }
  std::printf("T8.4 growing_source_test: %d failure(s)\n", g_failures);
  return 1;
}
