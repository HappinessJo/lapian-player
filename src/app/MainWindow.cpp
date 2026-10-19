#include "app/MainWindow.h"

#include <QByteArray>
#include <QDebug>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QString>

#include <cstddef>
#include <string>

extern "C" {
#include <libavutil/avutil.h>
}

#include "app/GfxWidget.h"
#include "media/MediaProbe.h"
#include "media/VideoDecoder.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("拉片软件 — 骨架 T0.1"));
  resize(1280, 720);

  gfx_ = new GfxWidget(this);
  setCentralWidget(gfx_);

  status_label_ = new QLabel(this);
  status_label_->setObjectName(QStringLiteral("statusInfo"));
  status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  statusBar()->addPermanentWidget(status_label_);

  base_info_ = QStringLiteral("Qt %1 · FFmpeg %2").arg(QString::fromLatin1(qVersion()),
                                                       QString::fromLatin1(av_version_info()));
  status_label_->setText(base_info_ + QStringLiteral(" · D3D11 初始化中…"));
  statusBar()->showMessage(QStringLiteral("尚未打开文件（T0.1 骨架，无媒体加载）"));

  connect(gfx_, &GfxWidget::deviceReady, this, &MainWindow::OnDeviceReady);
  connect(gfx_, &GfxWidget::deviceError, this, &MainWindow::OnDeviceError, Qt::QueuedConnection);
}

void MainWindow::OnDeviceReady(const QString& summary) {
  status_label_->setText(base_info_ + QStringLiteral(" · ") + summary);
}

void MainWindow::LoadFileForTest(const QString& path) {
  const QByteArray utf8 = path.toUtf8();
  const std::string path_utf8(utf8.constData(), static_cast<std::size_t>(utf8.size()));

  const lapian::media::MediaProbeResult probe = lapian::media::ProbeMedia(path_utf8);
  if (!probe.ok) {
    const QString message = QStringLiteral("打开失败：") +
                            QString::fromStdString(probe.error_detail);
    qWarning() << message;
    statusBar()->showMessage(message);
    return;
  }

  ID3D11Device* d3d_device = gfx_->d3d_device();
  if (d3d_device == nullptr) {
    statusBar()->showMessage(QStringLiteral("D3D11 设备未就绪，无法解码"));
    return;
  }

  lapian::media::VideoDecoder decoder;
  std::string error;
  if (!decoder.OpenWithD3D11Device(path_utf8, probe.info.video.stream_index, d3d_device, true,
                                    &error)) {
    const QString message = QStringLiteral("解码器打开失败：") + QString::fromStdString(error);
    qWarning() << message;
    statusBar()->showMessage(message);
    return;
  }

  lapian::media::DecodedFrameInfo frame;
  if (!decoder.DecodeFirstFrame(&frame, &error)) {
    const QString message = QStringLiteral("首帧解码失败：") + QString::fromStdString(error);
    qWarning() << message;
    statusBar()->showMessage(message);
    return;
  }

  gfx_->SetVideoFrame(frame.av_frame, probe.info.rotation.counterclockwise_degrees);

  const QFileInfo file_info(path);
  setWindowTitle(file_info.fileName() + QStringLiteral(" — 拉片软件"));
  status_label_->setText(
      QStringLiteral("%1 · %2×%3 · %4fps · %5 · 第 0 帧")
          .arg(file_info.fileName())
          .arg(probe.info.video.width)
          .arg(probe.info.video.height)
          .arg(probe.info.video.frame_rate.ToDouble(), 0, 'f', 3)
          .arg(frame.hardware ? QStringLiteral("硬解") : QStringLiteral("软解")));
  statusBar()->showMessage(QStringLiteral("已打开（T1.3 渲染验证）"));
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
