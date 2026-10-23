#pragma once

#include <QString>

#include <functional>

class MainWindow;

namespace lapian::app {

struct OnlineM1TestOptions {
  QString bvid;
  QString cid_text;
  qint64 cid = 0;
  int page = 1;
  QString tag;
  int timeout_ms = 20000;
  QString cache_root;
  QString pictures_root;
  bool cancel_only = false;
  // T8.8 OP3：未缓存 seek 后等下载追上，验证落点误差 0（默认不等，避免
  // 20 次冷启动测量被整片下载拖慢）。
  bool uncached_landing = false;
};

// T8.6 真实窗口 E2E：输入 BV → 解析确认 → 首帧 → Space → PgUp/PgDn → seek →
// 波形状态 → Ctrl+M → S → 关窗。只走产品入口/真实网络；不注入假数据。
class OnlineM1TestRunner {
 public:
  OnlineM1TestRunner(MainWindow* window, OnlineM1TestOptions options);
  void Start();

 private:
  bool WaitUntil(const std::function<bool()>& predicate, int timeout_ms,
                 const char* label);
  void Run();

  MainWindow* window_ = nullptr;
  OnlineM1TestOptions options_;
  int checks_ = 0;
  int failures_ = 0;
  bool online_ready_ = false;
  qint64 initial_frame_ = 0;
  int quality_ = 0;
};

}  // namespace lapian::app
