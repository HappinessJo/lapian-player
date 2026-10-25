#include "app/ui/TopBar.h"

#include <QAction>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QToolButton>

#include <algorithm>
#include <cstdio>

#include "app/ui/Theme.h"

namespace {

// 顶栏窄模式：<1000 逻辑像素时收起规格 chip（空出宽度给文件名与模式 chip）。
// 旧壳 InfoBar 的窄模式判据是"自身宽度 <760"，在 v2 里顶栏是全宽，所以
// 阈值为窗口宽度（规划 §3.1 的 1000 与检查器自动折叠同源）。
constexpr int kNarrowWidth = 1000;

}  // namespace

TopBar::TopBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("v2TopBar"));
  lapian::ui::SetStyledBackground(this);

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(8, 0, 8, 0);
  row->setSpacing(6);

  file_icon_ = new QLabel(QStringLiteral("▣"), this);
  file_icon_->setObjectName(QStringLiteral("v2FileIcon"));

  file_icon_->setFixedWidth(16);
  file_icon_->setAlignment(Qt::AlignCenter);
  row->addWidget(file_icon_, 0, Qt::AlignVCenter);

  // 占位：真正的 infoFile 标签由 AdoptFileLabel 塞进 v2FileLabel 容器。
  auto* file_holder = new QWidget(this);
  file_holder->setObjectName(QStringLiteral("v2FileLabel"));
  file_holder->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  auto* file_layout = new QHBoxLayout(file_holder);
  file_layout->setContentsMargins(0, 0, 0, 0);
  file_layout->setSpacing(0);
  row->addWidget(file_holder, 1, Qt::AlignVCenter);
  file_label_ = file_holder;

  auto* spec_chip = new QWidget(this);
  spec_chip->setObjectName(QStringLiteral("v2SpecChip"));
  lapian::ui::SetStyledBackground(spec_chip);
  spec_chip->setVisible(false);
  auto* spec_layout = new QHBoxLayout(spec_chip);
  spec_layout->setContentsMargins(6, 1, 6, 1);
  spec_layout->setSpacing(0);
  row->addWidget(spec_chip, 0, Qt::AlignVCenter);
  spec_chip_ = spec_chip;

  mode_chip_ = new QLabel(QStringLiteral("未打开"), this);
  mode_chip_->setObjectName(QStringLiteral("v2ModeChip"));

  mode_chip_->setContentsMargins(6, 1, 6, 1);
  lapian::ui::SetStyledBackground(mode_chip_);
  mode_chip_->setVisible(true);
  row->addWidget(mode_chip_, 0, Qt::AlignVCenter);

  // 在线清晰度入口（T8.9/T8.12 的原对象，默认隐藏）；放在模式 chip 右侧。
  auto* quality_holder = new QWidget(this);
  quality_holder->setObjectName(QStringLiteral("v2QualitySlot"));
  auto* quality_layout = new QHBoxLayout(quality_holder);
  quality_layout->setContentsMargins(0, 0, 0, 0);
  quality_layout->setSpacing(0);
  row->addWidget(quality_holder, 0, Qt::AlignVCenter);
  quality_button_ = quality_holder;

  row->addStretch(0);

  open_button_ = new QToolButton(this);
  open_button_->setObjectName(QStringLiteral("v2OpenButton"));
  open_button_->setText(QStringLiteral("打开"));
  open_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  open_button_->setFocusPolicy(Qt::NoFocus);
  open_button_->setAutoRaise(true);
  open_button_->setToolTip(QStringLiteral("打开本地文件（Ctrl+O）"));
  open_button_->setCursor(Qt::ArrowCursor);

  row->addWidget(open_button_, 0, Qt::AlignVCenter);

  online_button_ = new QToolButton(this);
  online_button_->setObjectName(QStringLiteral("v2OnlineButton"));
  online_button_->setText(QStringLiteral("在线"));
  online_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  online_button_->setFocusPolicy(Qt::NoFocus);
  online_button_->setAutoRaise(true);
  online_button_->setToolTip(QStringLiteral("在线打开（Ctrl+U）"));

  row->addWidget(online_button_, 0, Qt::AlignVCenter);

  help_button_ = new QToolButton(this);
  help_button_->setObjectName(QStringLiteral("v2HelpButton"));
  help_button_->setText(QStringLiteral("?"));
  help_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  help_button_->setFocusPolicy(Qt::NoFocus);
  help_button_->setAutoRaise(true);
  help_button_->setToolTip(QStringLiteral("键位一览"));

  help_button_->setFixedWidth(26);
  row->addWidget(help_button_, 0, Qt::AlignVCenter);
  connect(help_button_, &QToolButton::clicked, this, &TopBar::helpRequested);

  // 设置入口是 T8.3.1 的原对象（objectName 保留 cacheSettingsButton）。
  auto* settings_holder = new QWidget(this);
  settings_holder->setObjectName(QStringLiteral("v2SettingsSlot"));
  auto* settings_layout = new QHBoxLayout(settings_holder);
  settings_layout->setContentsMargins(0, 0, 0, 0);
  settings_layout->setSpacing(0);
  row->addWidget(settings_holder, 0, Qt::AlignVCenter);
  settings_button_ = settings_holder;
}

