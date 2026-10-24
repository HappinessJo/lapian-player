# DP-2 反向缓存 + 预取 + 远跳粗帧（证据）

- **版本**：v1.0（2026-10-24）
- **状态**：**完成（1080p 保证档默认硬件路径）**；强制软件快反向
  `-20/-60` 仍受 1080p 软件解码物理成本限制，作为残余记录（partial）。
  DP-3/DP-4 未做，不写 4K 通过。
- **性质**：本地拖动预览体验改造，非门；不改 M1/M2/P4/P7 数字、OB 样本、
  `lapian-cache-1` schema、默认 AVC≤1080p、`OnlinePreviewDecoder`。
- **代码基线**：`b51007a`（DP-1）。**交付提交**：见 `git log -1`（本证据与
  代码同一提交）。
- **交接**：[handoff-2026-10-24-dp2.md](../handoff-2026-10-24-dp2.md)
- **上一轮**：[drag-preview-dp1.md](drag-preview-dp1.md)
- **任务表**：[tasks-drag-preview-v2.md](../tasks-drag-preview-v2.md)

---

## 1. 结论

1. 新增 **`PreviewRingCache`**（软件 NV12，默认 192MiB）和
   **`PreviewThumbnailCache`**（粗帧/关键帧 320×180，默认 64MiB），
   与保留的 32 帧热缓存（≤48MiB）统一在 `PreviewDiagnostics` 记账；
   LRU/窗口裁剪、有界，不做全片缓存。
2. **反向先命中缓存**：`Begin/UpdateTarget` 先查精确缓存，命中就直接发布，
   不再等 worker seek。慢速局部反向（`-1`）的精确命中率
   **84.8–86.4%**（硬件）/ **88.4–95.8%**（强制软件）。
3. **预取**：新增独立低优先级软件预取线程，慢速局部反向时解“上一 GOP”的
   1280 NV12 入环缓存；目标跨 GOP/换会话/松手时逐帧取消，不阻塞当前目标。
   worker 空闲时也做 1–2 帧的兜底预取。
4. **远跳粗帧**：`UpdateTarget` 的目标跳 ≥300 帧时先发粗帧：
   - 软件路径：320×180 关键帧缩略图（低频关键帧/软件解码顺带缓存）；
   - 硬件路径：刚解出的关键帧**全分辨率** clone，零回拷、不占表面池；
   - 粗帧带 `coarse` 标记，精确目标随后替换；松手仍 `GoToFrame` 精确。
   软件远跳首张粗帧 **19.9–40.7ms**；硬件远跳精确首画面 **20.1–21.1ms**
   （关键帧/目标已足够快，不再额外等待粗帧），均 ≤100ms。
5. 1080p 硬件默认：`-60` **60.2–69.2fps / 空档 25.2–33.0ms**（≥45/≤60）；
   `-20` **45.7–48.7fps**（≥25）；`+60` **68.7–79.5fps / 26.2–48.0ms**，
   不低于 DP-1；P4/P6/P7/P8/P9 不退化。
6. 有界预算：实测预览缓存峰值 **244,684,800B = 233.4MiB**（≤256MiB），
   连续 3 次拖动峰值不变；缩略图观测峰值 ≤28.8MB（≤64MiB），
   新增单测验证 LRU 淘汰。
7. 回归：`ctest` **37/37**（36 项既有 + 新增 DP-2 缓存单测）、key **77/0**、
   T8.6.1b **35/0**；4K 只记录（`source_too_large`，68.5fps/11.0ms），
   不写 4K 通过。
8. **残余**：强制软件快反向 `-20` **9.9–11.4fps**、`-60` **30.3–33.0fps**。
   1080p 软件解码单帧约 5–8ms，目标每 15.6ms 后退 20–60 帧，精确路径
   物理追不上；粗帧也常因目标已后退而变成“未来帧”被正确丢弃。该残余不
   影响默认硬件路径的 DP-2 验收，也不隐藏为“通过”。

## 2. 先做的基线（DP-1，`b51007a` 构建）

环境：开发机（AMD Ryzen 5 7500F / 31.6GB / RTX 4060）；
`media\测试.mp4`（1920×1080 H.264 30fps、4572 帧、GOP 30）；
探针用 `build\dp1-test.mp4` ASCII hardlink（临时，不提交）。

