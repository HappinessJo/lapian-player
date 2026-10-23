#include "app/OnlineSession.h"

#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <utility>

#include "online/WinHttpClient.h"

namespace lapian::app {
namespace {

constexpr qint64 kReadyThresholdBytes = 300 * 1024;

lapian::online::CacheMetadata MakeMetadata(const OnlineRequest& request) {
  lapian::online::CacheMetadata metadata;
  metadata.title = request.title;
  metadata.owner = request.owner;
  metadata.duration_ms = request.duration_ms;
  return metadata;
}

}  // namespace

QString OnlineStateName(OnlineState state) {
  switch (state) {
    case OnlineState::kIdle:
      return QStringLiteral("idle");
    case OnlineState::kPreparing:
      return QStringLiteral("preparing");
    case OnlineState::kBuffering:
      return QStringLiteral("buffering");
    case OnlineState::kReady:
      return QStringLiteral("ready");
    case OnlineState::kStalled:
      return QStringLiteral("stalled");
    case OnlineState::kSeekingWait:
      return QStringLiteral("seeking_wait");
    case OnlineState::kRecovering:
      return QStringLiteral("recovering");
    case OnlineState::kError:
      return QStringLiteral("error");
    case OnlineState::kClosed:
      return QStringLiteral("closed");
  }
  return QStringLiteral("unknown");
}

OnlineSession::OnlineSession(lapian::online::CacheStore* cache, QObject* parent)
    : QObject(parent), cache_(cache) {
  poll_timer_ = std::make_unique<QTimer>();
  poll_timer_->setInterval(80);
  connect(poll_timer_.get(), &QTimer::timeout, this, &OnlineSession::Poll);
}

OnlineSession::~OnlineSession() { Close(); }

void OnlineSession::SetHttpClientForTest(
    std::unique_ptr<lapian::online::IHttpClient> client) {
  injected_http_ = std::move(client);
}

void OnlineSession::SetDownloadTransportForTest(
    std::unique_ptr<lapian::online::IDownloadTransport> transport) {
  injected_transport_ = std::move(transport);
}

void OnlineSession::SetPlayUrlDataForTest(
    const lapian::online::PlayUrlData& data) {
  injected_playurl_ = data;
  has_injected_playurl_ = true;
}

void OnlineSession::SetReadyThresholdForTest(qint64 bytes) {
  ready_threshold_ = std::max<qint64>(1, bytes);
}

void OnlineSession::SetCallbacks(StateCallback state,
                                 ProgressCallback progress,
                                 ReadyCallback ready, ErrorCallback error) {
  state_callback_ = std::move(state);
  progress_callback_ = std::move(progress);
  ready_callback_ = std::move(ready);
  error_callback_ = std::move(error);
}

void OnlineSession::SetState(OnlineState state, const QString& message) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
    progress_.error = (state == OnlineState::kError) ? message : QString();
  }
  if (state_callback_) {
    state_callback_(state, message);
  }
}

OnlineState OnlineSession::state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

OnlineProgress OnlineSession::progress() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return progress_;
}

lapian::online::DownloadState OnlineSession::job_state() const {
  return job_ ? job_->state() : lapian::online::DownloadState::kIdle;
}

QString OnlineSession::part_path() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return part_path_;
}

bool OnlineSession::HasPendingFinalize() const {
  return job_ &&
         job_->state() == lapian::online::DownloadState::kAwaitingFinalize;
}

void OnlineSession::PublishProgress() {
  OnlineProgress snapshot;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot = progress_;
  }
  if (progress_callback_) {
    progress_callback_(snapshot);
  }
}

bool OnlineSession::BuildPlan(
    const lapian::online::PlayUrlData& data,
    lapian::online::DownloadPlan* plan,
    lapian::online::CacheKey* key, QString* error) const {
  if (plan == nullptr || key == nullptr || error == nullptr) {
    return false;
  }
  error->clear();
  if (!data.HasDurl()) {
    *error = QStringLiteral("该视频没有可用的 durl 单文件流（M1 不支持 DASH）");
    return false;
  }
  if (data.durls.size() > 1) {
    *error = QStringLiteral("该视频返回 %1 段 durl，M1 暂不支持多段拼接")
                 .arg(data.durls.size());
    return false;
  }
  const lapian::online::DurlSegment& segment = data.durls.first();
  const qint64 total = segment.size > 0 ? segment.size : data.TotalBytes();
  if (total <= 0) {
    *error = QStringLiteral("playurl 没有给出可用的总大小，无法进入可增长播放");
    return false;
  }
  const QStringList urls = segment.AllUrls();
  if (urls.isEmpty()) {
    *error = QStringLiteral("playurl 没有返回可用下载地址");
    return false;
  }
  const int codecid = data.codecid > 0 ? data.codecid : 7;
  key->bvid = request_.bvid;
  key->cid = request_.cid;
  key->qn = data.quality;
  key->codecid = codecid;
  if (!lapian::online::IsValidCacheKey(*key)) {
    *error = QStringLiteral("缓存键无效（bvid/cid/qn/codecid）");
    return false;
  }
  plan->urls = urls;
  plan->total_bytes = total;
  plan->length_ms = segment.length_ms > 0 ? segment.length_ms : data.timelength_ms;
  plan->qn = data.quality;
  plan->codecid = codecid;
  plan->format = data.format;
  plan->metadata = MakeMetadata(request_);
  return true;
}

