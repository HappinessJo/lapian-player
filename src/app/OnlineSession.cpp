#include "app/OnlineSession.h"

#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <utility>

#include "online/DownloadTransport.h"
#include "online/Sessdata.h"
#include "online/WinHttpClient.h"

extern "C" {
#include <libavutil/error.h>
}

namespace lapian::app {
namespace {

constexpr qint64 kReadyThresholdBytes = 300 * 1024;
// T8.10：DASH 音频容器首包/probe 只需很少数据；96KB ≈ 7–8s AAC，足够
// avformat_find_stream_info 与首块解码，同时避免“等整个音频下完”。
constexpr qint64 kAudioReadyThresholdBytes = 96 * 1024;

// T8.13：与 DownloadJob/ProcessMemory 一致的 steady_clock 毫秒。
std::int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// T8.9 诊断：只对可能压住 UI 线程的同步段计时；>100ms 才打印（LAPIAN_ONLINE_LOG）。
class ScopedSlowLog {
 public:
  explicit ScopedSlowLog(const char* name, qint64 threshold_ms = 100)
      : name_(name), threshold_ms_(threshold_ms) {
    timer_.start();
  }
  ~ScopedSlowLog() {
    const qint64 elapsed = timer_.elapsed();
    if (elapsed < threshold_ms_ || !qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG")) {
      return;
    }
    std::fprintf(stderr, "T8.9 online_slow section=%s ms=%lld\n", name_,
                 static_cast<long long>(elapsed));
    std::fflush(stderr);
  }

 private:
  const char* name_ = "";
  qint64 threshold_ms_ = 100;
  QElapsedTimer timer_;
};

lapian::online::CacheMetadata MakeMetadata(const OnlineRequest& request) {
  lapian::online::CacheMetadata metadata;
  metadata.title = request.title;
  metadata.owner = request.owner;
  metadata.duration_ms = request.duration_ms;
  return metadata;
}

// T8.17 诊断：默认静默，只在 LAPIAN_ONLINE_LOG 下输出缓存键/命中分支。
// 只打印 bvid/cid/qn/codecid/audio_id/complete 等非敏感字段；绝不打印
// SESSDATA/Cookie/签名 URL。
void OnlineDiag(const char* format, ...) {
  if (!qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG")) {
    return;
  }
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
  std::fflush(stderr);
}

// T8.10：DASH 不返回总字节，用 `Range: bytes=0-0` 探测 Content-Range 总长。
// 服务端忽略 Range（200）时读第一个数据块后立即中止，用 Content-Length 兜底；
// 不接受“猜”的大小。
class FirstChunkSink final : public lapian::online::IDownloadSink {
 public:
  bool Write(const std::uint8_t*, std::size_t, QString*) override {
    return false;  // 只要响应头，不落盘
  }
};

struct TotalProbe {
  bool ok = false;
  qint64 total = 0;
  bool range_supported = false;
  int http_status = 0;
  qint64 elapsed_ms = 0;
  QString error;
  QString url_kind;
};

TotalProbe ProbeDashTotal(lapian::online::IDownloadTransport* transport,
                          const QStringList& urls, int timeout_ms,
                          const lapian::online::HttpCancelToken& cancel) {
  TotalProbe best;
  if (transport == nullptr || urls.isEmpty()) {
    best.error = QStringLiteral("没有可用的 DASH 流地址");
    return best;
  }
  for (int i = 0; i < urls.size(); ++i) {
    // 每个 URL 最多试两次（同一 CDN 的偶发 5xx/超时不换档）。
    for (int attempt = 0; attempt < 2; ++attempt) {
      lapian::online::DownloadRequest request;
      request.url = urls[i];
      request.offset = 0;
      request.range_end = 0;
      request.timeout_ms = timeout_ms;
      FirstChunkSink sink;
      const lapian::online::DownloadTransportResult result =
          transport->GetRange(request, &sink, cancel);
      TotalProbe probe;
      probe.http_status = result.http_status;
      probe.elapsed_ms = result.elapsed_ms;
      probe.url_kind = i == 0 ? QStringLiteral("main")
                              : QStringLiteral("backup%1").arg(i);
      if (result.http_status == 206 && result.content_range_total > 0) {
        probe.ok = true;
        probe.total = result.content_range_total;
        probe.range_supported = true;
      } else if (result.http_status == 200 && result.content_length > 0) {
        probe.ok = true;
        probe.total = result.content_length;
        probe.range_supported = false;
      } else if (result.status ==
                     lapian::online::DownloadTransportStatus::kSinkError &&
                 result.content_range_total > 0) {
        probe.ok = true;
        probe.total = result.content_range_total;
        probe.range_supported = true;
      } else if (result.status ==
                     lapian::online::DownloadTransportStatus::kSinkError &&
                 result.content_length > 0) {
        probe.ok = true;
        probe.total = result.content_length;
        probe.range_supported = false;
      } else {
        probe.error = result.error.isEmpty()
                          ? QStringLiteral("DASH 总字节探测失败")
                          : result.error;
      }
      if (probe.ok) {
        return probe;
      }
      best = probe;
      if (cancel.IsCancelled()) {
        return best;
      }
    }
  }
  return best;
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
    case OnlineState::kCacheHit:
      return QStringLiteral("cache_hit");
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

void OnlineSession::SetQualityCatalogForTest(const OnlineQualityCatalog& info) {
  injected_quality_ = info;
  has_injected_quality_ = true;
}

OnlineQualityCatalog OnlineSession::quality_catalog() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return quality_;
}

void OnlineSession::SetCallbacks(StateCallback state,
                                 ProgressCallback progress,
                                 ReadyCallback ready, ErrorCallback error,
                                 QualityCallback quality) {
  state_callback_ = std::move(state);
  progress_callback_ = std::move(progress);
  ready_callback_ = std::move(ready);
  error_callback_ = std::move(error);
  quality_callback_ = std::move(quality);
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

QString OnlineSession::audio_part_path() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return audio_part_path_;
}

QString OnlineSession::cache_mp4_path() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cache_hit_ ? part_path_ : QString();
}

QString OnlineSession::cache_audio_mp4_path() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cache_hit_ && dash_mode_ ? audio_part_path_ : QString();
}

