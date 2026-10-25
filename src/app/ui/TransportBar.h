#pragma once

#include <QWidget>

#include "app/ui/Tokens.h"

// Transport（规划 §2.1/§2.2）3A 骨架：只承载"搬过来"的读数与音量滑条，
// 给 3B 留出左侧按钮组（首/±1/播放/末、A/B、速度/静音）与右侧 6 开关的位置。
//
// 3A 明确不做宽度降级与读数改造（3B），所以左右组在这版是空容器。
class TransportBar : public QWidget {
  Q_OBJECT

 public:
  explicit TransportBar(QWidget* parent = nullptr);

  // 全部是 MainWindow 里已有的原对象（保留 objectName 与 getter）。
  void AdoptTimecode(QWidget* timecode);  // infoTimecode
  void AdoptFrame(QWidget* frame);        // infoFrame
  void AdoptTotal(QWidget* total);        // infoTotal
  void AdoptVolume(QWidget* volume);      // volumeSlider

  void ApplyTokens(const lapian::ui::Tokens& tokens);

  QWidget* leftHost() const { return left_slot_; }
  QWidget* readoutHost() const { return readout_slot_; }
  QWidget* rightHost() const { return right_slot_; }
  QWidget* timecodeWidget() const { return timecode_; }
  QWidget* frameWidget() const { return frame_; }
  QWidget* volumeWidget() const { return volume_; }

 private:
  QWidget* left_slot_ = nullptr;
  QWidget* readout_slot_ = nullptr;
  QWidget* right_slot_ = nullptr;
  QWidget* timecode_slot_ = nullptr;
  QWidget* meta_slot_ = nullptr;
  QWidget* volume_slot_ = nullptr;
  QWidget* timecode_ = nullptr;
  QWidget* frame_ = nullptr;
  QWidget* total_ = nullptr;
  QWidget* volume_ = nullptr;
};
