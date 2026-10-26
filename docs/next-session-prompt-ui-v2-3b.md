# 新会话提示词：UI v2 实现阶段 3B（播放层：Transport + 时间轴两条滑块）

- **状态**：待执行（2026-10-26 起草）。
- **前置**：3A 壳层 + 3A.5 视觉对齐已合并（`625683d` / `05154ca` / `058a7e1`），新旧壳门禁全绿。
- **本阶段只做 3B**：`TransportBar` 收尾（帧号/时间码内联输入、速度按钮、缓冲槽、四档宽度降级）+
  `TimelinePanel` 补两条滑块（`RangeBar`）、播放头常驻/越界箭头、缩放下限 8px/帧、波形 Fill、
  删掉时间轴内部横向滚动条。
  **不碰 3C 的检查器、不碰 3D 的在线/全屏/删 InfoBar。**
- **纪律**：一个阶段 = 可审查 commit + 全量回归；**新旧壳跑两遍**；**先按 [视觉基准](ui-v2-visual-reference.md)
  把原型渲成截图逐区对照再动手**（AGENTS §11 规则 16）；不达标不进 3C。

---

## 提示词（复制到新会话）

```text
你是拉片软件 UI v2 的「3B 播放层」实现 agent。3A 壳层与 3A.5 视觉对齐已完成并全绿，本轮只做 3B。

先读（按顺序，别跳）：
- AGENTS.md：§1 当前状态（找 "UI v2 3A 壳层" / "3A.5 视觉对齐" 两条）、§9 已知坑、§11 协作规则、§12 文档表
- docs/ui-v2-visual-reference.md（**视觉基准，本阶段必读**）：§1 铁律、§2 怎么把原型渲成截图、§3 组件对照表
  （看 Transport / 时间轴两行）、§5 遗留差距（第 2 条就是本阶段要补的"时间轴两条滑块"）、§6 验收口径
- docs/ui-v2-plan.md（已批准规划）：
  §2.1/§2.2 信息架构与组件职责（Transport 三组 + 读数 + 缓冲槽；时间轴头/两条滑块/画布）、
  §2.3 键位可发现性 17 行对照表（M5 就按它验）、
  §3.3 窄宽度矩阵（四档：≥760 / 660–759 / 560–659 / <560，**按 Transport 自身宽度降级**）、
  §4.2 令牌（h-track-zoom 4 / h-track-work 10、fs-readout 26 / fs-readout-sm 22、h-btn 26）、
  §4.4 M4 断言（每栏 = 令牌 ±2，画面 = H − 固定合计）、
  §7.5 原生 HWND 四条硬规则、§7.6 冷启动纪律、
  §7.7 objectName 清单（Transport 段与时间轴段）、§7.8 测试缝、§11 附录 A.6（高度策略）
- docs/handoff-2026-10-26-ui-v2-3a.md：§4.4（3A.5 做了什么 / setDefaultAction 坑）、§6 复现命令、§7 残余、§8 3B 入口
- docs/ui-v2-proto/plan.html（用浏览器打开；默认即产品形态）。仓库里已有渲好的
  docs/ui-v2-proto/render-plan-1280.png，时间轴那一段可以放大对比。
- docs/ui-v2-definition.md §6.1 的 M5（键位可发现 100%）、M7（帧号常驻：播放/暂停/拖动/全屏四态位置不变）
- 代码（只读，动手前先通读这 6 处）：
  src/app/ui/TransportBar.{h,cpp}（已有三组按钮 + 读数，缺输入/速度菜单/缓冲槽/四档降级）
  src/app/ui/TimelinePanel.{h,cpp}（已有头 + 缩放 + 数值，缺两条滑块）
  src/app/ui/Tokens.h、ui/Theme.cpp（令牌与唯一 QSS）、ui/Glyph.{h,cpp}（图标与 BindToolAction）
  src/app/TimelineWidget.{h,cpp}（HeightPolicy、ruler/waveform 高度、内部 scroll_bar_、播放头绘制）
  src/core/TimelineModel.{h,cpp}（**唯一真源**：pixels_per_frame / scroll / FrameAtX / SetScroll）
  src/app/MainWindow.cpp 里 v2 相关块：BuildShellUi / AdoptShellWidgets / SyncShellChrome /
  UpdateTimelineMeta / SetLoopIn|Out / ApplyLoopIn / loop_ 与 LoopRange 的用法

3B 要做的事（建议顺序 = 提交顺序，每步都要能编译 + 跑测试）：

1) Transport 收尾（改 ui/TransportBar）
   - **帧号/时间码内联输入**：点时间码（工具提示"点击输入时间码或帧号（Ctrl+G）"）→ 原地变 QLineEdit
     （h-input 22，objectName `v2ReadoutEdit`），Enter 提交、Esc 取消、失焦提交、非法输入回滚；
     接受 `1234`（帧号）与 `HH:MM:SS:FF` 两种写法（复用现有的时间码解析/格式化，别新写一套）；
     Ctrl+G 仍走既有 action（只把焦点送进输入框）。
   - **次读数**：给 `v2FrameNumber` / `v2FrameTotal` 补上 objectName（现在是借用的 infoFrame/infoTotal），
     `v2Timecode` 要给**标签本身**（现在是外层 slot 名），M7 的几何断言按这两个名字取控件。
   - **速度按钮** `v2SpeedButton`：替换现在借用的 speedInfo 标签（对象仍可由 MainWindow 提供文本，
     但外观是 26px 高按钮，点击弹 0.25–2x 菜单，[ / ] 仍可用）；菜单**点击时建**（§7.6，别在构造期建）。
   - **缓冲槽** `v2BufferSlot` / `v2BufferText`：只在在线 buffering/stalled 时出现（进度条 + `已缓存 12.3/30.9 MB`），
     本地模式隐藏；数据来自既有 OnlineSession 的进度回调，**不改 OnlineSession**。
   - **四档宽度降级**（按 Transport 自身宽度，显式隐藏，**不许靠裁剪**）：
     ≥760 全保留；660–759 收音量滑条（静音按钮保留）；560–659 收 A/B 与速度、读数 26→22px；
     <560 再收首末帧/标记/截图。优先级顺序照 §3.3 表，阈值按 QFontMetrics 实测校准 ±10% 以内。
     `TransportBar::minimumSizeHint()` 仍要返回小值，否则 820 宽窗口缩不下去（3A 踩过）。
   - 每个按钮的 tooltip 一字不差照 §2.3 的表（M5 逐条验）。

2) 时间轴两条滑块（新写 ui/RangeBar.{h,cpp}，改 ui/TimelinePanel）
   - `RangeBar` 两种模式：单值条（视图窗，`h-track-zoom` 视觉 4px / 行高 13px 热区）与区间条
     （工作区，`h-track-work` 视觉 10px / 行高 15px 热区）；支持拖端、拖中、点空白、滚轮、
     双击复位（工作区双击 = 清除 A/B 回到全片），全部 `Qt::NoFocus`。
   - 视图窗滑块：**全片坐标**，拖中 = 平移（改 TimelineModel 的 scroll），拖端 = 缩放（改 pixels_per_frame，
     下限 **1 帧 ≥8px**）；与滚轮/`+`/`-`/适配共用同一夹取路径。
   - 工作区滑块：**可见范围坐标**，拖端 = 改 A/B 入出点（走既有 SetLoopIn/SetLoopOut 语义，含 A>B 与 A=B 规则），
     拖中 = 平移整个区间；A/B 的**唯一常显数值**在面板头（已有 `工作区 … · 循环开/关`）。
   - objectName 按 §7.7：`v2ViewBar`/`v2ViewHandleLeft`/`v2ViewHandleRight`/`v2WorkBar`/
     `v2WorkHandleLeft`/`v2WorkHandleRight`。
   - **两条滑块与画布必须同源**（规划 Q3）：三者都从 TimelineWidget 的 TimelineModel 读，不许各存一份视图状态；
     新增 `TimelineViewFramesForTest()`（`first,last`）用于断言"视图窗 = 画布可见范围"。
   - 高度预算：面板仍是 132/112/96（令牌），面板头 26（dense 无头）+ 视图条 13 + 工作区条 15
     **都从画布区里扣**（画布 = 剩余），不许把面板总高顶出去——M4 只给了 ±2px。

3) 画布收尾（改 TimelineWidget）
   - **播放头常驻**：任何情况都画；不在可见范围时贴边画方向箭头（新增 `TimelinePlayheadVisibleForTest()`）。
   - **波形 Fill**：拉高时间轴时波形按 `clamp(0.62*avail, 8, 96)` 取高（96 封顶），拉高留白不放大波形；
     确认 Fill 模式下 `RulerHeight()/WaveformHeight()` 都按实际高度派生。
   - **删掉内部横向滚动条**（`scroll_bar_` / `timelineScrollBar` / `kScrollBarHeight` 及其 per-widget
     setStyleSheet）：视图窗滑块取代它。删完 `src/` 与 `tests/` 里应再无 `timelineScrollBar`。
   - 高度策略：`kFill` 已在 v2 生效（`MainWindow.cpp` 的 `SetHeightPolicy(kFill)`），**默认仍是 kFixed**，
     `tests/timeline_test.cpp` 的 128/72 断言一个字都不许改（附录 A.6）。

4) 状态同步去轮询
   - MainWindow 里 3A 留下的 `ui_meta_timer_`（250ms）→ 换成信号：时间轴的 zoom/scroll/loop/播放头变化
     由 TimelinePanel/TimelineWidget 发信号，面板头与循环按钮订阅。**不要再加第二个定时器**。

不要碰（硬边界，同 3A）：GfxWidget 呈现、PreviewDecoder/DragStrategy、OnlineSession 状态机、CacheStore、
src/core/*（含 TimelineModel 的语义；只允许加只读访问器）、media::StatusNotice 语义、spec §3.7 键位、
settings schema、任何门/阈值/性能数字。

门（新旧壳各跑一遍；命令见 3A 交接 §6）：
  ctest 37/37
  --dpi-test 4 DPI × normal/fullscreen/narrow：problems 为空 + M4 仍 62/62/55/59 实测值
      （3A 基线 64.2/64.2/64.4/56.6/60.8%）+ 每栏 ±2px；**新增两个用例**：档位切换（640/520 边界来回拖）、
      时间轴拖到 340（最大值）
  --banner-test 74/0（**注意：`loop=` 这个键读的是状态栏的 loop_label_，本阶段不要删/隐藏它**，
      改指向是 3D 的事，见规划附录 A.3）
  --key-test 77/0（加输入框最容易破的一关：输入框聚焦时全局键必须让位）
  --mark-test / --note-test 0 失败
  --drag-test：P4 不退化、P6 p95 ≤150ms，**时间轴 132 与 340 两种几何各测一次**（规划 R6/R11）
  --drag-strategy-test 7/7
  --loop-test / --loop-end-test 全过（工作区滑块会碰 A/B 语义）
  --startup-test：P1 ≤1.5s、P2 ≤2.5s；**本阶段会加控件，先量基线再收尾量一次，把差值写进交接**
  --fullscreen-test 全过（chrome 全 0 的断言不变）
  在线（需要 Medium 副本，见 AGENTS §9）：--online-longpress-test 54/0、T8.6.1b 35/0、
      --online-drag-parity-test 61/0（若一次跑挂，先按既有记录判断是不是 flaky，别当成本阶段回归）

退出条件：M5（§2.3 的 17 条 100% 有可见入口或 tooltip，逐条打勾写进交接）、M7（播放/暂停/拖动/全屏四态
帧号可见且位置不变）、P6 ≤150ms、P4 两种几何都不退化；M1/M4/M8 的数字一个都不许改。

交付：
- 交接 docs/handoff-<今天>-ui-v2-3b.md：改了什么（按文件）、门禁数字（新旧壳两张表）、M5 17 条对照结果、
  M7 四态几何、与规划的偏差逐条、残余、3C 入口；
- AGENTS.md §1/§12 记账（版本号 +1）；
- 视觉自检：把 docs/ui-v2-proto/plan.html 渲成 1280 宽截图（视觉基准 §2 有命令），与自己的
  --banner-test 截图并排看六个区域，把"已对齐/仍缺"写进交接。
```

