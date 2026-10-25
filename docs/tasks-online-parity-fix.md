# 在线播放修复任务：前后一致 + 丝滑流畅 + 清晰度切换

- **版本**：v1.3（2026-10-25；P0 首次拖动修复验收完成，追加 §9）
- **状态**：**pass（P0 范围）**——A 清晰度切换 **pass**；B 前后一致 **pass**；C 丝滑流畅 **pass（P0 门禁）**：首次拖动 first_publish 26–28ms、post-seek 后第一次拖动 first_delivered 48ms、稳态 58.5fps。**残余**：seek 落地 0ms 立即再拖仍有 ~640–655ms 空档（预热完成前窗口，按批准口径不门禁）、cold 合成场景 flicker 为方向语义 artifact、本地反向/策略 flaky，详见 §9.6；不得写成“所有场景无空档/全绿”。
- **代码基线**：`35fa8be`；P0 提交 `3084e33`（度量/探针）+ `c912770`（preview seek/首帧/预热）+ `35fa8be`（MainWindow 接线/门禁）；历史实现 `e1a89c6`+`4d89683`；文档提交见 `git log -1 -- docs/tasks-online-parity-fix.md`
- **性质**：在线播放体验/缓存修复；**不改本地拖动预览、M1/M2/P4/P6/P7、OB 样本、`lapian-cache-1` schema、默认 AVC≤1080p**
- **一句话**：在线拖动要像本地一样及时响应、前后一致；拉流后切清晰度不能再报“已完整缓存但未走缓存命中”。

---

## 0. 判定原则

本任务有 **3 个独立验收层**，缺一层都不算完成：

1. **A 清晰度切换**：能切、切对、命中正确、不静默换 codec。
2. **B 前后一致**：正向和反向行为一致，不能“向前可以、向后冻住”。
3. **C 丝滑流畅**：已缓存区拖动要接近本地，画面总是及时响应；不能只做到“前后一样差”。

> B 和 C 必须分开验收：正向 10fps、反向 10fps 满足 B，但不满足 C。

本文件是验收唯一来源；提示词只补充执行细节，不得另造更低标准。

---

## 1. 用户现象

### A. 在线拖动

- 进度条未缩放时，前后拖动画面直接停住，松手才变化。
- 最大缩放逐帧拖动：
  - 向前拖：前后帧来回闪烁；
  - 向后拖：画面冻住，松手才加载。
- 本地播放拖动正常；用户要求在线拖动**丝滑、跟手、前后一致**。

### B. 清晰度切换

- 在线拉流确认后，默认 1080P 播放。
- 切到大会员 1080P 高码率时提示：

```text
DASH条目已完整缓存，但本次未走缓存命中路径；请重新打开
```

- 结果：拉流后无法切换清晰度。

---

## 2. 硬要求

### 2.1 前后一致

> 任何与方向有关的交互/性能改动，必须同时测试正向和反向，并给出两组数字；不允许“前进可用、后退不可用”或两边行为不一致。只测一个方向视为未完成。

写入 `AGENTS.md` §11；§9 补一句：在线拖动/切档验收必须前后成对。

### 2.2 丝滑流畅

> 在线已缓存区的拖动必须接近本地：目标变化后画面及时响应，拖动过程中持续出帧，不出现长空档；不允许用关键帧/粗帧刷 presented 充数。

本地同场景是基线，不是“比现在好一点”。

---

## 3. 修复范围

### A. 清晰度切换（先做，独立提交）

**当前代码观察（待复现确认）**：

- `OnlineSession::Start` 的 `TryCacheHit` 按 `requested_qn + requested_codecid` 过滤；
- `DownloadJob::Start` 按实际 plan 的 key 发现完整缓存并返回 `kCompleted`；
- 两个键不一致时会走到“已完整缓存但未走缓存命中”的 guard；
- DASH 还要求视频+音频同时完整，`audio_id` 不匹配也会错过命中。

**要求**：

1. 解析出实际 `video_key/audio_key` 后、创建 `DownloadJob` 前，按实际键做精确缓存检查。
2. 视频+音频都完整 → cache-hit，0 网络。
3. 只有视频完整、音频缺失 → 明确“需要重建/补音频”，不报误导错误。
4. 请求 codecid 与缓存不同 → 下载请求的流，不能静默换 codec/降档。
5. `DownloadJob::Start` 的 `kCompleted` 改为内部信号/防御，不作为用户错误。