bool OnlineSession::HasPendingFinalize() const {
  using lapian::online::DownloadState;
  bool pending = false;
  auto consider = [&](const lapian::online::DownloadJob* job) -> bool {
    if (job == nullptr) {
      return true;  // 该路已完整并复用，无需 job/finalize
    }
    switch (job->state()) {
      case DownloadState::kCompleted:
        return true;
      case DownloadState::kAwaitingFinalize:
        pending = true;
        return true;
      default:
        return false;
    }
  };
  return consider(job_.get()) && consider(audio_job_.get()) && pending;
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

bool OnlineSession::BuildDashPlans(
    const ResolveOutcome& outcome,
    lapian::online::DownloadPlan* video_plan,
    lapian::online::DownloadPlan* audio_plan,
    lapian::online::CacheKey* video_key,
    lapian::online::CacheKey* audio_key, QString* error) const {
  using namespace lapian::online;
  if (video_plan == nullptr || audio_plan == nullptr || video_key == nullptr ||
      audio_key == nullptr || error == nullptr) {
    return false;
  }
  error->clear();
  if (!outcome.dash || outcome.video_total <= 0 || outcome.audio_total <= 0) {
    *error = QStringLiteral("DASH 双输入计划缺少总字节");
    return false;
  }
  const DashStream& video = outcome.video;
  const DashAudioStream& audio = outcome.audio;
  const QStringList video_urls = video.AllUrls();
  const QStringList audio_urls = audio.AllUrls();
  if (video_urls.isEmpty() || audio_urls.isEmpty()) {
    *error = QStringLiteral("DASH 流没有可用下载地址");
    return false;
  }
  const qint64 duration_ms =
      dash_catalog_.duration_ms > 0 ? dash_catalog_.duration_ms
                                    : request_.duration_ms;

  video_key->bvid = request_.bvid;
  video_key->cid = request_.cid;
  video_key->qn = video.qn;
  video_key->codecid = video.codecid;
  video_key->kind = CacheStreamKind::kVideo;
  video_key->audio_id = 0;
  audio_key->bvid = request_.bvid;
  audio_key->cid = request_.cid;
  audio_key->kind = CacheStreamKind::kAudio;
  audio_key->audio_id = audio.id;
  audio_key->qn = 0;
  audio_key->codecid = 0;
  if (!IsValidCacheKey(*video_key) || !IsValidCacheKey(*audio_key)) {
    *error = QStringLiteral("DASH 缓存键无效（bvid/cid/qn/codecid/audio_id）");
    return false;
  }

  video_plan->urls = video_urls;
  video_plan->total_bytes = outcome.video_total;
  video_plan->length_ms = duration_ms;
  video_plan->qn = video.qn;
  video_plan->codecid = video.codecid;
  video_plan->format = QStringLiteral("dash");
  video_plan->metadata = MakeMetadata(request_);
  video_plan->metadata.duration_ms = duration_ms;
  video_plan->metadata.width = video.width;
  video_plan->metadata.height = video.height;
  video_plan->metadata.fps = video.fps;
  video_plan->metadata.dash = true;
  video_plan->metadata.audio_id = audio.id;

  audio_plan->urls = audio_urls;
  audio_plan->total_bytes = outcome.audio_total;
  audio_plan->length_ms = duration_ms;
  audio_plan->qn = 0;
  audio_plan->codecid = 0;
  audio_plan->format = QStringLiteral("dash-audio");
  audio_plan->metadata = video_plan->metadata;
  return true;
}

void OnlineSession::StartResolveWorker(std::uint64_t generation) {
  JoinResolveWorker();
  auto outcome = std::make_shared<ResolveOutcome>();
  outcome->generation = generation;
  resolve_outcome_ = outcome;
  auto cancel = std::make_shared<lapian::online::HttpCancelToken>();
  resolve_cancel_ = cancel;
  lapian::online::IHttpClient* injected_http = injected_http_.get();
  lapian::online::IDownloadTransport* injected_transport = injected_transport_.get();
  const QString bvid = request_.bvid;
  const qint64 cid = request_.cid;
  const QString sessdata = request_.sessdata;
  const int requested_qn = request_.requested_qn;
  const int requested_codecid = request_.requested_codecid;
  const int durl_qn =
      requested_qn > 0 ? requested_qn : lapian::online::kM1RequestQn;
  const int dash_request_qn =
      requested_qn > 0 ? requested_qn : lapian::online::kDashRequestQn;
  try {
    resolve_thread_ = std::thread(
        [this, generation, outcome, cancel, injected_http, injected_transport,
         bvid, cid, sessdata, requested_qn, requested_codecid, durl_qn,
         dash_request_qn]() {
          lapian::online::IHttpClient* http = injected_http;
          std::unique_ptr<lapian::online::WinHttpClient> owned_http;
          if (http == nullptr) {
            owned_http = std::make_unique<lapian::online::WinHttpClient>();
            http = owned_http.get();
          }
          if (!dash_enabled_.load()) {
            // T8.10 诊断/回归：强制 M1 durl 路径，用于验证回退不退化。
            lapian::online::PlayUrlFetchOptions durl_options;
            durl_options.timeout_ms = lapian::online::kOnlineApiTimeoutMs;
            durl_options.sessdata = sessdata;
            durl_options.fourk = 0;
            outcome->durl_result = lapian::online::FetchPlayUrl(
                http, bvid, cid, durl_qn, durl_options, *cancel);
            lapian::online::ClearSecret(&durl_options.sessdata);
            outcome->durl_fetched = true;
            outcome->fallback_reason = QStringLiteral("DASH 已禁用（诊断）");
            {
              std::lock_guard<std::mutex> lock(outcome->mutex);
              outcome->done = true;
            }
            QMetaObject::invokeMethod(
                this, [this, generation]() { OnResolveDone(generation); },
                Qt::QueuedConnection);
            return;
          }
          lapian::online::DashFetchOptions dash_options;
          dash_options.timeout_ms = lapian::online::kOnlineApiTimeoutMs;
          dash_options.sessdata = sessdata;
          dash_options.qn = dash_request_qn;
          dash_options.fourk = 1;
          lapian::online::DashFetchResult dash = lapian::online::FetchDashCatalog(
              http, bvid, cid, dash_options, *cancel);
          lapian::online::ClearSecret(&dash_options.sessdata);

          QString fallback_reason;
          QString failure;
          if (!dash.error.ok()) {
            fallback_reason = QStringLiteral("DASH 目录失败：%1")
                                  .arg(dash.error.message.isEmpty()
                                           ? QStringLiteral("未知错误")
                                           : dash.error.message);
          } else {
            const lapian::online::DashStream* selected = nullptr;
            if (requested_qn > 0) {
              const lapian::online::QualityTier* tier =
                  dash.catalog.FindTier(requested_qn);
              if (tier == nullptr) {
                failure = QStringLiteral(
                              "服务端没有返回请求的档位 %1（qn=%2）；"
                              "不自动换到其他档")
                              .arg(lapian::online::QualityName(requested_qn))
                              .arg(requested_qn);
              } else if (requested_codecid > 0) {
                selected = tier->Stream(requested_codecid);
                if (selected == nullptr) {
                  failure = QStringLiteral("请求的档位 %1 没有编码 %2")
                                .arg(lapian::online::QualityName(requested_qn))
                                .arg(lapian::online::CodecName(requested_codecid));
                }
              } else {
                selected = tier->PreferredStream();
              }
            } else {
              selected = dash.catalog.DefaultStream();
              if (selected == nullptr) {
                fallback_reason =
                    QStringLiteral("DASH 目录没有可自动选择的视频档");
              }
            }
            if (requested_qn > 0 && dash.catalog.BestAudio() == nullptr) {
              failure = QStringLiteral("服务端没有返回 DASH 音频流，无法切档");
            } else if (requested_qn <= 0 &&
                       dash.catalog.BestAudio() == nullptr) {
              fallback_reason = QStringLiteral("DASH 目录没有音频流");
            }
            if (failure.isEmpty() && selected != nullptr &&
                dash.catalog.BestAudio() != nullptr) {
              const lapian::online::DashStream video = *selected;
              const lapian::online::DashAudioStream audio =
                  *dash.catalog.BestAudio();
              // DASH 不返回总字节：先探测（Range 0-0 / Content-Length），拿不到
              // 就不伪造，直接回退 durl。
              std::unique_ptr<lapian::online::WinHttpDownloadTransport> owned_transport;
              lapian::online::IDownloadTransport* transport = injected_transport;
              if (transport == nullptr) {
                owned_transport =
                    std::make_unique<lapian::online::WinHttpDownloadTransport>();
                transport = owned_transport.get();
              }
              const TotalProbe video_probe = ProbeDashTotal(
                  transport, video.AllUrls(),
                  lapian::online::kOnlineApiTimeoutMs, *cancel);
              const TotalProbe audio_probe = ProbeDashTotal(
                  transport, audio.AllUrls(),
                  lapian::online::kOnlineApiTimeoutMs, *cancel);
              if (cancel->IsCancelled()) {
                return;
              }
              if (!video_probe.ok || !audio_probe.ok) {
                if (requested_qn > 0) {
                  failure =
                      QStringLiteral("手动切档的 DASH 总字节探测失败"
                                     "（video=%1 audio=%2）")
                          .arg(video_probe.error, audio_probe.error);
                } else {
                  fallback_reason =
                      QStringLiteral("DASH 总字节探测失败（video=%1 audio=%2）")
                          .arg(video_probe.error, audio_probe.error);
                }
              } else {
                outcome->dash = true;
                outcome->video = video;
                outcome->audio = audio;
                outcome->video_total = video_probe.total;
                outcome->audio_total = audio_probe.total;
              }
            }
          }
          if (failure.isEmpty() && !outcome->dash) {
            lapian::online::PlayUrlFetchOptions durl_options;
            durl_options.timeout_ms = lapian::online::kOnlineApiTimeoutMs;
            durl_options.sessdata = sessdata;
            durl_options.fourk = 0;  // M1 路径字节不变
            outcome->durl_result = lapian::online::FetchPlayUrl(
                http, bvid, cid, durl_qn, durl_options, *cancel);
            lapian::online::ClearSecret(&durl_options.sessdata);
            outcome->durl_fetched = true;
          }
          {
            std::lock_guard<std::mutex> lock(outcome->mutex);
            outcome->dash_result = std::move(dash);
            outcome->fallback_reason = fallback_reason;
            outcome->failure = failure;
            outcome->done = true;
          }
          QMetaObject::invokeMethod(
              this, [this, generation]() { OnResolveDone(generation); },
              Qt::QueuedConnection);
        });
  } catch (...) {
    Fail(QStringLiteral("无法创建取流解析线程"));
  }
}

void OnlineSession::JoinResolveWorker() {
  if (resolve_thread_.joinable()) {
    resolve_thread_.join();
  }
}

void OnlineSession::StartQualityWorker(std::uint64_t generation) {
  if (quality_started_) {
    return;
  }
  quality_started_ = true;
  auto outcome = std::make_shared<QualityOutcome>();
  outcome->generation = generation;
  quality_outcome_ = outcome;
  if (has_injected_quality_) {
    {
      std::lock_guard<std::mutex> lock(outcome->mutex);
      outcome->done = true;
      outcome->info = injected_quality_;
    }
    QTimer::singleShot(0, this, [this, generation]() { OnQualityDone(generation); });
    return;
  }
  if (cache_hit_) {
    // 缓存命中在 EmitCacheHitReady 里用 meta 直接发；不应走到网络。
    return;
  }
  auto cancel = std::make_shared<lapian::online::HttpCancelToken>();
  quality_cancel_ = cancel;
  lapian::online::IHttpClient* injected = injected_http_ ? injected_http_.get()
                                                          : nullptr;
  const QString bvid = request_.bvid;
  const qint64 cid = request_.cid;
  const QString sessdata = request_.sessdata;
  const auto started = std::chrono::steady_clock::now();
  try {
    quality_thread_ = std::thread([this, generation, outcome, cancel, injected,
                                   bvid, cid, sessdata, started]() {
      // 独立 WinHttpClient：不与会话的 durl/刷新请求共享对象，避免生命周期
      // 耦合；WinHttpClient::Get 每次自建 session/连接，调用本身无共享状态。
      std::unique_ptr<lapian::online::WinHttpClient> owned;
      lapian::online::IHttpClient* client = injected;
      if (client == nullptr) {
        owned = std::make_unique<lapian::online::WinHttpClient>();
        client = owned.get();
      }
      lapian::online::DashFetchOptions options;
      options.timeout_ms = lapian::online::kOnlineApiTimeoutMs;
      options.sessdata = sessdata;
      // 档位目录请求：手动切档时用请求 qn（只影响 claimed quality，目录仍为
      // 服务端返回的完整梯）；自动时封顶 1080P + fourk=1。
      options.qn = request_.requested_qn > 0
                       ? request_.requested_qn
                       : lapian::online::kDashRequestQn;
      options.fourk = 1;
      const lapian::online::DashFetchResult result =
          lapian::online::FetchDashCatalog(client, bvid, cid, options, *cancel);
      OnlineQualityCatalog info;
      info.ok = result.error.ok();
      info.fetched = true;
      info.http_status = result.http_status;
      info.http_requests = result.http_requests;
      info.catalog = result.catalog;
      info.elapsed_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - started)
              .count();
      if (!result.error.ok()) {
        info.error = result.error.message.isEmpty()
                         ? QStringLiteral("档位获取失败")
                         : result.error.message;
      }
      {
        std::lock_guard<std::mutex> lock(outcome->mutex);
        outcome->done = true;
        outcome->info = std::move(info);
      }
      QMetaObject::invokeMethod(
          this, [this, generation]() { OnQualityDone(generation); },
          Qt::QueuedConnection);
    });
  } catch (...) {
    OnlineQualityCatalog info;
    info.fetched = true;
    info.error = QStringLiteral("无法创建档位线程");
    {
      std::lock_guard<std::mutex> lock(outcome->mutex);
      outcome->done = true;
      outcome->info = info;
    }
    QTimer::singleShot(0, this, [this, generation]() { OnQualityDone(generation); });
  }
}

