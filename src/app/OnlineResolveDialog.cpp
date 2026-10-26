#include "app/OnlineResolveDialog.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>

#include "online/BiliResolveClient.h"
#include "online/QrLoginController.h"
#include "online/Sessdata.h"
#include "online/WinHttpClient.h"

namespace {

// 3D：对话框进主题。语义色走全局 QSS 的属性选择器，不再 per-widget setStyleSheet。
void SetStateProperty(QWidget* widget, const char* state) {
  if (widget == nullptr) {
    return;
  }
  widget->setProperty("state", QString::fromLatin1(state));
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
}

QString FormatDuration(qint64 seconds) {
  if (seconds < 0) {
    seconds = 0;
  }
  const qint64 hours = seconds / 3600;
  const qint64 minutes = (seconds % 3600) / 60;
  const qint64 secs = seconds % 60;
  if (hours > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'));
  }
  return QStringLiteral("%1:%2")
      .arg(minutes, 2, 10, QLatin1Char('0'))
      .arg(secs, 2, 10, QLatin1Char('0'));
}

}  // namespace

OnlineResolveDialog::OnlineResolveDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(QStringLiteral("在线打开（BV / B 站视频链接）"));
  setModal(false);
  // P2：最小高度必须低于 150%/200% 的可用逻辑高度，否则窗口无法被夹到屏幕内；
  // 内容超高时由 QScrollArea 滚动（二维码/刷新/取消仍保证可见）。
  setMinimumSize(560, 420);
  http_ = std::make_unique<lapian::online::WinHttpClient>();
  SetupUi();
  ReloadCredentialState();
  AdjustToContent();
}

OnlineResolveDialog::~OnlineResolveDialog() {
  if (qr_controller_ != nullptr) {
    qr_controller_->Close();
  }
  if (cancel_ != nullptr) {
    cancel_->Cancel();
  }
  if (auth_cancel_ != nullptr) {
    auth_cancel_->Cancel();
  }
  if (worker_.joinable()) {
    worker_.join();
  }
  if (auth_worker_.joinable()) {
    auth_worker_.join();
  }
}

void OnlineResolveDialog::SetupUi() {
  // P2：主体放进 QScrollArea——100%/125% 时窗口按内容长高，150%/200% 可用高度
  // 不够时出现滚动条，而不是把二维码/刷新/取消裁掉。
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  scroll_area_ = new QScrollArea(this);
  scroll_area_->setObjectName(QStringLiteral("onlineScroll"));
  scroll_area_->setWidgetResizable(true);
  scroll_area_->setFrameShape(QFrame::NoFrame);
  scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll_content_ = new QWidget(scroll_area_);
  scroll_content_->setObjectName(QStringLiteral("onlineScrollContent"));
  auto* root = new QVBoxLayout(scroll_content_);
  root->setContentsMargins(14, 12, 14, 12);
  root->setSpacing(8);
  scroll_area_->setWidget(scroll_content_);
  outer->addWidget(scroll_area_);

  auto* intro = new QLabel(
      QStringLiteral("输入 BV 号或 B 站视频链接（支持 ?p=2）。解析只读取公开元数据，"
                     "不取流、不播放。请核对标题/UP/时长/分P 后再确认。"),
      this);
  intro->setWordWrap(true);
  root->addWidget(intro);

  auto* privacy_row = new QHBoxLayout();
  privacy_label_ = new QLabel(
      QStringLiteral(
          "隐私说明：在线模式只在你提交 BV/URL 后访问 api.bilibili.com；"
          "点击“扫码登录”时才访问 passport.bilibili.com 的官方二维码接口"
          "（generate/poll，不带 Cookie，本地渲染）。"
          "SESSDATA 可选用 Windows DPAPI 加密保存在 "
          "%APPDATA%\\拉片软件\\credentials.dat，只发送给 api.bilibili.com，"
          "不写入日志/缓存/截图；可随时点“清除”。"
          "在线缓存默认在 %LOCALAPPDATA%\\拉片软件\\cache（可在设置中清空）；"
          "无遥测、无自动更新、无崩溃上报。本地模式不联网。"),
      this);
  privacy_label_->setObjectName(QStringLiteral("onlinePrivacyNotice"));
  privacy_label_->setWordWrap(true);
  privacy_label_->setTextFormat(Qt::PlainText);
  privacy_hide_button_ = new QPushButton(QStringLiteral("隐藏说明"), this);
  privacy_hide_button_->setObjectName(QStringLiteral("onlinePrivacyHide"));
  privacy_row->addWidget(privacy_label_, 1);
  privacy_row->addWidget(privacy_hide_button_, 0, Qt::AlignTop);
  root->addLayout(privacy_row);

  auto* input_row = new QHBoxLayout();
  input_ = new QLineEdit(this);
  input_->setObjectName(QStringLiteral("onlineInput"));
  input_->setPlaceholderText(
      QStringLiteral("BV1... 或 https://www.bilibili.com/video/BV...?p=2"));
  input_->setClearButtonEnabled(true);
  input_row->addWidget(input_, 1);
  resolve_button_ = new QPushButton(QStringLiteral("解析"), this);
  resolve_button_->setObjectName(QStringLiteral("onlineResolveButton"));
  input_row->addWidget(resolve_button_);
  root->addLayout(input_row);

  status_label_ = new QLabel(QStringLiteral("尚未解析"), this);
  status_label_->setObjectName(QStringLiteral("onlineStatus"));
  status_label_->setWordWrap(true);
  root->addWidget(status_label_);

  SetupCredentialUi(root);

  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  title_label_ = new QLabel(this);
  title_label_->setObjectName(QStringLiteral("onlineTitle"));
  title_label_->setWordWrap(true);
  title_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  owner_label_ = new QLabel(this);
  owner_label_->setObjectName(QStringLiteral("onlineOwner"));
  owner_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  total_duration_label_ = new QLabel(this);
  total_duration_label_->setObjectName(QStringLiteral("onlineTotalDuration"));
  page_combo_ = new QComboBox(this);
  page_combo_->setObjectName(QStringLiteral("onlinePageCombo"));
  page_combo_->setEnabled(false);
  page_info_label_ = new QLabel(this);
  page_info_label_->setObjectName(QStringLiteral("onlinePageInfo"));
  page_info_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  page_duration_label_ = new QLabel(this);
  page_duration_label_->setObjectName(QStringLiteral("onlinePageDuration"));
  quality_label_ = new QLabel(this);
  quality_label_->setObjectName(QStringLiteral("onlineQualityHint"));
  quality_label_->setWordWrap(true);

  form->addRow(QStringLiteral("标题："), title_label_);
  form->addRow(QStringLiteral("UP主："), owner_label_);
  form->addRow(QStringLiteral("总时长："), total_duration_label_);
  form->addRow(QStringLiteral("分P："), page_combo_);
  form->addRow(QStringLiteral("当前："), page_info_label_);
  form->addRow(QStringLiteral("分P时长："), page_duration_label_);
  form->addRow(QStringLiteral("清晰度："), quality_label_);
  root->addLayout(form);

  error_label_ = new QLabel(this);
  error_label_->setObjectName(QStringLiteral("onlineError"));
  error_label_->setWordWrap(true);
  error_label_->hide();
  root->addWidget(error_label_);

  root->addStretch(1);

  auto* buttons = new QHBoxLayout();
  buttons->addStretch(1);
  cancel_button_ = new QPushButton(QStringLiteral("关闭"), this);
  cancel_button_->setObjectName(QStringLiteral("onlineCancelButton"));
  confirm_button_ = new QPushButton(QStringLiteral("确认"), this);
  confirm_button_->setObjectName(QStringLiteral("onlineConfirmButton"));
  confirm_button_->setToolTip(
      QStringLiteral("确认后在线取流并播放；若该 BV 已完整缓存，则直接打开本地缓存（0 网络）"));
  buttons->addWidget(cancel_button_);
  buttons->addWidget(confirm_button_);
  root->addLayout(buttons);

  poll_timer_ = new QTimer(this);
  poll_timer_->setInterval(20);
  connect(poll_timer_, &QTimer::timeout, this, &OnlineResolveDialog::PollResolve);
  connect(resolve_button_, &QPushButton::clicked, this, &OnlineResolveDialog::StartResolve);
  connect(input_, &QLineEdit::returnPressed, this, &OnlineResolveDialog::StartResolve);
  connect(page_combo_, &QComboBox::currentIndexChanged, this,
          &OnlineResolveDialog::OnPageChanged);
  connect(confirm_button_, &QPushButton::clicked, this, &OnlineResolveDialog::OnConfirm);
  connect(cancel_button_, &QPushButton::clicked, this, &OnlineResolveDialog::CancelResolve);
  setTabOrder(input_, resolve_button_);
  setTabOrder(resolve_button_, page_combo_);
  setTabOrder(page_combo_, confirm_button_);
  UpdateConfirmState();
}

