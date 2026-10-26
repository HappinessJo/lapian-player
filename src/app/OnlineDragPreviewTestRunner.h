#pragma once

#include <QString>

#include <cstdint>
#include <functional>
#include <vector>

class MainWindow;

namespace lapian::app {

struct OnlineDragPreviewTestOptions {
  QString bvid;
  qint64 cid = 0;
  int page = 1;
  QString tag;
  int timeout_ms = 30000;
  // 拖动模拟：总时长与目标采样数（默认 1s/60 次，约 60Hz 鼠标移动）。
  int drag_seconds = 1;
  int drag_samples = 60;
  // 已缓存区起点；探针会向后寻找足够长的连续已缓存窗口。
  qint64 start_frame = 15;
  // 只验证缓存命中（本地完整 data.mp4）路径；用于第二轮证据。
  bool cache_hit_only = false;
  // 只节流视频下载，保证未缓存区在测试窗口内仍然存在（0 = 不节流）。
  qint64 throttle_bytes_per_sec = 0;
  // 2026-10-25：前后一致 + 丝滑验收（--online-drag-parity-test）。
  bool parity = false;
  // 本地 1080p 同目标序列基线（--local-drag-parity-test）。
  bool local_only = false;
  QString local_file;
  // P0 热修探针（--scrub-space-test）：拖动松手后立刻按 Space。
  // 在线变体（给了 bvid）验证 seek 落地后补播；本地变体只作对照。
  bool scrub_space = false;
  bool scrub_space_local = false;
  int scrub_space_repeats = 20;
};

// T8.6.1b 真实窗口探针：在线增长会话拖动预览。
// 已缓存区：1s 连续拖动，preview_fps ≥30、UI gap ≤200ms、无 stale 呈现、
// 松手 frame/PTS 精确；未缓存区：提示“缓冲中（预览）”、0ms 级返回、可恢复、
// 松手可取消；反向拖动单调；拖动中 S 不截图；拖动中关窗 ≤1s。
// 只走产品入口/真实网络；不注入假数据。
class OnlineDragPreviewTestRunner {
 public:
  OnlineDragPreviewTestRunner(MainWindow* window,
                              OnlineDragPreviewTestOptions options);
  void Start();

 private:
  struct DragResult {
    bool ok = false;
    qint64 start_frame = 0;
    qint64 end_frame = 0;
    qint64 span_frames = 0;
    int samples = 0;
    int presented = 0;
    double fps_total = 0.0;
    double fps_active = 0.0;
    qint64 first_present_ms = -1;
    qint64 release_ms = 0;
    qint64 landing_ms = 0;
    int stale_presented = 0;
    bool monotonic = true;
    int dropped = 0;
    qint64 ui_gap_max_ms = 0;
    int ui_gap_over_200 = 0;
    // P0：每次拖动（只打印，不上门禁）。
    int max_missing_run = 0;
    bool exact_frame = false;
    bool exact_pts = false;
    bool preview_active_during = false;
    bool preview_stopped_after = false;
  };

  bool WaitUntil(const std::function<bool()>& predicate, int timeout_ms,
                 const char* label);
  void Pump(int ms);
  bool OpenOnline(bool expect_cache_hit, QString* error);
  bool OpenLocal(QString* error);
  bool WaitForCachedWindow(qint64* start, qint64* end, qint64 desired_span,
                           int timeout_ms);
  DragResult RunDragPass(const QString& label, qint64 start, qint64 end,
                         int samples, bool cache_hit, bool check_snapshot);
  void RunShortEdgePass();
  void RunUncachedPass();
  void RunCloseDuringDrag();
  void Run();
  void Check(const char* name, bool ok, const QString& detail = QString());

  // 2026-10-25 前后一致 + 丝滑验收。
  struct ParityResult {
    QString label;
    QString mode;
    qint64 start_frame = 0;
    qint64 end_frame = 0;
    int updates = 0;
    int presented = 0;
    double first_response_ms = -1.0;
    double first_from_start_ms = -1.0;
    double response_p95_ms = 0.0;
    double max_response_ms = 0.0;
    double max_gap_ms = 0.0;
    double active_fps = 0.0;
    double target_lag_p95 = 0.0;
    int stale_dropped = 0;
    int coarse_presented = 0;
    int cache_presented = 0;
    int max_consecutive_missing = 0;
    // P0 每次拖动度量（S1 只打印）：来自 MainWindow 的 OnScrubFinished 快照。
    qint64 open_ms = -1;
    qint64 first_publish_ms = -1;
    qint64 first_delivered_ms = -1;
    qint64 last_delivered_before_resume = -1;
    int published_before_release = 0;
    int decoder_published = 0;
    int decoder_delivered = 0;
    // P0 S3：baseline/预热/seek 落点诊断。
    qint64 baseline_frame = -1;
    qint64 warm_ms = -1;
    qint64 warm_frames = 0;
    qint64 warm_skipped = 0;
    qint64 warm_failures = 0;
    qint64 seek_fallbacks = 0;
    qint64 seek_keyframe = -1;
    qint64 seek_offset = 0;
    qint64 primed_frames = 0;
    int flicker = 0;
    qint64 release_ms = 0;
    qint64 landing_ms = 0;
    qint64 exact_ms = 0;
    bool landing_frame_ok = false;
    bool landing_pts_ok = false;
    qint64 landing_frame = -1;
    qint64 landing_pts = 0;
    qint64 ui_gap_ms = 0;
    bool ok = false;
    // 内部统计：每次目标更新的响应/滞后样本，用于 p95。
    std::vector<double> responses;
    std::vector<double> lags;
  };
  void RunParity();
  void RunLocalParity();
  void RunOnlineParity();
  void RunScrubSpace();
  bool EnsureLocalReady(QString* error);
  ParityResult RunParitySequence(const QString& label, qint64 start, qint64 end,
                                 int samples, int duration_ms, bool forward,
                                 int gap_limit_ms, bool check_snapshot,
                                 int initial_pump_ms = 80);
  ParityResult RunParityFrames(const QString& label, qint64 start, qint64 end,
                               const std::vector<qint64>& targets,
                               const std::vector<int>& due_ms, bool forward,
                               int gap_limit_ms, int initial_pump_ms = 80);
  ParityResult RunParityZoom(const QString& label, qint64 start, qint64 step,
                             int steps, int step_ms);
  ParityResult RunParityFar(const QString& label, qint64 start, qint64 target,
                            bool forward);
  void RunParityUncached();
  void PrintParity(const ParityResult& result);
  bool ParityLanding(qint64 target, ParityResult* result);
  int PresentedNow() const;
  qint64 DisplayedNow() const;
  bool CoarseNow() const;
  bool CacheNow() const;
  int StaleNow() const;

  MainWindow* window_ = nullptr;
  OnlineDragPreviewTestOptions options_;
  int checks_ = 0;
  int failures_ = 0;
  qint64 heartbeat_max_ms_ = 0;
  int heartbeat_over_200_ = 0;
  qint64 parity_ui_gap_max_ms_ = 0;
};

}  // namespace lapian::app