void OnlineSession::JoinQualityWorker() {
  if (quality_thread_.joinable()) {
    quality_thread_.join();
  }
}

void OnlineSession::OnQualityDone(std::uint64_t generation) {
  JoinQualityWorker();
  if (generation != generation_ || !active_.load() || closing_.load()) {
    return;
  }
  const auto outcome = quality_outcome_;
  if (!outcome) {
    return;
  }
  OnlineQualityCatalog info;
  {
    std::lock_guard<std::mutex> lock(outcome->mutex);
    if (!outcome->done) {
      return;
    }
    info = outcome->info;
  }
  quality_outcome_.reset();
  EmitQuality(info);
}

OnlineSession::CacheLookupResult OnlineSession::TryCacheHit(
    const OnlineRequest& request, QString* error,
    QVector<lapian::online::CacheKey>* rebuild_keys) {
  if (rebuild_keys != nullptr) {
    rebuild_keys->clear();
  }
  if (cache_ == nullptr || error == nullptr) {
    return CacheLookupResult::kMiss;
  }
  error->clear();
  QVector<lapian::online::CacheEntry> entries;
  QVector<lapian::online::CacheEntry> broken;
  QString scan_error;
  if (!cache_->Scan(&entries, &broken, &scan_error)) {
    // 缓存根不可读不应阻止在线播放；按未命中继续取流。
    OnlineDiag("T8.17 online_cache_lookup scan_failed bvid=%s cid=%lld\n",
               request.bvid.toUtf8().constData(),
               static_cast<long long>(request.cid));
    return CacheLookupResult::kMiss;
  }
  OnlineDiag("T8.17 online_cache_lookup begin bvid=%s cid=%lld requested_qn=%d "
             "requested_codecid=%d entries=%d broken=%d\n",
             request.bvid.toUtf8().constData(),
             static_cast<long long>(request.cid), request.requested_qn,
             request.requested_codecid, static_cast<int>(entries.size()),
             static_cast<int>(broken.size()));

  const lapian::online::CacheEntry* best = nullptr;
  for (const lapian::online::CacheEntry& entry : entries) {
    if (!entry.complete || entry.broken || entry.key.is_audio()) {
      OnlineDiag("T8.17 online_cache_lookup skip key=%s reason=not_complete_video\n",
                 lapian::online::CacheKeyLabel(entry.key).toUtf8().constData());
      continue;
    }
    if (entry.key.bvid != request.bvid || entry.key.cid != request.cid) {
      continue;
    }
    // T8.12 手动切档：必须命中请求的 qn/codecid；否则继续联网取该档，
    // 不能因为“同片有更高档缓存”而忽略用户选择。
    if (request.requested_qn > 0 && entry.key.qn != request.requested_qn) {
      OnlineDiag("T8.17 online_cache_lookup skip key=%s reason=qn_mismatch "
                 "requested_qn=%d\n",
                 lapian::online::CacheKeyLabel(entry.key).toUtf8().constData(),
                 request.requested_qn);
      continue;
    }
    if (request.requested_codecid > 0 &&
        entry.key.codecid != request.requested_codecid) {
      OnlineDiag("T8.17 online_cache_lookup skip key=%s reason=codecid_mismatch "
                 "requested_codecid=%d\n",
                 lapian::online::CacheKeyLabel(entry.key).toUtf8().constData(),
                 request.requested_codecid);
      continue;
    }
    if (best == nullptr || entry.last_access_at > best->last_access_at ||
        (entry.last_access_at == best->last_access_at &&
         entry.key.qn > best->key.qn)) {
      best = &entry;
    }
  }
  if (best == nullptr) {
    for (const lapian::online::CacheEntry& entry : broken) {
      if (entry.key.is_audio()) {
        continue;
      }
      if (entry.key.bvid == request.bvid && entry.key.cid == request.cid &&
          (request.requested_qn <= 0 ||
           entry.key.qn == request.requested_qn) &&
          (request.requested_codecid <= 0 ||
           entry.key.codecid == request.requested_codecid)) {
        *error = QStringLiteral("缓存损坏，需要重建：%1")
                     .arg(entry.broken_reason.isEmpty()
                              ? QStringLiteral("data.mp4/meta.json 校验失败")
                              : entry.broken_reason);
        if (rebuild_keys != nullptr) {
          rebuild_keys->push_back(entry.key);
        }
        return CacheLookupResult::kError;
      }
    }
    OnlineDiag("T8.17 online_cache_lookup miss reason=no_matching_video "
               "requested_qn=%d requested_codecid=%d\n",
               request.requested_qn, request.requested_codecid);
    return CacheLookupResult::kMiss;
  }
  OnlineDiag("T8.17 online_cache_lookup best key=%s dash=%d audio_id=%d\n",
             lapian::online::CacheKeyLabel(best->key).toUtf8().constData(),
             best->dash ? 1 : 0, best->audio_id);
  if (best->dash) {
    // T8.10：DASH 视频单流必须和配套音频条目同时完整才能命中；缺一个就
    // 明确报“需要重建”，不静默降级成无声播放、也不偷偷联网。
    return TryDashCacheHit(request, *best, error, rebuild_keys);
  }

  QString mp4_path;
  QString open_error;
  if (!cache_->OpenComplete(best->key, &mp4_path, &open_error)) {
    *error = QStringLiteral("缓存条目存在但无法打开：%1").arg(open_error);
    if (rebuild_keys != nullptr) {
      rebuild_keys->push_back(best->key);
    }
    return CacheLookupResult::kError;
  }

  cache_entry_ = *best;
  cache_hit_ = true;
  dash_mode_ = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    part_path_ = mp4_path;
    total_ = cache_entry_.total_bytes;
    qn_ = cache_entry_.key.qn;
    codecid_ = cache_entry_.key.codecid;
    progress_.published = cache_entry_.total_bytes;
    progress_.total = cache_entry_.total_bytes;
    progress_.bps = 0.0;
    progress_.job_state = QStringLiteral("cache_hit");
    progress_.qn = qn_;
    progress_.codecid = codecid_;
    progress_.complete = true;
    progress_.awaiting_finalize = false;
    progress_.error.clear();
  }
  SetState(OnlineState::kCacheHit,
           QStringLiteral("缓存命中：直接打开本地 data.mp4（0 网络）"));
  const std::uint64_t generation = generation_;
  QTimer::singleShot(0, this, [this, generation]() {
    if (generation == generation_ && active_.load() && !closing_.load()) {
      EmitCacheHitReady();
    }
  });
  PublishProgress();
  return CacheLookupResult::kHit;
}

