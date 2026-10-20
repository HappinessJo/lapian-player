#pragma once

#include <QMainWindow>
#include <QSize>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <vector>

#include "core/LoopRange.h"
#include "core/MarkStore.h"

struct AVFrame;

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QSlider;
class QTimer;
class GfxWidget;
class InfoBar;
class MarkPanel;
class PlaybackController;
class SnapshotService;
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
  void StepForwardForTest();
  // T2.6 自动化验证入口：与 Ctrl+G 输入框使用同一套解析。
  void GoToInputForTest(const QString& text);
  // T3.3 自动化验证入口：直接设置 0.25–2x 速度。
  void SetSpeedForTest(double rate);
  // T4.1 自动化验证入口：与 I/O/L/Shift+L 使用同一套逻辑与提示。
  void SetLoopInForTest();
  void SetLoopOutForTest();
  void ToggleLoopForTest();
  void ClearLoopForTest();
  QString LoopStateForTest() const;
  // T4.2 自动化验证入口：Space 语义与定帧/循环状态。
  void TogglePlaybackForTest();
  QString LoopPlaybackStateForTest() const;
  // T5.2 自动化验证入口：与 Ctrl+M / 侧栏点击 / 删除确认后的路径一致；
  // 删除入口跳过确认对话框，确认逻辑仍由 UI 路径覆盖。
  void AddMarkForTest();
  void DeleteMarkForTest(int index);
  void ActivateMarkForTest(int index);
  QString MarkStateForTest() const;
  int MarkCountForTest() const;
  std::int64_t MarkFrameAtForTest(int index) const;
  void ResetMarkPerfSamplesForTest();
  QString MarkStoragePathForTest() const;
  bool MarkUsingFallbackForTest() const;
  int MarkWarningCountForTest() const;
  QString MarkPerfSummaryForTest() const;
  // T5.3 自动化验证入口：选中第 index 条标记并在编辑框写入（不提交），
  // 与用户在侧栏点击行、键入文本走同一组控件；提交由 CommitNoteForTest 或关窗触发。
  void SetMarkNoteForTest(int index, const QString& label, const QString& note);
  void CommitNoteForTest();
  QString MarkTextForTest(int index) const;
  // T5.3：走真实删除确认对话框（自动化用 QTimer 点击 No/Yes）。
  void DeleteMarkWithConfirmForTest(int index);
  bool FocusNoteEditorForTest();
  // 发送与窗口关闭相同的 QCloseEvent（不隐藏窗口），用于验证关窗前的备注 flush。
  bool CloseForTest();
  void ResetNotePerfSamplesForTest();
  QString NotePerfSummaryForTest() const;
  // T5.4 自动化验证入口：注入图片目录回退根（须在 OpenFile/截图之前调用）。
  void SetSnapshotPicturesRootForTest(const QString& root);
  // T5.4 自动化验证入口：与 S 键走同一个 CaptureSnapshot；状态/路径/尺寸供断言。
  void CaptureSnapshotForTest();
  // 发送真实 S 键事件，验证 QAction 快捷键与焦点隔离，不只测内部函数。
  void PressSnapshotKeyForTest();
  // 等待截图队列清空并派发保存信号；自动化用它替代固定 sleep，避免 4K 编码竞态。
  bool DrainSnapshotsForTest();
  // 清除编辑框焦点，便于自动化继续走非文本路径。
  void ClearFocusForTest();
  QString SnapshotStateForTest() const;
  QStringList SnapshotSavedPathsForTest() const;
  QSize SnapshotDisplaySizeForTest() const;
  QString SnapshotLastPathForTest() const;
  bool SnapshotLastFallbackForTest() const;
  qint64 SnapshotLastCaptureFrameForTest() const;
  qint64 SnapshotLastSavedFrameForTest() const;
  QString SnapshotLastErrorForTest() const;
  QString SnapshotStatusForTest() const;
  void ResetSnapshotPerfSamplesForTest();
  QString SnapshotPerfSummaryForTest() const;

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

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
  void OnLoopJumped(qint64 frame, double media_seconds);
  void OnDeviceReady(const QString& summary);
  void OnDeviceError(const QString& message);
  void OnVolumeChanged(int value);
  void ToggleMute(bool muted);
  void PollAudioStatus();
  void PollWaveformStatus();
  void ToggleWaveform(bool visible);
  void SpeedDown();
  void SpeedUp();
  void SetLoopIn();
  void SetLoopOut();
  void ToggleLoop();
  void ClearLoop();
  void CaptureSnapshot();

 private:
  void ShowEmptyState();
  bool ApplyGotoText(const QString& text);
  bool IsTextInputFocused() const;
  bool CanEditLoop() const;
  void ApplyLoopIn();
  void ApplyLoopOut();
  void ApplyLoopToggle();
  void ApplyLoopClear();
  void SyncLoopUi();
  void ShowScrubTarget(std::int64_t frame);
  void StopPreview();
  void ShowOpenError(const QString& message);
  void ApplySpeed(double rate);
  void AddMarkAtCurrentFrame();
  void ApplyAddMark();
  void ApplyMarkTextEdited(const QString& id, const QString& label, const QString& note);
  void FlushMarkEditor();
  void ActivateMark(const QString& id, std::int64_t frame);
  void ApplyDeleteMark(const QString& id, bool confirm);
  bool SaveMarks(bool show_errors);
  void RefreshMarksUi(const QString& select_id = QString());
  void UpdateMarkStorageUi();
  bool FlushMarksBeforeSwitch();
  bool FlushMarksOnClose();
  void ShowMarkSaveFailure(const lapian::core::SaveResult& result);
  void ShowMarkWarnings();
  void CancelPendingMarkJump();
  void RememberDisplayedFrame(const AVFrame* frame, int rotation,
                              std::int64_t frame_index);

  GfxWidget* gfx_ = nullptr;
  TimelineWidget* timeline_ = nullptr;
  InfoBar* info_bar_ = nullptr;
  MarkPanel* mark_panel_ = nullptr;
  QLabel* status_label_ = nullptr;
  QLabel* audio_label_ = nullptr;
  QLabel* speed_label_ = nullptr;
  QLabel* loop_label_ = nullptr;
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
  QAction* loop_in_action_ = nullptr;
  QAction* loop_out_action_ = nullptr;
  QAction* loop_toggle_action_ = nullptr;
  QAction* loop_clear_action_ = nullptr;
  QAction* mark_action_ = nullptr;
  QAction* snapshot_action_ = nullptr;
  QString base_info_;
  QString audio_base_label_;
  std::unique_ptr<lapian::media::MediaSession> session_;
  std::unique_ptr<PlaybackController> playback_;
  std::unique_ptr<lapian::media::PreviewDecoder> preview_;
  std::unique_ptr<lapian::audio::AudioEngine> audio_;
  std::unique_ptr<lapian::audio::WaveformGenerator> waveform_;
  std::unique_ptr<lapian::core::MarkStore> marks_;
  std::unique_ptr<SnapshotService> snapshots_;
  lapian::core::LoopRange loop_;
  QTimer* preview_timer_ = nullptr;
  AVFrame* displayed_frame_ = nullptr;
  std::int64_t current_frame_ = 0;
  std::int64_t displayed_frame_index_ = -1;
  int displayed_rotation_ = 0;
  std::uint64_t audio_status_revision_ = 0;
  std::uint64_t waveform_status_revision_ = 0;
  std::uint64_t navigation_generation_ = 0;
  std::vector<double> mark_ui_samples_;
  std::vector<double> mark_exact_samples_;
  std::vector<double> note_save_samples_;
  std::vector<double> snapshot_capture_samples_;
  std::vector<double> snapshot_save_samples_;
  QStringList snapshot_saved_paths_;
  QString snapshot_last_path_;
  QString snapshot_last_error_;
  std::int64_t snapshot_last_capture_frame_ = -1;
  std::int64_t snapshot_last_saved_frame_ = -1;
  bool snapshot_last_fallback_ = false;
  bool snapshot_log_ = false;
  bool waveform_visible_ = true;
  int volume_ = 100;
  double rate_ = 1.0;
  bool muted_ = false;
  bool audio_error_shown_ = false;
  bool error_shown_ = false;
  bool marks_dirty_ = false;
};
