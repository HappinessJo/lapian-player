#include "app/InfoBar.h"

#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QString>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

QString FormatFps(const lapian::core::FrameRate& rate) {
  if (!rate.valid()) {
    return QStringLiteral("—");
  }
  return QString::number(rate.ToDouble(), 'f', 3);
}

QString FromUtf8(const std::string& text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

}  // namespace

InfoBar::InfoBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("infoBar"));
  setStyleSheet(QStringLiteral(
      "#infoBar { background:#202024; border-top:1px solid #3a3a40; }"
      "#infoBar QLabel { color:#d8d8dc; }"));

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(8, 4, 8, 4);
  root->setSpacing(2);

  grid_ = new QGridLayout();
  grid_->setContentsMargins(0, 0, 0, 0);
  grid_->setHorizontalSpacing(10);
  grid_->setVerticalSpacing(2);

  file_label_ = new QLabel(this);
  file_label_->setObjectName(QStringLiteral("infoFile"));
  // 文件名可能很长：允许布局压缩，再用 elidedText 省略，避免推高窗口最小宽度。
  file_label_->setMinimumWidth(0);
  file_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  format_label_ = new QLabel(this);
  format_label_->setObjectName(QStringLiteral("infoFormat"));
  frame_label_ = new QLabel(this);
  frame_label_->setObjectName(QStringLiteral("infoFrame"));
  QFont frame_font = frame_label_->font();
  frame_font.setPointSize(16);
  frame_font.setBold(true);
  frame_label_->setFont(frame_font);
  timecode_label_ = new QLabel(this);
  timecode_label_->setObjectName(QStringLiteral("infoTimecode"));
  timecode_label_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  total_label_ = new QLabel(this);
  total_label_->setObjectName(QStringLiteral("infoTotal"));

  // T8.12/DEC-4：帧号右侧常驻角标。只显示实际流 fps（有证据时附“降帧”）。
  online_fps_badge_ = new QLabel(this);
  online_fps_badge_->setObjectName(QStringLiteral("onlineFpsBadge"));
  online_fps_badge_->setTextFormat(Qt::PlainText);
  online_fps_badge_->setStyleSheet(
      QStringLiteral("background:#2a2a30; color:#ffd37a; padding:1px 5px; "
                     "border:1px solid #5a4a1a; border-radius:3px;"));
  online_fps_badge_->setToolTip(
      QStringLiteral("当前在线流实际 fps（playurl/容器）；不用于重算帧号"));
  online_fps_badge_->hide();

  root->addLayout(grid_);

  // T6.5：唯一横幅。六类提示共享这一个 QLabel，天然不会叠 banner；
  // 参与 root 纵向布局，出现时把时间轴/画面向上推，而不是覆盖它们。
  // T8.12：降帧横幅额外提供关闭按钮；其它提示不显示关闭按钮。
  banner_row_ = new QWidget(this);
  banner_row_->setObjectName(QStringLiteral("infoBannerRow"));
  auto* banner_layout = new QHBoxLayout(banner_row_);
  banner_layout->setContentsMargins(0, 0, 0, 0);
  banner_layout->setSpacing(4);
  banner_label_ = new QLabel(banner_row_);
  banner_label_->setObjectName(QStringLiteral("infoBanner"));
  banner_label_->setTextFormat(Qt::PlainText);
  banner_label_->setWordWrap(true);
  banner_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  banner_label_->setMinimumWidth(0);
  banner_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  banner_layout->addWidget(banner_label_, 1);
  banner_close_ = new QPushButton(QStringLiteral("×"), banner_row_);
  banner_close_->setObjectName(QStringLiteral("infoBannerClose"));
  banner_close_->setFlat(true);
  banner_close_->setFixedWidth(22);
  banner_close_->setToolTip(QStringLiteral("关闭本次降帧提示（帧号角标仍保留）"));
  banner_close_->hide();
  banner_layout->addWidget(banner_close_, 0, Qt::AlignTop);
  connect(banner_close_, &QPushButton::clicked, this, [this]() {
    online_banner_dismissed_ = true;
    UpdateNotices();
  });
  banner_row_->hide();
  root->addWidget(banner_row_);

  UpdateLayoutMode();
  Clear();
}

