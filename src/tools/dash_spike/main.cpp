// T8.10 spike（开发工具，不入分发）：验证 DASH 双输入的五个硬问题。
//
//   lapian_dash_spike --bvid BV... --cid 123 [--sessdata-file .secrets/...]
//                     [--qn 80] [--prefetch-mb 3] [--seek-seconds 20]
//
// 只读凭据文件，不打印 SESSDATA/签名 URL。下载写系统临时目录，退出前删除。

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "audio/AudioDecoder.h"
#include "audio/AudioFormat.h"
#include "media/VideoDecoder.h"
#include "online/BiliAuth.h"
#include "online/DownloadTransport.h"
#include "online/GrowingSource.h"
#include "online/PlayUrl.h"
#include "online/Sessdata.h"
#include "online/WinHttpClient.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/intreadwrite.h>
}

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(const char* name, bool ok, const std::string& detail = {}) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
  }
  std::printf("spike_check %s ok=%d%s%s\n", name, ok ? 1 : 0,
              detail.empty() ? "" : " detail=", detail.c_str());
  std::fflush(stdout);
}

QString ReadSessdata(const QString& explicit_path) {
  QString path = explicit_path;
  if (path.isEmpty()) {
    const QStringList candidates = {
        QDir::current().filePath(QStringLiteral(".secrets/bilibili_sessdata.txt")),
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral(".secrets/bilibili_sessdata.txt")),
    };
    for (const QString& candidate : candidates) {
      if (QFileInfo::exists(candidate)) {
        path = candidate;
        break;
      }
    }
  }
  if (path.isEmpty() || !QFileInfo::exists(path)) {
    return {};
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const auto normalized =
      lapian::online::NormalizeSessdata(QString::fromUtf8(file.readAll()));
  return normalized.ok ? normalized.value : QString();
}

// 接受首个数据块后主动失败：用于“只要响应头”的探测，避免把整文件读下来。
class FirstChunkSink final : public lapian::online::IDownloadSink {
 public:
  bool Write(const std::uint8_t*, std::size_t size, QString*) override {
    bytes += static_cast<qint64>(size);
    return false;
  }
  qint64 bytes = 0;
};

struct ProbeResult {
  bool ok = false;
  qint64 total = -1;
  int http_status = 0;
  bool range_supported = false;
  qint64 elapsed_ms = 0;
  QString error;
  QString url_kind;  // main / backup1 / backup2
};

ProbeResult ProbeTotal(lapian::online::IDownloadTransport* transport,
                       const QStringList& urls,
                       const lapian::online::HttpCancelToken& cancel) {
  ProbeResult best;
  for (int i = 0; i < urls.size(); ++i) {
    for (int attempt = 0; attempt < 2; ++attempt) {
      lapian::online::DownloadRequest request;
      request.url = urls[i];
      request.offset = 0;
      request.range_end = 0;
      request.timeout_ms = 8000;
      FirstChunkSink sink;
      const auto result = transport->GetRange(request, &sink, cancel);
      ProbeResult probe;
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
      } else if (result.status == lapian::online::DownloadTransportStatus::kSinkError &&
                 result.content_range_total > 0) {
        probe.ok = true;
        probe.total = result.content_range_total;
        probe.range_supported = true;
      } else if (result.status == lapian::online::DownloadTransportStatus::kSinkError &&
                 result.content_length > 0) {
        probe.ok = true;
        probe.total = result.content_length;
        probe.range_supported = false;
      } else {
        probe.error = result.error;
      }
      std::printf("spike_probe url=%s attempt=%d status=%d http=%d total=%lld "
                  "range=%d elapsed_ms=%lld error=%s\n",
                  i == 0 ? "main" : "backup", attempt, static_cast<int>(result.status),
                  result.http_status, static_cast<long long>(probe.total),
                  probe.range_supported ? 1 : 0,
                  static_cast<long long>(result.elapsed_ms),
                  probe.error.toUtf8().constData());
      std::fflush(stdout);
      if (probe.ok) {
        return probe;
      }
      best = probe;
    }
  }
  return best;
}

