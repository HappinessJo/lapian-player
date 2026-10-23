#pragma once

// T8.4 可增长读层（plan §12.4 / ADR-007 D3）。
//
// 目标：让 FFmpeg 在“边下边读”的 `.part` 上打开、seek、取消。
// - `GrowingSource`：已知总大小 + 已发布连续前缀 + 条件变量；**不创建线程**。
// - `GrowingCursor`：每个读者独立的 `pos`、只读文件句柄与 `AVIOContext`（64KB）。
// - `GrowingFormat`：用上面的 cursor 打开 `AVFormatContext`（`AVFMT_FLAG_CUSTOM_IO`）。
//
// 线程/锁约定：
// - 一个 `GrowingSource` 可被多个 reader 共享；每个 reader 有自己的 cursor/句柄/AVIO，
//   cursor 只允许在它所属的线程使用（FFmpeg 上下文同理）。
// - `GrowingSource` 内部只有一把 mutex + condition_variable；锁内不回调任何上层
//   （不碰 CacheStore、不做 I/O）。
// - 锁顺序：OnlineSession → CacheStore → GrowingSource；读层内部不调用 CacheStore。
// - 等待一律 `condition_variable::wait`（带谓词），不用 sleep/spin 轮询。
//
// 错误语义（plan §12.4.1）：
// - `pos >= Size()` → `AVERROR_EOF`；
// - 读到未发布区域 → 阻塞等待；
// - `Cancel()` → 所有阻塞读立即 `AVERROR_EXIT`（取消是终态）；
// - `Fail()` → 等待中的读 `AVERROR(EIO)`（**绝不能返回 EOF**），已发布前缀仍可读。
//
// 范围：T8.4 只做读层；HTTP/下载（T8.5）、MediaSession/UI 集成（T8.6）不在本文件。

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

#include <QString>

struct AVFormatContext;
struct AVIOContext;

namespace lapian::online {

// 64KB AVIO 缓冲（S2 spike 与 plan §12.4 的取值）。
constexpr int kGrowingAvioBufferBytes = 64 * 1024;

enum class GrowingWaitResult {
  Ready,      // offset < Published（无需等待或已被唤醒并满足）
  Cancelled,  // 源已 Cancel（AVERROR_EXIT）
  Failed,     // 下载不可恢复失败（读层映射为 AVERROR(EIO)）
  TimedOut,   // 仅诊断用（WaitForTimeout）；产品读路径默认无限等待
};

// 读层聚合统计（锁内快照；字段只增不减）。
struct GrowingSourceStats {
  std::int64_t size = 0;
  std::int64_t published = 0;
  std::int64_t publish_calls = 0;   // 调用 Publish() 的次数（含无效/重复）
  std::int64_t publish_events = 0;  // 实际推进 published 的次数
  std::int64_t waits = 0;           // 真正进入等待的次数（已满足不计）
  std::int64_t wait_ns = 0;         // 累计等待
  std::int64_t timeouts = 0;        // 带超时等待超时次数（丢唤醒/死锁诊断）
  std::int64_t notifications = 0;   // Publish/Fail/Cancel 触发的 notify_all 次数
  std::int64_t cancel_latency_ns = 0;  // 取消后阻塞读退出的最大观测延迟
  double publish_bytes_per_sec = 0.0;  // published / (最后一次发布 - 构造)
  std::int64_t first_publish_ns = 0;
  std::int64_t last_publish_ns = 0;
  int active_waiters = 0;
  int live_cursors = 0;
  bool cancelled = false;
  bool failed = false;
  int error_code = 0;  // Fail() 传进来的 FFmpeg 错误码（负数）
};

// 单读者统计（只有 cursor 所属线程写；跨线程读请先 join）。
struct GrowingCursorStats {
  std::int64_t reads = 0;
  std::int64_t read_bytes = 0;
  std::int64_t seeks = 0;
  std::int64_t waits = 0;       // 该 reader 在未发布区阻塞的次数
  std::int64_t wait_ns = 0;
  std::int64_t seek_waits = 0;  // seek 到未缓存区之后的第一次阻塞读
  std::int64_t seek_wait_ns = 0;
  std::int64_t first_wait_ns = 0;  // 首帧/首次阻塞等待（打开时的 probe 也算）
  std::int64_t max_read_end = 0;   // 读过的最大文件偏移（证据：首帧实际消耗）
  std::int64_t wait_timeouts = 0;  // 仅 SetWaitTimeoutMs>0 时可能非 0
  bool first_wait_recorded = false;
};

// 可增长数据源：已知总大小；Published() 是已发布连续前缀（MVP 无空洞）。
// 构造后若 Size()<=0 则 IsValid()==false，打开器必须拒绝进入可增长播放。
class GrowingSource {
 public:
  explicit GrowingSource(std::int64_t total_size);
  ~GrowingSource();

