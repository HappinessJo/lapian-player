#include <QApplication>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QTimer>

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
  QString goto_text;
  bool auto_play = false;
  double playback_rate = 1.0;
  bool has_playback_rate = false;
  const QStringList arguments = QCoreApplication::arguments();
  for (int i = 1; i < arguments.size(); ++i) {
    if (arguments[i] == QStringLiteral("--file") && i + 1 < arguments.size()) {
      file_path = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--goto") && i + 1 < arguments.size()) {
      goto_text = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--rate") && i + 1 < arguments.size()) {
      playback_rate = arguments[++i].toDouble();
      has_playback_rate = true;
    } else if (arguments[i] == QStringLiteral("--play")) {
      auto_play = true;
    } else if (!arguments[i].startsWith(QLatin1Char('-')) && file_path.isEmpty()) {
      file_path = arguments[i];
    }
  }

  MainWindow window;
  window.show();
  if (!file_path.isEmpty()) {
    QTimer::singleShot(0, &window, [&window, file_path, goto_text, auto_play, playback_rate,
                                    has_playback_rate]() {
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
    });
  }
  return QApplication::exec();
}
