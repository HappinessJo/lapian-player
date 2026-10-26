# 新会话提示词：UI v2 实现阶段 3D（在线 / 全屏 / 收口）

- **状态**：待执行（2026-10-26 起草）。
- **前置**：3A 壳层 + 3A.5 视觉对齐 + 3B 播放层 + **3C 记录层**已完成并全绿
  （代码 `9a713da`、交接 `38bb21e`；HEAD 以 `git log` 为准）。
- **本阶段目标**：把新壳从"本地记录闭环"推进到**可收口的唯一壳**——在线/模式 chip、清晰度菜单、
  横幅接管、全屏 chrome、对话框进主题，然后**删除 `InfoBar` 与旧布局分支**，`LAPIAN_UI_V2` 变 no-op。
  退出条件是 **M1–M9 全过**（M1 回归数字一个不动）+ 用户实机体感确认（记为体感，不写"验收通过"）。
- **纪律**：先按 [视觉基准](ui-v2-visual-reference.md) 渲原型逐区对照再动手（AGENTS §11 规则 16）；
  每一步一个可审查 commit + 全量回归；**新壳全绿之后**才做删除提交；删除旧壳/InfoBar 必须单独一个
  commit（出问题整体 `git revert` 回 3C，见规划 §8 回滚）。

---

## 提示词（复制到新会话）

