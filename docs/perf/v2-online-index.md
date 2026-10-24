# T8.11 证据：渐进索引 / 波形 / 总帧数（2026-10-24）

- **任务**：[tasks-v2.md](../../tasks-v2.md) **T8.11**（M2 第 3 项）；依据 [spec-v1.0-delta.md](../spec-v1.0-delta.md) v0.4 的 O3/O4、OP2/OP3/OP4、DEC-2，[ADR-007](../adr/ADR-007-bilibili-online.md) D3/D4，[T8.10 证据](v2-online-m2.md)、[T8.6.1a 交接](../handoff-2026-10-24-t8.6.1a.md)。
- **状态**：**T8.11 完成（不等同 M2 门）**。增量索引、三态总帧数、波形渐进可见、慢增长测试钩子与 `--online-index-test` 已实现；**未做** T8.12 切档/降帧提示、T8.13 加固、T8.14 M2 门、T8.15 大会员条件验收、T8.16 打包、T8.6.1b 拖动预览。
- **基线**：`1821d82`（T8.10 代码 `b560607`）；**M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema 未改**。
- **一句话**：真实 OB-3（1080P25、212.261s、5306 帧）在视频 2MB/s 受控慢增长下，稳定索引 6.2s 覆盖 30s 内容、20/20 已缓存 seek 落点 0、完成后精确 5306 帧、波形偏移 0.525 帧、UI 最大停顿 58ms；增量索引全程 `opens=1/reopens=0`、累计读字节 = 文件大小（`read_ratio=1.000`），不再每 650ms 从头全扫。

---

## 1. T8.10 现状与 T8.11 目标

T8.10 的产品路径是 `MainWindow::StartOnlineIndexBuild` 每 650ms 调一次
`BuildOnlineIndex(source, part, stream, 48, cancel)`；后者**每次从文件起点**打开
`GrowingFormat`、demux 到当前 `Published()` 前沿，再整体 `BuildFromEntries`。
后果：

- 已缓存前缀越长，单次重建越贵；慢网下“每次增长”都重复读前缀，CPU/I/O 随
  时间平方增长（按 650ms 周期，累计读字节 ≈ Σ published(t)）。
- 已暴露前缀的 PTS 校验虽然能防止帧号漂移，但索引进度只能整段替换。
- 总帧数在缓存完成前一直显示“扫描中”，没有“估算”态。
- 波形生成本身已是单线程顺序追加（不重解），但时间轴只在其它事件时重绘，
  渐进过程不可见。

T8.11 的目标：

1. 只 demux 已发布前缀；缓存增长时**增量推进**，不从头全扫；
2. 稳定前缀（tail guard 48 帧）单调增长，已暴露前缀 PTS 不变；
3. 总帧数三态：扫描中 / 估算 / 精确；缓存完成（读到 EOF）后精确；
4. 波形只追加 bucket，生成中可见，偏移 ≤1 帧；
5. 索引/波形/安装都不阻塞 UI >200ms；未缓存 seek 仍显示“缓冲中（目标帧 N）”。

---

## 2. 实现

### 2.1 `IncrementalOnlineIndexBuilder`

`src/media/OnlineIndexBuilder.h/.cpp` 新增增量 builder；`BuildOnlineIndex` 改为
一次性包装（旧测试/探针路径不变）。

- 首次 `EnsureOpen` 打开独立 `GrowingFormat`（100ms 前沿超时），保留
  `AVFormatContext`/parser/packet；后续 `Step()` 在同一上下文里继续读。
- **前沿 guard**：读之前检查 `Published() - cursor->Pos() < 256KB` 就停在
  frontier，避免在未发布区把 FFmpeg 的 sample 状态打脏；正常慢增长下不会
  撞 `ETIMEDOUT`，上下文可一直续用。
