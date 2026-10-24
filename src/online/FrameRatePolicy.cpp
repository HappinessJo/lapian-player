#include "online/FrameRatePolicy.h"

#include <QRegularExpression>
#include <QStringList>

#include <cmath>

namespace lapian::online {
namespace {

constexpr double k60Threshold = 59.5;

QString FormatFpsNumber(double fps) {
  if (!std::isfinite(fps) || fps <= 0.0) {
    return QString();
  }
  QString text = QString::number(fps, 'f', 3);
  while (text.endsWith(QLatin1Char('0'))) {
    text.chop(1);
  }
  if (text.endsWith(QLatin1Char('.'))) {
    text.chop(1);
  }
  return text;
}

}  // namespace

double InferSourceFpsHintFromText(const QString& text) {
  if (text.trimmed().isEmpty()) {
    return 0.0;
  }
  // 只匹配 23–240 的整数帧率；要求 fps/帧作为单位，避免把 “60 秒” 当帧率。
  static const QRegularExpression pattern(
      QStringLiteral("(?<![0-9])([2-9][0-9]|[12][0-9]{2})\\s*(?:fps|FPS|Fps|帧)"));
  QRegularExpressionMatchIterator it = pattern.globalMatch(text);
  while (it.hasNext()) {
    const QRegularExpressionMatch match = it.next();
    bool ok = false;
    const int fps = match.captured(1).toInt(&ok);
    if (ok && fps >= 23 && fps <= 240) {
      return static_cast<double>(fps);
    }
  }
  return 0.0;
}

QString FormatOnlineFps(double fps) { return FormatFpsNumber(fps); }

OnlineFrameRateAssessment AssessOnlineFrameRate(
    const OnlineFrameRateEvidence& evidence) {
  OnlineFrameRateAssessment result;
  const double actual = std::isfinite(evidence.stream_fps) &&
                                evidence.stream_fps > 0.0
                            ? evidence.stream_fps
                            : evidence.container_fps;
  if (std::isfinite(actual) && actual > 0.0) {
    result.actual_fps = actual;
    result.actual_known = true;
  }

  const double source_hint =
      std::isfinite(evidence.source_fps_hint) && evidence.source_fps_hint > 0.0
          ? evidence.source_fps_hint
          : 0.0;
  result.source_declares_60 =
      source_hint > 0.0 && std::fabs(source_hint - 60.0) < 1.0;
  result.selected_60_tier =
      evidence.selected_qn == 116 || evidence.selected_qn == 74;

  if (result.actual_known) {
    result.badge_text = QStringLiteral("%1fps").arg(
        FormatFpsNumber(result.actual_fps));
  }
  if (!result.actual_known) {
    return result;
  }

  result.frame_drop =
      (result.source_declares_60 || result.selected_60_tier) &&
      result.actual_fps < k60Threshold;
  if (!result.frame_drop) {
    result.status_text = QStringLiteral("实际 %1fps")
                             .arg(FormatFpsNumber(result.actual_fps));
    return result;
  }

  result.badge_text += QStringLiteral(" · 降帧");
  const QString actual_text = FormatFpsNumber(result.actual_fps);
  if (result.source_declares_60) {
    const double ratio = result.actual_fps > 0.0
                             ? 60.0 / result.actual_fps
                             : 0.0;
    result.approximate_half =
        std::isfinite(ratio) && ratio >= 1.7 && ratio <= 2.3;
    result.banner_text =
        QStringLiteral(
            "实际 %1fps（源片标题声明 60fps）：帧号与源片不同步，约 1/2；"
            "标记/截图记录实际 fps。")
            .arg(actual_text);
    result.status_text =
        QStringLiteral("实际 %1fps；帧号与源片不同步/约 1/2")
            .arg(actual_text);
  } else {
    // 只看到“选了 60fps 档但实际不够 60”这一条证据，不冒充已知源帧率。
    result.banner_text =
        QStringLiteral(
            "当前 60fps 档实际 %1fps：帧号与源片可能不同步（可能降帧）；"
            "标记/截图记录实际 fps。")
            .arg(actual_text);
    result.status_text =
        QStringLiteral("当前 60fps 档实际 %1fps；可能降帧")
            .arg(actual_text);
  }
  return result;
}

}  // namespace lapian::online
