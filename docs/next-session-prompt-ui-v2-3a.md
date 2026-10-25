# 新会话提示词：UI v2 实现阶段 3A（壳层）

- **状态**：待执行（2026-10-26 起草）。规划已批准，**4 项决策全部按建议**（见 [规划 §0.1](ui-v2-plan.md)）。
- **前置**：产品代码基线 `352b29f`；规划文档 HEAD 见 `git log`；`docs/ui-v2-plan.md` 已批准。
- **本阶段只做 3A**：壳层（Theme/Tokens/PlayerShell/StageStack/分隔条/三档密度/测试缝）。
  **不碰 3B 的 Transport 与时间轴面板、3C 的检查器、3D 的在线与删 InfoBar。**
- **纪律**：一个阶段 = 一个可审查 commit + 全量回归；**新旧壳跑两遍**；不达标不进 3B。

---

## 提示词（复制到新会话）

```text
你是拉片软件 UI v2 的「3A 壳层」实现 agent。规划已批准，本轮开始写产品代码，但只做 3A。

先读（按顺序）：
- AGENTS.md（§1 当前状态、§9 已知坑、§11 协作规则、§12 文档）
- docs/ui-v2-plan.md（**已批准的规划**）：§0.1 批准记录、§2 P1 信息架构、§3 P2 状态矩阵、
  §4 P3 令牌与高度预算、§6 P5 组件清单、§7 P6 技术拆分（尤其 7.1/7.2/7.3/7.5/7.7/7.8）、
  §8 P7 的 3A 段、§11 附录 A（测试改法）
- docs/ui-v2-proto/plan.html（评审版原型，浏览器打开；默认即产品形态）
- docs/ui-v2-definition.md §6.1（M1–M9）、§7.1（测试耦合）
- 只读代码：src/app/MainWindow.cpp:150-530（布局/action/状态栏）、782-1131（尺寸/响应式/断言）、
  src/app/GfxWidget.{h,cpp}（HWND 与 EnsureReady）、src/app/InfoBar.cpp:33-37、
  src/app/MarkPanel.cpp:61-89、src/app/TimelineWidget.{h,cpp}、src/core/TimelineModel.{h,cpp}

3A 要做的事（顺序即提交顺序，可拆 2–3 个 commit，但每个都要能编译 + 跑测试）：
1. `src/app/ui/Tokens.h` + `ui/Theme.{h,cpp}`：令牌常量、QPalette、单份全局 QSS、应用字体、
   `ApplyTheme(QApplication&)`。**铁律**：所有自绘容器 `setAttribute(Qt::WA_StyledBackground, true)`；
   禁止 per-widget setStyleSheet（除 fps 角标/危险按钮/banner kind 三处动态色）。
2. `ui/PlayerShell.{h,cpp}` + `ui/PlayerSplitter.{h,cpp}`：按规划 §7.1 的布局树搭壳；
   先"搬不换"——把现有 `TimelineWidget` / `MarkPanel` / `volume_slider_` / `notice_label_` /
   `audio_label_` / `waveform_label_` / `status_label_` / `cache_settings_button_` / `quality_button_`
   **原对象 reparent** 进新区（保住所有 getter），外观之后各阶段再换。
3. `ui/TopBar`（文件/规格/模式 chip + 打开/在线/?/设置 四个按钮，`setDefaultAction` 绑既有 QAction）
   与 `ui/BannerSlot`（一条横幅 + 关闭；复用 media::StatusNotice 的构建/择一/次级摘要，**一行不改**）。
4. `ui/StageStack`：QStackedWidget（page0 = GfxWidget，page1 = 空/加载/错误卡片）；
   空态隐藏 GfxWidget（规划 Q1 已实测 HWND 不重建）。**顺序**：先切 page0 + `gfx_->show()`，再取 `d3d_device()`。
   空态卡片 `setAcceptDrops(true)`（否则空态拖文件没反应）。
5. 三档密度 + `ApplyTokens()`：按客户区高度取档（≥640 / 520–639 / <520，20px 迟滞）；
   `--dpi-test` 的 4 档 + narrow 必须过。时间轴/检查器的 min/max 与折叠按 §7.4。
6. 开关：`LAPIAN_UI_V2=1` 或 `--ui-v2`（main.cpp 里构造 MainWindow 之前读一次；两套布局互斥构建）。
7. 测试缝：按规划 §7.8 加 getter（ShellState/DensityName/GfxVisible/StagePage/BannerSlotHeight/
   SplitterState/SetSplitSizes/DragSplitter/InspectorCollapsed/FullScreenChrome），
   并按附录 A.1/A.2/A.8 改 `LayoutProblemsForTest()`、`LayoutStateForTest()`、`--startup-test`、`--fullscreen-test`。
   **A.6 是硬约束**：`TimelineWidget` 高度策略默认 Fixed（保住 tests/timeline_test.cpp 的 128/72 断言），
   v2 才用 Fill——3A 可以只加 API 不改行为。

不要碰（硬边界，见规划 §6.3）：GfxWidget 呈现、PreviewDecoder/DragStrategy、OnlineSession、CacheStore、
src/core/*（含 TimelineModel）、media/StatusNotice 的语义、spec §3.7 键位、settings schema、任何门/阈值。

门（新旧壳各跑一遍，命令与判据见规划 §8 的 3A 段）：
  ctest 37/37；--dpi-test（4 DPI × normal/fullscreen/narrow，problems 为空 + 新 M4 62/62/55/59）；
  --banner-test 全过；--startup-test（P1/P2 + d3d_init_at_startup）；--key-test 77/0；
  --mark-test / --note-test 0 失败。
退出条件：M2（无浅色混入 + 对比度采样）、M3、M4、M7、M8 全过。

交付：3A 交接 `docs/handoff-2026-10-26-ui-v2-3a.md`（改了什么、门禁数字、残余、3B 入口），
并在 AGENTS.md §1/§12 记账。
```

---

## 3A 的"完成"定义（防自欺）

- 新壳能启动、能开本地文件、能播放/步进、时间轴与标记栏都在、空态是卡片；
- **旧壳不设 `LAPIAN_UI_V2` 时行为与 `352b29f` 完全一致**（回归对照）；
- `--dpi-test` 在 100/125/150/200% 与 820×500 下 `LayoutProblemsForTest()` 为空，
  且实测画面占比 ≥ 62/62/55/59；
- 顶栏/Transport/检查器/状态栏的正文对比度 ≥4.5:1（截屏采样 4 处，写进交接）；
- 冷启动 P1 ≤1.5s、P2 ≤2.5s（`--startup-test` 三次），并记录 `d3d_init_at_startup=0`；
- 空态 → 打开 → 出现画面 三次无黑屏/无交换链告警（`LAPIAN_TIMING_LOG` 首次 Present 耗时写进交接）。

## 3A 明确不做

- 不做 Transport 的宽度降级与读数改造（3B）；
- 不做时间轴两条滑块与波形 Fill（3B）；
- 不做检查器的 tab 化与折叠联动（3C）；
- 不删 InfoBar、不改在线相关文案与位置（3D）；
- 不动任何性能数字与门禁。
