#include "app/OnlineFetchTestRunner.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
}

#include "online/CacheStore.h"
#include "online/DownloadJob.h"
#include "online/DownloadTransport.h"
#include "online/GrowingSource.h"
#include "online/PlayUrl.h"
#include "online/Sessdata.h"
#include "online/WinHttpClient.h"

namespace lapian::app {
namespace {

using lapian::online::CacheKey;
using lapian::online::CacheMetadata;
using lapian::online::CacheStore;
using lapian::online::DownloadJob;
using lapian::online::DownloadPlan;
using lapian::online::DownloadRequest;
using lapian::online::DownloadState;
using lapian::online::DownloadStats;
using lapian::online::DownloadTransportResult;
using lapian::online::DownloadTransportStatus;
using lapian::online::GrowingFormat;
using lapian::online::GrowingSource;
using lapian::online::HttpCancelToken;
using lapian::online::IDownloadSink;
using lapian::online::IDownloadTransport;
using lapian::online::PlayUrlData;

constexpr qint64 kKb = 1024;
constexpr qint64 kFirstFrameBytes = 300 * kKb;  // 307200

int g_checks = 0;
int g_failures = 0;

void Check(const QString& name, bool ok, const QString& detail = QString()) {
  ++g_checks;
  if (ok) {
    std::printf("[ OK ] T8.5 %s\n", name.toUtf8().constData());
  } else {
    ++g_failures;
    std::printf("[FAIL] T8.5 %s%s%s\n", name.toUtf8().constData(),
                detail.isEmpty() ? "" : "：", detail.toUtf8().constData());
  }
  std::fflush(stdout);
}

void PrintKV(const char* key, const QString& value) {
  std::printf("%s=%s\n", key, value.toUtf8().constData());
}

QString Num(qint64 value) { return QString::number(value); }
QString NumDouble(double value) { return QString::number(value, 'f', 3); }

QString HashFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QString();
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file)) {
    return QString();
  }
  return QString::fromLatin1(hash.result().toHex());
}

