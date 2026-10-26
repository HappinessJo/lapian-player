# 新会话提示词：UI v2 实现阶段 3C（记录层：InspectorPanel 三 tab + 截图网格 + 信息页）

- **状态**：待执行（2026-10-26 起草）。
- **前置**：3A 壳层 + 3A.5 视觉对齐 + **3B 播放层**已合并且全绿
  （`625683d` / `05154ca` / `058a7e1` / `b806fb8` / `034f1b8` / `00a3728`）。
- **本阶段只做 3C**：把检查器从"直接露出 MarkPanel"改成 `InspectorPanel`（3 tab + 计数徽标 + 折叠 44 +
  页脚存储位置）+ 截图网格 + 信息表 + 折叠/聚焦联动。
  **不碰 3D 的在线/全屏/删 InfoBar/状态栏。**
- **纪律**：一个阶段 = 可审查 commit + 全量回归；**新旧壳跑两遍**；**先按 [视觉基准](ui-v2-visual-reference.md)
  把原型渲成截图逐区对照再动手**（AGENTS §11 规则 16）；不达标不进 3D。

---

## 提示词（复制到新会话）

```text
你是拉片软件 UI v2 的「3C 记录层」实现 agent。3A 壳层 / 3A.5 视觉对齐 / 3B 播放层已完成并全绿，
本轮只做 3C：检查器三 tab + 截图网格 + 信息页 + 折叠/聚焦联动。

先读（按顺序，别跳）：
- AGENTS.md：§1 当前状态（找 "UI v2 3A 壳层" / "3A.5 视觉对齐" / "UI v2 3B 播放层" 三条）、
  §9 已知坑、§11 协作规则、§12 文档表
- docs/handoff-2026-10-26-ui-v2-3b.md（**直接上游**）：§3 关键做法（滑块同源/内联输入/四档降级/去轮询/
  kFixed 14px）、§4 门禁两张表、§6 M7、§8 偏差、§10 残余、§11 3C 入口
- docs/ui-v2-visual-reference.md（**视觉基准，必读**）：§1 铁律、§2 渲图命令、§3 组件对照表
  （**看"检查器"那一行**）、§5 遗留差距（第 3 条 = 本阶段）、§6 验收口径
- docs/ui-v2-plan.md（已批准规划）：
  §2.1 区域与像素（检查器 304 / 220–560 / 折叠 44；头 26 · 列表 · 编辑区 · 页脚存储位置）、
  §2.2 控件归属（存储位置**只在**检查器页脚；3 个 tab + 计数徽标 + 折叠）、
  §2.3 键位可发现性 17 行（第 13 行"添加标记"的可见入口 = Transport 按钮 + 检查器 `+`；M5 要复核）、
  §4.2 令牌（w-inspector 304 / 220–560 / 折叠 44、inspector_head 26、h-chip 20、fs-micro 10 的徽标）、
  §7.1 布局树、§7.6 冷启动纪律（对话框/菜单点击时建）、§7.7 objectName 清单（**检查器那一段**）、
  §7.8 测试缝、§11 A.1/A.5（折叠/切 tab 的断言改法 + mark/note 新增 2 条用例）
- docs/handoff-2026-10-26-ui-v2-3a.md：§4.4（3A.5 视觉对齐口径、setDefaultAction 坑）、§6 复现命令
- docs/ui-v2-proto/plan.html（浏览器打开；默认即产品形态）。检查器那一段：
  `.ins-head 26`（三 tab + 计数 `.n` 徽标 + `+` + 折叠）、`.marklist` 卡片、`.editbox`（标签/备注）、
  `.ins-foot`（存储位置）；折叠态是 `.rail`（竖排"标记 6 / 截图 3" + 展开按钮）。
  仓库里已有 docs/ui-v2-proto/render-plan-1280.png，可放大对照。
- docs/ui-v2-definition.md §6.1 的 M5（键位可发现 100%）、M4（画面占比）、M2（对比度采样）
- 代码（只读，动手前通读这 7 处）：
  src/app/MarkPanel.{h,cpp}（现有侧栏：表头/树/编辑box/存储位置；objectName markPanel/markTree/
    markLabelEdit/markNoteEdit/markStorage；**必须原对象保留 + 能独立工作**，见事实 4）
  src/app/ui/PlayerShell.{h,cpp}（v2Inspector / v2InspectorRail(26px, 只有 » 按钮) / v2InspectorContent /
    SetInspectorCollapsed：折叠 = host 固定 44 + content hide + h_split SetCollapsed）
  src/app/MainWindow.cpp 里检查器相关块：BuildShellUi（mark_panel_ 塞进 inspectorContentHost）、
    SyncShellChrome、SetInspectorCollapsed 的调用点、MarkStateForTest/MarkCountForTest/MarkTextForTest/
    ActivateMarkForTest/DeleteMarkForTest/SetMarkNoteForTest/FocusNoteEditorForTest、
    SetMarks/LoadMarksForCurrentSession、snapshot_saved_paths_、SnapshotService 的接线
  src/app/ui/Tokens.h / ui/Theme.cpp / ui/Glyph.{h,cpp}（令牌、唯一 QSS、图标与 BindToolAction）
  src/media/SnapshotNaming.{h,cpp}（截图命名与目录：本地 {名}_f{帧}_{HH-MM-SS-FF}.png 落在
    <视频目录>/snapshots/；在线走 <Pictures>/拉片软件/<bvid>/ + index.jsonl）
  src/app/SnapshotService.{h,cpp}（Capture/Drain/snapshotSaved 信号；**没有枚举历史截图的 API**）
  src/media/MediaInfo.h（信息页要用的字段：format_name/duration_us/video{width,height,codec,frame_rate,
    nb_frames}/has_audio/audio{...}/rotation/vfr）

3C 要做的事（建议顺序 = 提交顺序，每步都要能编译 + 跑测试）：

1) 新写 `src/app/ui/InspectorPanel.{h,cpp}`（只做容器与 tab，不含业务）
   - 头 26（`v2InspectorHead`）：三个 tab 按钮 `v2TabMarks`（标记，右挂计数徽标 `v2MarkBadge`）、
     `v2TabShots`（截图，`v2ShotBadge`）、`v2TabInfo`（信息）；右侧 `+`（加标记，绑既有 mark_action_）
     与折叠按钮（把 PlayerShell 里那个 `v2InspectorCollapse` 搬进来，objectName 不变）。
   - `v2InspectorStack`（QStackedWidget 三页）：page0 = 既有 MarkPanel 原对象、page1 = 截图网格
     `v2ShotGrid`、page2 = 信息表 `v2InfoTable`。
   - 页脚 `v2InspectorFooter` + `v2StoreText`：**存储位置的唯一常显处**（MarkPanel 自己的
     `markStorage` 标签保留对象但不要在页脚之外重复展示）。
   - 折叠 44：`v2InspectorRail` 竖排显示"标记 N / 截图 M" + 展开按钮；展开 304（220–560）。
     折叠 = `mark_panel_->setVisible(false)`（**不销毁**）。
   - 全 `Qt::NoFocus`（tab/按钮），只有列表与编辑框可聚焦（键位矩阵依赖这个）。
2) 把 InspectorPanel 接进 PlayerShell / MainWindow（"搬不换"）
   - `MainWindow::BuildShellUi` 里 `mark_panel_` 不再直接塞进 `inspectorContentHost`，而是交给
     `shell_->inspectorPanel()->AdoptMarkPanel(mark_panel_)`；PlayerShell 的旧 rail 逻辑删掉或降级为
     InspectorPanel 内部实现，**`SetInspectorCollapsed` 的对外语义与调用点不变**。
   - 计数徽标订阅既有数据源：标记数 = `mark_panel_->MarkCount()`（`SetMarks` 后刷新；
     已有 `MarkCountForTest()` 同源），截图数 = 本次会话 `snapshot_saved_paths_.size()` + 历史扫描（见 3）。
   - 切 tab / 折叠时**先 `mark_panel_->FlushEditor()`**（T5.3 契约：切换选中/切文件/关窗之前必须 flush）。
3) 截图网格（`v2ShotGrid`）
   - 数据源两路：**在线**读 `<Pictures>/拉片软件/<bvid>/index.jsonl`（每行
     `{bvid,cid,qn,codecid,fps,frame,pts,path,saved_at}`，见 SnapshotNaming.h）；
     **本地**扫 `<视频目录>/snapshots/*.png`，用 SnapshotNaming 的命名规则反解帧号
     （建议在 SnapshotNaming 里加一个**纯函数** `ParseSnapshotFrameFromName()`，core/media 只允许加只读访问器）。
   - 每格显示缩略图 + `f<帧号>`；点击 = 跳到那一帧（沿用 T5.2 的顺序：**先重绘 UI，再
     `singleShot(0)` 精确解码**，别在点击处理器里同步 seek）。
   - 空态：没有截图时显示"还没有截图（S）"，不显示空网格。
   - 不要为截图网格新增后台线程/定时器；缩略图按需 `QPixmap` 缩放并限数量（首屏 ≤60 张）。
4) 信息页（`v2InfoTable`）
   - 两列表格：格式/分辨率/codec/帧率（标称与实际）/时长/总帧数（复用 `TotalFrameCount` 三态：
     精确 / 估算 ≈ / 扫描中）/音轨（codec·采样率·声道）/旋转/可变帧率提示/路径。
   - 数据来源：`session_->info()`（media::MediaInfo）+ MainWindow 现成的格式化逻辑；
     **不要**从 InfoBar 的标签文本反解析。
5) 折叠/聚焦联动（**新行为，必须写进文档**）
   - `FocusNoteEditorForTest()`（以及"点标记跳转后聚焦编辑框"这类路径）：若当前不是"标记"tab 或处于折叠态，
     先切到标记 tab + 展开，再聚焦。
   - 折叠后 `MarkCountForTest()`、`SelectedMarkId()` 必须不变（不重建数据，只 hide）。
   - `--key-test` 的 `focus-tree` 依赖"默认 tab = 标记 + 1280 宽下检查器展开" → **默认 tab 必须是标记**。
6) 测试与文档
   - `--mark-test` / `--note-test` 断言**一行不改**（0 失败），并按规划 A.5 **新增 2 条**：
     ① 折叠检查器后 MarkCount 与选中 id 不变；② 折叠态 `FocusNoteEditorForTest()` 自动展开到标记 tab。
   - 新 objectName 全按 §7.7（`v2InspectorHead`/`v2TabMarks`/`v2TabShots`/`v2TabInfo`/`v2MarkBadge`/
     `v2ShotBadge`/`v2InspectorStack`/`v2ShotGrid`/`v2InfoTable`/`v2InspectorFooter`/`v2StoreText`/
     `v2InspectorRail`/`v2InspectorCollapse`）；`markPanel`/`markTree`/`markLabelEdit`/`markNoteEdit`/
     `markStorage` 沿用旧名。
   - 视觉：按视觉基准 §2 渲原型，把检查器区（头 26 三 tab + 计数 + `+` / 列表卡片 / 编辑区 / 页脚）
     与自己的 `--banner-test` 截图并排看，把"已对齐/仍缺"写进交接。

门（**新旧壳各跑一遍**；命令见 3A 交接 §6 与 3B 交接 §9）：
  ctest 37/37（**`lapian_mark_panel_test` 不能挂**：MarkPanel 必须还能独立构造）
  --dpi-test 4 DPI × {normal,fullscreen,narrow} + 2 个新用例：problems 为空 + M4 仍 62/62/55/59 实测
      （3B 基线 64.17/64.17/64.42/56.64/60.80%）+ 每栏 ±2 + `inspector-width`（展开 220–560 / 折叠 44±1）
  --banner-test 74/0（**`loop=` 读的是状态栏 loop_label_，本阶段不要删/隐藏**，改指向是 3D）
  --key-test 77/0（**默认 tab 必须是标记**，否则 focus-tree 挂）
  --mark-test / --note-test 0 失败（+ 上述 2 条新用例）
  --drag-test（P4 不退化、P6 p95 ≤150ms）、--drag-strategy-test 7/7
  --loop-test / --loop-end-test 全过
  --startup-test P1 ≤1.5s、P2 ≤2.5s（3B 基线 161–181 / 363–400ms）
  --fullscreen-test 全过（chrome 全 0 的断言不变）
  --readout-edit-test（3B 新增：M5 tooltip 全表 + M7 四态 + 滑块对齐）
  在线（Medium 副本，AGENTS §9）：--online-longpress-test 54/0、T8.6.1b、--online-drag-parity-test 61/0

退出：M5 仍 100%（检查器 `+` 是"添加标记"的第二入口）、M4 数字一个都不许改、M7 不变、
      `--mark-test`/`--note-test` 0 失败；`ctest` 37/37。

不要碰（硬边界，同 3A/3B）：
  GfxWidget 呈现、PreviewDecoder/DragStrategy、OnlineSession 状态机、CacheStore、media::StatusNotice 语义、
  spec §3.7 键位、settings schema、任何门/阈值/性能数字、InfoBar（3D 才删）、状态栏 6 控件（3D）、
  横幅配色（3D）、3B 的 Transport 四档降级与两条滑块（除非回归）。

事实附录（已确认，别再踩一遍）：
1. 折叠现在的实现：`PlayerShell::SetInspectorCollapsed(true)` → `inspector_content_->hide()` +
   `inspector_host_` 宽固定 44 + `h_split_->SetCollapsed(true)`；`inspector_rail_` 是 26px 固定宽。
   折叠宽度是 `tokens_.inspector_collapsed`（44），`--dpi-test` 会断言 44±1，别改成别的值。
2. 200% DPI（928 宽）下检查器**自动折叠**（`inspector_auto_decided_`，只在首次打开媒体时按 width<1000 决定一次）
   → 折叠态（44px）里"标记 N / 截图 M"要竖排塞得下；信息页在折叠态不可见。
3. `mark_panel_` 的对象/hierarchy 被这些地方依赖：`--mark-test`/`--note-test`（走公有 API）、
   `--key-test` 的 `FocusMarkTreeForTest()`（findChild("markTree") + isVisible）、
   `tests/mark_panel_test.cpp`（**独立构造 MarkPanel**）、`MarkStateForTest()`。
4. 截图**没有**枚举 API：`SnapshotService` 只负责 Capture/Drain 与写 index.jsonl（且 index.jsonl 只有**在线**才写）。
   本地历史截图只能扫目录 + 解析文件名，别指望 `snapshot_saved_paths_`（它只存本次会话）。
5. MarkPanel 的 `markStorage` 标签与新的 `v2StoreText` 会重复 → 存储位置收敛到页脚；`markStorage` 对象保留
   （3A 的 17 个"沿用旧名"里有它），只是不要再在列表上方显示。
6. `--banner-test` 的 `volume=` 语义在 3B 已改成"音量入口还在"（滑条或静音按钮可见），别改回去。
7. 本机 `--dpi-test` 必须带 `--file`（否则常驻不退出）；`--mark-test` 会留下 sidecar，
   **跑 `--note-test` 前先删 `media\测试.mp4.lapian.json`**（3B 门禁脚本里已自动清）。
8. 从工作区直接跑 exe 是 **Low integrity**，在线用例必须用 Medium 副本（`%LOCALAPPDATA%\Programs\拉片软件`）。
9. 构建：仓库内 TEMP；**改 `CMakeLists.txt` 会让本机 CMake 重配崩（`0xC0000409`）** —— 把
   `build\release` 整目录改名后再全新 configure（3B 的做法：`build\release-3a5` 保留旧二进制做 A/B）。
10. 3B 的测试缝可直接复用：`SetSplitSizesForTest(tl, ins)` / `DragSplitterForTest` /
    `SetTimelineHeightForTest` / `ShellStateForTest` / `M7StateForTest` / `TimelineBarAlignForTest`；
    新增检查器相关缝请沿用"只读、无副作用、返回字符串"的风格（`InspectorStateForTest()`）。
11. 不要用 `Get-Content -Raw`/`Set-Content` 改带中文的源文件（3B 会话把 TransportBar.cpp 弄坏过一次）；
    `.ps1` 脚本保持纯 ASCII。`auto near = …` 编译不过（windef 宏）。
```
