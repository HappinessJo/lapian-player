#include "app/ui/TransportBar.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "app/ui/Glyph.h"
#include "app/ui/Theme.h"

namespace {

using lapian::ui::GlyphId;

QColor Dim() { return lapian::ui::Color(lapian::ui::kColorTextDim); }
QColor Accent() { return lapian::ui::Color(lapian::ui::kColorAccent); }
QColor Bright() { return lapian::ui::Color(lapian::ui::kColorText); }

}  // namespace

TransportBar::TransportBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("v2Transport"));
  lapian::ui::SetStyledBackground(this);

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(8, 0, 8, 0);
  row->setSpacing(0);

  // ── 左组：首 / −1 / 播放 / +1 / 末 │ A / B │ 速度 / 静音 / 音量 ───────
  left_group_ = new QWidget(this);
  left_group_->setObjectName(QStringLiteral("v2TransportLeft"));
  auto* left = new QHBoxLayout(left_group_);
  left->setContentsMargins(0, 0, 0, 0);
  left->setSpacing(2);

  first_button_ = MakeIconButton("v2ToStart", left_group_);
  first_button_->setToolTip(QStringLiteral("首帧（Home）"));
  lapian::ui::BindGlyph(first_button_, GlyphId::kToStart, Dim(), Accent());
  left->addWidget(first_button_);

  prev_button_ = MakeIconButton("v2StepBack", left_group_);
  prev_button_->setToolTip(QStringLiteral("上一帧（PgUp / ←）"));
  lapian::ui::BindGlyph(prev_button_, GlyphId::kStepBack, Dim(), Accent());
  left->addWidget(prev_button_);

  play_button_ = MakeIconButton("v2PlayButton", left_group_);
  play_button_->setToolTip(QStringLiteral("播放 / 暂停（Space）"));
  lapian::ui::BindGlyph(play_button_, GlyphId::kPlay, Bright(), Bright());
  left->addWidget(play_button_);

  next_button_ = MakeIconButton("v2StepFwd", left_group_);
  next_button_->setToolTip(QStringLiteral("下一帧（PgDn / →）"));
  lapian::ui::BindGlyph(next_button_, GlyphId::kStepFwd, Dim(), Accent());
  left->addWidget(next_button_);

  last_button_ = MakeIconButton("v2ToEnd", left_group_);
  last_button_->setToolTip(QStringLiteral("末帧（End）"));
  lapian::ui::BindGlyph(last_button_, GlyphId::kToEnd, Dim(), Accent());
  left->addWidget(last_button_);

  sep_left_ = MakeSeparator(left_group_);
  left->addWidget(sep_left_);

  in_button_ = MakeIconButton("v2SetInButton", left_group_);
  in_button_->setToolTip(QStringLiteral("设入点 A（I）"));
  lapian::ui::BindGlyph(in_button_, GlyphId::kSetIn, Dim(), Accent());
  left->addWidget(in_button_);

  out_button_ = MakeIconButton("v2SetOutButton", left_group_);
  out_button_->setToolTip(QStringLiteral("设出点 B（O）"));
  lapian::ui::BindGlyph(out_button_, GlyphId::kSetOut, Dim(), Accent());
  left->addWidget(out_button_);

  sep_ab_ = MakeSeparator(left_group_);
  left->addWidget(sep_ab_);

  // 速度：3A 沿用 MainWindow 的 speedInfo 标签（[ / ] 调整），3B 换按钮 + 菜单。
  speed_slot_ = new QWidget(left_group_);
  speed_slot_->setObjectName(QStringLiteral("v2SpeedSlot"));
  auto* speed_layout = new QHBoxLayout(speed_slot_);
  speed_layout->setContentsMargins(6, 0, 6, 0);
  speed_layout->setSpacing(0);
  speed_slot_->setToolTip(QStringLiteral("播放速度（[ / ]）"));
  left->addWidget(speed_slot_);

  mute_button_ = MakeIconButton("v2MuteButton", left_group_);
  mute_button_->setToolTip(QStringLiteral("静音（M）"));
  lapian::ui::BindGlyph(mute_button_, GlyphId::kVolume, Dim(), Accent());
  left->addWidget(mute_button_);

  volume_slot_ = new QWidget(left_group_);
  volume_slot_->setObjectName(QStringLiteral("v2VolumeSlot"));
  auto* volume_layout = new QHBoxLayout(volume_slot_);
  volume_layout->setContentsMargins(4, 0, 4, 0);
  volume_layout->setSpacing(0);
  left->addWidget(volume_slot_);

  row->addWidget(left_group_, 0, Qt::AlignVCenter);
  row->addStretch(1);

  // ── 读数块：26px 等宽时间码（主）+ 帧号/总帧（次）───────────────────
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
  timecode_layout->addStretch(1);
  readout_layout->addWidget(timecode_slot_, 0);
  meta_slot_ = new QWidget(readout_slot_);
  meta_slot_->setObjectName(QStringLiteral("v2TransportMeta"));
  auto* meta_layout = new QHBoxLayout(meta_slot_);
  meta_layout->setContentsMargins(0, 0, 0, 0);
  meta_layout->setSpacing(7);
  meta_layout->addStretch(1);
  readout_layout->addWidget(meta_slot_, 0);
  row->addWidget(readout_slot_, 0, Qt::AlignVCenter);
  row->addStretch(1);

  // ── 右组：缓冲槽 │ 循环 / 波形 / 标记 / 截图 / 全屏 / 检查器 ──────────
  right_group_ = new QWidget(this);
  right_group_->setObjectName(QStringLiteral("v2TransportRight"));
  auto* right = new QHBoxLayout(right_group_);
  right->setContentsMargins(0, 0, 0, 0);
  right->setSpacing(2);

  buffer_slot_ = new QWidget(right_group_);
  buffer_slot_->setObjectName(QStringLiteral("v2BufferSlot"));
  auto* buffer_layout = new QHBoxLayout(buffer_slot_);
  buffer_layout->setContentsMargins(0, 0, 0, 0);
  buffer_layout->setSpacing(0);
  buffer_slot_->setVisible(false);
  right->addWidget(buffer_slot_);

  sep_right_ = MakeSeparator(right_group_);
  right->addWidget(sep_right_);

  loop_button_ = MakeIconButton("v2LoopButton", right_group_);
  loop_button_->setToolTip(
      QStringLiteral("工作区循环开关（L）· 清除 A/B（Shift+L）"));
  loop_button_->setCheckable(true);
  lapian::ui::BindGlyph(loop_button_, GlyphId::kLoop, Dim(), Accent());
  right->addWidget(loop_button_);

  wave_button_ = MakeIconButton("v2WaveButton", right_group_);
  wave_button_->setToolTip(QStringLiteral("波形开关（W）"));
  lapian::ui::BindGlyph(wave_button_, GlyphId::kWave, Dim(), Accent());
  right->addWidget(wave_button_);

  mark_button_ = MakeIconButton("v2MarkButton", right_group_);
  mark_button_->setToolTip(QStringLiteral("在当前帧添加标记（Ctrl+M）"));
  lapian::ui::BindGlyph(mark_button_, GlyphId::kFlag, Dim(), Accent());
  right->addWidget(mark_button_);

  snap_button_ = MakeIconButton("v2SnapButton", right_group_);
  snap_button_->setToolTip(QStringLiteral("截图当前显示帧（S）"));
  lapian::ui::BindGlyph(snap_button_, GlyphId::kCamera, Dim(), Accent());
  right->addWidget(snap_button_);

  full_button_ = MakeIconButton("v2FullScreenButton", right_group_);
  full_button_->setToolTip(QStringLiteral("全屏（F / 双击画面）"));
  lapian::ui::BindGlyph(full_button_, GlyphId::kFull, Dim(), Accent());
  right->addWidget(full_button_);

  inspector_button_ = MakeIconButton("v2InspectorToggle", right_group_);
  inspector_button_->setToolTip(QStringLiteral("显示 / 隐藏检查器"));
  inspector_button_->setCheckable(true);
  inspector_button_->setChecked(true);
  lapian::ui::BindGlyph(inspector_button_, GlyphId::kPanel, Dim(), Accent());
  right->addWidget(inspector_button_);

  row->addWidget(right_group_, 0, Qt::AlignVCenter);

  connect(first_button_, &QToolButton::clicked, this,
          &TransportBar::firstFrameRequested);
  connect(last_button_, &QToolButton::clicked, this,
          &TransportBar::lastFrameRequested);
  connect(inspector_button_, &QToolButton::toggled, this, [this](bool on) {
    emit inspectorToggled(!on);
  });
}

