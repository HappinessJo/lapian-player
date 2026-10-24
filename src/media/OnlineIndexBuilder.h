#pragma once

// T8.6 在线部分索引 / T8.11 渐进索引：只 demux 已发布前缀，不把未下载区当成 EOF。
//
// 两条入口：
// - BuildOnlineIndex：一次性从文件起点扫到当前发布前沿（T8.6 兼容/测试用）。
// - IncrementalOnlineIndexBuilder：T8.11 产品路径。首次打开后保留 demux 上下文，
//   缓存增长时只读新增字节；接近发布前沿时先停（frontier guard），避免在未发布区
//   把 FFmpeg 的 sample 状态打脏。万一在 frontier 上读到 ETIMEDOUT（大 sample
//   跨过 guard），下一 Step 从“最后已解析字节”重开并跳过已解析区，不从头全扫。
//
// 帧号语义（与 T8.6/T8.10 一致）：
// - 只暴露“稳定前缀”（末尾留 tail_guard_frames 帧），下载增长时已暴露前缀的 PTS
//   不变，stable_frames 单调不减；
// - 完整下载/读到 EOF 后才暴露全部帧，complete=true；
// - spans 与 index 帧号一一对应，供 UI 判断目标是否已缓存；
// - 只针对视频流；波形/音频不走本类。
//
// 不联网、不接 UI；每个 builder 自己打开独立 GrowingFormat/Cursor。

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "media/FrameIndex.h"

struct AVCodecContext;
struct AVCodecParserContext;
struct AVPacket;
struct AVStream;

namespace lapian::online {
class GrowingSource;
class GrowingFormat;
}  // namespace lapian::online

namespace lapian::media {

struct OnlineFrameSpan {
  std::int64_t pos = -1;
  std::int64_t size = 0;
  bool keyframe = false;
};

struct OnlineIndexResult {
  bool ok = false;
  FrameIndex index;
  std::vector<OnlineFrameSpan> spans;  // 与 index 的帧号一一对应
  int stable_frames = 0;
  int parsed_frames = 0;
  bool complete = false;
  bool timed_out = false;
  double build_ms = 0.0;
  std::string error;
};

// 一次 Step 的结果（元数据，不含大数组；快照由 Snapshot() 单独取）。
struct OnlineIndexStepInfo {
  bool ok = true;           // false：不可重试错误/取消
  bool cancelled = false;
  bool complete = false;    // 已读到 EOF 且已完整发布
  bool at_frontier = false; // 停在已发布前沿，等下载增长
  bool appended = false;    // 本次是否新增了帧
  bool budget_hit = false;  // 达到 max_ms/max_packets 预算，仍可立即继续
  int appended_frames = 0;
  int packets_read = 0;
  int total_frames = 0;     // 已解析（未做 tail guard）的唯一帧数近似
  int stable_frames = 0;    // 最近一次 Snapshot 的稳定帧数
  double step_ms = 0.0;
  std::string error;
};

struct OnlineIndexStats {
  std::int64_t steps = 0;
  std::int64_t opens = 0;
  std::int64_t reopens = 0;          // 因 frontier/错误重开 demux 的次数
  std::int64_t resume_skips = 0;     // 重开后跳过的已解析包数
  std::int64_t resume_from_start = 0;  // seek 失败时退化为从头跳过
  std::int64_t packets = 0;
  std::int64_t parsed_frames = 0;
  std::int64_t read_bytes = 0;       // 所有 cursor 累计实际读字节
  double max_step_ms = 0.0;
  double total_step_ms = 0.0;
};

// T8.11 渐进索引。生命周期：Create 后由同一个后台线程调用 Step()/Snapshot()；
// Close/析构会关闭 demux 上下文（不取消 GrowingSource）。
class IncrementalOnlineIndexBuilder {
 public:
  ~IncrementalOnlineIndexBuilder();

  IncrementalOnlineIndexBuilder(const IncrementalOnlineIndexBuilder&) = delete;
  IncrementalOnlineIndexBuilder& operator=(const IncrementalOnlineIndexBuilder&) =
      delete;

  // 失败返回 nullptr + 可读错误；源无效/路径为空属于不可重试。
  static std::unique_ptr<IncrementalOnlineIndexBuilder> Create(
      std::shared_ptr<lapian::online::GrowingSource> source,
      const std::string& part_path_utf8, int video_stream_index,
      int tail_guard_frames, std::string* error);

  // 读取已发布前缀；最多 max_ms 毫秒或 max_packets 个视频包。
  // 取消/源失败返回 ok=false；其余“还没数据/读超时”返回 ok=true + at_frontier。
  OnlineIndexStepInfo Step(const std::atomic<bool>* cancel, int max_ms = 40,
                           int max_packets = 8192);

  // 生成当前稳定前缀快照（会排序/去重）。没有稳定帧时返回 false + error。
  // 快照只包含 stable_frames 帧，spans 与 index 对齐。
  bool Snapshot(OnlineIndexResult* out);

  bool complete() const { return complete_; }
  bool failed() const { return failed_; }
  int total_frames() const { return static_cast<int>(entries_.size()); }
  int stable_frames() const { return last_stable_; }
  const OnlineIndexStats& stats() const { return stats_; }
  const std::string& last_error() const { return last_error_; }
  // 0 表示不启用前沿 guard（一次性/测试路径会一直读到 ETIMEDOUT）。
  void SetFrontierGuardBytes(int bytes) { frontier_guard_bytes_ = bytes; }
  void Close();

 private:
  IncrementalOnlineIndexBuilder(
      std::shared_ptr<lapian::online::GrowingSource> source,
      std::string part_path_utf8, int video_stream_index, int tail_guard_frames);

  bool EnsureOpen();
  void CloseFormat();
  bool SeekToResume();
  std::int64_t FindKeyframePtsAtOrBefore(std::int64_t pts_us) const;
  bool SourceComplete() const;
  void FinishStep(OnlineIndexStepInfo* info,
                  const std::chrono::steady_clock::time_point& started);

  std::shared_ptr<lapian::online::GrowingSource> source_;
  std::string part_path_utf8_;
  int video_stream_index_ = -1;
  int tail_guard_frames_ = 0;

  std::unique_ptr<lapian::online::GrowingFormat> format_;
  AVStream* stream_ = nullptr;
  AVCodecParserContext* parser_ = nullptr;
  AVCodecContext* parser_context_ = nullptr;
  AVPacket* packet_ = nullptr;
  int selected_stream_ = -1;

  bool opened_ = false;
  bool complete_ = false;
  bool failed_ = false;
  bool eof_ = false;
  bool needs_reopen_ = false;
  bool skipping_ = false;
  bool last_frontier_ = false;
  int frontier_guard_bytes_ = 256 * 1024;

  std::int64_t resume_pos_ = 0;
  std::int64_t last_pts_us_ = 0;
  std::int64_t last_pos_end_ = 0;
  std::vector<FrameIndexEntry> entries_;
  std::vector<OnlineFrameSpan> spans_;
  int parsed_frames_ = 0;
  int packet_count_ = 0;
  int last_stable_ = 0;
  std::int64_t last_cursor_read_bytes_ = 0;
  std::string last_error_;
  OnlineIndexStats stats_;
};

// 一次性构建：从文件起点扫到当前发布前沿（或 EOF）。T8.6 兼容路径。
OnlineIndexResult BuildOnlineIndex(
    const std::shared_ptr<lapian::online::GrowingSource>& source,
    const std::string& part_path_utf8, int video_stream_index,
    int tail_guard_frames = 48,
    const std::atomic<bool>* cancel = nullptr);

}  // namespace lapian::media
