# DP-3 拖动交互策略 + 验收探针（证据）

- **版本**：v1.0（2026-10-24）
- **状态**：**完成（1080p 保证档默认硬件路径）**；强制软件快反向
  `-20/-60` 仍为 DP-2 残余，DP-3 未改善也未伪装通过；4K 只记录，不设通过线。
- **性质**：本地拖动预览体验改造，非门；不改 M1/M2/P4/P6/P7、OB 样本、
  `lapian-cache-1` schema、默认 AVC≤1080p、`OnlinePreviewDecoder`/T8.6.1b。
- **代码基线**：`7454c32`（DP-2）。**交付提交**：见 `git log -1`（本证据与
  代码同一提交）。
- **交接**：[handoff-2026-10-24-dp3.md](../handoff-2026-10-24-dp3.md)
- **上一轮**：[drag-preview-dp2.md](drag-preview-dp2.md)
- **任务表**：[tasks-drag-preview-v2.md](../tasks-drag-preview-v2.md)

---

## 1. 结论

1. 把拖动从“一条路径”改成显式四模式策略：**Exact / Fast / Reverse /
   FarJump**。判定只看可测量输入（`abs(step)`、方向、距上次目标时间、连续
   同向次数），阈值写死并进入诊断：`kDragExactMaxStep=4`、
   `kDragFarJumpStep=300`。代码在 `src/media/DragStrategy.*`，纯函数，
   `lapian_preview_cache_test` 覆盖边界。
2. **慢拖/小步（Exact）**：关闭粗帧替换（Begin/UpdateTarget/缩略图线程
   三处都跳过），并丢弃切换模式时尚未呈现的粗帧。`+1/-1` 在 33ms 慢拖
   节奏下 `target_lag p95=0` 帧，在 15.6ms 快节奏下 `+1` p95=1 帧、
   `-1` p95=2 帧；慢拖场景 `coarse_presents=0`、`coarse_after_exact=0`。
3. **快拖/大跳（Fast/Reverse）**：保持 DP-2 的中间帧/缓存路径，允许目标
   滞后；`+60` active 76.9–80.8fps / 空档 34.7–37.2ms，`-60` 54.4–64.4fps /
   空档 23.8–39.3ms，`-20` 47.3–49.2fps，均不冻住；诊断 `mode=fast|reverse`。
4. **反向缓存优先**：`-1` 精确命中 84.7–85.5%，`-20` 硬件 47–49fps；
   `-20` 不引入粗帧（DP-2 已证明粗帧追不上 15.6ms/20 帧的快反向）。
5. **远跳先粗后精**：FarJump 模式加“粗帧未呈现时暂存精确帧”的闸门，
   粗帧呈现后下一 tick 再替换。`--drag-far-test` 硬件前/后首张粗帧
   **20.8/20.6ms**，精确目标 **30.6/30.0ms**，`coarse_after_exact=0`；
   软件粗帧 **40.5/20.1ms**、精确 **110.4/219.9ms**。粗帧始终带
   `coarse=1`，精确帧随后替换，松手仍 `GoToFrame` 精确。
6. **诊断可区分来源**：`PreviewTestDiagnostics` 新增 `drag_mode` /
   `drag_mode_reason` / `step` / `interval_ms` / `speed_frames_per_s` /
   `same_direction_run` / `target_lag_frames|ms` / `cache_hit_rate` /
   `displayed_hardware`；配合既有 `displayed_coarse`、`displayed_from_cache`、
   `hw_active`、`first_coarse_ms`、`seeks`，可区分精确帧/粗帧/缓存帧/
   硬件帧/回退。
7. **验收探针**：新增 `--drag-strategy-test`，一次跑完
   慢拖前/后、快拖 +60/-60、反向 -20、远跳前/后、4 个 P6 松手样本；
   两个场景 line + 一个 summary，全部 `pass=1`。
8. **有界**：预览缓存峰值 **244,684,800B = 233.4MiB**（≤256MiB），3 次连续
   拖动峰值不变；缩略图本轮矩阵观测 ≤432KB，DP-2 观测 ≤28.8MB（预算
   64MiB 未变）；`readbacks=0`，硬件帧仍不缓存、不逐目标回拷。