OnlineSession::CacheLookupResult OnlineSession::TryDashCacheHit(
    const OnlineRequest& request, const lapian::online::CacheEntry& video,
    QString* error, QVector<lapian::online::CacheKey>* rebuild_keys) {
  using namespace lapian::online;
  OnlineDiag("T8.17 online_dash_cache video=%s audio_id=%d\n",
             CacheKeyLabel(video.key).toUtf8().constData(), video.audio_id);
  if (video.audio_id <= 0) {
    // T8.17：meta 缺少 audio_id 不代表视频数据坏了；不删视频，继续解析实际
    // 目录后按实际 audio key 做精确检查（0 网络命中或补齐）。
    partial_cache_notice_ = QStringLiteral(
        "视频缓存缺少配套 audio_id，需要重新解析音频；不会删除已缓存视频");
    OnlineDiag("T8.17 online_dash_cache result=miss reason=missing_audio_id "
               "video=%s\n",
               CacheKeyLabel(video.key).toUtf8().constData());
    return CacheLookupResult::kMiss;
  }
  QVector<CacheEntry> entries;
  QVector<CacheEntry> broken;
  if (!cache_->Scan(&entries, &broken, nullptr)) {
    return CacheLookupResult::kMiss;
  }
  CacheKey audio_key;
  audio_key.bvid = request.bvid;
  audio_key.cid = request.cid;
  audio_key.kind = CacheStreamKind::kAudio;
  audio_key.audio_id = video.audio_id;
  const CacheEntry* audio = nullptr;
  for (const CacheEntry& entry : entries) {
    if (entry.key == audio_key && entry.complete) {
      audio = &entry;
      break;
    }
  }
  for (const CacheEntry& entry : broken) {
    if (entry.key == audio_key) {
      *error = QStringLiteral("DASH 音频缓存损坏，需要重建/补音频：%1")
                   .arg(entry.broken_reason.isEmpty()
                            ? QStringLiteral("data.mp4/meta.json 校验失败")
                            : entry.broken_reason);
      partial_cache_notice_ =
          QStringLiteral("音频缓存损坏，将只重建音频（保留视频缓存）");
      if (rebuild_keys != nullptr) {
        // T8.17：只重建音频，保留完整视频；重建后按未命中继续补音频。
        rebuild_keys->push_back(audio_key);
      }
      return CacheLookupResult::kError;
    }
  }
  if (audio == nullptr) {
    // T8.17：视频完整、音频缺失时保留视频，继续解析实际音频 id 后只补音频；
    // 不再删视频重下，也不把“视频完整但音频缺失”报成完整缓存命中失败。
    partial_cache_notice_ =
        QStringLiteral("视频已完整缓存（%1），缺少音频（audio-%2）；"
                       "需要补下音频")
            .arg(QualityName(video.key.qn))
            .arg(video.audio_id);
    OnlineDiag("T8.17 online_dash_cache result=miss reason=audio_missing "
               "audio_key=%s\n",
               CacheKeyLabel(audio_key).toUtf8().constData());
    return CacheLookupResult::kMiss;
  }

  QString video_path;
  QString audio_path;
  QString open_error;
  if (!cache_->OpenComplete(video.key, &video_path, &open_error)) {
    *error = QStringLiteral("DASH 视频缓存无法打开：%1").arg(open_error);
    if (rebuild_keys != nullptr) {
      rebuild_keys->push_back(video.key);
      rebuild_keys->push_back(audio_key);
    }
    return CacheLookupResult::kError;
  }
  if (!cache_->OpenComplete(audio_key, &audio_path, &open_error)) {
    *error = QStringLiteral("DASH 音频缓存无法打开：%1").arg(open_error);
    if (rebuild_keys != nullptr) {
      rebuild_keys->push_back(video.key);
      rebuild_keys->push_back(audio_key);
    }
    return CacheLookupResult::kError;
  }

  cache_entry_ = video;
  cache_entry_audio_ = *audio;
  cache_hit_ = true;
  dash_mode_ = true;
  source_ = std::make_shared<GrowingSource>(video.total_bytes);
  audio_source_ = std::make_shared<GrowingSource>(audio->total_bytes);
  if (!source_->IsValid() || !audio_source_->IsValid()) {
    *error = QStringLiteral("DASH 缓存条目大小无效，需要重建");
    source_.reset();
    audio_source_.reset();
    return CacheLookupResult::kError;
  }
  // 完整缓存：直接发布全部字节；仍用 GrowingSource 统一双输入读路径，0 网络。
  source_->Publish(video.total_bytes);
  audio_source_->Publish(audio->total_bytes);
  total_ = video.total_bytes;
  audio_total_ = audio->total_bytes;
  qn_ = video.key.qn;
  codecid_ = video.key.codecid;
  audio_id_ = video.audio_id;
  dash_fps_ = video.fps > 0.0 ? video.fps
                              : (audio->fps > 0.0 ? audio->fps : 0.0);
  dash_duration_ms_ = video.duration_ms;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    part_path_ = video_path;
    audio_part_path_ = audio_path;
    progress_.published = total_ + audio_total_;
    progress_.total = total_ + audio_total_;
    progress_.bps = 0.0;
    progress_.job_state = QStringLiteral("cache_hit");
    progress_.qn = qn_;
    progress_.codecid = codecid_;
    progress_.complete = true;
    progress_.awaiting_finalize = false;
    progress_.error.clear();
  }
  SetState(OnlineState::kCacheHit,
           QStringLiteral("缓存命中：DASH 视频+音频（0 网络）"));
  OnlineDiag("T8.17 online_dash_cache result=hit video=%s audio=%s\n",
             CacheKeyLabel(video.key).toUtf8().constData(),
             CacheKeyLabel(audio_key).toUtf8().constData());
  const std::uint64_t generation = generation_;
  QTimer::singleShot(0, this, [this, generation]() {
    if (generation == generation_ && active_.load() && !closing_.load()) {
      EmitCacheHitReady();
    }
  });
  PublishProgress();
  return CacheLookupResult::kHit;
}

OnlineSession::DashCacheInspection OnlineSession::InspectDashCache(
    const lapian::online::CacheKey& video_key,
    const lapian::online::CacheKey& audio_key) {
  using namespace lapian::online;
  DashCacheInspection result;
  auto inspect = [&](const CacheKey& key, bool* complete, bool* broken,
                     CacheEntry* entry, QString* path, QString* error) {
    if (cache_ == nullptr) {
      *error = QStringLiteral("在线缓存未初始化");
      return;
    }
    QString load_error;
    const std::optional<CacheEntry> loaded = cache_->Load(key, &load_error);
    if (!loaded.has_value()) {
      *error = load_error.isEmpty() ? QStringLiteral("条目不存在") : load_error;
      return;
    }
    *entry = *loaded;
    if (loaded->broken) {
      *broken = true;
      *error = loaded->broken_reason.isEmpty()
                   ? QStringLiteral("data.mp4/meta.json 校验失败")
                   : loaded->broken_reason;
      return;
    }
    if (!loaded->complete) {
      *error = QStringLiteral("未完成：%1/%2 字节")
                   .arg(loaded->downloaded_bytes)
                   .arg(loaded->total_bytes);
      return;
    }
    QString open_error;
    if (!cache_->OpenComplete(key, path, &open_error)) {
      *error = open_error;
      return;
    }
    *complete = true;
  };
  inspect(video_key, &result.video_complete, &result.video_broken,
          &result.video, &result.video_path, &result.video_error);
  inspect(audio_key, &result.audio_complete, &result.audio_broken,
          &result.audio, &result.audio_path, &result.audio_error);
  OnlineDiag(
      "T8.17 online_dash_exact video=%s complete=%d broken=%d error=%s "
      "audio=%s complete=%d broken=%d error=%s\n",
      CacheKeyLabel(video_key).toUtf8().constData(),
      result.video_complete ? 1 : 0, result.video_broken ? 1 : 0,
      result.video_error.toUtf8().constData(),
      CacheKeyLabel(audio_key).toUtf8().constData(),
      result.audio_complete ? 1 : 0, result.audio_broken ? 1 : 0,
      result.audio_error.toUtf8().constData());
  return result;
}

bool OnlineSession::EmitResolvedDashCacheHit(
    const ResolveOutcome& outcome, const lapian::online::CacheKey& video_key,
    const lapian::online::CacheKey& audio_key,
    const DashCacheInspection& cache) {
  using namespace lapian::online;
  if (!cache.video_complete || !cache.audio_complete) {
    return false;
  }
  cache_entry_ = cache.video;
  cache_entry_audio_ = cache.audio;
  if (cache_entry_.fps <= 0.0) {
    cache_entry_.fps = outcome.video.fps;
  }
  cache_hit_ = true;
  dash_mode_ = true;
  video_reused_complete_ = true;
  audio_reused_complete_ = true;
  source_ = std::make_shared<GrowingSource>(cache.video.total_bytes);
  audio_source_ = std::make_shared<GrowingSource>(cache.audio.total_bytes);
  if (!source_->IsValid() || !audio_source_->IsValid()) {
    source_.reset();
    audio_source_.reset();
    return false;
  }
  source_->Publish(cache.video.total_bytes);
  audio_source_->Publish(cache.audio.total_bytes);
  total_ = cache.video.total_bytes;
  audio_total_ = cache.audio.total_bytes;
  qn_ = video_key.qn;
  codecid_ = video_key.codecid;
  audio_id_ = audio_key.audio_id;
  dash_fps_ = outcome.video.fps > 0.0 ? outcome.video.fps : cache_entry_.fps;
  dash_duration_ms_ = cache.video.duration_ms > 0
                          ? cache.video.duration_ms
                          : dash_catalog_.duration_ms;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    part_path_ = cache.video_path;
    audio_part_path_ = cache.audio_path;
    progress_.published = total_ + audio_total_;
    progress_.total = total_ + audio_total_;
    progress_.bps = 0.0;
    progress_.job_state = QStringLiteral("cache_hit");
    progress_.qn = qn_;
    progress_.codecid = codecid_;
    progress_.complete = true;
    progress_.awaiting_finalize = false;
    progress_.error.clear();
  }
  SetState(OnlineState::kCacheHit,
           QStringLiteral("缓存命中：DASH 视频+音频（0 网络）"));
  OnlineDiag("T8.17 online_dash_cache result=hit_after_resolve video=%s "
             "audio=%s\n",
             CacheKeyLabel(video_key).toUtf8().constData(),
             CacheKeyLabel(audio_key).toUtf8().constData());
  const std::uint64_t generation = generation_;
  QTimer::singleShot(0, this, [this, generation]() {
    if (generation == generation_ && active_.load() && !closing_.load()) {
      EmitCacheHitReady();
    }
  });
  PublishProgress();
  return true;
}

void OnlineSession::EmitDashQualityCatalog() {
  if (!dash_catalog_available_) {
    StartQualityWorker(generation_);
    return;
  }
  OnlineQualityCatalog catalog;
  catalog.ok = true;
  catalog.fetched = true;
  catalog.catalog = dash_catalog_;
  catalog.http_requests = dash_requests_;
  catalog.http_status = 200;
  EmitQuality(catalog);
}

