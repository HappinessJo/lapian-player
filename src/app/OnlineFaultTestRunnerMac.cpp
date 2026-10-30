#include "app/OnlineFaultTestRunner.h"

#include <QCoreApplication>
#include <cstdio>

namespace lapian::app {

OnlineFaultTestRunner::OnlineFaultTestRunner(MainWindow* window,
                                             OnlineFaultTestOptions options)
    : window_(window), options_(std::move(options)) {}

void OnlineFaultTestRunner::Start() {
  std::fprintf(stderr, "online_fault_test unavailable on macOS: Windows-only GUI probe\n");
  QCoreApplication::exit(2);
}

}  // namespace lapian::app