**验收**：

- 不再出现上述错误。
- 默认 1080P → 大会员高码率 → 切回 1080P30 全链路通过。
- 实际 qn/codecid、PTS 回位、帧号映射、标记/截图保留正确。
- 缓存命中 0 playurl/CDN；未命中正常下载。

### B. 在线拖动前后一致（第二阶段，独立提交）

**当前代码观察（待复现确认）**：

- `OnlinePreviewDecoder` 正向允许发布中间帧；
- 反向只接受“正好等于目标”的帧，`TakeLatestFrame` 还会丢弃比目标更靠后的帧；
- 目标更新快于解码时，反向永远等不到精确帧 → 冻住；
- 最大缩放 ±1 振荡放大该不对称 → 闪烁。

**要求**：

1. 前后使用同一套“最近可用帧 + 目标细化”规则。
2. 先查精确缓存；命中立即发布。
3. 没命中时发布“比上一张更接近当前目标”的帧，不再用方向决定是否发布。
4. 精确目标发布后，不允许更远的帧覆盖。
5. 目标变化只丢弃更远的旧帧，不因反向丢弃所有 ahead 帧。
6. 远跳/大范围先关键帧粗帧（`coarse=true`），再精确。
7. 近距反向优先缓存，不先跳关键帧。
8. 有界缓存（≤64MiB，建议 32/48 帧）；最大缩放 ±1 走精确缓存/续解；目标振荡不清空 latest。
9. 松手仍 `End + GoToFrame` 精确。

**验收**：

- 未缩放：前后拖动过程中都有画面更新，不冻住。
- 最大缩放：前后逐帧拖动每一步都呈现，不闪回、不冻住。
- 松手：前后都精确落到目标帧/PTS。
- 前后成对：presented 差 ≤10%，两边 landing 都精确。
- 只交正向数据视为未完成。

### C. 丝滑流畅（独立于 B，必须单独验收）

**定义**：目标变化后，画面要尽快响应；拖动过程中持续出帧，不能“等松手才动”。

**测量字段**：

```text
updates               目标更新次数
presented             实际呈现帧数
presented/updates     响应覆盖率
first_response_ms     开始拖/目标变化到第一张新画面
response_p95_ms       每次目标更新到下一张画面的 p95
max_gap_ms            拖动过程中最长无新画面间隔
active_fps            拖动活跃期呈现帧率
target_lag_p95        画面落后目标的帧数 p95
ui_gap_ms             UI 线程最长阻塞
stale_dropped         过期丢弃帧数
landing_frame/pts     松手精确落点
```

**本地基线**：用同一目标序列跑本地 1080p，记录
`active_fps / max_gap_ms / first_response_ms / target_lag_p95`，作为在线对照。

**已缓存区初始门槛**（跑完基线后可收紧；不得为通过而放宽）：

- `first_response p95 ≤100ms`；
- `max_gap ≤50ms`；
- `active_fps ≥ 本地同场景的 70%`；
- `response_p95 ≤100ms`；
- `ui_gap ≤50ms`；
- 不允许连续 2 个目标更新没有新画面。

**最大缩放逐帧前后拖**：

- 每一步 `first_response ≤100ms`；
- `max_gap ≤150ms`；
- 不允许连续 2 步无新画面；
- 不允许向前拖时出现“后一帧→前一帧→后一帧”的闪烁。

**未缓存区**：

- 必须出现“缓冲中”提示，不能静默冻住；
- 下载追上后自动恢复预览；
- 不能在 UI 线程阻塞读/同步 seek。

**粗帧/关键帧**：

- 远跳允许先粗后精；
- 不允许长期只发关键帧/粗帧充 `presented`；
- 精确目标发布后不能被更远帧覆盖；
- 只靠关键帧幻灯片不算丝滑。

---

## 4. 测试矩阵

