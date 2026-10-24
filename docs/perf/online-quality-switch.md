# T8.17 证据：在线清晰度切换修复（2026-10-25）

- **任务**：[tasks-online-parity-fix.md](../tasks-online-parity-fix.md) §3.A（本次只做“清晰度切换”，不做在线拖动）。
- **状态**：**A 层完成（独立提交）**；任务总判 **partial**（C 首次拖动冷启动未达门槛，见任务文档 §8）。默认 1080P（qn=80）→ 大会员 1080P+ 高码率（qn=112）→ 切回 1080P30（qn=80）真实链路通过；不再出现“DASH 条目已完整缓存，但本次未走缓存命中路径”。
- **基线**：`f696efc`；工作区干净。**未改** M1/M2/P4/P6/P7、OB 样本、`lapian-cache-1` schema、默认 AVC≤1080p、在线/本地拖动预览。
- **一句话**：切档前按**解析出的实际 video/audio key** 精确检查缓存；两条都完整 → cache-hit；只缺音频/只缺视频 → 复用完整一路、只补缺失一路；`DownloadJob::Start` 的 `kCompleted` 只作内部防御；请求 codecid 不同时必须下载该 codec，不能静默复用旧 codec。

---

## 1. 复现（修复前）

### 1.1 环境与命令

- **Medium 副本**：任务给定的 `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe` 当时被一个已运行实例占用（PID 46232，2026-10-24 23:53 启动，无命令行参数），无法覆盖 exe。为不改动用户正在使用的进程，本轮所有真实网络/Medium 证据使用同目录完整安装副本：
  `%LOCALAPPDATA%\Programs\lapian-t817\lapian.exe`（从同一安装目录复制，integrity=Medium）。**最终安装位置未替换**，见 §8。
- **凭据**：默认 DPAPI 凭据，只读；`nav` 返回 `login=1`、`vip_status=1`。未打印/落盘 SESSDATA、Cookie、签名 URL。
- **样本**：OB-1 `BV1BMnv6jE5S` / cid `42915922022`（111s，DASH 1080P AVC 30.000fps）。
- **探针（本轮新增）**：`--online-quality-switch-test`，只走产品入口 `MainWindow::StartOnlineSession` / `TriggerOnlineQualityEntryForTest`，显式独立缓存/截图/记录根。

```powershell
$exe = "$env:LOCALAPPDATA\Programs\lapian-t817\lapian.exe"
$root = "E:\拉片软件开发\build\t8.17-repro"
& $exe --online-quality-switch-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --tag t8.17-repro --timeout-ms 60000 `
  --quality-switch-high 112 --quality-switch-back 80 --quality-switch-wait-ms 120000 `
  --online-cache-root "$root\cache" --online-pictures "$root\pics" `
  --online-records-root "$root\records" --settings-path "$root\settings"
```

### 1.2 修复前实际键与缓存条目

默认打开（`requested_qn=0`，自动）：

```text
quality_switch_session phase=initial qn=80 codecid=7 fps=30.000 cache_hit=0 dash=1
quality_switch_cache phase=initial rel=BV1BMnv6jE5S/42915922022/80-7
  kind=video qn=80 codecid=7 audio_id=30280 container=dash complete=0 bytes=31057770
quality_switch_cache phase=initial rel=BV1BMnv6jE5S/42915922022/audio-30280
  kind=audio qn=0 codecid=0 audio_id=30280 complete=0 bytes=1294290
```

- 播放中两路仍是 `.part`（`complete=0` 是预期的“已发布未 finalize”）；切档关闭 reader 后旧档才 finalize。
- 切到 112 前：第 30 帧、PTS `1,000,000`、标记 1、在线记录 1、截图 1；初始会话 `dash_requests=1`、`playurl_requests=0`。

修复前点击 112（大会员 1080P+ 高码率，AVC 3,415,921bps）后：

```text
T8.17 online_cache_lookup begin ... requested_qn=112 requested_codecid=7 entries=2 broken=0
T8.17 online_cache_lookup skip key=.../80-7 reason=qn_mismatch requested_qn=112
T8.17 online_cache_lookup skip key=.../audio-30280 reason=not_complete_video
T8.17 online_cache_lookup miss reason=no_matching_video requested_qn=112 requested_codecid=7
T8.17 online_dash_keys ... actual_qn=112 actual_codecid=7
  video=.../112-7 audio=.../audio-30280 video_total=47168494 audio_total=1294290
