#include "app/ui/InspectorPanel.h"

#include <QAbstractButton>
#include <QAction>
#include <QDir>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <utility>

#include "app/MarkPanel.h"
#include "app/ui/Glyph.h"
#include "app/ui/Theme.h"
#include "app/ui/Tokens.h"

namespace lapian::ui {
namespace {

constexpr int kTabIconSize = 12;

QColor TabColor(bool active, bool hovered) {
  if (active) {
    return Color(kColorText);
  }
  return hovered ? Color(kColorTextDim) : Color(kColorTextMute);
}

// 自绘 tab：原型 .ins-tab（active = 面板底 + 顶部 2px accent 细线；右侧 1px 分隔）。
// 徽标是真实的 QLabel 子控件（objectName v2MarkBadge/v2ShotBadge），
// 既给截图/自动化定位，也不让 QToolButton 的默认绘制干扰布局。
class InspectorTab : public QAbstractButton {
 public:
  InspectorTab(const QString& object_name, const QString& badge_object_name,
               const QString& text, GlyphId glyph, const QString& tooltip,
               QWidget* parent)
      : QAbstractButton(parent), text_(text), glyph_(glyph) {
    setObjectName(object_name);
    setCheckable(true);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tooltip);
    // 允许在 220px 极值下被压缩（minimumSizeHint 只留一个字的宽度）；
    // 默认仍按 sizeHint 分配，否则 QBoxLayout 会把 tab 压成 0 宽。
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    badge_ = new QLabel(this);
    badge_->setObjectName(badge_object_name);
    badge_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    badge_->setAlignment(Qt::AlignCenter);
    badge_->setText(QStringLiteral("0"));
  }

  QLabel* badge() const { return badge_; }

  // 窄检查器（220px 极值）放下三个 tab：收掉图标与左右内边距。
  void SetCompact(bool compact) {
    if (compact_ == compact) {
      return;
    }
    compact_ = compact;
    PositionBadge();
    updateGeometry();
    update();
  }

  void ApplyTokens(const Tokens& tokens) {
    tokens_ = tokens;
    setFont(UiFont(tokens.fs_body));
    badge_->setFont(UiFont(tokens.fs_micro));
    badge_->setFixedHeight(std::max(12, tokens_.inspector_head - 10));
    PositionBadge();
    updateGeometry();
    update();
  }

  QSize sizeHint() const override {
    const QFontMetrics metrics(font());
    int width = TextX() + metrics.horizontalAdvance(text_) + 10;
    if (!badge_->isHidden()) {
      width += badge_->width() + 4;
    }
    return QSize(width, tokens_.inspector_head);
  }
  QSize minimumSizeHint() const override {
    return QSize(compact_ ? 26 : 40, tokens_.inspector_head);
  }

 protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect area = rect();
    const bool active = isChecked();
    painter.fillRect(area, active ? Color(kColorSurface) : Color(kColorSurface2));
    painter.fillRect(QRect(area.right(), area.top(), 1, area.height()),
                     Color(kColorBorder));
    if (active) {
      painter.fillRect(QRect(area.left(), area.top(), area.width() - 1, 2),
                       Color(kColorAccent));
    }
    const QColor foreground = TabColor(active, hovered_);
    if (!compact_) {
      const int icon_y = (area.height() - kTabIconSize) / 2;
      painter.drawPixmap(QRect(10, icon_y, kTabIconSize, kTabIconSize),
                         GlyphPixmap(glyph_, kTabIconSize,
                                     active ? Color(kColorAccent) : foreground,
                                     devicePixelRatioF()));
    }
    painter.setPen(foreground);
    painter.setFont(font());
    const QFontMetrics metrics(font());
    const int text_x = TextX();
    painter.drawText(QRect(text_x, 0, metrics.horizontalAdvance(text_) + 2,
                           area.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, text_);
  }

  void resizeEvent(QResizeEvent* event) override {
    QAbstractButton::resizeEvent(event);
    PositionBadge();
  }

  void enterEvent(QEnterEvent* event) override {
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
  }

  void leaveEvent(QEvent* event) override {
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
  }

 private:
  int TextX() const { return compact_ ? 7 : 10 + kTabIconSize + 5; }

