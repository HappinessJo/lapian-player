#include "app/InfoBar.h"

#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGridLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QString>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString FormatFps(const lapian::core::FrameRate& rate) {
  if (!rate.valid()) {
    return QStringLiteral("—");
  }
  return QString::number(rate.ToDouble(), 'f', 3);
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

  root->addLayout(grid_);

  banner_label_ = new QLabel(this);
  banner_label_->setObjectName(QStringLiteral("infoBanner"));
  banner_label_->setStyleSheet(
      QStringLiteral("background:#4a3600; color:#ffd37a; padding:2px 6px;"));
  banner_label_->hide();
  root->addWidget(banner_label_);

  UpdateLayoutMode();
  Clear();
}

void InfoBar::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  UpdateLayoutMode();
  UpdateFileText();
}

void InfoBar::UpdateLayoutMode() {
  // 1920×1080@150% 的逻辑宽度约 1280，@200% 约 960；侧栏占掉 240 后，
  // 信息栏在 <760 逻辑像素时把格式/总帧移到第二行，避免裁切或强行撑宽窗口。
  const bool narrow = width() < 760;
  if (narrow == narrow_) {
    return;
  }
  narrow_ = narrow;

  for (QLabel* label : {file_label_, format_label_, frame_label_, timecode_label_,
                        total_label_}) {
    grid_->removeWidget(label);
  }
  for (int column = 0; column < 5; ++column) {
    grid_->setColumnStretch(column, 0);
  }

  if (narrow_) {
    grid_->addWidget(file_label_, 0, 0, 1, 2);
    grid_->addWidget(frame_label_, 0, 2);
    grid_->addWidget(timecode_label_, 0, 3);
    grid_->addWidget(format_label_, 1, 0, 1, 2);
    grid_->addWidget(total_label_, 1, 2, 1, 2);
  } else {
    grid_->addWidget(file_label_, 0, 0);
    grid_->addWidget(format_label_, 0, 1);
    grid_->addWidget(frame_label_, 0, 2);
    grid_->addWidget(timecode_label_, 0, 3);
    grid_->addWidget(total_label_, 0, 4);
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

void InfoBar::Clear() {
  rate_ = lapian::core::FrameRate();
  vfr_ = false;
  file_name_.clear();
  file_label_->setText(QStringLiteral("未打开文件"));
  format_label_->clear();
  frame_label_->setText(QStringLiteral("帧 —"));
  timecode_label_->setText(QStringLiteral("--:--:--:--"));
  total_label_->clear();
  banner_label_->clear();
  banner_label_->hide();
  UpdateFileText();
}

void InfoBar::SetMedia(const lapian::media::MediaInfo& info, bool vfr) {
  rate_ = lapian::core::FrameRate(info.video.frame_rate.num, info.video.frame_rate.den);
  vfr_ = vfr;

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
  total_label_->setText(QStringLiteral("总帧数 %1")
                            .arg(QString::fromStdString(total.Display())));

  if (vfr_) {
    banner_label_->setText(QStringLiteral(
        "可变帧率（VFR）：帧号按显示帧计数，时间码不承诺与帧号严格对应"));
    banner_label_->show();
  } else if (total.state() == lapian::core::TotalFramesState::kScanning) {
    banner_label_->setText(QStringLiteral("总帧数扫描中…"));
    banner_label_->show();
  } else {
    banner_label_->clear();
    banner_label_->hide();
  }
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
