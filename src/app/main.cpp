#include <QAbstractButton>
#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
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
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>
#include <psapi.h>

#include "app/MainWindow.h"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);

  // T6.3 的设置会用到组织名/应用名，这里先固定下来
  QCoreApplication::setOrganizationName(QStringLiteral("拉片软件"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("lapian.local"));
  QCoreApplication::setApplicationName(QStringLiteral("拉片软件"));
  QCoreApplication::setApplicationVersion(QStringLiteral(LAPIAN_VERSION));

  // T1.4：支持 --file <path> 与系统文件关联传入的第一个本地路径。
  QString file_path;
  QString file_b;
  QString goto_text;
  bool auto_play = false;
  bool step_test = false;
  bool ab_test = false;
  bool loop_test = false;
  bool mark_test = false;
  bool note_test = false;
  bool snapshot_test = false;
  bool fullscreen_test = false;
  bool key_test = false;
  bool settings_test = false;
  bool settings_expect = false;
  QString settings_path_arg;
  int settings_expect_volume = -1;
  bool settings_expect_waveform = false;
  QString snapshot_pictures_arg;
  double playback_rate = 1.0;
  bool has_playback_rate = false;
  const QStringList arguments = QCoreApplication::arguments();
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
    } else if (arguments[i] == QStringLiteral("--snapshot-pictures") &&
               i + 1 < arguments.size()) {
      snapshot_pictures_arg = arguments[++i];
    } else if (!arguments[i].startsWith(QLatin1Char('-')) && file_path.isEmpty()) {
      file_path = arguments[i];
    }
  }

  if (loop_test) {
    qputenv("LAPIAN_LOOP_LOG", "1");
  }
  if (mark_test) {
    qputenv("LAPIAN_MARK_LOG", "1");
  }
  if (note_test) {
    qputenv("LAPIAN_MARK_LOG", "1");
  }
  // T6.3：--settings-test 用独立根，默认 build/t6.3-settings，避免碰用户配置；
  // --settings-path 可指向 %APPDATA%\拉片软件 做真实默认路径验证。
  QString settings_root = settings_path_arg;
  if ((settings_test || settings_expect) && settings_root.isEmpty()) {
    settings_root = QDir::currentPath() + QStringLiteral("/build/t6.3-settings");
  }
  if (settings_test || settings_expect) {
    qputenv("LAPIAN_SETTINGS_LOG", "1");
  }
  if (settings_test) {
    QDir().mkpath(settings_root);
    const QString settings_file =
        QDir(settings_root).filePath(QStringLiteral("settings.json"));
    if (QFileInfo(settings_file).isDir()) {
      QDir(settings_file).removeRecursively();
    } else {
      QFile::remove(settings_file);
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

  const auto process_start = std::chrono::steady_clock::now();
  MainWindow window(nullptr, settings_root);
  if (!snapshot_pictures_root.isEmpty()) {
    // 通过构造函数后的 setter 传 Unicode 路径，避免 qputenv 的 ANSI 限制。
    window.SetSnapshotPicturesRootForTest(snapshot_pictures_root);
  }
  window.show();
  if (!file_path.isEmpty()) {
    QTimer::singleShot(0, &window, [&window, &app, file_path, file_b, goto_text, auto_play,
                                    step_test, ab_test, loop_test, mark_test, note_test,
                                    snapshot_test, fullscreen_test, key_test, settings_test,
                                    settings_expect, settings_expect_volume,
                                    settings_expect_waveform, settings_root,
                                    snapshot_pictures_root, playback_rate,
                                    has_playback_rate, process_start]() {
      window.OpenFile(file_path);
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
        schedule(10000, [&window, log_state]() {
          window.TogglePlaybackForTest();  // 暂停，检查 3 次循环后 A/B 不变
          log_state("after-3-loops");
        });
        schedule(10200, [&window, log_state]() {
          window.ToggleLoopForTest();  // F4-A3：关闭循环
          window.GoToInputForTest(QStringLiteral("100"));
          window.StartPlaybackForTest();
          log_state("loop-off-play");
        });
        schedule(13000, [&window, log_state]() {
          log_state("after-161");  // 应已越过 160/161，且不回到 A
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
        schedule(18600, [&window, log_state]() {
          log_state("freeze-5s");  // 5s 后仍应 frame=100、音频时钟无效
        });
        schedule(18900, [&window, log_state]() {
          window.StepForwardForTest();  // F4-A10：定帧中 PgDn 到 101
          log_state("after-step");
        });
        schedule(19200, [&window, log_state]() {
          window.StartPlaybackForTest();  // Space：回到 100 重新定住
          log_state("refreeze");
        });
        schedule(19500, [&window, log_state]() {
          log_state("refreeze-hold");
        });
        schedule(19700, [&window, log_state]() {
          window.ToggleLoopForTest();  // F4-A10：关闭循环，从 100 恢复播放
          log_state("loop-off-resume");
        });
        schedule(20000, [&window, log_state]() {
          log_state("resume-after-loop-off");
          window.TogglePlaybackForTest();  // Space 暂停
        });
        schedule(20200, [&window, log_state]() {
          log_state("paused");
        });
        QTimer::singleShot(20500, Qt::PreciseTimer, &app, &QCoreApplication::quit);
      }
      if (mark_test) {
        // T5.2：101 个标记（0–100）、重开、点击跳转、100 标记操作与性能、
        // 删除持久化、空文件删除、损坏 sidecar 警告、F8-A3 文件隔离。
        const QString mark_a = file_path;
        const QString mark_b = file_b;
        const QString sidecar_a = mark_a + QStringLiteral(".lapian.json");
        const QString sidecar_b = mark_b + QStringLiteral(".lapian.json");
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
        QTimer::singleShot(11500, Qt::PreciseTimer, &app, &QCoreApplication::quit);
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
        schedule(1350, [&window, check]() {
          const QString state = window.MarkStateForTest();
          std::fprintf(stderr, "T5.3 note_click %s\n", state.toUtf8().constData());
          std::fflush(stderr);
          check("f6a1-click-jump-pause",
                state.contains(QStringLiteral("frame=100")) &&
                    state.contains(QStringLiteral("playing=0")));
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

          PROCESS_MEMORY_COUNTERS memory{};
          const bool memory_ok =
              GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)) != FALSE;
          const double working_mb =
              memory_ok ? static_cast<double>(memory.WorkingSetSize) / (1024.0 * 1024.0) : 0.0;
          const double peak_mb = memory_ok
                                     ? static_cast<double>(memory.PeakWorkingSetSize) /
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
    });
  }
  return QApplication::exec();
}