void OnlineSession::EmitCacheHitReady() {
  if (ready_emitted_ || !cache_hit_) {
    return;
  }
  ready_emitted_ = true;
  SetState(OnlineState::kReady, QStringLiteral("缓存命中，已打开"));
  if (!ready_callback_) {
    return;
  }
  OnlineReadyInfo info;
  info.cache_hit = true;
  info.cache_mp4_path = cache_mp4_path();
  info.part_path = info.cache_mp4_path;
  info.request = request_;
  info.title = request_.title.isEmpty() ? cache_entry_.title : request_.title;
  info.owner = request_.owner.isEmpty() ? cache_entry_.owner : request_.owner;
  info.duration_ms =
      cache_entry_.duration_ms > 0 ? cache_entry_.duration_ms
                                   : request_.duration_ms;
  info.total_bytes = cache_entry_.total_bytes;
  info.qn = cache_entry_.key.qn;
  info.codecid = cache_entry_.key.codecid;
  info.fps = cache_entry_.fps;
  info.playurl.quality = info.qn;
  info.playurl.codecid = info.codecid;
  info.playurl.format = QStringLiteral("mp4");
  info.playurl.timelength_ms = info.duration_ms;
  if (dash_mode_) {
    info.dash = true;
    info.source = source_;
    info.audio_source = audio_source_;
    info.audio_part_path = cache_audio_mp4_path();
    info.audio_id = audio_id_;
    info.audio_total_bytes = audio_total_;
    info.dash_fps = dash_fps_;
    info.dash_duration_ms = info.duration_ms;
    info.playurl.format = QStringLiteral("dash");
  }
  ready_callback_(info);

  // T8.9：缓存命中不联网；档位来自 meta（qn/codecid；meta 未记录 fps/分辨率时
  // 菜单只显示档位名，不伪造）。T8.17：解析后命中的路径已有完整目录，菜单
  // 仍可列出其它档位，不能被“缓存 meta 只有当前档”锁死。
  if (!quality_emitted_ && !quality_started_) {
    quality_started_ = true;
    if (dash_mode_ && dash_catalog_available_) {
      EmitDashQualityCatalog();
    } else {
      EmitQuality(CacheQualityFromEntry());
    }
  }
}

OnlineQualityCatalog OnlineSession::CacheQualityFromEntry() const {
  OnlineQualityCatalog info;
  info.fetched = true;
  info.ok = cache_entry_.key.qn > 0;
  info.cache_hit = true;
  lapian::online::DashCatalog& catalog = info.catalog;
  catalog.duration_ms = cache_entry_.duration_ms;
  catalog.claimed_quality = cache_entry_.key.qn;
  if (cache_entry_.key.qn > 0) {
    lapian::online::DashStream stream;
    stream.qn = cache_entry_.key.qn;
    stream.codecid = cache_entry_.key.codecid;
    stream.width = cache_entry_.width;
    stream.height = cache_entry_.height;
    stream.fps = cache_entry_.fps;
    // size 未知：缓存数据总字节不是单流字节（DASH 时更不成立），不填。
    catalog.videos.append(stream);
    lapian::online::QualityTier tier;
    tier.qn = stream.qn;
    tier.width = stream.width;
    tier.height = stream.height;
    tier.fps = stream.fps;
    tier.bandwidth = 0;
    tier.streams.append(stream);
    catalog.tiers.append(tier);
  }
  if (cache_entry_.dash && cache_entry_.audio_id > 0) {
    // DASH 缓存命中：菜单需要知道有独立音频流，否则会误报“没有音频流”。
    lapian::online::DashAudioStream audio;
    audio.id = cache_entry_.audio_id;
    audio.bandwidth = 0;
    audio.urls_ok = false;
    catalog.audios.append(audio);
  }
  return info;
}

void OnlineSession::EmitQuality(const OnlineQualityCatalog& info) {
  if (quality_emitted_) {
    return;
  }
  quality_emitted_ = true;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quality_ = info;
  }
  quality_requests_ += info.http_requests;
  if (quality_callback_) {
    quality_callback_(info);
  }
}

bool OnlineSession::RebuildCorruptEntries(
    const QVector<lapian::online::CacheKey>& keys, QString* error) {
  if (cache_ == nullptr) {
    if (error != nullptr) {
      *error = QStringLiteral("在线缓存未初始化");
    }
    return false;
  }
  for (const lapian::online::CacheKey& key : keys) {
    QString one_error;
    if (!cache_->Rebuild(key, &one_error)) {
      if (error != nullptr) {
        *error = one_error.isEmpty()
                     ? QStringLiteral("删除损坏缓存失败（%1）")
                           .arg(lapian::online::CacheKeyLabel(key))
                     : one_error;
      }
      return false;
    }
  }
  return true;
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
  audio_total_ = 0;
  qn_ = 0;
  codecid_ = 0;
  audio_id_ = 0;
  dash_mode_ = false;
  dash_fps_ = 0.0;
  dash_duration_ms_ = 0;
  dash_catalog_ = lapian::online::DashCatalog{};
  dash_catalog_available_ = false;
  dash_requests_ = 0;
  dash_fallback_reason_.clear();
  audio_ready_threshold_ = 0;
  quality_started_ = false;
  quality_emitted_ = false;
  quality_requests_ = 0;
  quality_outcome_.reset();
  quality_cancel_.reset();
  ++generation_;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = OnlineState::kPreparing;
    progress_ = OnlineProgress{};
    part_path_.clear();
    audio_part_path_.clear();
    last_error_.clear();
    quality_ = OnlineQualityCatalog{};
  }
  cache_hit_ = false;
  cache_mp4_path_.clear();
  cache_audio_mp4_path_.clear();
  cache_entry_ = lapian::online::CacheEntry{};
  cache_entry_audio_ = lapian::online::CacheEntry{};
  partial_cache_notice_.clear();
  video_reused_complete_ = false;
  audio_reused_complete_ = false;
  resolve_outcome_.reset();
  resolve_cancel_.reset();
  if (state_callback_) {
    state_callback_(OnlineState::kPreparing, QStringLiteral("准备取流…"));
  }

  // T8.7：完整缓存命中优先于 playurl/DownloadJob。命中时 0 网络；损坏条目
  // T8.13：检测到损坏/不完整时先 Rebuild（只删该条目数据，不动在线记录/截图），
  // 然后按未命中重新取流 —— O7-A5/O5-A7“检测/重建；重试成功；不显示错误帧”。
  QVector<lapian::online::CacheKey> rebuild_keys;
  const CacheLookupResult cache_result =
      TryCacheHit(request, error, &rebuild_keys);
  if (cache_result == CacheLookupResult::kError) {
    const QString original_error = *error;
    QString rebuild_error;
    if (!rebuild_keys.isEmpty() &&
        RebuildCorruptEntries(rebuild_keys, &rebuild_error)) {
      SetState(OnlineState::kRecovering,
               QStringLiteral("缓存损坏，已重建，正在重新下载…"));
    } else {
      active_.store(false);
      if (!rebuild_error.isEmpty()) {
        *error = QStringLiteral("%1；重建失败：%2")
                     .arg(original_error, rebuild_error);
      }
      SetState(OnlineState::kError, *error);
      return false;
    }
  } else if (cache_result == CacheLookupResult::kHit) {
    return true;
  }
  if (cache_result == CacheLookupResult::kMiss &&
      !partial_cache_notice_.isEmpty()) {
    // T8.17：视频完整/音频缺失不是错误；给 UI 明确“正在解析/补下音频”。
    // 损坏重建路径保留 kRecovering 的“已重建”提示，随后由 StartDashDownload
    // 覆盖成“补下音频/视频”的准确状态。
    SetState(OnlineState::kPreparing, partial_cache_notice_);
  }

  if (has_injected_playurl_) {
    // 测试注入：仍走同一下载/读层/回调路径，但不联网。
    ++playurl_requests_;
    const std::uint64_t generation = generation_;
    QTimer::singleShot(0, this, [this, generation]() {
      if (generation == generation_ && active_.load() && !closing_.load()) {
        StartDurlDownload(injected_playurl_, generation);
      }
    });
    return true;
  }
  StartResolveWorker(generation_);
  if (state() == OnlineState::kError) {
    std::lock_guard<std::mutex> lock(mutex_);
    *error = last_error_;
    return false;
  }
  return true;
}