class FileSink final : public lapian::online::IDownloadSink {
 public:
  FileSink(QFile* file, qint64 abort_after) : file_(file), abort_after_(abort_after) {}
  bool Write(const std::uint8_t* data, std::size_t size, QString* error) override {
    if (file_->write(reinterpret_cast<const char*>(data),
                     static_cast<qint64>(size)) != static_cast<qint64>(size)) {
      *error = QStringLiteral("写临时文件失败");
      return false;
    }
    written += static_cast<qint64>(size);
    if (abort_after_ > 0 && written >= abort_after_) {
      // 主动停止：spike 只预取前 N MB。
      return false;
    }
    return true;
  }
  qint64 written = 0;

 private:
  QFile* file_ = nullptr;
  qint64 abort_after_ = -1;
};

qint64 DownloadPrefix(lapian::online::IDownloadTransport* transport,
                      const QString& url, qint64 total, qint64 want,
                      QFile* file, const lapian::online::HttpCancelToken& cancel,
                      QString* error) {
  lapian::online::DownloadRequest request;
  request.url = url;
  request.offset = 0;
  request.range_end = -1;
  request.expected_total = total;
  request.timeout_ms = 20000;
  FileSink sink(file, want);
  const auto result = transport->GetRange(request, &sink, cancel);
  if (result.status != lapian::online::DownloadTransportStatus::kOk &&
      result.status != lapian::online::DownloadTransportStatus::kSinkError) {
    *error = result.error;
    return -1;
  }
  (void)file->flush();
  return sink.written;
}

