#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "media/MediaInfo.h"

namespace lapian::media {

// T6.5 状态提示：同一时刻 InfoBar 只显示最高优先级的一条，其余进状态栏。
// 纯 C++，不依赖 Qt/FFmpeg，便于单测与 UI 复用。
//
// 优先级（高 → 低）：
//   错误 > VFR > 10-bit > 超出保证范围 > 总帧扫描中 > 无音频
// 理由：
// - 错误是唯一“当前文件没打开/没换成”的状态，必须最醒目；
// - VFR 改变帧号语义，spec F2-A7 要求打开时提示，且不能排在“超出保证范围”后面
//   （手机 4K VFR 很常见，否则 VFR 提示会被性能提示挤掉）；
// - 10-bit 影响颜色正确性，比性能提示更具体；
// - 扫描中是过渡态，无音频只影响波形，都不该压过前面几类。
enum class StatusNoticeKind {
  kNoAudio = 0,
  kScanning = 1,
  kOutOfGuarantee = 2,
  kTenBit = 3,
  kFrameRateDrop = 4,
  kVfr = 5,
  kError = 6,
};

struct StatusNotice {
  StatusNoticeKind kind = StatusNoticeKind::kNoAudio;
  std::string tag;     // 状态栏短标签，如 "VFR"、"10-bit"
  std::string text;    // InfoBar 横幅完整文案
  std::string detail;  // 状态栏 tooltip / 诊断用详细说明
};

const char* StatusNoticeKindName(StatusNoticeKind kind);
int StatusNoticePriority(StatusNoticeKind kind);

// 测试/合成提示用：返回不带媒体细节的默认文案。
StatusNotice MakeStatusNotice(StatusNoticeKind kind);
bool ParseStatusNoticeKind(const std::string& name, StatusNoticeKind* kind);

// 由探测结果生成除错误外的提示。顺序不保证；primary 由 SelectPrimaryNotice 选。
// vfr_suspect 与 total_scanning 由 UI 状态传入，避免本模块依赖 FrameIndex。
std::vector<StatusNotice> BuildMediaStatusNotices(const MediaInfo& info, bool vfr_suspect,
                                                  bool total_scanning);

// 返回最高优先级提示的下标；空列表返回 npos。
inline constexpr std::size_t kNoNotice = std::numeric_limits<std::size_t>::max();
std::size_t SelectPrimaryNotice(const std::vector<StatusNotice>& notices);

// 状态栏“其余提示”短摘要；没有 secondary 时返回空串。
// 例：secondary=2 → "其他提示：VFR、无音频"
//     secondary=5 → "其他提示：VFR、10-bit 等 5 条"
std::string SecondaryNoticeSummary(const std::vector<StatusNotice>& notices,
                                   std::size_t primary_index);

// 状态栏 tooltip：只列 secondary，每行 "- [tag] text"；没有 secondary 返回空串。
std::string SecondaryNoticeTooltip(const std::vector<StatusNotice>& notices,
                                   std::size_t primary_index);

}  // namespace lapian::media
