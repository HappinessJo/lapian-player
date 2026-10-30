#include "app/OnlineCacheManageTestRunner.h"

#include <cstdio>

namespace lapian::app {

int RunOnlineCacheManageTest(const OnlineCacheManageTestOptions&) {
  std::fprintf(stderr, "online_cache_manage_test unavailable on macOS: Windows-only GUI probe\n");
  return 2;
}

}  // namespace lapian::app
