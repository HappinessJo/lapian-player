#include "app/ui/TimelinePanel.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include "app/ui/Glyph.h"
#include "app/ui/Theme.h"

namespace {

QColor Dim() { return lapian::ui::Color(lapian::ui::kColorTextDim); }
QColor Accent() { return lapian::ui::Color(lapian::ui::kColorAccent); }

}  // namespace

TimelinePanel::TimelinePanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("v2TimelinePanel"));
  lapian::ui::SetStyledBackground(this);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // ── 面板头 26：◷ 时间轴 ………… 工作区 … · 视图 … − + 适配 ────────────
  head_ = new QWidget(this);
  head_->setObjectName(QStringLiteral("v2TimelineHead"));
  lapian::ui::SetStyledBackground(head_);
  auto* head_row = new QHBoxLayout(head_);
  head_row->setContentsMargins(8, 0, 8, 0);
  head_row->setSpacing(6);

  auto* title = new QLabel(head_);
  title->setObjectName(QStringLiteral("v2TimelineTitle"));
  title->setFixedSize(12, 12);
  title->setPixmap(
      lapian::ui::GlyphPixmap(lapian::ui::GlyphId::kClock, 12, Dim()));
  head_row->addWidget(title, 0, Qt::AlignVCenter);

  auto* title_text = new QLabel(QStringLiteral("时间轴"), head_);
  title_text->setObjectName(QStringLiteral("v2TimelineTitleText"));
  head_row->addWidget(title_text, 0, Qt::AlignVCenter);

  head_row->addStretch(1);

  work_meta_ = new QLabel(head_);
  work_meta_->setObjectName(QStringLiteral("v2WorkMeta"));
  head_row->addWidget(work_meta_, 0, Qt::AlignVCenter);

  view_meta_ = new QLabel(head_);
  view_meta_->setObjectName(QStringLiteral("v2ViewMeta"));
  head_row->addWidget(view_meta_, 0, Qt::AlignVCenter);

  auto make_button = [this, head_row](const char* name, const char* tip) {
    auto* button = new QToolButton(head_);
    button->setObjectName(QString::fromLatin1(name));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setFocusPolicy(Qt::NoFocus);
    button->setAutoRaise(true);
    button->setFixedSize(20, 20);
    button->setIconSize(QSize(14, 14));
    button->setToolTip(QString::fromUtf8(tip));
    head_row->addWidget(button, 0, Qt::AlignVCenter);
    return button;
  };
  zoom_out_ = make_button("v2ZoomOut", "缩小时间轴（− / 滚轮）");
  zoom_in_ = make_button("v2ZoomIn", "放大时间轴（+ / 滚轮）");
  fit_ = make_button("v2ZoomFit", "适配窗口（全片可见）");
  lapian::ui::BindGlyph(zoom_out_, lapian::ui::GlyphId::kChevronLeft, Dim(),
                        Accent(), 14);
  lapian::ui::BindGlyph(zoom_in_, lapian::ui::GlyphId::kPlus, Dim(), Accent(),
                        14);
  lapian::ui::BindGlyph(fit_, lapian::ui::GlyphId::kFilter, Dim(), Accent(), 14);
  connect(fit_, &QToolButton::clicked, this, &TimelinePanel::fitRequested);

  root->addWidget(head_, 0);

  canvas_host_ = new QWidget(this);
  canvas_host_->setObjectName(QStringLiteral("v2TimelineCanvas"));
  auto* canvas_layout = new QVBoxLayout(canvas_host_);
  canvas_layout->setContentsMargins(0, 0, 0, 0);
  canvas_layout->setSpacing(0);
  root->addWidget(canvas_host_, 1);

  UpdateMeta(QString(), QString());
}

void TimelinePanel::BindActions(QAction* zoom_in, QAction* zoom_out) {
  // 与 Transport 一样不用 setDefaultAction（会清掉自绘图标）。
  if (zoom_in_ != nullptr && zoom_in != nullptr) {
    lapian::ui::BindToolAction(zoom_in_, zoom_in,
                               lapian::ui::GlyphId::kPlus, Dim(), Accent(), 14);
    zoom_in_->setFixedSize(20, 20);
  }
  if (zoom_out_ != nullptr && zoom_out != nullptr) {
    lapian::ui::BindToolAction(zoom_out_, zoom_out,
                               lapian::ui::GlyphId::kChevronLeft, Dim(),
                               Accent(), 14);
    zoom_out_->setFixedSize(20, 20);
  }
}
void TimelinePanel::UpdateMeta(const QString& work_text,
                               const QString& view_text) {
  if (work_meta_ != nullptr && work_meta_->text() != work_text) {
    work_meta_->setText(work_text);
  }
  if (view_meta_ != nullptr && view_meta_->text() != view_text) {
    view_meta_->setText(view_text);
  }
}

void TimelinePanel::ApplyTokens(const lapian::ui::Tokens& tokens) {
  if (head_ != nullptr) {
    head_->setFixedHeight(tokens.timeline_head);
    head_->setVisible(tokens.timeline_head > 0);
  }
  const QFont font = lapian::ui::UiFont(
      tokens.density == lapian::ui::Density::kDense ? 10.0 : tokens.fs_meta);
  for (QLabel* label : {work_meta_, view_meta_}) {
    if (label != nullptr) {
      label->setFont(font);
    }
  }
  if (auto* title = findChild<QLabel*>(QStringLiteral("v2TimelineTitleText"))) {
    title->setFont(font);
  }
}

void TimelinePanel::SetHeadVisible(bool visible) {
  if (head_ != nullptr) {
    head_->setVisible(visible);
  }
}
