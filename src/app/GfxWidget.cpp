#include "app/GfxWidget.h"

#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QUrl>

#include <cmath>
#include <string>

#include "platform/gfx/D3D11Context.h"
#include "platform/gfx/D3D11Device.h"
#include "platform/gfx/SwapChain.h"
#include "platform/gfx/VideoRenderer.h"

using lapian::gfx::D3D11Device;
using lapian::gfx::DeviceInfo;
using lapian::gfx::FeatureLevelName;
using lapian::gfx::SwapChain;
using lapian::gfx::VideoRenderer;

GfxWidget::GfxWidget(QWidget* parent) : QWidget(parent) {
  // 独立 HWND + 自己绘制（D3D11 交换链），Qt 不在这个区域上画任何东西
  setAttribute(Qt::WA_NativeWindow);
  setAttribute(Qt::WA_PaintOnScreen);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAttribute(Qt::WA_NoSystemBackground);
  setAutoFillBackground(false);
  // T8.6 后续：视频区原生 HWND 覆盖主窗口后，主窗口收不到 OLE drop；
  // 必须让视频子窗口自己接受拖放并转发。
  setAcceptDrops(true);
  // T6.4：最小尺寸按逻辑像素；1080p@200% 的逻辑可用高度约 540，
  // 原 480×270 会把窗口最小高度推到屏幕可用区之外。
  setMinimumSize(320, 180);
  setFocusPolicy(Qt::StrongFocus);
}

GfxWidget::~GfxWidget() = default;

bool GfxWidget::EnsureReady() {
  if (init_failed_) {
    return false;
  }
  if (device_ && swap_chain_ && video_renderer_ && device_->IsValid() &&
      swap_chain_->IsValid()) {
    return true;
  }

  device_ = std::make_unique<D3D11Device>();
  std::string error;
  if (!device_->Create(&error)) {
    init_failed_ = true;
    qWarning() << "D3D11 设备创建失败:" << QString::fromStdString(error);
    emit deviceError(QString::fromStdString(error));
    return false;
  }

  swap_chain_ = std::make_unique<SwapChain>();
  if (!swap_chain_->Create(device_->device(), reinterpret_cast<HWND>(winId()), &error)) {
    init_failed_ = true;
    qWarning() << "D3D11 交换链创建失败:" << QString::fromStdString(error);
    emit deviceError(QString::fromStdString(error));
    return false;
  }

  video_renderer_ = std::make_unique<VideoRenderer>();
  if (!video_renderer_->Initialize(device_->device(), swap_chain_->context(), &error)) {
    init_failed_ = true;
    qWarning() << "视频渲染器初始化失败:" << QString::fromStdString(error);
    emit deviceError(QString::fromStdString(error));
    return false;
  }

  const DeviceInfo& info = device_->info();
  const QString summary = QStringLiteral("D3D11 %1 · FL %2 · %3")
                              .arg(QString::fromStdWString(info.adapter_name),
                                   QString::fromLatin1(FeatureLevelName(info.feature_level)),
                                   info.hardware ? QStringLiteral("硬件") : QStringLiteral("WARP"));
  qInfo() << "D3D11 就绪:" << summary;
  emit deviceReady(summary);
  return true;
}

void GfxWidget::UpdateSwapChainSize() {
  if (!swap_chain_ || !swap_chain_->IsValid()) {
    return;
  }
  const double ratio = devicePixelRatioF();
  const UINT pixel_width = static_cast<UINT>(std::lround(this->width() * ratio));
  const UINT pixel_height = static_cast<UINT>(std::lround(this->height() * ratio));
  std::string error;
  {
    // 交换链重建也走 D3D11 立即上下文；与截图硬解回读互斥。
    const std::lock_guard<std::recursive_mutex> lock(
        lapian::gfx::D3D11ContextMutex());
    if (!swap_chain_->Resize(pixel_width, pixel_height, &error)) {
      qWarning() << "调整交换链失败:" << QString::fromStdString(error);
    }
  }
}

void GfxWidget::PresentFrame() {
  if (!swap_chain_ || !swap_chain_->IsValid()) {
    return;
  }
  // Render + Present 都使用立即上下文，整体与截图回读串行化。
  bool presented = false;
  {
    const std::lock_guard<std::recursive_mutex> lock(lapian::gfx::D3D11ContextMutex());
    HRESULT hr = E_FAIL;
    if (video_renderer_ && video_renderer_->HasFrame()) {
      video_renderer_->Render(swap_chain_->render_target_view(), swap_chain_->width(),
                              swap_chain_->height());
      hr = swap_chain_->Present();
      presented = SUCCEEDED(hr);
    } else {
      hr = swap_chain_->Render();
    }

    if (D3D11Device::IsDeviceLost(hr)) {
      if (!device_lost_reported_) {
        device_lost_reported_ = true;
        qWarning() << "D3D11 设备丢失，销毁后等待重建";
        emit deviceError(
            QStringLiteral("显卡设备丢失（驱动重置或远程会话切换），已尝试重建。"));
      }
      video_renderer_.reset();
      swap_chain_.reset();
      device_.reset();
      update();
      return;
    }
  }
  // T7.1：一次成功 Present 后清 pending；信号在锁外发，监听者不会卡住 D3D11 上下文。
  if (presented && present_pending_) {
    present_pending_ = false;
    emit framePresented();
  }
}

bool GfxWidget::SetVideoFrame(const AVFrame* frame, int rotation_counterclockwise_degrees,
                              bool report_error) {
  if (!EnsureReady() || !video_renderer_) {
    return false;
  }
  std::string error;
  bool uploaded = false;
  {
    // 硬件帧拷贝/软解上传/可能的回读都在这里发生，不能被截图线程打断。
    const std::lock_guard<std::recursive_mutex> lock(
        lapian::gfx::D3D11ContextMutex());
    uploaded = video_renderer_->SetFrame(frame, rotation_counterclockwise_degrees, &error);
  }
  if (!uploaded) {
    if (report_error) {
      qWarning() << "设置视频帧失败:" << QString::fromStdString(error);
      emit deviceError(QString::fromStdString(error));
    }
    return false;
  }
  present_pending_ = true;
  update();
  return true;
}

void GfxWidget::ClearVideoFrame() {
  if (video_renderer_) {
    video_renderer_->ClearFrame();
  }
  update();
}

ID3D11Device* GfxWidget::d3d_device() {
  if (!EnsureReady() || !device_) {
    return nullptr;
  }
  return device_->device();
}

QSize GfxWidget::swapChainSize() const {
  if (!swap_chain_ || !swap_chain_->IsValid()) {
    return {};
  }
  return QSize(static_cast<int>(swap_chain_->width()),
               static_cast<int>(swap_chain_->height()));
}

void GfxWidget::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (EnsureReady()) {
    UpdateSwapChainSize();
    PresentFrame();
  }
}

void GfxWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  if (EnsureReady()) {
    PresentFrame();
  }
}

void GfxWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (swap_chain_) {
    UpdateSwapChainSize();
    update();
  }
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
  // 步进/首末帧已由 MainWindow 的窗口级事件过滤器统一处理（T6.2），
  // 这样焦点在时间轴/侧栏/状态栏时同样可用，并只有一个连发源。
  QWidget::keyPressEvent(event);
}

void GfxWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  // T6.1：双击画面切换全屏。单击路径不受影响（拖动/焦点仍是 QWidget 默认）。
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
