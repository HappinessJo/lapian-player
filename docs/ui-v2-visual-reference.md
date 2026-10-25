# UI v2 视觉基准（原型 = 唯一参考稿）

- **状态**：生效（2026-10-26，用户拍板）。**所有后续阶段（3B/3C/3D 与任何 UI 改动）在动手前必须先读本文件，
  并把原型渲成截图逐区对照。**
- **视觉基准**：[ui-v2-proto/plan.html](ui-v2-proto/plan.html)（评审版，默认即产品形态）。
  `index.html` 是同源的讨论版（有区域标注、8 个状态、密度/强调色开关），查细节时看它。
  两者都**不是产品代码**，只是参考稿；产品实现是 C++20 + Qt Widgets（ADR-004）。
- **为什么有这份文件**：3A 第一版只按规划正文的数字（栏位高度/令牌）搭了骨架，数字全对但**观感朴素**——
  没有图标、没有按钮分组、没有面板卡片、没有面板头。用户 2026-10-26 明确要求"按原型的来"，
  于是补了一轮 `3A.5 视觉对齐`。本文件把这个口径固定下来，避免 3B/3C/3D 再各写一套自己的审美。

---

## 1. 铁律（每个阶段都要遵守）

1. **原型有的一律照原型**：布局、分组、间距、圆角、层级、图标形态、文字层级。原型没有的才自由发挥。
2. **规划正文与原型冲突时**：以原型为准（本轮唯一的例外见 §4 的语义色说明——两者其实一致）。
3. **动手前先对屏**：把原型渲成 1280 宽的截图（§2），与自己实现的 `--banner-test` 截图并排看，
   至少核对：顶栏 / 画面卡片 / Transport / 时间轴面板头 / 检查器 / 状态栏 六个区域。
4. **不允许**"数字对了就算过"：M4 只保证画面占比，不保证观感。
5. **本轮已定的两条硬约束不变**：
   - 全局只有**一份** QSS（`Theme::GlobalStyleSheet()`）+ 令牌（`Tokens.h`）；禁止 per-widget `setStyleSheet`
     （例外只有三处动态色：fps 角标、危险按钮、banner kind）。
   - 所有自绘容器 `setAttribute(Qt::WA_StyledBackground, true)`（`Theme::SetStyledBackground()`）。
6. 新增控件先想清楚"原型里它长什么样"；原型里没有的（例如在线缓冲槽、"?"帮助），
   形态向最近的原型元素靠（例如 chip / icon button）。

---

## 2. 如何把原型渲成截图（可复现）

```powershell
cd 'E:\拉片软件开发'
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$uri  = ([System.Uri]((Resolve-Path 'docs\ui-v2-proto\plan.html').Path)).AbsoluteUri
& $edge --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=1 `
        --window-size=1280,900 --virtual-time-budget=6000 `
        --screenshot="E:\拉片软件开发\build\ui-v2-shots\proto-paused.png" "$uri"
```

- 原型顶部有一条 **评审条（`#planbar`，≈44px）** 和一条 **模拟系统标题栏（30px）**，都**不属于产品界面**，
  对比时按"客户区 720"折算：`window-size` 高度 ≈ 720 + 44 + 30 + 26。
- 想换状态：`plan.html` 的评审条点一下切（本地主界面/空态/在线缓冲/在线错误/在线加载/全屏），
  或直接用 `index.html?mode=error&density=dense&...`（参数表见 [NOTES.md](ui-v2-proto/NOTES.md)）。
- 产品侧截图：`--banner-test` 会存 `build/t6.5-banner-<tag>-<phase>.png`（含顶栏/Transport/时间轴/检查器/状态栏）；
  `--dpi-test` 存 `build/t6.4-dpi-<tag>-<phase>.png`（注：画面区是独立 HWND，`grab()` 只能拿到黑底）。

---

## 3. 组件对照表（原型 → Qt）