void OnlineResolveDialog::SetupCredentialUi(QVBoxLayout* root) {
  auto* box = new QGroupBox(QStringLiteral("登录（可选）"), this);
  auto* box_layout = new QVBoxLayout(box);

  auth_status_label_ = new QLabel(QStringLiteral("未配置凭据：按未登录处理"), box);
  auth_status_label_->setObjectName(QStringLiteral("onlineAuthStatus"));
  auth_status_label_->setWordWrap(true);
  auth_status_label_->setTextFormat(Qt::PlainText);
  box_layout->addWidget(auth_status_label_);

  auto* row = new QHBoxLayout();
  credential_input_ = new QLineEdit(box);
  credential_input_->setObjectName(QStringLiteral("onlineCredentialInput"));
  credential_input_->setEchoMode(QLineEdit::Password);
  credential_input_->setPlaceholderText(
      QStringLiteral("粘贴 SESSDATA（只接受这一项 Cookie）"));
  credential_input_->setClearButtonEnabled(true);
  credential_input_->setMaxLength(1024);
  credential_save_button_ = new QPushButton(QStringLiteral("保存"), box);
  credential_save_button_->setObjectName(QStringLiteral("onlineCredentialSave"));
  credential_clear_button_ = new QPushButton(QStringLiteral("清除"), box);
  credential_clear_button_->setObjectName(QStringLiteral("onlineCredentialClear"));
  row->addWidget(credential_input_, 1);
  row->addWidget(credential_save_button_);
  row->addWidget(credential_clear_button_);
  box_layout->addLayout(row);

  auto* hint = new QLabel(
      QStringLiteral("只接受 SESSDATA；保存/清除都不会立即联网，首次提交 BV 时才检查"
                     "登录状态。cookie 只发 api.bilibili.com，不跟随重定向。"),
      box);
  hint->setWordWrap(true);
  hint->setTextFormat(Qt::PlainText);
  hint->setObjectName(QStringLiteral("onlineCredentialHint"));
  box_layout->addWidget(hint);

  // T8.2.2：扫码是并存的第二条入口；粘贴入口永远是主路径，不删除/不降级。
  auto* qr_row = new QHBoxLayout();
  auto* qr_hint = new QLabel(
      QStringLiteral("或使用 B 站手机客户端扫码登录（官方网页二维码，本地渲染）："),
      box);
  qr_hint->setWordWrap(true);
  qr_hint->setTextFormat(Qt::PlainText);
  qr_hint->setObjectName(QStringLiteral("onlineQrHint"));
  qr_start_button_ = new QPushButton(QStringLiteral("扫码登录"), box);
  qr_start_button_->setObjectName(QStringLiteral("onlineQrStart"));
  qr_row->addWidget(qr_hint, 1);
  qr_row->addWidget(qr_start_button_);
  box_layout->addLayout(qr_row);

  qr_panel_ = new QWidget(box);
  qr_panel_->setObjectName(QStringLiteral("onlineQrPanel"));
  auto* qr_panel_layout = new QVBoxLayout(qr_panel_);
  qr_panel_layout->setContentsMargins(0, 0, 0, 0);
  qr_image_label_ = new QLabel(qr_panel_);
  qr_image_label_->setObjectName(QStringLiteral("onlineQrImage"));
  qr_image_label_->setFixedSize(240, 240);
  qr_image_label_->setAlignment(Qt::AlignCenter);
  qr_image_label_->setToolTip(
      QStringLiteral("用 B 站手机客户端扫描；本图由本地渲染，不联网加载"));
  qr_panel_layout->addWidget(qr_image_label_, 0, Qt::AlignHCenter);
  qr_status_label_ = new QLabel(QStringLiteral("尚未开始扫码"), qr_panel_);
  qr_status_label_->setObjectName(QStringLiteral("onlineQrStatus"));
  qr_status_label_->setWordWrap(true);
  qr_status_label_->setTextFormat(Qt::PlainText);
  qr_panel_layout->addWidget(qr_status_label_);
  auto* qr_buttons = new QHBoxLayout();
  qr_buttons->addStretch(1);
  qr_refresh_button_ = new QPushButton(QStringLiteral("刷新二维码"), qr_panel_);
  qr_refresh_button_->setObjectName(QStringLiteral("onlineQrRefresh"));
  qr_cancel_button_ = new QPushButton(QStringLiteral("取消扫码"), qr_panel_);
  qr_cancel_button_->setObjectName(QStringLiteral("onlineQrCancel"));
  qr_buttons->addWidget(qr_refresh_button_);
  qr_buttons->addWidget(qr_cancel_button_);
  qr_panel_layout->addLayout(qr_buttons);
  box_layout->addWidget(qr_panel_);
  qr_panel_->hide();

  root->addWidget(box);

  connect(credential_save_button_, &QPushButton::clicked, this,
          &OnlineResolveDialog::OnSaveCredential);
  connect(credential_clear_button_, &QPushButton::clicked, this,
          &OnlineResolveDialog::OnClearCredential);
  connect(credential_input_, &QLineEdit::returnPressed, this,
          &OnlineResolveDialog::OnSaveCredential);
  connect(privacy_hide_button_, &QPushButton::clicked, this,
          &OnlineResolveDialog::OnHidePrivacy);
  connect(qr_start_button_, &QPushButton::clicked, this,
          &OnlineResolveDialog::OnStartQrLogin);
  connect(qr_refresh_button_, &QPushButton::clicked, this,
          &OnlineResolveDialog::OnQrRefresh);
  connect(qr_cancel_button_, &QPushButton::clicked, this,
          &OnlineResolveDialog::OnQrCancel);
}

