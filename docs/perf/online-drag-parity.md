# 在线拖动前后一致 + 丝滑 —— 验收记录

- **日期**：2026-10-25
- **基线**：`e1a89c6`（Agent 1 清晰度切换修复后的干净工作区）
- **范围**：在线增长会话（`.part`）拖动预览；本地拖动预览行为/阈值不变
- **结论（P0 后，2026-10-25）**：C **pass（P0 门禁）**——cold 首次拖动 first_publish **26–28ms**、post-seek 后第一次拖动 first_delivered **48ms**、稳态 **58.5fps**；**残余**：seek 落地 0ms 立即再拖仍有 **640–655ms** 空档（预热完成前窗口，见 §13.5），不得写成“所有场景无空档”。历史 partial 判定与数字见 §12。
- **证据命令**：§10；原始日志在 `build/online-parity-commit.log`、`build/local-parity2.log`、`build/parity-t861b-final.log`（`build/` 不入库）

## 1. 修改前复现（`e1a89c6`）

T8.6.1b 探针，DASH 1080P30（`BV1BMnv6jE5S` / cid `42915922022`，3313 帧，
前段关键帧约 150 帧 ≈ 5s；远端 3000 的关键帧实测 2862），已缓存区，1s/60 采样：

| 场景 | presented | fps_total | fps_active | first_ms | landing_ms |
|---|---|---|---|---|---|
| 正向 15→75（60 样本） | 55 | 55.00 | 61.18 | 101 | 126 |
| 反向 75→45（30 样本） | **14** | **14.00** | 31.18 | **551** | 59 |

反向只有 14 帧、首帧 551ms：拖动大部分时间画面不动，松手才变，和用户现象一致。

## 2. 根因（代码 + 日志确认）

1. **方向不对称**：反向只发布“正好等于目标”的帧（`info.pts_us >= target_pts`），
   `TakeLatestFrame` 又丢弃所有比目标靠后的 ahead 帧；正向允许中间帧。
2. **每次目标更新中断解码**：60Hz 目标下 worker 几乎永远到不了目标，缓存填不满；
   反向每一步都要从关键帧重解。
3. **5s GOP**：目标 75 的关键帧是 0，冷启动要解 76 帧；软件降采样+缓存约 200–380ms。
4. **缓存太小**：24 帧 × 1280 放不下一次 60 帧来回拖动（15→75→15），
   跨过缓存窗口就重新解 27 帧（约 135ms 空档）。

## 3. 实现（只改在线拖动路径）

- **统一调度**：正向/反向同一套规则——先查精确缓存（命中同步发布
  `from_cache=true`）；未命中发布“比上一张更接近目标且不越过目标”的帧；
  精确目标呈现后更远帧不能覆盖；目标变化只丢弃比上一张更远的旧帧；
  ±1 振荡不清空 latest。
- **一次解完进入时的目标**：目标更新不再中断当前 GOP 解码；解码过的帧全部写缓存。
  目标提前命中缓存时，继续向前补 10 帧缓存（不发布或发布当前目标），
  下一次向前拖动不缺帧、不重新 seek。
- **有界缓存**：72 帧 × 1024 长边 NV12 = **63,700,992B（60.75MiB）**，低于 64MiB；
  按距当前目标最远淘汰，目标两侧都留帧；`End/Close` 释放。
- **反向预取**：独立低优先级线程解“缓存窗口下沿之前的 30 帧”，目标拖到边界时已就绪；
  正向取消反向预取。
- **远跳**：≥300 帧先发 320×180 `coarse=true` 关键帧粗帧，再解精确目标；
  远跳期间不逐帧发布中间帧（避免“关键帧幻灯片”）；近距反向优先缓存。
- **松手**：`OnScrubFinished` 停预览 timer、关闭预览 reader（不与精确 seek 抢 `.part`）、
  保留有界缓存后走 `GoToFrame` 精确；会话关闭/切档仍 `End/Close` 释放。
- **未缓存区**：目标超出稳定前缀只置 waiting + “缓冲中（预览）”提示，不阻塞 UI；
  下载追上后 Tick 自动 `UpdateTarget(target,true)` 恢复。
- 文件：`src/media/OnlinePreviewDecoder.h/.cpp`、`src/app/MainWindow.cpp`
  （OnScrubStarted/OnScrubTargetChanged/OnScrubFinished/EnsureOnlinePreviewStarted/
  TickOnlinePreview + 测试访问器）、`src/app/OnlineDragPreviewTestRunner.h/.cpp`、
  `tests/online_preview_decoder_test.cpp`、`src/app/main.cpp`（新探针参数）、
  `src/media/VideoDecoder.h/.cpp`（可选 `thread_count`，默认 0，既有调用不变；
  只有在线预取用 2 线程）。

