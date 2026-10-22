#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

class MainWindow;

namespace lapian::app {

// T7.5 错误与鲁棒性探针（--robust-test）。
//
// 覆盖 spec §4.4 / F1-A5 / V8 的边界：缺失、空文件、垃圾数据、截断、无视频流、
// 目录/被独占文件、打开后源文件被截断、中文长路径、只读目录的 sidecar/截图
// 回退、USB 音频设备丢失模拟、不可达 UNC。所有打开/步进/截图都走产品入口
// （MainWindow::OpenFile / GoToFrame / CaptureSnapshot），只读 ForTest 状态。
//
// 失败判定：错误必须在 5s 内返回、旧会话保留、提示可读、UI 仍可操作；
// 不把“网络盘可访问但读写慢”伪装成通过（见交接的缺口）。
class RobustTestRunner : public QObject {
  Q_OBJECT

 public:
  struct Options {
    QString media;          // 基准素材（必须存在）
    QString tag;            // 日志标签
    QString root;           // 测试产物根目录
    QString only;           // all / readonly / network
    QString read_only_dir;  // 由 scripts\robust-verify.ps1 设 ACL 后传入
    QString pictures_root;  // 截图回退根（产品 --snapshot-pictures 同款）
  };

  RobustTestRunner(MainWindow* window, Options options, QObject* parent = nullptr);

  void Start();

 private:
  using Step = std::function<void()>;

  struct BadCase {
    QString label;
    QString path;
    QStringList expected_terms;  // 错误提示里应出现其一（空=只要求非空）
  };

  void Enqueue(const QString& name, Step step);
  void RunNext();
  void Check(const QString& name, bool ok, const QString& detail = QString());
  void Skip(const QString& name, const QString& reason);
  void Fail(const QString& reason);
  void Finish();

  void InstallModalGuard();
  void PollModalGuard();

  bool PrepareFixtures();
  QString RootPath(const QString& name) const;
  bool WriteBytes(const QString& name, const QByteArray& bytes);
  bool CopyFixture(const QString& source, const QString& name, QString* out_path);
  bool CreateLongPathTree(QString* out_video);

  void PollUntil(const QString& what, int timeout_ms, std::function<bool()> predicate,
                 Step on_ready, Step on_timeout = {});
  void OpenAndWait(const QString& path, Step on_ready);
  void LogState(const char* step) const;

  void RunBaseline();
  void CheckBaseline();
  void RunDeviceLoss();
  void RunBadCases();
  void RunBadCase(std::size_t index);
  void FinishBadCases();
  void RunLockedFile();
  void RunTruncatedFile();
  void RunLongPath();
  void RunReadOnly();
  void RunNetwork();

  MainWindow* window_ = nullptr;
  Options options_;
  std::deque<std::pair<QString, Step>> steps_;
  std::vector<BadCase> bad_cases_;

  int checks_ = 0;
  int failures_ = 0;
  int skips_ = 0;
  int modal_dismissed_ = 0;
  bool failed_ = false;
  bool infra_failed_ = false;
  bool finished_ = false;
  qint64 baseline_frame_ = 0;
  qint64 baseline_last_frame_ = 0;
  QString baseline_name_;
  QElapsedTimer total_timer_;

  QString long_video_;
  QString readonly_video_;
  QString locked_video_;
  QString truncate_video_;
  void* locked_handle_ = nullptr;  // HANDLE，避免在头文件引入 windows.h
};

}  // namespace lapian::app