void InfoBar::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  UpdateLayoutMode();
  UpdateFileText();
  if (banner_label_ != nullptr) {
    banner_label_->updateGeometry();
  }
}

void InfoBar::UpdateLayoutMode() {
  // 1920×1080@150% 的逻辑宽度约 1280，@200% 约 960；侧栏占掉 240 后，
  // 信息栏在 <760 逻辑像素时把格式/总帧移到第二行，避免裁切或强行撑宽窗口。
  const bool narrow = width() < 760;
  if (narrow == narrow_) {
    return;
  }
  narrow_ = narrow;

  for (QLabel* label :
       {file_label_, format_label_, frame_label_, timecode_label_, total_label_,
        online_fps_badge_}) {
    grid_->removeWidget(label);
  }
  for (int column = 0; column < 6; ++column) {
    grid_->setColumnStretch(column, 0);
  }

  if (narrow_) {
    grid_->addWidget(file_label_, 0, 0, 1, 2);
    grid_->addWidget(frame_label_, 0, 2);
    grid_->addWidget(online_fps_badge_, 0, 3);
    grid_->addWidget(format_label_, 1, 0, 1, 2);
    grid_->addWidget(timecode_label_, 1, 2);
    grid_->addWidget(total_label_, 1, 3);
  } else {
    grid_->addWidget(file_label_, 0, 0);
    grid_->addWidget(format_label_, 0, 1);
    grid_->addWidget(frame_label_, 0, 2);
    grid_->addWidget(online_fps_badge_, 0, 3);
    grid_->addWidget(timecode_label_, 0, 4);
    grid_->addWidget(total_label_, 0, 5);
  }
  grid_->setColumnStretch(0, 1);
  grid_->invalidate();
  updateGeometry();
}

void InfoBar::UpdateFileText() {
  const QString full =
      file_name_.isEmpty() ? QStringLiteral("未打开文件") : file_name_;
  const int available = std::max(0, file_label_->contentsRect().width() - 2);
  const QString shown =
      file_label_->fontMetrics().elidedText(full, Qt::ElideMiddle, available);
  if (file_label_->text() != shown) {
    file_label_->setText(shown);
  }
  if (file_label_->toolTip() != full) {
    file_label_->setToolTip(full);
  }
}

QString InfoBar::FileLabelTextForTest() const {
  return file_label_ == nullptr ? QString() : file_label_->text();
}

QString InfoBar::BannerKindForTest() const {
  if (primary_index_ == lapian::media::kNoNotice ||
      primary_index_ >= notices_.size()) {
    return QStringLiteral("none");
  }
  return QString::fromLatin1(
      lapian::media::StatusNoticeKindName(notices_[primary_index_].kind));
}

QString InfoBar::BannerTextForTest() const {
  return banner_label_ == nullptr ? QString() : banner_label_->text();
}

QString InfoBar::BannerTooltipForTest() const {
  return banner_label_ == nullptr ? QString() : banner_label_->toolTip();
}

bool InfoBar::BannerVisibleForTest() const {
  return banner_label_ != nullptr && banner_label_->isVisible();
}

QRect InfoBar::BannerGeometryForTest() const {
  return banner_label_ == nullptr ? QRect() : banner_label_->geometry();
}

QString InfoBar::SecondarySummaryForTest() const {
  return FromUtf8(lapian::media::SecondaryNoticeSummary(notices_, primary_index_));
}

QString InfoBar::SecondaryTooltipForTest() const {
  return FromUtf8(
      lapian::media::SecondaryNoticeTooltip(notices_, primary_index_));
}

QStringList InfoBar::ActiveNoticeKindsForTest() const {
  QStringList kinds;
  for (const lapian::media::StatusNotice& notice : notices_) {
    kinds << QString::fromLatin1(lapian::media::StatusNoticeKindName(notice.kind));
  }
  return kinds;
}

