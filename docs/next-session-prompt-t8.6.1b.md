# 下一个会话提示词：T8.6.1b（在线拖动预览，非门任务）

- **版本**：v1（2026-10-24，T8.16 收口后；基线 commit `0c19da5`）
- **直接复制本文件给新会话。** 只做 **T8.6.1b（在线拖动预览）**；不要做 T8.15（大会员条件验收，当前无账号→未验证）、T8.16（已完成）、T8.6.1a（已完成），也不要把 M1/M2 门重跑一遍当成 T8.6.1b 的验收。
- **当前状态**：M1（T8.8）与 M2（T8.14）均通过；T8.16 打包/隐私/卸载 **pass（非 MVP 门）**；`ctest` **35/35**、`dist` **34.3MB/25**；T8.15 无大会员账号，未验证；T8.6.1b 未做；T8.14 的 1 次 teardown `0xC0000374`、1 次 L-1 seed 卡住未定位/未复现，**不得写“无崩溃/已证明稳定”**。
- **一句话**：在线增长会话拖动时间轴时，当前只有帧号变化；T8.6.1b 要让**已缓存区**拖动中连续显示降质预览（≥30fps），**松手精确落帧**；未缓存区不冻结 UI、显示等待且可取消。缓存命中/本地文件的现有预览语义不能退化。

## 1. 先读

1. [AGENTS.md](../AGENTS.md)（v6.38：当前状态、约束、文档表）
2. [tasks-v2.md](../tasks-v2.md) 的 **T8.6.1b 行**、T8.6.1a 行、T8.11/T8.14 行；版本记录 v0.26
3. [T8.6.1a 证据](perf/v2-online-interaction.md)（重点 §0.1：增长会话 `preview_ == nullptr`、慢拖只有帧号变化；T8.6.1b 被明确留出）
4. [T8.11 证据](perf/v2-online-index.md)（部分索引/稳定前缀/未缓存目标；拖动预览不能依赖全索引）
5. [T8.6 证据](perf/v2-online-session.md)（在线会话、部分索引、seek/未缓存等待、UI 线程约束）
6. [spec-v1.0-delta.md](spec-v1.0-delta.md) 的 O2/O3/O10、OP2/OP3/OP5（只引用已拍板口径，不新增验收值）
7. [T8.14 M2 证据](perf/v2-online-m2.md) 与 [T8.14 交接](handoff-2026-10-24-t8.14-m2.md)（M2 基线/残余风险；T8.6.1b 不改门）
8. [T8.16 交接](handoff-2026-10-24-t8.16.md)（安装版、Medium、卸载器外部目录；不要重做打包/卸载）
9. 代码入口：
   - `src/app/MainWindow.cpp`：`OnScrubStarted`（约 L5403）、`OnScrubTargetChanged`（L5425）、`OnScrubFinished`（L5435）、`OnPreviewTick`（L5477）、`RequestOnlineSeek`（L3053）、`GoToFrame`/在线 seek 落地（L3232）、`preview_` 初始化（L4674）
   - `src/app/MainWindow.h`：`preview_`、`preview_timer_`、`online_seek_*`、`Scrub*ForTest`、在线测试钩子
   - `src/media/PreviewDecoder.{h,cpp}`：现有本地预览解码器（路径 + `FrameIndex`；最多约 1280 长边、NV12、独立线程、generation 丢弃过期帧）
   - `src/media/MediaSession.*` / `VideoDecoder.*`：`OpenGrowing`、`SeekToFrame/SeekToEstimatedPts`；DASH 视频/音频双输入
   - `src/online/GrowingSource.h`：`GrowingCursor`、`GrowingFormat`、`Publish/WaitFor/Interrupt`
   - `src/app/OnlineSession.h`：`OnlineReadyInfo.source/audio_source/part_path/dash`
   - `src/app/TimelineWidget.*`：`scrubStarted/scrubTargetChanged/scrubFinished`
   - 探针参考：`src/app/OnlineLongPressTestRunner.*`、`src/app/OnlineM2TestRunner.*`、`src/app/main.cpp` 的参数分支
