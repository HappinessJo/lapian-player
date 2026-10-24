#include "media/OnlineIndexBuilder.h"

#include <QString>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
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

using Clock = std::chrono::steady_clock;

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

IncrementalOnlineIndexBuilder::IncrementalOnlineIndexBuilder(
    std::shared_ptr<lapian::online::GrowingSource> source,
    std::string part_path_utf8, int video_stream_index, int tail_guard_frames)
    : source_(std::move(source)),
      part_path_utf8_(std::move(part_path_utf8)),
      video_stream_index_(video_stream_index),
      tail_guard_frames_(std::max(0, tail_guard_frames)) {}

IncrementalOnlineIndexBuilder::~IncrementalOnlineIndexBuilder() { Close(); }

std::unique_ptr<IncrementalOnlineIndexBuilder>
IncrementalOnlineIndexBuilder::Create(
    std::shared_ptr<lapian::online::GrowingSource> source,
    const std::string& part_path_utf8, int video_stream_index,
    int tail_guard_frames, std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  if (!source || !source->IsValid()) {
    if (error != nullptr) {
      *error = "可增长数据源无效";
    }
    return nullptr;
  }
  if (part_path_utf8.empty()) {
    if (error != nullptr) {
      *error = "可增长数据源缺少缓存文件路径";
    }
    return nullptr;
  }
  return std::unique_ptr<IncrementalOnlineIndexBuilder>(
      new IncrementalOnlineIndexBuilder(std::move(source), part_path_utf8,
                                        video_stream_index, tail_guard_frames));
}

bool IncrementalOnlineIndexBuilder::SourceComplete() const {
  return source_ && source_->Published() >= source_->Size();
}

void IncrementalOnlineIndexBuilder::CloseFormat() {
  if (format_ && format_->IsOpen()) {
    if (lapian::online::GrowingCursor* cursor = format_->cursor()) {
      const lapian::online::GrowingCursorStats stats = cursor->Stats();
      stats_.read_bytes += stats.read_bytes - last_cursor_read_bytes_;
      last_cursor_read_bytes_ = 0;
    }
  }
  if (parser_ != nullptr) {
    av_parser_close(parser_);
    parser_ = nullptr;
  }
  if (parser_context_ != nullptr) {
    avcodec_free_context(&parser_context_);
  }
  if (packet_ != nullptr) {
    av_packet_free(&packet_);
  }
  if (format_) {
    format_->Close();
    format_.reset();
  }
  stream_ = nullptr;
  selected_stream_ = -1;
  opened_ = false;
}

void IncrementalOnlineIndexBuilder::Close() {
  CloseFormat();
  skipping_ = false;
  needs_reopen_ = false;
}

std::int64_t IncrementalOnlineIndexBuilder::FindKeyframePtsAtOrBefore(
    std::int64_t pts_us) const {
  std::int64_t best = std::numeric_limits<std::int64_t>::min();
  for (const FrameIndexEntry& entry : entries_) {
    if (entry.keyframe && entry.pts_us <= pts_us && entry.pts_us > best) {
      best = entry.pts_us;
    }
  }
  return best;
}

bool IncrementalOnlineIndexBuilder::SeekToResume() {
  if (!format_ || !format_->IsOpen() || stream_ == nullptr ||
      format_->context() == nullptr) {
    return false;
  }
  AVFormatContext* context = format_->context();
  std::int64_t target_us = last_pts_us_;
  const std::int64_t keyframe_us = FindKeyframePtsAtOrBefore(last_pts_us_);
  if (keyframe_us != std::numeric_limits<std::int64_t>::min()) {
    target_us = keyframe_us;
  }
  const AVRational time_base = stream_->time_base;
  const std::int64_t target_ts =
      av_rescale_q(target_us, AV_TIME_BASE_Q, time_base);
  int ret = avformat_seek_file(
      context, selected_stream_, std::numeric_limits<std::int64_t>::min(),
      target_ts, std::numeric_limits<std::int64_t>::max(), 0);
  if (ret < 0) {
    ret = av_seek_frame(context, selected_stream_, target_ts,
                        AVSEEK_FLAG_BACKWARD);
  }
  if (ret < 0) {
    last_error_ = "续读 seek 失败：" + AvErrorText(ret);
    return false;
  }
  skipping_ = true;
  return true;
}