void InfoBar::Clear() {
  rate_ = lapian::core::FrameRate();
  vfr_ = false;
  has_media_ = false;
  scanning_ = false;
  synthetic_ = false;
  error_text_.clear();
  online_badge_text_.clear();
  online_banner_text_.clear();
  online_banner_dismissed_ = false;
  if (online_fps_badge_ != nullptr) {
    online_fps_badge_->clear();
    online_fps_badge_->hide();
  }
  info_ = lapian::media::MediaInfo{};
  total_ = lapian::core::TotalFrameCount{};
  file_name_.clear();
  file_label_->setText(QStringLiteral("未打开文件"));
  format_label_->clear();
  frame_label_->setText(QStringLiteral("帧 —"));
  timecode_label_->setText(QStringLiteral("--:--:--:--"));
  total_label_->clear();
  UpdateNotices();
  UpdateFileText();
}

void InfoBar::SetMedia(const lapian::media::MediaInfo& info, bool vfr) {
  info_ = info;
  has_media_ = true;
  vfr_ = vfr;
  scanning_ = false;
  synthetic_ = false;
  error_text_.clear();
  // 新会话不能继承上一个在线流的 fps 角标/降帧横幅；在线路径随后会显式设置。
  online_badge_text_.clear();
  online_banner_text_.clear();
  online_banner_dismissed_ = false;
  if (online_fps_badge_ != nullptr) {
    online_fps_badge_->clear();
    online_fps_badge_->hide();
  }
  rate_ = lapian::core::FrameRate(info.video.frame_rate.num, info.video.frame_rate.den);

  const QString path = QString::fromUtf8(info.path_utf8.c_str());
  file_name_ = QFileInfo(path).fileName();
  format_label_->setText(QStringLiteral("%1×%2 · %3 · %4 fps")
                             .arg(info.video.width)
                             .arg(info.video.height)
                             .arg(QString::fromStdString(info.video.codec))
                             .arg(FormatFps(rate_)));
  UpdateFileText();

  const std::int64_t known_frames =
      info.video.nb_frames_known ? info.video.nb_frames : 0;
  lapian::core::TotalFrameCount total = lapian::core::TotalFrameCount::FromContainer(
      known_frames, info.duration_us, rate_);
  if (vfr_) {
    total = lapian::core::TotalFrameCount::Scanning();
  }
  SetTotal(total);
  SetFrame(0);
}

void InfoBar::SetFrame(int frame_index) {
  frame_label_->setText(QStringLiteral("帧 %1").arg(frame_index));
  UpdateTimecode(frame_index);
}

void InfoBar::SetTotal(const lapian::core::TotalFrameCount& total) {
  total_ = total;
  total_label_->setText(QStringLiteral("总帧数 %1")
                            .arg(QString::fromStdString(total.Display())));
  scanning_ = total.state() == lapian::core::TotalFramesState::kScanning;
  UpdateNotices();
}

void InfoBar::SetError(const QString& message) {
  error_text_ = message;
  synthetic_ = false;
  UpdateNotices();
}

void InfoBar::ClearError() {
  if (error_text_.isEmpty()) {
    return;
  }
  error_text_.clear();
  UpdateNotices();
}

void InfoBar::SetOnlineFrameRateStatus(const QString& badge_text,
                                       const QString& banner_text) {
  online_badge_text_ = badge_text.trimmed();
  online_banner_text_ = banner_text.trimmed();
  online_banner_dismissed_ = false;
  if (online_fps_badge_ != nullptr) {
    online_fps_badge_->setText(online_badge_text_);
    online_fps_badge_->setVisible(!online_badge_text_.isEmpty());
    online_fps_badge_->setStyleSheet(
        online_banner_text_.isEmpty()
            ? QStringLiteral(
                  "background:#2a2a30; color:#d8d8dc; padding:1px 5px; "
                  "border:1px solid #3a3a40; border-radius:3px;")
            : QStringLiteral(
                  "background:#4a2c00; color:#ffd37a; padding:1px 5px; "
                  "border:1px solid #7a4c00; border-radius:3px;"));
  }
  UpdateNotices();
}

void InfoBar::ClearOnlineFrameRateStatus() {
  online_badge_text_.clear();
  online_banner_text_.clear();
  online_banner_dismissed_ = false;
  if (online_fps_badge_ != nullptr) {
    online_fps_badge_->clear();
    online_fps_badge_->hide();
  }
  UpdateNotices();
}

QString InfoBar::OnlineFpsBadgeTextForTest() const {
  return online_fps_badge_ == nullptr ? QString() : online_fps_badge_->text();
}