## 4. 本地 1080p 基线（同一目标序列）

`--local-drag-parity-test --file media\测试.mp4`（1920×1080 / 30fps / 4573 帧）：

| 场景 | updates | presented | first_ms | max_gap_ms | active_fps | lag_p95 | max_missing | flicker |
|---|---|---|---|---|---|---|---|---|
| 正向 15→75（60/1s） | 60 | 59 | 19 | 21 | **58.47** | 0 | 1 | 0 |
| 反向 75→15（60/1s） | 60 | 57 | 47 | 47 | **56.16** | 1 | 2 | 0 |
| 最大缩放 +10（40→49） | 10 | 9 | 62 | 62 | — | 0 | 1 | 0 |
| 最大缩放 −10（50→41） | 10 | 9 | 79 | 79 | — | 0 | 1 | 0 |
| 远跳 15→3000 | 1 | 2 | 16（粗帧） | 16 | — | 0 | 0 | 0 |
| 远跳 3000→15 | 1 | 1 | 3 | 3 | — | 0 | 0 | 0 |
| 本地 P6（`--drag-test`） | — | — | — | — | — | — | — | p95 **15.6ms** |

本地基线：正向 58.47fps / 21ms 空档，反向 56.16fps / 47ms；在线门槛按 70%
取正向 ≥40.9fps、反向 ≥39.3fps。缩放第一步等于按下帧（40/50），没有新画面，
所以 9/10 是正确结果；其余 9 步每步都有响应。

## 5. 在线前后两组数字（`--online-drag-parity-test`）

DASH 1080P AVC qn=80，缓存下界 1MiB/s（保证未缓存区存在），3313 帧/5s GOP；
`cold_backward` 为长 GOP 冷启动诊断（不计入丝滑门槛）：

| 场景 | updates | presented | first_ms | resp_p95 | max_resp | max_gap | active_fps | lag_p95 | stale | landing_frame/pts | ui_gap | coarse/cache | max_missing | flicker | exact_ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cold_backward 75→65 | 10 | 3 | **240** | 0 | 240 | 240 | 9.93 | 0 | 1 | 65 / 2,166,688 | 31 | 0 / 3 | 7 | 0 | — |
| **正向 15→75** | 60 | 58 | **32** | 0 | 49 | **32** | **57.88** | 0 | 1 | 75 / 2,500,000 | 31 | 0 / 58 | 1 | 0 | — |
| **反向 75→15** | 60 | 58 | **31** | 0 | 50 | **32** | **57.88** | 0 | 1 | 15 / 500,000 | 31 | 0 / 58 | 1 | 0 | — |
| 缩放 +10（40→49） | 10 | 9 | 62 | 0 | **70** | 62 | 16.36 | 0 | 1 | 49 / 1,633,313 | 30 | 0 / 9 | 1 | 0 | — |
| 缩放 −10（50→41） | 10 | 9 | 63 | 0 | **74** | 63 | 16.64 | 0 | 1 | 41 / 1,366,688 | 32 | 0 / 9 | 1 | 0 | — |
| 远跳 15→3000 | 1 | 2 | **31（粗帧）** | 31 | 31 | 31 | — | 0 | 0 | 3000 / 100,000,000 | 31 | 1 / 1 | 0 | 0 | **1810** |
| 远跳 3000→15 | 1 | 2 | **31（粗帧）** | 31 | 31 | 31 | — | 0 | 0 | 15 / 500,000 | 30 | 1 / 1 | 0 | 0 | **155** |
| 未缓存边界 | 1 | 1 | 0（提示） | 0 | — | — | — | — | 0 | 45 / 1,500,000 | 32 | — | — | — | — |

- 前后成对：presented **58 vs 58（差 0.0%）**，fps **57.88 vs 57.88**，
  首帧 32 vs 31ms，空档 32 vs 32ms，lag p95 都是 0，landing 都精确。
- 正向/反向都是 **cache_presented = presented**（58/58），没有用粗帧/keyframe
  充 presented（coarse=0）；最大缩放每一步都有响应（max_resp 70/74ms），
  无“后一帧→前一帧→后一帧”闪烁（flicker=0）。
- 缓存峰值 **63,700,992B / 72 帧**（≤64MiB）；整体预览缓存（在线模式不同时启用
  本地预览）≤256MiB；在线预览软解，`readbacks=0`。
