#include "app/ProcessMemory.h"

#include <mach/mach.h>
#include <mach/task_info.h>
#include <sys/resource.h>

#include <vector>

namespace lapian::app {

bool SampleProcessMemory(ProcessMemorySample* out) {
  if (out == nullptr) {
    return false;
  }
  *out = ProcessMemorySample{};

  task_basic_info_64_data_t info{};
  mach_msg_type_number_t count = TASK_BASIC_INFO_64_COUNT;
  const kern_return_t result = task_info(mach_task_self(), TASK_BASIC_INFO_64,
                                         reinterpret_cast<task_info_t>(&info), &count);
  if (result != KERN_SUCCESS) {
    return false;
  }

  out->working_set_bytes = info.resident_size;
  rusage usage{};
  out->peak_working_set_bytes = getrusage(RUSAGE_SELF, &usage) == 0
                                    ? static_cast<std::uint64_t>(usage.ru_maxrss)
                                    : info.resident_size;
  out->private_bytes = info.virtual_size;

  thread_array_t threads = nullptr;
  mach_msg_type_number_t thread_count = 0;
  if (task_threads(mach_task_self(), &threads, &thread_count) == KERN_SUCCESS) {
    out->thread_count = static_cast<std::uint32_t>(thread_count);
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads),
                  static_cast<vm_size_t>(thread_count * sizeof(thread_t)));
  }
  out->valid = true;
  return true;
}

}  // namespace lapian::app
