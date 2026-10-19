#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <cstdint>
#include <string>

#include "media/VideoDecoder.h"

namespace lapian::media {
class MediaSession;
}

// T1.5 视频播放时钟：QPC（QElapsedTimer）驱动，按帧 PTS 调度。
// 只做视频；音频主时钟在 T3 接入。非线程安全，只在 UI 线程使用。
class PlaybackController : public QObject {
  Q_OBJECT

 public:
  explicit PlaybackController(QObject* parent = nullptr);
  ~PlaybackController() override;

  void SetSession(lapian::media::MediaSession* session);
  // T2.4：seek 后同步播放位置；解码器已经停在目标帧之后。
  void SetPosition(std::int64_t frame, std::int64_t pts_us);
  void Play();
  void Pause();
  void Toggle();

  bool IsPlaying() const { return playing_; }
  int CurrentFrame() const { return current_frame_; }

 signals:
  void frameReady(const AVFrame* frame, int rotation, int frame_index);
  void stateChanged(bool playing);
  void reachedEnd();

 private slots:
  void Tick();

 private:
  double PtsToSeconds(std::int64_t pts) const;
  void Finish();

  lapian::media::MediaSession* session_ = nullptr;
  QTimer timer_;
  QElapsedTimer clock_;
  double start_media_seconds_ = 0.0;
  double current_pts_seconds_ = 0.0;
  int current_frame_ = 0;
  bool playing_ = false;
  bool end_ = false;
  bool has_pending_ = false;
  int pending_frame_ = 0;
  double pending_pts_seconds_ = 0.0;
  lapian::media::DecodedFrameInfo pending_info_;
};