QString HashBytes(const QByteArray& bytes) {
  return QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool ProbeMp4(const QString& path, qint64* duration_ms, int* width, int* height,
              QString* codec, QString* error) {
  AVFormatContext* context = nullptr;
  const QByteArray utf8_path = path.toUtf8();
  int ret = avformat_open_input(&context, utf8_path.constData(), nullptr, nullptr);
  if (ret < 0) {
    if (error != nullptr) {
      *error = QStringLiteral("open=%1").arg(lapian::online::AvErrorText(ret));
    }
    return false;
  }
  ret = avformat_find_stream_info(context, nullptr);
  if (ret < 0) {
    if (error != nullptr) {
      *error = QStringLiteral("stream_info=%1")
                   .arg(lapian::online::AvErrorText(ret));
    }
    avformat_close_input(&context);
    return false;
  }
  const int video =
      av_find_best_stream(context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (video < 0) {
    if (error != nullptr) {
      *error = QStringLiteral("no-video-stream");
    }
    avformat_close_input(&context);
    return false;
  }
  if (duration_ms != nullptr) {
    *duration_ms = context->duration > 0
                       ? context->duration / (AV_TIME_BASE / 1000)
                       : 0;
  }
  if (width != nullptr) {
    *width = context->streams[video]->codecpar->width;
  }
  if (height != nullptr) {
    *height = context->streams[video]->codecpar->height;
  }
  if (codec != nullptr) {
    const AVCodecDescriptor* descriptor =
        avcodec_descriptor_get(context->streams[video]->codecpar->codec_id);
    *codec = descriptor == nullptr ? QString()
                                   : QString::fromUtf8(descriptor->name);
  }
  avformat_close_input(&context);
  return true;
}

struct FirstFrameResult {
  bool ok = false;
  QString error;
  double open_ms = 0.0;
  double first_frame_ms = 0.0;
  qint64 consumed_bytes = 0;
  int width = 0;
  int height = 0;
  double fps = 0.0;
};

FirstFrameResult DecodeFirstFrame(const std::shared_ptr<GrowingSource>& source,
                                  const QString& part_path) {
  FirstFrameResult result;
  QElapsedTimer timer;
  timer.start();
  GrowingFormat format;
  if (!format.Open(source, part_path, &result.error)) {
    return result;
  }
  result.open_ms = timer.elapsed();
  AVFormatContext* context = format.context();
  const int video =
      av_find_best_stream(context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (video < 0) {
    result.error = QStringLiteral("没有视频流");
    return result;
  }
  AVStream* stream = context->streams[video];
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
    last_error = av_read_frame(context, packet);
    if (last_error < 0) {
      break;
    }
    if (packet->stream_index == video) {
      if (avcodec_send_packet(decoder, packet) >= 0) {
        while (avcodec_receive_frame(decoder, frame) == 0) {
          result.width = frame->width;
          result.height = frame->height;
          result.fps = av_q2d(stream->avg_frame_rate);
          got = true;
          av_frame_unref(frame);
          break;
        }
      }
    }
    av_packet_unref(packet);
  }
  result.ok = got;
  result.first_frame_ms = timer.elapsed();
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

// ---------------------------------------------------------------- fake transport

struct FakeBehavior {
  int http_status = 0;
  bool ignore_range = false;
  bool block = false;
  int chunk_delay_ms = 0;
  qint64 chunk_bytes = 128 * 1024;
};

class FakeTransport final : public IDownloadTransport {
 public:
  QByteArray data;
  QHash<QString, FakeBehavior> behaviors;

  DownloadTransportResult GetRange(const DownloadRequest& request,
                                   IDownloadSink* sink,
                                   const HttpCancelToken& cancel) override {
    DownloadTransportResult result;
    {
      QMutexLocker lock(&mutex_);
      requests_.append(request);
    }
    const FakeBehavior behavior = behaviors.value(request.url);
    if (behavior.http_status != 0) {
      result.status = DownloadTransportStatus::kHttpStatus;
      result.http_status = behavior.http_status;
      result.error = QStringLiteral("HTTP %1").arg(behavior.http_status);
      return result;
    }
    if (request.offset > data.size()) {
      result.status = DownloadTransportStatus::kHttpStatus;
      result.http_status = 416;
      result.error = QStringLiteral("HTTP 416");
      return result;
    }
    if (behavior.ignore_range && request.offset > 0) {
      result.status = DownloadTransportStatus::kRangeIgnored;
      result.range_ignored = true;
      result.http_status = 200;
      result.error = QStringLiteral("range ignored");
      return result;
    }
    if (behavior.block) {
      while (!cancel.IsCancelled()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      result.status = DownloadTransportStatus::kCancelled;
      result.cancelled = true;
      result.error = QStringLiteral("cancelled");
      return result;
    }
    result.http_status = request.offset > 0 ? 206 : 200;
    if (result.http_status == 206) {
      result.content_range_start = request.offset;
      result.content_range_end = data.size() - 1;
      result.content_range_total = data.size();
    } else {
      result.content_length = data.size();
    }
    qint64 offset = request.offset;
    QElapsedTimer timer;
    timer.start();
    while (offset < data.size()) {
      if (cancel.IsCancelled()) {
        result.status = DownloadTransportStatus::kCancelled;
        result.cancelled = true;
        result.bytes_written = offset - request.offset;
        result.error = QStringLiteral("cancelled");
        return result;
      }
      const qint64 chunk =
          std::min<qint64>(behavior.chunk_bytes, data.size() - offset);
      QString sink_error;
      if (sink == nullptr ||
          !sink->Write(reinterpret_cast<const std::uint8_t*>(
                           data.constData() + offset),
                       static_cast<std::size_t>(chunk), &sink_error)) {
        result.status = DownloadTransportStatus::kSinkError;
        result.bytes_written = offset - request.offset;
        result.error = sink_error;
        return result;
      }
      offset += chunk;
      if (behavior.chunk_delay_ms > 0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(behavior.chunk_delay_ms));
      }
    }
    result.bytes_written = offset - request.offset;
    result.first_byte_ms = 0;
    result.elapsed_ms = timer.elapsed();
    result.status = DownloadTransportStatus::kOk;
    result.ok = true;
    return result;
  }

  QVector<DownloadRequest> requests() const {
    QMutexLocker lock(&mutex_);
    return requests_;
  }

 private:
  mutable QMutex mutex_;
  QVector<DownloadRequest> requests_;
};

// ---------------------------------------------------------------- common helpers

QByteArray LoadSample(const QString& explicit_path) {
  QStringList candidates;
  if (!explicit_path.isEmpty()) {
    candidates.append(explicit_path);
  }
  candidates.append(
      QDir::current().filePath(QStringLiteral("build/test-media/faststart-720p.mp4")));
  candidates.append(
      QDir::current().filePath(QStringLiteral("build/test-media/faststart-60s.mp4")));
  for (const QString& path : candidates) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
      return file.readAll();
    }
  }
  return {};
}

CacheKey MakeKey(const QString& bvid, qint64 cid, int qn, int codecid) {
  CacheKey key;
  key.bvid = bvid;
  key.cid = cid;
  key.qn = qn;
  key.codecid = codecid;
  return key;
}

DownloadPlan MakePlan(const QStringList& urls, qint64 total, int qn,
                      int codecid, qint64 duration_ms) {
  DownloadPlan plan;
  plan.urls = urls;
  plan.total_bytes = total;
  plan.qn = qn;
  plan.codecid = codecid;
  plan.format = QStringLiteral("mp4720");
  plan.metadata.duration_ms = duration_ms;
  return plan;
}

void SeedPart(CacheStore* cache, const CacheKey& key, const QByteArray& data,
              qint64 bytes) {
  QString part_path;
  qint64 existing = 0;
  QString error;
  if (!cache->Reserve(key, data.size(), &error) ||
      !cache->OpenPart(key, &part_path, &existing, &error)) {
    Check(QStringLiteral("seed part"), false, error);
    return;
  }
  QFile file(part_path);
  if (!file.open(QIODevice::WriteOnly)) {
    Check(QStringLiteral("seed part open"), false, part_path);
    return;
  }
  file.write(data.constData(), bytes);
  file.close();
}

// ---------------------------------------------------------------- offline scenarios

bool RunOfflineScenario(const QString& name, const QByteArray& sample,
                        bool seed_prefix, bool ignore_range, bool backup_403,
                        bool refresh, bool block_cancel, bool first_frame,
                        const QString& root_path) {
  const int failures_before = g_failures;
  CacheStore cache(QDir(root_path).filePath(name));
  FakeTransport transport;
  transport.data = sample;
  const QString main_url = QStringLiteral("https://cdn1.example/v.mp4");
  const QString backup_url = QStringLiteral("https://cdn2.example/v.mp4");
  QStringList urls = {main_url};
  if (backup_403) {
    urls = {main_url, backup_url};
    FakeBehavior forbidden;
    forbidden.http_status = 403;
    transport.behaviors.insert(main_url, forbidden);
  }
  if (refresh) {
    urls = {main_url, backup_url, QStringLiteral("https://cdn3.example/v.mp4")};
    for (const QString& url : urls) {
      FakeBehavior forbidden;
      forbidden.http_status = 403;
      transport.behaviors.insert(url, forbidden);
    }
    transport.behaviors.insert(QStringLiteral("https://fresh.example/v.mp4"),
                               FakeBehavior());
  }
  if (ignore_range) {
    FakeBehavior behavior;
    behavior.ignore_range = true;
    transport.behaviors.insert(main_url, behavior);
  }
  if (block_cancel) {
    FakeBehavior behavior;
    behavior.block = true;
    transport.behaviors.insert(main_url, behavior);
  }
  if (first_frame) {
    FakeBehavior behavior;
    behavior.chunk_delay_ms = 20;
    behavior.chunk_bytes = 64 * 1024;
    transport.behaviors.insert(main_url, behavior);
  }

  const CacheKey key =
      MakeKey(QStringLiteral("BV1BMnv6jE5S"), 42915922022LL, 64, 7);
  const qint64 total = sample.size();
  if (seed_prefix) {
    SeedPart(&cache, key, sample, total / 3);
  }
  auto source = std::make_shared<GrowingSource>(total);
  int refresh_calls = 0;
  DownloadJob job(
      &cache, key, source, &transport,
      MakePlan(urls, total, 64, 7, 1500),
      [&](DownloadPlan* fresh, const HttpCancelToken&, QString*) {
        ++refresh_calls;
        *fresh = MakePlan({QStringLiteral("https://fresh.example/v.mp4")}, total,
                          64, 7, 1500);
        return refresh;
      });
  QString error;
  if (!job.Start(&error)) {
    Check(name + QStringLiteral(": start"), false, error);
    return false;
  }

  if (block_cancel) {
    for (int i = 0; i < 200 && transport.requests().isEmpty(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    QElapsedTimer timer;
    timer.start();
    job.Cancel();
    const bool finished = job.Wait(2000);
    const qint64 cancel_ms = timer.elapsed();
    Check(name + QStringLiteral(": cancel finished"), finished);
    Check(name + QStringLiteral(": cancel <=100ms"), cancel_ms <= 100,
          Num(cancel_ms));
    Check(name + QStringLiteral(": state cancelled"),
          job.state() == DownloadState::kCancelled);
    Check(name + QStringLiteral(": .part retained"),
          QFileInfo::exists(cache.PartPath(key)));
    Check(name + QStringLiteral(": no .mp4"),
          !QFileInfo::exists(cache.Mp4Path(key)));
    return g_failures == failures_before;
  }

  if (first_frame) {
    qint64 published = 0;
    for (int i = 0; i < 5000; ++i) {
      published = source->Published();
      if (published >= kFirstFrameBytes) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(name + QStringLiteral(": published >= 0.3MB"),
          published >= kFirstFrameBytes, Num(published));
    job.Pause();
    const FirstFrameResult first =
        DecodeFirstFrame(source, cache.PartPath(key));
    Check(name + QStringLiteral(": first frame"), first.ok, first.error);
    Check(name + QStringLiteral(": first frame 1920x1080"),
          first.width == 1920 && first.height == 1080,
          QStringLiteral("%1x%2").arg(first.width).arg(first.height));
    Check(name + QStringLiteral(": first frame consumed"), first.consumed_bytes > 0,
          Num(first.consumed_bytes));
    PrintKV("offline_first_frame_open_ms", NumDouble(first.open_ms));
    PrintKV("offline_first_frame_ms", NumDouble(first.first_frame_ms));
    PrintKV("offline_first_frame_consumed", Num(first.consumed_bytes));
    PrintKV("offline_first_frame_published", Num(published));
    job.Resume();
  }

  const bool finished = job.Wait(120000);
  Check(name + QStringLiteral(": finished"), finished,
        QString::number(static_cast<int>(job.state())));
  if (!finished || job.state() != DownloadState::kCompleted) {
    Check(name + QStringLiteral(": completed"),
          job.state() == DownloadState::kCompleted, job.stats().error);
    return false;
  }
  const DownloadStats stats = job.stats();
  Check(name + QStringLiteral(": written == total"),
        stats.written_bytes == total, Num(stats.written_bytes));
  Check(name + QStringLiteral(": sha256"),
        HashFile(cache.Mp4Path(key)) == HashBytes(sample), QString());
  Check(name + QStringLiteral(": .part renamed"),
        !QFileInfo::exists(cache.PartPath(key)));
  QString probe_error;
  Check(name + QStringLiteral(": ffprobe"),
        ProbeMp4(cache.Mp4Path(key), nullptr, nullptr, nullptr, nullptr,
                 &probe_error),
        probe_error);
  if (seed_prefix && !ignore_range) {
    const QVector<DownloadRequest> requests = transport.requests();
    Check(name + QStringLiteral(": first offset == existing"),
          !requests.isEmpty() && requests.first().offset == total / 3,
          requests.isEmpty() ? QStringLiteral("no request")
                             : Num(requests.first().offset));
    Check(name + QStringLiteral(": 206 resume"), stats.http_206 >= 1,
          Num(stats.http_206));
  }
  if (ignore_range) {
    Check(name + QStringLiteral(": range_ignored"), stats.range_ignored);
    Check(name + QStringLiteral(": truncated"), stats.range_ignored_truncated);
  }
  if (backup_403) {
    Check(name + QStringLiteral(": backup switch"),
          stats.backup_switches >= 1, Num(stats.backup_switches));
  }
  if (refresh) {
    Check(name + QStringLiteral(": refresh once"), refresh_calls == 1,
          Num(refresh_calls));
    Check(name + QStringLiteral(": stats refreshes"), stats.refreshes == 1,
          Num(stats.refreshes));
  }
  return g_failures == failures_before;
}

// 测试用故障注入：把 CDN 签名 URL 的 deadline 改成过期值（不打印 URL）。
QString BreakSignedUrl(const QString& url) {
  QUrl parsed(url);
  QUrlQuery query(parsed);
  query.removeAllQueryItems(QStringLiteral("deadline"));
  query.addQueryItem(QStringLiteral("deadline"), QStringLiteral("1"));
  parsed.setQuery(query);
  return parsed.toString(QUrl::FullyEncoded);
}

// ---------------------------------------------------------------- credentials

struct SecretFile {
  bool ok = false;
  QString path;
  QString source;
  QString sessdata;
  QString error;
};

QString ResolveSecretPath(const QString& explicit_path, QString* source) {
  if (!explicit_path.isEmpty()) {
    *source = QStringLiteral("arg");
    return explicit_path;
  }
  const QString env = qEnvironmentVariable("LAPIAN_SESSDATA_FILE");
  if (!env.isEmpty()) {
    *source = QStringLiteral("env");
    return env;
  }
  *source = QStringLiteral("default");
  const QStringList candidates = {
      QDir::current().filePath(QStringLiteral(".secrets/bilibili_sessdata.txt")),
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral(".secrets/bilibili_sessdata.txt")),
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("../.secrets/bilibili_sessdata.txt")),
  };
  for (const QString& candidate : candidates) {
    if (QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return candidates.first();
}

SecretFile ReadSecret(const QString& explicit_path) {
  SecretFile secret;
  secret.path = ResolveSecretPath(explicit_path, &secret.source);
  const QFileInfo info(secret.path);
  if (!info.exists() || !info.isFile()) {
    secret.error = QStringLiteral("missing_sessdata_file");
    return secret;
  }
  if (info.size() <= 0 || info.size() > 8 * 1024) {
    secret.error = QStringLiteral("invalid_sessdata_file_size");
    return secret;
  }
  QFile file(secret.path);
  if (!file.open(QIODevice::ReadOnly)) {
    secret.error = QStringLiteral("unreadable_sessdata_file");
    return secret;
  }
  const QByteArray raw = file.readAll();
  file.close();
  const lapian::online::SessdataNormalization normalized =
      lapian::online::NormalizeSessdata(QString::fromUtf8(raw));
  if (!normalized.ok) {
    secret.error = QStringLiteral("invalid_sessdata");
    return secret;
  }
  secret.ok = true;
  secret.sessdata = normalized.value;
  return secret;
}

// ---------------------------------------------------------------- real run

int RunReal(const OnlineFetchTestOptions& options) {
  SecretFile secret = ReadSecret(options.sessdata_file);
  if (!secret.ok) {
    PrintKV("error", secret.error);
    PrintKV("verdict", QStringLiteral("blocked"));
    return 2;
  }
  const QString tag = options.tag.isEmpty() ? options.bvid : options.tag;
  std::printf("T8.5 online_fetch real tag=%s bvid=%s cid=%lld secret_source=%s\n",
              tag.toUtf8().constData(), options.bvid.toUtf8().constData(),
              static_cast<long long>(options.cid), secret.source.toUtf8().constData());
  std::fflush(stdout);

  lapian::online::WinHttpClient http;
  HttpCancelToken cancel;
  lapian::online::PlayUrlFetchOptions fetch_options;
  fetch_options.timeout_ms = options.timeout_ms;
  fetch_options.sessdata = secret.sessdata;
  const int request_qn = options.qn_1080 ? 80 : 64;
  const lapian::online::PlayUrlFetchResult fetched =
      lapian::online::FetchPlayUrl(&http, options.bvid, options.cid, request_qn,
                                   fetch_options, cancel);
  lapian::online::ClearSecret(&fetch_options.sessdata);
  PrintKV("tag", tag);
  PrintKV("request_qn", Num(request_qn));
  PrintKV("playurl_requests", Num(fetched.http_requests));
  PrintKV("playurl_http_status", Num(fetched.http_status));
  PrintKV("playurl_elapsed_ms", Num(fetched.elapsed_ms));
  if (!fetched.error.ok()) {
    PrintKV("playurl_error_kind",
            lapian::online::ErrorKindName(fetched.error.kind));
    PrintKV("playurl_error", fetched.error.message);
    PrintKV("quality", Num(fetched.data.quality));
    PrintKV("verdict", QStringLiteral("fail"));
    return 1;
  }
  const PlayUrlData& data = fetched.data;
  const qint64 total = data.TotalBytes();
  const int codecid = data.codecid > 0 ? data.codecid : 7;
  const int actual_qn = data.quality;
  PrintKV("quality", Num(actual_qn));
  PrintKV("quality_name", lapian::online::QualityName(actual_qn));
  PrintKV("format", data.format);
  PrintKV("codecid", Num(codecid));
  PrintKV("size", Num(total));
  PrintKV("length_ms", Num(data.timelength_ms));
  PrintKV("durl_backups", Num(data.durls.first().backup_urls.size()));
  PrintKV("m1_720p", actual_qn >= 64 ? QStringLiteral("1") : QStringLiteral("0"));

  const CacheKey key = MakeKey(options.bvid, options.cid, actual_qn, codecid);
  if (!lapian::online::IsValidCacheKey(key) || total <= 0) {
    PrintKV("error", QStringLiteral("invalid_cache_key_or_size"));
    PrintKV("verdict", QStringLiteral("fail"));
    return 1;
  }
  QString root = options.root;
  if (root.isEmpty()) {
    root = QDir(QDir::tempPath())
               .filePath(QStringLiteral("lapian-fetch-%1").arg(tag));
  }
  CacheStore cache(root);
  QString cache_error;
  if (!cache.SetRoot(root, &cache_error)) {
    PrintKV("error", cache_error);
    PrintKV("verdict", QStringLiteral("fail"));
    return 1;
  }
  PrintKV("cache_root", root);

  auto source = std::make_shared<GrowingSource>(total);
  DownloadPlan plan;
  plan.urls = data.durls.first().AllUrls();
  plan.total_bytes = total;
  plan.length_ms = data.timelength_ms;
  plan.qn = actual_qn;
  plan.codecid = codecid;
  plan.format = data.format;
  plan.metadata.duration_ms = data.timelength_ms;
  if (!options.tag.isEmpty()) {
    plan.metadata.source_fps_hint = options.tag;
  }
  const QString break_mode = options.break_urls.trimmed().toLower();
  if (break_mode == QStringLiteral("primary") && !plan.urls.isEmpty()) {
    plan.urls[0] = BreakSignedUrl(plan.urls[0]);
    PrintKV("broken_urls", QStringLiteral("1"));
  } else if (break_mode == QStringLiteral("all")) {
    for (QString& url : plan.urls) {
      url = BreakSignedUrl(url);
    }
    PrintKV("broken_urls", Num(plan.urls.size()));
  }

  lapian::online::WinHttpDownloadTransport transport;
  DownloadJob job(
      &cache, key, source, &transport, plan,
      [&](DownloadPlan* fresh, const HttpCancelToken& refresh_cancel,
          QString* refresh_error) {
        lapian::online::PlayUrlFetchOptions refresh_options;
        refresh_options.timeout_ms = options.timeout_ms;
        refresh_options.sessdata = secret.sessdata;
        const lapian::online::PlayUrlFetchResult again =
            lapian::online::FetchPlayUrl(&http, options.bvid, options.cid,
                                         request_qn, refresh_options,
                                         refresh_cancel);
        lapian::online::ClearSecret(&refresh_options.sessdata);
        if (!again.error.ok()) {
          *refresh_error = again.error.message;
          return false;
        }
        fresh->urls = again.data.durls.first().AllUrls();
        fresh->total_bytes = again.data.TotalBytes();
        fresh->length_ms = again.data.timelength_ms;
        fresh->qn = again.data.quality;
        fresh->codecid = again.data.codecid > 0 ? again.data.codecid : 7;
        fresh->format = again.data.format;
        return true;
      },
      lapian::online::DownloadOptions{options.timeout_ms});
  QString error;
  if (!job.Start(&error)) {
    PrintKV("error", error);
    PrintKV("verdict", QStringLiteral("fail"));
    return 1;
  }
  if (options.exit_after_bytes > 0) {
    for (int i = 0; i < 30000; ++i) {
      if (source->Published() >= options.exit_after_bytes ||
          job.state() == DownloadState::kCompleted ||
          job.state() == DownloadState::kFailed) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const DownloadStats stats = job.stats();
    PrintKV("exit_after_bytes", Num(options.exit_after_bytes));
    PrintKV("published_at_exit", Num(source->Published()));
    PrintKV("existing_at_exit", Num(stats.existing_bytes));
    std::fflush(stdout);
    std::_Exit(0);
  }

  qint64 published = 0;
  for (int i = 0; i < 3000; ++i) {
    published = source->Published();
    if (published >= kFirstFrameBytes ||
        job.state() == DownloadState::kCompleted) {
      break;
    }
    if (job.state() == DownloadState::kFailed) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  bool first_ok = false;
  FirstFrameResult first;
  if (published >= kFirstFrameBytes || job.state() == DownloadState::kCompleted) {
    job.Pause();
    first = DecodeFirstFrame(source, job.part_path());
    first_ok = first.ok;
    job.Resume();
  }
  PrintKV("first_frame_published", Num(published));
  PrintKV("first_frame_ok", first_ok ? QStringLiteral("1") : QStringLiteral("0"));
  PrintKV("first_frame_open_ms", NumDouble(first.open_ms));
  PrintKV("first_frame_ms", NumDouble(first.first_frame_ms));
  PrintKV("first_frame_consumed", Num(first.consumed_bytes));
  PrintKV("first_frame_size",
          QStringLiteral("%1x%2").arg(first.width).arg(first.height));
  PrintKV("first_frame_fps", NumDouble(first.fps));
  if (!first_ok) {
    PrintKV("first_frame_error", first.error);
  }
  if (options.cancel_after_first_frame) {
    QElapsedTimer timer;
    timer.start();
    job.Cancel();
    job.Wait(5000);
    PrintKV("cancelled", QStringLiteral("1"));
    PrintKV("cancel_ms", Num(timer.elapsed()));
    PrintKV("part_bytes", Num(QFileInfo(cache.PartPath(key)).size()));
    PrintKV("verdict", first_ok ? QStringLiteral("cancelled-after-first-frame")
                                : QStringLiteral("fail"));
    return first_ok ? 0 : 1;
  }

  const bool finished = job.Wait(300000);
  const DownloadStats stats = job.stats();
  PrintKV("state", lapian::online::DownloadStateName(job.state()));
  PrintKV("finished", finished ? QStringLiteral("1") : QStringLiteral("0"));
  PrintKV("downloaded_bytes", Num(stats.downloaded_bytes));
  PrintKV("existing_bytes", Num(stats.existing_bytes));
  PrintKV("written_bytes", Num(stats.written_bytes));
  PrintKV("elapsed_ms", Num(stats.elapsed_ms));
  PrintKV("bps", NumDouble(stats.bps));
  PrintKV("first_byte_ms", Num(stats.first_byte_ms));
  PrintKV("range_requests", Num(stats.range_requests));
  PrintKV("http_206", Num(stats.http_206));
  PrintKV("http_200", Num(stats.http_200));
  PrintKV("backup_switches", Num(stats.backup_switches));
  PrintKV("retries", Num(stats.retries));
  PrintKV("refreshes", Num(stats.refreshes));
  PrintKV("range_ignored",
          stats.range_ignored ? QStringLiteral("1") : QStringLiteral("0"));

  bool ffprobe_ok = false;
  if (job.state() == DownloadState::kCompleted) {
    QString probe_error;
    qint64 duration_ms = 0;
    int width = 0;
    int height = 0;
    QString codec;
    ffprobe_ok = ProbeMp4(cache.Mp4Path(key), &duration_ms, &width, &height,
                          &codec, &probe_error);
    PrintKV("ffprobe", ffprobe_ok ? QStringLiteral("pass")
                                  : QStringLiteral("fail"));
    if (ffprobe_ok) {
      PrintKV("mp4_size", Num(QFileInfo(cache.Mp4Path(key)).size()));
      PrintKV("mp4_duration_ms", Num(duration_ms));
      PrintKV("mp4_video",
              QStringLiteral("%1x%2/%3").arg(width).arg(height).arg(codec));
      PrintKV("mp4_sha256", HashFile(cache.Mp4Path(key)));
    } else {
      PrintKV("ffprobe_error", probe_error);
    }
  } else {
    PrintKV("ffprobe", QStringLiteral("not-run"));
    PrintKV("download_error", stats.error);
    PrintKV("av_error", Num(stats.av_error));
  }
  const bool sample_pass = job.state() == DownloadState::kCompleted &&
                           ffprobe_ok && actual_qn >= 64 && first_ok;
  const bool sample_blocked = job.state() == DownloadState::kCompleted &&
                              ffprobe_ok && actual_qn < 64;
  PrintKV("sample_verdict", sample_pass   ? QStringLiteral("pass")
                            : sample_blocked ? QStringLiteral("blocked-quality")
                                             : QStringLiteral("fail"));
  PrintKV("verdict", sample_pass   ? QStringLiteral("pass")
                     : sample_blocked ? QStringLiteral("blocked")
                                      : QStringLiteral("fail"));
  lapian::online::ClearSecret(&secret.sessdata);
  return sample_pass ? 0 : (sample_blocked ? 2 : 1);
}

}  // namespace

int RunOnlineFetchTest(const OnlineFetchTestOptions& options) {
  if (options.real) {
    return RunReal(options);
  }

  std::printf("T8.5 online_fetch offline fake-transport mode\n");
  std::fflush(stdout);
  const QByteArray sample = LoadSample(options.media);
  if (sample.isEmpty()) {
    PrintKV("error", QStringLiteral("missing_local_sample"));
    PrintKV("verdict", QStringLiteral("blocked"));
    return 2;
  }
  PrintKV("sample_bytes", Num(sample.size()));
  PrintKV("sample_sha256", HashBytes(sample));

  QTemporaryDir temp;
  if (!temp.isValid()) {
    PrintKV("error", QStringLiteral("temp_dir_failed"));
    PrintKV("verdict", QStringLiteral("fail"));
    return 1;
  }
  const int failures_before = g_failures;
  RunOfflineScenario(QStringLiteral("normal"), sample, false, false, false,
                     false, false, false, temp.path());
  RunOfflineScenario(QStringLiteral("resume"), sample, true, false, false, false,
                     false, false, temp.path());
  RunOfflineScenario(QStringLiteral("range200"), sample, true, true, false,
                     false, false, false, temp.path());
  RunOfflineScenario(QStringLiteral("backup"), sample, false, false, true, false,
                     false, false, temp.path());
  RunOfflineScenario(QStringLiteral("refresh"), sample, false, false, false,
                     true, false, false, temp.path());
  RunOfflineScenario(QStringLiteral("cancel"), sample, false, false, false, false,
                     true, false, temp.path());
  RunOfflineScenario(QStringLiteral("firstframe"), sample, false, false, false,
                     false, false, true, temp.path());

  PrintKV("offline_checks", Num(g_checks));
  PrintKV("offline_failures", Num(g_failures));
  const bool pass = g_failures == failures_before;
  PrintKV("verdict", pass ? QStringLiteral("pass") : QStringLiteral("fail"));
  std::fflush(stdout);
  return pass ? 0 : 1;
}

}  // namespace lapian::app
