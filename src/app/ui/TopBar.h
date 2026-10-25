#pragma once

#include <QString>
#include <QWidget>

#include "app/ui/Tokens.h"

class QAction;
class QLabel;
class QResizeEvent;
class QToolButton;

// 顶栏（规划 §2.1/§2.2）：文件（elide + tooltip 全名）、规格 chip、模式 chip、
// 四个动作按钮（打开/在线/?/设置）+ 在线清晰度入口 + 在线 fps 角标。
//
// 3A 的"搬不换"约束：文件名/规格/角标都是 **InfoBar 里的原对象** reparent 过来，
// 所以 `InfoFileLabelForTest()`/`OnlineFpsBadgeTextForTest()` 这些 getter 不用改指向。
class TopBar : public QWidget {
  Q_OBJECT

 public:
  enum class ModeKind {
    kNeutral = 0,  // 未打开
    kLocal = 1,    // 本地·零网络（ok）
    kOnline = 2,   // 在线（cache）
    kWarn = 3,     // 解析中/缓冲/切档（warn）
    kError = 4,    // 打开失败（err）
  };

  explicit TopBar(QWidget* parent = nullptr);

  // 按钮绑定既有 QAction（setDefaultAction）：动作的 enabled/checked/text 是唯一真源。
  void BindActions(QAction* open, QAction* online);

  void AdoptFileLabel(QWidget* label);
  void AdoptSpecLabel(QWidget* label);
  void AdoptFpsBadge(QWidget* badge);
  void AdoptSettingsButton(QWidget* button);
  void AdoptQualityButton(QWidget* button);

  // 全名（不带路径）；内部做 ElideMiddle，tooltip 保留全名。
  void SetFileName(const QString& file_name);
  QString FileName() const { return file_name_; }
  QString FileLabelText() const;
  QString SpecText() const;
  bool NarrowMode() const { return narrow_; }

  void SetMode(const QString& text, ModeKind kind);
  QString ModeText() const;
  QString ModeKindName() const;

  void ApplyTokens(const lapian::ui::Tokens& tokens);

  QToolButton* openButton() const { return open_button_; }
  QToolButton* onlineButton() const { return online_button_; }
  QToolButton* helpButton() const { return help_button_; }
  QWidget* fileLabel() const { return file_label_; }
  QWidget* specChip() const { return spec_chip_; }
  QLabel* modeChip() const { return mode_chip_; }
  QWidget* qualityButton() const { return quality_button_; }
  QWidget* settingsButton() const { return settings_button_; }

 signals:
  void helpRequested();

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void UpdateFileElide();
  void RefreshChips();

  QLabel* file_icon_ = nullptr;
  QWidget* file_label_ = nullptr;
  QWidget* spec_chip_ = nullptr;
  QWidget* spec_label_ = nullptr;
  QLabel* mode_chip_ = nullptr;
  QWidget* fps_badge_ = nullptr;
  QToolButton* open_button_ = nullptr;
  QToolButton* online_button_ = nullptr;
  QToolButton* help_button_ = nullptr;
  QWidget* settings_button_ = nullptr;
  QWidget* quality_button_ = nullptr;

  QString file_name_;
  bool narrow_ = false;
  ModeKind mode_kind_ = ModeKind::kNeutral;
};
