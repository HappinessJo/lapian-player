#include "app/GfxWidget.h"

#include <QDebug>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>

#include <cmath>
#include <string>

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
  setMinimumSize(480, 270);
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
  if (!swap_chain_->Resize(pixel_width, pixel_height, &error)) {
    qWarning() << "调整交换链失败:" << QString::fromStdString(error);
  }
}

void GfxWidget::PresentFrame() {
  if (!swap_chain_ || !swap_chain_->IsValid()) {
    return;
  }
  HRESULT hr = E_FAIL;
  if (video_renderer_ && video_renderer_->HasFrame()) {
    video_renderer_->Render(swap_chain_->render_target_view(), swap_chain_->width(),
                            swap_chain_->height());
    hr = swap_chain_->Present();
  } else {
    hr = swap_chain_->Render();
  }

  if (D3D11Device::IsDeviceLost(hr)) {
    if (!device_lost_reported_) {
      device_lost_reported_ = true;
      qWarning() << "D3D11 设备丢失，销毁后等待重建";
      emit deviceError(QStringLiteral("显卡设备丢失（驱动重置或远程会话切换），已尝试重建。"));
    }
    video_renderer_.reset();
    swap_chain_.reset();
    device_.reset();
    update();
  }
}

void GfxWidget::SetVideoFrame(const AVFrame* frame, int rotation_counterclockwise_degrees) {
  if (!EnsureReady() || !video_renderer_) {
    return;
  }
  std::string error;
  if (!video_renderer_->SetFrame(frame, rotation_counterclockwise_degrees, &error)) {
    qWarning() << "设置视频帧失败:" << QString::fromStdString(error);
    emit deviceError(QString::fromStdString(error));
    return;
  }
  update();
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