- UI gap：每个场景 30–32ms（≤50ms）；T8.6.1b 冷启动场景心跳 77ms（长 GOP 解码）。

## 6. 门槛对照

| 门槛 | 结果 |
|---|---|
| first_response p95 ≤100ms | 正向 32 / 反向 31 / 缩放最坏 70,74 / 远跳粗帧 31,31；**冷启动 240ms 为残余** |
| max_gap ≤50ms | 32 / 32；缩放 ≤150 门槛下 62 / 63 |
| active_fps ≥ 本地 70% | 本地 58.47/56.16 → 门槛 40.9/39.3；在线 **57.88/57.88** |
| response_p95 ≤100ms | 0 / 0（max_resp 49/50） |
| UI gap ≤50ms | 30–32ms（本地 26–37ms） |
| 不允许连续 2 个目标无新画面 | 正向/反向 max_missing=1；缩放=1 |
| presented 差 ≤10% | **0.0%** |
| 两边 landing_frame/pts 精确 | 全部精确（见 §5） |
| 缩放每一步 first_response ≤100ms | 70 / 74ms 最坏 |
| 缩放 max_gap ≤150ms | 62 / 63ms |
| 缩放无闪烁 | flicker=0 / 0 |
| 远跳粗帧先出 | 31 / 31ms；粗帧后精确；presented=2（没有粗帧幻灯片） |
| 禁止粗帧长期充数 | coarse_presented=1，精确目标随后；smooth 场景 coarse=0 |
| 缓存 ≤64MiB | 60.75MiB / 72 帧 |
| `End/Close` 释放缓存 | 单测断言 cache_frames=0、cache_bytes=0 |
| 未缓存区不静默冻住 | “缓冲中（预览）：目标帧 N”0ms、waiting、可恢复、UI 0ms |
| 松手精确 | 全部 exact_frame/exact_pts；本地 P6 p95 15.6ms 不退化 |

## 7. 未缓存区行为

- 目标 `last-1`（稳定前缀外）：`ScrubTarget` 返回 0ms，状态栏/信息栏立即
  “缓冲中（预览）：目标帧 N”，`IsWaiting=true`，没有新帧、没有 UI 阻塞。
- 拖回已缓存区 +5 帧：`waiting=false`，画面恢复（presented 增加），松手精确
  落回目标帧/PTS（45 / 1,500,000）。
- T8.6.1b 未缓存段 35/0 通过（提示/等待/恢复/取消/落点）。

## 8. 残余（记录，不写成通过）

1. **长 GOP 冷启动**：反向 75→65 首帧 **240ms**（207–240ms 波动；5s GOP，K=0，
   软件解 76 帧 + 降采样/缓存）。这是第一次拖动、预览缓存为空时的成本；拖动一次后
   `Pause/Resume` 保留 72 帧缓存，后续正向/反向首帧 31–32ms。
   不通过硬件回拷或会话就绪前预热无法消除；未改产品门。
2. **远跳精确帧**：15→3000 的关键帧是 2862（约 138 帧前），粗帧 31ms 先出，
   精确帧 **1810ms** 才到；3000→15 精确 155ms。软件解 138 帧/降采样是物理成本；
   粗帧阶段只有 1 帧 presented，没有用关键帧刷帧率，但“精确帧等待”仍长。
3. **在线松手 landing**：T8.6.1b 正向 168ms（5s GOP 精确 seek）、反向 76ms；
   本地 P6 p95 **15.6ms** 不变。在线 landing 不是 spec P6 门，未改阈值。
4. **本地反向 max_missing=2**：本地基线在 60Hz 目标/60Hz 预览 timer 下偶尔连续
   两个目标共用一个 present（max_gap 仍 47ms）；在线暖缓存为 1。这是 timer 粒度，
   不是冻住。
5. 4K/HEVC、真实大会员高码率/60fps 在线拖动未验；本任务样本 1080P30。

## 9. 回归

- `ctest`：**37/37**。注意本机从 DSH 工作区启动的 exe 是 **Low integrity**，
  ctest 默认 `%TEMP%` 在 Medium 用户目录，`mark_store/app_settings/cache/...`
  等 11 项会因“无法创建临时目录”失败；把 `TEMP/TMP` 指到工作区
  `build\tmp-ctest` 后 37/37（见 §10）。
- 在线 T8.6.1b：**35/0 pass**（增长会话；正向 46/55.35fps/首帧 169ms，
  反向 27/30fps 上限/首帧 103ms，landing 168/76ms，UI gap 77ms）。
