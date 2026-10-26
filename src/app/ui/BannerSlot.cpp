#include "app/ui/BannerSlot.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyle>

#include <algorithm>

#include "app/ui/Glyph.h"
#include "app/ui/Theme.h"
#include "app/ui/Tokens.h"

namespace {

using lapian::media::StatusNoticeKind;

QString FromUtf8(const std::string& text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

bool IsWarnKind(StatusNoticeKind kind) {
  switch (kind) {
    case StatusNoticeKind::kOutOfGuarantee:
    case StatusNoticeKind::kTenBit:
    case StatusNoticeKind::kVfr:
    case StatusNoticeKind::kFrameRateDrop:
      return true;
    default:
      return false;
  }
}

}  // namespace

BannerSlot::BannerSlot(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("v2BannerSlot"));
  lapian::ui::SetStyledBackground(this);
  setProperty("kind", QStringLiteral("none"));

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(8, 2, 8, 2);
  row->setSpacing(6);

  // 原型 `.banner` 的左侧图标（3D 补）。颜色按 kind 由 UpdateBannerStyle 换。
  icon_ = new QLabel(this);
  icon_->setObjectName(QStringLiteral("v2BannerIcon"));
  icon_->setFixedSize(14, 14);
  icon_->setPixmap(lapian::ui::GlyphPixmap(
      lapian::ui::GlyphId::kInfo, 14,
      lapian::ui::Color(lapian::ui::kColorWarn)));  icon_->hide();
  row->addWidget(icon_, 0, Qt::AlignVCenter);

  // 唯一横幅的控件树（3D 从 InfoBar 下沉）：objectName 保持
  // `infoBanner` / `infoBannerClose`，同一时刻只有一条。
  row_ = new QWidget(this);
  row_->setObjectName(QStringLiteral("infoBannerRow"));
  auto* banner_layout = new QHBoxLayout(row_);
  banner_layout->setContentsMargins(0, 0, 0, 0);
  banner_layout->setSpacing(4);
  banner_label_ = new QLabel(row_);
  banner_label_->setObjectName(QStringLiteral("infoBanner"));
  banner_label_->setTextFormat(Qt::PlainText);
  banner_label_->setWordWrap(false);
  banner_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  banner_label_->setMinimumWidth(0);
  banner_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  banner_layout->addWidget(banner_label_, 1);
  banner_close_ = new QPushButton(QStringLiteral("×"), row_);
  banner_close_->setObjectName(QStringLiteral("infoBannerClose"));
  banner_close_->setFlat(true);
  banner_close_->setFixedWidth(22);
  banner_close_->setToolTip(QStringLiteral("关闭本次降帧提示（帧号角标仍保留）"));
  banner_close_->hide();
  banner_layout->addWidget(banner_close_, 0, Qt::AlignTop);
  connect(banner_close_, &QPushButton::clicked, this, &BannerSlot::Dismiss);
  row->addWidget(row_, 1);
  row_->hide();

  setFixedHeight(0);
}

void BannerSlot::AdoptBannerRow(QWidget* banner_row) {
  // 3A 的 reparent 入口。3D 起 BannerSlot 自己建横幅控件，不再需要把
  // InfoBar 的 banner_row_ 搬进来；保留空实现让 3A 的调用点可编译。
  Q_UNUSED(banner_row);
}

void BannerSlot::SetHeightToken(int token) {
  height_token_ = std::max(0, token);
  Refresh();
}

void BannerSlot::Refresh() {
  const bool shown = primary_index_ != lapian::media::kNoNotice && row_ != nullptr;
  const int target = shown ? height_token_ : 0;
  if (height() != target || minimumHeight() != target ||
      maximumHeight() != target) {
    setFixedHeight(target);
  }
  if (row_ != nullptr) {
    row_->setVisible(shown);
  }
  if (icon_ != nullptr) {
    icon_->setVisible(shown);
  }
}

void BannerSlot::SetMedia(const lapian::media::MediaInfo& info, bool vfr) {
  info_ = info;
  has_media_ = true;
  vfr_ = vfr;
  scanning_ = false;
  synthetic_ = false;
  error_text_.clear();
  online_banner_text_.clear();
  online_banner_dismissed_ = false;
  UpdateNotices();
}

