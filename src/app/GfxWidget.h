#pragma once

#include <QWidget>

#include <memory>

namespace lapian::gfx {
class D3D11Device;
class SwapChain;
}  // namespace lapian::gfx

// 视频区占位控件：一个带独立 HWND 的原生子表面，由 D3D11 交换链直接呈现。
// T0.1 仅清屏；T1.3 起在这里做 NV12/P010 着色与按 display matrix 旋转。
class GfxWidget : public QWidget {
  Q_OBJECT

 public:
  explicit GfxWidget(QWidget* parent = nullptr);
  ~GfxWidget() override;

 signals:
  void deviceReady(const QString& summary);
  void deviceError(const QString& message);

 protected:
  void showEvent(QShowEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  bool EnsureReady();
  void UpdateSwapChainSize();
  void PresentFrame();

  std::unique_ptr<lapian::gfx::D3D11Device> device_;
  std::unique_ptr<lapian::gfx::SwapChain> swap_chain_;
  bool init_failed_ = false;
  bool device_lost_reported_ = false;
};
