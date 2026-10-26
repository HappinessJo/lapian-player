> 给后续新开对话的 agent：先读本文件，再读 [spec.md](spec.md)、[plan.md](plan.md)、[tasks.md](tasks.md)。
> 产品决策以 `spec.md` v0.3 为准；技术方案以 `plan.md` + ADR 为准；v1 任务以 `tasks.md` 为准，**v2 以 [tasks-v2.md](tasks-v2.md) 为准**。
> 最后更新：2026-10-26（v6.57：**UI v2 3D 热修实现（非门）**：4 项用户实机反馈逐项修复——P0 拖动后立刻 Space 卡住（在线 seek 在途只记待播、落地补 Play；`--scrub-space-test` 在线 64/0、本地 62/0，修复关闭时 20/20 失败）；P1 在线改缓存目录两阶段（写 settings+pending、会话结束 idle 后换 store；cache manage 113/0、真实在线 `--cache-switch-online-test` 17/0）；P2 扫码面板按内容长高+夹屏+小屏滚动（QR 26/0×4 DPI、240×240/按钮完整）；P3 清晰度 chip（Theme `#onlineQualityButton` + 12px chevron；4 DPI 1280/820 顶栏 overflow=0）；提交 `534d909`/`b78bbe1`/`6533da0`/`efc6831`；门禁 ctest 37/37、banner 74/0、key 77/0、M4 不变、drag A/B 不差、M1/parity/longpress/offline/cache 不变（T8.6.1b 35/1、fault 92/0 ui_gap 203 为既有）；证据 [hotfix 实现交接](docs/handoff-2026-10-26-ui-v2-3d-hotfix-close.md)；v6.56：**UI v2 3D 热修交接/提示词（未改代码）**：用户实机反馈 4 项——拖动后立刻 Space 播放卡住（在线异步 seek 落地 `SetPosition`→`Pause`）、在线模式不能改缓存目录（已确认两阶段保存）、扫码二维码显示不全（缺 `adjustSize`）、清晰度入口不像按钮；根因/方案/基线/测试见 [hotfix 交接](docs/handoff-2026-10-26-ui-v2-3d-hotfix.md)，新对话提示词 [next-session-prompt-ui-v2-3d-hotfix.md](docs/next-session-prompt-ui-v2-3d-hotfix.md)；v6.55：**UI v2 3D 在线/全屏/收口（非门）**：删除 `InfoBar`/旧布局（单壳；`LAPIAN_UI_V2`/`--ui-v2` 仍解析但 no-op）；顶栏模式 chip 收口（本地/在线解析/在线 qn+fps/缓存命中/缓冲/切档/打开失败，DEC-4 fps 角标进 chip）、清晰度菜单点击时重建、BannerSlot 接管唯一横幅（A.3/A.4 断言行不改）、状态栏压到 1 消息+3 指标（音频/缓存/波形）、设置/在线/扫码/帮助/菜单全部进 Theme、`--dpi-test --empty` 补 M3 空态；门禁单壳：`ctest 37/37`、`--dpi-test` 4 DPI×{播放,全屏,窄窗} problems 空 + 4 DPI 空态、M4 仍 **64.17/64.17/64.42/56.64/60.80%**、`--banner-test 74/0`、`--key-test 77/0`、mark/note 0、`--snapshot-test 35/0`、`--readout-edit-test 28/0` + M7 四态 `rel=85,13 32x14`、drag P4 64.5fps/P6 p95 19.9ms、strategy 7/7、startup P1 759–857ms/P2 988–1100ms（MarkPanel 首次 current item 预热，见 §1 3D 条目）、M2 14.66/14.66/13.92/14.66/11.24/13.92/15.80/6.72:1；在线 Medium：M1 24/0（匿名 qn=32 blocked-quality）、longpress 54/0、parity 61/0、T8.6.1b 24/1（213–228ms 既有本机基线）、QR 19/0+UI 21/0、offline workspace 45/0、fault 92/0（ui_gap 220ms，3C 1009ms 既有）；SESSDATA 过期（`vip_status=-1`）→ M2/quality/vip 1080P 未跑；v6.54：**UI v2 3C 记录层（非门）**：新增 `ui/InspectorPanel`（头 26 三 tab + 计数徽标 + `+` 绑既有 `mark_action_` + 折叠；stack 三页 = 既有 MarkPanel 原对象 / 截图网格 / 信息表；页脚存储位置唯一常显；折叠 44 rail 竖排"标记 N / 截图 M"）+ MarkPanel `SetEmbeddedMode`（只 hide 自己的标题/存储/提示/表头，帧号改 accent 等宽；独立构造仍过）+ 截图网格两路数据源（在线 `index.jsonl` / 本地 `snapshots/*.png` + 新纯函数 `ParseSnapshotFrameFromName`）≤60 张、首次绘制懒加载、点击沿用 T5.2「先 UI 后 singleShot(0)」+ 信息页读 `session_->info()` + `TotalFrameCount` 三态 + 切 tab/折叠前 flush、`FocusNoteEditorForTest`/`ActivateMark` 自动展开到标记 tab（默认 tab=标记）；门禁新旧壳各一遍：`ctest 37/37`、`--dpi-test` 4 DPI problems 空（新壳每档 37 检查 + 折叠/展开/tab 用例）、M4 仍 **64.17/64.17/64.42/56.64/60.80%**、`--banner-test 74/0`、`--key-test 77/0`、mark/note 0（v2 新增 2 条 3C 用例、旧壳跳过）、`--snapshot-test 35/0`、fullscreen/loop/loop-end 全过、`--drag-test` P4 66.3–67.7fps / P6 15.6–22.3ms、strategy 7/7、`--startup-test` 187/414ms、`--readout-edit-test` 通过 + M7 四态 `rel=85,13 32x20`；在线 Medium：longpress 54/0、parity 61/0、T8.6.1b 24/0。**坑**：`--snapshot-test` 前清 `media/snapshots`；3B gate 脚本 `--tag` 应为 `--dpi-tag`。证据 [3C 交接](docs/handoff-2026-10-26-ui-v2-3c.md)；v6.53：**UI v2 3B 播放层（非门）**：Transport 收尾（帧号/时间码**内联输入** Enter/Esc/失焦/非法回滚、`v2SpeedButton` 0.25–2x 菜单、在线缓冲槽 `v2BufferSlot`、**四档宽度降级** ≥760/660–759/560–659/<560、tooltip 逐条照 §2.3）+ 新 `ui/RangeBar`（**视图窗 13px / 工作区 15px 两条滑块**，只读 `TimelineModel`，拖端缩放/改 A/B、拖中平移、滚轮/Shift 滚轮、双击适配/清除、全 NoFocus）+ 播放头常驻（越界贴边箭头）+ Fill 按实际高度派生（波形封顶 96）+ **删时间轴内部横向滚动条** + **删 250ms `ui_meta_timer_`**（改 `viewChanged/playheadChanged/loopRangeChanged` 信号驱动）；门禁**新旧壳各一遍全绿**：`ctest 37/37`、`--dpi-test` 4 DPI problems 全空 + M4 仍 **64.17/64.17/64.42/56.64/60.80%** + 每栏=令牌 + 新增 2 用例（档位切换 8 次落档 / 时间轴 340）、`--banner-test 74/0`、`--key-test 77/0`、mark/note 0、fullscreen/loop/loop-end 全过、`--drag-test` P4 66.0–96.1fps / P6 p95 15.4–20.4ms（tl=132 与 340 两种几何）、`--drag-strategy-test 7/7`、`--startup-test` 新壳 P1 161–181ms / P2 363–400ms；M5 **17/17**、M7 四态 `rel=85,13 32x20` 全一致；在线：longpress **54/0**、parity **61/0**（需 `--m1-throttle-bps 262144`）、T8.6.1b `checks=24/failures=1`（`forward_ui_gap_le_200ms` 204–239ms 超 200，**3A.5 原二进制同样 217ms 失败 → 本机既有，不是 3B 回归**；35 项含缓存命中相位，本轮未复现）。**坑**：`tests/timeline_test.cpp` 的 128/72、A/B 标签 y=42..53、次刻度 y=36.. 都依赖 `kFixed` 底部 14px（旧滚动条占位，改名 `kFixedBottomReserve` 保留）；本机 **CMake 重配崩 0xC0000409**（换 TEMP 无效，须整目录改名后全新配置）；`--banner-test` 的 `volume=` 语义改成"音量入口还在"（§3.3 收滑条但静音保留，narrow 相位 Transport 只有 502px）；**不要用 `Get-Content -Raw`/`Set-Content` 改带中文的源文件**（本会话曾把 `TransportBar.cpp` 读成 GBK 再写 UTF-8 → 乱码，只能重写）。证据 [交接](docs/handoff-2026-10-26-ui-v2-3b.md)；v6.52：**UI v2 3A.5 视觉对齐（用户拍板"按原型来"）**：新增 `ui/Glyph`（30 个 QPainter 矢量图标，无 qrc/SVG/图标字体）、`ui/TimelinePanel`（26px 面板头 + 工作区/视图/缩放）；`TransportBar` 补齐全按钮组并绑既有 QAction（首/±1/播放/末 │ A/B │ 速度/静音/音量 + 右侧循环/波形/标记/截图/全屏/检查器）、26px 等宽时间码；`TopBar` 图标 + 胶囊 chip + kbd 角标（`打开 Ctrl+O`/`在线 Ctrl+U`）+ 齿轮设置；面板卡片化（`#0e0e10` 工作区底 + `#17171a` 卡片 + 1px 边框 + 4px 圆角）、`#v2PlayButton` 实心 accent、1px 分组分隔符；**新增 [ui-v2-visual-reference.md](docs/ui-v2-visual-reference.md)（原型 = 唯一视觉参考稿：渲图命令/组件对照表/令牌配色/遗留差距），并在 §11 加规则 16**。门禁复跑：`--dpi-test` 4DPI×3相位 problems 全空、M4 仍 **64.2/64.2/64.4/56.6/60.8%**、banner 74/0、key 77/0、mark/note 0、startup/fullscreen 全过（旧壳 key 曾一次 flaky、复跑 2/2 通过）；M2 新皮肤采样 14.66/14.20/13.68/14.66/12.47/14.66/6.72:1 全 ≥4.5。**坑**：`QToolButton::setDefaultAction()` 在 action `changed()` 时把 icon 重置成 `action->icon()`（空）→ 自绘图标退化成"…"，改用 `Glyph::BindToolAction()`（单向同步 + clicked→trigger）；v6.51：**UI v2 3A 壳层完成（非门）**：新增 `src/app/ui/`（Tokens/Theme/PlayerShell/PlayerSplitter/TopBar/BannerSlot/StageStack/TransportBar 骨架）+ 三档密度（客户区高 ≥640/520–639/<520，20px 迟滞）+ `LAPIAN_UI_V2`/`--ui-v2`（两套布局互斥、旧壳一行不改）；做法是"搬不换"——InfoBar 仍是通知/规格/帧号状态源，`infoFile/infoFormat/infoTimecode/infoFrame/infoTotal/onlineFpsBadge/infoBannerRow` 与 `volumeSlider/cacheSettingsButton/qualityButton/MarkPanel/TimelineWidget` 原对象 reparent，故 `--banner-test` 8 键、`--online-quality-test --ui` 4 getter、mark/note 断言全不改；门禁新旧壳各一遍全绿：`ctest 37/37`、`--dpi-test` 4DPI×3相位 problems 空、M4 **64.2/64.2/64.4/56.6/60.8%**（门槛 62/62/62/55/59）+ 每栏=令牌±2、banner 74/0、key 77/0、mark/note 0、fullscreen 全过（v2 chrome appbar0/banner0/status0/inspector0/transport1/timeline1）、`d3d_init_at_startup=0`；M2 截屏采样 13.92/13.92/9.53/14.66/12.47/13.92/8.16:1、`text-meta` 5.24:1；**残余（必须查）**：新壳 `window.show()` 慢 ~0.5s（P1 268→773ms、P2 334→985ms，均 ≤1.5s/2.5s 门槛），已排除 QSS/Palette/字体/原生 HWND/布局循环，`-platform minimal` 20ms 说明在 Windows 平台首帧路径，未定位；另有空态×DPI 无自动化、Fill 波形比例临时值、拖分隔条 P4 未测、加载卡片未接在线、检查器折叠只判一次；证据 [交接](docs/handoff-2026-10-26-ui-v2-3a.md)；v6.50：**P0 发布收口完成（P0 范围）**：`925b8d5` 修复新缓存根首包 `bytes=0 ... 写入缓存文件失败`——根因是 DASH 总长探测 `FirstChunkSink` 主动停止被映射成 `kSinkError`，不是写盘；改 `headers_only` 只读响应头，3 个新根修复前各 2 条、修复后 0 条；`08484ee` 修复本地 `PreviewDecoder::CloneExactLocked` 两处无锁调用（`Process`/`PrefetchStep`）与 UI/预取线程缓存增删/AVFrame eviction 的竞态，修复前隔离 2/20 失败（0xC0000374/0xC0000005）、全量 ctest 首跑失败，修复后隔离 **50/50**、全量 `ctest` **3×37/37**；clean `-Clean -Package` **242s**、ctest 37/37、dist **25 文件 / 36,392,776B / 34.71MB**、exe **3,183,104B** SHA256 `8344A2BB…`、setup 11.9MB `C6F3B075…`、zip 14.49MB `8420FEA5…`；静默安装 exit 0/2s、安装版 hash 与 dist 一致、Start/桌面快捷方式 target+hash 一致、settings 未动；回归 P0 parity **61/0**（cold first_publish 26ms、post first_delivered 46ms、forward 56.94/backward 56.66fps、gap 31/32ms、presented 差 0.000、cache 60.75MiB/72、UI 31/32ms）、T8.6.1b **35/0**、A **42/0**、key **77/0**、local **9/0**、`--drag-test` P6 **18.0ms**、`--drag-strategy-test` **7/7**；**残余**：preview 崩溃无符号化 dump（按契约+压测收口，低概率未知）、4K/60 与 `post_seek_immediate` 未覆盖、用户体感非量化；证据 [tasks §9.9](docs/tasks-online-parity-fix.md)、[perf §13.8](docs/perf/online-drag-parity.md)、[交接](docs/handoff-2026-10-25-p0-close.md)；v6.49：**P0 在线拖动首次拖动修复 pass（P0 范围）**：`3084e33`（度量/探针）+ `c912770`（预览 seek 改 `SeekToKeyframePts`，fMP4 落点 offset -150→0；Resume 用当前显示帧 baseline；首张中间帧可发布；未缓存发最近缓存帧；`PrimeFrame`；`Warm` 静默预热并在完成后 idle 复用）+ `35fa8be`（MainWindow seek 落地 120ms/暂停 200ms 合并预热、warm timer 取消、cold/post-seek 正反门禁）；clean 重建 232.2s、ctest 37/37、dist **34.71MB/25**、Medium hash `D2CDEBDF…`；cold 首次拖动 first_publish **26–28ms**、post-seek 后第一次拖 first_delivered **48ms**/response 49ms、稳态 **58.53–58.71fps**/gap 32/33ms、presented **59/59（0.0%）**、UI 19–21ms、cache **60.75MiB/72 帧**；T8.6.1b 35/0、key 77/0、A 切档 42/0、本地 9/0、P6 23.8；**残余**：seek 落地 0ms 立即再拖 max_gap **640–655ms**（预热完成前窗口，不门禁）、cold 合成场景 flicker 13/14 为方向 artifact、本地反向 49.5–53.5fps 波动、drag-strategy 6/7 flaky、新根首写/下载重试；证据 [online-drag-parity.md](docs/perf/online-drag-parity.md) §13、[tasks-online-parity-fix.md](docs/tasks-online-parity-fix.md) §9、[交接](docs/handoff-2026-10-25-p0.md)；不改本地预览/门/schema、未上硬解；v6.48：**在线播放修复验收（partial）**：实现 `e1a89c6`（按实际 video/audio key 精确命中 DASH 缓存、只补缺失一路、kCompleted 降为内部信号）+ `4d89683`（在线拖动正/反向统一为“缓存→更近帧→精确后禁远帧”，72 帧/≤64MiB 有界缓存、反向预取、远跳先粗后精、松手精确）；从 HEAD `4d89683` clean Release 重建（222.4s，`ctest` 37/37）并用 Medium 副本复验：A 切档 qn=80→112→80、codecid=7、PTS 1,000,000、标记/记录/截图保留、切回 `cache_hit=1`/dash=playurl=0、42/0；B 正向/反向 presented **57/57（差 0.0%）**、landing 75/15 精确；C 稳态 first 17/31ms、空档 33/33ms、**56.21/56.72fps**（本地 58.53/57.00 的 96.0%/99.5%）、UI 32ms、max_missing 1，但**首次拖动 224ms/连续 6 个目标无新画面**超 100ms/“连续 2 个”门槛 → **C 未全达，判 partial，不写“完成/丝滑通过”**；`ctest` 37/37、key 77/0、T8.6.1b 35/0、P6 15.9/17.7ms、4K 只记录；探针 `response_p95` 恒 0（样本向量未填）、`landing_frame` 打印 PTS 为证据缺陷；证据 [online-quality-switch.md](docs/perf/online-quality-switch.md) §10、[online-drag-parity.md](docs/perf/online-drag-parity.md) §12、[交接](docs/handoff-2026-10-25-online-parity.md)；**DP-3 拖动交互策略 + 验收探针完成（1080p 保证档默认硬件路径）**：新增 `DragStrategy` 四模式（Exact ≤4 帧 / Fast / Reverse / FarJump ≥300 帧，按 step/方向/间隔/连续同向次数判定，阈值进诊断）；Exact 关闭粗帧替换，FarJump 用 `deferred_exact` 保证“先粗后精”，精确目标呈现后禁止粗帧替换；`PreviewTestDiagnostics` 增加 `drag_mode/reason/step/interval/speed/run/target_lag_frames|ms/cache_hit_rate/displayed_hardware`；新增 `--drag-strategy-test`。1080p 硬件：慢拖 +1/-1 lag p95 **0**（15.6ms 节奏 +1 p95=1/-1 p95=2）、`+60` 76.9–80.8fps/空档 34.7–37.2ms、`-60` 54.4–64.4fps/23.8–39.3ms、`-20` 47.3–49.2fps、`-1` 命中 84.7–85.5%、远跳粗帧 **20.8/20.6ms** 后精确 **30.6/30.0ms**；P4 94.9（本轮计时器粒度约 10ms）、P6 p95 18.8、P7 0.1/11.8、P8 12.8、P9 32.2；预览缓存峰值 233.4MiB、缩略图本轮 ≤432KB；`ctest` 37/37、key 77/0、T8.6.1b 35/0；强制软件快反向 `-20` 9.5fps/`-60` 31.4fps 为残余；不改 M1/M2/P4/P6/P7、OB、schema、默认 AVC≤1080p、在线预览；**DP-4 集成/发布完成（2026-10-24）**：从 `d586548` clean Release 重建，`ctest` 37/37、key 77/0、T8.6.1b 35/0；dist **34.55MB/25 文件**（36,227,912B）；Medium 副本 `--drag-test`/`--drag-strategy-test`/key 77/0/`--loop-test` 通过；重建后 `+60` 80.7fps、`-60` 50.4–57.5fps、`-20` 49.8fps、`-1` 命中 84.2%、P4 64.5/P6 22.3/P7 0.1/16.1/P8 12.9/P9 39.8，缓存峰值 233.4MiB；强制软件 `-20` 10.5fps/`-60` 33.4fps 与 4K 只记录为残余；zip/setup 已生成但未重装（E: SmartScreen 拦截）；不改门/数字、无代码改动；证据 [drag-preview-dp4.md](docs/perf/drag-preview-dp4.md)、交接 [handoff-2026-10-24-dp4.md](docs/handoff-2026-10-24-dp4.md)）

---

## 1. 当前状态