void TopBar::BindActions(QAction* open, QAction* online) {
  if (open_button_ != nullptr && open != nullptr) {
    open_button_->setDefaultAction(open);
    // setDefaultAction 会覆盖 text/tooltip/enabled；重新补上按钮自身语义。
    open_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    open_button_->setFocusPolicy(Qt::NoFocus);
    open_button_->setAutoRaise(true);
    // 动作 text 是"打开视频..."；按钮文案按规划 §2.2 用紧凑的"打开"。
    open_button_->setText(QStringLiteral("打开"));
    open_button_->setToolTip(QStringLiteral("打开本地文件（Ctrl+O）"));
  }
  if (online_button_ != nullptr && online != nullptr) {
    online_button_->setDefaultAction(online);
    online_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    online_button_->setFocusPolicy(Qt::NoFocus);
    online_button_->setAutoRaise(true);
    online_button_->setText(QStringLiteral("在线"));
    online_button_->setToolTip(QStringLiteral("在线打开（Ctrl+U）"));
  }
}

void TopBar::AdoptFileLabel(QWidget* label) {
  if (label == nullptr || file_label_ == nullptr) {
    return;
  }
  auto* layout = qobject_cast<QHBoxLayout*>(file_label_->layout());
  if (layout == nullptr) {
    return;
  }
  layout->addWidget(label);
  if (auto* qlabel = qobject_cast<QLabel*>(label)) {
    qlabel->setFont(lapian::ui::UiFont(13.0, QFont::DemiBold));
    qlabel->setMinimumWidth(0);
    qlabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  }
}

void TopBar::AdoptSpecLabel(QWidget* label) {
  if (label == nullptr || spec_chip_ == nullptr) {
    return;
  }
  auto* layout = qobject_cast<QHBoxLayout*>(spec_chip_->layout());
  if (layout == nullptr) {
    return;
  }
  layout->addWidget(label);
  if (auto* qlabel = qobject_cast<QLabel*>(label)) {

    qlabel->setContentsMargins(0, 0, 0, 0);
  }
  spec_label_ = label;
  RefreshChips();
}

void TopBar::AdoptFpsBadge(QWidget* badge) {
  if (badge == nullptr) {
    return;
  }
  // 角标跟着模式 chip（DEC-4：帧号旁的常驻角标），3A 直接挂在顶栏左组末尾。
  auto* row = qobject_cast<QHBoxLayout*>(layout());
  if (row == nullptr) {
    return;
  }
  row->insertWidget(4, badge, 0, Qt::AlignVCenter);
  fps_badge_ = badge;
}

void TopBar::AdoptSettingsButton(QWidget* button) {
  if (button == nullptr || settings_button_ == nullptr) {
    return;
  }
  auto* layout = qobject_cast<QHBoxLayout*>(settings_button_->layout());
  if (layout == nullptr) {
    return;
  }
  layout->addWidget(button);
  if (auto* qbutton = qobject_cast<QAbstractButton*>(button)) {
    qbutton->setFocusPolicy(Qt::NoFocus);

  }
}

void TopBar::AdoptQualityButton(QWidget* button) {
  if (button == nullptr || quality_button_ == nullptr) {
    return;
  }
  auto* layout = qobject_cast<QHBoxLayout*>(quality_button_->layout());
  if (layout == nullptr) {
    return;
  }
  layout->addWidget(button);
  if (auto* qbutton = qobject_cast<QAbstractButton*>(button)) {
    qbutton->setFocusPolicy(Qt::NoFocus);

  }
}

void TopBar::SetFileName(const QString& file_name) {
  file_name_ = file_name;
  UpdateFileElide();
  RefreshChips();
  if (auto* label = qobject_cast<QLabel*>(
          file_label_ == nullptr ? nullptr
                                 : file_label_->findChild<QLabel*>(
                                       QStringLiteral("infoFile")))) {
    label->setToolTip(file_name_.isEmpty() ? QString()
                                           : QStringLiteral("当前文件：%1").arg(file_name_));
  }
}

