#pragma once

// 共享的 WinHTTP 动态加载器（T8.2/T8.5）。
//
// - **动态加载 winhttp.dll**：发布包不出现 WINHTTP.DLL 导入（check-imports），
//   本地模式启动不加载网络栈（O9）；
// - 只从 System32 加载，避免工作目录同名 DLL 劫持；
// - 只供 src/online 内部使用，不对外暴露头文件。

#include <QString>

#include <mutex>

#include <windows.h>
#include <winhttp.h>

namespace lapian::online::detail {

class WinHttpApi {
 public:
  static WinHttpApi& Instance() {
    static WinHttpApi api;
    return api;
  }

  bool Load(QString* error) {
    std::call_once(once_, [this]() { LoadOnce(); });
    if (error != nullptr) {
      *error = load_error_;
    }
    return module_ != nullptr;
  }

  bool loaded() const { return module_ != nullptr; }

  decltype(&::WinHttpOpen) open = nullptr;
  decltype(&::WinHttpCloseHandle) close = nullptr;
  decltype(&::WinHttpConnect) connect = nullptr;
  decltype(&::WinHttpOpenRequest) open_request = nullptr;
  decltype(&::WinHttpSetStatusCallback) set_status_callback = nullptr;
  decltype(&::WinHttpSetTimeouts) set_timeouts = nullptr;
  decltype(&::WinHttpAddRequestHeaders) add_request_headers = nullptr;
  decltype(&::WinHttpSendRequest) send_request = nullptr;
  decltype(&::WinHttpReceiveResponse) receive_response = nullptr;
  decltype(&::WinHttpReadData) read_data = nullptr;
  decltype(&::WinHttpQueryHeaders) query_headers = nullptr;
  decltype(&::WinHttpCrackUrl) crack_url = nullptr;
  decltype(&::WinHttpSetOption) set_option = nullptr;

 private:
  WinHttpApi() = default;

  template <typename T>
  bool Resolve(const char* name, T* target) {
    *target = reinterpret_cast<T>(GetProcAddress(module_, name));
    return *target != nullptr;
  }

  void LoadOnce() {
    module_ =
        LoadLibraryExW(L"winhttp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module_ == nullptr) {
      load_error_ =
          QStringLiteral("无法加载 winhttp.dll（错误 %1）").arg(GetLastError());
      return;
    }
    const bool ok =
        Resolve("WinHttpOpen", &open) &&
        Resolve("WinHttpCloseHandle", &close) &&
        Resolve("WinHttpConnect", &connect) &&
        Resolve("WinHttpOpenRequest", &open_request) &&
        Resolve("WinHttpSetStatusCallback", &set_status_callback) &&
        Resolve("WinHttpSetTimeouts", &set_timeouts) &&
        Resolve("WinHttpAddRequestHeaders", &add_request_headers) &&
        Resolve("WinHttpSendRequest", &send_request) &&
        Resolve("WinHttpReceiveResponse", &receive_response) &&
        Resolve("WinHttpReadData", &read_data) &&
        Resolve("WinHttpQueryHeaders", &query_headers) &&
        Resolve("WinHttpCrackUrl", &crack_url) &&
        Resolve("WinHttpSetOption", &set_option);
    if (!ok) {
      load_error_ = QStringLiteral("winhttp.dll 缺少必要导出");
      FreeLibrary(module_);
      module_ = nullptr;
    }
  }

  std::once_flag once_;
  HMODULE module_ = nullptr;
  QString load_error_;
};

inline WinHttpApi& WinHttpApiInstance() { return WinHttpApi::Instance(); }

}  // namespace lapian::online::detail