void OnlineSession::StartPlayUrlWorker(std::uint64_t generation) {
  JoinPlayUrlWorker();
  auto outcome = std::make_shared<PlayUrlOutcome>();
  outcome->generation = generation;
  playurl_outcome_ = outcome;
  auto cancel = std::make_shared<lapian::online::HttpCancelToken>();
  playurl_cancel_ = cancel;
  lapian::online::IHttpClient* http = nullptr;
  if (injected_http_) {
    http = injected_http_.get();
  } else {
    http_ = std::make_unique<lapian::online::WinHttpClient>();
    http = http_.get();
  }
  const QString bvid = request_.bvid;
  const qint64 cid = request_.cid;
  const QString sessdata = request_.sessdata;
  const auto started = std::chrono::steady_clock::now();
  try {
    playurl_thread_ = std::thread([this, generation, outcome, cancel, http,
                                   bvid, cid, sessdata, started]() {
      lapian::online::PlayUrlFetchOptions options;
      options.timeout_ms = 6000;
      options.sessdata = sessdata;
      lapian::online::PlayUrlFetchResult result =
          lapian::online::FetchPlayUrl(http, bvid, cid, 64, options, *cancel);
      {
        std::lock_guard<std::mutex> lock(outcome->mutex);
        outcome->done = true;
        outcome->result = std::move(result);
        outcome->result.elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started)
                .count();
      }
      QMetaObject::invokeMethod(
          this, [this, generation]() { OnPlayUrlDone(generation); },
          Qt::QueuedConnection);
    });
  } catch (...) {
    Fail(QStringLiteral("无法创建取流线程"));
  }
}

void OnlineSession::JoinPlayUrlWorker() {
  if (playurl_thread_.joinable()) {
    playurl_thread_.join();
  }
}

bool OnlineSession::Start(const OnlineRequest& request, QString* error) {
  if (error == nullptr) {
    return false;
  }
  error->clear();
  if (cache_ == nullptr) {
    *error = QStringLiteral("在线缓存未初始化");
    return false;
  }
  if (!lapian::online::IsValidCacheBvid(request.bvid) || request.cid <= 0) {
    *error = QStringLiteral("bvid/cid 无效，不能开始在线会话");
    return false;
  }
  if (active_.load()) {
    Close();
  }
  request_ = request;
  request_.sessdata = request.sessdata;
  active_.store(true);
  closing_.store(false);
  ready_emitted_ = false;
  finalize_pending_ = false;
  total_ = 0;
  qn_ = 0;
  codecid_ = 0;
  ++generation_;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = OnlineState::kPreparing;
    progress_ = OnlineProgress{};
    part_path_.clear();
    last_error_.clear();
  }
  if (state_callback_) {
    state_callback_(OnlineState::kPreparing, QStringLiteral("准备取流…"));
  }

  if (has_injected_playurl_) {
    const std::uint64_t generation = generation_;
    QTimer::singleShot(0, this, [this, generation]() {
      OnPlayUrlDone(generation);
    });
    return true;
  }
  StartPlayUrlWorker(generation_);
  if (state() == OnlineState::kError) {
    std::lock_guard<std::mutex> lock(mutex_);
    *error = last_error_;
    return false;
  }
  return true;
}