  GrowingSource(const GrowingSource&) = delete;
  GrowingSource& operator=(const GrowingSource&) = delete;

  bool IsValid() const;
  // 不可用时的可读原因；可用时为空。
  QString InvalidReason() const;

  std::int64_t Size() const;
  std::int64_t Published() const;

  // 阻塞到 offset < Published / 取消 / 失败。offset < 0 视为已满足；
  // offset >= Size() 立即返回 Ready（调用方先判 EOF，不在这里伪装 EOF）。
  GrowingWaitResult WaitFor(std::int64_t offset);
  // 诊断版本（测试/压测看门狗）：超时返回 TimedOut，并计入 stats.timeouts。
  GrowingWaitResult WaitForTimeout(std::int64_t offset, std::int64_t timeout_ms);

  // 下载线程在**写盘后**调用；单调递增，超过 Size() 会被 clamp。
  // 已 Cancel/Fail 后调用不再推进 published（也不会唤醒等待者）。
  void Publish(std::int64_t published_bytes);

  // 下载不可恢复失败：唤醒所有等待者；error_code 传 FFmpeg 负数错误（0 → EIO）。
  void Fail(int av_error);

  // 关窗/换片：唤醒所有等待者；重复调用幂等。
  void Cancel();

  bool IsCancelled() const;
  bool HasFailed() const;
  int ErrorCode() const;

  GrowingSourceStats Stats() const;

 private:
  friend class GrowingCursor;

  GrowingWaitResult WaitForImpl(std::int64_t offset, std::int64_t timeout_ms);
  void RegisterCursor();
  void UnregisterCursor();

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  const std::int64_t size_ = 0;
  const QString invalid_reason_;
  std::int64_t published_ = 0;
  bool cancelled_ = false;
  bool failed_ = false;
  int error_code_ = 0;
  std::int64_t created_ns_ = 0;
  std::int64_t first_publish_ns_ = 0;
  std::int64_t last_publish_ns_ = 0;
  std::int64_t cancel_ns_ = 0;
  std::int64_t publish_calls_ = 0;
  std::int64_t publish_events_ = 0;
  std::int64_t waits_ = 0;
  std::int64_t wait_ns_ = 0;
  std::int64_t timeouts_ = 0;
  std::int64_t notifications_ = 0;
  std::int64_t cancel_latency_ns_ = 0;
  int active_waiters_ = 0;
  int live_cursors_ = 0;
};

// 单个读者：独立 pos + 只读句柄 + AVIOContext。
// 打开 .part 时用 FILE_SHARE_READ|FILE_SHARE_WRITE、**不带 FILE_SHARE_DELETE**；
// 这样 CacheStore 淘汰正在读的条目会得到 busy + evict_pending，而不是删掉正在读的文件。
class GrowingCursor {
 public:
  // source 必须 Size()>0；file_path 是 CacheStore::OpenPart 返回的 `.part` 路径。
  // 失败返回 nullptr + 可读错误（不抛异常）。
  static std::unique_ptr<GrowingCursor> Open(std::shared_ptr<GrowingSource> source,
                                             const QString& file_path, QString* error);
  ~GrowingCursor();

