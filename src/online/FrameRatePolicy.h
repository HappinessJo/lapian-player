#pragma once

// T8.12 在线实际帧率/降帧提示策略（纯函数，便于离线单测）。
//
// 口径：
// - 当前流实际 fps 优先用 playurl 的 dash.video[].frameRate（如 OB-2 的
//   29.412）；durl/缓存命中没有这个字段时才回退容器 fps。
// - 帧号/索引仍用容器帧序号（T8.11），不拿 API fps 重算总帧数。
// - 只有“有证据”才提示降帧：
//     1) 当前选的是 60fps 档（qn=116/74），实际 <59.5；
//     2) 标题明确声明 60fps（如 OB-2 的 “[UHD 60FPS]”），实际 <59.5。
//   普通 30.000/25.000 源不提示；没有源 fps 证据时只写“可能降帧”，不写死 2:1。
// - 真 1080P60（实际 >=59.5）不提示。

#include <QString>

namespace lapian::online {

struct OnlineFrameRateEvidence {
  double stream_fps = 0.0;     // playurl/dash 实际流 fps；0=未知
  double container_fps = 0.0;  // 容器 avg_frame_rate；仅回退/并列显示
  int selected_qn = 0;         // 当前实际/请求的 qn
  double source_fps_hint = 0.0;  // 标题等文本证据；0=没有
};

struct OnlineFrameRateAssessment {
  double actual_fps = 0.0;
  bool actual_known = false;
  bool source_declares_60 = false;
  bool selected_60_tier = false;
  bool frame_drop = false;       // 有证据的降帧
  bool approximate_half = false; // 仅 source 60 + 实际约一半
  QString badge_text;            // 帧号旁常驻角标（空=无实际 fps）
  QString banner_text;           // DEC-4 起播横幅（空=不提示）
  QString status_text;           // 状态栏/证据单行
};

// 从标题/分P标题中读取“60fps / 60 FPS / 60帧”这类声明。
// 只作为**文本证据**，不冒充 API 的源帧率；没有声明返回 0。
double InferSourceFpsHintFromText(const QString& text);

OnlineFrameRateAssessment AssessOnlineFrameRate(
    const OnlineFrameRateEvidence& evidence);

// 29.412 → "29.412"；30.000 → "30"；无效返回空。
QString FormatOnlineFps(double fps);

}  // namespace lapian::online
