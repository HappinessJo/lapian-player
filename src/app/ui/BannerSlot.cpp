#include "app/ui/BannerSlot.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>

#include <algorithm>

#include "app/ui/Theme.h"

BannerSlot::BannerSlot(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("v2BannerSlot"));
  lapian::ui::SetStyledBackground(this);
  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(8, 2, 8, 2);
  row->setSpacing(4);
  setFixedHeight(0);
}

void BannerSlot::AdoptBannerRow(QWidget* banner_row) {
  if (banner_row == nullptr) {
    return;
  }
  auto* box = qobject_cast<QHBoxLayout*>(layout());
  if (box == nullptr) {
    return;
  }
  row_ = banner_row;
  box->addWidget(banner_row, 1);
  // 横幅固定在一条里：不允许换行（规划 A.1 的 banner-clipped 判据改成
  // "单行 elide 后未裁切"），否则 26px 的槽会把两行文本裁掉。
  if (auto* label = qobject_cast<QLabel*>(
          banner_row->findChild<QLabel*>(QStringLiteral("infoBanner")))) {
    label->setWordWrap(false);
    label->setMinimumWidth(0);
  }
  Refresh();
}

void BannerSlot::SetHeightToken(int token) {
  if (token == height_token_ && row_ == nullptr) {
    return;
  }
  height_token_ = std::max(0, token);
  Refresh();
}

void BannerSlot::Refresh() {
  // 用 isHidden() 而不是 isVisible()：横幅的显隐由 InfoBar::ApplyNotices 控制，
  // isHidden() 直接反映那次 hide()/show()，不受父窗口是否已经 show 影响。
  const bool shown = row_ != nullptr && !row_->isHidden();
  const int target = shown ? height_token_ : 0;
  if (height() != target || minimumHeight() != target ||
      maximumHeight() != target) {
    setFixedHeight(target);
  }
  if (row_ != nullptr) {
    row_->setVisible(shown);
  }
}

bool BannerSlot::BannerVisible() const {
  return row_ != nullptr && !row_->isHidden();
}

QWidget* BannerSlot::bannerLabel() const {
  return row_ == nullptr
             ? nullptr
             : row_->findChild<QLabel*>(QStringLiteral("infoBanner"));
}

void BannerSlot::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (layout() != nullptr) {
    layout()->activate();
  }
}