---

## 3B 的"完成"定义（防自欺）

- Transport 在 1280 宽下**全部控件**都在，且每个按钮 tooltip 与规划 §2.3 一字不差；把窗口压到 820 与 560，
  控件按 §3.3 的优先级**消失而不是被裁**（截图 + 可见性断言各一份）。
- 点时间码能输入 `1234` 和 `00:00:41:04` 都能到同一帧；按 Esc 回滚；输入框里打字不会触发播放/步进（`--key-test`）。
- 两条滑块都能拖端、拖中、滚轮、双击复位；**画布可见范围与视图窗滑块始终一致**（同一帧号，不是"差不多"）；
  缩到最小仍满足 1 帧 ≥8px。
- 时间轴从 96 拖到 340，画面占比不变差（M4 断言过）、波形不超过 96px 高、P4 不退化。
- 播放头在"全片可见 / 放大到局部 / 拖到片尾"三种情况下都画得出来，越界时是贴边箭头而不是消失。
- 冷启动 P1/P2 与 3A 基线（773/985ms，含未定位的 +0.5s）比**没有新增** >50ms 的退化；
  若更差，先查是不是又踩了"构造期建菜单/预加载图标"。

## 3B 明确不做

- 不做检查器 tab 化/折叠联动/截图网格（3C）；
- 不删 `InfoBar`、不改在线文案与位置、不把 `--banner-test` 的 `loop=` 改指向（3D，附录 A.3/A.4）；
- 不做状态栏从 6 个控件压到"1 条消息 + 3 指标"（3D）；
- 不修 3A 的 `window.show()` +0.5s（已记为残余；修它要单独一轮，别顺带改）；
- 不动任何门禁阈值、性能数字、键位语义。