- 在线 T8.6.1b 缓存命中（本地完整 `data.mp4`，720P `BV1PAnt6qEga`）：
  **25/0 pass**（正向 55/60.04fps、反向 27/32.37fps、landing 2ms）。
- 本地 `--drag-strategy-test`：**7/7**（slow ±1 lag p95 0、fast +60 63.4fps、
  −60 61.7fps、−20 43.7fps、远跳粗帧 15/18ms→精确 31/46ms、release p95 15.2ms）。
- 本地 `--drag-test`：最终二进制复跑 P4 65.3fps（机器负载下波动；上一轮 94.7）、
  P6 p95 **15.6ms**、P7 0.0/11.9ms、P8 12.9、P9 p95 36.1ms，全部 pass；
  上一轮 94.7/18.1/0.2/11.2/12.8/30.2，同样 pass。
- 本地 key：**77/0**（`--key-test`，77 项 `ok=1`，0 项 `ok=0`）。
- `lapian_online_preview_test`：全部通过（正向 62.9fps、反向 60Hz **62.5fps**、
  Resume 缓存命中 10–22ms、End 后 cache_frames=0/cache_bytes=0）。
- 在线预览缓存峰值 60.75MiB/72 帧；本地缩略图 432KB（未改）；readbacks=0；
  无 UI 线程长阻塞、黑帧、崩溃、表面池耗尽。
- 未改 M1/M2/P4/P6/P7 门、OB 样本、`lapian-cache-1` schema、默认 AVC≤1080p、
  在线缓存语义、下载器、GrowingSource。
- `VideoDecoder::OpenWithFormat` 新增可选 `thread_count=0`：既有调用（本地预览/
  主播放/在线播放）不传，行为不变；只有在线预取传 2。

## 10. 命令

```powershell
$qt = "$PWD\third_party\Qt\6.8.3\msvc2022_64\bin"
$env:PATH = "$qt;$env:PATH"

# 1) 构建
cmake --build build\release --config Release --target lapian lapian_online_preview_test

# 2) 本地 1080p 基线（同一目标序列）
.\build\release\bin\lapian.exe --local-drag-parity-test --file "media\测试.mp4"

# 3) 在线前后一致 + 丝滑（DASH 1080P30，缓存 1MiB/s 保证未缓存边界）
.\build\release\bin\lapian.exe --online-drag-parity-test `
  --bvid BV1BMnv6jE5S --cid 42915922022 `
  --drag-seconds 1 --drag-samples 60 --m1-throttle-bps 1048576

# 4) 旧 T8.6.1b 回归（增长会话）
.\build\release\bin\lapian.exe --online-drag-preview-test `
  --bvid BV1BMnv6jE5S --cid 42915922022 `
  --online-cache-root build\parity-t861b-final-cache `
  --online-pictures build\parity-t861b-final-pictures `
  --online-records-root build\parity-t861b-final-records `
  --drag-seconds 1 --drag-samples 60 --m1-throttle-bps 120000

# 4b) T8.6.1b 缓存命中（完整 720P data.mp4）
.\build\release\bin\lapian.exe --online-drag-preview-test `
  --bvid BV1PAnt6qEga --cid 42971761157 --drag-cache-hit `
  --online-cache-root build\t8.6.1a-online-cache `
  --online-pictures build\parity-cachehit2-pictures `
  --online-records-root build\parity-cachehit2-records `
  --drag-seconds 1 --drag-samples 60

# 5) 本地回归
.\build\release\bin\lapian.exe --drag-strategy-test --file "media\测试.mp4"
.\build\release\bin\lapian.exe --drag-test --file "media\测试.mp4"
.\build\release\bin\lapian.exe --key-test --file "media\测试.mp4"

# Low integrity：ctest 的临时目录必须在工作区，否则会因无法创建 %TEMP% 失败
$tmp = Join-Path $PWD 'build\tmp-ctest'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$env:TEMP = $tmp; $env:TMP = $tmp
ctest --test-dir build\release -C Release --output-on-failure

# 6) 单测（也可由 ctest 跑）
.\build\release\Release\lapian_online_preview_test.exe "media\proxy\proxy-V1.mp4"
```

## 11. 提交

- 代码/探针/单测 + 本文件一个独立 commit（`fix: smooth symmetric online drag preview`）。
- 不包含 SESSDATA、Cookie、签名 URL、`build/` 日志。

---

## 12. Agent 3 独立复验（2026-10-25）

- **二进制**：clean 重建后的 Medium 副本（hash 见
  [online-quality-switch.md](online-quality-switch.md) §10），非 Agent 2 的工作产物。