bool IncrementalOnlineIndexBuilder::EnsureOpen() {
  if (opened_ && format_ && format_->IsOpen()) {
    return true;
  }
  CloseFormat();
  last_error_.clear();

  auto format = std::make_unique<lapian::online::GrowingFormat>();
  QString format_error;
  // 100ms 前沿超时：有数据就继续；读到发布前沿就返回，由下一次 Step 续读。
  if (!format->Open(source_, QString::fromUtf8(part_path_utf8_.c_str()),
                    &format_error, 100)) {
    last_error_ = format_error.isEmpty()
                      ? "打开可增长媒体失败"
                      : format_error.toUtf8().constData();
    return false;
  }
  format_ = std::move(format);
  format_->cursor()->SetWaitTimeoutMs(100);

  AVFormatContext* context = format_->context();
  int selected = video_stream_index_;
  if (selected < 0 || selected >= static_cast<int>(context->nb_streams) ||
      context->streams[selected]->codecpar == nullptr ||
      context->streams[selected]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
    selected = -1;
    for (unsigned int i = 0; i < context->nb_streams; ++i) {
      const AVCodecParameters* parameters = context->streams[i]->codecpar;
      if (parameters != nullptr && parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
        selected = static_cast<int>(i);
        break;
      }
    }
  }
  if (selected < 0) {
    last_error_ = "找不到视频流";
    CloseFormat();
    return false;
  }
  selected_stream_ = selected;
  stream_ = context->streams[selected];
  packet_ = av_packet_alloc();
  if (packet_ == nullptr) {
    last_error_ = "分配 packet 失败";
    CloseFormat();
    return false;
  }

  parser_ = av_parser_init(stream_->codecpar->codec_id);
  const AVCodec* parser_codec = avcodec_find_decoder(stream_->codecpar->codec_id);
  parser_context_ =
      parser_codec != nullptr ? avcodec_alloc_context3(parser_codec) : nullptr;
  if (parser_context_ != nullptr) {
    avcodec_parameters_to_context(parser_context_, stream_->codecpar);
  }
  if (parser_ != nullptr && parser_context_ == nullptr) {
    av_parser_close(parser_);
    parser_ = nullptr;
  }

  ++stats_.opens;
  opened_ = true;

  // 重开前把“最后成功解析到的字节末尾”作为续点；否则从 keyframe 读回来
  // 的已解析包会被重复追加。
  if (last_pos_end_ > resume_pos_) {
    resume_pos_ = last_pos_end_;
  }
  if (resume_pos_ > 0 || last_pts_us_ > 0) {
    ++stats_.reopens;
    if (!SeekToResume()) {
      // 极端容器不支持 seek：退化为从头读并跳过已解析区（仍只计算一次前缀）。
      ++stats_.resume_from_start;
      const int ret = avformat_seek_file(
          context, selected_stream_, 0, 0, 0, 0);
      if (ret < 0) {
        last_error_ = "续读 seek 失败且无法回到起点：" + AvErrorText(ret);
        CloseFormat();
        return false;
      }
      skipping_ = true;
    }
  }
  needs_reopen_ = false;
  return true;
}

