#pragma once

#include <cstdint>

namespace lapian::app {

// T7.2/T7.3：进程内存与资源采样。working set / private bytes（commit）来自
// GetProcessMemoryInfo，handle 数来自 GetProcessHandleCount，线程数来自
// Toolhelp32Snapshot（只统计当前进程）。
struct ProcessMemorySample {
  bool valid = false;
  std::uint64_t working_set_bytes = 0;
  std::uint64_t peak_working_set_bytes = 0;
  std::uint64_t private_bytes = 0;
  std::uint32_t handle_count = 0;
  std::uint32_t thread_count = 0;
};

bool SampleProcessMemory(ProcessMemorySample* out);

}  // namespace lapian::app
