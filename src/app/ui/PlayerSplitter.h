#pragma once

#include <QSplitter>
#include <QSplitterHandle>

// UI v2 可拖版块（规划 §1-Q2 / §7.4）：
// - 6px handle，悬停时画 2px accent 细线（不靠 QSS 技巧）
// - 双击 handle = 恢复当前档位默认尺寸（emit resetRequested）
// - 折叠时 setEnabled(false)，不抢焦点（Qt::NoFocus）
class PlayerSplitter;

class PlayerSplitHandle : public QSplitterHandle {
  Q_OBJECT

 public:
  PlayerSplitHandle(Qt::Orientation orientation, PlayerSplitter* parent);

  void SetAccentColor(const QColor& color) { accent_ = color; update(); }
  void SetLineColor(const QColor& color) { line_ = color; update(); }

 signals:
  void resetRequested();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  QColor accent_;
  QColor line_;
  bool hovered_ = false;
};

class PlayerSplitter : public QSplitter {
  Q_OBJECT

 public:
  explicit PlayerSplitter(Qt::Orientation orientation,
                          QWidget* parent = nullptr);

  // 双击复位：由 PlayerShell 接到 ResetSplitSizes()。
  void SetHandleWidthPx(int px);
  void SetCollapsed(bool collapsed);
  bool collapsed() const { return collapsed_; }

 signals:
  void resetRequested();

 protected:
  QSplitterHandle* createHandle() override;

 private:
  bool collapsed_ = false;
};
