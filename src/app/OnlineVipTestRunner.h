#pragma once

// T8.15 大会员可用档条件验收探针（O11，非 MVP 门）。
//
// 两种模式：
// - 无 GUI（`--vip-catalog-only`）：真实 nav + view + playurl(fnval=4048) 目录；
//   分别以 qn=80（产品自动请求）与 qn=120（最大请求）各拉一次，记录实际
//   dash.video[] 的 qn/codecid/fps/尺寸/带宽、默认档、菜单模型标注；
// - `--ui`（默认走真实窗口）：`MainWindow::StartOnlineSession` 按
//   requested_qn/requested_codecid 打开，记录实际 qn/codecid/fps、降帧横幅、
//   档位菜单、已缓存 PgUp/PgDn 严格 ±1 与 p95、4K 播放/逐帧、高档→1080P30
//   切档后的缓存/记录/标记/PTS 映射。
//
// 边界：
// - 只读默认 DPAPI 凭据（或显式 --vip-sessdata-file）；绝不打印/落盘 SESSDATA、
//   Cookie、签名 URL。
// - 不改 OnlineSession/M2 runner 的 nav_normal_user 语义；不改 M1/M2 门与数字。
// - 4K 不设性能门槛；60fps 未达 OP9 只写“可用档/partial”，不阻塞 MVP。
//
// 退出码：0=pass；1=fail；2=blocked（无凭据/非大会员/无档位/缺样本）。

#include <QObject>
#include <QString>

#include <cstdint>

class MainWindow;

namespace lapian::app {

struct OnlineVipTestOptions {
  QString bvid;
  QString cid_text;
  qint64 cid = 0;
  QString tag;
  int timeout_ms = 20000;
  int qn = 0;        // 0=自动；116/74=60fps；120=4K；80=1080P30
  int codecid = 0;   // 0=优先 AVC；7/12/13
  int expect_auto = -1;  // -1=按目录推导；1=必须自动选 60fps；0=必须不自动
  int play_seconds = 10;
  int seeks = 20;
  int switch_qn = 80;  // A5：高档切回 1080P30（0=不测）
  bool catalog_only = false;
  QString sessdata_file;  // 空=默认 DPAPI 凭据
  QString cache_root;
  QString pictures_root;
  QString records_root;
};

// 无 GUI 的档位目录探针；调用方已创建 QCoreApplication。
int RunOnlineVipCatalogTest(const OnlineVipTestOptions& options);

// `--ui` 实现：窗口已 show；走产品会话入口并断言实际档位/帧率/P7/切档。
class OnlineVipUiTestRunner : public QObject {
  Q_OBJECT

 public:
  OnlineVipUiTestRunner(MainWindow* window, OnlineVipTestOptions options);
  void Start();

 private:
  void Run();

  MainWindow* window_ = nullptr;
  OnlineVipTestOptions options_;
  int checks_ = 0;
  int failures_ = 0;
};

}  // namespace lapian::app
