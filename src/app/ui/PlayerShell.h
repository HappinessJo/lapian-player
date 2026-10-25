#pragma once

#include <QString>
#include <QWidget>

#include "app/ui/Tokens.h"

class BannerSlot;
class PlayerSplitter;
class QResizeEvent;
class StageStack;
class TopBar;
class TransportBar;
class QToolButton;

// UI v2 视图根（规划 §7.1）：只做布局与区域访问器，不含业务逻辑。
// 不持有 QAction、不读 PlaybackController/MarkStore；尺寸只由 ApplyTokens()
// 与用户拖拽改变，resizeEvent 只做"档位判定 + 尺寸落地"。
class PlayerShell : public QWidget {
  Q_OBJECT

 public:
  explicit PlayerShell(QWidget* parent = nullptr);

  TopBar* topBar() const { return top_bar_; }
  BannerSlot* banner() const { return banner_; }
  StageStack* stage() const { return stage_; }
  TransportBar* transport() const { return transport_; }
  QWidget* workspace() const { return workspace_; }
  // 时间轴面板 / 检查器的容器：旧 TimelineWidget / MarkPanel 原对象搬进去。
  QWidget* videoColumn() const { return video_column_; }
  QSize videoColumnMin() const { return video_column_ == nullptr ? QSize() : video_column_->minimumSize(); }
  QSize videoColumnSizeHint() const { return video_column_ == nullptr ? QSize() : video_column_->sizeHint(); }
  QWidget* timelineHost() const { return timeline_host_; }
  QWidget* inspectorHost() const { return inspector_host_; }
  QWidget* inspectorContentHost() const { return inspector_content_; }
  PlayerSplitter* vSplit() const { return v_split_; }
  PlayerSplitter* hSplit() const { return h_split_; }

  // 客户区高度 → 档位（≥640 / 520–639 / <520，20px 迟滞）→ 令牌落地。
  void ApplyClientHeight(int client_height);
  lapian::ui::Density density() const { return tokens_.density; }
  const lapian::ui::Tokens& tokens() const { return tokens_; }
  void SetDensity(lapian::ui::Density density);
  void ApplyTokens();

  void ResetSplitSizes();
  // 走与 handle 相同的夹取路径（96–340 / 220–560），断言极值不裁切。
  void SetSplitSizes(int timeline, int inspector);
  QString SplitterState() const;
  void DragSplitter(bool vertical, int delta);
  bool timelineUserResized() const { return timeline_user_resized_; }

  void SetInspectorCollapsed(bool collapsed);
  bool inspector_collapsed() const { return inspector_collapsed_; }
  void SetFullScreenChrome(bool fullscreen);
  bool fullscreen_chrome() const { return fullscreen_chrome_; }

  // 测试/诊断：当前实际几何。
  int TimelineHeight() const;
  int InspectorWidth() const;
  int TransportHeight() const;
  int TopBarHeight() const;
  int BannerHeight() const;
  QString DensityName() const { return QString::fromLatin1(tokens_.name); }

 protected:
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void ApplySplitSizes();
  void ApplyTimelineConstraints();
  void UpdateInspectorToggle();
  void ApplyWorkspaceMargins();

  TopBar* top_bar_ = nullptr;
  BannerSlot* banner_ = nullptr;
  QWidget* workspace_ = nullptr;
  StageStack* stage_ = nullptr;
  TransportBar* transport_ = nullptr;
  QWidget* video_column_ = nullptr;
  QWidget* timeline_host_ = nullptr;
  QWidget* inspector_host_ = nullptr;
  QWidget* inspector_rail_ = nullptr;
  QWidget* inspector_content_ = nullptr;
  QToolButton* inspector_toggle_ = nullptr;
  PlayerSplitter* v_split_ = nullptr;
  PlayerSplitter* h_split_ = nullptr;

  lapian::ui::Tokens tokens_ = lapian::ui::TokensFor(lapian::ui::Density::kRegular);
  bool has_density_ = false;
  int client_height_ = 0;
  bool timeline_user_resized_ = false;
  bool inspector_user_resized_ = false;
  int timeline_user_height_ = 0;
  int inspector_user_width_ = 0;
  bool inspector_collapsed_ = false;
  bool fullscreen_chrome_ = false;
  bool applying_sizes_ = false;
};
