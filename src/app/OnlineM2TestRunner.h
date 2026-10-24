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
  // T8.14 M2 门探针：OP4 全缓存连续播放停顿、OP5 默认 AppData 断网缓存命中。
  // 两者都直接按 bvid/cid 打开缓存命中路径，不做 nav/view/playurl。
  bool op4 = false;
  bool op5 = false;
  bool default_paths = false;
  // T8.14 缓存种子探针：只用于强制请求 1080P 档，避免默认缓存里旧 720P
  // 条目命中而无法生成 DASH 1080P 种子；0 = 产品默认。
  int requested_qn = 0;
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