  void PositionBadge() {
    if (badge_ == nullptr) {
      return;
    }
    const QFontMetrics metrics(font());
    const int badge_x = TextX() + metrics.horizontalAdvance(text_) + 4;
    const int badge_h = badge_->height() > 0 ? badge_->height()
                                            : std::max(12, tokens_.inspector_head - 10);
    badge_->move(badge_x, (height() - badge_h) / 2);
  }

  QString text_;
  GlyphId glyph_ = GlyphId::kFlag;
  QLabel* badge_ = nullptr;
  Tokens tokens_ = TokensFor(Density::kRegular);
  bool hovered_ = false;
  bool compact_ = false;
};

// 截图卡片：缩略图 + `f<帧号>` + 时间码。缩略图在**首次绘制**时才读盘
// （QScrollArea 只画可见区域 → 首屏只解码几张小图，无后台线程/定时器）。
class ShotTile : public QWidget {
 public:
  ShotTile(InspectorPanel::ShotItem item, std::function<void()> on_click,
           QWidget* parent)
      : QWidget(parent), item_(std::move(item)), on_click_(std::move(on_click)) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(92);
    setToolTip(QStringLiteral("%1\n第 %2 帧%3")
                   .arg(QDir::toNativeSeparators(item_.path))
                   .arg(item_.frame)
                   .arg(item_.timecode.isEmpty()
                            ? QString()
                            : QStringLiteral(" · ") + item_.timecode));
  }

  // 两列网格：按列宽派生高度（16:9 图 + 18px 说明行）。
  void SetFrameWidth(int width) {
    frame_width_ = std::max(80, width);
    const int image_height =
        std::clamp(static_cast<int>(frame_width_ * 9.0 / 16.0), 48, 190);
    setFixedHeight(image_height + 18);
    update();
  }

  QSize sizeHint() const override {
    return QSize(frame_width_ > 0 ? frame_width_ : 140, height());
  }

 protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect area = rect();
    QPainterPath clip;
    clip.addRoundedRect(QRectF(area).adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);
    painter.setClipPath(clip);
    painter.fillRect(area, hovered_ ? QColor(0x1d, 0x1d, 0x23)
                                    : QColor(0x10, 0x10, 0x13));
    const QRect image_rect(1, 1, std::max(1, area.width() - 2),
                           std::max(1, area.height() - 19));
    EnsurePixmap(image_rect.size());
    if (!pixmap_.isNull()) {
      QSize target = pixmap_.size();
      target.scale(image_rect.size(), Qt::KeepAspectRatio);
      const QRect destination(image_rect.left() +
                                  (image_rect.width() - target.width()) / 2,
                              image_rect.top() +
                                  (image_rect.height() - target.height()) / 2,
                              target.width(), target.height());
      painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
      painter.drawPixmap(destination, pixmap_);
    } else {
      painter.setPen(Color(kColorTextMute));
      painter.setFont(UiFont(10.0));
      painter.drawText(image_rect, Qt::AlignCenter,
                       QStringLiteral("缩略图不可用"));
    }
    painter.setClipping(false);
    painter.setPen(Color(kColorBorder));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(area).adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);

    const QRect caption(4, area.height() - 18, std::max(1, area.width() - 8), 16);
    painter.setFont(MonoFont(10.0));
    painter.setPen(Color(kColorTextDim));
    painter.drawText(caption, Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("f%1").arg(item_.frame));
    if (!item_.timecode.isEmpty()) {
      painter.setPen(Color(kColorTextMeta));
      painter.drawText(caption, Qt::AlignRight | Qt::AlignVCenter,
                       item_.timecode);
    }
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event != nullptr && event->button() == Qt::LeftButton &&
        rect().contains(event->position().toPoint()) && on_click_) {
      on_click_();
    }
    QWidget::mouseReleaseEvent(event);
  }

  void enterEvent(QEnterEvent* event) override {
    hovered_ = true;
    update();
    QWidget::enterEvent(event);
  }

  void leaveEvent(QEvent* event) override {
    hovered_ = false;
    update();
    QWidget::leaveEvent(event);
  }

 private:
  void EnsurePixmap(const QSize& image_size) {
    if (attempted_) {
      return;
    }
    attempted_ = true;
    QImageReader reader(item_.path);
    reader.setAutoTransform(true);
    // 只保留缩略图尺寸的像素（4K PNG 也不把整帧留在内存里）。
    const QSize target(std::max(64, image_size.width() * 2),
                       std::max(40, image_size.height() * 2));
    const QSize source = reader.size();
    if (source.isValid()) {
      QSize scaled = source;
      scaled.scale(target, Qt::KeepAspectRatio);
      if (scaled.isValid()) {
        reader.setScaledSize(scaled);
      }
    }
    QImage image = reader.read();
    if (image.isNull()) {
      return;
    }
    if (image.width() > target.width() || image.height() > target.height()) {
      image = image.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    pixmap_ = QPixmap::fromImage(image);
  }

  InspectorPanel::ShotItem item_;
  std::function<void()> on_click_;
  QPixmap pixmap_;
  int frame_width_ = 0;
  bool attempted_ = false;
  bool hovered_ = false;
};

}  // namespace