std::string AvError(int error) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(error, buffer, sizeof(buffer));
  return buffer;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("拉片软件"));
  QCoreApplication::setApplicationName(QStringLiteral("拉片软件"));

  QString bvid;
  qint64 cid = 0;
  QString sessdata_file;
  int qn = 0;
  int prefetch_mb = 3;
  double seek_seconds = 20.0;
  const QStringList args = QCoreApplication::arguments();
  for (int i = 1; i < args.size(); ++i) {
    const QString& arg = args[i];
    if (arg == QStringLiteral("--bvid") && i + 1 < args.size()) {
      bvid = args[++i];
    } else if (arg == QStringLiteral("--cid") && i + 1 < args.size()) {
      cid = args[++i].toLongLong();
    } else if (arg == QStringLiteral("--sessdata-file") && i + 1 < args.size()) {
      sessdata_file = args[++i];
    } else if (arg == QStringLiteral("--qn") && i + 1 < args.size()) {
      qn = args[++i].toInt();
    } else if (arg == QStringLiteral("--prefetch-mb") && i + 1 < args.size()) {
      prefetch_mb = std::max(1, args[++i].toInt());
    } else if (arg == QStringLiteral("--seek-seconds") && i + 1 < args.size()) {
      seek_seconds = args[++i].toDouble();
    }
  }
  if (bvid.isEmpty() || cid <= 0) {
    std::printf("usage: lapian_dash_spike --bvid BV... --cid 123\n");
    return 2;
  }
  QString sessdata = ReadSessdata(sessdata_file);
  if (sessdata.isEmpty()) {
    std::printf("spike_blocked missing_sessdata\n");
    return 2;
  }

  lapian::online::WinHttpClient http;
  lapian::online::HttpCancelToken cancel;
  const auto auth = lapian::online::CheckNav(sessdata, &http, cancel, 4000);
  std::printf("spike_auth login=%d vip=%d code=%d elapsed_ms=%lld\n",
              auth.is_login ? 1 : 0, auth.vip_status, auth.api_code,
              static_cast<long long>(auth.elapsed_ms));
  if (!auth.is_login || auth.vip_status != 0) {
    lapian::online::ClearSecret(&sessdata);
    std::printf("spike_blocked login_or_vip\n");
    return 2;
  }

  lapian::online::DashFetchOptions options;
  options.timeout_ms = 8000;
  options.sessdata = sessdata;
  options.qn = qn > 0 ? qn : lapian::online::kDashRequestQn;
  options.fourk = 1;
  const auto dash = lapian::online::FetchDashCatalog(&http, bvid, cid, options, cancel);
  lapian::online::ClearSecret(&options.sessdata);
  Check("dash_ok", dash.error.ok(), dash.error.message.toStdString());
  if (!dash.error.ok()) {
    lapian::online::ClearSecret(&sessdata);
    return 1;
  }
  const auto& catalog = dash.catalog;
  std::printf("spike_catalog duration_ms=%lld default_qn=%d videos=%d audios=%d\n",
              static_cast<long long>(catalog.duration_ms), catalog.DefaultQn(),
              static_cast<int>(catalog.videos.size()),
              static_cast<int>(catalog.audios.size()));
  const lapian::online::DashStream* video = catalog.DefaultStream();
  const lapian::online::DashAudioStream* audio = catalog.BestAudio();
  Check("video_stream", video != nullptr);
  Check("audio_stream", audio != nullptr);
  if (video == nullptr || audio == nullptr) {
    lapian::online::ClearSecret(&sessdata);
    return 1;
  }
  std::printf("spike_pick qn=%d codecid=%d fps=%.3f w=%d h=%d audio_id=%d "
              "audio_codecs=%s video_init=%s video_index=%s audio_init=%s "
              "audio_index=%s\n",
              video->qn, video->codecid, video->fps, video->width, video->height,
              audio->id, audio->codecs.toUtf8().constData(),
              video->init_range.ToString().toUtf8().constData(),
              video->index_range.ToString().toUtf8().constData(),
              audio->init_range.ToString().toUtf8().constData(),
              audio->index_range.ToString().toUtf8().constData());

  lapian::online::WinHttpDownloadTransport transport;
  const auto video_probe = ProbeTotal(&transport, video->AllUrls(), cancel);
  const auto audio_probe = ProbeTotal(&transport, audio->AllUrls(), cancel);
  Check("video_total_probe", video_probe.ok,
        std::to_string(video_probe.total));
  Check("audio_total_probe", audio_probe.ok,
        std::to_string(audio_probe.total));
  if (!video_probe.ok || !audio_probe.ok) {
    lapian::online::ClearSecret(&sessdata);
    return 1;
  }
  std::printf("spike_totals video=%lld (%s range=%d) audio=%lld (%s range=%d)\n",
              static_cast<long long>(video_probe.total),
              video_probe.url_kind.toUtf8().constData(),
              video_probe.range_supported ? 1 : 0,
              static_cast<long long>(audio_probe.total),
              audio_probe.url_kind.toUtf8().constData(),
              audio_probe.range_supported ? 1 : 0);

  QTemporaryDir temp(QDir::tempPath() + QStringLiteral("/lapian-dash-spike-XXXXXX"));
  if (!temp.isValid()) {
    std::printf("spike_blocked no_temp_dir\n");
    return 2;
  }
  const QString video_part = temp.filePath(QStringLiteral("video.part"));
  const QString audio_part = temp.filePath(QStringLiteral("audio.part"));
  QFile video_file(video_part);
  QFile audio_file(audio_part);
  if (!video_file.open(QIODevice::WriteOnly) || !audio_file.open(QIODevice::WriteOnly)) {
    std::printf("spike_blocked temp_open\n");
    return 2;
  }
  QString dl_error;
  QElapsedTimer timer;
  timer.start();
  const qint64 video_prefix = DownloadPrefix(
      &transport, video->url, video_probe.total,
      static_cast<qint64>(prefetch_mb) * 1024 * 1024, &video_file, cancel, &dl_error);
  const qint64 audio_prefix = DownloadPrefix(
      &transport, audio->url, audio_probe.total,
      static_cast<qint64>(prefetch_mb) * 1024 * 1024, &audio_file, cancel, &dl_error);
  std::printf("spike_prefetch video=%lld audio=%lld elapsed_ms=%lld error=%s\n",
              static_cast<long long>(video_prefix),
              static_cast<long long>(audio_prefix),
              static_cast<long long>(timer.elapsed()),
              dl_error.toUtf8().constData());
  video_file.close();
  audio_file.close();
  Check("video_prefix", video_prefix > 0);
  Check("audio_prefix", audio_prefix > 0);
  if (video_prefix <= 0 || audio_prefix <= 0) {
    lapian::online::ClearSecret(&sessdata);
    return 1;
  }

  auto video_source =
      std::make_shared<lapian::online::GrowingSource>(video_probe.total);
  auto audio_source =
      std::make_shared<lapian::online::GrowingSource>(audio_probe.total);
  video_source->Publish(video_prefix);
  audio_source->Publish(std::min(audio_prefix, audio_probe.total));

  lapian::online::GrowingFormat video_format;
  lapian::online::GrowingFormat audio_format;
  QString format_error;
  timer.restart();
  const bool video_opened =
      video_format.Open(video_source, video_part, &format_error, 0);
  std::printf("spike_video_open ok=%d ms=%lld error=%s\n", video_opened ? 1 : 0,
              static_cast<long long>(timer.elapsed()),
              format_error.toUtf8().constData());
  Check("video_format_open", video_opened);
  timer.restart();
  const bool audio_opened =
      audio_format.Open(audio_source, audio_part, &format_error, 0);
  std::printf("spike_audio_open ok=%d ms=%lld error=%s\n", audio_opened ? 1 : 0,
              static_cast<long long>(timer.elapsed()),
              format_error.toUtf8().constData());
  Check("audio_format_open", audio_opened);
  if (!video_opened || !audio_opened) {
    lapian::online::ClearSecret(&sessdata);
    return 1;
  }

  AVFormatContext* video_ctx = video_format.context();
  AVFormatContext* audio_ctx = audio_format.context();
  std::printf("spike_video_fmt duration=%lld start=%lld nb_streams=%u\n",
              static_cast<long long>(video_ctx->duration),
              static_cast<long long>(video_ctx->start_time),
              video_ctx->nb_streams);
  std::printf("spike_audio_fmt duration=%lld start=%lld nb_streams=%u\n",
              static_cast<long long>(audio_ctx->duration),
              static_cast<long long>(audio_ctx->start_time),
              audio_ctx->nb_streams);

  int video_index = -1;
  for (unsigned i = 0; i < video_ctx->nb_streams; ++i) {
    if (video_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_index = static_cast<int>(i);
      break;
    }
  }
  int audio_index = -1;
  for (unsigned i = 0; i < audio_ctx->nb_streams; ++i) {
    if (audio_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audio_index = static_cast<int>(i);
      break;
    }
  }
  Check("video_stream_found", video_index >= 0);
  Check("audio_stream_found", audio_index >= 0);
  if (video_index < 0 || audio_index < 0) {
    lapian::online::ClearSecret(&sessdata);
    return 1;
  }
  AVStream* video_stream = video_ctx->streams[video_index];
  AVStream* audio_stream = audio_ctx->streams[audio_index];
  std::printf("spike_streams video_tb=%d/%d video_avg=%d/%d video_r=%d/%d "
              "video_start=%lld audio_tb=%d/%d audio_start=%lld audio_rate=%d "
              "audio_ch=%d audio_codec=%s\n",
              video_stream->time_base.num, video_stream->time_base.den,
              video_stream->avg_frame_rate.num, video_stream->avg_frame_rate.den,
              video_stream->r_frame_rate.num, video_stream->r_frame_rate.den,
              static_cast<long long>(video_stream->start_time),
              audio_stream->time_base.num, audio_stream->time_base.den,
              static_cast<long long>(audio_stream->start_time),
              audio_stream->codecpar->sample_rate,
              audio_stream->codecpar->ch_layout.nb_channels,
              avcodec_get_name(audio_stream->codecpar->codec_id));

  lapian::media::VideoDecoder decoder;
  std::string decoder_error;
  timer.restart();
  const bool decoder_opened = decoder.OpenWithFormat(
      video_ctx, video_index, nullptr, false, &decoder_error);
  std::printf("spike_decoder_open ok=%d ms=%lld error=%s\n",
              decoder_opened ? 1 : 0, static_cast<long long>(timer.elapsed()),
              decoder_error.c_str());
  Check("video_decoder_open", decoder_opened);
  if (!decoder_opened) {
    lapian::online::ClearSecret(&sessdata);
    return 1;
  }
  lapian::media::DecodedFrameInfo first_frame;
  timer.restart();
  const bool first_ok = decoder.DecodeFirstFrame(&first_frame, &decoder_error);
  std::printf("spike_first_frame ok=%d ms=%lld pts_us=%lld size=%dx%d hw=%d "
              "error=%s\n",
              first_ok ? 1 : 0, static_cast<long long>(timer.elapsed()),
              static_cast<long long>(first_frame.pts_us), first_frame.width,
              first_frame.height, first_frame.hardware ? 1 : 0,
              decoder_error.c_str());
  Check("video_first_frame", first_ok);

  // 音频首包 PTS / skip_samples（与 muxed container 的 av_offset 计算口径一致）。
  AVPacket* packet = av_packet_alloc();
  std::int64_t audio_first_pts = AV_NOPTS_VALUE;
  int skip_samples = 0;
  while (packet != nullptr && av_read_frame(audio_ctx, packet) >= 0) {
    if (packet->stream_index == audio_index) {
      audio_first_pts = packet->pts;
      size_t side_size = 0;
      const std::uint8_t* side = av_packet_get_side_data(
          packet, AV_PKT_DATA_SKIP_SAMPLES, &side_size);
      if (side != nullptr && side_size >= sizeof(std::uint32_t)) {
        skip_samples = static_cast<int>(AV_RL32(side));
      }
      av_packet_unref(packet);
      break;
    }
    av_packet_unref(packet);
  }
  const std::int64_t video_start_us =
      video_stream->start_time == AV_NOPTS_VALUE
          ? 0
          : av_rescale_q(video_stream->start_time, video_stream->time_base,
                         AV_TIME_BASE_Q);
  const std::int64_t audio_start_us =
      audio_first_pts == AV_NOPTS_VALUE
          ? 0
          : av_rescale_q(audio_first_pts, audio_stream->time_base, AV_TIME_BASE_Q);
  std::printf("spike_audio_first pts=%lld start_us=%lld video_start_us=%lld "
              "skip_samples=%d\n",
              static_cast<long long>(audio_first_pts),
              static_cast<long long>(audio_start_us),
              static_cast<long long>(video_start_us), skip_samples);
  Check("audio_first_packet", audio_first_pts != AV_NOPTS_VALUE);

  lapian::audio::AudioFormat output;
  output.sample_rate = 48000;
  output.channels = 2;
  output.encoding = lapian::audio::SampleEncoding::kFloat32;
  lapian::audio::AudioDecoder audio_decoder;
  std::string audio_error;
  timer.restart();
  const bool audio_decoder_opened = audio_decoder.OpenWithFormat(
      audio_ctx, audio_index, output, &audio_error);
  std::printf("spike_audio_decoder ok=%d ms=%lld codec=%s src_rate=%d src_ch=%d "
              "error=%s\n",
              audio_decoder_opened ? 1 : 0,
              static_cast<long long>(timer.elapsed()),
              audio_decoder.info().codec.c_str(),
              audio_decoder.info().source_sample_rate,
              audio_decoder.info().source_channels, audio_error.c_str());
  Check("audio_decoder_open", audio_decoder_opened);
  if (audio_decoder_opened) {
    lapian::audio::AudioChunk chunk;
    timer.restart();
    const auto decode = audio_decoder.DecodeChunk(1024, &chunk, &audio_error);
    std::printf("spike_audio_chunk result=%d ms=%lld frames=%zu pts=%.6f "
                "error=%s\n",
                static_cast<int>(decode), static_cast<long long>(timer.elapsed()),
                chunk.frames, chunk.pts_seconds, audio_error.c_str());
    Check("audio_first_chunk", decode == lapian::audio::AudioDecodeResult::kFrame);
  }

  // seek fan-out：视频精确 seek 到 N 秒 + 音频 seek 到同一媒体秒。
  if (first_ok) {
    // T8.10：验证“精确到关键帧 PTS”的 seek 落点（fMP4 的 av_seek_frame 会
    // 落到前一个关键帧；SeekToKeyframePts 用 min=ts=max 约束）。
    const std::int64_t key_pts = 15'000'000;
    timer.restart();
    const bool key_seek = decoder.SeekToKeyframePts(key_pts, &decoder_error);
    lapian::media::DecodedFrameInfo key_frame;
    const bool key_landed =
        key_seek && decoder.DecodeNextFrame(&key_frame, &decoder_error);
    std::printf("spike_keyframe_seek ok=%d landed=%d ms=%lld pts_us=%lld "
                "target_us=%lld error=%s\n",
                key_seek ? 1 : 0, key_landed ? 1 : 0,
                static_cast<long long>(timer.elapsed()),
                static_cast<long long>(key_frame.pts_us),
                static_cast<long long>(key_pts), decoder_error.c_str());
    Check("video_keyframe_seek", key_seek && key_landed);
    Check("video_keyframe_landing",
          key_landed && std::llabs(key_frame.pts_us - key_pts) <= 1000);

    const std::int64_t target_us =
        static_cast<std::int64_t>(seek_seconds * 1.0e6);
    timer.restart();
    const bool video_seek = decoder.SeekToPts(target_us, &decoder_error);    lapian::media::DecodedFrameInfo seek_frame;
    const bool video_landed =
        video_seek && decoder.DecodeNextFrame(&seek_frame, &decoder_error);
    std::printf("spike_video_seek ok=%d landed=%d ms=%lld pts_us=%lld error=%s\n",
                video_seek ? 1 : 0, video_landed ? 1 : 0,
                static_cast<long long>(timer.elapsed()),
                static_cast<long long>(seek_frame.pts_us), decoder_error.c_str());
    Check("video_seek", video_seek && video_landed);

    timer.restart();
    const bool audio_seek = audio_decoder.SeekTo(seek_seconds, &audio_error);
    lapian::audio::AudioChunk seek_chunk;
    const auto seek_decode =
        audio_seek ? audio_decoder.DecodeChunk(1024, &seek_chunk, &audio_error)
                   : lapian::audio::AudioDecodeResult::kError;
    std::printf("spike_audio_seek ok=%d decode=%d ms=%lld pts=%.6f error=%s\n",
                audio_seek ? 1 : 0, static_cast<int>(seek_decode),
                static_cast<long long>(timer.elapsed()), seek_chunk.pts_seconds,
                audio_error.c_str());
    Check("audio_seek", audio_seek &&
                            seek_decode == lapian::audio::AudioDecodeResult::kFrame);
    if (video_landed && seek_decode == lapian::audio::AudioDecodeResult::kFrame) {
      const double drift_ms =
          (seek_chunk.pts_seconds - first_frame.pts_us / 1.0e6 -
           (seek_frame.pts_us - first_frame.pts_us) / 1.0e6) *
          1000.0;
      std::printf("spike_seek_drift_ms=%.3f\n", drift_ms);
    }
  }

  if (packet != nullptr) {
    av_packet_free(&packet);
  }
  audio_decoder.Close();
  decoder.Close();
  audio_format.Close();
  video_format.Close();
  video_source->Cancel();
  audio_source->Cancel();
  lapian::online::ClearSecret(&sessdata);

  std::printf("spike_result checks=%d failures=%d verdict=%s\n", g_checks,
              g_failures, g_failures == 0 ? "pass" : "fail");
  return g_failures == 0 ? 0 : 1;
}