10. 构建/安装：`scripts/build-release.ps1`、`scripts/package.ps1`；安装版在 `%LOCALAPPDATA%\Programs\拉片软件`（Medium）。工作区 `dist`/`build` 是 Low integrity；真实默认 AppData/凭据测试必须用安装版。

## 2. 任务边界（官方验收）

> 已缓存区慢拖画面连续更新 **≥30fps**（降质预览）；松手精确落帧；未缓存区不冻结 UI、可取消；不改变本地预览语义。

**必须做**：

1. 在线**增长会话**（`online_ready_ && !online_cache_hit_`）拖动时，已缓存区连续发布降质预览帧；不能只改帧号。
2. 松手后走现有精确路径 `GoToFrame(target)` / 在线 seek，落点必须为目标帧（frame + PTS 校验；不能拿预览帧冒充目标帧）。
3. 未缓存/未索引区：显示“缓冲中（预览）”或等价等待提示；UI 线程不阻塞；新目标/松手/关窗可取消，不产生无界线程或排队。
4. **缓存命中**（`online_cache_hit_`）和**本地文件**现有 `PreviewDecoder` 路径不退化；本地 T2.9.1 的“只发布目标帧/丢弃过期帧/32 帧缓存”语义不变。
5. DASH 双输入只预览**视频**流；音频不擦洗、不改变现有静音/暂停策略；rotation 沿用 `session_->info().rotation`。
6. 拖动中按 `S` 不截图（沿用现有限制/提示；预览帧不是目标帧）。
7. 新增可重复探针 `--online-drag-preview-test`（名称可调整，但必须能自动化测），证据写入 `docs/perf/v2-online-interaction.md`（追加 T8.6.1b 章节）和 `docs/handoff-2026-10-24-t8.6.1b.md`。

**不做 / 不改**：

- 不新增 4K/60fps 验收；不写“60fps/4K 可用”；不把 T8.15 混入。
- 不改 M1/M2 门、验收数字、OB 样本、DEC-2/DEC-5、`lapian-cache-1` schema/缓存键。
- 不改 T8.13 加固语义（失败后不 Cancel 读层、4s 看门狗、API 4.5s、缓存损坏自动 Rebuild）。
- 不改 T8.12 档位、T8.11 增量索引语义、T8.10 DASH 双输入、T8.6.1a 异步步进/单格排队/伪 release 守卫。
- 不做 T8.16 的安装/卸载/隐私重做；不要卸载现有安装版，不要清真实 `%LOCALAPPDATA%\拉片软件\cache`、`%APPDATA%\拉片软件` 记录/凭据/截图。
- 不把同步 `RequestOnlineSeek` 直接塞进 `mouseMove`/预览 tick；不能在 UI 线程 join/等网络/等 `GrowingSource` 阻塞读。
- 不把预览帧写成精确落帧；不把“菜单可选/预览可用”写成清晰度保证。

## 3. 现状与硬事实

### 3.1 现在为什么慢拖只有帧号

- `MainWindow::OnScrubStarted`：`preview_ != nullptr` 时才 `Begin/Start`；在线增长会话在 `ApplyOnlineReady` 里不创建 `preview_`（条件 `ready && (!online_mode_ || online_cache_hit_)`），所以走 `else if (OnlineStepEligible())`，只显示“在线增长会话暂无拖动预览；松开后跳转到目标帧”。
- `OnScrubTargetChanged`：只有 `preview_ != nullptr` 才 `UpdateTarget`；增长会话只 `ShowScrubTarget`（改帧号）。
- `OnScrubFinished`：增长会话用 `OnlineLastFrame()` clamp，然后 `GoToFrame(target)` 精确落帧；这个 clamp 是 T8.6.1a hotfix 加的，不要退回用部分索引长度。
- `preview_timer_` 16ms；`OnPreviewTick` 从 `PreviewDecoder::TakeLatestFrame` 取帧并 `gfx_->SetVideoFrame`；本地预览器只接受路径 + `FrameIndex`，不适合直接指向增长的 `.part`（打开时长度/EOF 会变化）。

### 3.2 T8.11 部分索引

