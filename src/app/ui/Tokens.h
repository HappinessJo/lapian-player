#pragma once

// UI v2 设计令牌（规划 §4.2/§4.3）。纯常量 + 纯函数，**不依赖 Qt**，
// 这样 Theme 与单测都能复用，且不会把壳层的依赖引到 core/media。
//
// 口径提醒（规划 §4.7）：令牌以**逻辑像素**定义；Qt 6 用
// QFont::setPixelSize(token)，高 DPI 下由 devicePixelRatio 缩放，逻辑尺寸不变。
namespace lapian::ui {

// 三档密度（规划 §3.1）：按**客户区高度**取档，带 20px 迟滞。
enum class Density {
  kRegular = 0,
  kCompact = 1,
  kDense = 2,
};

inline constexpr int kDensityHysteresis = 20;

// 纯阈值（无历史时使用；也是迟滞的外边界）。
inline constexpr int kRegularMinHeight = 640;
inline constexpr int kCompactMinHeight = 520;

inline const char* DensityName(Density density) {
  switch (density) {
    case Density::kRegular:
      return "regular";
    case Density::kCompact:
      return "compact";
    case Density::kDense:
      return "dense";
  }
  return "unknown";
}

inline int DensityIndex(Density density) {
  switch (density) {
    case Density::kRegular:
      return 0;
    case Density::kCompact:
      return 1;
    case Density::kDense:
      return 2;
  }
  return -1;
}

// 首次取档（无历史）：直接用阈值。客户区 720 → regular，624 → compact，
// 452/500 → dense（与规划 §3.2 的实测几何一致）。
inline Density DensityForHeight(int client_height) {
  if (client_height >= kRegularMinHeight) {
    return Density::kRegular;
  }
  if (client_height >= kCompactMinHeight) {
    return Density::kCompact;
  }
  return Density::kDense;
}

// 带 20px 迟滞的档位切换（规划 §3.1）：
//   regular  H ≥ 640 进入；回落 compact 需 H < 620；再往下 < 500 才进 dense
//   compact  H 520–639 进入；升 regular 需 H > 660；降 dense 需 H < 500
//   dense    升 compact 需 H > 540；H > 660 直接回 regular
inline Density ResolveDensity(int client_height, bool has_current,
                              Density current) {
  if (!has_current) {
    return DensityForHeight(client_height);
  }
  const int h = client_height;
  switch (current) {
    case Density::kRegular:
      if (h >= kRegularMinHeight - kDensityHysteresis) {
        return Density::kRegular;
      }
      // 一步落到 dense 也按档位边界判：H < 520 就是 dense 档，不先经过 compact
      // （否则 820×500 这类"从大窗口缩到 500 高"的场景会停在 compact）。
      return h < kCompactMinHeight ? Density::kDense : Density::kCompact;
    case Density::kCompact:
      if (h > kRegularMinHeight + kDensityHysteresis) {
        return Density::kRegular;
      }
      if (h >= kCompactMinHeight - kDensityHysteresis) {
        return Density::kCompact;
      }
      return Density::kDense;
    case Density::kDense:
      if (h > kRegularMinHeight + kDensityHysteresis) {
        return Density::kRegular;
      }
      if (h > kCompactMinHeight + kDensityHysteresis) {
        return Density::kCompact;
      }
      return Density::kDense;
  }
  return Density::kRegular;
}

// 每档的栏位高度与字号（规划 §4.2/§4.3/§4.5）。
struct Tokens {
  Density density = Density::kRegular;
  const char* name = "regular";

  // 固定栏（逻辑 px）
  int appbar = 38;     // 顶栏
  int banner = 26;     // 横幅槽（出现时才占位）
  int transport = 46;  // Transport
  int timeline = 132;  // 时间轴面板默认高
  int timeline_head = 26;
  int status = 24;  // 状态栏
  int margin = 6;   // 工作区外边距（四边）
  int handle = 6;   // QSplitter handle 宽

  // 控件
  int btn = 26;     // 普通图标/文字按钮
  int chip = 20;    // 顶栏 chip
  int play = 34;    // 播放按钮高
  int readout = 26; // 时间码主读数高

  // 面板
  int inspector = 304;
  int inspector_min = 220;
  int inspector_max = 560;
  int inspector_collapsed = 44;
  int inspector_head = 26;
  int timeline_min = 96;
  int timeline_max = 340;
  int waveform_cap = 96;  // 3B：时间轴拉高时波形封顶

