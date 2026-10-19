#pragma once

#include <QMainWindow>
#include <QString>

#include <memory>

struct AVFrame;

class QAction;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class GfxWidget;
class InfoBar;
class PlaybackController;

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
  // T1.5 自动化验证入口，等价于按一次 Space；不改变默认“打开不播放”。
  void StartPlaybackForTest();

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private slots:
  void OpenDialog();
  void TogglePlayback();
  void OnFrameReady(const AVFrame* frame, int rotation, int frame_index);
  void OnPlaybackStateChanged(bool playing);
  void OnPlaybackEnded();
  void OnDeviceReady(const QString& summary);
  void OnDeviceError(const QString& message);

 private:
  void ShowEmptyState();
  void ShowOpenError(const QString& message);
  void SaveCurrentStatePlaceholder();

  GfxWidget* gfx_ = nullptr;
  InfoBar* info_bar_ = nullptr;
  QLabel* status_label_ = nullptr;
  QAction* open_action_ = nullptr;
  QAction* play_action_ = nullptr;
  QString base_info_;
  std::unique_ptr<lapian::media::MediaSession> session_;
  std::unique_ptr<PlaybackController> playback_;
  bool error_shown_ = false;
};
