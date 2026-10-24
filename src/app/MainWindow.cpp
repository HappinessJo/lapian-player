#include "app/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
}

#include "app/CacheSettingsDialog.h"
#include "app/GfxWidget.h"
#include "app/InfoBar.h"
#include "app/MarkPanel.h"
#include "app/OnlineResolveDialog.h"
#include "app/OnlineSession.h"
#include "app/PlaybackController.h"
#include "app/SnapshotImage.h"
#include "app/SnapshotService.h"
#include "app/TimelineWidget.h"
#include "audio/AudioEngine.h"
#include "audio/WaveformGenerator.h"
#include "core/AppSettings.h"
#include "core/FrameMath.h"
#include "core/MediaClock.h"
#include "media/MediaProbe.h"
#include "media/MediaSession.h"
#include "media/OnlinePreviewDecoder.h"
#include "media/PathProbe.h"
#include "media/PreviewDecoder.h"
#include "online/CacheManager.h"
#include "online/CacheStore.h"
#include "online/CredentialStore.h"
#include "online/FrameRatePolicy.h"
#include "online/IHttpClient.h"
#include "online/PlayUrl.h"

namespace {

// T8.3.1：启动后做一次孤儿清理，之后每 6 小时一次。延迟启动清理是为了不把
// 首次扫描压在窗口显示/首帧路径上；测试模式由 main 关闭（见构造函数参数）。
constexpr int kCacheStartupMaintenanceDelayMs = 3000;
constexpr int kCacheMaintenanceIntervalMs = 6 * 60 * 60 * 1000;

QString DescribeAudioChannels(int channels) {
  if (channels == 1) {
    return QStringLiteral("单声道");
  }
  if (channels == 2) {
    return QStringLiteral("立体声");
  }
  return QStringLiteral("%1 声道").arg(channels);
}

QString FormatSpeed(double rate) {
  return QString::number(rate, 'g', 3) + QStringLiteral("x");
}

QString DescribeProbeError(lapian::media::MediaProbeError error) {
  using lapian::media::MediaProbeError;
  switch (error) {
    case MediaProbeError::kFileNotFound:
      return QStringLiteral("文件不存在或路径不可访问");
    case MediaProbeError::kPermissionDenied:
      return QStringLiteral("没有读取该文件的权限");
    case MediaProbeError::kUnsupportedFormat:
      return QStringLiteral("不支持该文件格式");
    case MediaProbeError::kCorrupt:
      return QStringLiteral("文件已损坏或不是完整的视频");
    case MediaProbeError::kNoVideoStream:
      return QStringLiteral("文件里没有视频流");
    case MediaProbeError::kStreamInfoFailed:
      return QStringLiteral("读取视频流信息失败");
    case MediaProbeError::kOpenFailed:
      return QStringLiteral("无法打开文件");
    case MediaProbeError::kNone:
    default:
      return QStringLiteral("无法打开文件");
  }
}

// 把备注中的换行/反斜杠转义成日志里的一行（T5.3 自动化证据）。
QString EscapeForLog(QString text) {
  text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
  text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
  text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
  return text;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent, const QString& settings_root,
                       bool enable_cache_maintenance)
    : QMainWindow(parent) {
  cache_maintenance_enabled_ = enable_cache_maintenance;
  settings_log_ = qEnvironmentVariableIsSet("LAPIAN_SETTINGS_LOG");
  startup_log_ = qEnvironmentVariableIsSet("LAPIAN_STARTUP_LOG");
  // T6.3：构造期间只建立 store；文件读取放在 UI 建好之后、首次打开文件之前，
  // 失败/损坏静默回退默认值。
  settings_store_ =
      std::make_unique<lapian::core::SettingsStore>(settings_root);
  setWindowTitle(QStringLiteral("拉片软件"));
  ApplyInitialWindowSize();
  setAcceptDrops(true);

  auto* central = new QWidget(this);
  auto* central_layout = new QHBoxLayout(central);
  central_layout->setContentsMargins(0, 0, 0, 0);
  central_layout->setSpacing(0);

  stage_ = new QWidget(central);
  stage_->setObjectName(QStringLiteral("videoStage"));
  auto* stage_layout = new QVBoxLayout(stage_);
  stage_layout->setContentsMargins(0, 0, 0, 0);
  stage_layout->setSpacing(0);
  gfx_ = new GfxWidget(stage_);
  timeline_ = new TimelineWidget(stage_);
  info_bar_ = new InfoBar(stage_);
  stage_layout->addWidget(gfx_, 1);
  stage_layout->addWidget(timeline_);
  stage_layout->addWidget(info_bar_);

  mark_panel_ = new MarkPanel(central);
  mark_panel_->setFixedWidth(300);
  central_layout->addWidget(stage_, 1);
  central_layout->addWidget(mark_panel_);
  setCentralWidget(central);

  open_action_ = new QAction(QStringLiteral("打开视频..."), this);
  open_action_->setShortcut(QKeySequence::Open);
  connect(open_action_, &QAction::triggered, this, &MainWindow::OpenDialog);
  addAction(open_action_);

  // T8.1：在线解析入口。只在用户按 Ctrl+U 后创建对话框/联网；本地启动不碰网络层。
  online_action_ = new QAction(QStringLiteral("在线打开..."), this);
  online_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+U")));
  online_action_->setShortcutContext(Qt::WindowShortcut);
  connect(online_action_, &QAction::triggered, this, &MainWindow::OpenOnlineDialog);
  addAction(online_action_);

  // T8.3.1：设置（在线缓存目录/配额/清空/孤儿清理）。快捷键不在 spec §3.7
  // 表内，不改变既有键位；文本输入聚焦时 Qt 仍会把 Ctrl+, 交给本 action。
  cache_settings_action_ = new QAction(QStringLiteral("缓存设置..."), this);
  cache_settings_action_->setObjectName(QStringLiteral("cacheSettingsAction"));
  cache_settings_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
  cache_settings_action_->setShortcutContext(Qt::WindowShortcut);
  connect(cache_settings_action_, &QAction::triggered, this,
          &MainWindow::ShowCacheSettingsDialog);
  addAction(cache_settings_action_);

  // T8.6：在线加载/缓冲中可取消；关窗也走同一条清理路径。快捷键不在 §3.7 表内，
  // 用 Ctrl+Shift+U，并在状态栏提示。
  online_cancel_action_ = new QAction(QStringLiteral("取消在线加载"), this);
  online_cancel_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+U")));
  online_cancel_action_->setShortcutContext(Qt::WindowShortcut);
  online_cancel_action_->setEnabled(false);
  connect(online_cancel_action_, &QAction::triggered, this,
          [this]() { CancelOnlineLoading(); });
  addAction(online_cancel_action_);

  play_action_ = new QAction(QStringLiteral("播放"), this);
  play_action_->setShortcut(Qt::Key_Space);
  play_action_->setShortcutContext(Qt::WindowShortcut);
  play_action_->setEnabled(false);
  connect(play_action_, &QAction::triggered, this, &MainWindow::TogglePlayback);
  addAction(play_action_);

  // 步进键由 GfxWidget 自己处理（300ms 后 70ms 连发）；不再设 QAction 快捷键，
  // 避免 QAction 与 GfxWidget 双触发。
  prev_action_ = new QAction(QStringLiteral("上一帧"), this);
  prev_action_->setEnabled(false);
  connect(prev_action_, &QAction::triggered, this, &MainWindow::StepBackward);
  addAction(prev_action_);

  next_action_ = new QAction(QStringLiteral("下一帧"), this);
  next_action_->setEnabled(false);
  connect(next_action_, &QAction::triggered, this, &MainWindow::StepForward);
  addAction(next_action_);

  goto_action_ = new QAction(QStringLiteral("跳到帧..."), this);
  goto_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
  goto_action_->setShortcutContext(Qt::WindowShortcut);
  goto_action_->setEnabled(false);
  connect(goto_action_, &QAction::triggered, this, &MainWindow::ShowGotoDialog);
  addAction(goto_action_);

  zoom_in_action_ = new QAction(QStringLiteral("放大时间轴"), this);
  // T6.2：'+'/'-' 由窗口级事件过滤器统一处理（见 HandleGlobalKey），
  // 不再注册 QAction 快捷键，避免不同键盘布局/Qt 组合产生双触发。
  zoom_in_action_->setShortcutContext(Qt::WindowShortcut);
  zoom_in_action_->setEnabled(false);
  connect(zoom_in_action_, &QAction::triggered, this, &MainWindow::ZoomTimelineIn);
  addAction(zoom_in_action_);

  zoom_out_action_ = new QAction(QStringLiteral("缩小时间轴"), this);
  // 同 zoom_in：'-' 由 HandleGlobalKey 处理。
  zoom_out_action_->setShortcutContext(Qt::WindowShortcut);
  zoom_out_action_->setEnabled(false);
  connect(zoom_out_action_, &QAction::triggered, this, &MainWindow::ZoomTimelineOut);
  addAction(zoom_out_action_);

  waveform_action_ = new QAction(QStringLiteral("显示波形"), this);
  waveform_action_->setShortcut(Qt::Key_W);
  waveform_action_->setShortcutContext(Qt::WindowShortcut);
  waveform_action_->setCheckable(true);
  waveform_action_->setChecked(true);
  waveform_action_->setEnabled(false);
  connect(waveform_action_, &QAction::toggled, this, &MainWindow::ToggleWaveform);
  addAction(waveform_action_);

  slower_action_ = new QAction(QStringLiteral("减速"), this);
  slower_action_->setShortcut(Qt::Key_BracketLeft);
  slower_action_->setShortcutContext(Qt::WindowShortcut);
  slower_action_->setEnabled(false);
  connect(slower_action_, &QAction::triggered, this, &MainWindow::SpeedDown);
  addAction(slower_action_);

  faster_action_ = new QAction(QStringLiteral("加速"), this);
  faster_action_->setShortcut(Qt::Key_BracketRight);
  faster_action_->setShortcutContext(Qt::WindowShortcut);
  faster_action_->setEnabled(false);
  connect(faster_action_, &QAction::triggered, this, &MainWindow::SpeedUp);
  addAction(faster_action_);

  // T4.1 A/B：I 设入点、O 设出点、L 开关循环、Shift+L 清除。
  loop_in_action_ = new QAction(QStringLiteral("设置入点 A"), this);
  loop_in_action_->setShortcut(Qt::Key_I);
  loop_in_action_->setShortcutContext(Qt::WindowShortcut);
  loop_in_action_->setEnabled(false);
  connect(loop_in_action_, &QAction::triggered, this, &MainWindow::SetLoopIn);
  addAction(loop_in_action_);

  loop_out_action_ = new QAction(QStringLiteral("设置出点 B"), this);
  loop_out_action_->setShortcut(Qt::Key_O);
  loop_out_action_->setShortcutContext(Qt::WindowShortcut);
  loop_out_action_->setEnabled(false);
  connect(loop_out_action_, &QAction::triggered, this, &MainWindow::SetLoopOut);
  addAction(loop_out_action_);

  loop_toggle_action_ = new QAction(QStringLiteral("循环开关"), this);
  loop_toggle_action_->setShortcut(Qt::Key_L);
  loop_toggle_action_->setShortcutContext(Qt::WindowShortcut);
  loop_toggle_action_->setEnabled(false);
  connect(loop_toggle_action_, &QAction::triggered, this, &MainWindow::ToggleLoop);
  addAction(loop_toggle_action_);

  loop_clear_action_ = new QAction(QStringLiteral("清除 A/B"), this);
  loop_clear_action_->setShortcut(QKeySequence(QStringLiteral("Shift+L")));
  loop_clear_action_->setShortcutContext(Qt::WindowShortcut);
  loop_clear_action_->setEnabled(false);
  connect(loop_clear_action_, &QAction::triggered, this, &MainWindow::ClearLoop);
  addAction(loop_clear_action_);

  // T5.2 标记：Ctrl+M 在当前帧添加；列表点击/删除由 MarkPanel 发信号。
  mark_action_ = new QAction(QStringLiteral("添加标记"), this);
  mark_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
  mark_action_->setShortcutContext(Qt::WindowShortcut);
  mark_action_->setEnabled(false);
  connect(mark_action_, &QAction::triggered, this, &MainWindow::AddMarkAtCurrentFrame);
  addAction(mark_action_);

  // T5.4 截图：S 在按键瞬间引用当前已显示帧，编码/写盘在 SnapshotService 工作线程。
  snapshot_action_ = new QAction(QStringLiteral("截图"), this);
  snapshot_action_->setShortcut(Qt::Key_S);
  snapshot_action_->setShortcutContext(Qt::WindowShortcut);
  snapshot_action_->setEnabled(false);
  connect(snapshot_action_, &QAction::triggered, this, &MainWindow::CaptureSnapshot);
  addAction(snapshot_action_);

  // T6.1 全屏：F 切换；Esc 只在全屏时启用，且文本输入聚焦时会让位。
  fullscreen_action_ = new QAction(QStringLiteral("全屏"), this);
  fullscreen_action_->setShortcut(Qt::Key_F);
  fullscreen_action_->setShortcutContext(Qt::WindowShortcut);
  connect(fullscreen_action_, &QAction::triggered, this, &MainWindow::ToggleFullScreen);
  addAction(fullscreen_action_);

  exit_fullscreen_action_ = new QAction(QStringLiteral("退出全屏"), this);
  exit_fullscreen_action_->setShortcut(Qt::Key_Escape);
  exit_fullscreen_action_->setShortcutContext(Qt::WindowShortcut);
  exit_fullscreen_action_->setEnabled(false);
  connect(exit_fullscreen_action_, &QAction::triggered, this, &MainWindow::ExitFullScreen);
  addAction(exit_fullscreen_action_);

  connect(mark_panel_, &MarkPanel::markActivated, this,
          [this](const QString& id, qint64 frame) {
            ActivateMark(id, static_cast<std::int64_t>(frame));
          });
  connect(mark_panel_, &MarkPanel::deleteRequested, this,
          [this](const QString& id) { ApplyDeleteMark(id, true); });
  connect(mark_panel_, &MarkPanel::markTextEdited, this,
          &MainWindow::ApplyMarkTextEdited);

  snapshot_log_ = qEnvironmentVariableIsSet("LAPIAN_SNAPSHOT_LOG");
  snapshots_ = std::make_unique<SnapshotService>(
      qEnvironmentVariable("LAPIAN_SNAPSHOT_PICTURES_DIR"));
  connect(snapshots_.get(), &SnapshotService::snapshotSaved, this,
          [this](const QString& path, bool used_fallback, qint64 frame_index,
                 qint64 encode_ms) {
            snapshot_saved_paths_.push_back(path);
            snapshot_last_path_ = path;
            snapshot_last_error_.clear();
            snapshot_last_fallback_ = used_fallback;
            snapshot_last_saved_frame_ = frame_index;
            snapshot_save_samples_.push_back(static_cast<double>(encode_ms));
            const QString native = QDir::toNativeSeparators(path);
            if (used_fallback) {
              statusBar()->showMessage(
                  QStringLiteral("视频目录不可写，截图已保存到图片目录：%1").arg(native),
                  8000);
            } else {
              statusBar()->showMessage(QStringLiteral("截图已保存：%1").arg(native), 5000);
            }
            if (snapshot_log_) {
              std::fprintf(stderr,
                           "T5.4 snapshot_saved path=%s frame=%lld fallback=%d "
                           "encode_ms=%lld\n",
                           path.toUtf8().constData(),
                           static_cast<long long>(frame_index),
                           used_fallback ? 1 : 0,
                           static_cast<long long>(encode_ms));
              std::fflush(stderr);
            }
          });
  connect(snapshots_.get(), &SnapshotService::snapshotFailed, this,
          [this](const QString& error) {
            snapshot_last_error_ = error;
            statusBar()->showMessage(QStringLiteral("截图失败：%1").arg(error), 8000);
            qWarning() << "截图失败:" << error;
            if (snapshot_log_) {
              std::fprintf(stderr, "T5.4 snapshot_failed error=%s\n",
                           error.toUtf8().constData());
              std::fflush(stderr);
            }
          });
  connect(snapshots_.get(), &SnapshotService::snapshotIndexWarning, this,
          [this](const QString& warning) {
            // 截图 PNG 已保存；只是 index.jsonl 追加失败。可见但不回滚截图。
            statusBar()->showMessage(
                QStringLiteral("截图清单写入失败：%1").arg(warning), 10000);
            if (snapshot_log_) {
              std::fprintf(stderr, "T8.7 snapshot_index_warning error=%s\n",
                           warning.toUtf8().constData());
              std::fflush(stderr);
            }
          });
  displayed_frame_ = av_frame_alloc();

  playback_ = std::make_unique<PlaybackController>(this);
  connect(playback_.get(), &PlaybackController::frameReady, this, &MainWindow::OnFrameReady);
  connect(playback_.get(), &PlaybackController::stateChanged, this,
          &MainWindow::OnPlaybackStateChanged);
  connect(playback_.get(), &PlaybackController::reachedEnd, this, &MainWindow::OnPlaybackEnded);
  connect(playback_.get(), &PlaybackController::loopJumped, this, &MainWindow::OnLoopJumped);
  connect(playback_.get(), &PlaybackController::bufferingChanged, this,
          [this](bool buffering, int frame, const QString& reason) {
            if (buffering) {
              ++online_buffer_event_count_;
              if (audio_) {
                audio_->Pause();
              }
              play_action_->setText(QStringLiteral("缓冲中"));
              statusBar()->showMessage(
                  QStringLiteral("缓冲中：第 %1 帧（自动暂停）%2")
                      .arg(frame)
                      .arg(reason.isEmpty() ? QString() : QStringLiteral(" · ") + reason),
                  12000);
              status_label_->setText(base_info_ +
                                     QStringLiteral(" · 缓冲中（自动暂停）"));
            } else {
              play_action_->setText(QStringLiteral("暂停"));
              statusBar()->showMessage(QStringLiteral("缓冲恢复，从当前帧继续"), 3000);
              status_label_->setText(base_info_ + QStringLiteral(" · 在线播放"));
              if (audio_) {
                audio_->PlayFrom(playback_->CurrentMediaSeconds());
              }
            }
          });

  preview_timer_ = new QTimer(this);
  preview_timer_->setInterval(16);
  preview_timer_->setTimerType(Qt::PreciseTimer);
  connect(preview_timer_, &QTimer::timeout, this, &MainWindow::OnPreviewTick);

  // T6.5：InfoBar 只显示一条横幅；其余状态提示汇总到这里，常驻状态栏。
  notice_label_ = new QLabel(this);
  notice_label_->setObjectName(QStringLiteral("noticeInfo"));
  notice_label_->setStyleSheet(QStringLiteral("color:#e8c46a;"));
  notice_label_->setMaximumWidth(240);
  notice_label_->setToolTip(QStringLiteral("其他状态提示"));
  notice_label_->hide();
  statusBar()->addPermanentWidget(notice_label_);

  audio_label_ = new QLabel(QStringLiteral("无音频"), this);
  audio_label_->setObjectName(QStringLiteral("audioInfo"));
  audio_label_->setToolTip(QStringLiteral("音频轨道与输出状态"));
  statusBar()->addPermanentWidget(audio_label_);

  speed_label_ = new QLabel(QStringLiteral("1x"), this);
  speed_label_->setObjectName(QStringLiteral("speedInfo"));
  speed_label_->setToolTip(QStringLiteral("播放速度（[ / ] 调整）"));
  statusBar()->addPermanentWidget(speed_label_);

  loop_label_ = new QLabel(QStringLiteral("循环：关"), this);
  loop_label_->setObjectName(QStringLiteral("loopInfo"));
  loop_label_->setToolTip(
      QStringLiteral("A/B 循环：I 入点 · O 出点 · L 开关 · Shift+L 清除"));
  statusBar()->addPermanentWidget(loop_label_);

  waveform_label_ = new QLabel(QStringLiteral("波形：—"), this);
  waveform_label_->setObjectName(QStringLiteral("waveformInfo"));
  waveform_label_->setToolTip(QStringLiteral("音频波形生成状态"));
  statusBar()->addPermanentWidget(waveform_label_);

  volume_slider_ = new QSlider(Qt::Horizontal, this);
  volume_slider_->setObjectName(QStringLiteral("volumeSlider"));
  volume_slider_->setRange(0, 100);
  volume_slider_->setValue(volume_);
  volume_slider_->setFixedWidth(110);
  volume_slider_->setEnabled(false);
  volume_slider_->setToolTip(QStringLiteral("音量 %1%").arg(volume_));
  connect(volume_slider_, &QSlider::valueChanged, this, &MainWindow::OnVolumeChanged);
  statusBar()->addPermanentWidget(volume_slider_);

  // T8.3.1：可见的“设置”入口（Ctrl+, 同一路径）；不新增主菜单/不改变 v1 快捷键。
  cache_settings_button_ = new QPushButton(QStringLiteral("设置"), this);
  cache_settings_button_->setObjectName(QStringLiteral("cacheSettingsButton"));
  cache_settings_button_->setFlat(true);
  cache_settings_button_->setToolTip(QStringLiteral("在线缓存设置（Ctrl+,）"));
  connect(cache_settings_button_, &QPushButton::clicked, this,
          &MainWindow::ShowCacheSettingsDialog);
  statusBar()->addPermanentWidget(cache_settings_button_);

  // T8.9：清晰度入口。只列示 playurl 实际返回的档位；DASH 播放/切档属
  // T8.10/T8.12，本任务不假装切换。本地模式隐藏。
  // 注意：不要用 QPushButton::setMenu()——实测（T8.9）状态栏里的带菜单按钮首次
  // 绘制会阻塞 UI ~620ms（M1 E2E 的 ui_gap 从 62ms 变成 679ms）。这里用普通按钮
  // + 点击时弹出 QMenu（菜单按需创建），保持首帧路径不碰菜单绘制。
  quality_button_ = new QPushButton(QStringLiteral("清晰度"), this);
  quality_button_->setObjectName(QStringLiteral("onlineQualityButton"));
  quality_button_->setFlat(true);
  quality_button_->setToolTip(
      QStringLiteral("在线清晰度（以 playurl 实际返回为准；T8.9 只列档）"));
  quality_button_->setVisible(false);
  quality_button_->setEnabled(false);
  connect(quality_button_, &QPushButton::clicked, this,
          &MainWindow::ShowOnlineQualityMenu);
  statusBar()->addPermanentWidget(quality_button_);

  mute_action_ = new QAction(QStringLiteral("静音"), this);
  mute_action_->setShortcut(Qt::Key_M);
  mute_action_->setShortcutContext(Qt::WindowShortcut);
  mute_action_->setCheckable(true);
  mute_action_->setEnabled(false);
  connect(mute_action_, &QAction::triggered, this, &MainWindow::ToggleMute);
  addAction(mute_action_);

  status_label_ = new QLabel(this);
  status_label_->setObjectName(QStringLiteral("statusInfo"));
  status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  statusBar()->addPermanentWidget(status_label_);

  connect(info_bar_, &InfoBar::noticesChanged, this, &MainWindow::UpdateStatusNotices);
  UpdateStatusNotices();

  audio_status_timer_ = new QTimer(this);
  audio_status_timer_->setInterval(200);
  connect(audio_status_timer_, &QTimer::timeout, this, &MainWindow::PollAudioStatus);
  connect(audio_status_timer_, &QTimer::timeout, this, &MainWindow::PollWaveformStatus);
  audio_status_timer_->start();

  base_info_ = QStringLiteral("Qt %1 · FFmpeg %2").arg(QString::fromLatin1(qVersion()),
                                                       QString::fromLatin1(av_version_info()));

  // T6.3：音量/波形跨会话。变更去抖 250ms 落盘；关窗/析构再 flush 一次。
  // 关窗保存不进入 SnapshotService::Drain 的等待链。
  settings_save_timer_ = new QTimer(this);
  settings_save_timer_->setSingleShot(true);
  settings_save_timer_->setInterval(250);
  connect(settings_save_timer_, &QTimer::timeout, this, [this]() { FlushSettings(); });
  LoadSettingsIntoUi();

  // T8.3.1：构造 CacheStore 本身不碰磁盘；真正 SetRoot/清理延后到启动定时器，
  // 避免把默认 AppData 写操作压在窗口显示路径上。测试模式由 main 关闭。
  InitializeOnlineCacheStore();
  cache_maintenance_timer_ = new QTimer(this);
  cache_maintenance_timer_->setInterval(kCacheMaintenanceIntervalMs);
  connect(cache_maintenance_timer_, &QTimer::timeout, this,
          [this]() { RunCacheMaintenance("periodic"); });
  if (cache_maintenance_enabled_) {
    cache_maintenance_timer_->start();
    QTimer::singleShot(kCacheStartupMaintenanceDelayMs, this,
                       [this]() { RunCacheMaintenance("startup"); });
  }

  ShowEmptyState();

  connect(gfx_, &GfxWidget::deviceReady, this, &MainWindow::OnDeviceReady);
  connect(gfx_, &GfxWidget::deviceError, this, &MainWindow::OnDeviceError, Qt::QueuedConnection);
  // T7.1：把首帧真正 Present 的时刻转给 main 的冷启动埋点。
  connect(gfx_, &GfxWidget::framePresented, this, &MainWindow::framePresented);
  connect(gfx_, &GfxWidget::framePresented, this,
          [this]() { ++presented_frame_count_; });
  connect(gfx_, &GfxWidget::playPauseRequested, this, &MainWindow::TogglePlayback);
  connect(gfx_, &GfxWidget::gotoRequested, this, &MainWindow::ShowGotoDialog);
  connect(gfx_, &GfxWidget::fullScreenToggleRequested, this, &MainWindow::ToggleFullScreen);
  // 视频区是独立原生 HWND，主窗口的 drop 到不了这里；由 GfxWidget 转发。
  connect(gfx_, &GfxWidget::localFileDropped, this, &MainWindow::OpenFile);
  // Esc 的可用性跟随“全屏 + 是否在文本框内”：编辑备注时 Esc 仍交给文本框，
  // 不在备注框里退出全屏（T5.3 的焦点让位语义）。
  connect(qApp, &QApplication::focusChanged, this,
          [this](QWidget*, QWidget*) { UpdateFullScreenActionState(); });

  // T6.2：步进/首末帧提升为窗口级快捷键。一个连发源（300ms 后 70ms），
  // 忽略 OS 自动重复；焦点切走、窗口失活、松开按键都停。
  step_repeat_timer_ = new QTimer(this);
  step_repeat_timer_->setTimerType(Qt::PreciseTimer);
  connect(step_repeat_timer_, &QTimer::timeout, this, [this]() {
    if (step_direction_ == 0) {
      return;
    }
    if (step_repeat_timer_->interval() != 70) {
      step_repeat_timer_->setInterval(70);
    }
    // T8.6.1a：在线 seek 是异步的，不能用 current_frame_ == before 判边界；
    // 状态机自己判断边界/排队，这里只负责按 tick 推进并在边界停连发。
    if (OnlineStepEligible()) {
      const bool keep_going = ApplyOnlineStep(step_direction_);
      if (qEnvironmentVariableIsSet("LAPIAN_KEY_LOG")) {
        std::fprintf(stderr,
                     "T8.6.1a online_step_tick dir=%d desired=%lld frame=%lld "
                     "keep=%d active=%d last=%lld stable=%lld eligible=%d "
                     "inflight=%d target=%lld seek_running=%d queued=%d "
                     "retargets=%d seeks=%d\n",
                     step_direction_,
                     static_cast<long long>(online_step_.desired),
                     static_cast<long long>(current_frame_), keep_going ? 1 : 0,
                     step_repeat_timer_->isActive() ? 1 : 0,
                     static_cast<long long>(OnlineLastFrame()),
                     static_cast<long long>(online_stable_frames_),
                     OnlineStepEligible() ? 1 : 0,
                     online_step_.inflight ? 1 : 0,
                     static_cast<long long>(online_step_.inflight_target),
                     online_seek_running_ ? 1 : 0, online_step_.queued ? 1 : 0,
                     online_step_.retargets, online_step_.seeks);
        std::fflush(stderr);
      }
      if (!keep_going) {
        StopStepRepeat("tick_keep_false");
      }
      return;
    }
    const std::int64_t before = current_frame_;
    GoToFrameForStep(before + step_direction_, step_direction_ < 0);
    if (qEnvironmentVariableIsSet("LAPIAN_KEY_LOG")) {
      std::fprintf(stderr, "T6.2 step_timer before=%lld after=%lld dir=%d active=%d\n",
                   static_cast<long long>(before),
                   static_cast<long long>(current_frame_), step_direction_,
                   step_repeat_timer_->isActive() ? 1 : 0);
      std::fflush(stderr);
    }
    if (current_frame_ == before) {
      StopStepRepeat("local_boundary");  // 到边界：不再重复 seek 同一帧
    }
  });
  connect(qApp, &QApplication::focusChanged, this,
          [this](QWidget* old_widget, QWidget* now_widget) {
            if (qEnvironmentVariableIsSet("LAPIAN_KEY_LOG")) {
              std::fprintf(stderr,
                           "T8.6.1a focus_changed old=%s new=%s dir=%d "
                           "frame=%lld\n",
                           old_widget != nullptr
                               ? old_widget->metaObject()->className()
                               : "null",
                           now_widget != nullptr
                               ? now_widget->metaObject()->className()
                               : "null",
                           step_direction_,
                           static_cast<long long>(current_frame_));
              std::fflush(stderr);
            }
            StopStepRepeat("focus_changed");
          });
  connect(qApp, &QGuiApplication::applicationStateChanged, this,
          [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive) {
              StopStepRepeat("app_inactive");
            }
          });
  qApp->installEventFilter(this);

  // T7.11.1：跨 GOP 后退第一步先把硬解窗口作为引用保留，目标帧呈现后再按块
  // 回拷进 FrameCache。0ms 定时器让按键/绘制事件先执行，回拷不进入 P7 计时。
  cache_fill_timer_ = new QTimer(this);
  cache_fill_timer_->setInterval(0);
  connect(cache_fill_timer_, &QTimer::timeout, this, &MainWindow::FillFrameCacheStep);

  connect(timeline_, &TimelineWidget::scrubStarted, this, &MainWindow::OnScrubStarted);
  connect(timeline_, &TimelineWidget::scrubTargetChanged, this,
          &MainWindow::OnScrubTargetChanged);
  connect(timeline_, &TimelineWidget::scrubFinished, this, &MainWindow::OnScrubFinished);

  UpdateResponsiveLayout();
}

MainWindow::~MainWindow() {
  // T8.6：兜底关闭在线下载/reader（正常关窗已在 closeEvent 里做过）。
  if (online_mode_ || online_) {
    ShutdownOnlineSession(true);
  }
  // T7.1：先取消并 join 索引线程（每包检查取消，通常毫秒级返回），再释放会话。
  CancelIndexBuild();
  // 先落盘设置（不等待截图/标记），再停掉可能还在跑的步进连发并摘掉应用级过滤器。
  FlushSettings();
  StopStepRepeat("shutdown");
  if (QCoreApplication::instance() != nullptr) {
    QCoreApplication::instance()->removeEventFilter(this);
  }
  // 再等工作线程结束，最后释放它可能引用的解码器/帧资源。
  if (snapshots_ != nullptr) {
    snapshots_->Drain();
  }
  av_frame_free(&displayed_frame_);
}

void MainWindow::LoadSettingsIntoUi() {
  if (settings_store_ == nullptr) {
    return;
  }
  QElapsedTimer timer;
  timer.start();
  const lapian::core::SettingsLoadResult result = settings_store_->Load();
  settings_last_load_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  settings_load_status_ = lapian::core::SettingsLoadStatusName(result.status);
  // T8.3：保留读到的缓存键；保存时只覆盖音量/波形，避免把 cache_root/quota 冲掉。
  loaded_settings_ = result.settings;
  ApplySettings(result.settings);
  if (settings_log_) {
    const QByteArray path = settings_store_->file_path().toUtf8();
    std::fprintf(stderr,
                 "T6.3 settings_load path=%s status=%s volume=%d waveform=%d "
                 "load_ms=%.3f\n",
                 path.constData(), settings_load_status_.toUtf8().constData(),
                 result.settings.volume, result.settings.waveform_visible ? 1 : 0,
                 settings_last_load_ms_);
    std::fflush(stderr);
  }
}

void MainWindow::ApplySettings(const lapian::core::AppSettings& settings) {
  const bool previous = applying_settings_;
  applying_settings_ = true;
  const int volume = std::clamp(settings.volume, 0, 100);
  if (volume_slider_ != nullptr) {
    // 走真实 valueChanged 路径，同步 volume_/tooltip/音频引擎（此时通常还没有音频）。
    volume_slider_->setValue(volume);
  } else {
    volume_ = volume;
  }
  if (waveform_action_ != nullptr) {
    // 走真实 toggled 路径，同步 waveform_visible_ 与时间轴波形带。
    waveform_action_->setChecked(settings.waveform_visible);
  } else {
    waveform_visible_ = settings.waveform_visible;
    if (timeline_ != nullptr) {
      timeline_->SetWaveformVisible(settings.waveform_visible);
    }
  }
  applying_settings_ = previous;
  settings_dirty_ = false;
}

void MainWindow::ScheduleSettingsSave() {
  if (applying_settings_) {
    return;
  }
  settings_dirty_ = true;
  if (settings_save_timer_ != nullptr) {
    settings_save_timer_->start();
  }
}

bool MainWindow::FlushSettings() {
  if (settings_save_timer_ != nullptr) {
    settings_save_timer_->stop();
  }
  if (settings_store_ == nullptr) {
    return false;
  }
  if (!settings_dirty_) {
    return true;
  }
  QElapsedTimer timer;
  timer.start();
  // T8.3：只改音量/波形，保留 cache_root/cache_quota_gb（避免把在线缓存设置冲掉）。
  lapian::core::AppSettings settings =
      lapian::core::MergeVolumeWaveform(loaded_settings_, volume_, waveform_visible_);
  const bool ok = settings_store_->Save(settings);
  settings_last_save_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  if (ok) {
    settings_dirty_ = false;
    loaded_settings_ = settings;
  }
  if (settings_log_) {
    const QByteArray path = settings_store_->file_path().toUtf8();
    std::fprintf(stderr,
                 "T6.3 settings_save path=%s ok=%d volume=%d waveform=%d "
                 "save_ms=%.3f\n",
                 path.constData(), ok ? 1 : 0, volume_, waveform_visible_ ? 1 : 0,
                 settings_last_save_ms_);
    std::fflush(stderr);
  }
  return ok;
}

void MainWindow::ApplyInitialWindowSize() {
  QScreen* screen = QGuiApplication::primaryScreen();
  if (screen == nullptr) {
    resize(1280, 720);
    return;
  }
  const QRect available = screen->availableGeometry();
  // 1080p@200% 的逻辑可用区约 960×540：留出标题栏/任务栏余量，避免窗口超出屏幕。
  const int target_width = std::min(1280, std::max(480, available.width() - 32));
  const int target_height = std::min(720, std::max(360, available.height() - 64));
  resize(target_width, target_height);
  // 如果子控件最小尺寸把 resize 顶大，用实际尺寸重新居中，避免 frame 越界。
  const QSize actual = size();
  move(available.x() + (available.width() - actual.width()) / 2,
       available.y() + (available.height() - actual.height()) / 2);
}

void MainWindow::UpdateResponsiveLayout() {
  if (mark_panel_ != nullptr) {
    // 100% 保持 300；高 DPI/窄窗口逐步让到 240，给画面与信息栏留空间。
    const int target = std::clamp(static_cast<int>(width() * 0.26), 240, 300);
    if (mark_panel_->width() != target) {
      mark_panel_->setFixedWidth(target);
    }
  }
  if (status_label_ != nullptr) {
    // Qt/FFmpeg 版本是次要信息；窄窗口先让位，避免状态栏把最小宽度撑大。
    status_label_->setVisible(width() >= 1000);
  }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  UpdateResponsiveLayout();
}

QSize MainWindow::AvailableScreenForTest() const {
  QScreen* screen = this->screen();
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  return screen == nullptr ? QSize() : screen->availableGeometry().size();
}

QString MainWindow::InfoFileLabelForTest() const {
  return info_bar_ == nullptr ? QString() : info_bar_->FileLabelTextForTest();
}

QString MainWindow::InfoFileNameForTest() const {
  return info_bar_ == nullptr ? QString() : info_bar_->FileNameForTest();
}

bool MainWindow::InfoNarrowModeForTest() const {
  return info_bar_ != nullptr && info_bar_->NarrowModeForTest();
}

void MainWindow::SetBannerNoticesForTest(const QStringList& kinds) {
  if (info_bar_ != nullptr) {
    info_bar_->SetSyntheticNoticesForTest(kinds);
  }
}

void MainWindow::ClearBannerNoticesForTest() {
  if (info_bar_ != nullptr) {
    info_bar_->ClearSyntheticNoticesForTest();
  }
}

QString MainWindow::BannerStateForTest() const {
  if (info_bar_ == nullptr) {
    return QStringLiteral("none");
  }
  const QRect banner = info_bar_->BannerGeometryForTest();
  const int banner_count =
      info_bar_->findChildren<QLabel*>(QStringLiteral("infoBanner")).size();
  return QStringLiteral(
             "kind=%1 visible=%2 banners=%3 text=%4 banner=%5,%6 %7x%8 info_h=%9 "
             "secondary=%10 active=%11")
      .arg(info_bar_->BannerKindForTest())
      .arg(info_bar_->BannerVisibleForTest() ? 1 : 0)
      .arg(banner_count)
      .arg(info_bar_->BannerTextForTest())
      .arg(banner.x())
      .arg(banner.y())
      .arg(banner.width())
      .arg(banner.height())
      .arg(info_bar_->height())
      .arg(info_bar_->SecondarySummaryForTest())
      .arg(info_bar_->ActiveNoticeKindsForTest().join(QLatin1Char(',')));
}

