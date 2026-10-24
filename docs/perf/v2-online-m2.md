# T8.10 证据：DASH 双输入（2026-10-24）

- **任务**：[tasks-v2.md](../../tasks-v2.md) **T8.10**（M2 第 2 项）；依据 [spec-v1.0-delta.md](../spec-v1.0-delta.md) v0.4 的 O2/O3/O4/O6、OP1–OP7、DEC-2/DEC-4，[ADR-007](../adr/ADR-007-bilibili-online.md) D3/D4，[T8.9 证据](v2-online-quality.md)。
- **状态**：**T8.10 完成（不等同 M2 门）**。DASH 视频+音频双输入、共享音频主时钟、seek fan-out、单流 EOF、durl 回退、DASH 缓存命中已实现；**未做** T8.11 渐进索引/波形、T8.12 切档策略/降帧提示、T8.13 故障加固、T8.14 M2 门、T8.15 大会员条件验收、T8.16 打包、T8.6.1b 拖动预览。
- **基线**：`a3cf655` + T8.10 变更；**M1/M2 门、验收数字、OB 样本未改**。
- **一句话**：登录普通用户 3/3 用 `playurl(fnval=4048)` 的 1080P AVC（qn=80）+ AAC（30280）双流播放；首帧 322/369/365ms、60s 音画偏差 p95 17.1/18.2/22.0ms（≤1 帧）、20/20 seek 落点与 PTS 都精确、视频 EOF 正常停住、内存峰值 211.5/209.7/201.7MB、DASH 缓存命中 0 网络 45–46ms；`--force-durl` 回退 28/0。

---

## 1. 先做 spike（3 条真实 OB）

工具：`src/tools/dash_spike/main.cpp`（开发工具，不入分发；控制台）。命令：

```powershell
build\release\bin\lapian_dash_spike.exe --bvid BV1BMnv6jE5S --cid 42915922022 --prefetch-mb 12 --seek-seconds 20
```

| 问题 | 实测（OB-1/2/3） | 结论 |
|---|---|---|
| DASH 总字节 | 主 URL `Range: bytes=0-0` 均 **206 + Content-Range**：视频 31,057,770 / 25,381,371 / 69,759,161；音频 1,294,290 / 1,240,382 / 5,408,198；探测 16–92ms | 用 Range 探测，**不伪造 size**；服务端忽略 Range 时用 Content-Length 兜底 |
| init/index | `0-917`/`918-1245`、`0-996`/`997-1256`、`0-1008`/`1009-1544`；顺序下载 0..total 已覆盖，不需要单独拼接 | 不用单独请求 init/index；总字节探测只看响应头 |
| 两个 GrowingFormat | 视频/音频 `avformat_open_input` 均成功（10ms/0ms）；视频首帧 8–12ms；音频首包 PTS=0；容器 duration 110.466/95.566/212.261s；音频 AAC 48k/44.1k 立体声 | 不 remux 可行 |
| seek fan-out | 视频 `SeekToKeyframePts` 到 15s 落 15.000s；音频 `AudioDecoder::SeekTo` 20s 落 20.0000s | 双流各自 seek 可行 |
| EOF/时钟 | 音频容器时长比视频长 0–62ms；视频 EOF 由 `reachedEnd` 停止，音频随后 EOF，不阻塞 | 单流 EOF 不阻塞 |

**spike 抓到的关键 bug**：fMP4（DASH）的 `av_seek_frame` 会把“正好是关键帧”的时间戳落到**前一个关键帧**（5s GOP 实测早 5s）。`MediaSession::SeekToFrame` 按“从索引关键帧数 N 帧”返回，导致 seek 落点 PTS 比目标早一整个 GOP；帧号仍被写成目标帧，肉眼/旧断言看不出来。修复见 §3.4。

---

## 2. 实现

### 2.1 双输入路径

