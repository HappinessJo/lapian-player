# 新会话提示词：DP-2 本地拖动预览反向缓存 / 预取 / 远跳粗帧

你是拉片软件（Windows x64，C++20 + Qt 6 + FFmpeg + D3D11VA）的后续 agent。
本次只做 **DP-2：反向缓存 + 预取 + 远跳粗帧**，不要做 DP-3/DP-4，也不要重做 DP-1。

## 先读

1. `AGENTS.md`（最新 v6.44，DP-1 已完成）
2. `docs/plan-drag-preview-v2.md`（§4.2、§5、§6 DP-2）
3. `docs/tasks-drag-preview-v2.md`（DP-2 的验收/停止/交付）
4. `docs/handoff-2026-10-24-dp1.md`（**必读**，DP-1 实现/边界/残余）
5. `docs/perf/drag-preview-dp1.md`（DP-1 证据与数字）
6. `docs/handoff-2026-10-24-drag-fast.md`（上一轮热修）
7. 代码：`src/media/PreviewDecoder.*`、`src/media/VideoDecoder.*`、
   `src/app/MainWindow.*`、`src/app/DragTestRunner.*`、
   `src/platform/gfx/VideoRenderer.*`、`tests/preview_decoder_test.cpp`

## 基线（DP-1，提交 `b51007a`）

- 代码基线：`b51007a`（DP-1）；上一轮 `079f0dd`，规划 `bf37ccd`。
- `ctest` 36/36、key 77/0、T8.6.1b 35/0；dist 仍是 DP-1 之前的 34.48MB/25，
  **DP-2 不要重建 dist**（DP-4 做）。
- 1080p `media\测试.mp4`：1920×1080 H.264 30fps、4573 帧、GOP 30。
- DP-1 实测（`--drag-fast`，10ms PreciseTimer，实际约 15.6ms 一次目标）：
  - `+60`：`hw_active=1`，fps 61.0–76.3，`max_gap_ms` 36–46；
    `target_lag_p50≈68 帧/2.27s`，p95 120 帧/4s。
  - `-60`：`hw_active=1`，fps 56.8–60.7，`max_gap_ms` 32–38；
    `target_lag_p50=60–120 帧/2–4s`；`cache_hits=0`、`seeks≈77/2s`。
  - `+3` 慢拖：60.6fps，`target_lag_p50=0`。
  - `--preview-force-software`：`hw_active=0`/`forced_software`，fps 仍 ~74，
    但 `target_lag_p50=4161 帧/138.7s`（证明 fps ≠ 跟手）。
  - P4 63.4–64.1fps、P6 16.6–28.6ms、P7/P8/P9 不退化。
- DP-1 关键实现/边界（不要改坏）：
  - `PreviewDecoder` 借用 `GfxWidget` 的共享 D3D11 设备；≤1080p 硬解，
    4K `source_too_large` 回退软件（**不要打开 4K 硬解**）。
  - 硬件帧不 `sws_scale`、**不缓存**：D3D11VA 表面池有限（spike 持有 16 张
    就会卡住；DP-1 只给 8 张额外表面）。
  - 硬件切片“固定进入时的目标”，到目标就交回 worker；不要让一次 Process
    无限追最新目标。
  - `End()` 在硬件路径有界等待 worker 空闲（≤100ms），避免松手精确 seek
    与预览硬解共用同一 D3D11 视频上下文。
  - 预览线程仍走 `D3D11ContextMutex`；不要为了缓存把 UI 阻塞拉长。

## DP-2 目标

1. **反向先命中缓存**：已解过的帧不再每次 seek；反向 -60 提升到
   **≥45fps / 空档 ≤60ms**，反向 -20 从 11.5fps 提升到 **≥25fps**。
2. **有界缓存**：播放头附近双向 `PreviewRingCache`，总预算 **≤256MB**；
   缩略图/LRU **≤64MB**；不做全片缓存，超限淘汰，不增长。
3. **预取**：空闲/拖动时预解当前 GOP、上一 GOP 的若干帧；可取消、低优先级，
   不能阻塞当前目标。
4. **远跳粗帧**：关键帧缩略图（建议 320×180，滑动窗口 + LRU），远跳先出粗帧，
   `first_coarse_ms ≤100ms`；粗帧不得被当成最终精确帧。
5. 松手仍 `GoToFrame` 精确，P6 ≤150ms 不变。

## 设计约束（硬）

- 不扩大现有 32 帧软件预览缓存；DP-2 的预算独立、统一记账。
- **硬件帧缓存要非常小心**：D3D11VA 表面池有限。优先缓存降采样后的
  软件 NV12；若要缓存硬件帧，只能保留极少量并计入显存/表面池预算，
  绝不能持有 16 张以上。必要时先回拷成软件帧再入缓存（记录回拷成本）。
