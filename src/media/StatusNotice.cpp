#include "media/StatusNotice.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace lapian::media {
namespace {

std::string Join(const std::vector<std::string>& parts, const std::string& separator) {
  std::string result;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      result += separator;
    }
    result += parts[i];
  }
  return result;
}

std::string FormatFps(double fps) {
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "%.3f", fps);
  std::string text(buffer);
  while (!text.empty() && text.back() == '0') {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.') {
    text.pop_back();
  }
  return text;
}

bool IsGuaranteedCodec(const std::string& codec) {
  return codec == "h264" || codec == "hevc";
}

std::string ResolutionText(const MediaInfo& info) {
  return std::to_string(info.video.width) + "×" + std::to_string(info.video.height);
}

// 10-bit 单独成条，不再把“非 8-bit”塞进超出保证范围的原因里，避免同一事实报两次。
bool BeyondGuarantee(const MediaInfo& info, std::vector<std::string>* reasons) {
  const VideoStreamInfo& video = info.video;
  const std::int64_t pixels = static_cast<std::int64_t>(video.width) * video.height;
  const std::int64_t guarantee_pixels = 1920LL * 1080LL;
  if (pixels > guarantee_pixels) {
    reasons->push_back(ResolutionText(info));
  }
  if (video.frame_rate.valid() && video.frame_rate.ToDouble() > 60.0 + 1.0e-6) {
    reasons->push_back(">" + FormatFps(video.frame_rate.ToDouble()) + "fps");
  }
  if (!video.codec.empty() && video.codec != "unknown" && !IsGuaranteedCodec(video.codec)) {
    reasons->push_back(video.codec);
  }
  return !reasons->empty();
}

}  // namespace

const char* StatusNoticeKindName(StatusNoticeKind kind) {
  switch (kind) {
    case StatusNoticeKind::kNoAudio:
      return "no-audio";
    case StatusNoticeKind::kScanning:
      return "scanning";
    case StatusNoticeKind::kOutOfGuarantee:
      return "out-of-range";
    case StatusNoticeKind::kTenBit:
      return "10-bit";
    case StatusNoticeKind::kFrameRateDrop:
      return "frame-drop";
    case StatusNoticeKind::kVfr:
      return "vfr";
    case StatusNoticeKind::kError:
      return "error";
  }
  return "unknown";
}

int StatusNoticePriority(StatusNoticeKind kind) {
  switch (kind) {
    case StatusNoticeKind::kNoAudio:
      return 0;
    case StatusNoticeKind::kScanning:
      return 1;
    case StatusNoticeKind::kOutOfGuarantee:
      return 2;
    case StatusNoticeKind::kTenBit:
      return 3;
    case StatusNoticeKind::kFrameRateDrop:
      return 4;
    case StatusNoticeKind::kVfr:
      return 5;
    case StatusNoticeKind::kError:
      return 6;
  }
  return -1;
}

StatusNotice MakeStatusNotice(StatusNoticeKind kind) {
  StatusNotice notice;
  notice.kind = kind;
  switch (kind) {
    case StatusNoticeKind::kNoAudio:
      notice.tag = "无音频";
      notice.text = "无音频：正常播放，波形不可用";
      notice.detail = "该文件没有音轨；播放、逐帧、标记与截图不受影响。";
      break;
    case StatusNoticeKind::kScanning:
      notice.tag = "扫描中";
      notice.text = "总帧数扫描中…";
      notice.detail = "正在建立 PTS 索引；总帧数暂为未知或估算值，步进/输入帧号可能暂不可用。";
      break;
    case StatusNoticeKind::kOutOfGuarantee:
      notice.tag = "超出保证";
      notice.text = "超出保证范围：仍可播放，性能不保证";
      notice.detail = "保证档：H.264/H.265 8-bit SDR，≤1080p60。";
      break;
    case StatusNoticeKind::kTenBit:
      notice.tag = "10-bit";
      notice.text = "10-bit/HDR：可尝试播放，但不承诺颜色正确";
      notice.detail = "首期不做 HDR/10-bit 色彩管理；颜色可能与源不一致。";
      break;
    case StatusNoticeKind::kFrameRateDrop:
      notice.tag = "降帧";
      notice.text = "帧率低于源片声明：帧号与源片不同步/约 1/2";
      notice.detail = "当前流实际 fps 以 playurl/容器为准；标记与截图记录实际 fps。";
      break;
    case StatusNoticeKind::kVfr:
      notice.tag = "VFR";
      notice.text = "可变帧率（VFR）：帧号按显示帧计数，时间码不承诺与帧号严格对应";
      notice.detail = "VFR 的帧号按实际显示帧计数；时间码按 PTS 换算，仅作显示。";
      break;
    case StatusNoticeKind::kError:
      notice.tag = "错误";
      notice.text = "打开失败：文件无法读取或格式不支持";
      notice.detail = "打开失败；旧画面保持不变，可换一个文件重试。";
      break;
  }
  return notice;
}

