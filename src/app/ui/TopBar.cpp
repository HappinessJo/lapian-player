#include "app/ui/TopBar.h"

#include <QAction>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QToolButton>

#include <algorithm>

#include "app/ui/Glyph.h"
#include "app/ui/Theme.h"

namespace {

// 顶栏窄模式：<1000 逻辑像素时收起规格 chip（空出宽度给文件名与模式 chip）。
constexpr int kNarrowWidth = 1000;

QColor DimColor() { return lapian::ui::Color(lapian::ui::kColorTextDim); }

}  // namespace

namespace lapian::ui {

TopActionChip::TopActionChip(GlyphId glyph, const QString& text,
                             const QString& kbd, QWidget* parent)
    : QWidget(parent) {
  setObjectName(text == QStringLiteral("打开") ? QStringLiteral("v2OpenButton")
                                               : QStringLiteral("v2OnlineButton"));
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::NoFocus);
  lapian::ui::SetStyledBackground(this);

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(6, 0, 6, 0);
  row->setSpacing(5);

  icon_ = new QLabel(this);
  icon_->setObjectName(QStringLiteral("v2ActionIcon"));
  icon_->setFixedSize(16, 16);
  icon_->setPixmap(lapian::ui::GlyphPixmap(glyph, 16, DimColor()));
  row->addWidget(icon_, 0, Qt::AlignVCenter);

  text_ = new QLabel(text, this);
  text_->setFont(lapian::ui::UiFont(12.0));
  row->addWidget(text_, 0, Qt::AlignVCenter);

  if (!kbd.isEmpty()) {
    kbd_ = new QLabel(kbd, this);
    kbd_->setObjectName(QStringLiteral("v2KbdBadge"));
    kbd_->setFont(lapian::ui::MonoFont(9.0));
    kbd_->setContentsMargins(3, 2, 3, 2);
    lapian::ui::SetStyledBackground(kbd_);
    row->addWidget(kbd_, 0, Qt::AlignVCenter);
  }
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void TopActionChip::SetAction(QAction* action) {
  action_ = action;
  if (action_ != nullptr) {
    connect(action_, &QAction::changed, this, [this]() { RefreshEnabled(); });
  }
  RefreshEnabled();
}

void TopActionChip::RefreshEnabled() {
  const bool enabled = action_ == nullptr || action_->isEnabled();
  if (isEnabled() != enabled) {
    setEnabled(enabled);
    setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
  }
}

void TopActionChip::mousePressEvent(QMouseEvent* event) {
  if (event != nullptr && event->button() == Qt::LeftButton && isEnabled()) {
    pressed_ = true;
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void TopActionChip::mouseReleaseEvent(QMouseEvent* event) {
  if (event != nullptr && event->button() == Qt::LeftButton && pressed_) {
    pressed_ = false;
    if (rect().contains(event->position().toPoint()) && action_ != nullptr &&
        action_->isEnabled()) {
      action_->trigger();
      event->accept();
      return;
    }
  }
  QWidget::mouseReleaseEvent(event);
}

void TopActionChip::enterEvent(QEnterEvent* event) {
  QWidget::enterEvent(event);
  update();
}

void TopActionChip::leaveEvent(QEvent* event) {
  QWidget::leaveEvent(event);
  update();
}

}  // namespace lapian::ui

TopBar::TopBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("v2TopBar"));
  lapian::ui::SetStyledBackground(this);

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(8, 0, 8, 0);
  row->setSpacing(6);

  // 文件：小图标 + 文件名（13px 半粗）。不设品牌区（规划 D2）。
  file_icon_ = new QLabel(this);
  file_icon_->setObjectName(QStringLiteral("v2FileIcon"));
  file_icon_->setFixedSize(16, 16);
  file_icon_->setPixmap(lapian::ui::GlyphPixmap(lapian::ui::GlyphId::kPanel, 15,
                                                DimColor()));
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

