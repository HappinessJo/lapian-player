#include "app/ui/PlayerShell.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "app/ui/BannerSlot.h"
#include "app/ui/PlayerSplitter.h"
#include "app/ui/StageStack.h"
#include "app/ui/Theme.h"
#include "app/ui/TopBar.h"
#include "app/ui/TransportBar.h"

namespace {

constexpr int kVideoColumnMinHeightBase = 170;  // 规划 §7.4：画面留 ≥170

}  // namespace

PlayerShell::PlayerShell(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("v2PlayerShell"));
  lapian::ui::SetStyledBackground(this);
  setMinimumSize(0, 0);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  top_bar_ = new TopBar(this);
  root->addWidget(top_bar_, 0);

  banner_ = new BannerSlot(this);
  root->addWidget(banner_, 0);

  workspace_ = new QWidget(this);
  workspace_->setObjectName(QStringLiteral("v2Workspace"));
  lapian::ui::SetStyledBackground(workspace_);
  auto* workspace_layout = new QHBoxLayout(workspace_);
  workspace_layout->setContentsMargins(tokens_.margin, tokens_.margin,
                                       tokens_.margin, tokens_.margin);
  workspace_layout->setSpacing(0);
  root->addWidget(workspace_, 1);

  h_split_ = new PlayerSplitter(Qt::Horizontal, workspace_);
  h_split_->setObjectName(QStringLiteral("v2HSplit"));
  workspace_layout->addWidget(h_split_, 1);

  // 注意：QSplitter 会把"以 splitter 为 parent 的子控件"自动加成一栏，所以这些
  // 面板先不带 parent 创建，再由 addWidget 收进去（否则同一控件会进两次，尺寸分配错乱）。
  v_split_ = new PlayerSplitter(Qt::Vertical, nullptr);
  v_split_->setObjectName(QStringLiteral("v2VSplit"));
  v_split_->setStretchFactor(0, 1);
  v_split_->setStretchFactor(1, 0);
  h_split_->addWidget(v_split_);

  video_column_ = new QWidget();
  video_column_->setObjectName(QStringLiteral("v2VideoColumn"));
  auto* video_layout = new QVBoxLayout(video_column_);
  video_layout->setContentsMargins(0, 0, 0, 0);
  video_layout->setSpacing(0);
  v_split_->addWidget(video_column_);

  stage_ = new StageStack(video_column_);
  video_layout->addWidget(stage_, 1);

  transport_ = new TransportBar(video_column_);
  video_layout->addWidget(transport_, 0);

  timeline_host_ = new QWidget();
  timeline_host_->setObjectName(QStringLiteral("v2TimelinePanel"));
  lapian::ui::SetStyledBackground(timeline_host_);
  ApplyTimelineConstraints();
  auto* timeline_layout = new QVBoxLayout(timeline_host_);
  timeline_layout->setContentsMargins(0, 0, 0, 0);
  timeline_layout->setSpacing(0);
  v_split_->addWidget(timeline_host_);
  if (QSplitterHandle* vhandle = v_split_->handle(1)) {
    vhandle->installEventFilter(this);
  }

  inspector_host_ = new QWidget();
  inspector_host_->setObjectName(QStringLiteral("v2Inspector"));
  lapian::ui::SetStyledBackground(inspector_host_);
  inspector_host_->setMinimumWidth(tokens_.inspector_min);
  inspector_host_->setMaximumWidth(tokens_.inspector_max);
  auto* inspector_layout = new QHBoxLayout(inspector_host_);
  inspector_layout->setContentsMargins(0, 0, 0, 0);
  inspector_layout->setSpacing(0);
  h_split_->addWidget(inspector_host_);
  h_split_->setStretchFactor(0, 1);
  h_split_->setStretchFactor(1, 0);

  inspector_rail_ = new QWidget(inspector_host_);
  inspector_rail_->setObjectName(QStringLiteral("v2InspectorRail"));
  lapian::ui::SetStyledBackground(inspector_rail_);
  inspector_rail_->setFixedWidth(26);
  auto* rail_layout = new QVBoxLayout(inspector_rail_);
  rail_layout->setContentsMargins(0, 4, 0, 0);
  rail_layout->setSpacing(4);
  inspector_toggle_ = new QToolButton(inspector_rail_);
  inspector_toggle_->setObjectName(QStringLiteral("v2InspectorCollapse"));
  inspector_toggle_->setText(QStringLiteral("»"));
  inspector_toggle_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  inspector_toggle_->setFocusPolicy(Qt::NoFocus);
  inspector_toggle_->setAutoRaise(true);
  inspector_toggle_->setFixedSize(24, tokens_.btn);
  rail_layout->addWidget(inspector_toggle_, 0, Qt::AlignHCenter);
  rail_layout->addStretch(1);
  inspector_layout->addWidget(inspector_rail_, 0);

  inspector_content_ = new QWidget(inspector_host_);
  inspector_content_->setObjectName(QStringLiteral("v2InspectorContent"));
  auto* content_layout = new QVBoxLayout(inspector_content_);
  content_layout->setContentsMargins(0, 0, 0, 0);
  content_layout->setSpacing(0);
  inspector_layout->addWidget(inspector_content_, 1);

  connect(inspector_toggle_, &QToolButton::clicked, this,
          [this]() { SetInspectorCollapsed(!inspector_collapsed_); });
  connect(v_split_, &PlayerSplitter::resetRequested, this,
          &PlayerShell::ResetSplitSizes);
  connect(h_split_, &PlayerSplitter::resetRequested, this,
          &PlayerShell::ResetSplitSizes);
  connect(v_split_, &QSplitter::splitterMoved, this, [this](int, int) {
    if (applying_sizes_) {
      return;
    }
    timeline_user_resized_ = true;
    timeline_user_height_ = timeline_host_->height();
  });
  connect(h_split_, &QSplitter::splitterMoved, this, [this](int, int) {
    if (applying_sizes_) {
      return;
    }
    inspector_user_resized_ = true;
    inspector_user_width_ = inspector_host_->width();
  });

  UpdateInspectorToggle();
  ApplyTokens();
}