bool InfoBar::OnlineFpsBadgeVisibleForTest() const {
  return online_fps_badge_ != nullptr && online_fps_badge_->isVisible();
}

bool InfoBar::BannerCloseVisibleForTest() const {
  return banner_close_ != nullptr && banner_close_->isVisible();
}

void InfoBar::DismissOnlineFrameRateBannerForTest() {
  if (online_banner_text_.isEmpty()) {
    return;
  }
  online_banner_dismissed_ = true;
  UpdateNotices();
}

void InfoBar::UpdateNotices() {
  if (synthetic_) {
    ApplyNotices();
    return;
  }

  notices_.clear();
  if (has_media_) {
    notices_ = lapian::media::BuildMediaStatusNotices(info_, vfr_, scanning_);
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

void InfoBar::ApplyNotices() {
  primary_index_ = lapian::media::SelectPrimaryNotice(notices_);
  if (primary_index_ == lapian::media::kNoNotice ||
      primary_index_ >= notices_.size()) {
    banner_label_->clear();
    banner_label_->setToolTip(QString());
    banner_label_->hide();
    if (banner_close_ != nullptr) {
      banner_close_->hide();
    }
    if (banner_row_ != nullptr) {
      banner_row_->hide();
    }
  } else {
    const lapian::media::StatusNotice& notice = notices_[primary_index_];
    banner_label_->setText(FromUtf8(notice.text));
    banner_label_->setToolTip(
        FromUtf8(notice.detail.empty() ? notice.text : notice.detail));
    UpdateBannerStyle(notice.kind);
    banner_label_->show();
    if (banner_row_ != nullptr) {
      banner_row_->show();
    }
    if (banner_close_ != nullptr) {
      banner_close_->setVisible(notice.kind ==
                                lapian::media::StatusNoticeKind::kFrameRateDrop);
    }
  }
  emit noticesChanged();
}

void InfoBar::UpdateBannerStyle(lapian::media::StatusNoticeKind kind) {
  QString style;
  switch (kind) {
    case lapian::media::StatusNoticeKind::kError:
      style = QStringLiteral(
          "background:#4a1616; color:#ffc4c4; padding:3px 6px; "
          "border-top:1px solid #7a2a2a;");
      break;
    case lapian::media::StatusNoticeKind::kNoAudio:
    case lapian::media::StatusNoticeKind::kScanning:
      style = QStringLiteral(
          "background:#1f2a36; color:#bcd6ee; padding:3px 6px; "
          "border-top:1px solid #2f4358;");
      break;
    case lapian::media::StatusNoticeKind::kOutOfGuarantee:
    case lapian::media::StatusNoticeKind::kTenBit:
    case lapian::media::StatusNoticeKind::kVfr:
      style = QStringLiteral(
          "background:#4a3600; color:#ffd37a; padding:3px 6px; "
          "border-top:1px solid #6a5200;");
      break;
    case lapian::media::StatusNoticeKind::kFrameRateDrop:
      style = QStringLiteral(
          "background:#4a2c00; color:#ffd37a; padding:3px 6px; "
          "border-top:1px solid #7a4c00;");
      break;
  }
  banner_label_->setStyleSheet(style);
}

void InfoBar::SetSyntheticNoticesForTest(const QStringList& kinds) {
  synthetic_ = true;
  notices_.clear();
  for (const QString& name : kinds) {
    lapian::media::StatusNoticeKind kind = lapian::media::StatusNoticeKind::kNoAudio;
    if (lapian::media::ParseStatusNoticeKind(name.toStdString(), &kind)) {
      notices_.push_back(lapian::media::MakeStatusNotice(kind));
    }
  }
  ApplyNotices();
}

void InfoBar::ClearSyntheticNoticesForTest() {
  if (!synthetic_) {
    return;
  }
  synthetic_ = false;
  UpdateNotices();
}

void InfoBar::UpdateTimecode(int frame_index) {
  if (vfr_ || !rate_.valid()) {
    timecode_label_->setText(QStringLiteral("--:--:--:--"));
    return;
  }
  const lapian::core::TimecodeFormatter formatter(rate_);
  timecode_label_->setText(
      QString::fromStdString(formatter.FromFrame(frame_index).ToString()));
}