InspectorPanel::InspectorPanel(QWidget* parent) : QWidget(parent) {
  SetStyledBackground(this);
  setFocusPolicy(Qt::NoFocus);
  setMinimumWidth(0);
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // 折叠 rail：竖排“标记 N / 截图 M”+ 展开按钮（原型 .rail）。
  rail_ = new QWidget(this);
  rail_->setObjectName(QStringLiteral("v2InspectorRail"));
  SetStyledBackground(rail_);
  rail_->setFocusPolicy(Qt::NoFocus);
  auto* rail_layout = new QVBoxLayout(rail_);
  rail_layout->setContentsMargins(0, 4, 0, 4);
  rail_layout->setSpacing(6);
  expand_button_ = new QToolButton(rail_);
  expand_button_->setObjectName(QStringLiteral("v2InspectorExpand"));
  expand_button_->setToolTip(QStringLiteral("展开检查器"));
  expand_button_->setFocusPolicy(Qt::NoFocus);
  expand_button_->setAutoRaise(true);
  expand_button_->setFixedSize(24, tokens_.btn);
  BindGlyph(expand_button_, GlyphId::kChevronLeft, Color(kColorTextDim),
            Color(kColorAccent), 14);
  connect(expand_button_, &QToolButton::clicked, this,
          &InspectorPanel::expandRequested);
  rail_layout->addWidget(expand_button_, 0, Qt::AlignHCenter);
  rail_text_ = new QLabel(rail_);
  rail_text_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
  rail_text_->setFocusPolicy(Qt::NoFocus);
  rail_text_->setFont(UiFont(tokens_.fs_micro));
  rail_text_->setWordWrap(false);
  rail_layout->addWidget(rail_text_, 0, Qt::AlignHCenter);
  rail_layout->addStretch(1);
  rail_->hide();
  layout->addWidget(rail_, 0);

  body_ = new QWidget(this);
  SetStyledBackground(body_);
  auto* body_layout = new QVBoxLayout(body_);
  body_layout->setContentsMargins(0, 0, 0, 0);
  body_layout->setSpacing(0);
  layout->addWidget(body_, 1);

  head_ = BuildHead();
  body_layout->addWidget(head_, 0);

  stack_ = new QStackedWidget(body_);
  stack_->setObjectName(QStringLiteral("v2InspectorStack"));
  stack_->setFocusPolicy(Qt::NoFocus);
  stack_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  stack_->setMinimumSize(0, 0);
  // page0（标记）由 AdoptMarkPanel 插到最前；先建 page1/page2。
  shots_page_ = BuildShotsPage();
  info_page_ = BuildInfoPage();
  stack_->addWidget(shots_page_);  // 临时索引 0
  stack_->addWidget(info_page_);   // 临时索引 1
  body_layout->addWidget(stack_, 1);

  footer_ = BuildFooter();
  body_layout->addWidget(footer_, 0);

  UpdateCounts();
  UpdateRailText();
  ApplyTokens(tokens_);
  SetCurrentPage();
}

void InspectorPanel::AdoptMarkPanel(MarkPanel* panel) {
  if (panel == nullptr || mark_panel_ == panel) {
    return;
  }
  mark_panel_ = panel;
  if (marks_page_ == nullptr) {
    marks_page_ = new QWidget(this);
    SetStyledBackground(marks_page_);
    auto* marks_layout = new QVBoxLayout(marks_page_);
    marks_layout->setContentsMargins(0, 0, 0, 0);
    marks_layout->setSpacing(0);
    panel->setParent(marks_page_);
    marks_layout->addWidget(panel, 1);
    stack_->insertWidget(0, marks_page_);
  }
  // 嵌入模式只收表头/存储/提示，不改任何对象与信号；树与编辑框仍可聚焦。
  panel->SetEmbeddedMode(true);
  panel->setMinimumWidth(0);
  panel->setMaximumWidth(QWIDGETSIZE_MAX);
  panel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  mark_count_ = panel->MarkCount();
  UpdateCounts();
  UpdateRailText();
  stack_->setCurrentIndex(static_cast<int>(tab_));
}

