#include "media/FrameIndex.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
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
#include <libavutil/pixfmt.h>
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

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

std::int64_t Abs64(std::int64_t value) { return value < 0 ? -value : value; }

}  // namespace

void FrameIndex::Reset() {
  entries_.clear();
  keyframe_frames_.clear();
  ready_ = false;
  reliable_ = false;
  duplicate_pts_count_ = 0;
  missing_pts_count_ = 0;
  multi_frame_packets_ = 0;
  consecutive_validation_failures_ = 0;
  build_ms_ = 0.0;
  warning_.clear();
}

void FrameIndex::Normalize() {
  if (entries_.empty()) {
    return;
  }
  std::sort(entries_.begin(), entries_.end(),
            [](const FrameIndexEntry& a, const FrameIndexEntry& b) {
              if (a.pts_us != b.pts_us) {
                return a.pts_us < b.pts_us;
              }
              return a.keyframe && !b.keyframe;
            });

  std::vector<FrameIndexEntry> unique;
  unique.reserve(entries_.size());
  for (const FrameIndexEntry& entry : entries_) {
    if (!unique.empty() && unique.back().pts_us == entry.pts_us) {
      ++duplicate_pts_count_;
      unique.back().keyframe = unique.back().keyframe || entry.keyframe;
      continue;
    }
    unique.push_back(entry);
  }
  entries_ = std::move(unique);
  RebuildKeyframes();
}

void FrameIndex::RebuildKeyframes() {
  keyframe_frames_.clear();
  for (std::size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].keyframe) {
      keyframe_frames_.push_back(static_cast<std::int64_t>(i));
    }
  }
}