| 区域 | 原型（plan.html） | 产品（Qt） | 状态 |
|---|---|---|---|
| 应用底色 | `--bg #0e0e10` | `Tokens::kColorAppBg` + `PlayerShell/Workspace` QSS | ✅ 3A.5 |
| 面板卡片 | `.panel { bg #17171a; border 1px #2b2b33; radius 4 }` | `#v2VideoCard` / `#v2TimelinePanel` / `#v2Inspector` | ✅ 3A.5 |
| 顶栏 | `.appbar h=38 bg #17171a 底边线`，文件图标+名、规格 chip、模式 chip、右 4 按钮 | `TopBar`（`v2TopBar`/`v2FileLabel`/`v2SpecChip`/`v2ModeChip`/`v2OpenButton`/`v2OnlineButton`/`v2HelpButton`/`settingsSlot`/`cacheSettingsButton`） | ✅ 3A.5 |
| 键位角标 | `.kbd {9px Consolas, border #34343d, bg #1e1e24}` | `#v2KbdBadge`（在"打开/在线"里） | ✅ 3A.5 |
| chip | `.chip { h=20, radius 10, padding 0 7, border, bg #1a1a1f, 11px }`；`.chip.ok { #9ed6a1 on #17201a }` | `#v2SpecChip`/`#v2ModeChip`（QSS 属性选择器上语义色：`kind=ok/cache/warn/error`） | ✅ 3A.5 |
| 图标 | 内联 `<symbol>` 16×16 线性图标 | `ui/Glyph.{h,cpp}`（QPainter 绘制，2×DPR 自适应，无 qrc/字体依赖） | ✅ 3A.5（形态近似，见 §5-1） |
| 横幅 | `.banner { h=26, bg #1b1a13, color #f0cb7a, 12px, 左侧图标 + 右侧 × }` | `BannerSlot`（内容仍是 InfoBar 的 `infoBannerRow`） | ⚠️ 容器已对；配色仍是 InfoBar 自己的（3D 收口） |
| 画面区 | `.stage { bg #000 }` | `#v2Stage`/`#v2StageVideo` | ✅ 3A.5 |
| Transport | `.transport 46px`：左组 5+2 按钮 + 速度 + 静音 + 音量；中间 26px 等宽时间码 + `帧 N / 总 N`；右组 6 开关 | `TransportBar`（`v2Transport`/`v2TransportLeft`/`v2TransportReadout`/`v2TransportRight`/`v2PlayButton`/`v2LoopButton`/…） | ✅ 3A.5（读数改用 `infoTimecode/infoFrame/infoTotal` 原对象） |
| 按钮态 | `.btn { h=26, min-w=26, radius 3, icon 16, color text-dim }`；`.btn.on { accent 18% 底 + accent 图标 + 35% 边框 }`；播放键实心 accent | QSS `QToolButton` 规则 + `#v2PlayButton`；`BindGlyph()` 在 checked 时换 accent 图标 | ✅ 3A.5 |
| 时间轴面板 | `.panel.timeline 132`：`.panelhead 26`（◷ 时间轴 + 工作区/视图/缩放）+ 视图窗条 13 + 工作区条 15 + 画布 | `TimelinePanel`（`v2TimelinePanel`/`v2TimelineHead`/`v2WorkMeta`/`v2ViewMeta`/`v2ZoomIn/Out/Fit`）+ 既有 `TimelineWidget` | ⚠️ 头+缩放已对；**两条滑块（视图窗/工作区）仍缺** → 3B |
| 检查器 | `.ins-head 26` 三 tab（标记/截图 3/信息）+ `+`；列表卡片；标签/备注编辑；`.ins-foot` 存储位置 | 现在只有 `MarkPanel` 自己的表头 | ❌ → 3C（3A 只保留原对象与 getter） |
| 状态栏 | `.statusbar 24`：左 1 条消息 + 右 3 个指标（音频/缓存/波形） | `QStatusBar` + `noticeInfo/audioInfo/loopInfo/waveformInfo/statusInfo` | ⚠️ 配色已对；**仍是 6 个控件**、`循环: 关` 与 Transport 循环键重复 → 3D |
| 分隔条 | `.split-h/.split-v { 6px }` + `::after` 1–2px accent 细线（悬停/拖拽时 65% 不透明） | `PlayerSplitter`/`PlayerSplitHandle`（悬停 2px accent） | ✅ 3A.5 |
| 密度 | `#app.density-compact/dense` 覆盖 appbar/banner/transport/timeline/status/panelhead | `Tokens::MakeTokens()` + `ResolveDensity()`（客户区高 + 20px 迟滞） | ✅ 3A.5（dense 无面板头） |