- **项目**：拉片软件，Windows 桌面端的帧级参考片审阅器。
- **阶段**：实现阶段，**M0 已完成**（T0.1 骨架/依赖锁定、T0.2 素材与测量、T0.3 精确 seek 探针）。T0.3 已按**方案 A** 决策：P7=±1；P6/P9 目标距前关键帧 ≤2s；长 GOP 按可用档。**T1.1–T1.6、T2.1–T2.10 + T2.9.1、T3.1–T3.7、T4.1–T4.2、T5.1–T5.4、T6.1–T6.5、T7.1、T7.2、T7.2.1** 代码完成；M1/M2/M3/M4 代码闭环，M5 标记/备注/截图、M6 全屏/键位/设置/DPI/状态横幅、M7 冷启动/内存与循环/画幅热修完成；**T7.3 soak 监测代码完成（`188c63e`/`31d0d94`/`852b68a`）；90min 单文件连续播放 `pass=1`（WS 150.9→153.1，+2.1MB），1h 同文件子区间循环也 `pass=1`（156.0→159.9，+3.9MB，23 次 B→A）；152s 循环的 +18.2/+20.8MB 未复现；V10 2h 未做，P14 未通过。T7.4（`40bba7f`）4K 路径：proxy-V4 硬解一轮 P4/P6/P7/P8/P9/P11 达到 4K 放宽档（P7 p95 2ms 级但每约 20 步有 ~410ms 缓存重填尖峰；seek 后 PgUp 单样本 201–213ms 超限，T7.9 已修 4K）；软解 P6/P9 未过**；T7.5 错误与鲁棒性：`--robust-test` 75 项/0 失败（6.84s）、只读目录 ACL 17/17、7 类坏文件 ≤34ms、不可达 UNC 3.03s（修复前 21s）、288 字符长路径 sidecar/截图通过、设备丢失模拟恢复；T7.6 打包/关联/许可/体积：`scripts/package.ps1`/`installer.iss` 每用户安装（非管理员、`%LOCALAPPDATA%\Programs\拉片软件`）、ProgID/4 扩展名 OpenWithProgids（不设默认）、便携 zip 13.65MB/setup 11.31MB、卸载清理且保留 settings/marks/sidecar；T7.8 基准回归：`scripts\bench-regression.ps1` + 阈值/基线，8 套件；T7.9 收口 P7 seek→PgUp（`d1c7369`，60 项/0 失败、30/30 容差、27/30 严格 ±10%；4K 2s GOP spot 206→0.06ms；基线 `dev-2026-10-22-t7.9`）；`ctest` 24/24、`dist` 32.89MB/25 文件；**可达 SMB 共享、物理设备丢失、真实 V8 未验；F1-A1 仅本机热缓存预检；P1/P2 热缓存；1080p 旧 372ms 经查是 10s GOP 关键帧边界（不属 80ms），4K 2s GOP 已修至 0.06ms；T7.11 P7 跨 GOP 边界实测（修复前）：2s GOP p95 145.60ms、1s 92.65ms；T7.11.1（`7af822e`）已用延后回拷修复为 2s **64.13ms**、1s **27.11ms**、1080p60 HEVC 2s 67.31ms，连续后退 p95 0.03/0.06ms；全量 60 项/0 失败、30/30 容差；2s 余量 15.9ms，基线机待 T7.7**；V1–V8 真素材手测待补；T7.10 合成 V1–V10 片单已生成，合成 V10 2h soak `v10-synth` `pass=1`（WS 162.4→164.8MB、包络 0.16MB/h、handles 691→675、threads 56→50、frame_stall=0、underruns=0、ui_gap 212ms；替身 P14 稳定性，不是正式 P14）；T7.10 的 V1–V9 冒烟 18/18；**T7.7 执行清单与预检脚本已入库**（`docs/perf/T7.7-acceptance-checklist.md`、`scripts/t7.7-preflight.ps1`），当前 `verdict=blocked`（真实 V1–V10 0/10、非基线机、冷缓存/Explorer 未做），本机 P7 2s 复跑 65.33ms（余量 14.67ms）、1s 30.07ms（short_gop pass=1），仍不写 T7.7/P1–P14 通过。**2026-10-22 用户决定：手头没有真实 V1–V10/基线机，停止正式 T7.7/P14 验收，按“开发完成、资源阻塞、未验收”的内部预览收口；T7.7 保持 blocked，不标完成、不宣称通过。** 预览说明见 [docs/preview-release-2026-10-22.md](docs/preview-release-2026-10-22.md)。**新对话先读 [docs/handoff-2026-10-22-t7.7.md](docs/handoff-2026-10-22-t7.7.md)**。
- **v2 在线（S1/S2/S3 完成；T8.0–T8.14 + **T8.16** 完成；T8.6.1a 完成；T8.6.1b 完成（非门）；T8.3.1 完成；**T8.15 完成（partial，非 MVP 门）**；DEC-1…**6** 已拍板；**M1 于 T8.8 通过；M2 于 T8.14 通过（六条硬条件）**；**T8.16 打包/隐私/卸载完成（pass，非 MVP 门）**；**T8.15 已执行并判 partial（2026-10-24；默认凭据 `nav vip_status=1`；A3 无 HEVC/AV1-only 60 样本 unverified、720P60 未返回）**；T8.2.2 真实扫码未验证）**：v2 做"输入 BV → 在线播放 → 逐帧研究"，保留本地模式。入口 [product-v2-vision.md](docs/product-v2-vision.md)、[ADR-007](docs/adr/ADR-007-bilibili-online.md)、[S2 spike 报告](docs/perf/v2-online-spike.md)、[S1/S2 交接](docs/handoff-2026-10-23-s1s2.md)、**[S3 增量规格](docs/spec-v1.0-delta.md)**、**[v2 任务](tasks-v2.md)**、[S3 交接](docs/handoff-2026-10-23-s3.md)、**[T8.1 证据](docs/perf/v2-online-resolve.md)**、[T8.1 交接](docs/handoff-2026-10-23-t8.1.md)、**[T8.2 证据](docs/perf/v2-online-http-selfcheck.md)**、[T8.2 交接](docs/handoff-2026-10-23-t8.2.md)、**[T8.3 证据](docs/perf/v2-online-cache.md)**、[T8.3 交接](docs/handoff-2026-10-23-t8.3.md)、**[T8.4 证据](docs/perf/v2-online-growing.md)**、[T8.4 交接](docs/handoff-2026-10-23-t8.4.md)、**[T8.5 证据](docs/perf/v2-online-m1.md)**、[T8.5 交接](docs/handoff-2026-10-23-t8.5.md)、**[T8.2.2 证据](docs/perf/v2-online-qr.md)**、[T8.2.2 交接](docs/handoff-2026-10-23-t8.2.2.md)。关键实测：普通用户登录只到 1080P30，1080P60/4K 不可得；可增长读层已验证；HTTP 栈必须自检（同一 Cookie：curl 通过、.NET -101，**T8.2 已用产品栈 WinHTTP 通过**）。**S3 未改 v1 冻结文件/产品代码；ADR-007 仅加“附条件接受”评审记录，D1–D5 未改。B 与 DEC-1…6 已全部拍板（M1→M2；5s/80ms/2.5s/100ms/80%；首次在线打开自检；横幅+角标；300MB+20GB 记账单测；**DEC-6 扫码登录并入 v2，非门**）；T8.1 解析层已完成（BV/URL→匿名 view→回显确认；156/0）；T8.2 已完成（WinHTTP nav 5 次 161–202ms、`cookie_sent=1`、`vip_status=0`；SESSDATA/DPAPI 凭据 UI、DEC-3 首次提交自检、日志脱敏、`--online-http-selfcheck`/`--online-auth-test`；libcurl 未构建）；T8.3 缓存存储已完成（`CacheStore`：默认根 `%LOCALAPPDATA%\拉片软件\cache`、meta.json、`.part`→`.mp4` 原子改名、20GiB 可配置配额 + LRU、空间预检、续传、损坏/重建、清空/孤儿、跨会话、长路径；300MB LRU 与 20GB 记账单测；`lapian_online_cache_test` 366/0、`--online-cache-test` 32/0；不联网、不含读层/下载/UI）；**T8.4 可增长读层已完成**（`GrowingSource` + 每读者 `GrowingCursor`/64KB `AVIOContext` + `GrowingFormat`；0.3MB faststart 出首帧（open 9ms/首帧 16ms/实读 131072B）、100/100 seek 落点 0、已缓存等待 0、未缓存等待 2073ms vs 预期 1750ms（+18.5%）、取消 0.15/0.24ms、4 读者 10min `deadlocks=0/lost_wakeups=0`、close/WS 有界、5s 等待 CPU 0ms、EOF/EIO/EXIT 三分、淘汰 busy；`lapian_growing_source_test` 157/0、`--online-growing-test` 22/0；不联网、不接 UI、不改 CacheStore 写语义）；**T8.5 durl 取流已完成**（`PlayUrl` + `DownloadTransport` + `DownloadJob`：playurl(fnval=0) 实际 qn、顺序下载、Range 续传、3 备份 URL、300ms/1s/2s 重试、URL 过期刷新一次、写盘后 `GrowingSource::Publish`、关闭写句柄后 `MarkComplete`；真实 OB-1/OB-3=720P，**OB-2 实际 360P（qn=16）→ M1 3/3 blocked**；三样本首帧 8–9/10–14ms、ffprobe pass；取消 7ms、`_Exit` 后续传 5,242,880B、真实 403 备份/刷新通过；`lapian_online_fetch_test` 94/0、`--online-fetch-test` 46/0、`ctest` 27/27；不接 UI）。**T8.2.2 扫码登录已完成（非门）**（`QrLogin` 官方 passport generate/poll、`QrCode` + vendored MIT qrcodegen 本地渲染、`QrLoginController` 1.5s/180s/取消(≤100ms)/nav 门/DPAPI、`OnlineResolveDialog` 扫码入口且粘贴保留；`lapian_online_qr_test` 115/0、`--online-qr-test` 19/0（`--ui` 21/0，dist 同）；**真实扫码未验证（无用户手机）**，不写 O8-A8）。**T8.6 在线会话/UI 已完成**（`OnlineSession` + MainWindow 入口；`OpenGrowing`/`OpenWithFormat`/`ProbeFormat`；部分索引/异步 seek/未缓存等待；音频/波形渐进；Ctrl+M/S；defer finalize/关窗；真实 3 条 E2E、冷缓存首帧 20×3 p95 624/725/459ms、P7 p95 20/20/25ms、seek p95 79/59/97ms、UI gap 60/61/62ms、缓冲耗尽/取消/错误通过；`ctest` 29/29；本地 P1/P2 290.68/347.43ms、P7 66.59ms；dist 33.57MB/25；**OB-2 仍 360P，M1 3/3 720P blocked，不判 M1**；证据 [v2-online-session.md](docs/perf/v2-online-session.md)、交接 [handoff-2026-10-23-t8.6.md](docs/handoff-2026-10-23-t8.6.md)）。**T8.7 缓存命中/在线记录已完成**（`OnlineSession::Start` 先 `CacheStore::Scan`+`OpenComplete`，命中直接本地打开 `data.mp4`，0 playurl/CDN/`.part`；`lapian-online-1` 记录 `%APPDATA%\拉片软件\online\<bvid>-<cid>.lapian.json`；截图 `Pictures\拉片软件\<bvid>\` + `index.jsonl`；清缓存保留/重建回访/损坏不阻塞/换档 PTS 映射；`--online-offline-test` workspace 45/0、真实缓存 44/0、Medium 默认 AppData 31/0×3（首帧 74–84ms、0 TCP/UDP、无 winhttp/wininet/urlmon）；`lapian_online_records_test` 52/0；`ctest` 30/30；本地 P1/P2 296.81/345.40ms、P7 跨 GOP 66.56ms；dist 33.68MB/25；**OB-2 仍 360P，M1 3/3 720P blocked，不判 M1**；证据 [v2-online-cache-hit.md](docs/perf/v2-online-cache-hit.md)、交接 [handoff-2026-10-23-t8.7.md](docs/handoff-2026-10-23-t8.7.md)）。**T8.8 M1 门完成并通过**（qn/fourk 矩阵 20 单元：OB-2 在 qn=64 为 360P、qn=80/112/120 为 1080P，fourk 无影响；最小修正是 `kM1RequestQn=80`、fourk=0、刷新同档；3/3 实际 qn=64/80/64、ffprobe pass；OP1 p95 648/532/527ms、OP2 p95 34/51/34ms、OP3 提示/取消 0ms+落点 0、OP5 p95 95ms/0 请求；续传/清缓存/DEC-5/本地 O9 全过；`ctest` 30/30、dist 33.69MB/25；证据 [v2-online-m1.md](docs/perf/v2-online-m1.md) §11–§17、交接 [handoff-2026-10-23-t8.8-m1.md](docs/handoff-2026-10-23-t8.8-m1.md)）。**T8.6.1a 在线长按已完成（非门）**（同步 `current_frame_` 检查改为异步步进状态机：已缓存单格排队不跳帧、未缓存最新目标重定向/取消、首末帧边界、短按/快速双击、外来 seek 从已请求目标 ±1；重建会话保留部分索引；`--online-longpress-test` 50 项/样本；真实 3 样本×2 轮 Medium 副本 + 构建目录 3 轮均 49–50/0、2s 25–26 帧 bad=0、UI gap 37–77ms、未缓存提示 0ms、取消 4–17ms、回缓存 31–51ms、关窗 377–587ms；本地 key-test 77/0、ctest 30/30、M1 E2E 28/0 与 landing 29/0、P1/P2/P7 279.03/361.57/65.52ms；证据 [v2-online-interaction.md](docs/perf/v2-online-interaction.md)、交接 [handoff-2026-10-24-t8.6.1a.md](docs/handoff-2026-10-24-t8.6.1a.md)）。**T8.6.1a 实机回归 hotfix 完成**（未缓存改单格排队/伪 release 守卫/拖动 clamp；用户样本 54/0；慢拖预览仍 T8.6.1b）；**用户确认 C 盘安装版长按正常，T8.6.1a 验收闭环**。**T8.9 DASH 解析与档位已完成（M2 第 1 项，非门）**：`playurl(fnval=4048)`/`dash.video[]/audio[]` 解析（qn/codecid/fps/带宽/size=未知/init+index/备份 URL）、AVC 默认/O6 顺序/60fps+4K 只手动、档位菜单（DASH 档置灰写 T8.10、页脚“菜单可选不等于保证可达”）；登录普通用户 **3/3 DASH 1080P AVC（qn=80）**、fps 30.000/29.412/25.000；116/74/120 **3/3 不可得**；`quality` 撒谎（匿名 qn=80 → quality=64、dash 只有 16/32）；durl 1080p 仍 1/3（不作保证）；`lapian_online_dash_test` 117/0、`ctest` 32/32、`--online-quality-test` 3×27/0 + `--ui` 21/0、M1 E2E 28/0（first_frame 640ms/UI gap 59ms）、缓存管理 72/0+98/0、key-test 77/0、dist 34.02MB/25；证据 [v2-online-quality.md](docs/perf/v2-online-quality.md)、交接 [handoff-2026-10-24-t8.9.md](docs/handoff-2026-10-24-t8.9.md)。**T8.10 DASH 双输入已完成（M2 第 2 项，非门）**：视频/音频各自 `GrowingSource`/`DownloadJob`/`AVFormatContext`、音频主时钟、seek fan-out、DASH 缓存命中、durl 回退；`--online-m2-test` 3/3 **34/0**（首帧 322.2/369.0/365.1ms、60s 音画偏差 p95 17.1/18.2/22.0ms、20/20 seek 帧号+PTS 精确、EOF 末帧停住、内存峰值 211.5/209.7/201.7MB、缓存命中 45–46ms/0 网络）；音频缓存键 `audio-<id>`（旧 meta 兼容）；**修复 fMP4 seek 落前一 GOP**（`SeekToKeyframePts`+PTS 校验）；`--force-durl` M1 28/0、DASH M1 25/0、`ctest` 32/32、缓存 396/0、管理 72/98、key 77/0；证据 [v2-online-m2.md](docs/perf/v2-online-m2.md)、交接 [handoff-2026-10-24-t8.10.md](docs/handoff-2026-10-24-t8.10.md)。**T8.11 渐进索引/波形/总帧数完成（M2 第 3 项，非门）**：OB-3 真实在线 27/0（首次索引 128ms、30s 内容 6.209s、20/20 seek 0 落点、完成 exact 5306、波形 0.525 帧、UI gap 58ms、read_ratio 1.000）。**T8.12 档位策略/手动切档/降帧提示完成（M2 第 4 项，非门）**：`OnlineRequest.requested_qn/requested_codecid`（默认 0→80，M1 URL 字节不变）、DASH 请求档缺失明确失败、缓存命中匹配请求档；`DefaultQn` 允许 AVC 60fps 自动、HEVC/AV1-only 60fps 只手动、4K 始终只手动；`QualityMenuContext` 权限列档/实际 qn/未登录 480P；`FrameRatePolicy` 标题 60fps 证据、dash_fps 实际值、普通 30/25 不误报、无证据只“可能降帧”；`MainWindow` 切档（停旧流/重取/索引波形/PTS 回位/标记保留/实际上下文）；`InfoBar` DEC-4 可关横幅 + 常驻角标；OB-2 29.412fps 横幅/角标，OB-1/2/3 1080→720 成功（qn=64、PTS 1,000,000→第 30 帧、标记 1→1、截图 qn64/codecid7/fps29.412）、菜单点击 61/75/76ms；真 60/4K 不可得/未验证；`ctest` 34/34、dash 125/0、frame-rate 27/0、quality 3×45/46/45、UI 50/56/50、M2 34/0×3、M1 28/0、index 27/22/28、长按 54/0、缓存 396/0、管理 75/0+72/0+98/0、key 77/0、dist 34.21MB/25；**T8.13 网络异常/并发加固完成（M2 第 5 项，非门，详见 [证据](docs/perf/v2-online-faults.md)/[交接](docs/handoff-2026-10-24-t8.13.md)）**；**T8.14 M2 门完成（pass，六条硬条件；3/3 DASH 1080P30、OP1–OP5、失败矩阵、O9/P1/P2/P7、内存/审计；残余 teardown 0xC0000374/seed 卡住未定位，不写无崩溃；详见 [证据](docs/perf/v2-online-m2.md)/[交接](docs/handoff-2026-10-24-t8.14-m2.md)）**；**T8.16 打包/隐私/卸载完成（pass，非 MVP 门；安装/卸载 0 TCP/UDP、P12、Medium、默认缓存/凭据、卸载缓存清理；未真删 20GB）**；**T8.6.1b 完成（非门）；T8.15 完成（partial，非 MVP 门；1080P60 AVC 自动/OP9 p95 47.5ms、4K 仅手动/能播/逐帧、60/4K→1080P30 切档；A3 unverified）**；**T8.3.1 缓存管理已完成（非门）**：设置页自选目录/配额/打开目录/清空/孤儿清理，清空保留当前会话/在线记录/截图；非门后续 T8.6.1a/T8.6.1b/T8.3.1 已清空；不改 M1/M2 门与数字。
- **拖动预览 v2（DP，2026-10-24，非门）**：DP-0 规划、DP-1 硬件预览完成；**DP-2 反向缓存/预取/远跳粗帧完成（1080p 保证档默认硬件路径，2026-10-24）**：新增 `PreviewRingCache`（软件 NV12，192MiB）与 `PreviewThumbnailCache`（320×180，64MiB），32 帧热缓存保留；`Begin/UpdateTarget` 先查精确缓存，局部慢速反向由独立低优先级预取线程解上一 GOP，远跳先发粗帧（软件缩略图/硬件关键帧全分辨率 clone）再精确；1080p `-60` 60.2–69.2fps/空档 25.2–33.0ms、`-20` 45.7–48.7fps、`+60` 68.7–79.5fps/26.2–48.0ms、`-1` 局部反向命中 84.8–86.4%；远跳首帧 20–41ms（软件粗帧 19.9–40.7；硬件精确首帧 20–21ms）；P4 64.1fps、P6 16.8ms、P7/P8/P9 不退化；预览缓存峰值 233.4MiB、缩略图 ≤28.8MB；`ctest` 37/37、key 77/0、T8.6.1b 35/0；强制软件快反向 `-20` 9.9–11.4fps/`-60` 30.3–33.0fps 为残余；证据 [drag-preview-dp2.md](docs/perf/drag-preview-dp2.md)、交接 [handoff-2026-10-24-dp2.md](docs/handoff-2026-10-24-dp2.md)；**DP-3 交互策略 + 验收探针完成（2026-10-24）**：`DragStrategy` 四模式（Exact ≤4 帧 / Fast / Reverse / FarJump ≥300 帧，按 step/方向/间隔/连续同向次数判定）；Exact 关闭粗帧，FarJump `deferred_exact` 先粗后精，精确目标后禁粗帧；`PreviewTestDiagnostics` 增加 `drag_mode/reason/step/interval/speed/run/target_lag_frames|ms/cache_hit_rate/displayed_hardware`，新增 `--drag-strategy-test`。1080p 硬件：慢拖 +1/-1 lag p95 **0**、`+60` 76.9–80.8fps/空档 34.7–37.2ms、`-60` 54.4–64.4fps/23.8–39.3ms、`-20` 47.3–49.2fps、`-1` 命中 84.7–85.5%、远跳粗帧 20.8/20.6ms→精确 30.6/30.0ms；P4 94.9（计时器约 10ms）、P6 p95 18.8、P7 0.1/11.8、P8 12.8、P9 32.2；`ctest` 37/37、key 77/0、T8.6.1b 35/0；强制软件 `-20` 9.5fps/`-60` 31.4fps 为残余；证据 [drag-preview-dp3.md](docs/perf/drag-preview-dp3.md)、交接 [handoff-2026-10-24-dp3.md](docs/handoff-2026-10-24-dp3.md)；**DP-4 集成/发布完成（2026-10-24）**：从 `d586548` clean Release 重建，`ctest` 37/37、key 77/0、T8.6.1b 35/0；dist **34.55MB/25 文件**；Medium 副本 `--drag-test`/`--drag-strategy-test`/key/`--loop-test` 通过；重建后 `+60` 80.7fps、`-60` 50.4–57.5fps、`-20` 49.8fps、`-1` 命中 84.2%、P4 64.5/P6 22.3/P7 0.1/16.1/P8 12.9/P9 39.8；缓存峰值 233.4MiB、`readbacks=0`；强制软件 `-20` 10.5fps/`-60` 33.4fps 与 4K 只记录为残余；zip/setup 已生成但未重装；不改门/数字、无代码改动；证据 [drag-preview-dp4.md](docs/perf/drag-preview-dp4.md)、交接 [handoff-2026-10-24-dp4.md](docs/handoff-2026-10-24-dp4.md)；不改 M1/M2/P4/P7、OB、schema、默认 AVC≤1080p、在线预览；缓存 ≤256MB、缩略图 ≤64MB，不做全片缓存。
- **本地拖动预览热修（2026-10-24，用户反馈，非门）**：`PreviewDecoder` 把目标版本与拖动会话分离，正向追最新目标/不中断、反向解完当前目标并允许粗帧；新增 `--drag-fast`。`media\测试.mp4`（1080p H.264 30fps、GOP 30 帧）快拖 +60 帧/16ms 从 **28.6fps/空档 93.8ms** → **61.0fps/31.7ms**，反向快拖 **42.2fps/48.5ms**；P4 61.3fps、P6 16.6ms、ctest 36/36、key 77/0；反向小步 -20 仍 11.5fps 为残余；未改 P4/P7 数字、M1/M2 门、在线缓存/OB/schema；证据 [local-drag-fast-2026-10-24.md](docs/perf/local-drag-fast-2026-10-24.md)、交接 [handoff-2026-10-24-drag-fast.md](docs/handoff-2026-10-24-drag-fast.md)。
- **在线拖动 P0（2026-10-25，非门，P0 范围 pass）**：`3084e33` 每次拖动度量/探针；`c912770` 预览 seek 改 `SeekToKeyframePts`（fMP4 落点 offset -150→0）、Resume 用当前显示帧 baseline、首张中间帧可发布、未缓存发最近缓存帧、`PrimeFrame`、`Warm` 静默预热并 idle 复用；`35fa8be` MainWindow seek 落地 120ms/暂停 200ms 合并预热、warm timer 取消、cold/post-seek 正反门禁。clean 重建 232.2s、ctest 37/37、dist 34.71MB/25、Medium hash `D2CDEBDF…`；cold 首次拖动 first_publish **26–28ms**、post-seek 后第一次拖 first_delivered **48ms**/response 49ms、稳态 **58.53–58.71fps**/gap 32/33ms、presented 59/59（0.0%）、cache 60.75MiB/72、UI 19–21ms；T8.6.1b 35/0、key 77/0、A 42/0、本地 9/0、P6 23.8；残余：seek 落地 0ms 立即再拖 max_gap 640–655ms（不门禁）、cold 合成场景 flicker artifact、本地反向 49.5–53.5fps 波动、drag-strategy 6/7 flaky；证据 [online-drag-parity.md](docs/perf/online-drag-parity.md) §13、[tasks-online-parity-fix.md](docs/tasks-online-parity-fix.md) §9、[交接](docs/handoff-2026-10-25-p0.md)。
- **P0 发布收口（2026-10-25，P0 范围，HEAD `08484ee`）**：`925b8d5` 修复新缓存根首包 `bytes=0 ... 写入缓存文件失败`（实为 DASH 总长探测 `FirstChunkSink` 主动停止被映射成 `kSinkError`；改 `headers_only` 只读响应头，3 新根修复前各 2 条→修复后 0 条）；`08484ee` 修复 `PreviewDecoder::CloneExactLocked` 两处无锁调用（`Process`/`PrefetchStep`）与 UI/预取线程缓存增删/AVFrame eviction 的竞态（修复前隔离 2/20 失败、全量 ctest 首跑失败；修复后隔离 50/50、全量 ctest 3×37/37）；clean 重建 **242s**、ctest 37/37、dist **25 文件/36,392,776B/34.71MB**、exe `8344A2BB…`、setup 11.9MB/zip 14.49MB；静默安装 exit 0/2s、安装版 hash 与 dist 一致、Start/桌面快捷方式 target+hash 一致、settings 未动；回归 parity **61/0**（cold first_publish 26ms、post 46ms、56.66–56.94fps、cache 60.75MiB/72）、T8.6.1b **35/0**、A **42/0**、key **77/0**、local **9/0**、drag P6 **18.0ms**、strategy **7/7**；**残余**：preview 崩溃无符号化 dump、4K/60 与 `post_seek_immediate` 未覆盖、用户体感非量化；证据 [tasks §9.9](docs/tasks-online-parity-fix.md)、[perf §13.8](docs/perf/online-drag-parity.md)、[交接](docs/handoff-2026-10-25-p0-close.md)。
- **UI v2 3A 壳层（2026-10-26，非门）**：新增 `src/app/ui/`（`Tokens.h`/`Theme`/`PlayerShell`/`PlayerSplitter`/`TopBar`/`BannerSlot`/`StageStack`/`TransportBar` 骨架）+ 三档密度（按**客户区高度** ≥640 / 520–639 / <520，20px 迟滞）+ `LAPIAN_UI_V2=1`/`--ui-v2` 开关（main.cpp 构造前读一次，两套布局互斥；**旧壳一行不改**，可回退）。做法是"搬不换"：`InfoBar` 仍是通知/规格/帧号唯一状态源（`media::StatusNotice` 语义未动），把 `infoFile/infoFormat/infoTimecode/infoFrame/infoTotal/onlineFpsBadge/infoBannerRow` 与 `volumeSlider/cacheSettingsButton/qualityButton/MarkPanel/TimelineWidget` **原对象 reparent** 进 TopBar / Transport / BannerSlot / 检查器 / 时间轴面板，因此 `--banner-test` 的 8 个字符串键、`--online-quality-test --ui` 的 4 个 getter、`--mark-test`/`--note-test` 断言全部不用改。空态用 `StageStack` page1 卡片（GfxWidget hide、D3D11 惰性到首次打开：`d3d_init_at_startup=0`），错误态在无画面时切卡片、有画面时保留 page0。门禁**新旧壳各跑一遍全绿**：`ctest 37/37`、`--dpi-test` 4 DPI（100/125/150/200%）× 3 相位（normal/fullscreen/narrow）`problems` 为空 + 新 M4 实测 **64.2 / 64.2 / 64.4 / 56.6 / 60.8%**（门槛 62/62/62/55/59）+ 每栏 = 令牌 ±2（38/46/132/24、34/40/112/22、30/36/96/20，handle 6）、`--banner-test` **74/0**、`--key-test` **77/0**、`--mark-test`/`--note-test` **0 失败**、`--fullscreen-test` 全过（v2 `chrome=appbar0/banner0/status0/inspector0/transport1/timeline1`）、M2 截屏采样 13.92/13.92/9.53/14.66/12.47/13.92/8.16:1（≥4.5，`text-meta` 5.24:1）。**残余（必须查）**：新壳 `window.show()` 比旧壳慢 ~0.5s（P1 268→773ms、P2 334→985ms，均 ≤1.5s/2.5s 门槛，M8 按阈值通过但"不退化"存疑）；已排除全局 QSS / QPalette / 应用字体 / GfxWidget 原生 HWND / 布局与分隔条循环（`split_apply` 6 次、TopBar resize 1 次），`-platform minimal` 下只有 20ms、100% 与 200% 同值（788ms）说明与像素数无关、是随控件数增长的 polish/布局成本，**未定位**；该 +0.5s 是**一次性启动成本**（窗口首次显示前），不在交互路径：同一素材 `--drag-test` 新旧壳各一遍，P4 预览 **96.0 vs 95.2fps**、P6 p95 **19.4 vs 26.2ms**、P7 0.1/12.0、P9 p95 **29.4 vs 37.6ms**、P8 长按 12.9fps/26 帧 pass（旧壳本轮 3.9fps/8 帧 pass=0，属旧壳既有偶发，不算 3A 回归）；另有：空态 × DPI 相位无自动化（`--dpi-test` 需要 `--file`）、时间轴 Fill 的波形比例是临时值、拖分隔条时 P4 未测、加载卡片未接在线流程、检查器自动折叠只判一次、图标暂用文字按钮。证据/复现命令 [交接](docs/handoff-2026-10-26-ui-v2-3a.md)。**3A.5 视觉对齐（同日，用户拍板"按原型来"）**：第一版只按规划数字搭骨架、观感朴素；现按 [ui-v2-visual-reference.md](docs/ui-v2-visual-reference.md)（**原型 = 唯一视觉参考稿**）补齐 `ui/Glyph`（30 个 QPainter 图标）、`ui/TimelinePanel`（26px 面板头 + 缩放）、Transport 全按钮组（绑既有 QAction）、TopBar 图标/胶囊 chip/kbd 角标、面板卡片化（`#0e0e10` 底 + `#17171a` 卡片 + 4px 圆角）、实心 accent 播放键；门禁复跑 dpi 4 档 problems 全空、M4 不变、banner 74/0、key 77/0、mark/note 0；**坑**：`setDefaultAction` 会清自绘 icon（退化成"…"），改 `Glyph::BindToolAction`。
- **UI v2 3B 播放层（2026-10-26，非门）**：Transport 收尾 + 时间轴两条滑块 + 画布收尾 + 去轮询。
  - **Transport**：时间码**内联输入**（点 `v2Timecode` → `v2ReadoutEdit`，`h-input` 22px；Enter 提交 / Esc 取消 /
    失焦提交 / 非法输入回滚；解析复用 `ApplyGotoText` 的帧号 + `HH:MM:SS:FF`；输入框接受 `ShortcutOverride`
    让全局键让位，与 MarkPanel 同一机制）；次读数改 `v2FrameNumber`/`v2FrameTotal`（标签本身叫 `v2Timecode`，
    外层 slot 改名 `v2TimecodeSlot`）；`v2SpeedButton`（点击时建 0.25–2x 菜单，复用 `core::kPlaybackRates`，
    `[`/`]` 不变，文本仍由 MainWindow 的 `UpdateSpeedText()` 给）；`v2BufferSlot`/`v2BufferText`（只在在线
    `buffering/stalled` 显示，`已缓存 x/y MB` + 进度条，本地隐藏，数据来自既有 `bufferingChanged`/`OnlineProgress`，
    **未改 OnlineSession**）；**四档宽度降级**按 Transport 自身宽度（≥760 全量 / 660–759 收音量 / 560–659
    收 A/B+速度且读数 26→22 / <560 再收首末帧·标记·截图；**帧号读数任何档位都保留**）；tooltip 逐条照 §2.3。
  - **时间轴**：新增 `ui/RangeBar`（视图窗 13px 行高/4px 轨道、工作区 15px/10px）——拖端=缩放/改 A/B、拖中=平移、
    点空白=居中/平移、滚轮=以播放头为锚缩放、Shift+滚轮=平移、双击=适配/清除 A/B，全 NoFocus；**只读
    `TimelineModel`，不存第二份视图状态**（`TimelineViewFramesForTest()` 把"画布可见范围"与"滑块窗口"一起打印）。
    面板 = 头 26 + 视图条 13 + 工作区条 15 + 画布 78（总高仍 132/112/96）；**画布左右各收 8px**，
    与滑块轨道的 8px 内边距严格一致（用户 2026-10-26 反馈"工作区两端对不准画布"→ 只给滑块缩进会让
    两端各差 8px；改成"画布往里收"后两侧都是 `8 + ratio×(面板宽−16)`，`TimelineBarAlignForTest()`
    断言 |dx| ≤ 1，实测 −1/+1）。播放头常驻、越界贴边箭头；
    Fill 模式标尺/刻度/标记带/A-B 标签按实际高度派生、波形封顶 96；**删除内部横向滚动条**
    （`timelineScrollBar`/`kScrollBarHeight` 全仓 0 处）。
  - **去轮询**：删 `ui_meta_timer_`（250ms）→ `TimelineWidget::viewChanged/playheadChanged/loopRangeChanged` +
    `PlayerShell::inspectorCollapsedChanged` 驱动面板头/循环按钮/工作区滑块。
  - 门禁：见 §12 的 v6.53 行与 [交接](docs/handoff-2026-10-26-ui-v2-3b.md)。**M5 17/17**（运行期 tooltip 全表）、
    **M7 四态 `rel=85,13 32x20` 全一致**、M4 数字未动、P6 ≤20.4ms、P4 不退化（tl=132/340 两种几何各一遍）。
- **UI v2 3C 记录层（2026-10-26，非门）**：检查器三 tab + 截图网格 + 信息页 + 折叠/聚焦联动。
  - **新增 `ui/InspectorPanel`**：头 26（`v2TabMarks/v2TabShots/v2TabInfo` + 计数徽标 `v2MarkBadge/v2ShotBadge`
    + `v2InspectorAdd` 绑既有 `mark_action_`（M5 第 13 行第二入口）+ `v2InspectorCollapse`）、
    `v2InspectorStack` 三页（page0 = **既有 MarkPanel 原对象** / page1 = `v2ShotGrid` / page2 = `v2InfoTable`）、
    页脚 `v2InspectorFooter+v2StoreText`（存储位置**唯一常显处**）、折叠 44（`v2InspectorRail` 竖排
    "标记 N / 截图 M" + `v2InspectorExpand`）。全 NoFocus（tab/按钮），只有列表与编辑框可聚焦。
  - **MarkPanel "搬不换"**：原对象/objectName/信号不变；新增 `SetEmbeddedMode` 只在嵌入时隐藏自己的标题/
    `markStorage`/提示/表头，帧号列改 accent 等宽左对齐，MarkStore 自动"帧 N"标签不再重复；独立构造
    （`lapian_mark_panel_test`）行为不变、仍过。
  - **截图网格**：在线读 `<Pictures>/拉片软件/<bvid>/index.jsonl`（回退 `%APPDATA%\拉片软件\online\snapshots/<bvid>`），
    本地扫 `<视频目录>/snapshots/*.png` + `<Pictures>/拉片软件/*.png` 并用新纯函数
    `snapshot::ParseSnapshotFrameFromName()` 反解帧号 + 视频名前缀过滤；≤60 张（徽标显示真实总数，
    超出 `v2ShotNote` 说明）；缩略图**首次绘制才读盘**（`QImageReader`，无后台线程/定时器）；
    点击沿用 T5.2 顺序（先重绘 UI，再 `singleShot(0)` 精确 `GoToFrame`）。空态"还没有截图（S）"不铺网格。
  - **信息页**：文件/格式/分辨率/视频/帧率（标称+实际）/时长/总帧数（复用 `TotalFrameCount` 三态）/
    音频/旋转/VFR/路径；数据来自 `session_->info()` + `info_bar_->TotalForTest()`，**不反解析标签文本**。
  - **折叠/聚焦联动**：切 tab/折叠前 `mark_panel_->FlushEditor()`；`FocusNoteEditorForTest()` 与
    `ActivateMark()` 在折叠/非标记 tab 时先展开并切到标记；**默认 tab = 标记**（`--key-test` 的 `focus-tree` 依赖）。
  - 门禁**新旧壳各一遍**：`ctest 37/37`（MarkPanel 独立构造仍过）、`--dpi-test` 4 DPI problems 全空
    （新壳每档 37 项检查，新增折叠 44/展开 560/220/截图页/信息页/窄 220 用例）、M4 仍
    **64.17/64.17/64.42/56.64/60.80%**、`--banner-test 74/0`、`--key-test 77/0`、mark/note 0
    （v2 新增 2 条 3C 用例全过、旧壳跳过）、`--snapshot-test 35/0`、fullscreen/loop/loop-end 全过、
    `--drag-test` P4 66.3–67.7fps / P6 p95 15.6–22.3ms、`--drag-strategy-test 7/7`、
    `--startup-test` 新壳 P1 187ms / P2 414ms（最终复跑 218/482ms）、`--readout-edit-test` 通过 + M7 四态 `rel=85,13 32x20`；
    在线 Medium 副本：longpress **54/0**、parity **61/0**、T8.6.1b **24/0**。
    **坑**：`--snapshot-test` 前要清 `media/snapshots`（既有测试要求目录恰好 3 个 PNG，历史遗留会误报
    `three-files`）；3B gate 脚本把 `--dpi-tag` 写成 `--tag`（截图名永远是 `x`），3C 已改。
    证据 [交接](docs/handoff-2026-10-26-ui-v2-3c.md)；下一步 3D 提示词见
    [next-session-prompt-ui-v2-3d.md](docs/next-session-prompt-ui-v2-3d.md)。
- **UI v2 3D 在线 / 全屏 / 收口（2026-10-26，非门）**：只剩一套布局（`LAPIAN_UI_V2`/`--ui-v2` 仍解析但 no-op）。
  - **顶栏模式 chip 收口**：`未打开`（neutral）/`本地 · 零网络`（ok）/`在线 · 解析中`、`在线 · qn · fps`、
    `缓存命中 · 0 网络`、`在线 · 缓冲中`、`切档中 → qn`、`打开失败`（err）；DEC-4 fps 角标（`onlineFpsBadge`
    同一 QLabel）搬进 chip 内部，降帧时 chip + 角标同时 warn。清晰度菜单改为**点击时重建**（§7.6），
    `OnlineQualityMenuActionsForTest()` 改读菜单模型，`TriggerOnlineQualityEntryForTest()` 直接走
    `OnOnlineQualityChosen`——断言一行未改。
  - **BannerSlot 接管唯一横幅**：`media::StatusNotice` 的构建/择一/次级摘要一行未改；`infoBanner`/
    `infoBannerClose` 保留，新增 `IsVisibleKind()`/`Dismiss()`/`SetMedia/SetError/SetOnlineFrameRateBanner`；
    `BannerStateForTest()` 8 键顺序不变（`info_h=` 语义 = BannerSlot 高度），`BannerUiStateForTest()` 的
    `loop=` 改指 Transport 循环键（A.3）；横幅配色按原型 `#1b1a13 + #f0cb7a + 左侧图标` 进 Theme。
  - **状态栏压缩**：1 条消息（`noticeInfo`）+ 3 指标（`audioInfo`/`cacheInfo`(新)/`waveformInfo`）；
    `speed/speed_label_`、`loop_label_`、`volume_slider_`、`statusInfo` 不再常驻（对象保留，避免旧断言/日志断链）。
  - **删除 InfoBar 与旧布局**（单独 commit）：文件/规格 → `TopBar` 自建 `infoFile`/`infoFormat`；
    帧号/时间码/总帧 → `TransportBar` 自建 `v2FrameNumber`/`v2Timecode`/`v2FrameTotal`（M7 四态仍
    `rel=85,13 32x14`）；唯一横幅 → `BannerSlot`；删除 `InfoBar.{h,cpp}`、`BuildLegacyUi`/
    `LayoutStateLegacy`/`LayoutProblemsLegacy` 与所有 `shell_ == nullptr` 分支；`RefreshInspectorInfo()`
    改读 `total_for_ui_`（精确/≈估算/扫描中三态不变）。**注意**：`QTreeWidget` 第一次设置 current item
    在本机是一次性 ~600ms 的 Qt 字体/样式初始化；已在 `BuildShellUi` 用假标记 + `SelectMark` 预热
    （P1 759–857ms / P2 988–1100ms，仍 ≤1.5s/2.5s；不预热会把 M1 的 ui_gap 推到 600ms）。
  - 门禁（单壳，100%）：`ctest 37/37`、`--dpi-test` 4 DPI × {播放,全屏,窄窗} problems 空 + `--dpi-test --empty`
    4 DPI 空态（mode=`未打开`/折叠 44/banner 0）、M4 仍 **64.17/64.17/64.42/56.64/60.80%**、`--banner-test 74/0`、
    `--key-test 77/0`、`--mark-test`/`--note-test 17/0`、`--snapshot-test 35/0`、`--readout-edit-test` 28/0 +
    M7 `rel=85,13 32x14`、`--fullscreen-test` chrome=`v2=1 appbar=0 banner=0 status=0 inspector=0 transport=1 timeline=1`、
    loop/loop-end 过、`--drag-test` P4 64.5fps / P6 p95 19.9ms、`--drag-strategy-test 7/7`、
    `--startup-test` P1 759–857ms / P2 988–1100ms、`d3d_init_at_startup=0`；M2 截屏采样 14.66/14.66/13.92/
    14.66/11.24/13.92/15.80/6.72:1（顶栏/Transport/检查器/状态栏/横幅/菜单/设置/在线对话框）。
  - 在线（Medium 副本）：M1 OB-1 **24/0 pass**（匿名 qn=32、`blocked-quality`；凭据 `vip_status=-1` 已过期）、
    longpress **54/0**（`--longpress-fresh-cache`）、parity **61/0**、T8.6.1b **24/1**（`forward_ui_gap` 222–228ms，
    3C v2 A/B 213ms，既有基线、非 3D 回归）、offline workspace **45/0**、cache **pass**、fault **92/0（10/10）**
    但 `ui_gap=220ms`（3C v2 1009ms，既有基线）、QR **19/0 + --ui 21/0**（需干净 `%TEMP%`）；M2/quality/vip 因
    SESSDATA 过期（`vip_status=-1`）无法跑 1080P 断言。**坑**：`--online-qr-test` 用系统 `%TEMP%`，脏 TEMP 会让
    `nav -101 no credential save` 假失败；`--online-fault-test` 的 `build/t8.13-fault` 需先清；`--snapshot-test`
    前清 `media/snapshots`。证据 [3D 交接](docs/handoff-2026-10-26-ui-v2-3d.md)。
  - **用户实机反馈（2026-10-26）**：4 项热修已定位并给出方案，**未改代码**：① 拖动后立刻 Space 卡住
    （在线异步 seek 落地 `SetPosition` 再 `Pause` 掉刚启动的播放）；② 在线模式不能改缓存目录
    （`ApplyCacheSettings`/`CacheSettingsDialog` 主动拒绝；已确认两阶段保存方案）；③ 扫码二维码面板
    显示不全（`qr_panel_->show()` 后未 `adjustSize`）；④ 清晰度入口像状态文字（flat button 无 QSS/图标）。
    交接 [hotfix](docs/handoff-2026-10-26-ui-v2-3d-hotfix.md)，新对话提示词
    [next-session-prompt-ui-v2-3d-hotfix.md](docs/next-session-prompt-ui-v2-3d-hotfix.md)。
  - **3D 热修实现（2026-10-26，非门）**：4 项全部修复、每项独立 commit（`534d909` P0 / `b78bbe1` P1 /
    `6533da0` P2 / `efc6831` P3），无新增 runner 文件（CMakeLists 未改）。P0：在线异步 seek 在途时
    Space 只记 `online_pending_play_`，落地 `SetPosition` 后补 `Play`；清标记点 = 新 seek/拖动/新请求/
    关会话/取消/失败/切档；`--scrub-space-test` 在线 64/0（20/20）、本地 62/0，修复关闭时在线
    20/20 失败 → 确认是**在线增长路径**问题。P1：在线保存缓存目录/配额 = 写 settings + pending，
    会话结束 idle 后 `ApplyPendingCacheStoreIfIdle()` 换 store（对话框打开期间不换）；cache manage
    `--ui` 113/0，真实在线 `--cache-switch-online-test` 17/0（播放不中断、`.part`/root 不变、下次
    会话落新根）。P2：扫码面板显示后按内容长高、夹到屏幕、小屏 `QScrollArea` 滚动；QR `--ui` 26/0
    ×4 DPI、QR 240×240+刷新/取消完整，截图 `build/hotfix-qr-*.png`。P3：`#onlineQualityButton` chip
    （surface/chipbg + 1px 边框 + 10px 圆角 + hover accent）+ 12px chevron + `清晰度 1080P`；
    100/125/150/200% 的 1280/820 截图 overflow=0。门禁：`ctest 37/37`、banner 74/0、key 77/0、
    mark/note/snapshot/readout 不变、M4 **64.17/64.17/64.42/56.64/60.80%**、drag 同机 A/B 不差
    （hotfix 95.6–95.9/21.6–24.1 vs b38df22 95.6–96.1/23.5–25.1）、strategy 7/7、startup 812/1052ms、
    local parity 9/0；在线 M1 24/0×3、parity 61/0、longpress 54/0、offline 45/0、cache 32/0；
    T8.6.1b 35/1（forward_ui_gap 225ms 既有）、fault 92/0（ui_gap 203ms 既有）；SESSDATA 仍过期
    （M2/quality/vip 1080P 未跑）。**坑**：恢复备份源文件会保留旧 mtime → MSBuild 跳过编译（需
    touch/clean-first）；`--mark-test` 前也要清 sidecar。证据 [热修实现交接](docs/handoff-2026-10-26-ui-v2-3d-hotfix-close.md)。