bool FrameIndex::BuildFromEntries(std::vector<FrameIndexEntry> entries, std::string* error) {
  Reset();
  if (entries.empty()) {
    if (error != nullptr) {
      *error = "索引为空";
    }
    return false;
  }
  entries_ = std::move(entries);
  Normalize();
  ready_ = true;
  reliable_ = true;
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool FrameIndex::BuildFromFile(const std::string& utf8_path, int video_stream_index,
                               std::string* error, const std::atomic<bool>* cancel) {
  Reset();
  const auto started = std::chrono::steady_clock::now();

  AVFormatContext* format = nullptr;
  int ret = avformat_open_input(&format, utf8_path.c_str(), nullptr, nullptr);
  if (ret < 0) {
    if (error != nullptr) {
      *error = "打开文件失败: " + AvErrorText(ret);
    }
    return false;
  }
  std::unique_ptr<AVFormatContext, void (*)(AVFormatContext*)> format_holder(
      format, [](AVFormatContext* context) {
        if (context != nullptr) {
          avformat_close_input(&context);
        }
      });

  ret = avformat_find_stream_info(format, nullptr);
  if (ret < 0) {
    if (error != nullptr) {
      *error = "读取流信息失败: " + AvErrorText(ret);
    }
    return false;
  }

  int selected = video_stream_index;
  if (selected < 0) {
    for (unsigned int i = 0; i < format->nb_streams; ++i) {
      if (format->streams[i]->codecpar != nullptr &&
          format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        selected = static_cast<int>(i);
        break;
      }
    }
  }
  if (selected < 0 || selected >= static_cast<int>(format->nb_streams)) {
    if (error != nullptr) {
      *error = "找不到视频流";
    }
    return false;
  }

  AVStream* stream = format->streams[selected];
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
    if (error != nullptr) {
      *error = "分配 packet 失败";
    }
    return false;
  }

  int packet_count = 0;
  int parsed_frames = 0;
  while (av_read_frame(format, packet) >= 0) {
    // T7.1：切换文件/关窗时取消后台索引，避免长时间占着文件。
    if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
      av_packet_unref(packet);
      if (error != nullptr) {
        *error = "索引已取消";
      }
      av_packet_free(&packet);
      if (parser != nullptr) {
        av_parser_close(parser);
      }
      if (parser_context != nullptr) {
        avcodec_free_context(&parser_context);
      }
      return false;
    }
    if (packet->stream_index != selected) {
      av_packet_unref(packet);
      continue;
    }
    ++packet_count;

    std::int64_t pts = packet->pts;
    if (pts == AV_NOPTS_VALUE) {
      pts = packet->dts;
    }
    if (pts == AV_NOPTS_VALUE) {
      ++missing_pts_count_;
    } else {
      FrameIndexEntry entry;
      entry.pts_us = av_rescale_q(pts, stream->time_base, AV_TIME_BASE_Q);
      entry.keyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
      entries_.push_back(entry);
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
        const int consumed = av_parser_parse2(parser, parser_context, &flushed, &flushed_size,
                                             nullptr, 0, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
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

  if (entries_.empty()) {
    if (error != nullptr) {
      *error = "视频流没有可索引的帧";
    }
    return false;
  }

  Normalize();
  ready_ = true;
  if (parsed_frames > packet_count) {
    multi_frame_packets_ = parsed_frames - packet_count;
  }
  reliable_ = missing_pts_count_ == 0 && multi_frame_packets_ == 0;
  if (missing_pts_count_ > 0) {
    warning_ = "部分视频包缺少 PTS/DTS，索引不可靠";
  } else if (multi_frame_packets_ > 0) {
    warning_ = "包内多帧，包级 PTS 索引不可靠";
  }
  build_ms_ = std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - started)
                  .count();
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

std::int64_t FrameIndex::PtsOfFrame(std::int64_t frame) const {
  if (frame < 0 || static_cast<std::size_t>(frame) >= entries_.size()) {
    return std::numeric_limits<std::int64_t>::min();
  }
  return entries_[static_cast<std::size_t>(frame)].pts_us;
}

std::int64_t FrameIndex::FrameOfPts(std::int64_t pts_us) const {
  if (entries_.empty()) {
    return -1;
  }
  const auto it = std::lower_bound(
      entries_.begin(), entries_.end(), pts_us,
      [](const FrameIndexEntry& entry, std::int64_t value) { return entry.pts_us < value; });
  if (it == entries_.begin()) {
    return 0;
  }
  if (it == entries_.end()) {
    return static_cast<std::int64_t>(entries_.size()) - 1;
  }
  const std::int64_t next = static_cast<std::int64_t>(it - entries_.begin());
  const std::int64_t previous = next - 1;
  const std::int64_t previous_distance = Abs64(pts_us - entries_[previous].pts_us);
  const std::int64_t next_distance = Abs64(entries_[next].pts_us - pts_us);
  return next_distance < previous_distance ? next : previous;
}

bool FrameIndex::FindExactPts(std::int64_t pts_us, std::int64_t* frame) const {
  if (entries_.empty()) {
    return false;
  }
  const auto it = std::lower_bound(
      entries_.begin(), entries_.end(), pts_us,
      [](const FrameIndexEntry& entry, std::int64_t value) { return entry.pts_us < value; });
  if (it == entries_.end() || it->pts_us != pts_us) {
    return false;
  }
  if (frame != nullptr) {
    *frame = static_cast<std::int64_t>(it - entries_.begin());
  }
  return true;
}

std::int64_t FrameIndex::KeyframeAtOrBefore(std::int64_t pts_us) const {
  if (keyframe_frames_.empty()) {
    return -1;
  }
  const auto it = std::upper_bound(
      keyframe_frames_.begin(), keyframe_frames_.end(), pts_us,
      [this](std::int64_t value, std::int64_t frame) {
        return value < entries_[static_cast<std::size_t>(frame)].pts_us;
      });
  if (it == keyframe_frames_.begin()) {
    return -1;
  }
  return *(it - 1);
}

const FrameIndexEntry* FrameIndex::Entry(std::int64_t frame) const {
  if (frame < 0 || static_cast<std::size_t>(frame) >= entries_.size()) {
    return nullptr;
  }
  return &entries_[static_cast<std::size_t>(frame)];
}

lapian::core::TotalFrameCount FrameIndex::Total() const {
  if (!ready_) {
    return lapian::core::TotalFrameCount::Unknown();
  }
  if (reliable_) {
    return lapian::core::TotalFrameCount::Exact(static_cast<std::int64_t>(entries_.size()));
  }
  return lapian::core::TotalFrameCount::Estimated(static_cast<std::int64_t>(entries_.size()));
}

bool FrameIndex::ValidateDecodedPts(std::int64_t frame, std::int64_t decoded_pts_us,
                                    std::int64_t frame_duration_us) {
  if (!ready_ || frame < 0 || static_cast<std::size_t>(frame) >= entries_.size()) {
    return false;
  }
  const std::int64_t tolerance = frame_duration_us > 0 ? frame_duration_us : 1;
  const std::int64_t deviation =
      Abs64(decoded_pts_us - entries_[static_cast<std::size_t>(frame)].pts_us);
  if (deviation > tolerance) {
    ++consecutive_validation_failures_;
    if (consecutive_validation_failures_ >= 3) {
      reliable_ = false;
      warning_ = "解码 PTS 与索引连续 3 次偏差，索引不可靠";
    }
    return false;
  }
  consecutive_validation_failures_ = 0;
  return true;
}

}  // namespace lapian::media
