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
  // T8.12 手动切档：0=自动（DASH 按 O6 顺序选 AVC；durl 仍请求 qn=80）。
  // >0 时 DASH 必须命中该 qn（缺失则明确失败，不回退到别的档）；durl 回退
  // 也请求该 qn，但实际 qn 仍只信服务端返回。codecid=0 表示优先 AVC。
  int requested_qn = 0;
  int requested_codecid = 0;
  // 只用一次；不打印/不落盘。MainWindow 从 CredentialStore 读出后传入。
  QString sessdata;
};

enum class OnlineState {
  kIdle = 0,
  kPreparing,
  kBuffering,
  kCacheHit,
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
  // T8.7：完整缓存命中时不建 GrowingSource/DownloadJob，直接打开 data.mp4。
  bool cache_hit = false;
  QString cache_mp4_path;
  QString title;
  QString owner;
  qint64 duration_ms = 0;
  qint64 total_bytes = 0;
  int qn = 0;
  int codecid = 0;
  double fps = 0.0;

  // T8.10：DASH 双输入。视频走 source/part_path；音频是独立容器/读层/下载。
  bool dash = false;
  std::shared_ptr<lapian::online::GrowingSource> audio_source;
  QString audio_part_path;
  int audio_id = 0;
  qint64 audio_total_bytes = 0;
  // DASH 目录给出的**实际** fps（OB-2 = 29.412）；容器 avg_frame_rate 可能报 30。
  double dash_fps = 0.0;
  qint64 dash_duration_ms = 0;
};

// T8.9 档位目录结果：DASH playurl(fnval=4048) 的解析结果 + 获取状态。
// 只用于“列档/菜单/证据”，**不接双输入播放**（T8.10）。
struct OnlineQualityCatalog {
  lapian::online::DashCatalog catalog;
  bool ok = false;
  bool cache_hit = false;  // 来自完整缓存 meta，未联网
  bool fetched = false;    // 是否已有结果（成功或失败）
  int http_status = 0;
  int http_requests = 0;
  qint64 elapsed_ms = 0;
  QString error;
};

class OnlineSession : public QObject {
  Q_OBJECT

 public:
  using StateCallback = std::function<void(OnlineState, const QString&)>;
  using ProgressCallback = std::function<void(const OnlineProgress&)>;
  using ReadyCallback = std::function<void(const OnlineReadyInfo&)>;
  using ErrorCallback = std::function<void(const QString&)>;
  // T8.9：档位目录（成功/失败/cache-hit）回到 UI；不改变会话状态。
  using QualityCallback = std::function<void(const OnlineQualityCatalog&)>;

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

  // T8.9：档位目录默认在首帧后异步取一次 fnval=4048；测试可关闭或直接注入。
  void SetQualityCatalogEnabledForTest(bool enabled) {
    quality_enabled_.store(enabled);
  }
  void SetQualityCatalogForTest(const OnlineQualityCatalog& info);
  OnlineQualityCatalog quality_catalog() const;
  // T8.10 诊断/回归：false 时跳过 DASH，直接走 M1 durl 单文件路径。
  void SetDashEnabledForTest(bool enabled) { dash_enabled_.store(enabled); }
  // T8.11 慢增长测试钩子：>0 时下载按字节/秒节流，复现“边下边索引/波形”；
  // 0（默认）不改变产品下载。audio_bps<0 表示与视频同速。必须在 Start 前调用。
  void SetDownloadThrottleForTest(qint64 video_bytes_per_sec,
                                  qint64 audio_bytes_per_sec = -1) {
    download_throttle_bps_ = video_bytes_per_sec > 0 ? video_bytes_per_sec : 0;
    audio_download_throttle_bps_ =
        audio_bytes_per_sec < 0 ? download_throttle_bps_
                                : (audio_bytes_per_sec > 0 ? audio_bytes_per_sec : 0);
  }

  void SetCallbacks(StateCallback state, ProgressCallback progress,
                    ReadyCallback ready, ErrorCallback error,
                    QualityCallback quality = {});

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
  QString audio_part_path() const;
  qint64 total_bytes() const { return total_; }
  qint64 audio_total_bytes() const { return audio_total_; }
  int qn() const { return qn_; }
  int codecid() const { return codecid_; }
  int audio_id() const { return audio_id_; }
  bool dash_mode() const { return dash_mode_; }
  bool ready_emitted() const { return ready_emitted_; }
  bool cache_hit() const { return cache_hit_; }
  QString cache_mp4_path() const;
  QString cache_audio_mp4_path() const;
  bool HasPendingFinalize() const;
  // reader 全部关闭后调用；内部仍走 CacheStore::MarkComplete。
  bool FinalizeAfterReadersStopped(QString* error);

  QString LastErrorForTest() const { return last_error_; }
  int PlayUrlRequestsForTest() const { return playurl_requests_; }
  int DashRequestsForTest() const { return dash_requests_; }
  int QualityRequestsForTest() const { return quality_requests_; }
  QString DashFallbackReasonForTest() const { return dash_fallback_reason_; }

 private slots:
  void Poll();