void OnlineSession::StartDurlDownload(const lapian::online::PlayUrlData& data,
                                      std::uint64_t generation) {
  (void)generation;
  playurl_data_ = data;
  lapian::online::DownloadPlan plan;
  lapian::online::CacheKey key;
  QString plan_error;
  if (!BuildPlan(data, &plan, &key, &plan_error)) {
    Fail(plan_error);
    return;
  }
  dash_mode_ = false;
  total_ = plan.total_bytes;
  qn_ = plan.qn;
  codecid_ = plan.codecid;
  audio_id_ = 0;
  audio_total_ = 0;
  if (ready_threshold_ <= 0) {
    ready_threshold_ = std::min<qint64>(kReadyThresholdBytes, total_);
  } else {
    ready_threshold_ = std::min<qint64>(ready_threshold_, total_);
  }

  {
    const ScopedSlowLog source_log("source_create");
    source_ = std::make_shared<lapian::online::GrowingSource>(total_);
    if (!source_->IsValid()) {
      Fail(source_->InvalidReason());
      return;
    }
  }

  if (injected_transport_) {
    transport_ = std::move(injected_transport_);
  } else {
    transport_ = std::make_unique<lapian::online::WinHttpDownloadTransport>();
  }
  if (injected_http_) {
    // 保持注入；刷新回调直接用。
  } else if (!http_) {
    http_ = std::make_unique<lapian::online::WinHttpClient>();
  }

  lapian::online::DownloadOptions options;
  options.defer_finalize = true;
  options.throttle_bytes_per_sec = download_throttle_bps_;
  options.stall_timeout_ms = lapian::online::kDownloadStallTimeoutMs;

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
    fetch_options.timeout_ms = lapian::online::kOnlineApiTimeoutMs;
    fetch_options.sessdata = request_.sessdata;
    // 刷新必须与初始请求同一档位，否则可能换回更低档并改变 plan.qn。
    const int refresh_qn = request_.requested_qn > 0
                               ? request_.requested_qn
                               : lapian::online::kM1RequestQn;
    lapian::online::PlayUrlFetchResult result = lapian::online::FetchPlayUrl(
        http, request_.bvid, request_.cid, refresh_qn, fetch_options, cancel);
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
  {
    const ScopedSlowLog job_log("job_start");
    if (!job->Start(&start_error)) {
      Fail(start_error.isEmpty() ? QStringLiteral("无法开始下载") : start_error);
      job.reset();
      return;
    }
  }
  job_ = std::move(job);
  if (job_->state() == lapian::online::DownloadState::kCompleted) {
    // T8.17：kCompleted 是内部信号（缓存竞态防御），转成正常缓存命中路径，
    // 不作为用户错误。
    QString mp4_path;
    QString open_error;
    const std::optional<lapian::online::CacheEntry> loaded =
        cache_->Load(key, nullptr);
    if (!loaded.has_value() ||
        !cache_->OpenComplete(key, &mp4_path, &open_error)) {
      Fail(QStringLiteral("缓存已完整但无法打开：%1")
               .arg(open_error.isEmpty() ? QStringLiteral("条目不存在")
                                         : open_error));
      return;
    }
    cache_entry_ = *loaded;
    cache_hit_ = true;
    dash_mode_ = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      part_path_ = mp4_path;
      total_ = cache_entry_.total_bytes;
      qn_ = key.qn;
      codecid_ = key.codecid;
      progress_.published = total_;
      progress_.total = total_;
      progress_.bps = 0.0;
      progress_.job_state = QStringLiteral("cache_hit");
      progress_.qn = qn_;
      progress_.codecid = codecid_;
      progress_.complete = true;
      progress_.awaiting_finalize = false;
      progress_.error.clear();
    }
    OnlineDiag("T8.17 online_job_start key=%s state=completed_convert_to_hit\n",
               lapian::online::CacheKeyLabel(key).toUtf8().constData());
    SetState(OnlineState::kCacheHit,
             QStringLiteral("缓存命中：直接打开本地 data.mp4（0 网络）"));
    const std::uint64_t cache_generation = generation_;
    QTimer::singleShot(0, this, [this, cache_generation]() {
      if (cache_generation == generation_ && active_.load() &&
          !closing_.load()) {
        EmitCacheHitReady();
      }
    });
    PublishProgress();
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

void OnlineSession::StartDashDownload(const ResolveOutcome& outcome,
                                      std::uint64_t generation) {
  (void)generation;
  lapian::online::DownloadPlan video_plan;
  lapian::online::DownloadPlan audio_plan;
  lapian::online::CacheKey video_key;
  lapian::online::CacheKey audio_key;
  QString plan_error;
  if (!BuildDashPlans(outcome, &video_plan, &audio_plan, &video_key, &audio_key,
                      &plan_error)) {
    Fail(plan_error);
    return;
  }
  OnlineDiag("T8.17 online_dash_keys bvid=%s cid=%lld requested_qn=%d "
             "requested_codecid=%d actual_qn=%d actual_codecid=%d video=%s "
             "audio=%s video_total=%lld audio_total=%lld\n",
             request_.bvid.toUtf8().constData(),
             static_cast<long long>(request_.cid), request_.requested_qn,
             request_.requested_codecid, video_plan.qn, video_plan.codecid,
             lapian::online::CacheKeyLabel(video_key).toUtf8().constData(),
             lapian::online::CacheKeyLabel(audio_key).toUtf8().constData(),
             static_cast<long long>(video_plan.total_bytes),
             static_cast<long long>(audio_plan.total_bytes));
  dash_mode_ = true;
  total_ = video_plan.total_bytes;
  audio_total_ = audio_plan.total_bytes;
  qn_ = video_plan.qn;
  codecid_ = video_plan.codecid;
  audio_id_ = outcome.audio.id;
  dash_fps_ = outcome.video.fps;
  dash_duration_ms_ = video_plan.metadata.duration_ms;
  if (ready_threshold_ <= 0) {
    ready_threshold_ = std::min<qint64>(kReadyThresholdBytes, total_);
  } else {
    ready_threshold_ = std::min<qint64>(ready_threshold_, total_);
  }
  audio_ready_threshold_ = std::min<qint64>(kAudioReadyThresholdBytes, audio_total_);

  // T8.17：解析出**实际** video/audio key 后、创建 DownloadJob 前做精确检查。
  // 不能只看 requested_qn/requested_codecid（切到新档时旧音频通常已完整）。
  DashCacheInspection cache = InspectDashCache(video_key, audio_key);
  if (cache.video_complete &&
      cache.video.total_bytes != video_plan.total_bytes) {
    OnlineDiag("T8.17 online_dash_stale key=%s cached_bytes=%lld plan_bytes=%lld\n",
               lapian::online::CacheKeyLabel(video_key).toUtf8().constData(),
               static_cast<long long>(cache.video.total_bytes),
               static_cast<long long>(video_plan.total_bytes));
    cache.video_broken = true;
  }
  if (cache.audio_complete &&
      cache.audio.total_bytes != audio_plan.total_bytes) {
    OnlineDiag("T8.17 online_dash_stale key=%s cached_bytes=%lld plan_bytes=%lld\n",
               lapian::online::CacheKeyLabel(audio_key).toUtf8().constData(),
               static_cast<long long>(cache.audio.total_bytes),
               static_cast<long long>(audio_plan.total_bytes));
    cache.audio_broken = true;
  }
  auto rebuild_broken = [&](const lapian::online::CacheKey& key, bool* complete,
                            bool* broken, QString* path,
                            const char* label) -> bool {
    if (!*broken) {
      return true;
    }
    QString rebuild_error;
    if (!cache_->Rebuild(key, &rebuild_error)) {
      Fail(QStringLiteral("DASH %1缓存损坏且重建失败：%2")
               .arg(QString::fromUtf8(label),
                    rebuild_error.isEmpty() ? QStringLiteral("未知错误")
                                            : rebuild_error));
      return false;
    }
    OnlineDiag("T8.17 online_dash_rebuild key=%s label=%s\n",
               lapian::online::CacheKeyLabel(key).toUtf8().constData(), label);
    *broken = false;
    *complete = false;
    path->clear();
    return true;
  };
  if (!rebuild_broken(video_key, &cache.video_complete, &cache.video_broken,
                      &cache.video_path, "视频") ||
      !rebuild_broken(audio_key, &cache.audio_complete, &cache.audio_broken,
                      &cache.audio_path, "音频")) {
    return;
  }
  if (cache.video_complete && cache.audio_complete) {
    if (!EmitResolvedDashCacheHit(outcome, video_key, audio_key, cache)) {
      Fail(QStringLiteral("DASH 缓存条目大小无效，需要重建"));
    }
    return;
  }

  {
    const ScopedSlowLog source_log("source_create");
    source_ = std::make_shared<lapian::online::GrowingSource>(total_);
    audio_source_ =
        std::make_shared<lapian::online::GrowingSource>(audio_total_);
    if (!source_->IsValid() || !audio_source_->IsValid()) {
      Fail(!source_->IsValid() ? source_->InvalidReason()
                               : audio_source_->InvalidReason());
      return;
    }
    if (cache.video_complete) {
      source_->Publish(total_);
      video_reused_complete_ = true;
    }
    if (cache.audio_complete) {
      audio_source_->Publish(audio_total_);
      audio_reused_complete_ = true;
    }
  }
  OnlineDiag("T8.17 online_dash_reuse video_complete=%d audio_complete=%d\n",
             cache.video_complete ? 1 : 0, cache.audio_complete ? 1 : 0);

  if (injected_transport_) {
    transport_ = std::move(injected_transport_);
  } else {
    transport_ = std::make_unique<lapian::online::WinHttpDownloadTransport>();
  }

  lapian::online::DownloadOptions options;
  options.defer_finalize = true;
  options.throttle_bytes_per_sec = download_throttle_bps_;
  options.stall_timeout_ms = lapian::online::kDownloadStallTimeoutMs;

  const int video_qn = outcome.video.qn;
  const int video_codecid = outcome.video.codecid;
  const int audio_id = outcome.audio.id;
  auto make_refresh = [this, video_qn, video_codecid, audio_id](bool audio) {
    return [this, video_qn, video_codecid, audio_id, audio](
               lapian::online::DownloadPlan* fresh,
               const lapian::online::HttpCancelToken& cancel,
               QString* refresh_error) -> bool {
      std::unique_ptr<lapian::online::WinHttpClient> owned;
      lapian::online::IHttpClient* http =
          injected_http_ ? injected_http_.get() : nullptr;
      if (http == nullptr) {
        owned = std::make_unique<lapian::online::WinHttpClient>();
        http = owned.get();
      }
      lapian::online::DashFetchOptions fetch_options;
      fetch_options.timeout_ms = lapian::online::kOnlineApiTimeoutMs;
      fetch_options.sessdata = request_.sessdata;
      fetch_options.qn = request_.requested_qn > 0
                             ? request_.requested_qn
                             : lapian::online::kDashRequestQn;
      fetch_options.fourk = 1;
      const lapian::online::DashFetchResult result =
          lapian::online::FetchDashCatalog(http, request_.bvid, request_.cid,
                                           fetch_options, cancel);
      lapian::online::ClearSecret(&fetch_options.sessdata);
      if (!result.error.ok()) {
        *refresh_error = result.error.message.isEmpty()
                             ? QStringLiteral("刷新 DASH 取流失败")
                             : result.error.message;
        return false;
      }
      if (audio) {
        const lapian::online::DashAudioStream* stream = nullptr;
        for (const auto& candidate : result.catalog.audios) {
          if (candidate.id == audio_id) {
            stream = &candidate;
            break;
          }
        }
        if (stream == nullptr) {
          *refresh_error = QStringLiteral("刷新后 DASH 音频流消失");
          return false;
        }
        fresh->urls = stream->AllUrls();
        fresh->qn = 0;
        fresh->codecid = 0;
        fresh->format = QStringLiteral("dash-audio");
      } else {
        const lapian::online::DashStream* stream = result.catalog.FindStream(
            video_qn, video_codecid);
        if (stream == nullptr) {
          *refresh_error = QStringLiteral("刷新后 DASH 视频档位消失");
          return false;
        }
        fresh->urls = stream->AllUrls();
        fresh->qn = stream->qn;
        fresh->codecid = stream->codecid;
        fresh->format = QStringLiteral("dash");
      }
      if (fresh->urls.isEmpty()) {
        *refresh_error = QStringLiteral("刷新后没有可用下载地址");
        return false;
      }
      return true;
    };
  };

  lapian::online::DownloadOptions audio_options = options;
  audio_options.throttle_bytes_per_sec = audio_download_throttle_bps_;
  std::unique_ptr<lapian::online::DownloadJob> video_job;
  std::unique_ptr<lapian::online::DownloadJob> audio_job;
  if (!cache.video_complete) {
    video_job = std::make_unique<lapian::online::DownloadJob>(
        cache_, video_key, source_, transport_.get(), video_plan,
        make_refresh(false), options);
  }
  if (!cache.audio_complete) {
    audio_job = std::make_unique<lapian::online::DownloadJob>(
        cache_, audio_key, audio_source_, transport_.get(), audio_plan,
        make_refresh(true), audio_options);
  }
  QString start_error;
  {
    const ScopedSlowLog job_log("job_start_dash");
    if (video_job != nullptr && !video_job->Start(&start_error)) {
      Fail(start_error.isEmpty() ? QStringLiteral("无法开始 DASH 视频下载")
                                 : start_error);
      return;
    }
    if (audio_job != nullptr && !audio_job->Start(&start_error)) {
      if (video_job != nullptr) {
        video_job->Cancel();
      }
      source_->Cancel();
      audio_source_->Cancel();
      Fail(start_error.isEmpty() ? QStringLiteral("无法开始 DASH 音频下载")
                                 : start_error);
      return;
    }
    // T8.17：DownloadJob::Start 发现完整缓存时返回 kCompleted。这是内部信号
    // （竞态防御），不是用户错误：发布已完整字节、复用该路数据，不报错。
    auto adopt_completed = [&](lapian::online::DownloadJob* job,
                               const lapian::online::CacheKey& key,
                               std::shared_ptr<lapian::online::GrowingSource> src,
                               qint64 total, QString* path,
                               bool* reused, const char* label) -> bool {
      if (job == nullptr ||
          job->state() != lapian::online::DownloadState::kCompleted) {
        return true;
      }
      QString mp4_path;
      QString open_error;
      if (!cache_->OpenComplete(key, &mp4_path, &open_error)) {
        Fail(QStringLiteral("DASH %1缓存状态不一致：%2")
                 .arg(QString::fromUtf8(label), open_error));
        return false;
      }
      src->Publish(total);
      *path = mp4_path;
      *reused = true;
      OnlineDiag("T8.17 online_job_already_complete stream=%s key=%s "
                 "already_complete=%d\n",
                 label, lapian::online::CacheKeyLabel(key).toUtf8().constData(),
                 job->stats().already_complete ? 1 : 0);
      return true;
    };
    if (!adopt_completed(video_job.get(), video_key, source_, total_,
                         &cache.video_path, &video_reused_complete_, "video") ||
        !adopt_completed(audio_job.get(), audio_key, audio_source_, audio_total_,
                         &cache.audio_path, &audio_reused_complete_, "audio")) {
      return;
    }
  }
  job_ = std::move(video_job);
  audio_job_ = std::move(audio_job);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    part_path_ = cache.video_complete || video_reused_complete_
                     ? cache.video_path
                     : (job_ != nullptr ? job_->part_path() : QString());
    audio_part_path_ =
        cache.audio_complete || audio_reused_complete_
            ? cache.audio_path
            : (audio_job_ != nullptr ? audio_job_->part_path() : QString());
  }
  if (cache.video_complete && !cache.audio_complete) {
    SetState(OnlineState::kBuffering,
             QStringLiteral("视频已完整缓存（%1），正在补下音频（audio-%2）")
                 .arg(lapian::online::QualityName(video_plan.qn))
                 .arg(audio_plan.metadata.audio_id));
  } else if (cache.audio_complete && !cache.video_complete) {
    SetState(OnlineState::kBuffering,
             QStringLiteral("音频已完整缓存，正在下载请求的视频档"
                            "（%1 codecid=%2）")
                 .arg(lapian::online::QualityName(video_plan.qn))
                 .arg(video_plan.codecid));
  } else {
    SetState(OnlineState::kBuffering, QStringLiteral("缓冲中 0%"));
  }
  poll_timer_->start();
  PublishProgress();
  Poll();
}

