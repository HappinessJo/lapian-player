#pragma once

#include <QString>

class MainWindow;

namespace lapian::app {

struct OnlineOfflineTestOptions {
  QString bvid;
  QString cid_text;
  qint64 cid = 0;
  QString tag;
  QString media_path;       // 用于构造完整缓存条目的本地 MP4
  QString cache_root;       // 空 = build/t8.7-online-cache
  QString pictures_root;    // 空 = build/t8.7-online-pictures
  QString records_root;     // 空 = build/t8.7-online-records
  int timeout_ms = 20000;
  bool keep_cache = false;  // 不重建缓存，直接打开已有完整条目
  bool skip_corrupt = false;
  // 默认 AppData 路径验收：不覆盖 cache/records；只做命中/记录/重开，跳过清缓存。
  bool use_default_paths = false;
};

// T8.7 真实窗口断网/缓存命中 E2E：
// 构造/复用完整缓存 → 注入永不成功的 HTTP 客户端 → 打开同 BV（0 请求）→
// 标记/备注/截图 → 关窗重开恢复 → 清缓存保留 → 重建回访 → 损坏记录不阻塞。
class OnlineOfflineTestRunner {
 public:
  OnlineOfflineTestRunner(MainWindow* window, OnlineOfflineTestOptions options);
  void Start();

 private:
  MainWindow* window_ = nullptr;
  OnlineOfflineTestOptions options_;
  int checks_ = 0;
  int failures_ = 0;
};

}  // namespace lapian::app