- **命令**：§10 第 3 条，独立根 `build\verify-parity`，`--m1-throttle-bps 1048576`。
- **结果**：`online_drag_parity_result checks=45 failures=0`。

### 12.1 本次数字（与 §5 有差异，以本次为准）

| 场景 | updates | presented | first_ms | max_resp | max_gap | active_fps | lag_p95 | stale | landing | ui_gap | max_missing | flicker |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 冷启动反向 75→65（诊断） | 10 | 4 | **224** | 224 | 224 | 13.20 | 0 | 1 | 65 / 2,166,688 | 32 | **6** | 0 |
| 正向 15→75 | 60 | **57** | **17** | 67 | **33** | **56.21** | 0 | 2 | 75 / 2,500,000 | 32 | 1 | 0 |
| 反向 75→15 | 60 | **57** | **31** | 67 | **33** | **56.72** | 0 | 1 | 15 / 500,000 | 31 | 1 | 0 |
| 缩放 +10 40→49 | 10 | 9 | 63 | 75 | 63 | 16.54 | 0 | 1 | 49 / 1,633,313 | 30 | 1 | 0 |
| 缩放 −10 50→41 | 10 | 9 | 63 | 86 | 76 | 15.90 | 0 | 1 | 41 / 1,366,688 | 31 | 1 | 0 |
| 远跳 15→3000 | 1 | 2 | **31（粗帧）** | 31 | 31 | — | 0 | 0 | 3000 / 100,000,000 | 31 | 0 | 0 |
| 远跳 3000→15 | 1 | 2 | **17（粗帧）** | 17 | 17 | — | 0 | 0 | 15 / 500,000 | 30 | 0 | 0 |
| 未缓存边界 | 1 | 1 | 0（提示） | — | — | — | — | 0 | 45 / 1,500,000 | 31 | — | — |

- 前后成对：presented **57 vs 57（0.0%）**，`parity_presented_diff_le_10pct=1`；两边
  `landing_frame_ok=1`、`landing_pts_ok=1`（远跳精确帧 1695ms/142ms，粗帧先出、精确后无粗帧）。
- 缓存峰值 **63,700,992B / 72 帧 = 60.75MiB**；`ui_gap max_ms=32`、`over_200=0`；
  未缓存提示 `缓冲中（预览）：目标帧 3312`、0ms、拖回 +5 帧恢复。
- 与 §5 的差异（同一二进制/机器，负载与计时粒度导致）：presented 57（§5 58）、
  active 56.21/56.72（§5 57.88/57.88）、max_resp 67（§5 49/50）、gap 33（§5 32）、
  缩放 max_resp 75/86（§5 70/74）、远跳精确 1695/142ms（§5 1810/155）、
  冷启动 224ms（§5 240）。结论不变：前后一致、稳态丝滑。

### 12.2 探针缺陷（必须在读表时扣掉）

1. `response_p95_ms` **恒为 0**：`RunParityFrames` 只把延迟写进 `max_response_ms`，
   `result.responses` 从未 append，`Percentile(responses,0.95)` 对空向量返回 0
   （`OnlineDragPreviewTestRunner.cpp:787`）。本表的 `max_resp` 是 p95 的保守上界：
   **max_resp 67ms ≤100**，因此“response_p95 ≤100ms”仍成立，但 0.00 不是测量值。
2. `landing_frame` 打印的是 PTS：`PrintParity` 的 `%21`/`%22` 都绑定 `landing_pts`
   （`OnlineDragPreviewTestRunner.cpp:655-656`）。落点精确性以
   `landing_frame_ok/landing_pts_ok`、T8.6.1b 的 `exact_frame=1/exact_pts=1` 与
   `online_seek_land target=… pts_us=…` 为准，不引用该字段。
3. 远跳 `max_gap_ms` 被赋值为 first_response，只有 1 个目标，不能当拖动空档看。

### 12.3 C 判定

- 稳态已缓存区（正向/反向 60 帧）：first 17/31ms、max_gap 33/33ms、active 56.21/56.72fps、
  UI gap 32ms、max_missing 1，全部达到 §3.C 门槛；相对本地基线 58.53/57.00fps 为
  **96.0%/99.5%**（门槛 70%）。
- **冷启动首次拖动 224ms、连续 6 个目标无新画面**，超过 §3.C 的 100ms 与“连续 2 个”门槛；
  T8.6.1b 首次正向 152ms 亦 >100ms（其自身门槛为 200ms，不替代 C）。
- 因此 C **不能写“丝滑通过”**，任务总判定为 **partial**（A/B pass）。
  若要收口，方向是会话就绪前预热预览解码器/首帧，或把“首次拖动”明确写成 C 的例外并
  由用户拍板；本轮不擅自改门槛。