void OnlineSession::OnResolveDone(std::uint64_t generation) {
  const ScopedSlowLog slow_log("resolve_done");
  JoinResolveWorker();
  if (generation != generation_ || !active_.load()) {
    return;
  }
  std::shared_ptr<ResolveOutcome> outcome = resolve_outcome_;
  if (!outcome) {
    Fail(QStringLiteral("取流结果丢失"));
    return;
  }
  bool done = false;
  bool dash = false;
  lapian::online::DashFetchResult dash_result;
  lapian::online::PlayUrlFetchResult durl_result;
  bool durl_fetched = false;
  QString fallback_reason;
  QString failure;
  {
    std::lock_guard<std::mutex> lock(outcome->mutex);
    done = outcome->done;
    dash = outcome->dash;
    dash_result = outcome->dash_result;
    durl_result = outcome->durl_result;
    durl_fetched = outcome->durl_fetched;
    fallback_reason = outcome->fallback_reason;
    failure = outcome->failure;
  }
  if (!done) {
    Fail(QStringLiteral("取流未完成"));
    return;
  }
  dash_requests_ += dash_result.http_requests;
  if (!failure.isEmpty()) {
    Fail(failure);
    return;
  }
  if (dash_result.error.ok()) {
    dash_catalog_ = dash_result.catalog;
    dash_catalog_available_ = true;
  }
  dash_fallback_reason_ = fallback_reason;
  if (dash) {
    StartDashDownload(*outcome, generation);
    return;
  }
  if (durl_fetched) {
    ++playurl_requests_;
  }
  if (!durl_result.error.ok()) {
    Fail(durl_result.error.message.isEmpty() ? QStringLiteral("取流失败")
                                               : durl_result.error.message);
    return;
  }
  if (!fallback_reason.isEmpty()) {
    SetState(OnlineState::kPreparing, fallback_reason);
  }
  StartDurlDownload(durl_result.data, generation);
}

void OnlineSession::EmitReady() {
  if (ready_emitted_) {
    return;
  }
  ready_emitted_ = true;
  SetState(OnlineState::kReady, QStringLiteral("首帧准备中"));
  if (!ready_callback_) {
    return;
  }
  OnlineReadyInfo info;
  info.source = source_;
  info.part_path = part_path();
  info.playurl = playurl_data_;
  info.request = request_;
  info.playurl.quality = qn_;
  info.playurl.codecid = codecid_;
  info.total_bytes = total_;
  info.qn = qn_;
  info.codecid = codecid_;
  info.dash = dash_mode_;
  if (dash_mode_) {
    info.audio_source = audio_source_;
    info.audio_part_path = audio_part_path();
    info.audio_id = audio_id_;
    info.audio_total_bytes = audio_total_;
    info.dash_fps = dash_fps_;
    info.dash_duration_ms = dash_duration_ms_;
    info.duration_ms =
        dash_duration_ms_ > 0 ? dash_duration_ms_ : request_.duration_ms;
    info.playurl.quality = qn_;
    info.playurl.codecid = codecid_;
    info.playurl.format = QStringLiteral("dash");
    info.playurl.timelength_ms = info.duration_ms;
  }
  ready_callback_(info);
  // 首帧回调之后再发档位：DASH 路径直接复用 resolve 阶段已拿到的目录，不再
  // 多打一次 API；durl 回退仍按 T8.9 在首帧后异步补目录。
  if (quality_enabled_.load() && !quality_started_) {
    if (dash_catalog_available_) {
      quality_started_ = true;
      EmitDashQualityCatalog();
    } else {
      StartQualityWorker(generation_);
    }
  }
}