- **规格**：[spec.md](spec.md) **v0.3**，首期唯一验收来源。
- **规划**：[plan.md](plan.md) v0.2（v1 技术方案）+ **v2 增量章节**、[tasks.md](tasks.md) v0.28（v1 已收口）、**[tasks-v2.md](tasks-v2.md) v0.29（v2 任务）**、[docs/spec-v1.0-delta.md](docs/spec-v1.0-delta.md)（v2 在线验收 v0.4）、[docs/adr/](docs/adr/)（架构决策）、[docs/spec-v0.3-delta.md](docs/spec-v0.3-delta.md)（已并入 v0.3）、[spec-v0.3-p6p7-proposal.md](docs/spec-v0.3-p6p7-proposal.md)（方案 A）。
- **仓库现状**：源码、构建/打包/测量脚本、seek 探针、媒体探测库/工具、音频与同步链路已入库；`dist` 当前 **34.71MB / 25 文件**，应用已能打开/播放/步进/跳帧/时间轴缩放滚动、点击/拖动定位、拖动预览与松手精确，并已接入第一条音轨的 WASAPI 输出、音量/静音、IAudioClock 主时钟与 QPC 回退、0.25–2x 变速、后台波形生成与时间轴绘制、A/B 入出点/色条/循环播放/A=B 定帧；T5.1 sidecar 存储、T5.2 标记 UI（Ctrl+M、侧栏、时间轴标记、点击跳转、删除确认、存储位置/警告）、T5.3 备注编辑（标签/备注、失焦/Enter/Ctrl+Enter 保存、关窗 flush、焦点隔离）与 T5.4 截图（`S`、当前显示帧、display matrix 旋转、WIC PNG、命名/重名序号、目录回退、后台编码）与 T6.1 全屏（`F`/双击、`Esc` 退出、几何/焦点恢复、交换链跟随 resize、全屏 `S`/时间轴可用）、T6.2 键位/焦点矩阵（全局步进/Home/End、`+`/`-`、文本/模态让位）、T6.3 设置持久化（音量/波形跨会话、循环/速度仅会话、损坏静默回退）、T6.4 DPI/布局（100–200% 响应式侧栏/信息栏、`--dpi-test`）、T6.5 状态横幅（六类提示、唯一横幅、其余进状态栏、`--banner-test`）、T2.9.1 反向拖动预览补丁（只发布目标帧、过期帧丢弃、32 帧缓存）、T6.2.1 长按连发补丁（忽略自动重复 KeyRelease）与 T7.1 启动埋点/后台索引（`--startup-test`/`--index-test`、首帧后扫描、`SetTotal(Scanning→Exact)`）与 T7.2 内存预算/有界帧缓存（`FrameCache` 64/256MiB、`--memory-test`、切文件释放、波形 4h 硬上限）与 T7.2.1 热修（B=末帧循环、PgUp 画幅/显示尺寸、`--loop-end-test`）与 T7.3 长时浸泡监测（`--memory-mode soak`、`SoakStats` 5min 下包络、线程数/UI 心跳/看门狗、`scripts\soak-bench.ps1`；30min/1h 探针触发 `memory_growth`，V10 2h 未做）与 T7.4 4K 路径（前进顺序解码、4K 后退窗口 20 帧、`--drag-test`/`--force-software-decode`、4K proxy 硬解 P4/P6/P7/P8/P9/P11 达标）与 T7.5 错误与鲁棒性（`--robust-test`、网络路径 3s 有界预检、长路径 sidecar、只读 ACL 脚本、设备丢失模拟；75 项/0 失败）与 T7.6 打包/关联/许可/体积（`scripts\package.ps1`/`installer.iss`、每用户安装、ProgID/4 扩展名 OpenWithProgids、便携 zip、setup 11.31MB、卸载清理保留用户数据）与 T7.8 基准回归（`scripts\bench-regression.ps1`、`scripts\bench\thresholds.json`、`baseline-dev.json`，8 套件）；`ctest` **37/37**（36 既有 + DP-2 `lapian_preview_cache_test`；`lapian_online_preview_test` 在既有 36 内）。
- **本文件**：给新对话 agent 的项目上下文，不替代 spec 的验收条款。

**规格与规划的差异**：`spec.md` v0.3 已应用；plan/tasks 已按 v0.3 同步。实现以 tasks.md 为准；后续只允许通过 spec 版本记录修正验收口径，不静默改。

**“冻结”的含义**：首期不再加功能；只允许通过 spec 版本记录修正表述或验收口径。新功能一律进候选池，不直接进首期。

---

## 2. 一句话产品

把 AE 时间轴里“拖到某一帧、前后翻帧、对着音乐看节奏”这一小块能力抽出来，做成 Windows 上秒开、零项目的参考片审阅器。只读，不剪辑；帧是第一等时间单位。

它有两种模式：**本地模式（v1）**打开已有文件，运行期零网络请求；**在线模式（v2）**输入 BV 在线播放、边看边缓存，只处理公开免费内容与用户自带 Cookie，不绕过付费/DRM/地区限制。文档分层见 [docs/spec-versioning.md](docs/spec-versioning.md)。

它不是“轻量版 AE”，也不是“普通播放器 + 逐帧快捷键”。差异化只可能来自三件事：

1. 帧号可直接读出、直接输入，不用在秒和毫秒之间换算；
2. 音频波形与画面共用同一根帧级时间轴，卡点一眼可见；
3. 打开即用，零项目、零加载。

---

## 3. 快速事实卡

| 项 | 结论 |
|---|---|
| 平台 | Windows 10 21H2+ / Windows 11 x64；不做 macOS / Linux / 网页 / 移动 |
| 技术栈 | C++20 + Qt 6 Widgets + FFmpeg LGPL 动态链接 + D3D11 + WASAPI（ADR-001/004） |
| 当前阶段 | 实现阶段；M0 完成；M1–M4（含 T2.9.1）、T5.1–T5.4、T6.1–T6.5、T7.1–T7.9 代码/脚本完成；T7.3 90min 单文件 + 1h 子区间循环 pass，P14/V10 未通过；T7.4 4K proxy 硬解一轮达标（T7.9 已修 seek 后 PgUp）；T7.5 鲁棒性 75 项/只读 17 项通过（可达 SMB/物理设备丢失/真实 V8 未验）；T7.6 每用户安装/ProgID/便携 zip/卸载本机通过（F1-A1 未正式验收）；T7.8 基准回归 + T7.9 P7 收口：60 项/0 失败、30/30 容差内（P1/P2 热缓存）；T7.10 合成 V10 2h soak `pass=1`、V1–V9 冒烟 18/18；T7.11 修复前 P7 跨 GOP 边界 2s 145.60ms/1s 92.65ms；**T7.11.1（`7af822e`）延后回拷修复后 2s 64.13ms、1s 27.11ms、1080p60 HEVC 67.31ms，连续后退 p95 0.03/0.06ms；全量 60 项/0 失败、30/30 容差；2s 余量 15.9ms；T7.7 清单/预检脚本已入库，本机预检 blocked，P7 2s 复跑 65.33ms；基线机待复跑；2026-10-22 用户决定内部预览收口，正式验收暂缓** |
| 规划/任务 | `plan.md` v0.2 + v2 章节 v0.3、`tasks.md` v0.28（v1 收口）、`tasks-v2.md` v0.29（v2）、`docs/adr/ADR-001`–`007`、`docs/spec-v1.0-delta.md` v0.4 |
| v2 在线 | S1/S2 完成；S3 定稿（T8.0–T8.14 + T8.16 + T8.6.1a + T8.6.1b + T8.3.1 + T8.15 完成；**M1/M2 通过**；**T8.16 打包/隐私/卸载 pass（非 MVP 门）**）；DEC-1…6 已拍板（**DEC-6 扫码登录并入，T8.2.2 非门**）；ADR-007 附条件接受；普通用户登录只到 1080P30；**大会员 T8.15 实测 1080P60 AVC/4K 可达（条件验收 partial），不进 MVP 验收**；可增长读层已完成（0.3MB 首帧/100 seek/取消 ≤1ms/4 读者 10min）；HTTP 栈已自检：WinHTTP nav `code=0/isLogin=true`（161–202ms，普通用户），libcurl 未构建；SESSDATA/DPAPI/DEC-3/脱敏完成；缓存存储完成（默认根/20GiB 配额/LRU/续传/损坏重建/清空；300MB LRU + 20GB 记账；不联网）；**T8.5 durl 取流完成**：真实 OB-1/OB-3=720P、**OB-2=360P（qn=16）→ M1 3/3 720P blocked**；三样本首帧 8–9/10–14ms、ffprobe pass；取消 7ms、`_Exit` 后续传、真实 403 备份/刷新通过；**T8.2.2 扫码登录完成（非门）**：官方 passport QR、本地渲染、4 态/180s/取消 16ms、SESSDATA-only + DPAPI + nav 门、粘贴保留；**真实扫码未验证**；**T8.6 在线会话/UI 完成**：真实 3 条 E2E、冷缓存首帧 20×3 p95 624/725/459ms、P7 p95 20/20/25ms、seek p95 79/59/97ms、UI gap 60/61/62ms、缓冲耗尽/取消/finalize 通过；**T8.7 缓存命中/在线记录完成**：命中 0 playurl/CDN/`.part`、`lapian-online-1`、截图 `index.jsonl`、清缓存保留/重建回访/损坏不阻塞；`--online-offline-test` 45/0、真实缓存 44/0、Medium 默认 AppData 31/0×3（首帧 74–84ms、0 TCP/UDP）；`ctest` 30/30；**T8.8 M1 通过**：qn/fourk 矩阵确认 OB-2 的 360P 是请求 qn=64 导致，最小修正 qn=80/fourk=0；3/3 实际 64/80/64、E2E 29/0×3、OP1 p95 648/532/527ms、OP2 34/51/34ms、OP3 0ms+落点 0、OP5 p95 95ms/0 请求、失败矩阵/续传/DEC-5/本地 O9 全过；dist 33.69MB/25；**T8.6.1a 在线长按完成（非门）**：真实 3 样本×2 轮 Medium 49–50/0、2s 25–26 帧 bad=0、UI gap 37–77ms、边界/短按/未缓存提示+取消通过；**同日实机回归 hotfix**：未缓存改单格排队（retargets=0）、伪 release 守卫、拖动 clamp，用户样本 54/0；慢拖预览仍 T8.6.1b；**T8.9 DASH 解析与档位完成（M2 第 1 项，非门）**：3/3 登录普通用户 DASH 1080P AVC（qn=80，fps 30.000/29.412/25.000）、116/74/120 3/3 不可得、`quality` 撒谎（匿名 quality=64、dash 只有 16/32）；档位菜单只列示/置灰、不把可选写成可达；`lapian_online_dash_test` 117/0、ctest 32/32、`--online-quality-test` 3×27/0+`--ui` 21/0、M1 E2E 28/0、dist 34.02MB/25；**T8.10 DASH 双输入完成（M2 第 2 项，非门）**：视频/音频双 `GrowingSource`/`DownloadJob`/`AVFormatContext`、音频主时钟、seek fan-out、DASH 缓存命中、durl 回退；`--online-m2-test` 3/3 **34/0**（首帧 322.2/369.0/365.1ms、60s 音画偏差 p95 17.1/18.2/22.0ms、20/20 seek 帧号+PTS 精确、EOF 末帧停住、内存峰值 211.5/209.7/201.7MB、缓存命中 45–46ms/0 网络）；音频缓存键 `audio-<id>`（旧 meta 兼容）；**修复 fMP4 seek 落前一 GOP**（`SeekToKeyframePts`+PTS 校验）；`--force-durl` M1 28/0、DASH M1 25/0、ctest 32/32、缓存 396/0、管理 72/98、key 77/0；**T8.11 渐进索引/波形/总帧数完成（M2 第 3 项，非门）**：OB-3 真实在线 27/0（首次索引 128ms、30s 内容 6.209s、20/20 seek 落点 0、exact 5306、波形 0.525 帧、UI gap 58ms）；**T8.12 档位策略/手动切档/降帧提示完成（M2 第 4 项，非门）**：普通用户 3/3 qn=80 AVC、OB-2 29.412fps 横幅+角标、1080→720 切档/PTS 回位/标记保留/截图实际值、菜单点击 61–76ms、真 60/4K 不可得/未验证；`ctest` 34/34、M2 34/0×3、M1 28/0、quality 45/46/45 + UI 50/56/50、dist 34.21MB/25；**T8.13 网络异常/并发加固完成（M2 第 5 项，非门）**：10 个窗口级故障场景 + 15 组离线注入全过；错误 ≤5s（断网 3,981–4,155ms / 看门狗 4,121–4,137ms / API 超时 4,515–4,529ms）、关闭 7–66ms、UI gap 73ms/0 次、4 读者 10min（ops 25,907、timeouts 0、close 16ms）、缓存损坏自动重建、磁盘不足 5ms、标记 sentinel 不丢、脱敏；`ctest` 35/35、M2 34/0×3、M1 28/0、quality UI 50/56/50、dist 34.3MB/25；真实 Windows 休眠未验证；**T8.14 M2 门完成（pass，六条硬条件；3/3 DASH 1080P30、OP1–OP5、失败矩阵、O9/P1/P2/P7、内存/审计；残余 teardown 0xC0000374/seed 卡住未定位，不写无崩溃；详见 [证据](docs/perf/v2-online-m2.md)/[交接](docs/handoff-2026-10-24-t8.14-m2.md)）**；**T8.16 打包/隐私/卸载完成（pass，非 MVP 门）**；**T8.6.1b 完成；T8.15 完成（partial，非 MVP 门；1080P60 AVC 自动/OP9 p95 47.5ms、4K 仅手动/能播/逐帧、60/4K→1080P30 切档；A3 unverified）**；**T8.3.1 缓存管理完成（非门）**：设置页目录/配额/打开目录/清空/孤儿清理，清空保留当前会话/记录/截图，Medium 默认 AppData 101/0；非门后续清空 |
| 帧号 | 0 基“显示帧序号”；CFR 精确；VFR 按显示帧计数并提示；等距帧格；`HH:MM:SS:FF` 时间码辅助 |
| 保证档 | 1080p，H.264/H.265，8-bit SDR，≤60fps，MP4/MOV/MKV；保证档要求 D3D11 + 硬解 |
| 可用档 | 4K ≤30fps；VP9/AV1/WebM 能播则播；H.265 4K60；长 GOP 帧精确但性能按可用档 |
| 不支持 | HDR/10-bit 的保证、8K、DRM、字幕、图片序列、GIF |
| 性能基线 | 4 核 CPU / 8GB 内存 / 集显 / SATA SSD / Windows 11 |
| 冷启动 | 窗口可交互 ≤1.5s；双击已关联文件到首帧 ≤2.5s |
| 拖动 | 拖动中降质预览 ≥30fps；松手 ≤150ms 精确到帧（4K ≤300ms；目标距前关键帧 ≤2s） |
| 步进 | ±1 帧；≤80ms p95（4K ≤150ms）；长按 ≥12 帧/秒，每帧呈现 |
| 波形 | 峰值包络；可开关；显示对齐 ≤1 帧；步进/拖动静音 |
| 循环 | A/B 区间；A=B 时单帧定帧（画面保持、音频静音） |
| 变速 | 0.25–2x；音频随速度变调，不做音高保持 |
| 记录 | 标记 + 备注（备注挂标记）+ 截图 PNG |
| 截图位置 | 视频同目录 `snapshots/`；不可写时用户图片目录 |
| 联网 | 首期零网络请求 |
| 体积/内存 | 分发 ≤100MB；空闲 ≤250MB；1080p 播放 ≤800MB；4K ≤1.5GB |
| 验收 | `spec.md` §4.1 指标 + §8 片单 |

---

## 4. 目标用户与核心场景

### 4.1 用户

- 静止系 MAD 作者
- PV 师
- MG 动画师
- 动画师

共同点：需要参考别人的影像来研究节奏、运动、构图与色彩；现有替代方案是“把视频下载下来丢进 AE，拖时间轴”。

**场景优先级**：四类用户在首期诉求高度重叠，功能对四类人都可用；但验收与默认交互优先服务 **MAD/PV 的“卡点 + 逐帧看转场”**。

### 4.2 核心场景（按频率排序）

- **S1 找卡点**：看波形找到鼓点/重音，拖到那一帧，再前后步进确认画面切点有没有踩在拍上。
- **S2 拆一段运动**：选 1–2 秒设 A/B 循环，用 0.25–0.5x 反复看，再逐帧看位置、形变、构图变化。
- **S3 精确回访**：记下“第 137 帧有个转场”，输入 137 直接回到那一帧对比前后。
- **S4 记录**：在关键帧打标记、写备注、截图；下次打开同一条片子，标记和备注还在。
- **S5 快速过片**：用 1.5–2x 过一遍，判断值不值得细看。

### 4.3 不服务的场景

- 边看边剪、边看边导出。
- 多人协作、共享工程。
- 手机/网页上看。

---

## 5. 首期功能范围（v0.3 冻结）

| 编号 | 能力 | 一句话定义 | 验收位置 |
|---|---|---|---|
| F1 | 打开与基础播放 | 对话框/拖拽/关联打开；停在首帧；播放、暂停、音量、全屏 | spec §3.1 |
| F2 | 帧时间轴与定位 | 帧刻度；缩放（最大 1 帧 ≥8px）；点击/拖动/输入帧号；时间码 | spec §3.2 |
| F3 | 逐帧步进 | PgUp/PgDn/方向键；长按连发；边界；静音 | spec §3.3 |
| F4 | 循环与变速 | A/B 循环；A=B 定帧；0.25–2x；音频变调 | spec §3.4 |
| F5 | 音频波形 | 波形可开关；与帧刻度对齐；后台生成 | spec §3.5 |
| F6 | 标记/备注/截图 | 帧标记、备注、当前帧 PNG | spec §3.6 |
| F7 | 键位与焦点 | Space、PgUp/PgDn、I/O/L、W/M、Ctrl+M、S、F 等 | spec §3.7 |
| F8 | 状态与持久化 | 音量/波形记忆；标记备注随文件恢复 | spec §3.8 |

**非功能**：性能见 `spec.md` §4.1（P1–P14）；支持范围见 §4.2；平台与显示见 §4.3；稳定性见 §4.4；轻量化见 §4.5。

---

## 6. 决策记录（已拍板，默认不再重开）

> 以下来自 2026-10-19 定义阶段对话：产品全景 → 10 个澄清问题（Q1–Q10）→ 范围分诊 → spec v0.1 → v0.2 补充确认。规划阶段的 Q1–Q8 与三条补充口径见 `plan.md` / `tasks.md` / `docs/spec-v0.3-delta.md`。
> 要改任何一条，先说明它影响哪些验收条款，再等用户确认；不要静默改。

| 议题 | 结论 | 理由 / 约束 |
|---|---|---|
| 帧的定义 | 0 基“显示帧序号”；CFR 精确；VFR 按显示帧计数并提示；时间码辅助 | 对齐 AE；VFR 的“第 N 帧 = 第几秒”语义模糊，精确映射成本高、收益低 |
| 素材范围 | 1080p H.264/H.265 8-bit SDR ≤60fps 为保证档；4K ≤30fps 为可用档；音频只取第一条 | 参考素材主要来自 B 站；4K 不该绑架首期；HDR/10-bit 是另一块色彩工程 |
| 性能验收 | `spec.md` §4.1 的 P1–P14；基线 4C/8G/集显/SATA SSD/Win11；P7=±1；P6/P9 目标距前关键帧 ≤2s；4K P9 ≤300ms | “高性能优先”必须可量化；长 GOP 按可用档，不伪装达标 |
| 拖动语义 | A 方案：拖动中降质实时预览，松手精确到帧 | 全分辨率 4K 长 GOP 逐帧拖动不现实；用户要的是“跟手”，不是“拖动中每一帧都全清晰” |
| 音频可视化 | 波形，不是频谱；可开关；显示对齐 ≤1 帧 | 卡点是瞬态/能量突变，波形比频谱更直接；全片频谱预计算成本明显更高 |
| 平台 | Windows 10/11 x64 only | 用户工作流在 Windows；跨平台会让首期工作量接近翻倍 |
| 循环 | A/B 区间循环；A=B 时单帧定帧 | 循环是拉片高频动作；定帧用于固定画面，音频静音避免爆音 |
| 变速 | 0.25–2x；音频随速度变调 | 看节奏够用；音高保持是额外成本，放候选 |
| 标记/备注/截图 | 全部进首期；备注挂标记；截图存 `snapshots/` | 用户明确要；记录是拉片闭环的一部分 |
| 时间轴缩放 | 必须有；最大 1 帧 ≥8px | 没有缩放，长片全片视图 1 像素跨多帧，拖不到单帧 |
| B 站 | 首期不做；零网络请求；进候选待定 | 帧精确访问仍需本地缓存，“免下载”不成立；接口/风控/合规风险高 |
| 未提及项 | 一律按 AE 习惯处理 | 用户明确指示：0 基、点击/拖动时间轴即暂停、打开不自动播放 |
| 候选池 | 只记一句话价值 + 触发条件；不写验收 | 防止范围膨胀；B 站/分镜/吸管标“待定” |
| 当前阶段 | 实现阶段；M0 完成；M1–M4（含 T2.9.1）、T5.1–T5.4、T6.1–T6.5、T7.1–T7.9 代码/脚本完成；T7.3 90min 单文件 + 1h 循环 pass，P14/V10 未通过；T7.4 4K proxy 硬解一轮达标；T7.5 鲁棒性通过（缺口见交接）；T7.6 每用户安装/关联/便携 zip/卸载本机通过（F1-A1 待 T7.7）；T7.8 基准回归 + T7.9 P7 收口：60 项/0 失败、30/30 容差内（P1/P2 热缓存；4K seek 后 PgUp 0.06ms）；T7.10 合成 V10 soak pass / V1–V9 冒烟 18/18；T7.11.1 P7 边界修复后 2s 64.13ms、1s 27.11ms、1080p60 HEVC 67.31ms，连续后退 p95 0.03/0.06ms；全量 60/0 | 规划已完成；spec v0.3 冻结；不重开已拍板决策 |
| 验收片单 | `spec.md` §8 的 V1–V10 | 固定片单才能复现性能验收 |

**规划阶段新增拍板（2026-10-19，详见 plan.md 与 docs/spec-v0.3-delta.md）**

- 技术栈：C++20 + Qt 6 Widgets + FFmpeg LGPL 动态链接 + D3D11VA + WASAPI；不换栈（ADR-001/004）。
- 时钟：音频设备主时钟，QPC 回退；步进/拖动静音并暂停时钟（ADR-002）。
- VFR：等距帧格 + PTS 索引；总帧数精确/估算/扫描中三态；VFR 首次远跳 ≤2s（ADR-003）。
- 预览/缓存：预览解码器 + 精确解码器双路径；LRU 帧缓存；不做代理/缩略图图集（ADR-005）。
- 持久化：sidecar + `%APPDATA%` 回退；原子写、按 id+updated_at 合并（ADR-006）。
- 验收补充：F3-A3 改为 2s ≥20 帧；P7=±1 帧步进、P8=顺序长按；P6/P9 目标距前关键帧 ≤2s；4K P9 ≤300ms；V10 必选；F6-A8 改为 UI≤100ms/画面按 P9；旋转按 display matrix、截图输出显示方向。
- **2026-10-19 P6/P7 方案 A**：P7 不再按随机 seek 考核；P6/P9 的 1080p 强数字以目标距前关键帧 ≤2s 为前提；长 GOP 仍帧精确但按可用档；4K P9 补 300ms。证据见 [T0.3 真实素材补测](docs/perf/T0.3-seek-probe-real-sample.md)。

---

## 7. 明确不做

> 本节原始版本是**首期（v1）非目标**。2026-10-23 起 v2 在线模式已开放“仅公开免费内容 + 用户自带 Cookie”的在线播放；弹幕/评论/账号体系/云端同步/批量抓取仍然不做（见 §1 v2 条目、[product-v2-vision.md](docs/product-v2-vision.md)、[spec-versioning.md](docs/spec-versioning.md)）。

**创作与输出**

- 剪辑、特效、调色、合成、多轨时间线。
- 视频/音频/序列帧/工程导出（单帧截图不算导出）。

**获取与联网**

- 弹幕、评论、账号体系（注册/个人主页/多账号）、云端同步、批量抓取/收藏夹下载 —— 仍然不做；v2 只做“用户自带凭据（粘贴 SESSDATA 或官方网页扫码，T8.2.2 非门）”用于提升清晰度。
- v2 已开放：B 站公开免费内容的在线播放 + 自动缓存（可选用户自带 Cookie）；**不做付费/DRM/地区限制绕过，不再分发**。

**分析与取色**

- 自动分镜识别、关键帧提取、镜头分类。
- 吸管取色。
- 自动节拍/BPM 检测。
- 频谱视图。

**播放增强**

- 字幕加载与渲染。
- HDR / 10-bit 色彩管理。
- 多音轨切换。
- 拖动时音频擦洗。
- 多视频并排对比。
- 多文件/播放列表/最近文件。
- 100% 画面缩放/平移（首期只做适应窗口）。

**平台**

- macOS / Linux / 网页 / 移动端。

**交互**

- 插件、脚本、自动化。
- 自定义快捷键/主题。
- 全局撤销/重做（删除标记有确认）。

---

## 8. 候选池（不构成本期承诺）

> 新想法先放这里：一句话价值 + 触发条件，不写验收标准，不提前细化。优先级不明标“待定”。

| 候选 | 一句话价值 | 触发条件 | 状态 |
|---|---|---|---|
| B 站 BV 号拉取 | 省去手动下载与找文件，把“看到→拉片”压成一次输入 | 本地播放与缓存链路稳定，且合规边界明确 | 待定 |
| 自动分镜识别 | 先看结构再看细节，快速知道作者切了多少个分镜 | “分镜”定义与推理方案确定，且不劣化冷启动 | 待定 |
| 吸管取色 | 直接取画面颜色，减少在 PS/AE 间来回 | 色彩口径（源色/显示色）确定 | 待定 |
| 频谱视图 | 看音色与频段分布，辅助判断配器与音效 | 波形链路稳定且有明确需求 | 候选 |
| 拖动音频擦洗 | 拖动时听到声音，更快找卡点 | 波形与 seek 性能达标 | 候选 |
| 自动节拍标记 | 自动标出鼓点/重音，减少人工找点 | 波形与时间轴稳定，准确率可接受 | 候选 |
| 多文件/播放列表/最近文件 | 一次导入一组参考片，连续看 | 单文件体验稳定 | 候选 |
| 标记/备注导出 | 把拉片记录交给别人或存档 | 标记功能稳定 | 候选 |
| 截图到剪贴板 | 截完直接粘进聊天/文档 | 截图功能稳定 | 候选 |
| 100% 缩放/平移 | 按像素查看细节与颜色 | 基础播放稳定 | 候选 |
| 字幕加载 | 参考片带字幕时可读 | 有明确需求 | 候选 |
| HDR/10-bit 色彩管理 | 正确显示高动态/广色域素材 | 有 HDR 素材需求 | 候选 |
| 并排对比两条视频 | 参考片与自己的版本逐帧对照 | 单文件体验稳定 | 候选 |
| 多音轨切换 | 参考片有多语言/多轨时可选 | 有明确需求 | 候选 |
| 自定义快捷键/主题 | 适配个人习惯 | 核心交互稳定 | 候选 |
| 全局撤销/重做 | 误删标记可恢复 | 标记功能稳定 | 候选 |
| macOS / Linux | 跨平台使用 | Windows 版稳定 | 候选 |

---

## 9. 风险与需要注意的坑

- **4K/HEVC 的帧精确 seek 是最大风险。** 长 GOP 下跳到任意帧都要从最近关键帧解起；要做到跟手，通常需要缓存/预解码/降采样预览，这些又顶到“轻量化”。首期只能承诺“保证档内不卡”，不能承诺“所有视频都不卡”。
- **长 GOP 的“任意帧”是物理风险。** T0.3 已测：1s GOP 硬解 p95 28ms、2s 76ms、8.3s 230ms；已按方案 A 决策（P7=±1；P6/P9 目标距前关键帧 ≤2s；长 GOP 可用档）。不要用 UI 掩盖长 GOP 的延迟。
- **VFR 索引可能包数≠帧数。** 按 ADR-003 做解码校验；不可靠时降级“解码计数”，慢但精确。
- **冷启动/体积反向锁死技术栈。** 不要引入 Electron/QML/重型框架；ADR-004 已定。
- **只读/网络盘的标记写入。** sidecar 失败回退 AppData；多实例按 id+updated_at 合并，首期不加锁。
- **标记点击的 UI 响应要和精确解码解耦。** 把同步 seek 塞回点击处理器会让长 GOP 下选中/播放头/帧号一起卡住；T5.2 先重绘 UI，再 `singleShot(0)` 精确解码，并用 `navigation_generation_` 取消过期跳转。T5.3 加文本框时别破坏这个顺序。
- **备注的“回车保存”口径已收窄，但没有改 spec。** T5.3 采用标签 `Enter` 保存、备注 `Enter` 换行/`Ctrl+Enter` 保存、失焦与关窗必存；这是对 spec §3.6“失焦或回车即时保存”的多行备注解释，已写入 [T5.3 交接](docs/handoff-2026-10-20-t5.3.md) §4。若要改成备注 Enter 保存，则备注只能单行，需走 spec §9 版本记录，不要静默改。
- **文本框聚焦时全局快捷键必须让位。** `MarkPanel::eventFilter` 已接受编辑框上除 Esc 外的 `ShortcutOverride`，MainWindow 各快捷键入口也有 `IsTextInputFocused()` 双保险；新增快捷键时要同时加这两层，否则备注输入会触发播放/循环/波形。
- **截图硬解回读与渲染共用 D3D11 立即上下文。** T5.4 用递归锁把解码/渲染/回读串行化；4K 回读可能让单帧呈现延迟，截图编码在工作线程，但不要把“按键 0.01ms / 编码 100–165ms”当成 P11/P9 通过。真实 V4/V5、ACL 只读目录与真人验收仍未做。
- **拖动预览中按 S 不截图。** 预览帧是降采样且可能不是目标帧；当前提示“松开后再截图”。这是实现边界，spec 未定义拖动中截图；若以后要支持，需另立任务。
- **VFR 是语义坑。** 帧号按显示帧计数可以验收，但“第 N 帧对应第几秒”不要承诺；打开时必须提示可变帧率。
- **音画对齐有隐藏偏移。** AAC 编码延迟、容器 delay 会让波形整体偏移几十 ms；spec 要求显示对齐 ≤1 帧，验收片 V9 专门测这个。
- **波形生成不能阻塞首帧。** 首帧优先可播，波形后台补齐；10 分钟 1080p 目标 ≤3s。
- **首期范围已经比最初 6 项大。** 标记/备注/截图是后加进首期的；性能指标没降。如果后面压不住，优先砍截图/备注，不要砍时间轴和波形——前者不阻塞“看”的闭环。
- **B 站不是“加个输入框”。** 登录、清晰度、风控、接口变动、版权合规都要处理；而且帧精确访问仍然要落本地缓存。保持候选“待定”，不要在首期偷偷做。
- **“分镜识别”定义不清。** 硬切检测 ≠ 分镜；镜头内推拉/特效/构图变化算不算，要先定义。本地模型 vs 云端还会冲击轻量化/隐私/成本。
- **竞品基线别低估。** PotPlayer、mpv、IINA 已有逐帧、帧号、A-B 循环。差异化是“帧时间轴 + 音频对齐 + 零加载”的组合，不是“能逐帧”本身。
- **吸管颜色口径容易翻车。** 吸的是源 YUV 还是显示 sRGB？10-bit/HDR 下差别很大。候选阶段就要先定色彩口径。
- **从 DSH 工作区直接启动的 exe 是 Low integrity。** `dist` / `build\release\bin` 的 `lapian.exe` 继承工作区 `Mandatory Label\Low Mandatory Level`，进程以 Low 运行，不能写 Medium 的 `%LOCALAPPDATA%` / `%APPDATA%`；在线会话会报“无法创建缓存目录：<path>”。默认路径验收/正常使用必须用 `%LOCALAPPDATA%\Programs\拉片软件`（T7.6 installer 或整目录复制）的 Medium 副本；以管理员运行是 High 向下写，不是修复。T8.8 的 M1 默认路径证据本来就来自 Medium 副本。
- **在线拖动/切档验收必须前后成对，不能只测正向。** 反向不精确/冻住、或拿“正向 10fps、反向 10fps”当通过，都算未完成；C 层还要看**首次拖动**（预览解码器冷启动、5s GOP）是否 ≤100ms，不能只测暖缓存。探针的 `response_p95_ms`/`landing_frame` 已在 P0 `3084e33` 修正；P0 后 cold 26–28ms、post-seek 48ms、稳态 58.5fps。读表区分 `first_publish`（decoder 首次发布，post 场景可能只是起点同帧）、`first_delivered`（含 initial_pump 80ms）、`first_response`（target 变化→画面，用户可见）。
- **新缓存根首包 `bytes=0 ... 写入缓存文件失败` 不是写盘失败（2026-10-25 已修复 `925b8d5`）。** 根因是 DASH 总长探测（`Range: bytes=0-0`）的 `FirstChunkSink` 主动停止被传输层映射成 `kSinkError`；改 `headers_only` 只读响应头、不写 sink，3 个新根修复前各 2 条→修复后 0 条；真实 `.part` 写入/续传未受影响。
- **本地 `PreviewDecoder::CloneExactLocked` 必须持锁（2026-10-25 已修复 `08484ee`）。** `Process`/`PrefetchStep` 曾无锁遍历热缓存/环缓存并 `av_frame_clone`，与 UI/预取线程的增删/eviction 竞争，造成 `lapian_preview_decoder_test` 偶发 0xC0000374/0xC0000005；修复后隔离 50/50、全量 ctest 3×37/37。未取得符号化 dump，不写“永不崩溃”；新增缓存读取路径时先检查锁契约。
- **P0 后仍有“seek 落地 0ms 立即再拖”窗口。** 丢弃预览器后 seek 到 1655 立即拖 1655→1645，实测 `max_response 645–646ms`、`max_gap 640–655ms`（1645 仍需从 K=1500 解 145 帧）；该场景只记录、不门禁（按批准的“预热完成后的每次拖动”口径）。`post_seek_immediate` 的 `first_publish=36` 被 baseline/PrimeFrame 帧污染，不可引用为“立即拖很快”；cold 的 flicker 13/14 是“显示 0→scrub 75→目标 65”的方向 artifact，不能当无闪烁证据。要消除需主 decoder 帧交接或硬解，不靠放宽门限。

