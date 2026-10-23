#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "media/FrameCache.h"
#include "media/FrameIndex.h"
#include "media/MediaInfo.h"
#include "media/MediaProbe.h"
#include "media/VideoDecoder.h"

struct AVFrame;
struct ID3D11Device;

namespace lapian::online {
class GrowingFormat;
class GrowingSource;
}  // namespace lapian::online

namespace lapian::media {

// T7.1：一次打开的分段耗时（毫秒），只用于冷启动测量，不参与逻辑。
struct MediaOpenTimings {
  double probe_ms = 0.0;
  double decoder_open_ms = 0.0;
  double first_frame_ms = 0.0;
  double index_ms = 0.0;
  double total_ms = 0.0;
  std::size_t index_frames = 0;
  bool index_ok = false;
};

// 一条已打开视频的媒体会话：探测结果 + 解码器 + 首帧。
// T1.4 用它切换文件；T1.5 在后面继续用同一个解码器做播放。
class MediaSession {
 public:
  MediaSession();
  ~MediaSession();

  MediaSession(const MediaSession&) = delete;
  MediaSession& operator=(const MediaSession&) = delete;

  // 成功后才替换当前会话；失败时保持旧会话不变。
  // T7.1：build_index=false 时只做探测/解码首帧，索引由调用方在后台建好后
  // 通过 InstallIndex 安装；默认 true 保持既有同步语义（回归测试用）。
  bool Open(const std::string& utf8_path, ID3D11Device* device, std::string* error,
            bool build_index = true);

  // T8.6：从可增长读层打开（在线 `.part`）。`part_path_utf8` 是实际读取文件；
  // `display_name_utf8` 只用于 UI/记录标识，不会打开。成功后会话持有 source/
  // GrowingFormat，生命周期长于 decoder；本地路径分支不受影响。
  bool OpenGrowing(std::shared_ptr<lapian::online::GrowingSource> source,
                   const std::string& part_path_utf8,
                   const std::string& display_name_utf8,
                   ID3D11Device* device, std::string* error,
                   bool prefer_hardware = true);
  void Close();

  bool IsGrowing() const { return growing_; }
  std::shared_ptr<lapian::online::GrowingSource> growing_source_for_index() const {
    return growing_source_;
  }
  // 已完成下载且所有字节可读。
  bool GrowingComplete() const;
  std::int64_t GrowingPublished() const;
  // AVIO 已读到的文件偏移；用于判断接下来解码是否会撞上未发布区。
  std::int64_t GrowingReadPosition() const;
  // 下一帧解码至少还需要 margin 字节的未读缓冲；UI 用它做播放缓冲闸门。
  bool CanDecodeAhead(std::int64_t margin_bytes) const;
  // T8.6 异步 seek：中断本会话的 growing cursor（不取消共享 source）。
  // 中断后该会话的 AVFormatContext 不可继续使用，调用方应关闭并重建。
  void SetGrowingInterrupt(std::shared_ptr<std::atomic<bool>> flag);
  void InterruptGrowingReads();
  bool GrowingInterrupted() const;
  // 未缓存区的 CFR 近似 seek：目标 PTS 由容器帧率/帧号估算；解码到 >= PTS 后
  // 校验落点，偏差超过半帧返回 false（不冒充精确）。
  bool SeekToEstimatedPts(std::int64_t target_pts_us, std::int64_t target_frame,
                          DecodedFrameInfo* out, std::string* error);
  // 部分索引：只替换索引，不清 decoder 顺序位置/帧缓存（帧号前缀保持稳定）。
  void InstallOnlineIndex(FrameIndex index, double build_ms);

  // 用当前会话的文件/流建索引；失败不影响已打开的会话（索引保持为空）。
  bool BuildIndexNow(std::string* error, const std::atomic<bool>* cancel = nullptr);
  // 安装后台建好的索引（调用方保证在 GUI 线程、且会话仍是同一个）。
  void InstallIndex(FrameIndex index, double build_ms);
  bool HasIndex() const { return index_.IsReady() && !index_.empty(); }
  int video_stream_index() const { return probe_.info.video.stream_index; }
  // T7.2/T7.9：普通跳转传 kSeekHandoff（保留目标前一帧引用，供 seek 后 PgUp 直接呈现）；
  // 后退步进传 kBackwardWindow 预热最近若干帧，后续连续后退可直接命中。
  bool SeekToFrame(std::int64_t frame, DecodedFrameInfo* out, std::string* error,
                   FrameCacheMode cache_mode = FrameCacheMode::kNone);
  // 缓存命中后解码器可能不在目标帧之后；起播前调用一次，必要时真正 seek。
  bool EnsureSequentialPosition(std::int64_t frame, std::string* error);

