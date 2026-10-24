# 下一会话提示词：DP-3 交互策略 + 验收探针

> 直接复制下面整段作为下一个会话的 prompt。  
> 当前代码基线：`7454c32`（DP-2）；DP-1 基线 `b51007a`，规划 `bf37ccd`。

---

你是拉片软件（Windows x64，C++20 + Qt 6 + FFmpeg + D3D11VA）的后续 agent。  
本次只做 **DP-3：拖动交互策略 + 验收探针**，不要做 DP-4，也不要重做 DP-1/DP-2。

## 先读

1. `AGENTS.md`（v6.45，DP-2 已完成）
2. `docs/plan-drag-preview-v2.md`（§4.3、§5、§6 DP-3）
3. `docs/tasks-drag-preview-v2.md`（DP-3 的验收/停止/交付）
4. `docs/handoff-2026-10-24-dp2.md`（**必读**，DP-2 实现/预算/边界/残余）
5. `docs/perf/drag-preview-dp2.md`（DP-2 证据与数字）
6. 代码：`src/media/PreviewDecoder.*`、`src/media/PreviewRingCache.*`、
   `src/app/MainWindow.*`、`src/app/DragTestRunner.*`、`src/app/main.cpp`、
   `src/platform/gfx/VideoRenderer.*`、`tests/preview_cache_test.cpp`、
   `tests/preview_decoder_test.cpp`

## 基线（DP-2，提交 `7454c32`）

- 代码基线：`7454c32`；DP-1 `b51007a`；规划 `bf37ccd`。
- `ctest` **37/37**（36 项既有 + DP-2 `lapian_preview_cache_test`）、
  key **77/0**、T8.6.1b **35/0**；dist 仍是 **34.48MB/25**，
  **DP-3 不要重建 dist**（DP-4 做）。
- 1080p `media\测试.mp4`：1920×1080 H.264 30fps、4573 帧、GOP 30。
- DP-2 实测（`--drag-fast`，10ms PreciseTimer，实际约 15.6ms 一次目标）：
  - `+60` 硬件：68.7–79.5fps / 空档 26.2–48.0ms（冷启动首轮 66.1/52.6）。
  - `-60` 硬件：60.2–69.2fps / 空档 25.2–33.0ms。
  - `-20` 硬件：45.7–48.7fps / 空档 53.8–59.1ms。
  - `-1` 局部反向：84.8–86.4% 精确缓存命中，lag p50 0 / p95 1–2 帧。
  - 远跳探针：软件粗帧 19.9–40.7ms；硬件精确首帧 20–21ms。
  - 预览缓存峰值 **244,684,800B = 233.4MiB**；缩略图观测峰值 22.7–28.8MB。
  - `--drag-test`：P4 **64.2fps**、P6 **18.9ms**、P7 0.1/15.1ms、
    P8 12.4fps、P9 36.9ms，全 pass。
- 强制软件快反向残余（DP-2 已记录，DP-3 不得假装通过）：
  `-20` **9.9–11.4fps**、`-60` **30.3–33.0fps**；`-1` 局部 84–96% 命中。
- DP-2 关键边界（不要改坏）：
  - 硬件帧不缓存、不逐目标回拷；远跳硬件粗帧是“关键帧全分辨率 clone”，
    粗帧带 `coarse` 标记，精确帧随后替换；松手 `GoToFrame` 精确。
  - `PreviewRingCache` 192MiB + 32 帧热缓存（≤48MiB）+ 粗帧缓存 64MiB；
    不扩成全片缓存；`End` 后收窄。
  - 预取线程独立软件解码、低优先级；目标跨 GOP/换会话/松手取消，
    不阻塞当前目标。
  - 4K 继续软件预览（`source_too_large`），**不打开 4K 硬解**，只记录。

## DP-3 目标

1. **把“快拖/慢拖/反向/远跳”写成可测策略**，不靠感觉：
   - 慢拖/小步：优先精确目标帧，允许等待精确帧；粗帧不应干扰；
   - 快拖/大跳：优先最近可用帧/缓存/粗帧，不冻住，允许目标滞后；
   - 反向：缓存命中优先；未命中按 DP-2 的粗帧/精确路径；
   - 远跳：先粗帧、后精确；粗帧永远不是最终精确帧；
   - 松手：停止预览，`GoToFrame` 精确；P6 ≤150ms 不变。
2. **策略判定可解释**：按目标变化速度/距离/方向（例如
   `abs(step)/tick`、方向、连续同向次数、距上次目标时间）分类，
   阈值写入诊断日志/字段，不能只写“感觉流畅”。
3. **验收探针**：新增或补齐 `--drag-strategy-test`（名称可调整）：
   - 慢拖 +1/-1：`target_lag` p95 ≤2 帧；粗帧不替换慢拖精确帧；
   - 快拖 +60/-60：不冻住，fps/空档不低于 DP-2；
   - 反向 -20：默认硬件 ≥25fps（软件残余如实记录）；
   - 远跳（前/后）：`first_coarse_ms` ≤100ms，粗帧 `coarse=1`，
     精确目标随后到达（诊断能区分粗帧/精确帧/缓存/硬件）；
   - 松手 P6 ≤150ms；无黑帧/崩溃。
   - `PreviewTestDiagnostics` 至少能读：target/displayed、`coarse`、
     `from_cache`、`hw_active`、`cache_hit_rate`、`first_coarse_ms`、
     `target_lag_frames/ms`、seeks。