void OnlineResolveDialog::AdjustToContent() {
  QScreen* screen = this->screen();
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  const QRect avail =
      screen == nullptr ? QRect() : screen->availableGeometry();
  int max_w = 900;
  int max_h = 900;
  if (!avail.isEmpty()) {
    max_w = std::max(320, avail.width() - 24);
    max_h = std::max(320, avail.height() - 48);
  }
  // QScrollArea 不会把内容的 sizeHint 传给顶层窗口；先按目标宽度摆一次，再按
  // 内容实际高度定窗口（100%/125% 一屏放下，150%/200% 夹到可用高并滚动）。
  const int preferred_w =
      std::min(max_w, std::max(560, std::min(720, width())));
  resize(preferred_w, height());
  updateGeometry();
  if (layout() != nullptr) {
    layout()->activate();
  }
  if (scroll_content_ != nullptr && scroll_content_->layout() != nullptr) {
    scroll_content_->layout()->activate();
  }
  int content_h = scroll_content_ == nullptr ? 0 : scroll_content_->sizeHint().height();
  if (scroll_content_ != nullptr && scroll_content_->layout() != nullptr) {
    content_h = std::max(content_h,
                         scroll_content_->layout()->sizeHint().height());
  }
  const int target_h = std::clamp(content_h + 4, 420, max_h);
  resize(preferred_w, target_h);
  if (layout() != nullptr) {
    layout()->activate();
  }
  if (scroll_content_ != nullptr && scroll_content_->layout() != nullptr) {
    scroll_content_->layout()->activate();
  }
  // 位置也夹回屏幕内（缩放后 frame 可能越界）。
  const QRect frame = frameGeometry();
  int x = frame.x();
  int y = frame.y();
  bool moved = false;
  if (frame.right() > avail.right()) {
    x -= frame.right() - avail.right();
    moved = true;
  }
  if (frame.bottom() > avail.bottom()) {
    y -= frame.bottom() - avail.bottom();
    moved = true;
  }
  if (x < avail.left()) {
    x = avail.left();
    moved = true;
  }
  if (y < avail.top()) {
    y = avail.top();
    moved = true;
  }
  if (moved) {
    move(x, y);
  }
  // 用户刚点“扫码登录/刷新”：把二维码面板滚进视口（小屏时顶部说明可滚走）。
  if (scroll_area_ != nullptr && qr_panel_ != nullptr &&
      qr_panel_->isVisible()) {
    scroll_area_->ensureWidgetVisible(qr_panel_, 0, 0);
    // 布局尚未完成时 ensureWidgetVisible 可能落在旧几何上；下一轮事件循环再滚一次。
    QTimer::singleShot(0, this, [this]() {
      if (scroll_area_ != nullptr && qr_panel_ != nullptr &&
          qr_panel_->isVisible()) {
        scroll_area_->ensureWidgetVisible(qr_panel_, 0, 0);
      }
    });
  }
}

void OnlineResolveDialog::StartResolve() {
  if (resolving_) {
    return;
  }

  ResetResult();
  last_http_requests_ = 0;
  last_resolve_ms_ = 0;
  last_cache_hit_ = false;

  QElapsedTimer local_timer;
  local_timer.start();
  const lapian::online::BiliInput input =
      lapian::online::NormalizeInput(input_ == nullptr ? QString() : input_->text());
  last_local_validation_ms_ = local_timer.elapsed();

  if (!input.ok) {
    // 本地非法：不创建 HTTP 请求、不启动线程；错误立即可读（O1-A2）。
    ShowError(input.error);
    status_label_->setText(QStringLiteral("本地校验未通过：未发起网络请求"));
    emit resolveCompleted(false);
    return;
  }

  // T8.7：已完整缓存的同 BV 优先离线打开；这一步只读磁盘，不发任何请求。
  // 带 ?p=N（N>1）的输入不做缓存探测：缓存 meta 没有分P 号，不能用猜测的
  // 分P 覆盖用户明确选择的页。
  if (cache_probe_ && (!input.page_from_input || input.page == 1)) {
    OnlineCacheSummary summary;
    QString probe_error;
    QElapsedTimer cache_timer;
    cache_timer.start();
    if (cache_probe_(input.bvid, &summary, &probe_error) && summary.cid > 0) {
      lapian::online::BiliVideo video;
      video.bvid = input.bvid;
      video.title = summary.title.isEmpty() ? input.bvid : summary.title;
      video.owner = summary.owner;
      video.duration_seconds = summary.duration_ms / 1000;
      lapian::online::BiliPage page;
      page.page = 1;
      page.cid = summary.cid;
      page.title = QStringLiteral("缓存");
      page.duration_seconds = summary.duration_ms / 1000;
      video.pages.push_back(page);
      video.selected_index = 0;
      ShowResult(video);
      last_cache_hit_ = true;
      last_http_requests_ = 0;
      last_resolve_ms_ = cache_timer.elapsed();
      status_label_->setText(
          QStringLiteral("缓存命中：离线打开，不发起网络请求；点“确认”开始"));
      if (quality_label_ != nullptr) {
        quality_label_->setText(
            QStringLiteral("缓存命中：qn=%1 · codecid=%2 · %3fps")
                .arg(summary.qn)
                .arg(summary.codecid)
                .arg(QString::number(summary.fps, 'f', 1)));
        quality_label_->show();
      }
      emit resolveCompleted(true);
      return;
    }
  }

  // T8.2 / DEC-3：有凭据时，用户首次提交合法 BV/URL 后才自检一次；与 view
  // 并行，nav 失败/超时不会拖住解析，也不会在启动/打开对话框/保存凭据时联网。
  if (http_ == nullptr) {
    http_ = std::make_unique<lapian::online::WinHttpClient>();
  }
  if (auth_.BeginCheck()) {
    ++nav_request_count_;
    StartAuthCheck();
  }

  cancel_ = std::make_shared<lapian::online::HttpCancelToken>();
  outcome_ = std::make_shared<ResolveOutcome>();
  status_label_->setText(QStringLiteral("解析中…（可取消）"));
  resolving_ = true;
  SetBusy(true);

  const QString text = input_->text();
  std::thread worker([this, text, outcome = outcome_, cancel = cancel_]() {
    lapian::online::BiliResolveClient client(http_.get());
    const lapian::online::BiliResolveResult result = client.Resolve(text, *cancel);
    std::lock_guard<std::mutex> lock(outcome->mutex);
    outcome->video = result.video;
    outcome->error = result.error;
    outcome->http_requests = result.http_requests;
    outcome->elapsed_ms = result.elapsed_ms;
    outcome->done = true;
  });
  worker_ = std::move(worker);
  poll_timer_->start();
}