  // 规格 chip：icon + 「1920×1080 · h264 · 30.000fps」，胶囊形。
  auto* spec_chip = new QWidget(this);
  spec_chip->setObjectName(QStringLiteral("v2SpecChip"));
  lapian::ui::SetStyledBackground(spec_chip);
  spec_chip->setVisible(false);
  auto* spec_layout = new QHBoxLayout(spec_chip);
  spec_layout->setContentsMargins(7, 0, 7, 0);
  spec_layout->setSpacing(4);
  auto* spec_icon = new QLabel(spec_chip);
  spec_icon->setObjectName(QStringLiteral("v2SpecIcon"));
  spec_icon->setFixedSize(12, 12);
  spec_icon->setPixmap(
      lapian::ui::GlyphPixmap(lapian::ui::GlyphId::kFilter, 12, DimColor()));
  spec_layout->addWidget(spec_icon, 0, Qt::AlignVCenter);
  row->addWidget(spec_chip, 0, Qt::AlignVCenter);
  spec_chip_ = spec_chip;
  spec_icon_ = spec_icon;

  // 模式 chip：icon + 文字，颜色+文字双编码（P6）。
  auto* mode_chip = new QWidget(this);
  mode_chip->setObjectName(QStringLiteral("v2ModeChip"));
  lapian::ui::SetStyledBackground(mode_chip);
  auto* mode_layout = new QHBoxLayout(mode_chip);
  mode_layout->setContentsMargins(7, 0, 8, 0);
  mode_layout->setSpacing(4);
  mode_icon_ = new QLabel(mode_chip);
  mode_icon_->setObjectName(QStringLiteral("v2ModeIcon"));
  mode_icon_->setFixedSize(12, 12);
  mode_layout->addWidget(mode_icon_, 0, Qt::AlignVCenter);
  mode_text_ = new QLabel(QStringLiteral("未打开"), mode_chip);
  mode_text_->setObjectName(QStringLiteral("v2ModeText"));
  mode_text_->setFont(lapian::ui::UiFont(11.0));
  mode_layout->addWidget(mode_text_, 0, Qt::AlignVCenter);
  row->addWidget(mode_chip, 0, Qt::AlignVCenter);
  mode_chip_ = mode_chip;

  // 在线清晰度入口（T8.9/T8.12 的原对象，默认隐藏）；放在模式 chip 右侧。
  auto* quality_holder = new QWidget(this);
  quality_holder->setObjectName(QStringLiteral("v2QualitySlot"));
  auto* quality_layout = new QHBoxLayout(quality_holder);
  quality_layout->setContentsMargins(0, 0, 0, 0);
  quality_layout->setSpacing(0);
  row->addWidget(quality_holder, 0, Qt::AlignVCenter);
  quality_button_ = quality_holder;

  row->addStretch(0);

  open_chip_ = new lapian::ui::TopActionChip(
      lapian::ui::GlyphId::kOpen, QStringLiteral("打开"),
      QStringLiteral("Ctrl+O"), this);
  open_chip_->setToolTip(QStringLiteral("打开本地文件（Ctrl+O）"));
  row->addWidget(open_chip_, 0, Qt::AlignVCenter);

  online_chip_ = new lapian::ui::TopActionChip(
      lapian::ui::GlyphId::kOnline, QStringLiteral("在线"),
      QStringLiteral("Ctrl+U"), this);
  online_chip_->setToolTip(QStringLiteral("在线打开（Ctrl+U）"));
  row->addWidget(online_chip_, 0, Qt::AlignVCenter);

  help_button_ = new QToolButton(this);
  help_button_->setObjectName(QStringLiteral("v2HelpButton"));
  help_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  help_button_->setFocusPolicy(Qt::NoFocus);
  help_button_->setAutoRaise(true);
  help_button_->setToolTip(QStringLiteral("键位一览"));
  lapian::ui::BindGlyph(help_button_, lapian::ui::GlyphId::kHelp, DimColor(),
                        DimColor(), 16);
  help_button_->setFixedSize(26, 26);
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

  SetMode(QStringLiteral("未打开"), ModeKind::kNeutral);
}

void TopBar::BindActions(QAction* open, QAction* online) {
  if (open_chip_ != nullptr) {
    open_chip_->SetAction(open);
  }
  if (online_chip_ != nullptr) {
    online_chip_->SetAction(online);
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
    qlabel->setFont(lapian::ui::UiFont(11.0));
    qlabel->setContentsMargins(0, 0, 0, 0);
  }
  spec_label_ = label;
  RefreshChips();
}

