#include "app/PlaybackController.h"

#include <utility>

#include "media/MediaSession.h"

PlaybackController::PlaybackController(QObject* parent) : QObject(parent) {
  timer_.setInterval(5);
  timer_.setTimerType(Qt::PreciseTimer);
  connect(&timer_, &QTimer::timeout, this, &PlaybackController::Tick);
}

PlaybackController::~PlaybackController() = default;

void PlaybackController::SetSession(lapian::media::MediaSession* session) {
  Pause();
  session_ = session;
  current_frame_ = 0;
  current_pts_seconds_ = 0.0;
  start_media_seconds_ = 0.0;
  end_ = false;
  has_pending_ = false;
  pending_frame_ = 0;
  pending_pts_seconds_ = 0.0;
  pending_info_ = lapian::media::DecodedFrameInfo{};
  if (session_ != nullptr && session_->IsOpen()) {
    current_pts_seconds_ = PtsToSeconds(session_->first_frame().pts);
    start_media_seconds_ = current_pts_seconds_;
  }
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

void PlaybackController::Play() {
  if (session_ == nullptr || !session_->IsOpen() || playing_ || end_) {
    return;
  }
  playing_ = true;
  start_media_seconds_ = current_pts_seconds_;
  clock_.restart();
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
  emit stateChanged(false);
}

void PlaybackController::Toggle() {
  if (playing_) {
    Pause();
  } else {
    Play();
  }
}

void PlaybackController::Tick() {
  if (!playing_ || session_ == nullptr) {
    return;
  }

  const double target_seconds =
      start_media_seconds_ + static_cast<double>(clock_.nsecsElapsed()) / 1.0e9;
  const int rotation = session_->info().rotation.counterclockwise_degrees;

  // 每 tick 最多追 8 帧，避免长时间阻塞 UI；正常 24/60fps 一帧就够。
  for (int guard = 0; guard < 8; ++guard) {
    if (!has_pending_) {
      lapian::media::DecodedFrameInfo info;
      std::string error;
      if (!session_->decoder().DecodeNextFrame(&info, &error)) {
        end_ = true;
        if (!has_pending_) {
          Finish();
        }
        return;
      }
      has_pending_ = true;
      pending_info_ = info;
      pending_frame_ = current_frame_ + 1;
      pending_pts_seconds_ = PtsToSeconds(info.pts);
    }

    if (pending_pts_seconds_ > target_seconds + 0.0005) {
      return;
    }

    current_frame_ = pending_frame_;
    current_pts_seconds_ = pending_pts_seconds_;
    has_pending_ = false;
    // 直连信号：MainWindow 会在返回前把帧复制到 GPU，之后才允许下一次解码。
    emit frameReady(pending_info_.av_frame, rotation, current_frame_);
  }
}

void PlaybackController::Finish() {
  playing_ = false;
  timer_.stop();
  emit stateChanged(false);
  emit reachedEnd();
}
