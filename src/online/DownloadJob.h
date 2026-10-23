#pragma once

// T8.5 下载任务：playurl 选出的 durl → CacheStore::Reserve/OpenPart →
// 顺序下载 + Range 续传 + 3 个备份 URL + 有限重试 + URL 过期刷新 →
// 每次写盘后 GrowingSource::Publish → 关闭写句柄后 MarkComplete。
//
// 边界：
// - 不接 UI、不解析 playurl（由 PlayUrl 层/调用方提供 DownloadPlan）；
// - 不改变 CacheStore 写语义；不共享/不关闭 reader 的读句柄；
// - 一个 DownloadJob 一个工作线程；WinHTTP 回调线程只做流式写盘 + Publish；
// - 锁顺序：OnlineSession → CacheStore → GrowingSource；本类内部锁不跨 CacheStore
//   长临界区，且不持有 CacheStore 锁等待 reader。
//
// 失败语义：
// - 取消：保留 `.part`，source->Cancel()（reader 收到 AVERROR_EXIT）；
// - 不可恢复失败：保留 `.part`，source->Fail(AVERROR(EIO/ENOSPC/...))；
// - 完成：关闭写句柄 → MarkComplete → OpenComplete 验证；source 已发布 total。

#include <QString>
#include <QStringList>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "online/CacheStore.h"
#include "online/DownloadTransport.h"
#include "online/GrowingSource.h"

namespace lapian::online {

// 调用方从 playurl 构造：urls[0] 主 URL，其余为 backup_url；total 必须 >0。
struct DownloadPlan {
  QStringList urls;
  std::int64_t total_bytes = 0;
  std::int64_t length_ms = 0;
  int qn = 0;
  int codecid = 0;
  QString format;
  CacheMetadata metadata;
};

enum class DownloadState {
  kIdle = 0,
  kPreparing,
  kDownloading,
  kPaused,
  kRetrying,
  kRefreshing,
  kCompleted,
  kFailed,
  kCancelled,
};

QString DownloadStateName(DownloadState state);

struct DownloadStats {
  std::int64_t total_bytes = 0;
  std::int64_t existing_bytes = 0;    // 启动时 .part 长度
  std::int64_t downloaded_bytes = 0;  // 本次会话实际写入字节（含覆盖重下）
  std::int64_t written_bytes = 0;     // 当前连续前缀（.part 目标长度）
  std::int64_t elapsed_ms = 0;
  std::int64_t first_byte_ms = -1;
  double bps = 0.0;

  int range_requests = 0;
  int http_206 = 0;
  int http_200 = 0;
  int backup_switches = 0;
  int retries = 0;
  int refreshes = 0;

  bool range_ignored = false;
  bool range_ignored_truncated = false;  // 无 reader：truncate 后从 0 写
  bool range_ignored_overwrote = false;  // 有 reader：原地覆盖，不截断
  bool already_complete = false;

  QString error;
  int av_error = 0;
};

// 刷新 playurl 一次；返回 false 时 error 给可读原因。cancel 可中断 HTTP。
using DownloadRefreshCallback = std::function<bool(
    DownloadPlan* plan, const HttpCancelToken& cancel, QString* error)>;

struct DownloadOptions {
  int attempt_timeout_ms = 20000;
  int max_attempts_per_url = 3;  // 同一 URL 连续失败次数上限
  int backoff_ms[3] = {300, 1000, 2000};
  int progress_interval_ms = 1000;
  QString user_agent;
  QString referer;
};

class DownloadJob {
 public:
  DownloadJob(CacheStore* cache, CacheKey key,
              std::shared_ptr<GrowingSource> source,
              IDownloadTransport* transport, DownloadPlan plan,
              DownloadRefreshCallback refresh = {},
              DownloadOptions options = {});
  ~DownloadJob();

  DownloadJob(const DownloadJob&) = delete;
  DownloadJob& operator=(const DownloadJob&) = delete;

  // Reserve/OpenPart/打开写句柄并启动工作线程。失败返回 false + 可读错误；
  // 失败时不启动线程、不改动已有 `.part`（Reserve 可能已建空文件）。
  bool Start(QString* error);

  // 暂停：中断当前 HTTP 尝试（≤100ms 内由传输层关闭请求），保留 `.part`。
  // 恢复：下一次尝试从当前 `.part` 长度续传。
  void Pause();
  void Resume();

  // 取消：立即置位并中断 HTTP；工作线程退出后 source->Cancel()。
  // Cancel() 本身不 join，不阻塞 UI；需要等待线程结束时用 Wait()。
  void Cancel();

  // 等待工作线程结束（-1 = 无限）。返回 true 表示已进入终态。
  bool Wait(int timeout_ms = -1);

  DownloadState state() const;
  DownloadStats stats() const;

  // Start 成功后可用；`.part` 绝对路径（不含签名 URL）。
  QString part_path() const;
  CacheKey key() const;

 private:
  class PartSink;
  friend class PartSink;

  void Run();
  bool WritePart(const std::uint8_t* data, std::size_t size, QString* error);
  bool IsCancelRequested() const;
  bool IsPauseRequested() const;
  bool SleepInterruptible(int ms);
  bool WaitWhilePaused();
  void UpdateState(DownloadState state);
  void SetStats(const std::function<void(DownloadStats*)>& update);

  void FinishCompleted();
  void FinishFailed(int av_error, const QString& error);
  void FinishCancelled();
  void CloseWriteHandle();

  CacheStore* cache_ = nullptr;
  CacheKey key_;
  std::shared_ptr<GrowingSource> source_;
  IDownloadTransport* transport_ = nullptr;
  DownloadPlan plan_;
  DownloadRefreshCallback refresh_;
  DownloadOptions options_;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  DownloadState state_ = DownloadState::kIdle;
  DownloadStats stats_;
  bool started_ = false;
  bool finished_ = false;
  bool cancelled_ = false;
  bool paused_ = false;
  std::shared_ptr<HttpCancelToken> attempt_cancel_;
  std::shared_ptr<HttpCancelToken> refresh_cancel_;
  std::thread worker_;

  // 仅工作线程/传输回调写；Start 后只读。
  std::intptr_t write_handle_ = -1;
  QString part_path_;
  std::int64_t existing_ = 0;
  std::int64_t total_ = 0;
  std::int64_t written_ = 0;        // 文件当前连续前缀（写盘后）
  std::int64_t session_bytes_ = 0;  // 本次会话写入（含覆盖）
  std::int64_t last_progress_ms_ = 0;
  std::int64_t run_start_ms_ = 0;
  int sink_av_error_ = 0;
};

}  // namespace lapian::online
