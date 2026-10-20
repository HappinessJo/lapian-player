// T5.2 标记侧栏离屏测试：100 条列表、点击/删除信号、存储位置与警告展示。

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QElapsedTimer>
#include <QLabel>
#include <QMouseEvent>
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

  panel.Clear();
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
