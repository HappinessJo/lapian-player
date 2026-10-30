#include "app/RobustTestRunner.h"
#include "app/MainWindow.h"

#include <QCoreApplication>
#include <cstdio>

namespace lapian::app {

RobustTestRunner::RobustTestRunner(MainWindow* window, Options options, QObject* parent)
    : QObject(parent == nullptr ? window : parent),
      window_(window),
      options_(std::move(options)) {}

void RobustTestRunner::Start() {
  std::fprintf(stderr, "robust_test unavailable on macOS: Windows-only GUI probe\n");
  QCoreApplication::exit(2);
}

}  // namespace lapian::app