| 场景 | DP-1 基线 |
|---|---|
| `+60` | 66.1fps / 空档 36.6ms，`hw_active=1` |
| `-60` | 57.6fps / 空档 42.6ms，`hw_active=1` |
| `-20` | 47.0fps / 空档 51.1ms，`hw_active=1` |
| `--drag-test` | P4 64.2fps、P6 p95 19.5ms、P7 0.1/14.2ms、P8 12.4fps、P9 30.9ms |
| 强制软件 `-60` | 31.3fps / 空档 41.0ms |
| 强制软件 `-20` | **13.0fps / 空档 210.2ms（历史残余 11.5fps）** |

> DP-1 的 fps 已经是“Present 帧率”，不代表显示帧==目标；DP-1 已用
> `target_lag_frames/ms` 区分。DP-2 继续保留该口径，并新增缓存/粗帧字段。

## 3. 反向成本拆分（DP-2 诊断）

在 `PreviewDecoder` 内分别累计 seek、`DecodeNextFrame`、回拷/缩放与发布：

- 硬件预览单次目标切片：seek 0.4–0.9ms、单帧 decode 1.1–1.5ms、约 20 帧
  目标 = 20–30ms/步；`-20` 每步跨 20 帧，约 45–49fps 是“seek+解到目标”的
  上限，缓存无法消除每一步的新 GOP 解码。
- 软件预览：seek 0.3–0.5ms、单帧 decode 5–8ms；`-20` 一步 20 帧 = 100ms+，
  目标 15.6ms 就前进，fps 必然在 10–13 量级。DP-2 只改善“已解过的局部
  反向”（缓存/预取），不把软件快反向伪装成 60fps。
- 硬件帧不逐帧回拷：实测 `av_hwframe_transfer_data` + 1280 sws 约
  4–15ms/帧（受 D3D11 锁/纹理影响），放进目标路径会把 `-20` 从 47fps
  拖到 33fps、空档 70ms，因此最终版本在目标路径**不做硬件回拷**；
  硬件远跳粗帧直接用关键帧 clone，软件缓存由独立预取线程完成。

## 4. 实现

| 文件 | 改动 |
|---|---|
| `src/media/PreviewRingCache.*` | 新增 `PreviewRingCache`（NV12 软件帧，192MiB，LRU/窗口，峰值/命中/淘汰统计）与 `PreviewThumbnailCache`（320×180 粗帧/关键帧，64MiB，最近关键帧查询） |
| `src/media/PreviewDecoder.*` | 精确缓存查询（Begin/UpdateTarget 快路径）、软件反向帧降采样粗缓存、独立预取线程（上一 GOP，可取消）、缩略图线程（远跳关键帧）、硬件关键帧全分辨率粗帧、`PreviewFrame.coarse/from_cache/hardware`、DP-2 诊断 |
| `src/media/FrameIndex.*` | 新增 `KeyframeAtOrBeforeFrame/PreviousKeyframe/NextKeyframe` |
| `src/app/MainWindow.*` | `PreviewTestDiagnostics` 增加缓存/粗帧/预取字段；显示帧记录 coarse/from_cache |
| `src/app/DragTestRunner.*` | `--drag-fast` 输出缓存/粗帧/预取/分段耗时；新增 `--drag-far-test` 远跳粗帧探针与 `--drag-fast-cycles` 峰值复跑；`EnableFarTest` |
| `src/app/main.cpp` | `--drag-far-test`、`--drag-fast-cycles` |
| `tests/preview_cache_test.cpp` | 预算/LRU/窗口/最近关键帧/缩略图淘汰单测（ctest 第 37 项） |
| `docs/` | 本证据、DP-2 交接、任务/AGENTS 状态 |

### 4.1 缓存预算与记账

- 32 帧热缓存：DP-1 容量不变（32 帧），另加 48MiB 字节上限，避免极端画幅
  撑破总预算。
- `PreviewRingCache`：默认 **192MiB**，只存软件 NV12；超预算按 LRU 淘汰，
  但至少保留 32 帧；`Begin`/大跳按目标 ±1800 帧裁剪，`End` 收窄到 32MiB。