void OnlineResolveDialog::PollResolve() {
  // 登录自检与 view 并行；谁先完成先处理，不让 nav 拖慢 view。
  PollAuth();

  if (!outcome_) {
    if (!auth_checking_) {
      poll_timer_->stop();
    }
    return;
  }

  lapian::online::BiliVideo video;
  lapian::online::BiliResolveError error;
  int requests = 0;
  qint64 elapsed_ms = 0;
  {
    std::lock_guard<std::mutex> lock(outcome_->mutex);
    if (!outcome_->done) {
      return;
    }
    video = outcome_->video;
    error = outcome_->error;
    requests = outcome_->http_requests;
    elapsed_ms = outcome_->elapsed_ms;
  }

  if (worker_.joinable()) {
    worker_.join();
  }
  outcome_.reset();
  resolving_ = false;
  last_http_requests_ = requests;
  total_http_requests_ += requests;
  last_resolve_ms_ = elapsed_ms;
  SetBusy(false);

  if (error.ok()) {
    ShowResult(video);
    emit resolveCompleted(true);
  } else {
    ShowError(error);
    emit resolveCompleted(false);
  }

  if (!auth_checking_) {
    poll_timer_->stop();
  }
}

void OnlineResolveDialog::OnPageChanged(int index) {
  if (!has_video_ || index < 0 || index >= video_.pages.size()) {
    return;
  }
  if (video_.selected_index == index) {
    return;
  }
  video_.selected_index = index;
  confirmed_ = false;
  UpdateSelectedPage();
  UpdateConfirmState();
  status_label_->setText(QStringLiteral("已切换分P；请重新核对时长/分P 后确认"));
}

void OnlineResolveDialog::OnConfirm() {
  if (resolving_ || !has_video_ || confirmed_) {
    return;
  }
  const lapian::online::BiliPage* page = video_.selected_page();
  if (page == nullptr) {
    return;
  }
  confirmed_ = true;
  UpdateConfirmState();
  status_label_->setText(
      QStringLiteral("已确认：%1 · P%2 · cid %3%4")
          .arg(video_.bvid)
          .arg(page->page)
          .arg(page->cid)
          .arg(last_cache_hit_ ? QStringLiteral("（缓存命中，离线打开）")
                               : QString()));
  emit confirmed(video_.bvid, page->cid, page->page);
}

void OnlineResolveDialog::CancelResolve() {
  if (!resolving_) {
    // 非解析中：按钮就是“关闭”；关窗事件会取消仍在进行的登录检查。
    close();
    return;
  }

  QElapsedTimer timer;
  timer.start();
  if (cancel_ != nullptr) {
    cancel_->Cancel();
  }
  if (auth_cancel_ != nullptr) {
    auth_cancel_->Cancel();
  }
  poll_timer_->stop();
  if (worker_.joinable()) {
    worker_.join();
  }
  if (auth_worker_.joinable()) {
    auth_worker_.join();
  }
  if (auth_checking_) {
    auth_.CancelCheck();
    auth_checking_ = false;
    ++auth_generation_;
    auth_outcome_.reset();
  }
  resolving_ = false;
  last_cancel_ms_ = timer.elapsed();
  outcome_.reset();
  SetBusy(false);
  status_label_->setText(QStringLiteral("已取消（未取流）"));
  UpdateAuthUi();
  UpdateConfirmState();
}

void OnlineResolveDialog::closeEvent(QCloseEvent* event) {
  if (qr_controller_ != nullptr) {
    qr_controller_->Close();
  }
  if (resolving_) {
    CancelResolve();
  }
  if (auth_checking_) {
    CancelAuthCheck();
  }
  QDialog::closeEvent(event);
}

void OnlineResolveDialog::Shutdown() {
  if (qr_controller_ != nullptr) {
    qr_controller_->Close();
  }
  if (resolving_) {
    CancelResolve();
  }
  if (auth_checking_) {
    CancelAuthCheck();
  }
  hide();
}

void OnlineResolveDialog::SetBusy(bool busy) {
  if (resolve_button_ != nullptr) {
    resolve_button_->setEnabled(!busy);
  }
  if (input_ != nullptr) {
    input_->setEnabled(!busy);
  }
  if (cancel_button_ != nullptr) {
    // 解析中 = 取消；空闲 = 关闭。始终可点，避免解析成功后没有关闭入口。
    cancel_button_->setEnabled(true);
    cancel_button_->setText(busy ? QStringLiteral("取消") : QStringLiteral("关闭"));
  }
  if (page_combo_ != nullptr) {
    page_combo_->setEnabled(!busy && has_video_ && video_.pages.size() > 1);
  }
  UpdateConfirmState();
}

void OnlineResolveDialog::ResetResult() {
  has_video_ = false;
  confirmed_ = false;
  video_ = lapian::online::BiliVideo();
  if (title_label_ != nullptr) {
    title_label_->clear();
  }
  if (owner_label_ != nullptr) {
    owner_label_->clear();
  }
  if (total_duration_label_ != nullptr) {
    total_duration_label_->clear();
  }
  if (page_info_label_ != nullptr) {
    page_info_label_->clear();
  }
  if (page_duration_label_ != nullptr) {
    page_duration_label_->clear();
  }
  if (quality_label_ != nullptr) {
    quality_label_->clear();
    quality_label_->hide();
  }
  if (error_label_ != nullptr) {
    error_label_->clear();
    error_label_->hide();
  }
  if (page_combo_ != nullptr) {
    const QSignalBlocker blocker(page_combo_);
    page_combo_->clear();
    page_combo_->setEnabled(false);
  }
  UpdateConfirmState();
}

