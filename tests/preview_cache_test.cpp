// DP-2 预览环缓存/关键帧缩略图缓存：预算、LRU、窗口裁剪、最近关键帧查询。

#include <cstdint>
#include <cstdio>
#include <string>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "media/PreviewRingCache.h"

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
  if (condition) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n", what.c_str());
    ++g_failures;
  }
}

AVFrame* MakeNv12(int width, int height) {
  AVFrame* frame = av_frame_alloc();
  if (frame == nullptr) {
    return nullptr;
  }
  frame->format = AV_PIX_FMT_NV12;
  frame->width = width;
  frame->height = height;
  if (av_frame_get_buffer(frame, 32) < 0) {
    av_frame_free(&frame);
    return nullptr;
  }
  return frame;
}

}  // namespace

int main() {
  std::printf("DP-2 预览缓存测试\n");
  Check(lapian::media::PreviewRingCache::kDefaultBudgetBytes ==
            192u * 1024u * 1024u,
        "预览环缓存默认预算 192MiB（与 32 帧热缓存合计 ≤256MiB）");
  Check(lapian::media::PreviewThumbnailCache::kDefaultBudgetBytes ==
            64u * 1024u * 1024u,
        "缩略图缓存默认预算 64MiB");

  {
    lapian::media::PreviewRingCache cache(2780u * 1024u);
    for (int i = 0; i < 100; ++i) {
      AVFrame* frame = MakeNv12(320, 180);
      if (i == 0) {
        Check(frame != nullptr, "能分配 NV12 测试帧");
      }
      if (frame == nullptr) {
        return 1;
      }
      cache.Put(i, i * 33333, frame);
    }
    const auto stats = cache.stats();
    Check(stats.entries == lapian::media::PreviewRingCache::kMinEntries,
          "超预算时淘汰到最小保留 32 帧");
    Check(stats.bytes <= cache.budget_bytes(),
          "淘汰后占用不超过预算（32×320×180 NV12）");
    Check(stats.peak_bytes >= stats.bytes,
          "记录峰值占用");
    Check(stats.evictions > 0, "记录淘汰次数");
    Check(cache.Clone(0) == nullptr, "最旧帧已淘汰");
    AVFrame* newest = cache.Clone(99);
    Check(newest != nullptr, "最新帧仍可命中");
    if (newest != nullptr) {
      Check(newest->width == 320 && newest->height == 180,
            "命中帧尺寸保持 320×180");
      av_frame_free(&newest);
    }
  }

  {
    lapian::media::PreviewRingCache cache(700u * 1024u);
    for (int i = 0; i < 32; ++i) {
      cache.Put(i, 0, MakeNv12(160, 90));
    }
    AVFrame* hit = cache.Clone(10);
    Check(hit != nullptr, "32 帧内可精确命中");
    if (hit != nullptr) {
      av_frame_free(&hit);
    }
    // 新增 5 帧后，刚访问过的 10 应该因 MRU 保留，最旧的 0..4 被淘汰。
    for (int i = 32; i < 37; ++i) {
      cache.Put(i, 0, MakeNv12(160, 90));
    }
    AVFrame* kept = cache.Clone(10);
    Check(kept != nullptr, "最近访问帧不受 LRU 淘汰影响");
    if (kept != nullptr) {
      av_frame_free(&kept);
    }
    Check(cache.Clone(0) == nullptr, "未访问的最旧帧被淘汰");
  }

  {
    lapian::media::PreviewRingCache cache(16u * 1024u * 1024u);
    for (int i = 0; i < 32; ++i) {
      cache.Put(i, 0, MakeNv12(160, 90));
    }
    cache.TrimToWindow(20, 3);
    Check(cache.Contains(20) && cache.Contains(17) && cache.Contains(23),
          "窗口内帧保留");
    Check(!cache.Contains(16) && !cache.Contains(24), "窗口外帧被裁剪");
    Check(cache.stats().entries == 7, "窗口裁剪后条目数正确");
  }

  {
    lapian::media::PreviewThumbnailCache thumbs(1u * 1024u * 1024u);
    for (int i = 0; i < 4; ++i) {
      thumbs.Put(i * 30, i * 1000000, MakeNv12(320, 180));
    }
    std::int64_t keyframe = -1;
    AVFrame* frame = thumbs.CloneNearestBelow(75, &keyframe);
    Check(frame != nullptr && keyframe == 60,
          "最近关键帧缩略图查询命中 60");
    if (frame != nullptr) {
      av_frame_free(&frame);
    }
    keyframe = -1;
    frame = thumbs.CloneNearestBelow(10, &keyframe);
    Check(frame != nullptr && keyframe == 0,
          "目标早于第一个关键帧时返回最前缩略图");
    if (frame != nullptr) {
      av_frame_free(&frame);
    }
    frame = thumbs.CloneNearestBelow(-1, &keyframe);
    Check(frame == nullptr, "目标早于所有关键帧时不返回未来粗帧");
    frame = thumbs.Clone(60);
    Check(frame != nullptr, "缩略图精确命中");
    if (frame != nullptr) {
      av_frame_free(&frame);
    }
    thumbs.TrimToWindow(60, 1);
    Check(thumbs.Contains(60) && !thumbs.Contains(0) && !thumbs.Contains(90),
          "缩略图滑动窗口裁剪");
  }

  {
    lapian::media::PreviewThumbnailCache thumbs(300u * 1024u);
    for (int i = 0; i < 10; ++i) {
      thumbs.Put(i * 30, i * 1000000, MakeNv12(320, 180));
    }
    const auto stats = thumbs.stats();
    Check(stats.bytes <= thumbs.budget_bytes(),
          "缩略图超预算时按 LRU 淘汰到 64MiB 预算内");
    Check(stats.evictions > 0, "缩略图记录淘汰次数");
    Check(thumbs.Clone(0) == nullptr, "最旧缩略图已淘汰");
    AVFrame* newest = thumbs.Clone(270);
    Check(newest != nullptr, "最新缩略图仍命中");
    if (newest != nullptr) {
      av_frame_free(&newest);
    }
  }

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("全部通过\n");
  return 0;
}
