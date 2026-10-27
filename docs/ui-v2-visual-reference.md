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

> **仓库里已有渲好的对照图（不想现场渲就看这两张）**：
> render-plan-1280.png（原型 1280×900）与
> render-vs-product-3a5.png（上=原型，下=2026-10-26 的 v2 壳层，
> 用于看清"已对齐/仍缺"的差别）。图是 2026-10-26 渲的，原型改动后请按 §2 重渲并覆盖。
> 3B 的时间轴逐区对照见 `build/ui-v2-compare-timeline.png`（上=原型、下=产品，2×）——
> 它是**构建产物**（不入库），复现方法：`--banner-test` 截图后按 §2 的坐标裁剪拼接。

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
| 顶栏 | `.appbar h=38 bg #17171a 底边线`，文件图标+名、规格 chip、模式 chip、右 4 按钮 | `TopBar`（`v2TopBar`/`v2FileLabel`/`v2SpecChip`/`v2ModeChip`/`v2OpenButton`/`v2OnlineButton`/`v2HelpButton`/`settingsSlot`/`cacheSettingsButton`） | ✅ 3A.5；**3D：模式 chip 收口**（本地/在线解析/在线 qn+fps/缓存命中/缓冲/切档/打开失败），DEC-4 `onlineFpsBadge` 进 chip 内 |
| 键位角标 | `.kbd {9px Consolas, border #34343d, bg #1e1e24}` | `#v2KbdBadge`（在"打开/在线"里） | ✅ 3A.5 |
| chip | `.chip { h=20, radius 10, padding 0 7, border, bg #1a1a1f, 11px }`；`.chip.ok { #9ed6a1 on #17201a }` | `#v2SpecChip`/`#v2ModeChip`（QSS 属性选择器上语义色：`kind=ok/cache/warn/error`） | ✅ 3A.5 |
| 图标 | 内联 `<symbol>` 16×16 线性图标 | `ui/Glyph.{h,cpp}`（QPainter 绘制，2×DPR 自适应，无 qrc/字体依赖） | ✅ 3A.5（形态近似，见 §5-1） |
| 横幅 | `.banner { h=26, bg #1b1a13, color #f0cb7a, 12px, 左侧图标 + 右侧 × }` | `BannerSlot`（`v2BannerSlot`/`v2BannerIcon`/`infoBanner`/`infoBannerClose`） | ✅ 3D：颜色/图标/单行/仅降帧可关；`media::StatusNotice` 语义未动 |
| 画面区 | `.stage { bg #000 }` | `#v2Stage`/`#v2StageVideo` | ✅ 3A.5 |
| Transport | `.transport 46px`：左组 5+2 按钮 + 速度 + 静音 + 音量；中间 26px 等宽时间码 + `帧 N / 总 N`；右组 6 开关 | `TransportBar`（`v2Transport`/`v2TransportLeft`/`v2TransportReadout`/`v2TransportRight`/`v2PlayButton`/`v2LoopButton`/…） | ✅ 3A.5；3B 内联输入/缓冲槽/四档降级；**3D：读数改由 Transport 自建**（`v2Timecode`/`v2FrameNumber`/`v2FrameTotal`） |
| 按钮态 | `.btn { h=26, min-w=26, radius 3, icon 16, color text-dim }`；`.btn.on { accent 18% 底 + accent 图标 + 35% 边框 }`；播放键实心 accent | QSS `QToolButton` 规则 + `#v2PlayButton`；`BindGlyph()` 在 checked 时换 accent 图标 | ✅ 3A.5 |
| 时间轴面板 | `.panel.timeline 132`：`.panelhead 26`（◷ 时间轴 + 工作区/视图/缩放）+ 视图窗条 13 + 工作区条 15 + 画布 | `TimelinePanel`（`v2TimelinePanel`/`v2TimelineHead`/`v2WorkMeta`/`v2ViewMeta`/`v2ZoomIn/Out/Fit`）+ `RangeBar`（`v2ViewBar`/`v2ViewHandleLeft/Right`/`v2WorkBar`/`v2WorkHandleLeft/Right`）+ 既有 `TimelineWidget` | ✅ 3B（两条滑块、播放头常驻、删内部滚动条；dense 档无头但数值未搬 Transport 副行 → 见 §5-2）。**有意偏差**：画布左右各 8px 外边距（原型画布铺满），与滑块轨道的 8px 内边距对齐，否则工作区端点对不准画布（2026-10-26 用户要求"往里收"） |
| 检查器 | `.ins-head 26` 三 tab（标记/截图 3/信息）+ `+`；列表卡片；标签/备注编辑；`.ins-foot` 存储位置 | `ui/InspectorPanel`（`v2InspectorHead`/`v2TabMarks`/`v2TabShots`/`v2TabInfo`/`v2MarkBadge`/`v2ShotBadge`/`v2InspectorAdd`/`v2InspectorCollapse`）+ `v2InspectorStack` 三页（MarkPanel 原对象 / `v2ShotGrid` / `v2InfoTable`）+ `v2InspectorFooter`/`v2StoreText` + 折叠 `v2InspectorRail`/`v2InspectorExpand` | ✅ 3C（行内删除/选中左侧 accent 竖条仍缺，见 §5-3） |
| 状态栏 | `.statusbar 24`：左 1 条消息 + 右 3 个指标（音频/缓存/波形） | `QStatusBar` + `noticeInfo`/`audioInfo`/`cacheInfo`/`waveformInfo` | ✅ 3D：1 消息 + 3 指标；`speed/loop/volume/statusInfo` 退出（对象保留） |
| 全屏 | 顶栏/横幅/状态栏/检查器隐藏，Transport/时间轴保留 | `PlayerShell::SetFullScreenChrome` + `--fullscreen-test` chrome | ✅ 3D 复核：`v2=1 appbar=0 banner=0 status=0 inspector=0 transport=1 timeline=1` |
| 对话框/菜单 | 原型无（主题内深色） | `Theme::GlobalStyleSheet`（`QDialog`/`QMenu`/`QMessageBox` + `online*`/`cache*`/`v2HelpDialog`） | ✅ 3D：设置/在线/扫码/帮助/速度/清晰度全深色；无 per-widget QSS |
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
  `text-mute #6f6f7a`（**仅禁用与装饰**）。对比度实测见 3A 交接 §4.4。
