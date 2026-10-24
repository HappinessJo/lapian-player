# 下一个会话提示词：T8.13（M2 网络异常 / 并发加固）

- **版本**：v1（2026-10-24，T8.12 收口后；基线 commit `cbf4f31`）
- **直接复制本文件给新会话。** 上一会话已完成 **T8.12（档位策略/手动切档/降帧提示）**；新会话**只做 T8.13**，不要顺带 T8.14（M2 门）/T8.15（大会员条件验收）/T8.16（打包）/T8.6.1b（拖动预览）。
- **当前状态**：**M1 已于 T8.8 通过**；T8.9/T8.10/T8.11/T8.12 完成，均**不是 M2 门**；T8.13 **未开始**；T8.14 才是 M2 门。当前 `tasks-v2.md` v0.23、`AGENTS.md` v6.35、`ctest` **34/34**、`dist` **34.21MB/25**。
- **T8.12 基线**：普通用户 3/3 实际 qn=80 AVC（30.000/29.412/25.000）；OB-2 29.412fps 降帧横幅/角标；OB-1/2/3 1080→720 成功（qn=64、PTS 1,000,000→第 30 帧、标记 1→1、截图 qn64/codecid7/fps29.412）；`--online-m2-test` 3/3 **34/0**；M1 `--force-durl` **28/0**；quality 3×45/46/45 + UI 50/56/50；index 27/22/28；长按 54/0；缓存 396/0；管理 75/0+72/0+98/0；key 77/0；P1/P2 262.79/309.08ms；P7 0.74/0.44ms。

## 1. 先读

1. [AGENTS.md](../AGENTS.md)（v6.35 当前状态/约束）
2. [tasks-v2.md](../tasks-v2.md) 的 **T8.13** 行、M2 门、版本记录 v0.23
3. [spec-v1.0-delta.md](spec-v1.0-delta.md) v0.4 的 **§0.2、O2/O4/O5/O7/O9、OP3/OP5/OP6/OP7、DEC-2/DEC-5**；重点是 O7-A1…A9、O4-A1…A8、O5-A5/A6/A7
4. [ADR-007](adr/ADR-007-bilibili-online.md)、[T8.10 证据](perf/v2-online-m2.md)、[T8.10 交接](handoff-2026-10-24-t8.10.md)
5. [T8.11 证据](perf/v2-online-index.md)、[T8.11 交接](handoff-2026-10-24-t8.11.md)
6. **[T8.12 证据](perf/v2-online-quality.md)**、**[T8.12 交接](handoff-2026-10-24-t8.12.md)**
7. [T8.6.1a 交接](handoff-2026-10-24-t8.6.1a.md)（异步长按/seek 状态机，不能破坏）
8. [T8.3.1 证据](perf/v2-online-cache-manage.md)（只读/磁盘满/清空保留记录）、[T7.5 鲁棒性](perf/T7.5-robustness.md)
9. 代码入口：
   - `src/app/OnlineSession.h/.cpp`：`Start`/`StartDurlDownload`/`StartDashDownload`/`StartResolveWorker`/`Poll`/`Fail`/`Cancel`/`Close`/`FinalizeAfterReadersStopped`；`ResolveOutcome`、`OnlineState`、`OnlineProgress`、`DownloadJob`/`audio_job_`
   - `src/online/DownloadJob.h/.cpp`：Range 续传、备份 URL、300ms/1s/2s 重试、`make_refresh`/URL 过期刷新、`FinalizeCompleted`
   - `src/online/WinHttpClient.*`、`WinHttpDownloadTransport.*`、`IHttpClient.h`、`DownloadTransport.h`
   - `src/online/GrowingSource.*`：读者/取消/阻塞唤醒、`Publish`/`MarkComplete`/`Cancel`
   - `src/app/MainWindow.cpp`：`ApplyOnlineState`、`ApplyOnlineProgress`、`ApplyOnlineReady`、`ApplyOnlineCacheHit`、`RequestOnlineSeek`、`ShutdownOnlineSession`、`TryApplyPendingOnlineQualitySwitch`、`SaveMarks`/`Snapshot`
   - `src/app/OnlineM2TestRunner.*`、`OnlineQualityTestRunner.*`、`OnlineOfflineTestRunner.*`、`OnlineFetchTestRunner.*`（故障注入/真实窗口探针可复用）
   - `src/online/CacheStore.*`、`CacheManager.*`（损坏/磁盘满/配额/清空）
   - `src/core/MarkStore.*`、`src/media/SnapshotNaming.*`（失败时标记/截图不丢）

