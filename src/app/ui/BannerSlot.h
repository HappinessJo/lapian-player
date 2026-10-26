#pragma once

#include <cstddef>
#include <vector>

#include <QRect>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "media/MediaInfo.h"
#include "media/StatusNotice.h"

class QLabel;
class QPushButton;
class QResizeEvent;

// 横幅槽（规划 §1-Q4 / §2.1 / 附录 A.3）：同一时刻只有 1 条横幅，参与纵向
// 布局（出现时把下面整体推下，**不覆盖**画面/时间轴）。高度 0 或令牌。
//
// 3D：InfoBar 的五职责拆开后，本类接管"唯一横幅"这一份：
//   - 通知的构建/择一/次级摘要仍然全部由 media::StatusNotice 提供（一行不改）；
//   - 这里只负责保存输入（MediaInfo / 错误 / 在线降帧 / 合成测试），调
//     BuildMediaStatusNotices + SelectPrimaryNotice，然后把 primary 画出来；
//   - `infoBanner` / `infoBannerClose` 两个 objectName 保留（--banner-test 与
//     --robust-test 按名字读），同一时刻仍然只有 1 条。
class BannerSlot : public QWidget {
  Q_OBJECT

 public:
  explicit BannerSlot(QWidget* parent = nullptr);

  // 3A 的 reparent 入口（infoBannerRow 原对象）；3D 后仍可调用，但行内容由
  // 本类接管。保留是为了测试/回退路径不炸。
  void AdoptBannerRow(QWidget* row);
  QWidget* row() const { return row_; }

  // 高度令牌（regular 26 / compact 24 / dense 22）；未出现横幅时高 0。
  void SetHeightToken(int token);
  int heightToken() const { return height_token_; }
  void Refresh();

  // ---- 通知输入（原 InfoBar 的 SetMedia / SetError / SetOnlineFrameRateStatus）----
  void SetMedia(const lapian::media::MediaInfo& info, bool vfr);
  void SetScanning(bool scanning);
  void SetError(const QString& message);
  void ClearError();
  // 在线降帧横幅：空串表示不显示；角标不在这里（在顶栏模式 chip 内）。
  void SetOnlineFrameRateBanner(const QString& banner_text);
  void ClearOnlineFrameRateBanner();
  // 全清（空态/切文件）。
  void Clear();

  // 自动化专用：绕过真实媒体，直接验证六类提示的优先级/布局。
  void SetSyntheticNoticesForTest(const QStringList& kinds);
  void ClearSyntheticNoticesForTest();

  // ---- 查询（沿用 InfoBar 的测试语义）----
  bool BannerVisible() const;
  bool IsVisibleKind(lapian::media::StatusNoticeKind kind) const;
  // 关掉当前降帧横幅（语义不变：只关横幅，角标保留）。
  void Dismiss();
  bool BannerCloseVisible() const;
  bool banner_close_visible() const { return BannerCloseVisible(); }

  QString KindName() const;  // "none" / "vfr" / "error" ...
  QString Text() const;
  QString Tooltip() const;
  QString SecondarySummary() const;
  QString SecondaryTooltip() const;
  QStringList ActiveNoticeKinds() const;
  QRect BannerGeometry() const;  // label 在 BannerSlot 坐标系

  QWidget* bannerLabel() const;
  QPushButton* closeButton() const { return banner_close_; }

 signals:
  void noticesChanged();

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void UpdateNotices();
  void ApplyNotices();
  void UpdateBannerStyle();
  void SetIconForKind(lapian::media::StatusNoticeKind kind);

  QWidget* row_ = nullptr;
  QLabel* icon_ = nullptr;
  QLabel* banner_label_ = nullptr;
  QPushButton* banner_close_ = nullptr;

  bool has_media_ = false;
  bool vfr_ = false;
  bool scanning_ = false;
  bool synthetic_ = false;
  QString error_text_;
  QString online_banner_text_;
  bool online_banner_dismissed_ = false;
  lapian::media::MediaInfo info_;
  std::vector<lapian::media::StatusNotice> notices_;
  std::size_t primary_index_ = lapian::media::kNoNotice;
  lapian::media::StatusNoticeKind primary_kind_ =
      lapian::media::StatusNoticeKind::kNoAudio;

  int height_token_ = 26;
};