---

## 13. P0 首次拖动修复（2026-10-25）

### 13.1 范围与提交

- 代码：`3084e33`（度量/探针）、`c912770`（decoder：strict seek / baseline / 首帧 / Prime / 未缓存 / Warm）、`35fa8be`（MainWindow 接线 + 门禁）。
- 不改本地 `PreviewDecoder`/`DragStrategy`、`OnlineSession`、门/阈值、schema、默认 AVC≤1080p、切档；未上硬解。
- 验证：S4 从 `35fa8be` clean Release 重建（232.2s，ctest 37/37），Medium 副本，新缓存根 ×2，1MiB/s。

### 13.2 根因

1. `Resume` 把 `last_delivered_frame` 清 -1；`ShouldPublishRegularLocked`/`TakeLatestFrame` 在 `last<0` 时只接受精确目标 → 5s GOP 冷启动要解 76 帧才出第一张画面。
2. 没有“当前显示帧”baseline，首目标中间帧无法通过“更接近且不越过”判定。
3. 预览 seek 用 `av_seek_frame`（`SeekToPts`）：fMP4 上 `keyframe=1500` 实际落到 ~1350（**offset=-150**），post-seek 首次拖 1.29–1.50s；主会话已有的 `SeekToKeyframePts`（`avformat_seek_file` 钉 min/ts/max）未用于预览。
4. `Pause` 关闭 reader/decoder 后没有后台预热，seek/暂停后第一次拖 ±1 仍从关键帧重解。

### 13.3 修复

- `DecodeToward`/`RunPrefetch` 改用 `SeekToKeyframePts`（失败 fallback `SeekToPts`，`seek_fallbacks`/`last_seek_landing_offset` 诊断）；S4 两轮 `seek_offset=0`、无 fallback。
- `Begin/Resume(displayed_frame)` 把当前显示帧作为 baseline；删除 `last<0` 硬闸；首张“更近且不越过目标”的帧立即发布。
- `PrimeFrame`：拖动开始时 clone 当前显示帧、worker 异步降到 1024 NV12 入缓存（不发布）。
- 未缓存目标：发 last 与 target 之间最近的缓存帧（`from_cache=true`），waiting/“缓冲中”保持。
- `Warm(frame)`：静默、目标 `frame+10`、缓存 `[frame-61,frame+10]`、完成后线程/decoder/reader idle；`Begin(warm_only)` 供无拖动预热；Pause 仍硬关闭，seek 前 Pause reader。
- MainWindow：seek 落地 120ms、播放暂停 200ms 合并预热；拖动/seek/停止取消；pass displayed baseline + PrimeFrame。

### 13.4 前后数字

before = S1/`build\p0-before*.log`；after = S4/`build\p0-verify-parity*.log`（两轮，取范围）。

| 场景 | 指标 | before | after |
|---|---|---|---|
| cold 75→65（首次拖动） | first_publish / first_delivered / first_response / max_missing | **263 / 272 / 190 / 5** | **26–28 / 32–36 / 1 / 0–1** |
| post_seek_backward 1575→1565 | first_publish / first_delivered / first_response / max_missing | **1287 / 1294 / 1204 / 9** | **0 / 48 / 49 / 1** |
| post_seek_forward 1605→1615 | 同上 | **1500 / 1511 / 1420 / 9** | **0 / 48 / 49 / 1** |
| forward 15→75 | first_publish / first_delivered / fps | 4 / 29 / 56.94 | 9 / 17–18 / **58.53–58.65** |
| backward 75→15 | 同上 | 9 / 29 / 56.77 | 4–5 / 111–112* / **58.65–58.71** |
| zoom +10 / −10 | first_publish / response | 4 / 63 | 4/10 / 62 |
| far + / − | 粗帧 / exact | 9,5 / 1667,137 | 39,22 / **695,119** |
| 未缓存 40→45 | first_publish / delivered | 9 / 42 | 4 / 17 |

\* 既有场景 `initial_pump_ms=80`，`first_delivered` 含 80ms 预热；post/immediate 为 0。跨场景比较看 `first_response`。

- post-seek：baseline 1575/1605、warm **337–386ms**、warm_frames 86/116、`seek_offset=0`、landing 精确、flicker=0。
- cold：baseline=0（当前显示帧，不是 scrub 起点 75）、open 10ms。
- 稳态：presented 59/59（0.0%）、fps ≥58.5、gap 32/33ms；UI gap 19–21ms；cache 峰值 60.75MiB/72 帧。
- 远跳 exact 1667/137 → 695/119ms（strict seek 的附带收益，未改门）。