| 场景 | 验收 |
|---|---|
| 在线未缩放正向 60 帧 | B + C：不冻、持续出帧、first_response/max_gap 达标 |
| 在线未缩放反向 60 帧 | B + C：与正向同量级，不是“一样差” |
| 最大缩放正向 +10 帧 | 每步呈现，不闪回，first_response ≤100ms |
| 最大缩放反向 -10 帧 | 每步呈现，不冻住 |
| 远跳正向/反向 | 粗帧先出，精确随后；粗帧不长期充数 |
| 未缓存边界 | 缓冲提示、不静默冻住、回缓存区恢复 |
| 松手 | 前后都精确 landing |
| 本地同目标序列 | 作为 C 的基线 |
| 清晰度：默认→高码率→1080P30 | 无错误、PTS/标记正确 |
| 清晰度缓存命中 | 0 playurl/CDN |
| 清晰度音频缺失 | 明确重建，不误导 |
| 本地拖动全矩阵 | 不退化 |

---

## 5. 执行顺序

**3 个 agent，串行：1 → 2 → 3，不要并行改代码。**

1. **Agent 1**：清晰度切换；提交 `fix: exact-key DASH cache hit on quality switch`。
2. **Agent 2**：在线拖动前后一致 + 丝滑流畅；提交 `fix: smooth symmetric online drag preview`。
3. **Agent 3**：验收 + 文档 + `AGENTS.md` 前后一致规则；提交 docs/验收。

Agent 1 和 Agent 2 都会碰 `MainWindow`/在线测试代码，必须串行。

---

## 6. 停止/回滚

- 清晰度修复出现静默换 codec/降档 → 停止，用户选择优先。
- 在线拖动改动导致本地预览回归 → 回滚该提交，保留清晰度修复。
- 只做到“前后一致但两边都低帧率” → 不算通过，记录 partial。
- 缓存峰值超过 256/64MiB 或出现无界缓存 → 缩预算，不突破。
- 只靠关键帧刷新充 `presented` → 不算丝滑，继续细化。
- 4K/HEVC 不达标 → 只记录，不设通过线。

---

## 7. 交付

- `docs/perf/online-quality-switch.md`
- `docs/perf/online-drag-parity.md`（含前后两组数字 + 本地基线 + 丝滑门槛）
- `docs/handoff-2026-10-25-online-parity.md`
- `AGENTS.md` §11 前后一致规则、§9 风险补充
- 两个代码提交 + 一个文档/验收提交
- 不打印/提交 SESSDATA、Cookie、签名 URL

---

## 8. 独立验收结果（Agent 3，2026-10-25；P0 前历史判定）

> 本节保留 P0 修复前的 partial 判定与数字；P0 后结论见 §9。

### 8.1 结论

| 层 | 判定 | 依据 |
|---|---|---|
| A 清晰度切换 | **pass** | 默认 1080P（qn=80）→ 1080P+（qn=112）→ 1080P30（qn=80）真实链路 `checks=42 failures=0`；无“已完整缓存…请重新打开”；切回 `cache_hit=1`、dash/playurl=0 |
| B 前后一致 | **pass** | 正向 57 presented / 反向 57 presented（差 **0.0%**）；两边 landing 都精确（75/PTS 2,500,000；15/PTS 500,000）；不是“两边一样差”（56.21/56.72fps，本地基线 58.53/57.00） |
| C 丝滑流畅 | **partial** | 稳态已缓存区：first 17/31ms、空档 33/33ms、active 56.21/56.72fps、UI gap 32ms、连续无新画面=1；但**首次拖动（预览解码器冷启动）first 224ms、连续 6 个目标无新画面**，超过 §3.C 的 100ms/“不允许连续 2 个”门槛 |
| 总判定 | **partial** | 按 §8 判定规则，C 未全部达到，不能写“完成”或“丝滑通过” |

### 8.2 环境与构建

- HEAD `4d89683`；实现提交 `e1a89c6`、`4d89683`；工作区 clean；任务文档 v1.1 为 `2fee5e6` 后代。
- `scripts\build-release.ps1 -Clean`：**222.4s**；`ctest` **37/37**（脚本内 62.14s，手动 61.21s）。
- `dist` **25 文件 / 36,359,496B / 34.68MB**；`lapian.exe` 3,149,824B、SHA256 `7A80C9A9B3C865185CEBA1A489FB1AE56635BDDA065E4699C829B0461B7A3761`。
- Medium 副本 `%LOCALAPPDATA%\Programs\拉片软件` 用本次 `dist\*` 覆盖（33 文件），`lapian.exe` hash 与 dist 一致。