T8.17 online_dash_job_start video_key=.../112-7 video_state=downloading
  audio_key=.../audio-30280 audio_state=completed already_complete=0/1
```

- `TryCacheHit` 未命中原因：请求 qn=112 没有匹配的视频条目（`no_matching_video`）；音频条目是音频键，不参与视频匹配。
- 实际 video key：`BV1BMnv6jE5S/42915922022/112-7`（总字节 47,168,494）；实际 audio key：`BV1BMnv6jE5S/42915922022/audio-30280`（1,294,290）。
- `DownloadJob::Start` 返回 **kCompleted 的键**：`audio-30280`（`already_complete=1`）；视频 112 正在下载。
- 随即触发旧 guard，用户看到：

```text
DASH 条目已完整缓存，但本次未走缓存命中路径；请重新打开
```

- 切档失败后缓存：`80-7 complete=1`、`audio-30280 complete=1`、`112-7 complete=0`（47,168,494 字节的 `.part`）。说明旧 1080P+音频已 finalize，只有新视频缺失。

### 1.3 根因

1. 切档前 `TryCacheHit` 按 `requested_qn/requested_codecid` 过滤视频；112 没有视频缓存 → miss。
2. DASH 音频跨档共享（`audio-30280`），默认 1080P 播放完成后音频已经完整；切到 112 时音频 `DownloadJob::Start` 立即返回 `kCompleted`。
3. 旧 `StartDashDownload` 把“任一 job 返回 kCompleted”当成“整组 DASH 已完整缓存”，直接 `Fail`，把合法的“音频已缓存、视频待下载”误报为完整缓存未走命中。
4. 旧 `TryDashCacheHit` 在“视频完整、音频缺失”时还会删除完整视频重下；这也是同一个“按请求键猜实际键”的问题。

---

## 2. 修复

### 2.1 解析后按实际键精确检查

`OnlineSession::StartDashDownload` 在 `BuildDashPlans` 之后、创建 `DownloadJob` 之前：

1. `InspectDashCache(video_key, audio_key)` 用实际键 `CacheStore::Load` + `OpenComplete`，分别得到 `video_complete/audio_complete/broken` 与 `data.mp4` 路径。
2. **两条都完整** → `EmitResolvedDashCacheHit`：发布两路 `GrowingSource`、`cache_hit=1`、直接打开本地 `data.mp4`；不创建 job、不发 playurl/CDN。
3. **只缺音频（视频完整）** → 复用视频（发布全部字节，不重建/不重下），只为音频建 job；状态栏明确“视频已完整缓存（1080P），正在补下音频（audio-30280）”。
4. **只缺视频（音频完整）** → 复用音频，只为请求的视频档建 job；状态栏“音频已完整缓存，正在下载请求的视频档（1080P+ codecid=7）”。**本用户现象正是这一支**。
5. 缓存条目总字节与本次探测不一致 → 只重建该条陈旧条目，按缺失重新下载，避免新旧流混用。
6. 损坏条目只重建损坏的那一路；完整的一路保留。

### 2.2 请求 codecid 不匹配

- 预检查键包含 `qn` 与 `codecid`；`requested_codecid=12` 时不会命中 `80-7`（AVC）。
- 解析后仍用实际 `video_key`（含 codecid）精确检查；只有精确键完整才复用。
- 单测覆盖：缓存有 AVC `80-7`、请求 HEVC `80-12` → 下载 HEVC URL，AVC `data.mp4` 字节不变、仍 complete。

### 2.3 `DownloadJob::Start` 的 `kCompleted` 只作内部信号

- DASH：某一路 `Start` 返回 `kCompleted` 时，`adopt_completed` 走 `OpenComplete`、发布该路字节、记录路径，**不 Fail**；另一路继续下载或复用。
- durl：`Start` 返回 `kCompleted` 时转成正常缓存命中（`OpenComplete` + `EmitCacheHitReady`），不再报用户错误。
- 用户不再看到“已完整缓存但未走缓存命中路径”。

### 2.4 切档落地重建菜单

`TryApplyPendingOnlineQualitySwitch` 在 `online_switch_pending_=false` 后调用 `RebuildOnlineQualityMenu()`。否则切档期间菜单按“会话还不能重新取流”置灰，落地后不会自动恢复，用户无法切回 1080P30（本轮真实验收发现的连带问题）。

### 2.5 诊断日志（默认不输出）

只在 `LAPIAN_ONLINE_LOG=1` 下输出，全部为键/状态，不含凭据/URL：

- `online_cache_lookup begin/skip/miss/best`：请求 qn/codecid、条目数、每条跳过原因（`qn_mismatch`/`codecid_mismatch`/`not_complete_video`）。
- `online_dash_cache`：预检查视频 key + 配套 audio_id，命中/miss/error 原因。
- `online_dash_keys`：请求档与**实际** qn/codecid、video/audio key、总字节。
- `online_dash_exact`：实际键 complete/broken/error。
- `online_dash_reuse` / `online_dash_job_start` / `online_job_already_complete`：复用哪一路、两 job 状态、kCompleted 的键。
- `online_dash_stale` / `online_dash_rebuild`：陈旧/损坏条目重建。

---

## 3. 单测

新增 4 组到 `lapian_online_fault_test`（ctest 仍 37/37，不新增 target）：

| 用例 | 关键断言 | 结果 |
|---|---|---|
| 完整 DASH 视频+音频 | `cache_hit=1`、`dash=1`、两路 `Published==total`、`playurl=0`、`dash=0`、HTTP=0、CDN 请求=0、路径为 `data.mp4` | pass |
| 视频完整/音频缺失 | 复用视频（`Published==total`，视频媒体请求 0）、只下载音频（≥1）、无错误回调/无“完整缓存”错误、音频 finalize 后 complete、视频文件 hash 不变 | pass |
| 请求 codecid 不匹配 | 请求 `80-12` 时下载 HEVC URL，实际 qn=80/codecid=12，AVC `80-7` 仍 complete 且 hash 不变，不静默复用 AVC | pass |
| 解析后实际键两条都完整 | meta audio_id 指向不存在音频时仍按实际 `audio-30280` 命中，`cache_hit=1`、playurl=0、媒体 CDN 请求=0（仅 1 次 DASH 目录 + 探测） | pass |

```text
T8.13 online_fault_test result checks=214 failures=0 verdict=pass
```

（原 153 项 + 本轮 61 项新增断言；同一 test target，ctest 数量不变。）

---

## 4. 真实切档验收（Medium 副本）

命令：

```powershell
$exe = "$env:LOCALAPPDATA\Programs\lapian-t817\lapian.exe"
$root = "E:\拉片软件开发\build\t8.17-final2"
& $exe --online-quality-switch-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --tag t8.17-final2 --timeout-ms 60000 `
  --quality-switch-high 112 --quality-switch-back 80 --quality-switch-wait-ms 120000 `
  --online-cache-root "$root\cache" --online-pictures "$root\pics" `
  --online-records-root "$root\records" --settings-path "$root\settings"
```