void OnlineSession::OnPlayUrlDone(std::uint64_t generation) {
  JoinPlayUrlWorker();
  if (generation != generation_ || !active_.load()) {
    return;
  }
  lapian::online::PlayUrlFetchResult fetch;
  if (has_injected_playurl_) {
    fetch.data = injected_playurl_;
  } else {
    const auto outcome = playurl_outcome_;
    if (!outcome) {
      Fail(QStringLiteral("取流结果丢失"));
      return;
    }
    std::lock_guard<std::mutex> lock(outcome->mutex);
    if (!outcome->done) {
      Fail(QStringLiteral("取流未完成"));
      return;
    }
    fetch = outcome->result;
  }
  ++playurl_requests_;

  playurl_data_ = fetch.data;
  if (!fetch.error.ok()) {
    Fail(fetch.error.message.isEmpty() ? QStringLiteral("取流失败")
                                       : fetch.error.message);
    return;
  }
  lapian::online::DownloadPlan plan;
  lapian::online::CacheKey key;
  QString plan_error;
  if (!BuildPlan(fetch.data, &plan, &key, &plan_error)) {
    Fail(plan_error);
    return;
  }
  total_ = plan.total_bytes;
  qn_ = plan.qn;
  codecid_ = plan.codecid;
  if (ready_threshold_ <= 0) {
    ready_threshold_ = std::min<qint64>(kReadyThresholdBytes, total_);
  } else {
    ready_threshold_ = std::min<qint64>(ready_threshold_, total_);
  }

  source_ = std::make_shared<lapian::online::GrowingSource>(total_);
  if (!source_->IsValid()) {
    Fail(source_->InvalidReason());
    return;
  }

  if (injected_transport_) {
    transport_ = std::move(injected_transport_);
  } else {
    transport_ = std::make_unique<lapian::online::WinHttpDownloadTransport>();
  }

  lapian::online::DownloadOptions options;
  options.defer_finalize = true;

  // 刷新回调运行在下载线程：只做一次 playurl，不碰 UI/日志中的签名 URL。
  auto refresh = [this](lapian::online::DownloadPlan* fresh,
                        const lapian::online::HttpCancelToken& cancel,
                        QString* refresh_error) -> bool {
    lapian::online::IHttpClient* http =
        injected_http_ ? injected_http_.get() : http_.get();
    if (http == nullptr) {
      *refresh_error = QStringLiteral("HTTP 客户端不可用");
      return false;
    }
    lapian::online::PlayUrlFetchOptions fetch_options;
    fetch_options.timeout_ms = 6000;
    fetch_options.sessdata = request_.sessdata;
    lapian::online::PlayUrlFetchResult result = lapian::online::FetchPlayUrl(
        http, request_.bvid, request_.cid, 64, fetch_options, cancel);
    if (!result.error.ok()) {
      *refresh_error = result.error.message.isEmpty()
                           ? QStringLiteral("刷新取流失败")
                           : result.error.message;
      return false;
    }
    lapian::online::CacheKey ignored_key;
    return BuildPlan(result.data, fresh, &ignored_key, refresh_error);
  };

  auto job = std::make_unique<lapian::online::DownloadJob>(
      cache_, key, source_, transport_.get(), plan, refresh, options);
  QString start_error;
  if (!job->Start(&start_error)) {
    Fail(start_error.isEmpty() ? QStringLiteral("无法开始下载") : start_error);
    job.reset();
    return;
  }
  job_ = std::move(job);
  if (job_->state() == lapian::online::DownloadState::kCompleted) {
    Fail(QStringLiteral("该视频已完整缓存；缓存命中打开将在 T8.7 接入"));
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    part_path_ = job_->part_path();
  }
  SetState(OnlineState::kBuffering, QStringLiteral("缓冲中 0%"));
  poll_timer_->start();
  PublishProgress();
  Poll();
}

