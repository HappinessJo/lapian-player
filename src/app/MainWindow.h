#pragma once

#include <QMainWindow>
#include <QString>

#include <cstdint>
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
  // 跳到 0 基帧号；成功时画面/信息栏同步。T2.4/T2.6 共用。
  void GoToFrame(std::int64_t frame);
  // T1.5 自动化验证入口，等价于按一次 Space；不改变默认“打开不播放”。
  void StartPlaybackForTest();
  // T2.6 自动化验证入口：与 Ctrl+G 输入框使用同一套解析。
  void GoToInputForTest(const QString& text);

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private slots:
  void OpenDialog();
  void ShowGotoDialog();
  void StepBackward();
  void StepForward();
  void TogglePlayback();
  void OnFrameReady(const AVFrame* frame, int rotation, int frame_index);
  void OnPlaybackStateChanged(bool playing);
  void OnPlaybackEnded();
  void OnDeviceReady(const QString& summary);
  void OnDeviceError(const QString& message);

 private:
  void ShowEmptyState();
  bool ApplyGotoText(const QString& text);
  void ShowOpenError(const QString& message);
  void SaveCurrentStatePlaceholder();

  GfxWidget* gfx_ = nullptr;
  InfoBar* info_bar_ = nullptr;
  QLabel* status_label_ = nullptr;
  QAction* open_action_ = nullptr;
  QAction* play_action_ = nullptr;
  QAction* prev_action_ = nullptr;
  QAction* next_action_ = nullptr;
  QAction* goto_action_ = nullptr;
  QString base_info_;
  std::unique_ptr<lapian::media::MediaSession> session_;
  std::unique_ptr<PlaybackController> playback_;
  std::int64_t current_frame_ = 0;
  bool error_shown_ = false;
};
