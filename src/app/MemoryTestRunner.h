#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <vector>

class MainWindow;

namespace lapian::app {

// T7.2：--memory-test 运行器。只做测量，不改变产品打开/播放/索引路径。
// 模式：empty（空态）、pause（打开后暂停）、play（播放）、step（后退/前进步进，
// 填满帧缓存）、switch（A→B→A 切文件释放）。
class MemoryTestRunner : public QObject {
  Q_OBJECT

 public:
  MemoryTestRunner(MainWindow* window, QString mode, QString tag, int seconds,
                   int steps, int switch_cycles, QString file_a, QString file_b,
                   QObject* parent = nullptr);
  void Start();

 private:
  enum class Mode {
    kEmpty,
    kPause,
    kPlay,
    kStep,
    kSwitch,
  };

  bool ParseMode(const QString& text);
  void Begin();
  void OpenAndWait(const QString& path, const QString& phase,
                   std::function<void()> next);
  void PollOpen(const QString& expected_name, std::function<void()> next);
  void OnReady();
  void ScheduleMilestones(int seconds, std::function<void()> done);
  void Sample(const QString& phase);
  void RunSteps(bool backward, int count, std::function<void()> done);
  void OnStepsDone();
  void SummaryAndExit();
  void Fail(const QString& reason);
  int LimitMiBForMode() const;

  MainWindow* window_ = nullptr;
  Mode mode_ = Mode::kPause;
  QString mode_text_;
  QString tag_;
  int seconds_ = 60;
  int steps_ = 40;
  int switch_cycles_ = 3;
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
};

}  // namespace lapian::app
