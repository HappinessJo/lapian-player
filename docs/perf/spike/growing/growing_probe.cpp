// S2 spike: can the demuxer read a growing source through a blocking AVIO?
//
// Simulates "download while playing": a worker thread advances a synthetic
// "cached bytes" counter at a fixed rate; the read callback blocks until the
// requested bytes are cached instead of reporting EOF. Measures how long the
// demuxer waits, and whether seek + frame accuracy survive.
//
// This is a dev probe. It is not part of the product build.

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double Ms(Clock::duration d) {
  return std::chrono::duration<double, std::milli>(d).count();
}

struct Shared {
  FILE* fp = nullptr;
  int64_t file_size = 0;
  int64_t pos = 0;
  std::atomic<int64_t> cached{0};
  std::atomic<bool> writer_done{false};
  std::atomic<int64_t> blocked_us{0};
  std::atomic<int> block_events{0};
  std::atomic<bool> cancel{false};
  std::mutex m;
  std::condition_variable cv;
};

int ReadPacket(void* opaque, uint8_t* buf, int buf_size) {
  auto* s = static_cast<Shared*>(opaque);
  if (s->cancel.load(std::memory_order_acquire)) return AVERROR_EXIT;
  if (s->pos >= s->file_size) return AVERROR_EOF;

  if (s->cached.load(std::memory_order_acquire) <= s->pos && !s->writer_done.load()) {
    auto t0 = Clock::now();
    std::unique_lock<std::mutex> lk(s->m);
    s->cv.wait(lk, [&] {
      return s->cached.load(std::memory_order_acquire) > s->pos ||
             s->writer_done.load() || s->cancel.load(std::memory_order_acquire);
    });
    s->blocked_us.fetch_add(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count());
    s->block_events.fetch_add(1);
    if (s->cancel.load(std::memory_order_acquire)) return AVERROR_EXIT;
  }

  int64_t avail = s->cached.load(std::memory_order_acquire) - s->pos;
  if (avail <= 0 && s->writer_done.load()) return AVERROR_EOF;
  if (avail <= 0) return AVERROR(EAGAIN);

  int n = buf_size;
  if (avail < n) n = static_cast<int>(avail);
  if (s->file_size - s->pos < n) n = static_cast<int>(s->file_size - s->pos);
  if (fseek(s->fp, static_cast<long>(s->pos), SEEK_SET) != 0) return AVERROR(EIO);
  size_t got = fread(buf, 1, static_cast<size_t>(n), s->fp);
  if (got == 0) return AVERROR_EOF;
  s->pos += static_cast<int64_t>(got);
  return static_cast<int>(got);
}

int64_t SeekPacket(void* opaque, int64_t offset, int whence) {
  auto* s = static_cast<Shared*>(opaque);
  if (whence == AVSEEK_SIZE) return s->file_size;
  whence &= ~AVSEEK_FORCE;

  int64_t target = 0;
  if (whence == SEEK_SET) target = offset;
  else if (whence == SEEK_CUR) target = s->pos + offset;
  else if (whence == SEEK_END) target = s->file_size + offset;
  else return AVERROR(EINVAL);

  if (target < 0) target = 0;
  if (target > s->file_size) target = s->file_size;
  s->pos = target;
  return target;
}

struct DecodeCtx {
  AVFormatContext* fmt = nullptr;
  AVCodecContext* dec = nullptr;
  int vstream = -1;
  AVRational tb{1, 1};
};

bool OpenDecode(const char* path, Shared* shared, DecodeCtx* dc, double* open_ms) {
  auto t0 = Clock::now();
  AVFormatContext* fmt = avformat_alloc_context();
  if (!fmt) return false;

  AVIOContext* avio = nullptr;
  if (shared) {
    constexpr int kIoBuf = 64 * 1024;
    unsigned char* iobuf = static_cast<unsigned char*>(av_malloc(kIoBuf));
    avio = avio_alloc_context(iobuf, kIoBuf, 0, shared, ReadPacket, nullptr, SeekPacket);
    if (!avio) return false;
    fmt->pb = avio;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
  }

  int ret = avformat_open_input(&fmt, shared ? nullptr : path, nullptr, nullptr);
  if (ret < 0) {
    char err[256];
    av_strerror(ret, err, sizeof(err));
    std::printf("open_failed=%s\n", err);
    return false;
  }
  if (avformat_find_stream_info(fmt, nullptr) < 0) {
    std::printf("stream_info_failed\n");
    return false;
  }
  int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (vs < 0) {
    std::printf("no_video_stream\n");
    return false;
  }
  AVStream* st = fmt->streams[vs];
  const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
  if (!codec) {
    std::printf("no_decoder\n");
    return false;
  }
  AVCodecContext* dec = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(dec, st->codecpar);
  dec->thread_count = 1;
  if (avcodec_open2(dec, codec, nullptr) < 0) {
    std::printf("decoder_open_failed\n");
    return false;
  }

  dc->fmt = fmt;
  dc->dec = dec;
  dc->vstream = vs;
  dc->tb = st->time_base;
  *open_ms = Ms(Clock::now() - t0);
  return true;
}