结果：**checks=42 failures=0 verdict=pass**。

### 4.1 默认 1080P → 1080P+（112）

```text
quality_switch_session phase=initial qn=80 codecid=7 fps=30.000 cache_hit=0 dash=1
online_dash_exact video=.../112-7 complete=0 broken=0 error=条目不存在
  audio=.../audio-30280 complete=1 broken=0 error=
online_dash_reuse video_complete=0 audio_complete=1
quality_switch_high landed=1 qn=112 codecid=7 fps=30.000 error=
```

- 实际 qn=112、codecid=7（AVC）、fps 30.000；**音频复用**（complete=1），只下载 112 视频。
- 旧 1080P 缓存保留：`80-7 complete=1`、`audio-30280 complete=1`；112 视频完整后 47,168,494 字节，切回时 finalize 为 complete。
- PTS/帧号：`1,000,000 → 第 30 帧`（旧第 30 帧）；标记 1→1；在线记录/截图路径保留。
- 高码率切档不是 cache-hit，正常走 DASH 目录 + CDN 下载；没有静默换 codec/降档。

### 4.2 1080P+（112）→ 1080P30（80）

```text
online_cache_lookup begin ... requested_qn=80 requested_codecid=7 entries=3
online_cache_lookup best key=.../80-7 dash=1 audio_id=30280
online_dash_cache result=hit video=.../80-7 audio=.../audio-30280
quality_switch_back landed=1 qn=80 codecid=7 fps=30.000 cache_hit=1
  dash_requests=0 playurl_requests=0 error=
```

