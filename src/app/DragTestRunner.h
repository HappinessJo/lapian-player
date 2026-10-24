#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QRandomGenerator>
#include <QString>

#include <chrono>
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
  // T8.17：只跑“快速大跳拖动”一种场景，输出 fps/最大呈现空档。
  DragTestRunner(MainWindow* window, QString file, int samples, QString tag,
                 bool fast_only, int fast_step, QObject* parent = nullptr);

  void Start();
  // DP-2：远跳粗帧探针（--drag-far-test）。
  void EnableFarTest() { far_only_ = true; }
  // DP-2：同一进程连续跑 N 次快拖，验证缓存峰值不持续增长。
  void SetFastCycles(int cycles) { fast_cycles_ = cycles > 0 ? cycles : 1; }

 private:
  void Begin();
  void PollOpen();
  void OnReady();
  void RunP9();
  void RunP6(int index, int total);
  void RunP4();
  void RunFast();
  void RunFar();
  void BeginFarJump(bool forward);
  void StartFarTarget();
  void FinishFarJump();
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
  bool fast_only_ = false;
  int fast_step_ = 60;
  int fast_cycles_ = 1;
  int fast_cycle_ = 0;

  // DP-2 远跳粗帧探针
  bool far_only_ = false;
  bool far_forward_ = true;
  qint64 far_start_ = 0;
  qint64 far_target_ = 0;
  double far_first_coarse_ms_ = 0.0;
  double far_first_present_ms_ = 0.0;
  double far_exact_ms_ = 0.0;
  double far_release_ms_ = 0.0;
  bool far_coarse_seen_ = false;
  bool far_first_present_seen_ = false;
  bool far_exact_seen_ = false;
  bool far_finishing_ = false;
  QElapsedTimer far_jump_timer_;
  QMetaObject::Connection far_present_connection_;
  std::vector<double> far_coarse_ms_samples_;
  std::vector<double> far_first_present_ms_samples_;
  std::vector<double> far_exact_ms_samples_;
  std::vector<double> far_release_ms_samples_;

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

  // T8.17 快速大跳拖动
  QElapsedTimer fast_timer_;
  qint64 fast_start_frame_ = 0;
  qint64 fast_target_ = 0;
  int fast_presents_start_ = 0;
  int fast_presents_ = 0;
  double fast_seconds_ = 0.0;
  double fast_fps_ = 0.0;
  double fast_max_gap_ms_ = 0.0;
  double fast_first_present_ms_ = 0.0;
  bool fast_has_present_ = false;
  // DP-1：+60/-60 在本片单上约 1.2s 就会撞到文件边界；之后目标不再移动，
  // 再拿 2s 总 Present 数算 fps 会把“停在目标帧”误判成退化。这里单独记录
  // 目标仍在移动的阶段（到第一次边界为止）的 fps。
  bool fast_boundary_hit_ = false;
  double fast_active_seconds_ = 0.0;
  int fast_active_presents_ = 0;
  // DP-1：Present 时“显示帧 vs 当前拖动目标”的滞后（绝对值）与解码诊断。
  std::vector<double> fast_target_lag_frames_;
  std::vector<double> fast_target_lag_ms_;
  std::chrono::steady_clock::time_point fast_last_present_{};
  QMetaObject::Connection fast_present_connection_;

  qint64 p6_target_ = 0;
  int p6_index_ = 0;
  int p6_total_ = 0;
  QRandomGenerator rng_{20261022u};
};

}  // namespace lapian::app