---

## 4. 令牌与配色（以代码为准）

- **令牌**：`src/app/ui/Tokens.h`（高度/间距/字号/圆角/语义色）。视觉侧唯一真源；
  改观感先改令牌，不要在控件里写魔数。
- **圆角**：kbd 2 / 按钮·输入框 3 / 面板 4 / 卡片 8 / chip 胶囊 10（原型 `--radius:4` + `.chip radius:10`）。
- **语义色**（原型 `.chip.ok` 与规划 §4.2 一致，实现用这一套）：
  `ok #9ed6a1 / warn #f0cb7a / err #f08c93 / cache #a6d8c4`，各自配 `*_bg` / `*_border`。
  原型 `:root` 里的 `--ok #6fbf73` 等是早期值，**不要用**。
- **文字层级**：`text #e8e8ee` / `text-dim #a2a2ad`（按钮/标签）/ `text-meta #8a8a95`（元信息）/
  `text-mute #6f6f7a`（**仅禁用与装饰**）。对比度实测见 [3A 交接](handoff-2026-10-26-ui-v2-3a.md) §4.4。
- **字号**：11px 元信息、12px 正文/按钮、13px 标题（文件名）、26px 等宽时间码（compact 22 / dense 22）。
  一律 `QFont::setPixelSize`（`Theme::UiFont/MonoFont`），**不在 QSS 里写字号**。

---

## 5. 3A.5 与原型仍有的差距（按接手顺序）

1. **图标是"形态近似"**：原型是手绘 SVG，当前 `Glyph.cpp` 用 QPainter 画了 30 个近似形（play/pause/循环/flag/相机/…）。
   形状能认、颜色跟令牌，但不是像素级一致；若要精修，改 `Glyph.cpp` 单个函数即可，不影响布局。
2. **时间轴还缺两条滑块**（视图窗 13px / 工作区 15px）与"标记旗标条"。这是 3B 的主线，
   面板头已经留好位置与数值（`工作区 … · 循环开` / `视图 … · 适应窗口`）。
3. **检查器还没 tab 化**：现在直接露出 `MarkPanel` 自己的表头。3C 做 `InspectorPanel`
   （三 tab + 折叠 44 + 页脚存储位置），届时 MarkPanel 只保留列表与编辑区。
4. **状态栏仍是 6 个控件**：原型是"1 条消息 + 3 指标"。3D 收口（同时把 `--banner-test` 的 `loop=`
   指向 Transport 的循环键，见规划附录 A.3）。
5. **横幅配色**：容器/高度已对，`warn` 态文案颜色仍来自 `InfoBar::UpdateBannerStyle()`；
   3D 删 InfoBar 时按原型 `.banner` 重写（`#1b1a13` + `#f0cb7a` + 左侧图标）。
6. **Transport 的宽度降级**只有 3A 的粗粒度（<700 收速度、<640 收 A/B、<520 收次读数）；
   原型还有 760/660/560 三档 + `tiny` 档隐藏 A/B 与循环药丸，3B 按规划 §3.3 做全量。
7. **画面区圆角**：面板卡片有 4px 圆角，但画面是子 HWND，四角不会跟着剪圆（Qt 无法裁剪原生子窗口）。
   这是实现限制，原型不体现；不要为它加遮罩层去压 HWND。

---

## 6. 验收口径（视觉部分）

- `--banner-test` / `--dpi-test` 的截图必须肉眼对得上原型（本文件 §3 的六个区域）。
- 每栏高度仍以令牌 ±2px 为准（M4 断言在 `LayoutProblemsForTest()` 里）。
- 正文对比度 ≥4.5:1（截屏采样，方法见 [3A 交接](handoff-2026-10-26-ui-v2-3a.md) §4.4）。
- 新增/改动控件必须带稳定 `objectName`（列表见规划 §7.7），否则截图对比与自动化都定位不到。