void OnlineResolveDialog::ShowResult(const lapian::online::BiliVideo& video) {
  video_ = video;
  has_video_ = true;
  confirmed_ = false;

  title_label_->setText(video.title);
  owner_label_->setText(video.owner);
  total_duration_label_->setText(FormatDuration(video.duration_seconds));
  UpdateQualityHint();
  error_label_->clear();
  error_label_->hide();

  {
    const QSignalBlocker blocker(page_combo_);
    page_combo_->clear();
    for (const lapian::online::BiliPage& page : video.pages) {
      page_combo_->addItem(
          QStringLiteral("P%1 · %2").arg(page.page).arg(page.title));
    }
    page_combo_->setCurrentIndex(video.selected_index);
  }
  page_combo_->setEnabled(video.pages.size() > 1);

  UpdateSelectedPage();
  UpdateConfirmState();
  status_label_->setText(
      QStringLiteral("解析成功：请核对标题/UP/时长/分P 后点“确认”"));
}

void OnlineResolveDialog::ShowError(const lapian::online::BiliResolveError& error) {
  has_video_ = false;
  confirmed_ = false;
  video_ = lapian::online::BiliVideo();
  title_label_->clear();
  owner_label_->clear();
  total_duration_label_->clear();
  page_info_label_->clear();
  page_duration_label_->clear();
  quality_label_->clear();
  quality_label_->hide();
  {
    const QSignalBlocker blocker(page_combo_);
    page_combo_->clear();
    page_combo_->setEnabled(false);
  }
  error_label_->setText(error.message);
  error_label_->show();
  const bool local_only = error.kind == lapian::online::BiliErrorKind::kInvalidInput &&
                          last_http_requests_ == 0;
  status_label_->setText(local_only
                             ? QStringLiteral("本地校验未通过：未发起网络请求")
                             : QStringLiteral("解析失败（未取流）"));
  UpdateConfirmState();
}

void OnlineResolveDialog::UpdateSelectedPage() {
  const lapian::online::BiliPage* page = video_.selected_page();
  if (page == nullptr) {
    page_info_label_->clear();
    page_duration_label_->clear();
    return;
  }
  page_info_label_->setText(
      QStringLiteral("P%1 · cid %2").arg(page->page).arg(page->cid));
  page_duration_label_->setText(FormatDuration(page->duration_seconds));
}

void OnlineResolveDialog::UpdateConfirmState() {
  if (confirm_button_ != nullptr) {
    confirm_button_->setEnabled(!resolving_ && has_video_ && !confirmed_);
  }
}

void OnlineResolveDialog::ReloadCredentialState() {
  credential_corrupt_ = false;
  const lapian::online::CredentialLoadResult load = credentials_.Load();
  if (load.has_credential) {
    auth_.SetCredentialPresent(true);
  } else {
    auth_.Reset();
    credential_corrupt_ = load.corrupt;
  }
  UpdateAuthUi();
  UpdateQualityHint();
}

void OnlineResolveDialog::UpdateAuthUi() {
  if (auth_status_label_ == nullptr) {
    return;
  }

  QString text;
  const char* state = "muted";
  if (auth_checking_) {
    text = QStringLiteral("登录检查中…（最多 %1ms；失败继续匿名解析）")
               .arg(auth_timeout_ms_);
    state = "warn";
  } else {
    switch (auth_.state()) {
      case lapian::online::AuthState::kAnonymous:
        if (credential_corrupt_) {
          text = QStringLiteral(
              "凭据文件损坏，已按未登录处理；请重新粘贴 SESSDATA 保存");
          state = "error";
        } else {
          text = QStringLiteral("未配置凭据：按未登录处理（清晰度受限）");
        }
        break;
      case lapian::online::AuthState::kNotChecked:
        text = QStringLiteral(
            "凭据已保存；首次提交 BV 时检查登录状态（不会现在联网）");
        break;
      case lapian::online::AuthState::kLoggedIn:
        text = QStringLiteral("已登录（清晰度以取流时返回为准）");
        state = "ok";
        break;
      case lapian::online::AuthState::kLoginUnavailable: {
        text = auth_.result().message;
        if (text.isEmpty()) {
          text = QStringLiteral(
              "登录不可用/凭据失效；将按未登录清晰度播放");
        }
        state = "error";
        break;
      }
      case lapian::online::AuthState::kChecking:
        text = QStringLiteral("登录检查中…");
        state = "warn";
        break;
    }
  }
  auth_status_label_->setText(text);
  SetStateProperty(auth_status_label_, state);

  const bool can_edit = !auth_checking_;
  if (credential_input_ != nullptr) {
    credential_input_->setEnabled(can_edit);
  }
  if (credential_save_button_ != nullptr) {
    credential_save_button_->setEnabled(can_edit);
  }
  if (credential_clear_button_ != nullptr) {
    credential_clear_button_->setEnabled(
        can_edit && (auth_.has_credential() || credential_corrupt_));
  }
}

void OnlineResolveDialog::UpdateQualityHint() {
  if (quality_label_ == nullptr) {
    return;
  }
  if (!has_video_) {
    quality_label_->hide();
    return;
  }

  QString text;
  switch (auth_.state()) {
    case lapian::online::AuthState::kLoggedIn:
      text = QStringLiteral("已登录：清晰度以取流时返回为准");
      break;
    case lapian::online::AuthState::kChecking:
      text = QStringLiteral("登录检查中…；当前按未登录清晰度解析");
      break;
    case lapian::online::AuthState::kLoginUnavailable:
      text = QStringLiteral(
          "登录不可用/凭据失效；将按未登录清晰度播放");
      break;
    case lapian::online::AuthState::kNotChecked:
      text = QStringLiteral(
          "登录状态尚未检查：按未登录清晰度解析（首次提交后检查）");
      break;
    case lapian::online::AuthState::kAnonymous:
      text = QStringLiteral("未登录：清晰度受限（实际档位以取流时返回为准）");
      break;
  }
  quality_label_->setText(text);
  quality_label_->show();
}

