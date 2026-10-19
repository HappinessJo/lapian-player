// T0.3 精确 seek 探针
//
// 回答一个问题：在“关键帧回退 → 解码到目标帧”这条路径上，P6（松手精确）
// 与 P7（单帧步进）的物理成本是多少。
//
// 不渲染、不做缓存、不做预取，只测：随机目标帧 → 关键帧 seek → 解码到目标 PTS
// 的墙钟时间。硬解与软解各跑一轮，输出 p50/p95、GOP 分布与每样本 CSV。
//
// 用法：
//   lapian_seek_probe --file V1.mp4 --samples 20 --hw on --csv out.csv
//   lapian_seek_probe --file V1.mp4 --samples 20 --hw off

#include <windows.h>

#include <d3d11.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/pixdesc.h>
}

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
  std::string file;
  int samples = 20;
  bool hw = true;
  unsigned seed = 1;
  std::string csv;
};

struct Sample {
  int index = 0;
  int64_t target_frame = 0;
  int64_t target_pts = 0;
  int64_t actual_pts = AV_NOPTS_VALUE;
  double delta_frames = 0;
  double ms = 0;
  int decoded_frames = 0;
  bool ok = false;
};

// 解码器登记的硬件像素格式（D3D11VA 是 d3d11va_vld，不是 d3d11）
AVPixelFormat g_hw_pix_fmt = AV_PIX_FMT_NONE;

void PrintUsage() {
  std::printf(
      "用法: lapian_seek_probe --file <视频> [--samples 20] [--hw on|off] [--seed 1] [--csv out.csv]\n");
}

bool ParseArgs(int argc, char** argv, Options* opts) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto next = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s 需要参数值\n", name);
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "--file") {
      opts->file = next("--file");
    } else if (arg == "--samples") {
      opts->samples = std::atoi(next("--samples").c_str());
    } else if (arg == "--hw") {
      std::string v = next("--hw");
      opts->hw = (v != "off" && v != "0" && v != "false");
    } else if (arg == "--seed") {
      opts->seed = static_cast<unsigned>(std::strtoul(next("--seed").c_str(), nullptr, 10));
    } else if (arg == "--csv") {
      opts->csv = next("--csv");
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage();
      std::exit(0);
    } else {
      std::fprintf(stderr, "未知参数：%s\n", arg.c_str());
      return false;
    }
  }
  if (opts->file.empty()) {
    PrintUsage();
    return false;
  }
  if (opts->samples <= 0) {
    opts->samples = 1;
  }
  return true;
}

double Percentile(std::vector<double> values, double p) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  size_t rank = static_cast<size_t>(std::ceil(p / 100.0 * values.size()));
  if (rank == 0) {
    rank = 1;
  }
  return values[rank - 1];
}

AVPixelFormat GetPreferredHwFormat(AVCodecContext* /*ctx*/,
                                   const enum AVPixelFormat* formats) {
  for (const enum AVPixelFormat* p = formats; *p != AV_PIX_FMT_NONE; ++p) {
    if (*p == g_hw_pix_fmt) {
      return *p;
    }
  }
  return formats[0];
}

}  // namespace