QToolButton* TransportBar::MakeIconButton(const char* object_name,
                                          QWidget* parent) {
  auto* button = new QToolButton(parent);
  button->setObjectName(QString::fromLatin1(object_name));
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setFocusPolicy(Qt::NoFocus);
  button->setAutoRaise(true);
  button->setFixedSize(26, 26);
  button->setIconSize(QSize(16, 16));
  return button;
}

QWidget* TransportBar::MakeSeparator(QWidget* parent) {
  auto* wrap = new QWidget(parent);
  wrap->setFixedWidth(11);
  auto* layout = new QHBoxLayout(wrap);
  layout->setContentsMargins(5, 0, 5, 0);
  layout->setSpacing(0);
  auto* line = new QWidget(wrap);
  line->setObjectName(QStringLiteral("v2TransportSep"));
  line->setFixedWidth(1);
  line->setFixedHeight(18);
  lapian::ui::SetStyledBackground(line);
  layout->addWidget(line);
  return wrap;
}

void TransportBar::BindActions(QAction* play, QAction* prev, QAction* next,
                               QAction* loop_in, QAction* loop_out,
                               QAction* loop_toggle, QAction* waveform,
                               QAction* mark, QAction* snapshot,
                               QAction* fullscreen, QAction* mute) {
  play_action_ = play;
  auto bind = [](QToolButton* button, QAction* action, GlyphId glyph) {
    if (button == nullptr || action == nullptr) {
      return;
    }
    // 不用 setDefaultAction：它在 action 每次 changed() 时把 icon 重置成
    // action->icon()（这些 action 没有 icon），自绘图标会消失成"…"。
    lapian::ui::BindToolAction(button, action, glyph, Dim(), Accent());
  };
  bind(prev_button_, prev, GlyphId::kStepBack);
  bind(next_button_, next, GlyphId::kStepFwd);
  bind(in_button_, loop_in, GlyphId::kSetIn);
  bind(out_button_, loop_out, GlyphId::kSetOut);
  bind(wave_button_, waveform, GlyphId::kWave);
  bind(mark_button_, mark, GlyphId::kFlag);
  bind(snap_button_, snapshot, GlyphId::kCamera);
  bind(full_button_, fullscreen, GlyphId::kFull);
  bind(mute_button_, mute, GlyphId::kVolume);
  if (play_button_ != nullptr && play != nullptr) {
    // 播放按钮是唯一实心 accent 按钮：图标固定用亮色，播放/暂停换形。
    lapian::ui::BindToolAction(play_button_, play, GlyphId::kPlay, Bright(),
                               Bright());
    play_button_->setToolTip(QStringLiteral("播放 / 暂停（Space）"));
    connect(play, &QAction::changed, this, [this]() { UpdatePlayGlyph(); });
    UpdatePlayGlyph();
  }
  if (loop_button_ != nullptr) {
    if (loop_toggle != nullptr) {
      connect(loop_button_, &QToolButton::clicked, loop_toggle,
              &QAction::trigger);
      loop_button_->setToolTip(
          QStringLiteral("工作区循环开关（L）· 清除 A/B（Shift+L）"));
    }
    SetLoopEnabled(false);
  }
}

