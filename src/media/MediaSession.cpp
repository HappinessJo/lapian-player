#include "media/MediaSession.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace lapian::media {

namespace {

using Clock = std::chrono::steady_clock;

double ElapsedMs(Clock::time_point started) {
  return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

}  // namespace

MediaSession::MediaSession() = default;

MediaSession::~MediaSession() = default;

bool MediaSession::Open(const std::string& utf8_path, ID3D11Device* device, std::string* error,
                        bool build_index) {
  if (error == nullptr) {
    return false;
  }
  error->clear();

  // T7.1：分段计时只用于冷启动报告；失败路径也记录，便于区分探测/解码/索引。
  const auto open_started = Clock::now();
  auto timings = MediaOpenTimings{};

  const auto probe_started = Clock::now();
  last_attempt_ = ProbeMedia(utf8_path);
  timings.probe_ms = ElapsedMs(probe_started);
  if (!last_attempt_.ok) {
    *error = last_attempt_.error_detail.empty() ? "无法打开文件" : last_attempt_.error_detail;
    timings.total_ms = ElapsedMs(open_started);
    last_open_timings_ = timings;
    return false;
  }

  const auto decoder_started = Clock::now();
  auto decoder = std::make_unique<VideoDecoder>();
  if (!decoder->OpenWithD3D11Device(utf8_path, last_attempt_.info.video.stream_index, device,
                                    true, error)) {
    timings.decoder_open_ms = ElapsedMs(decoder_started);
    timings.total_ms = ElapsedMs(open_started);
    last_open_timings_ = timings;
    return false;
  }
  timings.decoder_open_ms = ElapsedMs(decoder_started);

  const auto first_frame_started = Clock::now();
  DecodedFrameInfo first_frame;
  if (!decoder->DecodeFirstFrame(&first_frame, error)) {
    timings.first_frame_ms = ElapsedMs(first_frame_started);
    timings.total_ms = ElapsedMs(open_started);
    last_open_timings_ = timings;
    return false;
  }
  timings.first_frame_ms = ElapsedMs(first_frame_started);

  // 索引失败不阻塞打开；步进/输入在索引为空时禁用。
  // T7.1：异步打开时这里跳过，首帧先呈现，索引由后台 worker 建好后 InstallIndex。
  if (build_index) {
    const auto index_started = Clock::now();
    FrameIndex index;
    std::string index_error;
    if (!index.BuildFromFile(utf8_path, last_attempt_.info.video.stream_index, &index_error)) {
      index.Reset();
    } else {
      timings.index_ok = true;
      timings.index_frames = index.size();
    }
    timings.index_ms = ElapsedMs(index_started);
    index_ = std::move(index);
  } else {
    // 复用同一个 MediaSession 再次打开时必须丢掉旧索引，避免把上一个文件的
    // 帧号/PTS 带到新会话。
    index_.Reset();
  }
  timings.total_ms = ElapsedMs(open_started);
  last_open_timings_ = timings;

  // 只有探测/解码都成功才替换旧会话。
  probe_ = last_attempt_;
  decoder_ = std::move(decoder);
  first_frame_ = first_frame;
  open_ = true;
  return true;
}

bool MediaSession::BuildIndexNow(std::string* error, const std::atomic<bool>* cancel) {
  if (!open_) {
    if (error != nullptr) {
      *error = "会话未打开";
    }
    return false;
  }
  FrameIndex index;
  const auto started = Clock::now();
  std::string build_error;
  if (!index.BuildFromFile(probe_.info.path_utf8, video_stream_index(), &build_error, cancel)) {
    if (error != nullptr) {
      *error = build_error;
    }
    return false;
  }
  const double build_ms = ElapsedMs(started);
  InstallIndex(std::move(index), build_ms);
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

void MediaSession::InstallIndex(FrameIndex index, double build_ms) {
  index_ = std::move(index);
  last_open_timings_.index_ok = HasIndex();
  last_open_timings_.index_frames = index_.size();
  last_open_timings_.index_ms = build_ms;
}

bool MediaSession::SeekToFrame(std::int64_t frame, DecodedFrameInfo* out, std::string* error) {
  if (error == nullptr || out == nullptr) {
    return false;
  }
  error->clear();
  if (!open_ || decoder_ == nullptr) {
    *error = "会话未打开";
    return false;
  }
  if (!index_.IsReady() || index_.empty()) {
    *error = "PTS 索引未就绪";
    return false;
  }
  if (frame < 0 || static_cast<std::size_t>(frame) >= index_.size()) {
    *error = "目标帧越界";
    return false;
  }

  auto decode_range = [&](std::int64_t start_frame) -> bool {
    for (std::int64_t index = start_frame; index <= frame; ++index) {
      DecodedFrameInfo info;
      if (!decoder_->DecodeNextFrame(&info, error)) {
        return false;
      }
      if (index == frame) {
        *out = info;
        return true;
      }
    }
    *error = "seek 解码未到目标帧";
    return false;
  };

  if (index_.IsReliable()) {
    const std::int64_t target_pts = index_.PtsOfFrame(frame);
    std::int64_t keyframe = index_.KeyframeAtOrBefore(target_pts);
    if (keyframe < 0) {
      keyframe = 0;
    }
    if (decoder_->SeekToPts(index_.PtsOfFrame(keyframe), error) &&
        decode_range(keyframe)) {
      return true;
    }
  }

  // 索引不可靠或关键帧路径失败：从头解码计数。
  if (!decoder_->SeekToPts(index_.PtsOfFrame(0), error)) {
    return false;
  }
  return decode_range(0);
}

void MediaSession::Close() {
  decoder_.reset();
  index_.Reset();
  probe_ = MediaProbeResult{};
  last_attempt_ = MediaProbeResult{};
  first_frame_ = DecodedFrameInfo{};
  open_ = false;
}

}  // namespace lapian::media