- 实际 qn=80/codecid=7；`cache_hit=1`；**0 次 DASH 目录、0 次 playurl**，直接打开本地 `data.mp4`，无媒体下载。
- PTS `1,000,000 → 第 30 帧`；标记 1→1；记录/截图保留；112 与 80 两条视频 + 音频三个条目都 `complete=1`。
- 最终：

```text
quality_switch_after frame=30 pts=1000000 marks=1 record=1 snapshot=1 cache_entries=3
quality_switch_result checks=42 failures=0 verdict=pass
```

### 4.3 与修复前的对照

| 项 | 修复前 | 修复后 |
|---|---|---|
| 80→112 | `state=error`，报“DASH 条目已完整缓存…”；112 视频 `.part` 47,168,494B 未落地 | `landed=1`，qn=112/7，PTS/标记/截图保留 |
| 112→80 | 未到达 | `cache_hit=1`，qn=80/7，dash/playurl=0，三缓存条目 complete |
| 音频 complete 的处理 | 触发用户错误 | 复用音频，只补视频（或只补音频） |
| codecid 不同 | 预检查 miss，但后置 guard 可能误报 | 按实际 codecid 精确检查/下载 |

---

## 5. 回归

| 项 | 结果 |
|---|---|
| `--online-quality-switch-test`（OB-1，80→112→80） | **42/0 pass** |
| `lapian_online_fault_test` | **214/0 pass**（含 4 组新用例） |
| `ctest` | **37/37 pass**（61.39–62.17s；一次重跑见 §8.2） |
| `--key-test`（`media\测试.mp4`） | **77 项 ok=1 / 0 项 ok=0** |
| T8.6.1b `--online-drag-preview-test`（DASH） | **35/0 pass**（未改预览/拖动路径） |
| 日志脱敏 | SESSDATA/Cookie/`deadline=`/`upsig=`/签名 URL **0 命中** |

---

## 6. 改动文件

| 文件 | 内容 |
|---|---|
| `src/app/OnlineSession.h/.cpp` | 实际键精确缓存检查、两路复用/补流、kCompleted 防御、durl 缓存命中转换、`Poll/HasPendingFinalize/Close` 支持单路复用、诊断日志 |
| `src/app/MainWindow.h/.cpp` | `OnlineCacheFinalizableForTest()`；切档落地后重建档位菜单 |
| `src/app/OnlineQualitySwitchTestRunner.h/.cpp` | 新增 `--online-quality-switch-test`（默认 80→112→80，校验 PTS/帧号/标记/截图/记录/缓存/0 网络） |
| `src/app/main.cpp` | 新探针分流/参数/独立测试根 |
| `src/CMakeLists.txt` | 新 runner 源文件 |
| `tests/online_fault_test.cpp` | 4 组单测（完整命中/缺音频/codecid 不匹配/解析后命中） |
| `docs/perf/online-quality-switch.md` | 本文件 |

**没有改**：`OnlinePreviewDecoder`、`MainWindow` 拖动预览路径、M1/M2/P4/P6/P7 门/数字、OB 样本、`lapian-cache-1` schema、默认 AVC≤1080p、`lapian-cache-1` 目录/键语义。

---

## 7. 范围与不承诺

- 本任务只修“清晰度切换”；在线拖动前后一致（§3.B）未在本提交处理。
- 高码率档本次为 **qn=112（1080P+ / AVC 30fps，3,415,921bps）**；不是 1080P60/4K，不写这些档可达/通过。
- “0 CDN”由 `cache_hit=1`、`dash_requests=0`、`playurl_requests=0`、直接打开 `data.mp4` 证明；没有做逐包抓包/防火墙级证据。
- 高码率切档仍然要重新下载 112 视频（音频复用），不是无缝/并行切档。
- 切档前两路 `.part` 的 `complete=0` 是正常状态；切档关闭 reader 后才 finalize，文档/探针按 `progress.complete + finalizable` 判断。

---

## 8. 残余与说明

