#include "media/PathProbe.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <sys/stat.h>
#include <unistd.h>

namespace lapian::media {
namespace {

struct ProbeState {
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  bool exists = false;
  int error = 0;
};

PathProbeStatus MapError(int error, std::string* message) {
  switch (error) {
    case ENOENT:
    case ENOTDIR:
    case ENAMETOOLONG:
      if (message != nullptr) {
        *message = "路径或文件不存在";
      }
      return PathProbeStatus::kMissing;
    case EACCES:
    case EPERM:
    case EROFS:
      if (message != nullptr) {
        *message = "没有读取权限";
      }
      return PathProbeStatus::kPermissionDenied;
    default:
      if (message != nullptr) {
        *message = "无法访问路径（" + std::string(std::strerror(error)) + "）";
      }
      return PathProbeStatus::kError;
  }
}

}  // namespace

bool IsNetworkPath(const std::string& utf8_path) {
  if (utf8_path.empty()) {
    return false;
  }
  std::string path = utf8_path;
  std::replace(path.begin(), path.end(), '/', '\\');
  const auto starts = [&](const char* prefix) {
    return path.rfind(prefix, 0) == 0;
  };
  if (starts("\\\\?\\UNC\\")) {
    return true;
  }
  if (starts("\\\\?\\") || starts("\\\\.\\")) {
    return false;
  }
  return starts("\\\\");
}

PathProbeResult ProbePathWithTimeout(const std::string& utf8_path, int timeout_ms) {
  PathProbeResult result;
  result.status = PathProbeStatus::kError;
  result.message = "路径为空";
  if (utf8_path.empty()) {
    return result;
  }

  const int wait_ms = timeout_ms > 0 ? timeout_ms : 3000;
  const auto started = std::chrono::steady_clock::now();
  auto state = std::make_shared<ProbeState>();
  std::thread worker;
  try {
    worker = std::thread([state, utf8_path]() {
      struct stat info {};
      const int status = ::stat(utf8_path.c_str(), &info);
      const int error = status == 0 ? 0 : errno;
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->exists = status == 0;
        state->error = error;
        state->done = true;
      }
      state->cv.notify_all();
    });
  } catch (const std::exception&) {
    result.message = "无法启动路径预检线程";
    return result;
  }

  bool finished = false;
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    finished = state->cv.wait_for(lock, std::chrono::milliseconds(wait_ms),
                                  [&state]() { return state->done; });
  }
  if (!finished) {
    worker.detach();
    result.status = PathProbeStatus::kTimeout;
    result.message = "路径预检超时，已取消打开";
  } else {
    worker.join();
    if (state->exists) {
      result.status = PathProbeStatus::kReachable;
      result.message.clear();
    } else {
      result.status = MapError(state->error, &result.message);
    }
  }
  result.elapsed_ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - started)
                          .count();
  return result;
}

}  // namespace lapian::media