- **兜底重开**：若大 sample 跨过 guard 仍读到 `AVERROR(ETIMEDOUT)`，记下
  `resume_pos = 最后成功包 pos+size`、`last_pts`，关闭上下文；下一次 `Step`
  重开并 `avformat_seek_file` 到 `last_pts` 之前的关键帧，跳过 `pos < resume_pos`
  的已解析包，只追加新包。`resume_skips`/`reopens` 可观测。
- `Snapshot()` 才排序/去重并构造 `FrameIndex`；`stable_frames` 对未完成源保留
  tail guard 48 帧，完成源暴露全部帧；`stable` 单调不回退。
- `OnlineIndexStats`：`steps/opens/reopens/resume_skips/packets/parsed_frames/
  read_bytes/max_step_ms`，用于证明没有全量重扫。
- 只处理选定的视频流；音频/波形不经过本类。

### 2.2 MainWindow 安装策略

- 索引线程循环 `Step(40ms/8192 包)`；只有 `total_frames` 变化且距上次安装
  ≥250ms、或 `complete` 时才 `Snapshot` 并经 `QMetaObject::invokeMethod`
  回 GUI 线程。
- `InstallOnlineIndexSnapshot`：先做已暴露前缀 PTS 比对（不同则不安装），
  再替换 `FrameIndex`/`spans`，`ApplyIndexState`/`ReconcileOnlineMarks`。
- seek worker 正在读同一个 `FrameIndex` 时，快照存到 `online_index_pending_`；
  seek 成功/失败/取消/启动失败后调用 `InstallPendingOnlineIndex()` 装最新快照，
  避免 `complete` 快照被丢掉。
- 总帧数三态：`ApplyIndexState` 在在线增长会话中
  - 完成 → `Exact(stable_frames)`；
  - 未完成但容器 duration×fps 有值 → `Estimated(container)`（UI “≈N”）；
  - 否则 → `Scanning()`。
  不用 API fps 或部分索引冒充精确总帧数。
- 波形：保留 `WaveformGenerator::StartGrowing` 的单线程顺序解码（只追加
  bucket，不重解）；`PollWaveformStatus` 在 bucket 数增长/就绪时
  `timeline_->update()`，让渐进可见。

### 2.3 慢增长测试钩子

- `DownloadOptions.throttle_bytes_per_sec`（默认 0，不改产品下载）：在
  `WritePart` 写盘后、`Publish` 前按速率节流，`OnlineSession::SetDownloadThrottleForTest`
  只对 DASH 视频生效（音频保持正常速度；波形渐进由离线 case 覆盖）。
- `--online-index-test`：
  - 真实在线：OB-3 → 首帧 → 30s 内容索引 → 20 次已缓存 seek → 未缓存提示/取消 →
    完成精确总帧数 → 波形/UI gap/关窗；
  - `--index-offline-video/--index-offline-audio --index-throttle-bps`：本地缓存
    文件按 1–2MB/s `Publish`，验证增量边界/前缀稳定/完成精确/波形只追加。

---

## 3. 单测：真实 OB-3 受控增长（28/0）

命令：

```powershell
build\release\Release\lapian_online_index_test.exe
```

素材：`build\t8.10-final-ob3\cache\BV1GJ411x7h7\137649199\80-7\data.mp4`
（1920×1080、25fps、212.261s、69,759,161B、5306 帧）+ 独立 AAC
`audio-30280\data.mp4`（5,408,198B）。每 256KB `Publish` 后推一次 `Step`，
等价于 1–2MB/s 的慢增长节奏。

