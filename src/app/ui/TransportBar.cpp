#include "app/ui/TransportBar.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "app/ui/Glyph.h"
#include "app/ui/Theme.h"
#include "core/MediaClock.h"

namespace {

using lapian::ui::GlyphId;

// 规划 §3.3 的四档宽度边界（按 **Transport 自身宽度**，不是视口宽）。
// 预算是 §4.6 的 750px 全量：≥760 全保留、660–759 收音量、560–659 再收
// A/B+速度（读数 26→22）、<560 再收首末帧/标记/截图。
// 实测校准见交接：保留集实测 need=736（full 档），阈值留了 3% 余量。
constexpr int kTierVolumeWidth = 760;
constexpr int kTierAbSpeedWidth = 660;
constexpr int kTierFirstLastWidth = 560;

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

  // 速度：3B 换成真按钮（26px 高，点击弹 0.25–2x 菜单，[ / ] 仍可用）。
  speed_button_ = new QToolButton(left_group_);
  speed_button_->setObjectName(QStringLiteral("v2SpeedButton"));
  speed_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  speed_button_->setFocusPolicy(Qt::NoFocus);
  speed_button_->setAutoRaise(true);
  speed_button_->setText(QStringLiteral("1x"));
  speed_button_->setToolTip(QStringLiteral("播放速度（[ / ]）"));
  speed_button_->setFixedSize(34, 26);
  connect(speed_button_, &QToolButton::clicked, this,
          &TransportBar::ShowSpeedMenu);
  left->addWidget(speed_button_, 0, Qt::AlignVCenter);

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

  // ── 读数块：26px 等宽时间码（主，可点击输入）+ 帧号/总帧（次）───────
  readout_slot_ = new QWidget(this);
  readout_slot_->setObjectName(QStringLiteral("v2TransportReadout"));
  auto* readout_layout = new QVBoxLayout(readout_slot_);
  readout_layout->setContentsMargins(0, 0, 0, 0);
  readout_layout->setSpacing(0);
  timecode_slot_ = new QWidget(readout_slot_);
  // 名字给**标签本身**（规划 §7.7 的 v2Timecode）；外层 slot 用 v2TimecodeSlot。
  timecode_slot_->setObjectName(QStringLiteral("v2TimecodeSlot"));
  auto* timecode_layout = new QHBoxLayout(timecode_slot_);
  timecode_layout->setContentsMargins(0, 0, 0, 0);
  timecode_layout->setSpacing(0);
  timecode_layout->addStretch(1);
  timecode_layout->addStretch(1);
  // 3D：时间码主读数由 Transport 自己建（原 InfoBar 的标签在 3D 删除）。
  // objectName 仍是 `v2Timecode`（M5/M7 与 --readout-edit-test 依赖）。
  auto* timecode = new QLabel(QStringLiteral("--:--:--:--"), timecode_slot_);
  timecode->setObjectName(QStringLiteral("v2Timecode"));
  timecode->setAlignment(Qt::AlignCenter);
  timecode->setTextFormat(Qt::PlainText);
  timecode->setFont(lapian::ui::MonoFont(26.0, QFont::DemiBold));
  timecode->installEventFilter(this);
  timecode_layout->insertWidget(1, timecode);
  timecode_ = timecode;
  readout_layout->addWidget(timecode_slot_, 0);
  meta_slot_ = new QWidget(readout_slot_);
  meta_slot_->setObjectName(QStringLiteral("v2TransportMeta"));
  auto* meta_layout = new QHBoxLayout(meta_slot_);
  meta_layout->setContentsMargins(0, 0, 0, 0);
  meta_layout->setSpacing(7);
  meta_layout->addStretch(1);
  // 3D：帧号/总帧次读数由 Transport 自己建（objectName 沿用 v2FrameNumber /
  // v2FrameTotal，--banner-test / M7 / 全屏断言依赖）。
  frame_ = new QLabel(QStringLiteral("帧 —"), meta_slot_);
  frame_->setObjectName(QStringLiteral("v2FrameNumber"));
  frame_->setMinimumWidth(0);
  meta_layout->addWidget(frame_, 0, Qt::AlignVCenter);
  auto* slash = new QLabel(QStringLiteral("/"), meta_slot_);
  slash->setObjectName(QStringLiteral("v2MetaSlash"));
  meta_layout->addWidget(slash, 0, Qt::AlignVCenter);
  total_ = new QLabel(meta_slot_);
  total_->setObjectName(QStringLiteral("v2FrameTotal"));
  total_->setMinimumWidth(0);
  meta_layout->addWidget(total_, 0, Qt::AlignVCenter);
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
  buffer_layout->setContentsMargins(2, 0, 6, 0);
  buffer_layout->setSpacing(6);
  buffer_bar_ = new QProgressBar(buffer_slot_);
  buffer_bar_->setObjectName(QStringLiteral("v2BufferBar"));
  buffer_bar_->setTextVisible(false);
  buffer_bar_->setRange(0, 1000);
  buffer_bar_->setValue(0);
  buffer_bar_->setFixedWidth(84);
  buffer_bar_->setFixedHeight(6);
  buffer_bar_->setFocusPolicy(Qt::NoFocus);
  buffer_layout->addWidget(buffer_bar_, 0, Qt::AlignVCenter);
  buffer_text_ = new QLabel(buffer_slot_);
  buffer_text_->setObjectName(QStringLiteral("v2BufferText"));
  buffer_text_->setText(QStringLiteral("已缓存 ——"));
  buffer_layout->addWidget(buffer_text_, 0, Qt::AlignVCenter);
  buffer_slot_->setVisible(false);
  right->addWidget(buffer_slot_);

