#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QRandomGenerator>
#include <QString>
#include <vector>

class MainWindow;

namespace lapian::app {

// T7.4 测试专用探针：在真实窗口里走产品路径测 4K（或任意素材）的
// P4 拖动预览帧率、P6 松手精确、P7 ±1 步进、P8 长按、P9 跳转。
// 不改变产品语义；只通过 MainWindow 的 ForTest 入口发事件/读状态。
// 输出 T7.4 drag_summary，退出码 0（证据模式）；基础设施失败才返回 2。
class DragTestRunner : public QObject {
  Q_OBJECT

 public:
  DragTestRunner(MainWindow* window, QString file, int samples, QString tag,
                 QObject* parent = nullptr);

  void Start();

 private:
  void Begin();
  void PollOpen();
  void OnReady();
  void RunP9();
  void RunP6(int index, int total);
  void RunP4();
  void RunP7();
  void RunP8();
  void Finish();
  void Fail(const QString& reason);

  void PrintDistribution(const char* name, std::vector<double>* samples,
                         double threshold_ms);
  void LogSummary();

  MainWindow* window_ = nullptr;
  QString file_;
  QString tag_;
  int samples_ = 20;
  int open_poll_count_ = 0;
  bool failed_ = false;

  // 素材规格
  qint64 last_frame_ = 0;
  int max_prefix_frames_ = 60;
  bool is_4k_ = false;

  // P9 / P6 / P7 / P8 样本
  std::vector<double> p9_ms_;
  std::vector<qint64> p9_prefix_;
  std::vector<double> p6_ms_;
  std::vector<double> p7_backward_ms_;
  std::vector<double> p7_forward_ms_;
  std::vector<double> p7_spot_ms_;
  double p4_fps_ = 0.0;
  int p4_presents_ = 0;
  double p4_seconds_ = 0.0;
  qint64 p8_frames_ = 0;
  double p8_seconds_ = 0.0;
  int p8_presents_ = 0;

  // P4 扫掠状态
  QElapsedTimer p4_timer_;
  qint64 p4_start_frame_ = 0;
  qint64 p4_delta_ = 0;
  int p4_presents_start_ = 0;

  // P8 状态
  QElapsedTimer p8_timer_;
  qint64 p8_start_frame_ = 0;
  int p8_presents_start_ = 0;

  qint64 p6_target_ = 0;
  int p6_index_ = 0;
  int p6_total_ = 0;
  QRandomGenerator rng_{20261022u};
};

}  // namespace lapian::app
