#include "app/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QDebug>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
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
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
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
#include <vector>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
}

#include "app/GfxWidget.h"
#include "app/InfoBar.h"
#include "app/MarkPanel.h"
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
#include "media/PreviewDecoder.h"

namespace {

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

MainWindow::MainWindow(QWidget* parent, const QString& settings_root)
    : QMainWindow(parent) {
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
  displayed_frame_ = av_frame_alloc();

  playback_ = std::make_unique<PlaybackController>(this);
  connect(playback_.get(), &PlaybackController::frameReady, this, &MainWindow::OnFrameReady);
  connect(playback_.get(), &PlaybackController::stateChanged, this,
          &MainWindow::OnPlaybackStateChanged);
  connect(playback_.get(), &PlaybackController::reachedEnd, this, &MainWindow::OnPlaybackEnded);
  connect(playback_.get(), &PlaybackController::loopJumped, this, &MainWindow::OnLoopJumped);

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
      StopStepRepeat();  // 到边界：不再重复 seek 同一帧
    }
  });
  connect(qApp, &QApplication::focusChanged, this,
          [this](QWidget*, QWidget*) { StopStepRepeat(); });
  connect(qApp, &QGuiApplication::applicationStateChanged, this,
          [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive) {
              StopStepRepeat();
            }
          });
  qApp->installEventFilter(this);

  connect(timeline_, &TimelineWidget::scrubStarted, this, &MainWindow::OnScrubStarted);
  connect(timeline_, &TimelineWidget::scrubTargetChanged, this,
          &MainWindow::OnScrubTargetChanged);
  connect(timeline_, &TimelineWidget::scrubFinished, this, &MainWindow::OnScrubFinished);

  UpdateResponsiveLayout();
}

