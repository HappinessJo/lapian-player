#include "app/ui/StageStack.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

#include "app/ui/Theme.h"
#include "app/ui/Tokens.h"

namespace {

QString FirstLocalFile(const QMimeData* mime) {
  if (mime == nullptr || !mime->hasUrls()) {
    return QString();
  }
  for (const QUrl& url : mime->urls()) {
    if (url.isLocalFile()) {
      return url.toLocalFile();
    }
  }
  return QString();
}

}  // namespace

StageDropPage::StageDropPage(QWidget* parent) : QWidget(parent) {
  setAcceptDrops(true);
}

void StageDropPage::dragEnterEvent(QDragEnterEvent* event) {
  if (!FirstLocalFile(event == nullptr ? nullptr : event->mimeData()).isEmpty()) {
    event->acceptProposedAction();
    return;
  }
  QWidget::dragEnterEvent(event);
}

void StageDropPage::dragLeaveEvent(QDragLeaveEvent* event) {
  QWidget::dragLeaveEvent(event);
}

void StageDropPage::dropEvent(QDropEvent* event) {
  const QString path = FirstLocalFile(event == nullptr ? nullptr : event->mimeData());
  if (!path.isEmpty()) {
    event->acceptProposedAction();
    if (drop_handler_) {
      drop_handler_(path);
    }
    return;
  }
  QWidget::dropEvent(event);
}

StageStack::StageStack(QWidget* parent) : QStackedWidget(parent) {
  // 老壳的 stage 容器叫 videoStage；测试按这个名字找画面区容器（规划 §7.7）。
  setObjectName(QStringLiteral("videoStage"));
  lapian::ui::SetStyledBackground(this);
  setMinimumSize(0, 0);

  // page0：视频区（GfxWidget 在 MainWindow 里塞进来）
  video_page_ = new QWidget(this);
  video_page_->setObjectName(QStringLiteral("v2StageVideo"));
  auto* video_layout = new QVBoxLayout(video_page_);
  video_layout->setContentsMargins(0, 0, 0, 0);
  video_layout->setSpacing(0);
  addWidget(video_page_);

  auto make_page = [this](const char* name) {
    auto* page = new StageDropPage(this);
    page->setObjectName(QString::fromLatin1(name));
    // 空/加载/错误卡片不能把自己的最小高度灌给 QStackedWidget：否则 200% DPI
    // 下即使停在视频页，卡片的最小尺寸也会把画面列顶高、把时间轴挤到 123px。
    // 卡片内部靠布局自行收缩（极端高度由 --dpi-test 的 gfx/timeline 断言兜底）。
    page->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    page->setMinimumSize(0, 0);
    page->SetDropHandler([this](const QString& path) { emit fileDropped(path); });
    lapian::ui::SetStyledBackground(page);
    addWidget(page);
    return page;
  };

  // page1：空态
  empty_page_ = make_page("v2StageEmpty");
  {
    auto* outer = new QVBoxLayout(empty_page_);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->setSpacing(12);
    outer->addStretch(1);
    empty_title_ = new QLabel(QStringLiteral("把片子拖进来，或从这里打开"),
                              empty_page_);
    empty_title_->setObjectName(QStringLiteral("v2EmptyTitle"));
    empty_title_->setAlignment(Qt::AlignCenter);
    empty_title_->setFont(lapian::ui::UiFont(15.0, QFont::DemiBold));
    outer->addWidget(empty_title_);

    auto* hint = new QLabel(
        QStringLiteral("保证档：1080p · H.264/H.265 · 8-bit SDR · ≤60fps · 本地零网络"),
        empty_page_);
    hint->setObjectName(QStringLiteral("v2EmptyHint"));
    hint->setAlignment(Qt::AlignCenter);

    outer->addWidget(hint);

    auto* row = new QHBoxLayout();
    row->setSpacing(8);
    row->addStretch(1);
    empty_open_ = new QPushButton(QStringLiteral("打开本地文件"), empty_page_);
    empty_open_->setObjectName(QStringLiteral("v2EmptyOpen"));

    empty_open_->setFocusPolicy(Qt::NoFocus);
    empty_open_->setToolTip(QStringLiteral("打开本地文件（Ctrl+O）"));
    row->addWidget(empty_open_);
    empty_online_ = new QPushButton(QStringLiteral("在线打开"), empty_page_);
    empty_online_->setObjectName(QStringLiteral("v2EmptyOnline"));

    empty_online_->setFocusPolicy(Qt::NoFocus);
    empty_online_->setToolTip(QStringLiteral("在线打开（Ctrl+U）"));
    row->addWidget(empty_online_);
    row->addStretch(1);
    outer->addLayout(row);
    outer->addStretch(1);
    connect(empty_open_, &QPushButton::clicked, this,
            &StageStack::openLocalRequested);
    connect(empty_online_, &QPushButton::clicked, this,
            &StageStack::openOnlineRequested);
  }

  // page2：加载态
  loading_page_ = make_page("v2StageLoading");
  {
    auto* outer = new QVBoxLayout(loading_page_);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->setSpacing(10);
    outer->addStretch(1);
    loading_text_ = new QLabel(QStringLiteral("正在获取播放地址…"), loading_page_);
    loading_text_->setObjectName(QStringLiteral("v2LoadingText"));
    loading_text_->setAlignment(Qt::AlignCenter);

    outer->addWidget(loading_text_);
    loading_progress_ = new QProgressBar(loading_page_);
    loading_progress_->setObjectName(QStringLiteral("v2LoadingProgress"));
    loading_progress_->setRange(0, 100);
    loading_progress_->setValue(0);
    loading_progress_->setTextVisible(false);
    loading_progress_->setFixedHeight(6);
    loading_progress_->setMaximumWidth(260);
    loading_progress_->setVisible(false);
    auto* progress_row = new QHBoxLayout();
    progress_row->addStretch(1);
    progress_row->addWidget(loading_progress_);
    progress_row->addStretch(1);
    outer->addLayout(progress_row);
    loading_cancel_ = new QPushButton(QStringLiteral("取消"), loading_page_);
    loading_cancel_->setObjectName(QStringLiteral("v2LoadingCancel"));

    loading_cancel_->setFocusPolicy(Qt::NoFocus);
    loading_cancel_->setToolTip(QStringLiteral("取消在线加载（Ctrl+Shift+U）"));
    auto* cancel_row = new QHBoxLayout();
    cancel_row->addStretch(1);
    cancel_row->addWidget(loading_cancel_);
    cancel_row->addStretch(1);
    outer->addLayout(cancel_row);
    outer->addStretch(1);
    connect(loading_cancel_, &QPushButton::clicked, this,
            &StageStack::cancelRequested);
  }

  // page3：错误态
  error_page_ = make_page("v2StageError");
  {
    auto* outer = new QVBoxLayout(error_page_);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->setSpacing(10);
    outer->addStretch(1);
    error_title_ = new QLabel(QStringLiteral("这条打不开"), error_page_);
    error_title_->setObjectName(QStringLiteral("v2ErrorTitle"));
    error_title_->setAlignment(Qt::AlignCenter);
    error_title_->setFont(lapian::ui::UiFont(15.0, QFont::DemiBold));
    outer->addWidget(error_title_);
    error_reason_ = new QLabel(QString(), error_page_);
    error_reason_->setObjectName(QStringLiteral("v2ErrorReason"));
    error_reason_->setAlignment(Qt::AlignCenter);
    error_reason_->setWordWrap(true);

    outer->addWidget(error_reason_);
    auto* row = new QHBoxLayout();
    row->setSpacing(8);
    row->addStretch(1);
    auto add_button = [&row, this](const char* name, const QString& text,
                                   const QString& tip) {
      auto* button = new QPushButton(text, error_page_);
      button->setObjectName(QString::fromLatin1(name));

      button->setFocusPolicy(Qt::NoFocus);
      button->setToolTip(tip);
      row->addWidget(button);
      return button;
    };
    error_retry_ = add_button("v2ErrorRetry", QStringLiteral("重试"),
                              QStringLiteral("重新打开（本地文件走打开对话框）"));
    error_quality_ = add_button("v2ErrorQuality", QStringLiteral("换清晰度"),
                                QStringLiteral("在线打开并选择其他清晰度"));
    error_open_ = add_button("v2ErrorOpenLocal", QStringLiteral("打开本地文件"),
                             QStringLiteral("打开本地文件（Ctrl+O）"));
    row->addStretch(1);
    outer->addLayout(row);
    outer->addStretch(1);
    connect(error_retry_, &QPushButton::clicked, this,
            &StageStack::retryRequested);
    connect(error_quality_, &QPushButton::clicked, this,
            &StageStack::openOnlineRequested);
    connect(error_open_, &QPushButton::clicked, this,
            &StageStack::openLocalRequested);
  }

  ShowEmpty();
}

