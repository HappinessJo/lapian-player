#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QRandomGenerator>
#include <QString>

#include <chrono>
#include <vector>

class MainWindow;

namespace lapian::app {

// DP-3 验收探针：把“慢拖/快拖/反向/远跳/松手”写成可重复的场景矩阵，读取
// MainWindow::PreviewDiagnosticsForTest，输出 target_lag/粗帧/缓存/硬件/松手。
// 只通过 ForTest 入口走产品路径；不改产品语义，不作为正式 P4/P6/P7 门。
class DragStrategyTestRunner : public QObject {
  Q_OBJECT

 public:
  DragStrategyTestRunner(MainWindow* window, QString file,
                         QObject* parent = nullptr);

  void Start();

 private:
  struct Result {
    QString scenario;
    QString expected_mode;
    int updates = 0;
    int presents = 0;
    int coarse_presents = 0;
    int coarse_after_exact = 0;
    int hw_presents = 0;
    int cache_presents = 0;
    int configured_step = 0;
    double fps = 0.0;
    double active_fps = 0.0;
    double max_gap_ms = 0.0;
    double first_present_ms = -1.0;
    double first_coarse_ms = -1.0;
    double exact_after_coarse_ms = -1.0;
    double exact_ms = -1.0;
    double release_ms = 0.0;
    double cache_hit_rate = 0.0;
    bool hw_active = false;
    QString last_mode;
    QString last_reason;
    qint64 last_step = 0;
    qint64 last_interval_ms = 0;
    double last_speed_fps = 0.0;
    int last_run = 0;
    std::vector<double> lag_frames;
    std::vector<double> lag_ms;
    bool pass = false;
    QString pass_reason;
  };

  enum class Phase {
    Idle,
    Slow,
    Fast,
    Reverse20,
    Far,
    Release,
  };

  void Begin();
  void PollOpen();
  void OnReady();
  void StartNext();
  void StartSlow(bool forward);
  void StepSlow();
  void StartFast(int step, bool reverse20);
  void TickFast();
  void StartFar(bool forward);
  void StartFarTarget();
  void FinishFarSoon();
  void FinishScenario();
  void RunReleaseSamples();
  void RunReleaseSample();
  void OnPresent();
  void Finish();
  void Fail(const QString& reason);

  void ResetScenario(const QString& scenario, const QString& expected_mode);
  double ScenarioElapsedMs() const;
  static double Percentile(std::vector<double> values, double fraction);
  static double MaxOf(const std::vector<double>& values);

  MainWindow* window_ = nullptr;
  QString file_;
  Phase phase_ = Phase::Idle;
  int next_ = 0;
  int open_poll_count_ = 0;
  bool failed_ = false;
  bool scenario_active_ = false;
  bool has_present_ = false;
  bool exact_seen_ = false;
  bool far_finish_scheduled_ = false;
  bool slow_forward_ = true;
  qint64 last_frame_ = 0;
  double frame_rate_ = 30.0;
  qint64 scenario_start_frame_ = 0;
  qint64 scenario_target_ = 0;
  qint64 fast_step_ = 60;
  qint64 far_start_ = 0;
  qint64 far_target_ = 0;
  int slow_updates_ = 0;
  bool fast_boundary_hit_ = false;
  double fast_active_seconds_ = 0.0;
  int fast_active_presents_ = 0;
  int fast_presents_start_ = 0;
  QElapsedTimer scenario_timer_;
  std::chrono::steady_clock::time_point last_present_{};
  QMetaObject::Connection present_connection_;

  std::vector<Result> results_;
  Result current_;
  std::vector<double> release_samples_;
  int release_samples_target_ = 4;
  int release_samples_done_ = 0;
  QRandomGenerator rng_{20261024u};
};

}  // namespace lapian::app