OnlineIndexStepInfo IncrementalOnlineIndexBuilder::Step(
    const std::atomic<bool>* cancel, int max_ms, int max_packets) {
  OnlineIndexStepInfo info;
  const auto started = Clock::now();
  if (!source_ || !source_->IsValid()) {
    info.ok = false;
    info.error = "可增长数据源无效";
    return info;
  }
  if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
    info.ok = false;
    info.cancelled = true;
    info.error = "索引已取消";
    return info;
  }
  if (source_->IsCancelled()) {
    info.ok = false;
    info.cancelled = true;
    info.error = "索引已取消";
    return info;
  }
  if (source_->HasFailed()) {
    info.ok = false;
    info.error = "下载失败，索引停止";
    return info;
  }
  if (complete_) {
    info.complete = true;
    info.total_frames = static_cast<int>(entries_.size());
    info.stable_frames = last_stable_;
    FinishStep(&info, started);
    return info;
  }
  if (!EnsureOpen()) {
    // 未下载到能打开 moov/init 时属于可重试；完整文件仍打不开才是硬错误。
    const bool fatal = SourceComplete();
    info.ok = !fatal;
    info.at_frontier = !fatal;
    info.error = last_error_;
    if (fatal) {
      failed_ = true;
    }
    FinishStep(&info, started);
    return info;
  }

  AVFormatContext* context = format_->context();
  int packets_this_step = 0;
  int appended = 0;
  last_frontier_ = false;
  max_packets = std::max(1, max_packets);
  max_ms = std::max(1, max_ms);

  while (packets_this_step < max_packets) {
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - started)
            .count();
    if (elapsed_ms >= static_cast<double>(max_ms)) {
      info.budget_hit = true;
      break;
    }
    if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
      info.ok = false;
      info.cancelled = true;
      info.error = "索引已取消";
      CloseFormat();
      break;
    }

    // 只在已发布前缀内读；接近前沿先停，避免把 demux 的 sample 状态打脏。
    if (frontier_guard_bytes_ > 0 && !SourceComplete()) {
      const std::int64_t published = source_->Published();
      const std::int64_t read_pos = format_->cursor()->Pos();
      if (published - read_pos < frontier_guard_bytes_) {
        info.at_frontier = true;
        last_frontier_ = true;
        break;
      }
    }

    const int read = av_read_frame(context, packet_);
    if (read < 0) {
      if (read == AVERROR_EOF) {
        eof_ = true;
        info.complete = SourceComplete();
        break;
      }
      if (read == AVERROR(ETIMEDOUT)) {
        info.at_frontier = true;
        last_frontier_ = true;
        needs_reopen_ = true;
        break;
      }
      if (read == AVERROR_EXIT) {
        if (source_->IsCancelled()) {
          info.ok = false;
          info.cancelled = true;
          info.error = "索引已取消";
        } else {
          info.at_frontier = true;
          last_frontier_ = true;
          needs_reopen_ = true;
        }
        break;
      }
      info.ok = false;
      info.error = AvErrorText(read);
      needs_reopen_ = true;
      failed_ = true;
      break;
    }
    if (packet_->stream_index != selected_stream_) {
      av_packet_unref(packet_);
      continue;
    }
    ++packets_this_step;
    ++packet_count_;
    ++stats_.packets;

    std::int64_t pts = packet_->pts;
    if (pts == AV_NOPTS_VALUE) {
      pts = packet_->dts;
    }

    bool skip_packet = false;
    if (skipping_) {
      if (packet_->pos >= 0) {
        if (packet_->pos < resume_pos_) {
          skip_packet = true;
        } else {
          skipping_ = false;
        }
      } else {
        // 没有文件偏移的容器：按 PTS 跳过已解析区；重复 PTS 后面会去重。
        const std::int64_t pts_us =
            pts == AV_NOPTS_VALUE
                ? std::numeric_limits<std::int64_t>::min()
                : av_rescale_q(pts, stream_->time_base, AV_TIME_BASE_Q);
        if (pts_us != std::numeric_limits<std::int64_t>::min() &&
            pts_us <= last_pts_us_) {
          skip_packet = true;
        } else {
          skipping_ = false;
        }
      }
    }
    if (skip_packet) {
      ++stats_.resume_skips;
      av_packet_unref(packet_);
      continue;
    }

    if (pts != AV_NOPTS_VALUE) {
      TempEntry temp;
      temp.entry.pts_us = av_rescale_q(pts, stream_->time_base, AV_TIME_BASE_Q);
      temp.entry.keyframe = (packet_->flags & AV_PKT_FLAG_KEY) != 0;
      temp.span.pos = packet_->pos;
      temp.span.size = packet_->size;
      temp.span.keyframe = temp.entry.keyframe;
      entries_.push_back(temp.entry);
      spans_.push_back(temp.span);
      last_pts_us_ = std::max(last_pts_us_, temp.entry.pts_us);
      if (packet_->pos >= 0) {
        last_pos_end_ =
            std::max(last_pos_end_, packet_->pos + packet_->size);
      }
      ++appended;
    }

    if (parser_ != nullptr) {
      std::uint8_t* output = nullptr;
      int output_size = 0;
      av_parser_parse2(parser_, parser_context_, &output, &output_size,
                       packet_->data, packet_->size, packet_->pts, packet_->dts,
                       packet_->pos);
      if (output_size > 0) {
        ++parsed_frames_;
      }
      for (;;) {
        std::uint8_t* flushed = nullptr;
        int flushed_size = 0;
        const int consumed = av_parser_parse2(
            parser_, parser_context_, &flushed, &flushed_size, nullptr, 0,
            AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (flushed_size > 0) {
          ++parsed_frames_;
        }
        if (consumed <= 0 && flushed_size == 0) {
          break;
        }
      }
    }
    av_packet_unref(packet_);
  }

  if (eof_) {
    if (SourceComplete()) {
      complete_ = true;
      info.complete = true;
      info.at_frontier = false;
    } else {
      // 理论不可达（EOF 只在 pos>=Size 时出现）；保守重开等最终发布。
      needs_reopen_ = true;
    }
  }
  if (needs_reopen_) {
    CloseFormat();
  }
  info.appended = appended > 0;
  info.appended_frames = appended;
  info.packets_read = packets_this_step;
  info.total_frames = static_cast<int>(entries_.size());
  info.stable_frames = last_stable_;
  FinishStep(&info, started);
  return info;
}