void OnlineResolveDialog::StartAuthCheck() {
  lapian::online::CredentialLoadResult load = credentials_.Load();
  if (!load.has_credential) {
    lapian::online::AuthResult result;
    result.state = lapian::online::AuthState::kLoginUnavailable;
    result.error = load.corrupt
                       ? lapian::online::AuthError::kInvalidCredential
                       : lapian::online::AuthError::kNoCredential;
    result.message =
        load.corrupt
            ? QStringLiteral(
                  "凭据文件损坏，已按未登录处理；请重新保存凭据")
            : QStringLiteral("未配置凭据；将按未登录清晰度播放");
    auth_.CompleteCheck(result);
    credential_corrupt_ = load.corrupt;
    UpdateAuthUi();
    UpdateQualityHint();
    return;
  }

  credential_corrupt_ = false;
  auth_checking_ = true;
  ++auth_generation_;
  const std::uint64_t generation = auth_generation_;

  QString sessdata = load.sessdata;
  lapian::online::ClearSecret(&load.sessdata);

  auth_cancel_ = std::make_shared<lapian::online::HttpCancelToken>();
  auth_outcome_ = std::make_shared<AuthOutcome>();
  auth_outcome_->generation = generation;
  auto outcome = auth_outcome_;
  auto cancel = auth_cancel_;
  auto* http = http_.get();
  const int timeout_ms = auth_timeout_ms_;

  auth_worker_ = std::thread(
      [sessdata, http, cancel, outcome, timeout_ms]() mutable {
        lapian::online::AuthResult result =
            lapian::online::CheckNav(sessdata, http, *cancel, timeout_ms);
        lapian::online::ClearSecret(&sessdata);
        std::lock_guard<std::mutex> lock(outcome->mutex);
        outcome->result = result;
        outcome->done = true;
      });
  poll_timer_->start();
  UpdateAuthUi();
  UpdateQualityHint();
}

void OnlineResolveDialog::PollAuth() {
  if (!auth_outcome_) {
    return;
  }

  lapian::online::AuthResult result;
  std::uint64_t generation = 0;
  bool done = false;
  {
    std::lock_guard<std::mutex> lock(auth_outcome_->mutex);
    done = auth_outcome_->done;
    result = auth_outcome_->result;
    generation = auth_outcome_->generation;
  }
  if (!done) {
    return;
  }

  if (auth_worker_.joinable()) {
    auth_worker_.join();
  }
  auth_outcome_.reset();
  auth_checking_ = false;

  if (generation == auth_generation_) {
    auth_.CompleteCheck(result);
    credential_corrupt_ =
        result.error == lapian::online::AuthError::kInvalidCredential;
    UpdateAuthUi();
    UpdateQualityHint();
  }
}

void OnlineResolveDialog::JoinAuthWorker() {
  if (auth_worker_.joinable()) {
    auth_worker_.join();
  }
}

void OnlineResolveDialog::CancelAuthCheck() {
  if (auth_cancel_ != nullptr) {
    auth_cancel_->Cancel();
  }
  JoinAuthWorker();
  auth_.CancelCheck();
  auth_checking_ = false;
  ++auth_generation_;
  auth_outcome_.reset();
  UpdateAuthUi();
  UpdateQualityHint();
}

bool OnlineResolveDialog::SaveCredentialText(const QString& text, QString* error) {
  if (auth_checking_) {
    CancelAuthCheck();
  }
  if (qr_controller_ != nullptr && qr_controller_->IsBusy()) {
    qr_controller_->Cancel();
    if (qr_panel_ != nullptr) {
      qr_panel_->hide();
    }
  }
  QString save_error;
  if (!credentials_.Save(text, &save_error)) {
    if (error != nullptr) {
      *error = save_error;
    }
    return false;
  }
  credential_corrupt_ = false;
  auth_.SetCredentialPresent(true);
  UpdateAuthUi();
  UpdateQualityHint();
  return true;
}

bool OnlineResolveDialog::ClearCredentialNow() {
  if (auth_checking_) {
    CancelAuthCheck();
  }
  if (qr_controller_ != nullptr) {
    qr_controller_->Close();
    qr_controller_.reset();
  }
  if (qr_panel_ != nullptr) {
    qr_panel_->hide();
  }
  QString clear_error;
  if (!credentials_.Clear(&clear_error)) {
    if (auth_status_label_ != nullptr) {
      auth_status_label_->setText(clear_error);
      SetStateProperty(auth_status_label_, "error");
    }
    return false;
  }
  credential_corrupt_ = false;
  auth_.Reset();
  UpdateAuthUi();
  UpdateQualityHint();
  return true;
}

void OnlineResolveDialog::OnSaveCredential() {
  if (credential_input_ == nullptr || auth_status_label_ == nullptr) {
    return;
  }
  const QString text = credential_input_->text();
  QString error;
  if (!SaveCredentialText(text, &error)) {
    auth_status_label_->setText(error);
    SetStateProperty(auth_status_label_, "error");
    credential_input_->setFocus();
    return;
  }
  credential_input_->clear();
  auth_status_label_->setText(
      QStringLiteral("凭据已保存；首次提交 BV 时检查登录状态（不会现在联网）"));
}

void OnlineResolveDialog::OnClearCredential() {
  if (!ClearCredentialNow() || auth_status_label_ == nullptr) {
    return;
  }
  auth_status_label_->setText(
      QStringLiteral("已清除凭据；后续请求按未登录处理"));
}

void OnlineResolveDialog::OnHidePrivacy() {
  if (privacy_label_ != nullptr) {
    privacy_label_->hide();
  }
  if (privacy_hide_button_ != nullptr) {
    privacy_hide_button_->hide();
  }
}

void OnlineResolveDialog::EnsureQrController() {
  if (http_ == nullptr) {
    http_ = std::make_unique<lapian::online::WinHttpClient>();
  }
  if (qr_controller_ != nullptr) {
    return;
  }
  qr_controller_ = std::make_unique<lapian::online::QrLoginController>(
      http_.get(), &credentials_, this);
  connect(qr_controller_.get(), &lapian::online::QrLoginController::stateChanged,
          this, &OnlineResolveDialog::OnQrStateChanged);
  connect(qr_controller_.get(), &lapian::online::QrLoginController::qrImageChanged,
          this, &OnlineResolveDialog::OnQrImageChanged);
  connect(qr_controller_.get(), &lapian::online::QrLoginController::loginSucceeded,
          this, &OnlineResolveDialog::OnQrLoginSucceeded);
}

void OnlineResolveDialog::OnStartQrLogin() {
  EnsureQrController();
  if (qr_panel_ != nullptr) {
    qr_panel_->show();
  }
  if (qr_status_label_ != nullptr) {
    qr_status_label_->setText(QStringLiteral("正在生成二维码…"));
  }
  if (qr_start_button_ != nullptr) {
    qr_start_button_->setEnabled(false);
  }
  // P2：隐藏面板 show 后顶层窗口不会自动长高，必须重算并夹到屏幕。
  AdjustToContent();
  qr_controller_->Start();
}

