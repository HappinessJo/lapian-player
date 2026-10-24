#pragma once

#include <QString>

#include <cstdint>
#include <functional>

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
    bool exact_frame = false;
    bool exact_pts = false;
    bool preview_active_during = false;
    bool preview_stopped_after = false;
  };

  bool WaitUntil(const std::function<bool()>& predicate, int timeout_ms,
                 const char* label);
  void Pump(int ms);
  bool OpenOnline(bool expect_cache_hit, QString* error);
  bool WaitForCachedWindow(qint64* start, qint64* end, qint64 desired_span,
                           int timeout_ms);
  DragResult RunDragPass(const QString& label, qint64 start, qint64 end,
                         int samples, bool cache_hit, bool check_snapshot);
  void RunShortEdgePass();
  void RunUncachedPass();
  void RunCloseDuringDrag();
  void Run();
  void Check(const char* name, bool ok, const QString& detail = QString());

  MainWindow* window_ = nullptr;
  OnlineDragPreviewTestOptions options_;
  int checks_ = 0;
  int failures_ = 0;
  qint64 heartbeat_max_ms_ = 0;
  int heartbeat_over_200_ = 0;
};

}  // namespace lapian::app
