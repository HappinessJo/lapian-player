#pragma once

#include <QString>

#include <functional>

class MainWindow;

namespace lapian::app {

struct OnlineM2TestOptions {
  QString bvid;
  QString cid_text;
  qint64 cid = 0;
  QString tag;
  int timeout_ms = 30000;
  int play_seconds = 60;
  int seeks = 20;
  bool force_durl = false;
  bool cache_hit_only = false;
  bool skip_cache_hit = false;
  QString cache_root;
  QString pictures_root;
  QString records_root;
  QString sessdata_file;
};

// T8.10 真实窗口验收：DASH 双输入首帧/播放、60s 音画偏差、20 次已缓存 seek
// 落点 0 + seek 后音频从当前帧起、缓存命中 0 网络、内存/关窗；`--force-durl`
// 走 M1 回退路径。只走产品入口/真实网络，不注入假数据。
class OnlineM2TestRunner {
 public:
  OnlineM2TestRunner(MainWindow* window, OnlineM2TestOptions options);
  void Start();

 private:
  void Run();
  bool PumpUntil(const std::function<bool()>& predicate, int timeout_ms,
                 const char* label);
  void PumpFor(int ms);
  bool StartAndWaitReady(const QString& bvid, const QString& title,
                         qint64 duration_ms, const QString& sessdata,
                         int timeout_ms, double* ready_ms);

  MainWindow* window_ = nullptr;
  OnlineM2TestOptions options_;
  int checks_ = 0;
  int failures_ = 0;
};

}  // namespace lapian::app
