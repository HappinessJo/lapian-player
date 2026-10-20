// T6.5 状态提示单测：六类提示的生成、优先级、唯一横幅选择与状态栏摘要。
// 纯 C++，不依赖 Qt/FFmpeg。

#include <cstdio>
#include <string>
#include <vector>

#include "media/StatusNotice.h"

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

lapian::media::MediaInfo MakeInfo(int width, int height, int fps_num, int fps_den,
                                  const std::string& codec, int bit_depth,
                                  bool has_audio) {
  lapian::media::MediaInfo info;
  info.path_utf8 = "test.mp4";
  info.duration_us = 10'000'000;
  info.video.width = width;
  info.video.height = height;
  info.video.codec = codec;
  info.video.bit_depth = bit_depth;
  info.video.frame_rate = lapian::media::Rational{fps_num, fps_den};
  info.video.nb_frames = 240;
  info.video.nb_frames_known = true;
  info.has_audio = has_audio;
  if (has_audio) {
    info.audio.stream_index = 1;
    info.audio.codec = "aac";
    info.audio.sample_rate = 48000;
    info.audio.channels = 2;
  }
  return info;
}

bool HasKind(const std::vector<lapian::media::StatusNotice>& notices,
             lapian::media::StatusNoticeKind kind) {
  for (const auto& notice : notices) {
    if (notice.kind == kind) {
      return true;
    }
  }
  return false;
}

bool Contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

using lapian::media::StatusNoticeKind;

}  // namespace

