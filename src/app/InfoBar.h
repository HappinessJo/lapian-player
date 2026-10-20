#pragma once

#include <QString>
#include <QWidget>

#include "core/FrameMath.h"
#include "media/MediaInfo.h"

class QGridLayout;
class QLabel;
class QResizeEvent;

// T1.6 信息栏 v1：文件名、分辨率/编码/帧率、当前帧、总帧数、NDF 时间码。
// VFR/总帧扫描中时显示横幅占位；总帧数未知不崩。
// T6.4：窄宽度下格式/总帧换到第二行，文件名按宽度省略，避免把窗口最小宽度推大。
class InfoBar : public QWidget {
 public:
  explicit InfoBar(QWidget* parent = nullptr);

  void Clear();
  void SetMedia(const lapian::media::MediaInfo& info, bool vfr);
  void SetFrame(int frame_index);
  void SetTotal(const lapian::core::TotalFrameCount& total);

  QString FileLabelTextForTest() const;
  QString FileNameForTest() const { return file_name_; }
  bool NarrowModeForTest() const { return narrow_; }

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void UpdateTimecode(int frame_index);
  void UpdateLayoutMode();
  void UpdateFileText();

  QGridLayout* grid_ = nullptr;
  QLabel* file_label_ = nullptr;
  QLabel* format_label_ = nullptr;
  QLabel* frame_label_ = nullptr;
  QLabel* timecode_label_ = nullptr;
  QLabel* total_label_ = nullptr;
  QLabel* banner_label_ = nullptr;

  QString file_name_;
  bool narrow_ = false;

  lapian::core::FrameRate rate_;
  bool vfr_ = false;
};
