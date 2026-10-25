#pragma once

#include <QWidget>

class QResizeEvent;

// 横幅槽（规划 §1-Q4 / §2.1）：同一时刻只有 1 条横幅，参与纵向布局
// （出现时把下面整体推下，**不覆盖**画面/时间轴）。
//
// 3A 的"搬不换"约束：横幅用到的构建/择一/次级摘要语义全部留在
// media::StatusNotice + InfoBar::ApplyNotices 里，**一行不改**；本类只是
// 把 InfoBar 内部的 banner_row_（`infoBanner` 标签 + `infoBannerClose`）
// 原对象 reparent 进来，负责高度与几何。3D 删 InfoBar 时这段逻辑再下沉。
class BannerSlot : public QWidget {
  Q_OBJECT

 public:
  explicit BannerSlot(QWidget* parent = nullptr);

  void AdoptBannerRow(QWidget* row);
  QWidget* row() const { return row_; }

  // 高度令牌（regular 26 / compact 24 / dense 22）；未出现横幅时高 0。
  void SetHeightToken(int token);
  int heightToken() const { return height_token_; }
  void Refresh();

  bool BannerVisible() const;
  QWidget* bannerLabel() const;

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  QWidget* row_ = nullptr;
  int height_token_ = 26;
};
