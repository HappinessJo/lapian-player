// T5.2 标记侧栏离屏测试：100 条列表、点击/删除信号、存储位置与警告展示。

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointF>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "app/MarkPanel.h"

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
  if (condition) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n", what.c_str());
    ++g_failures;
  }
}

std::vector<lapian::core::Mark> MakeMarks(int count) {
  std::vector<lapian::core::Mark> marks;
  marks.reserve(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    lapian::core::Mark mark;
    mark.id = "id-" + std::to_string(i);
    mark.frame = i;
    mark.pts_us = static_cast<std::int64_t>(i) * 41667;
    mark.label = "帧 " + std::to_string(i);
    mark.note = i % 3 == 0 ? "第一处转场\n带换行" : "";
    mark.created_at = "2026-10-20T03:40:00.000Z";
    mark.updated_at = mark.created_at;
    marks.push_back(std::move(mark));
  }
  return marks;
}

void SendClick(QTreeWidget* tree, const QPoint& pos) {
  QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos), QPointF(pos),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(tree->viewport(), &press);
  QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos), QPointF(pos),
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(tree->viewport(), &release);
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  QApplication app(argc, argv);

  std::printf("T5.2 标记侧栏测试\n");
  MarkPanel panel;
  panel.resize(320, 620);
  panel.show();
  QApplication::processEvents();

  QTreeWidget* tree = panel.findChild<QTreeWidget*>(QStringLiteral("markTree"));
  QLabel* empty = panel.findChild<QLabel*>(QStringLiteral("markEmpty"));
  QLabel* warning_label =
      panel.findChild<QLabel*>(QStringLiteral("markWarning"));
  QAction* delete_action =
      panel.findChild<QAction*>(QStringLiteral("deleteMarkAction"));
  Check(tree != nullptr, "侧栏包含标记列表");
  Check(empty != nullptr, "侧栏包含空态提示");
  Check(delete_action != nullptr, "侧栏包含删除动作");

  const std::vector<lapian::core::Mark> marks = MakeMarks(100);
  panel.SetMarks(marks);
  QApplication::processEvents();
  Check(panel.MarkCount() == 100, "侧栏接收 100 个标记");
  Check(tree->topLevelItemCount() == 100, "列表显示 100 行");
  Check(!tree->isHidden(), "有标记时列表可见");
  Check(empty->isHidden(), "有标记时空态隐藏");
  Check(tree->topLevelItem(0)->text(0) == QStringLiteral("0"),
        "第一行帧号为 0");
  Check(tree->topLevelItem(99)->text(0) == QStringLiteral("99"),
        "最后一行帧号为 99");
  Check(tree->topLevelItem(0)->text(2).contains(QStringLiteral("第一处转场")),
        "备注摘要包含文本");
  Check(!tree->topLevelItem(0)->text(2).contains(QLatin1Char('\n')),
        "备注摘要压成单行");
  Check(tree->topLevelItem(1)->text(1) == QStringLiteral("帧 1"),
        "标签列显示标签");

  panel.SetStorage(QStringLiteral("C:/media/测试.mp4.lapian.json"), false);
  Check(panel.StorageSummary().contains(QStringLiteral("sidecar")),
        "非回退时显示 sidecar");
  Check(panel.StorageSummary().contains(QStringLiteral("未写入")),
        "未落盘时标注未写入");
  panel.SetStorage(QStringLiteral("C:/appdata/marks/abc.json"), true);
  Check(panel.StorageSummary().contains(QStringLiteral("%APPDATA% 回退")),
        "回退时明确显示 %APPDATA% 回退");
  panel.SetStorage(QStringLiteral("C:/media/测试.mp4.lapian.json"), false);

  panel.SetWarnings({});
  Check(panel.WarningCount() == 0 && warning_label->isHidden(),
        "无警告时不显示警告条");
  lapian::core::MarkWarning warning;
  warning.kind = lapian::core::MarkWarningKind::kPtsMismatch;
  warning.message = "标记帧与 PTS 不一致，已按 PTS 校正";
  warning.path = "C:/media/测试.mp4.lapian.json";
  panel.SetWarnings({warning});
  Check(panel.WarningCount() == 1 && !warning_label->isHidden(),
        "单条警告显示在侧栏");
  Check(warning_label->text().contains(QStringLiteral("PTS")),
        "警告文本可见");
  lapian::core::MarkWarning second = warning;
  second.kind = lapian::core::MarkWarningKind::kCorruptBackup;
  second.message = "标记文件损坏，已备份";
  panel.SetWarnings({warning, second});
  Check(warning_label->text().contains(QStringLiteral("2 条")),
        "多条警告显示汇总条数");
  Check(warning_label->toolTip().contains(QStringLiteral("损坏")),
        "警告详情在 tooltip 中");
  panel.SetWarnings({});

  QString activated_id;
  qint64 activated_frame = -1;
  int activated_count = 0;
  QObject::connect(&panel, &MarkPanel::markActivated,
                   [&](const QString& id, qint64 frame) {
                     activated_id = id;
                     activated_frame = frame;
                     ++activated_count;
                   });
  QTreeWidgetItem* first = tree->topLevelItem(0);
  tree->scrollToItem(first);
  QApplication::processEvents();
  const QRect rect = tree->visualItemRect(first);
  Check(rect.isValid() && !rect.isEmpty(), "首行有可点击矩形");
  SendClick(tree, rect.center());
  QApplication::processEvents();
  Check(activated_id == QStringLiteral("id-0"), "点击行发出标记 id");
  Check(activated_frame == 0, "点击行发出帧号 0");
  Check(activated_count >= 1, "点击触发跳转信号");

  panel.SelectMark("id-42");
  Check(panel.SelectedMarkId() == QStringLiteral("id-42"), "SelectMark 选中指定标记");
  panel.SelectNearFrame(43);
  Check(panel.SelectedMarkId() == QStringLiteral("id-43"),
        "删除后可按原帧选中下一条");

  QString delete_id;
  QObject::connect(&panel, &MarkPanel::deleteRequested,
                   [&](const QString& id) { delete_id = id; });
  panel.SelectMark("id-42");
  delete_action->trigger();
  Check(delete_id == QStringLiteral("id-42"), "Delete 动作发出选中标记 id");

  // 20 次点击的 UI 信号延迟（不含 MainWindow 解码）。
  std::vector<double> click_ms;
  click_ms.reserve(20);
  for (int i = 0; i < 20; ++i) {
    QTreeWidgetItem* item = tree->topLevelItem((i * 7) % 100);
    tree->scrollToItem(item);
    QApplication::processEvents();
    const QRect item_rect = tree->visualItemRect(item);
    QElapsedTimer timer;
    timer.start();
    SendClick(tree, item_rect.center());
    click_ms.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
  }
  std::sort(click_ms.begin(), click_ms.end());
  const double click_p95 =
      click_ms[static_cast<std::size_t>(click_ms.size() * 0.95)];
  std::printf("[perf] mark panel click p50=%.3fms p95=%.3fms\n",
              click_ms[click_ms.size() / 2], click_p95);
  Check(click_p95 < 10.0, "100 标记时点击信号 p95 < 10ms");

  // 100 行整表重建成本（添加/删除后都会重建）。
  std::vector<double> rebuild_ms;
  rebuild_ms.reserve(30);
  for (int i = 0; i < 30; ++i) {
    QElapsedTimer timer;
    timer.start();
    panel.SetMarks(marks);
    rebuild_ms.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
  }
  std::sort(rebuild_ms.begin(), rebuild_ms.end());
  const double rebuild_p95 =
      rebuild_ms[static_cast<std::size_t>(rebuild_ms.size() * 0.95)];
  std::printf("[perf] mark panel rebuild100 p50=%.3fms p95=%.3fms\n",
              rebuild_ms[rebuild_ms.size() / 2], rebuild_p95);
  Check(rebuild_p95 < 20.0, "100 标记列表重建 p95 < 20ms");

  // ---- T5.3 标签/备注编辑 ----
  QLineEdit* label_edit =
      panel.findChild<QLineEdit*>(QStringLiteral("markLabelEdit"));
  QPlainTextEdit* note_edit =
      panel.findChild<QPlainTextEdit*>(QStringLiteral("markNoteEdit"));
  QWidget* editor_box = panel.findChild<QWidget*>(QStringLiteral("markEditor"));
  Check(label_edit != nullptr && note_edit != nullptr && editor_box != nullptr,
        "侧栏包含标签/备注编辑框");

  panel.Clear();
  panel.SetMarks(marks);
  QApplication::processEvents();
  Check(editor_box->isHidden(), "没有选中标记时编辑区隐藏");

  QString edited_id;
  QString edited_label;
  QString edited_note;
  int edit_count = 0;
  QObject::connect(&panel, &MarkPanel::markTextEdited,
                   [&](const QString& id, const QString& label, const QString& note) {
                     edited_id = id;
                     edited_label = label;
                     edited_note = note;
                     ++edit_count;
                   });

  panel.SelectMark("id-5");
  QApplication::processEvents();
  Check(editor_box->isVisible(), "选中标记后编辑区可见");
  Check(label_edit->text() == QStringLiteral("帧 5"), "选中后载入标签");
  Check(note_edit->toPlainText().isEmpty(), "选中后载入空备注");

  edit_count = 0;
  label_edit->setText(QStringLiteral("转场"));
  QKeyEvent label_enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
                        QStringLiteral("\r"));
  QApplication::sendEvent(label_edit, &label_enter);
  Check(edit_count == 1 && edited_id == QStringLiteral("id-5") &&
            edited_label == QStringLiteral("转场") && edited_note.isEmpty(),
        "标签 Enter 提交 markTextEdited");
  Check(tree->topLevelItemCount() == 100 &&
            tree->currentItem()->text(1) == QStringLiteral("转场"),
        "提交只更新当前行标签且不重建列表");
  Check(panel.SelectedMarkId() == QStringLiteral("id-5"), "提交后保持选中");

  edit_count = 0;
  note_edit->setPlainText(QStringLiteral("第一行"));
  QKeyEvent plain_enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
                        QStringLiteral("\r"));
  QApplication::sendEvent(note_edit, &plain_enter);
  Check(edit_count == 0, "备注普通 Enter 不提交");
  Check(note_edit->toPlainText().contains(QLatin1Char('\n')),
        "备注普通 Enter 换行");

  note_edit->setPlainText(QStringLiteral("第一行\n第二行"));
  QKeyEvent ctrl_enter(QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier,
                       QStringLiteral("\r"));
  QApplication::sendEvent(note_edit, &ctrl_enter);
  Check(edit_count == 1 && edited_id == QStringLiteral("id-5") &&
            edited_note == QStringLiteral("第一行\n第二行"),
        "备注 Ctrl+Enter 提交多行文本");
  Check(tree->currentItem()->text(2) == QStringLiteral("第一行 第二行"),
        "备注摘要即时更新且压成单行");

  edit_count = 0;
  note_edit->clear();
  Check(panel.FlushEditor(), "清空备注可 flush");
  Check(edit_count == 1 && edited_note.isEmpty(), "允许保存空备注");
  Check(tree->currentItem()->text(2).isEmpty(), "空备注摘要为空");

  edit_count = 0;
  label_edit->setText(QStringLiteral("未提交标签"));
  panel.SelectMark("id-6");
  QApplication::processEvents();
  Check(edit_count == 1 && edited_id == QStringLiteral("id-5") &&
            edited_label == QStringLiteral("未提交标签"),
        "切换选中前提交旧标记的编辑");
  Check(panel.SelectedMarkId() == QStringLiteral("id-6"), "切换到新标记");
  Check(label_edit->text() == QStringLiteral("帧 6") &&
            note_edit->toPlainText().contains(QStringLiteral("第一处转场")),
        "新标记载入自己的标签/备注");

  QTreeWidgetItem* first_item = tree->topLevelItem(0);
  panel.UpdateMarkText(QStringLiteral("id-0"), QStringLiteral("外部标签"),
                       QStringLiteral("外部备注"));
  Check(tree->topLevelItem(0) == first_item, "UpdateMarkText 不重建列表");
  Check(first_item->text(1) == QStringLiteral("外部标签") &&
            first_item->text(2) == QStringLiteral("外部备注"),
        "UpdateMarkText 更新行文本");
  Check(panel.SelectedMarkId() == QStringLiteral("id-6"),
        "UpdateMarkText 不改变选中");

  // spec §3.7：文本框聚焦时 Space/W 等不应落到全局 QAction。
  const bool editor_focused = panel.FocusNoteEditorForTest();
  Check(editor_focused, "备注框可获得焦点");
  QKeyEvent space_override(QEvent::ShortcutOverride, Qt::Key_Space, Qt::NoModifier);
  QApplication::sendEvent(note_edit, &space_override);
  Check(space_override.isAccepted(), "备注框拦截 Space 的 ShortcutOverride");
  QKeyEvent w_override(QEvent::ShortcutOverride, Qt::Key_W, Qt::NoModifier);
  QApplication::sendEvent(note_edit, &w_override);
  Check(w_override.isAccepted(), "备注框拦截 W 的 ShortcutOverride");
  QKeyEvent ctrl_m_override(QEvent::ShortcutOverride, Qt::Key_M, Qt::ControlModifier);
  QApplication::sendEvent(note_edit, &ctrl_m_override);
  Check(ctrl_m_override.isAccepted(), "备注框拦截 Ctrl+M 的 ShortcutOverride");

  panel.Clear();
  QApplication::processEvents();
  Check(editor_box->isHidden(), "清空标记后编辑区隐藏");
  Check(panel.MarkCount() == 0, "Clear 清空标记");
  Check(panel.WarningCount() == 0, "Clear 清空警告");
  Check(!empty->isHidden(), "清空后显示空态");
  Check(tree->isHidden(), "清空后隐藏列表");

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("全部通过\n");
  return 0;
}