---

## 事实附录（给接手的人省时间，不用再 grep）

**3A.5 之后的代码地图**

| 文件 | 现状 | 3B 要动吗 |
|---|---|---|
| `ui/Tokens.h` | 三档 `Tokens`（appbar/banner/transport/timeline/timeline_head/status/margin/handle/btn/chip/play/readout/inspector…）+ 颜色/圆角常量，`MakeTokens()`/`ResolveDensity()`/`FixedTotal()` | 可能加 `track_zoom`/`track_work` 两个字段 |
| `ui/Theme.cpp` | 唯一 QSS（面板卡片/胶囊 chip/按钮/滑块/滚动条/菜单/输入框）+ `UiFont/MonoFont` + `ApplyTheme` | 输入框与滑块样式已就位，按需补 `#v2ReadoutEdit` |
| `ui/Glyph.{h,cpp}` | 30 个 QPainter 图标；`BindGlyph`（toggled 换色）、`BindToolAction`（绑 QAction） | 需要新图标时在此加 |
| `ui/TransportBar.{h,cpp}` | 三组按钮全部绑好既有 QAction；`v2Timecode`(slot)/`v2TransportMeta`/`v2SpeedSlot`/`v2VolumeSlot`；宽度降级只有 700/640/520 三刀 | **是**（本阶段主战场） |
| `ui/TimelinePanel.{h,cpp}` | 头（`v2TimelineHead`）+ `工作区/视图` 两段文本 + `−/+/适配`（绑既有 action，适配发信号）；`canvasHost()` 装 TimelineWidget | **是**（加两条 RangeBar） |
| `ui/PlayerShell.{h,cpp}` | 三块面板卡片 + 分隔条 + 密度/令牌/夹取；`timelinePanel()`、`timelineHost()`；时间轴未拖拽时 `setFixedHeight(令牌)`，按下 separators handle 时释放 | 少量（高度预算与夹取） |
| `ui/TopBar.{h,cpp}` `BannerSlot` `StageStack` `PlayerSplitter` | 3A.5 已完成（图标/chip/kbd 角标/卡片） | 否 |
| `TimelineWidget.{h,cpp}` | `HeightPolicy{kFixed,kFill}`（默认 Fixed）、`frame_count()/scroll_px()/fit_mode()`、内部 `scroll_bar_`、播放头/标尺/波形自绘 | **是**（滑块同源、播放头、删滚动条） |
| `MainWindow.{h,cpp}` | `BuildShellUi/AdoptShellWidgets/SyncShellChrome/UpdateTimelineMeta/JumpToEdgeFrame`；`ui_meta_timer_` 250ms；`LayoutProblemsV2/LayoutStateV2` + 20 个 `*ForTest()` | **是**（接线 + 去轮询 + 新 getter） |

