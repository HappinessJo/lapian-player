#include "app/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QString>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
}

#include "app/GfxWidget.h"
#include "app/InfoBar.h"
#include "app/PlaybackController.h"
#include "app/TimelineWidget.h"
#include "audio/AudioEngine.h"
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

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("拉片软件"));
  resize(1280, 720);
  setAcceptDrops(true);

  auto* central = new QWidget(this);
  auto* central_layout = new QVBoxLayout(central);
  central_layout->setContentsMargins(0, 0, 0, 0);
  central_layout->setSpacing(0);
  gfx_ = new GfxWidget(central);
  timeline_ = new TimelineWidget(central);
  info_bar_ = new InfoBar(central);
  central_layout->addWidget(gfx_, 1);
  central_layout->addWidget(timeline_);
  central_layout->addWidget(info_bar_);
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
  zoom_in_action_->setShortcut(QKeySequence(QStringLiteral("+")));
  zoom_in_action_->setShortcutContext(Qt::WindowShortcut);
  zoom_in_action_->setEnabled(false);
  connect(zoom_in_action_, &QAction::triggered, this, &MainWindow::ZoomTimelineIn);
  addAction(zoom_in_action_);

  zoom_out_action_ = new QAction(QStringLiteral("缩小时间轴"), this);
  zoom_out_action_->setShortcut(QKeySequence(QStringLiteral("-")));
  zoom_out_action_->setShortcutContext(Qt::WindowShortcut);
  zoom_out_action_->setEnabled(false);
  connect(zoom_out_action_, &QAction::triggered, this, &MainWindow::ZoomTimelineOut);
  addAction(zoom_out_action_);

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

  playback_ = std::make_unique<PlaybackController>(this);
  connect(playback_.get(), &PlaybackController::frameReady, this, &MainWindow::OnFrameReady);
  connect(playback_.get(), &PlaybackController::stateChanged, this,
          &MainWindow::OnPlaybackStateChanged);
  connect(playback_.get(), &PlaybackController::reachedEnd, this, &MainWindow::OnPlaybackEnded);

  preview_timer_ = new QTimer(this);
  preview_timer_->setInterval(16);
  preview_timer_->setTimerType(Qt::PreciseTimer);
  connect(preview_timer_, &QTimer::timeout, this, &MainWindow::OnPreviewTick);

  audio_label_ = new QLabel(QStringLiteral("无音频"), this);
  audio_label_->setObjectName(QStringLiteral("audioInfo"));
  audio_label_->setToolTip(QStringLiteral("音频轨道与输出状态"));
  statusBar()->addPermanentWidget(audio_label_);

  speed_label_ = new QLabel(QStringLiteral("1x"), this);
  speed_label_->setObjectName(QStringLiteral("speedInfo"));
  speed_label_->setToolTip(QStringLiteral("播放速度（[ / ] 调整）"));
  statusBar()->addPermanentWidget(speed_label_);

  volume_slider_ = new QSlider(Qt::Horizontal, this);
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

  audio_status_timer_ = new QTimer(this);
  audio_status_timer_->setInterval(200);
  connect(audio_status_timer_, &QTimer::timeout, this, &MainWindow::PollAudioStatus);
  audio_status_timer_->start();

  base_info_ = QStringLiteral("Qt %1 · FFmpeg %2").arg(QString::fromLatin1(qVersion()),
                                                       QString::fromLatin1(av_version_info()));
  ShowEmptyState();

  connect(gfx_, &GfxWidget::deviceReady, this, &MainWindow::OnDeviceReady);
  connect(gfx_, &GfxWidget::deviceError, this, &MainWindow::OnDeviceError, Qt::QueuedConnection);
  connect(gfx_, &GfxWidget::playPauseRequested, this, &MainWindow::TogglePlayback);
  connect(gfx_, &GfxWidget::stepRequested, this, [this](int delta) {
    GoToFrame(current_frame_ + delta);
  });
  connect(gfx_, &GfxWidget::gotoRequested, this, &MainWindow::ShowGotoDialog);
  connect(timeline_, &TimelineWidget::scrubStarted, this, &MainWindow::OnScrubStarted);
  connect(timeline_, &TimelineWidget::scrubTargetChanged, this,
          &MainWindow::OnScrubTargetChanged);
  connect(timeline_, &TimelineWidget::scrubFinished, this, &MainWindow::OnScrubFinished);
}

MainWindow::~MainWindow() = default;

