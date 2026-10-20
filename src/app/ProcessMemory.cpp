#include "app/ProcessMemory.h"

#include <windows.h>

#include <psapi.h>

namespace lapian::app {

bool SampleProcessMemory(ProcessMemorySample* out) {
  if (out == nullptr) {
    return false;
  }
  *out = ProcessMemorySample{};

  PROCESS_MEMORY_COUNTERS_EX counters{};
  const HANDLE process = GetCurrentProcess();
  if (GetProcessMemoryInfo(process,
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                           sizeof(counters)) == FALSE) {
    return false;
  }
  DWORD handles = 0;
  GetProcessHandleCount(process, &handles);

  out->working_set_bytes = counters.WorkingSetSize;
  out->peak_working_set_bytes = counters.PeakWorkingSetSize;
  out->private_bytes = counters.PrivateUsage;
  out->handle_count = static_cast<std::uint32_t>(handles);
  out->valid = true;
  return true;
}

}  // namespace lapian::app
