#pragma once

#include <QSize>
#include <QWidget>

#ifndef _WIN32
#include <QImage>
#endif

#include <memory>

struct AVFrame;
struct ID3D11Device;
class QDragEnterEvent;
class QDropEvent;
class QKeyEvent;
class QMouseEvent;

namespace lapian::gfx {
class D3D11Device;
class SwapChain;
class VideoRenderer;
}  // namespace lapian::gfx

// 视频显示控件。Windows 使用独立 HWND / D3D11 呈现；
// macOS 使用 Qt QImage / QPainter 绘制 FFmpeg 软件帧，保留旋转与窗口适配。
class GfxWidget : public QWidget {
  Q_OBJECT

 public:
  explicit GfxWidget(QWidget* parent = nullptr);
  ~GfxWidget() override;

  // 接受一帧并按 display matrix 的逆时针角度呈现；macOS 转换为 QImage。
  // DP-1：返回 false 表示上传失败（例如硬件帧来自其它设备）；预览路径据此回退
  // 软件解码，report_error=false 时不弹错误横幅（回退由预览层记录）。
  bool SetVideoFrame(const AVFrame* frame, int rotation_counterclockwise_degrees,
                     bool report_error = true);
  bool IsReady() const;
  void ClearVideoFrame();
  // Windows 解码器必须使用同一个 D3D11 设备；macOS 软件路径返回 nullptr。
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
  // Windows 的原生 HWND 会挡住 Qt 窗口级 drop；视频控件接受本地文件拖放，
  // 并转发给 MainWindow::OpenFile。macOS 使用同一入口。
  void localFileDropped(const QString& path);

 protected:
  void showEvent(QShowEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

 private:
  bool EnsureReady();
  void UpdateSwapChainSize();
  void PresentFrame();

#ifdef _WIN32
  std::unique_ptr<lapian::gfx::D3D11Device> device_;
  std::unique_ptr<lapian::gfx::SwapChain> swap_chain_;
  std::unique_ptr<lapian::gfx::VideoRenderer> video_renderer_;
#else
  QImage current_frame_;
  int rotation_ = 0;
  bool ready_ = false;
#endif
  bool init_failed_ = false;
  bool device_lost_reported_ = false;
  // T7.1：SetVideoFrame 成功后置位，下一次 Present 成功时发 framePresented。
  bool present_pending_ = false;
};
