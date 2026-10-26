#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "core/AppSettings.h"
#include "core/FrameMath.h"
#include "core/LoopRange.h"
#include "core/MarkStore.h"
#include "app/OnlineSession.h"
#include "media/DragStrategy.h"
#include "media/OnlineIndexBuilder.h"
#include "online/QualityMenu.h"

struct AVFrame;
struct OnlineCacheSummary;

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QKeyEvent;
class QLabel;
class QMenu;
class QPushButton;
class QResizeEvent;
class QSlider;
class QTimer;
class GfxWidget;
class MarkPanel;
class OnlineResolveDialog;
class CacheSettingsDialog;
class PlaybackController;
class PlayerShell;
class SnapshotService;
class StageStack;
class TimelineWidget;

namespace lapian::core {
struct AppSettings;
class SettingsStore;
}  // namespace lapian::core

namespace lapian::media {
enum class FrameCacheMode;
struct MediaInfo;
class MediaSession;
class OnlinePreviewDecoder;
class PreviewDecoder;
}

namespace lapian::audio {
class AudioEngine;
class WaveformGenerator;
}

namespace lapian::online {
class CacheStore;
class IDownloadTransport;
class IHttpClient;
struct CacheKey;
struct PlayUrlData;
}  // namespace lapian::online

namespace lapian::app {
class OnlineSession;
struct OnlineProgress;
struct OnlineReadyInfo;
struct OnlineRequest;
enum class OnlineState;
}

// DP-1：本地拖动预览诊断快照。target_lag 由探针用 target_frame/displayed_frame
// 计算；hw_active 只在预览解码器真的走 D3D11VA 时为 true。
struct PreviewTestDiagnostics {
  bool active = false;
  bool hw_requested = false;
  bool hw_active = false;
  QString hw_fallback_reason;
  qint64 target_frame = -1;
  qint64 displayed_frame = -1;
  qint64 displayed_pts_us = 0;
  bool displayed_coarse = false;
  bool displayed_from_cache = false;
  // DP-3：逐帧来源标记。hw_active 只说明解码器在硬解；displayed_hardware
  // 说明“这一张被呈现的帧”确实是硬件帧（粗帧/缓存帧通常为 false）。
  bool displayed_hardware = false;
  // DP-3：拖动策略与目标滞后。mode 由 MainWindow 按 step/方向/间隔分类，
  // 阈值见 media/DragStrategy.h；target_lag 是最近一次 Present 时显示帧与
  // 当前目标的绝对差（帧/毫秒），由探针直接读取，不再只看 fps。
  QString drag_mode;
  QString drag_mode_reason;
  qint64 drag_step = 0;
  qint64 drag_interval_ms = 0;
  double drag_speed_frames_per_s = 0.0;
  int drag_same_direction_run = 0;
  qint64 target_lag_frames = 0;
  double target_lag_ms = 0.0;
  double cache_hit_rate = 0.0;
  int seeks = 0;
  int cache_hits = 0;
  int cache_misses = 0;
  int cache_requests = 0;
  int coarse_hits = 0;
  int coarse_requests = 0;
  int coarse_published = 0;
  int prefetched_frames = 0;
  int readbacks = 0;
  qint64 cache_bytes = 0;
  qint64 cache_peak_bytes = 0;
  qint64 thumbnail_frames = 0;
  qint64 thumbnail_bytes = 0;
  qint64 thumbnail_peak_bytes = 0;
  double first_coarse_ms = -1.0;
  double last_coarse_ms = -1.0;
  double seek_ms_avg = 0.0;
  double decode_ms_avg = 0.0;
  double readback_ms_avg = 0.0;
  int published = 0;
  int presented = 0;
  int decoder_reopens = 0;
  // A（缓存命中拖动跟手）诊断：preview_ 存在但 0 帧时，必须能区分“配置路径
  // 不是真实文件”“预览解码器打开失败”“Begin 没调用/定时器空转取不到帧”。
  bool open_attempted = false;
  bool open_ok = false;
  QString open_error;
  QString last_error;
  QString configure_path;
  QString session_path;
  qint64 begin_count = 0;
  qint64 null_ticks = 0;
};

// P0：每次在线拖动的度量快照（只读；由 OnScrubFinished 在 Pause 前保存）。
// 只用于证据/探针，不参与任何发布/缓存/预热判定。
struct OnlineDragStatsSnapshot {
  bool valid = false;
  qint64 drag_ms = -1;
  qint64 open_ms = -1;
  qint64 first_publish_ms = -1;
  qint64 first_delivered_ms = -1;
  qint64 last_delivered_before_resume = -1;
  qint64 baseline_frame = -1;
  int published = 0;
  int delivered = 0;
  int published_before_release = 0;
};