void InspectorPanel::BindAddAction(QAction* action) {
  if (add_button_ == nullptr || action == nullptr) {
    return;
  }
  BindToolAction(add_button_, action, GlyphId::kPlus, Color(kColorTextDim),
                 Color(kColorAccent), 14);
  // 与 Transport 的 v2MarkButton 文案一致（M5 第 13 行；第二入口）。
  add_button_->setToolTip(QStringLiteral("在当前帧添加标记（Ctrl+M）"));
}

QWidget* InspectorPanel::BuildHead() {
  auto* head = new QWidget(body_);
  head->setObjectName(QStringLiteral("v2InspectorHead"));
  SetStyledBackground(head);
  head->setFocusPolicy(Qt::NoFocus);
  auto* layout = new QHBoxLayout(head);
  layout->setContentsMargins(0, 0, 4, 0);
  layout->setSpacing(0);

  tab_marks_ = new InspectorTab(QStringLiteral("v2TabMarks"),
                                QStringLiteral("v2MarkBadge"),
                                QStringLiteral("标记"), GlyphId::kFlag,
                                QStringLiteral("标记：帧列表与备注"), head);
  tab_shots_ = new InspectorTab(QStringLiteral("v2TabShots"),
                                QStringLiteral("v2ShotBadge"),
                                QStringLiteral("截图"), GlyphId::kCamera,
                                QStringLiteral("截图：当前片子的 PNG（S 截图）"),
                                head);
  tab_info_ = new InspectorTab(QStringLiteral("v2TabInfo"), QString(),
                               QStringLiteral("信息"), GlyphId::kInfo,
                               QStringLiteral("信息：容器/视频/音频属性"), head);
  auto* tab_marks = static_cast<InspectorTab*>(tab_marks_);
  auto* tab_shots = static_cast<InspectorTab*>(tab_shots_);
  auto* tab_info = static_cast<InspectorTab*>(tab_info_);
  mark_badge_ = tab_marks->badge();
  shot_badge_ = tab_shots->badge();
  tab_info->badge()->hide();
  layout->addWidget(tab_marks);
  layout->addWidget(tab_shots);
  layout->addWidget(tab_info);
  layout->addStretch(1);

  auto* actions = new QHBoxLayout();
  actions->setContentsMargins(0, 0, 0, 0);
  actions->setSpacing(2);
  add_button_ = new QToolButton(head);
  add_button_->setObjectName(QStringLiteral("v2InspectorAdd"));
  add_button_->setFocusPolicy(Qt::NoFocus);
  add_button_->setAutoRaise(true);
  add_button_->setToolTip(QStringLiteral("在当前帧添加标记（Ctrl+M）"));
  add_button_->setEnabled(false);
  BindGlyph(add_button_, GlyphId::kPlus, Color(kColorTextDim),
            Color(kColorAccent), 14);
  collapse_button_ = new QToolButton(head);
  collapse_button_->setObjectName(QStringLiteral("v2InspectorCollapse"));
  collapse_button_->setToolTip(QStringLiteral("折叠检查器"));
  collapse_button_->setFocusPolicy(Qt::NoFocus);
  collapse_button_->setAutoRaise(true);
  BindGlyph(collapse_button_, GlyphId::kChevronRight, Color(kColorTextDim),
            Color(kColorAccent), 14);
  connect(collapse_button_, &QToolButton::clicked, this,
          &InspectorPanel::collapseRequested);
  actions->addWidget(add_button_);
  actions->addWidget(collapse_button_);
  layout->addLayout(actions);

  connect(tab_marks, &QAbstractButton::clicked, this,
          [this]() { SetTab(Tab::kMarks); });
  connect(tab_shots, &QAbstractButton::clicked, this,
          [this]() { SetTab(Tab::kShots); });
  connect(tab_info, &QAbstractButton::clicked, this,
          [this]() { SetTab(Tab::kInfo); });
  return head;
}

