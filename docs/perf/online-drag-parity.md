# 在线拖动前后一致 + 丝滑 —— 验收记录

- **日期**：2026-10-25
- **基线**：`e1a89c6`（Agent 1 清晰度切换修复后的干净工作区）
- **范围**：在线增长会话（`.part`）拖动预览；本地拖动预览行为/阈值不变
- **结论**：**pass（已缓存区）**；长 GOP 冷启动/远跳精确帧为残余，见 §8
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