4. **保持 DP-2 预算**：预览缓存峰值 ≤256MB、缩略图 ≤64MB；
   不持续增长；不改 `FrameCache` 正式 64/256MiB 语义。

## 验收（DP-3）

- 慢拖目标滞后 **p95 ≤2 帧**；松手 P6 **≤150ms**；
- 快拖不冻住，空档不劣于 DP-2（`+60` 约 ≤50ms、`-60` ≤60ms）；
- 远跳首张粗帧 **≤100ms**，粗帧不得被当成最终精确帧；
- 诊断能区分精确帧/粗帧/缓存命中/硬件帧；
- `ctest` **37/37**、key **77/0**、T8.6.1b **35/0**；
- P4/P6/P7/P8/P9 不退化（P4 ≥30、P6 ≤150、P7 p95 ≤80ms 等阈值不变）；
- 4K 只记录，不写 4K 通过。

> 注意：`ctest` 现在是 **37/37**（DP-2 新增缓存单测），不是 36；
> 不要为了“回到 36”删测试。原有 36 项全过是硬要求。

## 停止/回退

- 策略导致主播放/在线预览/精确松手回归 → 回滚 DP-3，保留 DP-1/DP-2；
- 粗帧让画面明显向前跳，或慢拖被粗帧打断 → 丢弃该策略，保留精确路径；
- 预取/策略使 UI 空档、内存、表面池变差 → 缩预算/关预取；
- 4K/HEVC 仍不达标 → 只记录，不设通过、不改 1080p 门。

## 先做（不要直接堆代码）

1. 复跑 DP-2 基线：`+60`、`-60`、`-20`、`-1`、`--drag-far-test`、
   `--drag-test`，确认数字与 `hw_active=1`；先记录，不改代码。
2. 量清现有策略的实际切换点：慢拖/快拖/反向/远跳各自的 step、
   target 更新间隔、`target_lag`、粗帧是否被替换。
3. 再实现策略判定 + 探针；每一步可回退，先保证慢拖/松手，再动快拖/远跳。
4. 最后跑全量回归、4K 记录、写证据/交接。

## 复现命令

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$exe = 'build\release\bin\lapian.exe'

# 中文路径在探针 argv 下可能打不开；建 ASCII hardlink（不要提交）
New-Item -ItemType HardLink -Path build\dp1-test.mp4 -Value media\测试.mp4

& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step 60
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -60
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -20
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -1
& $exe --file build\dp1-test.mp4 --drag-far-test
& $exe --file build\dp1-test.mp4 --drag-test --drag-samples 20
& $exe --file build\dp1-test.mp4 --key-test
& $exe --file media\proxy\proxy-V4.mp4 --drag-fast --drag-fast-step 60  # 4K 只记录

# 构建（本机用 bundled cmake；TEMP/TMP 放 workspace，避免 MSBuild 写 Temp 失败）
$cmake = 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe'
$env:TEMP = (Resolve-Path build\tmp-build).Path; $env:TMP = $env:TEMP
& $cmake --build build\release --config Release

# ctest：Qt 必须在 PATH，TEMP/TMP 指到可写 ASCII 路径
$env:TEMP = (Resolve-Path build\tmp-ctest).Path; $env:TMP = $env:TEMP
ctest --test-dir build\release -C Release --output-on-failure
```

## 交付

- 代码 + 测试 + 证据 `docs/perf/drag-preview-dp3.md`；
- 交接 `docs/handoff-2026-10-24-dp3.md`；
- 更新 `AGENTS.md`（v6.46，如实写完成/partial/blocked）与
  `docs/tasks-drag-preview-v2.md` 状态；
- **一个可审查提交**，不混合 DP-4；工作区干净；**不重建 dist**；
- 4K 只记录；不写 M1/M2/P4/P7 通过。

## 不要做

- 不要重做 DP-1 共享设备硬解/4K 策略，不要打开 4K 硬解；
- 不要重做 DP-2 的缓存/预取/粗帧（可改策略层，但要保留预算与边界）；
- 不要缓存十几张硬件帧；不要无界/全片缓存；
- 不要改 P4/P6/P7 阈值、M1/M2 门、OB、`lapian-cache-1` schema、在线预览；
- 不要打印/提交 SESSDATA、Cookie、签名 URL；不写默认缓存/记录/凭据；
- 不要重建 dist（DP-4 做）、不要写 4K 通过。

---

## 建议的实现方向（可选，不强制）

- 在 `PreviewDecoder`/`PreviewController` 层引入显式 `DragMode`
  （`Exact` / `Fast` / `FarJump` / `Reverse`），用 step、方向、
  目标更新间隔和连续同向次数判定；模式进入/退出写诊断字段。
- 慢拖：关闭粗帧替换、优先精确；必要时让 worker 追到目标再返回。
- 快拖：允许粗帧/中间帧，但用 `last_delivered` 单调约束禁止向前跳。
- 远跳：`UpdateTarget` 先发粗帧；粗帧用 `PreviewFrame.coarse` 标记；
  精确帧到达后替换；松手 `End`+`GoToFrame`。
- 探针输出建议：
  `T8.17 drag_strategy mode=slow|fast|far|reverse target_lag_p95=...`
  `coarse_published=... first_coarse_ms=... exact_after_coarse_ms=...`
  `cache_hit_rate=... hw_active=... p6_ms=...`。
