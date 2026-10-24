#pragma once

// T8.9/T8.12 DASH 解析、档位策略与降帧提示探针。
//
// 两种模式：
// - 无 GUI（默认）：真实 playurl(fnval=4048) + view + durl 对照 + 匿名对照 +
//   错误 qn（116/0/9999）；同时验证实际 fps/降帧文案与权限列档；只从 .secrets /
//   LAPIAN_SESSDATA_FILE / --sessdata-file 读取 SESSDATA；绝不打印凭据或签名
//   URL；输出稳定 key=value。
// - `--ui`：真实窗口 + `MainWindow::StartOnlineSession`，验证档位菜单来自实际
//   playurl 返回、DEC-4 横幅/角标可关、手动 1080→720（PTS/标记/截图）以及
//   合成 60/4K 的“只手动/不保证可达”规则。
//
// 退出码：0=pass；1=fail；2=blocked（无凭据/登录失效/非普通用户账号等）。

#include <QObject>
#include <QString>

#include <cstdint>

class MainWindow;

namespace lapian::app {

struct OnlineQualityTestOptions {
  QString bvid;
  QString cid_text;
  qint64 cid = 0;
  QString tag;
  int timeout_ms = 20000;
  QString sessdata_file;
  bool anonymous = false;  // 不读凭据，只跑匿名目录（内部对照用）
  bool odd_qn = false;     // 额外请求 116/0/9999，验证错误 qn 不崩
  bool ui = false;
  QString cache_root;
  QString pictures_root;
  QString records_root;
};

// 无 GUI 实现；调用方已创建 QCoreApplication。
int RunOnlineQualityTest(const OnlineQualityTestOptions& options);

// `--ui` 实现：窗口已 show；Started 后走产品会话入口并断言菜单，结束时退出。
class OnlineQualityUiTestRunner : public QObject {
  Q_OBJECT

 public:
  OnlineQualityUiTestRunner(MainWindow* window, OnlineQualityTestOptions options);
  void Start();

 private:
  void Run();

  MainWindow* window_ = nullptr;
  OnlineQualityTestOptions options_;
  int checks_ = 0;
  int failures_ = 0;
};

}  // namespace lapian::app
