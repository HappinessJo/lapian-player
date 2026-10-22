#pragma once

// T8.2 产品路径登录自检探针（--online-auth-test）。
//
// 走真实 OnlineResolveDialog：
// - 打开对话框 / 保存凭据 / 清除凭据 / 非法输入：0 nav、0 view；
// - 首次提交合法 BV：nav 与 view 并行，nav 只发 1 次；
// - 第二次提交：使用会话缓存，不再 nav；
// - -101 / 超时：继续匿名 view，界面明确“登录不可用”；
// - 取消：≤100ms，检查状态回 not_checked（下次可重试）；
// - 隐私说明首次可见、可隐藏；本地模式路径不涉及。
//
// 使用 fake HTTP（不访问 B 站）；真实 nav 由 --online-http-selfcheck 覆盖。

#include <QObject>
#include <QString>

class MainWindow;
class OnlineResolveDialog;

namespace lapian::app {

class OnlineAuthTestRunner : public QObject {
  Q_OBJECT

 public:
  explicit OnlineAuthTestRunner(MainWindow* window, QObject* parent = nullptr);

  void Start();

 private:
  void Run();
  void Check(const QString& name, bool ok, const QString& detail = QString());
  void Log(const QString& line);
  void Finish(bool ok);

  MainWindow* window_ = nullptr;
  OnlineResolveDialog* dialog_ = nullptr;
  int checks_ = 0;
  int failures_ = 0;
};

}  // namespace lapian::app
