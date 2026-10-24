#include <QString>

#include <cmath>
#include <cstdio>

#include "online/FrameRatePolicy.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(const char* name, bool ok, const QString& detail = {}) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("FAIL %s%s%s\n", name,
                detail.isEmpty() ? "" : " detail=",
                detail.toUtf8().constData());
  } else {
    std::printf("ok %s\n", name);
  }
}

using lapian::online::AssessOnlineFrameRate;
using lapian::online::InferSourceFpsHintFromText;
using lapian::online::OnlineFrameRateAssessment;
using lapian::online::OnlineFrameRateEvidence;

OnlineFrameRateAssessment Assess(double stream_fps, double container_fps, int qn,
                                 double source_hint) {
  OnlineFrameRateEvidence evidence;
  evidence.stream_fps = stream_fps;
  evidence.container_fps = container_fps;
  evidence.selected_qn = qn;
  evidence.source_fps_hint = source_hint;
  return AssessOnlineFrameRate(evidence);
}

void TestSourceHint() {
  Check("hint_ob2_title",
        std::fabs(InferSourceFpsHintFromText(
                      QStringLiteral("【静止画MAD】[UHD 60FPS][CC字幕]")) -
                  60.0) < 0.01);
  Check("hint_60_frame", std::fabs(InferSourceFpsHintFromText(
                                       QStringLiteral("60帧 动画")) -
                                   60.0) < 0.01);
  Check("hint_30fps_is_not_60",
        std::fabs(InferSourceFpsHintFromText(QStringLiteral("30fps 样本")) -
                  30.0) < 0.01);
  Check("hint_no_fps", InferSourceFpsHintFromText(
                           QStringLiteral("在超市后门吸烟的二人")) == 0.0);
  Check("hint_not_160fps",
        InferSourceFpsHintFromText(QStringLiteral("160fps 素材")) != 60.0);
}

void TestOrdinaryRates() {
  const auto ob1 = Assess(30.0, 30.0, 80, 0.0);
  Check("ob1_30_no_drop", !ob1.frame_drop);
  Check("ob1_badge", ob1.badge_text == QStringLiteral("30fps"),
        ob1.badge_text);
  const auto ob3 = Assess(25.0, 25.0, 80, 0.0);
  Check("ob3_25_no_drop", !ob3.frame_drop);
  Check("ob3_no_banner", ob3.banner_text.isEmpty());
  const auto ordinary_294 = Assess(29.412, 30.0, 80, 0.0);
  Check("294_without_source_no_drop", !ordinary_294.frame_drop);
  Check("294_actual_visible",
        ordinary_294.badge_text.contains(QStringLiteral("29.412")),
        ordinary_294.badge_text);
}

void TestDropEvidence() {
  const auto ob2 = Assess(29.412, 30.0, 80, 60.0);
  Check("ob2_drop", ob2.frame_drop);
  Check("ob2_half", ob2.approximate_half);
  Check("ob2_banner_frame_mismatch",
        ob2.banner_text.contains(QStringLiteral("帧号与源片不同步")) &&
            ob2.banner_text.contains(QStringLiteral("约 1/2")),
        ob2.banner_text);
  Check("ob2_badge_drop",
        ob2.badge_text.contains(QStringLiteral("29.412")) &&
            ob2.badge_text.contains(QStringLiteral("降帧")),
        ob2.badge_text);
  Check("ob2_no_fake_2to1",
        !ob2.banner_text.contains(QStringLiteral("2:1")) &&
            !ob2.banner_text.contains(QStringLiteral("2：1")),
        ob2.banner_text);

  const auto selected60_unknown_source = Assess(30.0, 30.0, 116, 0.0);
  Check("selected60_drop", selected60_unknown_source.frame_drop);
  Check("selected60_only_possible",
        selected60_unknown_source.banner_text.contains(
            QStringLiteral("可能降帧")) &&
            !selected60_unknown_source.banner_text.contains(
                QStringLiteral("约 1/2")),
        selected60_unknown_source.banner_text);

  const auto true60 = Assess(60.0, 60.0, 116, 60.0);
  Check("true_60_no_drop", !true60.frame_drop);
  Check("true_60_no_banner", true60.banner_text.isEmpty());
  Check("true_60_badge", true60.badge_text == QStringLiteral("60fps"),
        true60.badge_text);

  const auto source60_actual60 = Assess(60.0, 60.0, 80, 60.0);
  Check("source60_actual60_no_drop", !source60_actual60.frame_drop);
  const auto source60_actual30 = Assess(30.0, 30.0, 80, 60.0);
  Check("source60_actual30_drop", source60_actual30.frame_drop);
  Check("source60_actual30_half", source60_actual30.approximate_half);
}

void TestFallbacks() {
  const auto durl = Assess(0.0, 25.0, 64, 0.0);
  Check("container_fallback", durl.actual_known &&
                                  std::fabs(durl.actual_fps - 25.0) < 0.01);
  const auto unknown = Assess(0.0, 0.0, 116, 0.0);
  Check("unknown_no_fake_badge", unknown.badge_text.isEmpty());
  Check("unknown_no_drop", !unknown.frame_drop);
}

}  // namespace

int main() {
  std::printf("T8.12 online frame-rate policy test\n");
  TestSourceHint();
  TestOrdinaryRates();
  TestDropEvidence();
  TestFallbacks();
  std::printf("checks=%d failures=%d verdict=%s\n", g_checks, g_failures,
              g_failures == 0 ? "pass" : "fail");
  return g_failures == 0 ? 0 : 1;
}