- 在线增长会话只有**稳定前缀** `session_->index()`（末尾 48 帧不暴露）；未覆盖目标走 `SeekToEstimatedPts`，解码后校验 PTS 偏差 ≤半帧，否则报错。
- 预览可以近似（降质/可能不是目标帧），但**不能**把近似帧当成精确目标；松手仍走精确 seek。
- `OnlineLastFrame()` 是当前可步进/可落点的上界；拖动目标必须 clamp 到它，不能拖到未缓存区后崩溃或跳错。

### 3.3 T8.6.1a 的并发约束

- 未缓存长按是“单格排队”，不取消在途 seek；拖动预览是另一条读路径，**不能**复用/干扰 `online_step_` 的 in-flight 状态。
- `RequestOnlineSeek` 在 UI 线程会取消旧 worker 并 `join()`；拖动中每帧调用它必然卡 UI，禁止。
- 预览解封装/解码必须在独立线程；新目标只更新“最新目标”，旧帧/generation 丢弃；同一时刻最多一个预览 worker/一个在途解码。
- 松手时先停止预览（不 join 到 UI 卡死；可短暂等待/异步收尾），再调用现有精确 seek；关窗路径要能结束预览线程（参考 `StopOnlineSeekWorker` 的 generation/cancel/join 设计，但不要破坏现有 seek）。

### 3.4 缓存命中 / 本地

- 完整缓存命中会话当前会创建本地 `PreviewDecoder`；慢拖应已工作。T8.6.1b 不要重复造一套，也不要改变其 32 帧/过期帧规则。
- 本地文件预览 T2.9.1 已验收；新增分支必须只在增长会话生效。
- DASH 视频 `source` 与音频 `audio_source` 分开；预览只读视频 source，不要读音频/建第二下载。

## 4. 建议实现路线

### 路线 A（推荐）：独立的在线预览读层

新增一个小型 `OnlinePreviewDecoder`（或把 `PreviewDecoder` 扩展为可接受 `GrowingSource` + `GrowingFormat`，但不要破坏本地路径 API）：

- 用独立 `GrowingSource::GrowingCursor` + `GrowingFormat::Open`（`AVFMT_FLAG_CUSTOM_IO`）打开视频 `.part`；
- 独立 `AVCodecContext`/解码线程；目标帧 → 最近关键帧 seek → 解码到目标附近 → 降采样到最长边约 1280 的 NV12（复用现有 `PreviewDecoder` 的转换/缩放代码最好）；
- 维护 `latest_target` + `generation`：拖动目标频繁变化时只保留最新目标；旧 generation 的帧 `TakeLatestFrame` 时丢弃；
- 已缓存区：按 `session_->index()` 的稳定前缀取目标/最近关键帧；目标超出稳定前缀时，用 CFR 估算 PTS 或显示等待，不宣称精确；
- 未缓存区：`GrowingCursor` 阻塞读只能发生在预览线程；UI 显示“缓冲中（预览）”，新目标/松手/关窗用 `Interrupt`/取消唤醒；
- `OnScrubStarted` 增长会话：暂停播放、启动预览线程/timer；`OnScrubTargetChanged`：只更新 latest target；`OnScrubFinished`：停预览，`GoToFrame(target)` 精确落帧；`OnPreviewTick`：只 `TakeLatestFrame` + `gfx_->SetVideoFrame`，不阻塞；
- 预览帧释放/内存有界：只保留最新 1–2 帧；`AVFrame` 所有权明确；关窗/切文件/换档时停止并释放。

### 路线 B（备选）：异步 seek 预览

如果复用精确 seek worker 做预览，必须：

- 不在 UI 线程调用 `RequestOnlineSeek`；改为一个带 latest-target 合并的预览 worker；
- 限制触发频率（例如 30–60ms 一次，不是每个 mouseMove）；
- 与 `online_seek_*`、`online_step_*` 状态隔离，避免互相取消/死锁；
- 松手仍走精确路径。

路线 B 容易与 T8.6.1a/精确 seek 的 generation 冲突；除非路线 A 成本明显过高，否则优先 A。

## 5. 测试与证据（最低要求）