## 2. 任务边界

**只做 T8.13：网络异常/并发加固。** 官方验收：

> **T8.13** 网络异常/并发加固：断网、超时、403、URL 过期、缓存损坏、磁盘满、并发读、关闭阻塞、休眠恢复。
> 验收：**≥10 个故障场景**：无崩溃/冻结；已缓存可播；重试可续；标记不丢；错误 ≤5s；日志脱敏；并发 4 读者 10min 无死锁。
> 证据：`--online-fault-test`；`docs/perf/v2-online-faults.md`。

必须覆盖（至少 10 条，可真实/注入组合）：

1. **下载中断网**：已缓存区继续播放、下载暂停、提示可读、恢复网络后从断点续传。
2. **未缓存 seek 时断网**：≤5s 可读错误，已缓存画面保持，可重试。
3. **resolve/playurl 超时**：≤5s 可读，进程不退出。
4. **API 错误矩阵**：-400/-404/62002/-101/超时，5s 内可读，不崩。
5. **主 CDN 403**：切备份 URL，不重复下载已缓存字节。
6. **URL 过期**：自动刷新 playurl 一次后继续；刷新失败则已缓存可播 + 可读提示。
7. **缓存损坏**（截断/改 meta/改大小）：检测/重建，不用坏数据冒充可播放。
8. **磁盘满/只读**：≤5s 可读错误；不崩；标记不丢；已缓存可播。
9. **并发读**：4 读者 10min 无死锁/无丢失唤醒；关闭有界。
10. **关闭阻塞**：下载中/未缓存 seek 中/切档中关窗，进程 ≤1s 退出。
11. **休眠恢复**：模拟系统挂起（线程停顿/时钟跳变/下载 stall）后恢复，不挂死、不重复写、可继续；**真实 Windows sleep 若无法自动执行，只能写“未验证”，不能冒充通过**。
12. **DASH 双输入单流失败**：音频/视频一路失败/取消时，不留下两套 reader/线程；错误可读；缓存不写成完成。

**不做 / 不改**：

- 不做 T8.14 M2 门；不写“M2 通过”；不写 60fps/4K 可用。
- 不新增账号/弹幕/评论/云端/批量抓取；不绕过付费/DRM/地区限制。
- **不改** M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema/缓存键/索引磁盘格式。
- **不改** T8.12 的档位策略/降帧口径、T8.10 的 DASH 双输入/音频缓存键/fMP4 `SeekToKeyframePts`、T8.11 的增量索引/三态总帧数、T8.6.1a 的异步步进/长按。
- 不把“故障注入钩子”泄漏成产品默认行为；默认路径仍是零网络/正常重试。

## 3. 现状与硬事实

### 3.1 已完成的代码能力

