#pragma once

#include <QMainWindow>
#include <QString>

#include <memory>

class QAction;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class GfxWidget;

namespace lapian::media {
class MediaSession;
}

// T1.4 主窗口：打开入口（Ctrl+O / 拖拽 / 命令行）、空态、错误提示、首帧显示。
// 播放、时间轴、标记等仍由后续任务接入。
class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  // 打开本地文件；成功才替换当前会话，失败保留旧画面。
  void OpenFile(const QString& path);

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private slots:
  void OpenDialog();
  void OnDeviceReady(const QString& summary);
  void OnDeviceError(const QString& message);

 private:
  void ShowEmptyState();
  void ShowOpenError(const QString& message);
  void SaveCurrentStatePlaceholder();

  GfxWidget* gfx_ = nullptr;
  QLabel* status_label_ = nullptr;
  QAction* open_action_ = nullptr;
  QString base_info_;
  std::unique_ptr<lapian::media::MediaSession> session_;
  bool error_shown_ = false;
};
