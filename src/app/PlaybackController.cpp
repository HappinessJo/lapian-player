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

}  // namespace

PlaybackController::PlaybackController(QObject* parent) : QObject(parent) {
  timer_.setInterval(5);
  timer_.setTimerType(Qt::PreciseTimer);
  connect(&timer_, &QTimer::timeout, this, &PlaybackController::Tick);
  timing_log_ = std::getenv("LAPIAN_TIMING_LOG") != nullptr;
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
    rate_pending_ = (audio_ != nullptr);
    audio_clock_active_ = false;
  }
}

void PlaybackController::Play() {
  if (session_ == nullptr || !session_->IsOpen() || playing_ || end_) {
    return;
  }
  playing_ = true;
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
  timer_.stop();
  clock_.Pause();
  start_pending_ = false;
  audio_clock_active_ = false;
  rate_pending_ = false;
  emit stateChanged(false);
}

void PlaybackController::Toggle() {
  if (playing_) {
    Pause();
  } else {
    Play();
  }
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
  if (!playing_ || session_ == nullptr) {
    return;
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

  for (int guard = 0; guard < 32; ++guard) {
    if (!has_pending_) {
      lapian::media::DecodedFrameInfo info;
      std::string error;
      if (!session_->decoder().DecodeNextFrame(&info, &error)) {
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
    }

    if (pending_pts_seconds_ > target + 0.0005) {
      emit_best();
      return;
    }

    const double late = target - pending_pts_seconds_;
    const int frame_index = pending_frame_;
    const double frame_seconds = pending_pts_seconds_;
    has_pending_ = false;

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
  timer_.stop();
  clock_.Pause();
  start_pending_ = false;
  audio_clock_active_ = false;
  rate_pending_ = false;
  emit stateChanged(false);
  emit reachedEnd();
}