```text
你是拉片软件 UI v2 的「3D 在线 / 全屏 / 收口」实现 agent。3A 壳层 / 3A.5 视觉对齐 / 3B 播放层 /
3C 记录层已完成并全绿，本轮做 3D：在线与全屏收口、对话框进主题，最后删除 InfoBar 与旧布局分支，
让 `LAPIAN_UI_V2` 变成 no-op。退出条件是 M1–M9 全过（M1 回归数字一个不动）+ 用户实机体感确认
（只记"体感"，不写"验收通过"）。

先读（按顺序，别跳）：
- AGENTS.md：§1 当前状态（找 "UI v2 3A 壳层" / "3A.5" / "UI v2 3B 播放层" / "UI v2 3C 记录层" 四条）、
  §9 已知坑、§11 协作规则、§12 文档表
- docs/handoff-2026-10-26-ui-v2-3c.md（**直接上游**）：§2 文件级改动、§3 五条做法（容器只做容器 /
  搬不换 / 折叠 44 / 截图网格 / 默认 tab=标记）、§4 门禁、§7 视觉自检、§8 偏差、§10 残余、§11 3D 入口
- docs/ui-v2-plan.md（已批准规划）：
  §1 Q4（**删除 InfoBar，通知语义搬到 BannerSlot；StatusNotice 一行不改**）、Q5（`LAPIAN_UI_V2` 的
  no-op 口径）、§2.1/§2.2（顶栏模式 chip、清晰度入口、横幅槽、状态栏 1 条消息 + 3 指标）、
  §2.3 17 行键位（**M5 必须 100%**）、§3.2 状态矩阵（加载/错误/缓冲/降帧/缓存命中）、§4.2 令牌、
  §7.1 布局树、§7.6 冷启动纪律（菜单/对话框点击时建）、§7.7 objectName（3D 删除 `info=` 别名）、
  §8 的 "3D 在线 / 全屏 / 收口" 段、§11 A.2/A.3/A.4（**InfoBar 删除的逐条改法**）
- docs/ui-v2-visual-reference.md（**视觉基准，必读**）：§1 铁律、§2 渲图命令、§3 组件对照表
  （**"顶栏 / 横幅 / 状态栏 / 全屏" 四行**）、§5 遗留差距（第 4 条状态栏=3D、第 5 条横幅配色=3D）、
  §6 验收口径
- docs/handoff-2026-10-26-ui-v2-3a.md：§4.4（视觉对齐/`setDefaultAction` 坑/`BindToolAction`）、
  §6 复现命令、§7 残余（**空态 × DPI 无自动化**，M3 要求补）
- docs/handoff-2026-10-26-ui-v2-3b.md：§3（去轮询/四档降级）、§10 残余（T8.6.1b 本机基线）
- docs/ui-v2-definition.md §6.1 的 M1–M9（尤其 M1 回归、M2 对比度、M3 DPI×4 相位、M4 画面占比、
  M5 键位可发现、M6 单横幅、M7 帧号常驻、M8 冷启动、M9 可回退）
- docs/ui-v2-proto/plan.html（默认即产品形态；对照顶栏模式 chip、横幅、状态栏、全屏）。
  仓库已有 docs/ui-v2-proto/render-plan-1280.png 与 render-vs-product-3a5.png，可先看再重渲。
- 代码（只读，动手前通读这 9 处）：
  src/app/InfoBar.{h,cpp}（**3D 要删**：文件/规格/帧号/时间码/总帧/唯一横幅/fps 角标的五职责与
    SetMedia/SetFrame/SetTotal/SetError/SetOnlineFrameRateStatus/ApplyNotices/SecondarySummary）
  src/app/ui/BannerSlot.{h,cpp}（现在只是"reparent InfoBar 的 banner_row + 高度"；3D 要把
    primary/secondary/关闭/kind 逻辑下沉进来，新增 IsVisibleKind()/Dismiss()/SetNotices()）
  src/app/ui/TopBar.{h,cpp}（ModeKind 4 档 + SetMode + AdoptFileLabel/AdoptSpecLabel/AdoptFpsBadge/
    AdoptQualityButton/BindActions；3D 要补"在线·1080P·29.412fps/缓存命中/解析中/错误/降帧"文案与
    fps 角标进 chip）
  src/app/ui/TransportBar.{h,cpp}（3B 已接管时间码主读数与 v2FrameNumber/v2FrameTotal 的**对象**；
    3D 删除 InfoBar 后要由 TransportBar 自己更新帧号/时间码/总帧文本）
  src/app/MainWindow.cpp/.h：`info_bar_` 的全部调用点（grep info_bar_ 约 30 处）、
    BannerStateForTest/BannerUiStateForTest/Info*ForTest/OnlineFpsBadge*/OnlineFrameRateBanner*、
    LayoutProblemsV2/LayoutStateForTest、BuildShellUi/AdoptShellWidgets/BuildLegacyUi/
    LayoutProblemsLegacy、UpdateShellModeChip、UpdateBufferSlot、RebuildOnlineQualityMenu、
    **3C 的 RefreshInspectorInfo 用了 info_bar_->TotalForTest()（删除后必须换源，否则信息页总帧数挂）**
  src/app/CacheSettingsDialog.{h,cpp} / OnlineResolveDialog.{h,cpp} / OnlineSession 的 UI 回调
    （对话框主题、清晰度菜单、缓冲/错误文案；**不改 OnlineSession 状态机**）
  src/app/ui/Theme.cpp + Tokens.h（单份 QSS / 令牌；横幅配色、状态栏压缩都改这里，不写 per-widget QSS）
  src/app/main.cpp（`--banner-test`/`--dpi-test`/`--online-quality-test --ui`/`--readout-edit-test`
    的接线；3D 补 `--dpi-test --empty` 空态相位）
  src/media/StatusNotice.{h,cpp}（**一行不改**；BuildMediaStatusNotices/SelectPrimaryNotice/
    SecondaryNoticeSummary/SecondaryNoticeTooltip/StatusNoticeKind）

3D 要做的事（建议提交顺序；前 3 步保持 InfoBar 仍可编译可测，第 4 步才删除）：

1) 顶栏模式 chip 收口（在线状态矩阵，规划 §3.2）
   - 本地：`本地 · 零网络`（ok）；在线加载：`在线 · 解析中`（warn）；在线播放：`在线 · 1080P ·
     29.412fps`（cache，用实际 qn/fps，不拿 API 标称冒充）；缓存命中：`缓存命中 · 0 网络`；
     打开失败：`打开失败`（err）；降帧：warn 且 DEC-4 fps 角标进 chip 内（同一 `onlineFpsBadge`
     对象 reparent，状态逻辑不动）。
   - 清晰度按钮/菜单：**点击时才建**菜单（§7.6），菜单项仍来自 `online_quality_`；不要改
     `QualityMenuContext`/`FrameRatePolicy` 语义。
   - 缓冲槽文案（`v2BufferSlot`）与状态栏消息口径照 §3.2；`--banner-test` 的 `volume=` 语义
     保持"音量入口还在"（滑条或静音按钮可见），不要改回去。
2) BannerSlot 接管横幅（规划 Q4 / 附录 A.3）
   - 把 `InfoBar::UpdateNotices/ApplyNotices` 的"primary 画出来 + 关闭按钮 + 几何"下沉到
     BannerSlot；`media::StatusNotice` 的构建/择一/次级摘要**一行不改**。
   - 保留 `infoBanner` 标签与 `infoBannerClose`（objectName 不变），同一时刻仍只有 1 条；
     其余进状态栏 `notice_label_`。
   - 新增 `IsVisibleKind(lapian::media::StatusNoticeKind)` / `Dismiss()` / 关闭可见性；MainWindow
     的 4 个在线降帧 getter 改为指向 BannerSlot（A.4 表），**--online-quality-test --ui /
     --online-vip-test 的断言一行不改**。
   - `BannerStateForTest()` 保留键与顺序（kind=/visible=/banners=/text=/banner=/info_h=/
     secondary=/active=）；`info_h=` 语义改为 BannerSlot 高度（名字不改，加注释）；
     `BannerUiStateForTest()` 的 `loop=` 改为 `loop_toggle_button_->isVisible()`（A.3 唯一要动的 1 行）。
   - 横幅配色按原型 `.banner`（#1b1a13 底 + #f0cb7a 文字 + 左侧图标）；M2 截屏重采样。
3) 全屏 chrome + 对话框进主题
   - 全屏：顶栏/横幅/状态栏/检查器隐藏、Transport 与时间轴保留、`Esc`/`F`/双击语义不变、
     退出后几何与焦点恢复；`--fullscreen-test` 的 `chrome=` 断言保持（旧壳删掉后只保留 v2 分支）。
   - 主题：CacheSettingsDialog / OnlineResolveDialog（二维码/粘贴）/ 帮助 QMessageBox /
     清晰度 QMenu / 设置对话框全部走 `Theme::GlobalStyleSheet()` + 令牌；只允许三处动态色
     （fps 角标/危险按钮/banner kind），**禁止 per-widget setStyleSheet**（现有 InfoBar 的样式要随删除消失）。
   - 状态栏按定义 §11-Q8 压到"1 条消息 + 3 指标（音频/缓存/波形）"；`speed_label_`/`volume_slider_`/
     `loop_label_` 不再常驻状态栏。**注意**：`--banner-test` 的 `loop=` 在 3C 仍读
     `loop_label_->isVisible()`；本阶段按 A.3 改指 Transport 循环键后，不要顺手删掉 `loop_label_`
     的其它用途（grep 后再删）。
4) 删除 `InfoBar` 与旧布局（**单独一个 commit，可整体 revert 回 3C**）
   - 先把"5 个职责"交给新主人：文件/规格 → TopBar；帧号/时间码/总帧 → TransportBar（对象
     `v2FrameNumber`/`v2Timecode`/`v2FrameTotal` 保留，几何与 M7 不变）；唯一横幅 → BannerSlot；
     fps 角标/降帧 → TopBar chip + BannerSlot；错误横幅 → BannerSlot。
   - **3C 依赖**：`RefreshInspectorInfo()` 现在用 `info_bar_->TotalForTest()` 显示总帧数三态；
     删除后改读新的总帧数来源（`session_->index().Total()` / `ContainerTotal()` / 新增
     `total_for_ui_` 都可，但必须仍能显示 精确 / ≈估算 / 扫描中 三态）。
   - `InfoFrameTextForTest()`/`InfoFileLabelForTest()`/`InfoFileNameForTest()`/`InfoNarrowModeForTest()`/
     `InfoBarVisibleForTest()`/`InfoBarHeightForTest()` **方法名不改**，内部改读 TopBar/Transport/
     BannerSlot（A.2/A.3）。
   - `LayoutStateForTest()` 的 `info=`/`info_<label>=` 别名删除（`banner=` 保留）；`LayoutProblemsForTest()`
     删 info-* 5 条、改 banner 判据；`--dpi-test`/`--banner-test` 的脚本按新键更新。
   - 删除 `src/app/InfoBar.{h,cpp}` 与 CMake 条目；删除 `BuildLegacyUi`/旧布局分支/
     `LayoutProblemsLegacy`/`LayoutStateForTest` 旧壳路径/所有 `shell_ == nullptr` 分支；
     `MainWindow` 构造函数的 `ui_v2` 参数保留但不再影响布局，`UiV2ForTest()` 恒 true；
     `LAPIAN_UI_V2`/`--ui-v2` 仍解析但**无效果**（旧脚本不炸）。
   - **本机 CMake 重配会崩（0xC0000409）**：删文件会触发重配；按 3B/3C 做法把 `build\release`
     整目录改名（如 `build\release-3c`）后全新 configure，保留旧二进制做 A/B。别在旧目录重试。
5) 空态 × DPI（M3 缺口）
   - 给 `--dpi-test` 加空态相位（`--empty`，不需要 `--file`），4 DPI × 空态/播放/全屏/窄窗口；
     空态下顶栏模式 chip=`未打开`、检查器默认折叠/展开口径按规划 §3.1，`LayoutProblemsForTest()` 为空。
6) 文档与收口
   - 新写 `docs/handoff-2026-10-26-ui-v2-3d.md`（或按实际日期）：文件级改动、6 步做法、
     新旧壳→单壳对照、M1–M9 逐条证据、M4 未动数字、M2 采样、残余、回滚说明。
   - 更新 AGENTS §1/§12（v6.55）、visual-reference（顶栏/横幅/状态栏/全屏四行收口）、
     ui-v2-plan（§7.7 删 `info=` 别名等）；`docs/next-session-prompt-ui-v2-3d.md` 入库后登记。

门（**删除前新壳一遍全绿；删除后单壳再全跑一遍**；在线一律用 Medium 副本
`%LOCALAPPDATA%\Programs\拉片软件`，工作区 exe 是 Low integrity）：
  ctest 37/37
  --dpi-test：4 DPI × {空态, 播放, 全屏, 窄窗口} problems 为空；每栏=令牌±2；
     检查器 304/220–560/折叠 44；M4 仍 64.17/64.17/64.42/56.64/60.80%（门槛 62/62/55/59）
  --banner-test 74/0（保留 8 个键；info_h= 语义=BannerSlot 高度；banners=1）
  --key-test 77/0（所有新按钮 NoFocus；Space/方向键不被吃掉）
  --mark-test / --note-test 0 失败（含 3C 的 2 条用例；3D 不许改断言）
  --snapshot-test 35/0（先清 media/snapshots）
  --readout-edit-test：M7 四态 geometry 不变；tooltip 全表 17/17（M5）
  --fullscreen-test / --loop-test / --loop-end-test 全过
  --drag-test：P4 不退化、P6 p95 ≤150ms；--drag-strategy-test 7/7
  --startup-test：新壳 P1 ≤1.5s、P2 ≤2.5s；d3d_init_at_startup=0
  M2：截屏采样 ≥4.5:1（顶栏/Transport/检查器/状态栏/横幅/菜单/对话框各至少 1 处）
  M6：banner 出现时只位移、不覆盖；单横幅
  在线（Medium 副本；参数见 3C 交接 §9 与 docs/handoff-2026-10-24-t8.14-m2.md）：
    --online-m1-test（3/3 样本，28/0 量级）、--online-m2-test（34/0 量级）、
    --online-offline-test 44/0（workspace 45/0 也可记录）、
    --online-quality-test --ui、--online-vip-test --ui、
    --online-drag-parity-test 61/0（需 --m1-throttle-bps 262144）、
    --online-drag-preview-test（T8.6.1b；本轮 24/0，35 项是含缓存命中相位的另一跑法）、
    --online-longpress-test 54/0、--online-qr-test 19/0（--ui 21/0）、
    --online-fault-test、--online-cache-test 32/0
  dist 体积（≤100MB；打包脚本按需，不重新装也行）

退出：M1–M9 全过（M1 回归数字一个不动：ctest/parity/T8.6.1b/key/A/local/P6）+ **状态栏/横幅/模式 chip
与原型肉眼对齐** + 用户实机体感确认（记"体感"，不写"验收通过"）。

不要碰（硬边界）：
  GfxWidget 呈现、PreviewDecoder/DragStrategy、OnlineSession 状态机、CacheStore、
  media::StatusNotice 语义、spec §3.7 键位、settings schema、任何门/阈值/性能数字、
  3B 的 Transport 四档降级与两条滑块、3C 的检查器布局（折叠 44/220–560/三 tab/页脚），
  除非是删除 InfoBar 必需的接线（那就只改接线不改行为）。

事实附录（3C 结束后的现状，别再踩一遍）：
1. 当前壳已含：InspectorPanel（三 tab/徽标/折叠/页脚/截图网格/信息页）、Transport 内联输入 +
   速度菜单 + 缓冲槽 + 四档降级、TimelinePanel 两条滑块、去轮询。3D 只做在线/全屏/收口/删壳。
2. **InfoBar 现在是"隐藏的状态拥有者"**：它的 QLabel 已被 reparent 到 TopBar/Transport/BannerSlot，
   但 SetMedia/SetFrame/SetTotal/SetError/SetOnlineFrameRateStatus/ApplyNotices 都还在 InfoBar 里。
   删除不是删文件，而是把 5 个职责搬完并改 30 处调用；先 grep `info_bar_` 再动手。
3. **3C 的 `RefreshInspectorInfo()` 读 `info_bar_->TotalForTest()`**；删除 InfoBar 时必须换源，
   否则检查器信息页的总帧数三态（精确/≈估算/扫描中）直接坏掉。
4. `BannerSlot` 现在没有 kind/dismiss API；A.4 的 4 个 getter 需要 `shell_->banner()->IsVisibleKind(...)`/
   `Dismiss()`；`infoBanner`/`infoBannerClose` 名字保留（`banners=` 恒 1）。
5. `BannerStateForTest()` 的键/顺序是硬契约（`--banner-test`、`--robust-test` 都读）；`info_h=`
   改语义不改名。`BannerUiStateForTest()` 的 6 个键里只有 `loop=` 需要改（A.3）。
6. `--online-quality-test --ui`/`--online-vip-test` 断言一行不改，只改 4 个 getter 指向（A.4）。
7. `--banner-test` 的 `loop=` 在 3D 前仍读 `statusBar` 的 `loop_label_`；3D 按 A.3 改指 Transport
   循环按钮。`volume=` 已在 3B 改为"音量入口还在"（滑条或静音按钮可见），别改回去。
8. `LAPIAN_UI_V2`/`--ui-v2` 在 3D 后是 no-op：仍解析、不报错，但只有一套布局；`UiV2ForTest()` 恒 true。
   删旧壳的 commit 必须单独一个，`git revert` 可整体回到 3C。
9. 删除 `InfoBar.{h,cpp}`/旧布局会改 `src/CMakeLists.txt` → 本机 CMake 重配崩（0xC0000409）；
   必须整目录改名后全新 configure（3B/3C 做法：`build\release` → `build\release-3c`）。
10. `--dpi-test` 的 tag 参数是 **`--dpi-tag`**（3B 脚本写成 `--tag` 导致截图名恒为 x；3C 已改）；
    `--dpi-test` 需要 `--file`（3D 补 `--empty` 空态相位，M3 要空态列）。
11. `--snapshot-test` 前清 `media\snapshots`（该检查要求目录恰好 3 个 PNG，历史遗留会误报
    `three-files`）；`--mark-test` 会留 `media\测试.mp4.lapian.json`，跑 `--note-test` 前删掉。
12. 在线用例必须用 Medium 副本；`--online-drag-parity-test` 要 `--m1-throttle-bps 262144`。
13. **工具坑**：在工具 shell 里跑 GUI 探针时**不要用 `$out = & exe ... 2>&1`**（本会话实测会把 GUI
    进程挂起/输出丢失，一次泄漏 5 个 lapian）；用 `& exe ... 2>&1 | Tee-Object -FilePath <log> |
    Select-String ...` 直接管道。`.ps1` 保持纯 ASCII；执行策略会拦脚本，必要时
    `powershell -NoProfile -ExecutionPolicy Bypass -File ...`（但输出捕获同上）。
14. 不要用 `Get-Content -Raw`/`Set-Content` 改带中文的源文件（会 ANSI→UTF-8 乱码，3B 踩过）；
    用 read/edit 工具。`auto near = …` 编译不过（windef 宏）。
15. 新增按钮用 `Glyph::BindToolAction`（`QToolButton::setDefaultAction` 会在 action changed() 时
    把自绘图标清空成"…"）；所有工具按钮/菜单项/tab 全 NoFocus（`--key-test` 77/0 依赖）。
16. 3C 的验收数字：dpi 新壳每档 37 检查、M4 64.17/64.17/64.42/56.64/60.80、banner 74/0、
    key 77/0、mark/note 0、snapshot 35/0、drag P4 66.3–67.7/P6 15.6–22.3、startup 187/414；
    3D 只能让这些数字**不变或更好**，任何门/阈值/性能数字都不许改。
```

---

## 备注（给写提示词的人/后续维护）

- 3D 是**唯一允许删旧代码**的阶段；删 `InfoBar`/旧布局前必须让新壳的 M1–M9 先过一遍并留证据。
- 如果时间不够，优先顺序：**在线模式 chip + 横幅接管 + 删 InfoBar**（M2/M5/M6）；
  对话框主题与状态栏压缩可以留到同一阶段的最后，但 M1–M9 的"验收"必须全绿才能写完成。
- 与 3C 的衔接点：`InspectorStateForTest()`、`--dpi-test` 的 `dpi_inspector` 行、检查器页脚
  的 `SetStorageText` 都不应被 3D 改动；3D 只允许改"总帧数来源"这条接线。