void PlayerShell::ApplyClientHeight(int client_height) {
  client_height_ = client_height;
  const lapian::ui::Density next =
      lapian::ui::ResolveDensity(client_height, has_density_, tokens_.density);
  has_density_ = true;
  if (next != tokens_.density) {
    SetDensity(next);
    return;
  }
  ApplyTokens();
}

void PlayerShell::SetDensity(lapian::ui::Density density) {
  tokens_ = lapian::ui::TokensFor(density);
  ApplyTokens();
}

void PlayerShell::ApplyTokens() {
  top_bar_->ApplyTokens(tokens_);
  banner_->SetHeightToken(tokens_.banner);
  transport_->ApplyTokens(tokens_);
  ApplyWorkspaceMargins();

  ApplyTimelineConstraints();
  video_column_->setMinimumHeight(kVideoColumnMinHeightBase + tokens_.transport);
  v_split_->SetHandleWidthPx(tokens_.handle);
  h_split_->SetHandleWidthPx(tokens_.handle);

  if (inspector_collapsed_) {
    inspector_host_->setMinimumWidth(tokens_.inspector_collapsed);
    inspector_host_->setMaximumWidth(tokens_.inspector_collapsed);
  } else {
    inspector_host_->setMinimumWidth(tokens_.inspector_min);
    inspector_host_->setMaximumWidth(tokens_.inspector_max);
  }
  inspector_toggle_->setFixedHeight(tokens_.btn);
  ApplySplitSizes();
}

void PlayerShell::ApplyTimelineConstraints() {
  if (timeline_host_ == nullptr) {
    return;
  }
  if (timeline_user_resized_) {
    timeline_host_->setMinimumHeight(tokens_.timeline_min);
    timeline_host_->setMaximumHeight(tokens_.timeline_max);
    return;
  }
  // 未拖拽时时间轴高度**就是令牌**（min = max）：QSplitter 在多次嵌套 resize
  // 下会按比例重分配（实测 200% DPI 把 96 变成 123、把画面挤掉 27px），
  // 用 min/max 锁死才能保证 M4 的"每栏高度 = 令牌 ±2"。用户按下 handle 时
  // 临时放开到 96–340（见 eventFilter），松手后按新高度重新锁死。
  const int fixed_height =
      std::clamp(tokens_.timeline, tokens_.timeline_min, tokens_.timeline_max);
  timeline_host_->setFixedHeight(fixed_height);
}

