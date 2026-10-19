#include "app/InfoBar.h"

#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>

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

  auto* row = new QHBoxLayout();
  row->setSpacing(10);
  file_label_ = new QLabel(this);
  file_label_->setObjectName(QStringLiteral("infoFile"));
  file_label_->setMinimumWidth(180);
  format_label_ = new QLabel(this);
  frame_label_ = new QLabel(this);
  frame_label_->setObjectName(QStringLiteral("infoFrame"));
  QFont frame_font = frame_label_->font();
  frame_font.setPointSize(16);
  frame_font.setBold(true);
  frame_label_->setFont(frame_font);
  timecode_label_ = new QLabel(this);
  timecode_label_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  total_label_ = new QLabel(this);

  row->addWidget(file_label_, 1);
  row->addWidget(format_label_);
  row->addSpacing(12);
  row->addWidget(frame_label_);
  row->addWidget(timecode_label_);
  row->addWidget(total_label_);
  root->addLayout(row);

  banner_label_ = new QLabel(this);
  banner_label_->setObjectName(QStringLiteral("infoBanner"));
  banner_label_->setStyleSheet(
      QStringLiteral("background:#4a3600; color:#ffd37a; padding:2px 6px;"));
  banner_label_->hide();
  root->addWidget(banner_label_);

  Clear();
}

void InfoBar::Clear() {
  rate_ = lapian::core::FrameRate();
  vfr_ = false;
  file_label_->setText(QStringLiteral("未打开文件"));
  format_label_->clear();
  frame_label_->setText(QStringLiteral("帧 —"));
  timecode_label_->setText(QStringLiteral("--:--:--:--"));
  total_label_->clear();
  banner_label_->clear();
  banner_label_->hide();
}

void InfoBar::SetMedia(const lapian::media::MediaInfo& info, bool vfr) {
  rate_ = lapian::core::FrameRate(info.video.frame_rate.num, info.video.frame_rate.den);
  vfr_ = vfr;

  const QString path = QString::fromUtf8(info.path_utf8.c_str());
  file_label_->setText(QFileInfo(path).fileName());
  format_label_->setText(QStringLiteral("%1×%2 · %3 · %4 fps")
                             .arg(info.video.width)
                             .arg(info.video.height)
                             .arg(QString::fromStdString(info.video.codec))
                             .arg(FormatFps(rate_)));

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
