#include "app/PlaybackController.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

extern "C" {
#include <libavutil/frame.h>
}

#include "audio/AudioEngine.h"
#include "media/MediaSession.h"

namespace {

constexpr auto kAudioClockStartTimeout = std::chrono::milliseconds(300);
constexpr double kDropLateThresholdSeconds = 0.100;
// T7.2.1：B 已解码但音频时钟在 B 之前停住时，按“一帧的墙钟时长 + 余量”
// 兜底回 A；不能用一个固定短超时，否则 0.25x 会在 B 真正到点前提前跳走。
constexpr int kMaxLoopJumpFailures = 3;

}  // namespace

PlaybackController::PlaybackController(QObject* parent) : QObject(parent) {
  timer_.setInterval(5);
  timer_.setTimerType(Qt::PreciseTimer);
  connect(&timer_, &QTimer::timeout, this, &PlaybackController::Tick);
  timing_log_ = std::getenv("LAPIAN_TIMING_LOG") != nullptr;
  loop_log_ = std::getenv("LAPIAN_LOOP_LOG") != nullptr;
}

PlaybackController::~PlaybackController() = default;

void PlaybackController::SetSession(lapian::media::MediaSession* session) {
  Pause();
  session_ = session;
  current_frame_ = 0;
  last_decoded_frame_ = 0;
  current_pts_seconds_ = 0.0;
  start_media_seconds_ = 0.0;
  end_ = false;
  audio_clock_active_ = false;
  start_pending_ = false;
  frozen_ = false;
  buffering_ = false;
  loop_enabled_ = false;
  loop_in_ = 0;
  loop_out_ = 0;
  loop_out_reached_ = false;
  loop_out_wait_active_ = false;
  loop_jump_count_ = 0;
  loop_jump_failures_ = 0;
  has_pending_ = false;
  pending_frame_ = 0;
  pending_pts_seconds_ = 0.0;
  pending_info_ = lapian::media::DecodedFrameInfo{};
  if (session_ != nullptr && session_->IsOpen()) {
    current_pts_seconds_ = PtsToSeconds(session_->first_frame().pts);
    start_media_seconds_ = current_pts_seconds_;
  }
  clock_.Seek(current_pts_seconds_);
}

void PlaybackController::SetAudioEngine(lapian::audio::AudioEngine* engine) {
  audio_ = engine;
  audio_clock_active_ = false;
  start_pending_ = false;
  rate_pending_ = false;
}

double PlaybackController::PtsToSeconds(std::int64_t pts) const {
  if (session_ == nullptr || !session_->IsOpen()) {
    return 0.0;
  }
  const lapian::media::Rational& time_base = session_->info().video.time_base;
  if (!time_base.valid()) {
    return 0.0;
  }
  return static_cast<double>(pts) * static_cast<double>(time_base.num) /
         static_cast<double>(time_base.den);
}

void PlaybackController::SetPosition(std::int64_t frame, std::int64_t pts_us) {
  Pause();
  current_frame_ = static_cast<int>(frame);
  last_decoded_frame_ = current_frame_;
  current_pts_seconds_ = static_cast<double>(pts_us) / 1.0e6;
  start_media_seconds_ = current_pts_seconds_;
  end_ = false;
  audio_clock_active_ = false;
  start_pending_ = false;
  rate_pending_ = false;
  frozen_ = false;
  buffering_ = false;
  loop_out_reached_ = LoopActive() && frame >= loop_out_;
  loop_out_wait_active_ = false;
  loop_jump_failures_ = 0;
  has_pending_ = false;
  pending_frame_ = 0;
  pending_pts_seconds_ = 0.0;
  pending_info_ = lapian::media::DecodedFrameInfo{};
  clock_.Seek(current_pts_seconds_);
}

void PlaybackController::SetRate(double rate) {
  if (!std::isfinite(rate) || rate <= 0.0) {
    rate = 1.0;
  }
  rate_ = std::clamp(rate, 0.25, 2.0);
  clock_.SetRate(rate_);
  if (playing_) {
    rate_pending_ = (audio_ != nullptr) && !frozen_;
    audio_clock_active_ = false;
  }
}