bool PlayerShell::eventFilter(QObject* watched, QEvent* event) {
  if (event != nullptr && v_split_ != nullptr && watched == v_split_->handle(1)) {
    if (event->type() == QEvent::MouseButtonPress) {
      // 拖拽期间放开 96–340，让 QSplitter 能移动这一栏。
      timeline_user_resized_ = true;
      timeline_user_height_ = timeline_host_->height();
      timeline_host_->setMinimumHeight(tokens_.timeline_min);
      timeline_host_->setMaximumHeight(tokens_.timeline_max);
    } else if (event->type() == QEvent::MouseButtonRelease) {
      timeline_user_height_ = timeline_host_->height();
    }
  }
  return QWidget::eventFilter(watched, event);
}
void PlayerShell::ApplyWorkspaceMargins() {
  if (workspace_ == nullptr || workspace_->layout() == nullptr) {
    return;
  }
  const int margin = fullscreen_chrome_ ? 0 : tokens_.margin;
  workspace_->layout()->setContentsMargins(margin, margin, margin, margin);
}

void PlayerShell::ApplySplitSizes() {
  if (applying_sizes_) {
    return;
  }
  applying_sizes_ = true;

  const int handle = tokens_.handle;
  int timeline_h = timeline_user_resized_ ? timeline_user_height_ : tokens_.timeline;
  timeline_h = std::clamp(timeline_h, tokens_.timeline_min, tokens_.timeline_max);

  const int vtotal = v_split_->height();
  if (vtotal > 0) {
    const int min_video = kVideoColumnMinHeightBase + tokens_.transport;
    int video_h = vtotal - handle - timeline_h;
    if (video_h < min_video) {
      // 空间不足：先保画面下限，再让时间轴退到 min（避免把画面压成 0）。
      video_h = min_video;
      timeline_h = std::clamp(vtotal - handle - video_h, tokens_.timeline_min,
                              tokens_.timeline_max);
    }
    // 用**实际几何**判断，不用 QSplitter::sizes()：布局中间态里 sizes() 可能已经
    // 是目标值而控件几何还没跟上（200% DPI 下出现过 sizes()=292/96 但实际 265/123）。
    if (timeline_host_->height() != timeline_h ||
        video_column_->height() != video_h) {
      v_split_->setSizes(QList<int>{video_h, timeline_h});
    }
  } else {
    v_split_->setSizes(QList<int>{512, timeline_h});
  }

  const int htotal = h_split_->width();
  int inspector_w = inspector_collapsed_
                        ? tokens_.inspector_collapsed
                        : (inspector_user_resized_ ? inspector_user_width_
                                                   : tokens_.inspector);
  if (inspector_collapsed_) {
    inspector_w = tokens_.inspector_collapsed;
  } else {
    inspector_w = std::clamp(inspector_w, tokens_.inspector_min,
                             tokens_.inspector_max);
  }
  if (htotal > 0) {
    int left_w = htotal - handle - inspector_w;
    if (left_w < 420) {
      left_w = std::min(htotal - handle, 420);
      inspector_w = std::max(0, htotal - handle - left_w);
    }
    if (inspector_host_->width() != inspector_w ||
        v_split_->width() != left_w) {
      h_split_->setSizes(QList<int>{left_w, inspector_w});
    }
  } else {
    h_split_->setSizes(QList<int>{760, inspector_w});
  }

  applying_sizes_ = false;
}

void PlayerShell::ResetSplitSizes() {
  timeline_user_resized_ = false;
  inspector_user_resized_ = false;
  timeline_user_height_ = tokens_.timeline;
  inspector_user_width_ = tokens_.inspector;
  ApplySplitSizes();
}

void PlayerShell::SetSplitSizes(int timeline, int inspector) {
  timeline_user_height_ =
      std::clamp(timeline, tokens_.timeline_min, tokens_.timeline_max);
  timeline_user_resized_ = true;
  if (!inspector_collapsed_) {
    inspector_user_width_ =
        std::clamp(inspector, tokens_.inspector_min, tokens_.inspector_max);
    inspector_user_resized_ = true;
  }
  ApplySplitSizes();
}

QString PlayerShell::SplitterState() const {
  // 诊断用：时间轴/检查器实际尺寸 + 拖拽状态（--dpi-test 的 split= 键）。
  const QList<int> vsizes = v_split_ == nullptr ? QList<int>() : v_split_->sizes();
  QString vs;
  for (int value : vsizes) {
    vs += QString::number(value) + QLatin1Char('/');
  }
  return QStringLiteral("tl=%1 ins=%2 vtotal=%3 vcount=%4 user=%5 vsizes=%6")
      .arg(timeline_host_ == nullptr ? 0 : timeline_host_->height())
      .arg(inspector_host_ == nullptr ? 0 : inspector_host_->width())
      .arg(v_split_ == nullptr ? 0 : v_split_->height())
      .arg(vsizes.size())
      .arg(timeline_user_resized_ ? 1 : 0)
      .arg(vs);
}