QWidget* InspectorPanel::BuildShotsPage() {
  auto* page = new QWidget(body_);
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  shot_scroll_ = new QScrollArea(page);
  shot_scroll_->setWidgetResizable(true);
  shot_scroll_->setFrameShape(QFrame::NoFrame);
  shot_scroll_->setFocusPolicy(Qt::NoFocus);
  shot_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  shot_scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  shot_grid_ = new QWidget(shot_scroll_);
  shot_grid_->setObjectName(QStringLiteral("v2ShotGrid"));
  SetStyledBackground(shot_grid_);
  shot_layout_ = new QGridLayout(shot_grid_);
  shot_layout_->setContentsMargins(8, 8, 8, 8);
  shot_layout_->setHorizontalSpacing(6);
  shot_layout_->setVerticalSpacing(6);
  shot_layout_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  shot_scroll_->setWidget(shot_grid_);
  shot_scroll_->hide();
  layout->addWidget(shot_scroll_, 1);

  shot_empty_ = new QLabel(QStringLiteral("还没有截图（S）"), page);
  shot_empty_->setObjectName(QStringLiteral("v2ShotEmpty"));
  shot_empty_->setAlignment(Qt::AlignCenter);
  shot_empty_->setWordWrap(true);
  shot_empty_->setFont(UiFont(tokens_.fs_body));
  shot_empty_->setFocusPolicy(Qt::NoFocus);
  layout->addWidget(shot_empty_, 1);

  shot_note_ = new QLabel(page);
  shot_note_->setObjectName(QStringLiteral("v2ShotNote"));
  shot_note_->setAlignment(Qt::AlignCenter);
  shot_note_->setFont(UiFont(tokens_.fs_micro));
  shot_note_->setFocusPolicy(Qt::NoFocus);
  shot_note_->hide();
  layout->addWidget(shot_note_, 0);
  return page;
}

QWidget* InspectorPanel::BuildInfoPage() {
  auto* page = new QWidget(body_);
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  info_table_ = new QTableWidget(0, 2, page);
  info_table_->setObjectName(QStringLiteral("v2InfoTable"));
  info_table_->setFocusPolicy(Qt::NoFocus);
  info_table_->setFrameShape(QFrame::NoFrame);
  info_table_->setShowGrid(false);
  info_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  info_table_->setSelectionMode(QAbstractItemView::NoSelection);
  info_table_->setWordWrap(false);
  info_table_->setTextElideMode(Qt::ElideMiddle);
  info_table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  info_table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  info_table_->horizontalHeader()->hide();
  info_table_->verticalHeader()->hide();
  info_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  info_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  info_table_->setColumnWidth(0, 64);
  info_table_->setMinimumSize(0, 0);
  info_table_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  layout->addWidget(info_table_, 1);
  return page;
}

QWidget* InspectorPanel::BuildFooter() {
  auto* footer = new QWidget(body_);
  footer->setObjectName(QStringLiteral("v2InspectorFooter"));
  SetStyledBackground(footer);
  footer->setFocusPolicy(Qt::NoFocus);
  auto* layout = new QHBoxLayout(footer);
  layout->setContentsMargins(8, 2, 8, 2);
  layout->setSpacing(6);
  auto* icon = new QLabel(footer);
  icon->setPixmap(GlyphPixmap(GlyphId::kFolder, 12, Color(kColorTextMeta)));
  icon->setFixedSize(12, 12);
  icon->setFocusPolicy(Qt::NoFocus);
  layout->addWidget(icon, 0);
  store_text_ = new QLabel(QStringLiteral("存储：—"), footer);
  store_text_->setObjectName(QStringLiteral("v2StoreText"));
  store_text_->setFont(UiFont(tokens_.fs_micro));
  store_text_->setFocusPolicy(Qt::NoFocus);
  store_text_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  layout->addWidget(store_text_, 1);
  return footer;
}