bool ParseStatusNoticeKind(const std::string& name, StatusNoticeKind* kind) {
  if (kind == nullptr) {
    return false;
  }
  if (name == "no-audio") {
    *kind = StatusNoticeKind::kNoAudio;
    return true;
  }
  if (name == "scanning") {
    *kind = StatusNoticeKind::kScanning;
    return true;
  }
  if (name == "out-of-range" || name == "out-of-guarantee") {
    *kind = StatusNoticeKind::kOutOfGuarantee;
    return true;
  }
  if (name == "10-bit" || name == "ten-bit") {
    *kind = StatusNoticeKind::kTenBit;
    return true;
  }
  if (name == "frame-drop" || name == "fps-drop") {
    *kind = StatusNoticeKind::kFrameRateDrop;
    return true;
  }
  if (name == "vfr") {
    *kind = StatusNoticeKind::kVfr;
    return true;
  }
  if (name == "error") {
    *kind = StatusNoticeKind::kError;
    return true;
  }
  return false;
}

std::vector<StatusNotice> BuildMediaStatusNotices(const MediaInfo& info, bool vfr_suspect,
                                                  bool total_scanning) {
  std::vector<StatusNotice> notices;

  if (!info.has_audio) {
    notices.push_back(MakeStatusNotice(StatusNoticeKind::kNoAudio));
  }

  if (total_scanning) {
    notices.push_back(MakeStatusNotice(StatusNoticeKind::kScanning));
  }

  std::vector<std::string> reasons;
  if (BeyondGuarantee(info, &reasons)) {
    StatusNotice notice = MakeStatusNotice(StatusNoticeKind::kOutOfGuarantee);
    notice.text = "超出保证范围（" + Join(reasons, " · ") +
                  "）：仍可播放，性能不保证";
    notice.detail = "保证档：H.264/H.265 8-bit SDR，≤1080p60；当前 " +
                    ResolutionText(info) +
                    (info.video.frame_rate.valid()
                         ? " " + FormatFps(info.video.frame_rate.ToDouble()) + "fps"
                         : std::string()) +
                    (info.video.codec.empty() ? std::string() : " " + info.video.codec) +
                    "；长 GOP/4K 等场景仍帧精确，但性能按可用档处理。";
    notices.push_back(std::move(notice));
  }

  if (info.video.bit_depth > 8) {
    StatusNotice notice = MakeStatusNotice(StatusNoticeKind::kTenBit);
    const std::string depth = std::to_string(info.video.bit_depth);
    notice.tag = depth + "-bit";
    notice.text = (info.video.bit_depth == 10
                       ? std::string("10-bit/HDR：可尝试播放，但不承诺颜色正确")
                       : depth + "-bit：可尝试播放，但不承诺颜色正确");
    notice.detail = "源位深 " + depth +
                    "-bit；首期不做 HDR/10-bit 色彩管理，颜色可能与源不一致。";
    notices.push_back(std::move(notice));
  }

  if (vfr_suspect) {
    notices.push_back(MakeStatusNotice(StatusNoticeKind::kVfr));
  }

  return notices;
}

std::size_t SelectPrimaryNotice(const std::vector<StatusNotice>& notices) {
  if (notices.empty()) {
    return kNoNotice;
  }
  std::size_t primary = kNoNotice;
  int best_priority = -1;
  for (std::size_t i = 0; i < notices.size(); ++i) {
    const int priority = StatusNoticePriority(notices[i].kind);
    if (priority > best_priority) {
      best_priority = priority;
      primary = i;
    }
  }
  return primary;
}

std::string SecondaryNoticeSummary(const std::vector<StatusNotice>& notices,
                                   std::size_t primary_index) {
  std::vector<const StatusNotice*> secondary;
  secondary.reserve(notices.size());
  for (std::size_t i = 0; i < notices.size(); ++i) {
    if (i != primary_index) {
      secondary.push_back(&notices[i]);
    }
  }
  if (secondary.empty()) {
    return {};
  }
  std::stable_sort(secondary.begin(), secondary.end(),
                   [](const StatusNotice* first, const StatusNotice* second) {
                     return StatusNoticePriority(first->kind) >
                            StatusNoticePriority(second->kind);
                   });

  std::string summary = "其他提示：";
  constexpr std::size_t kVisibleTags = 2;
  const std::size_t shown = std::min(kVisibleTags, secondary.size());
  for (std::size_t i = 0; i < shown; ++i) {
    if (i > 0) {
      summary += "、";
    }
    summary += secondary[i]->tag;
  }
  if (secondary.size() > shown) {
    summary += " 等 " + std::to_string(secondary.size()) + " 条";
  }
  return summary;
}

std::string SecondaryNoticeTooltip(const std::vector<StatusNotice>& notices,
                                   std::size_t primary_index) {
  std::vector<const StatusNotice*> secondary;
  secondary.reserve(notices.size());
  for (std::size_t i = 0; i < notices.size(); ++i) {
    if (i != primary_index) {
      secondary.push_back(&notices[i]);
    }
  }
  if (secondary.empty()) {
    return {};
  }
  std::stable_sort(secondary.begin(), secondary.end(),
                   [](const StatusNotice* first, const StatusNotice* second) {
                     return StatusNoticePriority(first->kind) >
                            StatusNoticePriority(second->kind);
                   });

  std::string tooltip =
      "其他状态提示（" + std::to_string(secondary.size()) + " 条）";
  for (const StatusNotice* notice : secondary) {
    tooltip += "\n- [" + notice->tag + "] " + notice->text;
  }
  return tooltip;
}

}  // namespace lapian::media
