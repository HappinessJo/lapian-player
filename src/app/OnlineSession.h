#pragma once

// T8.6 在线会话状态机：确认 → playurl → CacheStore + GrowingSource + DownloadJob
// → 等 0.3MB → 通知 UI 打开可增长媒体 → 关闭/完成前 finalize。
//
// 边界：
// - 只调度轻量状态与后台线程；UI 线程不做网络/阻塞读。
// - 下载/读层/缓存语义沿用 T8.5/T8.4；不改 CacheStore 写语义。
// - 完成时不立即 rename；defer_finalize 进入 kAwaitingFinalize，等 MainWindow
//   关闭所有 reader 后调用 FinalizeAfterReadersStopped()。
// - 本类不拥有 MediaSession/AudioEngine/Waveform；UI 在 ready 回调里打开它们。

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "online/CacheStore.h"
#include "online/DownloadJob.h"
#include "online/GrowingSource.h"
#include "online/PlayUrl.h"

class QTimer;

namespace lapian::online {
class IDownloadTransport;
class IHttpClient;
}  // namespace lapian::online

namespace lapian::app {

struct OnlineRequest {
  QString bvid;
  qint64 cid = 0;
  int page = 0;
  QString title;
  QString owner;
  qint64 duration_ms = 0;
  QString page_title;
  // 只用一次；不打印/不落盘。MainWindow 从 CredentialStore 读出后传入。
  QString sessdata;
};

enum class OnlineState {
  kIdle = 0,
  kPreparing,
  kBuffering,
  kReady,
  kStalled,
  kSeekingWait,
  kRecovering,
  kError,
  kClosed,
};

QString OnlineStateName(OnlineState state);

struct OnlineProgress {
  qint64 published = 0;
  qint64 total = 0;
  double bps = 0.0;
  QString job_state;
  int qn = 0;
  int codecid = 0;
  bool complete = false;
  bool awaiting_finalize = false;
  QString error;
};

// 首帧打开所需的全部信息；source/part 生命周期由 MainWindow 的 MediaSession
// 和其它 reader 持有。
struct OnlineReadyInfo {
  std::shared_ptr<lapian::online::GrowingSource> source;
  QString part_path;
  lapian::online::PlayUrlData playurl;
  OnlineRequest request;
};

class OnlineSession : public QObject {
  Q_OBJECT

 public:
  using StateCallback = std::function<void(OnlineState, const QString&)>;
  using ProgressCallback = std::function<void(const OnlineProgress&)>;
  using ReadyCallback = std::function<void(const OnlineReadyInfo&)>;
  using ErrorCallback = std::function<void(const QString&)>;

  explicit OnlineSession(lapian::online::CacheStore* cache,
                         QObject* parent = nullptr);
  ~OnlineSession() override;

  OnlineSession(const OnlineSession&) = delete;
  OnlineSession& operator=(const OnlineSession&) = delete;

  // 测试注入：不联网；必须在 Start 前调用。
  void SetHttpClientForTest(
      std::unique_ptr<lapian::online::IHttpClient> client);
  void SetDownloadTransportForTest(
      std::unique_ptr<lapian::online::IDownloadTransport> transport);
  void SetPlayUrlDataForTest(const lapian::online::PlayUrlData& data);
  // 测试注入：跳过 view/playurl 的等待，但仍走同一下载/读层/回调路径。
  void SetReadyThresholdForTest(qint64 bytes);

  void SetCallbacks(StateCallback state, ProgressCallback progress,
                    ReadyCallback ready, ErrorCallback error);

  // UI 线程调用。失败返回 false + 可读错误（不启动会话）。
  bool Start(const OnlineRequest& request, QString* error);

  // 用户取消：立即唤醒所有 reader + 中断下载；不 join（避免卡 UI）。
  void Cancel();
  // 关窗/切文件：取消并 join 所有后台线程。reader 需由调用方先关闭。
  void Close();
  bool IsActive() const { return active_; }

  OnlineState state() const;
  OnlineProgress progress() const;
  lapian::online::DownloadState job_state() const;
  QString part_path() const;
  qint64 total_bytes() const { return total_; }
  int qn() const { return qn_; }
  int codecid() const { return codecid_; }
  bool ready_emitted() const { return ready_emitted_; }
  bool HasPendingFinalize() const;
  // reader 全部关闭后调用；内部仍走 CacheStore::MarkComplete。
  bool FinalizeAfterReadersStopped(QString* error);

  QString LastErrorForTest() const { return last_error_; }
  int PlayUrlRequestsForTest() const { return playurl_requests_; }

 private slots:
  void Poll();

 private:
  struct PlayUrlOutcome {
    std::mutex mutex;
    bool done = false;
    lapian::online::PlayUrlFetchResult result;
    std::uint64_t generation = 0;
  };

  void SetState(OnlineState state, const QString& message);
  void PublishProgress();
  void StartPlayUrlWorker(std::uint64_t generation);
  void JoinPlayUrlWorker();
  bool BuildPlan(const lapian::online::PlayUrlData& data,
                 lapian::online::DownloadPlan* plan,
                 lapian::online::CacheKey* key, QString* error) const;
  void Fail(const QString& message);
  void OnPlayUrlDone(std::uint64_t generation);
  void CancelJobAndSource();

  lapian::online::CacheStore* cache_ = nullptr;
  std::unique_ptr<lapian::online::IHttpClient> injected_http_;
  std::unique_ptr<lapian::online::IDownloadTransport> injected_transport_;
  std::unique_ptr<lapian::online::IDownloadTransport> transport_;
  std::unique_ptr<lapian::online::IHttpClient> http_;
  bool has_injected_playurl_ = false;
  lapian::online::PlayUrlData injected_playurl_;

  OnlineRequest request_;
  lapian::online::PlayUrlData playurl_data_;
  std::shared_ptr<lapian::online::GrowingSource> source_;
  std::unique_ptr<lapian::online::DownloadJob> job_;
  std::unique_ptr<QTimer> poll_timer_;
  std::thread playurl_thread_;
  std::shared_ptr<PlayUrlOutcome> playurl_outcome_;
  std::shared_ptr<lapian::online::HttpCancelToken> playurl_cancel_;
  std::uint64_t generation_ = 0;

  mutable std::mutex mutex_;
  OnlineState state_ = OnlineState::kIdle;
  OnlineProgress progress_;
  QString part_path_;
  QString last_error_;
  qint64 total_ = 0;
  qint64 ready_threshold_ = 0;
  int qn_ = 0;
  int codecid_ = 0;
  int playurl_requests_ = 0;
  std::atomic<bool> active_{false};
  std::atomic<bool> closing_{false};
  bool ready_emitted_ = false;
  bool finalize_pending_ = false;

  StateCallback state_callback_;
  ProgressCallback progress_callback_;
  ReadyCallback ready_callback_;
  ErrorCallback error_callback_;
};

}  // namespace lapian::app