9. **回归**：`ctest` **37/37**、key **77/0**、T8.6.1b **35/0**；
   `--drag-test` P4 94.9、P6 p95 18.8ms、P7 0.1/11.8ms、P8 12.8fps、
   P9 32.2ms，全 pass；4K `hw_active=0/source_too_large` 仅记录。
10. **残余**：强制软件 `-20` **9.5fps/空档 211ms**、`-60` **31.4fps/
    空档 50.6ms**，与 DP-2 同量级，未达 25/45fps；这是 1080p 软件解码
    物理成本，不写成通过。

## 2. 环境与复现

- 开发机：AMD Ryzen 5 7500F / 31.6GB / RTX 4060（同 DP-1/DP-2）。
- 素材：`media\测试.mp4`（1920×1080 H.264 30fps、4572 帧、GOP 30）；
  探针用 `build\dp1-test.mp4` ASCII hardlink（临时，不提交）。
- 本轮机器同时有其他桌面程序（AfterFX 等）运行，fps 有 5–10 的波动；
  同会话先复跑 DP-2 二进制基线，再对比 DP-3，避免拿历史轻载数字硬比。
- 计时器说明：`--drag-fast` 仍是 10ms `Qt::PreciseTimer`；本轮系统计时器
  粒度约 10ms（DP-2 记录约 15.6ms），所以同一二进制 P4 从约 64fps 升到
  约 95fps。**P4 阈值/数字未改**，这是测量环境变化，不当作产品提升。

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$exe = 'build\release\bin\lapian.exe'
New-Item -ItemType HardLink -Path build\dp1-test.mp4 -Value media\测试.mp4 -ErrorAction SilentlyContinue

# DP-3 策略矩阵（新增）
& $exe --file build\dp1-test.mp4 --drag-strategy-test

# 快拖/反向/局部反向
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step 60
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -60
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -20
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -1
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -1 --drag-fast-cycles 3

# 远跳（默认硬件/强制软件）
& $exe --file build\dp1-test.mp4 --drag-far-test
& $exe --file build\dp1-test.mp4 --drag-far-test --preview-force-software

