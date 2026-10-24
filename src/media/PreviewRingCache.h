#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <unordered_map>

struct AVFrame;

namespace lapian::media {

// DP-2：拖动预览的有界软件帧缓存。只保存已经降采样/回拷好的 NV12 软件帧，
// 不保存 D3D11 硬件帧（D3D11VA 静态表面池有限，DP-1 实测持有 16 张就会卡住）。
// 所有方法都不是线程安全的；PreviewDecoder 在自己的互斥量内串行调用。
// 预算独立于正式 FrameCache（64/256MiB），并与预览热缓存、缩略图缓存统一
// 在 PreviewDiagnostics 里记账。
class PreviewRingCache {
 public:
  static constexpr std::size_t kDefaultBudgetBytes = 192u * 1024u * 1024u;
  static constexpr std::size_t kMinEntries = 32;
  static constexpr std::size_t kMaxEntries = 4096;

  struct Stats {
    std::size_t entries = 0;
    std::size_t bytes = 0;
    std::size_t peak_bytes = 0;
    int puts = 0;
    int hits = 0;
    int evictions = 0;
  };

  explicit PreviewRingCache(std::size_t budget_bytes = kDefaultBudgetBytes);
  ~PreviewRingCache();

  PreviewRingCache(const PreviewRingCache&) = delete;
  PreviewRingCache& operator=(const PreviewRingCache&) = delete;

  // 接管 frame 所有权；frame 为空时忽略。frame_index 已存在时先替换旧条目。
  void Put(std::int64_t frame_index, std::int64_t pts_us, AVFrame* frame);
  // 精确命中返回 av_frame_clone（调用方负责 av_frame_free），未命中返回 nullptr。
  AVFrame* Clone(std::int64_t frame_index);
  bool Contains(std::int64_t frame_index) const;
  // 丢掉距 center 超过 radius 帧的条目；用于切拖动起点后收窄窗口。
  void TrimToWindow(std::int64_t center, std::int64_t radius);
  // 手动收窄预算（End/关预览时用）；不能低于 kMinEntries 帧的占用。
  void TrimToBudget(std::size_t budget_bytes);
  void Clear();

  std::size_t budget_bytes() const { return budget_bytes_; }
  std::size_t bytes() const { return bytes_; }
  Stats stats() const;

 private:
  struct Entry {
    std::int64_t frame_index = 0;
    std::int64_t pts_us = 0;
    AVFrame* frame = nullptr;
    std::size_t bytes = 0;
    std::uint64_t tick = 0;
  };

  void EvictLocked(std::size_t budget_bytes);
  void EraseLocked(std::list<Entry>::iterator it);
  void MoveToFrontLocked(std::list<Entry>::iterator it);

  std::list<Entry> entries_;  // 头部最新（MRU），尾部最旧（LRU）
  std::unordered_map<std::int64_t, std::list<Entry>::iterator> index_;
  std::size_t budget_bytes_ = kDefaultBudgetBytes;
  std::size_t bytes_ = 0;
  std::size_t peak_bytes_ = 0;
  std::uint64_t tick_ = 0;
  int puts_ = 0;
  int hits_ = 0;
  int evictions_ = 0;
};

// DP-2：粗帧/关键帧缩略图缓存（320×180 NV12，滑动窗口 + LRU，预算 ≤64MiB）。
// 条目既可以是关键帧缩略图（远跳先用），也可以是软件反向解码时顺带降采样的
// 邻近帧（局部反向命中更准）。只用于“先有画面”；粗帧永远带
// PreviewFrame::coarse 标记，不会被当成最终精确帧。同样非线程安全，由
// PreviewDecoder 串行化。
class PreviewThumbnailCache {
 public:
  static constexpr std::size_t kDefaultBudgetBytes = 64u * 1024u * 1024u;
  static constexpr std::size_t kMaxEntries = 1024;

  struct Stats {
    std::size_t entries = 0;
    std::size_t bytes = 0;
    std::size_t peak_bytes = 0;
    int puts = 0;
    int hits = 0;
    int evictions = 0;
  };

  explicit PreviewThumbnailCache(
      std::size_t budget_bytes = kDefaultBudgetBytes);
  ~PreviewThumbnailCache();

  PreviewThumbnailCache(const PreviewThumbnailCache&) = delete;
  PreviewThumbnailCache& operator=(const PreviewThumbnailCache&) = delete;

  void Put(std::int64_t keyframe_frame, std::int64_t pts_us, AVFrame* frame);
  AVFrame* Clone(std::int64_t keyframe_frame);
  // 找 <= target_frame 的最近关键帧缩略图；*keyframe_frame 返回实际帧号。
  AVFrame* CloneNearestBelow(std::int64_t target_frame,
                             std::int64_t* keyframe_frame);
  bool Contains(std::int64_t keyframe_frame) const;
  void TrimToWindow(std::int64_t center_frame, std::int64_t radius_frames);
  void TrimToBudget(std::size_t budget_bytes);
  void Clear();

  std::size_t bytes() const { return bytes_; }
  std::size_t budget_bytes() const { return budget_bytes_; }
  Stats stats() const;

 private:
  struct Entry {
    std::int64_t pts_us = 0;
    AVFrame* frame = nullptr;
    std::size_t bytes = 0;
    std::uint64_t tick = 0;
  };

  void EvictLocked(std::size_t budget_bytes);

  std::map<std::int64_t, Entry> entries_;  // 按关键帧帧号升序
  std::size_t budget_bytes_ = kDefaultBudgetBytes;
  std::size_t bytes_ = 0;
  std::size_t peak_bytes_ = 0;
  std::uint64_t tick_ = 0;
  int puts_ = 0;
  int hits_ = 0;
  int evictions_ = 0;
};

}  // namespace lapian::media