  GrowingCursor(const GrowingCursor&) = delete;
  GrowingCursor& operator=(const GrowingCursor&) = delete;

  AVIOContext* avio() const;
  std::int64_t Pos() const;

  // FFmpeg AVIO 回调语义：支持 SEEK_SET/CUR/END 与 AVSEEK_SIZE；只改 pos，不触发网络。
  // 目标必须落在 [0, Size()]；越界/溢出/非法 whence 返回 AVERROR(EINVAL)（不静默 clamp）。
  std::int64_t Seek(std::int64_t offset, int whence);

  // 读未发布区会阻塞；返回 AVERROR_EOF/AVERROR_EXIT/AVERROR(EIO)/AVERROR(ETIMEDOUT)。
  int Read(std::uint8_t* buffer, int buffer_size);

  // 仅诊断：>0 时阻塞读使用带超时等待（默认 0 = 无限等待，产品路径）。
  void SetWaitTimeoutMs(int timeout_ms);
  int WaitTimeoutMs() const;

  // T8.6：单读者中断（不取消共享 GrowingSource）。T8.6 的异步 seek 在用户改目标时
  // 用它唤醒“卡在未缓存区”的解码线程；Read 在等待循环中每 25ms 检查一次。
  // 中断后 cursor 应被关闭/丢弃，不能继续在同一 AVFormatContext 上解码。
  void SetInterruptFlag(std::shared_ptr<std::atomic<bool>> flag);
  bool IsInterrupted() const;

  GrowingCursorStats Stats() const;

  // 释放 AVIO + 文件句柄；析构自动调用；重复调用安全。
  void Close();

 private:
  GrowingCursor(std::shared_ptr<GrowingSource> source, std::intptr_t handle,
                QString path);
  static int ReadThunk(void* opaque, std::uint8_t* buffer, int buffer_size);
  static std::int64_t SeekThunk(void* opaque, std::int64_t offset, int whence);

  std::shared_ptr<GrowingSource> source_;
  std::intptr_t handle_ = -1;  // HANDLE；头文件不引入 windows.h
  QString path_;
  AVIOContext* avio_ = nullptr;
  std::int64_t pos_ = 0;
  int wait_timeout_ms_ = 0;
  std::shared_ptr<std::atomic<bool>> interrupt_;
  bool after_uncached_seek_ = false;
  GrowingCursorStats stats_;
};

// 用 GrowingCursor 打开 AVFormatContext（AVFMT_FLAG_CUSTOM_IO，pb 不交给 FFmpeg 释放）。
// Open() 内部会 avformat_find_stream_info；失败返回 false + 可读错误。
class GrowingFormat {
 public:
  GrowingFormat();
  ~GrowingFormat();

  GrowingFormat(const GrowingFormat&) = delete;
  GrowingFormat& operator=(const GrowingFormat&) = delete;

  // wait_timeout_ms>0 时只用于索引/测试等可重试路径：读到当前发布前沿时以
  // ETIMEDOUT 结束，而不是无限等待。产品播放/首帧传 0（阻塞到有数据）。
  bool Open(std::shared_ptr<GrowingSource> source, const QString& file_path,
            QString* error, int wait_timeout_ms = 0);
  void Close();
  bool IsOpen() const;
  AVFormatContext* context() const;
  GrowingCursor* cursor() const;

 private:
  AVFormatContext* format_ = nullptr;
  std::shared_ptr<GrowingSource> source_;
  std::unique_ptr<GrowingCursor> cursor_;
};

// 把 FFmpeg 负数错误变成可读 UTF-8 文本（错误提示用）。
QString AvErrorText(int av_error);

}  // namespace lapian::online