### 13.5 残余与读表口径（必须同时引用）

1. **`post_seek_immediate`（只记录）**：StopPreview 丢弃 decoder → seek 1655 落地 0ms 立即拖 1655→1645；`max_response 645–646ms`、`max_gap 640–655ms`（1645 仍需从 K=1500 解 145 帧）。它的 `first_publish=36` 被 baseline/PrimeFrame 帧污染，**不可信**；该场景不门禁，代表“预热完成前”的窗口。要消除需主 decoder 帧交接或硬解，超出本轮。
2. **cold 是合成场景**：屏幕 0、ScrubStart 75、目标 75→65；probe 按 backward 记“显示帧增大”为 flicker（13/14）、`ok=0`。cold 门禁只查 first_publish/delivered/response/max_missing（全过），**不能当“无闪烁”证据**；真实反向 post_seek_backward flicker=0。
3. **`initial_pump_ms`**：既有场景 80，post/immediate 0。若按 80ms 计 post first_delivered 会是 128ms；target→画面的用户可见延迟看 `first_response`（49ms）。
4. **本地反向波动**：S4 local backward 49.55/53.52fps、gap 68/59ms、max_missing 3/2（§12 57.00/45/2），9/0 仍过；本地源码无 diff。
5. `--drag-strategy-test` 两次 6/7，唯一失败 `fast_forward max_gap 54.9/50.8ms`（阈值 50），既有 flaky。
6. 新根首包 `bytes=0 写入缓存文件失败` 后重试成功；一次 20s 下载超时后重试；S2/S3 各一次 `lapian_preview_decoder_test` 0xc0000374（隔离/复跑通过；S4 clean 未出现）。**2026-10-25 收口**：首写已定位为 DASH 总长探测假错误（不是写盘），预览崩溃已复现并修复缓存克隆竞态；见 §13.8。

### 13.6 门禁与回归

| 项 | 结果 |
|---|---|
| 每次拖动 first_publish ≤100ms | cold 26/28、post 0、forward 9、backward 4/5、zoom 4/10、far 39/22 |
| 不允许连续 2 目标无新画面 | cold 0/1、post 1、forward/backward 1、zoom 1 |
| 正反成对 | presented 59/59（0.0%）、post 正反各 10 目标 |
| 稳态 ≥ 本地 70% | 在线 58.5+ vs 本地 forward 58.4+ / backward 49.5–53.5（均 ≥70%） |
| ctest / key / T8.6.1b | **37/37** / **77/0** / **35/0** |
| A 回归 | **42/0** |
| 缓存 / UI gap | 60.75MiB/72 帧；19–21ms |

### 13.7 复验命令

```powershell
$exe = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"
# P0 在线矩阵（新根、1MiB/s）
& $exe --online-drag-parity-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --tag p0-verify --timeout-ms 90000 --drag-seconds 1 --drag-samples 60 `
  --m1-throttle-bps 1048576 `
  --online-cache-root build\verify-p0\cache --online-pictures build\verify-p0\pics `
  --online-records-root build\verify-p0\records --settings-path build\verify-p0\settings
# T8.6.1b / key / 本地
& $exe --online-drag-preview-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --drag-seconds 1 --drag-samples 60 --m1-throttle-bps 120000 `
  --online-cache-root build\verify-p0\t861b-cache --online-pictures build\verify-p0\t861b-pics `
  --online-records-root build\verify-p0\t861b-records --settings-path build\verify-p0\t861b-settings
