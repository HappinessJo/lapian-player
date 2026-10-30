#include <QAbstractButton>
#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

#include "app/DragStrategyTestRunner.h"
#include "app/DragTestRunner.h"
#include "app/GopBoundaryTestRunner.h"
#include "app/MainWindow.h"
#include "app/MemoryTestRunner.h"
#include "app/ProcessMemory.h"
#include "app/OnlineAuthTestRunner.h"
#include "app/OnlineCacheManageTestRunner.h"
#include "app/OnlineCacheTestRunner.h"
#include "app/OnlineDragPreviewTestRunner.h"
#include "app/OnlineFetchTestRunner.h"
#include "app/OnlineBufferTestRunner.h"
#include "app/OnlineGrowingTestRunner.h"
#include "app/OnlineHttpSelfCheckRunner.h"
#include "app/OnlineIndexTestRunner.h"
#include "app/OnlineFaultTestRunner.h"
#include "app/OnlineLongPressTestRunner.h"
#include "app/OnlineM1TestRunner.h"
#include "app/OnlineM2TestRunner.h"
#include "app/OnlineQualityTestRunner.h"
#include "app/OnlineQualitySwitchTestRunner.h"
#include "app/OnlineVipTestRunner.h"
#include "app/OnlineOfflineTestRunner.h"
#include "app/OnlineQrTestRunner.h"
#include "app/OnlineResolveTestRunner.h"
#include "app/RobustTestRunner.h"
#include "app/ui/Theme.h"

namespace {

bool HasArgument(int argc, char* argv[], const char* flag) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], flag) == 0) {
      return true;
    }
  }
  return false;
}

// 3B：主参数的 else-if 链已经到 MSVC 的"块嵌套太深"上限（C1061），3B 的两个
// 专用参数在进链之前先摘出来（连同它们的值一起从列表里删掉，否则值会被当成
// 位置参数当成文件路径）。
QStringList ExtractUiV2Args(const QStringList& args, int* timeline_height,
                            bool* readout_edit_test, bool* dpi_empty,
                            bool* ui_shots_test) {
  QStringList filtered;
  filtered.reserve(args.size());
  for (int i = 0; i < args.size(); ++i) {
    if (args[i] == QStringLiteral("--timeline-height") &&
        i + 1 < args.size()) {
      if (timeline_height != nullptr) {
        *timeline_height = args[++i].toInt();
      } else {
        ++i;
      }
      continue;
    }
    if (args[i] == QStringLiteral("--readout-edit-test")) {
      if (readout_edit_test != nullptr) {
        *readout_edit_test = true;
      }
      continue;
    }
    // 3D：M3 的空态相位（不需要 --file）。与 --dpi-test 一起用。
    if (args[i] == QStringLiteral("--empty")) {
      if (dpi_empty != nullptr) {
        *dpi_empty = true;
      }
      continue;
    }
    // 3D：M2 主题截图（主窗 + 设置/帮助/在线对话框 + 速度菜单）。
    if (args[i] == QStringLiteral("--ui-shots-test")) {
      if (ui_shots_test != nullptr) {
        *ui_shots_test = true;
      }
      continue;
    }
    filtered << args[i];
  }
  return filtered;
}

// 3B：时间码内联输入（M5 第 6 行 + "输入框聚焦时全局键让位"）。
// 走真实控件路径：点标签 → QLineEdit 出现 → Enter/失焦提交、Esc 取消、
// 非法输入回滚；解析复用 ApplyGotoText（帧号 + HH:MM:SS:FF）。
// 独立函数是为了不把 main 里的测试 lambda 再往下嵌一层（MSVC 嵌套上限）。
void RunReadoutEditTest(MainWindow& window) {
  auto failures = std::make_shared<int>(0);
  auto check = [failures](const char* name, bool ok) {
    if (!ok) {
      ++(*failures);
    }
    std::fprintf(stderr, "T6.2 readout_check %s ok=%d\n", name, ok ? 1 : 0);
    std::fflush(stderr);
  };
  QTimer::singleShot(700, &window, [&window, check, failures]() {
    auto label = window.findChild<QLabel*>(QStringLiteral("v2Timecode"));
    check("label-present", label != nullptr && label->isVisible());
    if (label == nullptr) {
      QCoreApplication::exit(*failures == 0 ? 0 : 1);
      return;
    }
    auto click_label = [label]() {
      const QPointF local(label->width() / 2.0, label->height() / 2.0);
      const QPointF global = label->mapToGlobal(local.toPoint());
      QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(label, &press);
      QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(label, &release);
    };
    auto send_key = [&window](int key, int modifiers = 0) {
      QWidget* target = QApplication::focusWidget();
      if (target == nullptr) {
        target = &window;
      }
      QKeyEvent press(QEvent::KeyPress, key, Qt::KeyboardModifiers(modifiers),
                      QString(), false);
      QApplication::sendEvent(target, &press);
      QKeyEvent release(QEvent::KeyRelease, key,
                        Qt::KeyboardModifiers(modifiers), QString(), false);
      QApplication::sendEvent(target, &release);
    };

    click_label();
    auto edit = window.findChild<QLineEdit*>(QStringLiteral("v2ReadoutEdit"));
    check("edit-created", edit != nullptr);
    if (edit == nullptr) {
      QCoreApplication::exit(*failures == 0 ? 0 : 1);
      return;
    }
    check("edit-visible", edit->isVisible());
    check("edit-focused", QApplication::focusWidget() == edit);
    check("edit-text-focus", window.IsTextInputFocusedForTest());
    check("label-hidden-while-editing", !label->isVisible());

    // 全局键让位：Space/PgDn/W/M/L/I/O/S/F/[ ] 都不得改变播放状态。
    const QString state_before = window.KeyStateForTest();
    const int keys[] = {Qt::Key_Space, Qt::Key_PageDown, Qt::Key_PageUp,
                        Qt::Key_W,     Qt::Key_M,        Qt::Key_L,
                        Qt::Key_I,     Qt::Key_O,        Qt::Key_S,
                        Qt::Key_F,     Qt::Key_BracketLeft,
                        Qt::Key_BracketRight};
    for (const int key : keys) {
      send_key(key);
    }
    check("keys-yield", window.KeyStateForTest() == state_before);

    // Enter 提交帧号；成功后 edit 收起、标签回来、帧号到位。
    edit->setText(QStringLiteral("123"));
    send_key(Qt::Key_Return);
    check("commit-frame", window.CurrentFrameForTest() == 123);
    check("edit-hidden-after-commit", !edit->isVisible());
    check("label-back", label->isVisible());
    std::fprintf(stderr, "T6.2 readout_commit frame=%lld label=%s\n",
                 static_cast<long long>(window.CurrentFrameForTest()),
                 window.ReadoutTextForTest().toUtf8().constData());
    std::fflush(stderr);

    // HH:MM:SS:FF：00:00:02:00 = 第 2 秒 = 30fps 下的第 60 帧。
    click_label();
    edit->setText(QStringLiteral("00:00:02:00"));
    send_key(Qt::Key_Return);
    const qint64 tc_frame = window.CurrentFrameForTest();
    std::fprintf(stderr, "T6.2 readout_timecode frame=%lld text=%s\n",
                 static_cast<long long>(tc_frame),
                 window.ReadoutTextForTest().toUtf8().constData());
    std::fflush(stderr);
    check("commit-timecode", tc_frame == 60);

    // Esc 取消：帧号不变。
    const qint64 before_esc = window.CurrentFrameForTest();
    click_label();
    edit->setText(QStringLiteral("99999"));
    send_key(Qt::Key_Escape);
    check("esc-cancels", window.CurrentFrameForTest() == before_esc);
    check("esc-keeps-label", label->isVisible());

    // 非法输入：回滚，不跳帧。
    const qint64 before_bad = window.CurrentFrameForTest();
    click_label();
    edit->setText(QStringLiteral("abc"));
    send_key(Qt::Key_Return);
    check("invalid-rollback", window.CurrentFrameForTest() == before_bad);
    check("invalid-label-back", label->isVisible());

    // 失焦提交。
    click_label();
    edit->setText(QStringLiteral("42"));
    window.ClearFocusForTest();
    QCoreApplication::processEvents();
    check("blur-commits", window.CurrentFrameForTest() == 42);

    // M7：帧号控件在播放/暂停/拖动/全屏四态的几何（rel = 帧号中心 − Transport
    // 中心；窗口尺寸变了也只有这个量能跨态比较）。四态都必须可见。
    auto log_m7 = [&window, check](const char* phase, QString* baseline) {
      const QString state = window.M7StateForTest();
      std::fprintf(stderr, "T6.2 readout_m7 %s %s\n", phase,
                   state.toUtf8().constData());
      std::fflush(stderr);
      check((std::string("m7-visible-") + phase).c_str(),
            state.contains(QStringLiteral("visible=1")));
      const int rel_pos = state.indexOf(QStringLiteral("rel="));
      const QString rel = rel_pos < 0 ? QString() : state.mid(rel_pos);
      if (baseline->isEmpty()) {
        *baseline = rel;
      }
      check((std::string("m7-rel-") + phase).c_str(), rel == *baseline);
    };
    QString m7_baseline;
    window.ClearFocusForTest();
    // 固定在同一帧号（100，3 位数）比较：帧号文本宽度一变，居中块的中心就会
    // 跟着挪几像素（那不是"位置不稳定"，是数字变宽）。
    auto goto_100 = [&window]() {
      window.GoToFrameForTest(100);
      QCoreApplication::processEvents();
    };
    goto_100();
    log_m7("paused", &m7_baseline);
    window.TogglePlaybackForTest();
    QCoreApplication::processEvents();
    log_m7("playing", &m7_baseline);
    window.TogglePlaybackForTest();
    QCoreApplication::processEvents();
    goto_100();
    window.ScrubStartForTest(100);
    window.ScrubTargetForTest(100);
    QCoreApplication::processEvents();
    log_m7("scrubbing", &m7_baseline);
    window.ScrubFinishForTest(100);
    QCoreApplication::processEvents();
    goto_100();
    window.ToggleFullScreenForTest();
    QCoreApplication::processEvents();
    log_m7("fullscreen", &m7_baseline);
    // M5 第 17 行：全屏时全屏按钮的 tooltip 变成"退出全屏（Esc）"。
    if (QWidget* full = window.findChild<QWidget*>(QStringLiteral("v2FullScreenButton"))) {
      std::fprintf(stderr, "T6.2 readout_exit_tooltip %s\n",
                   full->toolTip().toUtf8().constData());
      std::fflush(stderr);
      check("m5-exit-fullscreen-tooltip",
            full->toolTip() == QStringLiteral("退出全屏（Esc）"));
    }
    window.ToggleFullScreenForTest();
    QCoreApplication::processEvents();
    if (QWidget* full = window.findChild<QWidget*>(QStringLiteral("v2FullScreenButton"))) {
      check("m5-enter-fullscreen-tooltip",
            full->toolTip() == QStringLiteral("全屏（F / 双击画面）"));
    }

    // 工作区端点必须与画布上的同一帧垂直对齐（用户 2026-10-26 反馈的偏差）：
    // 设 A=300 / B=700，比较"滑块像素"与"画布像素"，两侧都要 |dx| ≤ 1。
    window.GoToFrameForTest(300);
    window.SetLoopInForTest();
    window.GoToFrameForTest(700);
    window.SetLoopOutForTest();
    QCoreApplication::processEvents();
    const QString align = window.TimelineBarAlignForTest();
    std::fprintf(stderr, "T6.2 readout_align %s\n", align.toUtf8().constData());
    std::fflush(stderr);
    auto align_value = [&align](const char* key) {
      const int at = align.indexOf(QString::fromLatin1(key) + QLatin1Char('='));
      if (at < 0) {
        return 999;
      }
      int start = at + static_cast<int>(std::strlen(key)) + 1;
      const bool negative = start < align.size() && align.at(start) == QLatin1Char('-');
      if (negative) {
        ++start;
      }
      int end = start;
      while (end < align.size() && align.at(end).isDigit()) {
        ++end;
      }
      const int value = align.mid(start, end - start).toInt();
      return negative ? -value : value;
    };
    const int dx_a = align_value("dx_a");
    const int dx_b = align_value("dx_b");
    std::fprintf(stderr, "T6.2 readout_align_dx a=%d b=%d\n", dx_a, dx_b);
    std::fflush(stderr);
    check("bar-align-a", std::abs(dx_a) <= 1);
    check("bar-align-b", std::abs(dx_b) <= 1);
    window.ClearLoopForTest();
    QCoreApplication::processEvents();

    std::fprintf(stderr, "T6.2 readout_summary fails=%d tier=%s split=%s\n",
                 *failures, window.TransportTierForTest().toUtf8().constData(),
                 window.SplitterStateForTest().toUtf8().constData());
    std::fflush(stderr);
    // M5 证据：把带 tooltip 的控件名与文案整表打出来（交接里逐条对照 §2.3）。
    for (QWidget* widget : window.findChildren<QWidget*>()) {
      const QString tip = widget->toolTip();
      if (tip.isEmpty()) {
        continue;
      }
      std::fprintf(stderr, "T6.2 tooltip %s | %s\n",
                   widget->objectName().toUtf8().constData(),
                   tip.toUtf8().constData());
    }
    std::fflush(stderr);
    QCoreApplication::exit(*failures == 0 ? 0 : 1);
  });
}

}  // namespace