- **本机 CMake 重配会崩（`0xC0000409`，2026-10-26 起）。** `build\release` 的增量构建正常，但只要 `src/CMakeLists.txt` 一改触发重配，`cmake -S . -B build\release` 就在探测 FFmpeg `find_library` 前后 fail-fast 退出（连 `--debug-output` 都来不及打印），换仓库内 `TMP/TEMP` 也无效；全新目录 `cmake -S . -B build\<新目录> -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=<Qt>` 可以成功。处理方式（3B 已用）：把旧目录 `move` 成 `build\release-3a5`，再全新配置一个 `build/release`，然后全量构建。别在旧目录里反复重试。
- **不要用 `Get-Content -Raw`/`Set-Content` 改带中文的源文件。** Windows PowerShell 5.1 默认按 ANSI(GBK) 读、按 UTF-8 写，会把中文注释和字符串变成乱码，而且 `-NoNewline` 之类的参数会顺手吃掉换行；3B 会话曾把 `src/app/ui/TransportBar.cpp` 整文件弄坏（只能按上下文重写）。批量替换用编辑器/`edit` 工具；`.ps1` 脚本本身也要**纯 ASCII**（无 BOM 时 5.1 按 ANSI 解析，中文会直接 ParserError）。

---

## 10. 术语表

| 术语 | 含义 |
|---|---|
| 拉片 | 逐帧研究参考影像的节奏、运动、构图与色彩；本产品不做剪辑 |
| 帧号 | 0 基“显示帧序号”；第一帧显示为 0 |
| 显示帧 | 解码后实际呈现的帧，区别于解码顺序 |
| CFR / VFR | 固定帧率 / 可变帧率 |
| 定帧 | A=B 的单帧循环状态：画面保持、音频静音、时间不推进 |
| A/B | 循环入点 / 出点 |
| 卡点 | 音乐重音/鼓点与画面切点对齐的位置 |
| 保证档 / 可用档 | 性能承诺范围 / 能播但不保证性能 |
| 首期 | 第一迭代，当前对应 `spec.md` v0.3 |
| 候选池 | 未来想法，不构成本期承诺 |

---

## 11. 与 agent 的协作规则

1. **先读本文件、`spec.md`、`plan.md`、`tasks.md` 与 ADR**；确认 spec 是 v0.3、tasks 已同步；不要凭记忆回答项目决策。**v2 会话另读 [docs/spec-versioning.md](docs/spec-versioning.md) 与 [docs/handoff-2026-10-23-s1s2.md](docs/handoff-2026-10-23-s1s2.md)；v2 任务以 `tasks-v2.md` 为准。**
2. **`spec.md` v0.3 是验收基础**；历史回补与 P6/P7 方案 A 已并入。冲突时先指出，按 §9 版本记录处理，不静默选。
3. **不重开 §6 已拍板项。** 用户要改时，先列出受影响的验收条款，再等确认；不要直接改。
4. **不自行加需求。** 新想法先放候选池，一句话价值 + 触发条件，不写验收；优先级不明标“待定”。
5. **当前阶段是实现阶段**：按 `tasks.md` 的 M0→M7 顺序做；一个任务 = 一次可审查变更；不跳任务、不顺带加功能。T0.3 已决策方案 A；M1/M2/M3（T1.1–T3.7）、M4（T4.1–T4.2）、T5.1–T5.4、T6.1–T6.5、T2.9.1、T6.2.1、T7.1–T7.2.1 代码完成；T7.3 90min 单文件 + 1h 子区间循环均 pass，无可复现持续增长，P14/V10 未通过；T7.4 4K proxy 硬解一轮 P4/P6/P7/P8/P9/P11 达标（软解 P6/P9 未过）；T7.5 错误与鲁棒性代码完成（75 项/0 失败、只读 ACL 17/17；可达 SMB/物理设备丢失/真实 V8 未验）；T7.6 打包/关联/许可/体积代码/脚本完成（每用户安装、ProgID/4 扩展名、便携 zip、卸载清理；F1-A1 仅预检）；T7.8 基准回归脚本/阈值/基线完成；T7.9 P7 seek→PgUp 收口（60 项/0 失败、30/30 容差，4K spot 0.06ms，基线 dev-2026-10-22-t7.9；1080p 旧 372ms 是 10s GOP 关键帧边界，不属 80ms）；T7.10 合成 V10 2h soak `pass=1`、V1–V9 冒烟 18/18；T7.11 P7 跨 GOP 边界专项（修复前 2s 145.60ms、1s 92.65ms）已由 T7.11.1（`7af822e`）用延后回拷修复：2s 64.13ms、1s 27.11ms、1080p60 HEVC 67.31ms，连续后退 p95 0.03/0.06ms；全量 60 项/0 失败、30/30 容差；2s 余量 15.9ms，基线机待 T7.7，正式 V10 2h 仍待。
6. **有歧义先问。** 用户偏好“给建议默认值 + 理由”，方便快速拍板；不要用“都可以”回避取舍。
7. **未提及的交互默认按 AE 习惯**；但涉及验收口径的，仍要在 spec 里写清楚。
8. **性能数字只能引用 `spec.md` §4.1 或 v0.3 已拍板口径**；没有片单不要编新指标，也不要把“感觉流畅”当验收。
9. **文档用中文**；术语按 §10；Windows 路径和中文文件名用 UTF-8。
10. **规格变更走版本记录**（按 `spec.md` §9），不要静默改冻结范围。
11. **实现按 `plan.md` + ADR**：技术栈已定（C++20/Qt/FFmpeg/D3D11/WASAPI）；要换栈先改 ADR 并等确认。
12. **回答风格**：先给结论，再给理由/风险；直接指出矛盾，不要默认接受用户的每个想法。
13. **性能任务必须带数字**：引用 `spec.md` §4.1 或 tasks.md 的验收值；附 `docs/perf/` 测量记录。
14. **候选池不进首期；`spec.md` 不静默改。** 新想法先放候选池；规格变更按 `spec.md` §9 版本记录处理。
15. **前后一致（双向验收）**：任何与方向有关的交互/性能改动，必须同时测试正向和反向，并给出两组数字；不允许“前进可用、后退不可用”或两边行为不一致。只测一个方向视为未完成。
16. **视觉基准是原型**：`docs/ui-v2-proto/plan.html`（评审版，默认即产品形态）是 UI 的**唯一视觉参考稿**；
    任何 UI 阶段（3B/3C/3D 或后续改动）动手前先读 [ui-v2-visual-reference.md](docs/ui-v2-visual-reference.md)
    （怎么把原型渲成截图、组件对照表、令牌/配色、遗留差距），并把原型截图与自己实现的
    `--banner-test` 截图逐区对照。**数字对（M4/令牌）不等于观感对**；原型与规划正文冲突时以原型为准。

---

## 12. 文档与变更

