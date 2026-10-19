#include "app/MainWindow.h"

#include <QAction>
#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QStandardPaths>
#include <QStatusBar>
#include <QString>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <memory>
#include <string>

extern "C" {
#include <libavutil/avutil.h>
}

#include "app/GfxWidget.h"
#include "app/InfoBar.h"
#include "app/PlaybackController.h"
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
  play_action_->setEnabled(true);

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