- `OnlineSession::Start` → 缓存命中（DASH 需要视频+音频两个条目）→ 否则 `StartResolveWorker`：
  1. `playurl(fnval=4048,qn=80,fourk=1)` 拿目录；
  2. `DefaultStream()`（AVC/非手动档）+ `BestAudio()`（最高带宽 AAC）；
  3. 两条流各做 `Range: bytes=0-0` 总字节探测；
  4. 成功 → 两个 `DownloadPlan`/`CacheKey`/`GrowingSource`/`DownloadJob`；失败 → 记录原因并回退 `playurl(fnval=0,qn=80,fourk=0)` 的 durl 单文件（M1 路径）。
- 视频与音频各自 `GrowingSource`、`DownloadJob`、`AVFormatContext`；`defer_finalize` 两个 job 都完成才落盘。
- ready 阈值：视频 300KB、音频 96KB（≈7–8s AAC），两者都到才发 ready，避免首帧后等音轨。
- 音频主时钟（IAudioClock）继续沿用；seek 时暂停视频与音频，落地后 `PlayFrom(当前帧媒体秒)` 让音频从当前帧起。
- `MediaSession::OpenGrowingWithOptions` 只把视频容器作为会话；音频 overlay 提供 `has_audio/stream_index=0/codec=aac`，`av_offset_us = -视频首帧 PTS`（OB-3 首帧 21ms → -21ms；OB-1/2 = 0）。
- `AudioEngine`/`WaveformGenerator` 用音频 `GrowingSource` + `audio-<id>` 路径，stream_index=0。
- `durl` 回退：`--force-durl` 可强制；产品在 DASH 目录/音频/总字节任一失败时自动回退，并记录 `DashFallbackReasonForTest()`。

### 2.2 音频缓存键（T8.9 留给本任务）

`lapian-cache-1` **schema 值不变**，新增可选字段：