void OnlineSession::Poll() {
  const ScopedSlowLog slow_log("poll");
  if (!active_.load() || closing_.load() || (!job_ && !audio_job_) ||
      !source_) {
    return;
  }
  using lapian::online::DownloadState;
  // T8.17：某一路实际键已完整时不再为它建 DownloadJob；用 kCompleted 代表
  // “该路已完整”，与下载完成的聚合/就绪/落盘判断保持一致。
  const DownloadState video_state =
      job_ ? job_->state() : DownloadState::kCompleted;
  const DownloadState audio_state =
      audio_job_ ? audio_job_->state() : DownloadState::kCompleted;
  const lapian::online::DownloadStats video_stats =
      job_ ? job_->stats() : lapian::online::DownloadStats{};
  const lapian::online::DownloadStats audio_stats =
      audio_job_ ? audio_job_->stats() : lapian::online::DownloadStats{};
  const qint64 video_published = source_->Published();
  const qint64 audio_published =
      audio_source_ ? audio_source_->Published() : 0;
  const qint64 published = video_published + audio_published;
  const qint64 total = source_->Size() + (audio_source_ ? audio_total_ : 0);

  // DASH 双下载聚合：任一失败即失败；两路都完成（等待落盘/已完整）才算完成。
  DownloadState job_state = video_state;
  if (dash_mode_ && audio_source_) {
    const bool video_done = video_state == DownloadState::kCompleted ||
                            video_state == DownloadState::kAwaitingFinalize;
    const bool audio_done = audio_state == DownloadState::kCompleted ||
                            audio_state == DownloadState::kAwaitingFinalize;
    if (video_state == DownloadState::kFailed ||
        audio_state == DownloadState::kFailed) {
      job_state = DownloadState::kFailed;
    } else if (video_state == DownloadState::kCancelled ||
               audio_state == DownloadState::kCancelled) {
      job_state = DownloadState::kCancelled;
    } else if (video_done && audio_done &&
               (video_state == DownloadState::kAwaitingFinalize ||
                audio_state == DownloadState::kAwaitingFinalize)) {
      job_state = DownloadState::kAwaitingFinalize;
    } else if (video_state == DownloadState::kRefreshing ||
               audio_state == DownloadState::kRefreshing) {
      job_state = DownloadState::kRefreshing;
    } else if (video_state == DownloadState::kRetrying ||
               audio_state == DownloadState::kRetrying) {
      job_state = DownloadState::kRetrying;
    } else if (video_state == DownloadState::kDownloading ||
               audio_state == DownloadState::kDownloading) {
      job_state = DownloadState::kDownloading;
    }
  }

  OnlineProgress snapshot;
  snapshot.published = published;
  snapshot.total = total;
  snapshot.bps = video_stats.bps + audio_stats.bps;
  snapshot.job_state = lapian::online::DownloadStateName(job_state);
  snapshot.qn = qn_;
  snapshot.codecid = codecid_;
  snapshot.complete =
      video_published >= total_ &&
      (!audio_source_ || audio_published >= audio_total_);
  snapshot.awaiting_finalize = job_state == DownloadState::kAwaitingFinalize;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_ = snapshot;
  }
  PublishProgress();

  if (job_state == DownloadState::kFailed) {
    QString message;
    if (video_state == DownloadState::kFailed) {
      message = video_stats.error.isEmpty() ? QStringLiteral("下载失败")
                                            : video_stats.error;
    } else {
      message = audio_stats.error.isEmpty()
                    ? QStringLiteral("音频下载失败")
                    : QStringLiteral("音频下载失败：%1").arg(audio_stats.error);
    }
    Fail(message);
    return;
  }
  if (job_state == DownloadState::kCancelled) {
    SetState(OnlineState::kClosed, QStringLiteral("已取消"));
    poll_timer_->stop();
    return;
  }
  if (job_state == DownloadState::kRetrying &&
      state() != OnlineState::kRecovering) {
    SetState(OnlineState::kRecovering, QStringLiteral("网络中断，重试中…"));
  } else if (job_state == DownloadState::kRefreshing &&
             state() != OnlineState::kRecovering) {
    SetState(OnlineState::kRecovering, QStringLiteral("取流过期，正在刷新…"));
  } else if (job_state == DownloadState::kDownloading &&
             state() == OnlineState::kRecovering) {
    SetState(ready_emitted_ ? OnlineState::kReady : OnlineState::kBuffering,
             ready_emitted_ ? QStringLiteral("下载恢复")
                            : QStringLiteral("缓冲中"));
  }

  const bool audio_ready =
      !dash_mode_ || !audio_job_ || audio_published >= audio_ready_threshold_;
  if (!ready_emitted_ && video_published >= ready_threshold_ && audio_ready) {
    EmitReady();
  }

  // T8.13：网络无响应看门狗。放在状态映射之后，避免同一轮被
  // “kDownloading → 恢复 kReady”覆盖；下一轮看到 kFailed 即给出可读错误。
  CheckDownloadStall();

  if (job_state == DownloadState::kAwaitingFinalize) {
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
  // T8.13：首帧后失败不能让已缓存区不可播。下载线程自己已经 source_->Fail()
  // （读已发布区正常，读前沿 EIO）；这里再 Cancel 会把所有读变成 AVERROR_EXIT，
  // 违反 O7-A1/A2“已缓存区仍可播放”。只有还没进入播放（无 reader）时保持旧行为。
  const bool has_readers =
      (source_ && source_->Stats().live_cursors > 0) ||
      (audio_source_ && audio_source_->Stats().live_cursors > 0);
  FailJobsAndSources(ready_emitted_ || has_readers);
}

void OnlineSession::FailJobsAndSources(bool keep_cached_readable) {
  if (job_) {
    job_->Cancel();
  }
  if (audio_job_) {
    audio_job_->Cancel();
  }
  if (keep_cached_readable) {
    // 读层进入“下载不可恢复失败”状态：已发布前缀仍可读，前沿读返回 EIO。
    if (source_ && !source_->IsCancelled()) {
      source_->Fail(AVERROR(EIO));
    }
    if (audio_source_ && !audio_source_->IsCancelled()) {
      audio_source_->Fail(AVERROR(EIO));
    }
    return;
  }
  if (source_) {
    source_->Cancel();
  }
  if (audio_source_) {
    audio_source_->Cancel();
  }
}

void OnlineSession::CheckDownloadStall() {
  if (!job_ && !audio_job_) {
    return;
  }
  const std::int64_t now = NowMs();
  auto stalled = [&](lapian::online::DownloadJob* job) {
    if (job == nullptr) {
      return false;
    }
    if (job->state() != lapian::online::DownloadState::kDownloading) {
      return false;
    }
    const lapian::online::DownloadStats stats = job->stats();
    return stats.last_progress_ms > 0 &&
           now - stats.last_progress_ms >=
               lapian::online::kDownloadStallTimeoutMs;
  };
  const bool video_stalled = stalled(job_.get());
  const bool audio_stalled = stalled(audio_job_.get());
  if (!video_stalled && !audio_stalled) {
    return;
  }
  if (video_stalled) {
    job_->AbortStalledAttempt();
  }
  if (audio_stalled) {
    audio_job_->AbortStalledAttempt();
  }
  SetState(OnlineState::kRecovering, QStringLiteral("网络无响应，重试中…"));
}

void OnlineSession::CancelJobAndSource() {
  if (job_) {
    job_->Cancel();
  }
  if (audio_job_) {
    audio_job_->Cancel();
  }
  if (source_) {
    source_->Cancel();
  }
  if (audio_source_) {
    audio_source_->Cancel();
  }
}

void OnlineSession::Cancel() {
  if (!active_.load()) {
    return;
  }
  closing_.store(true);
  ++generation_;
  if (resolve_cancel_) {
    resolve_cancel_->Cancel();
  }
  if (quality_cancel_) {
    quality_cancel_->Cancel();
  }
  if (poll_timer_) {
    poll_timer_->stop();
  }
  CancelJobAndSource();
  SetState(OnlineState::kClosed, QStringLiteral("已取消"));
}

void OnlineSession::Close() {
  if (!active_.load() && !job_ && !audio_job_ &&
      !resolve_thread_.joinable()) {
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
  if (resolve_cancel_) {
    resolve_cancel_->Cancel();
  }
  JoinResolveWorker();
  resolve_outcome_.reset();
  resolve_cancel_.reset();
  if (quality_cancel_) {
    quality_cancel_->Cancel();
  }
  JoinQualityWorker();
  quality_outcome_.reset();
  quality_cancel_.reset();
  CancelJobAndSource();
  if (job_) {
    job_->Wait(1000);
  }
  if (audio_job_) {
    audio_job_->Wait(1000);
  }
  job_.reset();
  audio_job_.reset();
  transport_.reset();
  source_.reset();
  audio_source_.reset();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = OnlineState::kClosed;
    part_path_.clear();
    audio_part_path_.clear();
  }
  if (state_callback_) {
    state_callback_(OnlineState::kClosed, QStringLiteral("在线会话已关闭"));
  }
}

bool OnlineSession::FinalizeAfterReadersStopped(QString* error) {
  if (error != nullptr) {
    error->clear();
  }
  if (!job_ && !audio_job_) {
    if (error != nullptr) {
      *error = QStringLiteral("没有可完成的下载");
    }
    return false;
  }
  auto finalize_one = [error](lapian::online::DownloadJob* job,
                              const char* label) -> bool {
    if (job == nullptr) {
      return true;
    }
    if (job->state() == lapian::online::DownloadState::kCompleted) {
      return true;
    }
    // 发布 total 与进入 kAwaitingFinalize 之间有一个很短的收尾窗口；关窗/切
    // 文件时 reader 已停，这里给 job 线程最多 500ms 完成收尾，避免偶发
    // “已完整但尚未 awaiting”导致缓存不落盘。
    for (int i = 0;
         i < 50 && job->state() == lapian::online::DownloadState::kDownloading;
         ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (job->state() != lapian::online::DownloadState::kAwaitingFinalize) {
      if (error != nullptr) {
        *error = label == nullptr
                     ? QStringLiteral("下载尚未完整，不能完成缓存")
                     : QStringLiteral("%1尚未完整，不能完成缓存")
                           .arg(QString::fromUtf8(label));
      }
      return false;
    }
    QString finalize_error;
    if (!job->FinalizeCompleted(&finalize_error)) {
      if (error != nullptr) {
        *error = finalize_error;
      }
      return false;
    }
    return true;
  };
  if (!finalize_one(job_.get(), "视频下载")) {
    return false;
  }
  if (audio_job_ && !finalize_one(audio_job_.get(), "音频下载")) {
    return false;
  }
  finalize_pending_ = false;
  return true;
}

}  // namespace lapian::app