void PlayerShell::DragSplitter(bool vertical, int delta) {
  PlayerSplitter* splitter = vertical ? v_split_ : h_split_;
  if (splitter == nullptr || splitter->count() < 2) {
    return;
  }
  QSplitterHandle* handle = splitter->handle(1);
  if (handle == nullptr) {
    return;
  }
  const QPoint local = handle->rect().center();
  const QPoint global = handle->mapToGlobal(local);
  QMouseEvent press(QEvent::MouseButtonPress, QPointF(local), QPointF(global),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(handle, &press);
  const QPoint to = vertical ? local + QPoint(0, delta) : local + QPoint(delta, 0);
  const QPoint to_global = vertical ? global + QPoint(0, delta)
                                    : global + QPoint(delta, 0);
  QMouseEvent move(QEvent::MouseMove, QPointF(to), QPointF(to_global),
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(handle, &move);
  QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to), QPointF(to_global),
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(handle, &release);
}

void PlayerShell::SetInspectorCollapsed(bool collapsed) {
  if (inspector_collapsed_ == collapsed) {
    return;
  }
  inspector_collapsed_ = collapsed;
  if (collapsed) {
    if (inspector_host_->width() > tokens_.inspector_collapsed) {
      inspector_user_width_ = inspector_host_->width();
    }
    inspector_content_->hide();
    inspector_host_->setMinimumWidth(tokens_.inspector_collapsed);
    inspector_host_->setMaximumWidth(tokens_.inspector_collapsed);
    h_split_->SetCollapsed(true);
    if (inspector_user_resized_) {
      inspector_user_width_ =
          std::clamp(inspector_user_width_, tokens_.inspector_min,
                     tokens_.inspector_max);
    }
  } else {
    inspector_host_->setMinimumWidth(tokens_.inspector_min);
    inspector_host_->setMaximumWidth(tokens_.inspector_max);
    inspector_content_->show();
    h_split_->SetCollapsed(false);
    if (inspector_user_width_ < tokens_.inspector_min) {
      inspector_user_width_ = tokens_.inspector;
    }
  }
  inspector_rail_->setFixedWidth(collapsed ? tokens_.inspector_collapsed : 26);
  UpdateInspectorToggle();
  ApplySplitSizes();
}

void PlayerShell::UpdateInspectorToggle() {
  if (inspector_toggle_ == nullptr) {
    return;
  }
  inspector_toggle_->setText(inspector_collapsed_ ? QStringLiteral("«")
                                                  : QStringLiteral("»"));
  inspector_toggle_->setToolTip(inspector_collapsed_
                                    ? QStringLiteral("展开检查器")
                                    : QStringLiteral("折叠检查器"));
}

void PlayerShell::SetFullScreenChrome(bool fullscreen) {
  fullscreen_chrome_ = fullscreen;
  top_bar_->setVisible(!fullscreen);
  // 横幅槽在无横幅时高 0；全屏时直接隐藏，避免空槽占位。
  banner_->setVisible(!fullscreen);
  inspector_host_->setVisible(!fullscreen);
  ApplyWorkspaceMargins();
  ApplySplitSizes();
  // 隐藏/显示检查器会让 QSplitter 重新分配一次空间；本帧的几何还没跟上，
  // 下一帧再落一次尺寸，保证退出全屏后画面宽度回到进入前（±1px）。
  QTimer::singleShot(0, this, [this]() { ApplySplitSizes(); });
  updateGeometry();
}

int PlayerShell::TimelineHeight() const {
  return timeline_host_ == nullptr ? 0 : timeline_host_->height();
}

int PlayerShell::InspectorWidth() const {
  return inspector_host_ == nullptr ? 0 : inspector_host_->width();
}

int PlayerShell::TransportHeight() const {
  return transport_ == nullptr ? 0 : transport_->height();
}

int PlayerShell::TopBarHeight() const {
  return top_bar_ == nullptr || top_bar_->isHidden() ? 0 : top_bar_->height();
}

int PlayerShell::BannerHeight() const {
  return banner_ == nullptr || banner_->isHidden() ? 0 : banner_->height();
}

void PlayerShell::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  // 布局落定后再按令牌夹一次：QSplitter 的 setSizes 在控件还没有真实
  // 尺寸时会被推迟/按比例改写，所以在每次 resize 后重放。
  ApplySplitSizes();
}
