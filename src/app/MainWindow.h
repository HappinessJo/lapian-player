#pragma once

#include <QMainWindow>
#include <QString>

#include <cstdint>
#include <memory>

struct AVFrame;

class QAction;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QSlider;
class QTimer;
class GfxWidget;
class InfoBar;
class PlaybackController;
class TimelineWidget;

namespace lapian::media {
class MediaSession;
class PreviewDecoder;
}

namespace lapian::audio {
class AudioEngine;
class WaveformGenerator;
}

// T1.4 主窗口：打开入口（Ctrl+O / 拖拽 / 命令行）、空态、错误提示、首帧显示。
// 播放、时间轴、标记等仍由后续任务接入。
class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  // 打开本地文件；成功才替换当前会话，失败保留旧画面。
  void OpenFile(const QString& path);
  // 跳到 0 基帧号；成功时画面/信息栏同步。T2.4/T2.6 共用。
  void GoToFrame(std::int64_t frame);
  // T1.5 自动化验证入口，等价于按一次 Space；不改变默认“打开不播放”。
  void StartPlaybackForTest();
  // T2.6 自动化验证入口：与 Ctrl+G 输入框使用同一套解析。
  void GoToInputForTest(const QString& text);
  // T3.3 自动化验证入口：直接设置 0.25–2x 速度。
  void SetSpeedForTest(double rate);

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private slots:
  void OpenDialog();
  void ShowGotoDialog();
  void StepBackward();
  void StepForward();
  void TogglePlayback();
  void ZoomTimelineIn();
  void ZoomTimelineOut();
  void OnScrubStarted(qint64 frame);
  void OnScrubTargetChanged(qint64 frame);
  void OnScrubFinished(qint64 frame);
  void OnPreviewTick();
  void OnFrameReady(const AVFrame* frame, int rotation, int frame_index);
  void OnPlaybackStateChanged(bool playing);
  void OnPlaybackEnded();
  void OnDeviceReady(const QString& summary);
  void OnDeviceError(const QString& message);
  void OnVolumeChanged(int value);
  void ToggleMute(bool muted);
  void PollAudioStatus();
  void PollWaveformStatus();
  void ToggleWaveform(bool visible);
  void SpeedDown();
  void SpeedUp();

 private:
  void ShowEmptyState();
  bool ApplyGotoText(const QString& text);
  bool IsTextInputFocused() const;
  void ShowScrubTarget(std::int64_t frame);
  void StopPreview();
  void ShowOpenError(const QString& message);
  void SaveCurrentStatePlaceholder();
  void ApplySpeed(double rate);

  GfxWidget* gfx_ = nullptr;
  TimelineWidget* timeline_ = nullptr;
  InfoBar* info_bar_ = nullptr;
  QLabel* status_label_ = nullptr;
  QLabel* audio_label_ = nullptr;
  QLabel* speed_label_ = nullptr;
  QLabel* waveform_label_ = nullptr;
  QSlider* volume_slider_ = nullptr;
  QAction* mute_action_ = nullptr;
  QAction* slower_action_ = nullptr;
  QAction* faster_action_ = nullptr;
  QTimer* audio_status_timer_ = nullptr;
  QAction* open_action_ = nullptr;
  QAction* play_action_ = nullptr;
  QAction* prev_action_ = nullptr;
  QAction* next_action_ = nullptr;
  QAction* goto_action_ = nullptr;
  QAction* zoom_in_action_ = nullptr;
  QAction* zoom_out_action_ = nullptr;
  QAction* waveform_action_ = nullptr;
  QString base_info_;
  QString audio_base_label_;
  std::unique_ptr<lapian::media::MediaSession> session_;
  std::unique_ptr<PlaybackController> playback_;
  std::unique_ptr<lapian::media::PreviewDecoder> preview_;
  std::unique_ptr<lapian::audio::AudioEngine> audio_;
  std::unique_ptr<lapian::audio::WaveformGenerator> waveform_;
  QTimer* preview_timer_ = nullptr;
  std::int64_t current_frame_ = 0;
  std::uint64_t audio_status_revision_ = 0;
  std::uint64_t waveform_status_revision_ = 0;
  bool waveform_visible_ = true;
  int volume_ = 100;
  double rate_ = 1.0;
  bool muted_ = false;
  bool audio_error_shown_ = false;
  bool error_shown_ = false;
};
