#pragma once

#include <windows.h>

#include <cstdio>
#include <string>

namespace lapian::gfx {

// 把 HRESULT 格式化成 "0x8007000E 系统消息"，用于错误上报与日志。
inline std::string HResultText(HRESULT hr) {
  char buffer[512] = {};
  const DWORD length = FormatMessageA(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
      static_cast<DWORD>(hr), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer,
      static_cast<DWORD>(sizeof(buffer)), nullptr);

  char code[32] = {};
  std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(hr));

  std::string text(code);
  if (length != 0) {
    std::string message(buffer, length);
    while (!message.empty() &&
           (message.back() == '\r' || message.back() == '\n' || message.back() == ' ')) {
      message.pop_back();
    }
    if (!message.empty()) {
      text += " ";
      text += message;
    }
  }
  return text;
}

}  // namespace lapian::gfx