- **字号**：11px 元信息、12px 正文/按钮、13px 标题（文件名）、26px 等宽时间码（compact 22 / dense 22）。
  一律 `QFont::setPixelSize`（`Theme::UiFont/MonoFont`），**不在 QSS 里写字号**。

---

## 5. 3A.5 与原型仍有的差距（按接手顺序）

1. **图标是"形态近似"**：原型是手绘 SVG，当前 `Glyph.cpp` 用 QPainter 画了 30 个近似形（play/pause/循环/flag/相机/…）。
   形状能认、颜色跟令牌，但不是像素级一致；若要精修，改 `Glyph.cpp` 单个函数即可，不影响布局。
2. **时间轴的波形配色/标记旗标条仍是 3A 的临时值**：两条滑块（视图窗 13px / 工作区 15px）与播放头常驻
   已在 3B 补上（形态与原型一致），但波形还是 `#7aa2f7` 实色 + `0.62×可用高`（原型是 `--wave` 55% 半透明、
   独立标记带），标记只有细线 + 3px 小旗（原型是带帧号文字的旗标条）。属"仍缺"，3C/收口再定。
   另：dense 档（客户区 <520）时间轴按 §3.1 无面板头，但"工作区/视图数值移到 Transport 副行"这半句
   **3B 未做**（Transport 副行在 502px 宽时只剩 ~190px，塞不下）→ 见 3B 交接 §10-2。