void IncrementalOnlineIndexBuilder::FinishStep(
    OnlineIndexStepInfo* info,
    const std::chrono::steady_clock::time_point& started) {
  info->step_ms =
      std::chrono::duration<double, std::milli>(Clock::now() - started).count();
  ++stats_.steps;
  stats_.total_step_ms += info->step_ms;
  stats_.max_step_ms = std::max(stats_.max_step_ms, info->step_ms);
  stats_.parsed_frames = parsed_frames_;
  if (format_ && format_->IsOpen()) {
    if (lapian::online::GrowingCursor* cursor = format_->cursor()) {
      const lapian::online::GrowingCursorStats cursor_stats = cursor->Stats();
      stats_.read_bytes +=
          cursor_stats.read_bytes - last_cursor_read_bytes_;
      last_cursor_read_bytes_ = cursor_stats.read_bytes;
    }
  }
}

bool IncrementalOnlineIndexBuilder::Snapshot(OnlineIndexResult* out) {
  if (out == nullptr) {
    return false;
  }
  *out = OnlineIndexResult{};
  out->parsed_frames = parsed_frames_;
  out->complete = complete_;
  out->timed_out = last_frontier_;
  out->build_ms = stats_.total_step_ms;
  if (entries_.empty()) {
    out->error = complete_ ? "视频流没有可索引的帧"
                           : "已发布前缀还没有视频帧";
    return false;
  }

  std::vector<TempEntry> temp;
  temp.reserve(entries_.size());
  for (std::size_t i = 0; i < entries_.size(); ++i) {
    TempEntry item;
    item.entry = entries_[i];
    item.span = spans_[i];
    temp.push_back(item);
  }
  std::sort(temp.begin(), temp.end(),
            [](const TempEntry& a, const TempEntry& b) {
              if (a.entry.pts_us != b.entry.pts_us) {
                return a.entry.pts_us < b.entry.pts_us;
              }
              return a.entry.keyframe && !b.entry.keyframe;
            });
  std::vector<TempEntry> unique;
  unique.reserve(temp.size());
  for (const TempEntry& item : temp) {
    if (!unique.empty() && unique.back().entry.pts_us == item.entry.pts_us) {
      unique.back().entry.keyframe =
          unique.back().entry.keyframe || item.entry.keyframe;
      unique.back().span.keyframe = unique.back().entry.keyframe;
      continue;
    }
    unique.push_back(item);
  }

  int stable = static_cast<int>(unique.size());
  if (!complete_ && tail_guard_frames_ > 0) {
    stable = std::max(0, stable - tail_guard_frames_);
  }
  // 单调：缓存只增长，已暴露前缀不回收。
  stable = std::max(stable, std::min(last_stable_,
                                     static_cast<int>(unique.size())));
  if (stable <= 0) {
    out->error = complete_ ? "视频流没有可索引的帧"
                           : "稳定索引前缀尚未形成";
    return false;
  }
  if (parsed_frames_ > packet_count_) {
    // 包内多帧会让 packet PTS 与显示帧号不能一一对应；不冒充精确。
    out->error = "包内多帧，部分索引不可靠";
    return false;
  }

  std::vector<FrameIndexEntry> frame_entries;
  frame_entries.reserve(static_cast<std::size_t>(stable));
  std::vector<OnlineFrameSpan> frame_spans;
  frame_spans.reserve(static_cast<std::size_t>(stable));
  for (int i = 0; i < stable; ++i) {
    frame_entries.push_back(unique[static_cast<std::size_t>(i)].entry);
    frame_spans.push_back(unique[static_cast<std::size_t>(i)].span);
  }
  std::string index_error;
  if (!out->index.BuildFromEntries(std::move(frame_entries), &index_error)) {
    out->error = index_error;
    return false;
  }
  if (out->index.size() != frame_spans.size()) {
    out->error = "索引与 packet span 数量不一致";
    return false;
  }
  out->spans = std::move(frame_spans);
  out->stable_frames = stable;
  out->ok = true;
  last_stable_ = stable;
  return true;
}