 private:
  struct ResolveOutcome {
    std::mutex mutex;
    bool done = false;
    bool dash = false;
    lapian::online::DashFetchResult dash_result;
    qint64 video_total = 0;
    qint64 audio_total = 0;
    lapian::online::DashStream video;
    lapian::online::DashAudioStream audio;
    lapian::online::PlayUrlFetchResult durl_result;
    bool durl_fetched = false;
    QString fallback_reason;
    // T8.12 手动切档：请求档位在服务端目录里不存在时直接失败，不静默换档。
    QString failure;
    std::uint64_t generation = 0;
  };

  struct QualityOutcome {
    std::mutex mutex;
    bool done = false;
    OnlineQualityCatalog info;
    std::uint64_t generation = 0;
  };

  void SetState(OnlineState state, const QString& message);
  void PublishProgress();
  // 返回 kHit 时已安排 ready 回调；kError 时 *error 可读（不联网）；kMiss 继续取流。
  enum class CacheLookupResult { kMiss, kHit, kError };
  CacheLookupResult TryCacheHit(const OnlineRequest& request, QString* error);
  CacheLookupResult TryDashCacheHit(const OnlineRequest& request,
                                    const lapian::online::CacheEntry& video,
                                    QString* error);
  void EmitCacheHitReady();
  void EmitDashCacheHitReady();
  void StartResolveWorker(std::uint64_t generation);
  void JoinResolveWorker();
  bool BuildPlan(const lapian::online::PlayUrlData& data,
                 lapian::online::DownloadPlan* plan,
                 lapian::online::CacheKey* key, QString* error) const;
  bool BuildDashPlans(const ResolveOutcome& outcome,
                      lapian::online::DownloadPlan* video_plan,
                      lapian::online::DownloadPlan* audio_plan,
                      lapian::online::CacheKey* video_key,
                      lapian::online::CacheKey* audio_key, QString* error) const;
  void StartDurlDownload(const lapian::online::PlayUrlData& data,
                         std::uint64_t generation);
  void StartDashDownload(const ResolveOutcome& outcome,
                         std::uint64_t generation);
  bool StartJobs(const lapian::online::DownloadPlan& video_plan,
                 const lapian::online::CacheKey& video_key,
                 const lapian::online::DownloadPlan& audio_plan,
                 const lapian::online::CacheKey& audio_key, QString* error);
  void Fail(const QString& message);
  void OnResolveDone(std::uint64_t generation);
  void CancelJobAndSource();
  void EmitReady();

  // T8.9 档位目录：首帧后起一个独立线程；失败不影响播放/会话状态。
  void StartQualityWorker(std::uint64_t generation);
  void JoinQualityWorker();
  void OnQualityDone(std::uint64_t generation);
  void EmitQuality(const OnlineQualityCatalog& info);
  OnlineQualityCatalog CacheQualityFromEntry() const;

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
  std::shared_ptr<lapian::online::GrowingSource> audio_source_;
  std::unique_ptr<lapian::online::DownloadJob> job_;
  std::unique_ptr<lapian::online::DownloadJob> audio_job_;
  std::unique_ptr<QTimer> poll_timer_;
  std::thread resolve_thread_;
  std::shared_ptr<ResolveOutcome> resolve_outcome_;
  std::shared_ptr<lapian::online::HttpCancelToken> resolve_cancel_;
  std::uint64_t generation_ = 0;

  // T8.10：DASH 目录（resolve 阶段已拿到；首帧后直接发 quality，不重复请求）。
  bool dash_mode_ = false;
  lapian::online::DashCatalog dash_catalog_;
  bool dash_catalog_available_ = false;
  int dash_requests_ = 0;
  QString dash_fallback_reason_;
  int audio_id_ = 0;
  qint64 audio_total_ = 0;
  qint64 audio_ready_threshold_ = 0;

  std::thread quality_thread_;
  std::shared_ptr<QualityOutcome> quality_outcome_;
  std::shared_ptr<lapian::online::HttpCancelToken> quality_cancel_;
  std::atomic<bool> quality_enabled_{true};
  std::atomic<bool> dash_enabled_{true};
  qint64 download_throttle_bps_ = 0;
  qint64 audio_download_throttle_bps_ = 0;
  bool has_injected_quality_ = false;
  OnlineQualityCatalog injected_quality_;
  bool quality_started_ = false;
  bool quality_emitted_ = false;
  int quality_requests_ = 0;
  OnlineQualityCatalog quality_;

  mutable std::mutex mutex_;
  OnlineState state_ = OnlineState::kIdle;
  OnlineProgress progress_;
  QString part_path_;
  QString audio_part_path_;
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
  bool cache_hit_ = false;
  QString cache_mp4_path_;
  QString cache_audio_mp4_path_;
  lapian::online::CacheEntry cache_entry_;
  lapian::online::CacheEntry cache_entry_audio_;
  double dash_fps_ = 0.0;
  qint64 dash_duration_ms_ = 0;

  StateCallback state_callback_;
  ProgressCallback progress_callback_;
  ReadyCallback ready_callback_;
  ErrorCallback error_callback_;
  QualityCallback quality_callback_;
};

}  // namespace lapian::app