- `DownloadJob` 已有：顺序 Range 下载、3 个备份 URL、300ms/1s/2s 重试、URL 过期刷新一次、`.part` 续传、写盘后 `Publish`、关闭写句柄后 `MarkComplete`、defer finalize。
- `OnlineSession` 已有状态机：`kPreparing/kBuffering/kCacheHit/kReady/kStalled/kSeekingWait/kRecovering/kError/kClosed`；有 `Cancel`/`Close`/`FinalizeAfterReadersStopped`；DASH 视频/音频双 `GrowingSource`/`DownloadJob`。
- `GrowingSource` 已有 4 读者 10min 无死锁单测（`lapian_growing_source_test` 157/0）、取消/唤醒/EOF/EIO/EXIT；但 **产品级** `--online-fault-test` 还没做。
- 缓存：`lapian_online_cache_test` 396/0、`--online-cache-manage-test` 75/0 + 72/0 + 98/0；只读/磁盘满/清空保留记录已有证据。
- 记录/截图：`lapian_online_records_test` 52/0；清缓存保留记录/截图沿用 T8.7/T8.3.1。
- 当前**没有** `--online-fault-test`，也**没有** `docs/perf/v2-online-faults.md`；T8.13 的主要新增就是这两项 + 必要修复。

### 3.2 当前回归基线（不得退化）

| 项 | 当前 |
|---|---|
| `ctest` | **34/34** |
| `--online-m2-test` OB-1/2/3 | **3/3 34/0** |
| `--online-m1-test --force-durl` | **28/0** |
| `--online-quality-test` OB-1/2/3 | **45/0、46/0、45/0** |
| `--online-quality-test --ui` OB-1/2/3 | **50/0、56/0、50/0** |
| `--online-index-test` 真实/离线 | **27/0 / 22/0**；单测 28/0 |
| 在线长按 OB-1 | **54/0** |
| 缓存/记录/管理/key | 396/0；52/0；75/0+72/0+98/0；77/0 |
| 本地 P1/P2/P7 | 262.79 / 309.08ms；P7 后退 0.74ms、前进 0.44ms |
| `dist` | **34.21MB / 25 文件** |

### 3.3 环境/执行坑

- 工作区 `dist\lapian.exe` / `build\release\bin\lapian.exe` 继承 **Low integrity**；在线默认 `%LOCALAPPDATA%` / `%APPDATA%` 写不进去。测试用 `build\t8.13-*` 显式缓存/记录/图片/设置根；默认路径只在 Medium 副本验证，否则写“未重跑”。
- 构建需要把 `TMP/TEMP` 指到工作区 `.deps\tmp`，否则 MSBuild 临时文件无权限。
- CMake 3.31 在已有 `CMakeCache.txt` 重配可能 `0xC0000409`；用 `scripts\configure.ps1`（会删 cache 保留构建产物）。
- PowerShell 对 native stderr 可能报 `NativeCommandError`；在线探针建议 `Start-Process ... -RedirectStandardOutput/-RedirectStandardError`，不要用 `2>&1 |` 配 `$ErrorActionPreference=Stop`。
- 真实在线凭据：`.secrets\bilibili_sessdata.txt`；日志不打印 SESSDATA/签名 URL；不提交缓存/截图/记录。
- 真实 `sleep` 需要系统/用户条件；无法自动做就明确“未验证”，不要用“线程 sleep 10s”冒充系统休眠。

## 4. 建议实现路线

1. **先做故障矩阵设计**（写进 `docs/perf/v2-online-faults.md` 的表头）：场景、注入点、预期 UI/状态、时间上界、是否真实/模拟、结果（pass/fail/unverified）。按 O7-A1…A9 优先。
2. **新 `OnlineFaultTestRunner` + `--online-fault-test`**
   - 复用 `OnlineM2TestRunner` 的窗口/缓存/凭据/计时结构；用显式根。
   - 优先“不联网”的注入：`IHttpClient`/`IDownloadTransport` 假实现、`SetHttpClientForTest`/`SetDownloadTransportForTest`、`SetPlayUrlDataForTest`、`CacheStore` 小配额/损坏文件、只读目录 ACL、`SetDownloadThrottleForTest`、`GrowingSource` 阻塞/取消。
   - 真实网络只保留最少：断网模型（`OnlineM1TestRunner` 已有）、真实 403/过期（T8.5 已有）可复用，不要每场景都打 CDN。
   - 每个场景记录 `elapsed_ms`、UI gap、错误文案、是否崩溃、缓存/标记 sentinel 是否保留；失败必须 fail，不把“跳过”写成 pass。
