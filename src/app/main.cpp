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

  // T1.3 临时调试入口：--file <path>；正式打开入口在 T1.4。
  QString file_path;
  const QStringList arguments = QCoreApplication::arguments();
  for (int i = 1; i < arguments.size(); ++i) {
    if (arguments[i] == QStringLiteral("--file") && i + 1 < arguments.size()) {
      file_path = arguments[++i];
    }
  }

  MainWindow window;
  window.show();
  if (!file_path.isEmpty()) {
    QTimer::singleShot(0, &window, [&window, file_path]() {
      window.LoadFileForTest(file_path);
    });
  }
  return QApplication::exec();
}