| 项 | 结果 |
|---|---|
| 完整一次性索引 | 5306 帧，`complete=1` |
| 增量快照 | 265 个；`stable` 单调、已暴露前缀 PTS 变化 0 |
| tail guard | 未完成时 `stable == total - 48`；完成时 5306 |
| 30s 内容索引 | `stable=765`（expected 750）时 published 12,845,056B；读取落后 196,608B |
| 完成精确 | 5306/5306 |
| 重复 PTS | 0 |
| 累计读字节 | 69,759,161B / 文件 69,759,161B = **1.000**；`opens=1`、`reopens=0` |
| 单步最大耗时 | **5.71ms** |
| 波形渐进 | 41 个不同 bucket 计数、40 个中间态；42,462 buckets；早期前缀逐字节不变 |
| 波形偏移 | `offset=-0.021s`（视频首帧 21ms），25fps 下 **0.525 帧**；完成 bucket 42,462 vs 预期 42,461 |
| 波形末端对齐 | `42462/200 + (-0.021) = 212.289s` vs 音频容器 212.3088s，差 **0.020s = 0.50 帧**（≤1） |

`ctest` 中该测试 0.8s，返回 77（SKIP）仅当素材缺失。

---

## 4. 离线 1–2MB/s 慢增长（22/0）

命令（受控 Publish 2MB/s）：

```powershell
build\release\bin\lapian.exe --online-index-test `
  --index-offline-video build\t8.10-final-ob3\cache\BV1GJ411x7h7\137649199\80-7\data.mp4 `
  --index-offline-audio build\t8.10-final-ob3\cache\BV1GJ411x7h7\137649199\audio-30280\data.mp4 `
  --index-throttle-bps 2000000 `
  --tag t8.11-offline-ob3 `
  --online-cache-root build\t8.11-offline\cache --online-pictures build\t8.11-offline\pics
```

| 项 | 结果 |
|---|---|
| 首次索引可用 | **115ms**（stable>0） |
| 30s 内容索引 | **6.084s**；此时 published 12,691,447/69,759,161B（18.2%）；`stable=752`（expected 750）；读取落后 239,607B |
| 快照/步数 | 1054 snapshots / 2246 steps；`reopens=0`、`resume_skips=0` |
| 完成精确 | 5306/5306；stable 单调、前缀 PTS 变化 0 |
| 累计读字节 | 69,759,161B / 文件大小 = **1.000** |
| 单步最大耗时 | **5.36ms** |
| 旧路径对照 | 30s 前缀单次 `BuildOnlineIndex` **166ms / 769 帧**（含 100ms 前沿超时）；增量同一时刻单步 max **5.36ms** |
| 波形渐进 | 83 个采样、82 个中间态；42,462 buckets；早期 2000 桶不变；`offset=-0.021s / 0.525 帧`；末端 212.289s vs 212.3088s（0.50 帧）；状态 ready |

**结论**：在 1–2MB/s 的受控增长下，索引在 30s 内容处已覆盖（尾部仅落后
~230KB，低于 256KB guard + 64KB AVIO 预读），且读字节严格等于文件一次；
旧路径同一前缀单次重建 ~166ms（650ms 周期约 26% 时间在重扫），新路径
单步 max ~5ms；不存在“每 650ms 从头全扫”。

---

## 5. 真实在线 OB-3（27/0）

命令（视频 2MB/s 节流，音频正常；真实登录/取流/缓存）：

```powershell
build\release\bin\lapian.exe --online-index-test `
  --bvid BV1GJ411x7h7 --cid 137649199 --tag t8.11-ob3 `
  --timeout-ms 120000 --index-throttle-bps 2000000 `
  --online-cache-root build\t8.11-ob3\cache `
  --online-pictures build\t8.11-ob3\pics `
  --online-records-root build\t8.11-ob3\records
```