### 8.3 A 层（Medium 副本，真实网络/默认 DPAPI）

命令：

```powershell
$exe = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"
& $exe --online-quality-switch-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --tag verify-t8.17 --timeout-ms 60000 --quality-switch-high 112 --quality-switch-back 80 `
  --quality-switch-wait-ms 120000 --online-cache-root build\verify-t8.17\cache `
  --online-pictures build\verify-t8.17\pics --online-records-root build\verify-t8.17\records `
  --settings-path build\verify-t8.17\settings
```

| 项 | 实测 |
|---|---|
| 初始 | qn=80 / codecid=7 / fps=30.000；ready=1；dash_requests=1、playurl=0 |
| 80→112 | landed=1；qn=**112** / codecid=**7** / fps=30.000；PTS **1,000,000→1,000,000**；第 30 帧；标记 1→1；记录/截图保留；音频复用（complete=1），只下载 112 视频 47,168,494B |
| 112→80 | landed=1；qn=**80** / codecid=**7**；`cache_hit=1`、**dash_requests=0、playurl_requests=0**；PTS 1,000,000、第 30 帧；标记/记录/截图保留；三条缓存 complete |
| 误导错误 | `no_misleading_complete_error=1`（切档 error 均为空） |

- “视频完整/音频缺失”分支：单测 `lapian_online_fault_test`（T8.17）已验证“复用视频、只补音频、无错误回调/无用户级完整缓存错误”；用户提示串在 `OnlineSession.cpp` 的“视频已完整缓存（…），正在补下音频（audio-…）”。**本轮真实窗口没有构造该分支**，不写“真实提示已目视通过”。
- 音频：音频轨重建/打开成功、缓存 complete；**可听性未由无人值守探针验证**。

### 8.4 B/C 层（Medium 副本，新缓存 + 1MiB/s 限速）

命令：

```powershell
& $exe --online-drag-parity-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --tag verify-parity --timeout-ms 90000 --drag-seconds 1 --drag-samples 60 `
  --m1-throttle-bps 1048576 --online-cache-root build\verify-parity\cache `
  --online-pictures build\verify-parity\pics --online-records-root build\verify-parity\records `
  --settings-path build\verify-parity\settings
```

结果：`checks=45 failures=0`。两组数字（另附冷启动诊断）：

| 场景 | updates | presented | first_ms | max_resp | max_gap | active_fps | lag_p95 | stale | landing | ui_gap | max_missing | flicker |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 冷启动反向 75→65（诊断，不计门槛） | 10 | 4 | **224** | 224 | 224 | 13.20 | 0 | 1 | 65 / 2,166,688 | 32 | **6** | 0 |
| **正向 15→75** | 60 | **57** | **17** | 67 | **33** | **56.21** | 0 | 2 | 75 / 2,500,000 | 32 | 1 | 0 |
| **反向 75→15** | 60 | **57** | **31** | 67 | **33** | **56.72** | 0 | 1 | 15 / 500,000 | 31 | 1 | 0 |
| 最大缩放正向 40→49 | 10 | 9 | 63 | 75 | 63 | 16.54 | 0 | 1 | 49 / 1,633,313 | 30 | 1 | 0 |
| 最大缩放反向 50→41 | 10 | 9 | 63 | 86 | 76 | 15.90 | 0 | 1 | 41 / 1,366,688 | 31 | 1 | 0 |
| 远跳正向 15→3000 | 1 | 2 | **31（粗帧）** | 31 | 31 | — | 0 | 0 | 3000 / 100,000,000 | 31 | 0 | 0 |
| 远跳反向 3000→15 | 1 | 2 | **17（粗帧）** | 17 | 17 | — | 0 | 0 | 15 / 500,000 | 30 | 0 | 0 |
| 未缓存边界 | 1 | 1 | 0（提示） | — | — | — | — | 0 | 45 / 1,500,000 | 31 | — | — |