QString TopBar::FileLabelText() const {
  if (file_label_ == nullptr) {
    return QString();
  }
  if (auto* label = file_label_->findChild<QLabel*>(QStringLiteral("infoFile"))) {
    return label->text();
  }
  return QString();
}

QString TopBar::SpecText() const {
  if (auto* label = qobject_cast<QLabel*>(spec_label_)) {
    return label->text();
  }
  return QString();
}

void TopBar::SetMode(const QString& text, ModeKind kind) {
  mode_kind_ = kind;
  if (mode_chip_ == nullptr) {
    return;
  }
  mode_chip_->setText(text);
  const char* kind_name = "neutral";
  switch (kind) {
    case ModeKind::kNeutral:
      kind_name = "neutral";
      break;
    case ModeKind::kLocal:
      kind_name = "local";
      break;
    case ModeKind::kOnline:
      kind_name = "online";
      break;
    case ModeKind::kWarn:
      kind_name = "warn";
      break;
    case ModeKind::kError:
      kind_name = "error";
      break;
  }
  // 语义色走 QSS 的属性选择器（不做 per-widget setStyleSheet；规划 §7.2 铁律）。
  mode_chip_->setProperty("kind", QString::fromLatin1(kind_name));
  mode_chip_->style()->unpolish(mode_chip_);
  mode_chip_->style()->polish(mode_chip_);
}

QString TopBar::ModeText() const {
  return mode_chip_ == nullptr ? QString() : mode_chip_->text();
}

QString TopBar::ModeKindName() const {
  switch (mode_kind_) {
    case ModeKind::kNeutral:
      return QStringLiteral("neutral");
    case ModeKind::kLocal:
      return QStringLiteral("local");
    case ModeKind::kOnline:
      return QStringLiteral("online");
    case ModeKind::kWarn:
      return QStringLiteral("warn");
    case ModeKind::kError:
      return QStringLiteral("error");
  }
  return QStringLiteral("neutral");
}

void TopBar::ApplyTokens(const lapian::ui::Tokens& tokens) {
  setFixedHeight(tokens.appbar);
  if (open_button_ != nullptr) {
    open_button_->setMinimumHeight(tokens.btn);
    open_button_->setMaximumHeight(tokens.btn);
  }
  if (online_button_ != nullptr) {
    online_button_->setMinimumHeight(tokens.btn);
    online_button_->setMaximumHeight(tokens.btn);
  }
  if (help_button_ != nullptr) {
    help_button_->setFixedHeight(tokens.btn);
  }
  for (QWidget* chip : {static_cast<QWidget*>(spec_chip_),
                        static_cast<QWidget*>(mode_chip_)}) {
    if (chip != nullptr) {
      chip->setMinimumHeight(tokens.chip);
      chip->setMaximumHeight(tokens.chip);
    }
  }
  UpdateFileElide();
}

void TopBar::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (layout() != nullptr) {
    layout()->activate();
  }
  const bool narrow = width() < kNarrowWidth;
  if (narrow != narrow_) {
    narrow_ = narrow;
    RefreshChips();
  }
  UpdateFileElide();
}

void TopBar::UpdateFileElide() {
  if (file_label_ == nullptr) {
    return;
  }
  auto* label = file_label_->findChild<QLabel*>(QStringLiteral("infoFile"));
  if (label == nullptr) {
    return;
  }
  const QString full =
      file_name_.isEmpty() ? QStringLiteral("未打开文件") : file_name_;
  const int available = std::max(0, label->contentsRect().width() - 2);
  const QString shown =
      label->fontMetrics().elidedText(full, Qt::ElideMiddle, available);
  if (label->text() != shown) {
    label->setText(shown);
  }
  if (label->toolTip().isEmpty() || file_name_.isEmpty()) {
    label->setToolTip(file_name_.isEmpty() ? QString()
                                           : QStringLiteral("当前文件：%1").arg(file_name_));
  }
}

void TopBar::RefreshChips() {
  if (spec_chip_ != nullptr) {
    bool has_spec = false;
    if (auto* label = qobject_cast<QLabel*>(spec_label_)) {
      has_spec = !label->text().isEmpty();
    }
    // 窄模式收起规格 chip：顶栏优先保住文件名与模式 chip。
    spec_chip_->setVisible(has_spec && !narrow_);
  }
}