void InspectorPanel::ApplyTokens(const Tokens& tokens) {
  tokens_ = tokens;
  if (head_ != nullptr) {
    head_->setFixedHeight(tokens_.inspector_head);
  }
  if (footer_ != nullptr) {
    footer_->setFixedHeight(tokens_.inspector_foot);
  }
  if (rail_ != nullptr) {
    rail_->setFixedWidth(tokens_.inspector_collapsed);
  }
  if (rail_text_ != nullptr) {
    rail_text_->setFont(UiFont(tokens_.fs_micro));
  }
  if (expand_button_ != nullptr) {
    expand_button_->setFixedSize(24, tokens_.btn);
  }
  for (QAbstractButton* tab : {tab_marks_, tab_shots_, tab_info_}) {
    auto* inspector_tab = static_cast<InspectorTab*>(tab);
    if (inspector_tab != nullptr) {
      inspector_tab->ApplyTokens(tokens_);
    }
  }
  const int action_size = std::max(16, tokens_.btn - 4);
  for (QToolButton* button : {add_button_, collapse_button_, expand_button_}) {
    if (button != nullptr) {
      button->setFixedSize(action_size, action_size);
    }
  }
  if (store_text_ != nullptr) {
    store_text_->setFont(UiFont(tokens_.fs_micro));
  }
  if (shot_empty_ != nullptr) {
    shot_empty_->setFont(UiFont(tokens_.fs_body));
  }
  if (shot_note_ != nullptr) {
    shot_note_->setFont(UiFont(tokens_.fs_micro));
  }
  if (info_table_ != nullptr) {
    const QFont label_font = UiFont(tokens_.fs_meta);
    const QFont value_font = MonoFont(tokens_.fs_meta);
    for (int row = 0; row < info_table_->rowCount(); ++row) {
      if (QTableWidgetItem* item = info_table_->item(row, 0)) {
        item->setFont(label_font);
      }
      if (QTableWidgetItem* item = info_table_->item(row, 1)) {
        item->setFont(value_font);
      }
    }
    for (int row = 0; row < info_table_->rowCount(); ++row) {
      info_table_->setRowHeight(row, std::max(18, tokens_.inspector_head - 4));
    }
  }
  UpdateStoreElide();
  UpdateRailText();
}

void InspectorPanel::SetCollapsed(bool collapsed) {
  if (collapsed_ == collapsed) {
    return;
  }
  if (collapsed && mark_panel_ != nullptr) {
    // T5.3 契约：折叠（编辑框会被隐藏）前先提交未保存文本。
    mark_panel_->FlushEditor();
  }
  collapsed_ = collapsed;
  if (mark_panel_ != nullptr) {
    // 明确只 hide 不销毁（数据/选中/信号全保留，--mark-test 断言依赖）。
    mark_panel_->setVisible(!collapsed_);
  }
  rail_->setVisible(collapsed_);
  body_->setVisible(!collapsed_);
  if (!collapsed_) {
    SetCurrentPage();
    if (tab_ == Tab::kShots && !shot_tiles_built_) {
      RebuildShotTiles();
    }
  }
  updateGeometry();
}

void InspectorPanel::SetTab(Tab tab) {
  if (tab_ == tab) {
    SetCurrentPage();
    return;
  }
  if (mark_panel_ != nullptr) {
    // T5.3 契约：切 tab 之前必须 flush（同一切换选中/切文件/关窗的口径）。
    mark_panel_->FlushEditor();
  }
  tab_ = tab;
  SetCurrentPage();
  emit tabChanged(static_cast<int>(tab_));
}

void InspectorPanel::SetCurrentPage() {
  if (stack_ == nullptr) {
    return;
  }
  stack_->setCurrentIndex(static_cast<int>(tab_));
  tab_marks_->setChecked(tab_ == Tab::kMarks);
  tab_shots_->setChecked(tab_ == Tab::kShots);
  tab_info_->setChecked(tab_ == Tab::kInfo);
  tab_marks_->update();
  tab_shots_->update();
  tab_info_->update();
  if (tab_ == Tab::kShots && !shot_tiles_built_) {
    RebuildShotTiles();
  }
}

void InspectorPanel::SetMarkCount(int count) {
  const int next = std::max(0, count);
  if (next == mark_count_) {
    return;
  }
  mark_count_ = next;
  UpdateCounts();
  UpdateRailText();
}

void InspectorPanel::SetShotCount(int count) {
  const int next = std::max(0, count);
  if (next == shot_total_count_) {
    return;
  }
  shot_total_count_ = next;
  UpdateCounts();
  UpdateRailText();
}

