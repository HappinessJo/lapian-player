#pragma once

#include <QSize>
#include <QWidget>

#include <memory>

struct AVFrame;
struct ID3D11Device;
class QKeyEvent;
class QMouseEvent;

namespace lapian::gfx {
class D3D11Device;
class SwapChain;
class VideoRenderer;
}  // namespace lapian::gfx

// 视频区占位控件：一个带独立 HWND 的原生子表面，由 D3D11 交换链直接呈现。
// T0.1 仅清屏；T1.3 起在这里做 NV12/P010 着色与按 display matrix 旋转。
class GfxWidget : public QWidget {
  Q_OBJECT

 public:
  explicit GfxWidget(QWidget* parent = nullptr);
  ~GfxWidget() override;

  // T1.3：把解码器的一帧复制到 GPU 并呈现；rotation 为 display matrix 的逆时针角度。
  void SetVideoFrame(const AVFrame* frame, int rotation_counterclockwise_degrees);
  void ClearVideoFrame();
  // T1.2/T1.3：解码器必须用同一个 D3D11 设备，硬件帧才能跨线程/模块拷贝。
  ID3D11Device* d3d_device();
  // T6.1：交换链后备缓冲的实际像素尺寸（逻辑尺寸 × devicePixelRatio）。
  // 全屏切换后用它确认交换链跟随了控件 resize；未初始化/设备丢失时返回空。
  QSize swapChainSize() const;

 signals:
  void deviceReady(const QString& summary);
  void deviceError(const QString& message);
  void playPauseRequested();
  void gotoRequested();
  // T6.1：双击画面进入/退出全屏；单击仍走 QWidget 默认（不吞拖动/焦点）。
  void fullScreenToggleRequested();
  // T7.1：一帧真正走到 Present 之后发一次；用于冷启动 P2 的“首帧呈现”埋点。
  void framePresented();

 protected:
  void showEvent(QShowEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

 private:
  bool EnsureReady();
  void UpdateSwapChainSize();
  void PresentFrame();

  std::unique_ptr<lapian::gfx::D3D11Device> device_;
  std::unique_ptr<lapian::gfx::SwapChain> swap_chain_;
  std::unique_ptr<lapian::gfx::VideoRenderer> video_renderer_;
  bool init_failed_ = false;
  bool device_lost_reported_ = false;
  // T7.1：SetVideoFrame 成功后置位，下一次 Present 成功时发 framePresented。
  bool present_pending_ = false;
};
