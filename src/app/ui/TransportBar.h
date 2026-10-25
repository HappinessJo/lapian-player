#pragma once

#include <QWidget>

#include "app/ui/Tokens.h"

class QAction;
class QLabel;
class QToolButton;

// Transport（规划 §2.1/§2.2 + 原型 plan.html）：左侧播放/步进/A-B/速度/静音/音量，
// 中间时间码主读数 + 帧号/总帧次读数，右侧缓冲槽 + 6 个开关。
//
// 视觉基准 = docs/ui-v2-proto/plan.html（分组 + 1px 分隔符 + 26px 等宽时间码）。
// 绑定的都是既有 QAction（enabled/checked 单一真源）；首/末帧与检查器开关没有
// QAction，用信号交给 MainWindow（它拥有窗口级语义）。
class TransportBar : public QWidget {
  Q_OBJECT

 public:
  explicit TransportBar(QWidget* parent = nullptr);

  // 既有 QAction（setDefaultAction）：动作是 enabled/checked 的唯一真源。
  void BindActions(QAction* play, QAction* prev, QAction* next, QAction* loop_in,
                   QAction* loop_out, QAction* loop_toggle, QAction* waveform,
                   QAction* mark, QAction* snapshot, QAction* fullscreen,
                   QAction* mute);

  // 全部是 MainWindow 里已有的原对象（保留 objectName 与 getter）。
  void AdoptTimecode(QWidget* timecode);  // infoTimecode
  void AdoptFrame(QWidget* frame);        // infoFrame
  void AdoptTotal(QWidget* total);        // infoTotal
  void AdoptVolume(QWidget* volume);      // volumeSlider
  void AdoptSpeedLabel(QWidget* speed);   // speedInfo

  void SetLoopEnabled(bool enabled);
  void SetInspectorCollapsed(bool collapsed);
  bool inspector_toggle_checked() const;

  void ApplyTokens(const lapian::ui::Tokens& tokens);

  QWidget* leftHost() const { return left_group_; }
  QWidget* readoutHost() const { return readout_slot_; }
  QWidget* rightHost() const { return right_group_; }
  QWidget* timecodeWidget() const { return timecode_; }
  QWidget* frameWidget() const { return frame_; }
  QWidget* volumeWidget() const { return volume_; }
  QToolButton* playButton() const { return play_button_; }
  QToolButton* loopButton() const { return loop_button_; }
  QToolButton* inspectorButton() const { return inspector_button_; }

 signals:
  void firstFrameRequested();
  void lastFrameRequested();
  void inspectorToggled(bool collapsed);

 public:
  // 原型的宽度降级在 3B 做全量；3A 只保证 820 宽窗口不被裁切、不推高窗口
  // 最小宽度：按自身宽度隐藏次要控件（速度 / A-B / 次读数）。
  QSize minimumSizeHint() const override;

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  QToolButton* MakeIconButton(const char* object_name, QWidget* parent);
  QWidget* MakeSeparator(QWidget* parent);
  void UpdatePlayGlyph();
  void UpdateNarrowState();

  QWidget* sep_left_ = nullptr;
  QWidget* sep_ab_ = nullptr;
  QWidget* sep_right_ = nullptr;
  bool narrow_state_valid_ = false;
  bool hide_speed_ = false;
  bool hide_ab_ = false;
  bool hide_meta_ = false;

  QWidget* left_group_ = nullptr;
  QWidget* readout_slot_ = nullptr;
  QWidget* right_group_ = nullptr;
  QWidget* timecode_slot_ = nullptr;
  QWidget* meta_slot_ = nullptr;
  QWidget* volume_slot_ = nullptr;
  QWidget* speed_slot_ = nullptr;
  QWidget* buffer_slot_ = nullptr;

  QToolButton* first_button_ = nullptr;
  QToolButton* prev_button_ = nullptr;
  QToolButton* play_button_ = nullptr;
  QToolButton* next_button_ = nullptr;
  QToolButton* last_button_ = nullptr;
  QToolButton* in_button_ = nullptr;
  QToolButton* out_button_ = nullptr;
  QToolButton* mute_button_ = nullptr;
  QToolButton* loop_button_ = nullptr;
  QToolButton* wave_button_ = nullptr;
  QToolButton* mark_button_ = nullptr;
  QToolButton* snap_button_ = nullptr;
  QToolButton* full_button_ = nullptr;
  QToolButton* inspector_button_ = nullptr;

  QWidget* timecode_ = nullptr;
  QWidget* frame_ = nullptr;
  QWidget* total_ = nullptr;
  QWidget* volume_ = nullptr;
  QWidget* speed_ = nullptr;
  QAction* play_action_ = nullptr;
};