int main(int argc, char** argv) {
  Options opts;
  if (!ParseArgs(argc, argv, &opts)) {
    return 2;
  }

  std::printf("FILE=%s\n", opts.file.c_str());

  AVFormatContext* fmt = nullptr;
  int ret = avformat_open_input(&fmt, opts.file.c_str(), nullptr, nullptr);
  if (ret < 0) {
    char err[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(ret, err, sizeof(err));
    std::printf("ERROR=打开失败: %s\n", err);
    return 3;
  }
  if (avformat_find_stream_info(fmt, nullptr) < 0) {
    std::printf("ERROR=读取流信息失败\n");
    avformat_close_input(&fmt);
    return 3;
  }

  const int vstream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (vstream < 0) {
    std::printf("ERROR=没有视频流\n");
    avformat_close_input(&fmt);
    return 3;
  }

  AVStream* st = fmt->streams[vstream];
  AVCodecParameters* par = st->codecpar;
  const AVCodec* codec = avcodec_find_decoder(par->codec_id);
  if (!codec) {
    std::printf("ERROR=没有可用解码器\n");
    avformat_close_input(&fmt);
    return 3;
  }

  // 记录解码器登记的硬件格式（供 get_format 与帧池参数使用）
  for (int i = 0;; ++i) {
    const AVCodecHWConfig* cfg = avcodec_get_hw_config(codec, i);
    if (cfg == nullptr) {
      break;
    }
    std::printf("HWCFG_%d=pix:%s methods:0x%x\n", i, av_get_pix_fmt_name(cfg->pix_fmt),
                cfg->methods);
    if (cfg->pix_fmt == AV_PIX_FMT_D3D11) {
      g_hw_pix_fmt = cfg->pix_fmt;  // 优先 d3d11va2（现代 D3D11 路径）
    } else if (g_hw_pix_fmt == AV_PIX_FMT_NONE) {
      g_hw_pix_fmt = cfg->pix_fmt;
    }
  }
  std::printf("HW_PIXFMT=%s\n",
              g_hw_pix_fmt == AV_PIX_FMT_NONE ? "none" : av_get_pix_fmt_name(g_hw_pix_fmt));

  AVCodecContext* dec = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(dec, par);
  dec->pkt_timebase = st->time_base;

  const bool hw_requested = opts.hw && g_hw_pix_fmt != AV_PIX_FMT_NONE;
  bool hw_ready = false;
  if (hw_requested) {
    AVBufferRef* device = nullptr;
    int err = av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
    if (err >= 0) {
      // 交给 libavcodec：get_format 选中硬件格式后，它会按 hw config 建帧池
      dec->hw_device_ctx = av_buffer_ref(device);
      dec->get_format = GetPreferredHwFormat;
      hw_ready = true;
      av_buffer_unref(&device);
    } else {
      char ebuf[AV_ERROR_MAX_STRING_SIZE] = {};
      av_strerror(err, ebuf, sizeof(ebuf));
      std::printf("NOTE=D3D11VA 设备创建失败(%s)，回退软解\n", ebuf);
    }
  }

  if (avcodec_open2(dec, codec, nullptr) < 0) {
    std::printf("ERROR=解码器打开失败\n");
    if (dec->hw_frames_ctx) {
      av_buffer_unref(&dec->hw_frames_ctx);
    }
    if (dec->hw_device_ctx) {
      av_buffer_unref(&dec->hw_device_ctx);
    }
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return 3;
  }

  // 帧率与时长
  AVRational fr = st->avg_frame_rate;
  if (fr.num <= 0 || fr.den <= 0) {
    fr = st->r_frame_rate;
  }
  const double fps = av_q2d(fr);
  const double tb = av_q2d(st->time_base);
  const double start_s = (st->start_time != AV_NOPTS_VALUE) ? st->start_time * tb : 0.0;
  const double stream_dur_s =
      (st->duration != AV_NOPTS_VALUE) ? st->duration * tb : (fmt->duration / (double)AV_TIME_BASE);

  int64_t total_frames = st->nb_frames;
  if (total_frames <= 0 && fps > 0 && stream_dur_s > 0) {
    total_frames = static_cast<int64_t>(std::llround(stream_dur_s * fps));
  }

  std::printf("CODEC=%s\n", avcodec_get_name(par->codec_id));
  std::printf("SIZE=%dx%d\n", par->width, par->height);
  std::printf("FPS=%.6f\n", fps);
  std::printf("TB=%.9f\n", tb);
  std::printf("DURATION_S=%.3f\n", stream_dur_s);
  std::printf("FRAMES=%lld\n", static_cast<long long>(total_frames));

  if (total_frames < 4) {
    std::printf("ERROR=帧数太少（%lld），无法取样\n", static_cast<long long>(total_frames));
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return 3;
  }

  // 建关键帧索引（只 demux，不解码）
  std::vector<int64_t> key_pts;
  AVPacket* pkt = av_packet_alloc();
  while (av_read_frame(fmt, pkt) >= 0) {
    if (pkt->stream_index == vstream && (pkt->flags & AV_PKT_FLAG_KEY) &&
        pkt->pts != AV_NOPTS_VALUE) {
      key_pts.push_back(pkt->pts);
    }
    av_packet_unref(pkt);
  }
  std::sort(key_pts.begin(), key_pts.end());
  key_pts.erase(std::unique(key_pts.begin(), key_pts.end()), key_pts.end());

  std::printf("KEYFRAMES=%zu\n", key_pts.size());
  if (key_pts.size() >= 2 && fps > 0) {
    std::vector<double> gop_frames;
    for (size_t i = 1; i < key_pts.size(); ++i) {
      gop_frames.push_back((key_pts[i] - key_pts[i - 1]) * tb * fps);
    }
    std::sort(gop_frames.begin(), gop_frames.end());
    double sum = 0;
    for (double v : gop_frames) {
      sum += v;
    }
    std::printf("GOP_MEAN_FRAMES=%.1f\n", sum / gop_frames.size());
    std::printf("GOP_MAX_FRAMES=%.1f\n", gop_frames.back());
    std::printf("GOP_P95_FRAMES=%.1f\n", Percentile(gop_frames, 95));
  }

  auto frame_to_pts = [&](int64_t frame) -> int64_t {
    const double t = start_s + static_cast<double>(frame) / fps;
    return static_cast<int64_t>(std::llround(t / tb));
  };
  auto pts_to_frame = [&](int64_t pts) -> int64_t {
    const double t = pts * tb - start_s;
    return static_cast<int64_t>(std::llround(t * fps));
  };

  // 预热：一次中间位置 seek，丢弃结果
  {
    const int64_t warm_pts = frame_to_pts(total_frames / 2);
    if (av_seek_frame(fmt, vstream, warm_pts, AVSEEK_FLAG_BACKWARD) >= 0) {
      avcodec_flush_buffers(dec);
      AVFrame* warm = av_frame_alloc();
      AVPacket* wp = av_packet_alloc();
      while (av_read_frame(fmt, wp) >= 0) {
        if (wp->stream_index != vstream) {
          av_packet_unref(wp);
          continue;
        }
        const int wr = avcodec_send_packet(dec, wp);
        av_packet_unref(wp);
        if (wr < 0 && wr != AVERROR(EAGAIN)) {
          break;
        }
        if (avcodec_receive_frame(dec, warm) == 0) {
          break;
        }
      }
      av_frame_free(&warm);
      av_packet_free(&wp);
    }
  }

  std::mt19937 rng(opts.seed);
  std::uniform_int_distribution<int64_t> pick(1, total_frames - 2);

  std::vector<Sample> samples;
  samples.reserve(static_cast<size_t>(opts.samples));

  AVPacket* dp = av_packet_alloc();
  AVFrame* df = av_frame_alloc();
  int actual_format = AV_PIX_FMT_NONE;

  for (int i = 0; i < opts.samples; ++i) {
    Sample s;
    s.index = i + 1;
    s.target_frame = pick(rng);
    s.target_pts = frame_to_pts(s.target_frame);

    bool found = false;
    auto drain = [&]() {
      while (!found) {
        const int rr = avcodec_receive_frame(dec, df);
        if (rr == AVERROR(EAGAIN) || rr == AVERROR_EOF) {
          return;
        }
        if (rr < 0) {
          return;
        }
        ++s.decoded_frames;
        if (actual_format == AV_PIX_FMT_NONE) {
          actual_format = static_cast<int>(df->format);
        }
        int64_t pts = df->best_effort_timestamp;
        if (pts == AV_NOPTS_VALUE) {
          pts = df->pts;
        }
        if (pts != AV_NOPTS_VALUE && pts >= s.target_pts) {
          s.actual_pts = pts;
          s.ok = true;
          found = true;
        }
        av_frame_unref(df);
      }
    };

    const auto t0 = Clock::now();
    if (av_seek_frame(fmt, vstream, s.target_pts, AVSEEK_FLAG_BACKWARD) < 0) {
      s.ok = false;
    } else {
      avcodec_flush_buffers(dec);
      bool eof = false;
      while (!found && !eof) {
        const int rr = av_read_frame(fmt, dp);
        if (rr < 0) {
          eof = true;
          break;
        }
        if (dp->stream_index != vstream) {
          av_packet_unref(dp);
          continue;
        }
        bool sent = false;
        while (!sent && !found) {
          const int sr = avcodec_send_packet(dec, dp);
          if (sr == AVERROR(EAGAIN)) {
            drain();
            continue;
          }
          if (sr < 0) {
            break;
          }
          sent = true;
        }
        av_packet_unref(dp);
        if (!found) {
          drain();
        }
      }
      if (!found) {
        // EOF 后必须 flush，否则最后几帧留在解码器缓冲里
        int sr = avcodec_send_packet(dec, nullptr);
        while (sr == AVERROR(EAGAIN)) {
          drain();
          sr = avcodec_send_packet(dec, nullptr);
        }
        drain();
      }
    }
    const auto t1 = Clock::now();
    s.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (s.ok) {
      s.delta_frames = static_cast<double>(pts_to_frame(s.actual_pts) - s.target_frame);
    }
    samples.push_back(s);
  }

  av_packet_free(&dp);
  av_frame_free(&df);

  int ok_count = 0;
  std::vector<double> times;
  for (const Sample& s : samples) {
    if (s.ok) {
      ++ok_count;
      times.push_back(s.ms);
    }
  }

  std::printf("SAMPLES=%d\n", opts.samples);
  std::printf("OK=%d\n", ok_count);
  std::printf("HW_REQUESTED=%d\n", hw_requested ? 1 : 0);
  std::printf("HW_ACTIVE=%d\n", hw_ready ? 1 : 0);
  std::printf("PIX_FMT=%s\n",
              actual_format == AV_PIX_FMT_NONE
                  ? "unknown"
                  : av_get_pix_fmt_name(static_cast<AVPixelFormat>(actual_format)));
  if (!times.empty()) {
    std::printf("P50_MS=%.2f\n", Percentile(times, 50));
    std::printf("P95_MS=%.2f\n", Percentile(times, 95));
    std::printf("MIN_MS=%.2f\n", *std::min_element(times.begin(), times.end()));
    std::printf("MAX_MS=%.2f\n", *std::max_element(times.begin(), times.end()));
  }
  std::printf("EXACT=%d\n", ok_count == opts.samples ? 1 : 0);

  if (!opts.csv.empty()) {
    FILE* f = nullptr;
    if (fopen_s(&f, opts.csv.c_str(), "wb") == 0 && f != nullptr) {
      std::fprintf(f,
                   "sample,target_frame,target_pts,actual_pts,delta_frames,decode_ms,"
                   "frames_decoded,ok\n");
      for (const Sample& s : samples) {
        std::fprintf(f, "%d,%lld,%lld,%lld,%.2f,%.2f,%d,%d\n", s.index,
                     static_cast<long long>(s.target_frame), static_cast<long long>(s.target_pts),
                     static_cast<long long>(s.actual_pts), s.delta_frames, s.ms,
                     s.decoded_frames, s.ok ? 1 : 0);
      }
      std::fclose(f);
      std::printf("CSV=%s\n", opts.csv.c_str());
    }
  }

  avcodec_free_context(&dec);
  avformat_close_input(&fmt);
  return ok_count == opts.samples ? 0 : 1;
}