void PlaybackController::SetLoopRange(bool enabled, std::int64_t in,
                                      std::int64_t out) {
  const bool freeze_now = enabled && in >= 0 && out >= in && in == out;
  loop_enabled_ = enabled;
  loop_in_ = in;
  loop_out_ = out;
  // 区间/开关变了，旧的 B 到达状态不能沿用。
  loop_out_reached_ = false;
  loop_out_wait_active_ = false;
  loop_jump_failures_ = 0;

  if (!playing_) {
    frozen_ = false;
    return;
  }
  if (freeze_now && !frozen_) {
    // 播放中把区间改成 A=B：先确认画面在 A，再进入定帧。
    if (current_frame_ != loop_in_ && !SeekToFrameInternal(loop_in_)) {
      return;
    }
    frozen_ = true;
    timer_.stop();
    clock_.Pause();
    start_pending_ = false;
    rate_pending_ = false;
    audio_clock_active_ = false;
    emit stateChanged(true);
    return;
  }
  if (!freeze_now && frozen_) {
    // 关闭循环/清除 A/B/改成非单帧区间：从当前帧继续正常播放（F4-A10）。
    frozen_ = false;
    start_media_seconds_ = current_pts_seconds_;
    clock_.Start(current_pts_seconds_, rate_);
    audio_clock_active_ = false;
    start_pending_ = (audio_ != nullptr);
    rate_pending_ = false;
    start_pending_since_ = std::chrono::steady_clock::now();
    end_ = false;
    timer_.start();
    emit stateChanged(true);
  }
}

void PlaybackController::SetDecodeGate(DecodeGate gate) {
  decode_gate_ = std::move(gate);
  if (!decode_gate_) {
    buffering_ = false;
  }
}

void PlaybackController::TryResumeFromBuffering() {
  if (!buffering_ || !playing_ || session_ == nullptr) {
    return;
  }
  if (decode_gate_) {
    std::string reason;
    if (!decode_gate_(last_decoded_frame_ + 1, &reason)) {
      return;
    }
  }
  buffering_ = false;
  start_media_seconds_ = current_pts_seconds_;
  clock_.Start(current_pts_seconds_, rate_);
  audio_clock_active_ = false;
  start_pending_ = (audio_ != nullptr);
  rate_pending_ = false;
  start_pending_since_ = std::chrono::steady_clock::now();
  end_ = false;
  emit bufferingChanged(false, static_cast<int>(current_frame_), QString());
  Tick();
}

void PlaybackController::Play() {
  if (session_ == nullptr || !session_->IsOpen() || playing_) {
    return;
  }
  buffering_ = false;

  if (LoopFreezeActive()) {
    // A=B：Space 只切换状态，不推进帧；当前不在 A 时先精确回到 A。
    if (current_frame_ != loop_in_ && !SeekToFrameInternal(loop_in_)) {
      return;
    }
    playing_ = true;
    frozen_ = true;
    end_ = false;
    start_media_seconds_ = current_pts_seconds_;
    clock_.Seek(current_pts_seconds_);
    audio_clock_active_ = false;
    start_pending_ = false;
    rate_pending_ = false;
    timer_.stop();
    emit stateChanged(true);
    return;
  }

  if (LoopActive() && current_frame_ > loop_out_) {
    // 从区间外起播：直接回到 A，避免播放到文件尾也不循环。
    if (!SeekToFrameInternal(loop_in_)) {
      return;
    }
  }
  if (end_ && !LoopActive()) {
    return;
  }
  // 从 B/末尾再起播时，B 已经“到达”，下一次 Tick 应直接回 A。
  loop_out_reached_ = LoopActive() && current_frame_ >= loop_out_;
  loop_out_wait_active_ = false;
  loop_jump_failures_ = 0;
  // T7.2：上一步进可能命中帧缓存，解码器没有停在当前帧之后；起播前补一次定位。
  std::string position_error;
  if (!session_->EnsureSequentialPosition(current_frame_, &position_error)) {
    std::fprintf(stderr, "T7.2 play_position_failed frame=%d error=%s\n", current_frame_,
                 position_error.c_str());
    std::fflush(stderr);
    return;
  }
  playing_ = true;
  frozen_ = false;
  start_media_seconds_ = current_pts_seconds_;
  clock_.Start(start_media_seconds_, rate_);
  audio_clock_active_ = false;
  start_pending_ = (audio_ != nullptr);
  rate_pending_ = false;
  start_pending_since_ = std::chrono::steady_clock::now();
  timer_.start();
  emit stateChanged(true);
  Tick();
}