MainWindow::~MainWindow() {
  // T7.1：先取消并 join 索引线程（每包检查取消，通常毫秒级返回），再释放会话。
  CancelIndexBuild();
  // 先落盘设置（不等待截图/标记），再停掉可能还在跑的步进连发并摘掉应用级过滤器。
  FlushSettings();
  StopStepRepeat();
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
  const bool ok = settings_store_->Save(
      lapian::core::AppSettings{volume_, waveform_visible_});
  settings_last_save_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  if (ok) {
    settings_dirty_ = false;
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
  for (QLabel* label : info_labels) {
    if (!label->isVisible()) {
      continue;
    }
    if (!info_bar_->rect().contains(label->geometry())) {
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
          first->geometry().intersects(second->geometry())) {
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
  statusBar()->showMessage(QStringLiteral("将视频拖到这里，或按 Ctrl+O 打开"));
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

void MainWindow::OpenFile(const QString& path) {
  if (path.isEmpty()) {
    return;
  }
  // T7.1：打开路径分段计时；只在冷启动日志开启时输出。
  QElapsedTimer open_timer;
  open_timer.start();
  double audio_open_ms = 0.0;
  double waveform_start_ms = 0.0;
  double first_upload_ms = 0.0;
  CancelPendingMarkJump();
  if (gfx_ == nullptr || gfx_->d3d_device() == nullptr) {
    ShowOpenError(QStringLiteral("图形设备未就绪，无法打开视频"));
    return;
  }

  if (playback_ != nullptr) {
    playback_->Pause();
  }
  // 旧文件的截图任务先完成，避免工作线程在会话替换后继续引用旧帧。
  if (snapshots_ != nullptr) {
    snapshots_->Drain();
  }

  const QByteArray utf8 = path.toUtf8();
  const std::string path_utf8(utf8.constData(), static_cast<std::size_t>(utf8.size()));

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
  session_ = std::move(candidate);
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

  const QFileInfo file_info(path);
  setWindowTitle(file_info.fileName() + QStringLiteral(" — 拉片软件"));

  info_bar_->SetMedia(info, session_->IsVfrSuspect());
  status_label_->setText(base_info_ + QStringLiteral(" · ") +
                         (frame.hardware ? QStringLiteral("硬解") : QStringLiteral("软解")));

  if (session_->IsVfrSuspect()) {
    statusBar()->showMessage(
        QStringLiteral("可变帧率（VFR）：帧号按显示帧计数，时间码不承诺与帧号严格对应"), 8000);
  } else {
    statusBar()->showMessage(QStringLiteral("已打开，停在第 0 帧"));
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
  marks_.reset();
  marks_dirty_ = false;
  if (mark_panel_ != nullptr) {
    mark_panel_->Clear();
  }
  if (timeline_ != nullptr) {
    timeline_->SetMarks({});
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
  if (timeline_ != nullptr) {
    timeline_->SetFrameCount(frames);
    timeline_->setEnabled(true);
  }
  const bool can_seek = ready;
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
  } else {
    info_bar_->SetTotal(ready ? session_->index().Total() : ContainerTotal());
  }
  SyncLoopUi();
  LoadMarksForCurrentSession();
  if (ready) {
    // T2.9：索引就绪后再预热预览器；扫描中不创建（没有可用索引）。
    preview_ = std::make_unique<lapian::media::PreviewDecoder>();
    preview_->Configure(session_->path_utf8(), session_->video_stream_index(),
                        &session_->index());
  }
  RefreshMarksUi();
  ShowMarkWarnings();
}

void MainWindow::LoadMarksForCurrentSession() {
  if (session_ == nullptr) {
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
  lapian::core::MarkStore opened_marks = lapian::core::MarkStore::Open(
      session_->path_utf8(), session_->info().duration_us, {}, mark_lookup);
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
    preview_timer_->stop();
    if (preview_ != nullptr) {
      preview_->End();
    }
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
    preview_->Begin(frame);
    if (preview_timer_ != nullptr && !preview_timer_->isActive()) {
      preview_timer_->start();
    }
  }
  statusBar()->showMessage(QStringLiteral("拖动预览：松开后精确到帧"), 1000);
}

void MainWindow::OnScrubTargetChanged(qint64 frame) {
  if (session_ == nullptr || !session_->IsOpen()) {
    return;
  }
  ShowScrubTarget(frame);
  if (preview_ != nullptr) {
    preview_->UpdateTarget(frame);
  }
}

void MainWindow::OnScrubFinished(qint64 frame) {
  if (session_ == nullptr || !session_->IsOpen() || session_->index().empty()) {
    return;
  }
  if (preview_timer_ != nullptr) {
    preview_timer_->stop();
  }
  if (preview_ != nullptr) {
    preview_->End();
  }
  const std::int64_t last_frame =
      static_cast<std::int64_t>(session_->index().size()) - 1;
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

void MainWindow::StopPreview() {
  if (preview_timer_ != nullptr) {
    preview_timer_->stop();
  }
  if (preview_ != nullptr) {
    preview_->Close();
  }
}

void MainWindow::OnPreviewTick() {
  if (preview_ == nullptr || session_ == nullptr || !session_->IsOpen() ||
      gfx_ == nullptr) {
    return;
  }
  const bool timing = qEnvironmentVariableIsSet("LAPIAN_TIMING_LOG");
  QElapsedTimer tick_timer;
  if (timing) {
    tick_timer.start();
  }
  lapian::media::PreviewFrame preview_frame = preview_->TakeLatestFrame();
  if (preview_frame.frame == nullptr) {
    return;
  }
  gfx_->SetVideoFrame(preview_frame.frame,
                      session_->info().rotation.counterclockwise_degrees);
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
      StopStepRepeat();
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
      StopStepRepeat();
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
  const std::int64_t before = current_frame_;
  // T7.2：后退键先预热最近一窗帧，后续连发可直接命中缓存。
  GoToFrameForStep(before + delta, delta < 0);
  if (current_frame_ == before) {
    StopStepRepeat();  // 已在边界：不启动连发
    return;
  }
  step_repeat_timer_->start(300);
}

void MainWindow::StopStepRepeat() {
  step_direction_ = 0;
  if (step_repeat_timer_ != nullptr) {
    step_repeat_timer_->stop();
  }
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
  // 普通跳转/拖动松手/标记跳转不写缓存：实测每次多一次 3MB 回拷，
  // 而连续后退需要的是“目标之前一窗帧”，由 GoToFrameForStep 单独预热。
  GoToFrameInternal(frame, lapian::media::FrameCacheMode::kNone);
}

void MainWindow::GoToFrameForStep(std::int64_t frame, bool backward) {
  // 后退步进预热一窗帧；前进步进顺序解码本身不命中缓存，写缓存只会增加拷贝。
  GoToFrameInternal(frame, backward ? lapian::media::FrameCacheMode::kBackwardWindow
                                    : lapian::media::FrameCacheMode::kNone);
}

void MainWindow::GoToFrameInternal(std::int64_t frame,
                                   lapian::media::FrameCacheMode mode) {
  if (session_ == nullptr || !session_->IsOpen() || !session_->index().IsReady() ||
      session_->index().empty()) {
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
    return;
  }

  gfx_->SetVideoFrame(info.av_frame, session_->info().rotation.counterclockwise_degrees);
  RememberDisplayedFrame(info.av_frame, session_->info().rotation.counterclockwise_degrees,
                         target);
  info_bar_->SetFrame(static_cast<int>(target));
  timeline_->SetCurrentFrame(target);
  playback_->SetPosition(target, info.pts_us);
  current_frame_ = target;
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
  if (info_bar_ != nullptr) {
    info_bar_->SetError(message);
  }
  statusBar()->showMessage(message);
  QMessageBox::warning(this, QStringLiteral("打开失败"), message);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  // T6.3：先把音量/波形落盘；它独立于截图/标记的等待链，失败也不阻止关窗。
  FlushSettings();
  CancelPendingMarkJump();
  // 关窗前把已按下的截图任务写完，避免用户以为存了却丢文件。
  if (snapshots_ != nullptr) {
    snapshots_->Drain();
  }
  if (!FlushMarksOnClose()) {
    event->ignore();
    return;
  }
  QMainWindow::closeEvent(event);
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
  // 先提交侧栏里未保存的标签/备注，避免增删标记时被列表刷新覆盖。
  FlushMarkEditor();
  const lapian::media::FrameIndex& index = session_->index();
  if (!index.IsReady() || index.empty()) {
    statusBar()->showMessage(QStringLiteral("帧索引未就绪，暂时不能添加标记"), 3000);
    return;
  }
  const lapian::media::FrameIndexEntry* entry = index.Entry(current_frame_);
  if (entry == nullptr) {
    statusBar()->showMessage(QStringLiteral("当前帧没有 PTS，暂时不能添加标记"), 3000);
    return;
  }
  std::string id;
  if (!marks_->AddMark(current_frame_, entry->pts_us, std::string(), std::string(), &id)) {
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
          .arg(marks_->using_fallback() ? QStringLiteral("%APPDATA% 回退")
                                        : QStringLiteral("sidecar")),
      3000);
  if (qEnvironmentVariableIsSet("LAPIAN_MARK_LOG")) {
    std::fprintf(stderr, "T5.2 mark_add frame=%lld count=%d\n",
                 static_cast<long long>(current_frame_), MarkCountForTest());
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
    preview_timer_->stop();
    if (preview_ != nullptr) {
      preview_->End();
    }
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
  const lapian::core::SaveResult result = marks_->Save();
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
  if (blocking) {
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
  snapshots_->Drain();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  return snapshots_->PendingCount() == 0;
}

void MainWindow::ClearFocusForTest() {
  if (QWidget* focused = QApplication::focusWidget()) {
    focused->clearFocus();
  }
}

void MainWindow::SetSnapshotPicturesRootForTest(const QString& root) {
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