3. **修产品缺陷，不改门**
   - 断网/超时后：`OnlineState::kRecovering`/`kError` 与 `DownloadJob` 重试/刷新路径；已缓存区可播；未缓存 seek 的错误 ≤5s。
   - 关闭：`ShutdownOnlineSession` 先 `Cancel` 下载、唤醒 reader、停索引/波形/seek；保持 ≤1s。
   - 磁盘满/只读：写失败回滚 + 可读错误；不把 `.part` 标完成；标记/截图不丢。
   - 缓存损坏：`CacheStore` 已有检测；产品路径要提示“重建缓存”，不把坏数据当成功。
   - 休眠恢复：优先做“下载 stall + QPC/时间跳变 + reader 继续”的注入；真实 sleep 另列 unverified。
4. **并发 4 读者**
   - 如果要产品级 10min：用 4 个 `GrowingCursor`/读线程 + 下载暂停/恢复/取消交错；记录死锁、丢失唤醒、关闭耗时、WS。可复用 `lapian_growing_source_test` 的 10min 经验，但必须至少有一次在线 Session/DownloadJob 级验证。
   - 10min 测试若太慢，至少单测 + 缩短 smoke；正式 10min 证据必须真跑，不能写成“按设计应通过”。
5. **日志脱敏**
   - 用哨兵 SESSDATA/签名 URL 查询串扫描所有日志/错误对话框；确认不含 Cookie/完整 query。
   - 403/刷新路径只记录 host/path/status，不打印 `deadline`/`upsig` 等签名参数。
6. **测试**
   - 新增 ctest（纯注入、无 GUI）：`lapian_online_fault_test`，覆盖 ≥10 场景中可离线的部分。
   - `--online-fault-test`：真实窗口，至少验证断网、未缓存 seek 断网、关闭、标记 sentinel、UI gap、错误 ≤5s。
   - `ctest` 从 34 增加到 35+；保持 M2/M1/quality/index/longpress 回归。
   - 不新增 flaky 的“真实 CDN 每次必现 403/过期”；注入优先。

## 5. 验收（按 O7/O4/O5/O9）

- **O7-A1** 下载中断网：已缓存区继续；下载暂停；提示；恢复后续传。
- **O7-A2** 未缓存 seek 断网：≤5s 可读错误；已缓存可播；可重试。
- **O7-A3** URL 过期/403：刷新一次继续；失败则已缓存可播+可读提示。
- **O7-A4** 主 CDN 失败：备份切换；不重复下载已缓存字节。
- **O7-A5** 缓存损坏：检测/重建；不显示错误帧。
- **O7-A6** 磁盘满：≤5s；不崩；标记不丢。
- **O7-A7** API 失败：5s 内可读；进程不退出。
- **O7-A8** 下载中杀进程/重开：`.part` 续传；在线记录不丢。
- **O7-A9** 所有场景 UI 线程停顿 >200ms 次数 = 0。
- **O4-A7** 等待中断网 ≤5s 可读；已缓存可播；可重试。
- **O4-A5** 等待中关窗 ≤1s，无死锁。
- **O5-A5/A6/A7** 磁盘不足、杀进程续传、缓存损坏。
- **T8.13 行**：≥10 场景、无崩溃/冻结、已缓存可播、重试可续、标记不丢、错误 ≤5s、日志脱敏、并发 4 读者 10min 无死锁。
- **不判 M2**；M2 门是 T8.14。60fps/4K 仍是 T8.15 条件验收，不在 T8.13。

## 6. 回归/测试命令

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure
```

真实/窗口探针用独立根，例如：

```powershell
build\release\bin\lapian.exe --online-fault-test --fault-set t8.13-core `
  --bvid BV1BMnv6jE5S --cid 42915922022 --timeout-ms 120000 `
  --online-cache-root build\t8.13-fault\cache `
  --online-pictures build\t8.13-fault\pics `
  --online-records-root build\t8.13-fault\records `
  --settings-path build\t8.13-fault\settings
```

