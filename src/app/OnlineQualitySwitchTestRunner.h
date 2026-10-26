#pragma once

// T8.17：在线清晰度切换真实窗口探针。
//
// 场景：默认 1080P（qn=80）播放在线视频 → 等视频+音频缓存都完整 → 切到
// 大会员高码率 1080P+（qn=112）→ 再切回 1080P30（qn=80）。
//
// 断言重点：
// - 不再出现“DASH 条目已完整缓存，但本次未走缓存命中路径”；
// - 实际 qn/codecid 与请求档一致，不静默换 codec/降档；
// - 切档后 PTS/帧号回位、标记/截图/在线记录保留、旧档缓存保留；
// - 切回已完整缓存的 1080P 时 cache-hit，playurl/DASH API 0 请求。
//
// 只走产品入口（MainWindow::StartOnlineSession / TriggerOnlineQualityEntryForTest），
// 不打印凭据/签名 URL；测试根由调用方显式传入。

#include <QString>

class MainWindow;

namespace lapian::app {

struct OnlineQualitySwitchTestOptions {
  QString bvid;
  QString cid_text;
  qint64 cid = 0;
  QString tag;
  int timeout_ms = 60000;
  int high_qn = 112;   // 大会员 1080P+ 高码率
  int back_qn = 80;    // 1080P30
  int wait_complete_ms = 120000;
  // B：缓存命中清晰度菜单探针（--quality-cache-hit-menu）。
  // 打开即缓存命中（0 网络）→ 点开菜单才拉完整目录 → 选非当前档 → 切回缓存档。
  bool cache_hit_menu = false;
  QString sessdata_file;
  QString cache_root;
  QString pictures_root;
  QString records_root;
};

class OnlineQualitySwitchUiTestRunner {
 public:
  OnlineQualitySwitchUiTestRunner(MainWindow* window,
                                  OnlineQualitySwitchTestOptions options);
  void Start();

 private:
  void Run();
  // B：缓存命中菜单按需拉目录 + 切档/切回的独立场景。
  void RunCacheHitMenu();
  void Finish();
  void Check(const QString& name, bool ok, const QString& detail = QString());
  void PrintCache(const QString& phase);

  MainWindow* window_ = nullptr;
  OnlineQualitySwitchTestOptions options_;
  int checks_ = 0;
  int failures_ = 0;
  bool finished_ = false;
  QString switch_one_error_;
  QString switch_two_error_;
};

}  // namespace lapian::app