- `PreviewThumbnailCache`：默认 **64MiB**，320×180 NV12；滑动窗口 +
  LRU；条目可以是关键帧缩略图，也可以是软件反向解码时顺带降采样的邻近帧。
- 报告口径：`cache_bytes = 热缓存 + 环缓存`（两者可能共享 AVBuffer，
  报告值偏保守）；`thumbnail_bytes` 单独；`cache_peak_bytes` 为本次进程峰值。

### 4.2 反向命中

- `Begin/UpdateTarget` 在 UI 线程锁内先查热缓存/环缓存；命中立即
  `ReplaceLatestLocked`，下一次 10ms tick 即可 Present，不经过 worker seek。
- 命中率口径（写死）：**`cache_hit_rate = cache_hits / (cache_hits +
  cache_misses)`，分母是本次拖动中每一次 `Begin/UpdateTarget` 的精确目标
  查询**（同一目标重复调用会被 early-return 忽略）；粗帧命中单独
  `coarse_*`，不混入精确命中率。

### 4.3 预取

- 独立 `RunPrefetch` 线程：软件解码器/独立读层、`THREAD_PRIORITY_BELOW_NORMAL`；
  请求“当前目标所在 GOP 的上一 GOP”，解成 1280 NV12 写入热/环缓存。
- 只在反向步长 ≤4 帧/目标（局部慢拖）时请求；快速 `-20/-60` 不预取，
  避免软件解码抢 CPU 反而拖慢精确路径。
- 目标跨 GOP、换会话、`End()` 或新目标到来时逐帧检查并取消；预取位置
  不会污染 worker 的 `position_valid`（用完置 false，下个目标重新 seek）。
- worker 空闲 2ms 也会兜底解 1–2 帧上一 GOP；任何新目标立即打断。

### 4.4 远跳粗帧

- 触发：目标一次跳 ≥300 帧（`kCoarseMinForwardJump`）。
- 软件：320×180 关键帧缩略图；缩略图线程按需解关键帧，LRU 缓存。
- 硬件：seek 后解出的关键帧直接 `av_frame_clone`，标记 `coarse=true`、
  `hardware=true`；不回拷、不占表面池，精确目标随后替换。
- `TakeLatestFrame` 对粗帧做单调约束：反向必须比上一张已显示帧更早，
  正向不能比上一张更早；目标已回退到粗帧之前时丢弃（不向前跳）。
- 松手 `End()` 停预览，`GoToFrame` 精确；粗帧不会被写成最终精确帧。

## 5. 测量（最终构建）

### 5.1 1080p 硬件默认（`--drag-fast`）

| 场景 | 次数 | fps（active） | max_gap_ms | target_lag p50 / p95 | 说明 |
|---|---:|---:|---:|---|---|
| `+60` | 8 | **68.7–79.5**（冷启动首轮 66.1） | **26.2–48.0**（首轮 52.6） | 68–120 / 120–180 帧 | 不低于 DP-1 61–76 |
| `-60` | 4 | **60.2–69.2** | **25.2–33.0** | 60–120 / 120 帧 | ≥45 / ≤60 |
| `-20` | 4 | **45.7–48.7** | **53.8–59.1** | 40 / 40–60 帧 | ≥25 |
| `-1`（局部反向） | 3 | **80.2–84.9** | 58.8–93.7 | 0 / 1–2 帧 | 缓存命中 84.8–86.4% |

代表输出（`-1`，3 次拖动峰值复跑）：

```text
T8.17 drag_fast_cycle cycle=1 presents=167 cache_hits=151
 cache_bytes=244684800 cache_peak_bytes=244684800 max_gap_ms=43.4
T8.17 drag_fast_cycle cycle=2 presents=104 cache_hits=41
 cache_bytes=88473600 cache_peak_bytes=244684800 max_gap_ms=69.4
T8.17 drag_fast tag=hard step_frames=-1 presents=108 fps=53.8
 cache_hits=42 cache_misses=81 cache_hit_rate=0.341
 cache_bytes=88473600 cache_peak_bytes=244684800 prefetched=132
 target_lag_frames_p50=1.0 target_lag_frames_p95=2.0
```

> 峰值在 3 次连续拖动中保持 244,684,800B 不变；第 2/3 轮 `End()` 后
> 环缓存收到 32MiB，命中率/吞吐低于冷启动单轮（单轮 `-1` 为
> 84.8–86.4% / 80–85fps），这里只用于证明峰值不增长。