- 前后成对：presented **57 vs 57（0.0%）**；一边不精确即不通过——两边 `landing_frame_ok=1`、`landing_pts_ok=1`。
- 缩放：最坏每一步 max_response **75/86ms ≤100**、max_gap **63/76ms ≤150**、无连续两步无画面、无闪烁。
- 远跳：粗帧 31/17ms 先出，精确帧 **1695/142ms** 后到；`coarse_presented=1`、精确后无粗帧。
- 未缓存：`缓冲中（预览）：目标帧 3312` 0ms、waiting、拖回 +5 帧恢复、松手 45/1,500,000 精确。
- 缓存峰值 **63,700,992B / 72 帧 = 60.75MiB**（≤64MiB）；UI gap 32ms（≤50）。

### 8.5 本地基线（`--local-drag-parity-test`，1080p 同目标序列）

| 场景 | updates | presented | first_ms | max_resp | max_gap | active_fps | max_missing |
|---|---|---|---|---|---|---|---|
| 正向 15→75 | 60 | 59 | 17 | 26 | 21 | **58.53** | 1 |
| 反向 75→15 | 60 | 57 | 45 | 85 | 45 | **57.00** | 2 |

C 的 70% 门槛：正向 ≥40.97（在线 56.21，**96.0%**）、反向 ≥39.90（在线 56.72，**99.5%**）。

### 8.6 C 为何判 partial

- §3.C 写的是“已缓存区 first_response p95 ≤100ms、max_gap ≤50ms、不允许连续 2 个目标更新没有新画面”。
- 冷启动反向 75→65 的目标帧**已经下载到本地**（同一已缓存区），但预览解码器从关键帧解 76 帧，实测 **first 224ms、max_missing 6**，同时超过 100ms 与“连续 2 个”两条门槛；T8.6.1b 的首次正向也记录 152ms（其自身门槛是 200ms，不等于 C 的 100ms）。
- 之后的正向/反向（同一段缓存）为 17/31ms、33ms 空档、56fps，说明“方向不对称/冻住”已修好；但按任务口径不能把冷启动排除后写“完成”。

### 8.7 回归

| 项 | 结果 |
|---|---|
| `ctest`（手动，TMP/TEMP=`E:\拉片软件开发\.deps\tmp`，Qt bin 在 PATH） | **37/37**，61.21s（clean 构建脚本内 62.14s） |
| `--key-test`（Medium） | **77 项 ok=1 / 0 项 ok=0** |
| 默认路径（Medium，`--online-offline-test --offline-default-paths --offline-keep-cache`） | 默认缓存命中（80-7、`cache_hit=1`、0 playurl/HTTP、首帧 95ms）、默认记录可读（`lapian-online-1`）、默认截图写入；**31/3**（3 项为默认目录 T8.7 遗留 fixture 与探针期望字段不一致，不写 31/0） |
| T8.6.1b `--online-drag-preview-test`（Medium，增长会话） | **35/0 pass**；正向 48 presented/56.60 active/首帧 152ms/UI 62ms，反向 27/30.10/103ms，landing 75 与 45 精确，未缓存提示/恢复/关窗 37ms |
| `--drag-test --drag-samples 20`（1080p） | P4 65.6、P6 p95 **15.9**、P7 0.0/12.6、P8 12.8、P9 33.5；Medium：65.6/**17.7**/12.8/12.4/30.4 |
| `--drag-strategy-test` | 3 次中 2 次 `all_pass=1`；1 次 fast_forward `max_gap=51.5ms`（阈值 50，超 1.5ms）导致 6/7，复跑通过 |
| `--drag-far-test` | 粗帧 13.9/19.8ms → 精确 21.0/29.8ms；release p95 13.3ms；`readbacks=0` |
| P6 松手 | 1080p p95 **15.9/17.7ms** ≤150ms，不退化 |
| 在线预览缓存 | 峰值 **60.75MiB**（≤256MiB）；缩略图 432,000B（≤64MiB）；在线预览软解、readbacks=0 |
| 4K（只记录） | `proxy-V4`：P4 63.5、P6 p95 **144.6**（4K 阈值 300）、P7 2.0/4.7、P9 229.7、预览缓存峰值 248,832,000B（237.3MiB）；**不写 4K 通过** |
| 黑帧/崩溃/设备丢失/表面池 | 本轮所有探针 exit 0、无 error/device-lost/surface 事件；黑帧无埋点，**不写“无黑帧已证明”** |

### 8.8 残余与证据缺陷

