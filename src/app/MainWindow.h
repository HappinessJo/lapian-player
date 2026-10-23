#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "core/AppSettings.h"
#include "core/FrameMath.h"
#include "core/LoopRange.h"
#include "core/MarkStore.h"
#include "media/OnlineIndexBuilder.h"

struct AVFrame;

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QKeyEvent;
class QLabel;
class QResizeEvent;
class QSlider;
class QTimer;
class GfxWidget;
class InfoBar;
class MarkPanel;
class OnlineResolveDialog;
class PlaybackController;
class SnapshotService;
class TimelineWidget;

namespace lapian::core {
struct AppSettings;
class SettingsStore;
}  // namespace lapian::core

namespace lapian::media {
enum class FrameCacheMode;
class MediaSession;
class PreviewDecoder;
}

namespace lapian::audio {
class AudioEngine;
class WaveformGenerator;
}

namespace lapian::online {
class CacheStore;
class IDownloadTransport;
struct PlayUrlData;
}  // namespace lapian::online

namespace lapian::app {
class OnlineSession;
struct OnlineProgress;
struct OnlineReadyInfo;
struct OnlineRequest;
enum class OnlineState;
}

// T1.4 主窗口：打开入口（Ctrl+O / 拖拽 / 命令行）、空态、错误提示、首帧显示。
// 播放、时间轴、标记等仍由后续任务接入。
class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr,
                      const QString& settings_root = QString());
  ~MainWindow() override;

  // T8.1 在线解析：Ctrl+U 打开的模型对话框；测试入口走同一路径。
  void OpenOnlineDialogForTest();
  OnlineResolveDialog* OnlineDialogForTest() const { return online_dialog_; }

  // 打开本地文件；成功才替换当前会话，失败保留旧画面。
  void OpenFile(const QString& path);
  // T8.6 在线会话：产品确认路径与自动化共用。返回 false 时 last_online_error 可读。
  bool StartOnlineSession(const lapian::app::OnlineRequest& request);
  QString OnlineStateForTest() const;
  QString OnlineProgressForTest() const;
  QString OnlineLastErrorForTest() const;
  bool OnlineReadyForTest() const { return online_ready_; }
  bool OnlineModeForTest() const { return online_mode_; }
  qint64 OnlineStableFramesForTest() const;
  bool OnlineSeekIdleForTest() const { return !online_seek_running_; }
  int OnlineQualityForTest() const { return online_qn_; }
  QString OnlinePartPathForTest() const { return online_part_path_; }
  qint64 OnlineContainerFramesForTest() const;
  QString StatusMessageForTest() const;
  void CancelOnlineLoadingForTest() { CancelOnlineLoading(); }
  void CancelOnlineForTest() { CancelOnlineLoading(); }
  // 等待在线首帧（或错误/超时），供 --online-m1-test 使用。
  bool WaitForOnlineReadyForTest(int timeout_ms);
  // 跳到 0 基帧号；成功时画面/信息栏同步。T2.4/T2.6 共用。
  void GoToFrame(std::int64_t frame);
  // T7.4 4K 性能探针入口：走与真实鼠标/键盘完全相同的产品路径。
  void ScrubStartForTest(qint64 frame);
  void ScrubTargetForTest(qint64 frame);
  void ScrubFinishForTest(qint64 frame);
  void GoToFrameForTest(qint64 frame);
  // 目标帧距前关键帧的帧数（P6/P9 的 ≤2s 条件）；索引未就绪返回 -1。
  qint64 FrameDistanceFromKeyframeForTest(qint64 frame) const;
  bool HardwareDecodeForTest() const;
  int PresentedFrameCountForTest() const;
  double FrameRateForTest() const;
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
  // T7.2.1：B→A 成功跳转次数，用于“B 在末帧”回归。
  int LoopJumpCountForTest() const;
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
  // T6.1 自动化验证入口：与 F / Esc / 双击画面走同一套切换逻辑与提示。
  void ToggleFullScreenForTest();
  void PressFullScreenKeyForTest();
  void PressEscapeKeyForTest();
  void PressSpaceKeyForTest();
  void FocusVideoForTest();
  void DoubleClickVideoForTest();
  bool IsFullScreenForTest() const;
  bool IsMaximizedForTest() const;
  bool IsPlayingForTest() const;
  QString FullScreenStateForTest() const;
  QSize GfxWidgetSizeForTest() const;
  QSize SwapChainSizeForTest() const;
  QSize TimelineSizeForTest() const;
  qint64 TimelineFrameAtForTest(int x, int y) const;
  void ClickTimelineForTest(int x, int y);
  QString InfoFrameTextForTest() const;
  QString FocusWidgetNameForTest() const;
  bool IsTextInputFocusedForTest() const;
  bool TimelineVisibleForTest() const;
  bool InfoBarVisibleForTest() const;
  qint64 CurrentFrameForTest() const;
  qint64 LastFullScreenToggleMsForTest() const;
  // T6.2 键位/焦点矩阵：真实 QKeyEvent（可带修饰键/OS 重复）发到当前焦点控件；
  // Home/End/PgUp/PgDn/←/→ 由窗口级事件过滤器统一处理。
  void PressKeyForTest(int key, int modifiers = 0);
  void PressKeyDownForTest(int key, int modifiers = 0);
  void PressKeyUpForTest(int key, int modifiers = 0);
  void PressAutoRepeatKeyForTest(int key, int modifiers = 0);
  void PressAutoRepeatReleaseForTest(int key, int modifiers = 0);
  bool FocusMarkTreeForTest();
  bool FocusVolumeSliderForTest();
  QString KeyStateForTest() const;
  QString ShortcutMapForTest() const;
  bool StepRepeatActiveForTest() const;
  double SpeedForTest() const;
  bool WaveformVisibleForTest() const;
  bool MutedForTest() const;
  int VolumeForTest() const;
  qint64 LastFrameIndexForTest() const;
  double TimelinePixelsPerFrameForTest() const;
  // T6.3 设置持久化：与滑块/W 开关走同一组控件；Flush 等价于关窗保存。
  void SetVolumeForTest(int volume);
  void SetWaveformVisibleForTest(bool visible);
  bool FlushSettingsForTest();
  void ReloadSettingsForTest();
  QString SettingsPathForTest() const;
  QString SettingsStateForTest() const;
  QString SettingsLoadStatusForTest() const;
  QString DefaultSettingsRootForTest() const;
  double LastSettingsSaveMsForTest() const;
  double LastSettingsLoadMsForTest() const;
  // T6.4 DPI/布局：当前窗口/面板/信息栏/状态栏的几何问题清单；空表示无裁切/重叠。
  QString LayoutStateForTest() const;
  QStringList LayoutProblemsForTest() const;
  QSize AvailableScreenForTest() const;
  QString InfoFileLabelForTest() const;
  QString InfoFileNameForTest() const;
  bool InfoNarrowModeForTest() const;
  // T6.5 状态横幅：真实媒体/合成提示、状态栏次要提示与布局断言。
  void SetBannerNoticesForTest(const QStringList& kinds);
  void ClearBannerNoticesForTest();
  QString BannerStateForTest() const;
  QString BannerUiStateForTest() const;
  QString SecondaryStatusTextForTest() const;
  int InfoBarHeightForTest() const;
  // T7.1 冷启动：打开成功（会话已替换）与首帧呈现信号，供 --startup-test 计时。
  bool HasMediaForTest() const { return session_ != nullptr; }
  // T7.1 后台索引：产品路径异步；旧 GUI 回归测试在打开后立即导航，需同步建索引。
  void SetIndexSynchronousForTest(bool sync) { sync_index_for_test_ = sync; }
  bool IndexReadyForTest() const;
  qint64 IndexFrameCountForTest() const;
  QString IndexStateForTest() const;
  // T7.2：帧缓存上限/占用/命中统计（用于 --memory-test 与日志）。
  QString FrameCacheStateForTest() const;
  // T7.3：长时浸泡采样用的轻量读数（缓存条目、音频/波形状态与 underrun）。
  int FrameCacheEntryCountForTest() const;
  qint64 FrameCacheByteCountForTest() const;
  QString AudioStateForTest() const;
  qulonglong AudioUnderrunCountForTest() const;
  QString WaveformStateForTest() const;
  // T7.5：设备丢失模拟与错误详情（--robust-test 用；产品语义不变）。
  void SimulateAudioDeviceLossForTest(bool active);
  QString LastOpenErrorForTest() const;
  QString LastNavigationErrorForTest() const;
  // T7.5：--robust-test 用独立 AppData 根，避免只读目录回退写进真实
  // %APPDATA%\拉片软件\marks。空 = 产品默认。
  void SetMarkAppDataRootForTest(const QString& root);
  // T8.6 E2E：在开始在线会话前覆盖缓存根（不写用户默认目录）。
  void SetOnlineCacheRootForTest(const QString& root);
  // T8.6 缓冲耗尽/错误注入：只影响下一次在线会话，不进入产品默认路径。
  void SetOnlineTransportForTest(
      std::unique_ptr<lapian::online::IDownloadTransport> transport);
  void SetOnlinePlayUrlForTest(const lapian::online::PlayUrlData& data);
  void SetOnlineReadyThresholdForTest(qint64 bytes);
  bool IsOnlineBufferingForTest() const;
  int OnlineBufferEventCountForTest() const { return online_buffer_event_count_; }
  QString OnlineGateDebugForTest() const;

 signals:
  // T7.1：每一帧真正 Present 后转发一次（GfxWidget::framePresented）。
  void framePresented();

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private slots:
  void OpenDialog();
  void OpenOnlineDialog();
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
  void ShowOnlineDialog();
  void EnsureOnlineSession();
  void BeginOnlineRequest(const lapian::app::OnlineRequest& request);
  bool ApplyOnlineReady(const lapian::app::OnlineReadyInfo& info);
  void ApplyOnlineProgress(const lapian::app::OnlineProgress& progress);
  void ApplyOnlineState(lapian::app::OnlineState state, const QString& message);
  void StartOnlineIndexBuild();
  void CancelOnlineLoading();
  void StopOnlineIndexBuild();
  void StopOnlineSeekWorker();
  void RequestOnlineSeek(std::int64_t target, lapian::media::FrameCacheMode mode,
                         bool exact_index_covered);
  bool OnlineFrameCached(std::int64_t frame) const;
  void ShutdownOnlineSession(bool finalize_if_complete);
  void GoToFrameInternal(std::int64_t frame, lapian::media::FrameCacheMode mode);
  void GoToFrameForStep(std::int64_t frame, bool backward);
  // T7.11.1：把待回拷的后退窗口按块写进 FrameCache（每块 4 帧，0ms 定时器）。
  void FillFrameCacheStep();
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
  // T6.1：全屏进入/退出。保存/恢复普通窗口几何、最大化状态与焦点；
  // 不改布局（时间轴/信息栏/侧栏/状态栏在全屏下仍可用）。
  void EnterFullScreen();
  void ExitFullScreen();
  void ToggleFullScreen();
  void UpdateFullScreenActionState();
  void SendKeyForTest(int key, int modifiers = 0, bool auto_repeat = false);
  void RestoreFocusIfNeeded(QWidget* expected);
  // T6.2：全局步进/首末帧。步进是第一等交互，焦点在时间轴/侧栏/状态栏时也应可用；
  // 文本框与模态对话框让位，OS 自动重复只吞掉、不额外走帧。
  bool CanStepFrames() const;
  bool HandleGlobalKey(QKeyEvent* event);
  void StartStepRepeat(int delta);
  void StopStepRepeat();
  void SendKeyPressForTest(int key, int modifiers, bool auto_repeat);
  void SendKeyReleaseForTest(int key, int modifiers, bool auto_repeat = false);
  // T6.3：加载/应用/保存设置。只在 UI 构造完成后、首次打开文件前应用；
  // 读取失败静默回退默认，保存失败不阻塞关窗。
  void LoadSettingsIntoUi();
  void ApplySettings(const lapian::core::AppSettings& settings);
  void ScheduleSettingsSave();
  bool FlushSettings();
  // T6.4：初始窗口按屏幕可用区收缩；侧栏/次要状态在窄宽度下让位。
  void ApplyInitialWindowSize();
  void UpdateResponsiveLayout();
  // T6.5：InfoBar 只显示一条横幅，其余提示汇总到状态栏常驻标签。
  void UpdateStatusNotices();
  // T7.1：索引后置。首帧先出，索引在后台线程建；扫描中禁用依赖索引的导航。
  void BeginIndexScan();
  void StartIndexBuild();
  void CancelIndexBuild();
  void ApplyIndexReady(std::uint64_t generation);
  void ApplyIndexFailed(std::uint64_t generation);
  void ApplyIndexState();
  void LoadMarksForCurrentSession();
  lapian::core::TotalFrameCount ContainerTotal() const;

  GfxWidget* gfx_ = nullptr;
  QWidget* stage_ = nullptr;
  TimelineWidget* timeline_ = nullptr;
  InfoBar* info_bar_ = nullptr;
  MarkPanel* mark_panel_ = nullptr;
  OnlineResolveDialog* online_dialog_ = nullptr;
  QLabel* status_label_ = nullptr;
  QLabel* audio_label_ = nullptr;
  QLabel* speed_label_ = nullptr;
  QLabel* loop_label_ = nullptr;
  QLabel* waveform_label_ = nullptr;
  QLabel* notice_label_ = nullptr;
  QSlider* volume_slider_ = nullptr;
  QAction* mute_action_ = nullptr;
  QAction* slower_action_ = nullptr;
  QAction* faster_action_ = nullptr;
  QTimer* audio_status_timer_ = nullptr;
  QAction* open_action_ = nullptr;
  QAction* online_action_ = nullptr;
  QAction* online_cancel_action_ = nullptr;
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
  QAction* fullscreen_action_ = nullptr;
  QAction* exit_fullscreen_action_ = nullptr;
  QString base_info_;
  QString audio_base_label_;
  std::shared_ptr<lapian::media::MediaSession> session_;
  std::unique_ptr<lapian::app::OnlineSession> online_;
  std::unique_ptr<lapian::online::CacheStore> online_cache_;
  std::unique_ptr<PlaybackController> playback_;
  std::unique_ptr<lapian::media::PreviewDecoder> preview_;
  std::unique_ptr<lapian::audio::AudioEngine> audio_;
  std::unique_ptr<lapian::audio::WaveformGenerator> waveform_;
  std::unique_ptr<lapian::core::MarkStore> marks_;
  std::unique_ptr<lapian::core::SettingsStore> settings_store_;
  std::unique_ptr<SnapshotService> snapshots_;
  lapian::core::LoopRange loop_;
  QTimer* preview_timer_ = nullptr;
  QTimer* step_repeat_timer_ = nullptr;
  QTimer* settings_save_timer_ = nullptr;
  // T7.11.1：后退跨 GOP 第一步的硬解窗口延后回拷（呈现后再填缓存）。
  QTimer* cache_fill_timer_ = nullptr;
  int step_direction_ = 0;
  AVFrame* displayed_frame_ = nullptr;
  std::int64_t current_frame_ = 0;
  std::int64_t current_pts_us_ = 0;
  std::int64_t displayed_frame_index_ = -1;
  // T7.4：本次会话真正 Present 的视频帧数（P8“每帧呈现”探针）。
  int presented_frame_count_ = 0;
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
  // T7.5：失败原因供 --robust-test 与状态诊断读取；成功打开/跳转后清空。
  QString last_open_error_;
  QString last_navigation_error_;
  QString mark_appdata_root_for_test_;
  QString snapshot_pictures_root_for_test_;
  std::int64_t snapshot_last_capture_frame_ = -1;
  std::int64_t snapshot_last_saved_frame_ = -1;
  bool snapshot_last_fallback_ = false;
  bool snapshot_log_ = false;
  // T6.1 全屏：进入前的普通几何/最大化状态与焦点控件，退出时恢复。
  QByteArray fullscreen_geometry_;
  QPointer<QWidget> fullscreen_focus_widget_;
  bool fullscreen_was_maximized_ = false;
  qint64 fullscreen_last_toggle_ms_ = -1;
  // T8.6 在线会话/部分索引/异步 seek。
  bool online_mode_ = false;
  bool online_ready_ = false;
  bool online_finalized_ = false;
  bool has_online_test_playurl_ = false;
  qint64 online_test_threshold_ = 0;
  std::unique_ptr<lapian::online::IDownloadTransport> online_test_transport_;
  std::shared_ptr<lapian::online::PlayUrlData> online_test_playurl_;
  int online_buffer_event_count_ = 0;
  bool online_gate_last_ = true;
  qint64 online_gate_published_ = 0;
  qint64 online_gate_readpos_ = 0;
  QString online_bvid_;
  QString online_title_;
  QString online_part_path_;
  QString online_display_path_;
  qint64 online_cid_ = 0;
  qint64 online_total_bytes_ = 0;
  qint64 online_published_bytes_ = 0;
  int online_qn_ = 0;
  int online_codecid_ = 0;
  int online_stable_frames_ = 0;
  bool online_index_complete_ = false;
  std::vector<lapian::media::OnlineFrameSpan> online_spans_;
  QString online_last_error_;
  std::thread online_index_thread_;
  std::shared_ptr<std::atomic<bool>> online_index_cancel_;
  std::uint64_t online_index_generation_ = 0;
  std::thread online_seek_thread_;
  std::shared_ptr<std::atomic<bool>> online_seek_cancel_;
  std::uint64_t online_seek_generation_ = 0;
  bool online_seek_running_ = false;
  bool online_session_poisoned_ = false;
  bool waveform_visible_ = true;
  int volume_ = 100;
  double rate_ = 1.0;
  bool muted_ = false;
  bool audio_error_shown_ = false;
  bool error_shown_ = false;
  bool marks_dirty_ = false;
  // T6.3 设置持久化：只写 volume/waveform；loop/rate/mute 不落盘。
  bool settings_dirty_ = false;
  bool applying_settings_ = false;
  bool settings_log_ = false;
  // T8.3：最近一次读到的设置；保存音量/波形时保留 cache_root/cache_quota_gb。
  lapian::core::AppSettings loaded_settings_;
  // T7.1：冷启动分段计时日志（LAPIAN_STARTUP_LOG 或 --startup-test）。
  bool startup_log_ = false;
  // T7.1：索引线程与取消。CancelIndexBuild 会 join，索引循环每包检查取消标志。
  bool sync_index_for_test_ = false;
  std::uint64_t index_generation_ = 0;
  bool index_scanning_ = false;
  std::thread index_thread_;
  std::shared_ptr<std::atomic<bool>> index_cancel_;
  QElapsedTimer index_scan_timer_;
  QString settings_load_status_;
  double settings_last_save_ms_ = 0.0;
  double settings_last_load_ms_ = 0.0;
};