void PlaybackController::Pause() {
  if (!playing_) {
    return;
  }
  playing_ = false;
  frozen_ = false;
  if (buffering_) {
    buffering_ = false;
    emit bufferingChanged(false, static_cast<int>(current_frame_), QString());
  }
  timer_.stop();
  clock_.Pause();
  start_pending_ = false;
  audio_clock_active_ = false;
  rate_pending_ = false;
  loop_out_wait_active_ = false;
  emit stateChanged(false);
}

void PlaybackController::Toggle() {
  if (playing_) {
    Pause();
  } else {
    Play();
  }
}

bool PlaybackController::LoopActive() const {
  return loop_enabled_ && loop_in_ >= 0 && loop_out_ >= loop_in_;
}

bool PlaybackController::LoopFreezeActive() const {
  return LoopActive() && loop_in_ == loop_out_;
}

bool PlaybackController::SeekToFrameInternal(std::int64_t frame) {
  if (session_ == nullptr || !session_->IsOpen()) {
    return false;
  }
  lapian::media::DecodedFrameInfo info;
  std::string error;
  if (!session_->SeekToFrame(frame, &info, &error,
                            lapian::media::FrameCacheMode::kNone)) {
    std::fprintf(stderr, "T4.2 seek_to_frame failed frame=%lld error=%s\n",
                 static_cast<long long>(frame), error.c_str());
    std::fflush(stderr);
    return false;
  }
  current_frame_ = static_cast<int>(frame);
  last_decoded_frame_ = static_cast<int>(frame);
  current_pts_seconds_ = PtsToSeconds(info.pts);
  start_media_seconds_ = current_pts_seconds_;
  has_pending_ = false;
  pending_frame_ = 0;
  pending_pts_seconds_ = 0.0;
  pending_info_ = lapian::media::DecodedFrameInfo{};
  end_ = false;
  loop_out_reached_ = LoopActive() && frame >= loop_out_;
  loop_out_wait_active_ = false;
  loop_jump_failures_ = 0;

  const int rotation = session_->info().rotation.counterclockwise_degrees;
  AVFrame* clone = av_frame_clone(info.av_frame);
  if (clone != nullptr) {
    // 先呈现 A，再让调用方重置时钟/音频，避免黑屏或停在 B 的旧画面。
    emit frameReady(clone, rotation, current_frame_);
    av_frame_free(&clone);
  }
  return true;
}

bool PlaybackController::JumpToLoopStart(std::int64_t trigger_frame) {
  const auto jump_start = std::chrono::steady_clock::now();
  const int pending_before = pending_frame_;
  if (!SeekToFrameInternal(loop_in_)) {
    ++loop_jump_failures_;
    if (loop_log_) {
      std::fprintf(stderr, "T4.2 loop_jump_failed from=%lld to=%lld failures=%d\n",
                   static_cast<long long>(trigger_frame),
                   static_cast<long long>(loop_in_), loop_jump_failures_);
      std::fflush(stderr);
    }
    return false;
  }
  const double seek_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - jump_start)
          .count();
  start_media_seconds_ = current_pts_seconds_;
  clock_.Start(current_pts_seconds_, rate_);
  audio_clock_active_ = false;
  start_pending_ = (audio_ != nullptr);
  rate_pending_ = false;
  start_pending_since_ = std::chrono::steady_clock::now();
  end_ = false;
  loop_out_reached_ = false;
  loop_out_wait_active_ = false;
  loop_jump_failures_ = 0;
  ++loop_jump_count_;

  if (loop_log_) {
    std::fprintf(stderr,
                 "T4.2 loop_jump from=%lld pending=%lld to=%lld seek_ms=%.2f\n",
                 static_cast<long long>(trigger_frame),
                 static_cast<long long>(pending_before),
                 static_cast<long long>(loop_in_), seek_ms);
    std::fflush(stderr);
  }
  emit loopJumped(static_cast<qint64>(current_frame_), current_pts_seconds_);
  return true;
}

double PlaybackController::TargetSeconds() {
  // 变速后音频引擎会重配重采样并重建时钟；这期间先走 QPC，
  // 音频时钟重新可用后切回，避免整段播放永久掉到 QPC。
  if (rate_pending_) {
    double target = 0.0;
    if (audio_ != nullptr && audio_->ClockSeconds(&target) && std::isfinite(target)) {
      rate_pending_ = false;
      audio_clock_active_ = true;
      return target;
    }
    if (audio_clock_active_) {
      audio_clock_active_ = false;
      clock_.Seek(current_pts_seconds_);
    }
    return clock_.NowSeconds();
  }
  if (audio_clock_active_) {
    double target = 0.0;
    if (audio_ != nullptr && audio_->ClockSeconds(&target) && std::isfinite(target)) {
      return target;
    }
    // 音频时钟中途失效：以当前呈现帧为锚点回退 QPC，避免画面冻结。
    audio_clock_active_ = false;
    clock_.Seek(current_pts_seconds_);
  }
  return clock_.NowSeconds();
}