1. **首次拖动冷启动**：224ms / 连续 6 个目标无新画面（5s GOP，预览缓存为空）；T8.6.1b 首次正向 152ms。这是 C 判 partial 的唯一原因。
2. **探针字段缺陷**（`OnlineDragPreviewTestRunner`）：
   - `response_p95_ms` 恒为 0：`result.responses` 从未 append，`Percentile(responses,0.95)` 取空向量；本报告用 `max_response_ms=67ms`（p95 的保守上界）替代，不把它当 0ms 通过。
   - `landing_frame` 打印的是 `landing_pts`（`PrintParity` 第 21/22 个 arg 都是 pts）；精确落点以 `landing_frame_ok/landing_pts_ok`、`exact_frame/exact_pts` 与 seek 日志为准。
3. **新缓存根首次写盘**：本轮 A/B/C/T8.6.1b 四次新根运行都出现一次 `cdn_done bytes=0 error=写入缓存文件失败`，随即重试成功（视频/音频各一次）；不影响 A/B/C 判定，但属于首次打开 UX 瑕疵，`DownloadJob`/`CacheStore`/`WinHttpDownloadTransport` 本轮未改，建议另立任务。
4. **未缓存区**只测了“拖入未缓存/拖回恢复/松手精确”，未做长时间等待下载追上的稳定性。
5. **4K/HEVC、1080P60、大会员高码率**只记录，不写通过。
6. **真实窗口的“补音频”提示**未目视；只有单测+代码路径。
7. **默认路径探针 31/3**：Medium 默认 AppData 的缓存命中、记录读取、截图写入都正常，但探针的 `mp4_size_matches`/`record_mark_fields`/`snapshot_index_fields` 与 T8.7 遗留默认记录/截图索引不一致；不写“默认路径全过”。

### 8.9 证据文件

- `docs/perf/online-quality-switch.md` §10（独立复验）
- `docs/perf/online-drag-parity.md` §12（独立复验 + 探针缺陷）
- `docs/handoff-2026-10-25-online-parity.md`
- 原始日志在 `build/verify-*.log`（`build/` 不入库）

---

## 9. P0 首次拖动修复（2026-10-25）

### 9.1 结论

| 层 | 判定 | 依据 |
|---|---|---|
| A 清晰度切换 | **pass** | 未改切档；clean 重建后 `--online-quality-switch-test` **42/0**，qn 80→112→80、codecid=7、PTS/帧号保持、切回 `cache_hit=1`、dash/playurl=0 |
| B 前后一致 | **pass** | 正向/反向 presented **59/59（0.0%）**，fps 58.53–58.65/58.65–58.71，gap 32/33ms，landing 精确 |
| C 丝滑流畅 | **pass（P0 门禁）** | cold 首次拖动 first_publish **26/28ms**、delivered **32/36ms**、response 1ms、max_missing **0/1**；post-seek 后第一次拖动 first_delivered **48/48ms**、response 49ms、max_missing 1；稳态 ≥58.5fps；无连续 2 目标无画面 |
| 总判定 | **pass（P0 范围）** | 残余与证据口径见 §9.6；不能写“所有场景无空档/全绿” |

### 9.2 根因（代码确认）

1. **每次 `Resume` 把发布基准清成 -1**：`OnlinePreviewDecoder::Resume` 设 `last_delivered_frame=-1`，`ShouldPublishRegularLocked` 又在 `last<0` 时拒绝所有普通帧，`TakeLatestFrame` 同样拒绝非精确帧 → 5s GOP 冷启动必须解到精确目标才出第一张画面。
2. **首目标只等精确帧**：没有“当前显示帧”作为 baseline，中间帧无法通过“比上一张更接近目标”的判定。
3. **preview seek 落到前一 GOP**：预览路径用 `VideoDecoder::SeekToPts`（`av_seek_frame`），fMP4 上实测 keyframe=1500 却从 ~1350 解起（早 150 帧），post-seek 首次拖动 1.29–1.50s；主会话已有的 `SeekToKeyframePts`（`avformat_seek_file` 钉 min/ts/max）未用于预览。
4. **seek 后无预热**：`Pause` 关闭 reader/decoder，落地后没有后台预热；下一次拖 ±1 仍从关键帧重解。

### 9.3 实现（按提交）