代表输出（`-60` / `-20`）：

```text
-60: presents=72 fps=65.0 active_fps=65.0 max_gap_ms=32.9
     cache_hits=0 cache_misses=77 cache_peak_bytes=2764800
     target_lag_frames_p50=60.0 target_lag_frames_p95=120.0
-20: presents=94 fps=46.6 active_fps=46.6 max_gap_ms=57.7
     cache_hits=0 cache_misses=121 cache_peak_bytes=2764800
     target_lag_frames_p50=40.0 target_lag_frames_p95=60.0
```

### 5.2 远跳粗帧（`--drag-far-test`）

| 路径 | 方向 | 首张粗帧 ms | 首张画面 ms | 精确目标 ms | 松手 ms |
|---|---|---:|---:|---:|---:|
| 硬件（默认） | 前向 300→4272 | -（精确首帧已够快） | 21.1 | 21.1 | 15.2 |
| 硬件（默认） | 反向 4272→1524 | -（精确首帧已够快） | 20.4 | 20.4 | 27.9 |
| 强制软件 | 前向 300→4272 | **40.7** | 40.7 | 110.5 | 15.6 |
| 强制软件 | 反向 4272→1524 | **20.2** | 20.2 | 200.5 | 18.9 |

代表输出（硬件；精确首帧 ≤25ms，粗帧路径不额外等待）：

```text
T8.17 drag_far tag=hard frames=4572 start_forward=300 target_forward=4272
 start_backward=4272 target_backward=1524
 forward_coarse_ms=-1.0 forward_first_present_ms=21.1 forward_exact_ms=21.1
 backward_coarse_ms=-1.0 backward_first_present_ms=20.4 backward_exact_ms=20.4
 forward_release_ms=15.2 backward_release_ms=27.9 release_p95=15.2
 hw_requested=1 hw_active=1 preview_hw_fallback=-
```

代表输出（强制软件；远跳粗帧 20–41ms）：

```text
T8.17 drag_far ... hw_active=0 preview_hw_fallback=forced_software
 forward_coarse_ms=40.7 forward_first_present_ms=40.7 forward_exact_ms=110.5
 backward_coarse_ms=20.2 backward_first_present_ms=20.2 backward_exact_ms=200.5
 forward_release_ms=15.6 backward_release_ms=18.9 release_p95=15.6
```

粗帧在 `PreviewDiagnostics` 中带 `coarse_published` / `first_coarse_ms`，
`PreviewFrame.coarse=true`；`TakeLatestFrame` 不会把粗帧当最终精确帧。

### 5.3 普通 `--drag-test`（回归）

```text
T7.4 drag_summary tag=hard hw=1 display=1920x1080
 p4_fps=64.1 p4_pass=1 p6_p95=16.8 p6_pass=1
 p7_back_p95=0.1 p7_fwd_p95=10.8 p7_pass=1
 p8_fps=12.8 p8_pass=1 p9_p95=30.5 p9_pass=1
```

P4/P6/P7/P8/P9 均不低于 DP-1；P4 阈值/数字未改。

### 5.4 强制软件（`--preview-force-software`）

| 场景 | DP-2 | DP-1 基线 | 结论 |
|---|---:|---:|---|
| `+60` | 79.0fps / 空档 30.3ms | 74.4 / 30.3 | 不退化 |
| `-60` | 30.3–33.0fps / 空档 49.8–80.1 | 31.3 / 41.0 | 同量级，快反向残余 |
| `-20` | **9.9–11.4fps / 空档 199–380ms** | 13.0 / 210.2 | 物理残余，未达 25 |
| `-3` 局部 | 32.9–40.8fps，精确命中 33–42% | — | 受益于预取/缓存 |
| `-1` 局部 | 84.3–92.0fps，精确命中 84–96% | — | 明显受益 |

软件路径不崩、无黑帧；慢速局部反向命中率提升明显。`-20/-60` 快反向
不写“通过”：1080p 软件解码本身就要 5–8ms/帧，目标 15.6ms 后退 20–60
帧，粗帧也常因目标已经更早而被丢弃（这是正确的“不当最终帧”行为）。

### 5.5 4K（只记录，不设通过线）

