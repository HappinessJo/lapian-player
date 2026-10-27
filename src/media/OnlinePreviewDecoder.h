#pragma once

// T8.6.1b / 在线拖动前后一致：只服务“增长中的 .part 会话”的拖动预览。
//
// 与本地 PreviewDecoder 的边界：
// - 本地/完整缓存命中继续走 PreviewDecoder（路径 + FrameIndex），本类不接管；
// - 本类用独立 GrowingSource::GrowingCursor/GrowingFormat 打开视频 `.part`，
//   不碰 MediaSession 的 decoder/reader，也不碰 T8.6.1a 的 online_seek 状态；
// - 只按“稳定前缀索引”预览已缓存区；目标超出稳定前缀时只标记 waiting，
//   绝不进入阻塞读，UI 线程不会等待。
//
// 前后一致（2026-10-25）：
// - 正向/反向走同一套规则：先查精确缓存；命中立即发布（from_cache=true）；
//   没命中时解码并发布“比上一张更接近目标且不越过目标”的帧；
// - 不再用 target_direction 决定“反向只发 exact”；
// - 精确目标发布后，更远的帧不能覆盖它；目标变化只丢弃“比上一张更远”的
//   旧帧，不因为反向就丢弃所有 ahead 帧；
// - 远跳（≥300 帧）先发 coarse=true 的关键帧粗帧，再解精确目标；近距反向
//   优先缓存，不先跳关键帧。
//
// 丝滑（2026-10-25）：
// - 工作线程不再“每个目标更新就中断 seek”：一次处理把目标 GOP 解完并写入
//   有界缓存，目标更新只影响发布判定；下一次目标可直接命中缓存；
// - 独立低优先级预取线程沿拖动方向预解上一 GOP，覆盖反向连续拖动；
// - 有界缓存按运行时预算（默认 64MiB / 1024 长边；D2 起可 Configure），
//   两端都存，按距当前目标最远淘汰；End/Close 释放。
//
// D2（2026-10-27）运行时配置：
// - 预览长边与缓存预算从编译期常量改为实例配置，默认仍是 1024 / 64MiB，
//   未调用 Configure 的旧调用方行为不变；
// - 帧数上限由预算 / 16:9 估计帧字节算出（不预分配，峰值只随实际缓存帧增长）；
// - 分配失败先降预算到 75% 重试一次，仍失败回退 1024 / 64MiB，不崩溃、不无限重试。
//
// 线程/取消：
// - 每个实例一个预览线程 + 一个预取线程；Begin 启动，End/Close 取消并 join
//   （不在 UI 线程同步 seek）；
// - 同一时刻只有一个在途解码；最新帧只留 1 张。
//
// 帧所有权：OnlinePreviewFrame 的 frame 归调用方，取走后需 av_frame_free。
// 预览帧是降采样 NV12（最长边约 1280），不是精确目标帧；松手仍走 GoToFrame。

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "media/PreviewDecoder.h"

namespace lapian::online {
class GrowingSource;
}

namespace lapian::media {

class FrameIndex;

class OnlinePreviewDecoder {
 public:
  OnlinePreviewDecoder();
  ~OnlinePreviewDecoder();

  OnlinePreviewDecoder(const OnlinePreviewDecoder&) = delete;
  OnlinePreviewDecoder& operator=(const OnlinePreviewDecoder&) = delete;

  // D2：运行时预览配置。默认值与旧编译期常量一致（1024 / 64MiB），保证
  // 未配置的旧调用方（本地/旧测试/预热）行为不变；D3 从设置页取值。
  struct Config {
    int max_side = 1024;                        // 预览帧长边上限（256..1920）
    std::size_t cache_budget_bytes = 64u << 20;  // 缓存预算（8MiB..4096MiB）
    bool operator==(const Config&) const = default;
  };
  // 必须在 Begin 前调用；Begin 之后返回 false 且不改变已生效配置。
  // 越界值被夹到 256..1920 / 8MiB..4096MiB（不崩、不拒绝）。
  bool Configure(const Config& config);
  Config config() const;  // 实际生效（可能已被分配失败回退改写）
  // 由 cache_budget_bytes / 16:9 估计帧字节算出的帧数上限（至少 8）。
  // 只是上限，不做预分配；缓存峰值只随实际缓存帧增长。
  int cached_frame_limit() const;
  // 最近发布或缓存帧的实际宽高（NV12，无则 0）。
  int preview_width() const;
  int preview_height() const;
  // 是否触发过降预算/降分辨率回退（分配失败路径）。
  bool alloc_fallback_used() const;
  // 仅测试：让接下来 n 次预览帧分配强制失败，用来验证回退路径。
  void ForceAllocationFailureForTest(int n);

  // source/part_path 必须对应同一个 `.part`；index 是稳定的只读快照。
  // 调用后立即返回；打开/首帧在后台线程。
  // displayed_frame 是拖动开始时屏幕上真正显示的帧，用作发布基准；-1 时
  // 退回 initial_frame。warm_only=true 时只做一次静默预热（不发布、不等待），
  // 供“seek 落地/暂停但用户还没拖过”的场景创建预热器。
  void Begin(std::shared_ptr<lapian::online::GrowingSource> source,
             const std::string& part_path_utf8, int video_stream_index,
             std::shared_ptr<const FrameIndex> index,
             std::int64_t initial_frame, bool initial_cached,
             std::int64_t displayed_frame = -1, bool warm_only = false);
  // 索引增长时替换快照（GUI 线程拷贝，worker 只持有 shared_ptr）。
  void UpdateIndex(std::shared_ptr<const FrameIndex> index);
  // frame 是拖动目标；cached=false 表示未缓存/未索引，只显示等待，不解码。
  // 精确缓存命中会在这里同步发布（from_cache=true），不依赖下一次 tick。
  void UpdateTarget(std::int64_t frame, bool cached);

