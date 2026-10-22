// T7.2 帧缓存单测：字节预算、条目上限、LRU、克隆独立性、清空与分辨率预算。

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

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

#include "media/FrameCache.h"
#include "media/VideoDecoder.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
  if (condition) {
    std::printf("[ OK ] %s\n", what);
  } else {
    std::printf("[FAIL] %s\n", what);
    ++g_failures;
  }
}

AVFrame* MakeNv12(int width, int height, int seed) {
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
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      frame->data[0][y * frame->linesize[0] + x] = static_cast<std::uint8_t>(seed);
    }
  }
  return frame;
}

}  // namespace

int main() {
  using lapian::media::FrameCache;
  using lapian::media::FrameCacheEntryInfo;
  using lapian::media::FrameCacheLimits;

  std::printf("T7.2 帧缓存测试\n");

  constexpr std::size_t kMiB = 1024 * 1024;
  const FrameCacheLimits hd = FrameCache::LimitsForFrame(1920, 1080, 8);
  Check(hd.max_bytes == 64 * kMiB, "1080p 预算 64MiB");
  Check(hd.max_entries == 24, "1080p 条目上限 24");
  const FrameCacheLimits uhd = FrameCache::LimitsForFrame(3840, 2160, 8);
  Check(uhd.max_bytes == 256 * kMiB, "4K 预算 256MiB（T7.4）");
  Check(uhd.max_entries == 20, "4K 条目上限 20（T7.4 后退窗口）");
  const FrameCacheLimits ten_bit = FrameCache::LimitsForFrame(1920, 1080, 10);
  Check(ten_bit.max_bytes == 64 * kMiB, "1080p 10-bit 预算仍为 64MiB");

  const std::size_t frame_bytes = FrameCache::FrameBytesFor(64, 64, 8);
  Check(frame_bytes == 64u * 64u * 3u / 2u, "64x64 NV12 帧字节数 6144");
  Check(FrameCache::FrameBytesFor(64, 64, 10) == 64u * 64u * 3u,
        "64x64 P010 帧字节数 12288");

  FrameCache cache;
  cache.SetLimits(frame_bytes * 3, 8);
  Check(cache.stats().entries == 0 && cache.stats().bytes == 0, "初始为空");

  for (int i = 0; i < 3; ++i) {
    AVFrame* frame = MakeNv12(64, 64, 10 + i);
    Check(frame != nullptr, "创建 NV12 测试帧");
    if (frame == nullptr) {
      continue;
    }
    const bool stored = cache.Put(frame, i, i, static_cast<std::int64_t>(i) * 1000,
                                  40000, i == 0);
    Check(stored, "写入帧");
    av_frame_free(&frame);
  }
  Check(cache.stats().entries == 3, "3 个条目");
  Check(cache.stats().bytes == frame_bytes * 3, "字节计数精确");
  Check(cache.Contains(0) && cache.Contains(1) && cache.Contains(2), "三帧都在缓存");

  AVFrame* got = nullptr;
  FrameCacheEntryInfo meta;
  Check(cache.Get(0, &got, &meta), "命中帧 0");
  Check(got != nullptr && got->data[0][0] == 10, "命中帧数据正确");
  Check(meta.pts_us == 0 && meta.duration_us == 40000 && meta.keyframe,
        "命中元数据正确");
  if (got != nullptr) {
    // 释放命中引用不应释放缓存持有的底层数据。
    av_frame_free(&got);
  }
  got = nullptr;
  Check(cache.Get(0, &got, nullptr), "释放引用后再次命中帧 0");
  Check(got != nullptr && got->data[0][0] == 10 && got->buf[0] != nullptr,
        "缓存仍持有底层 AVBuffer");
  if (got != nullptr) {
    av_frame_free(&got);
  }
  Check(cache.stats().hits == 2, "命中计数 2");
  Check(cache.stats().misses == 0, "未命中计数 0");

  // 触过帧 0 后写入帧 3：最久未用的帧 1 应被淘汰。
  AVFrame* frame3 = MakeNv12(64, 64, 13);
  Check(frame3 != nullptr, "创建帧 3");
  if (frame3 != nullptr) {
    Check(cache.Put(frame3, 3, 3, 3000, 40000, false), "写入帧 3");
    av_frame_free(&frame3);
  }
  Check(cache.stats().entries == 3, "淘汰后仍为 3 个条目");
  Check(!cache.Contains(1), "LRU 淘汰最久未用帧 1");
  Check(cache.Contains(0) && cache.Contains(2) && cache.Contains(3),
        "刚访问的帧 0 保留");
  Check(cache.stats().evictions == 1, "淘汰计数 1");

  // 未命中计数。
  AVFrame* missing = nullptr;
  Check(!cache.Get(1, &missing, nullptr), "已淘汰帧未命中");
  Check(missing == nullptr, "未命中不返回帧");
  Check(cache.stats().misses == 1, "未命中计数 1");

  // 条目上限优先于字节上限。
  cache.SetLimits(frame_bytes * 100, 2);
  Check(cache.stats().entries == 0 && cache.stats().bytes == 0,
        "SetLimits 清空并重置统计");
  for (int i = 0; i < 3; ++i) {
    AVFrame* frame = MakeNv12(64, 64, 20 + i);
    if (frame != nullptr) {
      cache.Put(frame, i, i, i * 1000, 40000, false);
      av_frame_free(&frame);
    }
  }
  Check(cache.stats().entries == 2, "条目上限 2");
  Check(!cache.Contains(0) && cache.Contains(1) && cache.Contains(2),
        "条目上限按 LRU 淘汰");

  // 单帧超过预算：拒绝写入，且不影响已有条目。
  cache.SetLimits(frame_bytes - 1, 4);
  AVFrame* too_big = MakeNv12(64, 64, 30);
  if (too_big != nullptr) {
    Check(!cache.Put(too_big, 9, 9, 9000, 40000, false), "超预算帧被拒绝");
    av_frame_free(&too_big);
  }
  Check(cache.stats().entries == 0, "超预算帧不占缓存");

  // 小于一帧的预算会被提升到一帧，避免预算永远拒绝所有帧。
  const std::size_t huge_bytes = FrameCache::FrameBytesFor(16384, 8640, 10);
  const FrameCacheLimits huge = FrameCache::LimitsForFrame(16384, 8640, 10);
  Check(huge.max_bytes >= huge_bytes, "超大帧预算至少容纳一帧");
  Check(huge.max_bytes / huge_bytes == 1, "超大帧预算只够一帧");
  Check(huge.max_entries == 1, "超大帧单帧超预算时条目上限降为 1");
  // T7.4：4K 档预算扩到 256MiB；8K/10-bit 一帧约 95MiB，仍能放 2 帧。
  const std::size_t eight_k_bytes = FrameCache::FrameBytesFor(7680, 4320, 10);
  const FrameCacheLimits eight_k = FrameCache::LimitsForFrame(7680, 4320, 10);
  Check(eight_k.max_bytes >= eight_k_bytes * 2, "8K/10-bit 至少容纳 2 帧");

  cache.SetLimits(frame_bytes * 4, 4);
  AVFrame* frame = MakeNv12(64, 64, 40);
  if (frame != nullptr) {
    cache.Put(frame, 7, 7, 7000, 40000, false);
    av_frame_free(&frame);
  }
  Check(cache.stats().entries == 1, "清空前有一个条目");
  cache.Clear();
  Check(cache.stats().entries == 0 && cache.stats().bytes == 0,
        "Clear 释放条目与字节计数");
  Check(cache.stats().hits == 0 && cache.stats().misses == 0,
        "Clear 重置命中统计");

  // 真实硬件帧回拷：D3D11VA 帧必须能转成软件 NV12 再缓存，且不持有解码表面。
  if (std::filesystem::exists("media/proxy/proxy-V4.mp4")) {
    lapian::media::VideoDecoder decoder;
    std::string error;
    if (decoder.Open("media/proxy/proxy-V4.mp4", 0, true, &error)) {
      lapian::media::DecodedFrameInfo decoded;
      if (decoder.DecodeFirstFrame(&decoded, &error)) {
        FrameCache hw_cache;
        hw_cache.SetLimits(128 * kMiB, 8);
        const bool stored =
            hw_cache.Put(decoded.av_frame, 0, decoded.pts, decoded.pts_us,
                         decoded.duration_us, decoded.key_frame);
        Check(stored, "硬件/软件首帧回拷缓存成功");
        Check(hw_cache.stats().entries == 1, "回拷后缓存有一个条目");
        Check(hw_cache.stats().bytes >= 3840u * 2160u * 3u / 2u,
              "4K NV12 帧字节数 >= 12MB");
        AVFrame* hit = nullptr;
        FrameCacheEntryInfo hit_info;
        Check(hw_cache.Get(0, &hit, &hit_info), "回拷帧可命中");
        Check(hit != nullptr && hit->width == decoded.width &&
                  hit->height == decoded.height && hit->hw_frames_ctx == nullptr,
              "命中帧是软件帧且尺寸一致");
        if (hit != nullptr) {
          av_frame_free(&hit);
        }
        std::printf("       hw=%d %dx%d bytes=%zu\n", decoded.hardware ? 1 : 0,
                    decoded.width, decoded.height, hw_cache.stats().bytes);
      } else {
        std::printf("[SKIP] 4K 首帧解码失败：%s\n", error.c_str());
      }
    } else {
      std::printf("[SKIP] 4K 解码器打开失败：%s\n", error.c_str());
    }
  } else {
    std::printf("[SKIP] media/proxy/proxy-V4.mp4 不存在\n");
  }

  // 1080p 的 D3D11VA 纹理常见 1920x1088 分配、1920x1080 显示；回拷缓存必须
  // 保留显示尺寸，否则缓存命中后 fit 会跳变（T7.2.1）。
  if (std::filesystem::exists("media/测试.mp4")) {
    lapian::media::VideoDecoder decoder;
    std::string error;
    if (decoder.Open("media/测试.mp4", 0, true, &error)) {
      lapian::media::DecodedFrameInfo decoded;
      if (decoder.DecodeFirstFrame(&decoded, &error)) {
        FrameCache hd_cache;
        hd_cache.SetLimits(64 * kMiB, 24);
        const bool stored =
            hd_cache.Put(decoded.av_frame, 0, decoded.pts, decoded.pts_us,
                         decoded.duration_us, decoded.key_frame);
        Check(stored, "1080p 硬件帧回拷缓存成功");
        AVFrame* hit = nullptr;
        Check(hd_cache.Get(0, &hit, nullptr), "1080p 回拷帧可命中");
        Check(hit != nullptr && hit->width == decoded.width &&
                  hit->height == decoded.height,
              "1080p 缓存帧保留显示尺寸（不是 1088 编码尺寸）");
        if (hit != nullptr) {
          std::printf("       1080p hw=%d frame=%dx%d hit=%dx%d\n",
                      decoded.hardware ? 1 : 0, decoded.width, decoded.height,
                      hit->width, hit->height);
          av_frame_free(&hit);
        }
      } else {
        std::printf("[SKIP] 1080p 首帧解码失败：%s\n", error.c_str());
      }
    } else {
      std::printf("[SKIP] 1080p 解码器打开失败：%s\n", error.c_str());
    }
  } else {
    std::printf("[SKIP] media/测试.mp4 不存在\n");
  }

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  std::printf("全部通过\n");
  return 0;
}
