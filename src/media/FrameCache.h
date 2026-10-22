#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>

struct AVFrame;

namespace lapian::media {

// T7.2：精确 seek 的帧缓存策略。
// - kNone：不读也不写（普通跳转/前进步进/播放循环，避免改变解码器位置语义）；
// - kBackwardWindow：命中则直接返回；未命中时缓存解码到目标前的最后 N 帧
//   （后退步进），后续连续后退可直接命中。
// 普通跳转也写缓存（target-only）实测每次多一次 3MB 回拷、对连续后退无帮助，
// 因此产品路径只在后退步进预热。
enum class FrameCacheMode {
  kNone,
  kBackwardWindow,
};

struct FrameCacheLimits {
  std::size_t max_bytes = 0;
  std::size_t max_entries = 0;
};

struct FrameCacheEntryInfo {
  std::int64_t pts = 0;
  std::int64_t pts_us = 0;
  std::int64_t duration_us = 0;
  bool keyframe = false;
  std::size_t bytes = 0;
};

struct FrameCacheStats {
  std::size_t entries = 0;
  std::size_t bytes = 0;
  std::size_t max_bytes = 0;
  std::size_t max_entries = 0;
  std::uint64_t hits = 0;
  std::uint64_t misses = 0;
  std::uint64_t puts = 0;
  std::uint64_t evictions = 0;
};

// T7.2：按字节预算淘汰的 LRU 帧缓存。
// 只缓存“系统内存里的软件帧”：硬件帧在 Put 时通过 av_hwframe_transfer_data 拷到
// NV12/P010，避免长期持有 D3D11VA 解码表面把解码器池耗尽。软件帧用 av_frame_clone
// 引用底层 AVBuffer，成本 O(1)。非线程安全，只在 GUI 线程使用。
class FrameCache {
 public:
  FrameCache();
  ~FrameCache();

  FrameCache(const FrameCache&) = delete;
  FrameCache& operator=(const FrameCache&) = delete;

  // 会清空现有条目。max_bytes=0 或 max_entries=0 表示禁用缓存。
  void SetLimits(std::size_t max_bytes, std::size_t max_entries);
  void Clear();

  // frame 仍是调用方的；成功时缓存持有一份独立引用（软件帧 ref / 硬件帧回拷）。
  bool Put(const AVFrame* frame, std::int64_t frame_index, std::int64_t pts,
           std::int64_t pts_us, std::int64_t duration_us, bool keyframe);

  // 命中时 *out 是缓存帧的 AVBuffer 引用（只读，不要修改帧数据），
  // 调用方负责 av_frame_free；info 非空时回填该帧的 PTS/关键帧/字节数。
  bool Get(std::int64_t frame_index, AVFrame** out, FrameCacheEntryInfo* info = nullptr);

  bool Contains(std::int64_t frame_index) const;
  FrameCacheStats stats() const;

  // 按分辨率/位深给预算：1080p 64MiB/24 帧、4K 256MiB/20 帧（T7.4 从 96MiB/8
  // 扩到 20 帧的后退窗口）、720p 及以下 32MiB/24 帧；10-bit（P010）按 3 字节/像素计。
  static FrameCacheLimits LimitsForFrame(int width, int height, int bit_depth);

  // 一帧 NV12（8-bit）或 P010（10-bit）的近似字节数；用于预算与日志。
  static std::size_t FrameBytesFor(int width, int height, int bit_depth);

 private:
  struct Entry {
    AVFrame* frame = nullptr;
    std::int64_t frame_index = -1;
    std::int64_t pts = 0;
    std::int64_t pts_us = 0;
    std::int64_t duration_us = 0;
    bool keyframe = false;
    std::size_t bytes = 0;
  };

  using EntryList = std::list<Entry>;
  using EntryMap = std::unordered_map<std::int64_t, EntryList::iterator>;

  static void FreeEntry(Entry* entry);
  void EvictUntilWithinLimits();

  EntryList entries_;  // 头部最旧（LRU），尾部最新（MRU）
  EntryMap by_frame_;
  std::size_t bytes_ = 0;
  std::size_t max_bytes_ = 0;
  std::size_t max_entries_ = 0;
  std::uint64_t hits_ = 0;
  std::uint64_t misses_ = 0;
  std::uint64_t puts_ = 0;
  std::uint64_t evictions_ = 0;
};

}  // namespace lapian::media
