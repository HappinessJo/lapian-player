#pragma once

#include <QRect>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <cstddef>
#include <vector>

#include "core/FrameMath.h"
#include "media/MediaInfo.h"
#include "media/StatusNotice.h"

class QGridLayout;
class QLabel;
class QResizeEvent;

// T1.6 信息栏 v1：文件名、分辨率/编码/帧率、当前帧、总帧数、NDF 时间码。
// T6.5：所有状态提示先按优先级排序，InfoBar 只显示最高优先级的一条横幅；
// 其余由 MainWindow 放进状态栏。横幅参与纵向布局，不覆盖时间轴/帧号/播放控件。
// T6.4：窄宽度下格式/总帧换到第二行，文件名按宽度省略，避免把窗口最小宽度推大。
class InfoBar : public QWidget {
  Q_OBJECT

 public:
  explicit InfoBar(QWidget* parent = nullptr);

  void Clear();
  void SetMedia(const lapian::media::MediaInfo& info, bool vfr);
  void SetFrame(int frame_index);
  void SetTotal(const lapian::core::TotalFrameCount& total);
  // T6.5：打开失败等错误横幅；下一次 SetMedia/Clear 自动清除。
  void SetError(const QString& message);
  void ClearError();

  QString FileLabelTextForTest() const;
  QString FileNameForTest() const { return file_name_; }
  bool NarrowModeForTest() const { return narrow_; }
  QString BannerKindForTest() const;
  QString BannerTextForTest() const;
  QString BannerTooltipForTest() const;
  bool BannerVisibleForTest() const;
  QRect BannerGeometryForTest() const;
  QString SecondarySummaryForTest() const;
  QString SecondaryTooltipForTest() const;
  QStringList ActiveNoticeKindsForTest() const;
  // 自动化专用：绕过真实媒体，直接验证六类提示的优先级/布局。
  void SetSyntheticNoticesForTest(const QStringList& kinds);
  void ClearSyntheticNoticesForTest();

 signals:
  void noticesChanged();

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void UpdateTimecode(int frame_index);
  void UpdateLayoutMode();
  void UpdateFileText();
  void UpdateNotices();
  void ApplyNotices();
  void UpdateBannerStyle(lapian::media::StatusNoticeKind kind);

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
  bool has_media_ = false;
  bool scanning_ = false;
  bool synthetic_ = false;
  QString error_text_;
  lapian::media::MediaInfo info_;
  std::vector<lapian::media::StatusNotice> notices_;
  std::size_t primary_index_ = lapian::media::kNoNotice;
};
