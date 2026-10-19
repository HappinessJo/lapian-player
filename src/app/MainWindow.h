#pragma once

#include <QMainWindow>
#include <QString>

class QLabel;
class GfxWidget;

// T0.1 主窗口：空窗口 + 状态栏环境信息。
// 不加载媒体、不建时间轴、不写任何 F1–F8 行为。
class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

  // T1.3 临时入口：命令行 --file 打开并显示首帧；T1.4 会替换为正式打开流程。
  void LoadFileForTest(const QString& path);

 private slots:
  void OnDeviceReady(const QString& summary);
  void OnDeviceError(const QString& message);

 private:
  GfxWidget* gfx_ = nullptr;
  QLabel* status_label_ = nullptr;
  QString base_info_;
  bool error_shown_ = false;
};