void InspectorPanel::SetShotItems(const QVector<ShotItem>& items) {
  bool same = items.size() == shot_items_.size();
  for (int i = 0; same && i < items.size(); ++i) {
    same = items[i].path == shot_items_[i].path &&
           items[i].frame == shot_items_[i].frame &&
           items[i].timecode == shot_items_[i].timecode;
  }
  shot_items_ = items;
  shot_total_count_ = items.size();
  if (shot_empty_ != nullptr) {
    shot_empty_->setVisible(shot_total_count_ == 0);
  }
  if (shot_scroll_ != nullptr) {
    shot_scroll_->setVisible(shot_total_count_ > 0);
  }
  if (shot_note_ != nullptr) {
    shot_note_->setVisible(shot_total_count_ > kMaxShotTiles);
    shot_note_->setText(QStringLiteral("共 %1 张 · 显示最近 %2 张")
                            .arg(shot_total_count_)
                            .arg(kMaxShotTiles));
  }
  UpdateCounts();
  UpdateRailText();
  if (same) {
    return;
  }
  shot_tiles_built_ = false;
  if (tab_ == Tab::kShots) {
    RebuildShotTiles();
  }
}

void InspectorPanel::SetInfoRows(const QVector<InfoRow>& rows) {
  if (info_table_ == nullptr) {
    return;
  }
  bool same = rows.size() == info_rows_.size();
  for (int i = 0; same && i < rows.size(); ++i) {
    same = rows[i].label == info_rows_[i].label &&
           rows[i].value == info_rows_[i].value;
  }
  if (same) {
    return;
  }
  info_rows_ = rows;
  info_table_->setRowCount(rows.size());
  const QFont label_font = UiFont(tokens_.fs_meta);
  const QFont value_font = MonoFont(tokens_.fs_meta);
  const QColor label_color = Color(kColorTextMeta);
  const QColor value_color = Color(kColorText);
  for (int row = 0; row < rows.size(); ++row) {
    auto* label = new QTableWidgetItem(rows[row].label);
    label->setFont(label_font);
    label->setForeground(label_color);
    label->setFlags(Qt::ItemIsEnabled);
    auto* value = new QTableWidgetItem(rows[row].value);
    value->setFont(value_font);
    value->setForeground(value_color);
    value->setFlags(Qt::ItemIsEnabled);
    // 原型 .kv 是 space-between：标签左、数值右。
    value->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setToolTip(rows[row].value);
    info_table_->setItem(row, 0, label);
    info_table_->setItem(row, 1, value);
    info_table_->setRowHeight(row, std::max(18, tokens_.inspector_head - 4));
  }
}

void InspectorPanel::SetStorageText(const QString& text, bool warning) {
  if (text == store_full_text_ && warning == store_warning_) {
    return;
  }
  store_full_text_ = text;
  if (store_text_ != nullptr) {
    store_text_->setToolTip(text);
    store_warning_ = warning;
    // 动态属性 + 全局 QSS（禁止 per-widget setStyleSheet）。
    store_text_->setProperty("warning", warning);
    store_text_->style()->unpolish(store_text_);
    store_text_->style()->polish(store_text_);
  }
  UpdateStoreElide();
}

void InspectorPanel::UpdateCounts() {
  auto set_badge = [](QLabel* badge, int count) {
    if (badge == nullptr) {
      return;
    }
    badge->setText(QString::number(count));
    const QFontMetrics metrics(badge->font());
    badge->setFixedWidth(metrics.horizontalAdvance(badge->text()) + 2);
    badge->setVisible(true);
  };
  set_badge(mark_badge_, mark_count_);
  set_badge(shot_badge_, shot_total_count_);
  // 徽标宽度变化会影响 tab 的 sizeHint。
  for (QAbstractButton* tab : {tab_marks_, tab_shots_, tab_info_}) {
    if (tab != nullptr) {
      tab->updateGeometry();
      tab->update();
    }
  }
}

void InspectorPanel::UpdateRailText() {
  if (rail_text_ == nullptr) {
    return;
  }
  rail_text_->setText(QStringLiteral("标记\n%1\n截图\n%2")
                          .arg(mark_count_)
                          .arg(shot_total_count_));
}

void InspectorPanel::UpdateStoreElide() {
  if (store_text_ == nullptr) {
    return;
  }
  int available = 0;
  if (footer_ != nullptr && footer_->width() > 0) {
    available = footer_->width() - 16 - 12 - 6;  // 左右边距 + 图标 + 间距
  }
  if (available <= 8) {
    available = width() - 34;
  }
  if (available <= 8) {
    store_text_->setText(store_full_text_);
    return;
  }
  const QFontMetrics metrics(store_text_->font());
  store_text_->setText(
      metrics.elidedText(store_full_text_, Qt::ElideMiddle, available));
}