void BannerSlot::SetScanning(bool scanning) {
  if (scanning_ == scanning) {
    return;
  }
  scanning_ = scanning;
  if (!synthetic_) {
    UpdateNotices();
  }
}

void BannerSlot::SetError(const QString& message) {
  error_text_ = message;
  synthetic_ = false;
  UpdateNotices();
}

void BannerSlot::ClearError() {
  if (error_text_.isEmpty()) {
    return;
  }
  error_text_.clear();
  UpdateNotices();
}

void BannerSlot::SetOnlineFrameRateBanner(const QString& banner_text) {
  online_banner_text_ = banner_text.trimmed();
  online_banner_dismissed_ = false;
  UpdateNotices();
}

void BannerSlot::ClearOnlineFrameRateBanner() {
  online_banner_text_.clear();
  online_banner_dismissed_ = false;
  UpdateNotices();
}

void BannerSlot::Clear() {
  has_media_ = false;
  vfr_ = false;
  scanning_ = false;
  synthetic_ = false;
  error_text_.clear();
  online_banner_text_.clear();
  online_banner_dismissed_ = false;
  info_ = lapian::media::MediaInfo{};
  notices_.clear();
  ApplyNotices();
}

void BannerSlot::UpdateNotices() {
  if (synthetic_) {
    ApplyNotices();
    return;
  }
  notices_.clear();
  if (has_media_) {
    notices_ =
        lapian::media::BuildMediaStatusNotices(info_, vfr_, scanning_);
  }
  const QString trimmed = error_text_.trimmed();
  if (!trimmed.isEmpty()) {
    const QString first_line = trimmed.section(QChar('\n'), 0, 0).trimmed();
    lapian::media::StatusNotice notice =
        lapian::media::MakeStatusNotice(lapian::media::StatusNoticeKind::kError);
    notice.text = QStringLiteral("打开失败：%1").arg(first_line).toStdString();
    notice.detail = trimmed.toStdString();
    notices_.push_back(std::move(notice));
  }
  if (!online_banner_text_.isEmpty() && !online_banner_dismissed_) {
    lapian::media::StatusNotice notice = lapian::media::MakeStatusNotice(
        lapian::media::StatusNoticeKind::kFrameRateDrop);
    notice.text = online_banner_text_.toStdString();
    notice.detail =
        QStringLiteral("当前流实际 fps 以 playurl/容器为准；"
                       "标记与截图记录实际 fps；帧号只对当前流成立。")
            .toStdString();
    notices_.push_back(std::move(notice));
  }
  ApplyNotices();
}

void BannerSlot::ApplyNotices() {
  primary_index_ = lapian::media::SelectPrimaryNotice(notices_);
  if (primary_index_ == lapian::media::kNoNotice ||
      primary_index_ >= notices_.size()) {
    if (banner_label_ != nullptr) {
      banner_label_->clear();
      banner_label_->setToolTip(QString());
      banner_label_->hide();
    }
    if (banner_close_ != nullptr) {
      banner_close_->hide();
    }
    setProperty("kind", QStringLiteral("none"));
  } else {
    const lapian::media::StatusNotice& notice = notices_[primary_index_];
    primary_kind_ = notice.kind;
    if (banner_label_ != nullptr) {
      banner_label_->setText(FromUtf8(notice.text));
      banner_label_->setToolTip(
          FromUtf8(notice.detail.empty() ? notice.text : notice.detail));
      banner_label_->show();
    }
    if (banner_close_ != nullptr) {
      banner_close_->setVisible(notice.kind ==
                                lapian::media::StatusNoticeKind::kFrameRateDrop);
    }
    UpdateBannerStyle();
  }
  // 颜色的动态部分走全局 QSS 的属性选择器（不写 per-widget 样式表）。
  style()->unpolish(this);
  style()->polish(this);
  Refresh();
  emit noticesChanged();
}

void BannerSlot::UpdateBannerStyle() {
  const bool warn = IsWarnKind(primary_kind_);
  const char* kind_name = "info";
  switch (primary_kind_) {
    case lapian::media::StatusNoticeKind::kError:
      kind_name = "error";
      break;
    case lapian::media::StatusNoticeKind::kNoAudio:
    case lapian::media::StatusNoticeKind::kScanning:
      kind_name = "info";
      break;
    default:
      kind_name = warn ? "warn" : "info";
      break;
  }
  setProperty("kind", QString::fromLatin1(kind_name));
  SetIconForKind(primary_kind_);
}

