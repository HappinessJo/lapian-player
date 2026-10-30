#include "app/OnlineGrowingTestRunner.h"

#include <cstdio>

namespace lapian::app {

int RunOnlineGrowingTest(const OnlineGrowingTestOptions&) {
  std::fprintf(stderr, "online_growing_test unavailable on macOS: Windows-only GUI probe\n");
  return 2;
}

}  // namespace lapian::app