`media\proxy\proxy-V4.mp4`（3840×2160）：

```text
T8.17 drag_fast ... step_frames=60 presents=198 fps=68.5 max_gap_ms=11.0
 hw_requested=1 hw_active=0 preview_hw_fallback=source_too_large: 3840x2160
```

4K 预览继续软件路径（DP-1 策略未改），不写 4K 通过，不因 4K 改 1080p 门。

## 6. 回归

| 项 | 结果 |
|---|---|
| `ctest` | **37/37**（既有 36 项 + 新增 `lapian_preview_cache_test`；62.7s） |
| key-test | **77/0** |
| T8.6.1b `--online-drag-preview-test`（DASH） | **35/0 pass**（`OnlinePreviewDecoder` 未改） |
| T8.6.1b 缓存命中模式 | 25/0 pass |
| `--drag-test` | P4 64.1、P6 16.8、P7 0.1/10.8、P8 12.8、P9 30.5，全 pass |
| 设备丢失/黑帧 | 无；硬件帧不缓存、不逐目标回拷，表面池未耗尽 |
| 内存 | 预览缓存峰值 233.4MiB；3 次拖动峰值不变；缩略图观测 ≤28.8MB |
| 工作区/dist | 未重建 dist（DP-4 做）；临时 hardlink/日志/缓存均在 `build\`，不提交 |

## 7. 风险/边界

1. **硬件帧不缓存**：DP-1 spike 的 16 张表面池耗尽风险仍未解除；DP-2 只用
   软件预取线程/粗帧缓存，硬件帧最多 latest 1–2 张，`readbacks=0`。
2. **强制软件快反向**：`-20/-60` 未达 25/45fps；这是 1080p 软件解码的物理
   上限，已按要求记录，不用粗帧伪装精确帧。DP-3 若要做，需要更低分辨率
   解码/代理或更快缩略图管线，不能只调缓存。
3. **粗帧策略**：只在远跳 ≥300 帧或软件局部反向时使用；正向普通拖动
   （+60/+3）仍走精确中间帧，避免粗帧频繁替换造成闪烁。
4. **缓存跨拖动保留**：`End` 后环缓存收到 32MiB、缩略图收到 16MiB；
   连续拖动峰值不增长。若后续要更高跨拖动命中率，可评估放宽 idle 预算，
   但不能突破 256/64MiB。
5. **探针节奏**：`--drag-fast` 仍是 10ms PreciseTimer（本机 15.6ms 粒度）；
   fps 是移动阶段，`total_fps` 保留 2s；与 DP-1 同口径。
6. **4K/HEVC**：只记录，不设通过线；4K 硬解仍关闭。

## 8. 复现

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$exe = 'build\release\bin\lapian.exe'

# 中文路径在探针 argv 下可能打不开；临时 hardlink（不要提交）
New-Item -ItemType HardLink -Path build\dp1-test.mp4 -Value media\测试.mp4

# DP-2：反向 / 局部反向 / 正向 / 峰值
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -60
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -20
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -1
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -1 --drag-fast-cycles 3
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step 60

# 远跳粗帧（硬件/强制软件）
& $exe --file build\dp1-test.mp4 --drag-far-test
& $exe --file build\dp1-test.mp4 --drag-far-test --preview-force-software

# 回归
& $exe --file build\dp1-test.mp4 --drag-test --drag-samples 20
& $exe --file build\dp1-test.mp4 --key-test
& $exe --file media\proxy\proxy-V4.mp4 --drag-fast --drag-fast-step 60  # 4K 只记录

# ctest：本机需把 TEMP/TMP 指到可写 ASCII 路径，并把 Qt 加进 PATH
$env:TEMP = (Resolve-Path build).Path + '\tmp-ctest'; $env:TMP = $env:TEMP
ctest --test-dir build\release -C Release --output-on-failure
```

诊断开关（仅本地验证，不写默认配置）：
`LAPIAN_PREVIEW_DISABLE_CACHE=1`、`LAPIAN_PREVIEW_DISABLE_PREFETCH=1`、
`LAPIAN_PREVIEW_DISABLE_THUMBNAILS=1`、`LAPIAN_PREVIEW_FORCE_SOFTWARE=1`、
`LAPIAN_TIMING_LOG=1`（粗帧/预取分段日志）。