& $exe --key-test --file 'media\测试.mp4'
& $exe --local-drag-parity-test --file 'media\测试.mp4'
```

- clean 重建：`scripts\build-release.ps1 -Clean`（232.2s）；dist 25 文件 / 36,392,264B / 34.71MB；exe SHA256 `D2CDEBDF637F055B1550417DEF5A0FBA17246C9AFFFBBF0ACD5508A6661ACEDB`。
- 原始日志：`build\p0-before*.log`、`build\p0-verify-*.log`（`build/` 不入库）。

## 13.8 发布收口：首写假失败与预览解码器崩溃（2026-10-25）

### 13.8.1 新根 `bytes=0 ... 写入缓存文件失败` = DASH 总长探测假错误

- **复现（修复前）**：3 个全新根各跑一次 P0 parity，均有 2 条（视频+音频）：
  `cdn_done status=206 bytes=0 range_ignored=0 error=写入缓存文件失败`，且都在
  `online_dash_keys` 之前；随后真实下载完整成功（31,057,770B / 1,294,290B）。
  日志 `build\t2-r1..r3.err.log`。
- **根因**：`ProbeDashTotal` 发 `Range: bytes=0-0`，旧 `FirstChunkSink` 返回
  false 且不写 error 来停止读 body；`WinHttpDownloadTransport::OnRead` 将
  “sink 返回 false + 空 error”映射为 `kSinkError` 并落成“写入缓存文件失败”。
  新根首开没有缓存命中才走探测，故只在首次出现；`.part` 写入路径本身正常。
- **修复 `925b8d5`**：新增 `DownloadRequest::headers_only`；传输层在
  `OnHeaders` 校验完 `Content-Length`/`Content-Range` 后直接 `kOk`，不 `StartRead`、
  不碰 sink；探测传 `nullptr`。真实下载 `headers_only=false`，写盘/续传/备份/
  刷新/配额/schema 不变。
- **验证**：修复后 3 个全新根（`build\t2-f1..f3.err.log`）0 条该错误，探测日志
  `headers_only=1 error=`、视频/音频总大小与修复前一致、parity 61/0。

### 13.8.2 `lapian_preview_decoder_test` 0xC0000374 / 0xC0000005

- **复现（修复前）**：隔离 20 次失败 2 次（0xC0000374、0xC0000005）；全量 ctest
  首跑失败（WER `avutil-59.dll+0x43612 c0000005`）；事件日志 2026-10-25 00:40
  （2 次）、13:19、13:41 另有 `c0000374`。
- **根因**：`PreviewDecoder::Impl::CloneExactLocked` 注释要求持 `mutex`，但
  `Process`（精确缓存命中）与 `PrefetchStep`（预取前查缓存）两处无锁调用。
  该函数遍历热缓存 `deque`/环缓存 `list` 并 `av_frame_clone`；UI 线程
  `Begin/End/UpdateTarget` 与预取线程并发 push/erase/evict 同一批 AVFrame，
  命中被 eviction 的帧即在 avutil 崩溃，容器并发增删则堆损坏。
- **修复 `08484ee`**：两处 clone 改为作用域内持锁，出锁后再 `Publish`/free；
  缓存预算/发布策略/门禁未改。
- **验证（修复后）**：隔离 **50/50**（50 份 `全部通过`、353s）；全量 `ctest`
  **3/3 37/37**（64/62/63s）；18:58 后无新 preview WER。未取得符号化 dump，
  不写“证明无崩溃”。

### 13.8.3 最终产物 / 安装 / 回归（HEAD `08484ee`）

| 项 | 结果 |
|---|---|
| clean `-Clean -Package` | **242s**、exit 0、脚本内 ctest 37/37（63.43s） |
| dist | **25 文件 / 36,392,776B / 34.71MB**；exe 3,183,104B |
| exe SHA256 | `8344A2BB871130A7F21726FA72BA167296942BCEE5ABB47699D1D6AE6C1304CA` |
| setup / zip | 12,474,791B（11.9MB）/ 15,197,307B（14.49MB）；SHA256 `C6F3B075…` / `8420FEA5…` |
| 安装 | `/VERYSILENT /CURRENTUSER` exit 0 / 2s；安装版 hash 与 dist 一致；Start/桌面快捷方式 target+hash 一致；settings 1183B 未动 |
| P0 parity（新根） | **61/0**；cold first_publish 26ms / first_delivered 36ms；post 46ms；forward 56.94fps / backward 56.66fps；gap 31/32ms；presented 差 0.000；cache 60.75MiB/72；UI 31/32ms |
| T8.6.1b / A / key / local | **35/0** / **42/0** / **77/0** / **9/0** |
| `--drag-test` | P4 64.4、P6 p95 18.0ms、P7 0.0/16.9、P8 12.8、P9 31.1，全 pass |
| `--drag-strategy-test` | **7/7 all_pass=1**；release p95 14.5ms |

- 日志：`build\t2-r*.err.log`、`build\t2-f*.err.log`、`build\t3-post-*.out.log`、
  `build\t3-ctest-*.log`、`build\t4-build.out.log`、`build\t4-install.log`、
  `build\t5-*.{out,err}.log`、`out\package\package-report.txt`（`build/`/`out/` 不入库）。
- 残余：无符号化 dump；4K/60 与 `post_seek_immediate` 未覆盖；用户体感确认非量化。
  详见 [tasks-online-parity-fix.md](../tasks-online-parity-fix.md) §9.9、
  [handoff-2026-10-25-p0-close.md](../handoff-2026-10-25-p0-close.md)。