  // 字号（逻辑 px）
  double fs_tick = 9.5;
  double fs_micro = 10.0;
  double fs_meta = 11.0;
  double fs_body = 12.0;
  double fs_title = 13.0;
  double fs_card = 15.0;
  double fs_readout = 26.0;
  double fs_readout_sm = 22.0;

  // 固定栏合计（画面区 = 客户区高 − 固定合计）。
  int FixedTotal() const {
    return appbar + 2 * margin + handle + transport + timeline + status;
  }
};

inline Tokens MakeTokens(Density density) {
  Tokens t;
  t.density = density;
  t.name = DensityName(density);
  switch (density) {
    case Density::kRegular:
      break;
    case Density::kCompact:
      t.appbar = 34;
      t.banner = 24;
      t.transport = 40;
      t.timeline = 112;
      t.timeline_head = 24;
      t.status = 22;
      t.margin = 4;
      t.btn = 24;
      t.chip = 18;
      t.play = 30;
      t.readout = 22;
      t.inspector_head = 24;
      t.fs_readout = 22.0;
      t.fs_readout_sm = 20.0;
      break;
    case Density::kDense:
      t.appbar = 30;
      t.banner = 22;
      t.transport = 36;
      t.timeline = 96;
      t.timeline_head = 0;  // dense 档时间轴无头（3B 把数值移到 Transport 副行）
      t.status = 20;
      t.margin = 4;
      t.btn = 22;
      t.chip = 18;
      t.play = 28;
      t.readout = 22;
      t.inspector_head = 22;
      t.fs_readout = 22.0;
      t.fs_readout_sm = 20.0;
      break;
  }
  return t;
}

inline Tokens TokensFor(Density density) { return MakeTokens(density); }

// ---- 色值（规划 §4.2 语义色；无 Qt 依赖，Theme 转 QColor）----
// accent/text/text-dim/text-meta/text-mute 的对比度按 WCAG 手算，见规划 §4.2；
// text-mute 只用于禁用/装饰，不得承载信息（M2）。
inline constexpr const char* kColorSurface = "#17171a";
inline constexpr const char* kColorSurface2 = "#1c1c20";
inline constexpr const char* kColorSurface3 = "#23232a";
inline constexpr const char* kColorBorder = "#2b2b33";
inline constexpr const char* kColorBorderStrong = "#3a3a44";
inline constexpr const char* kColorText = "#e8e8ee";
inline constexpr const char* kColorTextDim = "#a2a2ad";
inline constexpr const char* kColorTextMeta = "#8a8a95";
inline constexpr const char* kColorTextMute = "#6f6f7a";
inline constexpr const char* kColorAccent = "#5b8def";
inline constexpr const char* kColorAccentBg = "#1b2333";
inline constexpr const char* kColorAccentBorder = "#2c3f66";
inline constexpr const char* kColorOk = "#9ed6a1";
inline constexpr const char* kColorOkBg = "#17201a";
inline constexpr const char* kColorOkBorder = "#2f4a32";
inline constexpr const char* kColorWarn = "#f0cb7a";
inline constexpr const char* kColorWarnBg = "#241f10";
inline constexpr const char* kColorWarnBorder = "#5a4a1a";
inline constexpr const char* kColorErr = "#f08c93";
inline constexpr const char* kColorErrBg = "#241417";
inline constexpr const char* kColorErrBorder = "#5a2a2e";
inline constexpr const char* kColorCache = "#a6d8c4";
inline constexpr const char* kColorCacheBg = "#14211d";
inline constexpr const char* kColorCacheBorder = "#2c4a40";
inline constexpr const char* kColorWave = "#6f9df0";

// M4 画面占比门槛（规划 §4.4 批准值 62/62/55/59）：
// regular/compact ≥62%，dense 客户区 ≥500 时 ≥59%（窄窗口 820×500），
// dense 且 H < 500 时 ≥55%（200% DPI 的 928×452）。
inline constexpr double kStageRatioRegular = 62.0;
inline constexpr double kStageRatioCompact = 62.0;
inline constexpr double kStageRatioDense = 55.0;
inline constexpr double kStageRatioDenseTall = 59.0;
inline constexpr int kDenseTallMinHeight = 500;

inline double StageRatioFloor(Density density, int client_height) {
  switch (density) {
    case Density::kRegular:
      return kStageRatioRegular;
    case Density::kCompact:
      return kStageRatioCompact;
    case Density::kDense:
      return client_height >= kDenseTallMinHeight ? kStageRatioDenseTall
                                                  : kStageRatioDense;
  }
  return kStageRatioRegular;
}

}  // namespace lapian::ui
