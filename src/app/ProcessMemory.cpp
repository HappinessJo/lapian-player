#include "app/ProcessMemory.h"

#include <windows.h>

#include <psapi.h>
#include <tlhelp32.h>

namespace lapian::app {
namespace {

// 只统计本进程线程；快照失败时返回 0，不影响其他字段的有效性。
std::uint32_t CountProcessThreads(DWORD process_id) {
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return 0;
  }
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  std::uint32_t count = 0;
  if (Thread32First(snapshot, &entry) != FALSE) {
    do {
      if (entry.th32OwnerProcessID == process_id) {
        ++count;
      }
      entry.dwSize = sizeof(entry);
    } while (Thread32Next(snapshot, &entry) != FALSE);
  }
  CloseHandle(snapshot);
  return count;
}

}  // namespace

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
  out->thread_count = CountProcessThreads(GetCurrentProcessId());
  out->valid = true;
  return true;
}

}  // namespace lapian::app