# 回归
& $exe --file build\dp1-test.mp4 --drag-test --drag-samples 20
& $exe --file build\dp1-test.mp4 --key-test
& $exe --file media\proxy\proxy-V4.mp4 --drag-fast --drag-fast-step 60  # 4K 只记录
ctest --test-dir build\release -C Release --output-on-failure
```

> 中文路径在探针 argv 下可能打不开；hardlink 只用于测量，不提交。

## 3. 策略定义（写死，可单测）

| 模式 | 判定 | 行为 |
|---|---|---|
| `Exact` | `step ≤ 4`（含 +1/-1，与方向无关） | 优先精确目标帧；不允许粗帧发布/替换；可等待精确帧 |
| `Fast` | `step 5..299` 且向前 | 允许中间帧/缓存；不阻塞 UI；允许目标滞后 |
| `Reverse` | `step 5..299` 且向后 | 缓存命中优先；未命中走 DP-2 精确/远跳粗帧路径；不新增粗帧 |
| `FarJump` | `step ≥ 300`（前/后） | 先粗帧、后精确；粗帧未呈现时精确帧暂存，下一 tick 替换 |

- 判定还记录 `since_previous_ms`、`same_direction_run`、`speed_frames_per_s`
  与静态原因字符串（`small_step_le_4` / `forward_sustained` /
  `reverse_sustained` / `far_jump_step_ge_300` 等），全部进诊断/探针。
- `Exact` 的“不允许粗帧”在三个入口同时生效：`Begin`、`UpdateTarget`、
  缩略图线程 `MaybePublishThumbnailLocked`；从快拖切回 `Exact` 时还会丢弃
  尚未呈现的粗帧。
- **通用规则**：精确目标已经呈现后，任何粗帧都不得再替换（尤其远跳反向
  落定后缩略图线程补发的更早关键帧）。这条不依赖模式，是 DP-3 新加的
  安全约束。
- `FarJump` 的“先粗后精”不是靠 sleep：粗帧发布后 UI 未取走时，精确帧进
  `deferred_exact`；`TakeLatestFrame` 取走粗帧后提升精确帧，下一 tick
  交给 UI。松手 `End` 会丢弃暂存帧，仍走 `GoToFrame` 精确。

### 3.1 改动前实测的“实际切换点”

在写策略前先用 DP-2 二进制量清现有行为（`--drag-fast`，目标更新实际
约 15.6ms 一次；`--drag-far-test` 单次跳）：

| 场景 | 每次 step | 实际间隔 | Present 时 lag p50/p95 | 粗帧是否出现 |
|---|---:|---:|---|---|
| `+60` | 60 帧 | 15.6ms | 70 / 123–130 帧 | 否（<300） |
| `-60` | 60 帧 | 15.6ms | 120 / 120 帧 | 否（<300） |
| `-20` | 20 帧 | 15.6ms | 40 / 60 帧 | 否（<300） |
| `-1` | 1 帧 | 15.6ms | 0 / 2 帧 | 否（命中 85%） |
| 慢拖 `+1`（33ms） | 1 帧 | 33ms | 0 / 0 帧 | 否 |
| 远跳 300→4272 | 3972 帧 | 一次 | 目标前粗帧 | 是（关键帧） |

结论：DP-2 的“切换”只由 `step>=300` 的粗帧阈值隐式决定；`-20/-60`
没有策略差异，`-1` 靠缓存命中，慢拖没有显式“禁粗帧”保证。这就是 DP-3
要补的空白。另测 `+3`/15.6ms 的 lag p95=3 帧——它不在 DP-3 慢拖门
（+1/-1）内，只记录，不写成通过。

## 4. DP-2 基线复跑（改动前，同一二进制）

本会话先跑 DP-2 构建（`7454c32`），确认 `hw_active=1`，仅记录不改代码：

| 场景 | 运行 | 结果 |
|---|---:|---|
| `+60` | 1 | 70.9fps / 空档 37.7ms / lag p95 180 帧 |
| `-60` | 3 | **48.7 / 58.1 / 58.1fps**；空档 44.0 / 25.7 / 34.3ms |
| `-20` | 3 | **43.7 / 50.3 / 48.3fps**；空档 79.4 / 52.8 / 44.3ms |
| `-1` | 1 | 82.7fps / 空档 73.1ms / 命中 84.6% / lag p95 1 帧 |
| `--drag-far-test` | 1 | 前 20.2ms、后 19.4ms（精确首帧，粗帧未呈现）；松手 17.3/24.8ms |
| `--drag-test` | 1 | P4 74.2、P6 p95 31.4ms、P7 0.16/13.34ms、P9 30.99ms |

> DP-2 证据记录的 `-60 60.2–69.2fps` 是在更轻载时测得；本会话同二进制
> 只有 48.7–58.1fps，说明 `-60` 的 5–10fps 波动主要是机器负载，不是
> DP-3 回归。DP-3 的 `-60` 为 54.4–64.4fps，在本会话基线上没有变差。

## 5. DP-3 策略探针（`--drag-strategy-test`）

两次运行均 `all_pass=1`。代表输出（run 1，字段已按行首展开）：

```text
T8.17 drag_strategy scenario=slow_forward mode=exact reason=small_step_le_4
 step=1 step_cfg=1 interval_ms=34 speed_fps=29 run=60 updates=60 presents=61
 hw_presents=61 cache_presents=0 fps=27.5 active_fps=27.5 max_gap_ms=47.2
 lag_frames_p50=0.0 lag_frames_p95=0.0 lag_frames_max=0.0
 coarse_presents=0 coarse_after_exact=0 first_coarse_ms=-1.0
 release_ms=12.1 cache_hit_rate=0.000 hw_active=1 pass=1