int main() {
  using lapian::media::BuildMediaStatusNotices;
  using lapian::media::kNoNotice;
  using lapian::media::MakeStatusNotice;
  using lapian::media::ParseStatusNoticeKind;
  using lapian::media::SecondaryNoticeSummary;
  using lapian::media::SecondaryNoticeTooltip;
  using lapian::media::SelectPrimaryNotice;
  using lapian::media::StatusNotice;
  using lapian::media::StatusNoticeKindName;
  using lapian::media::StatusNoticePriority;

  std::printf("T6.5 状态提示测试\n");

  const auto baseline = MakeInfo(1920, 1080, 24, 1, "h264", 8, true);
  auto notices = BuildMediaStatusNotices(baseline, false, false);
  Check(notices.empty(), "1080p H.264 8-bit CFR 有音频：无提示");
  Check(SelectPrimaryNotice(notices) == kNoNotice, "无提示时 primary=npos");
  Check(SecondaryNoticeSummary(notices, kNoNotice).empty(), "无提示时状态栏摘要为空");
  Check(SecondaryNoticeTooltip(notices, kNoNotice).empty(), "无提示时 tooltip 为空");

  const auto sixty = MakeInfo(1920, 1080, 60, 1, "h264", 8, true);
  Check(BuildMediaStatusNotices(sixty, false, false).empty(), "1080p60 正好在保证档内");
  const auto ntsc60 = MakeInfo(1920, 1080, 60000, 1001, "h264", 8, true);
  Check(BuildMediaStatusNotices(ntsc60, false, false).empty(), "59.94fps 在保证档内");
  const auto just_over_60 = MakeInfo(1920, 1080, 60001, 1000, "h264", 8, true);
  notices = BuildMediaStatusNotices(just_over_60, false, false);
  Check(notices.size() == 1 &&
            notices[0].kind == StatusNoticeKind::kOutOfGuarantee,
        "60.001fps 判为超出保证范围");
  const auto high_fps = MakeInfo(1920, 1080, 120, 1, "h264", 8, true);
  notices = BuildMediaStatusNotices(high_fps, false, false);
  Check(notices.size() == 1 && notices[0].kind == StatusNoticeKind::kOutOfGuarantee,
        "120fps 判为超出保证范围");
  Check(Contains(notices[0].text, "120fps"), "超出保证范围文案带帧率");
  Check(Contains(notices[0].text, "性能不保证"), "超出保证范围文案明确性能不保证");

  const auto uhd = MakeInfo(3840, 2160, 30, 1, "h264", 8, true);
  notices = BuildMediaStatusNotices(uhd, false, false);
  Check(notices.size() == 1 && notices[0].kind == StatusNoticeKind::kOutOfGuarantee,
        "4K 判为超出保证范围");
  Check(Contains(notices[0].text, "3840×2160"), "4K 文案带分辨率");
  Check(Contains(notices[0].detail, "保证档"), "4K 详情引用保证档口径");

  const auto dci = MakeInfo(2048, 1080, 24, 1, "h264", 8, true);
  Check(!BuildMediaStatusNotices(dci, false, false).empty(),
        "2048×1080 像素数超过 1080p，判超出");

  const auto portrait = MakeInfo(1080, 1920, 60, 1, "h264", 8, true);
  Check(BuildMediaStatusNotices(portrait, false, false).empty(),
        "1080×1920 竖屏像素数等于 1080p，不误报");

  const auto vp9 = MakeInfo(1920, 1080, 30, 1, "vp9", 8, true);
  notices = BuildMediaStatusNotices(vp9, false, false);
  Check(notices.size() == 1 && notices[0].kind == StatusNoticeKind::kOutOfGuarantee,
        "VP9 判为超出保证范围");
  Check(Contains(notices[0].text, "vp9"), "VP9 文案带编码名");

  const auto ten_bit = MakeInfo(1920, 1080, 24, 1, "hevc", 10, true);
  notices = BuildMediaStatusNotices(ten_bit, false, false);
  Check(notices.size() == 1 && notices[0].kind == StatusNoticeKind::kTenBit,
        "10-bit 只产生一条 10-bit 提示");
  Check(notices[0].tag == "10-bit", "10-bit 状态栏标签");
  Check(Contains(notices[0].text, "颜色"), "10-bit 文案说明颜色不保证");
  Check(HasKind(notices, StatusNoticeKind::kTenBit), "10-bit kind 正确");

  const auto twelve_bit = MakeInfo(1920, 1080, 24, 1, "hevc", 12, true);
  notices = BuildMediaStatusNotices(twelve_bit, false, false);
  Check(notices.size() == 1 && notices[0].tag == "12-bit", "12-bit 标签按实际位深");

  const auto uhd_ten_bit = MakeInfo(3840, 2160, 30, 1, "hevc", 10, true);
  notices = BuildMediaStatusNotices(uhd_ten_bit, false, false);
  const std::size_t uhd_ten_primary = SelectPrimaryNotice(notices);
  Check(uhd_ten_primary != kNoNotice &&
            notices[uhd_ten_primary].kind == StatusNoticeKind::kTenBit,
        "4K 10-bit：10-bit 优先于超出保证范围");
  Check(Contains(SecondaryNoticeSummary(notices, uhd_ten_primary), "超出保证"),
        "4K 10-bit：性能提示进入状态栏");

  const auto no_audio = MakeInfo(1920, 1080, 24, 1, "h264", 8, false);
  notices = BuildMediaStatusNotices(no_audio, false, false);
  Check(notices.size() == 1 && notices[0].kind == StatusNoticeKind::kNoAudio,
        "无音轨产生无音频提示");
  Check(Contains(notices[0].text, "波形不可用"), "无音频文案说明波形不可用");

  notices = BuildMediaStatusNotices(no_audio, false, true);
  Check(notices.size() == 2, "无音频 + 扫描中共两条");
  Check(notices[SelectPrimaryNotice(notices)].kind == StatusNoticeKind::kScanning,
        "扫描中优先于无音频");
  Check(Contains(SecondaryNoticeSummary(notices, SelectPrimaryNotice(notices)), "无音频"),
        "无音频进入状态栏");

  const auto vfr = MakeInfo(1080, 1920, 30, 1, "h264", 8, false);
  notices = BuildMediaStatusNotices(vfr, true, true);
  const std::size_t vfr_primary = SelectPrimaryNotice(notices);
  Check(vfr_primary != kNoNotice && notices[vfr_primary].kind == StatusNoticeKind::kVfr,
        "VFR + 扫描中 + 无音频：VFR 优先");
  const std::string vfr_secondary = SecondaryNoticeSummary(notices, vfr_primary);
  Check(Contains(vfr_secondary, "扫描中"), "VFR 文件：扫描中进入状态栏");
  Check(Contains(vfr_secondary, "无音频"), "VFR 文件：无音频进入状态栏");
  Check(Contains(SecondaryNoticeTooltip(notices, vfr_primary), "总帧数扫描中"),
        "VFR 状态栏 tooltip 含扫描中文案");

  const auto vfr_4k = MakeInfo(3840, 2160, 30, 1, "hevc", 8, true);
  notices = BuildMediaStatusNotices(vfr_4k, true, false);
  Check(notices[SelectPrimaryNotice(notices)].kind == StatusNoticeKind::kVfr,
        "4K VFR：VFR 优先于超出保证范围");

  const auto all_info = MakeInfo(3840, 2160, 30, 1, "hevc", 10, false);
  notices = BuildMediaStatusNotices(all_info, true, true);
  notices.push_back(MakeStatusNotice(StatusNoticeKind::kError));
  const std::size_t all_primary = SelectPrimaryNotice(notices);
  Check(all_primary != kNoNotice &&
            notices[all_primary].kind == StatusNoticeKind::kError,
        "六类提示同开：错误最优先");
  const std::string all_summary = SecondaryNoticeSummary(notices, all_primary);
  Check(Contains(all_summary, "VFR"), "错误横幅时 VFR 进状态栏");
  Check(Contains(all_summary, "10-bit"), "错误横幅时 10-bit 进状态栏");
  Check(Contains(all_summary, "等 5 条"), "错误横幅时其余 5 条汇总计数");
  const std::string all_tooltip = SecondaryNoticeTooltip(notices, all_primary);
  Check(Contains(all_tooltip, "超出保证范围"), "tooltip 含超出保证范围正文");
  Check(Contains(all_tooltip, "无音频"), "tooltip 含无音频正文");

  StatusNotice error_notice = MakeStatusNotice(StatusNoticeKind::kError);
  Check(!error_notice.text.empty() && !error_notice.detail.empty(),
        "错误默认文案非空");
  error_notice.text = "打开失败：测试";
  std::vector<StatusNotice> only_error{error_notice};
  Check(SelectPrimaryNotice(only_error) == 0, "只有错误时 primary=0");
  Check(SecondaryNoticeSummary(only_error, 0).empty(), "只有错误时无 secondary");

  const StatusNoticeKind kinds[] = {
      StatusNoticeKind::kNoAudio,       StatusNoticeKind::kScanning,
      StatusNoticeKind::kOutOfGuarantee, StatusNoticeKind::kTenBit,
      StatusNoticeKind::kVfr,           StatusNoticeKind::kError};
  for (StatusNoticeKind kind : kinds) {
    const std::string name = StatusNoticeKindName(kind);
    StatusNoticeKind parsed = StatusNoticeKind::kNoAudio;
    Check(ParseStatusNoticeKind(name, &parsed) && parsed == kind,
          "kind 名称 roundtrip: " + name);
    Check(StatusNoticePriority(kind) >= 0, "优先级非负: " + name);
    const StatusNotice made = MakeStatusNotice(kind);
    Check(!made.tag.empty() && !made.text.empty(), "默认提示非空: " + name);
  }
  Check(StatusNoticePriority(StatusNoticeKind::kError) >
            StatusNoticePriority(StatusNoticeKind::kVfr),
        "错误 > VFR");
  Check(StatusNoticePriority(StatusNoticeKind::kVfr) >
            StatusNoticePriority(StatusNoticeKind::kTenBit),
        "VFR > 10-bit");
  Check(StatusNoticePriority(StatusNoticeKind::kTenBit) >
            StatusNoticePriority(StatusNoticeKind::kOutOfGuarantee),
        "10-bit > 超出保证");
  Check(StatusNoticePriority(StatusNoticeKind::kOutOfGuarantee) >
            StatusNoticePriority(StatusNoticeKind::kScanning),
        "超出保证 > 扫描中");
  Check(StatusNoticePriority(StatusNoticeKind::kScanning) >
            StatusNoticePriority(StatusNoticeKind::kNoAudio),
        "扫描中 > 无音频");

  StatusNoticeKind parsed = StatusNoticeKind::kNoAudio;
  Check(!ParseStatusNoticeKind("bogus", &parsed), "未知提示名被拒绝");
  Check(ParseStatusNoticeKind("out-of-guarantee", &parsed) &&
            parsed == StatusNoticeKind::kOutOfGuarantee,
        "兼容 out-of-guarantee 别名");

  std::vector<StatusNotice> two = {MakeStatusNotice(StatusNoticeKind::kNoAudio),
                                   MakeStatusNotice(StatusNoticeKind::kVfr)};
  Check(Contains(SecondaryNoticeSummary(two, 1), "其他提示：无音频"),
        "两条提示：secondary 摘要格式");

  std::printf("T6.5 状态提示结果：%s（%d 项失败）\n",
              g_failures == 0 ? "PASS" : "FAIL", g_failures);
  return g_failures == 0 ? 0 : 1;
}
