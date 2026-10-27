#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <functional>
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
  // T3.3：0.25–2x；不改变当前帧/时间码，只改变媒体时间推进速度。
  void SetRate(double rate);
  // T4.2 A/B：enabled 只在 A/B 完整时为 true；A=B 时进入定帧语义。
  void SetLoopRange(bool enabled, std::int64_t in, std::int64_t out);
  // T8.6 在线缓冲闸门：返回 false 时暂停推进（画面保持），等 UI 轮询再次调用
  // TryResumeFromBuffering；不把未缓存读塞进 UI 线程。
  using DecodeGate = std::function<bool(std::int64_t next_frame, std::string* reason)>;
  void SetDecodeGate(DecodeGate gate);
  // L1：循环跳转预检。返回 false 时不做同步 seek，直接关循环 + 提示（不 Finish、
  // 不 reachedEnd），把“在线 A 未缓存”的长等待降级成可见提示。
  using LoopSeekGate = std::function<bool(std::int64_t frame)>;
  void SetLoopSeekGate(LoopSeekGate gate);
  // L1 test-only：注入 n 次循环跳转失败，验证“只关循环、不废播放”。
  void SetFailLoopSeeksForTest(int n) { fail_loop_seeks_for_test_ = n; }
  int LoopDisabledCountForTest() const { return loop_disabled_count_; }
  // T8.6：在线播放时，音频时钟失速（数据前沿阻塞）也作为缓冲耗尽处理。
  void SetBufferOnAudioStall(bool enabled) { buffer_on_audio_stall_ = enabled; }
  void TryResumeFromBuffering();
  bool IsBuffering() const { return buffering_; }
  void Play();
  void Pause();
  void Toggle();

  bool IsPlaying() const { return playing_; }
  int CurrentFrame() const { return current_frame_; }
  // T7.2.1：B→A 成功跳转次数；用于“B 在末帧”回归，确认不是停在 B。
  int LoopJumpCountForTest() const { return loop_jump_count_; }
  double rate() const { return rate_; }
  // A=B 且循环开启时为 true：时间不推进、音频不输出。
  bool frozen() const { return frozen_; }
  // 当前帧的媒体时间（秒）；T3.1 音频起播用它作为 seek 目标。
  double CurrentMediaSeconds() const { return current_pts_seconds_; }

 signals:
  void frameReady(const AVFrame* frame, int rotation, int frame_index);
  void stateChanged(bool playing);
  void reachedEnd();
  // B→A 跳转完成；MainWindow 用它把音频重新 seek 到 A。
  void loopJumped(qint64 frame, double media_seconds);
  // T8.6：缓冲耗尽（true）/恢复（false）；MainWindow 负责提示与暂停音频。
  void bufferingChanged(bool buffering, int frame, const QString& reason);
  // L1：循环跳转失败/预检拦截时只关循环、保持播放；reason 是给用户看的提示。
  void loopDisabled(const QString& reason);

 private slots:
  void Tick();

 private:
  double PtsToSeconds(std::int64_t pts) const;
  double TargetSeconds();
  void Finish();
  bool LoopActive() const;
  bool LoopFreezeActive() const;
  // seek 到指定帧并立即呈现；用于循环跳转/定帧回到 A，不改变 playing_。
  bool SeekToFrameInternal(std::int64_t frame);
  bool JumpToLoopStart(std::int64_t trigger_frame);
  // L1：循环跳转失败达到上限/预检拦截时只关循环——不 Finish()、不发 reachedEnd，
  // 保持 playing_ 与时钟，从当前（B）位置继续播。
  void DisableLoopAfterFailures();
  void DisableLoop(const QString& reason, const char* diagnostic,
                   std::int64_t trigger_frame);

  lapian::media::MediaSession* session_ = nullptr;
  lapian::audio::AudioEngine* audio_ = nullptr;
  QTimer timer_;
  lapian::core::MediaClock clock_;
  double start_media_seconds_ = 0.0;
  double current_pts_seconds_ = 0.0;
  double rate_ = 1.0;
  int current_frame_ = 0;
  int last_decoded_frame_ = 0;
  bool playing_ = false;
  bool end_ = false;
  bool audio_clock_active_ = false;
  bool start_pending_ = false;
  bool rate_pending_ = false;
  bool frozen_ = false;
  bool buffering_ = false;
  bool buffer_on_audio_stall_ = false;
  DecodeGate decode_gate_;
  LoopSeekGate loop_seek_gate_;
  int fail_loop_seeks_for_test_ = 0;
  int loop_disabled_count_ = 0;
  bool loop_enabled_ = false;
  std::int64_t loop_in_ = 0;
  std::int64_t loop_out_ = 0;
  // T7.2.1：B 在末帧时没有 B+1 可等；显式记录“B 已解到”，并用墙钟兜底
  // 处理音频时钟在 B 之前停住（音频 EOF/设备时钟不推进）。
  bool loop_out_reached_ = false;
  bool loop_out_wait_active_ = false;
  std::chrono::steady_clock::time_point loop_out_wait_since_;
  int loop_jump_count_ = 0;
  int loop_jump_failures_ = 0;
  std::chrono::steady_clock::time_point start_pending_since_;
  bool timing_log_ = false;
  bool loop_log_ = false;
  bool has_pending_ = false;
  int pending_frame_ = 0;
  double pending_pts_seconds_ = 0.0;
  lapian::media::DecodedFrameInfo pending_info_;
};
