#pragma once

#include <QString>

#include <functional>

class MainWindow;

namespace lapian::app {

struct OnlineLongPressTestOptions {
  QString bvid;
  qint64 cid = 0;
  int page = 1;
  QString tag;
  int timeout_ms = 30000;
  // 长按保持时长（默认 2s，与 T8.6.1a 验收一致）。
  int hold_ms = 2000;
  // 已缓存区长按的起点；要留出约 2s 连发（~26 帧）的余量。
  qint64 start_frame = 30;
};

// T8.6.1a 真实窗口探针：在线会话下长按 PgUp/PgDn。
// 覆盖：已缓存区前进/后退 2s（方向/无跳帧/UI gap）、松开停止、短按（单次/快速
// 双击）、首末帧边界停止、未缓存区步进提示与取消（≤100ms）、关窗有界。
// 只走产品入口/真实网络；不注入假数据。
class OnlineLongPressTestRunner {
 public:
  OnlineLongPressTestRunner(MainWindow* window,
                            OnlineLongPressTestOptions options);
  void Start();

 private:
  bool WaitUntil(const std::function<bool()>& predicate, int timeout_ms,
                 const char* label);
  bool WaitStepIdle(int timeout_ms, const char* label);
  // 按住 key 指定时长（期间派发事件，让连发定时器/心跳/seek 完成回调运行）。
  void HoldKey(int key, int hold_ms);
  void Run();

  MainWindow* window_ = nullptr;
  OnlineLongPressTestOptions options_;
  int checks_ = 0;
  int failures_ = 0;
  int quality_ = 0;
};

}  // namespace lapian::app
