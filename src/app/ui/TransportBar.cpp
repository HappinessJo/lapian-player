#include "app/ui/TransportBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>

#include "app/ui/Theme.h"

TransportBar::TransportBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("v2Transport"));
  lapian::ui::SetStyledBackground(this);

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(8, 0, 8, 0);
  row->setSpacing(8);

  left_slot_ = new QWidget(this);
  left_slot_->setObjectName(QStringLiteral("v2TransportLeft"));
  row->addWidget(left_slot_, 1, Qt::AlignVCenter);

  // 读数块：时间码（主）+ 帧号/总帧（次）。3A 用 InfoBar 的原对象。
  readout_slot_ = new QWidget(this);
  readout_slot_->setObjectName(QStringLiteral("v2TransportReadout"));
  auto* readout_layout = new QVBoxLayout(readout_slot_);
  readout_layout->setContentsMargins(0, 0, 0, 0);
  readout_layout->setSpacing(0);
  timecode_slot_ = new QWidget(readout_slot_);
  timecode_slot_->setObjectName(QStringLiteral("v2Timecode"));
  auto* timecode_layout = new QHBoxLayout(timecode_slot_);
  timecode_layout->setContentsMargins(0, 0, 0, 0);
  timecode_layout->setSpacing(0);
  timecode_layout->addStretch(1);
  readout_layout->addWidget(timecode_slot_, 0);
  meta_slot_ = new QWidget(readout_slot_);
  meta_slot_->setObjectName(QStringLiteral("v2TransportMeta"));
  auto* meta_layout = new QHBoxLayout(meta_slot_);
  meta_layout->setContentsMargins(0, 0, 0, 0);
  meta_layout->setSpacing(8);
  meta_layout->addStretch(1);
  readout_layout->addWidget(meta_slot_, 0);
  row->addWidget(readout_slot_, 0, Qt::AlignVCenter);

  right_slot_ = new QWidget(this);
  right_slot_->setObjectName(QStringLiteral("v2TransportRight"));
  auto* right_layout = new QHBoxLayout(right_slot_);
  right_layout->setContentsMargins(0, 0, 0, 0);
  right_layout->setSpacing(6);
  right_layout->addStretch(1);
  volume_slot_ = new QWidget(right_slot_);
  volume_slot_->setObjectName(QStringLiteral("v2VolumeSlot"));
  auto* volume_layout = new QHBoxLayout(volume_slot_);
  volume_layout->setContentsMargins(0, 0, 0, 0);
  volume_layout->setSpacing(0);
  right_layout->addWidget(volume_slot_, 0, Qt::AlignVCenter);
  row->addWidget(right_slot_, 1, Qt::AlignVCenter);
}

void TransportBar::AdoptTimecode(QWidget* timecode) {
  if (timecode == nullptr || timecode_slot_ == nullptr) {
    return;
  }
  auto* layout = qobject_cast<QHBoxLayout*>(timecode_slot_->layout());
  if (layout == nullptr) {
    return;
  }
  timecode_ = timecode;
  layout->addWidget(timecode);
}

void TransportBar::AdoptFrame(QWidget* frame) {
  if (frame == nullptr || meta_slot_ == nullptr) {
    return;
  }
  auto* layout = qobject_cast<QHBoxLayout*>(meta_slot_->layout());
  if (layout == nullptr) {
    return;
  }
  frame_ = frame;
  layout->addWidget(frame);
}

void TransportBar::AdoptTotal(QWidget* total) {
  if (total == nullptr || meta_slot_ == nullptr) {
    return;
  }
  auto* layout = qobject_cast<QHBoxLayout*>(meta_slot_->layout());
  if (layout == nullptr) {
    return;
  }
  total_ = total;
  layout->addWidget(total);
}

void TransportBar::AdoptVolume(QWidget* volume) {
  if (volume == nullptr || volume_slot_ == nullptr) {
    return;
  }
  auto* layout = qobject_cast<QHBoxLayout*>(volume_slot_->layout());
  if (layout == nullptr) {
    return;
  }
  volume_ = volume;
  layout->addWidget(volume);
}

void TransportBar::ApplyTokens(const lapian::ui::Tokens& tokens) {
  setFixedHeight(tokens.transport);
  const int timecode_height = tokens.readout;
  const int meta_height = std::max(12, tokens.transport - tokens.readout);
  if (timecode_slot_ != nullptr) {
    timecode_slot_->setFixedHeight(timecode_height);
  }
  if (meta_slot_ != nullptr) {
    meta_slot_->setFixedHeight(meta_height);
  }
  if (readout_slot_ != nullptr) {
    readout_slot_->setFixedHeight(tokens.transport);
  }
  // 读数不写 QSS 字号：这里按令牌下发 setPixelSize（规划 §4.7）。
  const double timecode_px = std::max(11.0, static_cast<double>(tokens.readout) - 6.0);
  if (auto* label = qobject_cast<QLabel*>(timecode_)) {
    label->setFont(lapian::ui::MonoFont(timecode_px, QFont::DemiBold));
    label->setAlignment(Qt::AlignCenter);
  }
  for (QWidget* widget : {frame_, total_}) {
    if (auto* label = qobject_cast<QLabel*>(widget)) {
      label->setAlignment(Qt::AlignCenter);
    }
  }
  if (volume_ != nullptr) {
    volume_->setFixedHeight(std::max(12, std::min(tokens.btn, tokens.transport - 8)));
  }
}