void StageStack::SetVideoWidget(QWidget* video) {
  if (video == nullptr || video_page_ == nullptr) {
    return;
  }
  auto* layout = qobject_cast<QVBoxLayout*>(video_page_->layout());
  if (layout == nullptr) {
    return;
  }
  video_widget_ = video;
  layout->addWidget(video, 1);
}

void StageStack::ShowVideo() {
  page_ = Page::kVideo;
  setCurrentWidget(video_page_);
  // 先显示画面页（GfxWidget 的 showEvent → EnsureReady），再让调用方取
  // d3d_device()。顺序反了会拿隐藏窗口的 HWND 建交换链（规划 §7.5-2）。
  if (video_widget_ != nullptr) {
    video_widget_->show();
  }
}

void StageStack::ShowEmpty() {
  page_ = Page::kEmpty;
  setCurrentWidget(empty_page_);
}

void StageStack::ShowLoading(const QString& text) {
  page_ = Page::kLoading;
  if (loading_text_ != nullptr) {
    loading_text_->setText(text.isEmpty() ? QStringLiteral("正在加载…") : text);
  }
  setCurrentWidget(loading_page_);
}

void StageStack::ShowError(const QString& title, const QString& reason) {
  page_ = Page::kError;
  if (error_title_ != nullptr && !title.isEmpty()) {
    error_title_->setText(title);
  }
  if (error_reason_ != nullptr) {
    error_reason_->setText(reason);
    error_reason_->setToolTip(reason);
  }
  setCurrentWidget(error_page_);
}

StageStack::Page StageStack::page() const { return page_; }

QString StageStack::PageName() const {
  switch (page_) {
    case Page::kVideo:
      return QStringLiteral("video");
    case Page::kEmpty:
      return QStringLiteral("empty");
    case Page::kLoading:
      return QStringLiteral("loading");
    case Page::kError:
      return QStringLiteral("error");
  }
  return QStringLiteral("unknown");
}

bool StageStack::GfxVisible() const {
  return page_ == Page::kVideo && video_widget_ != nullptr &&
         video_widget_->isVisible();
}

void StageStack::SetLoadingProgress(int percent) {
  if (loading_progress_ == nullptr) {
    return;
  }
  loading_progress_->setVisible(percent >= 0);
  if (percent >= 0) {
    loading_progress_->setValue(qBound(0, percent, 100));
  }
}