| 项 | 结果 |
|---|---|
| 实际样本 | DASH 1080P AVC qn=80 + AAC 30280；1920×1080、**25.000fps**、容器 212.261s、视频 69,759,161B、音频 5,408,198B |
| 首帧 | **493.1ms**（冷缓存） |
| 首次索引可用 | 就绪后 **128ms**（首个快照已含 stable>0） |
| 30s 内容索引 | **6.209s**；published 13,107,200B（视频 18.8%）；`stable=780`（expected 750）；总帧数 `estimate:5307` |
| 已缓存 seek | 20/20 落点 0；p50/p95 未逐项打印，p95 **56.2ms**（≤200ms） |
| 未缓存 seek | 目标帧 1138：提示 **0ms**（“缓冲中（目标帧 1138）”）；改回已缓存帧取消 **26ms**，落点 541 精确 |
| 完成后总帧数 | `exact:5306`；索引 5306；容器估算 5307（212.261×25 四舍五入差 1 帧） |
| 增量统计 | `steps=285`、`opens=1`、`reopens=0`、`skips=0`、`read_bytes=69,759,161`、`read_ratio=1.000`、`max_step_ms=10.70` |
| 波形 | ready；42,462 buckets（预期≈42,452）；偏移 **0.525 帧** |
| UI 停顿 | 最大 **58ms**（>200ms 事件 0） |
| 关窗 | **21ms** |

### 5.1 三态总帧数

| 阶段 | 显示 | 依据 |
|---|---|---|
| 首帧前/容器未知 | `scanning` / “扫描中” | 无 duration/fps |
| 增长中（OB-3） | `estimate:5307` / “≈5307” | 容器 `duration×fps`；不是 API fps |
| 读完 EOF | `exact:5306` / “5306” | 增量索引实际帧数 |

OB-2 的 API fps=29.412 与容器 30fps 不一致时，帧号/总帧数仍只用容器值；
完成后 exact 以索引为准（T8.10 §4.7 口径未改）。

---

## 6. 回归

| 项 | 结果 |
|---|---|
| `ctest` | **33/33**（新增 `lapian_online_index_test`） |
| `lapian_online_index_test` | **28/0**（0.8s） |
| `--online-m2-test` OB-1/2/3 | **3/3 34/0**：首帧 361.9/357.6/306.9ms、qn=80、60s 音画偏差 p95 17.0/18.0/20.1ms、20/20 seek 0 失败（p95 110.3/120.5/93.0ms）、EOF 3313/2866/5305、内存峰值 209.4/206.1/207.5MB、缓存命中 31ms/0 网络、关窗 21–23ms |
| `--online-m1-test --force-durl` | **28/0**；首帧/播放、P7 p95 25ms、已缓存 seek p95 51ms、未缓存提示 0ms、UI gap 52ms、关窗 24ms |
| `--online-quality-test` | 3×27/0（OB-1/2/3）；`--ui` **21/0**（OB-3；档位只有 80/64/32/16，无 4K） |
| `lapian_online_cache_test` | **396/0** |
| 缓存管理 workspace / `--ui` | **75/0** / **98/0** |
| 本地 key-test | **77/0** |
| 本地 P1/P2 | 276.98–299.32ms / 322.65–327.44ms（各 3 次 spot） |
| 本地 P7 step（40 步） | 后退 p95 **0.85ms**、前进 p95 **0.45ms**；max 57.65/11.44ms；内存 268.9MB |
| 4K / 1080P60 / 大会员 | **未验证/不可得**（T8.15；不写 MVP 通过） |
| M2 门 | **未判**；T8.14 |

---

## 7. 关键决定与解释

1. **增量续读，不每 650ms 重开从头扫**：保留 demux 上下文 + 256KB 前沿 guard；
   正常慢增长只在同一上下文里读新增字节。实测 `opens=1`、`read_ratio=1.000`。
2. **guard 先停，不把 FFmpeg sample 状态打脏**：`ETIMEDOUT` 只作为兜底；
   真发生时用 `resume_pos` 重开 + 跳包，保证不重复、不丢帧。
3. **快照只在 GUI 线程小步安装**：250ms 节流；seek 期间挂起，结束后补装，
   `complete` 不丢。UI 最大停顿 58ms。
4. **30s 覆盖用“索引稳定帧数”而不是字节比例**：视频 VBR，字节/总字节不代表
   时间；30s 内容实际约 12.7MB（OB-3 前段码率较高），索引在 published
   12.66MB 时已覆盖 31s 画面，读取只落后 ~200KB。