- 不改 `FrameCache` 正式 64/256MiB 语义；不改主播放解码/时钟/在线路径。
- 不改 `OnlinePreviewDecoder`、T8.6.1b、M1/M2/P4/P6/P7 门/数字、OB、
  `lapian-cache-1` schema、默认 AVC≤1080p。
- 不打印/提交 SESSDATA、Cookie、签名 URL；不写默认缓存/记录/凭据。
- 4K/HEVC 只记录，不设通过线；4K 继续软件预览。
- 任何主播放/在线/精确松手回归 → 回滚 DP-2 修改，保留 DP-1。

## 先做（不要直接堆代码）

1. 复跑 DP-1 基线：`+60`、`-60`、`-20`、`--drag-test`，确认数字与
   `hw_active=1`；先记录，不改代码。
2. 测清反向成本：`-20`/`-60` 每次 seek、解到目标、Present 的分段耗时；
   区分“没缓存”“缓存太小”“缓存命中但发布慢”。
3. 给 `--drag-fast` 增加/补齐 DP-2 诊断：
   - `cache_hits` / `cache_misses` / `cache_hit_rate`；
   - `cache_bytes` / `cache_peak_bytes` / `thumbnail_bytes`；
   - `first_coarse_ms`（远跳首张粗帧）；
   - 可区分精确帧/粗帧/硬件帧/缓存命中（至少日志/诊断字段）。
4. 先实现**软件 NV12 有界环缓存 + 反向命中**，验证 -20/-60；再加预取和
   关键帧缩略图。每步都可回退。
5. 用 `--preview-force-software` 验证软件路径同样受益/不崩；用
   1080p 硬件路径验证 `hw_active=1` 不回退。

## 验收（DP-2）

- 反向 -60：**≥45fps / 空档 ≤60ms**；反向 -20：**≥25fps**（同
  `media\测试.mp4`，`--drag-fast --drag-fast-step -20` 或新增 -20 场景）。
- 反向局部拖动 `cache_hit_rate ≥70%`（口径写进证据，明确分母）。
- 远跳首张粗帧 **≤100ms**；松手 P6 **≤150ms**。
- 预览缓存峰值 **≤256MB**，缩略图 **≤64MB**；不出现持续增长。
- `+60` 不低于 DP-1（fps 61–76 / 空档 ≤50ms），P4/P6/P7/P8/P9 不退化。
- `ctest` 36/36、key 77/0、T8.6.1b 35/0；无黑帧、无崩溃、无设备丢失。
- 4K 只记录；不写 4K 通过。

## 停止/回退

- 缓存导致内存/显存超限、主播放抖动、UI 空档变差：缩预算/关预取，保留 DP-1。
- 粗帧造成明显向前跳或把粗帧当最终帧：丢弃粗帧，保留精确路径。
- 硬件帧缓存触发表面池耗尽/卡住：不要强行缓存硬件帧，改软件缓存或缩小窗口。
- 任何 M1/M2/P4/P6/P7、在线预览、精确松手回归：回滚 DP-2。

## 交付

- 代码 + 测试 + 证据 `docs/perf/drag-preview-dp2.md`；
- 交接 `docs/handoff-2026-10-24-dp2.md`；
- 更新 `AGENTS.md`（v6.45，DP-2 完成/partial/blocked 如实写）、
  `docs/tasks-drag-preview-v2.md` 状态；
- **一个可审查提交**，不混合 DP-3/DP-4；工作区干净；不重建 dist。

## 复现命令

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$exe = 'build\release\bin\lapian.exe'

# 中文路径在探针 argv 下可能打不开；建 ASCII hardlink（不要提交）
New-Item -ItemType HardLink -Path build\dp1-test.mp4 -Value media\测试.mp4

& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step 60
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -60
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -20
& $exe --file build\dp1-test.mp4 --drag-test --drag-samples 20
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step 60 --preview-force-software
& $exe --file media\proxy\proxy-V4.mp4 --drag-fast --drag-fast-step 60   # 4K 只记录

# ctest：本机需把 TEMP/TMP 指到可写 ASCII 路径
$env:TEMP = (Resolve-Path build).Path + '\tmp-ctest'; $env:TMP = $env:TEMP
ctest --test-dir build\release -C Release --output-on-failure
```

## 不要做

- 不要重做 DP-1 的共享设备硬解/4K 策略；不要打开 4K 硬解。
- 不要缓存十几张硬件帧；不要把缓存做成无界/全片。
- 不要改 P4/P6/P7 阈值、M1/M2 门、OB、schema、在线预览。
- 不要重建 dist（DP-4 做）、不要写 4K 通过。
