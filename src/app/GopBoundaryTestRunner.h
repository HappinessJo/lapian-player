#pragma once

#include <QObject>
#include <QString>

#include <vector>

class MainWindow;

namespace lapian::app {

// T7.11：P7 跨 GOP 边界后退专项（spec §4.1 / T2.4）。
// 目标帧是 GOP 的第一个关键帧，PgUp 到前一 GOP 末帧；2s GOP 1080p 按 80ms，
// 4K 按 150ms（P7 4K 档）。只走 MainWindow 的 ForTest 产品入口，不预热缓存、
// 不跳过解码；与 T7.9 的“精确 seek 后紧接 PgUp”普通单步分开记录。
// 退出码：0 = 已产出证据（pass 见输出行），2 = 基础设施失败。
class GopBoundaryTestRunner : public QObject {
  Q_OBJECT

 public:
  GopBoundaryTestRunner(MainWindow* window, QString file, int samples, QString tag,
                        QObject* parent = nullptr);

  void Start();

 private:
  struct Boundary {
    qint64 previous_keyframe = 0;
    qint64 keyframe = 0;
    qint64 gop_frames = 0;
  };

  struct Sample {
    Boundary boundary;
    double seek_ms = 0.0;
    double up_ms = 0.0;
    bool frame_ok = false;
  };

  struct GroupStats {
    std::size_t n = 0;
    double p50 = 0.0;
    double p95 = 0.0;
    double max = 0.0;
    std::size_t over = 0;
  };

  void Begin();
  void PollOpen();
  void OnReady();
  void CollectBoundaries();
  void RunSample();
  void Finish();
  void Fail(const QString& reason);

  GroupStats Summarize(const std::vector<double>& values,
                       double threshold_ms) const;

  MainWindow* window_ = nullptr;
  QString file_;
  QString tag_;
  int samples_ = 20;
  int open_poll_count_ = 0;
  bool failed_ = false;

  qint64 last_frame_ = 0;
  double fps_ = 0.0;
  bool is_4k_ = false;
  double threshold_ms_ = 80.0;

  std::vector<Boundary> boundaries_;
  std::vector<std::size_t> sample_order_;
  std::vector<Sample> measured_;
  int next_sample_ = 0;
};

}  // namespace lapian::app
