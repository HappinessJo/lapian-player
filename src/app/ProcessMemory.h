#pragma once

#include <cstdint>

namespace lapian::app {

// T7.2：进程内存采样。working set / private bytes（commit）都来自
// GetProcessMemoryInfo，handle 数来自 GetProcessHandleCount。
struct ProcessMemorySample {
  bool valid = false;
  std::uint64_t working_set_bytes = 0;
  std::uint64_t peak_working_set_bytes = 0;
  std::uint64_t private_bytes = 0;
  std::uint32_t handle_count = 0;
};

bool SampleProcessMemory(ProcessMemorySample* out);

}  // namespace lapian::app