  // T7.11.1：后退步进跨 GOP 时的延后回拷。窗口内的硬解帧先只保留 AVFrame 引用
  // （O(1)，不触发 GPU→CPU 拷贝），目标帧立即返回给调用方呈现；调用方在事件循环
  // 空闲时用 CopyPendingCacheFrames 分块写入 FrameCache。这样“跨 GOP 边界后退
  // 第一步”不再把 24 帧同步回拷（76–105ms）算进 P7 呈现延迟。
  bool HasPendingCacheWindow() const { return !pending_cache_window_.empty(); }
  // 最多回拷 max_frames 帧；返回 true 表示窗口里还有剩余。只在 GUI 线程调用。
  bool CopyPendingCacheFrames(std::size_t max_frames);
  // 丢弃尚未回拷的引用（切文件/关窗/开始新的解码前调用，避免占住解码表面）。
  void CancelPendingCacheWindow();

  bool IsOpen() const { return open_; }
  const MediaInfo& info() const { return probe_.info; }
  const MediaProbeResult& probe() const { return probe_; }
  // 最近一次尝试的探测结果（失败时也可读，用于 UI 错误分类）。
  const MediaProbeResult& last_attempt() const { return last_attempt_; }
  const DecodedFrameInfo& first_frame() const { return first_frame_; }
  const FrameIndex& index() const { return index_; }
  // T7.2：帧缓存上限/命中统计（日志与测试用）。
  const FrameCache& frame_cache() const { return frame_cache_; }
  // 最近一次 Open 的分段耗时（T7.1 冷启动报告）。
  const MediaOpenTimings& last_open_timings() const { return last_open_timings_; }
  VideoDecoder& decoder() { return *decoder_; }
  const std::string& path_utf8() const { return probe_.info.path_utf8; }
  bool IsVfrSuspect() const { return probe_.info.vfr.suspect; }

 private:
  void ClearSeekHandoff();
  bool AddPendingCacheFrame(const DecodedFrameInfo& info, std::int64_t frame_index);

  struct PendingCacheFrame {
    AVFrame* frame = nullptr;
    std::int64_t frame_index = -1;
    DecodedFrameInfo info;
  };

  MediaProbeResult probe_;
  MediaProbeResult last_attempt_;
  FrameIndex index_;
  FrameCache frame_cache_;
  std::unique_ptr<VideoDecoder> decoder_;
  // 声明顺序保证析构时 decoder 先于 growing_format/source 释放。
  std::shared_ptr<lapian::online::GrowingSource> growing_source_;
  std::unique_ptr<lapian::online::GrowingFormat> growing_format_;
  std::shared_ptr<std::atomic<bool>> growing_interrupt_;
  std::string growing_part_path_;
  bool growing_ = false;
  DecodedFrameInfo first_frame_;
  MediaOpenTimings last_open_timings_;
  // 缓存命中时返回的软件帧，保持到下一次 Seek/Close，沿用解码器帧的生命周期约定。
  AVFrame* last_hit_frame_ = nullptr;
  // T7.9：精确 seek 时保留的目标前一帧引用（硬解帧保持 D3D11 纹理，不回拷），
  // 供“seek 后紧接 PgUp”直接呈现；下一次 Seek/Close 释放。
  AVFrame* seek_previous_frame_ = nullptr;
  std::int64_t seek_previous_index_ = -1;
  DecodedFrameInfo seek_previous_info_;
  // T7.11.1：等待回拷的后退窗口帧（硬解帧引用 + 元数据）。只在 GUI 线程访问。
  std::vector<PendingCacheFrame> pending_cache_window_;
  // true 表示解码器未必停在“最后返回帧的下一帧”，起播前需要重新定位。
  bool decoder_stale_ = true;
  // T7.4：解码器顺序位置（下一帧 DecodeNextFrame 会返回的帧号）；-1 表示未知。
  // 前进步进/精确 seek 后紧接 PgDn 只解一帧，不再从关键帧重解整段。
  std::int64_t sequential_frame_ = -1;
  bool open_ = false;
};

}  // namespace lapian::media
