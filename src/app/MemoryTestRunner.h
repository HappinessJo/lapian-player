#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

#include "app/SoakStats.h"

class MainWindow;

namespace lapian::app {

// T7.2/T7.3：--memory-test 运行器。只做测量，不改变产品打开/播放/索引路径。
// 模式：empty（空态）、pause（打开后暂停）、play（播放）、step（后退/前进步进，
// 填满帧缓存）、switch（A→B→A 切文件释放）、soak（全片循环长时播放监测）。
class MemoryTestRunner : public QObject {
  Q_OBJECT

 public:
  MemoryTestRunner(MainWindow* window, QString mode, QString tag, int seconds,
                   int steps, int switch_cycles, int interval_seconds,
                   bool soak_loop, QString file_a, QString file_b,
                   QObject* parent = nullptr);
  ~MemoryTestRunner() override;

  void Start();

 private:
  enum class Mode {
    kEmpty,
    kPause,
    kPlay,
    kStep,
    kSwitch,
    kSoak,
  };

  bool ParseMode(const QString& text);
  void Begin();
  void OpenAndWait(const QString& path, const QString& phase,
                   std::function<void()> next);
  void PollOpen(const QString& expected_name, std::function<void()> next);
  void OnReady();
  void ScheduleMilestones(int seconds, std::function<void()> done);
  void ScheduleSoak();
  void FinishSoak();
  void Sample(const QString& phase);
  void RunSteps(bool backward, int count, std::function<void()> done);
  void OnStepsDone();
  void SummaryAndExit();
  void Fail(const QString& reason);
  int LimitMiBForMode() const;

  // T7.3：UI 心跳 + 独立看门狗线程，检测卡死并立即失败，避免 2h 浸泡挂死。
  void StartSoakMonitoring();
  void StopSoakMonitoring();
  void WatchdogLoop();
  double WarmupSeconds() const;

  MainWindow* window_ = nullptr;
  Mode mode_ = Mode::kPause;
  QString mode_text_;
  QString tag_;
  int seconds_ = 60;
  int steps_ = 40;
  int switch_cycles_ = 3;
  int interval_seconds_ = 30;
  bool soak_loop_ = true;
  int switch_done_ = 0;
  QString file_a_;
  QString file_b_;
  QString phase_;
  QElapsedTimer phase_timer_;
  QTimer* modal_watchdog_ = nullptr;
  int open_poll_count_ = 0;
  bool failed_ = false;
  std::vector<double> step_ms_;
  std::uint64_t max_ws_bytes_ = 0;
  std::uint64_t max_private_bytes_ = 0;
  std::uint64_t last_ws_bytes_ = 0;
  std::uint64_t last_private_bytes_ = 0;

  // T7.3 soak 状态
  QElapsedTimer soak_timer_;
  bool soak_collecting_ = false;
  bool soak_finished_ = false;
  std::vector<SoakSample> soak_samples_;
  SoakAnalysis soak_analysis_;
  QTimer* heartbeat_timer_ = nullptr;
  std::thread watchdog_thread_;
  std::atomic<bool> watchdog_stop_{false};
  std::atomic<std::int64_t> heartbeat_ms_{0};
  std::atomic<bool> watchdog_stall_logged_{false};
  double max_ui_gap_ms_ = 0.0;
};

}  // namespace lapian::app