void MainWindow::ShowEmptyState() {
  StopPreview();
  preview_.reset();
  if (audio_) {
    audio_.reset();
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
  if (slower_action_ != nullptr) {
    slower_action_->setEnabled(false);
  }
  if (faster_action_ != nullptr) {
    faster_action_->setEnabled(false);
  }
  current_frame_ = 0;
  setWindowTitle(QStringLiteral("拉片软件"));
  status_label_->setText(base_info_ + QStringLiteral(" · 未打开文件"));
  statusBar()->showMessage(QStringLiteral("将视频拖到这里，或按 Ctrl+O 打开"));
}

void MainWindow::OpenDialog() {
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
  if (gfx_ == nullptr || gfx_->d3d_device() == nullptr) {
    ShowOpenError(QStringLiteral("图形设备未就绪，无法打开视频"));
    return;
  }

  if (playback_ != nullptr) {
    playback_->Pause();
  }

  const QByteArray utf8 = path.toUtf8();
  const std::string path_utf8(utf8.constData(), static_cast<std::size_t>(utf8.size()));

  auto candidate = std::make_unique<lapian::media::MediaSession>();
  std::string error;
  if (!candidate->Open(path_utf8, gfx_->d3d_device(), &error)) {
    QString message = DescribeProbeError(candidate->last_attempt().error);
    if (!error.empty()) {
      message += QStringLiteral("\n\n") + QString::fromStdString(error);
    }
    ShowOpenError(message);
    return;
  }

  // T1.4 占位：当前还没有标记/备注可写；T6 在这里保存旧文件状态。
  SaveCurrentStatePlaceholder();
  // 旧预览器持有旧索引指针，必须在替换 session 之前停掉。
  StopPreview();
  preview_.reset();
  if (audio_) {
    audio_.reset();
  }
  session_ = std::move(candidate);
  playback_->SetSession(session_.get());
  current_frame_ = 0;
  play_action_->setEnabled(true);
  const bool can_seek = session_->index().IsReady() && !session_->index().empty();
  prev_action_->setEnabled(can_seek);
  next_action_->setEnabled(can_seek);
  goto_action_->setEnabled(can_seek);

  // T2.7：时间轴先用当前索引的帧数；索引为空时不画假刻度。
  const std::int64_t timeline_frames =
      static_cast<std::int64_t>(session_->index().size());
  timeline_->SetFrameCount(timeline_frames);
  zoom_in_action_->setEnabled(timeline_frames > 0);
  zoom_out_action_->setEnabled(timeline_frames > 0);
  slower_action_->setEnabled(true);
  faster_action_->setEnabled(true);
  speed_label_->setText(FormatSpeed(rate_));

  const lapian::media::MediaInfo& info = session_->info();
  const lapian::media::DecodedFrameInfo& frame = session_->first_frame();
  gfx_->SetVideoFrame(frame.av_frame, info.rotation.counterclockwise_degrees);

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

  // T3.1：音频异步打开；首帧已经呈现，不等音频设备初始化。
  audio_status_revision_ = 0;
  audio_error_shown_ = false;
  audio_base_label_.clear();
  audio_label_->setText(QStringLiteral("无音频"));
  volume_slider_->setEnabled(false);
  mute_action_->setEnabled(false);
  if (info.has_audio) {
    auto audio = std::make_unique<lapian::audio::AudioEngine>();
    audio->SetVolume(volume_);
    audio->SetMute(muted_);
    audio->SetRate(rate_);
    if (!audio->Open(path_utf8, info.audio.stream_index,
                     static_cast<double>(info.av_offset_us) / 1.0e6)) {
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

  // T2.9：预览解码器在后台预热；之后拖动时只发目标帧，不阻塞 UI。
  if (can_seek) {
    preview_ = std::make_unique<lapian::media::PreviewDecoder>();
    preview_->Configure(session_->path_utf8(), info.video.stream_index,
                        &session_->index());
  }
}

void MainWindow::ShowGotoDialog() {
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
  if (playback_ == nullptr || !session_ || !session_->IsOpen()) {
    return;
  }
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

void MainWindow::ShowScrubTarget(std::int64_t frame) {
  if (info_bar_ != nullptr) {
    info_bar_->SetFrame(static_cast<int>(frame));
  }
}

void MainWindow::OnScrubStarted(qint64 frame) {
  if (session_ == nullptr || !session_->IsOpen()) {
    return;
  }
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
  AVFrame* frame = preview_->TakeLatestFrame();
  if (frame == nullptr) {
    return;
  }
  gfx_->SetVideoFrame(frame, session_->info().rotation.counterclockwise_degrees);
  av_frame_free(&frame);
  if (timing && tick_timer.elapsed() > 200) {
    std::fprintf(stderr, "T2.10 preview_tick_ms=%lld\n",
                 static_cast<long long>(tick_timer.elapsed()));
    std::fflush(stderr);
  }
}

void MainWindow::OnFrameReady(const AVFrame* frame, int rotation, int frame_index) {
  if (gfx_ != nullptr && frame != nullptr) {
    gfx_->SetVideoFrame(frame, rotation);
  }
  if (info_bar_ != nullptr) {
    info_bar_->SetFrame(frame_index);
  }
  if (timeline_ != nullptr) {
    timeline_->SetCurrentFrame(frame_index);
  }
  current_frame_ = frame_index;
}

void MainWindow::StepBackward() {
  if (session_ != nullptr && session_->IsOpen()) {
    GoToFrame(current_frame_ - 1);
  }
}

void MainWindow::StepForward() {
  if (session_ != nullptr && session_->IsOpen()) {
    GoToFrame(current_frame_ + 1);
  }
}

void MainWindow::GoToFrame(std::int64_t frame) {
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
  if (!session_->SeekToFrame(target, &info, &error)) {
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
  info_bar_->SetFrame(static_cast<int>(target));
  timeline_->SetCurrentFrame(target);
  playback_->SetPosition(target, info.pts_us);
  current_frame_ = target;
  statusBar()->showMessage(QStringLiteral("第 %1 帧").arg(target), 2000);
}

void MainWindow::OnPlaybackStateChanged(bool playing) {
  play_action_->setText(playing ? QStringLiteral("暂停") : QStringLiteral("播放"));
  statusBar()->showMessage(playing ? QStringLiteral("播放中") : QStringLiteral("已暂停"), 2000);
  if (audio_) {
    if (playing) {
      audio_->PlayFrom(playback_->CurrentMediaSeconds());
    } else {
      audio_->Pause();
    }
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
  statusBar()->showMessage(message);
  QMessageBox::warning(this, QStringLiteral("打开失败"), message);
}

void MainWindow::SaveCurrentStatePlaceholder() {
  // T6 的 sidecar 写入接进来之前，这里保持无操作。
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