void TransportBar::UpdatePlayGlyph() {
  if (play_button_ == nullptr || play_action_ == nullptr) {
    return;
  }
  const QString text = play_action_->text();
  const bool playing =
      text.contains(QStringLiteral("暂停")) || text.contains(QStringLiteral("缓冲"));
  lapian::ui::BindGlyph(play_button_,
                        playing ? GlyphId::kPause : GlyphId::kPlay, Bright(),
                        Bright());
}

void TransportBar::SetLoopEnabled(bool enabled) {
  if (loop_button_ != nullptr && loop_button_->isChecked() != enabled) {
    const QSignalBlocker blocker(loop_button_);
    loop_button_->setChecked(enabled);
  }
}

void TransportBar::SetInspectorCollapsed(bool collapsed) {
  if (inspector_button_ != nullptr &&
      inspector_button_->isChecked() != !collapsed) {
    const QSignalBlocker blocker(inspector_button_);
    inspector_button_->setChecked(!collapsed);
  }
}

bool TransportBar::inspector_toggle_checked() const {
  return inspector_button_ != nullptr && inspector_button_->isChecked();
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
  // 读数允许被压缩：窗口 820 宽时先挤读数，不推高最小宽度。
  timecode->setMinimumWidth(0);
  layout->insertWidget(1, timecode);
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
  frame->setMinimumWidth(0);
  layout->insertWidget(1, frame);
  auto* slash = new QLabel(QStringLiteral("/"), meta_slot_);
  slash->setObjectName(QStringLiteral("v2MetaSlash"));
  layout->insertWidget(2, slash);
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
  total->setMinimumWidth(0);
  layout->insertWidget(3, total);
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
  // 音量条可被压缩（原型窄档先收音量），但不能没有下限高度。
  volume->setMinimumWidth(0);
  volume->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  layout->addWidget(volume);
}