OnlineIndexResult BuildOnlineIndex(
    const std::shared_ptr<lapian::online::GrowingSource>& source,
    const std::string& part_path_utf8, int video_stream_index,
    int tail_guard_frames, const std::atomic<bool>* cancel) {
  OnlineIndexResult result;
  std::string create_error;
  auto builder = IncrementalOnlineIndexBuilder::Create(
      source, part_path_utf8, video_stream_index, tail_guard_frames,
      &create_error);
  if (!builder) {
    result.error = create_error;
    return result;
  }
  builder->SetFrontierGuardBytes(0);

  bool timed_out = false;
  while (true) {
    const OnlineIndexStepInfo step =
        builder->Step(cancel, 10000, std::numeric_limits<int>::max());
    if (step.cancelled) {
      result.error = "索引已取消";
      result.timed_out = true;
      return result;
    }
    if (!step.ok) {
      result.error = step.error;
      result.complete = builder->complete();
      result.timed_out = step.at_frontier;
      return result;
    }
    timed_out = timed_out || step.at_frontier;
    if (step.complete || step.at_frontier) {
      break;
    }
    if (!step.appended && !step.budget_hit) {
      break;
    }
  }

  if (!builder->Snapshot(&result)) {
    result.complete = builder->complete();
    result.timed_out = timed_out;
    return result;
  }
  result.timed_out = timed_out || result.timed_out;
  result.complete = builder->complete();
  return result;
}

}  // namespace lapian::media