新增探针建议：

```powershell
lapian.exe --online-drag-preview-test `
  --bvid BV1BMnv6jE5S --cid 42915922022 `
  --online-cache-root build\t8.6.1b-cache `
  --online-pictures build\t8.6.1b-pictures `
  --online-records-root build\t8.6.1b-records `
  --drag-seconds 1 --drag-samples 60
```

探针至少覆盖：

1. **已缓存慢拖**：1s 连续拖动，`preview_fps`（实际呈现的预览帧/秒）**≥30**；UI gap >200ms = 0；无 stale frame（generation 落后帧不呈现）；松手目标帧 frame/PTS 精确。
2. **未缓存/未索引区**：拖到未缓存目标，提示 0ms 级出现、UI 不冻结；再拖回已缓存区可恢复；取消/松手/关窗无死锁；不产生无界线程。
3. **缓存命中**：完整缓存会话拖动仍 ≥30fps、松手精确；0 网络（沿用 T8.7/OP5 口径）。
4. **本地文件**：`--drag-test` 或现有本地拖动测试不退化；T2.9.1 反向拖动/过期帧规则不变。
5. **DASH + durl**：至少各 1 条；DASH 只预览视频，音频不擦洗；`--force-durl` 路径不被破坏。
6. **边界**：首帧/末帧、极短拖动、快速反向、拖动中关窗/切档/取消。

回归：

- `ctest` **35/35**（若新增单测则记录新总数；不要用旧数字冒充）。
- `--key-test` **77/0**、`--online-longpress-test` **54/0**/样本（T8.6.1a 不回退）。
- M2 `--online-m2-test` **3/3 34/0**（若只改预览 UI 且不碰 `OnlineSession`，可至少跑 1 条 + 明确引用 T8.14；不得写“M2 重新通过”）。
- P1/P2/P7 `scripts\bench-regression.ps1 -Suites startup,step1080` 不退化（热缓存口径）。
- 日志无 SESSDATA/Cookie/签名 URL；真实用户缓存/记录/凭据不破坏。

## 6. 停止条件

- 需要改 M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema、T8.13 加固、T8.12 档位、T8.11 索引、T8.10 DASH、T8.6.1a 步进语义才能实现 → **停下问**。
- 预览必须在 UI 线程做同步 seek/阻塞读，或 UI gap 无法压到 200ms 内 → **停下改设计**，不要用“看起来能拖”交差。
- 无法在已缓存区达到 ≥30fps，或松手不能精确落帧 → 写 **blocked/fail + 数字**，不要伪造。
- T8.14 的 teardown 崩溃/seed 卡住复现 → 先停下记录，不要带病做验收。
- 真实安装/Medium 环境不具备 → 用测试根和构建目录可以开发，但“默认 AppData/安装版”结论必须写未验证，不得用工作区 Low 进程冒充。

## 7. 完成时报告

1. **T8.6.1b 判定**：pass / blocked / fail；非门任务，不写 MVP 通过。
2. **拖动预览**：已缓存 `preview_fps`/UI gap/松手 frame+PTS；未缓存提示/取消/恢复；缓存命中/本地回归。
3. **实现**：新增文件/线程/取消/generation/内存上界；是否复用 `PreviewDecoder`；对本地/缓存命中/DASH 的边界。
4. **回归**：`ctest`、key-test、longpress、M2（或引用 T8.14 的理由）、P1/P2/P7、日志脱敏。
5. **缺口**：真实 4K/60fps 不测；未缓存区不承诺帧率；T8.14 残余风险保留；T8.15 未验证。
6. **文档**：`docs/perf/v2-online-interaction.md` 追加 T8.6.1b；新建 `docs/handoff-2026-10-24-t8.6.1b.md`；`tasks-v2.md` 升 v0.27（T8.6.1b 行结果/证据）；`AGENTS.md` 升 v6.39（下一任务状态）；一个可审查 commit，`git status` clean。
7. **下一步**：T8.15（有大会员账号才做，否则继续写未验证）；T8.6.1b 完成后 v2 非门后续清空，进入发布前审计/用户手测。
