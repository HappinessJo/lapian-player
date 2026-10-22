#pragma once

// T8.3 缓存存储/配额探针（--online-cache-test，无 GUI）。
//
// 只走 CacheStore + SettingsStore：临时根、合成（稀疏）文件，
// **不访问网络、不需要 SESSDATA、不碰真实 %LOCALAPPDATA% 缓存**；
// 退出时清理临时根。覆盖：
// - 默认根（%LOCALAPPDATA%\拉片软件\cache，不叠两层）与默认配额；
// - 300MB 配额下的 LRU 顺序、当前条目不淘汰、总占用 ≤ 配额；
// - .part 续传长度（新实例按文件长度校正）；
// - 损坏检测（截断 data.mp4）与重建；
// - 磁盘满预检（不可满足的 expected，≤5s 可读错误）；
// - 清空缓存保留在线记录 sentinel；
// - 缓存根/配额跨会话（settings.json 新键）；
// - Unicode + >MAX_PATH 长路径的创建/改名/读取；
// - meta.json 无明文 Cookie/签名 URL。
//
// 输出（key=value）：root/quota/usage/entries/lru/evicted/protected/resume/
// corrupt/rebuild/clear/records_kept/space_error_ms/cross_session/long_path/
// plaintext/checks/failures/elapsed_ms/verdict。
// 退出码：0 = verdict=pass；1 = 有失败；2 = 临时目录不可用。

#include <QString>

namespace lapian::app {

struct OnlineCacheTestOptions {
  QString root;  // 空 = <exe>/../t8.3-cache-<pid>；只用于调试
  bool keep_root = false;
};

int RunOnlineCacheTest(const OnlineCacheTestOptions& options);

}  // namespace lapian::app