| 文档 | 版本 | 日期 | 状态 |
|---|---|---|---|
| `spec.md` | v0.3 | 2026-10-19 | 首期范围冻结；已应用回补 + P6/P7 方案 A |
| `docs/handoff-2026-10-26-ui-v2-3d.md` | — | 2026-10-26 | **UI v2 3D 在线/全屏/收口交接**：文件级改动（TopBar 模式 chip/fps 角标、BannerSlot 接管横幅、Transport 自建读数、状态栏 3 指标、Theme 横幅/对话框、删除 InfoBar/旧布局、`--dpi-test --empty`/`--ui-shots-test`）、6 步做法、新旧壳→单壳对照、M1–M9 逐条证据与 M4/M2 数字、在线结果与凭据过期阻塞、残余/回滚 |
| `docs/handoff-2026-10-26-ui-v2-3d-hotfix.md` | — | 2026-10-26 | **3D 热修交接（未改代码）**：4 项用户实机反馈的根因/方案——拖动后立刻 Space 卡住（在线异步 seek 落地 `SetPosition`→`Pause`）、在线改缓存目录（两阶段保存，用户已确认）、扫码 QR 面板未 `adjustSize`、清晰度入口无 chip 样式；含当前基线表、测试方案、边界、执行顺序 |
| `docs/handoff-2026-10-26-ui-v2-3d-hotfix-close.md` | — | 2026-10-26 | **3D 热修实现交接（本轮）**：P0 待播/落地补 Play（`--scrub-space-test` 在线 64/0、本地 62/0；修复关闭 20/20 失败）、P1 两阶段缓存设置（cache manage 113/0、真实在线 `--cache-switch-online-test` 17/0）、P2 QR 长高/夹屏/滚动（26/0×4 DPI + 截图）、P3 清晰度 chip（4 DPI 1280/820 overflow=0）；四个 commit、门禁对比、残余/构建坑 |
| `docs/next-session-prompt-ui-v2-3d-hotfix.md` | — | 2026-10-26 | **3D 热修开工提示词**（复制到新会话）：先基线/复现，再 P0 Space 卡住 → P1 在线缓存目录 → P2 QR 布局 → P3 清晰度 chip；每项独立 commit、新 `--scrub-space-test`、门禁不退化、Medium 在线、构建/工具坑 |
| `AGENTS.md` | v6.57 | 2026-10-26 | 项目上下文；**UI v2 3D 热修实现（2026-10-26，非门）**：4 项修复/提交/门禁见 [hotfix 实现](docs/handoff-2026-10-26-ui-v2-3d-hotfix-close.md)；v6.56：**UI v2 3D 热修交接/提示词（2026-10-26，未改代码）**：4 项用户实机反馈（Space 卡住/在线缓存目录/扫码布局/清晰度入口）的根因、两阶段保存方案、基线/测试/边界见 [hotfix](docs/handoff-2026-10-26-ui-v2-3d-hotfix.md)；新对话提示词 [hotfix prompt](docs/next-session-prompt-ui-v2-3d-hotfix.md)；v6.55：**UI v2 3D 在线/全屏/收口（2026-10-26，非门）**：删除 `InfoBar`/旧布局（单壳、`LAPIAN_UI_V2` no-op）；TopBar 模式 chip + fps 角标进 chip + 清晰度点击建菜单；BannerSlot 接管唯一横幅（A.3/A.4 断言行不改）；状态栏 1 消息+3 指标；设置/在线/扫码/帮助/菜单进 Theme；`--dpi-test --empty`；门禁 `ctest 37/37`、dpi 4 DPI×3 + 空态、M4 未动、banner 74/0、key 77/0、snapshot 35/0、M7 `32x14`、drag P6 19.9ms、strategy 7/7、startup P1 759–857ms/P2 988–1100ms；在线 M1 24/0、longpress 54/0、parity 61/0、T8.6.1b 24/1（既有 213–228ms）、QR 19/21、offline 45/0、fault 92/0（ui_gap 220 既有）；凭据过期 → M2/quality/vip 1080P 未跑。证据 [3D 交接](docs/handoff-2026-10-26-ui-v2-3d.md)；v6.54：**UI v2 3C 记录层（2026-10-26，非门）**：新增 `ui/InspectorPanel`（头 26 三 tab + 计数徽标 + `+` 绑 `mark_action_` + 折叠；`v2InspectorStack` 三页 = 既有 MarkPanel 原对象 / 截图网格 / 信息表；页脚存储位置唯一常显；折叠 44 rail 竖排"标记 N / 截图 M"）；MarkPanel `SetEmbeddedMode` 只 hide 自己的标题/存储/提示/表头 + 帧号 accent 等宽；截图网格两路数据源（在线 `index.jsonl` / 本地 `snapshots/*.png` + 新纯函数 `ParseSnapshotFrameFromName`）≤60 张、首次绘制懒加载、点击沿用 T5.2 "先 UI 后 singleShot(0) 精确解码"；信息页读 `session_->info()` + `TotalFrameCount` 三态；切 tab/折叠前 flush、`FocusNoteEditorForTest`/`ActivateMark` 自动展开到标记 tab（默认 tab=标记）；门禁新旧壳各一遍全绿：`ctest 37/37`、`--dpi-test` 4 DPI problems 空（新壳每档 37 检查 + 折叠 44/展开 560/220/截图页/信息页用例）、M4 仍 **64.17/64.17/64.42/56.64/60.80%**、`--banner-test 74/0`、`--key-test 77/0`、mark/note 0（v2 新增 2 条 3C 用例、旧壳跳过）、`--snapshot-test 35/0`、fullscreen/loop/loop-end 全过、`--drag-test` P4 66.3–67.7fps / P6 15.6–22.3ms、strategy 7/7、`--startup-test` P1 187ms/P2 414ms、`--readout-edit-test` 通过 + M7 四态 `rel=85,13 32x20`；在线 Medium：longpress 54/0、parity 61/0、T8.6.1b 24/0。**坑**：`--snapshot-test` 前清 `media/snapshots`（要求恰好 3 个 PNG）；3B gate 脚本 `--tag` 应为 `--dpi-tag`。证据 [交接](docs/handoff-2026-10-26-ui-v2-3c.md)；v6.53：**UI v2 3B 播放层（2026-10-26，非门）**：Transport 收尾（帧号/时间码**内联输入** Enter/Esc/失焦/非法回滚 + `v2ReadoutEdit`、`v2SpeedButton` 0.25–2x 菜单、在线缓冲槽 `v2BufferSlot`、**四档宽度降级** ≥760/660–759/560–659/<560、tooltip 逐条照 §2.3）+ 新 `ui/RangeBar`（**视图窗/工作区两条滑块**，只读 `TimelineModel`，拖端缩放/改 A/B、拖中平移、滚轮/Shift 滚轮、双击适配/清除）+ 播放头常驻（越界贴边箭头）+ Fill 按实际高度派生 + **删时间轴内部横向滚动条** + **删 250ms `ui_meta_timer_`**（改信号驱动）；门禁**新旧壳各一遍全绿**：`ctest 37/37`、`--dpi-test` 4 DPI problems 全空 + M4 仍 **64.17/64.17/64.42/56.64/60.80%** + 新增 2 用例（档位切换 8 次落档 / 时间轴 340）、`--banner-test 74/0`、`--key-test 77/0`、mark/note 0、fullscreen/loop/loop-end 全过、`--drag-test` P4 66.0–96.1fps / P6 p95 15.4–20.4ms（tl=132 与 340）、`--drag-strategy-test 7/7`、`--startup-test` 新壳 P1 161–181ms / P2 363–400ms、新探针 `--readout-edit-test` 19/19 + M7 四态 `rel=85,13 32x20` + M5 17/17。**坑**：`tests/timeline_test.cpp` 的 128/72 与 A/B 标签像素断言依赖 `kFixed` 底部 14px（旧滚动条占位，改名 `kFixedBottomReserve` 保留）；本机 **CMake 重配崩 0xC0000409**（换 TEMP 无效，须整目录改名后全新配置）；`--banner-test` 的 `volume=` 语义改成"音量入口还在"（§3.3 收滑条但静音保留）。证据 [交接](docs/handoff-2026-10-26-ui-v2-3b.md)；v6.52：**UI v2 3A.5 视觉对齐（2026-10-26）**：按原型补图标/Transport 全按钮/TopBar chip+kbd/面板卡片/时间轴面板头；新增 [视觉基准文档](docs/ui-v2-visual-reference.md) 与 §11 规则 16；门禁复跑全绿；**坑**：`setDefaultAction` 清自绘 icon；v6.51：**UI v2 3A 壳层（2026-10-26，非门）**：`src/app/ui/` 新壳 + 三档密度 + `LAPIAN_UI_V2`/`--ui-v2`（旧壳一行不改）；门禁新旧壳各一遍全绿（ctest 37/37、`--dpi-test` 4DPI×3相位 problems 空、M4 64.2/64.2/64.4/56.6/60.8%、banner 74/0、key 77/0、mark/note 0、fullscreen 全过、`d3d_init_at_startup=0`、M2 截屏采样全 ≥4.5:1）；残余 `window.show()` 慢 ~0.5s 未定位；v6.50：**P0 发布收口（2026-10-25，P0 范围）**：`925b8d5` 首写假失败（DASH 总长探测改 `headers_only`；3 新根 2 条→0 条）+ `08484ee` 预览解码器 `CloneExactLocked` 缓存克隆竞态（隔离 50/50、全量 ctest 3×37/37）；clean 242s、ctest 37/37、dist 25 文件/36,392,776B/34.71MB、exe `8344A2BB…`、setup 11.9MB/zip 14.49MB；静默安装/快捷方式/hash/settings 核对通过；回归 parity 61/0（cold 26ms、post 46ms、56.66–56.94fps、cache 60.75MiB/72）、T8.6.1b 35/0、A 42/0、key 77/0、local 9/0、P6 18.0ms、strategy 7/7；残余无符号化 dump/4K60/post_seek_immediate/体感非量化；证据 [tasks §9.9](docs/tasks-online-parity-fix.md)、[perf §13.8](docs/perf/online-drag-parity.md)、[交接](docs/handoff-2026-10-25-p0-close.md)；v6.49：**在线播放修复验收（partial）**：实现 `e1a89c6`+`4d89683`；clean 重建 222.4s、ctest 37/37、dist 34.68MB/25、Medium 副本 hash 一致；A 切档 80→112→80、qn/codecid/PTS/标记/截图正确、切回 cache_hit=1/0 dash/playurl、42/0；B 正/反 57/57（0.0%）、landing 75/15 精确；C 稳态 17/31ms、33ms 空档、56.21/56.72fps（本地 70% 门槛的 96.0/99.5%）、UI 32ms，但首次拖动 224ms/连续 6 无新画面 → **不写完成**；ctest 37/37、key 77/0、T8.6.1b 35/0、P6 15.9/17.7、4K 只记录；探针 response_p95 恒 0/landing_frame=pts 为缺陷；证据 [online-quality-switch.md](docs/perf/online-quality-switch.md) §10、[online-drag-parity.md](docs/perf/online-drag-parity.md) §12、[交接](docs/handoff-2026-10-25-online-parity.md)；**DP-3 拖动交互策略 + 验收探针完成（1080p 默认硬件路径）**：`DragStrategy` 四模式（Exact ≤4/Fast/Reverse/FarJump ≥300），Exact 关粗帧、FarJump `deferred_exact` 先粗后精、精确后禁粗帧；慢拖 +1/-1 lag p95 0、`+60` 76.9–80.8fps/34.7–37.2ms、`-60` 54.4–64.4fps/23.8–39.3ms、`-20` 47.3–49.2fps、`-1` 命中 84.7–85.5%、远跳粗帧 20.8/20.6ms→精确 30.6/30.0ms；P4 94.9（计时器约 10ms）、P6 p95 18.8；`ctest` 37/37、key 77/0、T8.6.1b 35/0；**T8.15 partial（非 MVP 门）**；v2 在线 T8.0–T8.14 + T8.16 + T8.6.1a + T8.6.1b + T8.3.1 完成、DEC-1…6 已拍板、ADR-007 附条件接受、M1/M2 通过；证据 [drag-preview-dp3.md](docs/perf/drag-preview-dp3.md)、[DP-3 交接](docs/handoff-2026-10-24-dp3.md)；**DP-4 集成/发布完成**：dist 34.55MB/25、Medium 副本、ctest 37/37、key 77/0、T8.6.1b 35/0；强制软件/4K 残余；证据 [drag-preview-dp4.md](docs/perf/drag-preview-dp4.md)、[DP-4 交接](docs/handoff-2026-10-24-dp4.md)；v1 仍内部预览收口、T7.7/P14 正式验收暂缓；**P0 首拖修复 pass（2026-10-25）**：cold 26–28ms、post-seek 48ms、稳态 58.5fps、ctest 37/37、key 77/0、T8.6.1b 35/0；残余 0ms 立即再拖 640–655ms；证据 [online-drag-parity.md](docs/perf/online-drag-parity.md) §13 |
| `plan.md` | v0.2 + v2 章节 v0.3 | 2026-10-19 / 2026-10-23 | v1 技术规划（已同步 v0.3）+ v2 在线增量章节（定稿） |
| `tasks.md` | v0.28 | 2026-10-22 | 任务清单；T7.7 清单/预检、内部预览收口；正式 T7.7/P14 暂缓 |
| `docs/spec-v0.3-delta.md` | v0.2 | 2026-10-19 | 历史回补提案，已并入 v0.3 |
| `docs/ui-v2-plan.md` | v1.0 | 2026-10-26 | **UI v2 规划（P1–P8）已批准**：4 项决策按建议通过（去掉画面区 26px 表头 / M4 改为 62-62-55-59 + 每栏 ±2px / 三档密度按客户区高度自动取档 / QSplitter+自定义 handle）；空态隐藏 GfxWidget（实测 HWND 不重建）、TimelineModel 仍是唯一 source of truth、InfoBar 删除、`LAPIAN_UI_V2` 环境变量；零产品代码改动。评审版原型 [plan.html](docs/ui-v2-proto/plan.html)；3A 开工提示词 [next-session-prompt-ui-v2-3a.md](docs/next-session-prompt-ui-v2-3a.md)；3B 开工提示词 [next-session-prompt-ui-v2-3b.md](docs/next-session-prompt-ui-v2-3b.md) |
| `docs/handoff-2026-10-26-ui-v2-3b.md` | — | 2026-10-26 | **UI v2 3B 播放层交接**：文件级改动（新增 `ui/RangeBar`、Transport 收尾、TimelinePanel 两行、TimelineWidget 删滚动条/信号/播放头、InfoBar 读数改名、MainWindow 去轮询、main.cpp 新探针/`--timeline-height`/dpi 两用例）、5 条关键做法（滑块同源、内联输入、四档降级、去轮询、kFixed 14px 留白）、门禁新旧壳两张表 + M4 表 + 滑块几何、**M5 17/17 逐条 tooltip 表**、**M7 四态几何**（`rel=85,13 32x20`）、视觉自检六区域（已对齐/仍缺）、8 条与规划的偏差（`volume=` 语义、dpi 新用例、迟滞带占比、dense 副行未做、读数改名、minSizeHint 400、箭头、parity 限速）、8 条残余（CMake 重配崩、dense 副行、迟滞带、T8.6.1b UI gap 本机既有、波形/旗标条、旧壳丢滚动条）、3C 入口 |
| `docs/handoff-2026-10-26-ui-v2-3c.md` | — | 2026-10-26 | **UI v2 3C 记录层交接**：文件级改动（新增 `ui/InspectorPanel`、PlayerShell 接入、MarkPanel 嵌入模式、MainWindow 数据推送/截图扫描/信息页/折叠聚焦、`SnapshotNaming::ParseSnapshotFrameFromName`、Tokens/Theme/Glyph、main.cpp dpi/mark 用例、CMake）、5 条关键做法（容器只做容器、搬不换、折叠 44/展开 304、截图两路数据源 + 懒加载、flush/默认 tab）、门禁新旧壳两张表 + M4 表 + 检查器几何 + 3C 新用例表、**M5 17/17 + 第二入口 `v2InspectorAdd`**、M7 未动、视觉自检（含 `build/ui-v2-shots/3c-vs-proto-inspector.png`）、8 条与规划偏差（追加 4 个 objectName、页脚收敛、60 张截断、220 紧凑 tab、极值夹取等）、9 条残余（CMake 重配崩、MarkPanel 观感、60 张、snapshot 目录清理、T8.6.1b 基线、历史扫描时机等）、3D 入口 |
| `docs/handoff-2026-10-26-ui-v2-3a.md` | — | 2026-10-26 | UI v2 3A 壳层交接：改了什么（`src/app/ui/` + MainWindow/main.cpp/TimelineWidget）、"搬不换"做法（InfoBar 仍是状态源，10 个原对象 reparent）、门禁数字（新旧壳各一遍 / M4 表 / M2 截屏采样 / M3/M7/M8）、10 条与规划的偏差、8 条残余（首条 = `window.show()` 慢 0.5s 未定位）、3B 入口 |
| `docs/ui-v2-visual-reference.md` | — | 2026-10-26 | **UI 视觉基准（原型 = 唯一参考稿）**：渲图命令、组件对照表（原型 → Qt objectName → 状态）、令牌/配色/圆角、3A.5 已对 vs 遗留差距（时间轴两条滑块=3B / 检查器 tab=3C / 状态栏压缩=3D）、视觉验收口径 |
| `docs/next-session-prompt-ui-v2-3d.md` | — | 2026-10-26 | **3D 在线/全屏/收口开工提示词**（可直接复制给新会话）：顶栏模式 chip（本地/在线/缓存命中/解析/错误/降帧）+ 清晰度菜单点击建 + 缓冲槽文案；BannerSlot 接管横幅（`IsVisibleKind`/`Dismiss`，A.3/A.4 改法）；全屏 chrome + 设置/在线/扫码/菜单进主题；**删除 `InfoBar` 与旧布局分支**（单独可 revert 的 commit）+ `LAPIAN_UI_V2` no-op；`--dpi-test --empty` 补 M3 空态；含 M1–M9 退出条件、新旧壳/单壳门禁清单（含 Medium 在线全套）、16 条事实附录（InfoBar 五职责、3C 信息页依赖 `TotalForTest`、CMake 重配崩、`--dpi-tag`、snapshot 清理、GUI 探针赋值式捕获会挂进程等） |
| `docs/next-session-prompt-ui-v2-3c.md` | — | 2026-10-26 | **3C 记录层开工提示词**（可直接复制给新会话）：`InspectorPanel` 三 tab + 计数徽标 + 折叠 44 + 页脚存储位置、MarkPanel 内嵌（同 objectName、可独立构造）、截图网格（在线读 index.jsonl / 本地扫 snapshots 目录 + 反解帧号）、信息表（`session_->info()`）、折叠/聚焦联动（`FocusNoteEditorForTest` 自动展开）；含 M5/M4/M7 退出条件、门禁清单（含 Medium 在线）、11 条事实附录（默认 tab 必须是标记否则 key-test 挂、截图无枚举 API、markStorage 收敛到页脚、CMake 重配崩、Sidecar 清理等） |
| `docs/next-session-prompt-ui-v2-3b.md` | — | 2026-10-26 | **3B 播放层开工提示词**（可直接复制给新会话）：Transport 收尾（帧号/时间码内联输入、速度按钮、缓冲槽、§3.3 四档宽度降级）+ 时间轴 `RangeBar` 两条滑块 + 播放头常驻/越界箭头 + 波形 Fill + 删内部滚动条 + 去 250ms 轮询；含 M5/M7 退出条件、门禁清单（含 Medium 副本的在线用例）、事实附录（代码地图 + 11 条已知坑） |
| `docs/ui-v2-proto/` | — | 2026-10-26 | 可点原型：`index.html`（讨论版，含三档密度/`?hdr=0`）、`plan.html`（评审版，默认即规划形态）、`NOTES.md` |
| `docs/spec-v0.3-p6p7-proposal.md` | v1.0 | 2026-10-19 | P6/P7 方案 A 提案，已拍板并应用 |
| `docs/adr/ADR-001`–`006` | v1 | 2026-10-19 | 已接受 |
| `docs/handoff-2026-10-19.md` | v2 | 2026-10-19 | 历史交接（已由 2026-10-20 交接取代） |
| `docs/handoff-2026-10-20.md` | v2 | 2026-10-20 | 历史交接：M2 帧闭环 T2.1–T2.10、验证数字、缺口 |
| `docs/handoff-2026-10-20-t3.1.md` | v1 | 2026-10-20 | 历史交接：T3.1 音频/WASAPI、验证结果、缺口 |
| `docs/handoff-2026-10-20-t3.2.md` | v1 | 2026-10-20 | 历史交接：T3.2 媒体时钟/同步、验证结果、缺口 |
| `docs/handoff-2026-10-20-t3.3.md` | v1 | 2026-10-20 | 历史交接：T3.3 变速/变调、验证结果、缺口 |
| `docs/handoff-2026-10-20-t3.4.md` | v1 | 2026-10-20 | 历史交接：T3.4 A/V 偏移/V9、验证结果、缺口 |
| `docs/handoff-2026-10-20-t3.5.md` | v1 | 2026-10-20 | 历史交接：T3.5 波形生成、验证结果、缺口 |
| `docs/handoff-2026-10-20-t3.6.md` | v1 | 2026-10-20 | 历史交接：T3.6 波形渲染/对齐、验证结果、缺口 |
| `docs/handoff-2026-10-20-t3.7.md` | v1 | 2026-10-20 | 历史交接：T3.7 步进/拖动音频策略、M3 收口、T4.1 下一步 |
| `docs/handoff-2026-10-20-m3.md` | v1 | 2026-10-20 | 历史交接：M3 基线、提交、验证数字、代码地图、缺口、T4.1/T4.2 |
| `docs/handoff-2026-10-20-t4.1.md` | v1 | 2026-10-20 | 历史交接：T4.1 A/B 数据/交互、F4-A4/A8 验证、下一步 T4.2 |
| `docs/handoff-2026-10-20-t4.2.md` | v1 | 2026-10-20 | 历史交接：T4.2 循环播放/A=B 定帧、F4-A1/A2/A3/A9/A10、下一步 T5.1 |
| `docs/handoff-2026-10-20-t5.1.md` | v1 | 2026-10-20 | 历史交接：T5.1 sidecar 存储、161 断言、只读目录回退、下一步 T5.2 |
| `docs/handoff-2026-10-20-t5.2.md` | v1 | 2026-10-20 | 历史交接：T5.2 标记 UI/跳转、100+ 标记自动化、UI/画面数字、下一步 T5.3 |
| `docs/handoff-2026-10-20-t5.3.md` | v1 | 2026-10-20 | 历史交接：T5.3 备注编辑/关窗 flush/焦点隔离、F6-A1/A2 与 F8-A2 自动化、下一步 T5.4 |
| `docs/handoff-2026-10-20-t5.4.md` | v1 | 2026-10-20 | 历史交接：T5.4 当前帧截图/旋转/目录回退/焦点隔离、F6-A3/A4/A5/A7 自动化范围、下一步 T6.1 |
| `docs/handoff-2026-10-20-t6.1.md` | v1 | 2026-10-20 | 历史交接：T6.1 全屏（F/双击/Esc、几何/焦点恢复、交换链跟随、全屏 S/时间轴、1080p→4K）、F1-A6 自动化范围、下一步 T6.2 |
| `docs/handoff-2026-10-20-t6.2.md` | v1 | 2026-10-20 | 历史交接：T6.2 键位/焦点矩阵（全局步进/Home/End、+/-、文本/模态让位）、75 项 --key-test、下一步 T6.3 |
| `docs/handoff-2026-10-20-t6.3.md` | v1 | 2026-10-20 | 历史交接：T6.3 设置持久化（音量/波形跨会话、原子写、损坏回退、F8-A1 两进程）、52 项 settings 检查、下一步 T6.4 |
| `docs/handoff-2026-10-20-t6.4.md` | v1 | 2026-10-20 | 历史交接：T6.4 DPI/布局（100–200%、响应式 InfoBar/侧栏、`--dpi-test` 9×4 检查）、下一步 T6.5 |
| `docs/handoff-2026-10-20-t2.9.1.md` | v1 | 2026-10-20 | 历史交接：T2.9.1 反向拖动预览（目标帧发布、过期帧丢弃、32 帧缓存）、下一步 T6.5 |
| `docs/handoff-2026-10-20-t6.2.1.md` | v1 | 2026-10-20 | 历史交接：T6.2.1 长按连发（自动重复 KeyRelease、77 项 key-test）、下一步 T6.5 |
| `docs/handoff-2026-10-20-t6.5.md` | v1 | 2026-10-20 | 历史交接：T6.5 状态横幅（六类提示优先级、唯一横幅、其余进状态栏、74 项 `--banner-test`）、下一步 T7.1 |
| `docs/handoff-2026-10-20-t7.1.md` | v1 | 2026-10-20 | 历史交接：T7.1 冷启动埋点与索引后置（P1/P2、`--startup-test`/`--index-test`、扫描中→就绪、标记延后加载） |
| `docs/handoff-2026-10-20-t7.2.md` | v1 | 2026-10-20 | 历史交接：T7.2 内存预算与有界缓存（P10/P11、`FrameCache`、`--memory-test`、切文件释放） |
| `docs/handoff-2026-10-20-t7.2.1.md` | v1 | 2026-10-20 | 历史交接：T7.2.1 B=末帧循环/PgUp 画幅热修（`--loop-end-test`、显示尺寸、下一步 T7.3） |
| `docs/handoff-2026-10-20-t7.3-wip.md` | v1 | 2026-10-20 | 历史 WIP 交接：T7.3 断网中断时的状态 |
| `docs/handoff-2026-10-21-t7.3.md` | v1 | 2026-10-22 | 历史交接：T7.3 90min 单文件 + 1h 子区间循环 pass；152s 异常未复现；V10 2h 未做 |
| `docs/handoff-2026-10-22-t7.4.md` | v1 | 2026-10-22 | 历史交接：T7.4 4K 路径、硬/软解两轮、P7 尖峰与缺口；T7.3 V10 仍待 |
| `docs/handoff-2026-10-22-t7.5.md` | v1 | 2026-10-22 | 历史交接：T7.5 错误与鲁棒性、5s 上界、长路径/只读/设备丢失模拟与缺口 |
| `docs/handoff-2026-10-22-t7.6.md` | v1 | 2026-10-22 | 历史交接：T7.6 打包/关联/许可/体积、每用户安装/卸载、F1-A1 预检与缺口 |
| `docs/handoff-2026-10-22-t7.8.md` | v1 | 2026-10-22 | 历史交接：T7.8 基准回归、26 项漂移；P7 seek 后 PgUp 缺口已由 T7.9 收口/更正 |
| `docs/handoff-2026-10-22-t7.9.md` | v1 | 2026-10-22 | 历史交接：P7 seek 后 PgUp handoff、1080p 10s GOP 边界更正、60 项/30 项基线 |
| `docs/handoff-2026-10-22-t7.10.md` | v1 | 2026-10-22 | 历史交接：合成 V1–V10 片单、V10 soak 启动/采样；收口见 T7.11 |
| `docs/handoff-2026-10-22-t7.11.md` | v1 | 2026-10-22 | 历史交接：合成 V10 soak 收口、V1–V9 冒烟、P7 跨 GOP 边界专项未过；已由 T7.11.1 修复 |
| `docs/handoff-2026-10-22-t7.11.1.md` | v1 | 2026-10-22 | 历史交接：P7 跨 GOP 边界延后回拷修复、2s 64.13ms/1s 27.11ms、全量 60/0；T7.7/正式 V10 待 |
| `docs/handoff-2026-10-22-t7.7.md` | v1 | 2026-10-22 | v1 交接（本地模式）：T7.7 验收清单/预检脚本、本机 precheck blocked、P7 2s 复跑 65.33ms；真实片单/基线机/冷缓存待 |
| `docs/perf/T7.7-acceptance-checklist.md` | v1 | 2026-10-22 | T7.7 F1–F8/P1–P14 执行清单、命令/素材/证据/口径、当前阻塞 |
| `docs/perf/T7.5-robustness.md` | v1 | 2026-10-22 | T7.5 坏文件/网络/长路径/只读 ACL/设备丢失/回归与缺口 |
| `docs/perf/T7.6-packaging.md` | v1 | 2026-10-22 | T7.6 体积/安装/关联/卸载/启动/零网络与限制 |
| `docs/perf/T7.8-bench.md` | v1 | 2026-10-22 | T7.8 8 套件/54 项、26 项同机漂移、P12 哈希、缺口与维护规则；T7.9 更正 1080p 10s GOP 边界 |
| `docs/perf/T7.9-p7-seek-handoff.md` | v1 | 2026-10-22 | T7.9 seek handoff 前后数字、P6/P9 回归、60 项/30 项基线与口径更正 |
| `docs/perf/T7.10-synthetic-suite.md` | v2 | 2026-10-22 | 合成 V1–V10 规格/生成方法/V8 探测；V10 2h soak 收口、V1–V9 冒烟 |
| `docs/perf/T7.11-gop-boundary.md` | v1 | 2026-10-22 | P7 跨 GOP 边界修复前：2s 145.60ms/1s 92.65ms、缓存回拷分项；修复见 T7.11.1 |
| `docs/perf/T7.11.1-gop-cache-defer.md` | v1 | 2026-10-22 | T7.11.1 延后回拷：2s 64.13ms/1s 27.11ms/1080p60 67.31ms、连续 0.03/0.06ms、全量 60/0、表面池/回拷顺序 |
| `docs/next-session-prompt.md` | v28 | 2026-10-22 | 给下个 agent 的新会话提示词（T7.11.1 P7 边界修复；代码 `7af822e`） |
| `docs/next-session-prompt-t7.4.md` | v1 | 2026-10-22 | 给下个 agent 的 T7.4 4K 路径提示词（可直接复制） |
| `docs/perf/T3.1-audio.md` | v1 | 2026-10-20 | T3.1 音频设备/播放/seek/无音轨测量记录 |
| `docs/perf/T3.2-sync.md` | v1 | 2026-10-20 | T3.2 音频主时钟/QPC 回退/24–60fps 同步测量记录 |
| `docs/perf/T3.3-speed.md` | v1 | 2026-10-20 | T3.3 0.25–2x 重采样/音频推进/2x 同步测量记录 |
| `docs/perf/T3.4-av-offset.md` | v1 | 2026-10-20 | T3.4 首包/priming offset、V9 click 对齐测量记录 |
| `docs/perf/T3.5-waveform.md` | v1 | 2026-10-20 | T3.5 10min 波形生成/V9 bucket/无音轨测量记录 |
| `docs/perf/T3.6-waveform-render.md` | v1 | 2026-10-20 | T3.6 波形高度/W、frame100 像素列、2h 波形 p95 记录 |
| `docs/perf/T3.7-audio-policy.md` | v1 | 2026-10-20 | T3.7 步进暂停/单次 seek/target 偏差记录 |
| `docs/perf/T4.2-loop.md` | v1 | 2026-10-20 | T4.2 循环 B→A 耗时、B+1、定帧 5s、长 GOP 可用档记录 |
| `docs/perf/T5.2-marks.md` | v1 | 2026-10-20 | T5.2 100+ 标记、重开/删除/隔离、点击 UI 与画面耗时、列表/时间轴 p95 |
| `docs/perf/T5.2-marks-ui.png` | v1 | 2026-10-20 | 101 标记 + 时间轴标记截图（视频区黑为原生 HWND 截图限制） |
| `docs/perf/T5.2-marks-warning.png` | v1 | 2026-10-20 | 损坏 sidecar 警告截图 |
| `docs/perf/T5.3-notes.md` | v1 | 2026-10-20 | T5.3 备注编辑/关窗重开/空备注/确认对话框/焦点隔离/保存 p95 |
| `docs/perf/T5.3-note-ui.png` | v1 | 2026-10-20 | 备注编辑区 + 侧栏截图（视频区黑为原生 HWND 截图限制） |
| `docs/perf/T5.4-snapshots.md` | v1 | 2026-10-20 | T5.4 3 连拍/播放中/4K/旋转/回退/焦点隔离与工作集记录 |
| `docs/perf/T5.4-rotated-90.png` | v1 | 2026-10-20 | 90° display matrix 四色象限截图（180×320） |
| `docs/perf/T6.1-fullscreen.md` | v1 | 2026-10-20 | T6.1 全屏进出/交换链/时间轴/截图/焦点/最大化/4K 切换与回归记录 |
| `docs/perf/T6.1-fullscreen-layout.png` | v1 | 2026-10-20 | 全屏布局截图（时间轴/帧号/状态栏；视频区黑为原生 HWND 限制） |
| `docs/perf/T6.2-keys.md` | v1 | 2026-10-20 | T6.2 §3.7 全表、焦点矩阵、长按/自动重复、文本/模态让位与回归记录 |
| `docs/perf/T6.3-settings.md` | v1 | 2026-10-20 | T6.3 默认路径/JSON/原子写、保存 p95、F8-A1 两进程、执行环境限制与回归记录 |
| `docs/perf/T6.4-dpi.md` | v1 | 2026-10-20 | T6.4 100/125/150/200% 几何、9×4 检查、高 DPI 全屏与截图记录 |
| `docs/perf/T6.4-dpi-100.png` | v1 | 2026-10-20 | 100% 正常窗口布局（视频区黑为原生 HWND 截图限制） |
| `docs/perf/T6.4-dpi-150.png` | v1 | 2026-10-20 | 150% 正常窗口布局 |
| `docs/perf/T6.4-dpi-200.png` | v1 | 2026-10-20 | 200% 正常窗口、InfoBar 两行 |
| `docs/perf/T6.4-dpi-200-narrow.png` | v1 | 2026-10-20 | 200% 下 820 宽窄窗口布局 |
| `docs/perf/T6.5-banners.md` | v1 | 2026-10-20 | T6.5 六类提示优先级、唯一横幅、100/200/820 几何、74 项检查、切换成本与缺口 |
| `docs/perf/T6.5-banner-100-all.png` | v1 | 2026-10-20 | 100% 六类全开：错误横幅 + 5 条进状态栏 |
| `docs/perf/T6.5-banner-100-10bit.png` | v1 | 2026-10-20 | 100% 10-bit 横幅 + 状态栏无音频 |
| `docs/perf/T6.5-banner-200-narrow.png` | v1 | 2026-10-20 | 200% 820 窄窗：VFR 横幅 + 4 条状态栏提示 |
| `docs/perf/T2.9.1-drag-backward.md` | v1 | 2026-10-20 | T2.9.1 反向拖动根因、三素材数字、回归与缺口 |
| `docs/perf/T6.2.1-key-repeat.md` | v1 | 2026-10-20 | T6.2.1 长按自动重复 KeyRelease、77 项、27 帧、回归与缺口 |
| `docs/perf/T7.1-cold-start.md` | v1 | 2026-10-20 | T7.1 P1/P2 20 次、分段预算、索引后置前后、扫描中→就绪、缺口 |
| `docs/perf/T7.2-memory.md` | v1 | 2026-10-20 | T7.2 P10/P11、帧缓存预算/步进、切文件释放、结构上限、回归与缺口 |
| `docs/perf/T7.2.1-loop-renderer.md` | v1 | 2026-10-20 | T7.2.1 B=末帧循环根因/数字、1080p 1088 纹理与渲染尺寸、回归与缺口 |
| `docs/perf/T7.3-soak.md` | v1 | 2026-10-22 | T7.3 循环/单文件 soak、WS 漂移、90min+1h pass、下包络判定与缺口 |
| `docs/perf/T7.4-4k.md` | v1 | 2026-10-22 | T7.4 4K proxy 拖动/松手/步进/长按/跳转/内存、硬解与软解、P7 尖峰与缺口 |
| `docs/product-v2-vision.md` | v1.1 | 2026-10-23 | v2 产品愿景；五个决定（在线语义/Cookie/清晰度缓存/合规边界/成功指标）；DEC-6 扫码登录改为 MVP 增强 |
| `docs/adr/ADR-007-bilibili-online.md` | v0.1 | 2026-10-22 / 2026-10-23 | v2 在线取流/缓存/可增长读层/DASH 双输入；**附条件接受**（T8.2/T8.4 回填；D1–D5 未改） |
| `docs/perf/v2-online-spike.md` | v0.2 | 2026-10-23 | S2 测量：档位矩阵（匿名 + 普通用户）、Range/吞吐、可增长读层/取消、失败码、go/no-go |
| `docs/handoff-2026-10-23-s1s2.md` | v1 | 2026-10-23 | S1/S2 交接：五决定、硬事实、未验证项、S3 拍板点与提示词 |
| `docs/spec-versioning.md` | v1.0 | 2026-10-23 | 文档分层与版本规则：v1 冻结 / v2 增量 / 完整品定义 / 任务分家（tasks-v2.md） |
| `docs/spec-v1.0-delta.md` | v0.4 | 2026-10-23 | v2 在线模式增量验收：O1–O11、OP1–OP10、缓存/错误/隐私、OB 片单、B 大会员条件验收、DEC-1…DEC-6 已拍板（DEC-6 扫码登录，非门）；未并入 spec.md |
| `tasks-v2.md` | v0.29 | 2026-10-24 | v2 任务：T8.0–T8.16 完成、**M1/M2 通过**；T8.6.1a/T8.6.1b/T8.3.1/T8.16 完成；**T8.15 完成（partial，非 MVP 门；1080P60 AVC 自动/OP9 47.5ms、4K 仅手动/能播/逐帧、60/4K→1080P30 切档；A3 unverified/720P60 未返回）**；DASH 1080P 55/61.18fps、未缓存提示/取消、缓存命中 55/58.82fps、durl 55/60.04fps、本地 p4 61.8fps；`ctest` 36/36、key 77/0、longpress 54/0；M1/M2 门与数字未改；**不写 MVP 通过** |
| `docs/perf/local-drag-fast-2026-10-24.md` | v1 | 2026-10-24 | 本地拖动预览快拖热修证据：`media\测试.mp4` 1080p/GOP30 基线 28.6fps/93.8ms → 61.0fps/31.7ms；反向 42.2fps/48.5ms；P4/P6/ctest/key 回归；反向 -20 残余 |
| `docs/handoff-2026-10-24-drag-fast.md` | v1 | 2026-10-24 | 交接：`PreviewDecoder` session/generation 分离、正向追最新目标、反向解完/粗帧、`--drag-fast`；残余与下一步硬件预览 P1 |
| `docs/plan-drag-preview-v2.md` | v1.1 | 2026-10-24 | 拖动预览 v2 规划：硬件预览/反向缓存/策略/验收/风险；DP-3 完成记录；非门，不改 M1/M2/P4/P7 |
| `docs/tasks-drag-preview-v2.md` | v1.4 | 2026-10-24 | DP-0/DP-1/DP-2/DP-3/DP-4 完成（DP-4 dist 34.55MB/25、Medium 副本；强制软件快反向与 4K 残余）；每任务一会话，入口/验收/停止/交付 |
| `docs/perf/drag-preview-dp1.md` | v1.0 | 2026-10-24 | DP-1 证据：spike、共享设备硬解、回退原因、`+60` 61.0–76.3fps/36–46ms、`-60` 56.8–60.7fps/32–38ms、P4 64.1fps、target_lag、4K/强制软件记录、ctest/key/T8.6.1b |
| `docs/handoff-2026-10-24-dp1.md` | v1 | 2026-10-24 | DP-1 交接：实现/设备生命周期/回退、验证数字、残余、DP-2 下一步；4K 硬解关闭 |
| `docs/perf/drag-preview-dp2.md` | v1.0 | 2026-10-24 | DP-2 证据：环缓存/粗帧缓存预算、反向命中/预取/远跳粗帧、`-60` 60.2–69.2fps/25.2–33.0ms、`-20` 45.7–48.7fps、`-1` 命中 84.8–86.4%、首粗帧 19.9–40.7ms（硬件精确首帧 20–21ms）、峰值 233.4MiB、软件快反向残余、ctest 37/37 |
| `docs/handoff-2026-10-24-dp2.md` | v1 | 2026-10-24 | DP-2 交接：实现/预算/命中率口径/粗帧单调约束/预取隔离、验证数字、残余、DP-3 下一步；硬件帧不缓存 |
| `docs/perf/drag-preview-dp3.md` | v1.0 | 2026-10-24 | DP-3 证据：DragStrategy 四模式/阈值、慢拖 lag p95 0、快拖 +60 76.9–80.8fps、-60 54.4–64.4fps、-20 47.3–49.2fps、远跳粗帧 20.8/20.6ms→精确 30.6/30.0ms、P4/P6/P7/P8/P9、峰值 233.4MiB、软件残余、4K 只记录、ctest 37/37 |
| `docs/handoff-2026-10-24-dp3.md` | v1 | 2026-10-24 | DP-3 交接：模式判定/Exact 关粗帧/FarJump 闸门/精确后禁粗帧、验证数字、残余、DP-4 下一步；不改 P4/P6/P7 |
| `docs/perf/drag-preview-dp4.md` | v1.0 | 2026-10-24 | DP-4 证据：基线复跑、clean Release 构建/ctest 37/37、dist 34.55MB/25、重建后矩阵、缓存 233.4MiB、Medium 副本、zip/setup、软件快反向/4K 残余 |
| `docs/handoff-2026-10-24-dp4.md` | v1 | 2026-10-24 | DP-4 交接：发布/回归/Medium 验证、数字、环境坑（ctest TEMP/E: 策略/P4 计时器）、残余、不要做；DP-0…DP-4 收口 |
| `docs/next-session-prompt-dp3.md` | v1 | 2026-10-24 | 历史提示词（DP-3 已执行）：交互策略 + 验收探针；保留作口径参考 |
| `docs/next-session-prompt-dp4.md` | v1 | 2026-10-24 | 下一会话提示词：DP-4 集成/发布；含 DP-3 基线/边界/回归、dist/Medium 验证、停止/交付/不要做 |
| `docs/handoff-2026-10-24-drag-preview-plan.md` | v1 | 2026-10-24 | DP 规划交接：基线 `079f0dd`、现状/瓶颈/指标、DP-1 先做硬件预览 spike |
| `docs/next-session-prompt-dp1.md` | v1 | 2026-10-24 | 下一会话提示词：DP-1 硬件预览 + 诊断；含基线/文件/验收/停止条件 |
| `docs/next-session-prompt-dp2.md` | v1 | 2026-10-24 | 下一会话提示词：DP-2 反向缓存/预取/远跳粗帧；含 DP-1 基线/预算/验收/停止/交付 |
| `docs/handoff-2026-10-23-s3.md` | v1.2 | 2026-10-23 | S3 交接：T8.0 完成、DEC/B 拍板结果、ADR 附条件接受、S4 从 T8.1 开始 |
| `docs/perf/v2-online-resolve.md` | v1.0 | 2026-10-23 | T8.1 解析层证据：BV/URL、匿名 view、分P、错误矩阵、取消/超时、本地回归、dist 导入表 |
| `docs/handoff-2026-10-23-t8.1.md` | v1 | 2026-10-23 | 历史交接：T8.1 接口、156 项探针、缺口、T8.2 所需账号/网络/curl 对照 |
| `docs/perf/v2-online-http-selfcheck.md` | v1.0 | 2026-10-23 | **当前证据**：T8.2 HTTP 栈自检/凭据/DPAPI/DEC-3/脱敏；门 WinHTTP nav 通过（161–202ms、`cookie_sent=1`） |
| `docs/handoff-2026-10-23-t8.2.md` | v1 | 2026-10-23 | 历史交接：T8.2 接口、104/0 单测、54/0 产品探针、缺口、T8.3/T8.5 需要；提交 `0f322b0` |
| `docs/perf/v2-online-cache.md` | v1.0 | 2026-10-23 | **当前证据**：T8.3 缓存存储/配额；300MB LRU（283,116,589→188,744,706）、20GB 记账（10,737,418,599 ≤ 21,474,836,480）、续传/损坏重建/清空/磁盘满/跨会话/长路径；不联网 |
| `docs/handoff-2026-10-23-t8.3.md` | v1 | 2026-10-23 | 历史交接：T8.3 接口、366/0 单测、32/0 产品探针、缺口、T8.4/T8.5 需要；提交 `8483e24` |
| `docs/perf/v2-online-growing.md` | v1.0 | 2026-10-23 | **当前证据**：T8.4 可增长读层；0.3MB 首帧、100/100 seek、未缓存等待 +18.5%、取消 0.15/0.24ms、4 读者 10min pass；EOF/EIO/EXIT、淘汰 busy；不联网 |
| `docs/handoff-2026-10-23-t8.4.md` | v1 | 2026-10-23 | **当前交接**：T8.4 接口/数字/决定/缺口、T8.5 与 T8.6 需要；一个 commit |
| `docs/perf/v2-online-m1.md` | v2.0 | 2026-10-23 | **当前证据**：T8.5 durl 取流 + **T8.8 M1 门（通过）**；qn/fourk 矩阵 20 单元（OB-2 qn=64→360P、qn=80/112/120→1080P，fourk 无影响）；最小修正 qn=80/fourk=0；3/3 实际 64/80/64、ffprobe pass；E2E 29/0×3、OP1 p95 648/532/527ms、OP2 34/51/34ms、OP3 0ms+落点 0、OP5 95ms/0 请求；续传/清缓存/DEC-5/本地 O9；T8.5 历史 §1–§10 保留 |
| `docs/handoff-2026-10-23-t8.5.md` | v1 | 2026-10-23 | 历史交接：T8.5 接口/数字/决定/缺口、T8.6/T8.7 需要；M1 3/3 blocked；一个 commit |
| `docs/perf/v2-online-session.md` | v1.0 | 2026-10-23 | **当前证据**：T8.6 在线会话/UI；真实 3 条 E2E、冷缓存首帧 20×3 p95 624/725/459ms、P7 p95 20/20/25ms、seek p95 79/59/97ms、UI gap 60/61/62ms、缓冲耗尽/取消/finalize、本地 P1/P2/P7、ctest 29/29、dist 33.57MB/25；**OB-2 360P → M1 blocked** |
| `docs/handoff-2026-10-23-t8.6.md` | v1 | 2026-10-23 | 历史交接：T8.6 接口/线程/决定/数字/缺口、T8.7 需要；M1 3/3 blocked；一个 commit |
| `docs/perf/v2-online-cache-hit.md` | v1.0 | 2026-10-23 | **当前证据**：T8.7 缓存命中/在线记录；0 playurl/CDN/`.part`、`lapian-online-1`、截图 `index.jsonl`、清缓存保留/重建回访/损坏不阻塞/换档 PTS；workspace 45/0、真实缓存 44/0、Medium 默认 AppData 31/0×3（首帧 74–84ms、0 TCP/UDP、无 winhttp/wininet/urlmon）；本地 P1/P2 296.81/345.40ms、P7 跨 GOP 66.56ms；**OB-2 360P → M1 blocked** |
| `docs/handoff-2026-10-23-t8.7.md` | v1 | 2026-10-23 | T8.7 实现交接：接口/记录格式/线程/决定/数字/缺口、T8.8 需要；M1 3/3 blocked；一个 commit |
| `docs/handoff-2026-10-23-t8.8-m1.md` | v1 | 2026-10-23 | **当前交接**：T8.8 M1 门（通过）；矩阵/最小修正/3-3/OP1–OP5/失败矩阵/DEC-5/本地 O9、两个非产品测试前置问题、缺口与下一步 T8.9（等确认）；未进 M2 |
| `docs/handoff-2026-10-23-t8.8-start.md` | v1 | 2026-10-23 | **当前交接**：T8.8 启动；M1 门顺序/验收清单、OB-2 `qn=120+fourk=1` 复核线索（S2 曾得 80）、参数矩阵、pass/blocked 决策树、Medium/凭据约束 |
| `docs/perf/v2-online-interaction.md` | v1.3 | 2026-10-24 | **当前证据**：T8.6.1a + hotfix + 用户实机确认；**§8 T8.6.1b 在线拖动预览**：DASH 1080P30 已缓存 55 presented/54.95fps total/61.18 active、UI gap 69ms/0、stale 0、松手 frame 75/PTS 2,500,000；未缓存目标 3312 提示 0ms/waiting/取消；缓存命中 56/59.89fps、durl 55/60.04fps、本地 p4 61.8fps；ctest 36/36、key 77/0、longpress 54/0；M2 尝试被 `vip_status=1` 拒，引用 T8.14 |
| `docs/handoff-2026-10-24-t8.6.1b.md` | v1 | 2026-10-24 | **当前交接**：T8.6.1b 在线拖动预览接口/线程/取消/generation/内存上界、DASH/cache-hit/durl/本地证据、回归/缺口；非门；下一步 T8.15（已完成，partial）/发布前审计 |
| `docs/handoff-2026-10-24-t8.15-start.md` | v1 | 2026-10-24 | **T8.15 启动交接（历史，已被完成交接取代）**：用户已确认大会员账号（默认凭据 `nav vip_status=1`）；启动时 T8.15 尚未执行；O11-A1…A7/OP9/OP10、账号/Medium 约束、建议 `--online-vip-test`、测试/停止条件/交付；不写 MVP 通过 |
| `docs/perf/v2-online-vip.md` | v1.0 | 2026-10-24 | **当前证据**：T8.15 大会员可用档条件验收（**partial，非 MVP 门**）；默认 DPAPI `vip_status=1`；OB-2 大会员 qn=116 AVC 1920x1080@62.500fps/4.22Mbps（HEVC 58.824/AV1 60.150；74/120 不存在）；4K SDR `BV1afnv6GEDA` qn=120 AVC/HEVC/AV1 2160x3840@60；A1 pass、A2 partial（1080P60 AVC 自动、20 步 PgUp/PgDn ±1、p95 47.5ms、hw=1；720P60 未返回）、A3 unverified（无 HEVC/AV1-only 样本；提示缺口）、A4 pass（仅手动/能播/逐帧/无 P7-P9 承诺）、A5 pass（116→80/120→80 PTS 映射、标记/记录/缓存保留）；ctest 36/36、key 77/0、P1/P2/P7 0 超差、dist 34.48MB/25；不写 MVP 通过 |
| `docs/handoff-2026-10-24-t8.15.md` | v1 | 2026-10-24 | **当前交接**：T8.15 判定 partial、A1–A7 逐项、真实目录/P7/4K/切档数字、复现命令、回归（ctest/key/bench）、A3/720P60 未验证、瞬时缓存失败残余、改动文件与下一步；**不改 M1/M2 门/数字/OB/schema**；一个 commit |
| `docs/next-session-prompt-t8.15.md` | v1 | 2026-10-24 | **下个 agent 提示词**：T8.15 大会员可用档条件验收（非门）；基线 `b11e45a`（当前 HEAD 见 `git log`）、账号已具备、逐项 pass/partial/fail/unverified、禁止改 M1/M2 门/数字/OB/schema、不写 MVP 通过 |
| `docs/handoff-2026-10-24-t8.6.1a.md` | v1.2 | 2026-10-24 | **当前交接**：T8.6.1a 接口/状态机 + hotfix + 用户实机确认；首次 open 已量（44MB 首帧 634ms/512KB）；下一步 T8.9（M2，已确认）；未改 M1/M2 门与数字 |
| `docs/perf/v2-online-cache-manage.md` | v1.0 | 2026-10-24 | **当前证据**：T8.3.1 缓存管理（设置页目录/配额/打开目录/清空/启动+6h 孤儿清理）；workspace 72/0（无 UI）/98/0（UI）、Medium 默认 AppData **101/0**（integrity=medium）、只读 ACL 75/0、磁盘满 0.116–0.291ms；真实 AppData 与备份逐字节一致 |
| `docs/handoff-2026-10-24-t8.3.1.md` | v1 | 2026-10-24 | **当前交接**：T8.3.1 `CacheManager`/设置对话框/清空保留当前会话/启动+6h 清理/只读与磁盘满可读错误；接口、数字、决定、缺口、T8.9 起点；非门；一个 commit |
| `docs/perf/v2-online-quality.md` | v2.0 | 2026-10-24 | **当前证据**：T8.12 档位策略/降帧提示；普通用户 3/3 DASH 1080P AVC qn=80（30.000/29.412/25.000）、OB-2 29.412fps 横幅+角标（可关、角标保留）、OB-1/2/3 1080→720 成功（PTS 1,000,000→第 30 帧、标记 1→1、截图 qn64/codecid7/fps29.412、点击 61/75/76ms）、真 60/4K 不可得/未验证；quality 45/46/45 + UI 50/56/50、ctest 34/34；未判 M2 |
| `docs/handoff-2026-10-24-t8.9.md` | v1 | 2026-10-24 | **当前交接**：T8.9 接口/数字/决定/缺口、T8.10 需要；非门；一个 commit |
| `docs/perf/v2-online-faults.md` | v1.0 | 2026-10-24 | **当前证据**：T8.13 网络异常/并发加固；10 窗口场景 + 15 组离线注入、12 行矩阵、错误 ≤5s（3,981–4,529ms）、关闭 7–66ms、UI gap 73ms/0、4 读者 10min（timeouts 0/close 16ms）、缓存损坏自动重建、磁盘不足 5ms、脱敏；真实休眠未验证；未判 M2 |
| `docs/handoff-2026-10-24-t8.13.md` | v1 | 2026-10-24 | **当前交接**：T8.13 接口/4 个产品修正/数字/决定/残余风险、T8.14（M2 门）需要；非门；一个 commit |