void OnlineResolveDialog::OnQrStateChanged(int state, const QString& message) {
  const auto qr_state = static_cast<lapian::online::QrLoginState>(state);
  if (qr_status_label_ != nullptr) {
    QString text = message;
    if (qr_state == lapian::online::QrLoginState::kExpired ||
        qr_state == lapian::online::QrLoginState::kFailed ||
        qr_state == lapian::online::QrLoginState::kCancelled) {
      text += QStringLiteral("；仍可用上方粘贴 SESSDATA");
    }
    qr_status_label_->setText(text);
  }
  if (qr_start_button_ != nullptr) {
    qr_start_button_->setEnabled(
        qr_state == lapian::online::QrLoginState::kSuccess ||
        qr_state == lapian::online::QrLoginState::kExpired ||
        qr_state == lapian::online::QrLoginState::kFailed ||
        qr_state == lapian::online::QrLoginState::kCancelled);
  }
  if (qr_refresh_button_ != nullptr) {
    qr_refresh_button_->setEnabled(
        qr_state == lapian::online::QrLoginState::kExpired ||
        qr_state == lapian::online::QrLoginState::kFailed ||
        qr_state == lapian::online::QrLoginState::kCancelled ||
        qr_state == lapian::online::QrLoginState::kSuccess);
  }
  if (qr_cancel_button_ != nullptr) {
    qr_cancel_button_->setEnabled(
        qr_state == lapian::online::QrLoginState::kGenerating ||
        qr_state == lapian::online::QrLoginState::kWaiting ||
        qr_state == lapian::online::QrLoginState::kScanned ||
        qr_state == lapian::online::QrLoginState::kVerifying);
  }
  if (qr_state == lapian::online::QrLoginState::kSuccess &&
      qr_image_label_ != nullptr) {
    qr_image_label_->clear();
  }
}

void OnlineResolveDialog::OnQrImageChanged(const QImage& image) {
  if (qr_image_label_ == nullptr) {
    return;
  }
  if (image.isNull()) {
    qr_image_label_->clear();
    return;
  }
  qr_image_label_->setPixmap(QPixmap::fromImage(image).scaled(
      220, 220, Qt::KeepAspectRatio, Qt::FastTransformation));
}

void OnlineResolveDialog::OnQrLoginSucceeded() {
  if (qr_controller_ == nullptr) {
    return;
  }
  const lapian::online::AuthResult result = qr_controller_->authResult();
  credential_corrupt_ = false;
  auth_.SetCredentialPresent(true);
  if (auth_.BeginCheck()) {
    auth_.CompleteCheck(result);
  }
  UpdateAuthUi();
  UpdateQualityHint();
  if (qr_status_label_ != nullptr) {
    qr_status_label_->setText(
        QStringLiteral("已登录（扫码）；SESSDATA 已用 DPAPI 保存"));
  }
}

void OnlineResolveDialog::OnQrRefresh() {
  if (qr_controller_ == nullptr) {
    return;
  }
  if (qr_panel_ != nullptr) {
    qr_panel_->show();
  }
  AdjustToContent();
  qr_controller_->Refresh();
}

void OnlineResolveDialog::OnQrCancel() {
  if (qr_controller_ == nullptr) {
    return;
  }
  qr_controller_->Cancel();
  if (qr_panel_ != nullptr) {
    qr_panel_->hide();
  }
  if (qr_start_button_ != nullptr) {
    qr_start_button_->setEnabled(true);
  }
  AdjustToContent();
}

void OnlineResolveDialog::SetCredentialPathForTest(const QString& path) {
  if (auth_checking_) {
    CancelAuthCheck();
  }
  credentials_ = lapian::online::CredentialStore(path);
  ReloadCredentialState();
}

bool OnlineResolveDialog::SaveCredentialForTest(const QString& text) {
  QString error;
  return SaveCredentialText(text, &error);
}

bool OnlineResolveDialog::ClearCredentialForTest() {
  return ClearCredentialNow();
}

QString OnlineResolveDialog::AuthStatusTextForTest() const {
  return auth_status_label_ == nullptr ? QString() : auth_status_label_->text();
}

QString OnlineResolveDialog::AuthStateForTest() const {
  return lapian::online::AuthStateName(auth_.state());
}