- `3084e33 diag: add per-drag online preview metrics`：decoder `DragStats`（open/first_publish/published/delivered/last_delivered_before_resume）、MainWindow 每次拖动快照、Runner 每场景/每 T8.6.1b 拖动行、修 `response_p95` 空向量与 `landing_frame` 误印 PTS、新增 post-seek 诊断。
- `c912770 fix: align online preview seek and first-frame publish`：`DecodeToward`/`RunPrefetch` 改用 `SeekToKeyframePts`（失败 fallback + `seek_fallbacks`/landing offset 诊断）；`Begin/Resume` 接收 `displayed_frame` 作为 baseline；删除 `last<0` 硬闸，首张“更近且不越过目标”的中间帧立即发布；`PrimeFrame` 把当前显示帧异步入缓存；未缓存目标发布最近缓存帧且 waiting 保持；`Warm(frame)` 静默解到 `frame+10`、缓存 `[frame-61,frame+10]`、完成后 decoder/reader idle，`Begin(warm_only)` 供无拖动预热；修 open 失败后 Resume/Warm 的 thread 生命周期（join 两个线程）并加回归。
- `35fa8be fix: warm online preview after seek and gate first-frame latency`：MainWindow 拖动传 displayed baseline + PrimeFrame；seek 落地 120ms / 播放暂停 200ms 合并预热（拖动/seek/停止取消，seek 前 Pause reader）；真实 `OnlinePreviewWarmIdleForTest`；cold 与 post-seek 正反门禁；`post_seek_immediate` 只记录诊断。
- **未改**：本地 `PreviewDecoder`/`DragStrategy`、`OnlineSession`、门/阈值、`lapian-cache-1` schema、OB 样本、默认 AVC≤1080p、切档逻辑；未上硬解/回拷。

### 9.4 修复前后数字（before = S1 `p0-before`；after = S4 clean 重建，两轮）

| 场景 | first_publish before → after | first_delivered before → after | first_response before → after | max_missing before → after | fps after |
|---|---|---|---|---|---|
| cold 75→65（首次拖动） | **263 → 26/28ms** | **272 → 32/36ms** | **190 → 1ms** | **5 → 0/1** | 55.9–56.3 |
| post_seek_backward 1575→1565 | **1287 → 0ms** | **1294 → 48ms** | **1204 → 49ms** | **9 → 1** | 29.5（10 目标/300ms） |
| post_seek_forward 1605→1615 | **1500 → 0ms** | **1511 → 48ms** | **1420 → 49ms** | **9 → 1** | 29.5 |
| forward 15→75 | 4 → 9ms | 29 → 17/18ms | 17 → 16/29ms | 1 → 1 | **58.53/58.65** |
| backward 75→15 | 9 → 4/5ms | 29 → 111/112ms* | 17 → 30ms | 1 → 1 | **58.65/58.71** |
| zoom +10 / −10 | 4 → 4/10ms | 29/28 → 16/17ms | 63 → 62ms | 1 → 1 | 16.6 |
| far + / − | 9/5（粗帧）→ 39/22（粗帧） | 44/29 → — | 32/16 → 39/22 | 0 → 0 | exact 1667/137 → **695/119ms** |
| 未缓存 40→45 | 9 → 4ms | 42 → 17ms | 0 → 0（提示） | 0 → 0 | — |

\* backward/forward 等既有场景仍用 `initial_pump_ms=80`（原有口径），`first_delivered` 含 80ms 预热；post/immediate 用 0。不能跨场景直接比较 `first_delivered`，用户可见延迟看 `first_response`。

- 前后成对：presented **59/59（0.0%）**；landing 全部精确（75/15、1565、1615；远跳 3000/15）。
- 预热：post_seek warm **337–386ms**、warm_frames 86/116、`seek_offset=0`、baseline 1575/1605；cold baseline=0（当前显示帧，不是 scrub 起点 75）。
- 缓存峰值 **63,700,992B / 72 帧 = 60.75MiB**（≤64MiB）；UI gap **19–21ms**（≤50）。

### 9.5 门槛对照