// Decode forward until a frame with pts >= target_s is produced.
bool DecodeUntil(DecodeCtx* dc, double target_s, int max_packets, double* pts_s,
                 int* packets) {
  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  bool got = false;
  *packets = 0;
  *pts_s = -1;

  auto take_frames = [&]() {
    while (avcodec_receive_frame(dc->dec, frame) == 0) {
      double p = frame->pts == AV_NOPTS_VALUE
                     ? -1.0
                     : frame->pts * av_q2d(dc->tb);
      if (p >= target_s - 1e-6) {
        *pts_s = p;
        got = true;
      }
      av_frame_unref(frame);
      if (got) return true;
    }
    return false;
  };

  if (take_frames()) {
    av_packet_free(&pkt);
    av_frame_free(&frame);
    return true;
  }

  while (*packets < max_packets) {
    int ret = av_read_frame(dc->fmt, pkt);
    if (ret < 0) break;
    if (pkt->stream_index == dc->vstream) {
      (*packets)++;
      if (avcodec_send_packet(dc->dec, pkt) >= 0) {
        if (take_frames()) {
          av_packet_unref(pkt);
          break;
        }
      }
    }
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  av_frame_free(&frame);
  return got;
}

void CloseDecode(DecodeCtx* dc) {
  if (dc->dec) avcodec_free_context(&dc->dec);
  if (dc->fmt) {
    if (dc->fmt->pb) {
      av_freep(&dc->fmt->pb->buffer);
      avio_context_free(&dc->fmt->pb);
    }
    avformat_close_input(&dc->fmt);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::printf(
        "usage: growing_probe <file.mp4> <initial_cached_mb> <rate_mb_s> "
        "[baseline|cancel] [cancel_after_ms]\n");
    return 2;
  }
  const char* path = argv[1];
  double initial_mb = atof(argv[2]);
  double rate_mb_s = atof(argv[3]);
  bool baseline = (argc > 4 && std::strcmp(argv[4], "baseline") == 0);
  bool cancel_mode = (argc > 4 && std::strcmp(argv[4], "cancel") == 0);
  int cancel_after_ms = (cancel_mode && argc > 5) ? atoi(argv[5]) : 500;

  FILE* fp = fopen(path, "rb");
  if (!fp) {
    std::printf("cannot_open_file\n");
    return 2;
  }
  _fseeki64(fp, 0, SEEK_END);
  int64_t size = _ftelli64(fp);
  _fseeki64(fp, 0, SEEK_SET);

  Shared shared;
  shared.fp = fp;
  shared.file_size = size;

  std::thread writer;
  if (!baseline) {
    int64_t initial = static_cast<int64_t>(initial_mb * 1024 * 1024);
    if (initial > size) initial = size;
    shared.cached.store(initial);
    int64_t rate_bps = static_cast<int64_t>(rate_mb_s * 1024 * 1024);
    writer = std::thread([&shared, rate_bps, size] {
      constexpr int kTickMs = 10;
      int64_t step = rate_bps * kTickMs / 1000;
      if (step < 1) step = 1;
      while (shared.cached.load() < size) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
        int64_t next = shared.cached.load() + step;
        if (next > size) next = size;
        shared.cached.store(next);
        { std::lock_guard<std::mutex> lk(shared.m); }
        shared.cv.notify_all();
      }
      shared.writer_done.store(true);
      { std::lock_guard<std::mutex> lk(shared.m); }
      shared.cv.notify_all();
    });
  }

  DecodeCtx dc;
  double open_ms = 0;
  if (!OpenDecode(path, baseline ? nullptr : &shared, &dc, &open_ms)) {
    if (writer.joinable()) { shared.writer_done.store(true); shared.cv.notify_all(); writer.join(); }
    fclose(fp);
    return 1;
  }

  AVStream* st = dc.fmt->streams[dc.vstream];
  double fps = st->avg_frame_rate.den ? av_q2d(st->avg_frame_rate) : 0.0;
  std::printf("mode=%s file_bytes=%lld cached_initial_mb=%.2f rate_mb_s=%.2f\n",
              baseline ? "baseline" : "growing", static_cast<long long>(size),
              baseline ? -1.0 : initial_mb, baseline ? -1.0 : rate_mb_s);
  std::printf("open_ms=%.1f fps=%.3f duration_s=%.3f\n", open_ms, fps,
              dc.fmt->duration / 1e6);

  double pts = -1;
  int packets = 0;
  auto t0 = Clock::now();
  bool first = DecodeUntil(&dc, 0.0, 100, &pts, &packets);
  std::printf("first_frame ok=%d wall_ms=%.1f pts=%.3f packets=%d blocked_ms=%.1f\n",
              first ? 1 : 0, Ms(Clock::now() - t0), pts, packets,
              shared.blocked_us.load() / 1000.0);

  if (cancel_mode) {
    // Simulate the user seeking away while a blocking read is in flight.
    std::thread canceller([&shared, cancel_after_ms] {
      std::this_thread::sleep_for(std::chrono::milliseconds(cancel_after_ms));
      shared.cancel.store(true);
      { std::lock_guard<std::mutex> lk(shared.m); }
      shared.cv.notify_all();
    });
    int64_t ts = static_cast<int64_t>(90.0 / av_q2d(dc.tb));
    auto tseek = Clock::now();
    av_seek_frame(dc.fmt, dc.vstream, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(dc.dec);
    double seek_pts = -1;
    int pkts = 0;
    bool ok = DecodeUntil(&dc, 90.0, 600, &seek_pts, &pkts);
    double wall = Ms(Clock::now() - tseek);
    std::printf(
        "cancel_mode after_ms=%d ok=%d wall_ms=%.1f pts=%.3f blocked_ms=%.1f\n",
        cancel_after_ms, ok ? 1 : 0, wall, seek_pts,
        shared.blocked_us.load() / 1000.0);
    canceller.join();
    CloseDecode(&dc);
    shared.writer_done.store(true);
    shared.cv.notify_all();
    if (writer.joinable()) writer.join();
    fclose(fp);
    return ok ? 3 : 0;  // 0 = cancelled as expected, 3 = seek still completed
  }

  const double targets[] = {47.3, 12.7, 83.1, 21.6, 91.4, 3.9, 61.2, 31.8};
  double max_err = 0;
  for (double target : targets) {
    int64_t ts = static_cast<int64_t>(target / av_q2d(dc.tb));
    int64_t before_blocked = shared.blocked_us.load();
    int before_events = shared.block_events.load();
    auto tseek = Clock::now();
    int ret = av_seek_frame(dc.fmt, dc.vstream, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(dc.dec);
    double seek_pts = -1;
    bool ok = false;
    if (ret >= 0) ok = DecodeUntil(&dc, target, 600, &seek_pts, &packets);
    double wall = Ms(Clock::now() - tseek);
    double blocked = (shared.blocked_us.load() - before_blocked) / 1000.0;
    int events = shared.block_events.load() - before_events;
    double err = (ok && seek_pts >= 0) ? (seek_pts - target) : -1;
    if (err > max_err) max_err = err;
    std::printf(
        "seek target=%.1f ret=%d ok=%d wall_ms=%.1f blocked_ms=%.1f events=%d "
        "pts=%.3f err=%.3f\n",
        target, ret, ok ? 1 : 0, wall, blocked, events, seek_pts, err);
    if (!ok) break;
  }

  std::printf("summary total_blocked_ms=%.1f total_events=%d max_seek_err_s=%.3f\n",
              shared.blocked_us.load() / 1000.0, shared.block_events.load(), max_err);

  CloseDecode(&dc);
  if (writer.joinable()) {
    shared.writer_done.store(true);
    shared.cv.notify_all();
    writer.join();
  }
  fclose(fp);
  return 0;
}