void OnlineSession::Poll() {
  if (!active_.load() || closing_.load() || !job_ || !source_) {
    return;
  }
  const lapian::online::DownloadState job_state = job_->state();
  const lapian::online::DownloadStats stats = job_->stats();
  const qint64 published = source_->Published();
  const qint64 total = source_->Size();

  OnlineProgress snapshot;
  snapshot.published = published;
  snapshot.total = total;
  snapshot.bps = stats.bps;
  snapshot.job_state = lapian::online::DownloadStateName(job_state);
  snapshot.qn = qn_;
  snapshot.codecid = codecid_;
  snapshot.complete = published >= total;
  snapshot.awaiting_finalize =
      job_state == lapian::online::DownloadState::kAwaitingFinalize;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_ = snapshot;
  }
  PublishProgress();

  if (job_state == lapian::online::DownloadState::kFailed) {
    Fail(stats.error.isEmpty() ? QStringLiteral("下载失败") : stats.error);
    return;
  }
  if (job_state == lapian::online::DownloadState::kCancelled) {
    SetState(OnlineState::kClosed, QStringLiteral("已取消"));
    poll_timer_->stop();
    return;
  }
  if (job_state == lapian::online::DownloadState::kRetrying &&
      state() != OnlineState::kRecovering) {
    SetState(OnlineState::kRecovering, QStringLiteral("网络中断，重试中…"));
  } else if (job_state == lapian::online::DownloadState::kRefreshing &&
             state() != OnlineState::kRecovering) {
    SetState(OnlineState::kRecovering, QStringLiteral("取流过期，正在刷新…"));
  } else if (job_state == lapian::online::DownloadState::kDownloading &&
             state() == OnlineState::kRecovering) {
    SetState(ready_emitted_ ? OnlineState::kReady : OnlineState::kBuffering,
             ready_emitted_ ? QStringLiteral("下载恢复")
                            : QStringLiteral("缓冲中"));
  }

  if (!ready_emitted_ && published >= ready_threshold_) {
    ready_emitted_ = true;
    SetState(OnlineState::kReady, QStringLiteral("首帧准备中"));
    if (ready_callback_) {
      OnlineReadyInfo info;
      info.source = source_;
      info.part_path = part_path();
      info.playurl = playurl_data_;
      info.request = request_;
      info.playurl.quality = qn_;
      info.playurl.codecid = codecid_;
      ready_callback_(info);
    }
  }

  if (job_state == lapian::online::DownloadState::kAwaitingFinalize) {
    if (!finalize_pending_) {
      finalize_pending_ = true;
      SetState(OnlineState::kReady,
               QStringLiteral("缓存已完整，等待关闭后落盘"));
    }
  } else if (published >= total) {
    finalize_pending_ = true;
  }
}

void OnlineSession::Fail(const QString& message) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_ = message;
    progress_.error = message;
  }
  SetState(OnlineState::kError, message);
  if (poll_timer_) {
    poll_timer_->stop();
  }
  if (error_callback_) {
    error_callback_(message);
  }
  CancelJobAndSource();
}

void OnlineSession::CancelJobAndSource() {
  if (job_) {
    job_->Cancel();
  }
  if (source_) {
    source_->Cancel();
  }
}

void OnlineSession::Cancel() {
  if (!active_.load()) {
    return;
  }
  closing_.store(true);
  ++generation_;
  if (playurl_cancel_) {
    playurl_cancel_->Cancel();
  }
  if (poll_timer_) {
    poll_timer_->stop();
  }
  CancelJobAndSource();
  SetState(OnlineState::kClosed, QStringLiteral("已取消"));
}

void OnlineSession::Close() {
  if (!active_.load() && !job_ && !playurl_thread_.joinable()) {
    if (poll_timer_) {
      poll_timer_->stop();
    }
    return;
  }
  closing_.store(true);
  active_.store(false);
  ++generation_;
  if (poll_timer_) {
    poll_timer_->stop();
  }
  if (playurl_cancel_) {
    playurl_cancel_->Cancel();
  }
  JoinPlayUrlWorker();
  playurl_outcome_.reset();
  playurl_cancel_.reset();
  CancelJobAndSource();
  if (job_) {
    job_->Wait(1000);
  }
  job_.reset();
  transport_.reset();
  source_.reset();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = OnlineState::kClosed;
    part_path_.clear();
  }
  if (state_callback_) {
    state_callback_(OnlineState::kClosed, QStringLiteral("在线会话已关闭"));
  }
}

bool OnlineSession::FinalizeAfterReadersStopped(QString* error) {
  if (error != nullptr) {
    error->clear();
  }
  if (!job_) {
    if (error != nullptr) {
      *error = QStringLiteral("没有可完成的下载");
    }
    return false;
  }
  if (job_->state() == lapian::online::DownloadState::kCompleted) {
    finalize_pending_ = false;
    return true;
  }
  // 发布 total 与进入 kAwaitingFinalize 之间有一个很短的收尾窗口；关窗/切
  // 文件时 reader 已停，这里给 job 线程最多 500ms 完成收尾，避免偶发
  // “已完整但尚未 awaiting”导致缓存不落盘。
  for (int i = 0; i < 50 &&
                  job_->state() == lapian::online::DownloadState::kDownloading;
       ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (job_->state() != lapian::online::DownloadState::kAwaitingFinalize) {
    if (error != nullptr) {
      *error = QStringLiteral("下载尚未完整，不能完成缓存");
    }
    return false;
  }
  QString finalize_error;
  if (!job_->FinalizeCompleted(&finalize_error)) {
    if (error != nullptr) {
      *error = finalize_error;
    }
    return false;
  }
  finalize_pending_ = false;
  return true;
}

}  // namespace lapian::app
