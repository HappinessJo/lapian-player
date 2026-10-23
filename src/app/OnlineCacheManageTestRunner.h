#pragma once

// T8.3.1 在线缓存管理探针（--online-cache-manage-test，无 GUI / 可选 --ui）：
// - 默认根/在线记录根：必须在 %LOCALAPPDATA%/%APPDATA% 下，不能是工作区；
// - 设置里的目录/配额跨会话、改目录不迁移旧缓存；
// - 清空缓存保留当前会话（含 bvid+cid 部分键）与在线记录/截图 sentinel；
// - 孤儿 .part、过期未完成、evict_pending 清理，当前会话不误删；
// - 磁盘满/只读/非法路径的可读错误；
// - `--ui`：设置对话框 + MainWindow 启动清理入口 smoke。
//
// 默认完全落在 build/t8.3.1-* 临时根，不碰真实 AppData 缓存；
// `--manage-default-paths` 才在 Medium 副本上使用 %LOCALAPPDATA%/%APPDATA%
// 下的唯一测试子目录（结束删除），真实默认根只读摘要、不做破坏性操作。
//
// 输出稳定 key=value；退出码 0=pass，1=fail，2=环境不满足。

#include <QString>

namespace lapian::app {

struct OnlineCacheManageTestOptions {
  QString root;           // 空 = build/t8.3.1-cache-manage-<pid>
  QString records_root;  // 空 = <root 的兄弟目录>/records
  QString settings_root; // 空 = <root 的兄弟目录>/settings
  QString readonly_root;  // 已由 ACL 置为只读的目录；空则用“文件冒充目录”
  bool default_paths = false;
  bool ui_smoke = false;
  bool keep_root = false;
};

int RunOnlineCacheManageTest(const OnlineCacheManageTestOptions& options);

}  // namespace lapian::app
