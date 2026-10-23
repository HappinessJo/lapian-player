#pragma once

#include <QString>

class MainWindow;

namespace lapian::app {

struct OnlineBufferTestOptions {
  QString media_path;
  int timeout_ms = 20000;
};

// T8.6 缓冲耗尽专项：本地文件作为 fake CDN，先给 600KB，再停 3s，再补齐。
// 验证播放中缓冲区耗尽 → 自动暂停/提示 → 数据到达后从当前帧恢复。
class OnlineBufferTestRunner {
 public:
  OnlineBufferTestRunner(MainWindow* window, OnlineBufferTestOptions options);
  void Start();

 private:
  void Run();
  MainWindow* window_ = nullptr;
  OnlineBufferTestOptions options_;
  int checks_ = 0;
  int failures_ = 0;
};

}  // namespace lapian::app