```

| 场景 | updates | presents | fps | 空档 | lag p50/p95/max 帧 | 粗帧 | 命中 | 松手 ms | pass |
|---|---:|---:|---:|---:|---|---:|---:|---:|---:|
| slow_forward（+1/33ms） | 60 | 61 | 27.5 | 47.2 | 0 / **0** / 0 | 0 | 0% | 12.1 | 1 |
| slow_backward（-1/33ms） | 60 | 61 | 20.6 | 78.4 | 0 / **0** / 0 | 0 | 19.7% | 15.3 | 1 |
| fast_forward（+60/10ms） | 126 | 79 | 39.3 | 27.6 | 60 / 60 / 60 | 0 | 1.3% | 12.6 | 1 |
| fast_backward（-60/10ms） | — | 74 | 37.4 | 26.4 | 0 / 0 / 60 | 0 | 1.3% | 15.4 | 1 |
| reverse_20（-20/10ms） | 133 | 94 | **41.5** | 48.0 | 20 / 20 / 20 | 0 | 0.9% | 11.3 | 1 |
| far_forward（300→4272） | 1 | 3 | — | 16.1 | 0 / 0 / 12 | **1** | 0% | 12.3 | 1 |
| far_backward（4272→1524） | 1 | 2 | — | 15.8 | 0 / 0 / 24 | **1** | 0% | 17.5 | 1 |

- `fast_forward/backward` 的 headline `fps` 是整 2s（含撞文件边界后的停顿）；
  判据使用 `active_fps=63.2/61.0` 和 `max_gap_ms=27.6/26.4`，与 DP-2 同口径。
- `far_forward`：`first_coarse_ms=16.0`、`exact_after_coarse_ms=16.0`、
  `exact_ms=32.0`；`far_backward`：16.0 / 15.0 / 31.0。粗帧先、精确后。
- `hw_presents=61/78/74` 证明逐帧 `displayed_hardware` 可读；slow_backward
  的 49 张硬件 + 12 张缓存也分别计数。
- `release_samples=11`（7 个场景 + 4 个专用 P6 样本），
  `release_p95=18.2–19.4ms`（≤150）。

summary：

```text
T8.17 drag_strategy_summary ... hw_active=1 slow_forward=1 slow_backward=1
 fast_forward=1 fast_backward=1 reverse_20=1 far_forward=1 far_backward=1
 cases=7 release_samples=11 release_p95=18.2 release_pass=1
 cache_bytes=45619200 cache_peak_bytes=88473600 thumbnail_bytes=432000
 thumbnail_peak_bytes=432000 all_pass=1
```

> 策略矩阵里远跳会 `End` 收窄缓存，所以峰值 88.5MB 小于单次 `-1` 拖动；
> 全局峰值见 §7。

## 6. 快拖/反向/远跳复跑（DP-3 最终构建）

### 6.1 快拖与局部反向

| 场景 | 次数 | fps | 空档 | lag p95 帧 | 命中 | 备注 |
|---|---:|---|---:|---:|---:|---|
| `+60` | 3 | **76.9–80.8** | 34.7–37.2 | 123–130 | 0% | 不冻住 |
| `-60` | 5 | **54.4–64.4** | 23.8–39.3 | 120 | 0% | 同会话 DP-2 48.7–58.1 |
| `-20` | 3 | **47.3–49.2** | 51.5–61.4 | 60 | 0% | ≥25 |
| `+1` | 1 | 93.0 | 20.8 | 1 | 0% | 小步精确 |
| `-1` | 2 | **81.6–85.4** | 35.1–71.7 | **2** | 84.7–85.5% | 局部反向 |

代表输出：

```text
T8.17 drag_fast ... step_frames=60 presents=82 fps=76.9 active_fps=76.9
 max_gap_ms=34.7 target_lag_frames_p95=123.0 hw_active=1 seeks=72
T8.17 drag_fast ... step_frames=-60 presents=... active_fps=64.4
 max_gap_ms=34.8 target_lag_frames_p95=120.0 hw_active=1 seeks=77
T8.17 drag_fast ... step_frames=-20 active_fps=48.4 max_gap_ms=58.9
 target_lag_frames_p95=60.0 hw_active=1 seeks=104
T8.17 drag_fast ... step_frames=-1 active_fps=85.4 cache_hit_rate=0.847
 target_lag_frames_p95=2.0 cache_peak_bytes=244684800 hw_active=1
