#include "app/MainWindow.h"

#include <QDebug>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QString>

extern "C" {
#include <libavutil/avutil.h>
}

#include "app/GfxWidget.h"

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

void MainWindow::OnDeviceError(const QString& message) {
  qWarning() << "图形错误:" << message;
  if (error_shown_) {
    return;
  }
  error_shown_ = true;
  QMessageBox::critical(this, QStringLiteral("图形初始化失败"),
                        message + QStringLiteral("\n\n窗口仍可操作，但画面区域不可用。"));
}