// T1.4 主窗口：打开入口（Ctrl+O / 拖拽 / 命令行）、空态、错误提示、首帧显示。
// 播放、时间轴、标记等仍由后续任务接入。
class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr,
                      const QString& settings_root = QString(),
                      bool enable_cache_maintenance = true,
                      bool ui_v2 = false);
  ~MainWindow() override;

  // UI v2（3D）：只剩一套布局；开关仍是 no-op 别名（旧脚本不炸）。
  bool UiV2ForTest() const { return true; }
  PlayerShell* ShellForTest() const { return shell_; }
  // §7.8 测试缝（全部只读/无副作用，除 SetSplitSizes/DragSplitter）。
  QString ShellStateForTest() const;
  QString DensityNameForTest() const;
  int GfxVisibleForTest() const;
  QString StagePageForTest() const;
  int BannerSlotHeightForTest() const;
  QRect BannerSlotGeometryForTest() const;
  QString SplitterStateForTest() const;
  void SetSplitSizesForTest(int timeline, int inspector);
  // 3B：只改时间轴高度（检查器保持当前宽度），走同一条 96–340 夹取路径。
  void SetTimelineHeightForTest(int timeline);
  void DragSplitterForTest(bool vertical, int delta);
  bool InspectorCollapsedForTest() const;
  // UI v2 3C：折叠/展开检查器（走与按钮相同的 PlayerShell::SetInspectorCollapsed）。
  void SetInspectorCollapsedForTest(bool collapsed);
  // 0=标记 / 1=截图 / 2=信息（走与 tab 点击相同的 SetTab：切换前 flush 编辑框）。
  void SetInspectorTabForTest(int tab);
  // 检查器状态（折叠/tab/页/计数/宽度/存储位置）——只读、无副作用，一行可断言。
  QString InspectorStateForTest() const;
  QString FullScreenChromeForTest() const;
  QString ReadoutTextForTest() const;
  QRect ReadoutGeometryForTest() const;
  // 3D：M2 视觉证据（主题内 主窗/设置对话框/帮助/在线对话框/速度菜单 各一张）。
  void CaptureThemeShotsForTest(const QString& directory);
  // P3：清晰度 chip 视觉证据（在线态文案 + 1280/820 两张图 + 顶栏溢出断言）。
  bool CaptureQualityChipShotsForTest(const QString& directory);
  QString TopBarOverflowForTest() const;
  // 3D：顶栏模式 chip 的完整文本（空态断言 `未打开`）。
  QString TopBarModeForTest() const;
  // 3B：视图窗 = 画布可见范围（规划 Q3 同源断言）+ 播放头常驻 + 宽度档位 +
  // 缓冲槽 + 两条滑块的合成拖拽（与真实鼠标同一路径）。
  QString TimelineViewFramesForTest() const;
  bool TimelinePlayheadVisibleForTest() const;
  QString TransportTierForTest() const;
  QString BufferSlotStateForTest() const;
  bool TimelineBarDragForTest(bool work_bar, int from_x, int to_x);
  QString TimelineBarGeometryForTest() const;
  // 工作区两端的"对齐"断言（2026-10-26 用户反馈的问题）：同一个帧在
  // **工作区滑块**上的像素 x 与在**画布**上的像素 x 必须相等（±1px）。
  QString TimelineBarAlignForTest() const;
  // M7：帧号控件（v2FrameNumber）在播放/暂停/拖动/全屏四态的几何。
  // 输出 `visible=1 rect=.. transport=.. rel=dx,dy wxh`；rel 是"帧号中心 −
  // Transport 中心"，全屏换了窗口尺寸也应当不变（唯一可跨态比较的量）。
  QString M7StateForTest() const;
  // T7.1：D3D11 是否在"没有媒体打开请求"时就初始化（旧壳=1，v2 空态隐藏=0）。
  bool D3dInitAtStartupForTest() const { return d3d_init_at_startup_; }

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
  // P0 热修：在线 seek 落地前按 Space 只记“待播”，落地后由回调补 Play。
  bool OnlinePendingPlayForTest() const { return online_pending_play_; }
  int OnlineQualityForTest() const { return online_qn_; }
  int OnlineCodecidForTest() const { return online_codecid_; }
  QString OnlinePartPathForTest() const { return online_part_path_; }
  qint64 OnlineContainerFramesForTest() const;
  // T8.10 DASH 双输入/音画/seek/内存验收 hooks。
  bool OnlineDashForTest() const { return online_dash_audio_; }
  int OnlineAudioIdForTest() const { return online_audio_id_; }
  qint64 OnlineAudioTotalBytesForTest() const { return online_audio_total_bytes_; }
  qint64 OnlineTotalBytesForTest() const { return online_total_bytes_; }
  double OnlineDashFpsForTest() const { return online_dash_fps_; }
  bool OnlinePlayingForTest() const;
  double OnlinePlaybackSecondsForTest() const;
  bool OnlineAudioClockForTest(double* seconds) const;
  double OnlineAudioPositionForTest() const;
  int OnlineAudioStateForTest() const;
  bool OnlineIndexReadyForTest() const;
  int OnlineIndexFramesForTest() const;
  bool OnlineIndexCompleteForTest() const { return online_index_complete_; }
  bool WaitForOnlineIndexFramesForTest(int min_frames, int timeout_ms);
  bool WaitForOnlineCacheCompleteForTest(int timeout_ms);
  // T8.11：帧号三态（unknown/scanning/estimate:<n>/exact:<n>）、增量索引统计、
  // 已缓存秒数、波形渐进/偏移 hooks。
  QString OnlineTotalStateForTest() const;
  QString OnlineIndexStatsForTest() const;
  qint64 OnlineCachedSecondsForTest() const;
  qint64 OnlinePublishedBytesForTest() const;
  double OnlineDurationSecondsForTest() const;
  double OnlineVideoFpsForTest() const;
  int OnlineWaveformBucketCountForTest() const;
  double OnlineWaveformOffsetFramesForTest() const;
  void SetOnlineDownloadThrottleForTest(qint64 bytes_per_sec);
  void SetOnlineDashEnabledForTest(bool enabled);
  void ShutdownOnlineForTest() { ShutdownOnlineSession(true); }
  qint64 CurrentPtsUsForTest() const { return current_pts_us_; }
  void PausePlaybackForTest();
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
  QString OnlineCacheRootForTest() const;
  // T8.3.1：缓存管理/启动清理的自动化入口（不弹设置对话框）。
  void SetCacheMaintenanceEnabledForTest(bool enabled);
  void RunCacheMaintenanceForTest();
  bool ApplyCacheSettingsForTest(const QString& root, int quota_gb,
                                 QString* error);
  // P1 热修：在线会话中保存缓存目录/配额 → 两阶段（本次会话继续用旧 store，
  // idle 后由 ApplyPendingCacheStoreIfIdle() 换新 store）。
  bool CacheSettingsPendingForTest() const { return cache_settings_pending_; }
  void ApplyPendingCacheStoreForTest() { ApplyPendingCacheStoreIfIdle(); }
  // 只给离屏探针模拟“在线会话进行中”（不启动网络会话，不改产品路径）。
  void SetOnlineModeForTest(bool online) { online_mode_ = online; }
  bool CacheRootReadyForTest() const { return online_cache_ready_; }
  QString CacheRootErrorForTest() const { return online_cache_error_; }
  QString CacheLastMaintenanceReasonForTest() const {
    return cache_last_maintenance_reason_;
  }
  qint64 CacheLastMaintenanceRemovedBytesForTest() const {
    return cache_last_maintenance_removed_bytes_;
  }
  int CacheLastMaintenanceRemovedEntriesForTest() const {
    return cache_last_maintenance_removed_entries_;
  }
  // T8.7 E2E：在线记录/截图回退根（空 = 真实 %APPDATA%\拉片软件\online）。
  void SetOnlineRecordsRootForTest(const QString& root);
  QString OnlineRecordPathForTest() const;
  QString OnlineSnapshotIndexPathForTest() const;
  bool OnlineCacheHitForTest() const { return online_cache_hit_; }
  QString OnlineCachePathForTest() const { return online_cache_path_; }
  bool OnlineCacheCompleteForTest() const;
  // T8.17：切换前等待两路下载都进入可 finalize 状态，保证旧档在切档时落盘
  // （否则 audio `.part` 不会变成 complete，无法验证“音频已缓存”分支）。
  bool OnlineCacheFinalizableForTest() const;
  int OnlinePlayUrlRequestsForTest() const;
  int OnlineDashRequestsForTest() const;
  // T8.7 断网验证：注入不会成功的 HTTP 客户端；命中缓存时它不应被调用。
  void SetOnlineHttpClientForTest(
      std::unique_ptr<lapian::online::IHttpClient> client);
  void ClearOnlineTestPlayUrlForTest();
  // T8.6 缓冲耗尽/错误注入：只影响下一次在线会话，不进入产品默认路径。
  void SetOnlineTransportForTest(
      std::unique_ptr<lapian::online::IDownloadTransport> transport);
  void SetOnlinePlayUrlForTest(const lapian::online::PlayUrlData& data);
  void SetOnlineReadyThresholdForTest(qint64 bytes);
  // T8.9 档位目录/菜单：默认在首帧后异步取一次 fnval=4048；测试可关/注入。
  void SetOnlineQualityCatalogEnabledForTest(bool enabled);
  void SetOnlineQualityForTest(const lapian::app::OnlineQualityCatalog& info);
  bool OnlineQualityButtonVisibleForTest() const;
  bool OnlineQualityReceivedForTest() const { return online_quality_.fetched; }
  QString OnlineQualityErrorForTest() const { return online_quality_.error; }
  // T8.12：实际流 fps/降帧、切档状态与 DEC-4 UI。
  double OnlineStreamFpsForTest() const { return online_actual_fps_; }
  double OnlineSourceFpsHintForTest() const { return online_source_fps_hint_; }
  QString OnlineStatusDetailForTest() const { return online_status_detail_; }
  bool OnlineFrameRateDropForTest() const { return online_frame_rate_drop_; }
  QString OnlineFpsBadgeTextForTest() const;
  bool OnlineFrameRateBannerVisibleForTest() const;
  bool OnlineFrameRateBannerCloseVisibleForTest() const;
  void DismissOnlineFrameRateBannerForTest();
  bool OnlineQualitySwitchPendingForTest() const {
    return online_switch_pending_;
  }
  QString OnlineQualitySwitchStatusForTest() const {
    return online_switch_status_;
  }
  bool WaitForOnlineQualitySwitchForTest(int qn, int timeout_ms);
  QString OnlineQualityMenuTextForTest() const;
  // 真实 QMenu 的 action 快照（text|enabled|checked），证明产品菜单已构建。
  QStringList OnlineQualityMenuActionsForTest() const;
  QString OnlineQualitySummaryForTest() const;
  int OnlineQualityRequestsForTest() const;
  bool WaitForOnlineQualityForTest(int timeout_ms);
  // B（缓存命中清晰度菜单）：点开菜单 = 产品入口，按需拉完整 DASH 目录。
  void ShowOnlineQualityMenuForTest();
  void HideOnlineQualityMenuForTest();
  bool OnlineQualityMenuVisibleForTest() const;
  bool OnlineQualityCatalogLoadingForTest() const {
    return online_quality_loading_;
  }
  // 触发菜单项：返回 false = 不存在/置灰（*status 可读）；true = 已触发。
  bool TriggerOnlineQualityEntryForTest(int qn, QString* status);
  // 菜单模型（与产品菜单共用 BuildQualityMenuModel）；T8.9 只列示不切档。
  QVector<lapian::online::QualityMenuEntry> OnlineQualityMenuEntriesForTest() const;
  bool IsOnlineBufferingForTest() const;
  int OnlineBufferEventCountForTest() const { return online_buffer_event_count_; }
  QString OnlineGateDebugForTest() const;
  // T8.6.1a 在线长按步进：可见状态与落地轨迹，供 --online-longpress-test 断言。
  std::int64_t OnlineLastFrameForTest() const;
  bool OnlineFrameCachedForTest(qint64 frame) const;
  qint64 OnlineFramePtsForTest(qint64 frame) const;
  // C2（test-only）：目标帧距“前关键帧（含自身）”的帧数/毫秒。P6 前提是
  // ≤2s；只读 session 索引，fps 用实际流 fps / 容器 fps / 30 回退。
  qint64 OnlineKeyframeDistanceFramesForTest(qint64 frame) const;
  double OnlineKeyframeDistanceMsForTest(qint64 frame) const;
  bool OnlineStepIdleForTest() const;
  QString OnlineStepStateForTest() const;
  std::vector<qint64> OnlineStepTraceForTest() const;
  void ResetOnlineStepTraceForTest();
  int OnlineStepBoundaryStopsForTest() const;
  int OnlineStepRetargetCountForTest() const;
  // T8.6.1b 在线拖动预览：已缓存区预览呈现/等待/过期帧统计，供探针断言。
  bool PreviewActiveForTest() const;
  // A：cache-hit-only 探针的显式预热（显示画面页 + D3D11 惰性初始化），返回
  // 耗时毫秒；探针会把它打印出来，不隐藏一次性成本。
  qint64 WarmupVideoStageForTest();
  bool OnlinePreviewActiveForTest() const { return online_preview_ != nullptr; }
  bool OnlinePreviewWaitingForTest() const;
  bool OnlinePreviewThreadRunningForTest() const;
  qint64 OnlinePreviewTargetForTest() const { return online_preview_target_; }
  qint64 OnlinePreviewLastFrameForTest() const {
    return online_preview_last_frame_;
  }
  qint64 OnlinePreviewLastPtsForTest() const { return online_preview_last_pts_; }
  int PreviewPresentedForTest() const { return preview_presented_count_; }
  PreviewTestDiagnostics PreviewDiagnosticsForTest() const;
  int OnlinePreviewPresentedForTest() const {
    return online_preview_presented_;
  }
  bool OnlinePreviewLastCoarseForTest() const {
    return online_preview_last_coarse_;
  }
  bool OnlinePreviewLastFromCacheForTest() const {
    return online_preview_last_from_cache_;
  }
  int OnlinePreviewCoarsePresentedForTest() const {
    return online_preview_coarse_presented_;
  }
  int OnlinePreviewCachePresentedForTest() const {
    return online_preview_cache_presented_;
  }
  std::size_t OnlinePreviewCacheBytesForTest() const;
  std::size_t OnlinePreviewCachePeakBytesForTest() const;
  std::uint64_t OnlinePreviewCacheHitsForTest() const;
  std::uint64_t OnlinePreviewPrefetchedForTest() const;
  std::uint64_t OnlinePreviewSeekCountForTest() const;
  std::uint64_t OnlinePreviewCoarsePublishedForTest() const;
  std::uint64_t OnlinePreviewStaleDroppedForTest() const;
  int OnlinePreviewCachedFramesForTest() const;
  int OnlinePreviewDroppedForTest() const { return online_preview_dropped_; }
  int OnlinePreviewErrorsForTest() const { return online_preview_errors_; }
  QString OnlinePreviewStateForTest() const;
  // P0：本次拖动（ScrubStart→ScrubFinished）的只读度量；S1 只记录不改行为。
  OnlineDragStatsSnapshot OnlineDragStatsForTest() const {
    return online_drag_stats_;
  }
  qint64 OnlinePreviewBaselineFrameForTest() const {
    return online_drag_stats_.baseline_frame;
  }
  qint64 OnlinePreviewWarmMsForTest() const;
  qint64 OnlinePreviewWarmFramesForTest() const;
  std::uint64_t OnlinePreviewWarmSkippedForTest() const;
  std::uint64_t OnlinePreviewWarmFailuresForTest() const;
  std::uint64_t OnlinePreviewSeekFallbacksForTest() const;
  qint64 OnlinePreviewLastSeekKeyframeForTest() const;
  qint64 OnlinePreviewLastSeekLandingOffsetForTest() const;
  std::uint64_t OnlinePreviewPrimedFramesForTest() const;
  bool OnlinePreviewWarmIdleForTest() const;
  void StopPreviewForTest() { StopPreview(); }
  void ResetPreviewStatsForTest();
  // 自动化模拟“物理键仍按住时收到伪 KeyRelease”；-1 走真实 GetAsyncKeyState。
  void SetStepKeyPhysicalDownForTest(int state) {
    step_key_physical_down_for_test_ = state;
  }

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
  // DP-3：按本次目标步长/方向/距上次目标时间/连续同向次数分类，写入诊断；
  // 结果同时交给 PreviewDecoder（Exact 关闭粗帧，FarJump 先粗后精）。
  void UpdatePreviewDragStrategy(qint64 frame);
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
  // T8.7：缓存命中的在线身份；用于在本地打开 data.mp4 后仍按 bvid+cid 存记录。
  struct OnlineOpenContext {
    QString bvid;
    QString title;
    QString owner;
    qint64 cid = 0;
    int qn = 0;
    int codecid = 0;
    double fps = 0.0;
    qint64 duration_ms = 0;
    qint64 total_bytes = 0;
    bool cache_hit = false;
  };

  void ShowEmptyState();
  // UI v2 壳层：构造视图根并"搬不换"地把既有控件 reparent 进新区。
  void BuildShellUi();
  void AdoptShellWidgets();
  void UpdateDensity();
  void UpdateShellStage();
  void UpdateShellModeChip();
  // UI v2（3A）：Transport 首/末帧、时间轴面板头、壳层状态同步（窄接口，无业务逻辑）。
  void JumpToEdgeFrame(bool to_end);
  void UpdateTimelineMeta();
  void SyncShellChrome();
  // 3B：工作区滑块手势 → 既有 SetLoopIn/SetLoopOut 语义（含 A>B、A=B 规则）。
  void ApplyWorkBarIn(std::int64_t frame);
  void ApplyWorkBarOut(std::int64_t frame);
  void ApplyWorkBarPan(std::int64_t in, std::int64_t out);
  void ApplyWorkBarClear();
  // 速度文本的唯一出口：旧壳状态栏标签 + v2 Transport 按钮（文本仍由 MainWindow 给）。
  void UpdateSpeedText();
  // 在线缓冲槽（仅 buffering/stalled 时可见；不看本地播放）。
  void UpdateBufferSlot();
  void ShowShellHelp();
  QStringList LayoutProblemsV2() const;
  QString LayoutStateV2() const;
  void ShowOnlineDialog();
  bool LookupOnlineCacheForDialog(const QString& bvid,
                                  OnlineCacheSummary* summary, QString* error);
  void EnsureOnlineSession();
  void InitializeOnlineCacheStore();
  bool EnsureOnlineCacheReady(QString* error);
  bool ApplyCacheSettings(const QString& root, int quota_gb, QString* error);
  // P1：只有 online_==nullptr && !online_mode_（idle）时把延迟的缓存设置换成
  // 新 CacheStore；在线/会话进行中一律不换，避免 OnlineSession 持有悬空指针
  // 或清掉正在写的 `.part`。
  void ApplyPendingCacheStoreIfIdle();
  bool ClearOnlineCache(QString* error, qint64* removed_bytes, int* kept_entries);
  bool CleanupOnlineCache(QString* error, qint64* removed_bytes,
                          int* removed_entries);
  bool OpenOnlineCacheDirectory(QString* error);
  void ShowCacheSettingsDialog();
  void RunCacheMaintenance(const char* reason);
  bool CurrentOnlineCacheKey(lapian::online::CacheKey* key) const;
  QString OnlineRecordsRoot() const;
  void BeginOnlineRequest(const lapian::app::OnlineRequest& request);
  // C4 打开预热：`d3d_init_at_startup=0` 时第一次真正显示画面页会触发 GfxWidget
  // 的 D3D11 惰性初始化（本机 145–200ms）。在线打开前先把这笔一次性成本付在
  // “用户刚点确认/正在解析”的等待窗口里：只切画面页 + 取设备，不打开媒体、
  // 不读缓存。重复调用在设备已存在时只返回；空态/本地打开绝不调用。
  qint64 PrewarmOnlineVideoStage();
  bool ApplyOnlineReady(const lapian::app::OnlineReadyInfo& info);
  bool ApplyOnlineCacheHit(const lapian::app::OnlineReadyInfo& info);
  // T8.9：档位目录回调 → 档位按钮/菜单；失败不影响播放。
  void ApplyOnlineQuality(const lapian::app::OnlineQualityCatalog& info);
  void ResetOnlineQuality();
  void RebuildOnlineQualityMenu();
  void ShowOnlineQualityMenu();
  lapian::online::QualityMenuContext CurrentQualityMenuContext() const;
  void OnOnlineQualityChosen(int qn);
  // T8.12：切档后的位置/状态恢复（按旧流 PTS 映射到新索引）与实际 fps/降帧 UI。
  void ClearOnlineQualitySwitch();
  void TryApplyPendingOnlineQualitySwitch();
  void ApplyOnlineFrameRateUi();
  QString BuildOnlineStatusDetail() const;
  void OpenLocalMedia(const QString& path, const OnlineOpenContext* online);
  void LoadOnlineMarks(const OnlineOpenContext& online);
  void ReconcileOnlineMarks();
  void ApplyOnlineProgress(const lapian::app::OnlineProgress& progress);
  void ApplyOnlineState(lapian::app::OnlineState state, const QString& message);
  void StartOnlineIndexBuild();
  void CancelOnlineLoading();
  void StopOnlineIndexBuild();
  // T8.11：索引快照只在 GUI 线程小步安装；seek 正在读当前索引时先挂起，
  // seek 结束后再装最新快照（避免丢掉 complete 快照）。
  void InstallOnlineIndexSnapshot(
      std::shared_ptr<lapian::media::OnlineIndexResult> built);
  void InstallPendingOnlineIndex();
  void StopOnlineSeekWorker();
  // T8.6.1a：在线步进不走同步 current_frame_ 检查，改为“目标帧 → 异步 seek →
  // 落地后再走下一步”的状态机；本地模式仍用原同步路径。
  struct OnlineStepState {
    bool has_desired = false;
    std::int64_t desired = 0;
    bool inflight = false;
    std::int64_t inflight_target = -1;
    bool inflight_cached = false;
    bool queued = false;
    std::int64_t queued_target = -1;
    bool queued_cached = false;
    int queued_direction = 0;
    int seeks = 0;
    int retargets = 0;
    int boundary_stops = 0;
    std::vector<std::int64_t> trace;
  };
  bool OnlineStepEligible() const;
  std::int64_t OnlineLastFrame() const;
  bool ApplyOnlineStep(int direction);
  void IssueOnlineStepSeek(std::int64_t target, bool cached);
  void OnOnlineStepLanded(std::int64_t target);
  void OnOnlineStepFailed(std::int64_t target);
  void ResetOnlineStepState();
  void ShowOnlineWaitPrompt(std::int64_t target);
  // T8.6.1b：在线增长会话的独立拖动预览读层。目标只保留最新值；未缓存目标
  // 只显示等待，不在 UI 线程解码/等待。松手先 End 预览，再走精确 seek。
  void EnsureOnlinePreviewStarted(std::int64_t frame,
                                  bool warm_only = false);
  void MaybeRefreshOnlinePreviewIndex();
  void ApplyOnlinePreviewWaitPrompt(std::int64_t target, bool force);
  void ClearOnlinePreviewWaitPrompt();
  void TickOnlinePreview();
  // P0：seek 落地/暂停后延迟预热预览解码器；拖动/seek 前取消。
  void ScheduleOnlinePreviewWarm(std::int64_t frame, int delay_ms = 120);
  void WarmOnlinePreviewNow();
  void WarmOnlinePreview(std::int64_t frame);
  void CancelOnlinePreviewWarm();
  bool OnlinePreviewWarmAllowed() const;
  std::int64_t OnlinePreviewDisplayedFrame() const;
  void EndScrubPreview();
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
  // DP-1：预览解码器借用的 D3D11 设备必须与 GfxWidget 当前设备一致；
  // 设备丢失重建后重开预览解码器。
  void EnsurePreviewDecoderDevice();
  // A（缓存命中拖动跟手）：DASH 缓存命中走 growing 会话，session_->path_utf8()
  // 是 display_path（只是展示名，磁盘上没有文件）；预览必须读缓存里真实的
  // data.mp4（online_part_path_/online_cache_path_）。
  QString CacheHitPreviewPath() const;
  void EnsureCacheHitPreviewDecoder();
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
  // UI v2 3C：检查器（三 tab）的数据推送与联动。
  // SyncInspectorChrome = 计数徽标 + 存储页脚 + 信息页（全部只读、无 IO）。
  void SyncInspectorChrome();
  // 扫历史截图（<视频目录>/snapshots 或在线 <bvid>/index.jsonl）；只在打开/截图后/切到截图页时调。
  void RefreshInspectorShots();
  void RefreshInspectorInfo();
  // 折叠/非标记 tab 时先展开并切到“标记”（FocusNoteEditor / 点标记跳转用）。
  void EnsureInspectorMarksVisible();
  QString SnapshotPicturesRoot() const;
  // 截图网格点击：先重绘 UI，再 singleShot(0) 精确解码（T5.2 顺序）。
  void ActivateShot(std::int64_t frame);
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
  void StopStepRepeat(const char* reason = "unspecified");
  bool IsStepKeyPhysicallyDown(int key) const;
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
  // 3D：InfoBar 拆解后的推送点（文件/规格 → TopBar，帧号/时间码/总帧 →
  // Transport，唯一横幅 → BannerSlot）。名字保持"UI 推送"语义，不做业务。
  void ApplyMediaInfoToUi(const lapian::media::MediaInfo& info, bool vfr);
  void ClearMediaInfoUi();
  void SetFrameForUi(std::int64_t frame_index);
  void SetTotalForUi(const lapian::core::TotalFrameCount& total);
  void ClearOnlineFrameRateUi();
  void UpdateCacheMetric();
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
  // UI v2（3D）：只有一套壳层；`ui_v2` 构造参数仍解析但不再影响布局。
  PlayerShell* shell_ = nullptr;
  StageStack* stage_stack_ = nullptr;
  bool media_open_requested_ = false;
  bool d3d_init_at_startup_ = false;
  bool d3d_init_seen_ = false;
  bool inspector_auto_decided_ = false;
  bool updating_layout_ = false;
  TimelineWidget* timeline_ = nullptr;
  // 3D：帧号/总帧的唯一状态源（Inspector 信息页与 IndexStateForTest 共用）。
  lapian::core::TotalFrameCount total_for_ui_;
  MarkPanel* mark_panel_ = nullptr;
  OnlineResolveDialog* online_dialog_ = nullptr;
  QLabel* status_label_ = nullptr;
  QLabel* audio_label_ = nullptr;
  QLabel* speed_label_ = nullptr;
  QLabel* loop_label_ = nullptr;
  QLabel* waveform_label_ = nullptr;
  QLabel* notice_label_ = nullptr;
  // 3D：状态栏第 3 个指标（音频/缓存/波形）。
  QLabel* cache_label_ = nullptr;
  QSlider* volume_slider_ = nullptr;
  QPushButton* cache_settings_button_ = nullptr;
  QAction* mute_action_ = nullptr;
  QAction* slower_action_ = nullptr;
  QAction* faster_action_ = nullptr;
  QTimer* audio_status_timer_ = nullptr;
  QAction* open_action_ = nullptr;
  QAction* online_action_ = nullptr;
  QAction* cache_settings_action_ = nullptr;
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
  // T8.9 清晰度菜单：只列示 DASH 目录 + 当前播放档位；不接 DASH 播放。
  QPushButton* quality_button_ = nullptr;
  QMenu* quality_menu_ = nullptr;
  lapian::app::OnlineQualityCatalog online_quality_;
  bool online_quality_enabled_ = true;
  // B：用户点开菜单后按需拉目录的在途标记（只 GUI 线程）。
  bool online_quality_loading_ = false;
  bool has_online_test_quality_ = false;
  lapian::app::OnlineQualityCatalog online_test_quality_;
  std::shared_ptr<lapian::media::MediaSession> session_;
  std::unique_ptr<lapian::app::OnlineSession> online_;
  std::unique_ptr<lapian::online::CacheStore> online_cache_;
  bool online_cache_ready_ = false;
  QString online_cache_error_;
  // P1：在线会话中保存的新目录/配额，等 idle 后再生效（旧 store 不替换）。
  bool cache_settings_pending_ = false;
  // 设置对话框打开期间绝不替换 store（对话框持有旧 CacheStore*）。
  bool cache_settings_dialog_open_ = false;
  QTimer* cache_maintenance_timer_ = nullptr;
  bool cache_maintenance_enabled_ = true;
  QString cache_last_maintenance_reason_;
  qint64 cache_last_maintenance_removed_bytes_ = 0;
  int cache_last_maintenance_removed_entries_ = 0;
  double cache_last_maintenance_ms_ = 0.0;
  std::unique_ptr<PlaybackController> playback_;
  std::unique_ptr<lapian::media::PreviewDecoder> preview_;
  std::unique_ptr<lapian::media::OnlinePreviewDecoder> online_preview_;
  std::unique_ptr<lapian::audio::AudioEngine> audio_;
  std::unique_ptr<lapian::audio::WaveformGenerator> waveform_;
  std::unique_ptr<lapian::core::MarkStore> marks_;
  std::unique_ptr<lapian::core::SettingsStore> settings_store_;
  std::unique_ptr<SnapshotService> snapshots_;
  lapian::core::LoopRange loop_;
  QTimer* preview_timer_ = nullptr;
  QTimer* step_repeat_timer_ = nullptr;
  QTimer* settings_save_timer_ = nullptr;
  // UI v2（3A）：面板头/循环按钮的轻量刷新在 3B 已改为信号驱动，无定时器。
  // T7.11.1：后退跨 GOP 第一步的硬解窗口延后回拷（呈现后再填缓存）。
  QTimer* cache_fill_timer_ = nullptr;
  // T8.6.1b 在线拖动预览状态/统计（只 GUI 线程写）。
  std::int64_t online_preview_target_ = -1;
  std::int64_t online_preview_last_frame_ = -1;
  std::int64_t online_preview_last_pts_ = 0;
  std::size_t online_preview_index_frames_ = 0;
  int online_preview_presented_ = 0;
  int online_preview_coarse_presented_ = 0;
  int online_preview_cache_presented_ = 0;
  bool online_preview_last_coarse_ = false;
  bool online_preview_last_from_cache_ = false;
  int online_preview_dropped_ = 0;
  int online_preview_errors_ = 0;
  // P0：每次在线拖动的度量（只 GUI 线程写；S1 只记录，不参与判定）。
  QElapsedTimer online_preview_drag_clock_;
  bool online_preview_drag_active_ = false;
  qint64 online_preview_drag_first_delivered_ms_ = -1;
  int online_preview_drag_published_ = 0;
  OnlineDragStatsSnapshot online_drag_stats_;
  // P0：seek/暂停后的延迟预热（单次定时器，120/200ms 合并）。
  QTimer* online_preview_warm_timer_ = nullptr;
  bool online_preview_warm_pending_ = false;
  std::int64_t online_preview_warm_target_ = -1;
  int preview_presented_count_ = 0;
  // A：预览解码器实际配置路径/Begin 次数/空 tick 计数（只 GUI 线程写；诊断用）。
  QString preview_configure_path_;
  QString preview_configure_session_path_;
  qint64 preview_begin_count_ = 0;
  qint64 preview_null_ticks_ = 0;
  // DP-1 本地预览诊断（只 GUI 线程写）：当前目标与最后成功上传的预览帧。
  std::int64_t preview_target_frame_ = -1;
  std::int64_t preview_displayed_frame_ = -1;
  std::int64_t preview_displayed_pts_us_ = 0;
  bool preview_displayed_coarse_ = false;
  bool preview_displayed_from_cache_ = false;
  bool preview_displayed_hardware_ = false;
  // DP-3：策略判定上下文（只 GUI 线程写）与最近一次 Present 的目标滞后。
  lapian::media::DragMode preview_drag_mode_ = lapian::media::DragMode::Exact;
  lapian::media::DragDecision preview_drag_decision_{};
  std::chrono::steady_clock::time_point preview_last_target_time_{};
  bool preview_last_target_time_valid_ = false;
  int preview_last_direction_ = 0;
  int preview_same_direction_run_ = 0;
  qint64 preview_last_step_ = 0;
  qint64 preview_last_interval_ms_ = 0;
  qint64 preview_target_lag_frames_ = 0;
  double preview_target_lag_ms_ = 0.0;
  bool online_preview_wait_status_ = false;
  bool online_preview_error_shown_ = false;
  QElapsedTimer online_preview_wait_timer_;
  int step_direction_ = 0;
  int step_key_physical_down_for_test_ = -1;
  QString step_stop_reason_;
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
  bool online_cache_hit_ = false;
  // C4 打开预热：最近一次预热耗时（-1 = 未预热）；以及“预热是否替用户把画面页
  // 从空态切走”，Start 同步失败时用它恢复空态卡片（错误横幅/状态照旧）。
  qint64 online_prewarm_stage_ms_ = -1;
  bool online_prewarm_switched_stage_ = false;
  bool online_dash_enabled_ = true;
  QString online_cache_path_;
  QString online_owner_;
  double online_fps_ = 0.0;
  bool has_online_test_playurl_ = false;
  qint64 online_test_threshold_ = 0;
  std::unique_ptr<lapian::online::IDownloadTransport> online_test_transport_;
  std::unique_ptr<lapian::online::IHttpClient> online_test_http_;
  std::shared_ptr<lapian::online::PlayUrlData> online_test_playurl_;
  QString online_records_root_for_test_;
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
  // T8.12：当前会话请求（含 requested_qn；sessdata 只内存，不打印/不落盘）。
  lapian::app::OnlineRequest online_request_;
  bool online_logged_in_ = true;
  double online_actual_fps_ = 0.0;
  double online_source_fps_hint_ = 0.0;
  bool online_frame_rate_drop_ = false;
  bool online_switch_pending_ = false;
  int online_switch_target_qn_ = 0;
  int online_switch_target_codecid_ = 0;
  std::int64_t online_switch_target_pts_us_ = 0;
  std::int64_t online_switch_old_frame_ = 0;
  double online_switch_old_fps_ = 0.0;
  QString online_switch_status_;
  // 只作用于下一帧 HTTP seek 状态栏，避免“切档完成/帧号可能变化”被落地提示覆盖。
  QString online_seek_completion_notice_;
  // T8.10 DASH 双输入：音频是独立 GrowingSource/容器；这里保存 session 与 seek
  // 重建所需的轻量引用（生命周期由 OnlineReadyInfo/AudioEngine/Waveform 持有）。
  bool online_dash_audio_ = false;
  std::shared_ptr<lapian::online::GrowingSource> online_audio_source_;
  QString online_audio_part_path_;
  int online_audio_id_ = 0;
  qint64 online_audio_total_bytes_ = 0;
  qint64 online_dash_duration_ms_ = 0;
  double online_dash_fps_ = 0.0;
  int online_stable_frames_ = 0;
  bool online_index_complete_ = false;
  std::vector<lapian::media::OnlineFrameSpan> online_spans_;
  QString online_last_error_;
  std::thread online_index_thread_;
  std::shared_ptr<std::atomic<bool>> online_index_cancel_;
  std::uint64_t online_index_generation_ = 0;
  std::shared_ptr<lapian::media::OnlineIndexResult> online_index_pending_;
  std::uint64_t online_index_pending_generation_ = 0;
  QString online_index_stats_;
  qint64 online_download_throttle_bps_ = 0;
  int waveform_last_bucket_count_ = 0;
  std::thread online_seek_thread_;
  std::shared_ptr<std::atomic<bool>> online_seek_cancel_;
  std::uint64_t online_seek_generation_ = 0;
  bool online_seek_running_ = false;
  // P0 热修：在线异步 seek 落地前按 Space 的“待播”意图。SetPosition() 第一行
  // 会 Pause()，所以不能在 seek 在途时直接 Play；落地回调补 Play 并清标记。
  bool online_pending_play_ = false;
  bool online_session_poisoned_ = false;
  // T8.6.1a：最近一次在线 seek 目标（未落地时步进从该目标 ±1，而不是从旧画面）。
  std::int64_t online_seek_target_ = -1;
  OnlineStepState online_step_;
  QString online_status_detail_;
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