void PlaybackController::Tick() {
  if (!playing_ || session_ == nullptr || frozen_ || buffering_) {
    return;
  }
  if (!has_pending_ && decode_gate_) {
    std::string reason;
    if (!decode_gate_(last_decoded_frame_ + 1, &reason)) {
      buffering_ = true;
      clock_.Pause();
      start_pending_ = false;
      audio_clock_active_ = false;
      rate_pending_ = false;
      emit bufferingChanged(
          true, static_cast<int>(current_frame_),
          reason.empty() ? QStringLiteral("缓冲中") : QString::fromStdString(reason));
      return;
    }
  }
  if (buffer_on_audio_stall_ && audio_clock_active_ && audio_ != nullptr) {
    double ignored = 0.0;
    if (!audio_->ClockSeconds(&ignored)) {
      buffering_ = true;
      clock_.Pause();
      start_pending_ = false;
      audio_clock_active_ = false;
      rate_pending_ = false;
      emit bufferingChanged(true, static_cast<int>(current_frame_),
                            QStringLiteral("等待下载"));
      return;
    }
  }
  if (!LoopActive()) {
    loop_out_reached_ = false;
    loop_out_wait_active_ = false;
  }

  if (start_pending_) {
    double ignored = 0.0;
    if (audio_ != nullptr && audio_->ClockSeconds(&ignored)) {
      start_pending_ = false;
      audio_clock_active_ = true;
    } else if (std::chrono::steady_clock::now() - start_pending_since_ >=
               kAudioClockStartTimeout) {
      // 设备打开慢/无 IAudioClock：回退 QPC，从当前帧继续。
      start_pending_ = false;
      audio_clock_active_ = false;
      clock_.Seek(current_pts_seconds_);
    } else {
      return;
    }
  }

  double target = TargetSeconds();
  if (!std::isfinite(target)) {
    audio_clock_active_ = false;
    clock_.Seek(current_pts_seconds_);
    target = clock_.NowSeconds();
  }
  const int rotation = session_->info().rotation.counterclockwise_degrees;

  // 每 tick 最多追 32 帧；只保留“最接近 target 且未过期”的一帧，
  // 落后 >100ms 的旧帧直接丢弃，避免把多帧过期画面连续塞给 GPU。
  AVFrame* best_frame = nullptr;
  int best_index = 0;
  double best_seconds = 0.0;
  auto emit_best = [&]() {
    if (best_frame == nullptr) {
      return;
    }
    current_frame_ = best_index;
    current_pts_seconds_ = best_seconds;
    // 直连信号：MainWindow 会在返回前把帧复制到 GPU，之后才允许下一次解码。
    emit frameReady(best_frame, rotation, current_frame_);
    if (timing_log_) {
      std::fprintf(stderr,
                   "T3.2 sync_ms=%.2f frame=%d target=%.4f clock=%s\n",
                   (current_pts_seconds_ - target) * 1000.0, current_frame_, target,
                   audio_clock_active_ ? "audio" : "qpc");
      std::fflush(stderr);
    }
    av_frame_free(&best_frame);
  };
  // B 已解码但 target 还没越过它时，先把 B 放进 best_frame，超时兜底时能呈现 B。
  auto present_pending_if_needed = [&]() {
    if (best_frame == nullptr && has_pending_ && pending_info_.av_frame != nullptr) {
      best_frame = av_frame_clone(pending_info_.av_frame);
      if (best_frame != nullptr) {
        best_index = pending_frame_;
        best_seconds = pending_pts_seconds_;
      }
    }
  };

  for (int guard = 0; guard < 32; ++guard) {
    if (!has_pending_) {
      lapian::media::DecodedFrameInfo info;
      std::string error;
      if (!session_->decoder().DecodeNextFrame(&info, &error)) {
        // 到文件尾：B 是末帧时没有 B+1，不能只依赖 current_frame_ 正好等于 B；
        // 只要已经解码到 B（或 B 之前最后一帧已呈现），且循环开启，就回 A。
        if (LoopActive() &&
            (loop_out_reached_ || current_frame_ >= loop_out_ ||
             last_decoded_frame_ >= loop_out_)) {
          emit_best();
          if (JumpToLoopStart(current_frame_)) {
            return;
          }
          if (loop_jump_failures_ >= kMaxLoopJumpFailures) {
            Finish();
          }
          return;
        }
        emit_best();
        end_ = true;
        Finish();
        return;
      }
      has_pending_ = true;
      pending_info_ = info;
      pending_frame_ = last_decoded_frame_ + 1;
      last_decoded_frame_ = pending_frame_;
      pending_pts_seconds_ = PtsToSeconds(info.pts);
      if (LoopActive() && pending_frame_ >= loop_out_) {
        loop_out_reached_ = true;
        if (!loop_out_wait_active_) {
          loop_out_wait_active_ = true;
          loop_out_wait_since_ = std::chrono::steady_clock::now();
        }
      } else if (pending_frame_ < loop_out_) {
        loop_out_wait_active_ = false;
      }
    }

    if (pending_pts_seconds_ > target + 0.0005) {
      // T7.2.1：B 已解码但音频时钟在 B 之前停住（音频先 EOF/设备时钟不推进）时，
      // 不能无限等 target 越过 B；超过“一帧墙钟 + 余量”就按 B 已到达回 A。
      const double media_frame_ms =
          pending_info_.duration_us > 0
              ? static_cast<double>(pending_info_.duration_us) / 1000.0
              : 41.7;
      const double hold_ms =
          std::max(100.0, media_frame_ms / std::max(0.25, rate_) * 1.25 + 30.0);
      if (LoopActive() && has_pending_ && pending_frame_ >= loop_out_ &&
          loop_out_wait_active_ &&
          std::chrono::steady_clock::now() - loop_out_wait_since_ >=
              std::chrono::milliseconds(static_cast<int>(hold_ms))) {
        present_pending_if_needed();
        if (loop_log_) {
          std::fprintf(stderr,
                       "T4.2 loop_end_hold frame=%d pts=%.4f target=%.4f "
                       "audio_clock=%d\n",
                       pending_frame_, pending_pts_seconds_, target,
                       audio_clock_active_ ? 1 : 0);
          std::fflush(stderr);
        }
        emit_best();
        if (JumpToLoopStart(pending_frame_)) {
          return;
        }
        if (loop_jump_failures_ >= kMaxLoopJumpFailures) {
          Finish();
        }
        return;
      }
      emit_best();
      return;
    }

    // B+1 已到呈现时间：丢弃它并回 A。B 已在上一 tick 呈现，B+1 不会被 emit。
    if (LoopActive() && pending_frame_ > loop_out_) {
      loop_out_reached_ = true;
      emit_best();
      if (JumpToLoopStart(current_frame_)) {
        return;
      }
      if (loop_jump_failures_ >= kMaxLoopJumpFailures) {
        Finish();
      }
      // 跳转失败时不要把 B+1 当普通帧呈现；下一 tick 重试。
      return;
    }

    const double late = target - pending_pts_seconds_;
    const int frame_index = pending_frame_;
    const double frame_seconds = pending_pts_seconds_;
    has_pending_ = false;
    if (LoopActive() && frame_index >= loop_out_) {
      loop_out_reached_ = true;
    }

    if (late > kDropLateThresholdSeconds) {
      continue;  // 丢弃过期帧，继续解到接近目标时间
    }

    // AVFrame 只保到下一次 DecodeNextFrame；这里 ref 一份，循环结束后再呈现。
    av_frame_free(&best_frame);
    best_frame = av_frame_clone(pending_info_.av_frame);
    if (best_frame != nullptr) {
      best_index = frame_index;
      best_seconds = frame_seconds;
    }
  }

  emit_best();
}

void PlaybackController::Finish() {
  playing_ = false;
  frozen_ = false;
  if (buffering_) {
    buffering_ = false;
    emit bufferingChanged(false, static_cast<int>(current_frame_), QString());
  }
  timer_.stop();
  clock_.Pause();
  start_pending_ = false;
  audio_clock_active_ = false;
  rate_pending_ = false;
  loop_out_reached_ = false;
  loop_out_wait_active_ = false;
  emit stateChanged(false);
  emit reachedEnd();
}