| `docs/perf/v2-online-m2.md` | v1.0 | 2026-10-24 | **当前证据**：T8.10 DASH 双输入；spike（总字节 206/双容器/首帧/seek）、3/3 1080P AVC+AAC、首帧 322.2/369.0/365.1ms、60s 音画偏差 p95 17.1/18.2/22.0ms、20/20 seek 帧号+PTS、EOF、内存峰值 211.5/209.7/201.7MB、缓存命中 45–46ms/0 网络；音频缓存键 `audio-<id>`；fMP4 seek 修复；`--force-durl` 回退；未判 M2 |
| `docs/perf/v2-online-packaging.md` | v1.0 | 2026-10-24 | **当前证据**：T8.16 打包/隐私/卸载；安装/卸载 0 TCP/UDP、P12、Medium、默认缓存/凭据、卸载缓存清理；Inno 卸载器静态 winhttp 残余、20GB 未真删 |
| `docs/handoff-2026-10-24-t8.16.md` | v1 | 2026-10-24 | **当前交接**：T8.16 结论/数字/证据/缺口；T8.15 未验证/T8.6.1b 待；非 MVP 门 |
| `docs/handoff-2026-10-24-t8.10.md` | v1 | 2026-10-24 | **当前交接**：T8.10 接口/数字/决定/缺口、T8.11 需要；非门；一个 commit |
| `docs/perf/v2-online-index.md` | v1.0 | 2026-10-24 | **当前证据**：T8.11 渐进索引/波形/总帧数；增量 builder（前沿 guard/resume_pos/快照/tail guard 48/统计）；OB-3 真实在线 27/0（首次索引 128ms、30s 内容 6.209s、20/20 seek 落点 0、exact 5306、波形 0.525 帧、UI gap 58ms、opens=1/reopens=0/read_ratio=1.000）、离线 2MB/s 22/0、单测 28/0；ctest 33/33；未判 M2 |
| `docs/handoff-2026-10-24-t8.11.md` | v1 | 2026-10-24 | **当前交接**：T8.11 接口/数字/决定/缺口、T8.12 需要；非门；一个 commit |
| `docs/handoff-2026-10-24-t8.12.md` | v1 | 2026-10-24 | **当前交接**：T8.12 档位策略/手动切档/降帧提示；接口、数字、决定、缺口、T8.13 起点；非门；一个 commit |
| `docs/next-session-prompt-t8.16.md` | v1 | 2026-10-24 | **当前提示词**：T8.14 后刷新，基线 `0dc71ae`；新会话 T8.16（打包/隐私/卸载，非 MVP 门）；安装到 `%LOCALAPPDATA%\Programs\拉片软件`、Medium/默认 AppData、安装/卸载 0 网络、P12、许可/隐私文本、卸载缓存清理选项、回归/停止条件 |
| `docs/next-session-prompt-t8.14.md` | v1 | 2026-10-24 | 历史提示词（T8.14 已执行）：T8.13 后刷新，基线 `01b98d5`；6 条硬条件、OP1–OP8（DEC-2）、OP4 5min 缺探针需最小补探针、OP5 需 Medium 副本、失败矩阵引用 T8.13、本地 O9/F1–F8、停止条件 |
| `docs/next-session-prompt-t8.13.md` | v1 | 2026-10-24 | 历史提示词（T8.13 已执行）：T8.12 后刷新，基线 `cbf4f31`；≥10 故障矩阵、`--online-fault-test`、错误 ≤5s、关闭 ≤1s、4 读者 10min、真实休眠写未验证 |
| `docs/next-session-prompt-t8.12.md` | v1 | 2026-10-24 | 历史提示词（T8.12 已执行）：T8.11 后刷新，基线 `a6cdf6d`；新会话 T8.12（档位策略/降帧提示）；fps 口径/权限列档/手动切档/PTS 映射/降帧横幅+角标/停止条件 |
| `docs/next-session-prompt-t8.11.md` | v1 | 2026-10-24 | 历史提示词：T8.10 后刷新，基线 `b560607`；T8.11（渐进索引/波形/总帧数） |
| `docs/next-session-prompt-t8.10.md` | v1 | 2026-10-24 | 历史提示词：T8.9 后刷新，基线 `a3cf655`；新会话 T8.10（DASH 双输入）；边界/验收/代码入口/spike 清单/音频缓存键建议/停止条件 |
| `docs/next-session-prompt-t8.9.md` | v2 | 2026-10-24 | 历史提示词：T8.3.1 后刷新，基线 `cbab29b`；新会话 T8.9（M2 DASH 解析/档位）；边界、验收、代码入口、工作区/默认路径约束、停止条件 |
| `docs/next-session-prompt-t8.8.md` | v1 | 2026-10-23 | 历史提示词：下一个 agent 的 T8.8（先做 durl qn/fourk 矩阵，再决定最小取流修正/blocked；3/3、OP1–OP5、失败矩阵、O9、停止条件） |
| `docs/perf/v2-online-qr.md` | v1.0 | 2026-10-23 | **当前证据**：T8.2.2 扫码登录；官方 passport generate/poll、本地 QR（v4/33 modules/246px）、4 态/180s/取消 16ms、SESSDATA-only/DPAPI/nav 门；115/0、19/0（UI 21/0，dist 同）；**真实扫码未验证**；非门 |
| `docs/handoff-2026-10-23-t8.2.2.md` | v1 | 2026-10-23 | **当前交接**：T8.2.2 接口/状态机/数字/缺口/回退；真实扫码未验证；非门；一个 commit |
| `docs/next-session-prompt-t8.3.md` | v1 | 2026-10-23 | 下一个 agent 的 T8.3 提示词（缓存存储/配额/续传；300MB/20GB、长路径、停止条件） |
| `docs/next-session-prompt-t8.4.md` | v1 | 2026-10-23 | 下一个 agent 的 T8.4 提示词（可增长读层；0.3MB 首帧/100 seek/取消 ≤100ms/4 读者 10min；离线测试、停止条件） |
| `docs/next-session-prompt-t8.5.md` | v1 | 2026-10-23 | 下一个 agent 的 T8.5 提示词（durl 720p；playurl/Range/续传/3 备份/过期刷新；3/3、首帧、停止条件） |
| `docs/handoff-2026-10-23-t8.6-start.md` | v1 | 2026-10-23 | **T8.6 启动交接**：当前路径-only 媒体层、部分索引、UI 线程阻塞、完成改名 vs reader、音频/波形五个硬缺口；方案 A/B/C、阶段 1/2/3、停止条件 |
| `docs/next-session-prompt-t8.6.md` | v1 | 2026-10-23 | 下一个 agent 的 T8.6 提示词（在线会话/UI；首帧/缓冲/seek/波形/标记/截图；先 spike、可拆阶段） |
| `docs/next-session-prompt-t8.7.md` | v1 | 2026-10-23 | 历史提示词：T8.7（缓存命中/在线记录；断网 0 请求、标记/截图/index.jsonl、清缓存保留；Medium 副本测默认路径；停止条件） |
| `docs/next-session-prompt-t8.2.2.md` | v1 | 2026-10-23 | 下一个 agent 的 T8.2.2 提示词（扫码登录；官方 QR generate/poll、本地渲染、只保留 SESSDATA；非门） |
| `docs/next-session-prompt-t8.2.md` | v1 | 2026-10-23 | 下一个 agent 的 T8.2 提示词（HTTP 栈自检、SESSDATA/DPAPI、DEC-3、日志脱敏、门/停止条件） |

**变更记录**

- 2026-10-19：完成定义阶段对话（Q1–Q10 + v0.2 补充确认），产出 `spec.md` v0.2；本文件建立。
- 2026-10-19：完成规划阶段（Q1–Q8 + plan/tasks/ADR），用户确认按推荐执行；本文件升至 v1.1，进入实现阶段。
- 2026-10-19：完成 M0（T0.1–T0.3）；T0.3 初步 No-Go（长 GOP），停在 M2 前等决策；本文件升至 v1.2，新增交接文档。
- 2026-10-19：完成 T1.1 媒体探测；真实素材补测确认 1s/2s/8.3s GOP 的 p95 为 28/76/230ms；用户拍板方案 A；`spec.md` 升 v0.3；本文件升至 v1.3。
- 2026-10-19：完成 T2.1 CFR 帧数学/总帧数/NDF 时间码（纯 C++ + 单测）；本文件升至 v1.4。
- 2026-10-19：完成 T1.2 视频解码（D3D11VA + 软解回退、首帧/PTS、10-bit 提示）；本文件升至 v1.5。
- 2026-10-19：完成 T1.3 D3D11 渲染（NV12/P010、fit/resize、旋转、上一帧保持、共享设备）；本文件升至 v1.6。
- 2026-10-19：完成 T1.4 打开入口（Ctrl+O、拖拽、命令行、空态、错误提示、失败保留旧会话）；本文件升至 v1.7。
- 2026-10-19：完成 T1.5 播放/暂停与 QPC 视频时钟（PTS 调度、末帧停止、Space）；本文件升至 v1.8。
- 2026-10-19：完成 T1.6 信息栏 v1（文件名/分辨率/帧率/当前帧/总帧三态/NDF 时间码/VFR 横幅）；本文件升至 v1.9。
- 2026-10-19：完成 T2.2 PTS 索引（PTS/关键帧/重复去重/二分/三态/解码校验降级）；本文件升至 v2.0。
- 2026-10-19：完成 T2.3 精确 seek（关键帧 backward、解码到目标帧、降级计数、generation/cancel）；本文件升至 v2.1。
- 2026-10-19：完成 T2.4 逐帧步进（±1/边界/暂停/同步播放位置）；本文件升至 v2.2。
- 2026-10-19：完成 T2.5 长按连发（300ms/70ms、忽略 OS 重复、焦点丢失停止）与 T2.6 帧号/时间码输入（Ctrl+G、整数/时间码、clamp、VFR 最近帧）；本文件升至 v2.3。
- 2026-10-20：完成交接文档 `docs/handoff-2026-10-20.md`，记录 M1/M2 帧闭环、验证数字、已知缺口与 T2.7 下一步；本文件升至 v2.4。
- 2026-10-20：完成 T2.7 时间轴渲染（帧刻度/标签、滚轮与 +/- 缩放、锚点、横向滚动、播放头、只画可见区、8px/帧上限）；新增 `lapian_timeline_test`，`ctest` 9/9；43.2 万帧离屏绘制 p95 0.167–0.227ms；本文件升至 v2.5。
- 2026-10-20：完成 T2.8 时间轴指针交互（floor 命中、物理像素对齐、按下即暂停、拖动目标帧语义、松手精确落帧、失败回滚）；`ctest` 9/9；真实窗口点击/拖动/播放中点击手测通过；本文件升至 v2.6。
- 2026-10-20：完成 T2.9 拖动预览解码器（独立软件解码线程、关键帧 seek、latest-frame、generation、1280 降采样、UI 16ms 轮询）；`ctest` 10/10；开发机 proxy-V1 首预览 31ms、2s 拖动发布 49–53fps；真实窗口拖动预览/松手精确通过；本文件升至 v2.7。
- 2026-10-20：完成 T2.10 拖动集成与松手精确（取消预览、清空 mailbox、松手无条件精确 GoToFrame、保持暂停、10s 拖动探针）；`ctest` 10/10；开发机单样本 1080p 1s GOP 17ms、4K 2s GOP 257ms、长 GOP 351ms 可用档；10s/330 次拖动无 >200ms tick；音频静音待 T3.7；本文件升至 v2.8。
- 2026-10-20：完成 M2 交接文档 v2（docs/handoff-2026-10-20.md），记录 T2.7–T2.10、验证数字、缺口与 T3.1 下一步；本文件升至 v2.9。
- 2026-10-20：完成 T3.1 音频解码与 WASAPI（首轨、shared mode、音量/静音、无音轨/设备不可用降级、设备失效重建、软件增益）；新增 `lapian_audio_test`，`ctest` 11/11；开发机 48kHz/2ch float32，V9 4s、proxy-V1 10s、seek 5s 均 0 underrun；`dist` 31.91MB/25 文件；设备拔出物理手测、V1 正式素材与 T3.2 同步仍待；本文件升至 v3.0。
- 2026-10-20：完成 T3.2 媒体时钟与同步（IAudioClock 主时钟、GetFrequency 单位换算、QPC 回退、按媒体时间调度、落后丢帧、`MediaClock`）；新增 `lapian_clock_test`，`ctest` 12/12；开发机 60s 24fps max 9.33ms/p95 4.68ms、30fps max 6.04ms、60fps max 7.67ms、无音轨 QPC max 4.43ms；2x/10min/V1/基线机仍待 T3.3/T7.7；本文件升至 v3.1。
- 2026-10-20：完成 T3.3 变速与变调（0.25–2x 档位、输出率 D/rate、时钟速率、音高随速度、实时切换、5ms 淡入淡出）；`ctest` 12/12；2x 60s 媒体 max 11.93ms/p95 9.51ms、0.5x max 2.32ms、0.25/0.5/2x 音频探针 0 underrun；听感/V1/循环重置仍待；本文件升至 v3.2。
- 2026-10-20：完成 T3.4 A/V 偏移（首包 PTS + skip_samples/priming、`MediaInfo.av_offset_us`、音频起播 seek 偏移、V9 click 校验）；`ctest` 12/12；V9 offset 0、click 4.169s vs frame100 4.1667s（2.3ms）；synthetic 100ms 片探测 +78ms；波形映射/T3.5 仍待；本文件升至 v3.3。
- 2026-10-20：完成 T3.5 后台波形生成（WaveformData 200 bucket/s min/max、独立线程/AudioDecoder、进度/取消、无音轨）；`ctest` 13/13；开发机 10min 391.5ms/120004 桶，V9 click bucket 对齐；绘制/对齐/W 开关仍待 T3.6；本文件升至 v3.4。
- 2026-10-20：完成 T3.6 波形渲染（时间轴 56px 波形带、W 开关、按帧 PTS 映射、T3.4 offset、MinMaxRange 按像素聚合、无音频空态）；`ctest` 13/13；frame100 bucket 像素断言通过、隐藏 128→72、2h 波形 fit p95 0.340ms；真人视觉/VFR/真实 2h 仍待；本文件升至 v3.5。
- 2026-10-20：完成 T3.7 步进/拖动音频策略（暂停时钟/不逐次 seek、恢复一次 `PlayFrom`、5ms 淡变、`--step-test`）；`ctest` 13/13；V9 step 后 target 1.167s vs frame28 1.1667s（0.33ms）；A=B 定帧明确留 T4.2；本文件升至 v3.6。
- 2026-10-20：完成 T4.1 A/B 数据与交互（`LoopRange` 校验/清除、I/O/L/Shift+L、时间轴色条/A/B 标签/循环高亮、状态栏循环状态、`--ab-test`）；新增 `lapian_loop_range_test`，`ctest` 14/14；proxy-V1 自动化验证 F4-A4/A8；`dist` 31.96MB/25 文件；本文件升至 v3.8。
- 2026-10-20：完成 T4.2 循环播放与 A=B 定帧（B+1 前回 A、时钟/音频重置、frozen、Space 语义、L/Shift+L/步进解除、`--loop-test`）；`ctest` 14/14；短 GOP 真素材 B→A 13–20ms/帧、4 次循环 A/B 不变、定帧 5s 音频时钟无效；长 GOP proxy 148–157ms 列可用档；`dist` 31.97MB/25 文件；本文件升至 v3.9。
- 2026-10-20：完成 T5.1 sidecar 存储（schema v1、路径规范化/SHA-256、原子写、AppData 回退、id+updated_at 合并、损坏备份、空文件删除、frame+pts 校正、真实 ACL 只读目录手测）；新增 `lapian_marks` 与 `lapian_mark_store_test`；`ctest` 15/15；`dist` 31.97MB/25 文件；本文件升至 v4.0。
- 2026-10-20：完成 T5.2 标记 UI 与跳转（`lapian_mark_ui`/MarkPanel、Ctrl+M、点击跳转+暂停、删除确认、时间轴粉色标记与 5px 吸附、存储位置/回退/警告展示、切文件/关窗 flush、`--mark-test`）；新增 `lapian_mark_panel_test`，`ctest` 16/16；101 标记重开/删除/空文件/F8-A3 隔离通过；UI p95 5.91ms、画面 p95 31.34ms（V9 1080p 短 GOP，非正式 P9）；`dist` 32.11MB/25 文件；本文件升至 v4.1。
- 2026-10-20：完成 T5.3 备注编辑与自动保存（MarkPanel 标签/备注编辑、标签 Enter/备注 Ctrl+Enter/失焦/切换/关窗提交、空备注、只更新当前行、焦点隔离、`--note-test`）；`ctest` 16/16；F8-A2 关窗重开、F6-A1 备注+点击跳转暂停、F6-A2 真实确认对话框 No→Yes、F8-A3 隔离通过；20 次提交保存 p95 7.38ms；`dist` 32.15MB/25 文件；本文件升至 v4.2。
- 2026-10-20：完成 T5.4 当前帧截图（`displayed_frame_` 引用、`SnapshotService` 后台硬解回读/WIC PNG、`SnapshotNaming` 命名/目录回退、display matrix 旋转、D3D11 上下文锁、`--snapshot-test`）；新增 `lapian_snapshot_naming_test`，`ctest` 17/17；1080p 3 连拍/不覆盖/`f100`/1920×1080、90° B 1080×1920、播放中保存帧==按键帧且 playing=1、S 键/焦点隔离/目录回退/标记不丢通过；1080p 保存 p95 32ms、4K p95 161ms、4K 工作集 452.8MB；`dist` 32.21MB/25 文件；真实 V4/V5、ACL 只读目录、真人验收与正式 P11 仍待；本文件升至 v4.3。
- 2026-10-20：完成 T6.1 全屏（`F`/双击切换、`Esc` 退出、几何/最大化/焦点恢复、全屏保留时间轴/信息栏/侧栏、Esc 在备注框让位、交换链跟随 resize）；新增 `--fullscreen-test`，1080p 38 断言 / +4K 48 断言全过且 4K 连续 2 次稳定；全屏中时间轴点击帧 60、`S` 保存帧==显示帧、PNG 1920×1080 / 3840×2160、交换链==控件×DPR、焦点 GfxWidget→GfxWidget、最大化可恢复；`SnapshotService` 改为先发 saved 信号再清 idle（修 4K Drain 竞态）；`lapian_video_renderer_test` 补 640×640 letterbox 断言；进出全屏 8–22ms、全屏 4K 截图 151–174ms（开发机）；`dist` 32.25MB/25 文件；F1-A6 真人/正式 V1/V4/V5/DPI 仍待；本文件升至 v4.4。
- 2026-10-20：完成 T6.2 键位/焦点矩阵（步进/Home/End 提升为窗口级事件过滤器，300ms→70ms 单连发源，忽略 OS 重复，焦点丢失/窗口失活停止；删除 GfxWidget 内部步进定时器；`+`/`-` 过滤器单源，兼容 `Key_Plus`/`Key_Equal+Shift`/numpad；文本与模态让位）；新增 `--key-test`，75 项检查连续 2 次 exit 0，覆盖 §3.7 全表、焦点矩阵、长按 450ms/2s 27 帧、文本/模态隔离；全屏/截图/note/mark/ab/loop 回归 exit 0；真人键盘/IME/AltGr 与 Ctrl+O 原生对话框仍待；`dist` 32.28MB/25 文件；本文件升至 v4.5。
- 2026-10-20：完成 T6.3 设置持久化（`core/AppSettings` 纯解析 + `SettingsStore`；`%APPDATA%\拉片软件\settings.json`、schema 1、QSaveFile 原子写、64 KiB 上限；音量/波形跨会话，循环/速度/静音不落盘；250ms 去抖 + 关窗/析构 flush，读取失败静默回退）；新增 `lapian_app_settings_test`，`ctest` 18/18；`--settings-test` + `--settings-expect` 两进程（重定向 APPDATA）47+5 项检查全过；默认根 `C:\Users\...\AppData\Roaming\拉片软件`，保存 p95 1.04ms；真实 `%APPDATA%` 写入受本会话执行环境（非系统进程写 AppData 被拒）限制未验证；`dist` 32.31MB/25 文件；本文件升至 v4.6。
- 2026-10-20：完成 T6.4 DPI/布局（初始窗口按屏幕可用区收缩、侧栏 240–300 响应式、InfoBar <760 两行 + 文件名省略、GfxWidget 最小 320×180、窄宽度隐藏状态栏版本标签）；新增 `--dpi-test`，`QT_SCALE_FACTOR` 100/125/150/200% 各 9 项检查、0 失败，正常/全屏/820 宽窄窗口无裁切/重叠；200% 窗口 928×452、InfoBar 687×53 两行、侧栏 241；150%/200% `--fullscreen-test` 各 38 项恢复通过；键位/设置/截图/note/mark/ab/loop 回归 exit 0；`dist` 32.34MB/25 文件；真实 Windows 显示缩放/4K/多显示器/真人字体与 IME 仍待；本文件升至 v4.7。
- 2026-10-20：完成 T2.9.1 反向拖动预览补丁（根因：反向目标每个鼠标事件都从关键帧重解并逐帧发布，表现为画面向前冲；UpdateTarget 不清理旧目标帧）。修复：PreviewFrame 带 frame_index/pts，反向只发布目标帧、丢弃过期帧，32 帧降采样缓存（约 44MB，End/Close 释放），Begin 起点目标模式；前向 58–64fps 保持。新增反向 600→560 每步目标帧/单调不增、快速反向 900ms 不前跳、反向后前向恢复测试；60s 反向 17.7–18.2ms/步、测试.mp4 25.4–25.6ms/步、proxy 长 GOP 39–41ms/步；ctest 18/18（14.16s）；dist 32.34MB/25 文件；用户已物理鼠标复测倒拖正常，未做 4K/VFR/正式 P3/P4；本文件升至 v4.8。
- 2026-10-20：完成 T6.2.1 长按 PgUp/PgDn 连发补丁（现象：物理长按最多 3 帧；根因：Qt 物理长按会额外发 KeyRelease，旧代码只要 step_direction_ != 0 就 StopStepRepeat()，约 500ms 第一次 OS 自动重复的 release 把连发停掉；300ms+70ms+70ms 正好 3 帧）。修复：KeyRelease 仅在 !isAutoRepeat() 时停止；自动重复 release 吞掉、自动重复 KeyPress 继续忽略；新增 PressAutoRepeatReleaseForTest 与 autorepeat-release-active/ignored 检查；--key-test 77 项连续 2 次 exit 0、2s 长按 27 帧；ctest 18/18（13.90s）；dist 32.34MB/25 文件；用户已物理键盘复测长按正常，未做硬件级自动化/IME/AltGr；本文件升至 v4.9。
- 2026-10-20：完成 T6.5 状态横幅（`media/StatusNotice` 六类提示与优先级：错误>VFR>10-bit>超出保证>扫描中>无音频；InfoBar 唯一横幅、按级别配色、参与纵向布局；其余进状态栏 `noticeInfo` 常驻标签；`MediaProbe` 新增 `bit_depth`；打开失败接错误横幅，成功打开清除）。新增 `lapian_status_notice_test` 69 断言、`ctest` 19/19（14.20s）；`--banner-test` 100%/200% 各 74 项、0 失败，820 窄窗无裁切/遮挡；`--dpi-test` 100/150/200% 各 9 项、0 失败；全屏 48/键位 77/截图 48/note/mark/ab/loop/settings 回归 exit 0；`dist` 32.43MB/25 文件；真实 200%/VFR/10-bit/HDR、正式 F2-A7/F1-A4 仍待，未做硬件/基线机结论；本文件升至 v5.0。
- 2026-10-20：完成 T7.1 冷启动埋点与索引后置（`--startup-test` 阶段日志、`GfxWidget::framePresented`、`MediaSession` 分段计时；产品路径首帧后后台建索引，`SetTotal(Scanning)`→Exact/Estimated，扫描中禁用依赖索引的导航，标记/预览器延后到索引就绪；`FrameIndex` 可取消；旧 GUI 回归用同步开关；`--index-test` 12 项；`scripts/startup-bench.ps1`）；开发机 20/20：proxy-V1 P1 p50 255.78ms/P2 308.26ms、1080p 152s P1 255.54ms/P2 319.39ms、4K proxy P1 257.05ms/P2 330.39ms；1080p 索引 374.3ms 移出 P2；`ctest` 19/19（13.89s）；`dist` 32.45MB/25 文件；未做冷缓存/基线机/正式 V1；本文件升至 v5.1。
- 2026-10-20：完成 T7.2 内存预算与有界缓存（`FrameCache` 软件帧 LRU：1080p 64MiB/24 帧、4K 96MiB/8 帧；D3D11VA 硬件帧回拷 NV12/P010，避免占用解码表面；只在后退步进 `kBackwardWindow` 预热，普通跳转/前进/播放 `kNone`；`EnsureSequentialPosition` 修缓存命中后起播位置；`Open`/`Close`/`InstallIndex` 清缓存；波形 4h 硬上限；新增 `--memory-test` empty/pause/play/step/switch 与 `GetProcessMemoryInfo` 采样）。开发机：空态 63.8MB、1080p 暂停 60s 89.3MB、1080p 播放 106.0MB、4K 播放 128.7MB；1080p 200 后退 p95 32.72ms、4K 40 后退 p95 228.09ms（4K P7 未达）；切文件 cache=0、10 循环 WS ~153MB；新增 `lapian_frame_cache_test`，`ctest` 20/20（13.94s）；`dist` 32.48MB/25 文件；未做基线机/正式 V1/V4/V10/2h soak；本文件升至 v5.2。
- 2026-10-20：完成 T7.2.1 热修（B 在文件末帧时循环停住；PgUp 画幅轻微跳变）。根因：末帧没有 B+1，EOF 分支依赖 `current_frame_`，音频时钟在 B 前停住时 `pending B` 永远等不到 target；1080p D3D11VA 纹理 1920×1088/显示 1920×1080，旧渲染器用纹理尺寸 fit，缓存软件帧用 1080，导致 PgUp 时比例变化。修复：`loop_out_reached_` + 一帧墙钟 hold + EOF/last_decoded 兜底 + 跳转失败重试；硬解帧按显示尺寸建纹理并只拷贝有效区域；新增 `--loop-end-test` 与 1080p 渲染/缓存回拷断言。开发机：V9/1080p/4K B=末帧 5s 内 5/6/6 次 B→A，`playing=1`；`ctest` 20/20（14.93s）；`dist` 32.49MB/25 文件；用户物理画幅复测/2h soak 未做；本文件升至 v5.3。
- 2026-10-20：完成 T7.3 长时浸泡监测代码（`--memory-mode soak`、`--memory-interval`、`SoakStats` 稳态斜率/句柄/线程/卡顿判定、UI 心跳与 30s hard-stall 看门狗、`ProcessMemory` 线程数、`scripts/soak-bench.ps1`、`lapian_soak_test`；修正 soak 时长链式漂移与脚本 UTF-8 BOM）。开发机最终代码 60s 探针 pass=1；300s/900s 探针 pass=1（900s WS 105.6→114.0MB，+8.4MB 慢漂移未判失败）；30min soak 在 t=150s 因用户学校断网主动停止。`ctest` 21/21（14.61s）；`dist` 32.51MB/25 文件。**不是 P14/V10/基线机/2h 通过**；当前交接 [T7.3 WIP](docs/handoff-2026-10-20-t7.3-wip.md)。

- 2026-10-21：完成 T7.3 浸泡监测（`188c63e`）与 5 分钟下包络增长判定（`31d0d94`）；30min 探针 `pass=0`（WS 107.8→126.1MB，+18.2；下包络 +15.3/27.12MB·h⁻¹）、1h 探针 `pass=0`（105.6→126.4MB，+20.8；下包络 +19.2/21.19MB·h⁻¹），t=2460 第 16 次 B→A 有持续 +7MB 台阶；cache 0、underrun 0、无卡死；`ctest` 21/21；`dist` 32.51MB/25 文件。**T7.3 验收未通过**，V10 2h 未做；当前交接 [T7.3](docs/handoff-2026-10-21-t7.3.md)、[perf](docs/perf/T7.3-soak.md)。

- 2026-10-22：T7.3 增加 `--memory-no-loop`（`852b68a`）与 A/B 子区间循环
  `--memory-loop-frames`（`5dfbe06`）。90min 单文件连续播放 `v10-1p5h`：
  `pass=1`，WS 150.9→153.1MB（+2.1），斜率 1.22MB/h；1h 同文件只循环前
  3650 帧（≈152s）`loopseg3600`：`pass=1`，WS 156.0→159.9MB（+3.9），
  斜率 0.79MB/h，23 次 B→A。152s 循环的 +18.2/+20.8MB 未复现，当前没有
  可复现的持续增长；但 1.5h ≠ V10 2h，P14 未通过。

- 2026-10-22：完成 T7.4 4K 路径（`40bba7f`）。根因：前进步进每次从关键帧重解（4K 前进 p95 230ms）；后退 8 帧窗口每 8 步未命中，D3D11VA 回拷约 160ms/次。修复：`MediaSession` 顺序前进快路径（p95 4.12ms）、4K 后退窗口 96MiB/8 → 256MiB/20、`--drag-test`/`--force-software-decode`。4K proxy（非 V4）硬解：P4 60.8/61.0fps、P6 142.6/143.4ms、P7 后退 1.86/1.67ms（max 410ms，1/20 超）、前进 4.34/4.40ms、P8 13.0fps 26/26 呈现、P9 225.4/226.8ms、P11 140.1MB；seek 后 PgUp 单样本 201–213ms 超 150；软解 P6 301.7/P9 320.6 未过。`ctest` 21/21；`dist` 32.54MB/25 文件。**不是正式 V4/基线机，T7.3 P14 仍待 V10**；历史交接 [T7.4](docs/handoff-2026-10-22-t7.4.md)、[perf](docs/perf/T7.4-4k.md)。