QString MainWindow::BannerUiStateForTest() const {
  return QStringLiteral("timeline=%1 frame=%2 play=%3 volume=%4 loop=%5 notice=%6")
      .arg(timeline_ != nullptr && timeline_->isVisible() ? 1 : 0)
      .arg(InfoFrameTextForTest())
      .arg(play_action_ != nullptr && play_action_->isEnabled() ? 1 : 0)
      .arg(volume_slider_ != nullptr && volume_slider_->isVisible() ? 1 : 0)
      .arg(loop_label_ != nullptr && loop_label_->isVisible() ? 1 : 0)
      .arg(notice_label_ != nullptr && notice_label_->isVisible() ? 1 : 0);
}

QString MainWindow::SecondaryStatusTextForTest() const {
  return notice_label_ == nullptr ? QString() : notice_label_->text();
}

int MainWindow::InfoBarHeightForTest() const {
  return info_bar_ == nullptr ? 0 : info_bar_->height();
}

void MainWindow::UpdateStatusNotices() {
  if (notice_label_ == nullptr || info_bar_ == nullptr) {
    return;
  }
  const QString summary = info_bar_->SecondarySummaryForTest();
  const QString tooltip = info_bar_->SecondaryTooltipForTest();
  const int available = std::max(0, notice_label_->maximumWidth() - 6);
  const QString shown =
      summary.isEmpty()
          ? QString()
          : notice_label_->fontMetrics().elidedText(summary, Qt::ElideRight, available);
  notice_label_->setText(shown);
  notice_label_->setToolTip(tooltip.isEmpty()
                                ? QStringLiteral("其他状态提示")
                                : tooltip);
  notice_label_->setVisible(!summary.isEmpty());
}

QString MainWindow::LayoutStateForTest() const {
  auto rect_text = [](const QRect& rect) {
    return QStringLiteral("%1,%2 %3x%4")
        .arg(rect.x())
        .arg(rect.y())
        .arg(rect.width())
        .arg(rect.height());
  };
  QScreen* screen = this->screen();
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  const QRect available =
      screen == nullptr ? QRect() : screen->availableGeometry();
  const QSize min_hint = minimumSizeHint();
  QStringList parts;
  parts << QStringLiteral("dpr=%1").arg(devicePixelRatioF(), 0, 'f', 2);
  parts << QStringLiteral("window=%1x%2").arg(width()).arg(height());
  parts << QStringLiteral("frame=%1").arg(rect_text(frameGeometry()));
  parts << QStringLiteral("available=%1").arg(rect_text(available));
  parts << QStringLiteral("min=%1x%2").arg(min_hint.width()).arg(min_hint.height());
  if (stage_ != nullptr) {
    parts << QStringLiteral("stage=%1").arg(rect_text(stage_->geometry()));
  }
  if (mark_panel_ != nullptr) {
    parts << QStringLiteral("panel=%1").arg(rect_text(mark_panel_->geometry()));
  }
  if (gfx_ != nullptr) {
    parts << QStringLiteral("gfx=%1").arg(rect_text(gfx_->geometry()));
  }
  if (timeline_ != nullptr) {
    parts << QStringLiteral("timeline=%1").arg(rect_text(timeline_->geometry()));
  }
  if (info_bar_ != nullptr) {
    parts << QStringLiteral("info=%1").arg(rect_text(info_bar_->geometry()));
    parts << QStringLiteral("info_narrow=%1")
                 .arg(info_bar_->NarrowModeForTest() ? 1 : 0);
    for (QLabel* label : info_bar_->findChildren<QLabel*>()) {
      if (label->isVisible()) {
        parts << QStringLiteral("info_%1=%2")
                     .arg(label->objectName(), rect_text(label->geometry()));
      }
    }
  }
  if (statusBar() != nullptr) {
    parts << QStringLiteral("status=%1").arg(rect_text(statusBar()->geometry()));
    for (QWidget* widget : {static_cast<QWidget*>(notice_label_),
                            static_cast<QWidget*>(audio_label_),
                            static_cast<QWidget*>(speed_label_),
                            static_cast<QWidget*>(loop_label_),
                            static_cast<QWidget*>(waveform_label_),
                            static_cast<QWidget*>(volume_slider_),
                            static_cast<QWidget*>(status_label_)}) {
      if (widget != nullptr && widget->isVisible()) {
        parts << QStringLiteral("status_%1=%2")
                     .arg(widget->objectName(), rect_text(widget->geometry()));
      }
    }
  }
  return parts.join(QLatin1Char(' '));
}

QStringList MainWindow::LayoutProblemsForTest() const {
  QStringList problems;
  QScreen* screen = this->screen();
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  const QRect available =
      screen == nullptr ? QRect() : screen->availableGeometry();
  const QRect screen_geometry =
      screen == nullptr ? QRect() : screen->geometry();
  if (isFullScreen()) {
    if (!screen_geometry.isEmpty() && size() != screen_geometry.size()) {
      problems << QStringLiteral("fullscreen-size");
    }
  } else if (!available.isEmpty()) {
    if (!available.contains(frameGeometry())) {
      problems << QStringLiteral("window-outside-screen");
    }
    const QSize min_hint = minimumSizeHint();
    if (min_hint.width() > available.width()) {
      problems << QStringLiteral("min-width-exceeds-screen");
    }
    if (min_hint.height() > available.height()) {
      problems << QStringLiteral("min-height-exceeds-screen");
    }
  }
  if (centralWidget() == nullptr || stage_ == nullptr || mark_panel_ == nullptr) {
    problems << QStringLiteral("missing-widgets");
    return problems;
  }
  if (!centralWidget()->rect().contains(stage_->geometry())) {
    problems << QStringLiteral("stage-outside-central");
  }
  if (!centralWidget()->rect().contains(mark_panel_->geometry())) {
    problems << QStringLiteral("panel-outside-central");
  }
  if (stage_->geometry().right() >= mark_panel_->geometry().left()) {
    problems << QStringLiteral("stage-panel-overlap");
  }
  if (mark_panel_->width() < 240 || mark_panel_->width() > 300) {
    problems << QStringLiteral("panel-width");
  }
  if (stage_->width() < 420) {
    problems << QStringLiteral("stage-too-narrow");
  }
  if (gfx_ == nullptr || !gfx_->isVisible() || gfx_->width() < 320 ||
      gfx_->height() < 120) {
    problems << QStringLiteral("gfx-too-small");
  }
  if (timeline_ == nullptr || !timeline_->isVisible() ||
      timeline_->height() < 72) {
    problems << QStringLiteral("timeline-too-small");
  }
  if (info_bar_ == nullptr || !info_bar_->isVisible() ||
      info_bar_->height() < 30) {
    problems << QStringLiteral("info-bar-too-small");
  }
  if (statusBar() == nullptr || statusBar()->height() < 20) {
    problems << QStringLiteral("status-bar-too-small");
  }

  const QList<QLabel*> info_labels =
      info_bar_ == nullptr ? QList<QLabel*>() : info_bar_->findChildren<QLabel*>();
  // T8.12：横幅有独立行容器，子 label 的 geometry 是相对父控件的；统一映射到
  // InfoBar 坐标系再判断裁切/重叠，避免把不同父控件的坐标混用。
  auto rect_in_info_bar = [this](QWidget* widget) {
    return QRect(widget->mapTo(info_bar_, QPoint(0, 0)), widget->size());
  };
  for (QLabel* label : info_labels) {
    if (!label->isVisible()) {
      continue;
    }
    if (!info_bar_->rect().contains(rect_in_info_bar(label))) {
      problems << QStringLiteral("info-label-outside");
    }
    if (!label->text().isEmpty() && label->width() < 24) {
      problems << QStringLiteral("info-label-narrow");
    }
  }
  for (int i = 0; i < info_labels.size(); ++i) {
    for (int j = i + 1; j < info_labels.size(); ++j) {
      QLabel* first = info_labels[i];
      QLabel* second = info_labels[j];
      if (first->isVisible() && second->isVisible() &&
          rect_in_info_bar(first).intersects(rect_in_info_bar(second))) {
        problems << QStringLiteral("info-label-overlap");
      }
    }
  }
  QLabel* file_label = info_bar_ == nullptr
                           ? nullptr
                           : info_bar_->findChild<QLabel*>(QStringLiteral("infoFile"));
  if (file_label != nullptr && file_label->isVisible() &&
      !file_label->text().isEmpty() &&
      file_label->fontMetrics().horizontalAdvance(file_label->text()) >
          file_label->width() + 2) {
    problems << QStringLiteral("file-label-clipped");
  }
  QLabel* banner_label = info_bar_ == nullptr
                             ? nullptr
                             : info_bar_->findChild<QLabel*>(
                                   QStringLiteral("infoBanner"));
  if (banner_label != nullptr && banner_label->isVisible()) {
    if (banner_label->height() < banner_label->fontMetrics().height()) {
      problems << QStringLiteral("banner-clipped");
    }
    if (banner_label->hasHeightForWidth() &&
        banner_label->heightForWidth(banner_label->width()) >
            banner_label->height() + 2) {
      problems << QStringLiteral("banner-text-clipped");
    }
    auto global_rect = [](QWidget* widget) {
      return QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size());
    };
    const QRect banner_rect = global_rect(banner_label);
    QLabel* frame_label =
        info_bar_->findChild<QLabel*>(QStringLiteral("infoFrame"));
    if (frame_label != nullptr && frame_label->isVisible() &&
        banner_rect.intersects(global_rect(frame_label))) {
      problems << QStringLiteral("banner-overlaps-frame");
    }
    if (timeline_ != nullptr && timeline_->isVisible() &&
        banner_rect.intersects(global_rect(timeline_))) {
      problems << QStringLiteral("banner-overlaps-timeline");
    }
    if (statusBar() != nullptr &&
        banner_rect.intersects(global_rect(statusBar()))) {
      problems << QStringLiteral("banner-overlaps-status");
    }
  }

  if (statusBar() != nullptr) {
    const std::vector<QWidget*> status_widgets = {
        notice_label_, audio_label_, speed_label_, loop_label_, waveform_label_,
        volume_slider_, status_label_};
    for (QWidget* widget : status_widgets) {
      if (widget == nullptr || !widget->isVisible()) {
        continue;
      }
      if (!statusBar()->rect().contains(widget->geometry())) {
        problems << QStringLiteral("status-widget-outside");
      }
      const int minimum_width = widget == volume_slider_ ? 80 : 10;
      if (widget->width() < minimum_width) {
        problems << QStringLiteral("status-widget-narrow");
      }
    }
    for (std::size_t i = 0; i < status_widgets.size(); ++i) {
      for (std::size_t j = i + 1; j < status_widgets.size(); ++j) {
        QWidget* first = status_widgets[i];
        QWidget* second = status_widgets[j];
        if (first != nullptr && second != nullptr && first->isVisible() &&
            second->isVisible() &&
            first->geometry().intersects(second->geometry())) {
          problems << QStringLiteral("status-widget-overlap");
        }
      }
    }
  }
  return problems;
}

void MainWindow::ShowEmptyState() {
  if (online_mode_ || online_) {
    ShutdownOnlineSession(false);
  }
  online_mode_ = false;
  online_ready_ = false;
  online_cache_hit_ = false;
  online_cache_path_.clear();
  // T7.1：空态/切文件前停止后台索引，避免旧结果装到空会话上。
  CancelIndexBuild();
  CancelPendingMarkJump();
  if (displayed_frame_ != nullptr) {
    av_frame_unref(displayed_frame_);
  }
  displayed_frame_index_ = -1;
  displayed_rotation_ = 0;
  StopPreview();
  preview_.reset();
  marks_.reset();
  marks_dirty_ = false;
  if (mark_panel_ != nullptr) {
    mark_panel_->Clear();
  }
  if (timeline_ != nullptr) {
    timeline_->ClearWaveform();
    timeline_->SetMarks({});
  }
  if (audio_) {
    audio_.reset();
  }
  if (waveform_) {
    waveform_->Cancel();
    waveform_.reset();
  }
  waveform_status_revision_ = 0;
  if (waveform_label_ != nullptr) {
    waveform_label_->setText(QStringLiteral("波形：—"));
  }
  if (playback_ != nullptr) {
    playback_->Pause();
    playback_->SetAudioEngine(nullptr);
    playback_->SetSession(nullptr);
    playback_->SetRate(1.0);
  }
  rate_ = 1.0;
  if (speed_label_ != nullptr) {
    speed_label_->setText(QStringLiteral("1x"));
  }
  loop_.Clear();
  if (loop_label_ != nullptr) {
    loop_label_->setText(QStringLiteral("循环：关"));
  }
  if (gfx_ != nullptr) {
    gfx_->ClearVideoFrame();
  }
  if (timeline_ != nullptr) {
    timeline_->Clear();
  }
  if (info_bar_ != nullptr) {
    info_bar_->Clear();
  }
  if (audio_label_ != nullptr) {
    audio_label_->setText(QStringLiteral("无音频"));
  }
  if (volume_slider_ != nullptr) {
    volume_slider_->setEnabled(false);
  }
  if (mute_action_ != nullptr) {
    mute_action_->setEnabled(false);
    mute_action_->setChecked(muted_);
    mute_action_->setText(muted_ ? QStringLiteral("取消静音") : QStringLiteral("静音"));
  }
  audio_base_label_.clear();
  audio_status_revision_ = 0;
  audio_error_shown_ = false;
  if (play_action_ != nullptr) {
    play_action_->setEnabled(false);
    play_action_->setText(QStringLiteral("播放"));
  }
  if (prev_action_ != nullptr) {
    prev_action_->setEnabled(false);
  }
  if (next_action_ != nullptr) {
    next_action_->setEnabled(false);
  }
  if (goto_action_ != nullptr) {
    goto_action_->setEnabled(false);
  }
  if (zoom_in_action_ != nullptr) {
    zoom_in_action_->setEnabled(false);
  }
  if (zoom_out_action_ != nullptr) {
    zoom_out_action_->setEnabled(false);
  }
  if (waveform_action_ != nullptr) {
    waveform_action_->setEnabled(false);
  }
  if (slower_action_ != nullptr) {
    slower_action_->setEnabled(false);
  }
  if (faster_action_ != nullptr) {
    faster_action_->setEnabled(false);
  }
  if (loop_in_action_ != nullptr) {
    loop_in_action_->setEnabled(false);
  }
  if (loop_out_action_ != nullptr) {
    loop_out_action_->setEnabled(false);
  }
  if (loop_toggle_action_ != nullptr) {
    loop_toggle_action_->setEnabled(false);
  }
  if (loop_clear_action_ != nullptr) {
    loop_clear_action_->setEnabled(false);
  }
  if (mark_action_ != nullptr) {
    mark_action_->setEnabled(false);
  }
  if (snapshot_action_ != nullptr) {
    snapshot_action_->setEnabled(false);
  }
  current_frame_ = 0;
  setWindowTitle(QStringLiteral("拉片软件"));
  status_label_->setText(base_info_ + QStringLiteral(" · 未打开文件"));
  statusBar()->showMessage(
      QStringLiteral("将视频拖到这里，或按 Ctrl+O 打开；在线按 Ctrl+U 输入 BV 号"));
}

void MainWindow::OpenDialog() {
  // spec §3.7：文本框聚焦时除 Esc/Enter 外不触发全局快捷键。
  if (IsTextInputFocused()) {
    return;
  }
  QString start_dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
  if (start_dir.isEmpty()) {
    start_dir = QDir::homePath();
  }
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("打开视频"), start_dir,
      QStringLiteral("视频文件 (*.mp4 *.mov *.mkv *.webm);;所有文件 (*.*)"));
  if (!path.isEmpty()) {
    OpenFile(path);
  }
}

void MainWindow::OpenOnlineDialog() {
  if (IsTextInputFocused()) {
    return;
  }
  ShowOnlineDialog();
}

void MainWindow::OpenOnlineDialogForTest() { ShowOnlineDialog(); }

void MainWindow::ShowOnlineDialog() {
  if (online_dialog_ == nullptr) {
    online_dialog_ = new OnlineResolveDialog(this);
    connect(online_dialog_, &OnlineResolveDialog::confirmed, this,
            [this](const QString& bvid, qint64 cid, int page) {
              const OnlineConfirmInfo info = online_dialog_->ConfirmInfo();
              lapian::app::OnlineRequest request;
              request.bvid = info.bvid.isEmpty() ? bvid : info.bvid;
              request.cid = info.cid > 0 ? info.cid : cid;
              request.page = info.page > 0 ? info.page : page;
              request.title = info.title;
              request.owner = info.owner;
              request.duration_ms = info.duration_ms;
              request.page_title = info.page_title;
              // 凭据只在内存中传给 playurl；不写日志/meta。
              lapian::online::CredentialStore credentials;
              const lapian::online::CredentialLoadResult loaded =
                  credentials.Load();
              if (loaded.ok) {
                request.sessdata = loaded.sessdata;
              }
              if (online_dialog_ != nullptr) {
                online_dialog_->hide();
              }
              BeginOnlineRequest(request);
            });
  }
  online_dialog_->SetCacheProbe(
      [this](const QString& bvid, OnlineCacheSummary* summary, QString* error) {
        return LookupOnlineCacheForDialog(bvid, summary, error);
      });
  online_dialog_->show();
  online_dialog_->raise();
  online_dialog_->activateWindow();
}

bool MainWindow::LookupOnlineCacheForDialog(const QString& bvid,
                                            OnlineCacheSummary* summary,
                                            QString* error) {
  if (summary == nullptr) {
    return false;
  }
  if (error != nullptr) {
    error->clear();
  }
  if (online_cache_ == nullptr) {
    EnsureOnlineSession();
  }
  QString cache_ready_error;
  if (!EnsureOnlineCacheReady(&cache_ready_error)) {
    if (error != nullptr) {
      *error = cache_ready_error;
    }
    return false;
  }
  QVector<lapian::online::CacheEntry> entries;
  QVector<lapian::online::CacheEntry> broken;
  QString scan_error;
  if (!online_cache_->Scan(&entries, &broken, &scan_error)) {
    if (error != nullptr) {
      *error = scan_error;
    }
    return false;
  }
  const lapian::online::CacheEntry* best = nullptr;
  for (const lapian::online::CacheEntry& entry : entries) {
    if (!entry.complete || entry.broken || entry.key.bvid != bvid) {
      continue;
    }
    if (best == nullptr || entry.last_access_at > best->last_access_at ||
        (entry.last_access_at == best->last_access_at &&
         entry.key.qn > best->key.qn)) {
      best = &entry;
    }
  }
  if (best == nullptr) {
    return false;
  }
  summary->bvid = bvid;
  summary->cid = best->key.cid;
  summary->title = best->title;
  summary->owner = best->owner;
  summary->duration_ms = best->duration_ms;
  summary->qn = best->key.qn;
  summary->codecid = best->key.codecid;
  summary->fps = best->fps;
  return true;
}

void MainWindow::InitializeOnlineCacheStore() {
  if (online_cache_) {
    return;
  }
  const qint64 quota =
      static_cast<qint64>(std::clamp(loaded_settings_.cache_quota_gb,
                                     lapian::core::kCacheQuotaMinGb,
                                     lapian::core::kCacheQuotaMaxGb)) *
      1024LL * 1024LL * 1024LL;
  online_cache_ =
      std::make_unique<lapian::online::CacheStore>(loaded_settings_.cache_root,
                                                   quota);
  online_cache_ready_ = false;
  online_cache_error_.clear();
}

bool MainWindow::EnsureOnlineCacheReady(QString* error) {
  if (online_cache_ == nullptr) {
    InitializeOnlineCacheStore();
  }
  if (online_cache_ == nullptr) {
    if (error != nullptr) {
      *error = QStringLiteral("在线缓存未初始化");
    }
    return false;
  }
  if (online_cache_ready_) {
    return true;
  }
  QString root_error;
  if (!online_cache_->SetRoot(online_cache_->root(), &root_error)) {
    online_cache_error_ = root_error;
    if (error != nullptr) {
      *error = root_error;
    }
    return false;
  }
  const qint64 quota =
      static_cast<qint64>(std::clamp(loaded_settings_.cache_quota_gb,
                                     lapian::core::kCacheQuotaMinGb,
                                     lapian::core::kCacheQuotaMaxGb)) *
      1024LL * 1024LL * 1024LL;
  QString quota_error;
  if (!online_cache_->SetQuotaBytes(quota, &quota_error)) {
    online_cache_error_ = quota_error;
    if (error != nullptr) {
      *error = quota_error;
    }
    return false;
  }
  online_cache_ready_ = true;
  online_cache_error_.clear();
  return true;
}

void MainWindow::EnsureOnlineSession() {
  if (online_) {
    return;
  }
  QString cache_error;
  if (!EnsureOnlineCacheReady(&cache_error)) {
    online_last_error_ =
        QStringLiteral("在线缓存目录不可用：%1").arg(cache_error);
    if (info_bar_ != nullptr) {
      info_bar_->SetError(online_last_error_);
    }
    statusBar()->showMessage(online_last_error_, 12000);
    return;
  }
  online_ = std::make_unique<lapian::app::OnlineSession>(online_cache_.get(), this);
  if (online_test_http_) {
    online_->SetHttpClientForTest(std::move(online_test_http_));
  }
  if (online_test_transport_) {
    online_->SetDownloadTransportForTest(std::move(online_test_transport_));
  }
  if (has_online_test_playurl_ && online_test_playurl_) {
    online_->SetPlayUrlDataForTest(*online_test_playurl_);
  }
  if (online_test_threshold_ > 0) {
    online_->SetReadyThresholdForTest(online_test_threshold_);
  }
  online_->SetDownloadThrottleForTest(online_download_throttle_bps_, 0);
  // T8.9：档位目录默认首帧后异步取；测试可关/注入（注入不联网）。
  online_->SetQualityCatalogEnabledForTest(online_quality_enabled_);
  if (has_online_test_quality_) {
    online_->SetQualityCatalogForTest(online_test_quality_);
  }
  online_->SetCallbacks(
      [this](lapian::app::OnlineState state, const QString& message) {
        ApplyOnlineState(state, message);
      },
      [this](const lapian::app::OnlineProgress& progress) {
        ApplyOnlineProgress(progress);
      },
      [this](const lapian::app::OnlineReadyInfo& info) {
        ApplyOnlineReady(info);
      },
      [this](const QString& message) {
        online_last_error_ = message;
        if (info_bar_ != nullptr) {
          info_bar_->SetError(message);
        }
        statusBar()->showMessage(QStringLiteral("在线错误：%1").arg(message), 12000);
      },
      [this](const lapian::app::OnlineQualityCatalog& info) {
        ApplyOnlineQuality(info);
      });
}

bool MainWindow::StartOnlineSession(const lapian::app::OnlineRequest& request) {
  QElapsedTimer start_timer;
  start_timer.start();
  BeginOnlineRequest(request);
  if (qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG")) {
    std::fprintf(stderr, "T8.9 online_start_ui ms=%lld active=%d\n",
                 static_cast<long long>(start_timer.elapsed()),
                 online_ != nullptr && online_->IsActive() ? 1 : 0);
    std::fflush(stderr);
  }
  return online_ != nullptr && online_->IsActive();
}

bool MainWindow::CurrentOnlineCacheKey(lapian::online::CacheKey* key) const {
  if (key == nullptr || !online_mode_ || online_bvid_.isEmpty() ||
      online_cid_ <= 0) {
    return false;
  }
  key->bvid = online_bvid_;
  key->cid = online_cid_;
  // qn/codecid 为 0 表示 playurl 尚未返回；CacheStore 会把 bvid+cid 当作
  // 部分保护键，避免清空/清理误删当前会话刚 Reserve 的 .part。
  key->qn = online_qn_;
  key->codecid = online_codecid_;
  return true;
}

QString MainWindow::OnlineRecordsRoot() const {
  if (!online_records_root_for_test_.isEmpty()) {
    return QDir::cleanPath(
        QFileInfo(online_records_root_for_test_).absoluteFilePath());
  }
  return lapian::online::CacheManager::DefaultRecordsRoot();
}

bool MainWindow::ApplyCacheSettings(const QString& root, int quota_gb,
                                    QString* error) {
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (online_mode_) {
    return fail(QStringLiteral("在线会话打开中，不能更改缓存目录或配额；"
                               "请先关闭在线视频。"));
  }
  const int clamped_gb = std::clamp(quota_gb, lapian::core::kCacheQuotaMinGb,
                                    lapian::core::kCacheQuotaMaxGb);
  const qint64 quota = static_cast<qint64>(clamped_gb) * 1024LL * 1024LL *
                       1024LL;
  const QString normalized = lapian::online::CacheManager::NormalizeRoot(root);
  if (normalized.isEmpty()) {
    return fail(QStringLiteral("缓存目录为空"));
  }
  const QString old_root =
      online_cache_ != nullptr ? online_cache_->root()
                               : lapian::online::CacheStore::DefaultRoot();
  const qint64 old_usage =
      online_cache_ != nullptr ? online_cache_->UsageBytes() : 0;

  auto next = std::make_unique<lapian::online::CacheStore>(normalized, quota);
  QString setup_error;
  if (!next->SetRoot(normalized, &setup_error)) {
    return fail(setup_error);
  }
  if (!next->SetQuotaBytes(quota, &setup_error)) {
    return fail(setup_error);
  }

  // 先写 settings.json，成功后才替换 online_cache_。这样保存失败时对话框
  // 持有的 CacheStore* 仍然有效，不会出现“设置没保存但缓存对象已被换掉”。
  lapian::core::AppSettings updated =
      lapian::core::MergeVolumeWaveform(loaded_settings_, volume_,
                                        waveform_visible_);
  updated.cache_root =
      QDir::cleanPath(normalized).compare(
          QDir::cleanPath(lapian::online::CacheStore::DefaultRoot()),
          Qt::CaseInsensitive) == 0
          ? QString()
          : normalized;
  updated.cache_quota_gb = clamped_gb;
  if (settings_store_ == nullptr) {
    return fail(QStringLiteral("设置存储未初始化，无法保存缓存目录/配额。"));
  }
  QElapsedTimer save_timer;
  save_timer.start();
  const bool saved = settings_store_->Save(updated);
  settings_last_save_ms_ =
      static_cast<double>(save_timer.nsecsElapsed()) / 1.0e6;
  if (!saved) {
    return fail(QStringLiteral("无法写入 settings.json，缓存目录/配额未改变；"
                               "请检查 %APPDATA% 写权限后重试。"));
  }
  if (online_ != nullptr) {
    ShutdownOnlineSession(false);
  }
  online_cache_ = std::move(next);
  online_cache_ready_ = true;
  online_cache_error_.clear();
  loaded_settings_ = updated;
  settings_dirty_ = false;
  // 配额调小后立即回收；读句柄占用时只提示，不阻止设置保存。
  QString evict_error;
  const bool evicted =
      online_cache_->EvictToQuota(lapian::online::CacheKey{}, &evict_error);
  if (old_root.compare(normalized, Qt::CaseInsensitive) != 0 &&
      old_usage > 0) {
    statusBar()->showMessage(
        QStringLiteral("已切换缓存目录；旧目录仍有 %1，未自动迁移或删除。")
            .arg(lapian::online::CacheStore::FormatBytes(old_usage)),
        12000);
  } else if (!evicted) {
    statusBar()->showMessage(
        QStringLiteral("配额已保存；暂未完全回收：%1").arg(evict_error), 10000);
  } else {
    statusBar()->showMessage(QStringLiteral("缓存设置已保存。"), 5000);
  }
  return true;
}

bool MainWindow::ClearOnlineCache(QString* error, qint64* removed_bytes,
                                  int* kept_entries) {
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  QString cache_error;
  if (!EnsureOnlineCacheReady(&cache_error)) {
    return fail(cache_error);
  }
  lapian::online::CacheKey key;
  const bool has_key = CurrentOnlineCacheKey(&key);
  if (online_mode_ && !has_key) {
    return fail(QStringLiteral("当前在线会话尚未确定缓存键，暂不能清空缓存；"
                               "请等首帧或关闭在线视频后重试。"));
  }
  lapian::online::CacheManager manager(online_cache_.get(), OnlineRecordsRoot());
  return manager.ClearAll(has_key ? &key : nullptr, error, removed_bytes,
                          kept_entries);
}

bool MainWindow::CleanupOnlineCache(QString* error, qint64* removed_bytes,
                                    int* removed_entries) {
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  QString cache_error;
  if (!EnsureOnlineCacheReady(&cache_error)) {
    return fail(cache_error);
  }
  lapian::online::CacheKey key;
  const bool has_key = CurrentOnlineCacheKey(&key);
  lapian::online::CacheManager manager(online_cache_.get(), OnlineRecordsRoot());
  const lapian::online::CacheMaintenanceResult result =
      manager.CleanupOrphans(has_key ? &key : nullptr,
                             lapian::online::CleanupOptions{}, false);
  if (!result.ok) {
    return fail(result.error);
  }
  if (removed_bytes != nullptr) {
    *removed_bytes = result.removed_bytes;
  }
  if (removed_entries != nullptr) {
    *removed_entries = result.removed_entries;
  }
  if (!result.warning.isEmpty()) {
    statusBar()->showMessage(result.warning, 10000);
  }
  return true;
}