int main(int argc, char* argv[]) {
  // T8.2：无 GUI 的 HTTP 栈自检必须在创建 QApplication/MainWindow 之前分流，
  // 否则会初始化窗口/网络外的东西，违背“无 GUI、本地启动不联网”的自检语义。
  if (HasArgument(argc, argv, "--online-http-selfcheck")) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("拉片软件"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("lapian.local"));
    QCoreApplication::setApplicationName(QStringLiteral("拉片软件"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LAPIAN_VERSION));

    lapian::app::OnlineHttpSelfCheckOptions options;
    const QStringList selfcheck_args = QCoreApplication::arguments();
    for (int i = 1; i < selfcheck_args.size(); ++i) {
      const QString& arg = selfcheck_args[i];
      if (arg == QStringLiteral("--http-stack") && i + 1 < selfcheck_args.size()) {
        options.stack = selfcheck_args[++i];
      } else if (arg.startsWith(QStringLiteral("--http-stack="))) {
        options.stack = arg.mid(static_cast<int>(std::strlen("--http-stack=")));
      } else if (arg == QStringLiteral("--sessdata-file") &&
                 i + 1 < selfcheck_args.size()) {
        options.sessdata_file = selfcheck_args[++i];
      } else if (arg.startsWith(QStringLiteral("--sessdata-file="))) {
        options.sessdata_file =
            arg.mid(static_cast<int>(std::strlen("--sessdata-file=")));
      } else if (arg == QStringLiteral("--timeout-ms") &&
                 i + 1 < selfcheck_args.size()) {
        options.timeout_ms = selfcheck_args[++i].toInt();
      } else if (arg.startsWith(QStringLiteral("--timeout-ms="))) {
        options.timeout_ms =
            arg.mid(static_cast<int>(std::strlen("--timeout-ms="))).toInt();
      } else if (arg == QStringLiteral("--verbose")) {
        options.verbose = true;
      }
    }
    if (options.timeout_ms < 200) {
      options.timeout_ms = 200;
    }
    if (options.timeout_ms > 10000) {
      options.timeout_ms = 10000;
    }
    return lapian::app::RunOnlineHttpSelfCheck(options);
  }

  // T8.3：缓存存储/配额探针同样无 GUI：临时根 + 合成文件，不联网、不需要 SESSDATA。
  if (HasArgument(argc, argv, "--online-cache-test")) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("拉片软件"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("lapian.local"));
    QCoreApplication::setApplicationName(QStringLiteral("拉片软件"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LAPIAN_VERSION));

    lapian::app::OnlineCacheTestOptions options;
    const QStringList cache_args = QCoreApplication::arguments();
    for (int i = 1; i < cache_args.size(); ++i) {
      const QString& arg = cache_args[i];
      if (arg == QStringLiteral("--cache-test-root") && i + 1 < cache_args.size()) {
        options.root = cache_args[++i];
      } else if (arg.startsWith(QStringLiteral("--cache-test-root="))) {
        options.root =
            arg.mid(static_cast<int>(std::strlen("--cache-test-root=")));
      } else if (arg == QStringLiteral("--cache-test-keep")) {
        options.keep_root = true;
      }
    }
    return lapian::app::RunOnlineCacheTest(options);
  }

  // T8.3.1：缓存管理探针（设置目录/配额、清空、孤儿清理、只读/磁盘满）。
  // 默认无 GUI、临时根；`--ui` 额外构造设置对话框与 MainWindow 清理入口。
  if (HasArgument(argc, argv, "--online-cache-manage-test")) {
    const bool ui_smoke = HasArgument(argc, argv, "--ui");
    std::unique_ptr<QCoreApplication> app;
    if (ui_smoke) {
      app = std::make_unique<QApplication>(argc, argv);
    } else {
      app = std::make_unique<QCoreApplication>(argc, argv);
    }
    QCoreApplication::setOrganizationName(QStringLiteral("拉片软件"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("lapian.local"));
    QCoreApplication::setApplicationName(QStringLiteral("拉片软件"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LAPIAN_VERSION));

    lapian::app::OnlineCacheManageTestOptions options;
    const QStringList manage_args = QCoreApplication::arguments();
    for (int i = 1; i < manage_args.size(); ++i) {
      const QString& arg = manage_args[i];
      auto take = [&](QString* target) {
        if (i + 1 < manage_args.size()) {
          *target = manage_args[++i];
          return true;
        }
        return false;
      };
      if (arg == QStringLiteral("--manage-root")) {
        take(&options.root);
      } else if (arg.startsWith(QStringLiteral("--manage-root="))) {
        options.root = arg.mid(static_cast<int>(std::strlen("--manage-root=")));
      } else if (arg == QStringLiteral("--manage-records-root")) {
        take(&options.records_root);
      } else if (arg.startsWith(QStringLiteral("--manage-records-root="))) {
        options.records_root =
            arg.mid(static_cast<int>(std::strlen("--manage-records-root=")));
      } else if (arg == QStringLiteral("--manage-settings-root")) {
        take(&options.settings_root);
      } else if (arg.startsWith(QStringLiteral("--manage-settings-root="))) {
        options.settings_root =
            arg.mid(static_cast<int>(std::strlen("--manage-settings-root=")));
      } else if (arg == QStringLiteral("--manage-readonly-root")) {
        take(&options.readonly_root);
      } else if (arg.startsWith(QStringLiteral("--manage-readonly-root="))) {
        options.readonly_root =
            arg.mid(static_cast<int>(std::strlen("--manage-readonly-root=")));
      } else if (arg == QStringLiteral("--manage-default-paths")) {
        options.default_paths = true;
      } else if (arg == QStringLiteral("--manage-keep")) {
        options.keep_root = true;
      }
    }
    options.ui_smoke = ui_smoke;
    return lapian::app::RunOnlineCacheManageTest(options);
  }

  // T8.9：DASH 解析与档位探针（默认无 GUI；`--ui` 走下面的真实窗口分支）。
  // 只读 .secrets/--sessdata-file，不打印凭据/签名 URL。
  if (HasArgument(argc, argv, "--online-quality-test") &&
      !HasArgument(argc, argv, "--ui")) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("拉片软件"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("lapian.local"));
    QCoreApplication::setApplicationName(QStringLiteral("拉片软件"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LAPIAN_VERSION));

    lapian::app::OnlineQualityTestOptions options;
    const QStringList quality_args = QCoreApplication::arguments();
    for (int i = 1; i < quality_args.size(); ++i) {
      const QString& arg = quality_args[i];
      auto take = [&](QString* target) {
        if (i + 1 < quality_args.size()) {
          *target = quality_args[++i];
          return true;
        }
        return false;
      };
      if (arg == QStringLiteral("--bvid")) {
        take(&options.bvid);
      } else if (arg.startsWith(QStringLiteral("--bvid="))) {
        options.bvid = arg.mid(static_cast<int>(std::strlen("--bvid=")));
      } else if (arg == QStringLiteral("--cid")) {
        take(&options.cid_text);
      } else if (arg.startsWith(QStringLiteral("--cid="))) {
        options.cid_text = arg.mid(static_cast<int>(std::strlen("--cid=")));
      } else if (arg == QStringLiteral("--tag")) {
        take(&options.tag);
      } else if (arg.startsWith(QStringLiteral("--tag="))) {
        options.tag = arg.mid(static_cast<int>(std::strlen("--tag=")));
      } else if (arg == QStringLiteral("--sessdata-file")) {
        take(&options.sessdata_file);
      } else if (arg.startsWith(QStringLiteral("--sessdata-file="))) {
        options.sessdata_file =
            arg.mid(static_cast<int>(std::strlen("--sessdata-file=")));
      } else if (arg == QStringLiteral("--timeout-ms")) {
        QString text;
        if (take(&text)) {
          options.timeout_ms = text.toInt();
        }
      } else if (arg.startsWith(QStringLiteral("--timeout-ms="))) {
        options.timeout_ms =
            arg.mid(static_cast<int>(std::strlen("--timeout-ms="))).toInt();
      } else if (arg == QStringLiteral("--anonymous")) {
        options.anonymous = true;
      } else if (arg == QStringLiteral("--odd-qn")) {
        options.odd_qn = true;
      }
    }
    options.cid = options.cid_text.toLongLong();
    if (options.cid <= 0) {
      options.cid = 0;
    }
    if (options.timeout_ms < 3000) {
      options.timeout_ms = 3000;
    }
    if (options.timeout_ms > 120000) {
      options.timeout_ms = 120000;
    }
    if (options.tag.isEmpty()) {
      options.tag = options.bvid;
    }
    return lapian::app::RunOnlineQualityTest(options);
  }

  // T8.15：大会员可用档条件验收（O11，非 MVP 门）。无 GUI 的档位目录探针；
  // `--ui` 走下面的真实窗口分支。只读默认 DPAPI 凭据 / --vip-sessdata-file。
  if (HasArgument(argc, argv, "--online-vip-test") &&
      HasArgument(argc, argv, "--vip-catalog-only")) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("拉片软件"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("lapian.local"));
    QCoreApplication::setApplicationName(QStringLiteral("拉片软件"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LAPIAN_VERSION));

    lapian::app::OnlineVipTestOptions options;
    options.catalog_only = true;
    const QStringList vip_args = QCoreApplication::arguments();
    for (int i = 1; i < vip_args.size(); ++i) {
      const QString& arg = vip_args[i];
      auto take = [&](QString* target) {
        if (i + 1 < vip_args.size()) {
          *target = vip_args[++i];
          return true;
        }
        return false;
      };
      if (arg == QStringLiteral("--bvid")) {
        take(&options.bvid);
      } else if (arg.startsWith(QStringLiteral("--bvid="))) {
        options.bvid = arg.mid(static_cast<int>(std::strlen("--bvid=")));
      } else if (arg == QStringLiteral("--cid")) {
        take(&options.cid_text);
      } else if (arg.startsWith(QStringLiteral("--cid="))) {
        options.cid_text = arg.mid(static_cast<int>(std::strlen("--cid=")));
      } else if (arg == QStringLiteral("--tag")) {
        take(&options.tag);
      } else if (arg.startsWith(QStringLiteral("--tag="))) {
        options.tag = arg.mid(static_cast<int>(std::strlen("--tag=")));
      } else if (arg == QStringLiteral("--vip-sessdata-file")) {
        take(&options.sessdata_file);
      } else if (arg.startsWith(QStringLiteral("--vip-sessdata-file="))) {
        options.sessdata_file =
            arg.mid(static_cast<int>(std::strlen("--vip-sessdata-file=")));
      } else if (arg == QStringLiteral("--timeout-ms")) {
        QString text;
        if (take(&text)) {
          options.timeout_ms = text.toInt();
        }
      } else if (arg.startsWith(QStringLiteral("--timeout-ms="))) {
        options.timeout_ms =
            arg.mid(static_cast<int>(std::strlen("--timeout-ms="))).toInt();
      }
    }
    options.cid = options.cid_text.toLongLong();
    if (options.cid <= 0) {
      options.cid = 0;
    }
    if (options.timeout_ms < 3000) {
      options.timeout_ms = 3000;
    }
    if (options.timeout_ms > 120000) {
      options.timeout_ms = 120000;
    }
    if (options.tag.isEmpty()) {
      options.tag = options.bvid;
    }
    return lapian::app::RunOnlineVipCatalogTest(options);
  }

  // T8.4：可增长读层探针同样无 GUI、离线（本地 faststart 样本 + 临时缓存根）。
  if (HasArgument(argc, argv, "--online-growing-test")) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("拉片软件"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("lapian.local"));
    QCoreApplication::setApplicationName(QStringLiteral("拉片软件"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LAPIAN_VERSION));

    lapian::app::OnlineGrowingTestOptions options;
    const QStringList growing_args = QCoreApplication::arguments();
    for (int i = 1; i < growing_args.size(); ++i) {
      const QString& arg = growing_args[i];
      if (arg == QStringLiteral("--growing-media") &&
          i + 1 < growing_args.size()) {
        options.media = growing_args[++i];
      } else if (arg.startsWith(QStringLiteral("--growing-media="))) {
        options.media =
            arg.mid(static_cast<int>(std::strlen("--growing-media=")));
      } else if (arg == QStringLiteral("--minutes") &&
                 i + 1 < growing_args.size()) {
        options.minutes = growing_args[++i].toInt();
      } else if (arg.startsWith(QStringLiteral("--minutes="))) {
        options.minutes =
            arg.mid(static_cast<int>(std::strlen("--minutes="))).toInt();
      } else if (arg == QStringLiteral("--readers") &&
                 i + 1 < growing_args.size()) {
        options.readers = growing_args[++i].toInt();
      } else if (arg.startsWith(QStringLiteral("--readers="))) {
        options.readers =
            arg.mid(static_cast<int>(std::strlen("--readers="))).toInt();
      } else if (arg == QStringLiteral("--growing-root") &&
                 i + 1 < growing_args.size()) {
        options.root = growing_args[++i];
      } else if (arg.startsWith(QStringLiteral("--growing-root="))) {
        options.root =
            arg.mid(static_cast<int>(std::strlen("--growing-root=")));
      } else if (arg == QStringLiteral("--growing-keep")) {
        options.keep_root = true;
      }
    }
    return lapian::app::RunOnlineGrowingTest(options);
  }

  // T8.5：durl 取流探针；默认离线 fake transport，`--real` 才联网。
  if (HasArgument(argc, argv, "--online-fetch-test")) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("拉片软件"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("lapian.local"));
    QCoreApplication::setApplicationName(QStringLiteral("拉片软件"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LAPIAN_VERSION));

    lapian::app::OnlineFetchTestOptions options;
    const QStringList fetch_args = QCoreApplication::arguments();
    for (int i = 1; i < fetch_args.size(); ++i) {
      const QString& arg = fetch_args[i];
      auto take_value = [&](QString* target) {
        if (i + 1 < fetch_args.size()) {
          *target = fetch_args[++i];
          return true;
        }
        return false;
      };
      if (arg == QStringLiteral("--real")) {
        options.real = true;
      } else if (arg == QStringLiteral("--bvid")) {
        take_value(&options.bvid);
      } else if (arg.startsWith(QStringLiteral("--bvid="))) {
        options.bvid = arg.mid(static_cast<int>(std::strlen("--bvid=")));
      } else if (arg == QStringLiteral("--cid")) {
        take_value(&options.cid_text);
      } else if (arg.startsWith(QStringLiteral("--cid="))) {
        options.cid_text = arg.mid(static_cast<int>(std::strlen("--cid=")));
      } else if (arg == QStringLiteral("--tag")) {
        take_value(&options.tag);
      } else if (arg.startsWith(QStringLiteral("--tag="))) {
        options.tag = arg.mid(static_cast<int>(std::strlen("--tag=")));
      } else if (arg == QStringLiteral("--sessdata-file")) {
        take_value(&options.sessdata_file);
      } else if (arg.startsWith(QStringLiteral("--sessdata-file="))) {
        options.sessdata_file =
            arg.mid(static_cast<int>(std::strlen("--sessdata-file=")));
      } else if (arg == QStringLiteral("--fetch-media")) {
        take_value(&options.media);
      } else if (arg.startsWith(QStringLiteral("--fetch-media="))) {
        options.media = arg.mid(static_cast<int>(std::strlen("--fetch-media=")));
      } else if (arg == QStringLiteral("--fetch-root")) {
        take_value(&options.root);
      } else if (arg.startsWith(QStringLiteral("--fetch-root="))) {
        options.root = arg.mid(static_cast<int>(std::strlen("--fetch-root=")));
      } else if (arg == QStringLiteral("--fetch-keep")) {
        options.keep_root = true;
      } else if (arg == QStringLiteral("--exit-after-bytes")) {
        QString value;
        if (take_value(&value)) {
          options.exit_after_bytes = value.toLongLong();
        }
      } else if (arg.startsWith(QStringLiteral("--exit-after-bytes="))) {
        options.exit_after_bytes =
            arg.mid(static_cast<int>(std::strlen("--exit-after-bytes=")))
                .toLongLong();
      } else if (arg == QStringLiteral("--cancel-after-first-frame")) {
        options.cancel_after_first_frame = true;
      } else if (arg == QStringLiteral("--qn-1080")) {
        options.qn_1080 = true;
      } else if (arg == QStringLiteral("--qn")) {
        QString value;
        if (take_value(&value)) {
          options.qn = value.toInt();
        }
      } else if (arg.startsWith(QStringLiteral("--qn="))) {
        options.qn =
            arg.mid(static_cast<int>(std::strlen("--qn="))).toInt();
      } else if (arg == QStringLiteral("--fourk")) {
        QString value;
        if (take_value(&value)) {
          options.fourk = value.toInt();
        }
      } else if (arg.startsWith(QStringLiteral("--fourk="))) {
        options.fourk =
            arg.mid(static_cast<int>(std::strlen("--fourk="))).toInt();
      } else if (arg == QStringLiteral("--break-urls")) {
        take_value(&options.break_urls);
      } else if (arg.startsWith(QStringLiteral("--break-urls="))) {
        options.break_urls =
            arg.mid(static_cast<int>(std::strlen("--break-urls=")));
      } else if (arg == QStringLiteral("--timeout-ms")) {
        QString value;
        if (take_value(&value)) {
          options.timeout_ms = value.toInt();
        }
      } else if (arg.startsWith(QStringLiteral("--timeout-ms="))) {
        options.timeout_ms =
            arg.mid(static_cast<int>(std::strlen("--timeout-ms="))).toInt();
      }
    }
    bool cid_ok = false;
    options.cid = options.cid_text.toLongLong(&cid_ok);
    if (!cid_ok) {
      options.cid = 0;
    }
    if (options.timeout_ms < 1000) {
      options.timeout_ms = 1000;
    }
    if (options.timeout_ms > 120000) {
      options.timeout_ms = 120000;
    }
    return lapian::app::RunOnlineFetchTest(options);
  }

  // T8.2.2：扫码登录探针；默认离线 fake HTTP，`--real-generate` 只验证官方
  // generate/poll 字段（不扫码、不保存凭据），无 GUI。
  if (HasArgument(argc, argv, "--online-qr-test")) {
    const bool ui_smoke = HasArgument(argc, argv, "--ui");
    std::unique_ptr<QCoreApplication> app;
    if (ui_smoke) {
      app = std::make_unique<QApplication>(argc, argv);
    } else {
      app = std::make_unique<QCoreApplication>(argc, argv);
    }
    QCoreApplication::setOrganizationName(QStringLiteral("拉片软件"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("lapian.local"));
    QCoreApplication::setApplicationName(QStringLiteral("拉片软件"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LAPIAN_VERSION));
    if (ui_smoke) {
      // P2：扫码态截图必须体现 3D 之后的深色主题（与产品路径一致）。
      lapian::ui::ApplyTheme(static_cast<QApplication&>(*app));
    }

    lapian::app::OnlineQrTestOptions options;
    options.real_generate = HasArgument(argc, argv, "--real-generate");
    options.ui_smoke = ui_smoke;
    const QStringList qr_args = QCoreApplication::arguments();
    for (int i = 1; i < qr_args.size(); ++i) {
      if (qr_args[i] == QStringLiteral("--timeout-ms") &&
          i + 1 < qr_args.size()) {
        options.timeout_ms = qr_args[++i].toInt();
      } else if (qr_args[i].startsWith(QStringLiteral("--timeout-ms="))) {
        options.timeout_ms =
            qr_args[i]
                .mid(static_cast<int>(std::strlen("--timeout-ms=")))
                .toInt();
      }
    }
    if (options.timeout_ms < 1000) {
      options.timeout_ms = 1000;
    }
    if (options.timeout_ms > 30000) {
      options.timeout_ms = 30000;
    }
    return lapian::app::RunOnlineQrTest(options);
  }

  // T7.1：进程启动时间取在 QApplication 之前，P1/P2 从双击（进程创建）算起。
  const auto startup_process_start = std::chrono::steady_clock::now();
  QApplication app(argc, argv);
  const auto app_created_at = std::chrono::steady_clock::now();

  // T6.3 的设置会用到组织名/应用名，这里先固定下来
  QCoreApplication::setOrganizationName(QStringLiteral("拉片软件"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("lapian.local"));
  QCoreApplication::setApplicationName(QStringLiteral("拉片软件"));
  QCoreApplication::setApplicationVersion(QStringLiteral(LAPIAN_VERSION));

  // UI v2（3A）：开关在构造 MainWindow 之前**只读一次**；两套布局互斥构建，
  // 不做运行期切换（规划 §7.3/Q5）。环境变量 LAPIAN_UI_V2 或命令行 --ui-v2。
  auto env_flag_enabled = [](const char* name) {
    if (!qEnvironmentVariableIsSet(name)) {
      return false;
    }
    const QString value = qEnvironmentVariable(name).trimmed().toLower();
    return value == QStringLiteral("1") || value == QStringLiteral("true") ||
           value == QStringLiteral("on") || value == QStringLiteral("yes");
  };
  const bool ui_v2 =
      HasArgument(argc, argv, "--ui-v2") || env_flag_enabled("LAPIAN_UI_V2");
  // 3D：只剩一套壳层；LAPIAN_UI_V2/--ui-v2 仍解析但无效果（旧脚本不炸）。
  // 主题必须无条件应用（旧壳已删除，不存在"旧外观"回滚分支）。
  {
    QElapsedTimer theme_timer;
    theme_timer.start();
    lapian::ui::ApplyTheme(app);
    std::fprintf(stderr, "ui_v2 enabled=%d theme=applied theme_ms=%.2f\n",
                 ui_v2 ? 1 : 0,
                 static_cast<double>(theme_timer.nsecsElapsed()) / 1.0e6);
    std::fflush(stderr);
  }

  // T1.4：支持 --file <path> 与系统文件关联传入的第一个本地路径。
  QString file_path;
  QString file_b;
  QString goto_text;
  bool auto_play = false;
  bool step_test = false;
  bool ab_test = false;
  bool loop_test = false;
  // L1：--loop-test 只跑在线缓存命中 3 轮相位（需要 --bvid/--cid/--online-cache-root）。
  bool loop_online_cache_hit = false;
  // T7.2.1：B=末帧回归（A=last-20，B=last），确认不会停在 B。
  bool loop_end_test = false;
  bool mark_test = false;
  bool note_test = false;
  bool snapshot_test = false;
  bool fullscreen_test = false;
  bool key_test = false;
  // 3B：时间码内联输入（提交/取消/回滚/全局键让位）。
  bool readout_edit_test = false;
  bool settings_test = false;
  bool settings_expect = false;
  bool dpi_test = false;
  // 3D：`--dpi-test --empty` 跑空态相位（不需要 --file）。
  bool dpi_empty = false;
  bool ui_shots_test = false;
  bool banner_test = false;
  // T7.1：冷启动测量模式；有 --file 时等首帧真正 Present 后退出，否则等事件循环可交互。
  bool startup_test = false;
  // T7.1：后台索引路径回归（扫描中状态 → 就绪 → 导航/标记可用）。
  bool index_test = false;
  // T7.2：进程内存测量模式（empty/pause/play/step/switch）。
  bool memory_test = false;
  QString memory_mode = QStringLiteral("pause");
  QString memory_tag;
  int memory_seconds = 60;
  int memory_steps = 40;
  int memory_switch_cycles = 3;
  int memory_interval = 30;
  bool memory_soak_loop = true;
  int memory_loop_frames = 0;
  // T7.4：4K 路径探针（P4/P6/P7/P8/P9）与测试专用强制软解。
  bool drag_test = false;
  int drag_samples = 20;
  QString drag_tag;
  bool drag_fast = false;
  int drag_fast_step = 60;
  int drag_fast_cycles = 1;
  // 3B：时间轴几何（0 = 令牌默认；--timeline-height 340 用于 R6/R11 的第二组几何）。
  int timeline_height = 0;
  // DP-2：远跳粗帧探针。
  bool drag_far = false;
  // DP-3：慢拖/快拖/反向/远跳/松手策略矩阵探针。
  bool drag_strategy_test = false;
  bool force_software_decode = false;
  // DP-1：只强制本地拖动预览走软件解码，用于验证自动回退路径仍能出帧。
  bool preview_force_software = false;
  // T7.11：P7 跨 GOP 边界后退专项（目标为关键帧 → PgUp 到前一 GOP 末帧）。
  bool gop_boundary_test = false;
  int gop_boundary_samples = 20;
  QString gop_boundary_tag;
  // T7.5：错误与鲁棒性探针。
  bool robust_test = false;
  // T8.1：在线解析层探针（OB-1/2/3、OB-5、多P、取消/关窗）。
  bool online_resolve_test = false;
  // T8.2：产品路径登录自检探针（凭据 UI、DEC-3 时机、-101/超时/取消）。
  bool online_auth_test = false;
  bool online_m1_test = false;
  // T8.6.1a：在线长按 PgUp/PgDn 异步步进探针。
  bool online_longpress_test = false;
  int online_longpress_hold_ms = 2000;
  bool online_longpress_fresh_cache = false;
  // T8.6.1b：在线拖动预览探针（增长会话/缓存命中两轮）。
  bool online_drag_preview_test = false;
  // 2026-10-25：在线拖动前后一致 + 丝滑验收 / 本地 1080p 基线。
  bool online_drag_parity_test = false;
  bool local_drag_parity_test = false;
  int online_drag_seconds = 1;
  int online_drag_samples = 60;
  // C8（2026-10-26）：反向目标数。0 = 旧口径 min(30, samples)；>0 = 与正向
  // 同跨度、min(N, samples) 个目标（N == samples 才是同一目标率/跨度对测）。
  int online_drag_backward_samples = 0;
  qint64 online_drag_start_frame = 15;
  // C4：--drag-start-frame 是否显式给出（反向比例硬门只在 A 默认窗口生效）。
  bool online_drag_start_frame_explicit = false;
  bool online_drag_cache_hit = false;
  // C4 诊断：缓存命中打开后等待 N 毫秒再拖动（--drag-wait-ms）。
  int online_drag_wait_ms = 0;
  // D4（test-only）：拖动预览视觉证据目录（--preview-dump-dir）。
  QString preview_dump_dir_arg;
  // P3（2026-10-27，test-only）：长跨度门开关（--long-span-gate）。在进入主
  // else-if 链之前摘出，避免给已接近 MSVC C1061 上限的主链再加分支。
  bool online_drag_long_span_gate = false;
  // --drag-samples 是否显式给出；未给出时 P3 按 60×秒 自动给目标数（60Hz）。
  bool online_drag_samples_explicit = false;
  // --drag-backward-samples 是否显式给出；--long-span-gate 且未给出时自动置
  // “反向与正向同目标数/同跨度”（等价 --drag-backward-full），避免漏参数时
  // 反向只跑 30 个目标让 3 条 long_span 检查假失败。
  bool online_drag_backward_samples_explicit = false;
  // P0 热修：拖动松手后立刻按 Space（在线增长会话 20 轮；本地变体只作对照）。
  bool scrub_space_test = false;
  // P1 热修：真实在线会话中保存缓存目录（两阶段），不中断播放。
  bool cache_switch_test = false;
  QString online_m1_bvid;
  QString online_m1_cid_text;
  QString online_m1_tag;
  QString online_m1_pictures;
  QString online_m1_cache_root;
  int online_m1_page = 1;
  qint64 online_m1_cid = 0;
  int online_m1_timeout_ms = 20000;
  bool online_m1_cancel_only = false;
  bool online_m1_uncached_landing = false;
  qint64 online_m1_throttle_bps = 0;
  // T8.7：完整缓存命中/断网/在线记录 E2E。
  bool online_offline_test = false;
  QString online_offline_media;
  QString online_records_root;
  bool online_offline_keep_cache = false;
  bool online_offline_skip_corrupt = false;
  bool online_offline_default_paths = false;
  bool online_buffer_test = false;
  QString online_buffer_media;
  int online_buffer_timeout_ms = 20000;
  // T8.9：DASH 解析/档位探针（默认无 GUI；`--ui` 走真实窗口/档位菜单）。
  bool online_quality_test = false;
  bool online_quality_ui = false;
  bool online_quality_anonymous = false;
  bool online_quality_odd_qn = false;
  bool online_quality_no_catalog = false;
  QString online_quality_sessdata_file;
  // T8.17：默认 1080P → 大会员 1080P+ 高码率 → 切回 1080P30。
  bool online_quality_switch_test = false;
  int online_quality_switch_high_qn = 112;
  int online_quality_switch_back_qn = 80;
  int online_quality_switch_wait_ms = 120000;
  bool online_quality_switch_cache_hit_menu = false;
  // T8.15：大会员可用档条件验收（O11，非门）。
  bool online_vip_test = false;
  int online_vip_qn = 0;
  int online_vip_codecid = 0;
  int online_vip_expect_auto = -1;
  int online_vip_play_seconds = 10;
  int online_vip_seeks = 20;
  int online_vip_switch_qn = 80;
  QString online_vip_sessdata_file;
  // T8.10：DASH 双输入验收（真实窗口）。
  bool online_m2_test = false;
  bool online_m2_force_durl = false;
  bool online_m2_cache_hit_only = false;
  bool online_m2_skip_cache_hit = false;
  bool online_m2_op4 = false;
  bool online_m2_op5 = false;
  bool online_m2_default_paths = false;
  int online_m2_requested_qn = 0;
  int online_m2_play_seconds = 60;
  int online_m2_seeks = 20;
  // T8.11：渐进索引/波形/总帧数（真实 OB-3 + 受控慢增长离线 case）。
  bool online_index_test = false;
  qint64 online_index_throttle_bps = 0;
  int online_index_seeks = 20;
  QString online_index_offline_video;
  QString online_index_offline_audio;
  // T8.13：网络异常/并发加固（真实窗口，全部注入，不联网）。
  bool online_fault_test = false;
  QString online_fault_media;
  QString online_fault_only;
  QString online_fault_set = QStringLiteral("t8.13-core");
  QString robust_media;
  QString robust_tag;
  QString robust_root;
  QString robust_only;
  QString robust_readonly_dir;
  QString settings_path_arg;
  QString dpi_tag;
  QString banner_tag;
  QString banner_vfr;
  QString banner_tenbit;
  QString banner_noaudio;
  int settings_expect_volume = -1;
  bool settings_expect_waveform = false;
  double dpi_expect_scale = -1.0;
  QString snapshot_pictures_arg;
  double playback_rate = 1.0;
  bool has_playback_rate = false;
  QStringList arguments =
      ExtractUiV2Args(QCoreApplication::arguments(), &timeline_height,
                      &readout_edit_test, &dpi_empty, &ui_shots_test);
  // C8（2026-10-26，test-only）：反向目标数在进入主参数链前摘出，避免再给
  // 这条 else-if 链加分支（MSVC C1061 块嵌套上限，主链已接近上限）。
  for (int i = 1; i < arguments.size();) {
    if (arguments[i] == QStringLiteral("--drag-backward-samples")) {
      if (i + 1 < arguments.size()) {
        online_drag_backward_samples = arguments[i + 1].toInt();
        online_drag_backward_samples_explicit = true;
        arguments.removeAt(i + 1);
      }
      arguments.removeAt(i);
    } else if (arguments[i] == QStringLiteral("--drag-backward-full")) {
      online_drag_backward_samples = -1;
      arguments.removeAt(i);
    } else if (arguments[i] == QStringLiteral("--preview-max-side") &&
               i + 1 < arguments.size()) {
      // D2（test-only）：在线预览配置覆盖，由 MainWindow 在会话就绪时读取；
      // 不走产品设置页，也不影响本地 PreviewDecoder。
      qputenv("LAPIAN_PREVIEW_MAX_SIDE", arguments[i + 1].toUtf8());
      arguments.removeAt(i + 1);
      arguments.removeAt(i);
    } else if (arguments[i] == QStringLiteral("--preview-cache-mb") &&
               i + 1 < arguments.size()) {
      qputenv("LAPIAN_PREVIEW_CACHE_MB", arguments[i + 1].toUtf8());
      arguments.removeAt(i + 1);
      arguments.removeAt(i);
    } else if (arguments[i] == QStringLiteral("--preview-resolution") &&
               i + 1 < arguments.size()) {
      qputenv("LAPIAN_PREVIEW_RESOLUTION", arguments[i + 1].toUtf8());
      arguments.removeAt(i + 1);
      arguments.removeAt(i);
    } else if (arguments[i] == QStringLiteral("--preview-dump-dir") &&
               i + 1 < arguments.size()) {
      // D4（test-only）：视觉证据 dump 目录；由拖动预览探针在正向拖动结束后
      // 把最近的已缓存帧写成 .nv12，不影响产品截图/精确帧路径。
      preview_dump_dir_arg = arguments[i + 1];
      arguments.removeAt(i + 1);
      arguments.removeAt(i);
    } else if (arguments[i] == QStringLiteral("--long-span-gate")) {
      // P3（test-only）：长跨度门；不进主 else-if 链（C1061）。
      online_drag_long_span_gate = true;
      arguments.removeAt(i);
    } else {
      ++i;
    }
  }
  for (int i = 1; i < arguments.size(); ++i) {
    if (arguments[i] == QStringLiteral("--file") && i + 1 < arguments.size()) {
      file_path = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--file-b") && i + 1 < arguments.size()) {
      file_b = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--goto") && i + 1 < arguments.size()) {
      goto_text = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--rate") && i + 1 < arguments.size()) {
      playback_rate = arguments[++i].toDouble();
      has_playback_rate = true;
    } else if (arguments[i] == QStringLiteral("--play")) {
      auto_play = true;
    } else if (arguments[i] == QStringLiteral("--step-test")) {
      step_test = true;
    } else if (arguments[i] == QStringLiteral("--ab-test")) {
      ab_test = true;
    } else if (arguments[i] == QStringLiteral("--loop-test")) {
      loop_test = true;
    } else if (arguments[i] == QStringLiteral("--loop-end-test")) {
      loop_end_test = true;
    } else if (arguments[i] == QStringLiteral("--mark-test")) {
      mark_test = true;
    } else if (arguments[i] == QStringLiteral("--note-test")) {
      note_test = true;
    } else if (arguments[i] == QStringLiteral("--snapshot-test")) {
      snapshot_test = true;
    } else if (arguments[i] == QStringLiteral("--fullscreen-test")) {
      fullscreen_test = true;
    } else if (arguments[i] == QStringLiteral("--key-test")) {
      key_test = true;
    } else if (arguments[i] == QStringLiteral("--settings-test")) {
      settings_test = true;
    } else if (arguments[i] == QStringLiteral("--settings-expect") &&
               i + 2 < arguments.size()) {
      settings_expect = true;
      settings_expect_volume = arguments[++i].toInt();
      settings_expect_waveform = arguments[++i].toInt() != 0;
    } else if (arguments[i] == QStringLiteral("--settings-path") &&
               i + 1 < arguments.size()) {
      settings_path_arg = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--dpi-test")) {
      dpi_test = true;
    } else if (arguments[i] == QStringLiteral("--banner-test")) {
      banner_test = true;
    } else if (arguments[i] == QStringLiteral("--startup-test")) {
      startup_test = true;
    } else if (arguments[i] == QStringLiteral("--index-test")) {
      index_test = true;
    } else if (arguments[i] == QStringLiteral("--memory-test")) {
      memory_test = true;
    } else if (arguments[i] == QStringLiteral("--memory-mode") &&
               i + 1 < arguments.size()) {
      memory_mode = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--memory-seconds") &&
               i + 1 < arguments.size()) {
      memory_seconds = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--memory-steps") &&
               i + 1 < arguments.size()) {
      memory_steps = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--memory-switch-cycles") &&
               i + 1 < arguments.size()) {
      memory_switch_cycles = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--memory-interval") &&
               i + 1 < arguments.size()) {
      memory_interval = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--memory-no-loop")) {
      memory_soak_loop = false;
    } else if (arguments[i] == QStringLiteral("--memory-loop-frames") &&
               i + 1 < arguments.size()) {
      memory_loop_frames = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--memory-tag") &&
               i + 1 < arguments.size()) {
      memory_tag = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--drag-test")) {
      drag_test = true;
    } else if (arguments[i] == QStringLiteral("--drag-fast")) {
      drag_test = true;
      drag_fast = true;
    } else if (arguments[i] == QStringLiteral("--drag-far-test")) {
      drag_test = true;
      drag_far = true;
    } else if (arguments[i] == QStringLiteral("--drag-strategy-test")) {
      drag_strategy_test = true;
    } else if (arguments[i] == QStringLiteral("--drag-fast-step") &&
               i + 1 < arguments.size()) {
      drag_fast_step = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--drag-fast-cycles") &&
               i + 1 < arguments.size()) {
      drag_fast_cycles = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--drag-samples") &&
               i + 1 < arguments.size()) {
      drag_samples = arguments[++i].toInt();
      online_drag_samples = drag_samples;
      online_drag_samples_explicit = true;
    } else if (arguments[i] == QStringLiteral("--drag-tag") &&
               i + 1 < arguments.size()) {
      drag_tag = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--force-software-decode")) {
      force_software_decode = true;
    } else if (arguments[i] == QStringLiteral("--preview-force-software")) {
      preview_force_software = true;
    } else if (arguments[i] == QStringLiteral("--gop-boundary-test")) {
      gop_boundary_test = true;
    } else if (arguments[i] == QStringLiteral("--gop-boundary-samples") &&
               i + 1 < arguments.size()) {
      gop_boundary_samples = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--gop-boundary-tag") &&
               i + 1 < arguments.size()) {
      gop_boundary_tag = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--robust-test")) {
      robust_test = true;
    } else if (arguments[i] == QStringLiteral("--online-resolve-test")) {
      online_resolve_test = true;
    } else if (arguments[i] == QStringLiteral("--online-auth-test")) {
      online_auth_test = true;
    } else if (arguments[i] == QStringLiteral("--online-m1-test")) {
      online_m1_test = true;
    } else if (arguments[i] == QStringLiteral("--online-longpress-test")) {
      online_longpress_test = true;
    } else if (arguments[i] == QStringLiteral("--online-drag-preview-test")) {
      online_drag_preview_test = true;
    } else if (arguments[i] == QStringLiteral("--online-drag-parity-test")) {
      online_drag_preview_test = true;
      online_drag_parity_test = true;
    } else if (arguments[i] == QStringLiteral("--local-drag-parity-test")) {
      local_drag_parity_test = true;
    } else if (arguments[i] == QStringLiteral("--drag-seconds") &&
               i + 1 < arguments.size()) {
      online_drag_seconds = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--drag-start-frame") &&
               i + 1 < arguments.size()) {
      online_drag_start_frame = arguments[++i].toLongLong();
      online_drag_start_frame_explicit = true;
    } else if (arguments[i] == QStringLiteral("--drag-cache-hit")) {
      online_drag_cache_hit = true;
    } else if (arguments[i] == QStringLiteral("--drag-throttle-bps") &&
               i + 1 < arguments.size()) {
      online_m1_throttle_bps = arguments[++i].toLongLong();
    } else if (arguments[i] == QStringLiteral("--longpress-hold-ms") &&
               i + 1 < arguments.size()) {
      online_longpress_hold_ms = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--longpress-fresh-cache")) {
      online_longpress_fresh_cache = true;
    } else if (arguments[i] == QStringLiteral("--cancel-only")) {
      online_m1_cancel_only = true;
    } else if (arguments[i] == QStringLiteral("--uncached-landing")) {
      online_m1_uncached_landing = true;
    } else if (arguments[i] == QStringLiteral("--m1-throttle-bps") &&
               i + 1 < arguments.size()) {
      online_m1_throttle_bps = arguments[++i].toLongLong();
    } else if (arguments[i] == QStringLiteral("--online-buffer-test")) {
      online_buffer_test = true;
    } else if (arguments[i] == QStringLiteral("--online-quality-test")) {
      online_quality_test = true;
    } else if (arguments[i] == QStringLiteral("--online-m2-test")) {
      online_m2_test = true;
    } else if (arguments[i] == QStringLiteral("--force-durl")) {
      online_m2_force_durl = true;
    } else if (arguments[i] == QStringLiteral("--m2-cache-hit-only")) {
      online_m2_cache_hit_only = true;
    } else if (arguments[i] == QStringLiteral("--m2-skip-cache-hit")) {
      online_m2_skip_cache_hit = true;
    } else if (arguments[i] == QStringLiteral("--m2-op4")) {
      online_m2_op4 = true;
    } else if (arguments[i] == QStringLiteral("--m2-op5")) {
      online_m2_op5 = true;
    } else if (arguments[i] == QStringLiteral("--m2-default-paths")) {
      online_m2_default_paths = true;
    } else if (arguments[i] == QStringLiteral("--m2-qn") &&
               i + 1 < arguments.size()) {
      online_m2_requested_qn = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--m2-play-seconds") &&
               i + 1 < arguments.size()) {
      online_m2_play_seconds = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--m2-seeks") &&
               i + 1 < arguments.size()) {
      online_m2_seeks = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--online-index-test")) {
      online_index_test = true;
    } else if (arguments[i] == QStringLiteral("--online-vip-test")) {
      online_vip_test = true;
    } else if (arguments[i] == QStringLiteral("--vip-qn") &&
               i + 1 < arguments.size()) {
      online_vip_qn = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--vip-codecid") &&
               i + 1 < arguments.size()) {
      online_vip_codecid = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--vip-expect-auto") &&
               i + 1 < arguments.size()) {
      online_vip_expect_auto = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--vip-play-seconds") &&
               i + 1 < arguments.size()) {
      online_vip_play_seconds = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--vip-seeks") &&
               i + 1 < arguments.size()) {
      online_vip_seeks = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--vip-switch-qn") &&
               i + 1 < arguments.size()) {
      online_vip_switch_qn = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--vip-sessdata-file") &&
               i + 1 < arguments.size()) {
      online_vip_sessdata_file = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--online-fault-test")) {
      online_fault_test = true;
    } else if (arguments[i] == QStringLiteral("--fault-media") &&
               i + 1 < arguments.size()) {
      online_fault_media = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--fault-only") &&
               i + 1 < arguments.size()) {
      online_fault_only = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--fault-set") &&
               i + 1 < arguments.size()) {
      online_fault_set = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--index-throttle-bps") &&
               i + 1 < arguments.size()) {
      online_index_throttle_bps = arguments[++i].toLongLong();
    } else if (arguments[i] == QStringLiteral("--index-seeks") &&
               i + 1 < arguments.size()) {
      online_index_seeks = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--index-offline-video") &&
               i + 1 < arguments.size()) {
      online_index_offline_video = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--index-offline-audio") &&
               i + 1 < arguments.size()) {
      online_index_offline_audio = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--ui")) {
      online_quality_ui = true;
    } else if (arguments[i] == QStringLiteral("--anonymous")) {
      online_quality_anonymous = true;
    } else if (arguments[i] == QStringLiteral("--odd-qn")) {
      online_quality_odd_qn = true;
    } else if (arguments[i] == QStringLiteral("--no-quality-catalog")) {
      // T8.9 诊断：关闭首帧后的 fnval=4048 档位目录请求（A/B 回归用）。
      online_quality_no_catalog = true;
    } else if (arguments[i] == QStringLiteral("--sessdata-file") &&
               i + 1 < arguments.size()) {
      online_quality_sessdata_file = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--online-offline-test")) {
      online_offline_test = true;
    } else if (arguments[i] == QStringLiteral("--offline-media") &&
               i + 1 < arguments.size()) {
      online_offline_media = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--online-records-root") &&
               i + 1 < arguments.size()) {
      online_records_root = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--offline-keep-cache")) {
      online_offline_keep_cache = true;
    } else if (arguments[i] == QStringLiteral("--offline-skip-corrupt")) {
      online_offline_skip_corrupt = true;
    } else if (arguments[i] == QStringLiteral("--offline-default-paths")) {
      online_offline_default_paths = true;
    } else if (arguments[i] == QStringLiteral("--buffer-media") &&
               i + 1 < arguments.size()) {
      online_buffer_media = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--bvid") &&
               i + 1 < arguments.size()) {
      online_m1_bvid = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--cid") &&
               i + 1 < arguments.size()) {
      online_m1_cid_text = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--page") &&
               i + 1 < arguments.size()) {
      online_m1_page = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--tag") &&
               i + 1 < arguments.size()) {
      online_m1_tag = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--online-pictures") &&
               i + 1 < arguments.size()) {
      online_m1_pictures = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--online-cache-root") &&
               i + 1 < arguments.size()) {
      online_m1_cache_root = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--timeout-ms") &&
               i + 1 < arguments.size()) {
      online_m1_timeout_ms = arguments[++i].toInt();
      online_buffer_timeout_ms = online_m1_timeout_ms;
    } else if (arguments[i] == QStringLiteral("--robust-media") &&
               i + 1 < arguments.size()) {
      robust_media = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--robust-tag") &&
               i + 1 < arguments.size()) {
      robust_tag = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--robust-root") &&
               i + 1 < arguments.size()) {
      robust_root = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--robust-only") &&
               i + 1 < arguments.size()) {
      robust_only = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--robust-readonly-dir") &&
               i + 1 < arguments.size()) {
      robust_readonly_dir = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--banner-tag") &&
               i + 1 < arguments.size()) {
      banner_tag = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--banner-vfr") &&
               i + 1 < arguments.size()) {
      banner_vfr = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--banner-tenbit") &&
               i + 1 < arguments.size()) {
      banner_tenbit = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--banner-noaudio") &&
               i + 1 < arguments.size()) {
      banner_noaudio = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--dpi-expect-scale") &&
               i + 1 < arguments.size()) {
      dpi_expect_scale = arguments[++i].toDouble();
    } else if (arguments[i] == QStringLiteral("--dpi-tag") &&
               i + 1 < arguments.size()) {
      dpi_tag = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--snapshot-pictures") &&
               i + 1 < arguments.size()) {
      snapshot_pictures_arg = arguments[++i];
    } else if (!arguments[i].startsWith(QLatin1Char('-')) && file_path.isEmpty()) {
      file_path = arguments[i];
    }
    // T8.17：单独解析，避免把主参数链再加深一层（MSVC C1061 嵌套上限）。
    if (arguments[i] == QStringLiteral("--online-quality-switch-test")) {
      online_quality_switch_test = true;
    } else if (arguments[i] == QStringLiteral("--quality-switch-high") &&
               i + 1 < arguments.size()) {
      online_quality_switch_high_qn = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--quality-switch-back") &&
               i + 1 < arguments.size()) {
      online_quality_switch_back_qn = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--quality-switch-wait-ms") &&
               i + 1 < arguments.size()) {
      online_quality_switch_wait_ms = arguments[++i].toInt();
    } else if (arguments[i] == QStringLiteral("--quality-cache-hit-menu")) {
      // B：缓存命中打开 → 点开清晰度菜单按需拉目录 → 选非当前档 → 切回缓存档。
      online_quality_switch_cache_hit_menu = true;
    } else if (arguments[i] == QStringLiteral("--drag-wait-ms") &&
               i + 1 < arguments.size()) {
      // C4 诊断：缓存命中打开后先等 N 毫秒再开始拖动（0 = 不等待）。
      online_drag_wait_ms = arguments[++i].toInt();
    }
  }

  // P0：单独扫描（不塞进上面的 if/else-if 长链，MSVC 的块嵌套有上限）。
  // 在线变体必须带 --bvid/--cid；只给 --file 时走本地对照路径。
  if (arguments.contains(QStringLiteral("--scrub-space-test"))) {
    scrub_space_test = true;
  }
  if (arguments.contains(QStringLiteral("--cache-switch-online-test"))) {
    cache_switch_test = true;
  }
  if (arguments.contains(QStringLiteral("--loop-online-cache-hit"))) {
    loop_online_cache_hit = true;
  }
  if ((scrub_space_test || cache_switch_test) && !online_m1_bvid.isEmpty()) {
    online_drag_preview_test = true;
  }
  if (loop_online_cache_hit) {
    bool cid_ok = false;
    online_m1_cid = online_m1_cid_text.toLongLong(&cid_ok);
    if (!cid_ok) {
      online_m1_cid = 0;
    }
    if (online_m1_bvid.isEmpty() || online_m1_cid <= 0) {
      std::fprintf(stderr,
                   "loop_online_fail reason=missing_bvid_cid usage=--loop-test "
                   "--loop-online-cache-hit --bvid BV... --cid 123 "
                   "--online-cache-root <dir>\n");
      std::fflush(stderr);
      return 2;
    }
    qputenv("LAPIAN_ONLINE_LOG", "1");
  }

  if (online_m1_test || online_offline_test || online_longpress_test ||
      online_drag_preview_test || online_drag_parity_test ||
      online_quality_test || online_quality_switch_test || online_m2_test ||
      online_vip_test ||
      (online_index_test && online_index_offline_video.isEmpty())) {
    if (online_quality_test && !online_quality_ui) {
      // 无 GUI 的 T8.9 探针在上面已分流；走到这里说明参数组合不成立。
      std::fprintf(stderr, "online_quality_fail reason=ui_flag_required\n");
      std::fflush(stderr);
      return 2;
    }
    bool cid_ok = false;
    online_m1_cid = online_m1_cid_text.toLongLong(&cid_ok);
    if (!cid_ok) {
      online_m1_cid = 0;
    }
    if (online_m1_page < 1) {
      online_m1_page = 1;
    }
    if (online_m1_timeout_ms < 3000) {
      online_m1_timeout_ms = 3000;
    }
    // P3（test-only）：长跨度格（20s/40s）单次等待上限放到 300s；只放宽拖动
    // 预览探针的等待，其它在线探针仍按 120s 收口。
    const int online_timeout_cap =
        (online_drag_preview_test || online_drag_parity_test) ? 300000 : 120000;
    if (online_m1_timeout_ms > online_timeout_cap) {
      online_m1_timeout_ms = online_timeout_cap;
    }
    if (online_m1_tag.isEmpty()) {
      online_m1_tag = online_m1_bvid;
    }
    if (online_vip_test) {
      // T8.15：cid 可省略，探针用 view 的 P1 cid；bvid 必填。
      if (online_m1_bvid.isEmpty()) {
        std::fprintf(stderr,
                     "online_vip_fail reason=missing_bvid usage=--online-vip-test "
                     "--ui --bvid BV... [--cid 123]\n");
        std::fflush(stderr);
        return 2;
      }
      if (online_vip_qn < 0) {
        online_vip_qn = 0;
      }
      if (online_vip_codecid < 0 || online_vip_codecid > 13) {
        online_vip_codecid = 0;
      }
      if (online_vip_play_seconds < 1) {
        online_vip_play_seconds = 1;
      }
      if (online_vip_play_seconds > 120) {
        online_vip_play_seconds = 120;
      }
      if (online_vip_seeks < 0) {
        online_vip_seeks = 0;
      }
      if (online_vip_seeks > 100) {
        online_vip_seeks = 100;
      }
    } else if (online_m1_bvid.isEmpty() || online_m1_cid <= 0) {
      const char* name = online_offline_test ? "offline"
                          : online_longpress_test ? "longpress"
                          : online_drag_preview_test ? "drag_preview"
                          : online_quality_test ? "quality"
                          : online_m2_test ? "m2"
                          : online_index_test ? "index"
                                              : "m1";
      std::fprintf(stderr,
                   "online_%s_fail reason=missing_bvid_cid usage=--online-%s-test "
                   "--bvid BV... --cid 123\n",
                   name, name);
      std::fflush(stderr);
      return 2;
    }
    if (online_longpress_hold_ms < 500) {
      online_longpress_hold_ms = 500;
    }
    if (online_longpress_hold_ms > 10000) {
      online_longpress_hold_ms = 10000;
    }
    if (online_drag_seconds < 1) {
      online_drag_seconds = 1;
    }
    if (online_drag_seconds > 60) {
      // P3（test-only）：长跨度格最长 40s（60×S 帧、60×S 个目标）；上限留 60s。
      online_drag_seconds = 60;
    }
    if (!online_drag_samples_explicit) {
      // P3：探针口径 = 60S 个目标、跨度 60S 帧、60Hz；30fps 素材下 S 秒目标
      // 等于 2S 秒内容（fps×2×S 帧）。1s 仍是 60（与旧口径完全一致）。
      online_drag_samples = online_drag_seconds * 60;
    }
    if (online_drag_samples < 10) {
      online_drag_samples = 10;
    }
    if (online_drag_samples > 3600) {
      online_drag_samples = 3600;
    }
    if (online_drag_long_span_gate &&
        !online_drag_backward_samples_explicit) {
      // P3 收尾（test-only）：长跨度门要求“反向与正向同目标数 + 同跨度”。
      // 未显式给 --drag-backward-samples 时自动置等价 --drag-backward-full；
      // 显式给了不同值则尊重用户，由 long_span_same_target_rate 判 fail。
      online_drag_backward_samples = -1;
    }
    if (online_drag_backward_samples < 0) {
      // --drag-backward-full / 负值：反向与正向同目标数（同跨度对测）。
      online_drag_backward_samples = online_drag_samples;
    }
    if (online_drag_backward_samples > online_drag_samples) {
      online_drag_backward_samples = online_drag_samples;
    }
    if (online_drag_start_frame < 0) {
      online_drag_start_frame = 0;
    }
    qputenv("LAPIAN_ONLINE_LOG", "1");
    qputenv("LAPIAN_SNAPSHOT_LOG", "1");
  }
  if (loop_test) {
    qputenv("LAPIAN_LOOP_LOG", "1");
  }
  // T7.4：只影响测试路径，产品默认仍是硬解优先 + 失败回退软解。
  if (force_software_decode) {
    qputenv("LAPIAN_FORCE_SOFTWARE", "1");
    std::fprintf(stderr, "T7.4 decode_mode forced=software\n");
    std::fflush(stderr);
  }
  // DP-1：只强制预览软解（主播放仍按默认硬解），验证回退路径仍能出帧。
  if (preview_force_software) {
    qputenv("LAPIAN_PREVIEW_FORCE_SOFTWARE", "1");
    std::fprintf(stderr, "DP-1 preview_decode_mode forced=software\n");
    std::fflush(stderr);
  }
  if (mark_test) {
    qputenv("LAPIAN_MARK_LOG", "1");
  }
  if (note_test) {
    qputenv("LAPIAN_MARK_LOG", "1");
  }
  // T7.11：跨 GOP 边界专项需要 seek_log 证明每个 PgUp 都是从上一个关键帧
  // 解完整 GOP（hit=0 decoded=60），而不是命中缓存或 handoff。
  if (gop_boundary_test) {
    qputenv("LAPIAN_SEEK_LOG", "1");
  }
  // T6.3：--settings-test 用独立根，默认 build/t6.3-settings，避免碰用户配置；
  // --settings-path 可指向 %APPDATA%\拉片软件 做真实默认路径验证。
  // T6.5：--banner-test 同样用独立根，保证波形开/音量默认，布局可复现。
  QString settings_root = settings_path_arg;
  if ((settings_test || settings_expect || banner_test || online_m1_test ||
       online_longpress_test || online_drag_preview_test || online_buffer_test ||
       online_offline_test || online_quality_test ||
       online_quality_switch_test || online_vip_test ||
       online_fault_test) &&
      settings_root.isEmpty()) {
    QString settings_name = QStringLiteral("/build/t8.6-settings");
    if (settings_test || settings_expect) {
      settings_name = QStringLiteral("/build/t6.3-settings");
    } else if (banner_test) {
      settings_name = QStringLiteral("/build/t6.5-settings");
    } else if (online_quality_test) {
      settings_name = QStringLiteral("/build/t8.9-settings");
    } else if (online_quality_switch_test) {
      settings_name = QStringLiteral("/build/t8.17-settings");
    } else if (online_vip_test) {
      settings_name = QStringLiteral("/build/t8.15-settings");
    } else if (online_fault_test) {
      settings_name = QStringLiteral("/build/t8.13-fault/settings");
    } else if (online_offline_test) {
      settings_name = QStringLiteral("/build/t8.7-settings");
    } else if (online_longpress_test) {
      settings_name = QStringLiteral("/build/t8.6.1a-settings");
    } else if (online_drag_preview_test) {
      settings_name = QStringLiteral("/build/t8.6.1b-settings");
    }
    settings_root = QDir::currentPath() + settings_name;
  }
  if (settings_test || settings_expect) {
    qputenv("LAPIAN_SETTINGS_LOG", "1");
  }
  if (online_fault_test && !settings_root.isEmpty()) {
    QDir().mkpath(settings_root);
  }
  if (settings_test || (banner_test && settings_path_arg.isEmpty())) {
    QDir().mkpath(settings_root);
    const QString settings_file =
        QDir(settings_root).filePath(QStringLiteral("settings.json"));
    if (QFileInfo(settings_file).isDir()) {
      QDir(settings_file).removeRecursively();
    } else {
      QFile::remove(settings_file);
    }
  }
  if (banner_test) {
    // 真实素材优先；缺文件时测试按 skip 处理，不阻塞其他检查。
    const QString media_dir = QDir::currentPath() + QStringLiteral("/build/test-media");
    if (banner_vfr.isEmpty()) {
      banner_vfr = media_dir + QStringLiteral("/vfr.mp4");
    }
    if (banner_tenbit.isEmpty()) {
      banner_tenbit = media_dir + QStringLiteral("/10bit.mp4");
    }
    if (banner_noaudio.isEmpty()) {
      banner_noaudio = media_dir + QStringLiteral("/noaudio.mp4");
    }
    if (file_b.isEmpty()) {
      const QString v4 = QDir::currentPath() + QStringLiteral("/media/proxy/proxy-V4.mp4");
      if (QFileInfo::exists(v4)) {
        file_b = v4;
      }
    }
  }
  QString snapshot_pictures_root;
  if (snapshot_test || fullscreen_test) {
    qputenv("LAPIAN_SNAPSHOT_LOG", "1");
    const QString default_root =
        QDir::currentPath() +
        (snapshot_test ? QStringLiteral("/build/t5.4-snapshot-pictures")
                       : QStringLiteral("/build/t6.1-fullscreen-pictures"));
    snapshot_pictures_root =
        snapshot_pictures_arg.isEmpty() ? default_root : snapshot_pictures_arg;
    snapshot_pictures_root = QFileInfo(snapshot_pictures_root).absoluteFilePath();
    QDir(snapshot_pictures_root).removeRecursively();
    QDir().mkpath(snapshot_pictures_root);
  }

  // T7.5：错误与鲁棒性探针的独立根。不覆盖 APPDATA，改用 MainWindow 的
  // 测试 setter 注入 MarkStore 回退根，避免污染真实用户目录。
  if (robust_test) {
    if (robust_tag.isEmpty()) {
      robust_tag = QStringLiteral("default");
    }
    if (robust_root.isEmpty()) {
      robust_root = QDir::currentPath() + QStringLiteral("/build/t7.5-robust/") +
                    robust_tag;
    }
    robust_root = QFileInfo(robust_root).absoluteFilePath();
    QDir(robust_root).removeRecursively();
    QDir().mkpath(robust_root);
    QDir().mkpath(robust_root + QStringLiteral("/appdata"));
    QDir().mkpath(robust_root + QStringLiteral("/pictures"));
    if (robust_media.isEmpty()) {
      const QString proxy =
          QDir::currentPath() + QStringLiteral("/media/proxy/proxy-V1.mp4");
      const QString v9 = QDir::currentPath() + QStringLiteral("/media/V9.mp4");
      if (QFileInfo::exists(proxy)) {
        robust_media = proxy;
      } else if (QFileInfo::exists(v9)) {
        robust_media = v9;
      } else {
        robust_media = file_path;
      }
    }
    if (robust_media.isEmpty()) {
      std::fprintf(stderr, "T7.5 robust_fail reason=missing_media\n");
      return 2;
    }
    if (robust_only.isEmpty()) {
      robust_only = QStringLiteral("all");
    }
    qputenv("LAPIAN_OPEN_LOG", "1");
    qputenv("LAPIAN_MARK_LOG", "1");
  }

  // T8.1：在线解析探针打开请求日志；产品默认不打印请求细节。
  if (online_resolve_test) {
    qputenv("LAPIAN_ONLINE_LOG", "1");
  }

  // T7.1：冷启动埋点。--startup-test 自动开日志；单独设置 LAPIAN_STARTUP_LOG=1
  // 也能在不退出程序的情况下观察分段耗时。
  if (startup_test) {
    qputenv("LAPIAN_STARTUP_LOG", "1");
  }
  const bool startup_log = startup_test || qEnvironmentVariableIsSet("LAPIAN_STARTUP_LOG");
  auto startup_ms = [startup_process_start](std::chrono::steady_clock::time_point at) {
    return std::chrono::duration<double, std::milli>(at - startup_process_start).count();
  };
  auto log_startup_at = [startup_log, startup_ms](
                            const char* stage,
                            std::chrono::steady_clock::time_point at) {
    if (!startup_log) {
      return;
    }
    std::fprintf(stderr, "T7.1 startup stage=%s t_ms=%.2f\n", stage, startup_ms(at));
    std::fflush(stderr);
  };
  auto log_startup = [log_startup_at](const char* stage) {
    log_startup_at(stage, std::chrono::steady_clock::now());
  };
  log_startup_at("app_created", app_created_at);

  // T8.2：产品路径登录探针用独立 APPDATA，避免读到本机真实凭据；
  // 该探针只使用 fake HTTP，不访问网络。
  if (online_auth_test) {
    const QString auth_probe_root =
        QDir::currentPath() + QStringLiteral("/build/t8.2-auth-probe/appdata");
    QDir(auth_probe_root).removeRecursively();
    QDir().mkpath(auth_probe_root);
    qputenv("APPDATA", auth_probe_root.toUtf8());
  }

  const auto process_start = std::chrono::steady_clock::now();
  // T8.3.1：所有自检/GUI 回归都不在真实默认缓存上跑启动清理；产品路径默认开启。
  bool running_test_mode = false;
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    if (arg.endsWith(QStringLiteral("-test")) ||
        arg == QStringLiteral("--settings-expect") ||
        arg == QStringLiteral("--drag-fast")) {
      running_test_mode = true;
      break;
    }
  }
  MainWindow window(nullptr, settings_root, !running_test_mode, ui_v2);
  if (online_m2_force_durl &&
      (online_m1_test || online_longpress_test || online_drag_preview_test ||
       online_quality_test)) {
    // T8.10 回归：--force-durl 让既有 M1/长按/档位 UI 探针走 M1 单文件路径，
    // 用来证明 DASH 双输入没有改变 M1 语义。
    window.SetOnlineDashEnabledForTest(false);
  }
  log_startup("window_created");
  if (ui_shots_test) {
    if (!file_path.isEmpty()) {
      window.OpenFile(file_path);
    }
    QTimer::singleShot(1200, &window, [&window]() {
      const QString shots =
          QDir::currentPath() + QStringLiteral("/build/ui-v2-shots");
      window.CaptureThemeShotsForTest(shots);
      // P3：清晰度 chip 视觉证据 + 1280/820 顶栏溢出断言。
      const bool quality_ok = window.CaptureQualityChipShotsForTest(shots);
      QCoreApplication::exit(quality_ok ? 0 : 1);
    });
  }
  if (online_quality_no_catalog) {
    window.SetOnlineQualityCatalogEnabledForTest(false);
    std::fprintf(stderr, "online_quality_catalog disabled=1\n");
    std::fflush(stderr);
  }
  // T7.1：旧 GUI 回归在 OpenFile 后立即导航；保持同步索引语义。
  // 产品路径与 --startup-test 走异步索引（首帧先出，后台扫描）。
  const bool legacy_gui_test =
      step_test || ab_test || loop_test || loop_end_test || mark_test || note_test ||
      snapshot_test ||
      fullscreen_test || key_test || settings_test || settings_expect || dpi_test ||
      banner_test;
  if (legacy_gui_test) {
    window.SetIndexSynchronousForTest(true);
  }
  if (online_m1_test || online_longpress_test || online_drag_preview_test ||
      online_drag_parity_test || online_quality_test ||
      online_quality_switch_test || online_vip_test ||
      (online_m2_test && !online_m2_default_paths) || online_index_test ||
      (online_offline_test && !online_offline_default_paths)) {
    if (online_m1_cache_root.isEmpty()) {
      QString cache_name = QStringLiteral("/build/t8.6-online-cache");
      if (online_offline_test) {
        cache_name = QStringLiteral("/build/t8.7-online-cache");
      } else if (online_longpress_test) {
        cache_name = QStringLiteral("/build/t8.6.1a-online-cache");
      } else if (online_drag_parity_test) {
        cache_name = QStringLiteral("/build/t8.18-online-parity-cache");
      } else if (online_drag_preview_test) {
        cache_name = QStringLiteral("/build/t8.6.1b-online-cache");
      } else if (online_quality_test) {
        cache_name = QStringLiteral("/build/t8.9-online-cache");
      } else if (online_quality_switch_test) {
        cache_name = QStringLiteral("/build/t8.17-online-cache");
      } else if (online_vip_test) {
        cache_name = QStringLiteral("/build/t8.15-online-cache");
      } else if (online_m2_test) {
        cache_name = QStringLiteral("/build/t8.10-online-cache");
      } else if (online_index_test) {
        cache_name = QStringLiteral("/build/t8.11-online-cache");
      }
      online_m1_cache_root = QDir::currentPath() + cache_name;
    }
    online_m1_cache_root = QFileInfo(online_m1_cache_root).absoluteFilePath();
    // T8.6.1a 可选：长按探针要求增长中的会话（缓存命中会走本地同步步进）。
    if (online_longpress_test && online_longpress_fresh_cache) {
      QDir(online_m1_cache_root).removeRecursively();
    }
    // T8.6.1b：增长会话阶段用干净缓存，保证第一次拖到的是 `.part`；缓存命中
    // 阶段（--drag-cache-hit）必须保留上一轮完整缓存。
    if (online_drag_preview_test && !online_drag_cache_hit) {
      QDir(online_m1_cache_root).removeRecursively();
    }
    if (online_drag_parity_test && !online_drag_cache_hit) {
      QDir(online_m1_cache_root).removeRecursively();
    }
    QDir().mkpath(online_m1_cache_root);
    window.SetOnlineCacheRootForTest(online_m1_cache_root);
    if (online_m1_pictures.isEmpty()) {
      QString pictures_name = QStringLiteral("/build/t8.6-online-pictures");
      if (online_offline_test) {
        pictures_name = QStringLiteral("/build/t8.7-online-pictures");
      } else if (online_longpress_test) {
        pictures_name = QStringLiteral("/build/t8.6.1a-online-pictures");
      } else if (online_drag_parity_test) {
        pictures_name = QStringLiteral("/build/t8.18-online-parity-pictures");
      } else if (online_drag_preview_test) {
        pictures_name = QStringLiteral("/build/t8.6.1b-online-pictures");
      } else if (online_quality_test) {
        pictures_name = QStringLiteral("/build/t8.9-online-pictures");
      } else if (online_quality_switch_test) {
        pictures_name = QStringLiteral("/build/t8.17-online-pictures");
      } else if (online_vip_test) {
        pictures_name = QStringLiteral("/build/t8.15-online-pictures");
      } else if (online_m2_test) {
        pictures_name = QStringLiteral("/build/t8.10-online-pictures");
      } else if (online_index_test) {
        pictures_name = QStringLiteral("/build/t8.11-online-pictures");
      }
      online_m1_pictures = QDir::currentPath() + pictures_name;
    }
    online_m1_pictures = QFileInfo(online_m1_pictures).absoluteFilePath();
    QDir(online_m1_pictures).removeRecursively();
    QDir().mkpath(online_m1_pictures);
    window.SetSnapshotPicturesRootForTest(online_m1_pictures);
  }
  if (online_offline_test) {
    if (online_offline_default_paths) {
      // 默认 AppData 验收：只允许覆盖图片目录（显式传入时），不碰缓存/记录。
      if (!online_m1_pictures.isEmpty()) {
        online_m1_pictures =
            QFileInfo(online_m1_pictures).absoluteFilePath();
        QDir().mkpath(online_m1_pictures);
        window.SetSnapshotPicturesRootForTest(online_m1_pictures);
      }
    } else {
      if (online_records_root.isEmpty()) {
        online_records_root =
            QDir::currentPath() + QStringLiteral("/build/t8.7-online-records");
      }
      online_records_root = QFileInfo(online_records_root).absoluteFilePath();
      QDir(online_records_root).removeRecursively();
      QDir().mkpath(online_records_root);
      window.SetOnlineRecordsRootForTest(online_records_root);
    }
  } else if (online_m1_test || online_longpress_test ||
             online_drag_preview_test || online_quality_test ||
             online_quality_switch_test || online_vip_test ||
             (online_m2_test && !online_m2_default_paths) || online_index_test) {
    // T8.7：在线记录现在会落盘；E2E 用独立根，避免 Low 工作区写 AppData
    // 失败时弹出模态“保存失败”阻塞自动化。
    if (online_records_root.isEmpty()) {
      QString records_name = QStringLiteral("/build/t8.7-online-records-m1");
      if (online_longpress_test) {
        records_name = QStringLiteral("/build/t8.6.1a-online-records");
      } else if (online_drag_parity_test) {
        records_name = QStringLiteral("/build/t8.18-online-parity-records");
      } else if (online_drag_preview_test) {
        records_name = QStringLiteral("/build/t8.6.1b-online-records");
      } else if (online_quality_test) {
        records_name = QStringLiteral("/build/t8.9-online-records");
      } else if (online_quality_switch_test) {
        records_name = QStringLiteral("/build/t8.17-online-records");
      } else if (online_vip_test) {
        records_name = QStringLiteral("/build/t8.15-online-records");
      } else if (online_m2_test) {
        records_name = QStringLiteral("/build/t8.10-online-records");
      } else if (online_index_test) {
        records_name = QStringLiteral("/build/t8.11-online-records");
      }
      online_records_root = QDir::currentPath() + records_name;
    }
    online_records_root = QFileInfo(online_records_root).absoluteFilePath();
    QDir(online_records_root).removeRecursively();
    QDir().mkpath(online_records_root);
    window.SetOnlineRecordsRootForTest(online_records_root);
  } else if (online_buffer_test) {
    online_records_root =
        QDir::currentPath() + QStringLiteral("/build/t8.7-online-records-buffer");
    online_records_root = QFileInfo(online_records_root).absoluteFilePath();
    QDir(online_records_root).removeRecursively();
    QDir().mkpath(online_records_root);
    window.SetOnlineRecordsRootForTest(online_records_root);
  }
  // L1：--loop-test 的在线缓存命中相位——缓存根由 --online-cache-root 指定（通常是
  // 夹具 / 已下载目录的副本）；记录/截图用独立根，不写 AppData。
  if (loop_test && loop_online_cache_hit) {
    if (online_m1_cache_root.isEmpty()) {
      online_m1_cache_root =
          QDir::currentPath() + QStringLiteral("/build/L1/cache");
    }
    online_m1_cache_root = QFileInfo(online_m1_cache_root).absoluteFilePath();
    QDir().mkpath(online_m1_cache_root);
    window.SetOnlineCacheRootForTest(online_m1_cache_root);
    if (online_m1_pictures.isEmpty()) {
      online_m1_pictures =
          QDir::currentPath() + QStringLiteral("/build/L1/pictures");
    }
    online_m1_pictures = QFileInfo(online_m1_pictures).absoluteFilePath();
    QDir(online_m1_pictures).removeRecursively();
    QDir().mkpath(online_m1_pictures);
    window.SetSnapshotPicturesRootForTest(online_m1_pictures);
    if (online_records_root.isEmpty()) {
      online_records_root =
          QDir::currentPath() + QStringLiteral("/build/L1/records");
    }
    online_records_root = QFileInfo(online_records_root).absoluteFilePath();
    QDir(online_records_root).removeRecursively();
    QDir().mkpath(online_records_root);
    window.SetOnlineRecordsRootForTest(online_records_root);
  }
  // T8.13：故障探针全部注入，不需要真实 BV/凭据，但必须用独立根（不碰 AppData）。
  if (online_fault_test) {
    const QString fault_root =
        QDir::currentPath() + QStringLiteral("/build/t8.13-fault");
    if (online_m1_cache_root.isEmpty()) {
      online_m1_cache_root = fault_root + QStringLiteral("/cache");
    }
    online_m1_cache_root = QFileInfo(online_m1_cache_root).absoluteFilePath();
    QDir().mkpath(online_m1_cache_root);
    window.SetOnlineCacheRootForTest(online_m1_cache_root);
    if (online_m1_pictures.isEmpty()) {
      online_m1_pictures = fault_root + QStringLiteral("/pics");
    }
    online_m1_pictures = QFileInfo(online_m1_pictures).absoluteFilePath();
    QDir().mkpath(online_m1_pictures);
    window.SetSnapshotPicturesRootForTest(online_m1_pictures);
    if (online_records_root.isEmpty()) {
      online_records_root = fault_root + QStringLiteral("/records");
    }
    online_records_root = QFileInfo(online_records_root).absoluteFilePath();
    QDir().mkpath(online_records_root);
    window.SetOnlineRecordsRootForTest(online_records_root);
    if (online_fault_media.isEmpty()) {
      const QString fast =
          QDir::currentPath() +
          QStringLiteral("/build/test-media/faststart-720p.mp4");
      const QString proxy =
          QDir::currentPath() + QStringLiteral("/media/proxy/proxy-V1.mp4");
      online_fault_media = QFileInfo::exists(fast) ? fast : proxy;
    }
    online_fault_media = QFileInfo(online_fault_media).absoluteFilePath();
  }
  if (!snapshot_pictures_root.isEmpty()) {
    // 通过构造函数后的 setter 传 Unicode 路径，避免 qputenv 的 ANSI 限制。
    window.SetSnapshotPicturesRootForTest(snapshot_pictures_root);
  }
  if (robust_test) {
    window.SetMarkAppDataRootForTest(robust_root + QStringLiteral("/appdata"));
    window.SetSnapshotPicturesRootForTest(robust_root + QStringLiteral("/pictures"));
  }

  // T7.1：首帧 Present 后写一条 first_present 并退出；80ms 留给 DWM/绘制收尾。
  auto first_present_seen = std::make_shared<bool>(false);
  if (startup_test) {
    QObject::connect(
        &window, &MainWindow::framePresented, &window,
        [&app, log_startup_at, first_present_seen]() {
          if (*first_present_seen) {
            return;
          }
          *first_present_seen = true;
          log_startup_at("first_present", std::chrono::steady_clock::now());
          QTimer::singleShot(80, &app, &QCoreApplication::quit);
        });
    // 防止设备/解码异常时挂死自动测量。
    QTimer::singleShot(10000, &app, []() {
      std::fprintf(stderr, "T7.1 startup stage=timeout\n");
      std::fflush(stderr);
      QCoreApplication::exit(3);
    });
  }

  window.show();
  log_startup("window_shown");

  // T7.2：内存测量走产品异步索引路径；runner 自己负责打开/播放/切文件。
  std::unique_ptr<lapian::app::MemoryTestRunner> memory_runner;
  if (memory_test) {
    if (memory_tag.isEmpty()) {
      memory_tag = memory_mode;
    }
    memory_runner = std::make_unique<lapian::app::MemoryTestRunner>(
        &window, memory_mode, memory_tag, memory_seconds, memory_steps,
        memory_switch_cycles, memory_interval, memory_soak_loop,
        memory_loop_frames, file_path, file_b);
    memory_runner->Start();
  }

  // T7.4：4K 路径探针；同样走产品异步索引与真实拖动/步进入口。
  std::unique_ptr<lapian::app::DragTestRunner> drag_runner;
  if (drag_test) {
    if (drag_tag.isEmpty()) {
      drag_tag = force_software_decode ? QStringLiteral("soft") : QStringLiteral("hard");
    }
    drag_runner = std::make_unique<lapian::app::DragTestRunner>(
        &window, file_path, drag_samples, drag_tag, drag_fast, drag_fast_step);
    if (drag_far) {
      drag_runner->EnableFarTest();
    }
    drag_runner->SetFastCycles(drag_fast_cycles);
    // 3B：时间轴几何必须在 runner 打开文件/开始测量之前落地（R6/R11 两种几何）。
    if (timeline_height > 0) {
      window.SetTimelineHeightForTest(timeline_height);
      std::fprintf(stderr, "T7.4 timeline_height requested=%d split=%s\n",
                   timeline_height,
                   window.SplitterStateForTest().toUtf8().constData());
      std::fflush(stderr);
    }
    drag_runner->Start();
  }

  // DP-3：交互策略验收矩阵（慢拖/快拖/反向/远跳/松手）。
  std::unique_ptr<lapian::app::DragStrategyTestRunner> drag_strategy_runner;
  if (drag_strategy_test) {
    drag_strategy_runner =
        std::make_unique<lapian::app::DragStrategyTestRunner>(&window, file_path);
    drag_strategy_runner->Start();
  }

  // T7.11：P7 跨 GOP 边界后退专项；与拖动/步进探针共用产品入口。
  std::unique_ptr<lapian::app::GopBoundaryTestRunner> gop_boundary_runner;
  if (gop_boundary_test) {
    if (gop_boundary_tag.isEmpty()) {
      gop_boundary_tag =
          force_software_decode ? QStringLiteral("soft") : QStringLiteral("hard");
    }
    gop_boundary_runner = std::make_unique<lapian::app::GopBoundaryTestRunner>(
        &window, file_path, gop_boundary_samples, gop_boundary_tag);
    gop_boundary_runner->Start();
  }

  // T7.5：错误与鲁棒性探针。
  std::unique_ptr<lapian::app::RobustTestRunner> robust_runner;
  if (robust_test) {
    lapian::app::RobustTestRunner::Options options;
    options.media = robust_media;
    options.tag = robust_tag;
    options.root = robust_root;
    options.only = robust_only;
    options.read_only_dir = robust_readonly_dir;
    options.pictures_root = robust_root + QStringLiteral("/pictures");
    robust_runner =
        std::make_unique<lapian::app::RobustTestRunner>(&window, options);
    robust_runner->Start();
  }

  // T8.1：在线解析探针。
  std::unique_ptr<lapian::app::OnlineResolveTestRunner> online_resolve_runner;
  if (online_resolve_test) {
    online_resolve_runner =
        std::make_unique<lapian::app::OnlineResolveTestRunner>(&window);
    online_resolve_runner->Start();
  }

  // T8.2：凭据 UI / DEC-3 时机 / nav 失败继续匿名。
  std::unique_ptr<lapian::app::OnlineAuthTestRunner> online_auth_runner;
  if (online_auth_test) {
    online_auth_runner =
        std::make_unique<lapian::app::OnlineAuthTestRunner>(&window);
    online_auth_runner->Start();
  }

  std::unique_ptr<lapian::app::OnlineBufferTestRunner> online_buffer_runner;
  if (online_buffer_test) {
    if (online_buffer_media.isEmpty()) {
      const QString fast =
          QDir::currentPath() +
          QStringLiteral("/build/test-media/faststart-720p.mp4");
      online_buffer_media = QFileInfo::exists(fast)
                                ? fast
                                : QDir::currentPath() +
                                      QStringLiteral("/media/V9.mp4");
    }
    lapian::app::OnlineBufferTestOptions options;
    options.media_path = online_buffer_media;
    options.timeout_ms = online_buffer_timeout_ms;
    online_buffer_runner =
        std::make_unique<lapian::app::OnlineBufferTestRunner>(&window, options);
    online_buffer_runner->Start();
  }

  std::unique_ptr<lapian::app::OnlineM1TestRunner> online_m1_runner;
  if (online_m1_test) {
    lapian::app::OnlineM1TestOptions options;
    options.bvid = online_m1_bvid;
    options.cid = online_m1_cid;
    options.page = online_m1_page;
    options.tag = online_m1_tag;
    options.timeout_ms = online_m1_timeout_ms;
    options.pictures_root = online_m1_pictures;
    options.cancel_only = online_m1_cancel_only;
    options.uncached_landing = online_m1_uncached_landing;
    options.throttle_bytes_per_sec = online_m1_throttle_bps;
    online_m1_runner =
        std::make_unique<lapian::app::OnlineM1TestRunner>(&window, options);
    online_m1_runner->Start();
  }

  // T8.9：`--online-quality-test --ui`，真实窗口验证档位按钮/菜单与“不假装切档”。
  std::unique_ptr<lapian::app::OnlineQualityUiTestRunner> online_quality_runner;
  if (online_quality_test) {
    lapian::app::OnlineQualityTestOptions options;
    options.bvid = online_m1_bvid;
    options.cid = online_m1_cid;
    options.cid_text = online_m1_cid_text;
    options.tag = online_m1_tag;
    options.timeout_ms = online_m1_timeout_ms;
    options.sessdata_file = online_quality_sessdata_file;
    options.anonymous = online_quality_anonymous;
    options.odd_qn = online_quality_odd_qn;
    options.ui = true;
    options.cache_root = online_m1_cache_root;
    options.pictures_root = online_m1_pictures;
    options.records_root = online_records_root;
    online_quality_runner =
        std::make_unique<lapian::app::OnlineQualityUiTestRunner>(&window, options);
    online_quality_runner->Start();
  }

  // T8.17：默认 1080P → 大会员 1080P+ → 切回 1080P30 的真实窗口探针。
  std::unique_ptr<lapian::app::OnlineQualitySwitchUiTestRunner>
      online_quality_switch_runner;
  if (online_quality_switch_test) {
    lapian::app::OnlineQualitySwitchTestOptions options;
    options.bvid = online_m1_bvid;
    options.cid = online_m1_cid;
    options.cid_text = online_m1_cid_text;
    options.tag = online_m1_tag;
    options.timeout_ms = online_m1_timeout_ms;
    options.high_qn = online_quality_switch_high_qn > 0
                          ? online_quality_switch_high_qn
                          : 112;
    options.back_qn = online_quality_switch_back_qn > 0
                          ? online_quality_switch_back_qn
                          : 80;
    options.wait_complete_ms = std::max(30000, online_quality_switch_wait_ms);
    options.sessdata_file = online_quality_sessdata_file;
    options.cache_root = online_m1_cache_root;
    options.pictures_root = online_m1_pictures;
    options.records_root = online_records_root;
    options.cache_hit_menu = online_quality_switch_cache_hit_menu;
    online_quality_switch_runner =
        std::make_unique<lapian::app::OnlineQualitySwitchUiTestRunner>(
            &window, options);
    online_quality_switch_runner->Start();
  }

  // T8.15：大会员可用档条件验收（真实窗口；O11，非 MVP 门）。
  std::unique_ptr<lapian::app::OnlineVipUiTestRunner> online_vip_runner;
  if (online_vip_test) {
    lapian::app::OnlineVipTestOptions options;
    options.bvid = online_m1_bvid;
    options.cid_text = online_m1_cid_text;
    options.cid = online_m1_cid;
    options.tag = online_m1_tag;
    options.timeout_ms = online_m1_timeout_ms;
    options.qn = online_vip_qn;
    options.codecid = online_vip_codecid;
    options.expect_auto = online_vip_expect_auto;
    options.play_seconds = online_vip_play_seconds;
    options.seeks = online_vip_seeks;
    options.switch_qn = online_vip_switch_qn;
    options.sessdata_file = online_vip_sessdata_file;
    options.cache_root = online_m1_cache_root;
    options.pictures_root = online_m1_pictures;
    options.records_root = online_records_root;
    online_vip_runner =
        std::make_unique<lapian::app::OnlineVipUiTestRunner>(&window, options);
    online_vip_runner->Start();
  }

  // T8.10：DASH 双输入真实窗口验收（音画/seek/缓存命中/内存）。
  std::unique_ptr<lapian::app::OnlineM2TestRunner> online_m2_runner;
  if (online_m2_test) {
    lapian::app::OnlineM2TestOptions options;
    options.bvid = online_m1_bvid;
    options.cid_text = online_m1_cid_text;
    options.cid = online_m1_cid;
    options.tag = online_m1_tag;
    options.timeout_ms = online_m1_timeout_ms;
    options.play_seconds = online_m2_play_seconds;
    options.seeks = online_m2_seeks;
    options.force_durl = online_m2_force_durl;
    options.cache_hit_only = online_m2_cache_hit_only;
    options.skip_cache_hit = online_m2_skip_cache_hit;
    options.op4 = online_m2_op4;
    options.op5 = online_m2_op5;
    options.default_paths = online_m2_default_paths;
    options.requested_qn = online_m2_requested_qn;
    options.cache_root = online_m1_cache_root;
    options.pictures_root = online_m1_pictures;
    options.records_root = online_records_root;
    options.sessdata_file = online_quality_sessdata_file;
    online_m2_runner =
        std::make_unique<lapian::app::OnlineM2TestRunner>(&window, options);
    online_m2_runner->Start();
  }

  // T8.11：渐进索引/波形/总帧数（真实在线 + 受控慢增长离线 case）。
  std::unique_ptr<lapian::app::OnlineIndexTestRunner> online_index_runner;
  if (online_index_test) {
    lapian::app::OnlineIndexTestOptions options;
    options.bvid = online_m1_bvid;
    options.cid_text = online_m1_cid_text;
    options.cid = online_m1_cid;
    options.tag = online_m1_tag;
    options.timeout_ms = online_m1_timeout_ms;
    options.seeks = online_index_seeks;
    options.throttle_bytes_per_sec = online_index_throttle_bps;
    options.offline_video = online_index_offline_video;
    options.offline_audio = online_index_offline_audio;
    options.sessdata_file = online_quality_sessdata_file;
    online_index_runner =
        std::make_unique<lapian::app::OnlineIndexTestRunner>(&window, options);
    online_index_runner->Start();
  }

  // T8.6.1a：在线长按 PgUp/PgDn（异步步进队列/边界/未缓存提示）。
  std::unique_ptr<lapian::app::OnlineLongPressTestRunner> online_longpress_runner;
  if (online_longpress_test) {
    lapian::app::OnlineLongPressTestOptions options;
    options.bvid = online_m1_bvid;
    options.cid = online_m1_cid;
    options.page = online_m1_page;
    options.tag = online_m1_tag;
    options.timeout_ms = online_m1_timeout_ms;
    options.hold_ms = online_longpress_hold_ms;
    online_longpress_runner =
        std::make_unique<lapian::app::OnlineLongPressTestRunner>(&window, options);
    online_longpress_runner->Start();
  }

  // T8.6.1b：在线增长会话拖动预览（已缓存 FPS/精确落帧/未缓存提示/缓存命中）。
  std::unique_ptr<lapian::app::OnlineDragPreviewTestRunner>
      online_drag_preview_runner;
  if (online_drag_preview_test || local_drag_parity_test || scrub_space_test ||
      cache_switch_test) {
    lapian::app::OnlineDragPreviewTestOptions options;
    options.bvid = online_m1_bvid;
    options.cid = online_m1_cid;
    options.page = online_m1_page;
    options.tag = online_m1_tag;
    options.timeout_ms = online_m1_timeout_ms;
    options.drag_seconds = online_drag_seconds;
    options.drag_samples = online_drag_samples;
    options.drag_backward_samples = online_drag_backward_samples;
    options.start_frame = online_drag_start_frame;
    options.start_frame_explicit = online_drag_start_frame_explicit;
    options.cache_hit_only = online_drag_cache_hit;
    options.drag_wait_ms = online_drag_wait_ms;
    options.preview_dump_dir = preview_dump_dir_arg;
    options.long_span_gate = online_drag_long_span_gate;
    options.throttle_bytes_per_sec = online_m1_throttle_bps;
    options.parity = online_drag_parity_test;
    options.local_only = local_drag_parity_test;
    options.local_file = file_path;
    options.scrub_space = scrub_space_test;
    options.scrub_space_local = scrub_space_test && online_m1_bvid.isEmpty();
    options.cache_switch = cache_switch_test;
    online_drag_preview_runner =
        std::make_unique<lapian::app::OnlineDragPreviewTestRunner>(&window,
                                                                   options);
    online_drag_preview_runner->Start();
  }

  std::unique_ptr<lapian::app::OnlineOfflineTestRunner> online_offline_runner;
  if (online_offline_test) {
    if (online_offline_media.isEmpty()) {
      const QString fast =
          QDir::currentPath() +
          QStringLiteral("/build/test-media/faststart-720p.mp4");
      online_offline_media =
          QFileInfo::exists(fast) ? fast
                                  : QDir::currentPath() + QStringLiteral("/media/V9.mp4");
    }
    lapian::app::OnlineOfflineTestOptions options;
    options.bvid = online_m1_bvid;
    options.cid = online_m1_cid;
    options.tag = online_m1_tag;
    options.media_path = online_offline_media;
    options.cache_root = online_m1_cache_root;
    options.pictures_root = online_m1_pictures;
    options.records_root = online_records_root;
    options.timeout_ms = online_m1_timeout_ms;
    options.keep_cache = online_offline_keep_cache;
    options.skip_corrupt = online_offline_skip_corrupt;
    options.use_default_paths = online_offline_default_paths;
    online_offline_runner =
        std::make_unique<lapian::app::OnlineOfflineTestRunner>(&window, options);
    online_offline_runner->Start();
  }

  // T8.13：网络异常/并发加固（真实窗口 + 注入假 CDN；不断言 M2）。
  std::unique_ptr<lapian::app::OnlineFaultTestRunner> online_fault_runner;
  if (online_fault_test) {
    lapian::app::OnlineFaultTestOptions options;
    options.bvid = online_m1_bvid;
    options.cid = online_m1_cid > 0 ? online_m1_cid : 42915922022LL;
    options.page = online_m1_page;
    options.tag = online_m1_tag.isEmpty() ? online_fault_set : online_m1_tag;
    options.media_path = online_fault_media;
    options.cache_root = online_m1_cache_root;
    options.pictures_root = online_m1_pictures;
    options.records_root = online_records_root;
    options.only = online_fault_only;
    options.timeout_ms = online_m1_timeout_ms;
    online_fault_runner =
        std::make_unique<lapian::app::OnlineFaultTestRunner>(&window, options);
    online_fault_runner->Start();
  }

  // 事件循环第一次派发：窗口已 show 且能处理事件（P1 的“可交互”口径）。
  QTimer::singleShot(0, &window, [&app, &window, startup_test, startup_log, startup_ms,
                                  file_path, ui_v2]() {
    if (startup_log) {
      std::fprintf(stderr, "T7.1 startup stage=ui_ready t_ms=%.2f\n",
                   startup_ms(std::chrono::steady_clock::now()));
      // A.8：d3d_init_at_startup=0 表示"空态隐藏 GfxWidget 生效"——即 D3D11
      // 不是在窗口 show 时就创建，而是被首次打开媒体触发（P1 因此更快）。
      std::fprintf(stderr,
                   "T7.1 startup d3d_init_at_startup=%d ui_v2=%d density=%s\n",
                   window.D3dInitAtStartupForTest() ? 1 : 0, ui_v2 ? 1 : 0,
                   window.DensityNameForTest().toUtf8().constData());
      std::fflush(stderr);
    }
    if (startup_test && file_path.isEmpty()) {
      // P1 无文件：可交互后再留 120ms 让空态首帧绘制，然后正常退出。
      QTimer::singleShot(120, &app, &QCoreApplication::quit);
    }
  });

  if ((!file_path.isEmpty() || (dpi_test && dpi_empty) ||
       (loop_test && loop_online_cache_hit)) &&
      !memory_test && !drag_test && !robust_test && !gop_boundary_test &&
      !ui_shots_test) {
    QTimer::singleShot(0, &window, [&window, &app, file_path, file_b, goto_text, auto_play,
                                    step_test, ab_test, loop_test, loop_end_test,
                                    mark_test, note_test, readout_edit_test,
                                    snapshot_test, fullscreen_test, key_test, settings_test,
                                    settings_expect, settings_expect_volume,
                                    settings_expect_waveform, settings_root, dpi_test,
                                    dpi_empty,
                                    dpi_expect_scale, dpi_tag, banner_test, banner_tag,
                                    banner_vfr, banner_tenbit, banner_noaudio,
                                    snapshot_pictures_root, playback_rate,
                                    has_playback_rate, process_start, startup_test,
                                    index_test, startup_log, startup_ms, log_startup,
                                    log_startup_at, timeline_height,
                                    loop_online_cache_hit, online_m1_bvid,
                                    online_m1_cid]() {
      if (startup_log) {
        std::fprintf(stderr, "T7.1 startup stage=open_begin t_ms=%.2f\n",
                     startup_ms(std::chrono::steady_clock::now()));
        std::fflush(stderr);
      }
      if (!file_path.isEmpty()) {
        window.OpenFile(file_path);
      }
      log_startup("open_returned");
      // 3B：--timeline-height 让 P4/P6 在"时间轴 132 / 340"两种几何下各测一次
      // （规划 R6/R11）。走 SetSplitSizesForTest 的同一条夹取路径。
      if (timeline_height > 0) {
        window.SetTimelineHeightForTest(timeline_height);
        std::fprintf(stderr, "T7.4 timeline_height requested=%d split=%s\n",
                     timeline_height,
                     window.SplitterStateForTest().toUtf8().constData());
        std::fflush(stderr);
      }
      if (startup_test && !window.HasMediaForTest()) {
        std::fprintf(stderr, "T7.1 startup stage=open_failed\n");
        std::fflush(stderr);
        QCoreApplication::exit(2);
        return;
      }
      if (index_test) {
        // T7.1 异步索引：扫描中 → 就绪；随后再开一个带 sidecar 的文件验证标记
        // 在索引完成后加载。
        auto index_failures = std::make_shared<int>(0);
        auto index_checks = std::make_shared<int>(0);
        auto index_poll_count = std::make_shared<int>(0);
        auto index_check = [index_failures, index_checks](const char* name, bool ok) {
          ++(*index_checks);
          if (!ok) {
            ++(*index_failures);
          }
          std::fprintf(stderr, "T7.1 index_check %s ok=%d\n", name, ok ? 1 : 0);
          std::fflush(stderr);
        };
        const QString index_video_b = file_b;

        QTimer::singleShot(120, &window, [&window, index_check]() {
          const QString state = window.IndexStateForTest();
          if (!state.contains(QStringLiteral("scanning=1"))) {
            std::fprintf(stderr, "T7.1 index_test scan_too_fast %s\n",
                         state.toUtf8().constData());
            std::fflush(stderr);
            return;
          }
          std::fprintf(stderr, "T7.1 index_state scanning %s\n",
                       state.toUtf8().constData());
          std::fflush(stderr);
          index_check("scanning-ready-off",
                      state.contains(QStringLiteral("ready=0")));
          index_check("scanning-total",
                      state.contains(QStringLiteral("total=1")));
          index_check("scanning-nav-disabled",
                      state.contains(QStringLiteral("nav=0")));
          index_check("scanning-timeline-disabled",
                      state.contains(QStringLiteral("timeline=0")));
          index_check("scanning-frame0", window.CurrentFrameForTest() == 0);
        });

        auto poll = std::make_shared<std::function<void()>>();
        *poll = [&window, &app, index_check, index_failures, index_poll_count,
                 index_video_b, poll]() {
          if (++(*index_poll_count) > 200) {
            index_check("index-ready-timeout", false);
            QCoreApplication::exit(*index_failures == 0 ? 0 : 1);
            return;
          }
          if (!window.IndexReadyForTest()) {
            QTimer::singleShot(50, &window, [poll]() { (*poll)(); });
            return;
          }
          const QString state = window.IndexStateForTest();
          std::fprintf(stderr, "T7.1 index_state ready %s\n",
                       state.toUtf8().constData());
          std::fflush(stderr);
          index_check("ready-state",
                      state.contains(QStringLiteral("ready=1")) &&
                          state.contains(QStringLiteral("scanning=0")));
          index_check("ready-frames", window.IndexFrameCountForTest() > 0);
          index_check("ready-nav-enabled",
                      state.contains(QStringLiteral("nav=1")));
          index_check("ready-timeline-enabled",
                      state.contains(QStringLiteral("timeline=1")));
          index_check("ready-total-exact",
                      state.contains(QStringLiteral("total=3")));
          if (index_video_b.isEmpty()) {
            QCoreApplication::exit(*index_failures == 0 ? 0 : 1);
            return;
          }
          // 带 sidecar 的文件：索引完成后才加载标记（异步路径）。
          window.OpenFile(index_video_b);
          QTimer::singleShot(500, &window, [&window, index_check, index_failures]() {
            index_check("b-ready", window.IndexReadyForTest());
            index_check("b-marks-loaded", window.MarkCountForTest() == 1);
            QCoreApplication::exit(*index_failures == 0 ? 0 : 1);
          });
        };
        QTimer::singleShot(60, &window, [poll]() { (*poll)(); });
      }
      if (!goto_text.isEmpty()) {
        window.GoToInputForTest(goto_text);
      }
      if (has_playback_rate) {
        window.SetSpeedForTest(playback_rate);
      }
      if (auto_play) {
        window.StartPlaybackForTest();
      }
      if (step_test) {
        QTimer::singleShot(1200, &window, [&window]() { window.StepForwardForTest(); });
        QTimer::singleShot(1800, &window, [&window]() { window.StartPlaybackForTest(); });
      }
      if (ab_test) {
        // T4.1 F4-A4/A8 自动化：状态日志 + 时间轴截图（build/t4.1-ab-*.png）。
        auto log_state = [&window](const char* step) {
          const QByteArray state = window.LoopStateForTest().toUtf8();
          std::fprintf(stderr, "T4.1 ab_test %s %s\n", step, state.constData());
          std::fflush(stderr);
        };
        auto save_shot = [&window](const char* name) {
          const QString dir = QDir::currentPath() + QStringLiteral("/build");
          QDir().mkpath(dir);
          const QString path = dir + QStringLiteral("/t4.1-ab-") +
                               QString::fromLatin1(name) + QStringLiteral(".png");
          const bool ok = window.grab().save(path);
          std::fprintf(stderr, "T4.1 ab_test shot=%s ok=%d\n",
                       path.toUtf8().constData(), ok ? 1 : 0);
          std::fflush(stderr);
        };

        QTimer::singleShot(400, &window, [&window, log_state]() {
          window.GoToInputForTest(QStringLiteral("100"));
          window.SetLoopInForTest();
          log_state("a100");
        });
        QTimer::singleShot(600, &window, [&window, log_state, save_shot]() {
          window.GoToInputForTest(QStringLiteral("160"));
          window.SetLoopOutForTest();
          log_state("b160");
          save_shot("range-off");
        });
        QTimer::singleShot(800, &window, [&window, log_state, save_shot]() {
          window.ToggleLoopForTest();
          log_state("loop-on");
          save_shot("loop-on");
        });
        QTimer::singleShot(1000, &window, [&window, log_state, save_shot]() {
          window.GoToInputForTest(QStringLiteral("90"));
          window.SetLoopOutForTest();  // F4-A8：必须被拒绝，A/B 与循环不变
          log_state("reject-out90");
          save_shot("reject-out90");
        });
        QTimer::singleShot(1200, &window, [&window, log_state]() {
          window.GoToInputForTest(QStringLiteral("170"));
          window.SetLoopInForTest();  // 晚于原 B：清 B 并关循环
          log_state("in170-clears-b");
        });
        QTimer::singleShot(1400, &window, [&window, log_state]() {
          window.GoToInputForTest(QStringLiteral("190"));
          window.SetLoopOutForTest();
          window.ToggleLoopForTest();
          log_state("loop-on2");
        });
        QTimer::singleShot(1600, &window, [&window, log_state, save_shot]() {
          window.ClearLoopForTest();  // F4-A4：色条消失、循环关闭
          log_state("cleared");
          save_shot("cleared");
        });
        QTimer::singleShot(1800, &app, &QCoreApplication::quit);
      }
      if (loop_test) {
        // T4.2 F4-A1/A2/A3/A9/A10：循环跳转、定帧、Space 语义与解除条件。
        // 使用 PreciseTimer，避免 CoarseTimer 的 5% 容差让后续回调乱序。
        auto schedule = [&window](int ms, auto fn) {
          QTimer::singleShot(ms, Qt::PreciseTimer, &window, std::move(fn));
        };
        auto log_state = [&window, process_start](const char* step) {
          const double elapsed_ms =
              std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - process_start)
                  .count();
          const QByteArray state = window.LoopPlaybackStateForTest().toUtf8();
          std::fprintf(stderr, "T4.2 loop_test t=%.0fms %s %s\n", elapsed_ms,
                       step, state.constData());
          std::fflush(stderr);
        };
        auto failures = std::make_shared<int>(0);
        auto check = [failures](const char* name, bool ok) {
          if (!ok) {
            ++(*failures);
          }
          std::fprintf(stderr, "T4.2 loop_check %s ok=%d\n", name, ok ? 1 : 0);
          std::fflush(stderr);
        };
        auto finish = [&app, failures]() {
          std::fprintf(stderr, "T4.2 loop_test_summary failures=%d\n", *failures);
          std::fflush(stderr);
          QCoreApplication::exit(*failures == 0 ? 0 : 1);
        };
        // L1（2026-10-27）：在线缓存命中相位。只跑这个相位：
        // 打开缓存命中会话 → A/B 播 3 轮 → 帧号前进、播放始终可用、无「播放结束」。
        if (loop_online_cache_hit) {
          auto online_ready = std::make_shared<bool>(false);
          auto prev_jumps = std::make_shared<int>(-1);
          auto end_baseline = std::make_shared<int>(-1);
          schedule(300, [&window, bvid = online_m1_bvid, cid = online_m1_cid]() {
            const bool started = window.StartOnlineCacheHitForTest(bvid, cid);
            std::fprintf(stderr, "T4.2 loop_online start=%d bvid=%s cid=%lld\n",
                         started ? 1 : 0, bvid.toUtf8().constData(),
                         static_cast<long long>(cid));
            std::fflush(stderr);
          });
          schedule(1500, [&window, check, log_state, online_ready]() {
            const bool ready = window.WaitForOnlineReadyForTest(15000);
            const bool index_ok =
                ready && window.WaitForOnlineIndexFramesForTest(200, 12000);
            *online_ready = ready && index_ok;
            std::fprintf(stderr,
                         "T4.2 loop_online ready=%d cache_hit=%d index=%d "
                         "frames=%d\n",
                         ready ? 1 : 0, window.OnlineCacheHitForTest() ? 1 : 0,
                         index_ok ? 1 : 0, window.OnlineIndexFramesForTest());
            std::fflush(stderr);
            check("loop-online-cache-hit", ready && window.OnlineCacheHitForTest());
            check("loop-online-index-ready", index_ok);
            if (!*online_ready) {
              return;
            }
            window.GoToInputForTest(QStringLiteral("100"));
            window.SetLoopInForTest();
            window.GoToInputForTest(QStringLiteral("160"));
            window.SetLoopOutForTest();
            window.ToggleLoopForTest();
            window.GoToInputForTest(QStringLiteral("100"));
            window.StartPlaybackForTest();
            log_state("online-loop-start");
          });
          auto sample = [&window, check, log_state, online_ready, prev_jumps,
                         end_baseline](const char* tag, bool require_jumps) {
            if (!*online_ready) {
              return;
            }
            if (*end_baseline < 0) {
              *end_baseline = window.PlaybackEndCountForTest();
            }
            const qint64 frame = window.CurrentFrameForTest();
            const int jumps = window.LoopJumpCountForTest();
            log_state(tag);
            std::fprintf(stderr,
                         "T4.2 loop_online_probe %s frame=%lld jumps=%d "
                         "play_enabled=%d playing=%d end_count=%d loop=%s\n",
                         tag, static_cast<long long>(frame), jumps,
                         window.PlayActionEnabledForTest() ? 1 : 0,
                         window.IsPlayingForTest() ? 1 : 0,
                         window.PlaybackEndCountForTest(),
                         window.LoopStateForTest().toUtf8().constData());
            std::fflush(stderr);
            check("loop-online-playing", window.IsPlayingForTest());
            check("loop-online-play-enabled", window.PlayActionEnabledForTest());
            check("loop-online-no-reached-end",
                  window.PlaybackEndCountForTest() == *end_baseline);
            check("loop-online-frame-in-range", frame >= 100 && frame <= 160);
            if (*prev_jumps >= 0) {
              check("loop-online-jumps-advance", jumps > *prev_jumps);
            }
            if (require_jumps) {
              check("loop-online-3-rounds", jumps >= 3);
              check("loop-online-loop-on",
                    window.LoopStateForTest().contains(QStringLiteral("loop=1")));
            }
            *prev_jumps = jumps;
          };
          schedule(5000, [sample]() { sample("online-sample-1", false); });
          schedule(7000, [sample]() { sample("online-sample-2", false); });
          schedule(9000, [sample]() { sample("online-sample-3", true); });
          schedule(9500, [&window, check, log_state, online_ready]() {
            if (!*online_ready) {
              return;
            }
            window.TogglePlaybackForTest();  // Space 暂停
            log_state("online-paused");
            check("loop-online-pause", !window.IsPlayingForTest());
            check("loop-online-pause-play-enabled",
                  window.PlayActionEnabledForTest());
            window.TogglePlaybackForTest();  // Space 继续
            log_state("online-resumed");
            check("loop-online-resume", window.IsPlayingForTest());
          });
          schedule(10000, [&window, check]() {
            std::fprintf(stderr, "T4.2 loop_online loop_disabled=%d\n",
                         window.LoopDisabledCountForTest());
            std::fflush(stderr);
            check("loop-online-no-loop-disabled",
                  window.LoopDisabledCountForTest() == 0);
          });

          // ---- W1（2026-10-27）：在线缓存命中按 bvid+cid 记住工作区 A/B ----
          auto w1_online_field = [](const QString& state, const char* key) {
            const QString prefix = QString::fromLatin1(key) + QLatin1Char('=');
            for (const QString& token : state.split(QLatin1Char(' '))) {
              if (token.startsWith(prefix)) {
                return token.mid(prefix.size());
              }
            }
            return QString();
          };
          auto w1_online_log = [&window](const char* step) {
            std::fprintf(stderr, "W1 workrange_online %s %s\n", step,
                         window.WorkRangeStateForTest().toUtf8().constData());
            std::fflush(stderr);
          };
          auto w1_online_frames = std::make_shared<int>(0);
          schedule(10500, [&window, check, w1_online_field, w1_online_log,
                           w1_online_frames, bvid = online_m1_bvid,
                           cid = online_m1_cid]() {
            window.SetWorkRangeForTest(100, 160);
            window.FlushWorkRangeForTest();
            w1_online_log("saved");
            const QString state = window.WorkRangeStateForTest();
            *w1_online_frames = w1_online_field(state, "frames").toInt();
            const QString path = w1_online_field(state, "path");
            const QString expected_name =
                QStringLiteral("%1-%2.lapian-workrange.json")
                    .arg(bvid)
                    .arg(cid);
            std::fprintf(stderr,
                         "W1 workrange_online_saved has_record=%s a=%s b=%s "
                         "path=%s exists=%d playurl=%d dash=%d\n",
                         w1_online_field(state, "has_record")
                             .toUtf8()
                             .constData(),
                         w1_online_field(state, "a").toUtf8().constData(),
                         w1_online_field(state, "b").toUtf8().constData(),
                         path.toUtf8().constData(),
                         QFileInfo::exists(path) ? 1 : 0,
                         window.OnlinePlayUrlRequestsForTest(),
                         window.OnlineDashRequestsForTest());
            std::fflush(stderr);
            check("workrange-online-saved",
                  w1_online_field(state, "has_record") ==
                          QStringLiteral("1") &&
                      w1_online_field(state, "a") == QStringLiteral("100") &&
                      w1_online_field(state, "b") == QStringLiteral("160"));
            check("workrange-online-path",
                  path.endsWith(expected_name) && QFileInfo::exists(path));
            // 本会话循环仍开着（L1 相位开的）；重启后应回到“循环关”，证明开关不落盘。
            check("workrange-online-loop-on-before",
                  window.LoopStateForTest().contains(QStringLiteral("loop=1")));
            check("workrange-online-no-network-saved",
                  window.OnlinePlayUrlRequestsForTest() == 0 &&
                      window.OnlineDashRequestsForTest() == 0);
            // 新会话：重新开始同一个缓存命中视频。
            window.StartOnlineCacheHitForTest(bvid, cid);
          });
          schedule(11500, [&window, check, w1_online_field, w1_online_log,
                           w1_online_frames, bvid = online_m1_bvid,
                           cid = online_m1_cid, finish]() {
            const bool ready = window.WaitForOnlineReadyForTest(15000);
            const int wait_frames = std::max(200, *w1_online_frames);
            const bool index_ok =
                ready && window.WaitForOnlineIndexFramesForTest(wait_frames,
                                                                12000);
            // 恢复只在索引覆盖到记录帧数（或完整）后发生；再给一个事件循环。
            if (index_ok) {
              QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            }
            w1_online_log("reloaded");
            const QString state = window.WorkRangeStateForTest();
            const QString path = w1_online_field(state, "path");
            std::fprintf(stderr,
                         "W1 workrange_online_reload ready=%d cache_hit=%d "
                         "index=%d frames=%d has_record=%s a=%s b=%s loop=%s "
                         "playurl=%d dash=%d path=%s\n",
                         ready ? 1 : 0, window.OnlineCacheHitForTest() ? 1 : 0,
                         index_ok ? 1 : 0, window.OnlineIndexFramesForTest(),
                         w1_online_field(state, "has_record")
                             .toUtf8()
                             .constData(),
                         w1_online_field(state, "a").toUtf8().constData(),
                         w1_online_field(state, "b").toUtf8().constData(),
                         window.LoopStateForTest().toUtf8().constData(),
                         window.OnlinePlayUrlRequestsForTest(),
                         window.OnlineDashRequestsForTest(),
                         path.toUtf8().constData());
            std::fflush(stderr);
            check("workrange-online-save-reload-ready", ready && index_ok);
            check("workrange-online-save-reload-a",
                  w1_online_field(state, "a") == QStringLiteral("100"));
            check("workrange-online-save-reload-b",
                  w1_online_field(state, "b") == QStringLiteral("160"));
            check("workrange-online-save-reload-record",
                  w1_online_field(state, "has_record") == QStringLiteral("1"));
            check("workrange-online-save-reload-loop-off",
                  window.LoopStateForTest().contains(QStringLiteral("loop=0")) &&
                      window.LoopStateForTest().contains(
                          QStringLiteral("has_in=1")));
            check("workrange-online-no-network",
                  window.OnlinePlayUrlRequestsForTest() == 0 &&
                      window.OnlineDashRequestsForTest() == 0);
            std::fprintf(stderr,
                         "W1 workrange_online final bvid=%s cid=%lld\n",
                         bvid.toUtf8().constData(),
                         static_cast<long long>(cid));
            std::fflush(stderr);
            finish();
          });
          return;
        }

        schedule(400, [&window, log_state]() {
          window.GoToInputForTest(QStringLiteral("100"));
          window.SetLoopInForTest();
          log_state("a100");
        });
        schedule(700, [&window, log_state]() {
          window.GoToInputForTest(QStringLiteral("160"));
          window.SetLoopOutForTest();
          window.ToggleLoopForTest();
          window.GoToInputForTest(QStringLiteral("100"));
          log_state("loop-ready");
        });
        schedule(1000, [&window, log_state]() {
          window.StartPlaybackForTest();
          log_state("play-start");
        });
        schedule(10000, [&window, log_state, check]() {
          window.TogglePlaybackForTest();  // 暂停，检查 3 次循环后 A/B 不变
          log_state("after-3-loops");
          const QString state = window.LoopStateForTest();
          check("f4-a1-loop-jumps", window.LoopJumpCountForTest() >= 3);
          check("f4-a1-loop-state", state.contains(QStringLiteral("loop=1")) &&
                                        state.contains(QStringLiteral("a=100")) &&
                                        state.contains(QStringLiteral("b=160")));
        });
        schedule(10200, [&window, log_state]() {
          window.ToggleLoopForTest();  // F4-A3：关闭循环
          window.GoToInputForTest(QStringLiteral("100"));
          window.StartPlaybackForTest();
          log_state("loop-off-play");
        });
        schedule(13000, [&window, log_state, check]() {
          log_state("after-161");  // 应已越过 160/161，且不回到 A
          check("f4-a3-passes-b",
                window.CurrentFrameForTest() > 161 &&
                    window.LoopStateForTest().contains(QStringLiteral("loop=0")));
          window.TogglePlaybackForTest();
        });
        schedule(13200, [&window, log_state]() {
          log_state("paused-before-freeze");
          window.GoToInputForTest(QStringLiteral("100"));
          window.SetLoopOutForTest();  // A=B=100
          log_state("freeze-ready");
        });
        schedule(13400, [&window, log_state]() {
          window.ToggleLoopForTest();
          log_state("freeze-armed");
        });
        schedule(13600, [&window, log_state]() {
          window.StartPlaybackForTest();  // A=B：进入定帧
          log_state("freeze-start");
        });
        schedule(18600, [&window, log_state, check]() {
          log_state("freeze-5s");  // 5s 后仍应 frame=100、音频时钟无效
          const QString state = window.LoopPlaybackStateForTest();
          check("f4-a9-freeze-frame",
                window.CurrentFrameForTest() == 100 &&
                    state.contains(QStringLiteral("frozen=1")) &&
                    state.contains(QStringLiteral("playing=1")));
          check("f4-a9-freeze-audio-clock",
                state.contains(QStringLiteral("audio_clock=0")));
        });
        schedule(18900, [&window, log_state, check]() {
          window.StepForwardForTest();  // F4-A10：定帧中 PgDn 到 101
          log_state("after-step");
          check("f4-a10-step-101", window.CurrentFrameForTest() == 101 &&
                                       !window.IsPlayingForTest());
        });
        schedule(19200, [&window, log_state, check]() {
          window.StartPlaybackForTest();  // Space：回到 100 重新定住
          log_state("refreeze");
          check("f4-a10-refreeze", window.IsPlayingForTest() &&
                                       window.CurrentFrameForTest() == 100);
        });
        schedule(19500, [&window, log_state]() {
          log_state("refreeze-hold");
        });
        schedule(19700, [&window, log_state, check]() {
          window.ToggleLoopForTest();  // F4-A10：关闭循环，从 100 恢复播放
          log_state("loop-off-resume");
          check("f4-a10-loop-off-resumes", window.IsPlayingForTest());
        });
        schedule(20000, [&window, log_state, check]() {
          log_state("resume-after-loop-off");
          check("f4-a10-resume-advances",
                window.IsPlayingForTest() && window.CurrentFrameForTest() > 100);
          window.TogglePlaybackForTest();  // Space 暂停
        });
        schedule(20200, [&window, log_state, check]() {
          log_state("paused");
          check("f4-a10-paused", !window.IsPlayingForTest());
        });

        // ---- L1：结束态（Space 必须可用） ----
        auto end_baseline = std::make_shared<int>(-1);
        schedule(21000, [&window, log_state, end_baseline]() {
          const qint64 last = window.LastFrameIndexForTest();
          *end_baseline = window.PlaybackEndCountForTest();
          window.GoToInputForTest(
              QString::number(std::max<qint64>(0, last - 12)));
          window.StartPlaybackForTest();
          log_state("end-near");
        });
        schedule(23500, [&window, check, log_state, end_baseline]() {
          log_state("at-end");
          std::fprintf(stderr,
                       "T4.2 loop_end_probe end_count=%d baseline=%d "
                       "play_enabled=%d text=%s status=%s\n",
                       window.PlaybackEndCountForTest(), *end_baseline,
                       window.PlayActionEnabledForTest() ? 1 : 0,
                       window.PlayActionTextForTest().toUtf8().constData(),
                       window.StatusMessageForTest().toUtf8().constData());
          std::fflush(stderr);
          check("loop-end-reached", window.PlaybackEndCountForTest() > *end_baseline);
          check("loop-end-play-enabled", window.PlayActionEnabledForTest());
          check("loop-end-play-text",
                window.PlayActionTextForTest() == QStringLiteral("播放"));
          check("loop-end-status",
                window.StatusMessageForTest().contains(QStringLiteral("播放结束")));
          check("loop-end-not-playing", !window.IsPlayingForTest());
          window.PressSpaceKeyForTest();  // Space 必须能重新起播
        });
        schedule(24000, [&window, check, log_state]() {
          log_state("space-after-end");
          check("loop-end-space-restarts", window.IsPlayingForTest());
          check("loop-end-space-play-enabled", window.PlayActionEnabledForTest());
          window.PressSpaceKeyForTest();  // 暂停，准备失败注入相位
        });

        // ---- L1：循环跳转失败只关循环、不废播放（注入 3 次失败） ----
        auto fail_end_baseline = std::make_shared<int>(-1);
        schedule(24500, [&window, log_state, fail_end_baseline]() {
          window.GoToInputForTest(QStringLiteral("200"));
          window.SetLoopInForTest();
          window.GoToInputForTest(QStringLiteral("260"));
          window.SetLoopOutForTest();
          window.ToggleLoopForTest();
          window.GoToInputForTest(QStringLiteral("200"));
          window.StartPlaybackForTest();
          *fail_end_baseline = window.PlaybackEndCountForTest();
          log_state("fail-ready");
        });
        schedule(25000, [&window]() {
          window.SetFailLoopSeeksForTest(3);
          std::fprintf(stderr, "T4.2 loop_fail_inject n=3\n");
          std::fflush(stderr);
        });
        schedule(29000, [&window, check, log_state, fail_end_baseline]() {
          log_state("after-fail");
          std::fprintf(stderr,
                       "T4.2 loop_fail_probe disabled=%d playing=%d "
                       "play_enabled=%d end_count=%d baseline=%d loop_state=%s\n",
                       window.LoopDisabledCountForTest(),
                       window.IsPlayingForTest() ? 1 : 0,
                       window.PlayActionEnabledForTest() ? 1 : 0,
                       window.PlaybackEndCountForTest(), *fail_end_baseline,
                       window.LoopStateForTest().toUtf8().constData());
          std::fflush(stderr);
          check("loop-fail-disabled", window.LoopDisabledCountForTest() >= 1);
          check("loop-fail-loop-off",
                window.LoopStateForTest().contains(QStringLiteral("loop=0")));
          check("loop-fail-play-enabled", window.PlayActionEnabledForTest());
          check("loop-fail-still-playing", window.IsPlayingForTest());
          check("loop-fail-no-reached-end",
                window.PlaybackEndCountForTest() == *fail_end_baseline);
        });
        schedule(29500, [&window, check, log_state]() {
          window.PressSpaceKeyForTest();  // Space 暂停
          log_state("fail-paused");
          check("loop-fail-space-pauses", !window.IsPlayingForTest());
        });
        schedule(30000, [&window, check, log_state]() {
          window.PressSpaceKeyForTest();  // Space 继续
          log_state("fail-resumed");
          check("loop-fail-space-resumes", window.IsPlayingForTest());
          window.PressSpaceKeyForTest();  // 暂停
        });

        // ---- L1：A=B 定帧可见 + 音频停 + F4-A10 回归 ----
        schedule(30500, [&window, log_state]() {
          window.GoToInputForTest(QStringLiteral("100"));
          window.SetLoopInForTest();
          window.SetLoopOutForTest();  // A=B=100
          window.ToggleLoopForTest();
          window.StartPlaybackForTest();
          log_state("freeze2-start");
        });
        schedule(31000, [&window, check, log_state]() {
          log_state("freeze2-hold");
          const QString state = window.LoopPlaybackStateForTest();
          const QString status = window.StatusMessageForTest();
          std::fprintf(stderr, "T4.2 loop_freeze_probe status=%s state=%s\n",
                       status.toUtf8().constData(), state.toUtf8().constData());
          std::fflush(stderr);
          check("loop-freeze-hint", status.contains(QStringLiteral("定帧")));
          check("loop-freeze-frame", window.CurrentFrameForTest() == 100);
          check("loop-freeze-playing", window.IsPlayingForTest());
          check("loop-freeze-audio-paused",
                state.contains(QStringLiteral("audio_clock=0")) &&
                    !state.contains(QStringLiteral("audio_state=2")));
        });
        schedule(31500, [&window, check, log_state]() {
          window.StepForwardForTest();  // F4-A10：定帧中 PgDn 到 101
          log_state("freeze2-step");
          check("loop-freeze-step-leaves", !window.IsPlayingForTest() &&
                                               window.CurrentFrameForTest() == 101);
          check("loop-freeze-step-loop-on",
                window.LoopStateForTest().contains(QStringLiteral("loop=1")));
        });
        schedule(32000, [&window, log_state]() {
          window.PressSpaceKeyForTest();  // 回到 A 重新定住
          log_state("freeze2-refreeze");
        });
        schedule(32500, [&window, check, log_state]() {
          log_state("freeze2-refreeze-hold");
          check("loop-freeze-refreeze", window.IsPlayingForTest() &&
                                            window.CurrentFrameForTest() == 100);
          window.ToggleLoopForTest();  // F4-A10：L 关闭循环 → 从 100 恢复播放
          log_state("freeze2-loop-off");
        });
        schedule(33000, [&window, check, log_state]() {
          log_state("freeze2-resumed");
          check("loop-freeze-exit-resumes",
                window.IsPlayingForTest() && window.CurrentFrameForTest() > 100);
          window.PressSpaceKeyForTest();  // 暂停
        });
        schedule(33500, [&window, check, log_state]() {
          log_state("freeze2-paused");
          check("loop-freeze-exit-pauses", !window.IsPlayingForTest());
        });

        // ---- L1：循环预检拦截（gate=false）——不做同步 seek，只关循环 + 提示 ----
        auto gate_jumps = std::make_shared<int>(-1);
        auto gate_end = std::make_shared<int>(-1);
        schedule(33800, [&window, log_state, gate_jumps, gate_end]() {
          window.GoToInputForTest(QStringLiteral("300"));
          window.SetLoopInForTest();
          window.GoToInputForTest(QStringLiteral("340"));
          window.SetLoopOutForTest();
          window.ToggleLoopForTest();
          window.GoToInputForTest(QStringLiteral("300"));
          window.StartPlaybackForTest();
          *gate_jumps = window.LoopJumpCountForTest();
          *gate_end = window.PlaybackEndCountForTest();
          window.SetLoopSeekDeniedForTest(true);
          log_state("gate-denied");
        });
        schedule(35500, [&window, check, log_state, gate_jumps, gate_end]() {
          log_state("gate-blocked");
          std::fprintf(stderr,
                       "T4.2 loop_gate_probe disabled=%d playing=%d "
                       "play_enabled=%d jumps=%d baseline_jumps=%d end_count=%d "
                       "status=%s loop_state=%s\n",
                       window.LoopDisabledCountForTest(),
                       window.IsPlayingForTest() ? 1 : 0,
                       window.PlayActionEnabledForTest() ? 1 : 0,
                       window.LoopJumpCountForTest(), *gate_jumps,
                       window.PlaybackEndCountForTest(),
                       window.StatusMessageForTest().toUtf8().constData(),
                       window.LoopStateForTest().toUtf8().constData());
          std::fflush(stderr);
          check("loop-gate-disabled", window.LoopDisabledCountForTest() >= 2);
          check("loop-gate-loop-off",
                window.LoopStateForTest().contains(QStringLiteral("loop=0")));
          check("loop-gate-play-enabled", window.PlayActionEnabledForTest());
          check("loop-gate-still-playing", window.IsPlayingForTest());
          check("loop-gate-no-sync-seek",
                window.LoopJumpCountForTest() == *gate_jumps);
          check("loop-gate-no-reached-end",
                window.PlaybackEndCountForTest() == *gate_end);
          check("loop-gate-hint", window.StatusMessageForTest().contains(
                                      QStringLiteral("尚未缓存")));
          window.SetLoopSeekDeniedForTest(false);
          window.PressSpaceKeyForTest();  // 暂停
        });
        schedule(36000, [&window, check, log_state]() {
          log_state("gate-paused");
          check("loop-gate-paused", !window.IsPlayingForTest());
        });

        // ---- W1（2026-10-27）：工作区 A/B 按视频记忆（本地 sidecar） ----
        auto w1_field = [](const QString& state, const char* key) {
          const QString prefix = QString::fromLatin1(key) + QLatin1Char('=');
          for (const QString& token : state.split(QLatin1Char(' '))) {
            if (token.startsWith(prefix)) {
              return token.mid(prefix.size());
            }
          }
          return QString();
        };
        auto w1_log = [&window](const char* step) {
          std::fprintf(stderr, "W1 workrange %s %s\n", step,
                       window.WorkRangeStateForTest().toUtf8().constData());
          std::fflush(stderr);
        };
        auto w1_write_json = [](const QString& path, const QJsonObject& object) {
          QFile file(path);
          if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
          }
          const QByteArray bytes =
              QJsonDocument(object).toJson(QJsonDocument::Indented);
          const bool ok = file.write(bytes) == bytes.size();
          file.close();
          return ok;
        };
        auto w1_read_json = [](const QString& path) {
          QFile file(path);
          if (!file.open(QIODevice::ReadOnly)) {
            return QJsonObject();
          }
          return QJsonDocument::fromJson(file.readAll()).object();
        };
        auto w1_saves_before = std::make_shared<int>(0);
        auto w1_frames = std::make_shared<qint64>(0);

        // workrange-full-range-default：无记录 → A/B 为全片两头、循环关。
        schedule(36500, [&window, w1_log, file_path]() {
          window.ClearWorkRangeForTest();  // 清掉前面 F4 相位留下的记录
          w1_log("reset");
          window.OpenFile(file_path);  // 重开同一视频 = 新会话
        });
        schedule(37400, [&window, check, w1_field, w1_log, w1_frames]() {
          w1_log("default-open");
          const QString state = window.WorkRangeStateForTest();
          *w1_frames = w1_field(state, "frames").toLongLong();
          const QString path = w1_field(state, "path");
          std::fprintf(stderr,
                       "W1 workrange_default has_record=%s a=%s b=%s frames=%lld "
                       "loop_state=%s exists=%d\n",
                       w1_field(state, "has_record").toUtf8().constData(),
                       w1_field(state, "a").toUtf8().constData(),
                       w1_field(state, "b").toUtf8().constData(),
                       static_cast<long long>(*w1_frames),
                       window.LoopStateForTest().toUtf8().constData(),
                       QFileInfo::exists(path) ? 1 : 0);
          std::fflush(stderr);
          check("workrange-full-range-default-record",
                w1_field(state, "has_record") == QStringLiteral("0"));
          check("workrange-full-range-default-a",
                w1_field(state, "a") == QStringLiteral("0"));
          check("workrange-full-range-default-b",
                w1_field(state, "b") == QString::number(*w1_frames - 1));
          check("workrange-full-range-default-loop-off",
                window.LoopStateForTest().contains(QStringLiteral("has_in=0")) &&
                    window.LoopStateForTest().contains(QStringLiteral("loop=0")));
          check("workrange-full-range-default-no-file", !QFileInfo::exists(path));
          // 设为 A=100/B=160 并显式 flush。
          window.SetWorkRangeForTest(100, 160);
          window.FlushWorkRangeForTest();
        });
        schedule(38100,
                 [&window, check, w1_field, w1_log, w1_saves_before,
                  w1_read_json, file_path]() {
          w1_log("saved");
          const QString state = window.WorkRangeStateForTest();
          const QString path = w1_field(state, "path");
          std::fprintf(stderr,
                       "W1 workrange_saved a=%s b=%s has_record=%s saves=%s "
                       "path=%s exists=%d\n",
                       w1_field(state, "a").toUtf8().constData(),
                       w1_field(state, "b").toUtf8().constData(),
                       w1_field(state, "has_record").toUtf8().constData(),
                       w1_field(state, "saves").toUtf8().constData(),
                       path.toUtf8().constData(),
                       QFileInfo::exists(path) ? 1 : 0);
          std::fflush(stderr);
          *w1_saves_before = window.WorkRangeSaveCountForTest();
          check("workrange-save-state",
                w1_field(state, "has_record") == QStringLiteral("1") &&
                    w1_field(state, "a") == QStringLiteral("100") &&
                    w1_field(state, "b") == QStringLiteral("160"));
          check("workrange-save-file", QFileInfo::exists(path));
          check("workrange-save-json",
                w1_read_json(path).value(QStringLiteral("in")).toInteger() == 100 &&
                    w1_read_json(path).value(QStringLiteral("out")).toInteger() ==
                        160);
          window.OpenFile(file_path);  // 关闭再打开同一视频
        });
        schedule(39000,
                 [&window, check, w1_field, w1_log, w1_saves_before]() {
                   w1_log("reloaded");
                   const QString state = window.WorkRangeStateForTest();
                   const QString loop_state = window.LoopStateForTest();
                   std::fprintf(stderr,
                                "W1 workrange_reloaded a=%s b=%s has_record=%s "
                                "loop_state=%s status=%s\n",
                                w1_field(state, "a").toUtf8().constData(),
                                w1_field(state, "b").toUtf8().constData(),
                                w1_field(state, "has_record").toUtf8().constData(),
                                loop_state.toUtf8().constData(),
                                window.StatusMessageForTest().toUtf8().constData());
                   std::fflush(stderr);
                   check("workrange-save-reload-a",
                         w1_field(state, "a") == QStringLiteral("100"));
                   check("workrange-save-reload-b",
                         w1_field(state, "b") == QStringLiteral("160"));
                   check("workrange-save-reload-record",
                         w1_field(state, "has_record") == QStringLiteral("1"));
                   check("workrange-save-reload-loop-off",
                         loop_state.contains(QStringLiteral("loop=0")) &&
                             loop_state.contains(QStringLiteral("has_in=1")));
                   *w1_saves_before = window.WorkRangeSaveCountForTest();
                   // 连续 5 次改 A/B，不手动 flush：500ms 防抖只应写 1 次。
                   for (int i = 0; i < 5; ++i) {
                     window.SetWorkRangeForTest(101 + i, 161 + i);
                   }
                 });
        schedule(39750,
                 [&window, check, w1_field, w1_log, w1_saves_before,
                  w1_read_json, w1_write_json, file_path]() {
          w1_log("debounce");
          const QString state = window.WorkRangeStateForTest();
          const int saves = window.WorkRangeSaveCountForTest();
          std::fprintf(stderr,
                       "W1 workrange_debounce saves=%d baseline=%d delta=%d "
                       "a=%s b=%s\n",
                       saves, *w1_saves_before, saves - *w1_saves_before,
                       w1_field(state, "a").toUtf8().constData(),
                       w1_field(state, "b").toUtf8().constData());
          std::fflush(stderr);
          const QString path = w1_field(state, "path");
          const QJsonObject json = w1_read_json(path);
          check("workrange-debounce-save-count",
                saves - *w1_saves_before == 1);
          check("workrange-debounce-last-range",
                w1_field(state, "a") == QStringLiteral("105") &&
                    w1_field(state, "b") == QStringLiteral("165"));
          check("workrange-debounce-json",
                json.value(QStringLiteral("in")).toInteger() == 105 &&
                    json.value(QStringLiteral("out")).toInteger() == 165);
          // 指纹不符：先写错 size。
          QJsonObject tampered = json;
          tampered.insert(QStringLiteral("size"),
                          json.value(QStringLiteral("size")).toInteger() + 7);
          check("workrange-fingerprint-write-size",
                w1_write_json(path, tampered));
          window.OpenFile(file_path);
        });
        schedule(40500,
                 [&window, check, w1_field, w1_log, w1_frames,
                  w1_write_json, w1_read_json, file_path]() {
                   w1_log("bad-size");
                   const QString state = window.WorkRangeStateForTest();
                   std::fprintf(stderr, "W1 workrange_bad_size %s\n",
                                state.toUtf8().constData());
                   std::fflush(stderr);
                   check("workrange-fingerprint-mismatch-size",
                         w1_field(state, "has_record") == QStringLiteral("0") &&
                             w1_field(state, "a") == QStringLiteral("0") &&
                             w1_field(state, "b") ==
                                 QString::number(*w1_frames - 1));
                   // 再写错 mtime（差 60s > 2000ms）。
                   const QString path = w1_field(state, "path");
                   QJsonObject tampered = w1_read_json(path);
                   tampered.insert(QStringLiteral("size"),
                                   tampered.value(QStringLiteral("size"))
                                           .toInteger() -
                                       7);
                   tampered.insert(
                       QStringLiteral("mtime_ms"),
                       tampered.value(QStringLiteral("mtime_ms")).toInteger() -
                           60'000);
                   check("workrange-fingerprint-write-mtime",
                         w1_write_json(path, tampered));
                   window.OpenFile(file_path);
                 });
        schedule(41400, [&window, check, w1_field, w1_log, w1_frames]() {
          w1_log("bad-mtime");
          const QString state = window.WorkRangeStateForTest();
          std::fprintf(stderr, "W1 workrange_bad_mtime %s\n",
                       state.toUtf8().constData());
          std::fflush(stderr);
          check("workrange-fingerprint-mismatch-mtime",
                w1_field(state, "has_record") == QStringLiteral("0") &&
                    w1_field(state, "a") == QStringLiteral("0") &&
                    w1_field(state, "b") == QString::number(*w1_frames - 1));
          // 恢复 A/B 后走 Shift+L（清除）路径：文件应被删除。
          window.SetWorkRangeForTest(120, 180);
          window.FlushWorkRangeForTest();
        });
        schedule(42200, [&window, check, w1_field, w1_log]() {
          w1_log("clear-ready");
          const QString state = window.WorkRangeStateForTest();
          check("workrange-clear-saved",
                w1_field(state, "has_record") == QStringLiteral("1") &&
                    QFileInfo::exists(w1_field(state, "path")));
          window.ClearLoopForTest();  // Shift+L
        });
        schedule(42800, [&window, check, w1_field, w1_log, file_path]() {
          w1_log("cleared");
          const QString state = window.WorkRangeStateForTest();
          const QString path = w1_field(state, "path");
          std::fprintf(stderr,
                       "W1 workrange_cleared exists=%d loop_state=%s status=%s\n",
                       QFileInfo::exists(path) ? 1 : 0,
                       window.LoopStateForTest().toUtf8().constData(),
                       window.StatusMessageForTest().toUtf8().constData());
          std::fflush(stderr);
          check("workrange-clear-removes", !QFileInfo::exists(path));
          check("workrange-clear-loop-off",
                window.LoopStateForTest().contains(QStringLiteral("has_in=0")) &&
                    window.LoopStateForTest().contains(QStringLiteral("loop=0")));
          window.OpenFile(file_path);
        });
        schedule(43700,
                 [&window, check, w1_field, w1_log, w1_frames, file_b]() {
                   w1_log("cleared-reload");
                   const QString state = window.WorkRangeStateForTest();
                   std::fprintf(stderr, "W1 workrange_cleared_reload %s\n",
                                state.toUtf8().constData());
                   std::fflush(stderr);
                   check("workrange-clear-reload-full-range",
                         w1_field(state, "has_record") == QStringLiteral("0") &&
                             w1_field(state, "a") == QStringLiteral("0") &&
                             w1_field(state, "b") ==
                                 QString::number(*w1_frames - 1));
                   check("workrange-clear-reload-loop-off",
                         window.LoopStateForTest().contains(
                             QStringLiteral("has_in=0")));
                   if (!file_b.isEmpty()) {
                     // F8-A4“换视频不串”：A 存 50..90 后打开 B再回来。
                     window.SetWorkRangeForTest(50, 90);
                     window.FlushWorkRangeForTest();
                     window.OpenFile(file_b);
                   }
                 });
        auto w1_b_range = std::make_shared<std::pair<qint64, qint64>>(0, 0);
        schedule(44400, [&window, check, w1_field, w1_log, w1_b_range, file_b,
                         file_path, finish]() {
          if (file_b.isEmpty()) {
            finish();
            return;
          }
          w1_log("other-video-opened");
          const QString state = window.WorkRangeStateForTest();
          check("workrange-switch-video-b-default",
                w1_field(state, "has_record") == QStringLiteral("0"));
          // B 自己存一个区间（按 B 的帧数取 1/4..1/2），再回到 A 验证 A 仍是 50..90。
          const qint64 frames = w1_field(state, "frames").toLongLong();
          const qint64 b_in = std::max<qint64>(1, frames / 4);
          const qint64 b_out = std::max<qint64>(b_in, frames / 2);
          *w1_b_range = {b_in, b_out};
          window.ClearWorkRangeForTest();
          window.SetWorkRangeForTest(static_cast<int>(b_in),
                                     static_cast<int>(b_out));
          window.FlushWorkRangeForTest();
          window.OpenFile(file_path);
        });
        schedule(45200, [&window, check, w1_field, w1_log, file_b, file_path]() {
          if (file_b.isEmpty()) {
            return;
          }
          w1_log("back-to-a");
          const QString state = window.WorkRangeStateForTest();
          check("workrange-switch-video-a-kept",
                w1_field(state, "has_record") == QStringLiteral("1") &&
                    w1_field(state, "a") == QStringLiteral("50") &&
                    w1_field(state, "b") == QStringLiteral("90"));
          window.OpenFile(file_b);
        });
        schedule(46000, [&window, check, w1_field, w1_log, w1_b_range, file_b,
                         finish]() {
          if (file_b.isEmpty()) {
            return;
          }
          w1_log("other-video-reload");
          const QString state = window.WorkRangeStateForTest();
          check("workrange-switch-video-b-kept",
                w1_field(state, "has_record") == QStringLiteral("1") &&
                    w1_field(state, "a") ==
                        QString::number(w1_b_range->first) &&
                    w1_field(state, "b") ==
                        QString::number(w1_b_range->second));
          finish();
        });
      }
      if (loop_end_test) {
        // T7.2.1：A=last-20、B=last，B 是文件末帧；必须持续回 A，不能停在 B。
        auto failures = std::make_shared<int>(0);
        auto last_frame = std::make_shared<qint64>(-1);
        auto check = [failures](const char* name, bool ok) {
          if (!ok) {
            ++(*failures);
          }
          std::fprintf(stderr, "T7.2.1 loop_end_check %s ok=%d\n", name, ok ? 1 : 0);
          std::fflush(stderr);
        };
        auto schedule = [&window](int ms, auto fn) {
          QTimer::singleShot(ms, Qt::PreciseTimer, &window, std::move(fn));
        };
        schedule(300, [&window, last_frame, check]() {
          const qint64 last = window.LastFrameIndexForTest();
          const qint64 a = std::max<qint64>(0, last - 20);
          *last_frame = last;
          window.GoToInputForTest(QString::number(a));
          window.SetLoopInForTest();
          window.GoToInputForTest(QString::number(last));
          window.SetLoopOutForTest();
          window.ToggleLoopForTest();
          const QString loop_state = window.LoopStateForTest();
          std::fprintf(stderr, "T7.2.1 loop_end_ready %s\n",
                       loop_state.toUtf8().constData());
          std::fflush(stderr);
          check("loop-ready", loop_state.contains(QStringLiteral("loop=1")) &&
                                  loop_state.contains(QStringLiteral("b=") +
                                                      QString::number(last)));
          window.GoToInputForTest(QString::number(a));
          window.StartPlaybackForTest();
        });
        schedule(5000, [&window, failures, last_frame, check]() {
          const int jumps = window.LoopJumpCountForTest();
          const qint64 frame = window.CurrentFrameForTest();
          const qint64 last = *last_frame;
          const QString state = window.LoopPlaybackStateForTest();
          std::fprintf(stderr,
                       "T7.2.1 loop_end_result jumps=%d frame=%lld last=%lld state=%s\n",
                       jumps, static_cast<long long>(frame),
                       static_cast<long long>(last), state.toUtf8().constData());
          std::fflush(stderr);
          check("loop-jumps-at-least-3", jumps >= 3);
          check("still-playing", state.contains(QStringLiteral("playing=1")));
          check("loop-still-on", state.contains(QStringLiteral("loop=1")));
          check("frame-in-range", frame >= 0 && frame <= last);
          QCoreApplication::exit(*failures == 0 ? 0 : 1);
        });
      }
      if (readout_edit_test) {
        RunReadoutEditTest(window);
      }
      if (mark_test) {
        // T5.2：101 个标记（0–100）、重开、点击跳转、100 标记操作与性能、
        // 删除持久化、空文件删除、损坏 sidecar 警告、F8-A3 文件隔离。
        // 3C 新增 2 条：折叠不改数据/选中；折叠态 FocusNoteEditor 自动展开到标记 tab。
        const QString mark_a = file_path;
        const QString mark_b = file_b;
        const QString sidecar_a = mark_a + QStringLiteral(".lapian.json");
        const QString sidecar_b = mark_b + QStringLiteral(".lapian.json");
        auto failures = std::make_shared<int>(0);
        auto check = [failures](const char* name, bool ok) {
          if (!ok) {
            ++(*failures);
          }
          std::fprintf(stderr, "T5.2 mark_check %s ok=%d\n", name, ok ? 1 : 0);
          std::fflush(stderr);
        };
        auto schedule = [&window](int ms, auto fn) {
          QTimer::singleShot(ms, Qt::PreciseTimer, &window, std::move(fn));
        };
        auto log_state = [&window](const char* step) {
          const QByteArray state = window.MarkStateForTest().toUtf8();
          std::fprintf(stderr, "T5.2 mark_test %s %s\n", step, state.constData());
          std::fflush(stderr);
        };
        auto log_storage = [&window]() {
          std::fprintf(stderr, "T5.2 mark_storage path=%s fallback=%d\n",
                       window.MarkStoragePathForTest().toUtf8().constData(),
                       window.MarkUsingFallbackForTest() ? 1 : 0);
          std::fflush(stderr);
        };
        auto log_inspector = [&window](const char* step) {
          std::fprintf(stderr, "T5.2 mark_inspector %s %s\n", step,
                       window.InspectorStateForTest().toUtf8().constData());
          std::fflush(stderr);
        };

        schedule(400, [&window, sidecar_a, log_state, log_storage]() {
          log_state("a-opened");
          log_storage();
          std::fprintf(stderr, "T5.2 mark_sidecar_a exists_before=%d\n",
                       QFileInfo::exists(sidecar_a) ? 1 : 0);
          std::fflush(stderr);
          for (int frame = 0; frame <= 100; ++frame) {
            window.GoToInputForTest(QString::number(frame));
            window.AddMarkForTest();
          }
          log_state("a-added-101");
          std::fprintf(stderr, "T5.2 mark_sidecar_a exists_after=%d\n",
                       QFileInfo::exists(sidecar_a) ? 1 : 0);
          std::fflush(stderr);
        });
        schedule(4000, [&window, mark_a, log_state]() {
          window.OpenFile(mark_a);
          log_state("a-reopened");
          std::fprintf(stderr, "T5.2 mark_reopen count=%d first=%lld last=%lld\n",
                       window.MarkCountForTest(),
                       static_cast<long long>(window.MarkFrameAtForTest(0)),
                       static_cast<long long>(
                           window.MarkFrameAtForTest(window.MarkCountForTest() - 1)));
          std::fflush(stderr);
        });
        schedule(4400, [&window]() { window.ActivateMarkForTest(100); });
        schedule(4700, [&window, log_state]() { log_state("click-frame100"); });
        // 3C 用例 ①：折叠检查器后 MarkCount 与选中 id 不变（只 hide，不重建数据）。
        // 3C 用例 ②：折叠态 FocusNoteEditorForTest() 自动展开并切回“标记”tab。
        // 两条只对 v2 壳有意义（旧壳没有检查器 tab/折叠），旧壳跳过、断言一行不改。
        schedule(4800, [&window, check, log_inspector]() {
          if (!window.UiV2ForTest()) {
            return;
          }
          const QString before = window.MarkStateForTest();
          window.SetInspectorCollapsedForTest(true);
          const QString collapsed = window.MarkStateForTest();
          log_inspector("collapsed");
          check("collapse-inspector", window.InspectorCollapsedForTest());
          check("collapse-keeps-marks", before == collapsed);
          const bool focused = window.FocusNoteEditorForTest();
          log_inspector("focus-after-collapsed");
          const QString expanded = window.InspectorStateForTest();
          check("focus-note-auto-expand",
                focused && !window.InspectorCollapsedForTest() &&
                    expanded.contains(QStringLiteral("collapsed=0")) &&
                    expanded.contains(QStringLiteral("tab=marks")));
        });
        schedule(4900, [&window]() { window.ResetMarkPerfSamplesForTest(); });
        for (int i = 0; i < 20; ++i) {
          schedule(5000 + i * 150, [&window, i]() { window.ActivateMarkForTest(i * 5); });
        }
        schedule(8200, [&window, log_state]() {
          log_state("after-20-clicks");
          std::fprintf(stderr, "T5.2 mark_perf %s\n",
                       window.MarkPerfSummaryForTest().toUtf8().constData());
          std::fflush(stderr);
          const QString dir = QDir::currentPath() + QStringLiteral("/build");
          QDir().mkpath(dir);
          const bool ok =
              window.grab().save(dir + QStringLiteral("/t5.2-marks-ui.png"));
          std::fprintf(stderr, "T5.2 mark_shot ui ok=%d\n", ok ? 1 : 0);
          std::fflush(stderr);
        });
        schedule(8500, [&window, log_state]() {
          window.DeleteMarkForTest(42);
          log_state("deleted-42");
        });
        schedule(8900, [&window, mark_a, log_state]() {
          window.OpenFile(mark_a);
          log_state("a-reopened-after-delete");
        });
        schedule(9400, [&window, mark_b, sidecar_b, log_state]() {
          QFile corrupt(sidecar_b);
          if (corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            corrupt.write("{ broken sidecar");
            corrupt.close();
          }
          window.OpenFile(mark_b);
          log_state("b-opened-corrupt-with-a-100");
          std::fprintf(stderr, "T5.2 mark_b sidecar_bak=%d\n",
                       QFileInfo::exists(sidecar_b + QStringLiteral(".bak")) ? 1 : 0);
          std::fflush(stderr);
        });
        schedule(9900, [&window, log_state]() {
          log_state("b-after-corrupt");
          const QString dir = QDir::currentPath() + QStringLiteral("/build");
          QDir().mkpath(dir);
          const bool ok =
              window.grab().save(dir + QStringLiteral("/t5.2-marks-warning.png"));
          std::fprintf(stderr, "T5.2 mark_shot warning ok=%d\n", ok ? 1 : 0);
          std::fflush(stderr);
        });
        schedule(10000, [&window, mark_a, log_state]() {
          window.OpenFile(mark_a);
          log_state("a-after-switching-to-b");
          std::fprintf(stderr, "T5.2 mark_f8a3 a_count_after_switch=%d\n",
                       window.MarkCountForTest());
          std::fflush(stderr);
        });
        schedule(10500, [&window, sidecar_a, log_state]() {
          while (window.MarkCountForTest() > 0) {
            window.DeleteMarkForTest(0);
          }
          log_state("deleted-all");
          std::fprintf(stderr, "T5.2 mark_sidecar_a exists_after_delete_all=%d\n",
                       QFileInfo::exists(sidecar_a) ? 1 : 0);
          std::fflush(stderr);
        });
        schedule(11000, [&window, mark_b, log_state]() {
          window.OpenFile(mark_b);
          log_state("b-final-empty");
        });
        QTimer::singleShot(11500, Qt::PreciseTimer, &app,
                           [&app, failures]() {
                             app.exit(*failures == 0 ? 0 : 1);
                           });
      }
      if (note_test) {
        // T5.3：编辑 → 直接关窗 → 重开（F8-A2）、空备注、覆盖保存、焦点隔离、
        // 文件隔离；每次提交都走 MarkStore::SetMarkText + Save。
        const QString note_a = file_path;
        const QString note_b = file_b;
        const QString sidecar_a = note_a + QStringLiteral(".lapian.json");
        const QString sidecar_b = note_b + QStringLiteral(".lapian.json");
        auto failures = std::make_shared<int>(0);
        auto schedule = [&window](int ms, auto fn) {
          QTimer::singleShot(ms, Qt::PreciseTimer, &window, std::move(fn));
        };
        auto check = [failures](const char* name, bool ok) {
          if (!ok) {
            ++(*failures);
          }
          std::fprintf(stderr, "T5.3 note_check %s ok=%d\n", name, ok ? 1 : 0);
          std::fflush(stderr);
        };
        auto log_text = [&window](const char* step) {
          const QByteArray text = window.MarkTextForTest(0).toUtf8();
          std::fprintf(stderr, "T5.3 note_test %s count=%d %s\n", step,
                       window.MarkCountForTest(), text.constData());
          std::fflush(stderr);
        };
        // 在 QMessageBox::question 的嵌套事件循环里点按钮；默认按钮必须是 No。
        auto answer_delete_dialog =
            [check](QMessageBox::StandardButton answer) {
              auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
              if (box == nullptr) {
                check("confirm-dialog-found", false);
                return;
              }
              const QAbstractButton* default_button = box->defaultButton();
              const QAbstractButton* no_button = box->button(QMessageBox::No);
              const bool default_no =
                  default_button != nullptr && default_button == no_button;
              std::fprintf(stderr,
                           "T5.3 note_confirm dialog=%d default_no=%d answer=%d\n",
                           box->windowTitle().contains(QStringLiteral("删除")) ? 1 : 0,
                           default_no ? 1 : 0, static_cast<int>(answer));
              std::fflush(stderr);
              check("confirm-default-no", default_no);
              QAbstractButton* target = box->button(answer);
              if (target == nullptr) {
                check("confirm-button-found", false);
                box->done(answer);
                return;
              }
              target->click();
            };

        schedule(500, [&window, sidecar_a, log_text]() {
          window.GoToInputForTest(QStringLiteral("100"));
          window.AddMarkForTest();
          log_text("added");
          std::fprintf(stderr, "T5.3 note_sidecar exists=%d\n",
                       QFileInfo::exists(sidecar_a) ? 1 : 0);
          std::fflush(stderr);
        });
        schedule(800, [&window, log_text, check]() {
          window.SetMarkNoteForTest(0, QStringLiteral("转场A"),
                                    QStringLiteral("第一行\n第二行"));
          log_text("pending-before-close");
          check("not-committed-before-close",
                window.MarkTextForTest(0) == QStringLiteral("frame=100 label=帧 100 note="));
          const bool focused = window.FocusNoteEditorForTest();
          const bool playing_before =
              window.MarkStateForTest().contains(QStringLiteral("playing=0"));
          window.TogglePlaybackForTest();  // spec §3.7：文本框聚焦时 Space 不播放
          const bool playing_after =
              window.MarkStateForTest().contains(QStringLiteral("playing=0"));
          std::fprintf(stderr,
                       "T5.3 note_focus focused=%d playing_before=%d playing_after=%d\n",
                       focused ? 1 : 0, playing_before ? 1 : 0, playing_after ? 1 : 0);
          std::fflush(stderr);
          check("focus-blocks-space", focused && playing_after);
          const QString shot_dir = QDir::currentPath() + QStringLiteral("/build");
          QDir().mkpath(shot_dir);
          const bool shot =
              window.grab().save(shot_dir + QStringLiteral("/t5.3-note-ui.png"));
          std::fprintf(stderr, "T5.3 note_shot ui ok=%d\n", shot ? 1 : 0);
          std::fflush(stderr);
          const bool closed = window.CloseForTest();
          std::fprintf(stderr, "T5.3 note_close accepted=%d\n", closed ? 1 : 0);
          std::fflush(stderr);
          check("close-accepted", closed);
          log_text("after-close");
        });
        schedule(1100, [&window, note_a, log_text, check]() {
          window.OpenFile(note_a);
          log_text("reopened-after-close");
          check("f8a2-note-persisted",
                window.MarkTextForTest(0) ==
                    QStringLiteral("frame=100 label=转场A note=第一行\\n第二行"));
        });
        schedule(1200, [&window]() { window.ActivateMarkForTest(0); });
        // T7.2：ActivateMark 按 T5.2 语义把精确解码放到下一个事件循环；前面
        // OpenFile 的同步索引可能把 1200/1350 两个定时器一起拖到索引之后。
        // 固定 150ms 等待会与 0ms 跳转事件竞争，这里改成有界轮询。
        schedule(1350, [&window, check]() {
          auto tries = std::make_shared<int>(0);
          auto poll = std::make_shared<std::function<void()>>();
          *poll = [&window, check, tries, poll]() {
            const QString state = window.MarkStateForTest();
            const bool done = state.contains(QStringLiteral("frame=100")) ||
                              ++(*tries) >= 20;
            if (!done) {
              QTimer::singleShot(25, &window, [poll]() { (*poll)(); });
              return;
            }
            std::fprintf(stderr, "T5.3 note_click %s\n", state.toUtf8().constData());
            std::fflush(stderr);
            check("f6a1-click-jump-pause",
                  state.contains(QStringLiteral("frame=100")) &&
                      state.contains(QStringLiteral("playing=0")));
          };
          (*poll)();
        });
        schedule(1500, [&window, log_text, check]() {
          window.SetMarkNoteForTest(0, QString(), QString());
          window.CommitNoteForTest();
          log_text("empty-saved");
          check("empty-note-stored",
                window.MarkTextForTest(0) == QStringLiteral("frame=100 label= note="));
        });
        schedule(1800, [&window, note_a, log_text, check]() {
          window.OpenFile(note_a);
          log_text("reopened-empty");
          check("empty-note-persisted",
                window.MarkTextForTest(0) == QStringLiteral("frame=100 label= note="));
        });
        schedule(2200, [&window, log_text]() {
          window.SetMarkNoteForTest(0, QStringLiteral("转场B"),
                                    QStringLiteral("第二版\n覆盖保存"));
          const bool closed = window.CloseForTest();
          std::fprintf(stderr, "T5.3 note_close overwrite accepted=%d\n",
                       closed ? 1 : 0);
          std::fflush(stderr);
          log_text("overwrite-after-close");
        });
        schedule(2500, [&window, note_a, log_text, check]() {
          window.OpenFile(note_a);
          log_text("reopened-overwrite");
          check("overwrite-persisted",
                window.MarkTextForTest(0) ==
                    QStringLiteral("frame=100 label=转场B note=第二版\\n覆盖保存"));
        });
        schedule(2800, [&window, log_text, check]() {
          window.ResetNotePerfSamplesForTest();
          for (int i = 0; i < 20; ++i) {
            window.SetMarkNoteForTest(0, QStringLiteral("标签%1").arg(i),
                                      QStringLiteral("备注%1").arg(i));
            window.CommitNoteForTest();
          }
          const QByteArray perf = window.NotePerfSummaryForTest().toUtf8();
          std::fprintf(stderr, "T5.3 note_perf %s\n", perf.constData());
          std::fflush(stderr);
          check("perf-samples",
                window.NotePerfSummaryForTest().contains(QStringLiteral("n=20")));
          log_text("after-20-commits");
          check("last-commit-stored",
                window.MarkTextForTest(0) ==
                    QStringLiteral("frame=100 label=标签19 note=备注19"));
        });
        schedule(3100, [&window, note_a, log_text, check]() {
          window.OpenFile(note_a);
          log_text("reopened-after-20");
          check("last-commit-persisted",
                window.MarkTextForTest(0) ==
                    QStringLiteral("frame=100 label=标签19 note=备注19"));
        });
        // F6-A2：真实删除确认对话框路径：取消不删，确认才删。
        schedule(3150, [&window]() { window.DeleteMarkWithConfirmForTest(0); });
        schedule(3200, [answer_delete_dialog]() {
          answer_delete_dialog(QMessageBox::No);
        });
        schedule(3300, [&window, log_text, check]() {
          log_text("after-confirm-no");
          check("confirm-no-keeps-mark", window.MarkCountForTest() == 1);
        });
        schedule(3350, [&window]() { window.DeleteMarkWithConfirmForTest(0); });
        schedule(3400, [answer_delete_dialog]() {
          answer_delete_dialog(QMessageBox::Yes);
        });
        schedule(3500, [&window, sidecar_a, log_text, check]() {
          log_text("after-confirm-yes");
          check("confirm-yes-removes-mark", window.MarkCountForTest() == 0);
          check("confirm-yes-removes-sidecar", !QFileInfo::exists(sidecar_a));
        });
        schedule(3800, [&window, note_b, sidecar_b, log_text, check]() {
          if (note_b.isEmpty()) {
            check("file-b-skipped", true);
            return;
          }
          window.OpenFile(note_b);
          log_text("b-opened");
          check("f8a3-b-empty", window.MarkCountForTest() == 0);
          check("f8a3-b-no-file", !QFileInfo::exists(sidecar_b));
        });
        QTimer::singleShot(4100, Qt::PreciseTimer, &app, [failures]() {
          QCoreApplication::exit(*failures == 0 ? 0 : 1);
        });
      }
      if (snapshot_test) {
        // T5.4 F6-A3/A4/A5/A7：V9 帧 100 连按 3 次、播放中按键、目录回退、
        // 旋转素材（--file-b）；断言 PNG 尺寸/文件名/不覆盖/播放不中断。
        const QString shot_video_a = file_path;
        const QString shot_video_b = file_b;
        const QString shot_dir_a =
            QFileInfo(shot_video_a).absolutePath() + QStringLiteral("/snapshots");
        const QString pictures_root = snapshot_pictures_root;
        auto failures = std::make_shared<int>(0);
        auto schedule = [&window](int ms, auto fn) {
          QTimer::singleShot(ms, Qt::PreciseTimer, &window, std::move(fn));
        };
        auto check = [failures](const char* name, bool ok) {
          if (!ok) {
            ++(*failures);
          }
          std::fprintf(stderr, "T5.4 snapshot_check %s ok=%d\n", name, ok ? 1 : 0);
          std::fflush(stderr);
        };
        auto log_state = [&window](const char* step) {
          const QByteArray state = window.SnapshotStateForTest().toUtf8();
          std::fprintf(stderr, "T5.4 snapshot_state %s %s\n", step, state.constData());
          std::fflush(stderr);
        };
        auto check_png = [check](const QString& path, const QSize& expected,
                                 const char* name) {
          const QImage image(path);
          check((std::string(name) + "-loads").c_str(), !image.isNull());
          check((std::string(name) + "-size").c_str(), image.size() == expected);
          check((std::string(name) + "-f100").c_str(),
                QFileInfo(path).fileName().contains(QStringLiteral("f100")));
          std::fprintf(stderr,
                       "T5.4 snapshot_png %s path=%s actual=%dx%d expected=%dx%d\n", name,
                       path.toUtf8().constData(), image.width(), image.height(),
                       expected.width(), expected.height());
          std::fflush(stderr);
        };

        schedule(500, [&window, shot_dir_a, log_state]() {
          QDir(shot_dir_a).removeRecursively();
          window.GoToInputForTest(QStringLiteral("100"));
          const QSize display = window.SnapshotDisplaySizeForTest();
          std::fprintf(stderr, "T5.4 snapshot_source size=%dx%d state=%s\n",
                       display.width(), display.height(),
                       window.SnapshotStateForTest().toUtf8().constData());
          std::fflush(stderr);
          log_state("before-3");
          window.ResetSnapshotPerfSamplesForTest();
          window.CaptureSnapshotForTest();
          window.CaptureSnapshotForTest();
          window.CaptureSnapshotForTest();
          log_state("after-3-pressed");
        });

        schedule(7000, [&window, shot_dir_a, check, check_png, log_state]() {
          window.DrainSnapshotsForTest();
          const QStringList paths = window.SnapshotSavedPathsForTest();
          const QStringList files = QDir(shot_dir_a).entryList(
              QStringList{QStringLiteral("*.png")}, QDir::Files);
          const QSize display = window.SnapshotDisplaySizeForTest();
          check("three-saved", paths.size() == 3);
          check("three-files", files.size() == 3);
          if (paths.size() == 3) {
            check("three-distinct",
                  paths[0] != paths[1] && paths[1] != paths[2] && paths[0] != paths[2]);
            check_png(paths[0], display, "shot1");
            check_png(paths[1], display, "shot2");
            check_png(paths[2], display, "shot3");
          }
          std::fprintf(stderr, "T5.4 snapshot_perf three %s\n",
                       window.SnapshotPerfSummaryForTest().toUtf8().constData());
          std::fflush(stderr);
          log_state("after-3-saved");

          // 真实 S 键：走 QAction 快捷键/Win 事件分发，而不是直接调函数。
          window.GoToInputForTest(QStringLiteral("100"));
          window.PressSnapshotKeyForTest();
          log_state("after-s-key");
        });

        schedule(7500, [&window, check, log_state]() {
          window.DrainSnapshotsForTest();
          log_state("key-shot-check");
          check("s-key-saved", window.SnapshotSavedPathsForTest().size() == 4);
          check("s-key-frame", window.SnapshotLastSavedFrameForTest() == 100);
          check("s-key-f100",
                QFileInfo(window.SnapshotLastPathForTest())
                    .fileName()
                    .contains(QStringLiteral("f100")));

          // spec §3.7：备注框聚焦时 S 是文本输入，不能触发截图。
          window.AddMarkForTest();
          const bool focused = window.FocusNoteEditorForTest();
          const int before_focus_s = window.SnapshotSavedPathsForTest().size();
          window.PressSnapshotKeyForTest();
          window.DrainSnapshotsForTest();
          const int after_focus_s = window.SnapshotSavedPathsForTest().size();
          check("s-focus-blocked", focused && after_focus_s == before_focus_s);
          window.ClearFocusForTest();

          // F6-A4：从帧 20 播放，按键瞬间截一帧；帧 20 留出足够播放时间。
          window.GoToInputForTest(QStringLiteral("20"));
          window.StartPlaybackForTest();
          log_state("play-start");
          check("play-started",
                window.SnapshotStateForTest().contains(QStringLiteral("playing=1")));
        });

        schedule(8000, [&window, check, log_state]() {
          check("play-before-capture",
                window.SnapshotStateForTest().contains(QStringLiteral("playing=1")));
          window.CaptureSnapshotForTest();
          log_state("play-captured");
        });

        schedule(10000, [&window, check, log_state]() {
          window.DrainSnapshotsForTest();
          log_state("play-after-capture");
          check("play-not-paused",
                window.SnapshotStateForTest().contains(QStringLiteral("playing=1")));
          check("play-shot-saved", !window.SnapshotLastPathForTest().isEmpty());
          check("f6a4-saved-key-frame",
                window.SnapshotLastSavedFrameForTest() ==
                    window.SnapshotLastCaptureFrameForTest());
          check("play-shot-exists", QFileInfo::exists(window.SnapshotLastPathForTest()));
          window.TogglePlaybackForTest();
          log_state("play-paused");
        });

        schedule(10300, [&window, shot_dir_a, check, log_state]() {
          // F6-A7：snapshots 被同名文件占用 → 回退图片目录，并给出提示。
          const QString backup = shot_dir_a + QStringLiteral("-normal");
          QDir(backup).removeRecursively();
          QDir dir;
          check("fallback-rename", dir.rename(shot_dir_a, backup));
          QFile blocker(shot_dir_a);
          const bool opened = blocker.open(QIODevice::WriteOnly);
          check("fallback-blocker", opened);
          if (opened) {
            blocker.write("x");
            blocker.close();
          }
          // 焦点测试时已添加的标记用于验证 F6-A7“当前标记不丢”。
          window.GoToInputForTest(QStringLiteral("100"));
          window.CaptureSnapshotForTest();
          log_state("fallback-pressed");
        });

        schedule(12000, [&window, pictures_root, shot_dir_a, check, check_png,
                         log_state]() {
          window.DrainSnapshotsForTest();
          log_state("fallback-saved");
          const QString path = window.SnapshotLastPathForTest();
          const QString fallback_dir =
              QDir(pictures_root).filePath(QStringLiteral("拉片软件"));
          check("fallback-flag", window.SnapshotLastFallbackForTest());
          check("fallback-under-pictures",
                QFileInfo(path).absolutePath() == QDir::cleanPath(fallback_dir));
          check("fallback-exists", QFileInfo::exists(path));
          check("fallback-status",
                window.SnapshotStatusForTest().contains(QStringLiteral("图片目录")));
          check("f6a7-mark-kept", window.MarkCountForTest() >= 1);
          if (QFileInfo::exists(path)) {
            check_png(path, window.SnapshotDisplaySizeForTest(), "fallback");
          }

          // 恢复 A 的 snapshots 目录，避免同名 blocker 影响随后打开的 B。
          QFile::remove(shot_dir_a);
          QDir dir;
          check("fallback-restore",
                dir.rename(shot_dir_a + QStringLiteral("-normal"), shot_dir_a));
        });

        if (!shot_video_b.isEmpty()) {
          schedule(12300, [&window, shot_video_b, log_state]() {
            window.OpenFile(shot_video_b);
            window.GoToInputForTest(QStringLiteral("100"));
            log_state("rotated-before");
            window.CaptureSnapshotForTest();
          });
          schedule(14500, [&window, check, check_png, log_state]() {
            window.DrainSnapshotsForTest();
            log_state("rotated-saved");
            const QString path = window.SnapshotLastPathForTest();
            check("rotated-saved", !path.isEmpty());
            check("rotated-size", window.SnapshotDisplaySizeForTest() == QSize(1080, 1920));
            check("rotated-frame", window.SnapshotLastSavedFrameForTest() == 100);
            if (!path.isEmpty()) {
              check_png(path, window.SnapshotDisplaySizeForTest(), "rotated");
            }
          });
        }

        const int final_ms = shot_video_b.isEmpty() ? 14300 : 16500;
        schedule(final_ms, [&window, failures, check, log_state]() {
          window.DrainSnapshotsForTest();
          log_state("final");
          std::fprintf(stderr, "T5.4 snapshot_perf final %s\n",
                       window.SnapshotPerfSummaryForTest().toUtf8().constData());
          std::fflush(stderr);

          lapian::app::ProcessMemorySample memory;
          const bool memory_ok = lapian::app::SampleProcessMemory(&memory);
          const double working_mb =
              memory_ok ? static_cast<double>(memory.working_set_bytes) / (1024.0 * 1024.0) : 0.0;
          const double peak_mb = memory_ok
                                     ? static_cast<double>(memory.peak_working_set_bytes) /
                                           (1024.0 * 1024.0)
                                     : 0.0;
          if (memory_ok) {
            std::fprintf(stderr,
                         "T5.4 snapshot_mem working_set_mb=%.1f peak_working_set_mb=%.1f\n",
                         working_mb, peak_mb);
            std::fflush(stderr);
          }
          const QSize display = window.SnapshotDisplaySizeForTest();
          const double limit_mb =
              (display.width() >= 3840 || display.height() >= 3840) ? 1536.0 : 800.0;
          if (memory_ok) {
            check("p11-working-set", working_mb <= limit_mb);
          }
          check("no-last-error", window.SnapshotLastErrorForTest().isEmpty());

          QCoreApplication::exit(*failures == 0 ? 0 : 1);
        });
      }
      if (fullscreen_test) {
        // T6.1 F1-A6：F/双击全屏、Esc 退出、布局/帧号/时间轴/截图/焦点；
        // --file-b 给 4K 时再验证全屏中 1080p→4K 切换与交换链。
        const QString fs_video_b = file_b;
        auto failures = std::make_shared<int>(0);
        struct FullScreenTestState {
          QRect normal_geometry;
          QSize normal_gfx;
          QSize normal_swap;
          qint64 timeline_expected = -1;
          QString focus_before_exit;
          int snapshot_before_editor = 0;
        };
        auto state = std::make_shared<FullScreenTestState>();
        auto schedule = [&window](int ms, auto fn) {
          QTimer::singleShot(ms, Qt::PreciseTimer, &window, std::move(fn));
        };
        auto check = [failures](const char* name, bool ok) {
          if (!ok) {
            ++(*failures);
          }
          std::fprintf(stderr, "T6.1 fullscreen_check %s ok=%d\n", name, ok ? 1 : 0);
          std::fflush(stderr);
        };
        auto log_state = [&window](const char* step) {
          const QByteArray text = window.FullScreenStateForTest().toUtf8();
          std::fprintf(stderr, "T6.1 fullscreen_state %s %s\n", step, text.constData());
          std::fflush(stderr);
        };
        auto check_swap = [&window, check](const char* name) {
          const QSize gfx = window.GfxWidgetSizeForTest();
          const QSize swap = window.SwapChainSizeForTest();
          const double dpr = window.devicePixelRatioF();
          const int expected_w = static_cast<int>(std::lround(gfx.width() * dpr));
          const int expected_h = static_cast<int>(std::lround(gfx.height() * dpr));
          const bool ok = gfx.width() > 0 && gfx.height() > 0 && swap.width() > 0 &&
                          std::abs(swap.width() - expected_w) <= 1 &&
                          std::abs(swap.height() - expected_h) <= 1;
          check(name, ok);
          std::fprintf(stderr,
                       "T6.1 fullscreen_swap %s gfx=%dx%d dpr=%.2f swap=%dx%d "
                       "expected=%dx%d\n",
                       name, gfx.width(), gfx.height(), dpr, swap.width(), swap.height(),
                       expected_w, expected_h);
          std::fflush(stderr);
        };
        auto check_png = [check](const QString& path, const QSize& expected,
                                 const char* name) {
          const QImage image(path);
          check((std::string(name) + "-loads").c_str(), !image.isNull());
          check((std::string(name) + "-size").c_str(), image.size() == expected);
          std::fprintf(stderr,
                       "T6.1 fullscreen_png %s path=%s actual=%dx%d expected=%dx%d\n", name,
                       path.toUtf8().constData(), image.width(), image.height(),
                       expected.width(), expected.height());
          std::fflush(stderr);
        };

        schedule(500, [&window, state, log_state]() {
          window.GoToInputForTest(QStringLiteral("100"));
          state->normal_geometry = window.geometry();
          state->normal_gfx = window.GfxWidgetSizeForTest();
          state->normal_swap = window.SwapChainSizeForTest();
          log_state("normal");
        });

        schedule(700, [&window, log_state]() {
          window.PressFullScreenKeyForTest();
          log_state("f-pressed");
        });

        schedule(950, [&window, state, check, check_swap, log_state]() {
          log_state("entered");
          check("f-enters", window.IsFullScreenForTest());
          const QRect screen =
              window.screen() == nullptr ? QRect() : window.screen()->geometry();
          check("covers-screen",
                std::abs(window.width() - screen.width()) <= 2 &&
                    std::abs(window.height() - screen.height()) <= 2);
          const QSize gfx = window.GfxWidgetSizeForTest();
          check("gfx-grew", gfx.width() > state->normal_gfx.width() &&
                                gfx.height() > state->normal_gfx.height());
          check_swap("swap-fullscreen");
          check("timeline-visible", window.TimelineVisibleForTest());
          check("infobar-visible", window.InfoBarVisibleForTest());
          check("info-frame-100",
                window.InfoFrameTextForTest().contains(QStringLiteral("100")));
          check("frame-100", window.CurrentFrameForTest() == 100);
          check("display-1920x1080",
                window.SnapshotDisplaySizeForTest() == QSize(1920, 1080));
          // A.8：全屏 chrome 断言。旧壳返回 v2=0（没有顶栏/横幅，状态栏与
          // 检查器在全屏下保留），所以隐藏类断言只在 v2 壳里检查。
          const QString chrome = window.FullScreenChromeForTest();
          std::fprintf(stderr, "T6.1 fullscreen_chrome chrome=%s\n",
                       chrome.toUtf8().constData());
          std::fflush(stderr);
          check("chrome-appbar", chrome.contains(QStringLiteral("appbar=0")));
          check("chrome-timeline", chrome.contains(QStringLiteral("timeline=1")));
          if (chrome.contains(QStringLiteral("v2=1"))) {
            check("chrome-banner", chrome.contains(QStringLiteral("banner=0")));
            check("chrome-status", chrome.contains(QStringLiteral("status=0")));
            check("chrome-inspector",
                  chrome.contains(QStringLiteral("inspector=0")));
            check("chrome-transport",
                  chrome.contains(QStringLiteral("transport=1")));
          }
          // 全屏布局截图：视频区是原生 HWND，grab() 只能拿到黑底；截图只用于
          // 证明时间轴/信息栏/侧栏没有被全屏裁掉（已知限制，见交接）。
          const QString shot =
              QDir::currentPath() + QStringLiteral("/build/t6.1-fullscreen.png");
          const bool saved = window.grab().save(shot);
          std::fprintf(stderr, "T6.1 fullscreen_shot path=%s ok=%d\n",
                       shot.toUtf8().constData(), saved ? 1 : 0);
          std::fflush(stderr);
          check("layout-shot", saved);
        });

        schedule(1150, [&window, state]() {
          const QSize size = window.TimelineSizeForTest();
          const int x = size.width() / 2;
          const int y = 12;
          state->timeline_expected = window.TimelineFrameAtForTest(x, y);
          std::fprintf(stderr, "T6.1 fullscreen_timeline x=%d expected=%lld\n", x,
                       static_cast<long long>(state->timeline_expected));
          std::fflush(stderr);
          window.ClickTimelineForTest(x, y);
        });

        schedule(1500, [&window, state, check, log_state]() {
          log_state("timeline-clicked");
          check("timeline-click-frame",
                state->timeline_expected >= 0 &&
                    window.CurrentFrameForTest() == state->timeline_expected);
          check("timeline-click-paused", !window.IsPlayingForTest());
          check("timeline-click-info",
                window.InfoFrameTextForTest().contains(
                    QString::number(state->timeline_expected)));
        });

        schedule(1700, [&window, log_state]() {
          window.GoToInputForTest(QStringLiteral("100"));
          window.PressSnapshotKeyForTest();
          log_state("snapshot-pressed");
        });

        schedule(2050, [&window, check, check_png, log_state]() {
          window.DrainSnapshotsForTest();
          log_state("snapshot-saved");
          const QString path = window.SnapshotLastPathForTest();
          check("fullscreen-snapshot-saved", !path.isEmpty());
          check("fullscreen-snapshot-frame",
                window.SnapshotLastSavedFrameForTest() == 100);
          check("fullscreen-snapshot-exists", QFileInfo::exists(path));
          check_png(path, QSize(1920, 1080), "fullscreen-shot");
        });

        schedule(2300, [&window, state]() {
          // spec §3.7 / T5.3：备注框聚焦时 F/S 是文本输入，Esc 也不退全屏。
          window.AddMarkForTest();
          state->snapshot_before_editor = window.SnapshotSavedPathsForTest().size();
          window.FocusNoteEditorForTest();
          window.PressFullScreenKeyForTest();
          window.PressSnapshotKeyForTest();
          window.PressEscapeKeyForTest();
        });

        schedule(2600, [&window, state, check, log_state]() {
          log_state("editor-keys");
          check("f-in-editor-kept-fullscreen", window.IsFullScreenForTest());
          check("s-in-editor-no-snapshot",
                window.SnapshotSavedPathsForTest().size() ==
                    state->snapshot_before_editor);
          check("esc-in-editor-kept-fullscreen", window.IsFullScreenForTest());
          check("editor-still-focused", window.IsTextInputFocusedForTest());
          window.ClearFocusForTest();
        });

        schedule(2850, [&window, state, log_state]() {
          window.FocusVideoForTest();
          state->focus_before_exit = window.FocusWidgetNameForTest();
          window.PressFullScreenKeyForTest();  // F 退出全屏
        });

        schedule(3150, [&window, state, check, check_swap, log_state]() {
          log_state("exited");
          check("f-exits", !window.IsFullScreenForTest());
          const QRect now = window.geometry();
          check("geometry-restored",
                std::abs(now.width() - state->normal_geometry.width()) <= 2 &&
                    std::abs(now.height() - state->normal_geometry.height()) <= 2 &&
                    std::abs(now.x() - state->normal_geometry.x()) <= 2 &&
                    std::abs(now.y() - state->normal_geometry.y()) <= 2);
          const QSize gfx = window.GfxWidgetSizeForTest();
          check("gfx-restored",
                std::abs(gfx.width() - state->normal_gfx.width()) <= 1 &&
                    std::abs(gfx.height() - state->normal_gfx.height()) <= 1);
          check_swap("swap-normal");
          check("focus-kept", window.FocusWidgetNameForTest() == state->focus_before_exit);
          check("timeline-visible-after", window.TimelineVisibleForTest());
          check("info-visible-after", window.InfoBarVisibleForTest());
          check("frame-kept", window.CurrentFrameForTest() == 100);
          std::fprintf(stderr, "T6.1 fullscreen_focus before=%s after=%s\n",
                       state->focus_before_exit.toUtf8().constData(),
                       window.FocusWidgetNameForTest().toUtf8().constData());
          std::fflush(stderr);
        });

        schedule(3350, [&window]() { window.PressFullScreenKeyForTest(); });
        schedule(3600, [&window, check]() {
          check("f-reenters", window.IsFullScreenForTest());
          window.PressFullScreenKeyForTest();
        });
        schedule(3850, [&window, check]() {
          check("f-toggles-exit", !window.IsFullScreenForTest());
        });

        schedule(4050, [&window]() { window.DoubleClickVideoForTest(); });
        schedule(4300, [&window, check]() {
          check("dblclick-enters", window.IsFullScreenForTest());
          window.DoubleClickVideoForTest();
        });
        schedule(4550, [&window, check]() {
          check("dblclick-exits", !window.IsFullScreenForTest());
        });

        schedule(4750, [&window]() {
          window.GoToInputForTest(QStringLiteral("20"));
          window.PressSpaceKeyForTest();
        });
        schedule(5000, [&window, check, log_state]() {
          log_state("space-playing");
          check("space-plays-after-exit", window.IsPlayingForTest());
          window.PressSpaceKeyForTest();
        });
        schedule(5250, [&window, check, log_state]() {
          log_state("space-paused");
          check("space-pauses-after-exit", !window.IsPlayingForTest());
        });

        if (!fs_video_b.isEmpty()) {
          schedule(5450, [&window]() {
            window.PressFullScreenKeyForTest();  // 全屏中切 4K
          });
          schedule(5700, [&window, fs_video_b, log_state]() {
            window.OpenFile(fs_video_b);
            window.GoToInputForTest(QStringLiteral("60"));
            window.PressSnapshotKeyForTest();
            log_state("4k-pressed");
          });
          schedule(6250, [&window, check, check_png, check_swap, log_state]() {
            window.DrainSnapshotsForTest();
            log_state("4k-saved");
            check("4k-fullscreen-kept", window.IsFullScreenForTest());
            check("4k-display",
                  window.SnapshotDisplaySizeForTest() == QSize(3840, 2160));
            check("4k-frame", window.SnapshotLastSavedFrameForTest() == 60);
            check("4k-no-error", window.SnapshotLastErrorForTest().isEmpty());
            check_png(window.SnapshotLastPathForTest(), QSize(3840, 2160),
                      "4k-fullscreen");
            check_swap("swap-4k");
            check("4k-info-frame",
                  window.InfoFrameTextForTest().contains(QStringLiteral("60")));
            window.PressEscapeKeyForTest();
          });
          schedule(6500, [&window, check, log_state]() {
            log_state("4k-exited");
            check("4k-esc-exits", !window.IsFullScreenForTest());
            check("4k-timeline-visible", window.TimelineVisibleForTest());
          });
        }

        const int final_ms = fs_video_b.isEmpty() ? 6100 : 7400;
        schedule(final_ms - 500, [&window]() { window.showMaximized(); });
        schedule(final_ms - 300, [&window, check]() {
          // 最大化状态下进入/退出全屏：退出后应回到最大化，而不是普通窗口。
          window.PressFullScreenKeyForTest();
          check("maximized-f-enters", window.IsFullScreenForTest());
          window.PressEscapeKeyForTest();
        });
        schedule(final_ms, [&window, failures, check, log_state]() {
          window.DrainSnapshotsForTest();
          log_state("maximized-exited");
          check("maximized-restored", window.IsMaximizedForTest());
          std::fprintf(stderr, "T6.1 fullscreen_toggle_ms=%lld\n",
                       static_cast<long long>(window.LastFullScreenToggleMsForTest()));
          std::fflush(stderr);
          QCoreApplication::exit(*failures == 0 ? 0 : 1);
        });
      }
      if (key_test) {
        // T6.2 键位/焦点矩阵：spec §3.7 全表、焦点切换、OS 重复、文本让位、模态让位。
        auto failures = std::make_shared<int>(0);
        struct KeyTestState {
          bool goto_seen = false;
          qint64 goto_frame_before = -1;
          bool modal_seen = false;
          qint64 modal_frame_before = -1;
          qint64 modal_frame_after = -1;
          int marks_before_modal = 0;
          qint64 hold_start_frame = -1;
          qint64 hold_auto_before = -1;
          qint64 hold_auto_after = -1;
          qint64 hold_release_frame = -1;
          qint64 focus_hold_frame = -1;
          qint64 long_hold_start = -1;
          qint64 long_hold_steps = -1;
          QString editor_before;
          int snapshots_before_editor = 0;
        };
        auto state = std::make_shared<KeyTestState>();
        auto schedule = [&window](int ms, auto fn) {
          QTimer::singleShot(ms, Qt::PreciseTimer, &window, std::move(fn));
        };
        auto check = [failures](const char* name, bool ok) {
          if (!ok) {
            ++(*failures);
          }
          std::fprintf(stderr, "T6.2 key_check %s ok=%d\n", name, ok ? 1 : 0);
          std::fflush(stderr);
        };
        auto log_state = [&window](const char* step) {
          const QByteArray text = window.KeyStateForTest().toUtf8();
          std::fprintf(stderr, "T6.2 key_state %s %s\n", step, text.constData());
          std::fflush(stderr);
        };

        schedule(500, [&window, state, check, log_state]() {
          auto press = [&window](int key, int modifiers) {
            window.PressKeyForTest(key, modifiers);
          };
          window.GoToInputForTest(QStringLiteral("100"));
          log_state("ready");

          press(Qt::Key_PageDown, 0);
          check("pgdn-step", window.CurrentFrameForTest() == 101);
          press(Qt::Key_PageUp, 0);
          check("pgup-step", window.CurrentFrameForTest() == 100);
          press(Qt::Key_Right, 0);
          check("right-step", window.CurrentFrameForTest() == 101);
          press(Qt::Key_Left, 0);
          check("left-step", window.CurrentFrameForTest() == 100);
          press(Qt::Key_Home, 0);
          check("home-first", window.CurrentFrameForTest() == 0);
          const qint64 last = window.LastFrameIndexForTest();
          check("last-index", last > 1000);
          press(Qt::Key_End, 0);
          check("end-last", window.CurrentFrameForTest() == last);
          press(Qt::Key_Home, 0);
          check("home-again", window.CurrentFrameForTest() == 0);

          window.GoToInputForTest(QStringLiteral("20"));
          press(Qt::Key_Space, 0);
          check("space-play", window.IsPlayingForTest());
          press(Qt::Key_Space, 0);
          check("space-pause", !window.IsPlayingForTest());

          const double speed0 = window.SpeedForTest();
          press(Qt::Key_BracketRight, 0);
          check("faster", window.SpeedForTest() > speed0);
          press(Qt::Key_BracketLeft, 0);
          check("slower-restore", std::abs(window.SpeedForTest() - speed0) < 1.0e-9);

          const bool wave0 = window.WaveformVisibleForTest();
          press(Qt::Key_W, 0);
          check("wave-off", window.WaveformVisibleForTest() != wave0);
          press(Qt::Key_W, 0);
          check("wave-on", window.WaveformVisibleForTest() == wave0);

          const bool mute0 = window.MutedForTest();
          press(Qt::Key_M, 0);
          check("mute-toggle", window.MutedForTest() != mute0);
          press(Qt::Key_M, 0);
          check("mute-restore", window.MutedForTest() == mute0);

          window.GoToInputForTest(QStringLiteral("50"));
          press(Qt::Key_I, 0);
          window.GoToInputForTest(QStringLiteral("100"));
          press(Qt::Key_O, 0);
          press(Qt::Key_L, 0);
          const QString loop_on = window.LoopStateForTest();
          std::fprintf(stderr, "T6.2 key_loop %s\n", loop_on.toUtf8().constData());
          std::fflush(stderr);
          check("loop-on", loop_on.contains(QStringLiteral("loop=1")) &&
                               loop_on.contains(QStringLiteral("a=50")) &&
                               loop_on.contains(QStringLiteral("b=100")));
          press(Qt::Key_L, Qt::ShiftModifier);
          check("shift-l-clears",
                window.LoopStateForTest().contains(QStringLiteral("loop=0")));

          const int marks0 = window.MarkCountForTest();
          press(Qt::Key_M, Qt::ControlModifier);
          check("ctrl-m-add", window.MarkCountForTest() == marks0 + 1);

          const int snaps0 = window.SnapshotSavedPathsForTest().size();
          press(Qt::Key_S, 0);
          window.DrainSnapshotsForTest();
          check("s-key-saved",
                window.SnapshotSavedPathsForTest().size() == snaps0 + 1);
          std::fprintf(stderr, "T6.2 key_snapshot %s\n",
                       window.SnapshotStateForTest().toUtf8().constData());
          std::fflush(stderr);
          check("s-key-frame", window.SnapshotLastSavedFrameForTest() == 100);

          press(Qt::Key_F, 0);
          check("f-fullscreen", window.IsFullScreenForTest());
          press(Qt::Key_Escape, 0);
          check("esc-exits", !window.IsFullScreenForTest());

          // Ctrl+G：真实对话框路径；用定时器在嵌套事件循环里关掉。
          window.GoToInputForTest(QStringLiteral("50"));
          state->goto_frame_before = window.CurrentFrameForTest();
          QTimer::singleShot(100, &window, [&window, state]() {
            QWidget* dialog = QApplication::activeModalWidget();
            state->goto_seen = dialog != nullptr;
            if (dialog != nullptr) {
              dialog->close();
            }
          });
          press(Qt::Key_G, Qt::ControlModifier);
          check("ctrlg-opened", state->goto_seen);
          check("ctrlg-no-jump",
                window.CurrentFrameForTest() == state->goto_frame_before);

          const QString mapping = window.ShortcutMapForTest();
          check("map-open", mapping.contains(QStringLiteral("open=Ctrl+O")));
          check("map-goto", mapping.contains(QStringLiteral("goto=Ctrl+G")));
          check("map-play", mapping.contains(QStringLiteral("play=Space")));
          check("map-in", mapping.contains(QStringLiteral("in=I")));
          check("map-out", mapping.contains(QStringLiteral("out=O")));
          check("map-loop", mapping.contains(QStringLiteral("loop=L")));
          check("map-clear", mapping.contains(QStringLiteral("clear=Shift+L")));
          check("map-slower", mapping.contains(QStringLiteral("slower=[")));
          check("map-faster", mapping.contains(QStringLiteral("faster=]")));
          check("map-wave", mapping.contains(QStringLiteral("wave=W")));
          check("map-mute", mapping.contains(QStringLiteral("mute=M")));
          check("map-mark", mapping.contains(QStringLiteral("mark=Ctrl+M")));
          check("map-snap", mapping.contains(QStringLiteral("snap=S")));
          check("map-full", mapping.contains(QStringLiteral("full=F")));
          check("map-esc", mapping.contains(QStringLiteral("esc=Esc")));
          check("map-zoom-in", mapping.contains(QStringLiteral("zoom_in=+")));
          check("map-zoom-out", mapping.contains(QStringLiteral("zoom_out=-")));

          // +/-：60s 短片 fit 约 0.68px/帧，放大/缩小都有空间。
          // 比例断言同时确认没有“QAction + 过滤器”双触发。
          const double ppf0 = window.TimelinePixelsPerFrameForTest();
          press(Qt::Key_Plus, Qt::ShiftModifier);
          const double ppf1 = window.TimelinePixelsPerFrameForTest();
          check("zoom-in-key", ppf1 > ppf0 && ppf1 / ppf0 > 1.1 &&
                                   ppf1 / ppf0 < 1.4);
          press(Qt::Key_Minus, 0);
          const double ppf2 = window.TimelinePixelsPerFrameForTest();
          check("zoom-out-key", ppf2 < ppf1 && ppf2 / ppf1 > 0.7 &&
                                    ppf2 / ppf1 < 0.9);
          check("zoom-restore", std::abs(ppf2 - ppf0) < 1.0e-6);
          press(Qt::Key_Plus, 0);
          const double ppf3 = window.TimelinePixelsPerFrameForTest();
          check("zoom-numpad-plus", ppf3 > ppf2);
          press(Qt::Key_Minus, 0);
          std::fprintf(stderr,
                       "T6.2 key_zoom ppf0=%.3f ppf1=%.3f ppf2=%.3f ppf3=%.3f\n",
                       ppf0, ppf1, ppf2, ppf3);
          std::fflush(stderr);

          // 焦点矩阵：视频/侧栏树/音量滑块/无焦点都应走同一套步进。
          window.FocusVideoForTest();
          window.GoToInputForTest(QStringLiteral("50"));
          press(Qt::Key_Right, 0);
          check("focus-video-step", window.CurrentFrameForTest() == 51);
          check("focus-tree", window.FocusMarkTreeForTest());
          window.GoToInputForTest(QStringLiteral("50"));
          press(Qt::Key_PageDown, 0);
          check("focus-tree-step", window.CurrentFrameForTest() == 51);
          press(Qt::Key_Home, 0);
          check("focus-tree-home", window.CurrentFrameForTest() == 0);
          check("focus-slider", window.FocusVolumeSliderForTest());
          window.GoToInputForTest(QStringLiteral("50"));
          press(Qt::Key_Right, 0);
          check("focus-slider-step", window.CurrentFrameForTest() == 51);
          window.ClearFocusForTest();
          window.GoToInputForTest(QStringLiteral("50"));
          press(Qt::Key_Left, 0);
          check("focus-none-step", window.CurrentFrameForTest() == 49);
          log_state("matrix");
        });

        // 长按：300ms 后 70ms 连发；OS 自动重复不额外走帧。
        schedule(3000, [&window, state]() {
          window.FocusVideoForTest();
          window.GoToInputForTest(QStringLiteral("50"));
          state->hold_start_frame = window.CurrentFrameForTest();
          window.PressKeyDownForTest(Qt::Key_PageDown, 0);
        });
        schedule(3450, [&window, state, check]() {
          check("hold-advances",
                window.CurrentFrameForTest() >= state->hold_start_frame + 3);
          check("hold-active", window.StepRepeatActiveForTest());
          state->hold_auto_before = window.CurrentFrameForTest();
          window.PressAutoRepeatKeyForTest(Qt::Key_PageDown, 0);
          window.PressAutoRepeatKeyForTest(Qt::Key_PageDown, 0);
          state->hold_auto_after = window.CurrentFrameForTest();
          check("autorepeat-ignored",
                state->hold_auto_after == state->hold_auto_before);
          std::fprintf(stderr,
                       "T6.2 key_hold start=%lld after=%lld autorepeat=%lld\n",
                       static_cast<long long>(state->hold_start_frame),
                       static_cast<long long>(state->hold_auto_after),
                       static_cast<long long>(state->hold_auto_before));
          std::fflush(stderr);
        });
        schedule(3500, [&window, check]() {
          check("autorepeat-release-active", window.StepRepeatActiveForTest());
          window.PressAutoRepeatReleaseForTest(Qt::Key_PageDown, 0);
          check("autorepeat-release-ignored", window.StepRepeatActiveForTest());
        });
        schedule(3550, [&window, state, check]() {
          window.PressKeyUpForTest(Qt::Key_PageDown, 0);
          state->hold_release_frame = window.CurrentFrameForTest();
          check("hold-release-stops", !window.StepRepeatActiveForTest());
        });
        schedule(3900, [&window, state, check]() {
          check("hold-stopped",
                window.CurrentFrameForTest() == state->hold_release_frame);
          const qint64 before = window.CurrentFrameForTest();
          window.PressAutoRepeatKeyForTest(Qt::Key_PageDown, 0);
          check("idle-autorepeat-ignored",
                window.CurrentFrameForTest() == before);
        });
        // 焦点切换必须停掉连发：按住 PgDn 时把焦点移到音量滑块。
        schedule(4000, [&window, state]() {
          window.FocusVideoForTest();
          window.GoToInputForTest(QStringLiteral("50"));
          window.PressKeyDownForTest(Qt::Key_PageDown, 0);
        });
        schedule(4100, [&window, state, check]() {
          check("focus-hold-active", window.StepRepeatActiveForTest());
          check("focus-hold-slider", window.FocusVolumeSliderForTest());
          state->focus_hold_frame = window.CurrentFrameForTest();
          check("focus-hold-stopped", !window.StepRepeatActiveForTest());
          window.PressKeyUpForTest(Qt::Key_PageDown, 0);
        });
        schedule(4300, [&window, state, check]() {
          check("focus-hold-frozen",
                window.CurrentFrameForTest() == state->focus_hold_frame);
        });

        // 文本让位：全屏 + 备注框聚焦时，所有全局键都不改变播放状态。
        schedule(4500, [&window, state, check, log_state]() {
          window.FocusVideoForTest();
          window.PressKeyForTest(Qt::Key_F, 0);
          check("editor-fullscreen", window.IsFullScreenForTest());
          window.SetMarkNoteForTest(0, QStringLiteral("标签"),
                                    QStringLiteral("备注"));
          check("editor-focused", window.FocusNoteEditorForTest());
          state->editor_before = window.KeyStateForTest();
          state->snapshots_before_editor =
              window.SnapshotSavedPathsForTest().size();
          const int keys[] = {Qt::Key_Space, Qt::Key_PageDown, Qt::Key_PageUp,
                              Qt::Key_Left,  Qt::Key_Right,    Qt::Key_Home,
                              Qt::Key_End,   Qt::Key_I,        Qt::Key_O,
                              Qt::Key_L,     Qt::Key_W,        Qt::Key_M,
                              Qt::Key_S,     Qt::Key_F,        Qt::Key_BracketLeft,
                              Qt::Key_BracketRight};
          for (int key : keys) {
            window.PressKeyForTest(key, 0);
          }
          window.PressKeyForTest(Qt::Key_L, Qt::ShiftModifier);
          window.PressKeyForTest(Qt::Key_M, Qt::ControlModifier);
          window.PressKeyForTest(Qt::Key_G, Qt::ControlModifier);
          window.PressKeyForTest(Qt::Key_Escape, 0);
          check("editor-state-unchanged",
                window.KeyStateForTest() == state->editor_before);
          check("editor-no-snapshot",
                window.SnapshotSavedPathsForTest().size() ==
                    state->snapshots_before_editor);
          check("editor-fullscreen-kept", window.IsFullScreenForTest());
          check("editor-focus-kept", window.IsTextInputFocusedForTest());
          window.ClearFocusForTest();
          window.PressKeyForTest(Qt::Key_Escape, 0);
          check("editor-esc-after-blur", !window.IsFullScreenForTest());
          log_state("editor-done");
        });

        // 模态让位：删除确认框打开时，步进键不应改变画面。
        schedule(5000, [&window, state, check, log_state]() {
          window.GoToInputForTest(QStringLiteral("80"));
          state->modal_frame_before = window.CurrentFrameForTest();
          state->marks_before_modal = window.MarkCountForTest();
          QTimer::singleShot(100, &window, [&window, state]() {
            QMessageBox* box =
                qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            state->modal_seen = box != nullptr;
            state->modal_frame_after = window.CurrentFrameForTest();
            window.PressKeyForTest(Qt::Key_PageDown, 0);
            state->modal_frame_after = window.CurrentFrameForTest();
            if (box != nullptr) {
              QAbstractButton* no = box->button(QMessageBox::No);
              if (no != nullptr) {
                no->click();
              } else {
                box->reject();
              }
            }
          });
          window.DeleteMarkWithConfirmForTest(0);
          check("modal-seen", state->modal_seen);
          check("modal-frame-unchanged",
                state->modal_frame_after == state->modal_frame_before);
          check("modal-mark-kept",
                window.MarkCountForTest() == state->marks_before_modal);
          log_state("modal-done");
        });

        // T2.5 回归：长按 2s 应 ≥20 帧（70ms 连发），松手后停。
        schedule(5600, [&window, state]() {
          window.FocusVideoForTest();
          window.GoToInputForTest(QStringLiteral("50"));
          state->long_hold_start = window.CurrentFrameForTest();
          window.PressKeyDownForTest(Qt::Key_PageDown, 0);
        });
        schedule(7700, [&window, state, check]() {
          state->long_hold_steps =
              window.CurrentFrameForTest() - state->long_hold_start;
          check("hold-2s-20frames", state->long_hold_steps >= 20);
          std::fprintf(stderr, "T6.2 key_hold_2s steps=%lld\n",
                       static_cast<long long>(state->long_hold_steps));
          std::fflush(stderr);
          window.PressKeyUpForTest(Qt::Key_PageDown, 0);
        });
        schedule(8000, [&window, check]() {
          check("hold-2s-stopped", !window.StepRepeatActiveForTest());
        });

        schedule(8500, [&window, failures, check, log_state]() {
          window.DrainSnapshotsForTest();
          log_state("final");
          const QByteArray mapping = window.ShortcutMapForTest().toUtf8();
          std::fprintf(stderr, "T6.2 key_shortcuts %s\n", mapping.constData());
          std::fflush(stderr);
          QCoreApplication::exit(*failures == 0 ? 0 : 1);
        });
      }
      if (settings_test) {
        // T6.3 F8-A1：默认值 → 改音量/关波形 → 原子落盘 → 损坏/非法/未知键/未来
        // schema → 目录占用回退 → 关窗保存。只持久化 volume/waveform。
        auto failures = std::make_shared<int>(0);
        auto check = [failures](const char* name, bool ok) {
          if (!ok) {
            ++(*failures);
          }
          std::fprintf(stderr, "T6.3 settings_check %s ok=%d\n", name, ok ? 1 : 0);
          std::fflush(stderr);
        };
        auto log_state = [&window](const char* step) {
          const QByteArray state = window.SettingsStateForTest().toUtf8();
          std::fprintf(stderr, "T6.3 settings_state %s %s\n", step, state.constData());
          std::fflush(stderr);
        };
        QTimer::singleShot(500, &window,
                           [&window, failures, check, log_state, settings_root]() {
          auto write_file = [](const QString& path, const QByteArray& bytes) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
              return false;
            }
            const bool ok = file.write(bytes) == bytes.size();
            file.close();
            return ok;
          };
          auto read_object = [](const QString& path, QJsonObject* object) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
              return false;
            }
            QJsonParseError error{};
            const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
            if (error.error != QJsonParseError::NoError || !document.isObject()) {
              return false;
            }
            *object = document.object();
            return true;
          };

          const QString path = window.SettingsPathForTest();
          const QString injected_root = QFileInfo(settings_root).absoluteFilePath();
          check("status-missing",
                window.SettingsLoadStatusForTest() == QStringLiteral("missing"));
          check("default-volume", window.VolumeForTest() == 100);
          check("default-waveform", window.WaveformVisibleForTest());
          check("no-file-at-start", !QFileInfo::exists(path));
          check("path-root", QFileInfo(path).absolutePath() == injected_root);
          const QString default_root =
              QDir::fromNativeSeparators(window.DefaultSettingsRootForTest());
          const bool default_in_appdata =
              default_root.contains(QStringLiteral("appdata/roaming"),
                                    Qt::CaseInsensitive);
          const bool default_matches_injected =
              QFileInfo(default_root).absoluteFilePath() == injected_root;
          check("default-root", default_in_appdata || default_matches_injected);
          check("default-root-name", default_root.endsWith(QStringLiteral("拉片软件")));
          std::fprintf(stderr, "T6.3 settings_default_root path=%s\n",
                       QDir::toNativeSeparators(window.DefaultSettingsRootForTest())
                           .toUtf8()
                           .constData());
          std::fflush(stderr);
          log_state("defaults");

          window.SetVolumeForTest(37);
          window.SetWaveformVisibleForTest(false);
          check("dirty-after-change",
                window.SettingsStateForTest().contains(QStringLiteral("dirty=1")));
          check("flush-ok", window.FlushSettingsForTest());
          check("file-exists", QFileInfo::exists(path));
          QJsonObject object;
          check("file-json", read_object(path, &object));
          check("file-schema", object.value(QStringLiteral("schema")).toInt() == 1);
          check("file-volume", object.value(QStringLiteral("volume")).toInt() == 37);
          check("file-waveform",
                !object.value(QStringLiteral("waveform")).toBool(true));
          check("dirty-cleared",
                window.SettingsStateForTest().contains(QStringLiteral("dirty=0")));
          log_state("saved");

          // 关窗路径：不再显式 Flush，CloseForTest 发送真实 QCloseEvent。
          window.SetVolumeForTest(55);
          check("close-accepted", window.CloseForTest());
          check("close-saved-volume",
                read_object(path, &object) &&
                    object.value(QStringLiteral("volume")).toInt() == 55);
          check("close-saved-waveform",
                read_object(path, &object) &&
                    !object.value(QStringLiteral("waveform")).toBool(true));

          // 损坏：回退默认，不崩溃、不改写原文件（下一次保存才覆盖）。
          check("write-corrupt", write_file(path, QByteArrayLiteral("{not json")));
          window.ReloadSettingsForTest();
          check("corrupt-status",
                window.SettingsLoadStatusForTest() == QStringLiteral("corrupt"));
          check("corrupt-volume", window.VolumeForTest() == 100);
          check("corrupt-waveform", window.WaveformVisibleForTest());
          check("corrupt-not-dirty",
                window.SettingsStateForTest().contains(QStringLiteral("dirty=0")));

          // 非法字段：逐项回退/clamp；未知键忽略。
          check("write-invalid",
                write_file(path,
                           QByteArrayLiteral("{\"schema\":1,\"volume\":999,"
                                             "\"waveform\":\"no\",\"extra\":42}")));
          window.ReloadSettingsForTest();
          check("invalid-status",
                window.SettingsLoadStatusForTest() == QStringLiteral("loaded"));
          check("invalid-volume-clamp", window.VolumeForTest() == 100);
          check("invalid-waveform-default", window.WaveformVisibleForTest());

          check("write-unknown",
                write_file(path,
                           QByteArrayLiteral("{\"schema\":1,\"volume\":33,"
                                             "\"waveform\":false,\"unknown\":\"x\","
                                             "\"future\":{\"nested\":[1,2]}}")));
          window.ReloadSettingsForTest();
          check("unknown-status",
                window.SettingsLoadStatusForTest() == QStringLiteral("loaded"));
          check("unknown-volume", window.VolumeForTest() == 33);
          check("unknown-waveform", !window.WaveformVisibleForTest());
          check("unknown-not-dirty",
                window.SettingsStateForTest().contains(QStringLiteral("dirty=0")));

          // 未来 schema：整份回退默认，不猜测字段语义。
          check("write-future",
                write_file(path,
                           QByteArrayLiteral("{\"schema\":2,\"volume\":10,"
                                             "\"waveform\":false}")));
          window.ReloadSettingsForTest();
          check("future-status",
                window.SettingsLoadStatusForTest() ==
                    QStringLiteral("unsupported_schema"));
          check("future-defaults", window.VolumeForTest() == 100 &&
                                       window.WaveformVisibleForTest());

          // 路径被目录占用：读回退默认，写失败但不崩溃；dirty 保留供下次重试。
          check("remove-file", QFile::remove(path));
          check("make-dir-at-file", QDir().mkpath(path));
          window.ReloadSettingsForTest();
          check("dir-status",
                window.SettingsLoadStatusForTest() == QStringLiteral("unreadable"));
          check("dir-defaults", window.VolumeForTest() == 100 &&
                                    window.WaveformVisibleForTest());
          window.SetVolumeForTest(80);
          check("dir-save-fails", !window.FlushSettingsForTest());
          check("dir-save-keeps-dirty",
                window.SettingsStateForTest().contains(QStringLiteral("dirty=1")));
          check("remove-dir", QDir().rmdir(path));

          // 循环/速度只本次会话：写入配置前后都不应进入 settings.json。
          window.SetSpeedForTest(1.5);
          window.GoToInputForTest(QStringLiteral("50"));
          window.SetLoopInForTest();
          window.GoToInputForTest(QStringLiteral("100"));
          window.SetLoopOutForTest();
          window.ToggleLoopForTest();
          check("loop-for-session",
                window.LoopStateForTest().contains(QStringLiteral("loop=1")));

          // 20 次保存延迟（开发机探针，不作为验收指标）。
          std::vector<double> samples;
          for (int i = 0; i < 20; ++i) {
            window.SetVolumeForTest(40 + i % 10);
            if (!window.FlushSettingsForTest()) {
              check("perf-save", false);
              break;
            }
            samples.push_back(window.LastSettingsSaveMsForTest());
          }
          std::sort(samples.begin(), samples.end());
          if (!samples.empty()) {
            const double p50 = samples[samples.size() / 2];
            const std::size_t p95_index = std::min(
                samples.size() - 1,
                static_cast<std::size_t>(static_cast<double>(samples.size()) * 0.95));
            std::fprintf(stderr,
                         "T6.3 settings_save_perf n=%zu p50=%.3fms p95=%.3fms "
                         "max=%.3fms load_ms=%.3f\n",
                         samples.size(), p50, samples[p95_index], samples.back(),
                         window.LastSettingsLoadMsForTest());
            std::fflush(stderr);
          }

          // 给第二个进程（--settings-expect 55 0）留一份确定状态。
          window.SetVolumeForTest(55);
          window.SetWaveformVisibleForTest(false);
          check("final-flush", window.FlushSettingsForTest());
          check("final-file", read_object(path, &object) &&
                                  object.value(QStringLiteral("volume")).toInt() == 55 &&
                                  !object.value(QStringLiteral("waveform")).toBool(true));
          // 从磁盘重载，确认落盘内容可被下一次启动直接使用。
          window.ReloadSettingsForTest();
          check("final-reload-status",
                window.SettingsLoadStatusForTest() == QStringLiteral("loaded"));
          check("final-reload-values", window.VolumeForTest() == 55 &&
                                           !window.WaveformVisibleForTest());
          log_state("final");
          QCoreApplication::exit(*failures == 0 ? 0 : 1);
        });
      }
      if (settings_expect) {
        // T6.3 第二次进程：验证 F8-A1 状态恢复；循环/速度不跨会话。
        auto failures = std::make_shared<int>(0);
        auto check = [failures](const char* name, bool ok) {
          if (!ok) {
            ++(*failures);
          }
          std::fprintf(stderr, "T6.3 settings_check %s ok=%d\n", name, ok ? 1 : 0);
          std::fflush(stderr);
        };
        QTimer::singleShot(500, &window,
                           [&window, failures, check, settings_expect_volume,
                            settings_expect_waveform]() {
          check("status-loaded",
                window.SettingsLoadStatusForTest() == QStringLiteral("loaded"));
          check("volume-restored", window.VolumeForTest() == settings_expect_volume);
          check("waveform-restored",
                window.WaveformVisibleForTest() == settings_expect_waveform);
          check("loop-session-only",
                window.LoopStateForTest().contains(QStringLiteral("loop=0")));
          check("rate-session-only", std::abs(window.SpeedForTest() - 1.0) < 1.0e-9);
          std::fprintf(stderr,
                       "T6.3 settings_expect status=%s volume=%d waveform=%d "
                       "expected_volume=%d expected_waveform=%d\n",
                       window.SettingsLoadStatusForTest().toUtf8().constData(),
                       window.VolumeForTest(), window.WaveformVisibleForTest() ? 1 : 0,
                       settings_expect_volume, settings_expect_waveform ? 1 : 0);
          std::fflush(stderr);
          QCoreApplication::exit(*failures == 0 ? 0 : 1);
        });
      }
      if (dpi_test) {
        // T6.4：正常窗口/全屏/窄窗口的布局不裁切、不重叠；由 QT_SCALE_FACTOR 驱动 DPI。
        auto failures = std::make_shared<int>(0);
        auto check = [failures](const char* name, bool ok) {
          if (!ok) {
            ++(*failures);
          }
          std::fprintf(stderr, "T6.4 dpi_check %s ok=%d\n", name, ok ? 1 : 0);
          std::fflush(stderr);
        };
        auto verify = [&window, check, dpi_tag](const char* phase) {
          const QStringList problems = window.LayoutProblemsForTest();
          const QByteArray state = window.LayoutStateForTest().toUtf8();
          std::fprintf(stderr, "T6.4 dpi_state %s %s\n", phase, state.constData());
          std::fflush(stderr);
          for (const QString& problem : problems) {
            std::fprintf(stderr, "T6.4 dpi_problem %s %s\n", phase,
                         problem.toUtf8().constData());
          }
          std::fflush(stderr);
          check((std::string(phase) + "-layout").c_str(), problems.isEmpty());
          const QString shot =
              QDir::currentPath() + QStringLiteral("/build/t6.4-dpi-") +
              (dpi_tag.isEmpty() ? QStringLiteral("x") : dpi_tag) +
              QStringLiteral("-") + QString::fromLatin1(phase) + QStringLiteral(".png");
          const bool saved = window.grab().save(shot);
          check((std::string(phase) + "-shot").c_str(), saved);
          std::fprintf(stderr, "T6.4 dpi_shot %s path=%s ok=%d\n", phase,
                       shot.toUtf8().constData(), saved ? 1 : 0);
          std::fflush(stderr);
        };
        QTimer::singleShot(
            600, &window,
            [&window, failures, check, verify, dpi_expect_scale, dpi_tag,
             dpi_empty]() {
          if (dpi_expect_scale > 0.0) {
            check("dpr", std::abs(window.devicePixelRatioF() - dpi_expect_scale) < 0.01);
          }
          const QSize available = window.AvailableScreenForTest();
          std::fprintf(stderr,
                       "T6.4 dpi_env scale=%.2f available=%dx%d info_narrow=%d "
                       "file=%s full_name=%s\n",
                       window.devicePixelRatioF(), available.width(),
                       available.height(), window.InfoNarrowModeForTest() ? 1 : 0,
                       window.InfoFileLabelForTest().toUtf8().constData(),
                       window.InfoFileNameForTest().toUtf8().constData());
          std::fflush(stderr);
          verify(dpi_empty ? "empty" : "normal");
          if (dpi_empty) {
            // M3 空态列（规划 §3.2 状态 1）：模式 chip=`未打开`、检查器折叠 44、
            // 横幅槽高 0；LayoutProblemsForTest() 已由 verify 断言为空。
            std::fprintf(stderr,
                         "T6.4 dpi_empty mode=%s collapsed=%d banner_h=%d\n",
                         window.TopBarModeForTest().toUtf8().constData(),
                         window.InspectorCollapsedForTest() ? 1 : 0,
                         window.InfoBarHeightForTest());
            std::fflush(stderr);
            check("empty-mode",
                  window.TopBarModeForTest() == QStringLiteral("未打开"));
            check("empty-inspector-collapsed",
                  window.InspectorCollapsedForTest());
            check("empty-banner-height", window.InfoBarHeightForTest() == 0);
          }
          window.PressFullScreenKeyForTest();
          QTimer::singleShot(
              350, &window,
              [&window, check, verify, available, failures, dpi_tag,
               dpi_empty]() {
            check("fullscreen", window.IsFullScreenForTest());
            verify(dpi_empty ? "empty-fullscreen" : "fullscreen");
            window.PressEscapeKeyForTest();
            QTimer::singleShot(
                250, &window,
                [&window, check, verify, available, failures, dpi_tag,
                 dpi_empty]() {
              check("exit-fullscreen", !window.IsFullScreenForTest());
              const int narrow_height =
                  std::min(500, std::max(360, available.height() - 64));
              window.resize(820, narrow_height);
              QTimer::singleShot(250, &window,
                                 [&window, verify, failures, check, dpi_tag,
                                  dpi_empty]() {
                verify(dpi_empty ? "empty-narrow" : "narrow");
                // 3B/3C 的新用例在空态下同样成立（时间轴/检查器/两条滑块都存在）。
                // 3B 新用例 ①：档位切换（640/520 边界来回拖）——每次落档后
                // 布局断言必须仍为空（"每栏 = 令牌 ±2" 在换档后同样成立）。
                // 注意 1：20px 迟滞带（620–639 / 500–519）里画面占比**必然**低于
                // M4 门槛（M4 只在那 4 个几何上定义），所以这里只排除
                // stage-ratio-low，其余布局断言一条不放。
                // 注意 2：高 DPI 下屏幕可用逻辑高度装不下 640/660 的窗口，按
                // 可用高度夹取并在日志里打印实际高度（不是所有 DPI 都测得到边界）。
                const QSize avail = window.AvailableScreenForTest();
                // 先把窗口贴到 (0,0)：否则 150%/200% 下"窗口右上角 + 标题栏"
                // 会超出可用区，被 window-outside-screen 判红（与裁切无关）。
                window.move(0, 0);
                QCoreApplication::processEvents();
                const int max_h = std::max(400, avail.height() - 48);
                const int win_w =
                    std::min(1280, std::max(700, avail.width() - 40));
                const int heights[] = {500, 640, 639, 660, 520, 519, 640, 500};
                for (const int h : heights) {
                  const int target_h = std::min(h, max_h);
                  window.resize(win_w, target_h);
                  QCoreApplication::processEvents();
                  QStringList problems = window.LayoutProblemsForTest();
                  problems.removeAll(QStringLiteral("stage-ratio-low"));
                  for (const QString& problem : problems) {
                    std::fprintf(stderr,
                                 "T6.4 dpi_problem tier-switch-%d %s\n", h,
                                 problem.toUtf8().constData());
                  }
                  std::fprintf(stderr,
                               "T6.4 dpi_tier h=%d applied=%d tier=%s shell=%s\n",
                               h, target_h,
                               window.DensityNameForTest().toUtf8().constData(),
                               window.ShellStateForTest().toUtf8().constData());
                  std::fflush(stderr);
                  check((std::string("tier-switch-") + std::to_string(h) +
                         "-layout").c_str(),
                        problems.isEmpty());
                }
                // 3B 新用例 ②：时间轴拖到 340（最大值）——不裁切、不顶出面板。
                // 200% DPI（可用高 ~516）时 340 在物理上放不下（画面会被压到
                // 170 以下），此时只断言"布局合法"，tl 值按实际打印。
                window.resize(win_w, std::min(720, max_h));
                QCoreApplication::processEvents();
                window.SetTimelineHeightForTest(340);
                const QString split_after = window.SplitterStateForTest();
                const bool room_for_340 = max_h >= 640;
                std::fprintf(stderr,
                             "T6.4 dpi_tier340 max_h=%d room=%d split=%s\n",
                             max_h, room_for_340 ? 1 : 0,
                             split_after.toUtf8().constData());
                std::fflush(stderr);
                QCoreApplication::processEvents();
                verify("timeline-340");
                const QString bars = window.TimelineBarGeometryForTest();
                std::fprintf(stderr, "T6.4 dpi_bars %s split=%s view=%s tier=%s\n",
                             bars.toUtf8().constData(),
                             window.SplitterStateForTest().toUtf8().constData(),
                             window.TimelineViewFramesForTest().toUtf8().constData(),
                             window.TransportTierForTest().toUtf8().constData());
                std::fflush(stderr);
                check("timeline-340-height",
                      !room_for_340 ||
                          window.SplitterStateForTest().contains(
                              QStringLiteral("tl=340")));
                const QStringList tall = window.LayoutProblemsForTest();
                for (const QString& problem : tall) {
                  std::fprintf(stderr, "T6.4 dpi_problem timeline-340 %s\n",
                               problem.toUtf8().constData());
                }
                check("timeline-340-layout", tall.isEmpty());
                auto int_value = [](const QString& text, const QString& key) {
                  const int at = text.indexOf(key + QLatin1Char('='));
                  if (at < 0) {
                    return -999;
                  }
                  int start = at + key.size() + 1;
                  int end = start;
                  while (end < text.size() && text.at(end).isDigit()) {
                    ++end;
                  }
                  return text.mid(start, end - start).toInt();
                };
                // 3C 新用例 ①：折叠检查器 44±1（LayoutProblems 的 inspector-width
                // 分支 + rail 可见）。折叠只 hide，MarkPanel 与数据不重建。
                window.SetInspectorCollapsedForTest(true);
                QCoreApplication::processEvents();
                std::fprintf(stderr, "T6.4 dpi_inspector collapsed %s\n",
                             window.InspectorStateForTest().toUtf8().constData());
                std::fflush(stderr);
                QStringList collapsed_problems = window.LayoutProblemsForTest();
                for (const QString& problem : collapsed_problems) {
                  std::fprintf(stderr, "T6.4 dpi_problem inspector-collapsed %s\n",
                               problem.toUtf8().constData());
                }
                check("inspector-collapsed-layout", collapsed_problems.isEmpty());
                check("inspector-collapsed-state",
                      window.InspectorCollapsedForTest() &&
                          window.InspectorStateForTest().contains(
                              QStringLiteral("collapsed=1")) &&
                          window.InspectorStateForTest().contains(
                              QStringLiteral("rail=1")));
                const QString tag_name =
                    dpi_tag.isEmpty() ? QStringLiteral("x") : dpi_tag;
                check("inspector-collapsed-shot",
                      window.grab().save(
                          QDir::currentPath() + QStringLiteral("/build/t6.4-dpi-") +
                          tag_name +
                          QStringLiteral("-inspector-collapsed.png")));
                // 3C 新用例 ②：展开后把检查器拖到极值（560 / 220；窄窗口按
                // “画面列 ≥420”夹取），两种宽度下布局都必须合法。
                window.SetInspectorCollapsedForTest(false);
                QCoreApplication::processEvents();
                const QString tier_name = window.DensityNameForTest();
                const int workspace_margin =
                    tier_name == QStringLiteral("regular") ? 6 : 4;
                const int split_total = window.width() - 2 * workspace_margin;
                const int wide_expected = std::max(
                    220, std::min(560, split_total - 6 - 420));
                window.SetSplitSizesForTest(340, wide_expected);
                QCoreApplication::processEvents();
                const QString wide_state = window.InspectorStateForTest();
                std::fprintf(stderr,
                             "T6.4 dpi_inspector wide_expected=%d %s\n",
                             wide_expected, wide_state.toUtf8().constData());
                std::fflush(stderr);
                QStringList wide_problems = window.LayoutProblemsForTest();
                for (const QString& problem : wide_problems) {
                  std::fprintf(stderr, "T6.4 dpi_problem inspector-wide %s\n",
                               problem.toUtf8().constData());
                }
                check("inspector-wide-width",
                      std::abs(int_value(window.SplitterStateForTest(),
                                         QStringLiteral("ins")) -
                               wide_expected) <= 2);
                check("inspector-wide-layout", wide_problems.isEmpty());
                check("inspector-wide-shot",
                      window.grab().save(
                          QDir::currentPath() + QStringLiteral("/build/t6.4-dpi-") +
                          tag_name + QStringLiteral("-inspector-wide.png")));
                // 3C：三个 tab 都要能切（截图页懒建缩略图 / 信息页 11 行/
                // 切回标记页），并各留一张截图做视觉证据。
                window.SetInspectorTabForTest(1);
                const QString shots_state = window.InspectorStateForTest();
                std::fprintf(stderr, "T6.4 dpi_inspector shots %s\n",
                             shots_state.toUtf8().constData());
                std::fflush(stderr);
                QStringList shots_problems = window.LayoutProblemsForTest();
                check("inspector-shots-page",
                      shots_state.contains(QStringLiteral("tab=shots")) &&
                          shots_state.contains(QStringLiteral("page=1")));
                check("inspector-shots-layout", shots_problems.isEmpty());
                check("inspector-shots-shot",
                      window.grab().save(
                          QDir::currentPath() + QStringLiteral("/build/t6.4-dpi-") +
                          tag_name + QStringLiteral("-inspector-shots.png")));
                window.SetInspectorTabForTest(2);
                const QString info_state = window.InspectorStateForTest();
                std::fprintf(stderr, "T6.4 dpi_inspector info %s\n",
                             info_state.toUtf8().constData());
                std::fflush(stderr);
                QStringList info_problems = window.LayoutProblemsForTest();
                check("inspector-info-page",
                      info_state.contains(QStringLiteral("tab=info")) &&
                          info_state.contains(QStringLiteral("page=2")));
                check("inspector-info-rows",
                      int_value(info_state, QStringLiteral("info")) > 0);
                check("inspector-info-layout", info_problems.isEmpty());
                check("inspector-info-shot",
                      window.grab().save(
                          QDir::currentPath() + QStringLiteral("/build/t6.4-dpi-") +
                          tag_name + QStringLiteral("-inspector-info.png")));
                window.SetInspectorTabForTest(0);
                window.SetSplitSizesForTest(340, 220);
                QCoreApplication::processEvents();
                const QString narrow_state = window.InspectorStateForTest();
                std::fprintf(stderr, "T6.4 dpi_inspector narrow %s\n",
                             narrow_state.toUtf8().constData());
                std::fflush(stderr);
                QStringList narrow_problems = window.LayoutProblemsForTest();
                for (const QString& problem : narrow_problems) {
                  std::fprintf(stderr, "T6.4 dpi_problem inspector-narrow %s\n",
                               problem.toUtf8().constData());
                }
                check("inspector-narrow-width",
                      std::abs(int_value(window.SplitterStateForTest(),
                                         QStringLiteral("ins")) -
                               220) <= 2);
                check("inspector-narrow-layout", narrow_problems.isEmpty());
                check("inspector-narrow-shot",
                      window.grab().save(
                          QDir::currentPath() + QStringLiteral("/build/t6.4-dpi-") +
                          tag_name + QStringLiteral("-inspector-narrow.png")));
                QCoreApplication::exit(*failures == 0 ? 0 : 1);
              });
            });
          });
        });
      }
      if (banner_test) {
        // T6.5：真实素材（4K/VFR/10-bit/无音频/错误）+ 合成全量提示，
        // 验证同一时刻只有一条横幅、其余进状态栏、且不遮挡时间轴/帧号/播放控件。
        auto failures = std::make_shared<int>(0);
        auto checks = std::make_shared<int>(0);
        struct BannerTestState {
          int baseline_info_h = 0;
          qint64 baseline_frame = -1;
          bool have_4k = false;
          bool have_vfr = false;
          bool have_10bit = false;
          bool have_noaudio = false;
        };
        auto state = std::make_shared<BannerTestState>();
        auto banner_perf = std::make_shared<std::vector<double>>();
        banner_perf->reserve(50);
        const QString base_video = file_path;
        const QString fourk_video = file_b;
        const QString vfr_video = banner_vfr;
        const QString tenbit_video = banner_tenbit;
        const QString noaudio_video = banner_noaudio;
        const QString shot_tag = banner_tag;
        state->have_4k = !fourk_video.isEmpty() && QFileInfo(fourk_video).isFile();
        state->have_vfr = !vfr_video.isEmpty() && QFileInfo(vfr_video).isFile();
        state->have_10bit =
            !tenbit_video.isEmpty() && QFileInfo(tenbit_video).isFile();
        state->have_noaudio =
            !noaudio_video.isEmpty() && QFileInfo(noaudio_video).isFile();
        auto schedule = [&window](int ms, auto fn) {
          QTimer::singleShot(ms, Qt::PreciseTimer, &window, std::move(fn));
        };
        auto check = [failures, checks](const char* name, bool ok) {
          ++(*checks);
          if (!ok) {
            ++(*failures);
          }
          std::fprintf(stderr, "T6.5 banner_check %s ok=%d\n", name, ok ? 1 : 0);
          std::fflush(stderr);
        };
        auto log_state = [&window](const char* step) {
          const QByteArray state_text = window.BannerStateForTest().toUtf8();
          std::fprintf(stderr, "T6.5 banner_state %s %s\n", step,
                       state_text.constData());
          std::fflush(stderr);
        };
        auto check_layout = [&window, check, log_state](const char* phase) {
          if (window.centralWidget() != nullptr &&
              window.centralWidget()->layout() != nullptr) {
            window.centralWidget()->layout()->activate();
          }
          log_state(phase);
          const QStringList problems = window.LayoutProblemsForTest();
          for (const QString& problem : problems) {
            std::fprintf(stderr, "T6.5 banner_problem %s %s\n", phase,
                         problem.toUtf8().constData());
          }
          const std::string prefix(phase);
          check((prefix + "-layout").c_str(), problems.isEmpty());
          const QString ui = window.BannerUiStateForTest();
          std::fprintf(stderr, "T6.5 banner_ui %s %s\n", phase,
                       ui.toUtf8().constData());
          std::fflush(stderr);
          check((prefix + "-controls").c_str(),
                ui.contains(QStringLiteral("timeline=1")) &&
                    ui.contains(QStringLiteral("play=1")) &&
                    ui.contains(QStringLiteral("volume=1")) &&
                    ui.contains(QStringLiteral("loop=1")) &&
                    ui.contains(QStringLiteral("frame=帧")));
          check((prefix + "-one-banner").c_str(),
                window.BannerStateForTest().contains(QStringLiteral("banners=1")));
        };
        auto shot = [&window, shot_tag, check](const char* phase) {
          const QString dir = QDir::currentPath() + QStringLiteral("/build");
          QDir().mkpath(dir);
          const QString tag =
              shot_tag.isEmpty() ? QStringLiteral("x") : shot_tag;
          const QString path = dir + QStringLiteral("/t6.5-banner-") + tag +
                               QStringLiteral("-") + QString::fromLatin1(phase) +
                               QStringLiteral(".png");
          const bool saved = window.grab().save(path);
          std::fprintf(stderr, "T6.5 banner_shot %s path=%s ok=%d\n", phase,
                       path.toUtf8().constData(), saved ? 1 : 0);
          std::fflush(stderr);
          check((std::string(phase) + "-shot").c_str(), saved);
        };
        // 打开失败会弹模态框；测试里 250ms 兜底关闭，避免任何意外素材把进程卡住。
        auto* watchdog = new QTimer(&window);
        QObject::connect(watchdog, &QTimer::timeout, &window, []() {
          auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
          if (box != nullptr) {
            std::fprintf(stderr, "T6.5 banner_modal_closed\n");
            std::fflush(stderr);
            box->done(QMessageBox::Cancel);
          }
        });
        watchdog->start(250);

        schedule(500, [&window, state, check, check_layout, shot]() {
          const QString banner = window.BannerStateForTest();
          check("baseline-kind", banner.contains(QStringLiteral("kind=none")));
          check("baseline-hidden", !banner.contains(QStringLiteral("visible=1")));
          check("baseline-secondary", window.SecondaryStatusTextForTest().isEmpty());
          state->baseline_info_h = window.InfoBarHeightForTest();
          state->baseline_frame = window.CurrentFrameForTest();
          check_layout("baseline");
          shot("baseline");
        });

        if (state->have_4k) {
          schedule(750, [&window, fourk_video]() { window.OpenFile(fourk_video); });
          schedule(1250, [&window, state, check, check_layout, shot]() {
            const QString banner = window.BannerStateForTest();
            check("4k-kind", banner.contains(QStringLiteral("kind=out-of-range")));
            check("4k-visible", banner.contains(QStringLiteral("visible=1")));
            check("4k-resolution", banner.contains(QStringLiteral("3840×2160")));
            check("4k-height",
                  window.InfoBarHeightForTest() > state->baseline_info_h);
            check_layout("4k");
            shot("4k");
          });
        } else {
          std::fprintf(stderr, "T6.5 banner_skip 4k\n");
        }

        if (state->have_vfr) {
          schedule(1600, [&window, vfr_video]() { window.OpenFile(vfr_video); });
          schedule(2100, [&window, check, check_layout, shot]() {
            const QString banner = window.BannerStateForTest();
            check("vfr-kind", banner.contains(QStringLiteral("kind=vfr")));
            check("vfr-text", banner.contains(QStringLiteral("可变帧率")));
            check("vfr-scanning-secondary",
                  banner.contains(QStringLiteral("扫描中")));
            check_layout("vfr");
            shot("vfr");
          });
        } else {
          std::fprintf(stderr, "T6.5 banner_skip vfr\n");
        }

        if (state->have_10bit) {
          schedule(2400, [&window, tenbit_video]() { window.OpenFile(tenbit_video); });
          schedule(2900, [&window, check, check_layout, shot]() {
            const QString banner = window.BannerStateForTest();
            check("10bit-kind", banner.contains(QStringLiteral("kind=10-bit")));
            check("10bit-text", banner.contains(QStringLiteral("颜色")));
            check("10bit-noaudio-secondary",
                  banner.contains(QStringLiteral("无音频")));
            check_layout("10bit");
            shot("10bit");
          });
        } else {
          std::fprintf(stderr, "T6.5 banner_skip 10bit\n");
        }

        if (state->have_noaudio) {
          schedule(3200, [&window, noaudio_video]() { window.OpenFile(noaudio_video); });
          schedule(3700, [&window, check, check_layout, shot]() {
            const QString banner = window.BannerStateForTest();
            check("noaudio-kind", banner.contains(QStringLiteral("kind=no-audio")));
            check("noaudio-text", banner.contains(QStringLiteral("波形")));
            check("noaudio-only", window.SecondaryStatusTextForTest().isEmpty());
            check_layout("noaudio");
            shot("noaudio");
          });
        } else {
          std::fprintf(stderr, "T6.5 banner_skip noaudio\n");
        }

        schedule(4000, [&window, base_video]() { window.OpenFile(base_video); });
        schedule(4500, [&window, check, check_layout]() {
          const QString banner = window.BannerStateForTest();
          check("restore-kind", banner.contains(QStringLiteral("kind=none")));
          check("restore-secondary", window.SecondaryStatusTextForTest().isEmpty());
          check_layout("restore");
        });

        // V8：打开不存在/损坏文件，错误横幅最高优先级，旧画面保留。
        schedule(4700, [&window, state, check, check_layout, shot, base_video]() {
          const QString missing = base_video + QStringLiteral(".t6.5-missing.mp4");
          QTimer::singleShot(80, &window, []() {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (box != nullptr) {
              box->done(QMessageBox::Cancel);
            }
          });
          window.OpenFile(missing);
          const QString banner = window.BannerStateForTest();
          check("error-kind", banner.contains(QStringLiteral("kind=error")));
          check("error-visible", banner.contains(QStringLiteral("visible=1")));
          check("error-text", banner.contains(QStringLiteral("打开失败")));
          check("error-secondary", window.SecondaryStatusTextForTest().isEmpty());
          check("error-kept-frame",
                window.CurrentFrameForTest() == state->baseline_frame);
          check_layout("error");
          shot("error");
        });

        schedule(5300, [&window, base_video]() { window.OpenFile(base_video); });
        schedule(5800, [&window, check, check_layout]() {
          const QString banner = window.BannerStateForTest();
          check("error-cleared", banner.contains(QStringLiteral("kind=none")));
          check_layout("cleared");
        });

        // 横幅切换成本（开发机探针）：setText + 重新布局激活，不含 GPU 呈现。
        schedule(5900, [&window, banner_perf]() {
          const QStringList scenarios[] = {
              QStringList{QStringLiteral("vfr"), QStringLiteral("no-audio")},
              QStringList{QStringLiteral("10-bit"), QStringLiteral("scanning")},
              QStringList{QStringLiteral("error")},
              QStringList{QStringLiteral("out-of-range")}};
          QElapsedTimer timer;
          timer.start();
          for (int i = 0; i < 50; ++i) {
            window.SetBannerNoticesForTest(scenarios[i % 4]);
            if (window.centralWidget() != nullptr &&
                window.centralWidget()->layout() != nullptr) {
              window.centralWidget()->layout()->activate();
            }
            banner_perf->push_back(
                static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
            timer.restart();
          }
          window.ClearBannerNoticesForTest();
        });

        // 合成全量：六类同开时只有错误横幅，其余 5 条进状态栏。
        schedule(6000, [&window]() {
          window.SetBannerNoticesForTest(
              QStringList{QStringLiteral("no-audio"), QStringLiteral("scanning"),
                          QStringLiteral("out-of-range"), QStringLiteral("10-bit"),
                          QStringLiteral("vfr"), QStringLiteral("error")});
        });
        schedule(6500, [&window, check, check_layout, shot]() {
          const QString banner = window.BannerStateForTest();
          check("all-kind", banner.contains(QStringLiteral("kind=error")));
          check("all-active",
                banner.contains(QStringLiteral("no-audio")) &&
                    banner.contains(QStringLiteral("scanning")) &&
                    banner.contains(QStringLiteral("out-of-range")) &&
                    banner.contains(QStringLiteral("10-bit")) &&
                    banner.contains(QStringLiteral("vfr")));
          check("all-secondary-count",
                window.SecondaryStatusTextForTest().contains(QStringLiteral("等 5 条")));
          check("all-secondary-visible",
                window.BannerUiStateForTest().contains(QStringLiteral("notice=1")));
          check_layout("all");
          shot("all");
        });

        // 去掉错误后 VFR 成为唯一横幅；再压到 820 窄窗验证换行/不裁切。
        schedule(6800, [&window]() {
          window.SetBannerNoticesForTest(
              QStringList{QStringLiteral("no-audio"), QStringLiteral("scanning"),
                          QStringLiteral("out-of-range"), QStringLiteral("10-bit"),
                          QStringLiteral("vfr")});
        });
        schedule(7200, [&window, check, check_layout]() {
          const QString banner = window.BannerStateForTest();
          check("vfr-primary", banner.contains(QStringLiteral("kind=vfr")));
          check("vfr-secondary-count",
                window.SecondaryStatusTextForTest().contains(QStringLiteral("等 4 条")));
          check_layout("vfr-primary");
          const QSize available = window.AvailableScreenForTest();
          const int narrow_height =
              std::min(500, std::max(360, available.height() - 64));
          window.resize(820, narrow_height);
        });
        schedule(7800, [&window, check, check_layout, shot]() {
          check("narrow-width", std::abs(window.width() - 820) <= 2);
          check("narrow-mode", window.InfoNarrowModeForTest());
          check("narrow-kind",
                window.BannerStateForTest().contains(QStringLiteral("kind=vfr")));
          check_layout("narrow");
          shot("narrow");
        });
        schedule(8300, [&window, failures, checks, check, log_state,
                        banner_perf]() {
          log_state("final");
          std::vector<double> samples = *banner_perf;
          std::sort(samples.begin(), samples.end());
          if (!samples.empty()) {
            const double p50 = samples[samples.size() / 2];
            const std::size_t p95_index = std::min(
                samples.size() - 1,
                static_cast<std::size_t>(static_cast<double>(samples.size()) * 0.95));
            std::fprintf(stderr,
                         "T6.5 banner_perf updates=%zu p50=%.3fms p95=%.3fms "
                         "max=%.3fms\n",
                         samples.size(), p50, samples[p95_index], samples.back());
          }
          std::fprintf(stderr,
                       "T6.5 banner_summary checks=%d fails=%d dpr=%.2f info_h=%d\n",
                       *checks, *failures, window.devicePixelRatioF(),
                       window.InfoBarHeightForTest());
          std::fflush(stderr);
          QCoreApplication::exit(*failures == 0 ? 0 : 1);
        });
      }
    });
  }
  return QApplication::exec();
}
