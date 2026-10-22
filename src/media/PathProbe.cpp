#include "media/PathProbe.h"

#include <windows.h>

#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>

namespace lapian::media {
namespace {

std::wstring ToWide(const std::string& utf8_path) {
  if (utf8_path.empty()) {
    return {};
  }
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8_path.c_str(),
                                       static_cast<int>(utf8_path.size()), nullptr, 0);
  if (size <= 0) {
    return {};
  }
  std::wstring wide(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8_path.c_str(),
                      static_cast<int>(utf8_path.size()), wide.data(), size);
  return wide;
}

std::string NormalizeSeparators(std::string path) {
  for (char& ch : path) {
    if (ch == '/') {
      ch = '\\';
    }
  }
  return path;
}

bool StartsWithNoCase(const std::string& text, const char* prefix) {
  const std::size_t length = std::char_traits<char>::length(prefix);
  if (text.size() < length) {
    return false;
  }
  for (std::size_t index = 0; index < length; ++index) {
    const unsigned char left = static_cast<unsigned char>(text[index]);
    const unsigned char right = static_cast<unsigned char>(prefix[index]);
    if (std::toupper(left) != std::toupper(right)) {
      return false;
    }
  }
  return true;
}

PathProbeStatus MapWin32Error(DWORD error, std::string* message) {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
    case ERROR_NETWORK_UNREACHABLE:
    case ERROR_NETNAME_DELETED:
    case ERROR_DEV_NOT_EXIST:
    case ERROR_NOT_READY:
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_DRIVER_LEVEL:
      if (message != nullptr) {
        *message = "网络路径或文件不存在（主机/共享可能已离线）";
      }
      return PathProbeStatus::kMissing;
    case ERROR_ACCESS_DENIED:
    case ERROR_NETWORK_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
      if (message != nullptr) {
        *message = "没有读取权限，或文件正被其他程序独占";
      }
      return PathProbeStatus::kPermissionDenied;
    default:
      if (message != nullptr) {
        *message = "无法访问路径（Win32 错误 " + std::to_string(error) + "）";
      }
      return PathProbeStatus::kError;
  }
}

struct ProbeState {
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  DWORD attributes = INVALID_FILE_ATTRIBUTES;
  DWORD error = ERROR_SUCCESS;
};

}  // namespace

bool IsNetworkPath(const std::string& utf8_path) {
  if (utf8_path.empty()) {
    return false;
  }
  const std::string path = NormalizeSeparators(utf8_path);
  if (StartsWithNoCase(path, "\\\\?\\UNC\\")) {
    return true;
  }
  // 扩展/设备命名空间（\\?\C:\...、\\.\COM1、\\?\Volume{...}）都是本地。
  if (StartsWithNoCase(path, "\\\\?\\") || StartsWithNoCase(path, "\\\\.\\")) {
    return false;
  }
  if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') {
    return true;
  }
  if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) &&
      path[1] == ':' && path[2] == '\\') {
    wchar_t root[4] = {static_cast<wchar_t>(path[0]), L':', L'\\', L'\0'};
    return GetDriveTypeW(root) == DRIVE_REMOTE;
  }
  return false;
}

PathProbeResult ProbePathWithTimeout(const std::string& utf8_path, int timeout_ms) {
  PathProbeResult result;
  result.status = PathProbeStatus::kError;
  result.message = "路径为空";
  if (utf8_path.empty()) {
    return result;
  }
  const int wait_ms = timeout_ms > 0 ? timeout_ms : 3000;
  const std::wstring wide = ToWide(utf8_path);
  if (wide.empty()) {
    result.message = "路径不是有效的 UTF-8";
    return result;
  }

  const auto started = std::chrono::steady_clock::now();
  auto state = std::make_shared<ProbeState>();
  std::thread worker;
  try {
    worker = std::thread([state, wide]() {
      const DWORD attributes = GetFileAttributesW(wide.c_str());
      const DWORD error =
          attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->attributes = attributes;
        state->error = error;
        state->done = true;
      }
      state->cv.notify_all();
    });
  } catch (const std::exception&) {
    result.status = PathProbeStatus::kError;
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
    // 工作线程仍卡在 SMB 连接里；它只持有共享状态和宽字符串，detach 是安全的。
    worker.detach();
    result.status = PathProbeStatus::kTimeout;
    const int seconds = (wait_ms + 999) / 1000;
    result.message =
        "网络路径在 " + std::to_string(seconds) + "s 内无响应，已取消打开";
    result.elapsed_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                  started)
            .count();
    return result;
  }
  worker.join();
  if (state->attributes != INVALID_FILE_ATTRIBUTES) {
    result.status = PathProbeStatus::kReachable;
    result.message.clear();
  } else {
    result.status = MapWin32Error(state->error, &result.message);
  }
  result.elapsed_ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - started)
                          .count();
  return result;
}

}  // namespace lapian::media