  sep_right_ = MakeSeparator(right_group_);
  right->addWidget(sep_right_);

  loop_button_ = MakeIconButton("v2LoopButton", right_group_);
  loop_button_->setToolTip(QStringLiteral(
      "工作区循环开关（L）· 清除 A/B（Shift+L）· 双击轨道恢复全片"));
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
  fullscreen_action_ = fullscreen;
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
  bind(mute_button_, mute, GlyphId::kVolume);
  if (full_button_ != nullptr && fullscreen != nullptr) {
    lapian::ui::BindToolAction(full_button_, fullscreen, GlyphId::kFull, Dim(),
                               Accent());
    SetFullScreenActive(fullscreen_active_);
  }
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
      loop_button_->setToolTip(QStringLiteral(
          "工作区循环开关（L）· 清除 A/B（Shift+L）· 双击轨道恢复全片"));
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

void TransportBar::SetSpeedText(const QString& text) {
  if (speed_button_ == nullptr || speed_button_->text() == text) {
    return;
  }
  speed_button_->setText(text);
}

// ---- 3D：帧号 / 时间码 / 总帧（原 InfoBar 的显示侧，口径不变）----

void TransportBar::SetFrameRate(const lapian::core::FrameRate& rate, bool vfr) {
  rate_ = rate;
  vfr_ = vfr;
}

void TransportBar::SetFrame(qint64 frame_index) {
  if (auto* label = qobject_cast<QLabel*>(frame_)) {
    label->setText(QStringLiteral("帧 %1").arg(frame_index));
  }
  if (auto* label = qobject_cast<QLabel*>(timecode_)) {
    if (vfr_ || !rate_.valid()) {
      label->setText(QStringLiteral("--:--:--:--"));
    } else {
      const lapian::core::TimecodeFormatter formatter(rate_);
      label->setText(QString::fromStdString(
          formatter.FromFrame(frame_index).ToString()));
    }
  }
}

void TransportBar::SetTotal(const lapian::core::TotalFrameCount& total) {
  total_count_ = total;
  if (auto* label = qobject_cast<QLabel*>(total_)) {
    label->setText(QStringLiteral("总帧数 %1")
                       .arg(QString::fromStdString(total.Display())));
  }
}

void TransportBar::ClearReadouts() {
  rate_ = lapian::core::FrameRate();
  vfr_ = false;
  total_count_ = lapian::core::TotalFrameCount{};
  if (auto* label = qobject_cast<QLabel*>(frame_)) {
    label->setText(QStringLiteral("帧 —"));
  }
  if (auto* label = qobject_cast<QLabel*>(timecode_)) {
    label->setText(QStringLiteral("--:--:--:--"));
  }
  if (auto* label = qobject_cast<QLabel*>(total_)) {
    label->clear();
  }
}

QString TransportBar::FrameText() const {
  auto* label = qobject_cast<QLabel*>(frame_);
  return label == nullptr ? QString() : label->text();
}

QString TransportBar::TotalText() const {
  auto* label = qobject_cast<QLabel*>(total_);
  return label == nullptr ? QString() : label->text();
}

QString TransportBar::TimecodeText() const {
  auto* label = qobject_cast<QLabel*>(timecode_);
  return label == nullptr ? QString() : label->text();
}

void TransportBar::SetFullScreenActive(bool fullscreen) {
  fullscreen_active_ = fullscreen;
  if (full_button_ == nullptr) {
    return;
  }
  full_button_->setToolTip(fullscreen
                               ? QStringLiteral("退出全屏（Esc）")
                               : QStringLiteral("全屏（F / 双击画面）"));
}

void TransportBar::SetBufferState(bool visible, qint64 cached_bytes,
                                  qint64 total_bytes) {
  if (buffer_slot_ == nullptr) {
    return;
  }
  if (!visible) {
    if (buffer_slot_->isVisible()) {
      buffer_slot_->setVisible(false);
    }
    return;
  }
  constexpr double kMegabyte = 1024.0 * 1024.0;
  const double cached_mb = static_cast<double>(std::max<qint64>(0, cached_bytes)) /
                           kMegabyte;
  const double total_mb = static_cast<double>(std::max<qint64>(0, total_bytes)) /
                          kMegabyte;
  if (buffer_text_ != nullptr) {
    buffer_text_->setText(
        total_mb > 0.0
            ? QStringLiteral("已缓存 %1/%2 MB")
                  .arg(cached_mb, 0, 'f', 1)
                  .arg(total_mb, 0, 'f', 1)
            : QStringLiteral("已缓存 %1 MB").arg(cached_mb, 0, 'f', 1));
  }
  if (buffer_bar_ != nullptr) {
    const int value =
        total_bytes > 0
            ? static_cast<int>(std::lround(
                  1000.0 * static_cast<double>(cached_bytes) /
                  static_cast<double>(total_bytes)))
            : 0;
    buffer_bar_->setValue(std::clamp(value, 0, 1000));
  }
  if (!buffer_slot_->isVisible()) {
    buffer_slot_->setVisible(true);
  }
}

void TransportBar::SetTimecodeCommit(TimecodeCommit commit) {
  commit_ = std::move(commit);
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

void TransportBar::ApplyReadoutFont(bool small) {
  readout_px_ = small ? tokens_.fs_readout_sm : tokens_.fs_readout;
  const double px = std::max(11.0, readout_px_);
  if (auto* label = qobject_cast<QLabel*>(timecode_)) {
    QFont font = lapian::ui::MonoFont(px, QFont::DemiBold);
    font.setLetterSpacing(QFont::PercentageSpacing, 104.0);
    label->setFont(font);
    label->setAlignment(Qt::AlignCenter);
  }
  if (readout_edit_ != nullptr) {
    QFont font = lapian::ui::MonoFont(px, QFont::DemiBold);
    font.setLetterSpacing(QFont::PercentageSpacing, 104.0);
    readout_edit_->setFont(font);
  }
}

void TransportBar::ApplyTokens(const lapian::ui::Tokens& tokens) {
  tokens_ = tokens;
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
  ApplyReadoutFont(small_readout_);
  const double meta_px = tokens.density == lapian::ui::Density::kDense
                             ? 10.0
                             : tokens.fs_meta;
  for (QWidget* widget : {frame_, total_}) {
    if (auto* label = qobject_cast<QLabel*>(widget)) {
      label->setFont(lapian::ui::UiFont(meta_px));
      label->setAlignment(Qt::AlignCenter);
    }
  }
  if (speed_button_ != nullptr) {
    speed_button_->setFont(
        lapian::ui::MonoFont(tokens.fs_meta + 1.0, QFont::DemiBold));
    speed_button_->setFixedSize(34, tokens.btn);
  }
  if (buffer_text_ != nullptr) {
    buffer_text_->setFont(lapian::ui::UiFont(meta_px));
  }
  if (buffer_bar_ != nullptr) {
    buffer_bar_->setFixedHeight(std::max(4, tokens.btn - 20));
  }
  if (readout_edit_ != nullptr) {
    readout_edit_->setFixedHeight(tokens.input);
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
  // 令牌变了（档位切换）：强制按新宽度/新字号重算一次。
  narrow_state_valid_ = false;
  UpdateNarrowState();
}

QSize TransportBar::minimumSizeHint() const {
  // 允许被压窄：窄档由 UpdateNarrowState() 按自身宽度隐藏次要控件，
  // 否则 820 宽窗口会被"13 个按钮 + 读数 + 音量"顶住而缩不下去。
  // 下限取 <560 档保留集的实测需要（约 400），再小必裁切。
  return QSize(400, height());
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
  const bool hide_volume = w < kTierVolumeWidth;
  const bool hide_ab = w < kTierAbSpeedWidth;
  const bool hide_speed = w < kTierAbSpeedWidth;
  const bool hide_first_last = w < kTierFirstLastWidth;
  const bool hide_mark_snap = w < kTierFirstLastWidth;
  const bool small = w < kTierAbSpeedWidth;
  if (narrow_state_valid_ && hide_volume == hide_volume_ &&
      hide_ab == hide_ab_ && hide_speed == hide_speed_ &&
      hide_first_last == hide_first_last_ &&
      hide_mark_snap == hide_mark_snap_ && small == small_readout_) {
    return;
  }
  hide_volume_ = hide_volume;
  hide_ab_ = hide_ab;
  hide_speed_ = hide_speed;
  hide_first_last_ = hide_first_last;
  hide_mark_snap_ = hide_mark_snap;
  small_readout_ = small;
  narrow_state_valid_ = true;

  if (volume_slot_ != nullptr) {
    volume_slot_->setVisible(!hide_volume_);
  }
  if (sep_ab_ != nullptr) {
    sep_ab_->setVisible(!hide_ab_);
  }
  if (in_button_ != nullptr) {
    in_button_->setVisible(!hide_ab_);
  }
  if (out_button_ != nullptr) {
    out_button_->setVisible(!hide_ab_);
  }
  if (speed_button_ != nullptr) {
    speed_button_->setVisible(!hide_speed_);
  }
  if (first_button_ != nullptr) {
    first_button_->setVisible(!hide_first_last_);
  }
  if (last_button_ != nullptr) {
    last_button_->setVisible(!hide_first_last_);
  }
  if (mark_button_ != nullptr) {
    mark_button_->setVisible(!hide_mark_snap_);
  }
  if (snap_button_ != nullptr) {
    snap_button_->setVisible(!hide_mark_snap_);
  }
  // 次读数（帧号/总帧）**任何档位都保留**：M7 要求播放/暂停/拖动/全屏四态
  // 帧号可见且位置不变；读数只是从 26px 收到 22px。
  ApplyReadoutFont(small_readout_);
  tier_need_ = RetainedNeedWidth();
}

int TransportBar::RetainedNeedWidth() const {
  // 实测校准（规划 §3.3 ±10%）：只累加**当前仍可见**的组（隐藏项在
  // QBoxLayout::sizeHint 里被跳过），再加左右内边距。
  int need = 16;
  for (QWidget* group : {left_group_, readout_slot_, right_group_}) {
    if (group != nullptr && group->isVisible()) {
      need += group->sizeHint().width();
    }
  }
  return need;
}

QString TransportBar::TierStateForTest() const {
  const char* tier = "full";
  if (hide_first_last_) {
    tier = "bare";
  } else if (hide_ab_) {
    tier = "basic";
  } else if (hide_volume_) {
    tier = "no-volume";
  }
  // need 现算（构造期/首次 resize 前的缓存值恒为 0，不可信）。
  const int need = RetainedNeedWidth();
  return QStringLiteral(
             "tier=%1 w=%2 need=%3 volume=%4 ab=%5 speed=%6 first_last=%7 "
             "mark_snap=%8 readout=%9")
      .arg(QString::fromLatin1(tier))
      .arg(width())
      .arg(need)
      .arg(hide_volume_ ? 0 : 1)
      .arg(hide_ab_ ? 0 : 1)
      .arg(hide_speed_ ? 0 : 1)
      .arg(hide_first_last_ ? 0 : 1)
      .arg(hide_mark_snap_ ? 0 : 1)
      .arg(readout_px_, 0, 'f', 0);
}

void TransportBar::ShowSpeedMenu() {
  if (speed_button_ == nullptr) {
    return;
  }
  // 菜单点击时建（规划 §7.6：不预建菜单/对话框）。
  if (speed_menu_ == nullptr) {
    speed_menu_ = new QMenu(this);
    speed_menu_->setObjectName(QStringLiteral("v2SpeedMenu"));
    for (const double rate : lapian::core::kPlaybackRates) {
      const QString text = QString::number(rate, 'g', 3) + QStringLiteral("x");
      QAction* action = speed_menu_->addAction(text);
      action->setCheckable(true);
      connect(action, &QAction::triggered, this,
              [this, rate]() { emit speedSelected(rate); });
    }
  }
  const QString current = speed_button_->text();
  for (QAction* action : speed_menu_->actions()) {
    action->setChecked(action->text() == current);
  }
  speed_menu_->popup(
      speed_button_->mapToGlobal(QPoint(0, speed_button_->height() + 2)));
}

void TransportBar::BeginTimecodeEdit() {
  if (editing_timecode_ || timecode_ == nullptr || timecode_slot_ == nullptr) {
    return;
  }
  auto* label = qobject_cast<QLabel*>(timecode_);
  if (label == nullptr) {
    return;
  }
  auto* layout = qobject_cast<QHBoxLayout*>(timecode_slot_->layout());
  if (layout == nullptr) {
    return;
  }
  if (readout_edit_ == nullptr) {
    readout_edit_ = new QLineEdit(timecode_slot_);
    readout_edit_->setObjectName(QStringLiteral("v2ReadoutEdit"));
    readout_edit_->setAlignment(Qt::AlignCenter);
    readout_edit_->setFixedHeight(tokens_.input);
    readout_edit_->setToolTip(QStringLiteral("帧号（0 基）或 HH:MM:SS:FF"));
    readout_edit_->installEventFilter(this);
    layout->insertWidget(2, readout_edit_);
    readout_edit_->hide();
    ApplyReadoutFont(small_readout_);
  }
  if (label->text().startsWith(QStringLiteral("--"))) {
    return;  // 空态：没有可跳转的会话
  }
  readout_edit_->setText(label->text());
  readout_edit_->setFixedWidth(std::max(80, label->width()));
  editing_timecode_ = true;
  label->hide();
  readout_edit_->show();
  readout_edit_->setFocus(Qt::OtherFocusReason);
  readout_edit_->selectAll();
}

void TransportBar::FinishTimecodeEdit(bool commit) {
  if (!editing_timecode_ || readout_edit_ == nullptr) {
    return;
  }
  editing_timecode_ = false;
  const QString text = readout_edit_->text().trimmed();
  if (commit && !text.isEmpty() && commit_) {
    // 提交失败 = 非法输入/超出范围：静默回滚（标签文本没变过）。
    commit_(text);
  }
  readout_edit_->hide();
  if (timecode_ != nullptr) {
    timecode_->show();
  }
  readout_edit_->clearFocus();
}

bool TransportBar::eventFilter(QObject* watched, QEvent* event) {
  if (watched == timecode_ && event->type() == QEvent::MouseButtonRelease) {
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() == Qt::LeftButton) {
      BeginTimecodeEdit();
      return true;
    }
  }
  if (readout_edit_ != nullptr && watched == readout_edit_) {
    if (event->type() == QEvent::ShortcutOverride) {
      auto* key = static_cast<QKeyEvent*>(event);
      // spec §3.7：输入框聚焦时全局快捷键必须让位（Esc 留给输入框取消）。
      if (key->key() != Qt::Key_Escape) {
        event->accept();
        return true;
      }
      return false;
    }
    if (event->type() == QEvent::KeyPress) {
      auto* key = static_cast<QKeyEvent*>(event);
      if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        FinishTimecodeEdit(true);
        return true;
      }
      if (key->key() == Qt::Key_Escape) {
        FinishTimecodeEdit(false);
        return true;
      }
    }
    if (event->type() == QEvent::FocusOut && editing_timecode_) {
      // 失焦提交（点别处 = 确认输入）。
      FinishTimecodeEdit(true);
      return false;
    }
  }
  return QWidget::eventFilter(watched, event);
}