void TopBar::AdoptFpsBadge(QWidget* badge) {
  if (badge == nullptr) {
    return;
  }
  auto* row = qobject_cast<QHBoxLayout*>(layout());
  if (row == nullptr) {
    return;
  }
  // 角标跟着模式 chip（DEC-4），插在模式 chip 之后。
  row->insertWidget(5, badge, 0, Qt::AlignVCenter);
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
    // 原型顶栏最右是齿轮图标按钮；文案让位给图标（tooltip 保留"设置"）。
    qbutton->setFocusPolicy(Qt::NoFocus);
    qbutton->setToolTip(QStringLiteral("设置 / 在线缓存（Ctrl+,）"));
    if (auto* push = qobject_cast<QPushButton*>(button)) {
      push->setText(QString());
      push->setIcon(QIcon(lapian::ui::GlyphPixmap(
          lapian::ui::GlyphId::kSettings, 16, DimColor())));
      push->setIconSize(QSize(16, 16));
      push->setFixedSize(26, 26);
    }
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
    qbutton->setFont(lapian::ui::UiFont(11.0));
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
    label->setToolTip(file_name_.isEmpty()
                          ? QString()
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
  if (mode_text_ != nullptr) {
    mode_text_->setText(text);
  }
  const char* kind_name = "neutral";
  lapian::ui::GlyphId glyph = lapian::ui::GlyphId::kInfo;
  QColor icon_color = lapian::ui::Color(lapian::ui::kColorTextMeta);
  switch (kind) {
    case ModeKind::kNeutral:
      kind_name = "neutral";
      glyph = lapian::ui::GlyphId::kInfo;
      icon_color = lapian::ui::Color(lapian::ui::kColorTextMeta);
      break;
    case ModeKind::kLocal:
      kind_name = "ok";
      glyph = lapian::ui::GlyphId::kCheck;
      icon_color = lapian::ui::Color(lapian::ui::kColorOk);
      break;
    case ModeKind::kOnline:
      kind_name = "cache";
      glyph = lapian::ui::GlyphId::kOnline;
      icon_color = lapian::ui::Color(lapian::ui::kColorCache);
      break;
    case ModeKind::kWarn:
      kind_name = "warn";
      glyph = lapian::ui::GlyphId::kClock;
      icon_color = lapian::ui::Color(lapian::ui::kColorWarn);
      break;
    case ModeKind::kError:
      kind_name = "error";
      glyph = lapian::ui::GlyphId::kInfo;
      icon_color = lapian::ui::Color(lapian::ui::kColorErr);
      break;
  }
  if (mode_icon_ != nullptr) {
    mode_icon_->setPixmap(lapian::ui::GlyphPixmap(glyph, 12, icon_color));
  }
  // 语义色走 QSS 的属性选择器（不做 per-widget setStyleSheet；规划 §7.2 铁律）。
  for (QWidget* chip : {static_cast<QWidget*>(mode_chip_),
                        static_cast<QWidget*>(mode_text_)}) {
    if (chip == nullptr) {
      continue;
    }
    chip->setProperty("kind", QString::fromLatin1(kind_name));
    chip->style()->unpolish(chip);
    chip->style()->polish(chip);
  }
}

QString TopBar::ModeText() const {
  return mode_text_ == nullptr ? QString() : mode_text_->text();
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
  if (open_chip_ != nullptr) {
    open_chip_->setFixedHeight(tokens.btn);
  }
  if (online_chip_ != nullptr) {
    online_chip_->setFixedHeight(tokens.btn);
  }
  if (help_button_ != nullptr) {
    help_button_->setFixedSize(tokens.btn, tokens.btn);
  }
  for (QWidget* chip : {static_cast<QWidget*>(spec_chip_),
                        static_cast<QWidget*>(mode_chip_)}) {
    if (chip != nullptr) {
      chip->setFixedHeight(tokens.chip);
    }
  }
  UpdateFileElide();
}

QSize TopBar::minimumSizeHint() const {
  return QSize(240, height());
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
    label->setToolTip(file_name_.isEmpty()
                          ? QString()
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