基线（不得退化）：

- `ctest` **≥35/35**（新增 `lapian_online_fault_test` 后；不得少于 34）。
- `--online-m2-test` OB-1/2/3 **3/3 34/0**。
- `--online-m1-test --force-durl` **28/0**。
- `--online-quality-test` 3×**45/46/45** + `--ui` **50/56/50**。
- `--online-index-test` 真实 **27/0**、离线 **22/0**；长按 **54/0**。
- 缓存 396/0、管理 75/0+72/0+98/0、key 77/0、P1/P2/P7。
- `dist` 体积仍 ≤100MB；T8.13 若改产品代码，重新组装并记录。
- 测试根 `build\t8.13-*`；不写真实 AppData；不提交缓存/截图/记录；日志不打印 SESSDATA/签名 URL。

## 7. 停止条件

遇到以下情况先停下记录/问用户，不要偷偷扩任务：

- 要改 M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema/缓存键/索引格式；
- 要改已暴露的帧号语义，或会让 T8.11 的 5306/2866、T8.12 的 29.412 提示变化；
- 故障加固必须大改线程模型，或关闭 >1s、错误 >5s、UI gap >200ms 无法保证；
- 真实系统休眠/真实断网/SMB/物理设备丢失不可复现：写“未验证”，不要伪造 pass；
- 只能靠修改产品默认行为才能过测试，或故障注入钩子会影响正常路径；
- 要求把 T8.13 写成 M2 通过（不应发生；M2 门是 T8.14）。

## 8. 完成时

- 一个可审查 commit；`git status` clean。
- 更新 `docs/perf/v2-online-faults.md`（≥10 场景矩阵、注入点、时间上界、真实/模拟、pass/fail/unverified、日志脱敏、并发 10min、缺口）、`docs/handoff-2026-10-24-t8.13.md`、`tasks-v2.md` v0.24、`AGENTS.md` v6.36。
- 报告：场景数、pass/fail/unverified、错误/关闭/UI gap 数字、续传/备份/刷新、缓存损坏/磁盘满、标记 sentinel、4 读者 10min、日志脱敏、回归数字、缺口。
- **不要把 T8.13 写成 M2 通过**；M2 门是 T8.14。**不要把“注入可过”写成“真实 CDN 可过”**。
- 建议下一步：T8.14（M2 门）。

## 9. 交接给你的现成接口/事实

- `DownloadJob`：`DownloadPlan`、`DownloadOptions{defer_finalize, throttle_bytes_per_sec}`、`Start/Cancel/Wait`、`stats()`、`FinalizeCompleted`；内部有备份/重试/refresh。
- `OnlineSession`：`SetHttpClientForTest`、`SetDownloadTransportForTest`、`SetPlayUrlDataForTest`、`SetReadyThresholdForTest`、`SetDashEnabledForTest`、`SetDownloadThrottleForTest`、`LastErrorForTest`；`OnlineState`/`OnlineProgress`。
- `GrowingSource`：`Publish`/`Cancel`/`Cursor`/`WaitForBytes`/统计；4 读者 10min 单测已有。
- `CacheStore`：小配额/LRU/损坏/清空/Range 续传；`CacheManager` 只读/磁盘满可读错误。
- `MarkStore`/`SnapshotNaming`：在线记录/截图字段；失败不应丢 sentinel。
- 当前未做：`--online-fault-test`、`docs/perf/v2-online-faults.md`、产品级 4 读者 10min、真实 sleep。
- 当前已知缺口：T8.12 无真实 fps 不同切档；缓存命中切档未跑；HEVC-only/4K 不可得。这些**不是 T8.13 的主线**，不要顺手改档位策略。