bool OnlineResolveDialog::WaitForAuthForTest(int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (auth_checking_ && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return !auth_checking_;
}

bool OnlineResolveDialog::PrivacyNoticeVisibleForTest() const {
  return privacy_label_ != nullptr && !privacy_label_->isHidden();
}

QString OnlineResolveDialog::PrivacyNoticeTextForTest() const {
  return privacy_label_ == nullptr ? QString() : privacy_label_->text();
}

void OnlineResolveDialog::HidePrivacyNoticeForTest() { OnHidePrivacy(); }

void OnlineResolveDialog::SetAuthTimeoutForTest(int timeout_ms) {
  auth_timeout_ms_ = std::clamp(timeout_ms, 100, 10000);
}

void OnlineResolveDialog::StartQrLoginForTest() { OnStartQrLogin(); }

void OnlineResolveDialog::SetQrPollIntervalForTest(int ms) {
  EnsureQrController();
  qr_controller_->SetPollIntervalForTest(ms);
}

void OnlineResolveDialog::SetQrExpiryForTest(int ms) {
  EnsureQrController();
  qr_controller_->SetExpiryMsForTest(ms);
}

QString OnlineResolveDialog::QrStateForTest() const {
  return qr_controller_ == nullptr
             ? QStringLiteral("idle")
             : lapian::online::QrLoginStateName(qr_controller_->state());
}

QString OnlineResolveDialog::QrStatusTextForTest() const {
  return qr_status_label_ == nullptr ? QString() : qr_status_label_->text();
}

QSize OnlineResolveDialog::QrImageSizeForTest() const {
  if (qr_image_label_ == nullptr) {
    return QSize();
  }
  const QPixmap pixmap = qr_image_label_->pixmap();
  return pixmap.isNull() ? QSize() : pixmap.size();
}

bool OnlineResolveDialog::QrPanelVisibleForTest() const {
  return qr_panel_ != nullptr && !qr_panel_->isHidden();
}

QSize OnlineResolveDialog::QrLabelSizeForTest() const {
  return qr_image_label_ == nullptr ? QSize() : qr_image_label_->size();
}

bool OnlineResolveDialog::QrImageFullyVisibleForTest() const {
  if (qr_image_label_ == nullptr || !qr_image_label_->isVisible()) {
    return false;
  }
  if (scroll_area_ == nullptr || !scroll_area_->viewport()->isVisible()) {
    return false;
  }
  QWidget* viewport = scroll_area_->viewport();
  const QRect rect(viewport->mapFromGlobal(
                       qr_image_label_->mapToGlobal(QPoint(0, 0))),
                   qr_image_label_->size());
  return viewport->rect().contains(rect);
}

bool OnlineResolveDialog::QrControlsVisibleForTest() const {
  if (scroll_area_ == nullptr || !scroll_area_->viewport()->isVisible()) {
    return false;
  }
  QWidget* viewport = scroll_area_->viewport();
  auto fully_visible = [viewport](QWidget* widget) {
    if (widget == nullptr || !widget->isVisible()) {
      return false;
    }
    const QRect rect(
        viewport->mapFromGlobal(widget->mapToGlobal(QPoint(0, 0))),
        widget->size());
    return viewport->rect().contains(rect);
  };
  return fully_visible(qr_image_label_) &&
         fully_visible(qr_refresh_button_) &&
         fully_visible(qr_cancel_button_);
}

bool OnlineResolveDialog::DialogWithinScreenForTest() const {
  QScreen* screen = this->screen();
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  if (screen == nullptr) {
    return false;
  }
  return screen->availableGeometry().contains(frameGeometry());
}

QString OnlineResolveDialog::QrLayoutStateForTest() const {
  auto rect_text = [](const QRect& rect) {
    return QStringLiteral("%1,%2 %3x%4")
        .arg(rect.x())
        .arg(rect.y())
        .arg(rect.width())
        .arg(rect.height());
  };
  QScreen* screen = this->screen();
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  const QRect avail =
      screen == nullptr ? QRect() : screen->availableGeometry();
  const QRect qr_rect =
      qr_image_label_ == nullptr
          ? QRect()
          : QRect(qr_image_label_->mapToGlobal(QPoint(0, 0)),
                  qr_image_label_->size());
  QString scroll_state = QStringLiteral("scroll=none");
  if (scroll_area_ != nullptr) {
    scroll_state = QStringLiteral("scroll=%1x%2 bar=%3")
                       .arg(scroll_area_->viewport()->width())
                       .arg(scroll_area_->viewport()->height())
                       .arg(scroll_area_->verticalScrollBar()->value());
  }
  return QStringLiteral(
             "dialog=%1x%2 frame=%3 avail=%4 qr=%5 label=%6x%7 "
             "qr_visible=%8 controls=%9 within=%10 %11")
      .arg(width())
      .arg(height())
      .arg(rect_text(frameGeometry()), rect_text(avail),
           rect_text(qr_rect))
      .arg(qr_image_label_ == nullptr ? 0 : qr_image_label_->width())
      .arg(qr_image_label_ == nullptr ? 0 : qr_image_label_->height())
      .arg(QrImageFullyVisibleForTest() ? 1 : 0)
      .arg(QrControlsVisibleForTest() ? 1 : 0)
      .arg(DialogWithinScreenForTest() ? 1 : 0)
      .arg(scroll_state);
}

qint64 OnlineResolveDialog::CancelQrForTest() {
  if (qr_controller_ == nullptr) {
    return -1;
  }
  qr_controller_->Cancel();
  return qr_controller_->lastCancelMsForTest();
}

bool OnlineResolveDialog::WaitForResolveForTest(int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (resolving_ && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return !resolving_;
}

void OnlineResolveDialog::SetHttpClientForTest(
    std::unique_ptr<lapian::online::IHttpClient> client) {
  if (resolving_ || auth_checking_) {
    return;
  }
  if (qr_controller_ != nullptr) {
    qr_controller_->Close();
    qr_controller_.reset();
  }
  http_ = client != nullptr ? std::move(client)
                            : std::make_unique<lapian::online::WinHttpClient>();
}

void OnlineResolveDialog::SetInputForTest(const QString& text) {
  input_->setText(text);
}

void OnlineResolveDialog::SelectPageForTest(int index) {
  if (page_combo_ == nullptr) {
    return;
  }
  page_combo_->setCurrentIndex(index);
}

void OnlineResolveDialog::ClickConfirmForTest() { OnConfirm(); }

void OnlineResolveDialog::CloseForTest() { close(); }

qint64 OnlineResolveDialog::CancelAndCloseForTest() {
  QElapsedTimer timer;
  timer.start();
  if (resolving_) {
    CancelResolve();
  }
  close();
  return timer.elapsed();
}

qint64 OnlineResolveDialog::CancelForTest() {
  if (!resolving_) {
    return -1;
  }
  CancelResolve();
  return last_cancel_ms_;
}

bool OnlineResolveDialog::ConfirmEnabledForTest() const {
  return confirm_button_ != nullptr && confirm_button_->isEnabled();
}

QString OnlineResolveDialog::InputForTest() const {
  return input_ == nullptr ? QString() : input_->text();
}

QString OnlineResolveDialog::StatusTextForTest() const {
  return status_label_ == nullptr ? QString() : status_label_->text();
}

QString OnlineResolveDialog::ErrorTextForTest() const {
  return error_label_ == nullptr ? QString() : error_label_->text();
}

QString OnlineResolveDialog::TitleTextForTest() const {
  return title_label_ == nullptr ? QString() : title_label_->text();
}

QString OnlineResolveDialog::OwnerTextForTest() const {
  return owner_label_ == nullptr ? QString() : owner_label_->text();
}

QString OnlineResolveDialog::DurationTextForTest() const {
  return page_duration_label_ == nullptr ? QString() : page_duration_label_->text();
}

QString OnlineResolveDialog::TotalDurationTextForTest() const {
  return total_duration_label_ == nullptr ? QString()
                                          : total_duration_label_->text();
}

QString OnlineResolveDialog::CurrentPageTextForTest() const {
  return page_info_label_ == nullptr ? QString() : page_info_label_->text();
}

QString OnlineResolveDialog::QualityHintForTest() const {
  return quality_label_ == nullptr ? QString() : quality_label_->text();
}

int OnlineResolveDialog::PageCountForTest() const {
  return page_combo_ == nullptr ? 0 : page_combo_->count();
}

int OnlineResolveDialog::SelectedPageIndexForTest() const {
  return page_combo_ == nullptr ? -1 : page_combo_->currentIndex();
}

QString OnlineResolveDialog::PageLabelAtForTest(int index) const {
  if (page_combo_ == nullptr || index < 0 || index >= page_combo_->count()) {
    return QString();
  }
  return page_combo_->itemText(index);
}

OnlineConfirmInfo OnlineResolveDialog::ConfirmInfo() const {
  OnlineConfirmInfo info;
  const lapian::online::BiliPage* page = video_.selected_page();
  if (!has_video_ || page == nullptr) {
    return info;
  }
  info.bvid = video_.bvid;
  info.cid = page->cid;
  info.page = page->page;
  info.title = video_.title;
  info.owner = video_.owner;
  info.duration_ms = page->duration_seconds * 1000;
  if (info.duration_ms <= 0) {
    info.duration_ms = video_.duration_seconds * 1000;
  }
  info.page_title = page->title;
  return info;
}