bool MainWindow::OpenOnlineCacheDirectory(QString* error) {
  auto fail = [&](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  QString cache_error;
  if (!EnsureOnlineCacheReady(&cache_error)) {
    return fail(cache_error);
  }
  const QString root = online_cache_->root();
  if (!QDir().mkpath(root)) {
    return fail(QStringLiteral("无法创建缓存目录：%1").arg(root));
  }
  if (!QDesktopServices::openUrl(QUrl::fromLocalFile(root))) {
    return fail(QStringLiteral("无法打开缓存目录：%1").arg(root));
  }
  return true;
}

void MainWindow::ShowCacheSettingsDialog() {
  if (online_cache_ == nullptr) {
    InitializeOnlineCacheStore();
  }
  QString ready_error;
  if (!EnsureOnlineCacheReady(&ready_error)) {
    // 目录不可写仍打开设置：对话框会显示只读/权限错误，并允许改目录。
    statusBar()->showMessage(
        QStringLiteral("在线缓存目录不可用：%1").arg(ready_error), 10000);
  }
  if (online_cache_ == nullptr) {
    QMessageBox::warning(this, QStringLiteral("缓存设置"),
                         QStringLiteral("在线缓存未初始化。"));
    return;
  }
  lapian::app::CacheSettingsDialog::Callbacks callbacks;
  callbacks.apply = [this](const QString& root, int quota_gb, QString* error) {
    return ApplyCacheSettings(root, quota_gb, error);
  };
  callbacks.clear = [this](QString* error, qint64* removed, int* kept) {
    return ClearOnlineCache(error, removed, kept);
  };
  callbacks.cleanup = [this](QString* error, qint64* removed, int* entries) {
    return CleanupOnlineCache(error, removed, entries);
  };
  callbacks.open_directory = [this](QString* error) {
    return OpenOnlineCacheDirectory(error);
  };
  const QString description =
      online_mode_
          ? QStringLiteral("在线会话打开中：不能更改缓存目录或配额；"
                           "清空缓存会保留当前会话的条目。")
          : QString();
  lapian::app::CacheSettingsDialog dialog(
      online_cache_.get(), OnlineRecordsRoot(), online_mode_, description,
      callbacks, this);
  dialog.exec();
}

void MainWindow::RunCacheMaintenance(const char* reason) {
  if (!cache_maintenance_enabled_) {
    return;
  }
  const QString reason_text =
      QString::fromLatin1(reason == nullptr ? "manual" : reason);
  QElapsedTimer timer;
  timer.start();
  cache_last_maintenance_reason_ = reason_text;
  cache_last_maintenance_removed_bytes_ = 0;
  cache_last_maintenance_removed_entries_ = 0;
  QString cache_error;
  if (!EnsureOnlineCacheReady(&cache_error)) {
    cache_last_maintenance_ms_ =
        static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
    statusBar()->showMessage(
        QStringLiteral("在线缓存目录不可用：%1").arg(cache_error), 12000);
    if (qEnvironmentVariableIsSet("LAPIAN_CACHE_LOG")) {
      std::fprintf(stderr,
                   "T8.3.1 cache_maintenance reason=%s ok=0 error=\"%s\" "
                   "elapsed_ms=%.3f\n",
                   reason_text.toUtf8().constData(),
                   cache_error.toUtf8().constData(),
                   cache_last_maintenance_ms_);
      std::fflush(stderr);
    }
    return;
  }
  lapian::online::CacheKey key;
  const bool has_key = CurrentOnlineCacheKey(&key);
  lapian::online::CacheManager manager(online_cache_.get(), OnlineRecordsRoot());
  const lapian::online::CacheMaintenanceResult result =
      manager.CleanupOrphans(has_key ? &key : nullptr,
                             lapian::online::CleanupOptions{}, true);
  cache_last_maintenance_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  cache_last_maintenance_removed_bytes_ = result.removed_bytes;
  cache_last_maintenance_removed_entries_ = result.removed_entries;
  if (!result.ok) {
    statusBar()->showMessage(
        QStringLiteral("在线缓存清理失败：%1").arg(result.error), 10000);
  } else if (result.removed_bytes > 0 || result.removed_entries > 0) {
    statusBar()->showMessage(
        QStringLiteral("已清理在线缓存：%1 个条目，%2")
            .arg(result.removed_entries)
            .arg(lapian::online::CacheStore::FormatBytes(result.removed_bytes)),
        8000);
  }
  if (qEnvironmentVariableIsSet("LAPIAN_CACHE_LOG")) {
    std::fprintf(stderr,
                 "T8.3.1 cache_maintenance reason=%s ok=%d removed_bytes=%lld "
                 "removed_entries=%d usage_after=%lld protected=%d "
                 "elapsed_ms=%.3f warning=\"%s\"\n",
                 reason_text.toUtf8().constData(), result.ok ? 1 : 0,
                 static_cast<long long>(result.removed_bytes),
                 result.removed_entries,
                 static_cast<long long>(result.usage_after),
                 result.current_protected ? 1 : 0,
                 cache_last_maintenance_ms_,
                 result.warning.toUtf8().constData());
    std::fflush(stderr);
  }
}

void MainWindow::RunCacheMaintenanceForTest() { RunCacheMaintenance("test"); }

bool MainWindow::ApplyCacheSettingsForTest(const QString& root, int quota_gb,
                                           QString* error) {
  return ApplyCacheSettings(root, quota_gb, error);
}

void MainWindow::SetCacheMaintenanceEnabledForTest(bool enabled) {
  cache_maintenance_enabled_ = enabled;
  if (cache_maintenance_timer_ != nullptr) {
    if (enabled) {
      if (!cache_maintenance_timer_->isActive()) {
        cache_maintenance_timer_->start();
      }
    } else {
      cache_maintenance_timer_->stop();
    }
  }
}

void MainWindow::BeginOnlineRequest(const lapian::app::OnlineRequest& request) {
  QElapsedTimer begin_timer;
  begin_timer.start();
  if (request.bvid.isEmpty() || request.cid <= 0) {
    online_last_error_ = QStringLiteral("在线请求缺少 bvid/cid");
    statusBar()->showMessage(online_last_error_, 8000);
    return;
  }
  ClearOnlineQualitySwitch();
  ShutdownOnlineSession(false);
  EnsureOnlineSession();
  if (online_ == nullptr) {
    online_mode_ = false;
    if (online_last_error_.isEmpty()) {
      online_last_error_ = QStringLiteral("无法开始在线会话");
    }
    if (info_bar_ != nullptr) {
      info_bar_->SetError(online_last_error_);
    }
    statusBar()->showMessage(online_last_error_, 12000);
    return;
  }
  online_mode_ = true;
  online_ready_ = false;
  online_finalized_ = false;
  online_cache_hit_ = false;
  online_cache_path_.clear();
  online_owner_.clear();
  online_fps_ = 0.0;
  online_actual_fps_ = 0.0;
  online_source_fps_hint_ = lapian::online::InferSourceFpsHintFromText(
      request.title + QLatin1Char(' ') + request.page_title);
  online_frame_rate_drop_ = false;
  online_request_ = request;
  online_logged_in_ = !request.sessdata.isEmpty();
  online_bvid_ = request.bvid;
  online_title_ = request.title;
  online_cid_ = request.cid;
  online_qn_ = 0;
  online_codecid_ = 0;
  online_published_bytes_ = 0;
  online_total_bytes_ = 0;
  online_dash_audio_ = false;
  online_audio_source_.reset();
  online_audio_part_path_.clear();
  online_audio_id_ = 0;
  online_audio_total_bytes_ = 0;
  online_dash_duration_ms_ = 0;
  online_dash_fps_ = 0.0;
  online_stable_frames_ = 0;
  online_index_complete_ = false;
  online_spans_.clear();
  online_index_pending_.reset();
  online_index_pending_generation_ = 0;
  online_index_stats_.clear();
  online_last_error_.clear();
  online_buffer_event_count_ = 0;
  online_gate_last_ = true;
  // T8.6.1a：新会话从干净的步进状态开始。
  online_step_ = OnlineStepState{};
  online_seek_target_ = -1;
  online_seek_completion_notice_.clear();
  online_status_detail_.clear();
  online_gate_published_ = 0;
  online_gate_readpos_ = 0;
  if (info_bar_ != nullptr) {
    info_bar_->ClearOnlineFrameRateStatus();
  }
  ResetOnlineQuality();
  if (online_cancel_action_ != nullptr) {
    online_cancel_action_->setEnabled(true);
  }
  if (playback_) {
    playback_->Pause();
  }
  const QString label = request.title.isEmpty() ? request.bvid : request.title;
  setWindowTitle(QStringLiteral("%1 — 拉片软件（在线）").arg(label));
  status_label_->setText(base_info_ + QStringLiteral(" · 在线：准备取流…"));
  statusBar()->showMessage(QStringLiteral("正在解析在线视频…"));

  QString error;
  if (online_ != nullptr) {
    online_->SetDashEnabledForTest(online_dash_enabled_);
  }
  if (!online_ || !online_->Start(request, &error)) {
    online_mode_ = false;
    online_last_error_ = error.isEmpty() ? QStringLiteral("无法开始在线会话") : error;
    lapian::online::ClearSecret(&online_request_.sessdata);
    online_request_ = lapian::app::OnlineRequest{};
    online_actual_fps_ = 0.0;
    if (info_bar_ != nullptr) {
      info_bar_->SetError(online_last_error_);
    }
    statusBar()->showMessage(online_last_error_, 10000);
  }
  if (qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG")) {
    std::fprintf(stderr, "T8.9 online_begin_done elapsed_ms=%lld ok=%d\n",
                 static_cast<long long>(begin_timer.elapsed()),
                 online_mode_ && online_ready_ ? 1 : 0);
    std::fflush(stderr);
  }
}

bool MainWindow::ApplyOnlineReady(const lapian::app::OnlineReadyInfo& info) {
  if (!online_mode_) {
    return false;
  }
  // T8.9 诊断：记录首帧就绪路径的分段耗时（只在 LAPIAN_ONLINE_LOG 下打印）。
  QElapsedTimer ready_steps;
  ready_steps.start();
  auto ready_step = [&ready_steps](const char* name) {
    if (qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG")) {
      std::fprintf(stderr, "T8.9 online_ready step=%s ms=%.1f\n", name,
                   static_cast<double>(ready_steps.elapsed()));
      std::fflush(stderr);
    }
  };
  if (info.cache_hit && !info.dash) {
    return ApplyOnlineCacheHit(info);
  }
  if (!info.source || info.part_path.isEmpty() || gfx_ == nullptr ||
      gfx_->d3d_device() == nullptr) {
    online_last_error_ = QStringLiteral("在线首帧打开缺少数据源/图形设备");
    statusBar()->showMessage(online_last_error_, 8000);
    ready_step("early_no_source");
    return false;
  }
  // 在线记录：bvid+cid 持久化；截图在 T8.7 走图片目录 + index.jsonl。
  QString pictures = snapshot_pictures_root_for_test_;
  if (pictures.isEmpty()) {
    pictures =
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  }
  if (pictures.isEmpty()) {
    pictures = QDir::tempPath();
  }
  const QString display_path = QDir(pictures).filePath(
      QStringLiteral("lapian-online-%1-%2.mp4")
          .arg(info.request.bvid)
          .arg(info.request.cid));
  const QByteArray part_utf8 = info.part_path.toUtf8();
  const std::string part_path(part_utf8.constData(),
                              static_cast<std::size_t>(part_utf8.size()));
  const QByteArray display_utf8 = display_path.toUtf8();
  const std::string display_path_utf8(
      display_utf8.constData(), static_cast<std::size_t>(display_utf8.size()));

  auto candidate = std::make_shared<lapian::media::MediaSession>();
  std::string error;
  QElapsedTimer open_timer;
  open_timer.start();
  lapian::media::GrowingOpenOptions open_options;
  open_options.dash = info.dash;
  open_options.duration_ms = info.dash_duration_ms > 0 ? info.dash_duration_ms
                                                       : info.duration_ms;
  open_options.fps = info.dash_fps;
  if (info.dash) {
    open_options.audio.has_audio = true;
    open_options.audio.stream_index = 0;
    open_options.audio.codec = "aac";
  }
  const bool opened = candidate->OpenGrowingWithOptions(
      info.source, part_path, display_path_utf8, gfx_->d3d_device(), &error,
      open_options, true);
  const double open_ms = static_cast<double>(open_timer.nsecsElapsed()) / 1.0e6;
  if (!opened) {
    online_last_error_ =
        QStringLiteral("在线首帧打开失败：%1").arg(QString::fromStdString(error));
    if (info_bar_ != nullptr) {
      info_bar_->SetError(online_last_error_);
    }
    statusBar()->showMessage(online_last_error_, 10000);
    if (online_) {
      online_->Cancel();
    }
    ready_step("early_open_failed");
    return false;
  }
  if (qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG") || startup_log_) {
    std::fprintf(stderr,
                 "T8.6 online_open open_ms=%.2f probe_ms=%.2f decoder_ms=%.2f "
                 "first_frame_ms=%.2f published=%lld/%lld\n",
                 open_ms, candidate->last_open_timings().probe_ms,
                 candidate->last_open_timings().decoder_open_ms,
                 candidate->last_open_timings().first_frame_ms,
                 static_cast<long long>(info.source->Published()),
                 static_cast<long long>(info.source->Size()));
    std::fflush(stderr);
  }

  // 旧本地会话的标记先落盘；失败时询问，不静默丢。online_ready_ 仍为 false，
  // 因此这里走的仍是本地 sidecar 保存路径。
  if (!FlushMarksBeforeSwitch()) {
    online_last_error_ = QStringLiteral("旧文件标记未保存，已取消在线打开");
    if (online_) {
      online_->Cancel();
      online_->Close();
      online_.reset();
    }
    online_mode_ = false;
    if (online_cancel_action_ != nullptr) {
      online_cancel_action_->setEnabled(false);
    }
    ready_step("early_marks_flush_failed");
    return false;
  }
  ready_step("media_opened");

  // 旧本地/在线会话的 reader 必须全部停掉，再替换。
  if (playback_) {
    playback_->Pause();
  }
  StopOnlineSeekWorker();
  StopOnlineIndexBuild();
  StopPreview();
  preview_.reset();
  CancelIndexBuild();
  if (cache_fill_timer_ != nullptr) {
    cache_fill_timer_->stop();
  }
  if (audio_) {
    audio_->Close();
    audio_.reset();
  }
  if (waveform_) {
    waveform_->Cancel();
    waveform_.reset();
  }
  if (playback_) {
    playback_->SetAudioEngine(nullptr);
    playback_->SetSession(nullptr);
  }
  session_ = std::move(candidate);
  if (playback_) {
    playback_->SetSession(session_.get());
    playback_->SetBufferOnAudioStall(true);
    playback_->SetDecodeGate(
        [this](std::int64_t, std::string* reason) -> bool {
          if (!online_mode_ || session_ == nullptr || !session_->IsGrowing()) {
            return true;
          }
          online_gate_published_ = session_->GrowingPublished();
          online_gate_readpos_ = session_->GrowingReadPosition();
          // 1MB 余量：一次 Tick 可能连续解 32 帧，且 AVIO 有 64KB 预读；
          // 余量太小会让 av_read_frame 在 UI 线程等未缓存区（实测 256KB 仍会
          // 在慢源上阻塞）。不足时自动暂停，等下载追上来恢复。
          const bool can = session_->CanDecodeAhead(1024 * 1024);
          online_gate_last_ = can;
          if (can) {
            return true;
          }
          if (reason != nullptr) {
            *reason = "等待下载";
          }
          return false;
        });
  }
  current_frame_ = 0;
  online_ready_ = true;
  online_cache_hit_ = info.cache_hit;
  if (info.cache_hit) {
    online_cache_path_ = info.cache_mp4_path;
  } else {
    online_cache_path_.clear();
  }
  online_dash_audio_ = info.dash;
  online_audio_source_ = info.audio_source;
  online_audio_part_path_ = info.audio_part_path;
  online_audio_id_ = info.audio_id;
  online_audio_total_bytes_ = info.audio_total_bytes;
  online_dash_duration_ms_ = info.dash_duration_ms;
  online_dash_fps_ = info.dash_fps;
  online_request_ = info.request;
  online_logged_in_ = !info.request.sessdata.isEmpty();
  online_bvid_ = info.request.bvid;
  online_title_ = info.request.title;
  online_cid_ = info.request.cid;
  online_qn_ = info.playurl.quality;
  online_codecid_ = info.playurl.codecid;
  online_owner_ = info.request.owner;
  online_fps_ = session_->info().video.frame_rate.ToDouble();
  // 实际流 fps 用 playurl dash_fps（OB-2=29.412），容器 fps 只作回退/并列。
  online_actual_fps_ =
      online_dash_fps_ > 0.0 ? online_dash_fps_ : online_fps_;
  online_source_fps_hint_ = std::max(
      online_source_fps_hint_,
      lapian::online::InferSourceFpsHintFromText(
          online_title_ + QLatin1Char(' ') + info.request.page_title));
  online_total_bytes_ = info.source->Size();
  online_published_bytes_ = info.source->Published();
  online_part_path_ = info.part_path;
  online_display_path_ = display_path;
  online_stable_frames_ = 0;
  online_index_complete_ = false;
  online_spans_.clear();
  online_index_pending_.reset();
  online_index_pending_generation_ = 0;
  online_index_stats_.clear();
  online_last_error_.clear();
  online_step_ = OnlineStepState{};
  online_seek_target_ = -1;
  online_status_detail_.clear();

  const lapian::media::MediaInfo& media = session_->info();
  const lapian::media::DecodedFrameInfo& frame = session_->first_frame();
  current_pts_us_ = frame.pts_us;
  gfx_->SetVideoFrame(frame.av_frame, media.rotation.counterclockwise_degrees);
  RememberDisplayedFrame(frame.av_frame, media.rotation.counterclockwise_degrees, 0);
  info_bar_->SetMedia(media, session_->IsVfrSuspect());
  info_bar_->SetTotal(lapian::core::TotalFrameCount::Scanning());
  const QString quality = lapian::online::QualityName(online_qn_);
  setWindowTitle(QStringLiteral("%1 — 拉片软件（在线 · %2）")
                     .arg(online_title_.isEmpty() ? online_bvid_ : online_title_,
                          quality));
  ApplyOnlineFrameRateUi();
  online_status_detail_ = BuildOnlineStatusDetail();
  status_label_->setText(base_info_ + online_status_detail_);
  // T8.6.1a：seek 落地后用它把状态栏从“缓冲中（目标帧 N）”恢复成档位/帧率。
  // T8.9：档位按钮在首帧后可见；目录稍后异步补齐（先显示当前播放档位）。
  if (quality_button_ != nullptr) {
    quality_button_->setVisible(true);
    quality_button_->setEnabled(true);
    quality_button_->setText(
        QStringLiteral("清晰度：%1").arg(lapian::online::QualityName(online_qn_)));
    quality_button_->setToolTip(
        QStringLiteral("当前实际 qn=%1 codecid=%2；请求 qn=%3；"
                       "档位以服务端返回为准")
            .arg(online_qn_)
            .arg(online_codecid_)
            .arg(online_request_.requested_qn > 0
                     ? QString::number(online_request_.requested_qn)
                     : QStringLiteral("自动")));
  }
  RebuildOnlineQualityMenu();
  statusBar()->showMessage(QStringLiteral("在线首帧已呈现，默认暂停"), 6000);
  play_action_->setEnabled(true);
  if (snapshot_action_ != nullptr) {
    snapshot_action_->setEnabled(true);
  }
  if (timeline_ != nullptr) {
    const lapian::core::TotalFrameCount total = ContainerTotal();
    timeline_->SetFrameCount(total.has_value() ? total.value() : 0);
    timeline_->SetCurrentFrame(0);
    timeline_->setEnabled(true);
  }
  if (prev_action_ != nullptr) {
    prev_action_->setEnabled(true);
  }
  if (next_action_ != nullptr) {
    next_action_->setEnabled(true);
  }
  if (goto_action_ != nullptr) {
    goto_action_->setEnabled(true);
  }
  if (mark_action_ != nullptr) {
    mark_action_->setEnabled(true);
  }
  if (waveform_action_ != nullptr) {
    waveform_action_->setEnabled(true);
  }

  // T8.7 在线记录：bvid+cid 持久化；不再使用 T8.6 的会话内假标记。
  OnlineOpenContext mark_context;
  mark_context.bvid = info.request.bvid;
  mark_context.cid = info.request.cid;
  mark_context.title = info.request.title;
  mark_context.owner = info.request.owner;
  mark_context.qn = info.playurl.quality;
  mark_context.codecid = info.playurl.codecid;
  mark_context.fps = online_actual_fps_;
  mark_context.duration_ms = info.request.duration_ms;
  mark_context.total_bytes = info.source->Size();
  mark_context.cache_hit = info.cache_hit;
  LoadOnlineMarks(mark_context);
  if (marks_ != nullptr) {
    marks_->SetOnlineContext(online_actual_fps_, online_qn_, online_codecid_);
    // 换档后把记录头部的当前流 qn/codecid/fps 落盘；逐条标记保留创建时
    // 的 online_meta_（旧流实际值），不改成新流值。没有标记时不写空记录。
    if (!marks_->marks().empty()) {
      marks_dirty_ = true;
      SaveMarks(false);
    }
  }
  ready_step("marks_loaded");

  // 音频跟随：AudioEngine 自己打开另一个 GrowingFormat；读到 frontier 会等待。
  // T8.10：DASH 的音频是独立容器（stream_index=0），与视频容器分开打开。
  const bool has_dash_audio = info.dash && info.audio_source;
  const bool has_audio = media.has_audio || has_dash_audio;
  const double audio_offset_seconds =
      static_cast<double>(media.av_offset_us) / 1.0e6;
  if (has_audio) {
    auto audio = std::make_unique<lapian::audio::AudioEngine>();
    audio->SetVolume(volume_);
    audio->SetMute(muted_);
    audio->SetRate(rate_);
    const bool audio_opened =
        info.dash
            ? audio->OpenGrowing(info.audio_source, info.audio_part_path.toUtf8().constData(),
                                 0, audio_offset_seconds)
            : audio->OpenGrowing(info.source, part_path, media.audio.stream_index,
                                 audio_offset_seconds);
    if (audio_opened) {
      audio_ = std::move(audio);
      if (media.audio.sample_rate > 0) {
        audio_base_label_ =
            QStringLiteral("音频：%1 %2 kHz %3")
                .arg(QString::fromStdString(media.audio.codec).toUpper())
                .arg(QString::number(
                         static_cast<double>(media.audio.sample_rate) / 1000.0, 'g', 3))
                .arg(DescribeAudioChannels(media.audio.channels));
      } else {
        audio_base_label_ = QStringLiteral("音频：AAC（DASH 独立音轨）");
      }
      audio_label_->setText(audio_base_label_);
      volume_slider_->setEnabled(true);
      mute_action_->setEnabled(true);
    } else {
      audio_label_->setText(QStringLiteral("音频不可用"));
    }
  } else {
    audio_label_->setText(QStringLiteral("无音频"));
  }
  ready_step("audio_opened");
  if (playback_) {
    playback_->SetAudioEngine(audio_.get());
    playback_->SetRate(rate_);
  }

  // 波形：独立 GrowingFormat，渐进填充；不因未下载完而阻塞 UI。
  if (has_audio) {
    waveform_ = std::make_unique<lapian::audio::WaveformGenerator>();
    waveform_last_bucket_count_ = 0;
    const bool waveform_started =
        info.dash
            ? waveform_->StartGrowing(info.audio_source, info.audio_part_path.toUtf8().constData(),
                                      0, audio_offset_seconds)
            : waveform_->StartGrowing(info.source, part_path,
                                      media.audio.stream_index,
                                      audio_offset_seconds);
    if (waveform_started) {
      if (const auto waveform_data = waveform_->data()) {
        const double fps = media.video.frame_rate.ToDouble();
        timeline_->SetWaveformSource(
            [waveform_data]() { return waveform_data->bucket_count(); },
            [waveform_data](std::size_t begin, std::size_t end) {
              const auto range = waveform_data->MinMaxRange(begin, end);
              return TimelineWidget::WaveformBucketView{range.min, range.max};
            },
            audio_offset_seconds,
            fps > 0.0 ? 1.0 / fps : 1.0 / 30.0,
            [this](std::int64_t frame) -> std::int64_t {
              if (!online_ready_ || session_ == nullptr ||
                  !session_->index().IsReady()) {
                return -1;
              }
              return session_->index().PtsOfFrame(frame);
            });
      }
      timeline_->SetWaveformVisible(waveform_visible_);
    } else {
      waveform_label_->setText(QStringLiteral("波形：失败"));
    }
  } else {
    waveform_label_->setText(QStringLiteral("波形：无音频"));
    timeline_->SetWaveformMessage(QStringLiteral("无音频"));
  }
  ready_step("waveform_started");

  StartOnlineIndexBuild();
  ready_step("index_started");
  return true;
}

bool MainWindow::ApplyOnlineCacheHit(
    const lapian::app::OnlineReadyInfo& info) {
  if (info.cache_mp4_path.isEmpty()) {
    online_last_error_ = QStringLiteral("缓存命中缺少 data.mp4 路径");
    statusBar()->showMessage(online_last_error_, 8000);
    online_mode_ = false;
    online_ready_ = false;
    return false;
  }
  online_request_ = info.request;
  online_logged_in_ = !info.request.sessdata.isEmpty();
  online_dash_fps_ = info.dash_fps;
  online_actual_fps_ = info.dash_fps > 0.0 ? info.dash_fps : info.fps;
  online_source_fps_hint_ = lapian::online::InferSourceFpsHintFromText(
      info.title + QLatin1Char(' ') + info.request.page_title);
  OnlineOpenContext online;
  online.bvid = info.request.bvid;
  online.cid = info.request.cid;
  online.title = info.title;
  online.owner = info.owner;
  online.qn = info.qn;
  online.codecid = info.codecid;
  online.fps = online_actual_fps_;
  online.duration_ms = info.duration_ms;
  online.total_bytes = info.total_bytes;
  online.cache_hit = true;

  const std::shared_ptr<lapian::media::MediaSession> previous = session_;
  OpenLocalMedia(info.cache_mp4_path, &online);
  if (session_ == previous || !online_ready_ || !online_cache_hit_ ||
      session_ == nullptr) {
    online_last_error_ = QStringLiteral("缓存命中打开失败：%1")
                             .arg(QDir::toNativeSeparators(info.cache_mp4_path));
    if (info_bar_ != nullptr) {
      info_bar_->SetError(online_last_error_);
    }
    statusBar()->showMessage(online_last_error_, 10000);
    online_mode_ = false;
    online_ready_ = false;
    return false;
  }
  if (online_cancel_action_ != nullptr) {
    online_cancel_action_->setEnabled(false);
  }
  if (session_ != nullptr) {
    online_fps_ = session_->info().video.frame_rate.ToDouble();
  }
  if (online_dash_fps_ <= 0.0) {
    online_actual_fps_ = online_fps_;
  }
  ApplyOnlineFrameRateUi();
  online_status_detail_ = BuildOnlineStatusDetail();
  if (status_label_ != nullptr) {
    status_label_->setText(base_info_ + online_status_detail_);
  }
  if (quality_button_ != nullptr) {
    quality_button_->setText(
        QStringLiteral("清晰度：%1").arg(lapian::online::QualityName(online_qn_)));
  }
  if (marks_ != nullptr) {
    marks_->SetOnlineContext(online_actual_fps_, online_qn_, online_codecid_);
    if (!marks_->marks().empty()) {
      marks_dirty_ = true;
      SaveMarks(false);
    }
  }
  RebuildOnlineQualityMenu();
  TryApplyPendingOnlineQualitySwitch();
  return true;
}

void MainWindow::LoadOnlineMarks(const OnlineOpenContext& online) {
  if (session_ == nullptr) {
    return;
  }
  lapian::core::MarkStoreOptions options;
  if (!online_records_root_for_test_.isEmpty()) {
    options.appdata_online_dir =
        QDir::toNativeSeparators(online_records_root_for_test_).toUtf8().toStdString();
  }
  lapian::core::FrameLookup lookup;
  const lapian::media::FrameIndex& index = session_->index();
  if (index.IsReady() && !index.empty()) {
    lookup.frame_count = static_cast<std::int64_t>(index.size());
    lookup.pts_of_frame = [&index](std::int64_t frame, std::int64_t* pts_us) {
      const lapian::media::FrameIndexEntry* entry = index.Entry(frame);
      if (entry == nullptr) {
        return false;
      }
      *pts_us = entry->pts_us;
      return true;
    };
    lookup.frame_of_pts = [&index](std::int64_t pts_us, std::int64_t* frame) {
      // T8.12：换档后不同流不是逐帧 PTS 一一对应；在 0.5s 内按最近 PTS
      // 映射标记，超出容差返回 false，避免部分索引把标记吸到末端。
      const std::int64_t nearest = index.FrameOfPts(pts_us);
      if (nearest < 0) {
        return false;
      }
      const std::int64_t nearest_pts = index.PtsOfFrame(nearest);
      const std::int64_t delta = nearest_pts - pts_us;
      if (delta > 500000 || delta < -500000) {
        return false;
      }
      *frame = nearest;
      return true;
    };
  }
  const double fps = online.fps > 0.0
                         ? online.fps
                         : session_->info().video.frame_rate.ToDouble();
  const QByteArray bvid_utf8 = online.bvid.toUtf8();
  marks_ = std::make_unique<lapian::core::MarkStore>(
      lapian::core::MarkStore::OpenOnline(
          std::string(bvid_utf8.constData(),
                      static_cast<std::size_t>(bvid_utf8.size())),
          online.cid, fps, online.qn, online.codecid, options, lookup));
  marks_dirty_ = false;
  if (mark_panel_ != nullptr) {
    mark_panel_->Clear();
  }
  RefreshMarksUi();
  ShowMarkWarnings();
}

void MainWindow::ReconcileOnlineMarks() {
  if (marks_ == nullptr || !marks_->online() || session_ == nullptr) {
    return;
  }
  const lapian::media::FrameIndex& index = session_->index();
  if (!index.IsReady() || index.empty()) {
    return;
  }
  lapian::core::FrameLookup lookup;
  lookup.frame_count = static_cast<std::int64_t>(index.size());
  lookup.pts_of_frame = [&index](std::int64_t frame, std::int64_t* pts_us) {
    const lapian::media::FrameIndexEntry* entry = index.Entry(frame);
    if (entry == nullptr) {
      return false;
    }
    *pts_us = entry->pts_us;
    return true;
  };
  lookup.frame_of_pts = [&index](std::int64_t pts_us, std::int64_t* frame) {
    const std::int64_t nearest = index.FrameOfPts(pts_us);
    if (nearest < 0) {
      return false;
    }
    const std::int64_t nearest_pts = index.PtsOfFrame(nearest);
    const std::int64_t delta = nearest_pts - pts_us;
    if (delta > 500000 || delta < -500000) {
      return false;
    }
    *frame = nearest;
    return true;
  };
  bool changed = false;
  marks_->ReconcileFrames(lookup, nullptr, &changed);
  if (!changed) {
    return;
  }
  marks_dirty_ = true;
  SaveMarks(false);
  RefreshMarksUi();
  ShowMarkWarnings();
  statusBar()->showMessage(
      QStringLiteral("在线记录帧率/帧号与当前流不同，已按 PTS 映射"), 6000);
}

void MainWindow::ApplyOnlineFrameRateUi() {
  if (!online_mode_ || session_ == nullptr) {
    online_actual_fps_ = 0.0;
    online_frame_rate_drop_ = false;
    if (info_bar_ != nullptr) {
      info_bar_->ClearOnlineFrameRateStatus();
    }
    return;
  }
  const lapian::online::OnlineFrameRateEvidence evidence{
      online_dash_fps_, online_fps_, online_qn_, online_source_fps_hint_};
  const lapian::online::OnlineFrameRateAssessment assessment =
      lapian::online::AssessOnlineFrameRate(evidence);
  online_actual_fps_ = assessment.actual_fps;
  online_frame_rate_drop_ = assessment.frame_drop;
  if (info_bar_ != nullptr) {
    info_bar_->SetOnlineFrameRateStatus(assessment.badge_text,
                                        assessment.banner_text);
  }
  if (assessment.frame_drop) {
    statusBar()->showMessage(
        QStringLiteral("%1（帧号只对当前流成立）").arg(assessment.status_text),
        12000);
  }
}

QString MainWindow::BuildOnlineStatusDetail() const {
  if (session_ == nullptr) {
    return QString();
  }
  const lapian::media::MediaInfo& media = session_->info();
  QString detail = QStringLiteral(" · 在线%1 · %2 · %3x%4")
                       .arg(online_cache_hit_ ? QStringLiteral("缓存命中")
                                              : QString(),
                            lapian::online::QualityName(online_qn_))
                       .arg(media.video.width)
                       .arg(media.video.height);
  const double container_fps = media.video.frame_rate.ToDouble();
  const double actual =
      online_actual_fps_ > 0.0 ? online_actual_fps_ : container_fps;
  if (actual > 0.0) {
    detail += QStringLiteral(" · 实际 %1fps")
                  .arg(lapian::online::FormatOnlineFps(actual));
  }
  if (actual > 0.0 && container_fps > 0.0 &&
      std::fabs(actual - container_fps) > 0.01) {
    // T8.12：实际流 fps 与容器 avg_frame_rate 不同时并列写证据。
    detail += QStringLiteral("（容器 %1fps）")
                  .arg(lapian::online::FormatOnlineFps(container_fps));
  }
  if (online_frame_rate_drop_) {
    detail += online_source_fps_hint_ > 59.5
                  ? QStringLiteral(" · 帧号与源片不同步/约 1/2")
                  : QStringLiteral(" · 可能降帧");
  }
  return detail;
}

void MainWindow::ApplyOnlineState(lapian::app::OnlineState state,
                                  const QString& message) {
  if (!online_mode_) {
    return;
  }
  using lapian::app::OnlineState;
  switch (state) {
    case OnlineState::kPreparing:
      status_label_->setText(base_info_ + QStringLiteral(" · 在线：准备取流…"));
      break;
    case OnlineState::kBuffering:
      status_label_->setText(base_info_ + QStringLiteral(" · 在线：缓冲中…"));
      break;
    case OnlineState::kCacheHit:
      status_label_->setText(base_info_ + QStringLiteral(" · 在线：缓存命中"));
      break;
    case OnlineState::kReady:
      // 首帧后状态栏保留档位/实际 fps/降帧提示，不被“已就绪”覆盖。
      break;
    case OnlineState::kRecovering:
      statusBar()->showMessage(message, 6000);
      break;
    case OnlineState::kError:
      online_last_error_ = message;
      ClearOnlineQualitySwitch();
      if (info_bar_ != nullptr) {
        info_bar_->SetError(message);
      }
      status_label_->setText(base_info_ + QStringLiteral(" · 在线错误"));
      statusBar()->showMessage(QStringLiteral("在线错误：%1").arg(message), 12000);
      break;
    case OnlineState::kClosed:
      break;
    default:
      break;
  }
}

void MainWindow::ApplyOnlineProgress(
    const lapian::app::OnlineProgress& progress) {
  if (!online_mode_) {
    return;
  }
  online_published_bytes_ = progress.published;
  // T8.11：首帧后 online_total_bytes_ 只代表视频流（索引/缓存进度分项），
  // 不能被 DASH 视频+音频的合并 progress.total 覆盖。
  if (!online_ready_) {
    online_total_bytes_ = progress.total;
  }
  if (online_ready_) {
    return;  // 首帧后不覆盖播放/seek 状态栏；下载继续在后台。
  }
  const double percent =
      progress.total > 0
          ? 100.0 * static_cast<double>(progress.published) /
                static_cast<double>(progress.total)
          : 0.0;
  const QString bytes =
      QStringLiteral("%1/%2")
          .arg(lapian::online::CacheStore::FormatBytes(progress.published),
               lapian::online::CacheStore::FormatBytes(progress.total));
  const QString speed =
      progress.bps > 0.0
          ? QStringLiteral(" · %1/s").arg(
                lapian::online::CacheStore::FormatBytes(
                    static_cast<qint64>(progress.bps)))
          : QString();
  status_label_->setText(
      base_info_ +
      QStringLiteral(" · 在线：缓冲中 %1%（%2%3）")
          .arg(QString::number(percent, 'f', 0), bytes, speed));
  statusBar()->showMessage(
      QStringLiteral("缓冲中 %1% · %2%3 · Ctrl+Shift+U 取消")
          .arg(QString::number(percent, 'f', 0), bytes, speed));
}

void MainWindow::StopOnlineIndexBuild() {
  ++online_index_generation_;
  online_index_pending_.reset();
  online_index_pending_generation_ = 0;
  if (online_index_cancel_) {
    online_index_cancel_->store(true, std::memory_order_relaxed);
  }
  if (online_index_thread_.joinable()) {
    online_index_thread_.join();
  }
  online_index_cancel_.reset();
}

void MainWindow::InstallPendingOnlineIndex() {
  if (!online_index_pending_ || online_seek_running_) {
    return;
  }
  auto built = std::move(online_index_pending_);
  online_index_pending_.reset();
  if (online_index_pending_generation_ != online_index_generation_) {
    return;
  }
  InstallOnlineIndexSnapshot(std::move(built));
}

void MainWindow::InstallOnlineIndexSnapshot(
    std::shared_ptr<lapian::media::OnlineIndexResult> built) {
  if (!built || !built->ok || !online_mode_ || session_ == nullptr) {
    return;
  }
  // 帧号前缀必须稳定；若新快照在已暴露前缀上出现不同 PTS，不安装，
  // 避免 UI 帧号漂移（T8.6 语义不变）。
  const lapian::media::FrameIndex& current = session_->index();
  const std::size_t compare = std::min(current.size(), built->index.size());
  for (std::size_t i = 0; i < compare; ++i) {
    const lapian::media::FrameIndexEntry* a =
        current.Entry(static_cast<std::int64_t>(i));
    const lapian::media::FrameIndexEntry* b =
        built->index.Entry(static_cast<std::int64_t>(i));
    if (a != nullptr && b != nullptr && a->pts_us != b->pts_us) {
      if (qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG")) {
        std::fprintf(stderr,
                     "T8.11 online_index_prefix_changed frame=%zu old=%lld "
                     "new=%lld\n",
                     i, static_cast<long long>(a->pts_us),
                     static_cast<long long>(b->pts_us));
        std::fflush(stderr);
      }
      return;
    }
  }
  const bool first_install = online_stable_frames_ == 0;
  const int previous_stable = online_stable_frames_;
  online_stable_frames_ = built->stable_frames;
  online_index_complete_ = built->complete;
  online_spans_ = std::move(built->spans);
  const double build_ms = built->build_ms;
  session_->InstallOnlineIndex(std::move(built->index), build_ms);
  ApplyIndexState();
  ReconcileOnlineMarks();
  if (mark_action_ != nullptr) {
    mark_action_->setEnabled(online_stable_frames_ > 0);
  }
  if (online_index_complete_) {
    statusBar()->showMessage(
        QStringLiteral("在线索引完成：%1 帧").arg(online_stable_frames_), 5000);
  } else if (first_install ||
             (previous_stable > 0 &&
              online_stable_frames_ - previous_stable >= 30)) {
    statusBar()->showMessage(
        QStringLiteral("在线索引：已缓存 %1 帧（扫描中）")
            .arg(online_stable_frames_),
        2000);
  }
}

void MainWindow::StartOnlineIndexBuild() {
  StopOnlineIndexBuild();
  online_index_stats_.clear();
  if (!online_ready_ || session_ == nullptr || !session_->IsGrowing()) {
    return;
  }
  const auto source = session_->growing_source_for_index();
  if (!source) {
    return;
  }
  const std::uint64_t generation = online_index_generation_;
  auto cancel = std::make_shared<std::atomic<bool>>(false);
  online_index_cancel_ = cancel;
  const std::string part = online_part_path_.toUtf8().toStdString();
  const int stream = session_->video_stream_index();
  try {
    online_index_thread_ =
        std::thread([this, generation, source, part, stream, cancel]() {
          std::unique_ptr<lapian::media::IncrementalOnlineIndexBuilder> builder;
          std::string create_error;
          int create_failures = 0;
          int last_posted_total = -1;
          auto last_post = std::chrono::steady_clock::now() -
                           std::chrono::seconds(1);
          while (!cancel->load(std::memory_order_relaxed)) {
            if (!builder) {
              builder = lapian::media::IncrementalOnlineIndexBuilder::Create(
                  source, part, stream, 48, &create_error);
              if (!builder) {
                if (++create_failures == 1 ||
                    create_failures % 20 == 0) {
                  std::fprintf(
                      stderr,
                      "T8.11 online_index_open_wait attempts=%d published=%lld "
                      "error=%s\n",
                      create_failures,
                      static_cast<long long>(source->Published()),
                      create_error.c_str());
                  std::fflush(stderr);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
              }
              create_failures = 0;
            }
            const lapian::media::OnlineIndexStepInfo step =
                builder->Step(cancel.get(), 40, 8192);
            if (cancel->load(std::memory_order_relaxed)) {
              return;
            }
            if (!step.ok) {
              if (step.cancelled) {
                return;
              }
              std::fprintf(stderr,
                           "T8.11 online_index_step_failed error=%s "
                           "total=%d complete=%d\n",
                           step.error.c_str(), step.total_frames,
                           step.complete ? 1 : 0);
              std::fflush(stderr);
              return;
            }
            const auto now = std::chrono::steady_clock::now();
            const bool due =
                step.complete ||
                (now - last_post >= std::chrono::milliseconds(250) &&
                 builder->total_frames() != last_posted_total);
            if (due) {
              auto built =
                  std::make_shared<lapian::media::OnlineIndexResult>();
              if (builder->Snapshot(built.get())) {
                last_post = now;
                last_posted_total = builder->total_frames();
                const lapian::media::OnlineIndexStats stats = builder->stats();
                const QString stats_text =
                    QStringLiteral(
                        "steps=%1 opens=%2 reopens=%3 skips=%4 from_start=%5 "
                        "packets=%6 parsed=%7 read_bytes=%8 max_step_ms=%9 "
                        "complete=%10")
                        .arg(stats.steps)
                        .arg(stats.opens)
                        .arg(stats.reopens)
                        .arg(stats.resume_skips)
                        .arg(stats.resume_from_start)
                        .arg(stats.packets)
                        .arg(stats.parsed_frames)
                        .arg(stats.read_bytes)
                        .arg(QString::number(stats.max_step_ms, 'f', 2))
                        .arg(builder->complete() ? 1 : 0);
                QMetaObject::invokeMethod(
                    this,
                    [this, generation, built, stats_text]() {
                      if (generation != online_index_generation_ ||
                          !online_mode_ || session_ == nullptr) {
                        return;
                      }
                      online_index_stats_ = stats_text;
                      if (online_seek_running_) {
                        // seek worker 正在读同一个 FrameIndex；等它结束后
                        // 再安装最新快照，complete 快照不能被丢掉。
                        online_index_pending_ = built;
                        online_index_pending_generation_ = generation;
                        return;
                      }
                      InstallOnlineIndexSnapshot(std::move(built));
                    },
                    Qt::QueuedConnection);
              }
            }
            if (step.complete) {
              return;
            }
            if (step.at_frontier || (!step.appended && !step.budget_hit)) {
              // 等下载增长；guard 保证正常情况下不会在这里撞 ETIMEDOUT。
              std::this_thread::sleep_for(std::chrono::milliseconds(120));
            }
          }
        });
  } catch (...) {
    statusBar()->showMessage(QStringLiteral("在线索引线程启动失败"), 5000);
  }
}

void MainWindow::StopOnlineSeekWorker() {
  ++online_seek_generation_;
  if (!online_seek_running_) {
    if (online_seek_thread_.joinable()) {
      online_seek_thread_.join();
    }
    online_seek_cancel_.reset();
    return;
  }
  if (online_seek_cancel_) {
    online_seek_cancel_->store(true, std::memory_order_relaxed);
  }
  if (session_) {
    session_->InterruptGrowingReads();
  }
  if (online_seek_thread_.joinable()) {
    online_seek_thread_.join();
    online_session_poisoned_ = true;
  }
  online_seek_cancel_.reset();
  online_seek_running_ = false;
}

bool MainWindow::OnlineFrameCached(std::int64_t frame) const {
  if (session_ == nullptr || !session_->IsGrowing()) {
    return false;
  }
  if (session_->GrowingComplete()) {
    return true;
  }
  const lapian::media::FrameIndex& index = session_->index();
  if (!index.IsReady() || frame < 0 ||
      frame >= static_cast<std::int64_t>(online_spans_.size()) ||
      frame >= static_cast<std::int64_t>(index.size())) {
    return false;
  }
  const lapian::media::OnlineFrameSpan& span =
      online_spans_[static_cast<std::size_t>(frame)];
  if (span.pos < 0) {
    return false;
  }
  const lapian::media::FrameIndexEntry* entry = index.Entry(frame);
  if (entry == nullptr) {
    return false;
  }
  const std::int64_t keyframe = index.KeyframeAtOrBefore(entry->pts_us);
  std::int64_t need_start = span.pos;
  if (keyframe >= 0 &&
      keyframe < static_cast<std::int64_t>(online_spans_.size())) {
    const auto& key_span =
        online_spans_[static_cast<std::size_t>(keyframe)];
    if (key_span.pos >= 0) {
      need_start = std::min(need_start, key_span.pos);
    }
  }
  const std::int64_t need_end = span.pos + span.size;
  return need_start >= 0 && need_end > 0 &&
         need_end + 64 * 1024 <= session_->GrowingPublished();
}

bool MainWindow::OnlineStepEligible() const {
  // 缓存命中走本地全量索引/同步 seek，不能套用在线状态机；只有正在增长的
  // GrowingSource 会话才需要异步步进。
  return online_mode_ && online_ready_ && !online_cache_hit_ &&
         session_ != nullptr && session_->IsOpen() && session_->IsGrowing();
}

std::int64_t MainWindow::OnlineLastFrame() const {
  if (session_ == nullptr || !session_->IsOpen()) {
    return -1;
  }
  // 与 T8.6 GoToFrameInternal 的边界口径完全一致：完整索引优先，其次容器
  // 总帧数，最后退回部分索引稳定帧数。不要在这里另造一套“缓存边界”。
  if (online_index_complete_ && online_stable_frames_ > 0) {
    return static_cast<std::int64_t>(online_stable_frames_) - 1;
  }
  const lapian::core::TotalFrameCount container = ContainerTotal();
  if (container.has_value()) {
    return container.value() - 1;
  }
  if (online_stable_frames_ > 0) {
    return static_cast<std::int64_t>(online_stable_frames_) - 1;
  }
  return -1;
}

void MainWindow::ShowOnlineWaitPrompt(std::int64_t target) {
  statusBar()->showMessage(
      QStringLiteral("缓冲中（目标帧 %1）").arg(target), 8000);
  if (status_label_ != nullptr) {
    status_label_->setText(
        base_info_ +
        QStringLiteral(" · 在线：缓冲中（目标帧 %1）").arg(target));
  }
}

void MainWindow::ResetOnlineStepState() {
  online_step_.has_desired = false;
  online_step_.desired = 0;
  online_step_.inflight = false;
  online_step_.inflight_target = -1;
  online_step_.inflight_cached = false;
  online_step_.queued = false;
  online_step_.queued_target = -1;
  online_step_.queued_cached = false;
  online_step_.queued_direction = 0;
}

void MainWindow::IssueOnlineStepSeek(std::int64_t target, bool cached) {
  online_step_.has_desired = true;
  online_step_.desired = target;
  online_step_.inflight = true;
  online_step_.inflight_target = target;
  online_step_.inflight_cached = cached;
  ++online_step_.seeks;
  const bool exact = session_ != nullptr && session_->index().IsReady() &&
                     !session_->index().empty() &&
                     target <
                         static_cast<std::int64_t>(session_->index().size());
  if (cached) {
    statusBar()->showMessage(
        QStringLiteral("第 %1 帧（已缓存）").arg(target), 1200);
  } else {
    ShowOnlineWaitPrompt(target);
  }
  RequestOnlineSeek(target, lapian::media::FrameCacheMode::kSeekHandoff,
                    exact);
  if (!online_seek_running_) {
    // 线程没起来（错误已由 RequestOnlineSeek 提示）：不要在途状态卡住步进。
    online_step_.inflight = false;
    online_step_.queued = false;
    online_step_.queued_direction = 0;
  }
}

bool MainWindow::ApplyOnlineStep(int direction) {
  if (direction == 0) {
    return false;
  }
  if (!OnlineStepEligible()) {
    if (qEnvironmentVariableIsSet("LAPIAN_KEY_LOG")) {
      std::fprintf(stderr,
                   "T8.6.1a step_reject eligible=0 online=%d ready=%d cache_hit=%d "
                   "open=%d growing=%d\n",
                   online_mode_ ? 1 : 0, online_ready_ ? 1 : 0,
                   online_cache_hit_ ? 1 : 0,
                   session_ != nullptr && session_->IsOpen() ? 1 : 0,
                   session_ != nullptr && session_->IsGrowing() ? 1 : 0);
      std::fflush(stderr);
    }
    return false;
  }
  const std::int64_t last = OnlineLastFrame();
  if (last < 0) {
    if (qEnvironmentVariableIsSet("LAPIAN_KEY_LOG")) {
      std::fprintf(stderr,
                   "T8.6.1a step_reject last=-1 stable=%lld container_has=%d\n",
                   static_cast<long long>(online_stable_frames_),
                   ContainerTotal().has_value() ? 1 : 0);
      std::fflush(stderr);
    }
    statusBar()->showMessage(
        QStringLiteral("在线索引扫描中，目标帧尚不可定位"), 3000);
    return false;  // 调用方停止连发
  }
  const bool step_seek_running = online_step_.inflight && online_seek_running_;
  const bool foreign_seek_running = online_seek_running_ && !step_seek_running;
  if (foreign_seek_running) {
    // 时间轴/Home/End/标记跳转在途：按“已请求的目标帧”±1（状态栏显示的是
    // 目标帧，用户预期从它开始），而不是从旧画面开始。
    ResetOnlineStepState();
  }

  std::int64_t base = current_frame_;
  if (online_step_.inflight) {
    base = online_step_.queued ? online_step_.queued_target
                               : online_step_.inflight_target;
  } else if (foreign_seek_running && online_seek_target_ >= 0) {
    base = online_seek_target_;
  }
  const std::int64_t delta = direction > 0 ? 1 : -1;
  const std::int64_t next = base + delta;
  if (next < 0 || next > last) {
    ++online_step_.boundary_stops;
    if (qEnvironmentVariableIsSet("LAPIAN_KEY_LOG")) {
      std::fprintf(stderr,
                   "T8.6.1a step_boundary dir=%d base=%lld next=%lld last=%lld "
                   "stable=%lld frame=%lld\n",
                   direction, static_cast<long long>(base),
                   static_cast<long long>(next), static_cast<long long>(last),
                   static_cast<long long>(online_stable_frames_),
                   static_cast<long long>(current_frame_));
      std::fflush(stderr);
    }
    if (!online_step_.inflight) {
      statusBar()->showMessage(
          direction > 0 ? QStringLiteral("已是最后一帧")
                        : QStringLiteral("已是第一帧"),
          3000);
    }
    // 在途的一步仍会落地；不再排新目标，调用方停止连发。
    return false;
  }

  if (!online_step_.inflight) {
    ResetOnlineStepState();  // 清掉上次序列的排队/在途残留
    const bool cached = OnlineFrameCached(next);
    IssueOnlineStepSeek(next, cached);
    return true;
  }

  const bool next_cached = OnlineFrameCached(next);
  if (online_step_.queued) {
    // 队列只有一格：同向 tick 丢弃（不跳帧）；反向 tick 若走回在途目标则
    // 直接抵消，否则替换队列。
    if (next == online_step_.inflight_target) {
      online_step_.queued = false;
      online_step_.queued_target = -1;
      online_step_.queued_direction = 0;
      return true;
    }
    if (online_step_.queued_direction != direction) {
      online_step_.queued_target = next;
      online_step_.queued_cached = next_cached;
      online_step_.queued_direction = direction;
    }
    return true;
  }

  if (!online_step_.inflight_cached && !next_cached) {
    // 未缓存区：不要在 70ms tick 上反复取消在途 seek。seek 慢于 tick 时，
    // “最新目标重定向”会让每一步都被下一步取消，长按永远只落 1 帧。
    // 单格排队让当前 seek 有机会落地，落地后再走下一格；不承诺逐帧立即呈现
    // （OP3），但不会因为互相取消而停住。
    online_step_.queued = true;
    online_step_.queued_target = next;
    online_step_.queued_cached = false;
    online_step_.queued_direction = direction;
    online_step_.desired = next;
    ShowOnlineWaitPrompt(next);
    return true;
  }

  // 已缓存区：目标先排队，等当前帧落地后再走一步，保证屏幕逐帧连续。
  online_step_.queued = true;
  online_step_.queued_target = next;
  online_step_.queued_cached = next_cached;
  online_step_.queued_direction = direction;
  return true;
}

void MainWindow::OnOnlineStepLanded(std::int64_t target) {
  if (!online_step_.inflight || online_step_.inflight_target != target) {
    return;  // 非步进 seek 或已被新目标取代
  }
  online_step_.inflight = false;
  if (online_step_.trace.size() < 512) {
    online_step_.trace.push_back(target);
  }
  if (!online_step_.queued) {
    return;
  }
  const std::int64_t next = online_step_.queued_target;
  online_step_.queued = false;
  online_step_.queued_target = -1;
  online_step_.queued_direction = 0;
  // 落地时重新判断缓存状态：排队期间下载可能已经追上。
  IssueOnlineStepSeek(next, OnlineFrameCached(next));
}

void MainWindow::OnOnlineStepFailed(std::int64_t target) {
  if (!online_step_.inflight || online_step_.inflight_target != target) {
    return;
  }
  ResetOnlineStepState();
  StopStepRepeat("seek_failed");  // 失败后不要继续按 70ms 重试
}

void MainWindow::RequestOnlineSeek(std::int64_t target,
                                   lapian::media::FrameCacheMode mode,
                                   bool exact_index_covered) {
  if (!online_ready_ || session_ == nullptr || !session_->IsGrowing() ||
      !session_->growing_source_for_index()) {
    return;
  }
  // 取消上一个 seek：单读者 interrupt 不会取消共享下载源。
  if (online_seek_running_) {
    if (online_seek_cancel_) {
      online_seek_cancel_->store(true, std::memory_order_relaxed);
    }
    session_->InterruptGrowingReads();
    if (online_seek_thread_.joinable()) {
      online_seek_thread_.join();
    }
    online_seek_running_ = false;
    online_session_poisoned_ = true;
    InstallPendingOnlineIndex();
  }
  // 上一 worker 成功结束后 std::thread 仍 joinable；不先 join 就 move 赋值会
  // std::terminate（0xC0000409）。
  if (online_seek_thread_.joinable()) {
    online_seek_thread_.join();
  }
  const std::uint64_t generation = ++online_seek_generation_;
  auto cancel = std::make_shared<std::atomic<bool>>(false);
  online_seek_cancel_ = cancel;
  const auto source = session_->growing_source_for_index();
  const std::string part = online_part_path_.toUtf8().toStdString();
  const std::string display = online_display_path_.toUtf8().toStdString();
  auto current_session = session_;
  const bool needs_new_session = online_session_poisoned_;
  const bool exact = exact_index_covered && session_->index().IsReady() &&
                     target < static_cast<std::int64_t>(session_->index().size());
  // T8.12：切档后的最终提示只跟这一次 seek 走，不能被后续 seek 复用。
  const QString completion_notice = online_seek_completion_notice_;
  online_seek_completion_notice_.clear();
  if (qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG")) {
    std::fprintf(stderr,
                 "T8.10 online_seek_request target=%lld exact=%d index=%zu\n",
                 static_cast<long long>(target), exact ? 1 : 0,
                 session_->index().IsReady() ? session_->index().size() : 0);
    std::fflush(stderr);
  }
  // T8.6.1a：重建会话时总是带上当前部分索引（不只 exact 目标）。否则中断一个
  // 未缓存等待后，新会话没有索引，CanStepFrames/OnlineFrameCached 会短暂失效，
  // 在线长按会停在重建窗口里。
  lapian::media::FrameIndex index_copy =
      (needs_new_session && session_->index().IsReady() &&
       !session_->index().empty())
          ? session_->index()
          : lapian::media::FrameIndex{};
  ID3D11Device* device = gfx_ != nullptr ? gfx_->d3d_device() : nullptr;
  const lapian::media::VideoStreamInfo video = session_->info().video;
  online_seek_running_ = true;
  online_seek_target_ = target;

  try {
    online_seek_thread_ = std::thread(
        [this, generation, cancel, source, part, display, current_session,
         needs_new_session, exact, index_copy, device, video, target, mode,
         completion_notice]() {
          struct Result {
            std::shared_ptr<lapian::media::MediaSession> session;
            bool new_session = false;
            lapian::media::DecodedFrameInfo info;
            std::shared_ptr<AVFrame> frame;
            QString error;
            bool cancelled = false;
          };
          auto result = std::make_shared<Result>();
          result->new_session = needs_new_session;
          std::shared_ptr<lapian::media::MediaSession> use = current_session;
          if (needs_new_session) {
            auto candidate =
                std::make_shared<lapian::media::MediaSession>();
            std::string open_error;
            lapian::media::GrowingOpenOptions reopen_options;
            reopen_options.dash = online_dash_audio_;
            reopen_options.duration_ms = online_dash_duration_ms_;
            reopen_options.fps = online_dash_fps_;
            if (online_dash_audio_) {
              reopen_options.audio.has_audio = true;
              reopen_options.audio.stream_index = 0;
              reopen_options.audio.codec = "aac";
            }
            if (!candidate->OpenGrowingWithOptions(source, part, display, device,
                                                   &open_error, reopen_options,
                                                   true)) {
              result->error = QStringLiteral("重建在线会话失败：%1")
                                  .arg(QString::fromStdString(open_error));
              QMetaObject::invokeMethod(
                  this,
                  [this, generation, result]() {
                    if (generation != online_seek_generation_ || !online_mode_) {
                      return;
                    }
                    online_seek_running_ = false;
                    online_last_error_ = result->error;
                    statusBar()->showMessage(result->error, 8000);
                    InstallPendingOnlineIndex();
                  },
                  Qt::QueuedConnection);
              return;
            }
            if (index_copy.IsReady() && !index_copy.empty()) {
              candidate->InstallOnlineIndex(index_copy, 0.0);
            }
            use = candidate;
          }
          if (cancel->load(std::memory_order_relaxed)) {
            result->cancelled = true;
            return;
          }
          // 让本次 seek 的阻塞读可由新目标/关窗中断。
          use->SetGrowingInterrupt(cancel);
          std::string seek_error;
          bool ok = false;
          if (exact) {
            ok = use->SeekToFrame(target, &result->info, &seek_error, mode);
          } else {
            const double fps = video.frame_rate.ToDouble();
            if (fps <= 0.0) {
              result->error = QStringLiteral("容器没有可用帧率，无法定位未索引帧");
            } else {
              std::int64_t start_us = 0;
              if (video.start_time != lapian::media::kNoTimestamp &&
                  video.time_base.valid()) {
                const AVRational tb{video.time_base.num, video.time_base.den};
                start_us = av_rescale_q(video.start_time, tb, AV_TIME_BASE_Q);
              }
              const std::int64_t target_us =
                  start_us + static_cast<std::int64_t>(std::llround(
                                 static_cast<double>(target) * 1.0e6 / fps));
              ok = use->SeekToEstimatedPts(target_us, target, &result->info,
                                           &seek_error);
            }
            if (!ok && result->error.isEmpty()) {
              result->error = QString::fromStdString(seek_error);
            }
          }
          if (cancel->load(std::memory_order_relaxed)) {
            result->cancelled = true;
            return;
          }
          if (!ok) {
            if (result->error.isEmpty()) {
              result->error = QString::fromStdString(
                  use->GrowingInterrupted() ? "seek 已取消" : "seek 失败");
            }
            QMetaObject::invokeMethod(
                this,
                [this, generation, result, use, target]() {
                  if (generation != online_seek_generation_ || !online_mode_) {
                    return;
                  }
                  online_seek_running_ = false;
                  online_session_poisoned_ = true;
                  online_last_error_ = result->error;
                  statusBar()->showMessage(
                      QStringLiteral("跳帧失败：%1").arg(result->error), 6000);
                  OnOnlineStepFailed(target);
                  InstallPendingOnlineIndex();
                },
                Qt::QueuedConnection);
            return;
          }
          AVFrame* clone = av_frame_clone(result->info.av_frame);
          if (clone == nullptr) {
            result->error = QStringLiteral("复制目标帧失败");
            return;
          }
          result->frame = std::shared_ptr<AVFrame>(
              clone, [](AVFrame* frame) { av_frame_free(&frame); });
          result->info.av_frame = result->frame.get();
          result->session = use;
          QMetaObject::invokeMethod(
              this,
              [this, generation, result, target, completion_notice]() {
                if (generation != online_seek_generation_ || !online_mode_) {
                  return;
                }
                online_seek_running_ = false;
                online_seek_cancel_.reset();
                if (result->new_session && result->session) {
                  if (playback_) {
                    playback_->Pause();
                    playback_->SetAudioEngine(nullptr);
                    playback_->SetSession(nullptr);
                  }
                  session_ = result->session;
                  if (playback_) {
                    playback_->SetSession(session_.get());
                    playback_->SetAudioEngine(audio_.get());
                    playback_->SetRate(rate_);
                  }
                }
                if (!session_ || !result->frame) {
                  return;
                }
                if (qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG")) {
                  std::fprintf(stderr,
                               "T8.10 online_seek_land target=%lld pts_us=%lld "
                               "actual_frame=%lld\n",
                               static_cast<long long>(target),
                               static_cast<long long>(result->info.pts_us),
                               static_cast<long long>(CurrentFrameForTest()));
                  std::fflush(stderr);
                }
                online_session_poisoned_ = false;
                const int rotation =
                    session_->info().rotation.counterclockwise_degrees;
                gfx_->SetVideoFrame(result->frame.get(), rotation);
                RememberDisplayedFrame(result->frame.get(), rotation, target);
                info_bar_->SetFrame(static_cast<int>(target));
                timeline_->SetCurrentFrame(target);
                if (playback_) {
                  playback_->SetPosition(target, result->info.pts_us);
                }
                current_frame_ = target;
                current_pts_us_ = result->info.pts_us;
                last_navigation_error_.clear();
                // T8.6.1a：落地后撤掉“缓冲中（目标帧 N）”，状态栏恢复档位/帧率。
                if (status_label_ != nullptr && !online_status_detail_.isEmpty()) {
                  status_label_->setText(base_info_ + online_status_detail_);
                }
                if (!completion_notice.isEmpty()) {
                  statusBar()->showMessage(completion_notice, 12000);
                } else {
                  statusBar()->showMessage(
                      QStringLiteral("第 %1 帧（在线）").arg(target), 2000);
                }
                InstallPendingOnlineIndex();
                OnOnlineStepLanded(target);
              },
              Qt::QueuedConnection);
        });
  } catch (...) {
    online_seek_running_ = false;
    statusBar()->showMessage(QStringLiteral("在线 seek 线程启动失败"), 5000);
    InstallPendingOnlineIndex();
  }
}

void MainWindow::CancelOnlineLoading() {
  if (!online_ && !online_mode_) {
    return;
  }
  if (playback_) {
    playback_->Pause();
  }
  if (online_) {
    online_->Cancel();
  }
  ShowEmptyState();
  statusBar()->showMessage(
      QStringLiteral("已取消在线加载；缓存 .part 已保留，可再次打开续传"), 8000);
}

void MainWindow::ShutdownOnlineSession(bool finalize_if_complete) {
  (void)finalize_if_complete;  // 完整缓存总是落盘；未完整时该参数不改变取消语义
  if (!online_ && !online_mode_) {
    return;
  }
  const bool log = qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG");
  auto mark = [log](const char* step) {
    if (log) {
      std::fprintf(stderr, "T8.7 online_shutdown step=%s\n", step);
      std::fflush(stderr);
    }
  };
  mark("start");
  if (playback_) {
    playback_->Pause();
  }
  // 先取消下载并唤醒所有阻塞读（含预览的独立 cursor），再停预览/seek/索引；
  // 预览 reader 必须在 finalize 的 `.part`→`.mp4` 改名之前 close。
  if (online_) {
    online_->Cancel();
  }
  mark("cancelled");
  StopPreview();
  mark("preview_stopped");
  StopOnlineSeekWorker();
  mark("seek_stopped");
  StopOnlineIndexBuild();
  mark("index_stopped");
  if (playback_) {
    playback_->SetAudioEngine(nullptr);
  }
  if (audio_) {
    audio_->Close();
    audio_.reset();
  }
  if (waveform_) {
    waveform_->Cancel();
    waveform_.reset();
  }
  mark("audio_waveform_closed");
  if (session_) {
    if (playback_) {
      playback_->SetSession(nullptr);
    }
    session_->Close();
    session_.reset();
  }
  mark("session_closed");
  // HasPendingFinalize 只会在下载已完整、写句柄已关闭、进入 awaiting 时为真。
  // 即使调用方传 false（切到另一个在线/本地文件），也应把已完整的数据落盘；
  // 未完整的下载不会进入这里，仍按取消语义保留 .part。
  if (online_ && online_->HasPendingFinalize()) {
    mark("finalize_begin");
    QString error;
    if (!online_->FinalizeAfterReadersStopped(&error)) {
      online_last_error_ = error;
      statusBar()->showMessage(
          QStringLiteral("缓存落盘失败（已保留 .part）：%1").arg(error), 10000);
    } else {
      online_finalized_ = true;
    }
    mark("finalize_end");
  }
  if (online_) {
    online_->Close();
    online_.reset();
  }
  mark("online_closed");
  if (playback_) {
    playback_->SetDecodeGate({});
    playback_->SetBufferOnAudioStall(false);
  }
  if (online_cancel_action_ != nullptr) {
    online_cancel_action_->setEnabled(false);
  }
  online_mode_ = false;
  online_ready_ = false;
  online_cache_hit_ = false;
  online_cache_path_.clear();
  online_owner_.clear();
  online_fps_ = 0.0;
  online_actual_fps_ = 0.0;
  online_source_fps_hint_ = 0.0;
  online_frame_rate_drop_ = false;
  lapian::online::ClearSecret(&online_request_.sessdata);
  online_request_ = lapian::app::OnlineRequest{};
  online_logged_in_ = true;
  online_seek_completion_notice_.clear();
  online_stable_frames_ = 0;
  online_index_complete_ = false;
  online_spans_.clear();
  online_index_pending_.reset();
  online_index_pending_generation_ = 0;
  online_index_stats_.clear();
  online_part_path_.clear();
  online_display_path_.clear();
  online_published_bytes_ = 0;
  online_total_bytes_ = 0;
  online_dash_audio_ = false;
  online_audio_source_.reset();
  online_audio_part_path_.clear();
  online_audio_id_ = 0;
  online_audio_total_bytes_ = 0;
  online_dash_duration_ms_ = 0;
  online_dash_fps_ = 0.0;
  // T8.6.1a：会话结束时丢弃步进请求/在途标记；seek worker 已在上面 join。
  online_step_ = OnlineStepState{};
  online_seek_target_ = -1;
  online_status_detail_.clear();
  ClearOnlineQualitySwitch();
  if (info_bar_ != nullptr) {
    info_bar_->ClearOnlineFrameRateStatus();
  }
  ResetOnlineQuality();
  mark("end");
}

QString MainWindow::OnlineStateForTest() const {
  if (!online_) {
    return QStringLiteral("none");
  }
  const QString state = lapian::app::OnlineStateName(online_->state());
  const lapian::app::OnlineProgress progress = online_->progress();
  return QStringLiteral("state=%1 ready=%2 published=%3 total=%4 qn=%5")
      .arg(state)
      .arg(online_ready_ ? 1 : 0)
      .arg(progress.published)
      .arg(progress.total)
      .arg(progress.qn);
}

QString MainWindow::OnlineProgressForTest() const {
  if (!online_) {
    return QStringLiteral("none");
  }
  const lapian::app::OnlineProgress progress = online_->progress();
  return QStringLiteral("published=%1 total=%2 bps=%3 job=%4 qn=%5 codecid=%6")
      .arg(progress.published)
      .arg(progress.total)
      .arg(QString::number(progress.bps, 'f', 0), progress.job_state)
      .arg(progress.qn)
      .arg(progress.codecid);
}

QString MainWindow::OnlineLastErrorForTest() const { return online_last_error_; }

qint64 MainWindow::OnlineStableFramesForTest() const {
  return online_stable_frames_;
}

bool MainWindow::OnlinePlayingForTest() const {
  return playback_ != nullptr && playback_->IsPlaying();
}

double MainWindow::OnlinePlaybackSecondsForTest() const {
  return playback_ != nullptr ? playback_->CurrentMediaSeconds() : 0.0;
}

bool MainWindow::OnlineAudioClockForTest(double* seconds) const {
  if (seconds == nullptr || audio_ == nullptr) {
    return false;
  }
  return audio_->ClockSeconds(seconds);
}

double MainWindow::OnlineAudioPositionForTest() const {
  return audio_ != nullptr ? audio_->status().position_seconds : 0.0;
}

int MainWindow::OnlineAudioStateForTest() const {
  return audio_ != nullptr ? static_cast<int>(audio_->status().state) : -1;
}

bool MainWindow::OnlineIndexReadyForTest() const {
  return session_ != nullptr && session_->index().IsReady() &&
         !session_->index().empty();
}

int MainWindow::OnlineIndexFramesForTest() const {
  return OnlineIndexReadyForTest()
             ? static_cast<int>(session_->index().size())
             : 0;
}

bool MainWindow::WaitForOnlineIndexFramesForTest(int min_frames,
                                                 int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    if (online_index_complete_ ||
        (OnlineIndexReadyForTest() &&
         OnlineIndexFramesForTest() >= min_frames)) {
      return true;
    }
    if (!online_mode_ || online_ == nullptr ||
        online_->state() == lapian::app::OnlineState::kError) {
      return false;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  return OnlineIndexReadyForTest() &&
         (online_index_complete_ || OnlineIndexFramesForTest() >= min_frames);
}

bool MainWindow::WaitForOnlineCacheCompleteForTest(int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    if (online_ != nullptr && online_->progress().complete) {
      return true;
    }
    if (!online_mode_ || online_ == nullptr ||
        online_->state() == lapian::app::OnlineState::kError) {
      return false;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  return online_ != nullptr && online_->progress().complete;
}

void MainWindow::SetOnlineDashEnabledForTest(bool enabled) {
  online_dash_enabled_ = enabled;
  if (online_ != nullptr) {
    online_->SetDashEnabledForTest(enabled);
  }
}

void MainWindow::PausePlaybackForTest() {
  if (playback_ != nullptr) {
    playback_->Pause();
  }
}

qint64 MainWindow::OnlineContainerFramesForTest() const {
  const lapian::core::TotalFrameCount total = ContainerTotal();
  return total.has_value() ? total.value() : 0;
}

void MainWindow::SetOnlineDownloadThrottleForTest(qint64 bytes_per_sec) {
  online_download_throttle_bps_ = bytes_per_sec > 0 ? bytes_per_sec : 0;
  if (online_) {
    // 只节流视频：波形渐进已由离线 slow case 覆盖；音频跟视频同速会让
    // CDN 音频首两个尝试在慢回调下失败（重试可恢复，但证据噪音大）。
    online_->SetDownloadThrottleForTest(online_download_throttle_bps_, 0);
  }
}

QString MainWindow::OnlineTotalStateForTest() const {
  if (session_ == nullptr) {
    return QStringLiteral("unknown");
  }
  const bool online_growing = online_ready_ && !online_cache_hit_;
  if (!online_growing) {
    const lapian::core::TotalFrameCount total = ContainerTotal();
    return total.IsExact() ? QStringLiteral("exact:%1").arg(total.value())
                           : QStringLiteral("estimate:%1").arg(total.value());
  }
  if (online_index_complete_ && online_stable_frames_ > 0) {
    return QStringLiteral("exact:%1").arg(online_stable_frames_);
  }
  const lapian::core::TotalFrameCount total = ContainerTotal();
  if (total.has_value() && total.value() > 0) {
    return QStringLiteral("estimate:%1").arg(total.value());
  }
  return QStringLiteral("scanning");
}

QString MainWindow::OnlineIndexStatsForTest() const {
  return online_index_stats_;
}

qint64 MainWindow::OnlineCachedSecondsForTest() const {
  if (!online_ready_ || session_ == nullptr || online_total_bytes_ <= 0) {
    return -1;
  }
  const qint64 duration_us = session_->info().duration_us;
  if (duration_us <= 0) {
    return -1;
  }
  const double fraction =
      static_cast<double>(session_->GrowingPublished()) /
      static_cast<double>(online_total_bytes_);
  return static_cast<qint64>(
      std::llround(static_cast<double>(duration_us) / 1.0e6 * fraction));
}

qint64 MainWindow::OnlinePublishedBytesForTest() const {
  return session_ != nullptr ? session_->GrowingPublished() : 0;
}

double MainWindow::OnlineDurationSecondsForTest() const {
  if (session_ == nullptr) {
    return -1.0;
  }
  // 帧号/总帧数数学用容器 duration；DASH 目录 duration 只作回退。
  if (session_->info().duration_us > 0) {
    return static_cast<double>(session_->info().duration_us) / 1.0e6;
  }
  if (online_dash_duration_ms_ > 0) {
    return static_cast<double>(online_dash_duration_ms_) / 1000.0;
  }
  return -1.0;
}

double MainWindow::OnlineVideoFpsForTest() const {
  if (session_ == nullptr) {
    return -1.0;
  }
  return session_->info().video.frame_rate.ToDouble();
}

int MainWindow::OnlineWaveformBucketCountForTest() const {
  if (!waveform_) {
    return 0;
  }
  const auto waveform_data = waveform_->data();
  return waveform_data ? static_cast<int>(waveform_data->bucket_count()) : 0;
}

double MainWindow::OnlineWaveformOffsetFramesForTest() const {
  if (!waveform_ || session_ == nullptr) {
    return -1.0;
  }
  const auto waveform_data = waveform_->data();
  if (!waveform_data) {
    return -1.0;
  }
  const double fps = session_->info().video.frame_rate.ToDouble();
  if (fps <= 0.0) {
    return -1.0;
  }
  return std::abs(waveform_data->offset_seconds()) * fps;
}

QString MainWindow::StatusMessageForTest() const {
  return statusBar() != nullptr ? statusBar()->currentMessage() : QString();
}

bool MainWindow::WaitForOnlineReadyForTest(int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    if (online_ready_) {
      return true;
    }
    if (!online_mode_ || (online_ && online_->state() ==
                                          lapian::app::OnlineState::kError)) {
      return false;
    }
    QElapsedTimer pump;
    pump.start();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const qint64 pump_ms = pump.elapsed();
    if (pump_ms > 50 && qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG")) {
      std::fprintf(stderr,
                   "T8.9 wait_ready pump_ms=%lld total_ms=%lld ready=%d\n",
                   static_cast<long long>(pump_ms),
                   static_cast<long long>(timer.elapsed()),
                   online_ready_ ? 1 : 0);
      std::fflush(stderr);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return online_ready_;
}

// ---------------------------------------------------------------- T8.9 档位菜单

lapian::online::QualityMenuContext MainWindow::CurrentQualityMenuContext() const {
  lapian::online::QualityMenuContext context;
  context.current_qn = online_qn_;
  context.current_codecid = online_codecid_;
  context.requested_qn = online_request_.requested_qn;
  context.requested_codecid = online_request_.requested_codecid;
  context.current_from_cache = online_cache_hit_;
  // T8.12：只有真实在线会话就绪且服务端目录成功返回时才允许手动切档。
  // 离线合成菜单/T8.9 诊断仍会置灰，避免把“列示”写成“可达”。
  context.dash_playback_available =
      online_mode_ && online_ready_ && online_quality_.ok &&
      !online_switch_pending_;
  context.logged_in = online_logged_in_;
  context.actual_fps = online_actual_fps_;
  context.source_fps_hint = online_source_fps_hint_;
  context.error = online_quality_.error;
  return context;
}

QVector<lapian::online::QualityMenuEntry>
MainWindow::OnlineQualityMenuEntriesForTest() const {
  return lapian::online::BuildQualityMenuModel(online_quality_.catalog,
                                               CurrentQualityMenuContext());
}

QString MainWindow::OnlineQualityMenuTextForTest() const {
  return lapian::online::QualityMenuModelText(OnlineQualityMenuEntriesForTest());
}

QStringList MainWindow::OnlineQualityMenuActionsForTest() const {
  QStringList lines;
  if (quality_menu_ == nullptr) {
    return lines;
  }
  for (QAction* action : quality_menu_->actions()) {
    lines.append(QStringLiteral("%1|enabled=%2|checked=%3")
                     .arg(action->text())
                     .arg(action->isEnabled() ? 1 : 0)
                     .arg(action->isChecked() ? 1 : 0));
  }
  return lines;
}

void MainWindow::RebuildOnlineQualityMenu() {
  if (quality_menu_ == nullptr) {
    quality_menu_ = new QMenu(this);
    quality_menu_->setObjectName(QStringLiteral("onlineQualityMenu"));
  }
  quality_menu_->clear();
  const QVector<lapian::online::QualityMenuEntry> entries =
      OnlineQualityMenuEntriesForTest();
  for (const lapian::online::QualityMenuEntry& entry : entries) {
    QAction* action = quality_menu_->addAction(entry.text);
    action->setEnabled(entry.enabled);
    if (entry.kind == lapian::online::QualityMenuKind::kTier) {
      action->setCheckable(entry.checkable);
      action->setChecked(entry.checked);
      action->setData(entry.qn);
      const int qn = entry.qn;
      connect(action, &QAction::triggered, this,
              [this, qn]() { OnOnlineQualityChosen(qn); });
    }
    QString tip = entry.tooltip;
    if (!entry.disabled_reason.isEmpty()) {
      tip = tip.isEmpty()
                ? entry.disabled_reason
                : tip + QStringLiteral(" · ") + entry.disabled_reason;
    }
    action->setToolTip(tip);
  }
}

void MainWindow::ShowOnlineQualityMenu() {
  if (quality_button_ == nullptr) {
    return;
  }
  RebuildOnlineQualityMenu();
  if (quality_menu_ == nullptr || quality_menu_->isEmpty()) {
    return;
  }
  // 状态栏按钮在上方弹出：用按钮左上角作为锚点，Qt 会按屏幕边界修正。
  const QPoint anchor =
      quality_button_->mapToGlobal(QPoint(0, -quality_menu_->sizeHint().height()));
  quality_menu_->popup(anchor);
}

void MainWindow::ApplyOnlineQuality(
    const lapian::app::OnlineQualityCatalog& info) {
  online_quality_ = info;
  online_quality_.fetched = true;
  if (quality_button_ != nullptr) {
    quality_button_->setVisible(online_mode_ && online_ready_);
    quality_button_->setEnabled(true);
    const int shown_qn =
        online_qn_ > 0 ? online_qn_ : info.catalog.DefaultQn();
    QString text = QStringLiteral("清晰度：%1")
                       .arg(lapian::online::QualityName(shown_qn));
    if (info.cache_hit) {
      text += QStringLiteral("（缓存）");
    }
    quality_button_->setText(text);
    quality_button_->setToolTip(
        QStringLiteral("当前实际 qn=%1 codecid=%2；请求 qn=%3；"
                       "档位以服务端返回为准")
            .arg(online_qn_)
            .arg(online_codecid_)
            .arg(online_request_.requested_qn > 0
                     ? QString::number(online_request_.requested_qn)
                     : QStringLiteral("自动")));
  }
  RebuildOnlineQualityMenu();
  if (!info.ok && !info.error.isEmpty()) {
    // 档位目录失败不改变会话状态：播放/缓存/首帧都不受影响。
    statusBar()->showMessage(
        QStringLiteral("档位获取失败（不影响播放）：%1").arg(info.error), 8000);
  }
}

void MainWindow::ResetOnlineQuality() {
  online_quality_ = lapian::app::OnlineQualityCatalog{};
  if (quality_menu_ != nullptr) {
    quality_menu_->clear();
  }
  if (quality_button_ != nullptr) {
    quality_button_->setVisible(false);
    quality_button_->setEnabled(false);
    quality_button_->setText(QStringLiteral("清晰度"));
  }
}

void MainWindow::OnOnlineQualityChosen(int qn) {
  if (qn <= 0) {
    return;
  }
  if (!online_mode_ || !online_ready_ || session_ == nullptr) {
    statusBar()->showMessage(
        QStringLiteral("在线会话尚未就绪，暂不能切换档位"), 6000);
    return;
  }
  const lapian::online::QualityTier* tier =
      online_quality_.catalog.FindTier(qn);
  if (tier == nullptr) {
    statusBar()->showMessage(
        QStringLiteral("服务端没有返回该档位：%1；不自动换到其他档")
            .arg(lapian::online::QualityName(qn)),
        8000);
    return;
  }
  const lapian::online::DashStream* stream = tier->PreferredStream();
  if (stream == nullptr) {
    statusBar()->showMessage(
        QStringLiteral("档位 %1 没有可用视频流").arg(
            lapian::online::QualityName(qn)),
        8000);
    return;
  }
  const int target_codecid = stream->codecid;
  if (qn == online_qn_ && target_codecid == online_codecid_) {
    statusBar()->showMessage(
        QStringLiteral("已经是当前播放档位：%1")
            .arg(lapian::online::QualityName(qn)),
        6000);
    return;
  }

  // 切档前把当前标记/备注落盘（失败会询问；取消则不切），并记住旧流 PTS。
  if (!FlushMarksBeforeSwitch()) {
    statusBar()->showMessage(QStringLiteral("标记未保存，已取消切档"), 8000);
    return;
  }
  const std::int64_t old_frame = current_frame_;
  std::int64_t old_pts = current_pts_us_;
  if (session_->index().IsReady() && !session_->index().empty() &&
      old_frame >= 0 &&
      old_frame < static_cast<std::int64_t>(session_->index().size())) {
    // 索引 PTS 是换档映射的基准；current_pts_us_ 只作无索引时回退。
    const std::int64_t index_pts = session_->index().PtsOfFrame(old_frame);
    if (index_pts >= 0) {
      old_pts = index_pts;
    }
  }
  const double old_fps = online_actual_fps_;
  const int old_qn = online_qn_;
  const int old_codecid = online_codecid_;

  lapian::app::OnlineRequest request = online_request_;
  request.requested_qn = qn;
  request.requested_codecid = target_codecid;
  const QString switch_text =
      QStringLiteral("正在切换清晰度：%1 → %2；将重新取流并重建索引/波形，"
                     "帧号可能变化。")
          .arg(lapian::online::QualityName(old_qn),
               lapian::online::QualityName(qn));
  statusBar()->showMessage(switch_text, 12000);
  status_label_->setText(base_info_ + QStringLiteral(" · 在线：正在切档…"));
  if (playback_ != nullptr) {
    playback_->Pause();
  }

  BeginOnlineRequest(request);
  if (online_ == nullptr || !online_mode_) {
    ClearOnlineQualitySwitch();
    return;
  }
  online_switch_pending_ = true;
  online_switch_target_qn_ = qn;
  online_switch_target_codecid_ = target_codecid;
  online_switch_target_pts_us_ = old_pts;
  online_switch_old_frame_ = old_frame;
  online_switch_old_fps_ = old_fps;
  online_switch_status_ = switch_text;
  // BeginOnlineRequest 会先写“正在解析在线视频…”；切档提示要重新盖回去，
  // 否则触发菜单后用户只看到普通解析提示。
  statusBar()->showMessage(switch_text, 12000);
  if (qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG")) {
    std::fprintf(stderr,
                 "T8.12 online_quality_switch old_qn=%d new_qn=%d "
                 "old_codecid=%d new_codecid=%d old_frame=%lld old_pts_us=%lld "
                 "old_fps=%.3f\n",
                 old_qn, qn, old_codecid, target_codecid,
                 static_cast<long long>(old_frame),
                 static_cast<long long>(old_pts), old_fps);
    std::fflush(stderr);
  }
}

void MainWindow::ClearOnlineQualitySwitch() {
  online_switch_pending_ = false;
  online_switch_target_qn_ = 0;
  online_switch_target_codecid_ = 0;
  online_switch_target_pts_us_ = 0;
  online_switch_old_frame_ = 0;
  online_switch_old_fps_ = 0.0;
  online_switch_status_.clear();
}

void MainWindow::TryApplyPendingOnlineQualitySwitch() {
  if (!online_switch_pending_ || !online_mode_ || !online_ready_ ||
      session_ == nullptr || online_qn_ <= 0) {
    return;
  }
  const lapian::media::FrameIndex& index = session_->index();
  if (!index.IsReady() || index.empty()) {
    return;
  }
  const double fps = online_actual_fps_ > 0.0
                         ? online_actual_fps_
                         : session_->info().video.frame_rate.ToDouble();
  const std::int64_t last_pts =
      index.PtsOfFrame(static_cast<std::int64_t>(index.size()) - 1);
  const std::int64_t frame_duration_us =
      fps > 0.0 ? static_cast<std::int64_t>(1000000.0 / fps) : 40000;
  if (!online_index_complete_ &&
      online_switch_target_pts_us_ >
          last_pts + std::max<std::int64_t>(frame_duration_us * 2, 100000)) {
    // 新索引还没覆盖旧位置：继续等下一次索引快照，不把目标吸到当前末端。
    return;
  }

  const std::int64_t mapped_frame =
      index.FrameOfPts(online_switch_target_pts_us_);
  if (mapped_frame < 0 || mapped_frame >=
                              static_cast<std::int64_t>(index.size())) {
    return;
  }
  online_switch_pending_ = false;
  online_switch_status_ =
      QStringLiteral("已切换到 %1（qn=%2）；原 PTS 映射到第 %3 帧"
                     "（旧第 %4 帧，帧号可能变化）")
          .arg(lapian::online::QualityName(online_qn_))
          .arg(online_qn_)
          .arg(mapped_frame)
          .arg(online_switch_old_frame_);
  if (online_switch_old_fps_ > 0.0 && online_actual_fps_ > 0.0 &&
      std::fabs(online_switch_old_fps_ - online_actual_fps_) > 0.01) {
    online_switch_status_ +=
        QStringLiteral("；fps %1 → %2，按 PTS 映射")
            .arg(lapian::online::FormatOnlineFps(online_switch_old_fps_),
                 lapian::online::FormatOnlineFps(online_actual_fps_));
  }
  if (qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG")) {
    std::fprintf(stderr,
                 "T8.12 online_quality_switch_land qn=%d frame=%lld pts_us=%lld "
                 "old_frame=%lld old_pts_us=%lld\n",
                 online_qn_, static_cast<long long>(mapped_frame),
                 static_cast<long long>(index.PtsOfFrame(mapped_frame)),
                 static_cast<long long>(online_switch_old_frame_),
                 static_cast<long long>(online_switch_target_pts_us_));
    std::fflush(stderr);
  }
  const bool growing = !online_cache_hit_;
  if (growing) {
    // 让异步 seek 落地后再显示最终提示，避免被“第 N 帧（在线）”覆盖。
    online_seek_completion_notice_ = online_switch_status_;
  }
  GoToFrame(mapped_frame);
  if (!growing) {
    statusBar()->showMessage(online_switch_status_, 12000);
  }
}

void MainWindow::SetOnlineQualityCatalogEnabledForTest(bool enabled) {
  online_quality_enabled_ = enabled;
  if (online_ != nullptr) {
    online_->SetQualityCatalogEnabledForTest(enabled);
  }
}

void MainWindow::SetOnlineQualityForTest(
    const lapian::app::OnlineQualityCatalog& info) {
  online_test_quality_ = info;
  online_test_quality_.fetched = true;
  has_online_test_quality_ = true;
  if (online_ != nullptr) {
    online_->SetQualityCatalogForTest(online_test_quality_);
  } else {
    // 没有活动会话时也把模型/菜单更新出来，供 --ui 断言（不代表已联网）。
    ApplyOnlineQuality(online_test_quality_);
  }
}

bool MainWindow::OnlineQualityButtonVisibleForTest() const {
  return quality_button_ != nullptr && quality_button_->isVisible();
}

int MainWindow::OnlineQualityRequestsForTest() const {
  return online_ != nullptr ? online_->QualityRequestsForTest() : 0;
}

QString MainWindow::OnlineFpsBadgeTextForTest() const {
  return info_bar_ != nullptr ? info_bar_->OnlineFpsBadgeTextForTest()
                              : QString();
}

bool MainWindow::OnlineFrameRateBannerVisibleForTest() const {
  return info_bar_ != nullptr && info_bar_->BannerVisibleForTest() &&
         info_bar_->BannerKindForTest() == QStringLiteral("frame-drop");
}

bool MainWindow::OnlineFrameRateBannerCloseVisibleForTest() const {
  return info_bar_ != nullptr && info_bar_->BannerCloseVisibleForTest();
}

void MainWindow::DismissOnlineFrameRateBannerForTest() {
  if (info_bar_ != nullptr) {
    info_bar_->DismissOnlineFrameRateBannerForTest();
  }
}

bool MainWindow::WaitForOnlineQualitySwitchForTest(int qn, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (online_mode_ && online_ready_ && online_qn_ == qn &&
        !online_switch_pending_ && !online_seek_running_) {
      return true;
    }
    if (!online_mode_ || online_ == nullptr ||
        online_->state() == lapian::app::OnlineState::kError) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return online_mode_ && online_ready_ && online_qn_ == qn &&
         !online_switch_pending_ && !online_seek_running_;
}

QString MainWindow::OnlineQualitySummaryForTest() const {
  return QStringLiteral(
             "ok=%1 cache_hit=%2 fetched=%3 http_status=%4 elapsed_ms=%5 "
             "requests=%6 error=%7 %8")
      .arg(online_quality_.ok ? 1 : 0)
      .arg(online_quality_.cache_hit ? 1 : 0)
      .arg(online_quality_.fetched ? 1 : 0)
      .arg(online_quality_.http_status)
      .arg(online_quality_.elapsed_ms)
      .arg(OnlineQualityRequestsForTest())
      .arg(online_quality_.error.isEmpty() ? QStringLiteral("-")
                                           : online_quality_.error)
      .arg(online_quality_.catalog.Summary());
}

bool MainWindow::WaitForOnlineQualityForTest(int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    if (online_quality_.fetched) {
      return true;
    }
    if (!online_mode_ ||
        (online_ && online_->state() == lapian::app::OnlineState::kError)) {
      return online_quality_.fetched;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return online_quality_.fetched;
}

bool MainWindow::TriggerOnlineQualityEntryForTest(int qn, QString* status) {
  if (status != nullptr) {
    status->clear();
  }
  if (quality_menu_ == nullptr) {
    return false;
  }
  for (QAction* action : quality_menu_->actions()) {
    if (!action->data().isValid() || action->data().toInt() != qn) {
      continue;
    }
    if (!action->isEnabled()) {
      if (status != nullptr) {
        *status = QStringLiteral("disabled: %1").arg(action->toolTip());
      }
      return false;
    }
    action->trigger();
    if (status != nullptr) {
      *status = StatusMessageForTest();
    }
    return true;
  }
  if (status != nullptr) {
    *status = QStringLiteral("not_found");
  }
  return false;
}

void MainWindow::OpenFile(const QString& path) {
  if (path.isEmpty()) {
    return;
  }
  OpenLocalMedia(path, nullptr);
}

void MainWindow::OpenLocalMedia(const QString& path,
                                const OnlineOpenContext* online) {
  // T7.1：打开路径分段计时；只在冷启动日志开启时输出。
  QElapsedTimer open_timer;
  open_timer.start();
  double audio_open_ms = 0.0;
  double waveform_start_ms = 0.0;
  double first_upload_ms = 0.0;
  // T7.5：Qt 文件对话框/拖拽给的是正斜杠路径。正斜杠绝对路径（尤其带盘符）
  // 在 FFmpeg 里可能被当成 URL，缺失文件会报 “Illegal byte sequence” 而不是
  // “文件不存在”。打开前统一转成 Windows 原生分隔符。
  const QString native_path = QDir::toNativeSeparators(path);
  const QByteArray utf8 = native_path.toUtf8();
  const std::string path_utf8(utf8.constData(), static_cast<std::size_t>(utf8.size()));

  if (online == nullptr) {
  // T8.6：切到本地文件前先停在线下载/reader（保留 .part，不 finalize 未完成项）。
  if (online_mode_ || online_) {
    ShutdownOnlineSession(false);
  }
  online_mode_ = false;
  online_ready_ = false;
  online_cache_hit_ = false;
  online_cache_path_.clear();
  if (playback_) {
    playback_->SetDecodeGate({});
    playback_->SetBufferOnAudioStall(false);
  }
  // T7.5：网络路径先在有限时间内预检。断开的映射盘/不可达 UNC 的 SMB 连接
  // 默认要 ~20s 才失败；如果直接进 avformat_open_input，UI 线程会冻结超过
  // spec §4.4 的 5s。预检失败时保留当前会话与播放状态，只报可读错误。
  if (lapian::media::IsNetworkPath(path_utf8)) {
    constexpr int kNetworkPathProbeTimeoutMs = 3000;
    QElapsedTimer probe_timer;
    probe_timer.start();
    const lapian::media::PathProbeResult probe =
        lapian::media::ProbePathWithTimeout(path_utf8, kNetworkPathProbeTimeoutMs);
    const double probe_ms = static_cast<double>(probe_timer.nsecsElapsed()) / 1.0e6;
    if (qEnvironmentVariableIsSet("LAPIAN_OPEN_LOG")) {
      std::fprintf(stderr,
                   "T7.5 open_probe kind=network status=%d elapsed_ms=%.1f\n",
                   static_cast<int>(probe.status), probe_ms);
      std::fflush(stderr);
    }
    if (probe.status != lapian::media::PathProbeStatus::kReachable) {
      ShowOpenError(QStringLiteral("无法打开网络路径：%1\n\n%2")
                        .arg(QDir::toNativeSeparators(path),
                             QString::fromStdString(probe.message)));
      return;
    }
  }
  // T7.5：文件夹不是视频。FFmpeg 对目录会报 ENOENT/权限错误，提示会误导；
  // 这里在进入探测前直接给准确说明（网络路径已在上面确认可达）。
  const QFileInfo input_info(path);
  if (input_info.exists() && input_info.isDir()) {
    ShowOpenError(QStringLiteral("这是一个文件夹，不是视频文件：\n%1")
                      .arg(QDir::toNativeSeparators(path)));
    return;
  }
  // T7.5：本地文件先试读一次。独占中的文件 FFmpeg 会报 ENOENT（提示“文件不存在”
  // 会误导）；ACL 拒绝则直接给出权限提示。网络路径已在上面预检，不再做可能
  // 阻塞的额外打开。
  if (!lapian::media::IsNetworkPath(path_utf8) && input_info.exists()) {
    QFile readable(path);
    if (!readable.open(QIODevice::ReadOnly)) {
      ShowOpenError(
          QStringLiteral("无法读取该文件（可能被其他程序独占，或权限不足）：\n%1\n\n%2")
              .arg(QDir::toNativeSeparators(path), readable.errorString()));
      return;
    }
    readable.close();
  }
  }  // online == nullptr：本地路径的预检/权限提示
  CancelPendingMarkJump();
  if (gfx_ == nullptr || gfx_->d3d_device() == nullptr) {
    ShowOpenError(QStringLiteral("图形设备未就绪，无法打开视频"));
    return;
  }

  if (playback_ != nullptr) {
    playback_->Pause();
  }
  // T7.11.1：旧会话的延后回拷窗口随会话替换作废，定时器不能对着新会话跑。
  if (cache_fill_timer_ != nullptr) {
    cache_fill_timer_->stop();
  }
  // 旧文件的截图任务先完成，避免工作线程在会话替换后继续引用旧帧。
  if (snapshots_ != nullptr) {
    snapshots_->Drain();
  }

  auto candidate = std::make_unique<lapian::media::MediaSession>();
  std::string error;
  // T7.1：产品路径不在这里建索引（首帧优先）；旧 GUI 回归测试用同步模式保持
  // 打开后即可导航的既有语义。
  if (!candidate->Open(path_utf8, gfx_->d3d_device(), &error, sync_index_for_test_)) {
    QString message = DescribeProbeError(candidate->last_attempt().error);
    if (!error.empty()) {
      message += QStringLiteral("\n\n") + QString::fromStdString(error);
    }
    ShowOpenError(message);
    return;
  }

  // T5.2：旧文件的标记先落盘；失败时明确询问，不静默丢标记。
  if (!FlushMarksBeforeSwitch()) {
    return;
  }
  if (timeline_ != nullptr) {
    // 先把旧波形的回调摘掉，避免 session 替换后绘制到悬空的 FrameIndex。
    timeline_->ClearWaveform();
  }

  // 旧预览器持有旧索引指针，必须在替换 session 之前停掉。
  StopPreview();
  preview_.reset();
  if (audio_) {
    audio_.reset();
  }
  // T7.1：旧索引线程先取消并 join，避免它把结果装到新会话上。
  CancelIndexBuild();
  session_ = std::shared_ptr<lapian::media::MediaSession>(std::move(candidate));
  last_open_error_.clear();
  playback_->SetSession(session_.get());
  current_frame_ = 0;
  snapshot_saved_paths_.clear();
  snapshot_last_path_.clear();
  snapshot_last_error_.clear();
  snapshot_last_capture_frame_ = -1;
  snapshot_last_saved_frame_ = -1;
  snapshot_last_fallback_ = false;
  snapshot_capture_samples_.clear();
  snapshot_save_samples_.clear();
  play_action_->setEnabled(true);
  if (snapshot_action_ != nullptr) {
    snapshot_action_->setEnabled(true);
  }

  const lapian::media::MediaInfo& info = session_->info();
  const lapian::media::DecodedFrameInfo& frame = session_->first_frame();
  {
    QElapsedTimer upload_timer;
    upload_timer.start();
    gfx_->SetVideoFrame(frame.av_frame, info.rotation.counterclockwise_degrees);
    first_upload_ms = static_cast<double>(upload_timer.nsecsElapsed()) / 1.0e6;
  }
  RememberDisplayedFrame(frame.av_frame, info.rotation.counterclockwise_degrees, 0);
  current_pts_us_ = frame.pts_us;

  if (online != nullptr) {
    online_mode_ = true;
    online_ready_ = true;
    online_cache_hit_ = online->cache_hit;
    online_bvid_ = online->bvid;
    online_title_ = online->title;
    online_cid_ = online->cid;
    online_qn_ = online->qn;
    online_codecid_ = online->codecid;
    online_owner_ = online->owner;
    online_fps_ = info.video.frame_rate.ToDouble();
    online_total_bytes_ =
        online->total_bytes > 0 ? online->total_bytes : QFileInfo(native_path).size();
    online_published_bytes_ = online_total_bytes_;
    online_cache_path_ = online->cache_hit ? native_path : QString();
    online_part_path_.clear();
    online_stable_frames_ = 0;
    online_index_complete_ = false;
    online_spans_.clear();
    online_index_pending_.reset();
    online_index_pending_generation_ = 0;
    online_index_stats_.clear();
    online_last_error_.clear();
    if (online_cancel_action_ != nullptr) {
      online_cancel_action_->setEnabled(false);
    }
    LoadOnlineMarks(*online);
  }

  const QFileInfo file_info(path);
  if (online != nullptr) {
    const QString label = online->title.isEmpty() ? online->bvid : online->title;
    const QString quality = lapian::online::QualityName(online->qn);
    setWindowTitle(QStringLiteral("%1 — 拉片软件（在线 · 缓存命中 · %2）")
                       .arg(label, quality));
  } else {
    setWindowTitle(file_info.fileName() + QStringLiteral(" — 拉片软件"));
  }

  info_bar_->SetMedia(info, session_->IsVfrSuspect());
  if (online != nullptr) {
    online_actual_fps_ =
        online->fps > 0.0 ? online->fps : info.video.frame_rate.ToDouble();
    ApplyOnlineFrameRateUi();
    online_status_detail_ = BuildOnlineStatusDetail();
    status_label_->setText(base_info_ + online_status_detail_);
    statusBar()->showMessage(
        QStringLiteral("缓存命中：已打开本地 data.mp4（0 网络）"), 8000);
    // T8.9：缓存命中也显示清晰度按钮；档位来自 meta（未联网）。
    if (quality_button_ != nullptr) {
      quality_button_->setVisible(true);
      quality_button_->setEnabled(true);
      quality_button_->setText(QStringLiteral("清晰度：%1").arg(
          lapian::online::QualityName(online_qn_)));
    }
    RebuildOnlineQualityMenu();
  } else {
    status_label_->setText(base_info_ + QStringLiteral(" · ") +
                           (frame.hardware ? QStringLiteral("硬解")
                                           : QStringLiteral("软解")));
  }

  if (online == nullptr) {
    if (session_->IsVfrSuspect()) {
      statusBar()->showMessage(
          QStringLiteral("可变帧率（VFR）：帧号按显示帧计数，时间码不承诺与帧号严格对应"),
          8000);
    } else {
      statusBar()->showMessage(QStringLiteral("已打开，停在第 0 帧"));
    }
  }

  // 新文件从“循环：关”开始；A/B 需要索引，扫描中由 SyncLoopUi 禁用。
  loop_.Clear();
  SyncLoopUi();

  // T7.1：索引已就绪（同步模式）→ 直接启用导航/标记/预览；
  // 同步模式下索引失败也不再重试，直接进入“无索引”状态；异步模式才扫描。
  if (session_->HasIndex()) {
    ApplyIndexState();
  } else if (sync_index_for_test_) {
    ApplyIndexState();
  } else {
    BeginIndexScan();
    StartIndexBuild();
  }

  // T3.1：音频异步打开；首帧已经呈现，不等音频设备初始化。
  audio_status_revision_ = 0;
  audio_error_shown_ = false;
  audio_base_label_.clear();
  audio_label_->setText(QStringLiteral("无音频"));
  volume_slider_->setEnabled(false);
  mute_action_->setEnabled(false);
  if (waveform_) {
    waveform_->Cancel();
    waveform_.reset();
  }
  waveform_status_revision_ = 0;
  waveform_label_->setText(QStringLiteral("波形：—"));
  if (info.has_audio) {
    auto audio = std::make_unique<lapian::audio::AudioEngine>();
    audio->SetVolume(volume_);
    audio->SetMute(muted_);
    audio->SetRate(rate_);
    QElapsedTimer audio_timer;
    audio_timer.start();
    const bool audio_opened =
        audio->Open(path_utf8, info.audio.stream_index,
                    static_cast<double>(info.av_offset_us) / 1.0e6);
    audio_open_ms = static_cast<double>(audio_timer.nsecsElapsed()) / 1.0e6;
    if (!audio_opened) {
      audio_label_->setText(QStringLiteral("音频不可用"));
      statusBar()->showMessage(QStringLiteral("音频线程启动失败，视频继续静音播放"), 5000);
    } else {
      audio_base_label_ =
          QStringLiteral("音频：%1 %2 kHz %3")
              .arg(QString::fromStdString(info.audio.codec).toUpper())
              .arg(QString::number(static_cast<double>(info.audio.sample_rate) / 1000.0, 'g', 3))
              .arg(DescribeAudioChannels(info.audio.channels));
      audio_label_->setText(audio_base_label_);
      volume_slider_->setEnabled(true);
      mute_action_->setEnabled(true);
      audio_ = std::move(audio);
    }
  }
  playback_->SetAudioEngine(audio_.get());
  playback_->SetRate(rate_);

  // T3.5：波形在独立线程里生成；T3.6 把它接到时间轴。
  if (waveform_action_ != nullptr) {
    waveform_action_->setEnabled(true);
    waveform_action_->setChecked(waveform_visible_);
  }
  if (info.has_audio) {
    waveform_ = std::make_unique<lapian::audio::WaveformGenerator>();
    waveform_last_bucket_count_ = 0;
    QElapsedTimer waveform_timer;
    waveform_timer.start();
    const bool waveform_started =
        waveform_->Start(path_utf8, info.audio.stream_index,
                         static_cast<double>(info.av_offset_us) / 1.0e6);
    waveform_start_ms = static_cast<double>(waveform_timer.nsecsElapsed()) / 1.0e6;
    if (!waveform_started) {
      waveform_label_->setText(QStringLiteral("波形：失败"));
      timeline_->SetWaveformMessage(QStringLiteral("波形生成失败"));
    } else if (const auto waveform_data = waveform_->data()) {
      const double fps = info.video.frame_rate.ToDouble();
      timeline_->SetWaveformSource(
          [waveform_data]() { return waveform_data->bucket_count(); },
          [waveform_data](std::size_t begin, std::size_t end) {
            const auto range = waveform_data->MinMaxRange(begin, end);
            return TimelineWidget::WaveformBucketView{range.min, range.max};
          },
          static_cast<double>(info.av_offset_us) / 1.0e6,
          fps > 0.0 ? 1.0 / fps : 1.0 / 30.0,
          [session = session_.get()](std::int64_t frame) -> std::int64_t {
            if (session == nullptr || !session->index().IsReady()) {
              return -1;
            }
            return session->index().PtsOfFrame(frame);
          });
      timeline_->SetWaveformVisible(waveform_visible_);
    }
  } else {
    waveform_label_->setText(QStringLiteral("波形：无音频"));
    timeline_->SetWaveformMessage(QStringLiteral("无音频"));
    timeline_->SetWaveformVisible(waveform_visible_);
  }

  // T2.9：预览解码器在索引就绪后预热（见 ApplyIndexState）；扫描中拖动禁用。

  if (startup_log_) {
    const lapian::media::MediaOpenTimings& timings = session_->last_open_timings();
    std::fprintf(stderr,
                 "T7.1 open_timing probe_ms=%.2f decoder_ms=%.2f first_frame_ms=%.2f "
                 "index_ms=%.2f index_frames=%zu index_ok=%d index_mode=%s "
                 "audio_open_ms=%.2f waveform_start_ms=%.2f first_upload_ms=%.2f "
                 "ui_total_ms=%.2f\n",
                 timings.probe_ms, timings.decoder_open_ms, timings.first_frame_ms,
                 timings.index_ms, timings.index_frames, timings.index_ok ? 1 : 0,
                 sync_index_for_test_ ? "sync" : "async", audio_open_ms,
                 waveform_start_ms, first_upload_ms,
                 static_cast<double>(open_timer.nsecsElapsed()) / 1.0e6);
    std::fflush(stderr);
  }
}

lapian::core::TotalFrameCount MainWindow::ContainerTotal() const {
  if (session_ == nullptr) {
    return lapian::core::TotalFrameCount::Unknown();
  }
  const lapian::media::MediaInfo& info = session_->info();
  const lapian::core::FrameRate rate(info.video.frame_rate.num, info.video.frame_rate.den);
  const std::int64_t known_frames =
      info.video.nb_frames_known ? info.video.nb_frames : 0;
  return lapian::core::TotalFrameCount::FromContainer(known_frames, info.duration_us, rate);
}

void MainWindow::BeginIndexScan() {
  index_scanning_ = true;
  index_scan_timer_.start();
  // 扫描中是过渡态：VFR 的扫描态已在 SetMedia 里给出，这里覆盖为同一状态。
  info_bar_->SetTotal(lapian::core::TotalFrameCount::Scanning());
  const lapian::core::TotalFrameCount estimate = ContainerTotal();
  const std::int64_t estimate_frames = estimate.has_value() ? estimate.value() : 0;
  if (timeline_ != nullptr) {
    timeline_->SetFrameCount(estimate_frames);
    // 索引未就绪时拖动/点击无法精确落帧，先禁用，索引完成后再启用。
    timeline_->setEnabled(false);
  }
  prev_action_->setEnabled(false);
  next_action_->setEnabled(false);
  goto_action_->setEnabled(false);
  zoom_in_action_->setEnabled(false);
  zoom_out_action_->setEnabled(false);
  if (mark_action_ != nullptr) {
    mark_action_->setEnabled(false);
  }
  if (waveform_action_ != nullptr) {
    waveform_action_->setEnabled(false);
  }
  slower_action_->setEnabled(true);
  faster_action_->setEnabled(true);
  speed_label_->setText(FormatSpeed(rate_));
  SyncLoopUi();
  // 索引就绪后再加载标记：MarkStore 需要索引做 frame+pts 校正。
  // T8.7 缓存命中：在线记录已在打开时加载并先于索引可见，扫描中不能清空。
  if (!online_ready_) {
    marks_.reset();
    marks_dirty_ = false;
    if (mark_panel_ != nullptr) {
      mark_panel_->Clear();
    }
    if (timeline_ != nullptr) {
      timeline_->SetMarks({});
    }
  }
  preview_.reset();
  if (startup_log_) {
    std::fprintf(stderr, "T7.1 index_scan estimate_frames=%lld\n",
                 static_cast<long long>(estimate_frames));
    std::fflush(stderr);
  }
}

void MainWindow::StartIndexBuild() {
  if (session_ == nullptr) {
    return;
  }
  CancelIndexBuild();
  const std::uint64_t generation = index_generation_;
  index_scanning_ = true;
  if (sync_index_for_test_) {
    std::string error;
    if (session_->BuildIndexNow(&error)) {
      ApplyIndexReady(generation);
    } else {
      ApplyIndexFailed(generation);
    }
    return;
  }

  auto cancel = std::make_shared<std::atomic<bool>>(false);
  index_cancel_ = cancel;
  const std::string path = session_->path_utf8();
  const int stream = session_->video_stream_index();
  try {
    index_thread_ = std::thread([this, generation, path, stream, cancel]() {
      auto built = std::make_shared<lapian::media::FrameIndex>();
      std::string error;
      const auto started = std::chrono::steady_clock::now();
      const bool ok = built->BuildFromFile(path, stream, &error, cancel.get());
      const double build_ms =
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - started)
              .count();
      if (cancel->load(std::memory_order_relaxed)) {
        return;
      }
      QMetaObject::invokeMethod(
          this,
          [this, generation, ok, built, build_ms]() {
            if (generation != index_generation_ || session_ == nullptr) {
              return;
            }
            if (ok) {
              session_->InstallIndex(std::move(*built), build_ms);
              ApplyIndexReady(generation);
            } else {
              ApplyIndexFailed(generation);
            }
          },
          Qt::QueuedConnection);
    });
  } catch (const std::exception& exception) {
    qWarning() << "索引线程启动失败:" << exception.what();
    ApplyIndexFailed(generation);
  }
}

void MainWindow::CancelIndexBuild() {
  // 先让 generation 失效，再通知 worker 停止；join 保证不会再有回调在析构后到达。
  ++index_generation_;
  index_scanning_ = false;
  if (index_cancel_) {
    index_cancel_->store(true, std::memory_order_relaxed);
  }
  if (index_thread_.joinable()) {
    index_thread_.join();
  }
  index_cancel_.reset();
}

void MainWindow::ApplyIndexReady(std::uint64_t generation) {
  if (generation != index_generation_ || session_ == nullptr) {
    return;
  }
  index_scanning_ = false;
  ApplyIndexState();
  if (online_ready_) {
    ReconcileOnlineMarks();
  }
  if (startup_log_) {
    const lapian::media::MediaOpenTimings& timings = session_->last_open_timings();
    std::fprintf(stderr,
                 "T7.1 index_ready frames=%zu index_ms=%.2f wait_ms=%lld total_ms=%.2f\n",
                 session_->index().size(), timings.index_ms,
                 static_cast<long long>(index_scan_timer_.elapsed()),
                 timings.total_ms);
    std::fflush(stderr);
  }
}

void MainWindow::ApplyIndexFailed(std::uint64_t generation) {
  if (generation != index_generation_ || session_ == nullptr) {
    return;
  }
  index_scanning_ = false;
  ApplyIndexState();
  statusBar()->showMessage(QStringLiteral("帧索引建立失败，逐帧/跳转不可用"), 8000);
  if (startup_log_) {
    std::fprintf(stderr, "T7.1 index_failed wait_ms=%lld\n",
                 static_cast<long long>(index_scan_timer_.elapsed()));
    std::fflush(stderr);
  }
}

void MainWindow::ApplyIndexState() {
  if (session_ == nullptr) {
    return;
  }
  const bool ready = session_->HasIndex();
  const std::int64_t frames =
      ready ? static_cast<std::int64_t>(session_->index().size()) : 0;
  const lapian::core::TotalFrameCount container_total = ContainerTotal();
  // 缓存命中是完整本地文件：索引/seek/步进全部按本地口径，不走部分索引。
  const bool online_growing = online_ready_ && !online_cache_hit_;
  const bool online_can_seek =
      online_growing && container_total.has_value() &&
      (online_stable_frames_ > 0 || online_index_complete_);
  if (timeline_ != nullptr) {
    const std::int64_t timeline_frames =
        online_growing
            ? (online_index_complete_ && online_stable_frames_ > 0
                   ? static_cast<std::int64_t>(online_stable_frames_)
                   : (container_total.has_value() ? container_total.value() : 0))
            : frames;
    timeline_->SetFrameCount(timeline_frames);
    timeline_->setEnabled(true);
  }
  const bool can_seek = ready || online_can_seek;
  prev_action_->setEnabled(can_seek);
  next_action_->setEnabled(can_seek);
  goto_action_->setEnabled(can_seek);
  if (mark_action_ != nullptr) {
    mark_action_->setEnabled(can_seek);
  }
  zoom_in_action_->setEnabled(frames > 0);
  zoom_out_action_->setEnabled(frames > 0);
  if (waveform_action_ != nullptr) {
    waveform_action_->setEnabled(frames > 0);
    waveform_action_->setChecked(waveform_visible_);
  }
  slower_action_->setEnabled(true);
  faster_action_->setEnabled(true);
  speed_label_->setText(FormatSpeed(rate_));
  // 扫描完成：总帧数从“扫描中”变为索引的精确/估算值；失败则回到容器值。
  // VFR 的总帧数按 T6.5 口径保持“扫描中”（帧号按显示帧计数，不承诺精确总数），
  // 不因为后台索引完成而改写这一提示语义。
  if (session_->IsVfrSuspect()) {
    info_bar_->SetTotal(lapian::core::TotalFrameCount::Scanning());
  } else if (online_growing) {
    // T8.11 三态：容器 duration×fps 可得时为“估算 ≈N”；索引读到 EOF 且缓存
    // 完整后才是“精确 N”。不拿 API fps/部分索引冒充精确总帧数。
    if (online_index_complete_ && online_stable_frames_ > 0) {
      info_bar_->SetTotal(
          lapian::core::TotalFrameCount::Exact(online_stable_frames_));
    } else if (container_total.has_value() && container_total.value() > 0) {
      info_bar_->SetTotal(lapian::core::TotalFrameCount::Estimated(
          container_total.value()));
    } else {
      info_bar_->SetTotal(lapian::core::TotalFrameCount::Scanning());
    }
  } else {
    info_bar_->SetTotal(ready ? session_->index().Total() : ContainerTotal());
  }
  SyncLoopUi();
  LoadMarksForCurrentSession();
  if (ready && (!online_mode_ || online_cache_hit_)) {
    // T2.9：索引就绪后再预热预览器；在线部分索引没有本地路径，预览器禁用。
    // DP-1：预览优先借用 GfxWidget 的 D3D11 设备硬解；设备生命周期由窗口保证。
    preview_ = std::make_unique<lapian::media::PreviewDecoder>();
    preview_->Configure(session_->path_utf8(), session_->video_stream_index(),
                        &session_->index(),
                        gfx_ != nullptr ? gfx_->d3d_device() : nullptr, true,
                        session_->info().video.width,
                        session_->info().video.height);
  }
  RefreshMarksUi();
  ShowMarkWarnings();
  // T8.12：换档后等新索引覆盖旧流 PTS，再映射回原位置。
  TryApplyPendingOnlineQualitySwitch();
}

void MainWindow::LoadMarksForCurrentSession() {
  if (session_ == nullptr) {
    return;
  }
  if (online_ready_) {
    // T8.7：在线记录已在 ApplyOnlineReady/缓存命中打开时加载；这里不再用
    // 会话内空标记覆盖，也不重置未保存状态。
    if (!marks_) {
      marks_ = std::make_unique<lapian::core::MarkStore>(
          lapian::core::MarkStore::OpenSessionOnly(session_->path_utf8(),
                                                   session_->info().duration_us));
    }
    return;
  }
  // T5.2：用当前索引构造 FrameLookup，让 MarkStore 做 frame+pts 校正；
  // 回调只在 Open 调用期间使用，不持有索引指针。
  lapian::core::FrameLookup mark_lookup;
  const lapian::media::FrameIndex& index = session_->index();
  if (index.IsReady() && !index.empty()) {
    mark_lookup.frame_count = static_cast<std::int64_t>(index.size());
    mark_lookup.pts_of_frame = [&index](std::int64_t frame, std::int64_t* pts_us) {
      const lapian::media::FrameIndexEntry* entry = index.Entry(frame);
      if (entry == nullptr) {
        return false;
      }
      *pts_us = entry->pts_us;
      return true;
    };
    mark_lookup.frame_of_pts = [&index](std::int64_t pts_us, std::int64_t* frame) {
      return index.FindExactPts(pts_us, frame);
    };
  }
  lapian::core::MarkStoreOptions mark_options;
  if (!mark_appdata_root_for_test_.isEmpty()) {
    mark_options.appdata_marks_dir =
        mark_appdata_root_for_test_.toUtf8().toStdString();
  }
  lapian::core::MarkStore opened_marks = lapian::core::MarkStore::Open(
      session_->path_utf8(), session_->info().duration_us, mark_options, mark_lookup);
  marks_ = std::make_unique<lapian::core::MarkStore>(std::move(opened_marks));
  marks_dirty_ = false;
}

bool MainWindow::IndexReadyForTest() const {
  return session_ != nullptr && session_->HasIndex();
}

qint64 MainWindow::IndexFrameCountForTest() const {
  return session_ == nullptr ? 0
                             : static_cast<qint64>(session_->index().size());
}

QString MainWindow::IndexStateForTest() const {
  const bool has_session = session_ != nullptr;
  const bool ready = IndexReadyForTest();
  int total_state = -1;
  if (info_bar_ != nullptr) {
    total_state = static_cast<int>(info_bar_->TotalForTest().state());
  }
  return QStringLiteral(
             "session=%1 ready=%2 scanning=%3 frames=%4 nav=%5 timeline=%6 total=%7")
      .arg(has_session ? 1 : 0)
      .arg(ready ? 1 : 0)
      .arg(index_scanning_ ? 1 : 0)
      .arg(IndexFrameCountForTest())
      .arg(prev_action_ != nullptr && prev_action_->isEnabled() ? 1 : 0)
      .arg(timeline_ != nullptr && timeline_->isEnabled() ? 1 : 0)
      .arg(total_state);
}

QString MainWindow::FrameCacheStateForTest() const {
  if (session_ == nullptr) {
    return QStringLiteral("none");
  }
  const lapian::media::FrameCacheStats stats = session_->frame_cache().stats();
  return QStringLiteral(
             "count=%1 bytes=%2 max_bytes=%3 max_entries=%4 hits=%5 misses=%6 "
             "puts=%7 evictions=%8")
      .arg(stats.entries)
      .arg(stats.bytes)
      .arg(stats.max_bytes)
      .arg(stats.max_entries)
      .arg(stats.hits)
      .arg(stats.misses)
      .arg(stats.puts)
      .arg(stats.evictions);
}

int MainWindow::FrameCacheEntryCountForTest() const {
  return session_ == nullptr ? 0
                             : session_->frame_cache().stats().entries;
}

qint64 MainWindow::FrameCacheByteCountForTest() const {
  return session_ == nullptr
             ? 0
             : static_cast<qint64>(session_->frame_cache().stats().bytes);
}

QString MainWindow::AudioStateForTest() const {
  if (!audio_) {
    return QStringLiteral("none");
  }
  using lapian::audio::AudioState;
  switch (audio_->status().state) {
    case AudioState::kClosed:
      return QStringLiteral("closed");
    case AudioState::kOpening:
      return QStringLiteral("opening");
    case AudioState::kPlaying:
      return QStringLiteral("playing");
    case AudioState::kPaused:
      return QStringLiteral("paused");
    case AudioState::kSilent:
      return QStringLiteral("silent");
    case AudioState::kError:
      return QStringLiteral("error");
  }
  return QStringLiteral("unknown");
}

qulonglong MainWindow::AudioUnderrunCountForTest() const {
  return audio_ ? static_cast<qulonglong>(audio_->status().underruns) : 0;
}

// T7.5：模拟 USB 音频设备拔出/恢复；只驱动 AudioEngine 已有状态机，
// 不改产品默认路径（真实设备失效仍由 AUDCLNT_E_DEVICE_INVALIDATED 触发）。
void MainWindow::SimulateAudioDeviceLossForTest(bool active) {
  if (audio_) {
    audio_->SimulateDeviceLossForTest(active);
  }
}

QString MainWindow::LastOpenErrorForTest() const { return last_open_error_; }

QString MainWindow::LastNavigationErrorForTest() const {
  return last_navigation_error_;
}

void MainWindow::SetMarkAppDataRootForTest(const QString& root) {
  mark_appdata_root_for_test_ = root;
}

void MainWindow::SetOnlineCacheRootForTest(const QString& root) {
  if (online_) {
    ShutdownOnlineSession(false);
  }
  loaded_settings_.cache_root = root;
  online_cache_.reset();
  online_cache_ready_ = false;
  online_cache_error_.clear();
}

QString MainWindow::OnlineCacheRootForTest() const {
  if (online_cache_ != nullptr) {
    return online_cache_->root();
  }
  if (!loaded_settings_.cache_root.isEmpty()) {
    return loaded_settings_.cache_root;
  }
  return lapian::online::CacheStore::DefaultRoot();
}

void MainWindow::SetOnlineRecordsRootForTest(const QString& root) {
  online_records_root_for_test_ = root;
}

QString MainWindow::OnlineRecordPathForTest() const {
  if (marks_ == nullptr || !marks_->online()) {
    return QString();
  }
  return QString::fromStdString(marks_->storage_path());
}

QString MainWindow::OnlineSnapshotIndexPathForTest() const {
  if (snapshot_last_path_.isEmpty()) {
    return QString();
  }
  return QDir(QFileInfo(snapshot_last_path_).absolutePath())
      .filePath(QStringLiteral("index.jsonl"));
}

int MainWindow::OnlinePlayUrlRequestsForTest() const {
  return online_ == nullptr ? 0 : online_->PlayUrlRequestsForTest();
}

int MainWindow::OnlineDashRequestsForTest() const {
  return online_ == nullptr ? 0 : online_->DashRequestsForTest();
}

bool MainWindow::OnlineCacheCompleteForTest() const {
  return online_ != nullptr && online_->progress().complete;
}

void MainWindow::SetOnlineHttpClientForTest(
    std::unique_ptr<lapian::online::IHttpClient> client) {
  if (online_ != nullptr) {
    online_->SetHttpClientForTest(std::move(client));
    return;
  }
  online_test_http_ = std::move(client);
}

void MainWindow::ClearOnlineTestPlayUrlForTest() {
  has_online_test_playurl_ = false;
  online_test_playurl_.reset();
}

void MainWindow::SetOnlineTransportForTest(
    std::unique_ptr<lapian::online::IDownloadTransport> transport) {
  online_test_transport_ = std::move(transport);
}

void MainWindow::SetOnlinePlayUrlForTest(
    const lapian::online::PlayUrlData& data) {
  online_test_playurl_ =
      std::make_shared<lapian::online::PlayUrlData>(data);
  has_online_test_playurl_ = true;
}

void MainWindow::SetOnlineReadyThresholdForTest(qint64 bytes) {
  online_test_threshold_ = std::max<qint64>(1, bytes);
}

bool MainWindow::IsOnlineBufferingForTest() const {
  return playback_ != nullptr && playback_->IsBuffering();
}

QString MainWindow::OnlineGateDebugForTest() const {
  return QStringLiteral("gate=%1 published=%2 readpos=%3 buffering=%4 events=%5")
      .arg(online_gate_last_ ? 1 : 0)
      .arg(online_gate_published_)
      .arg(online_gate_readpos_)
      .arg(IsOnlineBufferingForTest() ? 1 : 0)
      .arg(online_buffer_event_count_);
}

std::int64_t MainWindow::OnlineLastFrameForTest() const {
  return OnlineLastFrame();
}

bool MainWindow::OnlineFrameCachedForTest(qint64 frame) const {
  return OnlineFrameCached(frame);
}

qint64 MainWindow::OnlineFramePtsForTest(qint64 frame) const {
  if (session_ == nullptr || !session_->index().IsReady() || frame < 0 ||
      frame >= static_cast<qint64>(session_->index().size())) {
    return -1;
  }
  return session_->index().PtsOfFrame(frame);
}

bool MainWindow::OnlineStepIdleForTest() const {
  return !online_seek_running_ && !online_step_.inflight && !online_step_.queued;
}

QString MainWindow::OnlineStepStateForTest() const {
  return QStringLiteral(
             "dir=%1 desired=%2 inflight=%3 target=%4 cached=%5 queued=%6 "
             "qtarget=%7 seeks=%8 retargets=%9 boundary=%10 trace=%11 stop=%12")
      .arg(step_direction_)
      .arg(online_step_.desired)
      .arg(online_step_.inflight ? 1 : 0)
      .arg(online_step_.inflight_target)
      .arg(online_step_.inflight_cached ? 1 : 0)
      .arg(online_step_.queued ? 1 : 0)
      .arg(online_step_.queued_target)
      .arg(online_step_.seeks)
      .arg(online_step_.retargets)
      .arg(online_step_.boundary_stops)
      .arg(static_cast<qint64>(online_step_.trace.size()))
      .arg(step_stop_reason_.isEmpty() ? QStringLiteral("none")
                                       : step_stop_reason_);
}

std::vector<qint64> MainWindow::OnlineStepTraceForTest() const {
  std::vector<qint64> out;
  out.reserve(online_step_.trace.size());
  for (const std::int64_t frame : online_step_.trace) {
    out.push_back(static_cast<qint64>(frame));
  }
  return out;
}

void MainWindow::ResetOnlineStepTraceForTest() { online_step_.trace.clear(); }

int MainWindow::OnlineStepBoundaryStopsForTest() const {
  return online_step_.boundary_stops;
}

int MainWindow::OnlineStepRetargetCountForTest() const {
  return online_step_.retargets;
}

bool MainWindow::PreviewActiveForTest() const {
  return preview_timer_ != nullptr && preview_timer_->isActive();
}

PreviewTestDiagnostics MainWindow::PreviewDiagnosticsForTest() const {
  PreviewTestDiagnostics result;
  result.active = PreviewActiveForTest();
  result.target_frame = preview_target_frame_;
  result.displayed_frame = preview_displayed_frame_;
  result.displayed_pts_us = preview_displayed_pts_us_;
  result.displayed_coarse = preview_displayed_coarse_;
  result.displayed_from_cache = preview_displayed_from_cache_;
  result.displayed_hardware = preview_displayed_hardware_;
  result.drag_mode =
      QString::fromLatin1(lapian::media::DragModeName(preview_drag_mode_));
  result.drag_mode_reason =
      QString::fromLatin1(preview_drag_decision_.reason);
  result.drag_step = preview_last_step_;
  result.drag_interval_ms = preview_last_interval_ms_;
  result.drag_speed_frames_per_s = preview_drag_decision_.speed_frames_per_s;
  result.drag_same_direction_run = preview_same_direction_run_;
  result.target_lag_frames = preview_target_lag_frames_;
  result.target_lag_ms = preview_target_lag_ms_;
  result.presented = preview_presented_count_;
  if (preview_ == nullptr) {
    return result;
  }
  const lapian::media::PreviewDiagnostics diagnostics = preview_->diagnostics();
  result.hw_requested = diagnostics.hw_requested;
  result.hw_active = diagnostics.hw_active;
  result.hw_fallback_reason = QString::fromStdString(diagnostics.hw_fallback_reason);
  result.seeks = diagnostics.seeks;
  result.cache_hits = diagnostics.cache_hits;
  result.cache_misses = diagnostics.cache_misses;
  result.cache_requests = diagnostics.cache_requests;
  result.cache_hit_rate =
      diagnostics.cache_requests > 0
          ? static_cast<double>(diagnostics.cache_hits) /
                static_cast<double>(diagnostics.cache_requests)
          : 0.0;
  result.coarse_hits = diagnostics.coarse_hits;
  result.coarse_requests = diagnostics.coarse_requests;
  result.coarse_published = diagnostics.coarse_published;
  result.prefetched_frames = diagnostics.prefetched_frames;
  result.readbacks = diagnostics.readbacks;
  result.cache_bytes = static_cast<qint64>(diagnostics.cache_bytes);
  result.cache_peak_bytes = static_cast<qint64>(diagnostics.cache_peak_bytes);
  result.thumbnail_frames = static_cast<qint64>(diagnostics.thumbnail_frames);
  result.thumbnail_bytes = static_cast<qint64>(diagnostics.thumbnail_bytes);
  result.thumbnail_peak_bytes =
      static_cast<qint64>(diagnostics.thumbnail_peak_bytes);
  result.first_coarse_ms = diagnostics.first_coarse_ms;
  result.last_coarse_ms = diagnostics.last_coarse_ms;
  result.seek_ms_avg = diagnostics.seek_ms_avg;
  result.decode_ms_avg = diagnostics.decode_ms_avg;
  result.readback_ms_avg = diagnostics.readback_ms_avg;
  result.published = diagnostics.published;
  result.decoder_reopens = diagnostics.decoder_reopens;
  return result;
}

bool MainWindow::OnlinePreviewWaitingForTest() const {
  return online_preview_ != nullptr && online_preview_->IsWaiting();
}

bool MainWindow::OnlinePreviewThreadRunningForTest() const {
  return online_preview_ != nullptr && online_preview_->IsThreadRunning();
}

QString MainWindow::OnlinePreviewStateForTest() const {
  const bool active = online_preview_ != nullptr;
  const QString error =
      active ? QString::fromStdString(online_preview_->last_error())
             : QString();
  return QStringLiteral(
             "active=%1 waiting=%2 thread=%3 target=%4 presented=%5 "
             "dropped=%6 last_frame=%7 last_pts=%8 index_frames=%9 "
             "cache_frames=%10 seeks=%11 error=%12")
      .arg(active ? 1 : 0)
      .arg(OnlinePreviewWaitingForTest() ? 1 : 0)
      .arg(OnlinePreviewThreadRunningForTest() ? 1 : 0)
      .arg(online_preview_target_)
      .arg(online_preview_presented_)
      .arg(online_preview_dropped_)
      .arg(online_preview_last_frame_)
      .arg(online_preview_last_pts_)
      .arg(static_cast<qint64>(online_preview_index_frames_))
      .arg(active ? online_preview_->cached_frames() : 0)
      .arg(active ? static_cast<qint64>(online_preview_->seek_count()) : 0)
      .arg(error.isEmpty() ? QStringLiteral("none") : error);
}

void MainWindow::ResetPreviewStatsForTest() {
  online_preview_target_ = -1;
  online_preview_last_frame_ = -1;
  online_preview_last_pts_ = 0;
  online_preview_presented_ = 0;
  online_preview_dropped_ = 0;
  online_preview_errors_ = 0;
  preview_presented_count_ = 0;
  preview_target_frame_ = -1;
  preview_displayed_frame_ = -1;
  preview_displayed_pts_us_ = 0;
  preview_displayed_coarse_ = false;
  preview_displayed_from_cache_ = false;
  preview_displayed_hardware_ = false;
  preview_drag_mode_ = lapian::media::DragMode::Exact;
  preview_drag_decision_ = lapian::media::DragDecision{};
  preview_last_target_time_valid_ = false;
  preview_last_direction_ = 0;
  preview_same_direction_run_ = 0;
  preview_last_step_ = 0;
  preview_last_interval_ms_ = 0;
  preview_target_lag_frames_ = 0;
  preview_target_lag_ms_ = 0.0;
  online_preview_wait_status_ = false;
  online_preview_error_shown_ = false;
}

QString MainWindow::WaveformStateForTest() const {
  if (!waveform_) {
    return QStringLiteral("none");
  }
  using lapian::audio::WaveformState;
  const lapian::audio::WaveformStatus status = waveform_->status();
  switch (status.state) {
    case WaveformState::kIdle:
      return QStringLiteral("idle");
    case WaveformState::kRunning:
      return QStringLiteral("running:%1%")
          .arg(QString::number(status.progress * 100.0, 'f', 0));
    case WaveformState::kReady:
      return QStringLiteral("ready");
    case WaveformState::kFailed:
      return QStringLiteral("failed");
    case WaveformState::kNoAudio:
      return QStringLiteral("noaudio");
    case WaveformState::kCancelled:
      return QStringLiteral("cancelled");
  }
  return QStringLiteral("unknown");
}

void MainWindow::ShowGotoDialog() {
  if (IsTextInputFocused()) {
    return;
  }
  if (session_ == nullptr || !session_->IsOpen() || session_->index().empty()) {
    return;
  }
  bool ok = false;
  const QString input = QInputDialog::getText(
      this, QStringLiteral("跳到帧"),
      QStringLiteral("帧号（0 基）或 HH:MM:SS:FF"),
      QLineEdit::Normal, QString::number(current_frame_), &ok);
  if (!ok) {
    return;
  }
  const QString text = input.trimmed();
  if (text.isEmpty()) {
    return;
  }
  if (!ApplyGotoText(text)) {
    QMessageBox::warning(this, QStringLiteral("跳转失败"),
                         QStringLiteral("无法识别帧号或 HH:MM:SS:FF"));
  }
}

bool MainWindow::ApplyGotoText(const QString& text) {
  if (session_ == nullptr || !session_->IsOpen() || session_->index().empty()) {
    return false;
  }

  std::int64_t target = -1;
  bool numeric = false;
  const std::int64_t as_number = text.toLongLong(&numeric);
  if (numeric) {
    target = as_number;
  } else {
    const lapian::media::MediaInfo& info = session_->info();
    const lapian::core::FrameRate rate(info.video.frame_rate.num, info.video.frame_rate.den);
    if (session_->IsVfrSuspect()) {
      int hours = 0;
      int minutes = 0;
      int seconds = 0;
      int frames = 0;
      const QByteArray bytes = text.toLatin1();
      if (std::sscanf(bytes.constData(), "%d:%d:%d:%d", &hours, &minutes, &seconds, &frames) !=
          4) {
        return false;
      }
      const double nominal = rate.valid() ? static_cast<double>(rate.NominalFps()) : 30.0;
      const double time_seconds = hours * 3600.0 + minutes * 60.0 + seconds +
                                  static_cast<double>(frames) / nominal;
      target = session_->index().FrameOfPts(
          static_cast<std::int64_t>(time_seconds * 1'000'000.0));
    } else {
      const lapian::core::TimecodeFormatter formatter(rate);
      if (!formatter.valid() || !formatter.Parse(text.toStdString(), &target)) {
        return false;
      }
    }
  }

  if (target < 0) {
    return false;
  }
  const std::int64_t last_frame =
      static_cast<std::int64_t>(session_->index().size()) - 1;
  const std::int64_t clamped = lapian::core::ClampFrame(target, last_frame);
  CancelPendingMarkJump();
  GoToFrame(clamped);
  if (clamped != target) {
    statusBar()->showMessage(QStringLiteral("帧号越界，已到边界 %1").arg(clamped), 4000);
  }
  return true;
}

void MainWindow::GoToInputForTest(const QString& text) {
  ApplyGotoText(text.trimmed());
}

void MainWindow::TogglePlayback() {
  // 备注/帧号输入时 Space 属于文本输入，不开始播放（spec §3.7）。
  if (IsTextInputFocused()) {
    return;
  }
  if (playback_ == nullptr || !session_ || !session_->IsOpen()) {
    return;
  }
  CancelPendingMarkJump();
  // 拖动中按 Space：先放弃预览，避免预览帧在播放开始后覆盖画面。
  if (preview_timer_ != nullptr && preview_timer_->isActive()) {
    EndScrubPreview();
  }
  playback_->Toggle();
}

void MainWindow::StartPlaybackForTest() {
  if (playback_ != nullptr && session_ && session_->IsOpen() && !playback_->IsPlaying()) {
    playback_->Play();
  }
}

void MainWindow::StepForwardForTest() { StepForward(); }

bool MainWindow::IsTextInputFocused() const {
  QWidget* focus = QApplication::focusWidget();
  return qobject_cast<QLineEdit*>(focus) != nullptr ||
         qobject_cast<QTextEdit*>(focus) != nullptr ||
         qobject_cast<QPlainTextEdit*>(focus) != nullptr;
}

void MainWindow::ZoomTimelineIn() {
  if (IsTextInputFocused()) {
    return;
  }
  if (timeline_ != nullptr) {
    timeline_->ZoomIn();
  }
}

void MainWindow::ZoomTimelineOut() {
  if (IsTextInputFocused()) {
    return;
  }
  if (timeline_ != nullptr) {
    timeline_->ZoomOut();
  }
}

void MainWindow::ToggleWaveform(bool visible) {
  if (IsTextInputFocused()) {
    // W 是文本输入的一部分；静默恢复勾选状态，避免 toggled 递归。
    if (waveform_action_ != nullptr) {
      const QSignalBlocker blocker(waveform_action_);
      waveform_action_->setChecked(!visible);
    }
    return;
  }
  waveform_visible_ = visible;
  if (timeline_ != nullptr) {
    timeline_->SetWaveformVisible(visible);
  }
  ScheduleSettingsSave();
}

void MainWindow::ApplySpeed(double rate) {
  if (!std::isfinite(rate)) {
    return;
  }
  rate_ = std::clamp(rate, 0.25, 2.0);
  if (speed_label_ != nullptr) {
    speed_label_->setText(FormatSpeed(rate_));
  }
  if (playback_ != nullptr) {
    playback_->SetRate(rate_);
  }
  if (audio_ != nullptr) {
    audio_->SetRate(rate_);
  }
  statusBar()->showMessage(QStringLiteral("速度 %1").arg(FormatSpeed(rate_)), 1500);
}

void MainWindow::SpeedDown() {
  if (IsTextInputFocused()) {
    return;
  }
  ApplySpeed(lapian::core::StepPlaybackRate(rate_, -1));
}

void MainWindow::SpeedUp() {
  if (IsTextInputFocused()) {
    return;
  }
  ApplySpeed(lapian::core::StepPlaybackRate(rate_, 1));
}

void MainWindow::SetSpeedForTest(double rate) { ApplySpeed(rate); }

bool MainWindow::CanEditLoop() const {
  return session_ != nullptr && session_->IsOpen() &&
         session_->index().IsReady() && !session_->index().empty();
}

void MainWindow::SyncLoopUi() {
  const bool can_edit = CanEditLoop();
  if (loop_in_action_ != nullptr) {
    loop_in_action_->setEnabled(can_edit);
  }
  if (loop_out_action_ != nullptr) {
    loop_out_action_->setEnabled(can_edit);
  }
  if (loop_toggle_action_ != nullptr) {
    loop_toggle_action_->setEnabled(can_edit);
  }
  if (loop_clear_action_ != nullptr) {
    loop_clear_action_->setEnabled(can_edit);
  }
  if (timeline_ != nullptr) {
    timeline_->SetLoopRange(loop_);
  }
  if (playback_ != nullptr) {
    playback_->SetLoopRange(loop_.enabled() && loop_.complete(), loop_.in(),
                            loop_.out());
  }
  if (loop_label_ == nullptr) {
    return;
  }
  if (!loop_.has_in()) {
    loop_label_->setText(QStringLiteral("循环：关"));
    return;
  }
  if (!loop_.has_out()) {
    loop_label_->setText(QStringLiteral("循环：关 · A=%1（未设 B）")
                             .arg(static_cast<qlonglong>(loop_.in())));
    return;
  }
  const QString range =
      loop_.is_single_frame()
          ? QStringLiteral("A=B=%1").arg(static_cast<qlonglong>(loop_.in()))
          : QStringLiteral("A=%1 B=%2")
                .arg(static_cast<qlonglong>(loop_.in()))
                .arg(static_cast<qlonglong>(loop_.out()));
  loop_label_->setText((loop_.enabled() ? QStringLiteral("循环：开 · ")
                                        : QStringLiteral("循环：关 · ")) +
                       range);
}

void MainWindow::ApplyLoopIn() {
  if (!CanEditLoop()) {
    return;
  }
  const lapian::core::LoopEditResult result = loop_.SetIn(current_frame_);
  switch (result) {
    case lapian::core::LoopEditResult::kSet:
      statusBar()->showMessage(
          QStringLiteral("入点 A = %1 帧").arg(static_cast<qlonglong>(loop_.in())),
          2500);
      break;
    case lapian::core::LoopEditResult::kClearedOut:
      statusBar()->showMessage(
          QStringLiteral("入点 A = %1 晚于原出点，已清除 B")
              .arg(static_cast<qlonglong>(loop_.in())),
          4000);
      break;
    default:
      break;
  }
  SyncLoopUi();
}

void MainWindow::ApplyLoopOut() {
  if (!CanEditLoop()) {
    return;
  }
  const lapian::core::LoopEditResult result = loop_.SetOut(current_frame_);
  switch (result) {
    case lapian::core::LoopEditResult::kSet:
      statusBar()->showMessage(
          QStringLiteral("出点 B = %1 帧").arg(static_cast<qlonglong>(loop_.out())),
          2500);
      break;
    case lapian::core::LoopEditResult::kNeedsIn:
      statusBar()->showMessage(QStringLiteral("请先按 I 设置入点 A"), 3500);
      break;
    case lapian::core::LoopEditResult::kOutBeforeIn:
      statusBar()->showMessage(
          QStringLiteral("出点 B 不能早于入点 A（当前 A=%1），未设置")
              .arg(static_cast<qlonglong>(loop_.in())),
          4000);
      break;
    default:
      break;
  }
  SyncLoopUi();
}

void MainWindow::ApplyLoopToggle() {
  if (!CanEditLoop()) {
    return;
  }
  const lapian::core::LoopEditResult result = loop_.Toggle();
  if (result == lapian::core::LoopEditResult::kNeedsRange) {
    statusBar()->showMessage(QStringLiteral("请先设置 A/B 入出点，再开循环"), 3500);
    SyncLoopUi();
    return;
  }
  SyncLoopUi();
  if (loop_.enabled()) {
    const QString range =
        loop_.is_single_frame()
            ? QStringLiteral("A=B=%1").arg(static_cast<qlonglong>(loop_.in()))
            : QStringLiteral("A=%1 B=%2")
                  .arg(static_cast<qlonglong>(loop_.in()))
                  .arg(static_cast<qlonglong>(loop_.out()));
    statusBar()->showMessage(QStringLiteral("循环开启：") + range, 3000);
  } else {
    statusBar()->showMessage(QStringLiteral("循环关闭"), 2500);
  }
}

void MainWindow::ApplyLoopClear() {
  if (!CanEditLoop()) {
    return;
  }
  const bool had_range = loop_.has_in() || loop_.has_out();
  loop_.Clear();
  SyncLoopUi();
  statusBar()->showMessage(had_range ? QStringLiteral("已清除 A/B，循环关闭")
                                     : QStringLiteral("没有 A/B 可清除"),
                           2500);
}

void MainWindow::SetLoopIn() {
  if (IsTextInputFocused()) {
    return;
  }
  ApplyLoopIn();
}

void MainWindow::SetLoopOut() {
  if (IsTextInputFocused()) {
    return;
  }
  ApplyLoopOut();
}

void MainWindow::ToggleLoop() {
  if (IsTextInputFocused()) {
    return;
  }
  ApplyLoopToggle();
}

void MainWindow::ClearLoop() {
  if (IsTextInputFocused()) {
    return;
  }
  ApplyLoopClear();
}

void MainWindow::SetLoopInForTest() { ApplyLoopIn(); }

void MainWindow::SetLoopOutForTest() { ApplyLoopOut(); }

void MainWindow::ToggleLoopForTest() { ApplyLoopToggle(); }

void MainWindow::ClearLoopForTest() { ApplyLoopClear(); }

QString MainWindow::LoopStateForTest() const {
  return QStringLiteral("frame=%1 loop=%2 has_in=%3 a=%4 has_out=%5 b=%6")
      .arg(static_cast<qlonglong>(current_frame_))
      .arg(loop_.enabled() ? 1 : 0)
      .arg(loop_.has_in() ? 1 : 0)
      .arg(static_cast<qlonglong>(loop_.in()))
      .arg(loop_.has_out() ? 1 : 0)
      .arg(static_cast<qlonglong>(loop_.out()));
}

void MainWindow::TogglePlaybackForTest() { TogglePlayback(); }

int MainWindow::LoopJumpCountForTest() const {
  return playback_ == nullptr ? 0 : playback_->LoopJumpCountForTest();
}

QString MainWindow::LoopPlaybackStateForTest() const {
  bool audio_clock = false;
  double audio_clock_seconds = 0.0;
  int audio_state = -1;
  double audio_position = 0.0;
  if (audio_) {
    audio_clock = audio_->ClockSeconds(&audio_clock_seconds);
    const lapian::audio::AudioStatus status = audio_->status();
    audio_state = static_cast<int>(status.state);
    audio_position = status.position_seconds;
  }
  const bool playing = playback_ != nullptr && playback_->IsPlaying();
  const bool frozen = playback_ != nullptr && playback_->frozen();
  return QStringLiteral(
             "frame=%1 playing=%2 frozen=%3 loop=%4 a=%5 b=%6 "
             "audio_clock=%7 audio_state=%8 audio_pos=%9 audio_clock_sec=%10")
      .arg(static_cast<qlonglong>(current_frame_))
      .arg(playing ? 1 : 0)
      .arg(frozen ? 1 : 0)
      .arg(loop_.enabled() ? 1 : 0)
      .arg(static_cast<qlonglong>(loop_.in()))
      .arg(static_cast<qlonglong>(loop_.out()))
      .arg(audio_clock ? 1 : 0)
      .arg(audio_state)
      .arg(QString::number(audio_position, 'f', 3))
      .arg(QString::number(audio_clock_seconds, 'f', 3));
}

void MainWindow::ShowScrubTarget(std::int64_t frame) {
  if (info_bar_ != nullptr) {
    info_bar_->SetFrame(static_cast<int>(frame));
  }
}

void MainWindow::OnScrubStarted(qint64 frame) {
  if (session_ == nullptr || !session_->IsOpen()) {
    return;
  }
  CancelPendingMarkJump();
  if (playback_ != nullptr) {
    playback_->Pause();
  }
  ShowScrubTarget(frame);
  if (preview_ != nullptr) {
    preview_target_frame_ = frame;
    preview_displayed_frame_ = -1;
    preview_displayed_pts_us_ = 0;
    preview_displayed_coarse_ = false;
    preview_displayed_from_cache_ = false;
    preview_displayed_hardware_ = false;
    // DP-3：新拖动从 Exact 起步，阈值/模式的上下文全部清零；真正切到
    // Fast/Reverse/FarJump 要等目标更新后由 UpdatePreviewDragStrategy 判定。
    preview_drag_mode_ = lapian::media::DragMode::Exact;
    preview_drag_decision_ = lapian::media::DragDecision{};
    preview_drag_decision_.reason = "begin";
    preview_last_target_time_ = std::chrono::steady_clock::now();
    preview_last_target_time_valid_ = true;
    preview_last_direction_ = 0;
    preview_same_direction_run_ = 0;
    preview_last_step_ = 0;
    preview_last_interval_ms_ = 0;
    preview_target_lag_frames_ = 0;
    preview_target_lag_ms_ = 0.0;
    EnsurePreviewDecoderDevice();
    preview_->SetDragMode(preview_drag_mode_);
    preview_->Begin(frame);
    if (preview_timer_ != nullptr) {
      // DP-1：Windows 默认 15.6ms 定时器粒度会把 16ms 推到 ~31ms，导致
      // 预览帧每隔一个 tick 才被取走（P4 掉到 ~35fps）。本地预览用 10ms
      // 截止时间，稳定落在 15.6ms 粒度上（约 64Hz）；在线预览仍用 16ms。
      preview_timer_->setInterval(10);
      if (!preview_timer_->isActive()) {
        preview_timer_->start();
      }
    }
    statusBar()->showMessage(QStringLiteral("拖动预览：松开后精确到帧"), 1000);
    return;
  }
  if (OnlineStepEligible()) {
    // T8.6.1b：增长会话走独立的 GrowingSource 预览读层；未缓存区只提示等待。
    EnsureOnlinePreviewStarted(frame);
    if (online_preview_ != nullptr) {
      if (!OnlineFrameCached(frame)) {
        ApplyOnlinePreviewWaitPrompt(frame, true);
      } else {
        statusBar()->showMessage(
            QStringLiteral("拖动预览：松开后精确到帧"), 1000);
      }
    } else {
      ApplyOnlinePreviewWaitPrompt(frame, true);
    }
    return;
  }
}

void MainWindow::UpdatePreviewDragStrategy(qint64 frame) {
  const qint64 previous = preview_target_frame_;
  if (previous < 0 || frame == previous) {
    return;
  }
  const qint64 step = frame > previous ? frame - previous : previous - frame;
  const int direction = frame > previous ? 1 : -1;
  const auto now = std::chrono::steady_clock::now();
  qint64 interval_ms = 0;
  if (preview_last_target_time_valid_) {
    interval_ms = static_cast<qint64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - preview_last_target_time_)
            .count());
    if (interval_ms < 0) {
      interval_ms = 0;
    }
  }
  if (direction == preview_last_direction_) {
    ++preview_same_direction_run_;
  } else {
    preview_same_direction_run_ = 1;
  }
  lapian::media::DragInput input;
  input.target = frame;
  input.previous_target = previous;
  input.step = step;
  input.direction = direction;
  input.since_previous_ms = interval_ms;
  input.same_direction_run = preview_same_direction_run_;
  preview_drag_decision_ = lapian::media::ClassifyDrag(input);
  preview_drag_mode_ = preview_drag_decision_.mode;
  preview_last_step_ = step;
  preview_last_interval_ms_ = interval_ms;
  preview_last_direction_ = direction;
  preview_last_target_time_ = now;
  preview_last_target_time_valid_ = true;
  if (preview_ != nullptr) {
    preview_->SetDragMode(preview_drag_mode_);
  }
}

void MainWindow::OnScrubTargetChanged(qint64 frame) {
  if (session_ == nullptr || !session_->IsOpen()) {
    return;
  }
  ShowScrubTarget(frame);
  if (preview_ != nullptr) {
    // DP-3：先按上一个目标与本次步长/方向/间隔分类，再写目标；策略结果
    // 交给 PreviewDecoder，决定 Exact 是否允许粗帧、FarJump 是否先粗后精。
    UpdatePreviewDragStrategy(frame);
    preview_target_frame_ = frame;
    preview_->UpdateTarget(frame);
    return;
  }
  if (!OnlineStepEligible()) {
    return;
  }
  if (online_preview_ == nullptr) {
    EnsureOnlinePreviewStarted(frame);
  }
  if (online_preview_ == nullptr) {
    ApplyOnlinePreviewWaitPrompt(frame, true);
    return;
  }
  online_preview_target_ = frame;
  MaybeRefreshOnlinePreviewIndex();
  const bool cached = OnlineFrameCached(frame);
  online_preview_->UpdateTarget(frame, cached);
  if (!cached) {
    ApplyOnlinePreviewWaitPrompt(frame, false);
  }
}

void MainWindow::OnScrubFinished(qint64 frame) {
  if (session_ == nullptr || !session_->IsOpen()) {
    return;
  }
  // 先停预览/join 预览线程，再走精确 seek；预览线程的 reader 不能和松手后的
  // 精确 seek/finalize 抢同一个 `.part` 句柄。
  EndScrubPreview();
  if (session_->index().empty() && !OnlineStepEligible()) {
    return;
  }
  const std::int64_t last_frame =
      OnlineStepEligible() ? OnlineLastFrame()
                           : static_cast<std::int64_t>(session_->index().size()) - 1;
  if (last_frame < 0) {
    return;
  }
  const std::int64_t target =
      lapian::core::ClampFrame(frame, last_frame);
  // 预览帧可能是降采样的，且不一定正好是目标帧；松手必须回到精确帧。
  const bool timing = qEnvironmentVariableIsSet("LAPIAN_TIMING_LOG");
  QElapsedTimer release_timer;
  if (timing) {
    release_timer.start();
  }
  GoToFrame(target);
  if (timing) {
    std::fprintf(stderr, "T2.10 release_exact_ms=%lld frame=%lld\n",
                 static_cast<long long>(release_timer.elapsed()),
                 static_cast<long long>(target));
    std::fflush(stderr);
  }
}

void MainWindow::EndScrubPreview() {
  if (preview_timer_ != nullptr) {
    preview_timer_->stop();
  }
  if (preview_ != nullptr) {
    preview_->End();
  }
  if (online_preview_ != nullptr) {
    online_preview_dropped_ =
        static_cast<int>(online_preview_->stale_dropped());
    if (!online_preview_error_shown_ &&
        !online_preview_->last_error().empty()) {
      ++online_preview_errors_;
    }
    online_preview_->End();
    online_preview_.reset();
  }
  online_preview_index_frames_ = 0;
  online_preview_error_shown_ = false;
  ClearOnlinePreviewWaitPrompt();
}

void MainWindow::EnsureOnlinePreviewStarted(std::int64_t frame) {
  if (online_preview_ != nullptr || !OnlineStepEligible() || session_ == nullptr) {
    return;
  }
  const lapian::media::FrameIndex& index = session_->index();
  if (!index.IsReady() || index.empty() || online_stable_frames_ <= 0 ||
      online_part_path_.isEmpty()) {
    return;
  }
  auto source = session_->growing_source_for_index();
  if (!source) {
    return;
  }
  auto snapshot = std::make_shared<lapian::media::FrameIndex>(index);
  const QByteArray part_utf8 = online_part_path_.toUtf8();
  const std::string part_path(part_utf8.constData(),
                              static_cast<std::size_t>(part_utf8.size()));
  online_preview_ = std::make_unique<lapian::media::OnlinePreviewDecoder>();
  online_preview_index_frames_ = snapshot->size();
  online_preview_target_ = frame;
  online_preview_error_shown_ = false;
  const bool initial_cached = OnlineFrameCached(frame);
  online_preview_->Begin(std::move(source), part_path,
                         session_->video_stream_index(), std::move(snapshot),
                         frame, initial_cached);
  if (preview_timer_ != nullptr) {
    preview_timer_->setInterval(16);
    if (!preview_timer_->isActive()) {
      preview_timer_->start();
    }
  }
}

void MainWindow::MaybeRefreshOnlinePreviewIndex() {
  if (online_preview_ == nullptr || session_ == nullptr) {
    return;
  }
  const lapian::media::FrameIndex& index = session_->index();
  if (!index.IsReady() || index.empty() ||
      index.size() <= online_preview_index_frames_) {
    return;
  }
  // 目标已经超出快照时必须立刻换；否则按每增长 30 帧节流，避免拖动中频繁
  // 在 UI 线程拷贝大索引。
  const bool target_beyond_snapshot =
      online_preview_target_ >=
      static_cast<std::int64_t>(online_preview_index_frames_);
  if (!target_beyond_snapshot &&
      index.size() <= online_preview_index_frames_ + 30) {
    return;
  }
  auto snapshot = std::make_shared<lapian::media::FrameIndex>(index);
  online_preview_index_frames_ = snapshot->size();
  online_preview_->UpdateIndex(std::move(snapshot));
}

void MainWindow::ApplyOnlinePreviewWaitPrompt(std::int64_t target, bool force) {
  if (status_label_ == nullptr) {
    return;
  }
  if (online_preview_wait_status_ && !force &&
      online_preview_wait_timer_.isValid() &&
      online_preview_wait_timer_.elapsed() < 100) {
    return;
  }
  statusBar()->showMessage(
      QStringLiteral("缓冲中（预览）：目标帧 %1").arg(target), 8000);
  status_label_->setText(
      base_info_ +
      QStringLiteral(" · 拖动预览：缓冲中（目标帧 %1）").arg(target));
  online_preview_wait_status_ = true;
  online_preview_wait_timer_.restart();
}

void MainWindow::ClearOnlinePreviewWaitPrompt() {
  if (!online_preview_wait_status_) {
    return;
  }
  online_preview_wait_status_ = false;
  online_preview_wait_timer_.invalidate();
  if (status_label_ != nullptr) {
    status_label_->setText(base_info_ + online_status_detail_);
  }
}

void MainWindow::TickOnlinePreview() {
  if (online_preview_ == nullptr || session_ == nullptr || gfx_ == nullptr) {
    return;
  }
  MaybeRefreshOnlinePreviewIndex();
  const std::int64_t target = online_preview_target_;
  // 下载追上后，同一个未缓存目标可能变可预览；每个 tick 重新问一次。
  if (target >= 0 && online_preview_->IsWaiting() &&
      OnlineFrameCached(target)) {
    online_preview_->UpdateTarget(target, true);
  }
  lapian::media::PreviewFrame preview_frame =
      online_preview_->TakeLatestFrame();
  if (preview_frame.frame != nullptr) {
    gfx_->SetVideoFrame(
        preview_frame.frame,
        session_->info().rotation.counterclockwise_degrees);
    ++preview_presented_count_;
    ++online_preview_presented_;
    online_preview_last_frame_ = preview_frame.frame_index;
    online_preview_last_pts_ = preview_frame.pts_us;
    av_frame_free(&preview_frame.frame);
  }
  online_preview_dropped_ =
      static_cast<int>(online_preview_->stale_dropped());
  const std::string error = online_preview_->last_error();
  if (!error.empty()) {
    if (!online_preview_error_shown_) {
      online_preview_error_shown_ = true;
      ++online_preview_errors_;
      statusBar()->showMessage(
          QStringLiteral("拖动预览不可用：%1")
              .arg(QString::fromStdString(error)),
          6000);
      ClearOnlinePreviewWaitPrompt();
    }
    return;
  }
  if (online_preview_->IsWaiting()) {
    ApplyOnlinePreviewWaitPrompt(target, false);
  } else {
    ClearOnlinePreviewWaitPrompt();
  }
}

void MainWindow::EnsurePreviewDecoderDevice() {
  if (preview_ == nullptr || session_ == nullptr || !session_->IsOpen()) {
    return;
  }
  ID3D11Device* device = gfx_ != nullptr ? gfx_->d3d_device() : nullptr;
  if (preview_->DeviceMatches(device)) {
    return;
  }
  // 设备丢失后 GfxWidget 会重建设备；旧预览解码器仍引用旧设备（硬件帧无法
  // 上传到新设备）。这里重开预览解码器，新设备可用时继续硬解，否则回退软件。
  const bool resume = preview_timer_ != nullptr && preview_timer_->isActive() &&
                      preview_target_frame_ >= 0;
  preview_->Close();
  preview_->Configure(session_->path_utf8(), session_->video_stream_index(),
                      &session_->index(), device, true,
                      session_->info().video.width,
                      session_->info().video.height);
  if (resume) {
    // 设备重建后恢复同一次拖动的策略，避免 FarJump 退化成 Exact 而丢掉
    // “先粗后精”，或 Exact 被误当成快拖重新允许粗帧。
    preview_->SetDragMode(preview_drag_mode_);
    preview_->Begin(preview_target_frame_);
  }
}

void MainWindow::StopPreview() {
  if (preview_timer_ != nullptr) {
    preview_timer_->stop();
  }
  if (preview_ != nullptr) {
    preview_->Close();
  }
  if (online_preview_ != nullptr) {
    online_preview_dropped_ =
        static_cast<int>(online_preview_->stale_dropped());
    online_preview_->Close();
    online_preview_.reset();
  }
  online_preview_index_frames_ = 0;
  online_preview_error_shown_ = false;
  ClearOnlinePreviewWaitPrompt();
}

void MainWindow::OnPreviewTick() {
  if (session_ == nullptr || !session_->IsOpen() || gfx_ == nullptr) {
    return;
  }
  if (online_preview_ != nullptr && OnlineStepEligible()) {
    TickOnlinePreview();
    return;
  }
  if (preview_ == nullptr) {
    return;
  }
  // DP-1：设备丢失重建后，预览解码器仍持有旧设备；重开后再继续取帧。
  EnsurePreviewDecoderDevice();
  const bool timing = qEnvironmentVariableIsSet("LAPIAN_TIMING_LOG");
  QElapsedTimer tick_timer;
  if (timing) {
    tick_timer.start();
  }
  lapian::media::PreviewFrame preview_frame = preview_->TakeLatestFrame();
  if (preview_frame.frame == nullptr) {
    return;
  }
  // DP-1：硬件帧上传失败不弹错误横幅，直接回退软件并记录 preview_hw_fallback。
  const bool uploaded = gfx_->SetVideoFrame(
      preview_frame.frame, session_->info().rotation.counterclockwise_degrees,
      false);
  if (!uploaded) {
    if (preview_->diagnostics().hw_active) {
      preview_->ForceSoftwareFallback("upload_failed");
    }
    av_frame_free(&preview_frame.frame);
    return;
  }
  ++preview_presented_count_;
  preview_displayed_frame_ = preview_frame.frame_index;
  preview_displayed_pts_us_ = preview_frame.pts_us;
  preview_displayed_coarse_ = preview_frame.coarse;
  preview_displayed_from_cache_ = preview_frame.from_cache;
  preview_displayed_hardware_ = preview_frame.hardware;
  // DP-3：Present 时记录“显示帧 vs 当前目标”的绝对滞后；慢拖要求 ≤2 帧，
  // 快拖只记录 p50/p95，不能用 fps 冒充跟随。
  if (preview_target_frame_ >= 0 && preview_displayed_frame_ >= 0) {
    preview_target_lag_frames_ = preview_displayed_frame_ > preview_target_frame_
                                     ? preview_displayed_frame_ -
                                           preview_target_frame_
                                     : preview_target_frame_ -
                                           preview_displayed_frame_;
  } else {
    preview_target_lag_frames_ = 0;
  }
  const double preview_fps = FrameRateForTest();
  preview_target_lag_ms_ =
      preview_fps > 0.0
          ? static_cast<double>(preview_target_lag_frames_) * 1000.0 / preview_fps
          : 0.0;
  av_frame_free(&preview_frame.frame);
  if (timing && tick_timer.elapsed() > 200) {
    std::fprintf(stderr, "T2.10 preview_tick_ms=%lld\n",
                 static_cast<long long>(tick_timer.elapsed()));
    std::fflush(stderr);
  }
}

void MainWindow::OnFrameReady(const AVFrame* frame, int rotation, int frame_index) {
  if (playback_ != nullptr && playback_->IsPlaying() && loop_.enabled() &&
      loop_.complete() && frame_index > loop_.out() &&
      qEnvironmentVariableIsSet("LAPIAN_LOOP_LOG")) {
    // 循环播放期间不应呈现 B+1；出现即为回归。
    std::fprintf(stderr, "T4.2 ERROR presented_after_b frame=%d b=%lld\n",
                 frame_index, static_cast<long long>(loop_.out()));
    std::fflush(stderr);
  }
  if (gfx_ != nullptr && frame != nullptr) {
    gfx_->SetVideoFrame(frame, rotation);
    RememberDisplayedFrame(frame, rotation, frame_index);
  }
  if (info_bar_ != nullptr) {
    info_bar_->SetFrame(frame_index);
  }
  if (timeline_ != nullptr) {
    timeline_->SetCurrentFrame(frame_index);
  }
  current_frame_ = frame_index;
  if (session_ != nullptr && session_->index().IsReady()) {
    if (const lapian::media::FrameIndexEntry* entry =
            session_->index().Entry(frame_index)) {
      current_pts_us_ = entry->pts_us;
    }
  } else if (playback_ != nullptr) {
    current_pts_us_ = static_cast<std::int64_t>(
        playback_->CurrentMediaSeconds() * 1.0e6);
  }
}

void MainWindow::RememberDisplayedFrame(const AVFrame* frame, int rotation,
                                        std::int64_t frame_index) {
  if (frame == nullptr) {
    return;
  }
  if (displayed_frame_ == nullptr) {
    displayed_frame_ = av_frame_alloc();
  }
  if (displayed_frame_ == nullptr) {
    displayed_frame_index_ = -1;
    return;
  }
  av_frame_unref(displayed_frame_);
  if (av_frame_ref(displayed_frame_, frame) < 0) {
    displayed_frame_index_ = -1;
    return;
  }
  displayed_frame_index_ = frame_index;
  displayed_rotation_ = rotation;
}

void MainWindow::CaptureSnapshot() {
  // 备注/帧号输入框里的 S 是文本输入，不是截图快捷键（spec §3.7）。
  if (IsTextInputFocused()) {
    return;
  }
  if (session_ == nullptr || !session_->IsOpen() || displayed_frame_ == nullptr ||
      displayed_frame_index_ < 0) {
    statusBar()->showMessage(QStringLiteral("当前没有可截图的画面"), 3000);
    return;
  }
  // 拖动中的画面是降采样预览，不是源分辨率；不把预览帧当正式截图。
  if (preview_timer_ != nullptr && preview_timer_->isActive()) {
    statusBar()->showMessage(QStringLiteral("拖动预览中，松开后再截图"), 3000);
    return;
  }
  if (snapshots_ == nullptr) {
    return;
  }

  SnapshotService::Request request;
  request.video_path = QString::fromStdString(session_->path_utf8());
  request.frame = displayed_frame_index_;
  request.rotation_counterclockwise_degrees = displayed_rotation_;
  request.fps_num = session_->info().video.frame_rate.num;
  request.fps_den = session_->info().video.frame_rate.den;
  if (online_ready_) {
    request.online = true;
    request.online_bvid = online_bvid_;
    request.online_cid = online_cid_;
    request.online_qn = online_qn_;
    request.online_codecid = online_codecid_;
    request.online_fps = online_actual_fps_ > 0.0
                             ? online_actual_fps_
                             : (online_fps_ > 0.0
                                    ? online_fps_
                                    : session_->info().video.frame_rate.ToDouble());
    request.online_pts_us = current_pts_us_;
    request.online_appdata_root = online_records_root_for_test_;
  }

  // 按键路径只做 av_frame_ref（O(1)）；回读/转换/编码/写盘全在工作线程。
  QElapsedTimer timer;
  timer.start();
  const bool queued = snapshots_->Capture(displayed_frame_, request);
  const double capture_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  snapshot_capture_samples_.push_back(capture_ms);
  if (!queued) {
    statusBar()->showMessage(QStringLiteral("截图队列已满或当前帧不可引用"), 4000);
    return;
  }

  snapshot_last_capture_frame_ = displayed_frame_index_;
  if (snapshot_log_) {
    std::fprintf(stderr,
                 "T5.4 snapshot_capture frame=%lld rotation=%d capture_ms=%.2f "
                 "pending=%d\n",
                 static_cast<long long>(displayed_frame_index_), displayed_rotation_,
                 capture_ms, snapshots_->PendingCount());
    std::fflush(stderr);
  }
  statusBar()->showMessage(
      QStringLiteral("正在保存截图（帧 %1）…")
          .arg(static_cast<qlonglong>(displayed_frame_index_)),
      3000);
}

void MainWindow::ToggleFullScreen() {
  // spec §3.7：文本框聚焦时除 Esc/Enter 外不触发全局快捷键。F 与双击画面
  // 都不该在备注/帧号输入中间抢焦点（MarkPanel 的 ShortcutOverride 是第一层）。
  if (IsTextInputFocused()) {
    return;
  }
  if (isFullScreen()) {
    ExitFullScreen();
  } else {
    EnterFullScreen();
  }
}

void MainWindow::EnterFullScreen() {
  if (isFullScreen()) {
    return;
  }
  // 布局不改：时间轴/信息栏/侧栏/状态栏在全屏下仍可用（F1-A6 只要求
  // 画面比例不变、时间轴与帧号仍可用）。
  fullscreen_geometry_ = saveGeometry();
  fullscreen_was_maximized_ = isMaximized();
  fullscreen_focus_widget_ = QApplication::focusWidget();

  QElapsedTimer timer;
  timer.start();
  showFullScreen();
  fullscreen_last_toggle_ms_ = timer.elapsed();
  UpdateFullScreenActionState();
  RestoreFocusIfNeeded(fullscreen_focus_widget_);
  if (gfx_ != nullptr) {
    gfx_->update();
  }
  statusBar()->showMessage(QStringLiteral("已进入全屏：F / Esc 退出"), 2500);
  qInfo() << "进入全屏:" << width() << "x" << height()
          << "耗时" << fullscreen_last_toggle_ms_ << "ms";
}

void MainWindow::ExitFullScreen() {
  if (!isFullScreen()) {
    return;
  }
  QPointer<QWidget> focus_before = QApplication::focusWidget();
  const bool was_maximized = fullscreen_was_maximized_;
  const QByteArray geometry = fullscreen_geometry_;

  QElapsedTimer timer;
  timer.start();
  if (was_maximized) {
    showMaximized();
  } else {
    showNormal();
    restoreGeometry(geometry);
  }
  fullscreen_last_toggle_ms_ = timer.elapsed();
  fullscreen_was_maximized_ = false;
  UpdateFullScreenActionState();
  // 全屏切换不应让键盘焦点消失：优先恢复到退出前的控件，其次退回进入前的。
  RestoreFocusIfNeeded(focus_before);
  if (QApplication::focusWidget() == nullptr) {
    RestoreFocusIfNeeded(fullscreen_focus_widget_);
  }
  fullscreen_focus_widget_.clear();
  if (gfx_ != nullptr) {
    gfx_->update();
  }
  statusBar()->showMessage(QStringLiteral("已退出全屏"), 1500);
  qInfo() << "退出全屏:" << width() << "x" << height()
          << "耗时" << fullscreen_last_toggle_ms_ << "ms";
}

void MainWindow::UpdateFullScreenActionState() {
  if (exit_fullscreen_action_ == nullptr) {
    return;
  }
  // 文本框聚焦时把 Esc 留给文本框（T5.3），同时 handler 里还会再检查一次。
  exit_fullscreen_action_->setEnabled(isFullScreen() && !IsTextInputFocused());
}

void MainWindow::RestoreFocusIfNeeded(QWidget* expected) {
  if (expected == nullptr || !expected->isVisible() ||
      expected->focusPolicy() == Qt::NoFocus) {
    return;
  }
  if (QApplication::focusWidget() == expected) {
    return;
  }
  expected->setFocus(Qt::OtherFocusReason);
}

bool MainWindow::CanStepFrames() const {
  return session_ != nullptr && session_->IsOpen() && session_->index().IsReady() &&
         !session_->index().empty();
}

bool MainWindow::HandleGlobalKey(QKeyEvent* event) {
  if (event == nullptr) {
    return false;
  }
  const int key = event->key();
  const Qt::KeyboardModifiers key_modifiers = event->modifiers();
  if (qEnvironmentVariableIsSet("LAPIAN_KEY_LOG") &&
      (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
    std::fprintf(stderr,
                 "T6.2 key_event type=%d key=%d auto=%d dir=%d frame=%lld\n",
                 static_cast<int>(event->type()), key,
                 event->isAutoRepeat() ? 1 : 0, step_direction_,
                 static_cast<long long>(current_frame_));
    std::fflush(stderr);
  }
  const bool is_step = key == Qt::Key_PageUp || key == Qt::Key_PageDown ||
                       key == Qt::Key_Left || key == Qt::Key_Right;
  const bool is_nav = key == Qt::Key_Home || key == Qt::Key_End;
  // US 布局的 Shift+= 在 Qt 里通常已是 Key_Plus；少数布局/输入法会保留
  // Key_Equal + Shift，这里一并接住，避免 '+' 在某些键盘上失效。
  const bool is_zoom_in = key == Qt::Key_Plus ||
                          (key == Qt::Key_Equal &&
                           key_modifiers.testFlag(Qt::ShiftModifier));
  const bool is_zoom = is_zoom_in || key == Qt::Key_Minus;
  if (!is_step && !is_nav && !is_zoom) {
    return false;
  }
  if (event->type() == QEvent::KeyRelease) {
    // 只吞掉“正在连发”的松开；文本框里的松开仍交给文本框。
    if (is_step && step_direction_ != 0) {
      // Qt 在物理长按时会为 OS 自动重复插入 KeyRelease（见 Qt FAQ“长按收到
      // 多个 keyReleaseEvents”）。这种 release 不是松开，不能停连发；真正松开
      // 的 KeyRelease 才是 isAutoRepeat()==false。
      if (event->isAutoRepeat()) {
        return true;
      }
      // Windows/Qt 在某些时序下会在物理键仍按住时补一个非 autoRepeat 的
      // release；只认真正的松开，避免长按在第一步后停住。
      if (IsStepKeyPhysicallyDown(key)) {
        if (qEnvironmentVariableIsSet("LAPIAN_KEY_LOG")) {
          std::fprintf(stderr,
                       "T8.6.1a step_release_ignored physical_down key=%d "
                       "dir=%d frame=%lld\n",
                       key, step_direction_,
                       static_cast<long long>(current_frame_));
          std::fflush(stderr);
        }
        return true;
      }
      StopStepRepeat("keyup");
      return true;
    }
    return false;
  }
  if (event->type() != QEvent::KeyPress) {
    return false;
  }
  const Qt::KeyboardModifiers modifiers = key_modifiers;
  if (modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::AltModifier) ||
      modifiers.testFlag(Qt::MetaModifier)) {
    return false;  // AltGr = Ctrl+Alt；不要把它当步进
  }
  if (QApplication::activeModalWidget() != nullptr ||
      QApplication::activePopupWidget() != nullptr) {
    return false;
  }
  if (IsTextInputFocused()) {
    return false;  // spec §3.7：文本框内方向键/Home/End 归文本
  }
  if (is_zoom) {
    if (event->isAutoRepeat()) {
      return true;
    }
    QAction* action = is_zoom_in ? zoom_in_action_ : zoom_out_action_;
    if (action == nullptr || !action->isEnabled()) {
      return false;
    }
    if (is_zoom_in) {
      ZoomTimelineIn();
    } else {
      ZoomTimelineOut();
    }
    return true;
  }
  if (!CanStepFrames()) {
    return false;
  }
  if (event->isAutoRepeat()) {
    // 物理长按期间若连发被焦点/伪 release 停掉，OS 自动重复的 KeyPress 是
    // 恢复连发的可靠信号；只恢复“可恢复”的停止原因，边界/seek 失败不重试。
    const bool recoverable = step_stop_reason_ == QStringLiteral("keyup") ||
                             step_stop_reason_ == QStringLiteral("focus_changed") ||
                             step_stop_reason_ == QStringLiteral("window_deactivate") ||
                             step_stop_reason_ == QStringLiteral("app_inactive");
    const int direction =
        (key == Qt::Key_PageUp || key == Qt::Key_Left) ? -1 : 1;
    if (step_direction_ != 0 && step_repeat_timer_ != nullptr &&
        !step_repeat_timer_->isActive()) {
      step_repeat_timer_->start(70);
    } else if (step_direction_ == 0 && recoverable &&
               IsStepKeyPhysicallyDown(key)) {
      StartStepRepeat(direction);
    }
    return true;  // OS 自动重复不额外走帧；连发由 step_repeat_timer_ 控制
  }
  if (is_nav) {
    const std::int64_t last =
        static_cast<std::int64_t>(session_->index().size()) - 1;
    GoToFrame(key == Qt::Key_Home ? 0 : last);
    return true;
  }
  StartStepRepeat((key == Qt::Key_PageUp || key == Qt::Key_Left) ? -1 : 1);
  return true;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (event != nullptr) {
    if (event->type() == QEvent::WindowDeactivate) {
      StopStepRepeat("window_deactivate");
    } else if (event->type() == QEvent::KeyPress ||
               event->type() == QEvent::KeyRelease) {
      if (HandleGlobalKey(static_cast<QKeyEvent*>(event))) {
        return true;
      }
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::StartStepRepeat(int delta) {
  if (step_repeat_timer_ == nullptr) {
    return;
  }
  step_direction_ = delta;
  step_stop_reason_.clear();
  if (qEnvironmentVariableIsSet("LAPIAN_KEY_LOG")) {
    std::fprintf(stderr,
                 "T8.6.1a step_start dir=%d online=%d frame=%lld last=%lld "
                 "stable=%lld\n",
                 delta, OnlineStepEligible() ? 1 : 0,
                 static_cast<long long>(current_frame_),
                 static_cast<long long>(OnlineLastFrame()),
                 static_cast<long long>(online_stable_frames_));
    std::fflush(stderr);
  }
  // T8.6.1a：在线步进的目标/在途/排队由状态机维护；这里不能用 current_frame_
  // 的同步变化判断“是否走了一帧”，否则第一步还没落地就误判为边界。
  if (OnlineStepEligible()) {
    if (!ApplyOnlineStep(delta)) {
      StopStepRepeat("start_boundary");  // 已在边界（或索引尚不可定位）：不启动连发
      return;
    }
    step_repeat_timer_->start(300);
    return;
  }
  const std::int64_t before = current_frame_;
  // T7.2：后退键先预热最近一窗帧，后续连发可直接命中缓存。
  GoToFrameForStep(before + delta, delta < 0);
  if (current_frame_ == before) {
    StopStepRepeat("start_boundary");  // 已在边界：不启动连发
    return;
  }
  step_repeat_timer_->start(300);
}

void MainWindow::StopStepRepeat(const char* reason) {
  if (qEnvironmentVariableIsSet("LAPIAN_KEY_LOG") && step_direction_ != 0) {
    std::fprintf(stderr,
                 "T8.6.1a step_stop reason=%s dir=%d frame=%lld desired=%lld "
                 "inflight=%d target=%lld seek_running=%d queued=%d active=%d\n",
                 reason != nullptr ? reason : "null", step_direction_,
                 static_cast<long long>(current_frame_),
                 static_cast<long long>(online_step_.desired),
                 online_step_.inflight ? 1 : 0,
                 static_cast<long long>(online_step_.inflight_target),
                 online_seek_running_ ? 1 : 0, online_step_.queued ? 1 : 0,
                 step_repeat_timer_ != nullptr && step_repeat_timer_->isActive()
                     ? 1
                     : 0);
    std::fflush(stderr);
  }
  step_stop_reason_ = QString::fromLatin1(reason != nullptr ? reason : "");
  step_direction_ = 0;
  if (step_repeat_timer_ != nullptr) {
    step_repeat_timer_->stop();
  }
}

bool MainWindow::IsStepKeyPhysicallyDown(int key) const {
  if (step_key_physical_down_for_test_ >= 0) {
    return step_key_physical_down_for_test_ != 0;
  }
#ifdef _WIN32
  UINT vk = 0;
  switch (key) {
    case Qt::Key_PageUp:
      vk = VK_PRIOR;
      break;
    case Qt::Key_PageDown:
      vk = VK_NEXT;
      break;
    case Qt::Key_Left:
      vk = VK_LEFT;
      break;
    case Qt::Key_Right:
      vk = VK_RIGHT;
      break;
    default:
      return false;
  }
  return (::GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
#else
  (void)key;
  return false;
#endif
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
  // QAction 的 Esc 快捷键被禁用（例如文本框聚焦）时，按键会沿父链传到这里；
  // 这里再做一次“编辑框内不退全屏”的兜底。
  if (event != nullptr && event->key() == Qt::Key_Escape && isFullScreen() &&
      !IsTextInputFocused()) {
    ExitFullScreen();
    event->accept();
    return;
  }
  QMainWindow::keyPressEvent(event);
}

void MainWindow::SendKeyPressForTest(int key, int modifiers, bool auto_repeat) {
  QWidget* target = QApplication::focusWidget();
  if (target == nullptr) {
    target = this;
  }
  QKeyEvent press(QEvent::KeyPress, key, Qt::KeyboardModifiers(modifiers),
                  QString(), auto_repeat);
  QApplication::sendEvent(target, &press);
}

void MainWindow::SendKeyReleaseForTest(int key, int modifiers, bool auto_repeat) {
  QWidget* target = QApplication::focusWidget();
  if (target == nullptr) {
    target = this;
  }
  QKeyEvent release(QEvent::KeyRelease, key, Qt::KeyboardModifiers(modifiers),
                    QString(), auto_repeat);
  QApplication::sendEvent(target, &release);
}

void MainWindow::SendKeyForTest(int key, int modifiers, bool auto_repeat) {
  SendKeyPressForTest(key, modifiers, auto_repeat);
  if (!auto_repeat) {
    SendKeyReleaseForTest(key, modifiers);
  }
}

void MainWindow::PressKeyForTest(int key, int modifiers) {
  SendKeyForTest(key, modifiers);
}

void MainWindow::PressKeyDownForTest(int key, int modifiers) {
  SendKeyPressForTest(key, modifiers, false);
}

void MainWindow::PressKeyUpForTest(int key, int modifiers) {
  SendKeyReleaseForTest(key, modifiers);
}

void MainWindow::PressAutoRepeatKeyForTest(int key, int modifiers) {
  SendKeyPressForTest(key, modifiers, true);
}

void MainWindow::PressAutoRepeatReleaseForTest(int key, int modifiers) {
  SendKeyReleaseForTest(key, modifiers, true);
}

void MainWindow::ToggleFullScreenForTest() { ToggleFullScreen(); }

void MainWindow::PressFullScreenKeyForTest() { SendKeyForTest(Qt::Key_F); }

void MainWindow::PressEscapeKeyForTest() { SendKeyForTest(Qt::Key_Escape); }

void MainWindow::PressSpaceKeyForTest() { SendKeyForTest(Qt::Key_Space); }

void MainWindow::FocusVideoForTest() {
  if (gfx_ != nullptr) {
    gfx_->setFocus(Qt::OtherFocusReason);
  }
}

void MainWindow::DoubleClickVideoForTest() {
  if (gfx_ == nullptr) {
    return;
  }
  const QPointF local(gfx_->width() / 2.0, gfx_->height() / 2.0);
  const QPointF global = gfx_->mapToGlobal(local.toPoint());
  QMouseEvent event(QEvent::MouseButtonDblClick, local, global, Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(gfx_, &event);
}

bool MainWindow::IsFullScreenForTest() const { return isFullScreen(); }

bool MainWindow::IsMaximizedForTest() const { return isMaximized(); }

bool MainWindow::IsPlayingForTest() const {
  return playback_ != nullptr && playback_->IsPlaying();
}

QString MainWindow::FullScreenStateForTest() const {
  const QSize gfx_size = GfxWidgetSizeForTest();
  const QSize swap_size = SwapChainSizeForTest();
  const QSize display = SnapshotDisplaySizeForTest();
  QString info_frame = InfoFrameTextForTest();
  info_frame.replace(QLatin1Char(' '), QLatin1Char('_'));
  const double dpr = gfx_ == nullptr ? 0.0 : gfx_->devicePixelRatioF();
  QStringList parts;
  parts << QStringLiteral("fullscreen=%1").arg(isFullScreen() ? 1 : 0);
  parts << QStringLiteral("frame=%1").arg(static_cast<qlonglong>(current_frame_));
  parts << QStringLiteral("playing=%1").arg(IsPlayingForTest() ? 1 : 0);
  parts << QStringLiteral("gfx=%1x%2").arg(gfx_size.width()).arg(gfx_size.height());
  parts << QStringLiteral("swap=%1x%2").arg(swap_size.width()).arg(swap_size.height());
  parts << QStringLiteral("dpr=%1").arg(dpr, 0, 'f', 2);
  parts << QStringLiteral("timeline=%1").arg(TimelineVisibleForTest() ? 1 : 0);
  parts << QStringLiteral("info=%1").arg(InfoBarVisibleForTest() ? 1 : 0);
  parts << QStringLiteral("info_frame=%1").arg(info_frame);
  parts << QStringLiteral("display=%1x%2").arg(display.width()).arg(display.height());
  parts << QStringLiteral("focus=%1").arg(FocusWidgetNameForTest());
  parts << QStringLiteral("toggle_ms=%1").arg(fullscreen_last_toggle_ms_);
  return parts.join(QLatin1Char(' '));
}

QSize MainWindow::GfxWidgetSizeForTest() const {
  return gfx_ == nullptr ? QSize() : gfx_->size();
}

QSize MainWindow::SwapChainSizeForTest() const {
  return gfx_ == nullptr ? QSize() : gfx_->swapChainSize();
}

QSize MainWindow::TimelineSizeForTest() const {
  return timeline_ == nullptr ? QSize() : timeline_->size();
}

qint64 MainWindow::TimelineFrameAtForTest(int x, int y) const {
  if (timeline_ == nullptr) {
    return -1;
  }
  return static_cast<qint64>(timeline_->FrameAtPosition(QPointF(x, y)));
}

void MainWindow::ClickTimelineForTest(int x, int y) {
  if (timeline_ == nullptr) {
    return;
  }
  const QPointF local(x, y);
  const QPointF global = timeline_->mapToGlobal(QPoint(x, y));
  QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(timeline_, &press);
  QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton,
                      Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(timeline_, &release);
}

QString MainWindow::InfoFrameTextForTest() const {
  if (info_bar_ == nullptr) {
    return {};
  }
  const QLabel* label = info_bar_->findChild<QLabel*>(QStringLiteral("infoFrame"));
  return label == nullptr ? QString() : label->text();
}

QString MainWindow::FocusWidgetNameForTest() const {
  QWidget* focus = QApplication::focusWidget();
  if (focus == nullptr) {
    return QStringLiteral("none");
  }
  return QString::fromLatin1(focus->metaObject()->className());
}

bool MainWindow::IsTextInputFocusedForTest() const { return IsTextInputFocused(); }

bool MainWindow::TimelineVisibleForTest() const {
  return timeline_ != nullptr && timeline_->isVisible();
}

bool MainWindow::InfoBarVisibleForTest() const {
  return info_bar_ != nullptr && info_bar_->isVisible();
}

qint64 MainWindow::CurrentFrameForTest() const {
  return static_cast<qint64>(current_frame_);
}

qint64 MainWindow::LastFullScreenToggleMsForTest() const {
  return fullscreen_last_toggle_ms_;
}

QString MainWindow::KeyStateForTest() const {
  QStringList parts;
  parts << QStringLiteral("frame=%1").arg(static_cast<qlonglong>(current_frame_));
  parts << QStringLiteral("last=%1").arg(LastFrameIndexForTest());
  parts << QStringLiteral("playing=%1").arg(IsPlayingForTest() ? 1 : 0);
  parts << QStringLiteral("fullscreen=%1").arg(isFullScreen() ? 1 : 0);
  parts << QStringLiteral("speed=%1").arg(rate_);
  parts << QStringLiteral("muted=%1").arg(muted_ ? 1 : 0);
  parts << QStringLiteral("volume=%1").arg(volume_);
  parts << QStringLiteral("loop=%1").arg(loop_.enabled() ? 1 : 0);
  parts << QStringLiteral("a=%1").arg(static_cast<qlonglong>(loop_.in()));
  parts << QStringLiteral("b=%1").arg(static_cast<qlonglong>(loop_.out()));
  parts << QStringLiteral("waveform=%1").arg(waveform_visible_ ? 1 : 0);
  parts << QStringLiteral("zoom_in_on=%1").arg(
      zoom_in_action_ != nullptr && zoom_in_action_->isEnabled() ? 1 : 0);
  parts << QStringLiteral("zoom_out_on=%1").arg(
      zoom_out_action_ != nullptr && zoom_out_action_->isEnabled() ? 1 : 0);
  parts << QStringLiteral("marks=%1").arg(MarkCountForTest());
  parts << QStringLiteral("snapshots=%1").arg(snapshot_saved_paths_.size());
  parts << QStringLiteral("focus=%1").arg(FocusWidgetNameForTest());
  parts << QStringLiteral("text_focus=%1").arg(IsTextInputFocused() ? 1 : 0);
  parts << QStringLiteral("repeat=%1").arg(StepRepeatActiveForTest() ? 1 : 0);
  return parts.join(QLatin1Char(' '));
}

QString MainWindow::ShortcutMapForTest() const {
  QStringList parts;
  auto add = [&parts](const char* name, QAction* action) {
    if (action != nullptr) {
      parts << QStringLiteral("%1=%2")
                   .arg(QString::fromLatin1(name), action->shortcut().toString());
    }
  };
  add("open", open_action_);
  add("online", online_action_);
  add("play", play_action_);
  add("goto", goto_action_);
  // T6.2：'+'/'-' 由窗口级事件过滤器处理，不经过 QAction 快捷键。
  parts << QStringLiteral("zoom_in=+");
  parts << QStringLiteral("zoom_out=-");
  add("wave", waveform_action_);
  add("slower", slower_action_);
  add("faster", faster_action_);
  add("in", loop_in_action_);
  add("out", loop_out_action_);
  add("loop", loop_toggle_action_);
  add("clear", loop_clear_action_);
  add("mark", mark_action_);
  add("snap", snapshot_action_);
  add("full", fullscreen_action_);
  add("esc", exit_fullscreen_action_);
  add("mute", mute_action_);
  return parts.join(QLatin1Char(' '));
}

bool MainWindow::FocusMarkTreeForTest() {
  if (mark_panel_ == nullptr) {
    return false;
  }
  QWidget* tree = mark_panel_->findChild<QWidget*>(QStringLiteral("markTree"));
  if (tree == nullptr || !tree->isVisible()) {
    return false;
  }
  tree->setFocus(Qt::OtherFocusReason);
  return tree->hasFocus();
}

bool MainWindow::FocusVolumeSliderForTest() {
  if (volume_slider_ == nullptr || !volume_slider_->isVisible()) {
    return false;
  }
  volume_slider_->setFocus(Qt::OtherFocusReason);
  return volume_slider_->hasFocus();
}

bool MainWindow::StepRepeatActiveForTest() const {
  return step_direction_ != 0 && step_repeat_timer_ != nullptr &&
         step_repeat_timer_->isActive();
}

double MainWindow::SpeedForTest() const { return rate_; }

bool MainWindow::WaveformVisibleForTest() const { return waveform_visible_; }

bool MainWindow::MutedForTest() const { return muted_; }

int MainWindow::VolumeForTest() const { return volume_; }

qint64 MainWindow::LastFrameIndexForTest() const {
  if (!CanStepFrames()) {
    return -1;
  }
  return static_cast<qint64>(session_->index().size()) - 1;
}

double MainWindow::TimelinePixelsPerFrameForTest() const {
  return timeline_ == nullptr ? 0.0 : timeline_->pixels_per_frame();
}

void MainWindow::SetVolumeForTest(int volume) {
  const int clamped = std::clamp(volume, 0, 100);
  if (volume_slider_ != nullptr) {
    volume_slider_->setValue(clamped);
    return;
  }
  volume_ = clamped;
  ScheduleSettingsSave();
}

void MainWindow::SetWaveformVisibleForTest(bool visible) {
  if (waveform_action_ != nullptr) {
    waveform_action_->setChecked(visible);
    return;
  }
  waveform_visible_ = visible;
  if (timeline_ != nullptr) {
    timeline_->SetWaveformVisible(visible);
  }
  ScheduleSettingsSave();
}

bool MainWindow::FlushSettingsForTest() { return FlushSettings(); }

void MainWindow::ReloadSettingsForTest() { LoadSettingsIntoUi(); }

QString MainWindow::SettingsPathForTest() const {
  return settings_store_ == nullptr ? QString() : settings_store_->file_path();
}

QString MainWindow::SettingsStateForTest() const {
  QString text = QStringLiteral("path=%1 status=%2 volume=%3 waveform=%4 dirty=%5");
  text = text.arg(QDir::toNativeSeparators(SettingsPathForTest()))
             .arg(settings_load_status_)
             .arg(volume_)
             .arg(waveform_visible_ ? 1 : 0)
             .arg(settings_dirty_ ? 1 : 0);
  return text;
}

QString MainWindow::SettingsLoadStatusForTest() const { return settings_load_status_; }

QString MainWindow::DefaultSettingsRootForTest() const {
  return lapian::core::SettingsStore::DefaultRootDir();
}

double MainWindow::LastSettingsSaveMsForTest() const {
  return settings_last_save_ms_;
}

double MainWindow::LastSettingsLoadMsForTest() const {
  return settings_last_load_ms_;
}

void MainWindow::StepBackward() {
  if (session_ != nullptr && session_->IsOpen()) {
    CancelPendingMarkJump();
    GoToFrameForStep(current_frame_ - 1, true);
  }
}

void MainWindow::StepForward() {
  if (session_ != nullptr && session_->IsOpen()) {
    CancelPendingMarkJump();
    GoToFrameForStep(current_frame_ + 1, false);
  }
}

void MainWindow::GoToFrame(std::int64_t frame) {
  // T7.9：精确 seek 时保留目标前一帧的引用（不回拷），使“seek 后紧接 PgUp”
  // 直接呈现；不读 FrameCache，避免改变 P9 冷随机跳转的语义。连续后退仍由
  // GoToFrameForStep 的 kBackwardWindow 预热。
  GoToFrameInternal(frame, lapian::media::FrameCacheMode::kSeekHandoff);
}

void MainWindow::GoToFrameForStep(std::int64_t frame, bool backward) {
  // T8.6.1a：在线步进统一走异步状态机；frame 参数在在线分支不使用（目标由
  // 已请求帧/排队帧推导），保留参数以兼容本地同步路径与既有探针。
  if (OnlineStepEligible()) {
    ApplyOnlineStep(backward ? -1 : 1);
    return;
  }
  // 后退步进预热一窗帧；前进步进顺序解码本身不命中缓存，写缓存只会增加拷贝。
  GoToFrameInternal(frame, backward ? lapian::media::FrameCacheMode::kBackwardWindow
                                    : lapian::media::FrameCacheMode::kNone);
}

void MainWindow::GoToFrameInternal(std::int64_t frame,
                                   lapian::media::FrameCacheMode mode) {
  if (session_ == nullptr || !session_->IsOpen()) {
    return;
  }
  if (online_ready_ && !online_cache_hit_) {
    // T8.6.1a：这是非步进导航（时间轴/Home/End/标记/输入帧号），清掉步进状态，
    // 避免在途步进完成后再补一步造成方向错乱。
    ResetOnlineStepState();
    const std::int64_t last_frame = OnlineLastFrame();
    if (last_frame < 0) {
      statusBar()->showMessage(
          QStringLiteral("在线索引扫描中，目标帧尚不可定位"), 3000);
      return;
    }
    const std::int64_t target = lapian::core::ClampFrame(frame, last_frame);
    if (playback_) {
      playback_->Pause();
    }
    const bool exact =
        session_->index().IsReady() && !session_->index().empty() &&
        target < static_cast<std::int64_t>(session_->index().size());
    const bool cached = exact && OnlineFrameCached(target);
    if (cached) {
      statusBar()->showMessage(
          QStringLiteral("第 %1 帧（已缓存）").arg(target), 1200);
    } else {
      ShowOnlineWaitPrompt(target);
    }
    // 在线 seek 不复用 v1 的 kBackwardWindow：那会写入 FrameCache 并持有
    // 多帧硬解表面；在线 reader/缓存由 OnlineSession 管理，统一走 handoff。
    RequestOnlineSeek(target, lapian::media::FrameCacheMode::kSeekHandoff,
                      exact);
    return;
  }
  if (!session_->index().IsReady() || session_->index().empty()) {
    return;
  }
  const std::int64_t last_frame =
      static_cast<std::int64_t>(session_->index().size()) - 1;
  const std::int64_t target = lapian::core::ClampFrame(frame, last_frame);
  playback_->Pause();

  lapian::media::DecodedFrameInfo info;
  std::string error;
  if (!session_->SeekToFrame(target, &info, &error, mode)) {
    // 拖动时 UI 已先显示目标帧；失败必须退回实际呈现帧，不能把目标当结果。
    if (timeline_ != nullptr) {
      timeline_->SetCurrentFrame(current_frame_);
    }
    if (info_bar_ != nullptr) {
      info_bar_->SetFrame(static_cast<int>(current_frame_));
    }
    statusBar()->showMessage(QStringLiteral("跳帧失败：") + QString::fromStdString(error), 5000);
    last_navigation_error_ = QString::fromStdString(error);
    return;
  }
  last_navigation_error_.clear();

  gfx_->SetVideoFrame(info.av_frame, session_->info().rotation.counterclockwise_degrees);
  RememberDisplayedFrame(info.av_frame, session_->info().rotation.counterclockwise_degrees,
                         target);
  info_bar_->SetFrame(static_cast<int>(target));
  timeline_->SetCurrentFrame(target);
  playback_->SetPosition(target, info.pts_us);
  current_frame_ = target;
  current_pts_us_ = info.pts_us;
  // T7.11.1：后退窗口的硬解帧先以引用形式留在会话里；画面已经呈现，回拷放到
  // 事件循环空闲时做，避免跨 GOP 第一步把 24 帧 GPU→CPU 拷贝算进 UI 延迟。
  if (mode == lapian::media::FrameCacheMode::kBackwardWindow &&
      session_->HasPendingCacheWindow() && cache_fill_timer_ != nullptr) {
    cache_fill_timer_->start();
  }
  if (qEnvironmentVariableIsSet("LAPIAN_CACHE_LOG")) {
    const lapian::media::FrameCacheStats stats = session_->frame_cache().stats();
    std::fprintf(stderr,
                 "T7.2 cache frame=%lld mode=%d count=%zu bytes=%zu max_bytes=%zu "
                 "hits=%llu misses=%llu puts=%llu evictions=%llu\n",
                 static_cast<long long>(target), static_cast<int>(mode), stats.entries,
                 stats.bytes, stats.max_bytes,
                 static_cast<unsigned long long>(stats.hits),
                 static_cast<unsigned long long>(stats.misses),
                 static_cast<unsigned long long>(stats.puts),
                 static_cast<unsigned long long>(stats.evictions));
    std::fflush(stderr);
  }
  statusBar()->showMessage(QStringLiteral("第 %1 帧").arg(target), 2000);
}

void MainWindow::FillFrameCacheStep() {
  if (session_ == nullptr || !session_->IsOpen() ||
      !session_->HasPendingCacheWindow()) {
    if (cache_fill_timer_ != nullptr) {
      cache_fill_timer_->stop();
    }
    return;
  }
  // 4 帧 ≈ 12ms，留出事件循环给按键/绘制；24 帧窗口约 6 个 tick 填完。
  const bool more = session_->CopyPendingCacheFrames(4);
  if (!more && cache_fill_timer_ != nullptr) {
    cache_fill_timer_->stop();
  }
}

// T7.4：拖动/跳转/步进探针只转发到产品入口，不改变任何语义。
void MainWindow::ScrubStartForTest(qint64 frame) { OnScrubStarted(frame); }

void MainWindow::ScrubTargetForTest(qint64 frame) { OnScrubTargetChanged(frame); }

void MainWindow::ScrubFinishForTest(qint64 frame) { OnScrubFinished(frame); }

void MainWindow::GoToFrameForTest(qint64 frame) { GoToFrame(frame); }

qint64 MainWindow::FrameDistanceFromKeyframeForTest(qint64 frame) const {
  if (session_ == nullptr || !session_->index().IsReady() ||
      session_->index().empty() || frame < 0 ||
      frame >= static_cast<qint64>(session_->index().size())) {
    return -1;
  }
  const lapian::media::FrameIndex& index = session_->index();
  const qint64 keyframe = index.KeyframeAtOrBefore(index.PtsOfFrame(frame));
  return keyframe < 0 ? frame : frame - keyframe;
}

bool MainWindow::HardwareDecodeForTest() const {
  return session_ != nullptr && session_->IsOpen() && session_->first_frame().hardware;
}

int MainWindow::PresentedFrameCountForTest() const { return presented_frame_count_; }

double MainWindow::FrameRateForTest() const {
  if (session_ == nullptr || !session_->IsOpen()) {
    return 0.0;
  }
  return session_->info().video.frame_rate.ToDouble();
}

void MainWindow::OnPlaybackStateChanged(bool playing) {
  play_action_->setText(playing ? QStringLiteral("暂停") : QStringLiteral("播放"));
  const bool frozen = playing && playback_ != nullptr && playback_->frozen();
  if (frozen) {
    statusBar()->showMessage(
        QStringLiteral("定帧：A=B=%1（画面保持、音频静音）")
            .arg(static_cast<qlonglong>(current_frame_)),
        3000);
  } else {
    statusBar()->showMessage(playing ? QStringLiteral("播放中") : QStringLiteral("已暂停"),
                             2000);
  }
  if (audio_) {
    if (playing && !frozen) {
      audio_->PlayFrom(playback_->CurrentMediaSeconds());
    } else {
      audio_->Pause();
    }
  }
}

void MainWindow::OnLoopJumped(qint64 frame, double media_seconds) {
  if (audio_) {
    audio_->PlayFrom(media_seconds);
  }
  statusBar()->showMessage(QStringLiteral("循环：回到第 %1 帧").arg(frame), 1200);
  if (qEnvironmentVariableIsSet("LAPIAN_LOOP_LOG")) {
    std::fprintf(stderr, "T4.2 loop_audio_restart frame=%lld target=%.4f\n",
                 static_cast<long long>(frame), media_seconds);
    std::fflush(stderr);
  }
}

void MainWindow::OnPlaybackEnded() {
  play_action_->setText(QStringLiteral("播放"));
  play_action_->setEnabled(false);
  if (audio_) {
    audio_->Pause();
  }
  statusBar()->showMessage(QStringLiteral("播放结束"), 5000);
}

void MainWindow::ShowOpenError(const QString& message) {
  qWarning() << "打开失败:" << message;
  last_open_error_ = message;
  if (info_bar_ != nullptr) {
    info_bar_->SetError(message);
  }
  statusBar()->showMessage(message);
  QMessageBox::warning(this, QStringLiteral("打开失败"), message);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  const bool close_log = qEnvironmentVariableIsSet("LAPIAN_ONLINE_LOG");
  auto close_mark = [close_log](const char* step) {
    if (close_log) {
      std::fprintf(stderr, "T8.7 close step=%s\n", step);
      std::fflush(stderr);
    }
  };
  close_mark("start");
  // T8.3.1：关窗后不再触发启动/定期缓存清理。
  if (cache_maintenance_timer_ != nullptr) {
    cache_maintenance_timer_->stop();
  }
  // T8.1：在线解析中关窗必须取消并 join 工作线程，不能把请求/线程留给析构。
  if (online_dialog_ != nullptr) {
    online_dialog_->Shutdown();
  }
  close_mark("dialog");
  // T8.6：关窗 ≤1s：先取消下载/唤醒 reader，再按“音频/波形/索引/seek →
  // MediaSession → MarkComplete”的顺序收尾；reader 先停，rename 不 busy。
  if (online_mode_ || online_) {
    ShutdownOnlineSession(true);
  }
  close_mark("online");
  // T6.3：先把音量/波形落盘；它独立于截图/标记的等待链，失败也不阻止关窗。
  FlushSettings();
  close_mark("settings");
  CancelPendingMarkJump();
  // 关窗前把已按下的截图任务写完，避免用户以为存了却丢文件。
  if (snapshots_ != nullptr) {
    snapshots_->Drain();
  }
  close_mark("snapshots");
  if (!FlushMarksOnClose()) {
    close_mark("marks_failed");
    event->ignore();
    return;
  }
  close_mark("marks");
  QMainWindow::closeEvent(event);
  close_mark("end");
}

void MainWindow::AddMarkAtCurrentFrame() {
  if (IsTextInputFocused()) {
    return;
  }
  ApplyAddMark();
}

void MainWindow::AddMarkForTest() { ApplyAddMark(); }

void MainWindow::ApplyAddMark() {
  if (session_ == nullptr || !session_->IsOpen() || marks_ == nullptr) {
    return;
  }
  QElapsedTimer add_timer;
  add_timer.start();
  // 先提交侧栏里未保存的标签/备注，避免增删标记时被列表刷新覆盖。
  FlushMarkEditor();
  const lapian::media::FrameIndex& index = session_->index();
  std::int64_t pts_us = current_pts_us_;
  if (index.IsReady() && !index.empty()) {
    if (const lapian::media::FrameIndexEntry* entry = index.Entry(current_frame_)) {
      pts_us = entry->pts_us;
    }
  }
  if (pts_us <= 0 && session_->first_frame().av_frame != nullptr) {
    pts_us = session_->first_frame().pts_us;
  }
  if (pts_us <= 0 && !online_ready_) {
    statusBar()->showMessage(QStringLiteral("当前帧没有 PTS，暂时不能添加标记"), 3000);
    return;
  }
  std::string id;
  if (!marks_->AddMark(current_frame_, pts_us, std::string(), std::string(), &id)) {
    statusBar()->showMessage(QStringLiteral("添加标记失败"), 3000);
    return;
  }
  marks_dirty_ = true;
  const bool saved = SaveMarks(true);
  RefreshMarksUi(QString::fromStdString(id));
  statusBar()->showMessage(
      QStringLiteral("已添加标记：帧 %1 · %2 · %3")
          .arg(static_cast<qlonglong>(current_frame_))
          .arg(saved ? QStringLiteral("已保存") : QStringLiteral("未保存"))
          .arg(marks_->online()
                   ? QStringLiteral("在线记录")
                   : (marks_->using_fallback() ? QStringLiteral("%APPDATA% 回退")
                                               : QStringLiteral("sidecar"))),
      3000);
  if (qEnvironmentVariableIsSet("LAPIAN_MARK_LOG")) {
    std::fprintf(stderr, "T5.2 mark_add frame=%lld count=%d total_ms=%.2f\n",
                 static_cast<long long>(current_frame_), MarkCountForTest(),
                 static_cast<double>(add_timer.nsecsElapsed()) / 1.0e6);
    std::fflush(stderr);
  }
}

void MainWindow::ApplyMarkTextEdited(const QString& id, const QString& label,
                                     const QString& note) {
  if (marks_ == nullptr || mark_panel_ == nullptr) {
    return;
  }
  const std::string id_utf8 = id.toStdString();
  const lapian::core::Mark* mark = marks_->FindMark(id_utf8);
  if (mark == nullptr) {
    return;
  }
  const std::string label_utf8 = label.toStdString();
  const std::string note_utf8 = note.toStdString();
  if (mark->label == label_utf8 && mark->note == note_utf8) {
    return;  // 失焦/切换选中时内容没变，不产生一次多余写盘。
  }
  const std::int64_t frame = mark->frame;
  if (!marks_->SetMarkText(id_utf8, label_utf8, note_utf8)) {
    return;
  }
  marks_dirty_ = true;
  // 只更新这一行的标签/备注摘要，不重建列表，选中与滚动位置保持不变。
  mark_panel_->UpdateMarkText(id, label, note);

  const bool timing = qEnvironmentVariableIsSet("LAPIAN_MARK_LOG");
  QElapsedTimer timer;
  if (timing) {
    timer.start();
  }
  const bool saved = SaveMarks(true);
  if (timing) {
    const double ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
    if (note_save_samples_.size() < 1000) {
      note_save_samples_.push_back(ms);
    }
    std::fprintf(stderr,
                 "T5.3 note_commit frame=%lld label=%s note=%s saved=%d save_ms=%.2f\n",
                 static_cast<long long>(frame), EscapeForLog(label).toUtf8().constData(),
                 EscapeForLog(note).toUtf8().constData(), saved ? 1 : 0, ms);
    std::fflush(stderr);
  }
  statusBar()->showMessage(
      saved ? QStringLiteral("标记 %1 已保存").arg(static_cast<qlonglong>(frame))
            : QStringLiteral("标记 %1 未保存").arg(static_cast<qlonglong>(frame)),
      1500);
}

void MainWindow::FlushMarkEditor() {
  if (mark_panel_ != nullptr) {
    mark_panel_->FlushEditor();
  }
}

void MainWindow::ActivateMark(const QString& id, std::int64_t frame) {
  if (session_ == nullptr || !session_->IsOpen() || marks_ == nullptr ||
      timeline_ == nullptr || info_bar_ == nullptr || mark_panel_ == nullptr) {
    return;
  }
  const lapian::media::FrameIndex& index = session_->index();
  if (!index.IsReady() || index.empty()) {
    return;
  }
  const std::int64_t last_frame = static_cast<std::int64_t>(index.size()) - 1;
  const std::int64_t target = lapian::core::ClampFrame(frame, last_frame);

  CancelPendingMarkJump();
  const std::uint64_t generation = navigation_generation_;
  QElapsedTimer timer;
  timer.start();

  if (preview_timer_ != nullptr && preview_timer_->isActive()) {
    EndScrubPreview();
  }
  if (playback_ != nullptr) {
    playback_->Pause();
  }
  mark_panel_->SelectMark(id.toStdString());
  timeline_->SetCurrentFrame(target);
  info_bar_->SetFrame(static_cast<int>(target));
  // F6-A8：选中/播放头/帧号先可见，再让解码在下一个事件循环执行。
  mark_panel_->repaint();
  timeline_->repaint();
  info_bar_->repaint();
  const double ui_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  mark_ui_samples_.push_back(ui_ms);
  if (qEnvironmentVariableIsSet("LAPIAN_MARK_LOG")) {
    std::fprintf(stderr, "T5.2 mark_ui_ms=%.2f frame=%lld\n", ui_ms,
                 static_cast<long long>(target));
    std::fflush(stderr);
  }
  statusBar()->showMessage(
      QStringLiteral("标记：第 %1 帧（跳转中）").arg(static_cast<qlonglong>(target)),
      2000);

  QTimer::singleShot(0, this, [this, target, generation, timer]() {
    if (generation != navigation_generation_ || session_ == nullptr ||
        !session_->IsOpen()) {
      return;
    }
    GoToFrame(target);
    const double exact_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
    const bool ok = current_frame_ == target && timeline_ != nullptr &&
                    timeline_->current_frame() == target;
    if (ok) {
      mark_exact_samples_.push_back(exact_ms);
    }
    if (qEnvironmentVariableIsSet("LAPIAN_MARK_LOG")) {
      std::fprintf(stderr, "T5.2 mark_exact_ms=%.2f frame=%lld ok=%d\n", exact_ms,
                   static_cast<long long>(target), ok ? 1 : 0);
      std::fflush(stderr);
    }
  });
}

void MainWindow::ApplyDeleteMark(const QString& id, bool confirm) {
  if (marks_ == nullptr) {
    return;
  }
  // 删除前先落盘编辑框内容；若删的就是正在编辑的标记，也不会丢别的修改。
  FlushMarkEditor();
  const std::string id_utf8 = id.toStdString();
  const lapian::core::Mark* mark = marks_->FindMark(id_utf8);
  if (mark == nullptr) {
    return;
  }
  const std::int64_t frame = mark->frame;
  const QString label = QString::fromStdString(mark->label);
  if (confirm) {
    const QString text =
        QStringLiteral("删除标记“%1”（帧 %2）？")
            .arg(label.isEmpty() ? QStringLiteral("帧 %1").arg(
                                       static_cast<qlonglong>(frame))
                                 : label)
            .arg(static_cast<qlonglong>(frame));
    const QMessageBox::StandardButton answer =
        QMessageBox::question(this, QStringLiteral("删除标记"), text,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      return;
    }
  }
  if (!marks_->RemoveMark(id_utf8)) {
    return;
  }
  marks_dirty_ = true;
  const bool saved = SaveMarks(true);
  RefreshMarksUi();
  mark_panel_->SelectNearFrame(frame);
  statusBar()->showMessage(
      QStringLiteral("已删除标记（帧 %1）· %2")
          .arg(static_cast<qlonglong>(frame))
          .arg(saved ? QStringLiteral("已保存") : QStringLiteral("未保存")),
      2500);
}

void MainWindow::DeleteMarkForTest(int index) {
  if (marks_ == nullptr || index < 0 ||
      index >= static_cast<int>(marks_->marks().size())) {
    return;
  }
  const QString id = QString::fromStdString(marks_->marks()[index].id);
  ApplyDeleteMark(id, false);
}

void MainWindow::ActivateMarkForTest(int index) {
  if (marks_ == nullptr || index < 0 ||
      index >= static_cast<int>(marks_->marks().size())) {
    return;
  }
  const lapian::core::Mark& mark = marks_->marks()[index];
  ActivateMark(QString::fromStdString(mark.id), mark.frame);
}

bool MainWindow::SaveMarks(bool show_errors) {
  if (marks_ == nullptr || !marks_dirty_) {
    return true;
  }
  QElapsedTimer save_timer;
  save_timer.start();
  const lapian::core::SaveResult result = marks_->Save();
  if (qEnvironmentVariableIsSet("LAPIAN_MARK_LOG")) {
    std::fprintf(stderr, "T8.7 mark_save ms=%.2f ok=%d online=%d path=%s\n",
                 static_cast<double>(save_timer.nsecsElapsed()) / 1.0e6,
                 result.ok ? 1 : 0, marks_->online() ? 1 : 0,
                 result.path.c_str());
    std::fflush(stderr);
  }
  if (result.ok) {
    marks_dirty_ = false;
    UpdateMarkStorageUi();
    return true;
  }
  UpdateMarkStorageUi();
  if (show_errors) {
    ShowMarkSaveFailure(result);
  }
  return false;
}

void MainWindow::RefreshMarksUi(const QString& select_id) {
  if (marks_ == nullptr || mark_panel_ == nullptr) {
    return;
  }
  mark_panel_->SetMarks(marks_->marks());
  UpdateMarkStorageUi();
  std::vector<std::int64_t> frames;
  frames.reserve(marks_->marks().size());
  for (const lapian::core::Mark& mark : marks_->marks()) {
    frames.push_back(mark.frame);
  }
  if (timeline_ != nullptr) {
    timeline_->SetMarks(std::move(frames));
  }
  if (!select_id.isEmpty()) {
    mark_panel_->SelectMark(select_id.toStdString());
  }
}

void MainWindow::UpdateMarkStorageUi() {
  if (marks_ == nullptr || mark_panel_ == nullptr) {
    return;
  }
  if (marks_ != nullptr && marks_->online()) {
    mark_panel_->SetStorage(QString::fromStdString(marks_->storage_path()), false);
    mark_panel_->SetWarnings(marks_->warnings());
    return;
  }
  if (online_ready_) {
    mark_panel_->SetStorage(QStringLiteral("在线会话内（记录未初始化）"), false);
    mark_panel_->SetWarnings(marks_->warnings());
    return;
  }
  mark_panel_->SetStorage(QString::fromStdString(marks_->storage_path()),
                          marks_->using_fallback());
  mark_panel_->SetWarnings(marks_->warnings());
}

bool MainWindow::FlushMarksBeforeSwitch() {
  // 编辑框里的修改可能还没进入 marks_dirty_；必须先提交再判断。
  FlushMarkEditor();
  if (marks_ == nullptr || !marks_dirty_) {
    return true;
  }
  const lapian::core::SaveResult result = marks_->Save();
  UpdateMarkStorageUi();
  if (result.ok) {
    marks_dirty_ = false;
    return true;
  }
  const QString detail = result.error.empty()
                             ? QStringLiteral("未知错误")
                             : QString::fromStdString(result.error);
  statusBar()->showMessage(QStringLiteral("标记保存失败：") + detail, 8000);
  const QMessageBox::StandardButton answer = QMessageBox::warning(
      this, QStringLiteral("标记保存失败"),
      QStringLiteral("旧文件的标记未能保存：\n%1\n\n仍要打开新文件吗？未保存的修改会丢失。")
          .arg(detail),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  return answer == QMessageBox::Yes;
}

bool MainWindow::FlushMarksOnClose() {
  // F8-A2：编辑备注后直接关窗也必须落盘；commit 走与失焦/回车相同的信号路径。
  FlushMarkEditor();
  if (marks_ == nullptr || !marks_dirty_) {
    return true;
  }
  const lapian::core::SaveResult result = marks_->Save();
  UpdateMarkStorageUi();
  if (result.ok) {
    marks_dirty_ = false;
    return true;
  }
  const QString detail = result.error.empty()
                             ? QStringLiteral("未知错误")
                             : QString::fromStdString(result.error);
  statusBar()->showMessage(QStringLiteral("标记保存失败：") + detail, 8000);
  const QMessageBox::StandardButton answer = QMessageBox::warning(
      this, QStringLiteral("标记未保存"),
      QStringLiteral("标记保存失败：\n%1\n\n仍要退出吗？未保存的修改会丢失。")
          .arg(detail),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  return answer == QMessageBox::Yes;
}

void MainWindow::ShowMarkSaveFailure(const lapian::core::SaveResult& result) {
  const QString detail = result.error.empty()
                             ? QStringLiteral("未知错误")
                             : QString::fromStdString(result.error);
  UpdateMarkStorageUi();
  statusBar()->showMessage(QStringLiteral("标记保存失败：") + detail, 8000);
  QMessageBox::warning(
      this, QStringLiteral("标记保存失败"),
      detail + QStringLiteral(
                   "\n\n标记仍在本次会话中；请检查视频目录或 %APPDATA% 的写入权限。"));
}

void MainWindow::ShowMarkWarnings() {
  if (marks_ == nullptr || mark_panel_ == nullptr || marks_->warnings().empty()) {
    return;
  }
  const std::vector<lapian::core::MarkWarning>& warnings = marks_->warnings();
  mark_panel_->SetWarnings(warnings);
  const QString first = QString::fromStdString(warnings.front().message);
  statusBar()->showMessage(
      warnings.size() == 1
          ? first
          : QStringLiteral("%1（另有 %2 条，见右侧标记栏）")
                .arg(first)
                .arg(warnings.size() - 1),
      9000);

  bool blocking = false;
  QStringList lines;
  for (const lapian::core::MarkWarning& warning : warnings) {
    QString line = QString::fromStdString(warning.message);
    if (!warning.path.empty()) {
      line += QStringLiteral("\n  ") +
              QDir::toNativeSeparators(QString::fromStdString(warning.path));
    }
    lines.push_back(line);
    if (warning.kind == lapian::core::MarkWarningKind::kUnsupportedSchema ||
        warning.kind == lapian::core::MarkWarningKind::kReadFailed) {
      blocking = true;
    }
  }
  if (blocking && (marks_ == nullptr || !marks_->online())) {
    QMessageBox::warning(this, QStringLiteral("标记文件提示"),
                         lines.join(QStringLiteral("\n\n")));
  }
}

void MainWindow::CancelPendingMarkJump() { ++navigation_generation_; }

QString MainWindow::MarkStateForTest() const {
  const int count =
      marks_ == nullptr ? 0 : static_cast<int>(marks_->marks().size());
  const int warnings =
      marks_ == nullptr ? 0 : static_cast<int>(marks_->warnings().size());
  const QString selected =
      mark_panel_ == nullptr ? QString() : mark_panel_->SelectedMarkId();
  return QStringLiteral(
             "frame=%1 count=%2 warnings=%3 fallback=%4 selected=%5 timeline=%6 "
             "playing=%7")
      .arg(static_cast<qlonglong>(current_frame_))
      .arg(count)
      .arg(warnings)
      .arg(marks_ != nullptr && marks_->using_fallback() ? 1 : 0)
      .arg(selected.isEmpty() ? QStringLiteral("-") : selected.left(8))
      .arg(timeline_ == nullptr ? -1 : static_cast<qlonglong>(timeline_->current_frame()))
      .arg(playback_ != nullptr && playback_->IsPlaying() ? 1 : 0);
}

int MainWindow::MarkCountForTest() const {
  return marks_ == nullptr ? 0 : static_cast<int>(marks_->marks().size());
}

std::int64_t MainWindow::MarkFrameAtForTest(int index) const {
  if (marks_ == nullptr || index < 0 ||
      index >= static_cast<int>(marks_->marks().size())) {
    return -1;
  }
  return marks_->marks()[index].frame;
}

void MainWindow::ResetMarkPerfSamplesForTest() {
  mark_ui_samples_.clear();
  mark_exact_samples_.clear();
}

QString MainWindow::MarkStoragePathForTest() const {
  return marks_ == nullptr ? QString() : QString::fromStdString(marks_->storage_path());
}

bool MainWindow::MarkUsingFallbackForTest() const {
  return marks_ != nullptr && marks_->using_fallback();
}

int MainWindow::MarkWarningCountForTest() const {
  return marks_ == nullptr ? 0 : static_cast<int>(marks_->warnings().size());
}

QString MainWindow::MarkPerfSummaryForTest() const {
  auto summarize = [](std::vector<double> samples, const QString& prefix) {
    if (samples.empty()) {
      return QStringLiteral("%1_n=0").arg(prefix);
    }
    std::sort(samples.begin(), samples.end());
    const double p50 = samples[samples.size() / 2];
    const std::size_t p95_index =
        std::min(samples.size() - 1,
                 static_cast<std::size_t>(static_cast<double>(samples.size()) * 0.95));
    const double p95 = samples[p95_index];
    const double max = samples.back();
    return QStringLiteral("%1_n=%2 %1_p50=%3 %1_p95=%4 %1_max=%5")
        .arg(prefix)
        .arg(samples.size())
        .arg(p50, 0, 'f', 2)
        .arg(p95, 0, 'f', 2)
        .arg(max, 0, 'f', 2);
  };
  return summarize(mark_ui_samples_, QStringLiteral("ui")) + QStringLiteral(" ") +
         summarize(mark_exact_samples_, QStringLiteral("exact"));
}

void MainWindow::SetMarkNoteForTest(int index, const QString& label,
                                    const QString& note) {
  if (marks_ == nullptr || mark_panel_ == nullptr || index < 0 ||
      index >= static_cast<int>(marks_->marks().size())) {
    return;
  }
  const lapian::core::Mark& mark = marks_->marks()[index];
  // 与用户点击行 → 在编辑框键入等价；这里刻意不提交，留给关窗/Commit 触发。
  mark_panel_->SelectMark(mark.id);
  mark_panel_->SetEditorTextForTest(label, note);
}

void MainWindow::CommitNoteForTest() { FlushMarkEditor(); }

QString MainWindow::MarkTextForTest(int index) const {
  if (marks_ == nullptr || index < 0 ||
      index >= static_cast<int>(marks_->marks().size())) {
    return QStringLiteral("-");
  }
  const lapian::core::Mark& mark = marks_->marks()[index];
  return QStringLiteral("frame=%1 label=%2 note=%3")
      .arg(static_cast<qlonglong>(mark.frame))
      .arg(EscapeForLog(QString::fromStdString(mark.label)),
           EscapeForLog(QString::fromStdString(mark.note)));
}

void MainWindow::DeleteMarkWithConfirmForTest(int index) {
  if (marks_ == nullptr || index < 0 ||
      index >= static_cast<int>(marks_->marks().size())) {
    return;
  }
  ApplyDeleteMark(QString::fromStdString(marks_->marks()[index].id), true);
}

bool MainWindow::FocusNoteEditorForTest() {
  return mark_panel_ != nullptr && mark_panel_->FocusNoteEditorForTest();
}

bool MainWindow::CloseForTest() {
  // 走真实的 closeEvent（flush 备注 + 保存），但不隐藏/销毁窗口，便于随后重开验证。
  QCloseEvent event;
  QApplication::sendEvent(this, &event);
  return event.isAccepted();
}

void MainWindow::ResetNotePerfSamplesForTest() { note_save_samples_.clear(); }

QString MainWindow::NotePerfSummaryForTest() const {
  if (note_save_samples_.empty()) {
    return QStringLiteral("note_save_n=0");
  }
  std::vector<double> samples = note_save_samples_;
  std::sort(samples.begin(), samples.end());
  const double p50 = samples[samples.size() / 2];
  const std::size_t p95_index =
      std::min(samples.size() - 1,
               static_cast<std::size_t>(static_cast<double>(samples.size()) * 0.95));
  return QStringLiteral("note_save_n=%1 p50=%2ms p95=%3ms max=%4ms")
      .arg(samples.size())
      .arg(p50, 0, 'f', 2)
      .arg(samples[p95_index], 0, 'f', 2)
      .arg(samples.back(), 0, 'f', 2);
}

void MainWindow::CaptureSnapshotForTest() { CaptureSnapshot(); }

void MainWindow::PressSnapshotKeyForTest() {
  QWidget* target = QApplication::focusWidget();
  if (target == nullptr) {
    target = this;
  }
  QKeyEvent press(QEvent::KeyPress, Qt::Key_S, Qt::NoModifier);
  QApplication::sendEvent(target, &press);
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_S, Qt::NoModifier);
  QApplication::sendEvent(target, &release);
}

bool MainWindow::DrainSnapshotsForTest() {
  if (snapshots_ == nullptr) {
    return false;
  }
  QElapsedTimer drain_timer;
  drain_timer.start();
  snapshots_->Drain();
  const double drain_ms = static_cast<double>(drain_timer.nsecsElapsed()) / 1.0e6;
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  if (snapshot_log_) {
    std::fprintf(stderr, "T8.7 snapshot_drain ms=%.2f pending=%d\n", drain_ms,
                 snapshots_->PendingCount());
    std::fflush(stderr);
  }
  return snapshots_->PendingCount() == 0;
}

void MainWindow::ClearFocusForTest() {
  if (QWidget* focused = QApplication::focusWidget()) {
    focused->clearFocus();
  }
}

void MainWindow::SetSnapshotPicturesRootForTest(const QString& root) {
  snapshot_pictures_root_for_test_ = root;
  if (snapshots_ != nullptr) {
    snapshots_->SetPicturesRoot(root);
  }
}

QString MainWindow::SnapshotStateForTest() const {
  const int pending = snapshots_ == nullptr ? 0 : snapshots_->PendingCount();
  return QStringLiteral(
             "saved=%1 pending=%2 last_frame=%3 capture_frame=%4 fallback=%5 "
             "playing=%6 current=%7 last=%8 error=%9 hardware=%10")
      .arg(static_cast<qlonglong>(snapshot_saved_paths_.size()))
      .arg(pending)
      .arg(static_cast<qlonglong>(snapshot_last_saved_frame_))
      .arg(static_cast<qlonglong>(snapshot_last_capture_frame_))
      .arg(snapshot_last_fallback_ ? 1 : 0)
      .arg(playback_ != nullptr && playback_->IsPlaying() ? 1 : 0)
      .arg(static_cast<qlonglong>(current_frame_))
      .arg(snapshot_last_path_.isEmpty() ? QStringLiteral("-") : snapshot_last_path_)
      .arg(snapshot_last_error_.isEmpty() ? QStringLiteral("-") : snapshot_last_error_)
      .arg(displayed_frame_ != nullptr && displayed_frame_->hw_frames_ctx != nullptr ? 1 : 0);
}

QStringList MainWindow::SnapshotSavedPathsForTest() const {
  return snapshot_saved_paths_;
}

QSize MainWindow::SnapshotDisplaySizeForTest() const {
  if (displayed_frame_ == nullptr || displayed_frame_->width <= 0 ||
      displayed_frame_->height <= 0) {
    return {};
  }
  return lapian::snapshot::SnapshotDisplaySize(
      QSize(displayed_frame_->width, displayed_frame_->height), displayed_rotation_);
}

QString MainWindow::SnapshotLastPathForTest() const { return snapshot_last_path_; }

bool MainWindow::SnapshotLastFallbackForTest() const { return snapshot_last_fallback_; }

qint64 MainWindow::SnapshotLastCaptureFrameForTest() const {
  return static_cast<qint64>(snapshot_last_capture_frame_);
}

qint64 MainWindow::SnapshotLastSavedFrameForTest() const {
  return static_cast<qint64>(snapshot_last_saved_frame_);
}

QString MainWindow::SnapshotLastErrorForTest() const { return snapshot_last_error_; }

QString MainWindow::SnapshotStatusForTest() const {
  return statusBar() == nullptr ? QString() : statusBar()->currentMessage();
}

void MainWindow::ResetSnapshotPerfSamplesForTest() {
  snapshot_capture_samples_.clear();
  snapshot_save_samples_.clear();
}

QString MainWindow::SnapshotPerfSummaryForTest() const {
  auto summarize = [](std::vector<double> samples, const QString& prefix) {
    if (samples.empty()) {
      return QStringLiteral("%1_n=0").arg(prefix);
    }
    std::sort(samples.begin(), samples.end());
    const double p50 = samples[samples.size() / 2];
    const std::size_t p95_index =
        std::min(samples.size() - 1,
                 static_cast<std::size_t>(static_cast<double>(samples.size()) * 0.95));
    return QStringLiteral("%1_n=%2 %1_p50=%3ms %1_p95=%4ms %1_max=%5ms")
        .arg(prefix)
        .arg(samples.size())
        .arg(p50, 0, 'f', 2)
        .arg(samples[p95_index], 0, 'f', 2)
        .arg(samples.back(), 0, 'f', 2);
  };
  return summarize(snapshot_capture_samples_, QStringLiteral("capture")) +
         QStringLiteral(" ") +
         summarize(snapshot_save_samples_, QStringLiteral("save"));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
  if (event == nullptr || event->mimeData() == nullptr) {
    return;
  }
  for (const QUrl& url : event->mimeData()->urls()) {
    if (url.isLocalFile()) {
      event->acceptProposedAction();
      return;
    }
  }
}

void MainWindow::dropEvent(QDropEvent* event) {
  if (event == nullptr || event->mimeData() == nullptr) {
    return;
  }
  for (const QUrl& url : event->mimeData()->urls()) {
    if (url.isLocalFile()) {
      event->acceptProposedAction();
      OpenFile(url.toLocalFile());
      return;
    }
  }
}

void MainWindow::PollWaveformStatus() {
  if (!waveform_) {
    return;
  }
  const lapian::audio::WaveformStatus status = waveform_->status();
  if (status.revision == waveform_status_revision_) {
    return;
  }
  waveform_status_revision_ = status.revision;

  using lapian::audio::WaveformState;
  const auto waveform_data = waveform_->data();
  const int bucket_count =
      waveform_data ? static_cast<int>(waveform_data->bucket_count()) : 0;
  const bool buckets_grew = bucket_count != waveform_last_bucket_count_;
  waveform_last_bucket_count_ = bucket_count;
  // T8.11：波形渐进可见。时间轴只在桶数量增长/就绪时重绘，避免每 200ms 空转。
  if (timeline_ != nullptr &&
      (buckets_grew || status.state == WaveformState::kReady)) {
    timeline_->update();
  }

  switch (status.state) {
    case WaveformState::kRunning:
      waveform_label_->setText(
          QStringLiteral("波形：%1%").arg(QString::number(status.progress * 100.0, 'f', 0)));
      break;
    case WaveformState::kReady:
      waveform_label_->setText(QStringLiteral("波形：就绪"));
      break;
    case WaveformState::kNoAudio:
      waveform_label_->setText(QStringLiteral("波形：无音频"));
      timeline_->SetWaveformMessage(QStringLiteral("无音频"));
      break;
    case WaveformState::kFailed:
      waveform_label_->setText(QStringLiteral("波形：失败"));
      timeline_->SetWaveformMessage(QStringLiteral("波形生成失败"));
      statusBar()->showMessage(QString::fromStdString(status.message), 5000);
      break;
    case WaveformState::kCancelled:
    case WaveformState::kIdle:
      waveform_label_->setText(QStringLiteral("波形：—"));
      break;
  }
}

void MainWindow::OnDeviceReady(const QString& summary) {
  if (!session_ || !session_->IsOpen()) {
    status_label_->setText(base_info_ + QStringLiteral(" · ") + summary);
  }
}

void MainWindow::OnDeviceError(const QString& message) {
  qWarning() << "图形错误:" << message;
  if (error_shown_) {
    return;
  }
  error_shown_ = true;
  QMessageBox::critical(this, QStringLiteral("图形初始化失败"),
                        message + QStringLiteral("\n\n窗口仍可操作，但画面区域不可用。"));
}

void MainWindow::OnVolumeChanged(int value) {
  volume_ = value;
  if (volume_slider_ != nullptr) {
    volume_slider_->setToolTip(QStringLiteral("音量 %1%").arg(value));
  }
  if (audio_) {
    audio_->SetVolume(value);
  }
  ScheduleSettingsSave();
}

void MainWindow::ToggleMute(bool muted) {
  if (IsTextInputFocused()) {
    if (mute_action_ != nullptr) {
      mute_action_->setChecked(!muted);
    }
    return;
  }
  muted_ = muted;
  if (mute_action_ != nullptr) {
    mute_action_->setText(muted ? QStringLiteral("取消静音") : QStringLiteral("静音"));
  }
  if (audio_) {
    audio_->SetMute(muted);
  }
  statusBar()->showMessage(muted ? QStringLiteral("已静音") : QStringLiteral("已取消静音"), 1500);
}

void MainWindow::PollAudioStatus() {
  if (playback_ && playback_->IsBuffering()) {
    playback_->TryResumeFromBuffering();
  }
  if (!audio_) {
    return;
  }
  const lapian::audio::AudioStatus status = audio_->status();
  if (status.revision == audio_status_revision_) {
    return;
  }
  audio_status_revision_ = status.revision;

  using lapian::audio::AudioState;
  const QString mute_suffix = status.muted ? QStringLiteral("（静音）") : QString();
  switch (status.state) {
    case AudioState::kOpening:
      audio_label_->setText(QStringLiteral("音频：准备中…"));
      break;
    case AudioState::kPlaying:
    case AudioState::kPaused:
      audio_label_->setText(audio_base_label_ + mute_suffix);
      break;
    case AudioState::kSilent:
      audio_label_->setText(QStringLiteral("音频：设备不可用"));
      break;
    case AudioState::kError:
      audio_label_->setText(QStringLiteral("音频不可用"));
      if (volume_slider_ != nullptr) {
        volume_slider_->setEnabled(false);
      }
      if (mute_action_ != nullptr) {
        mute_action_->setEnabled(false);
      }
      break;
    case AudioState::kClosed:
      audio_label_->setText(QStringLiteral("无音频"));
      break;
  }

  if (!status.message.empty()) {
    const QString message = QString::fromStdString(status.message);
    if (status.state == AudioState::kSilent || status.state == AudioState::kError) {
      statusBar()->showMessage(message, 5000);
    } else if (message == QStringLiteral("音频设备已恢复")) {
      statusBar()->showMessage(message, 3000);
    }
  }

  if (status.state == AudioState::kError && !audio_error_shown_) {
    audio_error_shown_ = true;
    qWarning() << "音频错误:" << QString::fromStdString(status.message);
  }
}