- 2026-10-22：完成 T7.5 错误与鲁棒性（`db9e004`）。根因/修复：断网 UNC 在 UI 线程 `avformat_open_input` 实测 21s → `PathProbe` 3s 有界预检；>MAX_PATH sidecar 的 `ReplaceFileW` 缺 `\\?\` 前缀而误回退 AppData → `NativeLongPath`；正斜杠缺失路径被 FFmpeg 当 URL（EILSEQ）→ 打开前转原生分隔符；目录/独占文件提示误导 → 目录直判 + 本地可读性预检；新增 `--robust-test`/`--robust-only` 与 `scripts\robust-readonly.ps1`。证据：坏文件 7 类 ≤34ms、锁文件 32ms、截断 seek 1.0ms、288 字符长路径 sidecar/截图、只读 ACL 17/17、不可达 UNC 3.03s、设备丢失模拟 silent→QPC 推进→恢复；`ctest` 22/22、`dist` 32.63MB/25 文件。**不是真实 V8/物理设备丢失/可达 SMB/基线机**；T7.3 P14 仍待 V10。当前交接 [T7.5](docs/handoff-2026-10-22-t7.5.md)、[perf](docs/perf/T7.5-robustness.md)。

- 2026-10-22：完成 T7.6 打包/关联/许可/体积（`1b349a7`）。新增 `scripts/package.ps1`/`installer.iss`/`packaging/README.txt` 与 `build-release.ps1 -Package`；Inno 6.7.3 每用户安装（非管理员、`%LOCALAPPDATA%\Programs\拉片软件`）；ProgID `Lapian.Video` + `.mp4/.mov/.mkv/.webm` OpenWithProgids（不设默认，`UserChoice` 未变）；setup 11.31MB、便携 zip 13.65MB、负载 32.74MB（P12 ≤100MB）；安装 33 文件/36.99MB，卸载清理程序/快捷方式/ProgID/我们的关联值并保留 settings/marks/sidecar；安装/启动/鲁棒/卸载轮询 0 TCP/UDP；`ctest` 22/22、`dist` 32.63MB/25 文件。**F1-A1 只做本机热缓存预检（窗口 359ms、首帧 359–400ms），Explorer 双击/冷缓存/基线机未做；安装器 UI 英文、未签名/无图标；E: 需复制到 C: 才能装**。历史交接 [T7.6](docs/handoff-2026-10-22-t7.6.md)、[perf](docs/perf/T7.6-packaging.md)。

- 2026-10-22：完成 T7.8 基准回归脚本/阈值/开发机基线（`892c5ac`）。新增 `scripts\bench-regression.ps1`（8 套件：startup/drag1080/drag4k/step1080/step4k/memory/robust/package，可选 readonly）、`scripts\bench\thresholds.json`（阈值全部来自 spec；1080p 40 样本、4K 20 样本；基线 ±10% + 噪声下限）、`scripts\bench\baseline-dev.json`（73 个数值 + 机器/素材/exe 元数据）；全量 194.8s、**54 项检查/0 失败/0 infra**；基线 `dev-2026-10-22`（`bench-20261022-183348.json`）与验证 `bench-20261022-184030.json`：**26/26 在容差内（24/26 严格 ±10%，2 项 P7 后退 p95 绝对差 0.19/0.60ms 按 5ms 下限）**；P1 p50 274ms/P2 328ms（热缓存）、P6 57.6/145.2ms、P7 0.95/2.93ms、P9 61.5/228.0ms、P10 96.3MB、P11 102.2/116.9MB、P12 32.63/11.31/13.65MB；`ctest` 22/22、`dist` 32.63MB/25 文件。**不是 T7.7；P1/P2 热缓存；proxy 素材；seek 后 PgUp 1080p 372ms/4K 206ms 仍超 80/150（T7.4 已知缺口，T7.8 只记录不掩盖）；T7.3 P14 仍待 V10**。当前交接 [T7.8](docs/handoff-2026-10-22-t7.8.md)、[perf](docs/perf/T7.8-bench.md)。

- 2026-10-22：完成 T7.9 P7 seek 后 PgUp 收口（`d1c7369`）。根因：`GoToFrame` 用 `kNone`，精确 seek 丢弃目标前一帧，PgUp 从关键帧重解（4K 2s GOP 196.57ms）；proxy-V1 的 spot 目标 250 正好是 10s GOP 关键帧，旧 372–460ms 是长 GOP 边界，不属 80ms。修复：`FrameCacheMode::kSeekHandoff` 只保留目标前一帧 `AVFrame` 引用（硬解纹理不回拷），PgUp 直接呈现；spot 目标避开关键帧并记录距离；bench drag 每类 +3 项，54→60 项。证据：spot PgUp 1080p 0.06ms/4K 0.06ms，PgDn 5.99/39.55ms；P6 56.34/143.38ms、P9 p95 61.45/226.78ms；全量 60/0/0、30/30 容差（27/30 严格 ±10%）；基线 `dev-2026-10-22-t7.9`（`bench-20261022-191014.json`），验证 `191338.json`；`ctest` 22/22、`dist` 32.63MB/25。**不是 T7.7；跨 GOP 边界专项仍待真实 V1；4K 仍 proxy；P14/V10 未通过**。当前交接 [T7.9](docs/handoff-2026-10-22-t7.9.md)、[perf](docs/perf/T7.9-p7-seek-handoff.md)。

- 2026-10-22：完成 T7.10 合成 V1–V10 片单（辅助素材，不是正式片单）。`media/synthetic-suite/`：V1 1080p24 6:01、V2 1080p30 152s、V3 1080p HEVC60 20s（proxy）、V4 4K30 15s（proxy）、V5 VFR 41.9s（stts 改写，VFR_SUSPECT=1）、V6 12min、V7 无音频、V8 5 个坏文件（全部 OK=0）、V9 click+白闪、V10 2h/7200.11s/5.23GB。V10 2h soak `v10-synth` 于 19:30:18 启动，19:41:51 t=690s 时 WS 164.9MB/handles 687/playing=1/underruns=0；尚无 `T7.3 soak_summary`，不写 P14 通过。未改产品代码/阈值/基线。证据 [T7.10](docs/handoff-2026-10-22-t7.10.md)、[perf](docs/perf/T7.10-synthetic-suite.md)。

- 2026-10-22：完成 T7.11 合成 V10 soak 收口 + V1–V9 冒烟 + P7 跨 GOP 边界专项（`4516d53`）。合成 V10 `v10-synth` 7200s `pass=1`（WS 162.4→164.8MB、包络 0.16MB/h、handles 691→675、threads 56→50、frame_stall=0、underruns=0、ui_gap 212ms）；V1–V9 冒烟 18/18（V8 错误对话框 0.31–0.35s、V9 click ±1 帧）；新增 `--gop-boundary-test`/`scripts\gop-boundary-bench.ps1`，用 `LAPIAN_SEEK_LOG` 证明每个 PgUp 都是 `hit=0 decoded=GOP`。结果：2s GOP 1080p p95 **145.60ms**（20/20 超 80）、1s GOP 收紧档 **92.65ms**（仍超）、长 GOP proxy-V1 340.78ms（单列）、4K proxy-V4 451.53ms（诊断）；分项 cache 回拷 76–105ms、解码 12–42ms。`ctest` 22/22、`dist` 32.65MB/25。**P7 边界未通过；T7.7 blocker，需异步/延后回拷或 spec 决策**。证据 [T7.11 交接](docs/handoff-2026-10-22-t7.11.md)、[perf](docs/perf/T7.11-gop-boundary.md)。

- 2026-10-22：完成 T7.11.1 P7 跨 GOP 边界后退修复（`7af822e`）。根因：后退第一步同步回拷 24 帧 FrameCache（76–105ms）；改为窗口硬解帧只留引用、目标帧先呈现、事件循环空闲 4 帧/片回拷，≤1080p 硬解 `extra_hw_frames=32`，回拷旧帧优先。开发机 2s GOP p95 **64.13ms**、1s **27.11ms**、1080p60 HEVC 2s **67.31ms**；连续后退 p95 0.03/0.06ms；全量 60/0、30/30 容差；`ctest` 22/22、`dist` 32.65MB/25、setup 11.31MB、zip 13.66MB。**不是 T7.7/基线机；2s 余量 15.9ms**。证据 [T7.11.1 交接](docs/handoff-2026-10-22-t7.11.1.md)、[perf](docs/perf/T7.11.1-gop-cache-defer.md)。

- 2026-10-22：完成 T7.7 验收清单与预检脚本（`docs/perf/T7.7-acceptance-checklist.md`、`scripts/t7.7-preflight.ps1`）。本机预检 `verdict=blocked`：真实 V1–V10 0/10、非基线机（6C/12T/31.6GB/RTX 4060）、关联未安装、冷缓存/Explorer 未做；合成片单 9/9 + V8 5/5 `OK=0`；`ctest` 22/22、`dist` 32.65MB/25、setup 11.31MB、zip 13.66MB。P7 2s 复跑 p95 **65.33ms**（0/20 超 80，余量 14.67ms）、1s **30.07ms**（`short_gop pass=1`，脚本 N/A）。**不是 T7.7 通过；基线机必须复跑**。证据 [T7.7 交接](docs/handoff-2026-10-22-t7.7.md)、[清单](docs/perf/T7.7-acceptance-checklist.md)。P7 边界已在开发机修复（T7.11.1：2s 64.13ms/1s 27.11ms，余量 15.9ms），但必须在基线机复跑并记录 iGPU 下 `extra_hw_frames=32` 的 P10/P11；若基线机超 80ms，按 T7.11.1 证据评估空闲预解/批量回拷，不改阈值。T7.3 合成 V10 2h `pass=1` 只算替身，正式 P14 仍需真实 V10；T7.9 的普通 seek 后 PgUp 0.06ms 不等于跨 GOP 边界专项。不要把 90min 当 2h，也不要因为单文件/循环/合成 soak 都通过就写 P14 通过；不要把 T7.4 的 4K proxy 当正式 V4；不要把 T7.5 的合成坏文件/模拟设备丢失当成 V8 实物与物理设备验收；不要把 T7.6 的热缓存预检当成 F1-A1 通过；不要把 T7.8 的热缓存 P1/P2 当成冷启动验收。

- 2026-10-23：完成 v2 在线 S1 产品定义 + S2 可行性验证（未改产品代码/spec/tasks）。S1 五个决定：无下载步骤+自动缓存可清、自带 Cookie、保帧率优先+20GB LRU、不绕过+凭据内最高档、任务完成率为主指标。S2 实测：匿名 DASH 恒 480p30；普通用户登录 DASH 1080P30（3/3）、durl 1080p 仅 1/3、1080P60/4K 不可得；1080p AVC 2.1–2.6Mbps；起播阈值 0.16–0.3MB；可增长读层（阻塞读/取消 3–15ms）已验证；同一 Cookie curl 通过而 .NET 一律 -101。产出 [vision](docs/product-v2-vision.md)、[ADR-007](docs/adr/ADR-007-bilibili-online.md)、[spike 报告](docs/perf/v2-online-spike.md)、[S1/S2 交接](docs/handoff-2026-10-23-s1s2.md)。S3 待开（只写文档）。本文件升至 v6.8。

- 2026-10-23：完成 v2 在线 S3 规格与计划草案（未写产品代码、未改 v1 冻结文件/ADR 结论）。产出 [spec-v1.0-delta.md](docs/spec-v1.0-delta.md)（O1–O10、OP1–OP8、缓存/错误/隐私、OB 片单、DEC-1…DEC-5）、[plan.md](plan.md) v2 章节（组件/线程/可增长读层/DASH 双输入/风险 R19–R34/两周计划）、[tasks-v2.md](tasks-v2.md)（T8.0–T8.16，M1 durl 720p → M2 DASH 1080p，每项含依赖/验收/证据）、[S3 交接](docs/handoff-2026-10-23-s3.md)。明确 4K/1080P60 不进 MVP 验收；M1/M2 顺序、在线数字、登录自检时机、60fps 提示形态、配额验收方式留用户拍板。本文件升至 v6.9。

- 2026-10-23：用户拍板 **B（大会员档条件验收）**。大会员 1080P60/4K SDR 做独立“可用档”条件验收：1080P60 若 AVC 可自动、HEVC-only 手动；4K 只手动；无大会员账号则 T8.15“未验证”；**不进 M1/M2 门**，不承诺 4K P7/P9/颜色/HDR。delta 升 v0.2（O11、OP9/OP10）、tasks-v2 升 v0.2（T8.15 升级）、plan v2 章节升 v0.2、S3 交接升 v1.1。DEC-1…DEC-5 仍待拍板。本文件升至 v6.10。

- 2026-10-23：完成 **T8.0（S3 收口）**，用户“全部按建议”拍板 DEC-1…5：M1→M2；起播 p95 ≤5s/已缓存 P7 ≤80ms/缓存命中 ≤2.5s/未缓存提示取消 ≤100ms/任务完成率 ≥80%；首次在线打开自检；60fps 横幅+常驻角标；300MB 自动 + 20GB 记账单测。ADR-007 **附条件接受**（T8.2/T8.4 回填；D1–D5 未改）。delta/tasks/plan v2 章节升 v0.3，S3 交接升 v1.2；S4 从 T8.1 开始。本文件升至 v6.11。

- 2026-10-23：完成 **T8.1 解析层**：BV/URL 归一化与本地校验、匿名 `view` 元数据、分P（默认 P1、`?p=`、切P）、错误映射、Ctrl+U 回显确认对话框、`--online-resolve-test`、`lapian_online_resolve_test`。ctest 23/23；产品探针 156/0（dist 同为 156/0）；本地非法 9 类 0ms/0 请求；取消 fake ≤5ms/关窗 ≤7ms、真实 WinHTTP 取消 15ms；P1 277.78ms/P2 334.30ms/P7 2s 67.37ms；dist 32.81MB/25 文件、无 `WINHTTP.DLL` 导入。T8.2/T8.5/缓存/播放未做。证据 [v2-online-resolve.md](docs/perf/v2-online-resolve.md)、交接 [T8.1](docs/handoff-2026-10-23-t8.1.md)；tasks-v2 升 v0.4。本文件升至 v6.12。
- 2026-10-23：完成 **T8.2 HTTP 栈自检/凭据**：WinHTTP 产品栈 nav `code=0/isLogin=true`（普通用户，5 次 161–202ms，`cookie_sent=1`，`vip_status=0`）；`HttpRequest::sessdata` 只发 `api.bilibili.com:443`、禁 cookie jar/重定向；SESSDATA/DPAPI 凭据 UI、DEC-3 首次提交自检、`-101`/412/429/超时映射、日志脱敏；`--online-http-selfcheck`、`--online-auth-test`、`lapian_online_auth_test`（104/0）。ctest 24/24；产品探针 54/0；T8.1 156/0（dist 同）；本地 P1 267.83ms/P2 324.17ms/P7 2s p95 66.64ms；dist 32.89MB/25 文件；check-imports 无 WinHTTP/TLS/QtNetwork；libcurl 未构建。证据 [v2-online-http-selfcheck.md](docs/perf/v2-online-http-selfcheck.md)、交接 [T8.2](docs/handoff-2026-10-23-t8.2.md)；tasks-v2 升 v0.5。本文件升至 v6.13。提交 `0f322b0`（v2 S3 + T8.0–T8.2，54 文件；工作树 clean）。

- 2026-10-23：完成 **T8.3 缓存存储与配额**：新增 `CacheStore`（默认根 `%LOCALAPPDATA%\拉片软件\cache`、`meta.json`、`.part`→`.mp4` 用 `MoveFileExW`+`\\?\`、20 GiB 默认/1–100GB 可配、LRU（当前 key 不淘汰、读句柄 `evict_pending`）、空间预检 `expected+512MB`、续传按 `.part` 长度、8 类损坏 broken/崩溃自愈、`Rebuild`/`ClearAll`/`CleanupOrphans`、Unicode/长路径）；`AppSettings` 新增 `cache_root`/`cache_quota_gb` + `MergeVolumeWaveform`（保存音量/波形不冲掉缓存键）；`--online-cache-test`（无 GUI，32/0，`verdict=pass`）；`lapian_online_cache_test`（366/0/0 skip，含 300MB LRU 283,116,589→188,744,706 与 20GB 记账 21→16→10 GiB、10,737,418,599 ≤ 21,474,836,480；磁盘满预检 0.082ms；长路径 301 字符；meta 无明文）。`ctest` 25/25；`app_settings_test` 80/0；本地 P1 290.73ms/P2 345.13ms/P7 2s p95 64.96ms（over 0）；dist 33.02MB/25 文件；check-imports 20 文件无新增网络导入；未联网、未做 T8.4/T8.5/T8.6/T8.7。证据 [v2-online-cache.md](docs/perf/v2-online-cache.md)、交接 [T8.3](docs/handoff-2026-10-23-t8.3.md)；tasks-v2 升 v0.6。本文件升至 v6.14。
- 2026-10-23：**DEC-6 拍板（扫码登录并入 v2）**：用户要求“傻瓜化”，选择 B——新增 **T8.2.2 扫码登录（非 M1/M2 门）**，排在 T8.4 之后、可与 T8.5 并行。只走 B 站官方网页二维码流程（`passport.bilibili.com` generate/poll）、二维码本地渲染（vendored MIT 单文件，不引入 WebEngine/新 DLL）、180s 过期可刷新、可取消、成功只保留 SESSDATA（DPAPI）、失败/过期/取消回退 SESSDATA 粘贴；**SESSDATA 粘贴与扫码长期并存（同一存储/自检/清除，粘贴不是过渡、不降级）**；不做账号密码/注册/个人主页/多账号/云同步；自动读浏览器 Cookie（Chrome/Edge App-Bound Encryption 需绕过）明确否决。文档：delta v0.4（O8 扫码来源 + O8-A8/A9、§6.1 passport 白名单、§1.3/§6.2、DEC-6 记录）、vision v1.1（决定 2 + 从“不做”移到 MVP 增强）、tasks-v2 v0.7（T8.2.2 任务行 + 排期 + 门说明）。M1/M2 门与全部验收数字不变。本文件升至 v6.15。
- 2026-10-23：完成 **T8.4 可增长读层**：新增 `GrowingSource`（已知总大小/Published/阻塞 `WaitFor`/`Publish`/`Fail`/`Cancel`/统计，mutex+cv，无自有线程）、`GrowingCursor`（每读者独立 `pos`+只读句柄+64KB `AVIOContext`，read/seek 回调）、`GrowingFormat`（`AVFMT_FLAG_CUSTOM_IO`）；失败映射 `AVERROR(EIO)`（绝不 EOF）、取消 `AVERROR_EXIT`、`pos>=Size()` EOF，失败后已发布前缀仍可读；seek 越界 EINVAL（不静默 clamp）；读者句柄不带 `FILE_SHARE_DELETE` 以让淘汰返回 busy+`evict_pending`。`--online-growing-test`（无 GUI、离线，22/0，`verdict=pass`；0.3MB 首帧 1920×1080（open 9ms/首帧 16ms/实读 131072B）、100/100 seek 落点 0、已缓存等待 0、未缓存等待 2073ms vs 预期 1750ms（+18.5%）、取消 0.15/0.24ms、4 读者短版 10s deadlocks=0/lost_wakeups=0/close 3ms/WS +9.77MB、5s 等待 CPU 0ms、EOF/EIO/EXIT、淘汰 busy）；`lapian_growing_source_test`（157/0，含 4GiB−1 稀疏文件 int64）；**4 读者 10min `deadlocks=0/lost_wakeups=0`（close/WS 见证据）**；`ctest` 26/26；`--online-cache-test` 32/0、`--online-auth-test` 54/0；本地 P1/P2/P7 无回归；dist/imports 见证据；未联网、未做 T8.5/T8.6/T8.7/T8.2.2。证据 [v2-online-growing.md](docs/perf/v2-online-growing.md)、交接 [T8.4](docs/handoff-2026-10-23-t8.4.md)；tasks-v2 升 v0.8。本文件升至 v6.16。
- 2026-10-23：完成 **T8.5 durl 取流**：新增 `PlayUrl`（`BuildPlayUrlUrl`/`ParsePlayUrlJson`/`FetchPlayUrl`，qn=64 fnval=0，实际 qn/format/durl/backup/size；无 durl/no-size/API 错误映射）、`DownloadTransport` + `WinHttpDownloadTransport`（HTTPS only、`Range: bytes=<offset>-`、`Accept-Encoding: identity`、UA/Referer、**无 Cookie**、禁 cookie jar/重定向、206 起点/Content-Range 总长对账、200@offset>0 返回 range-ignored、256KB 流式、20ms 取消轮询）、`DownloadJob`（Reserve→OpenPart→写 `.part`→写后 `Publish`→1s 进度→3 备份 URL/300ms/1s/2s 重试/刷新一次→关闭写句柄→`MarkComplete`；暂停/取消/失败 EIO/ENOSPC；统计）；`WinHttpApi.h` 共享动态加载器（`WinHttpClient` 行为不变）。真实 3 样本：OB-1/OB-3=720P（14,330,392/51,973,319B；首帧 9/14ms、8/13ms；ffprobe pass），**OB-2 实际 360P（qn=16；qn=64/80/112 均为 16）→ M1 3/3 720P blocked**；真实取消 7ms、`_Exit` 后 `.part` 5,242,880B 续传 206、真实 CDN 403 注入：备份切换 1 次/刷新 1 次均完成且 hash 一致。`lapian_online_fetch_test` 94/0；`--online-fetch-test` 46/0（build+dist）；`ctest` 27/27；resolve 156/0、auth 54/0、cache 32/0、growing 22/0、selfcheck pass；本地 P1 279.44ms/P2 338.99ms/P7 2s p95 65.38ms（over 0）；dist 33.28MB/25 文件；check-imports 20 文件无新增网络导入；未接 UI/T8.6、未做 DASH/T8.2.2。证据 [v2-online-m1.md](docs/perf/v2-online-m1.md)、交接 [T8.5](docs/handoff-2026-10-23-t8.5.md)；tasks-v2 升 v0.9。本文件升至 v6.17。
- 2026-10-23：完成 **T8.2.2 扫码登录（非门）**：新增 `QrLogin`（passport `generate`/`poll`、4 态/180s、SESSDATA 仅从 `data.url` 或 Set-Cookie 提取、状态机非法迁移拒绝）、`QrCode` + vendored MIT `qrcodegen`（commit `3c6d0b3`，本地 RGB32 渲染/quiet zone/无 WebEngine/QtNetwork/新 DLL）、`QrLoginController`（worker + 1.5s QTimer、180s 过期、取消 ≤100ms、nav 通过才 DPAPI 保存/显示已登录）、`OnlineResolveDialog` 扫码按钮/面板（粘贴入口永久保留、失败回退）；`IHttpClient::HttpResponse` 只加 `response_sessdata`（仅 passport Set-Cookie 的 SESSDATA）；`--online-qr-test`（19/0，`--ui` 21/0，dist 同）、`--real-generate` 实测官方字段（key 32、poll 86101、cookie_sent=0）；`lapian_online_qr_test` 115/0；**真实扫码未验证（无手机）**，不写 O8-A8；`ctest` 28/28；本地 P1 274.43ms/P2 331.18ms/P7 2s p95 65.20ms；dist 33.42MB/25 文件；check-imports 无新增网络导入。证据 [v2-online-qr.md](docs/perf/v2-online-qr.md)、交接 [T8.2.2](docs/handoff-2026-10-23-t8.2.2.md)；tasks-v2 升 v0.10。本文件升至 v6.18。
- 2026-10-23：**T8.6 启动交接/提示词入库（T8.6 未开始）**：新增 `docs/handoff-2026-10-23-t8.6-start.md` 与 `docs/next-session-prompt-t8.6.md`；记录当前 `MainWindow` 确认只显示状态栏、`MediaSession`/`VideoDecoder`/`AudioDecoder`/`WaveformGenerator`/`FrameIndex` 全部按路径打开、`GrowingFormat` 无消费者、部分索引/UI 线程阻塞/完成改名 vs reader/音频波形五个硬缺口；建议先 spike 首帧/未缓存 seek，再按阶段 1/2/3 推进；必要时拆 T8.6a/b/c；T8.6 验收列与 M1/M2 门/数字未改；tasks-v2 升 v0.11。本文件升至 v6.19。
- 2026-10-23：完成 **T8.6 在线会话/UI**：新增 `OnlineSession` 状态机（playurl→CacheStore/DownloadJob/GrowingSource→0.3MB→ready；取消/错误/进度；`defer_finalize` + reader 全停后 `FinalizeAfterReadersStopped`）、`OnlineIndexBuilder`（部分索引/100ms 前沿/稳定前缀 48 帧）、MainWindow 在线入口（真实确认/首帧默认暂停/Space/PgUp/PgDn/异步 seek/未缓存等待/波形/Ctrl+M/S/关窗）、`OpenGrowing`/`OpenWithFormat`/`ProbeFormat`（在线跳过包扫描）、音频/波形独立 GrowingFormat、1MB 解码闸门+音频失速看门狗、Ctrl+Shift+U 取消、会话内标记；`--online-m1-test`/`--cancel-only`/`--online-buffer-test`。真实 3 条 E2E：OB-1 首帧 604ms/P7 p95 20ms/seek p95 79ms/UI gap 60ms/关窗 645ms（720P）；OB-2 700ms/P7 20ms/seek 59ms/UI 61ms/关窗 18ms（**360P qn=16，quality=blocked**）；OB-3 503ms/P7 25ms/seek 97ms/UI 62ms/关窗 250ms（720P）。冷缓存 20×3 首帧 p95 624/725/459ms，全部 20/20≤5s；缓冲中取消 19ms、`.part` 保留；缓冲耗尽第 145 帧自动暂停/恢复、关窗 finalize 完整；错误 fake total-mismatch 128ms；`ctest` 29/29（`lapian_online_m1_test` 48/0）；本地 P1/P2 290.68/347.43ms（同机基线 285.31/344.55）、P7 2s 66.59ms；零网络 0 TCP/UDP、无 winhttp/wininet/urlmon/libcurl；fetch 46/0、qr 19/0（UI 21/0）、auth 54/0、resolve 156/0、cache 32/0、growing 22/0；dist 33.57MB/25；check-imports 20 文件。**OB-2 仍 360P，M1 3/3 720P blocked，T8.6 不判 M1；T8.7/T8.8/DASH/扫码未做**。证据 [v2-online-session.md](docs/perf/v2-online-session.md)、交接 [T8.6](docs/handoff-2026-10-23-t8.6.md)；tasks-v2 升 v0.12。本文件升至 v6.20。
- 2026-10-23：**v2 计划 v0.13 + T8.7 交接**：T8.6 真实使用反馈新增非门后续 **T8.6.1a（在线长按步进）、T8.6.1b（在线拖动预览，依赖 T8.11）、T8.3.1（缓存管理：自选目录/配额/清空/孤儿清理）**；T8.12 补“按账号允许档位列档、实际 qn 以服务端为准”，T8.16 补“安装到 `%LOCALAPPDATA%\Programs` 后直接双击、不依赖 Low integrity 工作区”；M1/M2 门、T8.7/T8.8 顺序与全部数字未改。新增 [docs/next-session-prompt-t8.7.md](docs/next-session-prompt-t8.7.md)（下一 agent 只做 T8.7，含 Medium 副本/默认 AppData 验证、断网 0 请求、记录/截图、停止条件）；本文件升至 v6.21。
- 2026-10-23：完成 **T8.7 缓存命中/在线记录**：`OnlineSession::Start` 先 `CacheStore::Scan`+`OpenComplete`，命中直接本地打开 `data.mp4`（0 playurl/CDN/`.part`，不进 `DownloadJob`）；`lapian-online-1`（`%APPDATA%\拉片软件\online\<bvid>-<cid>.lapian.json`，逐条 fps/qn/codecid，原子写/损坏备份/空删除/更高 schema 不覆盖）；截图 `Pictures\拉片软件\<bvid>\` + `index.jsonl`（bvid/cid/qn/codecid/fps/frame/pts/path/saved_at）；清缓存保留记录/重建回访/损坏不阻塞/换档按 PTS 映射；`--online-offline-test` workspace 45/0、真实 OB-1 缓存 44/0、Medium 默认 AppData 31/0×3（首帧 74–84ms、0 TCP/UDP、无 winhttp/wininet/urlmon）；`lapian_online_records_test` 52/0；`ctest` 30/30；本地 P1/P2 296.81/345.40ms、P7 跨 GOP 66.56ms；dist 33.68MB/25；**OB-2 仍 360P，M1 3/3 720P blocked，T8.7 不判 M1**；T8.8/T8.6.1a/b/T8.3.1/DASH/清晰度未做。证据 [v2-online-cache-hit.md](docs/perf/v2-online-cache-hit.md)、交接 [T8.7](docs/handoff-2026-10-23-t8.7.md)；tasks-v2 升 v0.14。本文件升至 v6.22。
- 2026-10-23：**T8.8 M1 门完成并通过**：qn/fourk 矩阵 20 单元证明 OB-2 的 360P 是产品请求 `qn=64` 造成（qn=80/112/120 → 80/1080P；fourk 无影响）；最小产品修正 `kM1RequestQn=80`、fourk=0、刷新同档；3/3 实际 qn=64/80/64、ffprobe pass；E2E 29/0×3；OP1 p95 648/532/527ms、OP2 p95 34/51/34ms、OP3 提示/取消 0ms+落点 0、OP5 p95 95ms+0 请求；杀进程续传 5,242,880B/hash 一致、清缓存保留记录 45/0；DEC-5 300MB→188,744,706B/20GB 记账通过；本地零网络、P1 342.92/P2 376.14/P7 64.79ms、`ctest` 30/30、dist 33.69MB/25；证据 [v2-online-m1.md](docs/perf/v2-online-m1.md) §11–§17、交接 [T8.8](docs/handoff-2026-10-23-t8.8-m1.md)；tasks-v2 升 v0.15；**未进 M2**。本文件升至 v6.24。
- 2026-10-23：**T8.8 启动交接/提示词入库（T8.8 未开始）**：新增 [T8.8 启动交接](docs/handoff-2026-10-23-t8.8-start.md) 与 [T8.8 提示词](docs/next-session-prompt-t8.8.md)，明确下一步只做 T8.8/M1 门、不进 M2；**关键线索**：S2 §2.2 记录登录后 `durl qn=120 + fourk=1` 时 OB-2 实际为 80（1080p），而 T8.5 产品固定 `qn=64` 且 `BuildPlayUrlUrl` 写死 `fourk=0`、`--qn-1080` 只映射 80；因此 OB-2 的 360P 可能是请求参数差异，T8.8 必须先做 qn=64/80/112/120 × fourk=0/1 矩阵，再决定最小取流修正或 blocked。交接含 M1 门清单（3/3 ≥720P、OP1–OP5、O1/O7 失败矩阵、DEC-5、本地 O9、无 P0/P1）、Medium 默认路径/凭据约束、pass/blocked 决策树；M1/M2 门与数字未改。本文件升至 v6.23。
- 2026-10-24：完成 **T8.6.1a 在线长按 PgUp/PgDn**（非门）：`StartStepRepeat`/连发定时器/`StepForward`/`StepBackward` 在增长会话改走异步步进状态机（不再用同步 `current_frame_` 判边界）；已缓存单格排队不跳帧、未缓存最新目标重定向/取消、首末帧边界、短按/快速双击、外来 seek 从已请求目标 ±1；重建 `MediaSession` 总是带部分索引；新增 `--online-longpress-test`（50 项/样本）。验证：Medium 副本 3 样本（OB-1 720P/OB-2 1080P30/OB-3 720P）×2 轮 + 构建目录 3 轮均 49–50/0；2s 长按 25–26 帧、轨迹 bad=0、UI gap 37–77ms、0 次 >200ms；未缓存提示 0ms、取消调用 4–17ms、回缓存 31–51ms；首末帧边界停止；短按 ±1/双击 ±2；本地 key-test 77/0（2s 27 帧）、ctest 30/30、M1 E2E 28/0 与 landing 29/0、buffer 10/0、P1/P2/P7 279.03/361.57/65.52ms。证据 [v2-online-interaction.md](docs/perf/v2-online-interaction.md)、交接 [T8.6.1a](docs/handoff-2026-10-24-t8.6.1a.md)；tasks-v2 升 v0.16；**M1/M2 门与数字未改，未进 T8.9/M2**。本文件升至 v6.25。
- 2026-10-24：完成 **T8.6.1a 实机回归 hotfix**（非门）：实机首次在线播放长按只走 1 帧、慢拖只变帧号。根因：未缓存每 70ms tick 取消在途 seek，慢 seek 永远落不了地（1 帧 starvation）；Qt/Windows 在物理键仍按住时可能补非 autoRepeat `KeyRelease`，焦点变化也停连发；`OnScrubFinished` 用部分索引长度 clamp。修复：未缓存改单格排队（retargets=0）、`GetAsyncKeyState` 伪 release 守卫、auto-repeat 恢复、`StopStepRepeat(reason)`/`step_start`/`focus_changed` 日志、拖动 clamp/提示。验证：用户样本 720P `BV1PAnt6qEga`/cid 42971761157 **54/0**（前进/后退 30→56/56→30 各 26 帧、bad=0；伪 release 8 帧、真正 keyup 停止；auto-repeat 保持）；`ctest` 30/30、本地 key-test 77/0；**慢拖预览仍 T8.6.1b；首次 open 已量（44MB 首帧 634ms/512KB 返回），不是主因，长等待是下载/播放缓冲；M1/M2 门与数字未改；未进 T8.9/M2**。证据 [v2-online-interaction.md](docs/perf/v2-online-interaction.md) v1.1、交接 [T8.6.1a](docs/handoff-2026-10-24-t8.6.1a.md) v1.1；tasks-v2 升 v0.17。**验证陷阱**：用户 Start Menu `lapian.lnk` 曾指向 `dist\lapian.exe`（2026-10-23 23:30，早于 T8.6.1a 01:10），实际测的是旧 T8.6 同步代码；已重指安装路径并替换 dist exe，此后手动验收先核对 `Get-Process lapian | Select Path,StartTime`。本文件升至 v6.26。
- 2026-10-24：**T8.6.1a 验收闭环**：用户确认 C 盘安装版 PgUp/PgDn 长按正常；结合 54/0、ctest 30/30、本地 key-test 77/0，T8.6.1a 收口。首次 open 实测 44MB 首帧 634ms/512KB，不是 open 阻塞。新增 [T8.9 提示词](docs/next-session-prompt-t8.9.md)；下一任务 **T8.9（M2）已确认**；tasks-v2 升 v0.18、perf/交接升 v1.2。M1/M2 门与数字未改。本文件升至 v6.27。
- 2026-10-24：**T8.3.1 缓存管理完成（非门）**：新增 `CacheManager`（摘要/记录/目录配额/清空/孤儿清理）+ 设置对话框（状态栏“设置”/Ctrl+,）；`CacheStore::ClearAll` 支持 bvid+cid 部分键保护当前会话，`CleanupOrphans` 返回条目数，`CheckDirectoryWritable` 可读只读错误；启动 3s + 每 6h 清理并回收配额；`--online-cache-manage-test` workspace 72/0（无 UI）/98/0（`--ui`）、Medium 默认 AppData **101/0**（integrity=medium，真实 AppData 与备份逐字节一致）、`lapian_online_cache_manage_test` 73/0、只读 ACL 75/0、`ctest` 31/31、dist 33.87MB/25；M1/M2 门与数字未改；tasks-v2 升 v0.19。本文件升至 v6.28。
- 2026-10-24：T8.3.1 交接后刷新 **T8.9 提示词到 v2**（基线 `cbab29b`；加入 T8.3.1 缓存管理语义、默认路径 Medium 约束、T8.9 边界/停止条件）；文档表同步。tasks-v2 仍 v0.19，M1/M2 门与数字未改。本文件升至 v6.29。

---

## 13. 新对话开场（可直接复制）

> 完整提示词见 [docs/next-session-prompt.md](docs/next-session-prompt.md)。核心：先读 `AGENTS.md`、`docs/handoff-2026-10-22-t7.7.md`、`docs/preview-release-2026-10-22.md`、`docs/perf/T7.7-acceptance-checklist.md`、`spec.md`、`plan.md`、`tasks.md`、ADR-001–006；T7.7 清单/预检脚本已入库，本机 `verdict=blocked`，P7 2s 复跑 65.33ms；**用户已决定内部预览收口，除非资源到位，不要追正式 T7.7/P14，也不要宣称通过**；以 `git log`/`ctest` 为准，不要重做 T1–T7.11.1（T7.11.1 已修复 P7 边界：2s 64.13ms、1s 27.11ms、1080p60 HEVC 67.31ms）；T7.7 仍需真实 V1–V10、基线机、冷缓存 F1-A1、Explorer 真双击，正式 P14 仍需真实 V10；4K 仍 proxy；T7.5/T7.6 缺口照旧；只做 F1–F8/P1–P14，spec v0.3 冻结；每任务保持 build/ctest/dist 通过并补交接/证据。
>
> **v2（在线）走另一条线**：S1/S2/S3 完成（T8.0–T8.12 完成）；DEC/B 已拍板（含 **DEC-6 扫码登录，T8.2.2 非门**）、ADR-007 附条件接受；**T8.8 M1 门已通过**（OB-2 的 360P 是请求 qn=64 导致，最小修正 qn=80/fourk=0；3/3 + OP1–OP3/OP5 + 失败矩阵 + 本地 O9）；T8.2.2 扫码完成但真实扫码未验证；**T8.6.1a 在线长按已完成（非门）**（异步步进/边界/未缓存；Medium 3 样本×2 轮 49–50/0、UI gap 37–77ms；**同日实机回归 hotfix**：未缓存单格排队/伪 release 守卫/拖动 clamp，用户样本 54/0；慢拖预览仍 T8.6.1b）；下一任务 T8.13（网络异常/并发加固），先读 [T8.12 交接](docs/handoff-2026-10-24-t8.12.md) 与 [T8.11 交接](docs/handoff-2026-10-24-t8.11.md)；T8.12 已完成档位策略/手动切档/降帧提示（OB-2 29.412fps 横幅+角标、1080→720、PTS 回位、标记保留），真 60/4K 仍不可得/未验证；tasks-v2 v0.23，非门 T8.6.1b 仍待**，见 [docs/spec-v1.0-delta.md](docs/spec-v1.0-delta.md)（v0.4）、[plan.md](plan.md) v2 章节、[tasks-v2.md](tasks-v2.md)（v0.23）、[S3 交接](docs/handoff-2026-10-23-s3.md)、[T8.1 交接](docs/handoff-2026-10-23-t8.1.md)、[T8.2 证据](docs/perf/v2-online-http-selfcheck.md)、[T8.2 交接](docs/handoff-2026-10-23-t8.2.md)、[T8.3 证据](docs/perf/v2-online-cache.md)、[T8.3 交接](docs/handoff-2026-10-23-t8.3.md)、[T8.4 证据](docs/perf/v2-online-growing.md)、[T8.4 交接](docs/handoff-2026-10-23-t8.4.md)、[T8.5 证据](docs/perf/v2-online-m1.md)、[T8.5 交接](docs/handoff-2026-10-23-t8.5.md)、[T8.2.2 证据](docs/perf/v2-online-qr.md)、[T8.2.2 交接](docs/handoff-2026-10-23-t8.2.2.md)、[T8.6 证据](docs/perf/v2-online-session.md)、[T8.6 交接](docs/handoff-2026-10-23-t8.6.md)、[T8.7 证据](docs/perf/v2-online-cache-hit.md)、[T8.7 交接](docs/handoff-2026-10-23-t8.7.md)、[T8.6.1a 证据](docs/perf/v2-online-interaction.md)、[T8.6.1a 交接](docs/handoff-2026-10-24-t8.6.1a.md)、[vision](docs/product-v2-vision.md)（v1.1）。不要用本节的 v1 提示词去做 v2。
- 2026-10-24：**T8.9 DASH 解析与档位完成（M2 第 1 项，非门）**：`playurl(fnval=4048)`/`dash.video[]/audio[]` 解析（qn/codecid/fps/带宽/size=未知/init+index/备份 URL）、AVC 默认/O6 顺序/60fps+4K 只手动、档位菜单（DASH 档置灰写 T8.10、页脚“菜单可选不等于保证可达”）；3/3 登录普通用户 DASH 1080P AVC（fps 30.000/29.412/25.000）、116/74/120 3/3 不可得、`quality` 撒谎（匿名 quality=64/dash 16/32）、durl 1080p 1/3；`lapian_online_dash_test` 117/0、`ctest` 32/32、`--online-quality-test` 3×27/0+`--ui` 21/0、M1 E2E 28/0（first_frame 640ms/UI gap 59ms，基线 656/62ms）、缓存管理 72/0+98/0、key-test 77/0、P1/P2 299.78–277.40/344.53–324.77ms、dist 34.02MB/25；M1/M2 门与数字未改；tasks-v2 升 v0.20。本文件升至 v6.30。
- 2026-10-24：**T8.9 交接后新增 T8.10 提示词**（基线 `a3cf655`）：DASH 双输入边界/验收/代码入口、先做 5 项 spike（total 字节/init-index Range/双 GrowingFormat/seek fan-out/EOF 时钟）、音频缓存键与 schema 决定点、停止条件；tasks-v2 v0.20，M1/M2 门与数字未改。本文件升至 v6.31。
- 2026-10-24：**T8.10 DASH 双输入完成（M2 第 2 项，非门）**：视频/音频双 `GrowingSource`/`DownloadJob`/`AVFormatContext`、音频主时钟、seek fan-out、DASH 缓存命中、durl 回退；音频缓存键 `audio-<id>` + meta `kind/audio_id/container=dash`（`lapian-cache-1` 不变、旧 meta 兼容）；**修复 fMP4 `av_seek_frame` 落前一 GOP**（`SeekToKeyframePts` + PTS 校验）；`--online-m2-test` 3/3 34/0（首帧 322.2/369.0/365.1ms、60s 音画偏差 p95 17.1/18.2/22.0ms、20/20 seek 帧号+PTS 精确、EOF 末帧停住、内存峰值 211.5/209.7/201.7MB、缓存命中 45–46ms/0 网络）；`--force-durl` M1 28/0、DASH M1 25/0、ctest 32/32、缓存 396/0、管理 72/98、key 77/0；**M1/M2 门、验收数字、OB 样本未改；未判 M2**；tasks-v2 升至 v0.21，下一任务 T8.11。本文件升至 v6.32。
- 2026-10-24：**T8.10 交接后新增 T8.11 提示词**（基线 `b560607`）：渐进索引/波形/总帧数边界、3min 样本（OB-3 212s/25fps）、`OnlineIndexBuilder` 当前“每 650ms 从头重建”的现状、慢增长测试要求、代码入口/停止条件/回归基线；tasks-v2 仍 v0.21，M1/M2 门与数字未改。本文件升至 v6.33。
- 2026-10-24：**T8.11 渐进索引/波形/总帧数完成（M2 第 3 项，非门）**：新增 `IncrementalOnlineIndexBuilder`（前沿 guard/`resume_pos` 重开、`Snapshot`、tail guard 48、`OnlineIndexStats`）、`MainWindow` 250ms 安装节流 + seek pending 补装 + 三态总帧数、波形 bucket 增长重绘、`DownloadOptions.throttle_bytes_per_sec` 测试钩子、`--online-index-test` 与 `lapian_online_index_test`；OB-3 真实在线 27/0（首次索引 128ms、30s 内容 6.209s、20/20 seek 0 落点、exact 5306、波形 0.525 帧、UI 69ms、`opens=1/reopens=0/read_ratio=1.000`）、离线 2MB/s 22/0、单测 28/0、`ctest` 33/33、M2 3/3 34/0、M1 `--force-durl` 28/0、quality 3×27/0+`--ui` 21/0、缓存 396/0、管理 75/0+98/0、key 77/0、P1 276.98–299.32ms、P2 322.65–327.44ms、P7 0.85/0.45ms；**M1/M2 门、验收数字、OB 样本、缓存 schema 未改；未判 M2**；下一任务 T8.12。本文件升至 v6.34。
- 2026-10-24：**T8.12 档位策略/手动切档/降帧提示完成（M2 第 4 项，非门）**：`OnlineRequest.requested_qn/requested_codecid`（默认 0→80，M1 URL 字节不变）、DASH 请求档缺失明确失败、缓存命中匹配请求档；`DefaultQn` 允许 AVC 60fps 自动、HEVC/AV1-only 60fps 只手动、4K 始终只手动；`QualityMenuContext` 权限列档/实际 qn/未登录 480P；`FrameRatePolicy` 标题 60fps 证据、dash_fps 实际值、普通 30/25 不误报、无证据只“可能降帧”；`MainWindow` 切档（停旧流/重取/索引波形/PTS 回位/标记保留/实际上下文）；`InfoBar` DEC-4 可关横幅 + 常驻角标；OB-2 29.412fps 横幅/角标，OB-1/2/3 1080→720 成功（qn=64、PTS 1,000,000→第 30 帧、标记 1→1、截图 qn64/codecid7/fps29.412）、菜单点击 61/75/76ms；真 60/4K 不可得/未验证；`ctest` 34/34、dash 125/0、frame-rate 27/0、quality 3×45/46/45、UI 50/56/50、M2 34/0×3、M1 28/0、index 27/22/28、长按 54/0、缓存 396/0、管理 75/0+72/0+98/0、key 77/0、dist 34.21MB/25；**M1/M2 门、验收数字、OB 样本、缓存 schema 未改；未判 M2**；下一任务 T8.13。本文件升至 v6.35。
- 2026-10-24：**T8.13 网络异常/并发加固完成（M2 第 5 项，非门）**：新增 `lapian_online_fault_test`（ctest，15 组 153 项 0 失败）与 `--online-fault-test`（真实窗口，10/10 场景 92 项 0 失败、`unverified=1`）；修 4 个产品缺陷（下载失败后不 Cancel 读层→已缓存前缀可读/前沿 EIO、4s 网络无响应看门狗、`kOnlineApiTimeoutMs=4500`、缓存损坏自动 Rebuild）；12 行矩阵全过：断网 3,981–4,155ms、看门狗 4,121–4,137ms、API 超时 4,515–4,529ms（真实 WinHTTP 静默服务器）、磁盘不足 5ms、关闭 7–66ms、UI gap 73ms/0 次、续传 offset 409,600/1,228,800、403→备份 1 次只下缺失字节、过期刷新 1 次、脱敏无 SESSDATA/签名 URL；4 读者真实 600s：ops 25,907、waits 522、`timeouts=0`、close 16ms；`ctest` 35/35、M2 3/3 34/0、M1 `--force-durl` 28/0、quality UI 50/56/50、index 27/0+22/0、长按 54/0、管理 72/0+98/0、key 77/0、P1/P2 268.67/322.34ms、P7 0.19/0.24ms、dist 34.3MB/25；**真实 Windows 休眠、真实 CDN 故障、窗口级 DASH 单流失败未验**；**开发期 1 次未复现崩溃/1 次未复现卡住已列残余风险（20+ 次全量运行无复现）**；**M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema 未改；未判 M2**；tasks-v2 升至 v0.24，下一任务 T8.14（M2 门）。本文件升至 v6.36。

- 2026-10-24：**T8.14 M2 门完成（pass，六条硬条件）**：3/3 OB DASH 1080P30（qn=80 AVC，fps 30.000/29.412/25.000，ready 330.5/365.6/353.6ms，34/0×3）；OP1 p95 327/366/343ms（20/20/20 有效；另列 1 次 10.4s 拖慢 + 1 次 resolve 超时，计入仍 20/21 ≤5s；1s 下行 4.985–10.98MB/s）；OP2 p95 46/57/53ms（20/20×3 ±1）；OP3 提示 0ms/取消 26ms/落点 0；OP4 补充长样本 L-1（386s，DASH 1080P AVC 29.970fps，非 OB）300,000ms、0 restart、stalls>200ms=0、underrun=0、UI gap 21ms、内存 121.9MB、17/0；OP5 Medium+默认 AppData 20/20、p95 111.7ms、0 playurl/DASH/quality/HTTP、0 TCP/UDP；60s 音画 p95 17.6/17.4/20.0ms、20/20 seek 精确；`--online-fault-test` 10/10 92/0、缓存 32/0、管理 72/0；O9 0 TCP/UDP（137 轮询）、key-test 77/0；P1/P2/P7 p95 311.87/390.59/1.47/0.53ms；空闲 63.2MB、在线峰值 ≤206.6MB；`ctest` 35/35、dist 34.3MB/25、导入表无 QtNetwork/TLS/winhttp、日志 0 SESSDATA/签名参数；**残余风险**：M2 期间 1 次未定位 teardown 0xC0000374、1 次 L-1 seed 卡住（未复现）、OP3 OB-3 慢 landing UI gap 36s（落点 0）、真实休眠/真实 CDN/窗口级 DASH 单流未验证、F1–F8 UI 探针未全重跑；**不写“无崩溃/已证明稳定”**；**M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema 未改；未做 4K/60、T8.15/T8.16/T8.6.1b**；tasks-v2 升至 v0.25，下一任务 T8.15（非门，可并行）/T8.6.1b/T8.16。本文件升至 v6.37。
- 2026-10-24：**T8.16 打包/隐私/卸载完成（pass，非 MVP 门）**：安装/卸载 0 TCP/UDP（12–13 / 6–7 采样）、P12 dist 34.32/stage+安装 34.43/zip 14.33/setup 11.79MB、安装版 Medium 直接启动、默认缓存 OP5 10/0（ready 120.7ms、qn=80、0 HTTP）、UI 写默认 credentials.dat（DPAPI）、卸载默认保留缓存/记录/截图/设置/凭据与 /CLEARCACHE 只删缓存、ctest 35/35、key 77/0、O9 65 采样 0、P1/P2/P7 308.81/361/1.63/0.34ms；Inno 卸载器静态 winhttp 在 {app} 外、20GB 未真删；T8.15 未验证、T8.6.1b 待；tasks-v2 v0.26，下一任务 T8.15/T8.6.1b。本文件升至 v6.38。
- 2026-10-24：**T8.6.1b 在线拖动预览完成（非门）**：新增 `OnlinePreviewDecoder`（独立 `GrowingFormat`/cursor/软解、稳定前缀、latest target+generation、1280 NV12、24 帧反向缓存、End interrupt+join）；`MainWindow` 增长会话拖动接入（已缓存连续预览、未缓存「缓冲中（预览）」、松手先停预览再精确 seek、索引快照节流、关窗/播放/切档前停预览、`S` 守卫）；新增 `--online-drag-preview-test` 与 `lapian_online_preview_test`；DASH 1080P30 已缓存 1s/60 样本 **55 presented / 54.95fps total / 61.18 active**、UI gap **69ms/0 次 >200**、stale 0、松手 **frame 75/PTS 2,500,000**、关窗 56ms；未缓存 3312 提示 0ms/waiting/取消 0ms；缓存命中 **56/59.89fps**/0 playurl+DASH、durl **55/60.04fps**、本地 p4 **61.8fps**/p6 51.0/p8 26 帧；`ctest` **36/36**、key **77/0**、longpress **54/0**；M2 1 条尝试被 `vip_status=1` 拒绝，**引用 T8.14 3/3 34/0，不写 M2 重新通过**；P1/P2/P7 **PASS exit 0**（P1 275.64/288.02ms、P2 332.66/341.7ms、P7 back 0.90/fwd 0.37ms，0 超差）；**M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema 未改；未做 T8.15/T8.16/4K/60**；tasks-v2 v0.27，下一任务 T8.15（大会员条件验收，非门；未执行）。本文件升至 v6.39。
- 2026-10-24：**T8.15 启动交接（非门，未执行）**：用户确认**已有大会员账号**；默认 DPAPI 凭据在 M2 预检中只读观察到 `nav vip_status=1`，但 **T8.15 仍未执行、未通过**。新增 [T8.15 启动交接](docs/handoff-2026-10-24-t8.15-start.md) 与 [新会话提示词](docs/next-session-prompt-t8.15.md)：账号/Medium 约束、O11-A1…A7/OP9/OP10、建议 `--online-vip-test`（116/74/120 实际档位、AVC 自动/HEVC-only 手动、4K 手动/能播、已缓存 P7≤80ms、切回 1080P30）、测试/停止/交付；**不改 M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema、默认 AVC≤1080p**；不写 MVP 通过；未执行任何 T8.15 实测、未把 VIP 登录写成档位可达。tasks-v2 v0.28，B 条件验收仍是下一任务。本文件升至 v6.40。
- 2026-10-24：**T8.15 大会员可用档条件验收完成（partial，部分通过，非 MVP 门）**：新增独立 `--online-vip-test`（`--vip-catalog-only`/GUI；复用 `OnlineSession.requested_qn/requested_codecid`、`QualityMenu`、`FrameRatePolicy`；只读默认 DPAPI；不打印凭据/URL）与只读 `OnlineCodecidForTest()`；默认凭据 `nav vip_status=1`（484B，SHA-256 不变）。真实目录：OB-2 `BV1mA411m7pS`（cid 1004967230，96s）大会员下 **qn=116 AVC 1920x1080@62.500fps/4.22Mbps**（同档 HEVC 58.824/AV1 60.150），80 AVC 29.412、64 AVC 29.412，**74/120 不存在**；4K SDR `BV1afnv6GEDA`（cid 42913040245，27s，2160x3840）**120 AVC/HEVC/AV1 均 60.000fps**，自动默认 116、不自动选 4K；4K HDR `BV1dJjv6PEWF` 只做目录。**A1 pass**；**A2 partial**（1080P60 AVC 自动 qn=116/codecid=7、容器 60.000/dash 62.500、无降帧横幅、20 次 PgUp/PgDn 严格 ±1、p95 **47.5ms**、hw=1、margin 179 帧≈2.86s；**720P60 qn=74 未返回 → unverified**）；**A3 unverified**（无 HEVC/AV1-only 60 样本；`FrameRatePolicy` 无“更高帧率档可手动切换”提示，代码审计缺口）；**A4 pass**（4K 仅手动；AVC 8s/0→477 帧、20 步 p95 60ms、复跑 10 步 77.3ms；HEVC 5s/10 步 p95 31ms；cache-hit p95 138.9ms 只记录；无崩溃；不承诺 P7/P9/颜色）；**A5 pass**（116→80 PTS 500,000→frame15、fps 62.5→29.412、标记/记录/旧档缓存保留、字节只增；120→80 PTS→frame15、fps 60→30）；**A6 partial（部分通过）**；**A7 门未改**。回归：`ctest` **36/36**、key **77/0**、P1/P2/P7 p95 306.98/372.79/0.9ms 0 超差、T8.6.1b/M1/M2 引用、dist **34.48MB/25**、0 SESSDATA/Cookie 值/签名 URL、默认缓存/记录/凭据未写；**残余**：4K 切档首跑瞬时“写入缓存文件失败”复跑通过、4K HEVC 一次“已完整缓存但未走命中”未复现、T8.14 teardown/seed 保留；**M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema 未改；不写 MVP 通过**；证据 [v2-online-vip.md](docs/perf/v2-online-vip.md)、交接 [handoff-2026-10-24-t8.15.md](docs/handoff-2026-10-24-t8.15.md)；tasks-v2 v0.29；v2 非门后续清空，发布前审计继续。本文件升至 v6.41。
- 2026-10-24：**本地拖动预览快速大跳热修完成（用户反馈，非门）**：`PreviewDecoder` 将 `generation`（目标版本）与 `session`（拖动会话）分离；正向拖动每解一帧追最新目标、发布中间帧且不再因目标变化中断；反向拖动先解完当前目标再取最新，允许“比当前目标晚但比上一张已显示帧更早”的粗帧；32 帧预览缓存保留。新增 `--drag-fast`（`--drag-fast-step`，正/负）真实产品路径探针。`media\测试.mp4`（1080p H.264 30fps、4573 帧、GOP 30 帧）快拖 +60 帧/16ms：**28.6fps/最大空档 93.8ms → 61.0fps/31.7ms**；+120/+300 约 59–60fps；反向 -60：**42.2fps/48.5ms**；反向 -20 仍 11.5fps/191.8ms 为残余。回归：P4 61.3fps、P6 16.6ms、P7 0.0/9.4ms、P8 12.7fps、P9 31.1ms；`lapian_preview_decoder_test` 全过（2s 63.85fps、反向 40 步精确/单调）；`ctest` 36/36、key 77/0、T8.6.1b 35/0。**未改 P4/P7 验收数字、M1/M2 门、OB 样本、`lapian-cache-1` schema、默认 AVC≤1080p；不写 4K/60fps 全片保证**；证据 [local-drag-fast-2026-10-24.md](docs/perf/local-drag-fast-2026-10-24.md)、交接 [handoff-2026-10-24-drag-fast.md](docs/handoff-2026-10-24-drag-fast.md)；dist 34.48MB/25；T8.15 仍 partial。本文件升至 v6.42。
- 2026-10-24：**拖动预览 v2 规划完成（DP 计划，非门，未实施）**：上一轮 `079f0dd` 解决“不冻住”，但用户实测超大幅度/反向仍不够；确认 `--drag-fast` 的 fps 不等于“显示帧==目标”。规划 DP-1 硬件预览（共享 D3D11、软件回退）、DP-2 反向缓存/预取/远跳关键帧粗帧、DP-3 快拖/慢拖策略 + `target_lag` 诊断、DP-4 集成；1080p 先达标、4K best-effort；预览缓存 ≤256MB、缩略图 ≤64MB，不做全片缓存；不改 M1/M2/P4/P7、OB、`lapian-cache-1` schema、默认 AVC≤1080p、`OnlinePreviewDecoder`/T8.6.1b。计划 [plan-drag-preview-v2.md](docs/plan-drag-preview-v2.md)、任务 [tasks-drag-preview-v2.md](docs/tasks-drag-preview-v2.md)、交接 [handoff-2026-10-24-drag-preview-plan.md](docs/handoff-2026-10-24-drag-preview-plan.md)、下一会话 [next-session-prompt-dp1.md](docs/next-session-prompt-dp1.md)。本文件升至 v6.43。
- 2026-10-24：**DP-1 硬件预览完成（1080p 保证档）**：`PreviewDecoder` 借用 GfxWidget 共享 D3D11 设备硬解、失败自动回退并记录 `preview_hw_fallback`；硬件帧不 `sws_scale`、不缓存；4K `source_too_large` 回退软件只记录；`--drag-fast` 新增 `hw_active`/`target_lag_frames|ms`/`seeks`/`cache_hits`/`active_fps`；`+60` 61.0–76.3fps/36–46ms、`-60` 56.8–60.7fps/32–38ms、P4 64.1fps、P6 16.6–28.6ms；`ctest` 36/36、key 77/0、T8.6.1b 35/0；证据 [drag-preview-dp1.md](docs/perf/drag-preview-dp1.md)、交接 [handoff-2026-10-24-dp1.md](docs/handoff-2026-10-24-dp1.md)；本文件升至 v6.44。
- 2026-10-24：**DP-2 反向缓存/预取/远跳粗帧完成（1080p 默认硬件；软件快反向残余）**：新增 `PreviewRingCache`（软件 NV12，192MiB）与 `PreviewThumbnailCache`（320×180，64MiB），32 帧热缓存保留；`Begin/UpdateTarget` 先查精确缓存，局部慢速反向由独立低优先级预取线程解上一 GOP，远跳先发粗帧（软件缩略图/硬件关键帧全分辨率 clone）再精确；`PreviewFrame.coarse/from_cache/hardware` 与 `--drag-fast` 缓存/粗帧/预取/分段耗时诊断；新增 `--drag-far-test`、`--drag-fast-cycles`、`lapian_preview_cache_test`。`media\测试.mp4` 1080p：`-60` 60.2–69.2fps/空档 25.2–33.0ms、`-20` 45.7–48.7fps、`+60` 68.7–79.5fps/26.2–48.0ms、`-1` 局部反向命中 84.8–86.4%、远跳首帧 20–41ms（软件粗帧 19.9–40.7；硬件精确首帧 20–21ms）；预览缓存峰值 233.4MiB、缩略图 ≤28.8MB、3 次拖动峰值不变；P4 64.1fps、P6 16.8ms、P7/P8/P9 不退化；`ctest` 37/37、key 77/0、T8.6.1b 35/0；强制软件快反向 `-20` 9.9–11.4fps/`-60` 30.3–33.0fps 为残余，不写通过；4K 只记录；不改 M1/M2/P4/P7、OB、schema、默认 AVC≤1080p、在线预览；证据 [drag-preview-dp2.md](docs/perf/drag-preview-dp2.md)、交接 [handoff-2026-10-24-dp2.md](docs/handoff-2026-10-24-dp2.md)；本文件升至 v6.45。
- 2026-10-24：**DP-3 拖动交互策略 + 验收探针完成（1080p 默认硬件；软件快反向残余）**：新增 `DragStrategy` 四模式（Exact ≤4 帧 / Fast / Reverse / FarJump ≥300 帧；按 step/方向/间隔/连续同向次数判定，阈值/速度/原因进诊断）；`PreviewDecoder::SetDragMode` 在 Exact 关闭粗帧（Begin/UpdateTarget/缩略图三处），FarJump 用 `deferred_exact` 保证粗帧先呈现、精确帧下一 tick 替换，并新增“精确目标已呈现后禁粗帧”的安全约束；`PreviewTestDiagnostics` 增加 `drag_mode/reason/step/interval/speed/run/target_lag_frames|ms/cache_hit_rate/displayed_hardware`；新增 `--drag-strategy-test` 场景矩阵。`media\测试.mp4` 1080p 硬件：慢拖 +1/-1 lag p95 **0**（15.6ms 节奏 +1 p95=1/-1 p95=2）、`+60` 76.9–80.8fps/空档 34.7–37.2ms、`-60` 54.4–64.4fps/23.8–39.3ms、`-20` 47.3–49.2fps、`-1` 命中 84.7–85.5%、远跳粗帧 **20.8/20.6ms** 后精确 **30.6/30.0ms**；P4 94.9（本轮计时器粒度约 10ms）、P6 p95 18.8、P7 0.1/11.8、P8 12.8、P9 32.2；预览缓存峰值 233.4MiB、3 次拖动不变、缩略图预算未变；`ctest` 37/37、key 77/0、T8.6.1b 35/0；强制软件 `-20` 9.5fps/`-60` 31.4fps 为残余（与 DP-2 同量级，不写通过）；4K 只记录（`source_too_large`）；不改 M1/M2/P4/P6/P7、OB、schema、默认 AVC≤1080p、在线预览；证据 [drag-preview-dp3.md](docs/perf/drag-preview-dp3.md)、交接 [handoff-2026-10-24-dp3.md](docs/handoff-2026-10-24-dp3.md)；本文件升至 v6.46。
- 2026-10-24：**DP-4 拖动预览 v2 集成/发布完成（DP-0…DP-4 收口，非门）**：从 `d586548` clean Release 重建（`build-release.ps1 -Clean`，256.4s）；`ctest` **37/37**、key **77/0**、T8.6.1b **35/0**（UI gap 93ms）；dist **25 文件 / 36,227,912B / 34.55MB**（比 DP-2 时期 +71,168B，全部来自 `lapian.exe`），无 QtNetwork/D3dcompiler_47/tls/networkinformation/generic，导入/环境自检通过；重建后策略矩阵 `all_pass=1`、`+60` 80.7fps/31.2ms、`-60` 50.4–57.5fps/29.0–38.7ms、`-20` 49.8fps、`-1` 82.3fps/命中 84.2%/lag p95 2、远跳粗帧 21.9/20.5ms→精确 29.8/40.8ms、`--drag-test` P4 64.5/P6 22.3/P7 0.1/16.1/P8 12.9/P9 39.8；预览缓存峰值 **244,684,800B=233.4MiB**、缩略图 ≤2.85MB、`readbacks=0`；Medium 副本（stage 整目录复制，hash 与 dist 一致）`--drag-test` P4 65.0/P6 17.7、策略矩阵 `all_pass=1`、key 77/0、`--loop-test` 3 循环/定帧/恢复，默认凭据/设置/记录/缓存未写；zip 14.43MB、setup 11.86MB，setup 从 E: 启动被 SmartScreen 拦截，本轮未重装；强制软件 `-20` 10.5fps/190ms、`-60` 33.4fps/50ms 与 4K 67.6fps/11.5ms/`source_too_large` 仍为残余/只记录；**不改 M1/M2/P4/P6/P7、OB、schema、默认 AVC≤1080p、在线预览；无代码改动**；证据 [drag-preview-dp4.md](docs/perf/drag-preview-dp4.md)、交接 [handoff-2026-10-24-dp4.md](docs/handoff-2026-10-24-dp4.md)；本文件升至 v6.47。
- 2026-10-25：**在线播放修复验收完成（partial，非门；不改代码）**：从 HEAD `4d89683`（实现 `e1a89c6` 清晰度切换 + `4d89683` 在线拖动）clean Release 重建（222.4s、`ctest` 37/37、dist **34.68MB/25 文件 / 36,359,496B**、`lapian.exe` 3,149,824B / SHA256 `7A80…3761`），并用 `dist\*` 覆盖 Medium 副本（33 文件、hash 一致）独立复验。**A pass**：真实链路 qn=80→112→80、codecid=7、fps=30.000、PTS 1,000,000→1,000,000、第 30 帧、标记/记录/截图保留；切回 `cache_hit=1`、dash=0、playurl=0；`checks=42 failures=0`；无“已完整缓存…”错误。**B pass**：正向 57/60 presented、17ms、33ms 空档、56.21fps；反向 57/60、31ms、33ms、56.72fps；presented 差 0.0%、landing 75/2,500,000 与 15/500,000 精确；最大缩放 ±10 每步 max_resp 75/86ms、无闪烁/无连续两步空；远跳粗帧 31/17ms→精确 1695/142ms；未缓存提示 0ms/可恢复/落点精确。**C 稳态达标但首次拖动 224ms/连续 6 个目标无新画面**（T8.6.1b 首次正向 152ms）超过 100ms/“连续 2 个”门槛 → **总判 partial，不写“完成/丝滑通过”**；本地基线 58.53/57.00fps（在线 96.0%/99.5%）、空档 21/45ms。回归：`ctest` **37/37**（手动 61.21s）、key **77/0**（Medium）、T8.6.1b **35/0**（Medium，UI gap 62ms）、`--drag-test` P4 65.6/P6 **15.9**/P7 0.0/12.6/P8 12.8/P9 33.5（Medium P6 17.7）、`--drag-strategy-test` 3 次中 2 次 `all_pass=1`（1 次 fast_forward 空档 51.5ms 边缘）、`--drag-far-test` 粗帧 13.9/19.8→精确 21.0/29.8ms、`readbacks=0`；在线预览缓存峰值 60.75MiB、缩略图 432KB；4K `proxy-V4` 只记录（P4 63.5/P6 144.6/P9 229.7/缓存 237.3MiB）。**残余**：首次拖动冷启动、探针 `response_p95` 恒 0 与 `landing_frame`=PTS、新缓存根首写失败一次后重试（A/B/C/T8.6.1b 四次复现）、未缓存区长等待未测、4K/60 未验。**不改 M1/M2/P4/P6/P7、OB、schema、默认 AVC≤1080p、本地 PreviewDecoder/DragStrategy；无源码改动**；证据 [online-quality-switch.md](docs/perf/online-quality-switch.md) §10、[online-drag-parity.md](docs/perf/online-drag-parity.md) §12、交接 [handoff-2026-10-25-online-parity.md](docs/handoff-2026-10-25-online-parity.md)；本文件升至 v6.48。
- 2026-10-25：**P0 发布收口完成（P0 范围，HEAD `08484ee`；v6.49 的 P0 首拖修复见文件头/§1）**：`925b8d5` 修复新缓存根首包 `bytes=0 ... 写入缓存文件失败`——实为 DASH 总长探测 `FirstChunkSink` 主动停止被映射成 `kSinkError`，不是写盘；新增 `DownloadRequest::headers_only`，探测只读响应头、不读 body/不碰 sink，下载路径不变；3 个全新根修复前各 2 条、修复后各 0 条，parity 61/0。`08484ee` 修复本地 `PreviewDecoder::Impl::CloneExactLocked` 在 `Process`/`PrefetchStep` 两处无锁调用，与 UI/预取线程的缓存增删、AVFrame eviction 竞争；修复前隔离 20 次失败 2 次（0xC0000374/0xC0000005）、全量 ctest 首跑失败（avutil），修复后隔离 **50/50**、全量 `ctest` **3×37/37**（64/62/63s）。T4：`build-release.ps1 -Clean -Package` **242s**、exit 0、脚本内 ctest 37/37（63.43s）、dist **25 文件 / 36,392,776B / 34.71MB**、`lapian.exe` 3,183,104B / SHA256 `8344A2BB871130A7F21726FA72BA167296942BCEE5ABB47699D1D6AE6C1304CA`、setup 12,474,791B（`C6F3B075…`）、zip 15,197,307B（`8420FEA5…`）；setup 复制到 `%TEMP%` 后 `/VERYSILENT /CURRENTUSER` exit 0/2s，安装版 hash 与 dist 一致，Start/桌面快捷方式 target+hash 一致，settings 1183B 未动。T5：parity **61/0**（cold first_publish 26ms / first_delivered 36ms、post 46ms、forward 56.94/backward 56.66fps、gap 31/32ms、presented 差 0.000、cache 63,700,992B/72、UI 31/32ms）、T8.6.1b **35/0**、A 80→112→80 **42/0**（PTS/帧号/标记/记录/截图保留、切回 cache_hit=1、dash/playurl=0）、key **77/0**、local **9/0**、`--drag-test` P4 64.4/P6 **18.0**/P7 0.0/16.9/P8 12.8/P9 31.1、`--drag-strategy-test` **7/7 all_pass=1**。**残余**：preview 崩溃无符号化 dump（本机无 cdb/无管理员配 LocalDumps，按契约+崩溃签名+压测收口，不写“证明无崩溃”）、4K/60 与 `post_seek_immediate` 未覆盖、用户“流畅度很好”仍是体感确认；不改门/阈值/schema/本地预览语义（仅补锁），未上硬解；证据 [tasks §9.9](docs/tasks-online-parity-fix.md)、[perf §13.8](docs/perf/online-drag-parity.md)、[交接](docs/handoff-2026-10-25-p0-close.md)；本文件升至 v6.50。