**已知坑（3A / 3A.5 踩过，别重踩）**

1. `QToolButton::setDefaultAction()` 会在 action 每次 `changed()` 时把 icon 重设成 `action->icon()`（空）→
   自绘图标退化成"…"。一律用 `Glyph::BindToolAction()`。
2. 加控件会把**窗口最小宽度**顶上去（820 宽用例直接失败）。条状容器覆写 `minimumSizeHint()` 返回小值，
   再按 §3.3 显式隐藏，别靠 `QSizePolicy::Ignored`（会把读数挤成 0 宽）。
3. 全局 QSS 里**不写字号**；字号由 `Theme::UiFont/MonoFont` 的 `setPixelSize` 按令牌下发。
4. 每个自绘容器都要 `Theme::SetStyledBackground()`（`WA_StyledBackground`），漏一个就是浅色底/1.28:1 事故。
5. `PlayerShell` 的时间轴在用户没拖过时是 `setFixedHeight(令牌)`；QSplitter 会重新分配尺寸 →
   改高度逻辑时保持"未拖拽 = 固定令牌"这条，否则 M4 的 `timeline-height` 断言会飘。
6. `--banner-test` 的 `loop=` 读 `loop_label_`（状态栏），删它 = 直接 74 项里的 1 项挂（3D 才改指向）。
7. `--dpi-test` 没有 `--file` 会常驻不退出（测试调度在 `!file_path.isEmpty()` 分支里）。
8. 从工作区直接跑 exe 是 **Low integrity**，在线用例必须用 Medium 副本（`%LOCALAPPDATA%\Programs\拉片软件`）。
9. 构建/单测都要仓库内 TEMP：`$env:TMP=$env:TEMP='E:\拉片软件开发\.deps\tmp'`；改 `CMakeLists.txt` 后
   本机 CMake 3.31 重配会崩（`0xC0000409`），先删 `build\release\CMakeCache.txt` 再 configure。
10. PowerShell 做多行替换时 `.Replace(a, @'…'@ -replace …)` 会被解析成 3 个实参；先把 here-string 存进变量。
11. `auto near = …` 编译失败（`near` 是 windef 宏）。