| 字段 | 视频条目 | 音频条目 | 旧 meta |
|---|---|---|---|
| 目录 | `<root>\<bvid>\<cid>\<qn>-<codecid>\` | `<root>\<bvid>\<cid>\audio-<audio_id>\` | 同旧 |
| `kind` | 不写（缺省 video） | `"audio"` | 缺省 video |
| `audio_id` | 配套音频 id（DASH 时写；durl 不写） | 音频 id | 无 |
| `container` | `"dash"`（DASH 视频单流）；durl 不写 | 不写 | 无 |

- `CacheKey` 增 `kind`（默认 video）+ `audio_id`（默认 0）；`IsValidCacheKey`：音频只接受 `audio_id>0, qn=0, codecid=0`，视频拒绝 `audio_id>0`。
- `EntryDir`/`CacheKeyLabel`/`Scan` 认识 `audio-<id>`；旧目录/旧 meta 完全可读、可清、可重建。
- 当前会话保护：`MatchesProtectedKey` 在 keep 是视频键时同时保护同 bvid+cid 的音频条目；`ClearAll`/`CleanupOrphans`/`EvictToQuota` 都覆盖（`--online-cache-manage-test` 也加了音频保留用例）。
- 缓存命中要求视频+音频都 complete；缺音频时明确报“需要清空该视频缓存后重下”，不静默无声播放、不偷偷联网。
- DASH 缓存命中仍用两个 `GrowingSource`（完整文件 `data.mp4` 全部 Publish），复用同一双输入读路径，0 playurl/CDN/`.part`。

### 2.3 关键代码

| 文件 | 内容 |
|---|---|
| `src/app/OnlineSession.*` | resolve（DASH→总字节→回退 durl）、双 plan/source/job、双 ready、双 finalize、DASH 缓存命中、refresh 按流重取 |
| `src/online/CacheStore.*` | `CacheStreamKind`、`CacheKey.kind/audio_id`、`audio-<id>` 目录、meta kind/audio_id/container、保护/扫描/清理 |
| `src/online/DownloadTransport.h` / `WinHttpDownloadTransport.cpp` | `DownloadRequest.range_end`（`bytes=0-0` 探测；默认 -1 保持 T8.5 行为） |
| `src/media/MediaSession.*` | `GrowingOpenOptions`（dash/fps/duration/audio overlay）、`OpenGrowingWithOptions`、`av_offset_us`、seek PTS 校验 |
| `src/media/VideoDecoder.*` | `SeekToKeyframePts`（`avformat_seek_file` min=ts=max） |
| `src/app/MainWindow.*` | DASH ready 打开、音频/波形独立源、seek 重建带 overlay、DASH cache-hit、M2 hooks |
| `src/app/OnlineM2TestRunner.*` | `--online-m2-test` 真实窗口验收 |
| `src/app/main.cpp` | `--online-m2-test` / `--force-durl` / `--m2-*`；M1 回归可强制 durl |

### 2.4 seek 落点修复（核心）

- 根因：fMP4 的 `av_seek_frame` 落在目标关键帧**前一个** GOP；`SeekToFrame` 从索引关键帧数 `frame-keyframe` 帧，于是返回早一个 GOP 的 PTS（OB-1 frame 3311 期望 110.366688s，实际 105.366688s；frame 1103 期望 36.766688s，实际 31.766688s），帧号却写成目标帧。
- 修复：`VideoDecoder::SeekToKeyframePts` 用 `avformat_seek_file(stream, keyframe_pts, keyframe_pts, keyframe_pts, BACKWARD)`；`SeekToFrame` 优先用它，失败退回 `SeekToPts`；`decode_range` 在返回目标帧前校验 `|info.pts_us - index.PtsOfFrame(frame)| ≤ 1s`，不一致就失败走从 0 解码，绝不把错误帧当目标帧。
- 验证：`--online-m2-test` 20/20 seek **PTS exact**；点尾 seek `expected=110366688 actual=110366688`；EOF 帧号 3313（不再漂到 3463）；`ctest` 32/32、本地 P7 0.85/0.35ms、key-test 77/0。

---

## 3. 验收（真实 3 条 OB，登录普通用户 `vip_status=0`）

命令（每条一份独立测试根）：

```powershell
build\release\bin\lapian.exe --online-m2-test `
  --bvid <BV> --cid <cid> --tag t8.10-final-<ob> --timeout-ms 30000 `
  --online-cache-root build\t8.10-final-<ob>\cache `
  --online-pictures build\t8.10-final-<ob>\pics `
  --online-records-root build\t8.10-final-<ob>\records
