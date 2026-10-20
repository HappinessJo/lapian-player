#pragma once

#include <mutex>

namespace lapian::gfx {

// D3D11 立即上下文由解码、渲染和截图回读共用；它不是线程安全的。
// 解码/渲染在 UI 线程，截图硬解回读在 SnapshotService 工作线程，双方用同一把
// 递归锁串行化，避免 GPU 命令交错。递归锁用于同线程内 SetFrame -> 上传等嵌套。
inline std::recursive_mutex& D3D11ContextMutex() {
  static std::recursive_mutex mutex;
  return mutex;
}

}  // namespace lapian::gfx