void InspectorPanel::UpdateShotGeometry() {
  if (shot_grid_ == nullptr || shot_layout_ == nullptr) {
    return;
  }
  const int grid_width = shot_grid_->width();
  if (grid_width > 0 && !shot_tiles_.isEmpty()) {
    const int available = grid_width - 16 - 6;  // 左右边距 8 + 列间距 6
    const int column_width = std::max(80, available / 2);
    for (QWidget* widget : shot_tiles_) {
      auto* tile = static_cast<ShotTile*>(widget);
      if (tile != nullptr) {
        tile->SetFrameWidth(column_width);
      }
    }
  }
  // QScrollArea(widgetResizable) 会把子控件压到视口大小；容器必须自己声明
  // "两列 × N 行"的最小高度，否则行与行会互相重叠（<60 张时也一样）。
  const int rows = (shot_tiles_.size() + 1) / 2;
  int row_height = 0;
  if (!shot_tiles_.isEmpty()) {
    row_height = shot_tiles_.front()->height();
  }
  const int total =
      rows <= 0 ? 0
                : 16 + rows * row_height +
                      (rows - 1) * shot_layout_->verticalSpacing();
  shot_grid_->setMinimumHeight(total);
}

void InspectorPanel::RebuildShotTiles() {
  if (shot_layout_ == nullptr) {
    return;
  }
  for (QWidget* tile : shot_tiles_) {
    shot_layout_->removeWidget(tile);
    tile->deleteLater();
  }
  shot_tiles_.clear();
  const int count = std::min<int>(shot_items_.size(), kMaxShotTiles);
  const int start = static_cast<int>(shot_items_.size()) - count;
  for (int i = start; i < shot_items_.size(); ++i) {
    const ShotItem item = shot_items_.at(i);
    const qint64 frame = item.frame;
    const QString path = item.path;
    auto* tile = new ShotTile(item,
                              [this, frame, path]() {
                                emit shotActivated(frame, path);
                              },
                              shot_grid_);
    const int index = i - start;
    shot_layout_->addWidget(tile, index / 2, index % 2);
    shot_tiles_.push_back(tile);
  }
  shot_tiles_built_ = true;
  UpdateShotGeometry();
}

void InspectorPanel::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  // 220px 极值时收掉 tab 图标与内边距，三个 tab + 两个动作按钮仍放得下。
  const bool compact = width() < 300;
  for (QAbstractButton* tab : {tab_marks_, tab_shots_, tab_info_}) {
    if (tab != nullptr) {
      static_cast<InspectorTab*>(tab)->SetCompact(compact);
    }
  }
  UpdateStoreElide();
  UpdateShotGeometry();
}

QString InspectorPanel::StateForTest() const {
  const int page = stack_ == nullptr ? -1 : stack_->currentIndex();
  const QString tab_name =
      tab_ == Tab::kMarks ? QStringLiteral("marks")
                          : (tab_ == Tab::kShots ? QStringLiteral("shots")
                                                 : QStringLiteral("info"));
  QString text = QStringLiteral("collapsed=%1 tab=%2 page=%3 w=%4 h=%5")
                     .arg(collapsed_ ? 1 : 0)
                     .arg(tab_name)
                     .arg(page)
                     .arg(width())
                     .arg(height());
  text += QStringLiteral(" marks=%1 shots=%2 tiles=%3")
              .arg(mark_count_)
              .arg(shot_total_count_)
              .arg(shot_tiles_.size());
  text += QStringLiteral(" badge=%1 shotbadge=%2 rail=%3 info=%4")
              .arg(mark_badge_ == nullptr ? QString() : mark_badge_->text())
              .arg(shot_badge_ == nullptr ? QString() : shot_badge_->text())
              .arg(rail_ != nullptr && !rail_->isHidden() ? 1 : 0)
              .arg(info_rows_.size());
  text += QStringLiteral(" store=%1").arg(store_full_text_);
  if (!shot_tiles_.isEmpty()) {
    const QWidget* first = shot_tiles_.front();
    text += QStringLiteral(" tile0=%1x%2 grid=%3x%4 viewport=%5")
                .arg(first->width())
                .arg(first->height())
                .arg(shot_grid_ == nullptr ? 0 : shot_grid_->width())
                .arg(shot_grid_ == nullptr ? 0 : shot_grid_->height())
                .arg(shot_scroll_ == nullptr
                         ? 0
                         : shot_scroll_->viewport()->height());
  }
  return text;
}

}  // namespace lapian::ui
