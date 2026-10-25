#pragma once

#include <QStackedWidget>
#include <QString>
#include <QWidget>

#include <functional>

class QLabel;
class QProgressBar;
class QPushButton;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;

// 可以接收文件拖放的页面容器：空态/错误态/加载态都是它。
// 视频区是独立 HWND（GfxWidget 自己接 drop），空态没有 HWND，
// 所以卡片必须自己 setAcceptDrops(true)，否则空态下拖文件没反应（规划 §7.5-4）。
class StageDropPage : public QWidget {
 public:
  explicit StageDropPage(QWidget* parent = nullptr);

  void SetDropHandler(std::function<void(const QString&)> handler) {
    drop_handler_ = std::move(handler);
  }

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragLeaveEvent(QDragLeaveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private:
  std::function<void(const QString&)> drop_handler_;
};

// 画面区（规划 §1-Q1 / §7.1）：page0 = GfxWidget（独立 HWND），
// page1 = 空态 / 加载 / 错误卡片。空态隐藏 GfxWidget；已实测
// hide/show 不销毁、不重建 HWND，交换链绑定不失效。
class StageStack : public QStackedWidget {
  Q_OBJECT

 public:
  enum class Page {
    kVideo = 0,
    kEmpty = 1,
    kLoading = 2,
    kError = 3,
  };

  explicit StageStack(QWidget* parent = nullptr);

  // 把 GfxWidget 放进 page0。**不**改变可见性；调用方负责顺序
  // （先 ShowVideo + gfx->show()，再取 d3d_device()，规划 §7.5-2）。
  void SetVideoWidget(QWidget* video);
  QWidget* videoWidget() const { return video_widget_; }

  void ShowVideo();
  void ShowEmpty();
  void ShowLoading(const QString& text);
  void ShowError(const QString& title, const QString& reason);

  Page page() const;
  QString PageName() const;
  bool GfxVisible() const;

  // 空态卡片上的两个入口（打开本地 / 在线打开）。
  QPushButton* emptyOpenButton() const { return empty_open_; }
  QPushButton* emptyOnlineButton() const { return empty_online_; }
  QPushButton* errorRetryButton() const { return error_retry_; }
  QPushButton* errorQualityButton() const { return error_quality_; }
  QPushButton* errorOpenButton() const { return error_open_; }
  QPushButton* loadingCancelButton() const { return loading_cancel_; }
  QLabel* errorReasonLabel() const { return error_reason_; }
  QLabel* loadingTextLabel() const { return loading_text_; }
  void SetLoadingProgress(int percent);  // <0 隐藏进度条

 signals:
  void openLocalRequested();
  void openOnlineRequested();
  void retryRequested();
  void cancelRequested();
  void fileDropped(const QString& path);

 private:
  QWidget* video_page_ = nullptr;
  QWidget* video_widget_ = nullptr;
  StageDropPage* empty_page_ = nullptr;
  StageDropPage* loading_page_ = nullptr;
  StageDropPage* error_page_ = nullptr;
  QLabel* empty_title_ = nullptr;
  QPushButton* empty_open_ = nullptr;
  QPushButton* empty_online_ = nullptr;
  QLabel* loading_text_ = nullptr;
  QProgressBar* loading_progress_ = nullptr;
  QPushButton* loading_cancel_ = nullptr;
  QLabel* error_title_ = nullptr;
  QLabel* error_reason_ = nullptr;
  QPushButton* error_retry_ = nullptr;
  QPushButton* error_quality_ = nullptr;
  QPushButton* error_open_ = nullptr;
  Page page_ = Page::kEmpty;
};