3. **检查器已 tab 化（3C，2026-10-26）**：`ui/InspectorPanel` 做了头 26 三 tab + 计数徽标 + `+`/折叠、
   `v2InspectorStack` 三页（既有 MarkPanel 原对象 / 截图网格 / 信息表）、页脚存储位置、折叠 44 rail。
   与原型仍有差距：标记行没有 hover 删除按钮、选中行没有左侧 2px accent 竖条（MarkPanel 既有行为，
   单测断言依赖）；截图网格 caption 按任务用 `f<帧号>`（原型是"第 N 帧"）；原型截图页的独立
   "截图目录"页脚被收敛成检查器的唯一常显页脚（规划 §2.2）。对照图：
   `build/ui-v2-shots/3c-vs-proto-inspector.png`（上=原型，下=产品；构建产物，不入库）。
4. **状态栏已压到 1 消息 + 3 指标（3D）**：`noticeInfo` + `audioInfo`/`cacheInfo`/`waveformInfo`；
   `speed_label_`/`loop_label_`/`volume_slider_`/`statusInfo` 退出常驻（对象保留）。`--banner-test` 的
   `loop=` 已改指 Transport 循环键（规划附录 A.3），断言未改。
5. **横幅配色已按原型重写（3D）**：`#1b1a13` 底 + `#f0cb7a` 文字 + 左侧 `v2BannerIcon`；error/info 有
   语义变体；同一时刻仍 1 条、单行、仅降帧可关。`InfoBar` 已删除，`UpdateBannerStyle` 的 per-widget
   QSS 一并消失。
6. **Transport 的宽度降级已按 §3.3 做成四档**（3B）：≥760 全量 / 660–759 收音量（静音保留）/
   560–659 收 A/B+速度且读数 26→22 / <560 再收首末帧·标记·截图。与原型一样是"按可用宽度退场"，
   差别只在阈值来源（原型用 `@media` 的窗口宽，产品用 **Transport 自身宽度**，按规划 §3.3）。
   实测保留集宽度 full 档 `need=736`（阈值 760）。
7. **画面区圆角**：面板卡片有 4px 圆角，但画面是子 HWND，四角不会跟着剪圆（Qt 无法裁剪原生子窗口）。
   这是实现限制，原型不体现；不要为它加遮罩层去压 HWND。
8. **3D 后仍缺**：在线 820 窄窗的顶栏模式 chip（`在线 · 1080P · 29.412fps · 降帧` + 清晰度按钮 +
   4 个按钮）未截图/未断言；真实 1080P 的在线截图因 SESSDATA 过期（`vip_status=-1`）未生成；检查器
   行内删除/选中 accent 竖条仍缺（与 3C 相同）。

---

## 6. 验收口径（视觉部分）

- `--banner-test` / `--dpi-test` 的截图必须肉眼对得上原型（本文件 §3 的六个区域）。
- **3B 起另有两个"文字化"的视觉证据**（不靠肉眼）：
  - `--readout-edit-test` 会打印**全部 tooltip 名+文案**（M5 逐条对照 §2.3）与 **M7 四态几何**
    （`rel=帧号中心−Transport 中心`，跨播放/暂停/拖动/全屏必须一致）。
  - `--dpi-test` 会打印 `dpi_bars view/work` 几何（两条滑块的行/轨道位置）与 `dpi_state` 的每栏高度。
- **3C 起再加一个**：`InspectorStateForTest()` / `--dpi-test` 的 `dpi_inspector ...` 行会打印
  `collapsed/tab/page/w/h/marks/shots/tiles/badge/shotbadge/rail/info/store`，并在每个 DPI 相位保存
  `build/t6.4-dpi-<tag>-inspector-{collapsed,wide,shots,info,narrow}.png`。
- 每栏高度仍以令牌 ±2px 为准（M4 断言在 `LayoutProblemsForTest()` 里）。
- 正文对比度 ≥4.5:1（截屏采样，方法见 3A 交接 §4.4）。
- 新增/改动控件必须带稳定 `objectName`（列表见规划 §7.7），否则截图对比与自动化都定位不到。
