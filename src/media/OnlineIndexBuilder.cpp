#include "media/OnlineIndexBuilder.h"

#include <QString>

#include <algorithm>
#include <chrono>
#include <memory>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "online/GrowingSource.h"

namespace lapian::media {
namespace {

std::string AvErrorText(int error) {
  if (error >= 0) {
    return {};
  }
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  if (av_strerror(error, buffer, sizeof(buffer)) < 0) {
    return {};
  }
  return buffer;
}

struct TempEntry {
  FrameIndexEntry entry;
  OnlineFrameSpan span;
};

}  // namespace

OnlineIndexResult BuildOnlineIndex(
    const std::shared_ptr<lapian::online::GrowingSource>& source,
    const std::string& part_path_utf8, int video_stream_index,
    int tail_guard_frames, const std::atomic<bool>* cancel) {
  OnlineIndexResult result;
  const auto started = std::chrono::steady_clock::now();
  auto finish = [&]() {
    result.build_ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - started)
                          .count();
    return result;
  };

  if (!source || !source->IsValid()) {
    result.error = "可增长数据源无效";
    return finish();
  }

  lapian::online::GrowingFormat format;
  QString format_error;
  // 100ms 前沿超时：有数据就继续读；读到当前发布前沿就结束，交给下一次重建。
  if (!format.Open(source, QString::fromUtf8(part_path_utf8.c_str()),
                   &format_error, 100)) {
    result.error = format_error.toUtf8().constData();
    result.complete = source->Published() >= source->Size();
    result.timed_out = result.error.find("timed out") != std::string::npos ||
                       result.error.find("超时") != std::string::npos;
    return finish();
  }

  AVFormatContext* context = format.context();
  int selected = video_stream_index;
  if (selected < 0) {
    for (unsigned int i = 0; i < context->nb_streams; ++i) {
      const AVCodecParameters* parameters = context->streams[i]->codecpar;
      if (parameters != nullptr && parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
        selected = static_cast<int>(i);
        break;
      }
    }
  }
  if (selected < 0 || selected >= static_cast<int>(context->nb_streams)) {
    result.error = "找不到视频流";
    return finish();
  }

  AVStream* stream = context->streams[selected];
  AVCodecParserContext* parser = av_parser_init(stream->codecpar->codec_id);
  const AVCodec* parser_codec = avcodec_find_decoder(stream->codecpar->codec_id);
  AVCodecContext* parser_context =
      parser_codec != nullptr ? avcodec_alloc_context3(parser_codec) : nullptr;
  if (parser_context != nullptr) {
    avcodec_parameters_to_context(parser_context, stream->codecpar);
  }
  if (parser != nullptr && parser_context == nullptr) {
    av_parser_close(parser);
    parser = nullptr;
  }

  AVPacket* packet = av_packet_alloc();
  if (packet == nullptr) {
    if (parser != nullptr) {
      av_parser_close(parser);
    }
    if (parser_context != nullptr) {
      avcodec_free_context(&parser_context);
    }
    result.error = "分配 packet 失败";
    return finish();
  }

  std::vector<TempEntry> entries;
  int packets = 0;
  int parsed_frames = 0;
  bool read_failed = false;
  std::string read_error;
  while (true) {
    if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
      result.error = "索引已取消";
      read_failed = true;
      break;
    }
    const int read = av_read_frame(context, packet);
    if (read < 0) {
      if (read == AVERROR(ETIMEDOUT)) {
        result.timed_out = true;
      } else if (read == AVERROR_EXIT) {
        result.timed_out = true;  // 关窗/取消：保留已有前缀
      } else if (read != AVERROR_EOF) {
        read_failed = true;
        read_error = AvErrorText(read);
      }
      break;
    }
    if (packet->stream_index != selected) {
      av_packet_unref(packet);
      continue;
    }
    ++packets;
    std::int64_t pts = packet->pts;
    if (pts == AV_NOPTS_VALUE) {
      pts = packet->dts;
    }
    if (pts != AV_NOPTS_VALUE) {
      TempEntry entry;
      entry.entry.pts_us = av_rescale_q(pts, stream->time_base, AV_TIME_BASE_Q);
      entry.entry.keyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
      entry.span.pos = packet->pos;
      entry.span.size = packet->size;
      entry.span.keyframe = entry.entry.keyframe;
      entries.push_back(entry);
    }

    if (parser != nullptr) {
      std::uint8_t* output = nullptr;
      int output_size = 0;
      av_parser_parse2(parser, parser_context, &output, &output_size, packet->data,
                       packet->size, packet->pts, packet->dts, packet->pos);
      if (output_size > 0) {
        ++parsed_frames;
      }
      for (;;) {
        std::uint8_t* flushed = nullptr;
        int flushed_size = 0;
        const int consumed = av_parser_parse2(
            parser, parser_context, &flushed, &flushed_size, nullptr, 0,
            AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (flushed_size > 0) {
          ++parsed_frames;
        }
        if (consumed <= 0 && flushed_size == 0) {
          break;
        }
      }
    }
    av_packet_unref(packet);
  }

  av_packet_free(&packet);
  if (parser != nullptr) {
    av_parser_close(parser);
  }
  if (parser_context != nullptr) {
    avcodec_free_context(&parser_context);
  }

  result.parsed_frames = parsed_frames;
  result.complete = source->Published() >= source->Size() && !read_failed;
  if (entries.empty()) {
    result.error = read_failed
                       ? (read_error.empty() ? "读取缓存前缀失败" : read_error)
                       : "已发布前缀还没有视频帧";
    return finish();
  }

  std::sort(entries.begin(), entries.end(),
            [](const TempEntry& a, const TempEntry& b) {
              if (a.entry.pts_us != b.entry.pts_us) {
                return a.entry.pts_us < b.entry.pts_us;
              }
              return a.entry.keyframe && !b.entry.keyframe;
            });
  std::vector<TempEntry> unique;
  unique.reserve(entries.size());
  for (const TempEntry& entry : entries) {
    if (!unique.empty() && unique.back().entry.pts_us == entry.entry.pts_us) {
      unique.back().entry.keyframe =
          unique.back().entry.keyframe || entry.entry.keyframe;
      unique.back().span.keyframe =
          unique.back().entry.keyframe;
      continue;
    }
    unique.push_back(entry);
  }

  int stable = static_cast<int>(unique.size());
  if (!result.complete && tail_guard_frames > 0) {
    stable = std::max(0, static_cast<int>(unique.size()) - tail_guard_frames);
  }
  result.stable_frames = stable;
  if (stable <= 0) {
    result.error = result.complete ? "视频流没有可索引的帧"
                                   : "稳定索引前缀尚未形成";
    return finish();
  }

  std::vector<FrameIndexEntry> frame_entries;
  frame_entries.reserve(static_cast<std::size_t>(stable));
  result.spans.reserve(static_cast<std::size_t>(stable));
  for (int i = 0; i < stable; ++i) {
    frame_entries.push_back(unique[static_cast<std::size_t>(i)].entry);
    result.spans.push_back(unique[static_cast<std::size_t>(i)].span);
  }
  std::string index_error;
  if (!result.index.BuildFromEntries(std::move(frame_entries), &index_error)) {
    result.error = index_error;
    return finish();
  }
  if (parsed_frames > packets) {
    // 包内多帧会让 packet PTS 与显示帧号不能一一对应；不冒充精确。
    result.error = "包内多帧，部分索引不可靠";
    return finish();
  }
  result.ok = true;
  if (read_failed && !read_error.empty()) {
    result.error = read_error;
  }
  return finish();
}

}  // namespace lapian::media