```

### 3.1 3/3 首帧/播放

| 样本 | 首帧 ready | qn | 视频流 fps（API/容器） | 音频 | 视频总字节 | 音频总字节 | 索引帧数 |
|---|---|---|---|---|---|---|---|
| OB-1 | **322.2ms** | 80 | **30.000 / 30** | 30280 AAC | 31,057,770 | 1,294,290 | 3314 |
| OB-2 | **369.0ms** | 80 | **29.412 / 30**（API 与容器不同，见 §5） | 30280 AAC | 25,381,371 | 1,240,382 | 2867 |
| OB-3 | **365.1ms** | 80 | **25.000 / 25** | 30280 AAC | 69,759,161 | 5,408,198 | 5306 |

- 3/3 `dash_dual=1`、`qn=80`、音频 id/字节均有效；`playurl_requests=0`（DASH 路径不请求 durl）。
- 默认 1080P AVC；60fps/4K 仍未出现在普通用户目录（T8.9 结论未变）。

### 3.2 60s 音画偏差（≤1 帧）

口径：每 ~20ms 采样 `IAudioClock` 与当前显示帧 PTS（帧中心 = PTS + 半帧）；`drift = audio_clock − frame_center`。音频主时钟驱动视频帧选择。

| 样本 | n | p50 | p95 | max | 判定 |
|---|---|---|---|---|---|
| OB-1 | 2844 | 8.7ms | **17.1ms** | 25.4ms | p95 0.51 帧，max 0.76 帧 |
| OB-2 | 2836 | 8.0ms | **18.2ms** | 26.0ms | p95 0.54 帧，max 0.76 帧 |
| OB-3 | 2850 | 9.0ms | **22.0ms** | 27.5ms | p95 0.55 帧，max 0.69 帧 |

3/3 `av_drift_p95_le_1frame` 与 `av_drift_max_le_1_5frame` 均通过；未见随时间累积（60s 内 p50 稳定）。OB-3 视频首帧 21ms 的 `av_offset_us=-21000` 已生效，没有出现“音频早 21ms 逐渐漂移”。

### 3.3 20 次已缓存 seek（落点 0）

目标随机分布在已缓存区，**只取距前关键帧 ≤2s**（P9 口径；DASH 1080P GOP=5s，长 GOP 不在本门槛）。

| 样本 | landing failures | PTS failures | p50 | p95 | max | 音频从当前帧起 failures |
|---|---|---|---|---|---|---|
| OB-1 | **0/20** | **0/20** | 77.3ms | 129.9ms | 170.3ms | 0/20 |
| OB-2 | **0/20** | **0/20** | 77.9ms | 110.3ms | 124.8ms | 0/20 |
| OB-3 | **0/20** | **0/20** | 76.5ms | 105.0ms | 137.4ms | 0/20 |

- landing = `当前帧号 == 目标帧号`；PTS = `|当前 PTS − 索引 PTS| ≤1ms`。
- 每次 seek 后播放 250ms，`AudioClock ≈ 目标 PTS + 0.25s`（容差 0.55s），验证不是旧位置/从头播。
- 5s GOP 点尾（距关键帧 149 帧）seek 约 260ms；**不属于 P9 ≤2s 口径**，记录不判失败。

### 3.4 单流 EOF

跳到 `last-2` 帧播放到结尾：3/3 在 ≤6s 内 `OnlinePlaying=false`，帧号停在末帧（OB-1 3313、OB-2 2866、OB-3 5305），PTS 与索引一致；未出现音频仍在播放导致播放器不结束/死锁。

### 3.5 内存 / 关窗 / 缓存命中

| 样本 | 峰值 working set | 关窗 | 缓存命中 ready | 0 网络（playurl/DASH/quality API） |
|---|---|---|---|---|
| OB-1 | **211.5MB** | 22ms | **46.2ms** | 0/0/0 |
| OB-2 | **209.7MB** | 23ms | **45.2ms** | 0/0/0 |
| OB-3 | **201.7MB** | 21ms | **45.0ms** | 0/0/0 |

- OP7 在线 1080p 内存 ≤800MB：3/3 通过（余量 >588MB）。
- 缓存命中仍是 DASH 双输入（`dash=1`、qn=80），两个完整 `data.mp4` 通过 `GrowingSource` 全量 Publish；0 playurl/CDN/`.part`。

### 3.6 durl 回退（M1 不退化）

- `--online-m2-test --force-durl`（OB-1）：8/0，`dash=0`、qn=64、首帧 669ms、播放推进；`playurl_requests=1`。
- `--online-m1-test --force-durl`（OB-1）：**28/0**；首帧 619ms、P7 p95 25ms、已缓存 seek p95 43ms、UI gap 57ms、未缓存提示检查保留。
- 不强制 durl 时的 `--online-m1-test`（DASH 路径）：**25/0**；首帧 330ms、P7 p95 68ms、已缓存 seek p95 95ms、UI gap 102ms；3 个“未缓存”检查因 31MB 在 2.7s 内下完、已无未缓存区而显式跳过（不是失败）。

---

## 4. 回归

| 项 | 结果 |
|---|---|
| `ctest` | **32/32**（31.80s） |
| `lapian_online_cache_test` | **396/0**（新增音频键/目录/meta/清空保护/配额保护/坏 kind 用例） |
| `lapian_online_cache_manage_test` | 通过（新增当前会话音频保留） |
| `--online-cache-manage-test` | workspace **72/0**、`--ui` **98/0** |
| `--online-quality-test` 3 条 | 各 27/0（总 39/0），3/3 pass；菜单文案改为“T8.12 切档”（不再写 T8.10 未接入） |
| `--online-quality-test --ui` | **21/0**；当前 DASH 1080P 勾选，其它档置灰写明 T8.12 |
| 本地 `--key-test` | **77/0**（`media\测试.mp4`；proxy-V1 只有 719 帧，不满足 `last-index>1000`，不是回归） |
| 本地 P1 | 291.41 / 293.89 / 293.74ms |
| 本地 P2 | 371.70 / 350.17 / 341.10ms |
| 本地 P7 step | 后退 p95 **0.85ms**、前进 **0.35ms**；内存 272.8MB |
| `dist` | 未重新打包；仍 34.02MB/25 文件（打包属 T8.16） |

---

## 5. 已知缺口 / 未验证

1. **OB-2 的 API fps 与容器/索引 fps 不一致**：API `frameRate=29.412`，fMP4 容器 `avg_frame_rate=30/1`，索引 2867 帧 / 95.566s = **30.0fps**。T8.10 记录 API 值用于展示/证据，帧号数学用容器值（否则总帧数会少 56 帧）。T8.12/DEC-4 需要明确“实际 fps”口径。
2. **DASH 无 size、只探测主 URL**：3/3 主 URL `206` 成功；备份 URL 的 Range 总字节未逐条测（T8.13 故障切换时再验）。服务端忽略 Range 的 Content-Length 兜底只有离线/合成路径，没有真实 CDN 反例。
3. **缓存命中要求视频+音频都完整**：目前 LRU 是条目级，理论上可能只淘汰音频；此时 T8.10 明确报“缓存不完整，请清空该视频缓存后重下”，不自动补下音频（T8.13 加固）。
4. **多音轨/杜比/FLAC 未做**：只选 `BestAudio()` 一条 AAC；`dash.dolby`/`flac` 未解析。
5. **无音轨的 DASH-only 样本未验**：目录无音频时回退 durl；若该稿件也没有 durl，则失败。OB 样本 3/3 都有音频/ durl。
6. **未缓存 seek 仍按 O4 等待**：T8.10 只验已缓存区落点；未缓存区提示/取消/落点由既有 T8.6 路径覆盖，未在 5s GOP 上重跑 20 次。
7. **60s 音画偏差是应用内时钟测量**：不是外部录屏/专业同步仪；p95/max 已记录，不能外推为“任意片源/任意声卡 ≤1 帧”。
8. **默认 AppData / Medium 副本未在 T8.10 重跑**：所有 M2 运行使用工作区显式测试根（Low integrity 可写）；T8.16 安装版默认路径才验证 `%LOCALAPPDATA%`。
9. **仅 OB-1/2/3、普通用户**：大会员 1080P60/4K 仍未验（T8.15）；`--online-m2-test` 为 GUI 探针，无无 GUI 版本。
10. **T8.11 未做**：索引/波形仍是“缓存增长后重扫”的 T8.6 策略；60s 测试里下载很快完成，未覆盖“边下边索引”的极端慢网。

---

## 6. 复现（最短）

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure

build\release\bin\lapian.exe --online-m2-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --tag t8.10-final-ob1 --timeout-ms 30000 `
  --online-cache-root build\t8.10-final-ob1\cache `
  --online-pictures build\t8.10-final-ob1\pics `
  --online-records-root build\t8.10-final-ob1\records
# OB-2: BV1mA411m7pS / 1004967230；OB-3: BV1GJ411x7h7 / 137649199

# 回退
build\release\bin\lapian.exe --online-m2-test --force-durl --bvid BV1BMnv6jE5S --cid 42915922022 ...
build\release\bin\lapian.exe --online-m1-test --force-durl --bvid BV1BMnv6jE5S --cid 42915922022 ...
```

`--m2-play-seconds` / `--m2-seeks` 可缩短 smoke（正式数字用默认 60/20）；`--m2-cache-hit-only` 只验已有完整缓存的 0 网络/首帧。