```

> `-1` 的 lag p95=2 是 15.6ms/2 帧后退的快节奏；33ms 慢拖节奏下
> p95=0。两者都不超过 DP-3 的 ≤2 帧门。

### 6.2 远跳（`--drag-far-test`）

| 路径 | 首张粗帧 ms | 首张画面 ms | 精确目标 ms | 粗帧→精确 ms | 松手 ms |
|---|---:|---:|---:|---:|---:|
| 硬件 前向 300→4272 | **20.8** | 20.8 | 30.6 | 9.8 | 17.5 |
| 硬件 反向 4272→1524 | **20.6** | 20.6 | 30.0 | 9.4 | 18.6 |
| 强制软件 前向 | **40.5** | 40.5 | 110.4 | 69.9 | 18.1 |
| 强制软件 反向 | **20.1** | 20.1 | 219.9 | 199.8 | 19.5 |

代表输出（硬件）：

```text
T8.17 drag_far tag=hard ... forward_coarse_ms=20.8
 forward_first_present_ms=20.8 forward_exact_ms=30.6
 backward_coarse_ms=20.6 backward_first_present_ms=20.6
 backward_exact_ms=30.0 forward_release_ms=17.5 backward_release_ms=18.6
 hw_active=1 coarse_published=1 first_coarse_diag=19.8
```

- DP-2 硬件远跳因精确首帧 20–21ms 已够快，粗帧常被同 tick 覆盖，
  `first_coarse_ms=-1`；DP-3 的 FarJump 闸门让粗帧先呈现，精确帧下一
  tick 到达，`coarse_after_exact=0`。
- 粗帧 `coarse=1`，精确帧 `coarse=0` 且 `displayed_frame==target`；
  松手仍 `End + GoToFrame`，不把粗帧当最终帧。

## 7. 内存/表面池

| 项 | 结果 | 预算 |
|---|---:|---:|
| 预览缓存峰值（`-1`） | **244,684,800B = 233.4MiB** | ≤256MiB |
| 3 次连续拖动峰值 | 244,684,800B 不变 | 不持续增长 |
| 策略矩阵峰值 | 88,473,600B（远跳 `End` 收窄后） | ≤256MiB |
| 缩略图峰值（本轮矩阵） | 432,000B | ≤64MiB |
| DP-2 缩略图观测 | ≤28.8MB | ≤64MiB |
| `readbacks` | 0 | 硬件帧不逐目标回拷 |

代表输出：

```text
T8.17 drag_fast_cycle cycle=1 cache_peak_bytes=244684800
T8.17 drag_fast_cycle cycle=2 cache_peak_bytes=244684800
T8.17 drag_fast tag=hard ... cache_peak_bytes=244684800 thumbnail_bytes=0
```

## 8. 回归

| 项 | DP-3 结果 | 判据 |
|---|---|---|
| `ctest` | **37/37**（62.8s） | 37/37 |
| `--key-test` | **77/0** | 77/0 |
| T8.6.1b `--online-drag-preview-test`（DASH） | **35/0 pass**，UI gap 86ms | 35/0 |
| `--drag-test` | P4 **94.9**、P6 p95 **18.8**、P7 0.1/11.8、P8 12.8、P9 32.2，全 pass | P4≥30、P6≤150、P7≤80 |
| 4K `proxy-V4` | 69.4fps / 空档 20.1ms / `hw_active=0` / `source_too_large` | 只记录 |
| 主播放/在线预览 | 未改 `OnlinePreviewDecoder`；T8.6.1b 通过 | 不退化 |
| 黑帧/崩溃/设备丢失 | 无；`readbacks=0`，表面池未耗尽 | 无 |

> P4 从 DP-2 的 64.1 升到 94.9，是因为本轮系统计时器粒度从约 15.6ms
> 变成约 10ms（同一 10ms PreciseTimer），不是 DP-3 改了 P4；P4 阈值
> 与数字语义未改。P6/P7/P8/P9 与 DP-2 同量级。

## 9. 强制软件残余（如实记录，不写成通过）

| 场景 | DP-3 强制软件 | DP-2 | 结论 |
|---|---:|---:|---|
| `-20` | **9.5fps / 空档 211.0ms / lag p95 400 帧** | 9.9–11.4fps / 199–380ms | 同量级，未达 25 |
| `-60` | **31.4fps / 空档 50.6ms** | 30.3–33.0fps | 同量级，未达 45 |
| `-1` | 89.5fps / 命中 94.2% / lag p95 0 | 84–96% | 受益于缓存/预取 |
| 远跳 | 粗帧 40.5/20.1ms，精确 110.4/219.9ms | 40.7/20.2、110.5/200.5 | 粗帧先到 |

1080p 软件解码单帧约 5–8ms，目标 15.6ms 后退 20–60 帧，精确路径物理
追不上；DP-3 没有用粗帧伪装精确帧，也没有为软件路径降低 1080p 门。

## 10. 4K（只记录，不设通过线）

`media\proxy\proxy-V4.mp4`（3840×2160）：

```text
T8.17 drag_fast ... step_frames=60 presents=191 fps=69.4 max_gap_ms=20.1
 hw_requested=1 hw_active=0 preview_hw_fallback=source_too_large: 3840x2160
 target_lag_frames_p95=314.0 cache_peak_bytes=44236800
