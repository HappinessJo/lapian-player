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
  bool auto_play = false;
  const QStringList arguments = QCoreApplication::arguments();
  for (int i = 1; i < arguments.size(); ++i) {
    if (arguments[i] == QStringLiteral("--file") && i + 1 < arguments.size()) {
      file_path = arguments[++i];
    } else if (arguments[i] == QStringLiteral("--play")) {
      auto_play = true;
    } else if (!arguments[i].startsWith(QLatin1Char('-')) && file_path.isEmpty()) {
      file_path = arguments[i];
    }
  }

  MainWindow window;
  window.show();
  if (!file_path.isEmpty()) {
    QTimer::singleShot(0, &window, [&window, file_path, auto_play]() {
      window.OpenFile(file_path);
      if (auto_play) {
        window.StartPlaybackForTest();
      }
    });
  }
  return QApplication::exec();
}
