#include "app/MainWindow.h"

#include <QAction>
#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QStandardPaths>
#include <QStatusBar>
#include <QString>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>

extern "C" {
#include <libavutil/avutil.h>
}

#include "app/GfxWidget.h"
#include "app/InfoBar.h"
#include "app/PlaybackController.h"
#include "core/FrameMath.h"
#include "media/MediaProbe.h"
#include "media/MediaSession.h"

namespace {

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
  info_bar_ = new InfoBar(central);
  central_layout->addWidget(gfx_, 1);
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

  playback_ = std::make_unique<PlaybackController>(this);
  connect(playback_.get(), &PlaybackController::frameReady, this, &MainWindow::OnFrameReady);
  connect(playback_.get(), &PlaybackController::stateChanged, this,
          &MainWindow::OnPlaybackStateChanged);
  connect(playback_.get(), &PlaybackController::reachedEnd, this, &MainWindow::OnPlaybackEnded);

  status_label_ = new QLabel(this);
  status_label_->setObjectName(QStringLiteral("statusInfo"));
  status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  statusBar()->addPermanentWidget(status_label_);

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
}

MainWindow::~MainWindow() = default;

void MainWindow::ShowEmptyState() {
  if (playback_ != nullptr) {
    playback_->Pause();
    playback_->SetSession(nullptr);
  }
  if (gfx_ != nullptr) {
    gfx_->ClearVideoFrame();
  }
  if (info_bar_ != nullptr) {
    info_bar_->Clear();
  }
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
  session_ = std::move(candidate);
  playback_->SetSession(session_.get());
  current_frame_ = 0;
  play_action_->setEnabled(true);
  const bool can_seek = session_->index().IsReady() && !session_->index().empty();
  prev_action_->setEnabled(can_seek);
  next_action_->setEnabled(can_seek);
  goto_action_->setEnabled(can_seek);

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
  playback_->Toggle();
}

void MainWindow::StartPlaybackForTest() {
  if (playback_ != nullptr && session_ && session_->IsOpen() && !playback_->IsPlaying()) {
    playback_->Play();
  }
}

void MainWindow::OnFrameReady(const AVFrame* frame, int rotation, int frame_index) {
  if (gfx_ != nullptr && frame != nullptr) {
    gfx_->SetVideoFrame(frame, rotation);
  }
  if (info_bar_ != nullptr) {
    info_bar_->SetFrame(frame_index);
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
    statusBar()->showMessage(QStringLiteral("跳帧失败：") + QString::fromStdString(error), 5000);
    return;
  }

  gfx_->SetVideoFrame(info.av_frame, session_->info().rotation.counterclockwise_degrees);
  info_bar_->SetFrame(static_cast<int>(target));
  playback_->SetPosition(target, info.pts_us);
  current_frame_ = target;
  statusBar()->showMessage(QStringLiteral("第 %1 帧").arg(target), 2000);
}

void MainWindow::OnPlaybackStateChanged(bool playing) {
  play_action_->setText(playing ? QStringLiteral("暂停") : QStringLiteral("播放"));
  statusBar()->showMessage(playing ? QStringLiteral("播放中") : QStringLiteral("已暂停"), 2000);
}

void MainWindow::OnPlaybackEnded() {
  play_action_->setText(QStringLiteral("播放"));
  play_action_->setEnabled(false);
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
