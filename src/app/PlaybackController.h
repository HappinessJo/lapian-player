#pragma once

#include <QObject>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <string>

#include "core/MediaClock.h"
#include "media/VideoDecoder.h"

namespace lapian::media {
class MediaSession;
}

namespace lapian::audio {
class AudioEngine;
}

// T1.5/T3.2 播放时钟：有音频且 IAudioClock 可用时以音频设备时钟为主，
// 否则回退 QPC。按媒体时间调度帧；落后超过阈值时丢弃旧帧追赶。
class PlaybackController : public QObject {
  Q_OBJECT

 public:
  explicit PlaybackController(QObject* parent = nullptr);
  ~PlaybackController() override;

  void SetSession(lapian::media::MediaSession* session);
  // MainWindow 在音频引擎创建/销毁时同步；空指针表示无音频或已关闭。
  void SetAudioEngine(lapian::audio::AudioEngine* engine);
  // T2.4：seek 后同步播放位置；解码器已经停在目标帧之后。
  void SetPosition(std::int64_t frame, std::int64_t pts_us);
  void Play();
  void Pause();
  void Toggle();

  bool IsPlaying() const { return playing_; }
  int CurrentFrame() const { return current_frame_; }
  // 当前帧的媒体时间（秒）；T3.1 音频起播用它作为 seek 目标。
  double CurrentMediaSeconds() const { return current_pts_seconds_; }

 signals:
  void frameReady(const AVFrame* frame, int rotation, int frame_index);
  void stateChanged(bool playing);
  void reachedEnd();

 private slots:
  void Tick();

 private:
  double PtsToSeconds(std::int64_t pts) const;
  double TargetSeconds();
  void Finish();

  lapian::media::MediaSession* session_ = nullptr;
  lapian::audio::AudioEngine* audio_ = nullptr;
  QTimer timer_;
  lapian::core::MediaClock clock_;
  double start_media_seconds_ = 0.0;
  double current_pts_seconds_ = 0.0;
  int current_frame_ = 0;
  int last_decoded_frame_ = 0;
  bool playing_ = false;
  bool end_ = false;
  bool audio_clock_active_ = false;
  bool start_pending_ = false;
  std::chrono::steady_clock::time_point start_pending_since_;
  bool timing_log_ = false;
  bool has_pending_ = false;
  int pending_frame_ = 0;
  double pending_pts_seconds_ = 0.0;
  lapian::media::DecodedFrameInfo pending_info_;
};