| 门槛 | 结果 |
|---|---|
| 每次拖动 first_publish ≤100ms | cold 26/28、post 0、forward 9、backward 4/5、zoom 4/10、far 粗帧 39/22；**P0 门禁场景全 ≤100** |
| 不允许连续 2 个目标无新画面 | cold 0/1、post 1、forward/backward 1、zoom 1 |
| 正反向都测、成对 | 59/59（差 0.0%）；post 正反各 10 目标 |
| 稳态 ≥ 本地 70% | 在线 58.53–58.71 vs 本地 58.42–58.47（forward）/49.55–53.52（backward，机器波动）；均 ≥70% |
| `ctest` | **37/37**（clean 构建脚本内 62.98s） |
| key / T8.6.1b | **77/0** / **35/0** |
| A 回归 | **42/0** |
| 缓存 | 60.75MiB/72 帧；End 后 0（单测） |
| UI gap | 19–21ms |

### 9.6 残余与证据口径（必须同时写）

1. **seek 落地 0ms 立即再拖仍是残余**：`post_seek_immediate` 用 `StopPreview` 丢弃 decoder 后 seek 到 1655 立即拖 1655→1645，`max_response 645–646ms`、`max_gap 640–655ms`（目标 1645 仍需从 K=1500 解 145 帧）。该场景**不门禁**（按用户批准的“预热完成后的每次拖动”口径）；它的 `first_publish=36` 被 baseline/PrimeFrame 帧污染，不能当“立即拖很快”引用。要消除需主 decoder 帧交接或硬解，超出本轮“不先上硬解”。
2. **cold 是合成场景**：屏幕显示 0、ScrubStart=75、目标 75→65；probe 按 backward 记“显示帧增大”为 flicker，实测 flicker 13/14、`ok=0`。cold 门禁只查 first_publish/delivered/response/max_missing（全过），**不能把 cold 当“无闪烁”证据**；真实反向见 post_seek_backward（flicker=0）。
3. **`initial_pump_ms` 口径**：既有场景默认 80ms（让冷 decoder 打开），post/immediate 为 0（decoder 已暖、首目标==显示帧，80ms 内没有目标变化）。按含 80ms 计 post first_delivered 会变 128ms；用户可见的 target→画面延迟是 first_response 49ms。
4. **本地反向波动**：S4 local backward 49.55/53.52fps、gap 68/59ms、max_missing 3/2（§12 为 57.00/45/2），9/0 仍过；本地源码未改，属机器负载/计时波动。
5. **`--drag-strategy-test` flaky**：S4 两次均 6/7，唯一失败 `fast_forward max_gap 54.9/50.8ms`（阈值 50）；本地 `PreviewDecoder`/`DragStrategy` 本轮无 diff，属既有 flake。
6. 新缓存根首包 `bytes=0 写入缓存文件失败` 后重试成功；一次 20s 下载超时后重试成功；S2/S3 各出现一次 `lapian_preview_decoder_test` 0xc0000374（隔离/复跑通过，S4 clean 未出现）；均按残余记录，不写“无崩溃/无错误”。

### 9.7 复验与提交

- clean Release 重建 **232.2s**（`scripts\build-release.ps1 -Clean`，exit 0，脚本内 ctest 37/37）。
- dist **25 文件 / 36,392,264B / 34.71MB**；`lapian.exe` 3,182,592B、SHA256 `D2CDEBDF637F055B1550417DEF5A0FBA17246C9AFFFBBF0ACD5508A6661ACEDB`；Medium 副本 hash 一致。
- 代码提交 `3084e33` / `c912770` / `35fa8be`；证据 [online-drag-parity.md](perf/online-drag-parity.md) §13、[handoff-2026-10-25-p0.md](handoff-2026-10-25-p0.md)；日志 `build\p0-before*.log`、`build\p0-verify-*.log`（`build/` 不入库）。

### 9.8 用户实机反馈（2026-10-25，非量化）

- 用户在使用安装版后反馈：**“现在的流畅度就做的很好了”**（2026-10-25 18:33）。
- 性质：**体感确认，不是量化验收**；未指定样本、方向、exe hash 或具体数值，不能替代 61/0 探针，也不覆盖 4K/60、`post_seek_immediate` 残余。
- 结论：P0 的 1080P30 日常拖动体验已被用户接受；发布收口见 [handoff-2026-10-25-p0-close.md](handoff-2026-10-25-p0-close.md)。