1. **主安装 exe 未替换**：`%LOCALAPPDATA%\Programs\拉片软件\lapian.exe` 被运行中实例锁定，本轮 Medium 证据来自同目录完整副本 `%LOCALAPPDATA%\Programs\lapian-t817\lapian.exe`。关闭该实例后可把 `build\release\bin\lapian.exe` 覆盖安装；本轮未强杀用户进程。
2. **ctest 一次 flaky**：第一次全量 `ctest` 时 `lapian_preview_decoder_test` 以 `0xC0000374`（heap corruption）退出；单独重跑 6.12s pass，随后全量 **37/37 pass**（61.39s、62.17s 两次）。这是 T8.14 已记录的 teardown 残余，本轮未定位、**不写“无崩溃”**；与清晰度切换改动无直接关联（未改 `OnlinePreviewDecoder`）。
3. **切档前诊断**：真实 Medium 运行使用默认 DPAPI 凭据；只输出 `vip_status=1` 标志，未输出账号/cookie/URL。
4. **pre-fix 探针版本**：复现使用只加诊断日志、未改行为的构建；旧探针在切档前把 `.part complete=0` 误当失败（2 项），已在最终探针改为“条目存在 + progress complete/finalizable”，不影响“切档失败”错误本身的复现。
5. **Qn/codecid 实际值只信服务端目录**；本轮 OB-1 的 112/7 AVC 30fps 来自真实 `playurl(fnval=4048)`。

---

## 9. 复现（最短）

```powershell
$env:TMP = 'E:\拉片软件开发\.deps\tmp'; $env:TEMP = $env:TMP
$exe = "$env:LOCALAPPDATA\Programs\lapian-t817\lapian.exe"
$root = "E:\拉片软件开发\build\t8.17-final2"
& $exe --online-quality-switch-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --tag t8.17-final2 --timeout-ms 60000 `
  --quality-switch-high 112 --quality-switch-back 80 --quality-switch-wait-ms 120000 `
  --online-cache-root "$root\cache" --online-pictures "$root\pics" `
  --online-records-root "$root\records" --settings-path "$root\settings"

# 单测 / 回归
& 'E:\拉片软件开发\build\release\Release\lapian_online_fault_test.exe'
ctest --test-dir build\release -C Release --output-on-failure
```

退出码：`0`=pass；`1`=fail；`2`=blocked（无凭据/非大会员/非 1080P/档位不存在）。

---

## 10. Agent 3 独立复验（2026-10-25）

- **二进制**：本次 `scripts\build-release.ps1 -Clean`（222.4s，ctest 37/37）后的 Medium 副本
  `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`，3,149,824B / SHA256
  `7A80C9A9B3C865185CEBA1A489FB1AE56635BDDA065E4699C829B0461B7A3761`（与 dist 一致）。
- **凭据**：默认 DPAPI（`credential_source=dpapi_default`），`nav vip_status=1`；未打印/落盘
  SESSDATA、Cookie、签名 URL（本轮日志敏感串 0 命中）。
- **命令**：§4 同一条，仅换 `--tag verify-t8.17` 与独立根 `build\verify-t8.17`。
- **结果**：`quality_switch_result checks=42 failures=0 verdict=pass`。

| 项 | 本次复验 |
|---|---|
| 初始 | qn=80 / codecid=7 / fps=30.000；ready=1；dash_requests=1、playurl_requests=0 |
| 80→112 | landed=1；qn=112 / codecid=7 / fps=30.000；PTS 1,000,000→1,000,000、第 30 帧；标记 1→1；记录/截图保留；音频 complete=1 复用，只下载 112 视频 47,168,494B |
| 112→80 | landed=1；qn=80 / codecid=7；`cache_hit=1`、dash_requests=0、playurl_requests=0；PTS 1,000,000、第 30 帧；标记/记录/截图保留；三条缓存 complete |
| 误导错误 | 两次切档 `error` 均为空，`no_misleading_complete_error=1` |

与 Agent 1 文档一致，未发现数字差异。补充两点：

1. **新缓存根首次写盘**：初始 ready 前出现一次 `cdn_done ... bytes=0 error=写入缓存文件失败`
   （视频/音频各一次），自动重试成功；本文件 §4 未记录该现象，属首次打开 UX 残余，
   `DownloadJob`/`CacheStore`/`WinHttpDownloadTransport` 未被本次两个提交修改。
2. **“视频完整/音频缺失”**：单测（`lapian_online_fault_test` T8.17）验证复用视频、只补音频、
   无错误回调；用户提示串在 `OnlineSession.cpp`。本轮真实窗口未构造该分支，不写目视通过。
   音频可听性同样未由无人值守探针验证。
