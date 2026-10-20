#include <QAbstractButton>
#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdio>
#include <chrono>
#include <memory>
#include <utility>

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

  const auto process_start = std::chrono::steady_clock::now();
  MainWindow window;
  window.show();
  if (!file_path.isEmpty()) {
    QTimer::singleShot(0, &window, [&window, &app, file_path, file_b, goto_text, auto_play,
                                    step_test, ab_test, loop_test, mark_test, note_test,
                                    playback_rate, has_playback_rate, process_start]() {
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
    });
  }
  return QApplication::exec();
}
