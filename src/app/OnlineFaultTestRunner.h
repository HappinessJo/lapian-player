#pragma once

#include <QString>

#include <functional>

class MainWindow;

namespace lapian::app {

struct OnlineFaultTestOptions {
  QString bvid;
  qint64 cid = 0;
  int page = 1;
  QString tag = QStringLiteral("t8.13-core");
  QString media_path;
  QString cache_root;
  QString pictures_root;
  QString records_root;
  // 只跑一个场景（调试用）；空 = 全跑。
  QString only;
  int timeout_ms = 120000;
};

// T8.13 真实窗口故障探针：全部走产品入口（MainWindow::StartOnlineSession →
// OnlineSession → DownloadJob → GrowingSource → MediaSession），但网络/取流用
// 注入的假 transport（本地媒体字节 + 故障脚本），不联网、不需要 SESSDATA。
//
// 验证：断网重试/续传、失败后已缓存区可播、未缓存 seek 断网 ≤5s、网络无响应
// 看门狗、磁盘不足、缓存损坏自动重建、下载中/未缓存 seek 中关闭 ≤1s、
// 休眠模拟（下载 stall 3s 后恢复）、标记 sentinel、UI 停顿 >200ms = 0、脱敏。
// 真实 Windows sleep 不可自动执行 → 报告“未验证”，不冒充通过。
class OnlineFaultTestRunner {
 public:
  OnlineFaultTestRunner(MainWindow* window, OnlineFaultTestOptions options);
  void Start();

 private:
  void Run();
  bool PumpUntil(const std::function<bool()>& predicate, int timeout_ms,
                 const char* label);
  void PumpFor(int ms);
  bool RunScenario(const QString& name, const std::function<void()>& body);
  void Check(const QString& name, bool ok, const QString& detail = QString());

  QString CurrentBvid() const;

  MainWindow* window_ = nullptr;
  OnlineFaultTestOptions options_;
  int checks_ = 0;
  int failures_ = 0;
  int scenarios_ = 0;
  int scenario_pass_ = 0;
  int scenario_fail_ = 0;
  // 当前场景内计数（用于“本场景是否通过”）。
  int scenario_checks_ = 0;
  int scenario_failures_ = 0;
  QString scenario_name_;
};

}  // namespace lapian::app