void BannerSlot::SetIconForKind(lapian::media::StatusNoticeKind kind) {
  if (icon_ == nullptr) {
    return;
  }
  lapian::ui::GlyphId glyph = lapian::ui::GlyphId::kInfo;
  const char* color = lapian::ui::kColorTextDim;
  switch (kind) {
    case lapian::media::StatusNoticeKind::kError:
      glyph = lapian::ui::GlyphId::kInfo;
      color = lapian::ui::kColorErr;
      break;
    case lapian::media::StatusNoticeKind::kScanning:
      glyph = lapian::ui::GlyphId::kClock;
      color = lapian::ui::kColorTextDim;
      break;
    case lapian::media::StatusNoticeKind::kNoAudio:
      glyph = lapian::ui::GlyphId::kInfo;
      color = lapian::ui::kColorTextDim;
      break;
    default:
      glyph = lapian::ui::GlyphId::kClock;
      color = lapian::ui::kColorWarn;
      break;
  }
  icon_->setPixmap(lapian::ui::GlyphPixmap(
      glyph, 14, lapian::ui::Color(color)));
}

bool BannerSlot::BannerVisible() const {
  return primary_index_ != lapian::media::kNoNotice && banner_label_ != nullptr &&
         !banner_label_->isHidden();
}

bool BannerSlot::IsVisibleKind(lapian::media::StatusNoticeKind kind) const {
  if (primary_index_ == lapian::media::kNoNotice ||
      primary_index_ >= notices_.size()) {
    return false;
  }
  return notices_[primary_index_].kind == kind && BannerVisible();
}

void BannerSlot::Dismiss() {
  if (online_banner_text_.isEmpty()) {
    return;
  }
  online_banner_dismissed_ = true;
  UpdateNotices();
}

bool BannerSlot::BannerCloseVisible() const {
  return banner_close_ != nullptr && banner_close_->isVisible();
}

QString BannerSlot::KindName() const {
  if (primary_index_ == lapian::media::kNoNotice ||
      primary_index_ >= notices_.size()) {
    return QStringLiteral("none");
  }
  return QString::fromLatin1(
      lapian::media::StatusNoticeKindName(notices_[primary_index_].kind));
}

QString BannerSlot::Text() const {
  return banner_label_ == nullptr ? QString() : banner_label_->text();
}

QString BannerSlot::Tooltip() const {
  return banner_label_ == nullptr ? QString() : banner_label_->toolTip();
}

QString BannerSlot::SecondarySummary() const {
  return FromUtf8(
      lapian::media::SecondaryNoticeSummary(notices_, primary_index_));
}

QString BannerSlot::SecondaryTooltip() const {
  return FromUtf8(
      lapian::media::SecondaryNoticeTooltip(notices_, primary_index_));
}

QStringList BannerSlot::ActiveNoticeKinds() const {
  QStringList kinds;
  for (const lapian::media::StatusNotice& notice : notices_) {
    kinds << QString::fromLatin1(
        lapian::media::StatusNoticeKindName(notice.kind));
  }
  return kinds;
}

QRect BannerSlot::BannerGeometry() const {
  if (banner_label_ == nullptr) {
    return QRect();
  }
  return QRect(banner_label_->mapTo(this, QPoint(0, 0)),
               banner_label_->size());
}

QWidget* BannerSlot::bannerLabel() const {
  return banner_label_;
}

void BannerSlot::SetSyntheticNoticesForTest(const QStringList& kinds) {
  synthetic_ = true;
  notices_.clear();
  for (const QString& name : kinds) {
    lapian::media::StatusNoticeKind kind =
        lapian::media::StatusNoticeKind::kNoAudio;
    if (lapian::media::ParseStatusNoticeKind(name.toStdString(), &kind)) {
      notices_.push_back(lapian::media::MakeStatusNotice(kind));
    }
  }
  ApplyNotices();
}

void BannerSlot::ClearSyntheticNoticesForTest() {
  if (!synthetic_) {
    return;
  }
  synthetic_ = false;
  UpdateNotices();
}

void BannerSlot::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (layout() != nullptr) {
    layout()->activate();
  }
}
