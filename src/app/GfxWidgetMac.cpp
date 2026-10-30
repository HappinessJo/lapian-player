#include "app/GfxWidget.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTransform>
#include <QUrl>

#include <algorithm>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

GfxWidget::GfxWidget(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAttribute(Qt::WA_NoSystemBackground);
  setAutoFillBackground(false);
  setAcceptDrops(true);
  setMinimumSize(320, 180);
  setFocusPolicy(Qt::StrongFocus);
}

GfxWidget::~GfxWidget() = default;

bool GfxWidget::EnsureReady() {
  if (init_failed_) {
    return false;
  }
  if (!ready_) {
    ready_ = true;
    emit deviceReady(QStringLiteral("Qt 软件渲染 · FFmpeg"));
  }
  return ready_;
}

void GfxWidget::UpdateSwapChainSize() {}

void GfxWidget::PresentFrame() { update(); }

bool GfxWidget::SetVideoFrame(const AVFrame* frame, int rotation_counterclockwise_degrees,
                              bool report_error) {
  if (frame == nullptr || frame->width <= 0 || frame->height <= 0 ||
      frame->hw_frames_ctx != nullptr) {
    if (report_error) {
      emit deviceError(QStringLiteral("macOS 渲染路径需要有效的软件视频帧"));
    }
    return false;
  }

  QImage converted(frame->width, frame->height, QImage::Format_RGBA8888);
  if (converted.isNull()) {
    if (report_error) {
      emit deviceError(QStringLiteral("无法分配视频帧缓冲"));
    }
    return false;
  }

  SwsContext* scaler = sws_getCachedContext(
      nullptr, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
      frame->width, frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (scaler == nullptr) {
    if (report_error) {
      emit deviceError(QStringLiteral("无法创建视频色彩转换上下文"));
    }
    return false;
  }
  std::uint8_t* destination[4] = {converted.bits(), nullptr, nullptr, nullptr};
  int destination_lines[4] = {static_cast<int>(converted.bytesPerLine()), 0, 0, 0};
  const int rows = sws_scale(scaler, frame->data, frame->linesize, 0, frame->height,
                             destination, destination_lines);
  sws_freeContext(scaler);
  if (rows != frame->height) {
    if (report_error) {
      emit deviceError(QStringLiteral("视频帧色彩转换失败"));
    }
    return false;
  }

  current_frame_ = std::move(converted);
  rotation_ = rotation_counterclockwise_degrees;
  present_pending_ = true;
  update();
  return true;
}

void GfxWidget::ClearVideoFrame() {
  current_frame_ = QImage();
  present_pending_ = false;
  update();
}

ID3D11Device* GfxWidget::d3d_device() { return nullptr; }

bool GfxWidget::IsReady() const { return ready_ && !init_failed_; }

QSize GfxWidget::swapChainSize() const {
  return QSize(static_cast<int>(std::lround(width() * devicePixelRatioF())),
               static_cast<int>(std::lround(height() * devicePixelRatioF())));
}

void GfxWidget::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  EnsureReady();
  update();
}

void GfxWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  if (!EnsureReady()) {
    return;
  }
  QPainter painter(this);
  painter.fillRect(rect(), QColor(14, 14, 16));
  if (current_frame_.isNull()) {
    return;
  }

  const QImage display = rotation_ == 0
                             ? current_frame_
                             : current_frame_.transformed(
                                   QTransform().rotate(-rotation_), Qt::FastTransformation);
  const QSize fitted = display.size().scaled(size(), Qt::KeepAspectRatio);
  const QRect target(QPoint((width() - fitted.width()) / 2,
                            (height() - fitted.height()) / 2),
                     fitted);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.drawImage(target, display);
  if (present_pending_) {
    present_pending_ = false;
    emit framePresented();
  }
}

void GfxWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  update();
}

void GfxWidget::keyPressEvent(QKeyEvent* event) {
  if (event->modifiers().testFlag(Qt::ControlModifier) && event->key() == Qt::Key_G &&
      !event->isAutoRepeat()) {
    emit gotoRequested();
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    emit playPauseRequested();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void GfxWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event != nullptr && event->button() == Qt::LeftButton) {
    emit fullScreenToggleRequested();
    event->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}

void GfxWidget::dragEnterEvent(QDragEnterEvent* event) {
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

void GfxWidget::dropEvent(QDropEvent* event) {
  if (event == nullptr || event->mimeData() == nullptr) {
    return;
  }
  for (const QUrl& url : event->mimeData()->urls()) {
    if (url.isLocalFile()) {
      event->acceptProposedAction();
      emit localFileDropped(url.toLocalFile());
      return;
    }
  }
}