void TransportBar::AdoptSpeedLabel(QWidget* speed) {
  if (speed == nullptr || speed_slot_ == nullptr) {
    return;
  }
  auto* layout = qobject_cast<QHBoxLayout*>(speed_slot_->layout());
  if (layout == nullptr) {
    return;
  }
  speed_ = speed;
  speed->setMinimumWidth(0);
  layout->addWidget(speed);
}

void TransportBar::ApplyTokens(const lapian::ui::Tokens& tokens) {
  setFixedHeight(tokens.transport);
  const int tc_h = tokens.readout;
  const int meta_h = std::max(12, tokens.transport - tokens.readout);
  if (timecode_slot_ != nullptr) {
    timecode_slot_->setFixedHeight(tc_h);
  }
  if (meta_slot_ != nullptr) {
    meta_slot_->setFixedHeight(meta_h);
  }
  if (readout_slot_ != nullptr) {
    readout_slot_->setFixedHeight(tokens.transport);
  }
  // 读数不写 QSS 字号：这里按令牌下发 setPixelSize（规划 §4.7）。
  const double timecode_px = std::max(11.0, tokens.fs_readout);
  if (auto* label = qobject_cast<QLabel*>(timecode_)) {
    QFont font = lapian::ui::MonoFont(timecode_px, QFont::DemiBold);
    font.setLetterSpacing(QFont::PercentageSpacing, 104.0);
    label->setFont(font);
    label->setAlignment(Qt::AlignCenter);
  }
  const double meta_px = tokens.density == lapian::ui::Density::kDense
                             ? 10.0
                             : tokens.fs_meta;
  for (QWidget* widget : {frame_, total_}) {
    if (auto* label = qobject_cast<QLabel*>(widget)) {
      label->setFont(lapian::ui::UiFont(meta_px));
      label->setAlignment(Qt::AlignCenter);
    }
  }
  if (auto* label = qobject_cast<QLabel*>(speed_)) {
    label->setFont(lapian::ui::MonoFont(tokens.fs_meta + 1.0, QFont::DemiBold));
    label->setAlignment(Qt::AlignCenter);
  }
  if (volume_ != nullptr) {
    volume_->setFixedHeight(
        std::max(12, std::min(tokens.btn, tokens.transport - 10)));
    volume_->setMinimumWidth(0);
    volume_->setMaximumWidth(78);
  }
  if (play_button_ != nullptr) {
    // 唯一的实心 accent 按钮：34×26（令牌 h-play）。
    play_button_->setFixedSize(tokens.play, tokens.btn);
  }
  for (QToolButton* button :
       {first_button_, prev_button_, next_button_, last_button_, in_button_,
        out_button_, mute_button_, loop_button_, wave_button_, mark_button_,
        snap_button_, full_button_, inspector_button_}) {
    if (button == nullptr) {
      continue;
    }
    button->setFixedSize(tokens.btn, tokens.btn);
  }
  if (buffer_slot_ != nullptr) {
    buffer_slot_->setFixedHeight(tokens.btn);
  }
  UpdateNarrowState();
}

QSize TransportBar::minimumSizeHint() const {
  // 允许被压窄：窄档由 UpdateNarrowState() 按自身宽度隐藏次要控件，
  // 否则 820 宽窗口会被"13 个按钮 + 读数 + 音量"顶住而缩不下去。
  return QSize(360, height());
}

void TransportBar::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  UpdateNarrowState();
}

void TransportBar::UpdateNarrowState() {
  if (left_group_ == nullptr) {
    return;
  }
  const int w = width();
  const bool hide_speed = w < 700;
  const bool hide_ab = w < 640;
  const bool hide_meta = w < 520;
  if (narrow_state_valid_ && hide_speed == hide_speed_ && hide_ab == hide_ab_ &&
      hide_meta == hide_meta_) {
    return;
  }
  hide_speed_ = hide_speed;
  hide_ab_ = hide_ab;
  hide_meta_ = hide_meta;
  narrow_state_valid_ = true;
  if (speed_slot_ != nullptr) {
    speed_slot_->setVisible(!hide_speed_);
  }
  if (sep_ab_ != nullptr) {
    sep_ab_->setVisible(!hide_ab_);
  }
  for (QWidget* widget :
       {static_cast<QWidget*>(in_button_), static_cast<QWidget*>(out_button_)}) {
    if (widget != nullptr) {
      widget->setVisible(!hide_ab_);
    }
  }
  // 次读数（帧号/总帧）在更窄时收起；时间码主读数始终保留（M7）。
  if (meta_slot_ != nullptr) {
    meta_slot_->setVisible(!hide_meta_);
  }
}
