// T1.4 会话切换测试：失败不破坏旧会话；成功替换路径/首帧。

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

#include "media/MediaSession.h"
#ifdef _WIN32
#include "platform/gfx/D3D11Device.h"
#endif

namespace {

int g_failures = 0;
bool g_skipped = false;

void Check(bool condition, const std::string& what) {
  if (condition) {
    std::printf("[ OK ] %s\n", what.c_str());
  } else {
    std::printf("[FAIL] %s\n", what.c_str());
    ++g_failures;
  }
}

void Skip(const std::string& what) {
  std::printf("[SKIP] %s\n", what.c_str());
  g_skipped = true;
}

bool FileExists(const std::string& path) {
  const std::u8string utf8(reinterpret_cast<const char8_t*>(path.data()), path.size());
  return std::filesystem::exists(std::filesystem::path(utf8));
}

}  // namespace

int main() {
  std::printf("T1.4 媒体会话测试\n");

  std::string error;
#ifdef _WIN32
  lapian::gfx::D3D11Device device;
  if (!device.Create(&error)) {
    Skip("D3D11 设备创建失败: " + error);
    return 77;
  }
  ID3D11Device* native_device = device.device();
#else
  ID3D11Device* native_device = nullptr;
#endif

  lapian::media::MediaSession session;
  Check(!session.Open("Z:\\missing\\no-such-video.mp4", native_device, &error),
        "不存在的文件打开失败");
  Check(!session.IsOpen(), "失败后会话未打开");
  Check(!error.empty(), "失败后有错误信息");

  if (!FileExists("media/V9.mp4")) {
    Skip("media/V9.mp4 不存在，跳过有效会话切换");
    return g_failures == 0 ? 77 : 1;
  }

  Check(session.Open("media/V9.mp4", native_device, &error), "打开 V9");
  Check(session.IsOpen(), "V9 会话已打开");
  Check(session.info().video.width == 1920 && session.info().video.height == 1080,
        "V9 元数据 1080p");
  Check(session.first_frame().av_frame != nullptr, "V9 首帧可用");

  Check(!session.Open("Z:\\missing\\no-such-video.mp4", native_device, &error),
        "已有会话时打开不存在文件失败");
  Check(session.IsOpen(), "失败后旧会话仍打开");
  Check(session.info().video.width == 1920, "失败后旧元数据保留");

  if (FileExists("media/proxy/proxy-V1.mp4")) {
    Check(session.Open("media/proxy/proxy-V1.mp4", native_device, &error), "切换到 proxy-V1");
    Check(session.path_utf8().find("proxy-V1") != std::string::npos, "切到新路径");
    Check(session.info().video.width == 1920, "新会话 1080p");
  }

  // T7.4：前进步进走顺序解码（不再从关键帧重解），必须仍然帧精确；
  // 后退步进命中/未命中缓存都返回目标帧 PTS。
  if (session.HasIndex() && session.index().size() > 200) {
    lapian::media::DecodedFrameInfo info;
    std::string seek_error;
    Check(session.SeekToFrame(100, &info, &seek_error), "跳转到第 100 帧");
    Check(info.pts_us == session.index().PtsOfFrame(100), "第 100 帧 PTS 精确");
    Check(session.SeekToFrame(101, &info, &seek_error), "顺序前进到第 101 帧");
    Check(info.pts_us == session.index().PtsOfFrame(101), "顺序前进帧精确");
    Check(session.SeekToFrame(100, &info, &seek_error,
                              lapian::media::FrameCacheMode::kBackwardWindow),
          "后退到第 100 帧（填缓存）");
    Check(info.pts_us == session.index().PtsOfFrame(100), "后退帧精确");
    Check(session.SeekToFrame(99, &info, &seek_error,
                              lapian::media::FrameCacheMode::kBackwardWindow),
          "后退到第 99 帧（命中缓存）");
    Check(info.pts_us == session.index().PtsOfFrame(99), "缓存命中帧精确");

    // T7.9：精确 seek 只保留目标前一帧的 AVFrame 引用（不写 FrameCache）；
    // 紧接 PgUp 走 handoff，不应从关键帧重解或触发缓存 miss。
    const std::uint64_t hits_before =
        session.frame_cache().stats().hits;
    const std::uint64_t misses_before =
        session.frame_cache().stats().misses;
    Check(session.SeekToFrame(200, &info, &seek_error,
                              lapian::media::FrameCacheMode::kSeekHandoff),
          "精确 seek 到第 200 帧（保留前帧引用）");
    Check(info.pts_us == session.index().PtsOfFrame(200), "seek handoff 目标帧精确");
    Check(!session.frame_cache().Contains(199), "seek handoff 不写 FrameCache");
    Check(!session.frame_cache().Contains(200), "seek handoff 不缓存目标帧");
    Check(session.SeekToFrame(199, &info, &seek_error,
                              lapian::media::FrameCacheMode::kBackwardWindow),
          "seek 后紧接 PgUp 走 handoff");
    Check(info.pts_us == session.index().PtsOfFrame(199), "seek 后 PgUp 帧精确");
    Check(session.frame_cache().stats().hits == hits_before,
          "seek 后 PgUp 未走 FrameCache 命中");
    Check(session.frame_cache().stats().misses == misses_before,
          "seek 后 PgUp 未走 FrameCache 未命中");
  }

  session.Close();
  Check(!session.IsOpen(), "Close 后会话关闭");

  if (g_failures > 0) {
    std::printf("失败 %d 项\n", g_failures);
    return 1;
  }
  if (g_skipped) {
    std::printf("部分素材跳过\n");
    return 77;
  }
  std::printf("全部通过\n");
  return 0;
}