  // 结束本次拖动：取消阻塞读、join 预览线程、释放帧缓存。
  void End();
  void Close();
  // 拖动松手后暂停（保留解码器位置与有界帧缓存，下一次拖动直接命中）；
  // 会话关闭/切换文件仍走 End/Close 释放缓存。
  void Pause();
  // 复用暂停/预热后的解码器开始下一次拖动；displayed_frame 同 Begin。
  void Resume(std::int64_t frame, bool cached,
              std::int64_t displayed_frame = -1);
  // 静默预热：从 frame 之前最近关键帧解到 min(last, frame+10)，只缓存
  // [target-(cached_frame_limit()-1), target] 区间；不发布、不改 waiting。
  // 完成后线程/decoder/reader 保持 idle，下一次 Resume 复用；Resume/End 会
  // 取消在途预热。
  void Warm(std::int64_t frame);
  // 把当前显示帧（clone 后异步降采样）放进预览缓存；不发布。硬解帧跳过。
  // 线程未启动时暂存，下一次 Run 处理。
  void PrimeFrame(const AVFrame* frame, std::int64_t frame_index);

  // 取走最新预览帧；没有可用的新帧时 frame == nullptr。
  // 只返回“比上一张更接近当前目标且不越过目标”的帧与精确目标；粗帧按
  // 拖动方向接受，精确目标呈现后不再接受更远的帧。
  PreviewFrame TakeLatestFrame();

  bool IsReady() const;
  bool IsWaiting() const;
  bool IsThreadRunning() const;
  bool IsWarmIdle() const;
  bool IsWarming() const;
  std::string last_error() const;

  // D4（仅测试）：把有界缓存里的该帧按 NV12 布局落盘（Y 平面 w*h + UV 平面
  // w*h/2，行内不含 linesize padding），用于人工复核清晰度；缓存没有该帧
  // 返回 false。缓存帧本身是 NV12 时直接写；max_side 等于源宽时缓存的是
  // av_frame_clone 的源帧（软件解码常见 yuv420p），dump 在 test-only 代码里
  // 把 U/V 交织成 NV12 布局，不改缓存里的帧。只读缓存，不触发解码、不发布、
  // 不淘汰、不改任何状态。
  bool DumpCachedFrameNv12ForTest(std::int64_t frame_index,
                                  const std::string& path) const;
  // D4（仅测试）：最近创建的实例。在线预览的有界缓存只存在于实例内部，探针
  // 需要它才能在“真实会话拖动结束后”dump 缓存帧；产品代码不得调用。
  static OnlinePreviewDecoder* LatestInstanceForTest();

  // P0 度量（只读，不参与任何发布/解码判定）：每次 Begin/Resume 重置。
  // - open_ms：Begin/Resume 启动线程 → 解码器打开完成；线程已就绪复用为 0。
  // - first_publish_ms：Begin/Resume → 本次第一次 PublishLocked 成功。
  // - published：本次 PublishLocked 成功次数。
  // - delivered：本次 TakeLatestFrame 成功取走次数。
  // - last_delivered_before_resume：Resume/Begin 之前最后一次 UI 取走的帧。
  struct DragStats {
    std::int64_t open_ms = -1;
    std::int64_t first_publish_ms = -1;
    std::uint64_t published = 0;
    std::uint64_t delivered = 0;
    // Begin/Resume 之前最后一次 UI 取走帧（旧值）与本拖动采用的发布基准。
    std::int64_t last_delivered_before_resume = -1;
    std::int64_t baseline_frame = -1;
  };
  DragStats drag_stats() const;
  // 预热/PrimeFrame 诊断（只读）。
  std::int64_t warm_ms() const;
  std::int64_t warm_frames() const;
  std::uint64_t warm_skipped() const;
  std::uint64_t warm_failures() const;
  std::uint64_t primed_frames() const;
  bool IsFrameCached(std::int64_t frame) const;

  // 诊断/探针：发布帧数、丢弃的过期帧、seek 次数、缓存帧数/字节。
  std::uint64_t frames_published() const;
  std::uint64_t stale_dropped() const;
  std::uint64_t seek_count() const;
  int cached_frames() const;
  std::int64_t last_published_frame() const;
  // 丝滑/一致性探针：缓存预算用量、命中/未命中、粗帧与预取计数。
  std::size_t cache_bytes() const;
  std::size_t cache_peak_bytes() const;
  std::uint64_t cache_hits() const;
  std::uint64_t cache_misses() const;
  std::uint64_t coarse_published() const;
  std::uint64_t prefetched_frames() const;
  std::uint64_t decoded_frames() const;
  // P0 seek 诊断：strict 关键帧 seek 失败回退次数；最近一次 seek 的关键帧、
  // 首个解码帧与偏差（负=落到更早的 GOP，正是 S1 发现的 fMP4 落前一 GOP）。
  std::uint64_t seek_fallbacks() const;
  std::int64_t last_seek_keyframe() const;
  std::int64_t last_seek_landing_frame() const;
  std::int64_t last_seek_landing_offset() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lapian::media