5. **三态只信容器/索引**：估算来自容器 duration×fps；完成后 exact 来自索引；
   OB-3 estimate 5307 → exact 5306 的 1 帧差是 212.261s 四舍五入，不掩盖。
6. **波形只追加**：`StartGrowing` 单线程顺序解码，bucket 前缀不变；UI 在
   bucket 增长时重绘。没有“每次增长重解音频”。
7. **测试节流只用于视频**：音频不节流，避免慢回调下 CDN 音频首两个尝试的
   写失败噪音；波形渐进由离线 2MB/s 音频 publisher 覆盖。

---

## 8. 缺口 / 未验证

1. **3min 1080P30 没有直接样本**：OB-3 是 1080P**25fps**、212.261s；本文件
   不把 25 写成 30。1080P30 仅在既有 T8.10/M2 的 30fps 样本（OB-1/OB-2）上
   间接覆盖；未做 3min 30fps 的专项索引计时。
2. **波形偏移是 `av_offset` 口径**：OB-3 = 21ms = 0.525 帧（≤1），但没有
   click 真值做像素级对齐；V9 click ≤1 帧是 T3.6 的既有证据。
3. **慢增长是测试钩子/离线 publisher**：真实 2MB/s 网络未长期跑；`--online-index-test`
   仍是 GUI 探针，没有 headless 版本。
4. **音频节流未在线验证**：在线只节流视频；音频波形渐进只在离线 case 看
   bucket 增长/前缀不变。
5. **已缓存 seek 目标**仍限制在稳定前缀、距关键帧 ≤2s；5s GOP 点尾/任意位置
   的 200ms 承诺不在本任务。
6. **默认 AppData/Medium 未重跑**；测试根在工作区（Low integrity），T8.16
   安装版再验。
7. **4K/1080P60/大会员未验证**（T8.15）；不写通过。
8. **h264 “missing picture”日志**：在线探针 stderr 有大量 FFmpeg 警告（不影响
   落点/总帧数/UI 数字）；未在本任务定性，留 T8.13 排查是否与 seek/解码路径有关。

---

## 9. 复现（最短）

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure

# 离线 1–2MB/s 慢增长（无网络）
build\release\bin\lapian.exe --online-index-test `
  --index-offline-video build\t8.10-final-ob3\cache\BV1GJ411x7h7\137649199\80-7\data.mp4 `
  --index-offline-audio build\t8.10-final-ob3\cache\BV1GJ411x7h7\137649199\audio-30280\data.mp4 `
  --index-throttle-bps 2000000 --tag t8.11-offline `
  --online-cache-root build\t8.11-offline\cache --online-pictures build\t8.11-offline\pics

# 真实 OB-3（需要 .secrets；视频 2MB/s 节流）
build\release\bin\lapian.exe --online-index-test --bvid BV1GJ411x7h7 --cid 137649199 `
  --tag t8.11-ob3 --timeout-ms 120000 --index-throttle-bps 2000000 `
  --online-cache-root build\t8.11-ob3\cache --online-pictures build\t8.11-ob3\pics `
  --online-records-root build\t8.11-ob3\records
```

---

## 10. 结论

T8.11 范围内完成：增量索引（只扫已发布前缀、`opens=1/reopens=0`、读字节
=文件一次）、三态总帧数（estimate:5307 → exact:5306）、波形渐进可见且
只追加（偏移 0.525 帧）、seek 期间快照挂起补装、未缓存提示保留、UI 停顿
58ms。OB-3 真实在线 27/0、离线慢增长 22/0、单测 28/0、`ctest` 33/33；
M2 回归 3/3 34/0、M1 `--force-durl` 28/0。**未判 M2；M1/M2 门、数字、OB 样本、
缓存 schema 未改。**
