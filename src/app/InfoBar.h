#pragma once

#include <QWidget>

#include "core/FrameMath.h"
#include "media/MediaInfo.h"

class QLabel;

// T1.6 信息栏 v1：文件名、分辨率/编码/帧率、当前帧、总帧数、NDF 时间码。
// VFR/总帧扫描中时显示横幅占位；总帧数未知不崩。
class InfoBar : public QWidget {
 public:
  explicit InfoBar(QWidget* parent = nullptr);

  void Clear();
  void SetMedia(const lapian::media::MediaInfo& info, bool vfr);
  void SetFrame(int frame_index);
  void SetTotal(const lapian::core::TotalFrameCount& total);

 private:
  void UpdateTimecode(int frame_index);

  QLabel* file_label_ = nullptr;
  QLabel* format_label_ = nullptr;
  QLabel* frame_label_ = nullptr;
  QLabel* timecode_label_ = nullptr;
  QLabel* total_label_ = nullptr;
  QLabel* banner_label_ = nullptr;

  lapian::core::FrameRate rate_;
  bool vfr_ = false;
};
