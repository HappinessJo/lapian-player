#pragma once

// T8.1 在线解析探针（--online-resolve-test）。
//
// 走产品路径：MainWindow::OpenOnlineDialogForTest → OnlineResolveDialog →
// BiliResolveClient → WinHttpClient。覆盖：
// - 本地非法输入（≤100ms、0 请求、不发网络）；
// - OB-1/2/3 匿名 view 的标题/UP/时长/分P；
// - URL ?p=2 与真实多P样本的切P；
// - OB-5 的 -400/-404/62002 与串片样本回显确认；
// - 解析中取消/关窗（fake 慢客户端 + 真实栈的取消由 ctest 本地慢服务器覆盖）。
//
// 退出码：0 = 全部检查通过；1 = 有失败；2 = 基础设施失败（无法打开对话框）。

#include <QObject>
#include <QString>
#include <QStringList>

#include <deque>
#include <functional>
#include <utility>

class MainWindow;
class OnlineResolveDialog;

namespace lapian::app {

class OnlineResolveTestRunner : public QObject {
  Q_OBJECT

 public:
  explicit OnlineResolveTestRunner(MainWindow* window, QObject* parent = nullptr);

  void Start();

 private:
  using Step = std::function<void()>;

  void Enqueue(const QString& name, Step step);
  void RunNext();
  void Check(const QString& name, bool ok, const QString& detail = QString());
  void Log(const QString& line);
  void Finish();

  void PollUntil(const QString& what, int timeout_ms,
                 std::function<bool()> predicate, Step on_ready,
                 Step on_timeout = {});
  void SaveShot(const QString& name);

  void StepDialogBasics();
  void StepLocalInvalid();
  void StepLiveSuccess(const QString& label, const QString& input,
                       const QString& expected_title, const QString& expected_owner,
                       const QString& expected_duration, qint64 expected_cid);
  void StepLiveUrl();
  void StepMultiPage();
  void StepFailureCode(const QString& label, const QString& input,
                       const QString& expected_term);
  void StepMismatch();
  void StepCancelFake();
  void StepCloseFake();

  MainWindow* window_ = nullptr;
  OnlineResolveDialog* dialog_ = nullptr;
  std::deque<std::pair<QString, Step>> steps_;
  int checks_ = 0;
  int failures_ = 0;
  int confirmed_events_ = 0;
  QString last_confirmed_bvid_;
  qint64 last_confirmed_cid_ = 0;
  int last_confirmed_page_ = 0;
  qint64 max_local_invalid_ms_ = 0;
  QString ob1_title_;
  QString ob2_title_;
};

}  // namespace lapian::app
