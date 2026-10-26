#pragma once

#include <cstdint>
#include <functional>

#include <QWidget>

#include "app/ui/Tokens.h"

class QAction;
class QLabel;
class QLineEdit;
class QMenu;
class QProgressBar;
class QToolButton;

// Transport（规划 §2.1/§2.2 + 原型 plan.html）：左侧播放/步进/A-B/速度/静音/音量，
// 中间时间码主读数（可点击输入）+ 帧号/总帧次读数，右侧缓冲槽 + 6 个开关。
//
// 视觉基准 = docs/ui-v2-proto/plan.html（分组 + 1px 分隔符 + 26px 等宽时间码）。
// 绑定的都是既有 QAction（enabled/checked 单一真源）；首/末帧与检查器开关没有
// QAction，用信号交给 MainWindow（它拥有窗口级语义）。
//
// 3B 追加：时间码内联输入、速度菜单按钮、在线缓冲槽、四档宽度降级（§3.3）。
class TransportBar : public QWidget {
  Q_OBJECT

 public:
  explicit TransportBar(QWidget* parent = nullptr);

  // 时间码提交：返回 false 表示非法输入 → 控件回滚（不弹框、不改帧）。
  using TimecodeCommit = std::function<bool(const QString& text)>;

  // 既有 QAction：动作是 enabled/checked 的唯一真源。
  void BindActions(QAction* play, QAction* prev, QAction* next, QAction* loop_in,
                   QAction* loop_out, QAction* loop_toggle, QAction* waveform,
                   QAction* mark, QAction* snapshot, QAction* fullscreen,
                   QAction* mute);

  // 全部是 MainWindow 里已有的原对象（保留 objectName 与 getter）。
  void AdoptTimecode(QWidget* timecode);  // v2Timecode
  void AdoptFrame(QWidget* frame);        // v2FrameNumber
  void AdoptTotal(QWidget* total);        // v2FrameTotal
  void AdoptVolume(QWidget* volume);      // volumeSlider

  void SetLoopEnabled(bool enabled);
  void SetInspectorCollapsed(bool collapsed);
  bool inspector_toggle_checked() const;
  // 速度文本仍由 MainWindow 给（FormatSpeed），这里只显示。
  void SetSpeedText(const QString& text);
  // 全屏时全屏按钮的 tooltip 变成"退出全屏（Esc）"（M5 第 17 行）。
  void SetFullScreenActive(bool fullscreen);
  // 缓冲槽：仅在线 buffering/stalled 时出现（数据来自 OnlineSession 回调）。
  void SetBufferState(bool visible, qint64 cached_bytes, qint64 total_bytes);
  void SetTimecodeCommit(TimecodeCommit commit);
  bool editing_timecode() const { return editing_timecode_; }

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
  QToolButton* speedButton() const { return speed_button_; }
  QToolButton* muteButton() const { return mute_button_; }
  QWidget* bufferSlot() const { return buffer_slot_; }
  QLabel* bufferText() const { return buffer_text_; }
  QLineEdit* readoutEdit() const { return readout_edit_; }
  // 宽度降级档位 + 实测所需的保留宽度（诊断/交接用，见 §3.3）。
  QString TierStateForTest() const;

 signals:
  void firstFrameRequested();
  void lastFrameRequested();
  void inspectorToggled(bool collapsed);
  void speedSelected(double rate);

 public:
  // 允许被压窄：窄档由 UpdateNarrowState() 按自身宽度隐藏次要控件。
  QSize minimumSizeHint() const override;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  QToolButton* MakeIconButton(const char* object_name, QWidget* parent);
  QWidget* MakeSeparator(QWidget* parent);
  void UpdatePlayGlyph();
  void UpdateNarrowState();
  void ApplyReadoutFont(bool small);
  void BeginTimecodeEdit();
  void FinishTimecodeEdit(bool commit);
  void ShowSpeedMenu();
  int RetainedNeedWidth() const;

  QWidget* sep_left_ = nullptr;
  QWidget* sep_ab_ = nullptr;
  QWidget* sep_right_ = nullptr;
  bool narrow_state_valid_ = false;
  bool hide_volume_ = false;
  bool hide_ab_ = false;
  bool hide_speed_ = false;
  bool hide_first_last_ = false;
  bool hide_mark_snap_ = false;
  bool small_readout_ = false;
  int tier_need_ = 0;

  QWidget* left_group_ = nullptr;
  QWidget* readout_slot_ = nullptr;
  QWidget* right_group_ = nullptr;
  QWidget* timecode_slot_ = nullptr;
  QWidget* meta_slot_ = nullptr;
  QWidget* volume_slot_ = nullptr;
  QWidget* speed_slot_ = nullptr;
  QWidget* buffer_slot_ = nullptr;
  QProgressBar* buffer_bar_ = nullptr;
  QLabel* buffer_text_ = nullptr;

  QToolButton* first_button_ = nullptr;
  QToolButton* prev_button_ = nullptr;
  QToolButton* play_button_ = nullptr;
  QToolButton* next_button_ = nullptr;
  QToolButton* last_button_ = nullptr;
  QToolButton* in_button_ = nullptr;
  QToolButton* out_button_ = nullptr;
  QToolButton* speed_button_ = nullptr;
  QToolButton* mute_button_ = nullptr;
  QToolButton* loop_button_ = nullptr;
  QToolButton* wave_button_ = nullptr;
  QToolButton* mark_button_ = nullptr;
  QToolButton* snap_button_ = nullptr;
  QToolButton* full_button_ = nullptr;
  QToolButton* inspector_button_ = nullptr;

  QWidget* timecode_ = nullptr;
  QLineEdit* readout_edit_ = nullptr;
  QWidget* frame_ = nullptr;
  QWidget* total_ = nullptr;
  QWidget* volume_ = nullptr;
  QAction* play_action_ = nullptr;
  QAction* fullscreen_action_ = nullptr;
  QMenu* speed_menu_ = nullptr;
  TimecodeCommit commit_;
  bool editing_timecode_ = false;
  bool fullscreen_active_ = false;
  double readout_px_ = 26.0;

  lapian::ui::Tokens tokens_ = lapian::ui::TokensFor(lapian::ui::Density::kRegular);
};