```

4K 继续软件预览（DP-1 策略未改），不打开 4K 硬解，不改 1080p 门；
不写 4K 通过。

## 11. 实现文件

| 文件 | 改动 |
|---|---|
| `src/media/DragStrategy.h/.cpp` | 新增四模式判定纯函数、阈值、速度/原因；产品与单测共用 |
| `src/media/PreviewDecoder.h/.cpp` | `SetDragMode`；Exact 关闭粗帧；FarJump 延迟精确帧；精确目标后禁粗帧；逐帧 `displayed_hardware` 诊断 |
| `src/app/MainWindow.h/.cpp` | 策略上下文/判定调用；`PreviewTestDiagnostics` 模式/lag/命中/硬件字段；逐帧记录 |
| `src/app/DragStrategyTestRunner.h/.cpp` | 新增 `--drag-strategy-test` 场景矩阵与 summary |
| `src/app/main.cpp` | `--drag-strategy-test` 参数与 runner 接线 |
| `src/CMakeLists.txt` | 新源文件 |
| `tests/preview_cache_test.cpp` | 策略阈值/边界/速度/原因单测（ctest 仍 37 项） |
| `docs/` | 本证据、DP-3 交接、AGENTS/任务/计划状态 |

## 12. 边界与后续

1. **慢拖门按 +1/-1 测**；+3/15.6ms 的 lag p95 可到 3 帧，是“一次目标
   更新跨 3 帧 + 10ms 取帧”的采样效应，不在 DP-3 慢拖门内，也不写成通过。
2. **模式判定按每次目标更新**；鼠标事件合并会让 `step` 短暂变大（例如
   `+60` 撞边界后最后一次 `step=31`），探针同时输出 `step_cfg` 便于复核。
3. **FarJump 延迟一个 tick 呈现精确帧**（约 10ms）；换来确定的“先粗后精”。
   `--drag-far-test` 精确目标仍 30ms 级，P6 不受影响。
4. **强制软件快反向仍是残余**；若要改善需要更低分辨率解码/代理或更快
   缩略图管线，不能只调缓存。DP-4 不做这个方向。
5. **4K/HEVC 只记录**；不设通过线、不写通过。
6. `End` 后缓存收窄到 32MiB 的 DP-2 语义未改；跨拖动命中率下降是预期。
7. 不改 `FrameCache` 正式 64/256MiB 语义、P4/P6/P7 阈值、M1/M2、OB、
   schema、默认 AVC≤1080p、在线预览。

## 13. 诊断开关（仅本地验证，不写默认配置）

`LAPIAN_PREVIEW_DISABLE_CACHE=1`、`LAPIAN_PREVIEW_DISABLE_PREFETCH=1`、
`LAPIAN_PREVIEW_DISABLE_THUMBNAILS=1`、`LAPIAN_PREVIEW_FORCE_SOFTWARE=1`、
`LAPIAN_TIMING_LOG=1`（粗帧/预取分段日志；DP-3 增加 Exact 模式
`DP3 coarse_skip exact_mode`）。
