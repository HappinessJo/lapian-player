# T8.13 网络异常/并发加固（M2 第 5 项，非门）

- **版本**：v1.0（2026-10-24）
- **任务**：`tasks-v2.md` T8.13（网络异常/并发加固）——**不是 M2 门**；M2 门是 T8.14。
- **基线**：`794e7dd`（docs: add T8.13 handoff prompt）；代码基线 `cbf4f31`（T8.12）。
- **一句话**：新增 `--online-fault-test`（真实窗口、全部注入、不联网）与 `lapian_online_fault_test`（ctest，153 项）；故障矩阵 **10 个窗口级场景 + 15 组离线注入** 全过；**错误 ≤5s、关闭 ≤1s、UI 停顿 >200ms = 0、4 读者 10min 无死锁、日志/记录无 SESSDATA/签名 URL**；修 4 个产品缺陷（失败后缓存区可播、网络无响应看门狗、API 超时 4.5s、缓存损坏自动重建）。**不判 M2**；真实 Windows 休眠/真实 CDN 403 未验证。

---

## 1. 范围与结论

| 项 | 结论 |
|---|---|
| 故障场景 | **≥10**：窗口级 10 个 + 离线下 15 组（见 §3/§4）；矩阵 12 行覆盖 prompt 要求 |
| 窗口级 `--online-fault-test` | **10/10 pass、92 项检查 0 失败**、`unverified=1`（真实休眠） |
| 离线 `lapian_online_fault_test` | **153 项 0 失败**（ctest **35/35**，其中新增测试 24.1s） |
| 错误 ≤5s | 断网失败 **3,981–4,155ms**；API 超时 **4,515–4,529ms**（真实 WinHTTP + 本机静默服务器）；看门狗 **4,121–4,137ms** |
| 关闭 ≤1s | 下载中 **7–16ms**、未缓存 seek 中 **60–66ms**、取消 **≤100ms**（0–7ms） |
| UI 停顿 >200ms | **0 次**；窗口级全跑最大心跳 **73ms** |
| 已缓存可播 | 失败后 seek 到已缓存帧落点/PTS 精确（帧 2，PTS 66,667µs）；播放推进 13 帧；前沿读 **EIO**（不伪装 EOF/EXIT） |
| 重试可续 | 同 URL Range 续传（第 2 请求 offset 409,600）；重开从 `.part` 续传（offset 1,228,800） |
| 备份/刷新 | 主 CDN 403 → 备份切换 1 次、只下缺失字节；URL 过期刷新 1 次后从断点继续；刷新失败保留 `.part` |
| 缓存损坏 | 截断 `data.mp4` → `CacheStore` 判 broken（1,069,188 vs 3,207,564）→ 会话**自动 Rebuild 后重下**，ready 132ms，不冒充缓存命中 |
| 磁盘不足 | 声明 2PB → **5ms** 可读错误「磁盘空间不足：需要 2048.0 TB，可用 307.5 GB」；不崩、不写坏 `.part` |
| 标记 sentinel | 失败前 1 → 失败后 1 → 重开 1；记录文件 584B 重建前后不变 |
| 4 读者 10min | 真实 **600s**：`ops=25,907`、`waits=522`、`deadlocks=0`、`lost_wakeups=0`（`timeouts=0`）、close **16ms**、`published=8,388,608` |
| 日志脱敏 | 错误/状态/进度/JSONL/meta 均无 `SENTINEL_SESSDATA`/`deadline=`/`upsig=`/`SENTINELSIG`/完整 CDN URL |
| 回归 | `ctest` 35/35；M2 **3/3 34/0**；M1 `--force-durl` **28/0**；quality UI **50/56/50**；index **27/0 + 22/0**；长按 **54/0**；管理 **72/0 + 98/0**；key **77/0**；P1/P2 **268.67/322.34ms**、P7 0.19/0.24ms；dist **34.3MB/25** |
| M2 门 | **未判、未写通过**；T8.14 才是 M2 门 |
| 未验证 | 真实 Windows 休眠（S3/S4）、真实 CDN 403/过期（本轮注入为主；T8.5/T8.8 有历史真实 403/刷新证据）、窗口级 DASH 单流失败（离线注入已验证）、只读 ACL（复用 T8.3.1 75/0） |

---

## 2. 产品修正（4 项，均不改 M1/M2 门/验收数字/OB 样本/`lapian-cache-1`）

### 2.1 下载失败后已缓存区仍可播（O7-A1/A2）

**缺陷**：`OnlineSession::Fail()` 无条件 `CancelJobAndSource()` → `GrowingSource::Cancel()`。`GrowingCursor::Read` 对 Cancel 是终态（即使读已发布区也返回 `AVERROR_EXIT`），所以下载一失败已缓存画面就不可播；与 “已缓存区仍可播放” 冲突。

**修正**：`Fail()` 改走 `FailJobsAndSources(keep_cached_readable)`：

- 首帧后（`ready_emitted_`）或存在 reader 时：只 `Cancel` 下载 job，读层保持 `Fail(EIO)` 语义——**已发布前缀可读、前沿返回 EIO**（不伪装 EOF）；
- 首帧前/关窗/取消：保持旧行为（`Cancel`）。
- 证据：离线 T2/T3（前沿 EIO、前缀可读）；窗口级 S2（失败后帧 2 落点/PTS 精确、播放推进）、S3（失败后帧 1 可播）。

### 2.2 网络无响应看门狗（O7 5s 上界、O4-A7）

**缺陷**：下载尝试 `attempt_timeout_ms=20s`；连接“静默”（黑洞/半开）时 UI 会长时间停在“缓冲中”，既没有可读错误也不进入重试。

**修正**：

- `DownloadJob::stats().last_progress_ms`（每次尝试开始/写盘/暂停恢复刷新）；
- `OnlineSession::Poll()` 每 80ms 检查，连续 `kDownloadStallTimeoutMs=4000ms` 无写盘 → `DownloadJob::AbortStalledAttempt()`；
- 下载线程立即 `kFailed` + 可读错误：「网络无响应：连续 4 秒没有收到下载数据，已暂停下载（已缓存区仍可播放；重新打开同一视频可续传）」；`.part` 保留。
- 证据：窗口级 S2/S3/S4 错误 **4,132–4,155ms**；离线 T9 同语义；10min 压测里 17 次瞬时断网全部重试成功。

### 2.3 API 超时 ≤5s（O7-A7）

**缺陷**：playurl/DASH/刷新/总字节探测原先用 `6000ms` 超时；“超时必须 5s 内可读”不成立。

**修正**：新增 `lapian::online::kOnlineApiTimeoutMs = 4500`，统一用于 resolve/刷新/probe。实测正常请求 100–300ms（M2/quality 全过），黑holed API 4.5s 返回「取流请求超时，请重试」。

### 2.4 缓存损坏自动重建（O5-A7/O7-A5）

**缺陷**：检测到损坏只报「缓存损坏，需要重建」，用户必须手动清缓存；O7-A5 要求“检测/重建；重试成功；不显示错误帧”。

**修正**：`TryCacheHit/TryDashCacheHit` 返回可重建键；`Start()` 先 `CacheStore::Rebuild(key)`（只删该条目数据，不动在线记录/截图），再按未命中重新取流并提示「缓存损坏，已重建，正在重新下载…」；重建失败才报错。证据：窗口级 S6（截断→自动重建→ready 132ms→重开缓存命中 0 下载、52ms、mark/记录 584B 保留）；离线 T10。

---

## 3. 故障矩阵（12 行 = prompt 必需项）

| # | 场景 | 注入点（真实/模拟） | 预期 | 结果 | 关键数字 |
|---|---|---|---|---|---|
| 1 | 下载中断网 | `FaultTransport` 前 N 请求交付 400KB 后断连（模拟） | 重试 + Range 续传；已缓存继续 | **pass** | 窗口 S1：requests=2、resume offset=409,600、ready 152ms、play→frame 13、close 16ms；离线 T1：hash 一致、`sink_bytes==total` |
| 2 | 未缓存 seek 断网 | 交付 1.2MB 后静默；seek 到片尾帧 200（模拟） | ≤5s 可读；已缓存可播；可重试 | **pass** | 窗口 S3：提示「缓冲中（目标帧 200）」60ms、网络错误 4,145ms、帧 1 可播、重开续传 offset 1,228,800、目标 200 落地、mark 1；离线 T3：阻塞读 EIO 1,349ms |
| 3 | resolve/playurl 超时 | 本机 127.0.0.1 静默服务器 + 真实 `WinHttpClient`（真实超时路径，无外网） | ≤5s 可读、进程不退出 | **pass** | 4,515/4,526/4,529ms（3 次），`timed_out=1`、错误可读；离线 T7 fake 超时 0ms |
| 4 | API 错误矩阵 | `IHttpClient` fake：-400/-404/62002/-101/HTTP 412（模拟） | 5s 内可读、不崩 | **pass** | 6/6 可读（含「登录不可用/凭据失效（-101）」）、0ms、无 URL |
| 5 | 主 CDN 403 | 主 URL 403、备份可用（模拟）；真实 403 见 T8.5/T8.8 | 切备份；不重复已缓存字节 | **pass** | 离线 T4：`backup_switches=1`、第 2 请求 offset=307,200、只下缺失字节、hash 一致 |
| 6 | URL 过期 | 全 3 URL 403 → refresh 回调给新 URL（模拟过期） | 刷新 1 次后从断点继续；失败保留缓存 | **pass** | 离线 T5：`refreshes=1`、新 URL offset=307,200、hash；T6 刷新失败：33ms、错误含「刷新」、`.part` 保留 |
| 7 | 缓存损坏 | 截断 `data.mp4`（1.07MB vs meta 3.21MB）/缺音频（模拟） | 检测/重建；不冒充可播放 | **pass** | 窗口 S6：broken 检出→自动 Rebuild→ready 132ms→重开 cache-hit 0 请求 52ms、mark/记录保留；离线 T10 同 |
| 8 | 磁盘满/只读 | playurl 声明 2PB（模拟磁盘不足）；只读 ACL 复用 T8.3.1 | ≤5s 可读；不崩；标记不丢 | **pass** | 窗口 S5：5ms、「磁盘空间不足：需要 2048.0 TB，可用 307.5 GB」、0 字节写入、记录 sentinel 不动；只读 ACL 75/0（T8.3.1，本轮未重跑） |
| 9 | 并发读 | 4 个 `GrowingCursor` + 下载暂停/恢复 + 瞬时断网（模拟） | 10min 无死锁/丢唤醒；关闭有界 | **pass** | 真实 600s：`ops=25,907`、`waits=522`、`timeouts=0`、`active_waiters=0`、close **16ms**、retries=17、pauses=29 |
| 10 | 关闭阻塞 | 下载中/未缓存 seek 中关窗（模拟） | ≤1s 退出 | **pass** | 窗口 S7 **7–8ms**、S8 **60–66ms**、S2/S4 5ms、S3 7ms；离线 T13 Cancel ≤100ms、Close 17ms |
| 11 | 休眠恢复 | `FaultTransport.stall_ms=3000`（先交付 600KB、停 3s、同一连接继续）；读线程停顿 | 不挂死、不重复写、可继续 | **pass（模拟）**；**真实 sleep = 未验证** | 窗口 S9：3000ms、单请求 offset=0、`sink==total=3,207,564`、下载完成、seek frame 10、close 15ms；真实 Windows S3/S4 无法自动执行 → **unverified** |
| 12 | DASH 双输入单流失败 | fake DASH JSON + 视/音两 URL；音频 200KB 后持续失败（模拟） | 聚合失败、两路终态、缓存不写完成、错误可读 | **pass** | 离线 T12：错误 1,450ms、「音频下载失败：网络中断（注入）」、视频 job 终态、无 `complete` 条目、Close 0ms、job 全部释放 |

> 真实 CDN 故障（403/过期/断网）本轮**没有**逐场景真打 CDN：注入矩阵用于确定性验收；真实网络只做 M2/quality/longpress/index 正常路径回归，以及 T8.5/T8.8 的历史真实 403/刷新证据。**不把注入可过写成真实 CDN 可过。**

---

## 4. 新增测试入口

### 4.1 `lapian_online_fault_test`（ctest，无 GUI、无网络）

`tests/online_fault_test.cpp` + `src/app/OnlineSession.cpp`（链接 `lapian_online`/FFmpeg/ws2_32）。15 组、**153 项**：

| 组 | 覆盖 |
|---|---|
| T1 | 断网重试 + Range 续传（无重复字节/hash） |
| T2 | 全线路失败：real-ladder 3,981ms ≤5s、`.part` 保留、前缀可读、前沿 EIO、重开续传 |
| T3 | 未缓存 seek 断网：阻塞读 EIO 1,324–1,349ms、缓存区可读 |
| T4/T5/T6 | 403→备份；过期→刷新 1 次；刷新失败可读错误 + 保留 `.part` |
| T7/T8 | API 矩阵 6 种；真实 WinHTTP 4.5s 超时（127.0.0.1 静默服务器） |
| T9 | 看门狗：ready 后静默 → 4.1–4.2s 可读错误、前缀可读、前沿 EIO、Cancel/Close ≤100ms/1s |
| T10 | 缓存截断 → 自动 Rebuild → ready 121–132ms、记录 sentinel 不动、finalize 后 complete |
| T11 | 磁盘不足（2PB）：≤5s 可读、不崩 |
| T12 | DASH 单流失败：聚合失败、两路终态、无 complete、Close 释放 |
| T13 | 缓冲中 Cancel ≤100ms、Close ≤1s |
| T14 | 脱敏：错误/状态/meta 无 SESSDATA/签名参数/完整 URL |
| T15 | 4 读者（默认 8s smoke；`--readers-minutes 10` 正式 10min） |

运行：

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
ctest --test-dir build\release -C Release -R lapian_online_fault_test --output-on-failure
# 10min 4 读者：
build\release\Release\lapian_online_fault_test.exe --readers-minutes 10
```

> **TMP 必须可写**：`QTemporaryDir` 失败时 `CacheStore("")` 会退回真实 `%LOCALAPPDATA%` 缓存；测试已加 `QTemporaryDir invalid → exit 77` 守卫。

### 4.2 `--online-fault-test`（真实窗口，全部注入）

`src/app/OnlineFaultTestRunner.cpp`：注入 `IDownloadTransport`（本地 MP4 字节 + 故障脚本）与 `PlayUrlData`，走 `MainWindow::StartOnlineSession → OnlineSession → DownloadJob → GrowingSource → MediaSession` 全链路；不联网、不需要 SESSDATA。

```powershell
build\release\bin\lapian.exe --online-fault-test --fault-set t8.13-core `
  --bvid BV1BMnv6jE5S --cid 42915922022 --timeout-ms 120000 `
  --online-cache-root build\t8.13-fault\cache `
  --online-pictures build\t8.13-fault\pics `
  --online-records-root build\t8.13-fault\records `
  --settings-path build\t8.13-fault\settings `
  --fault-media build\test-media\faststart-720p.mp4
```

- 默认媒体：`build/test-media/faststart-720p.mp4`（缺省回退 `media/proxy/proxy-V1.mp4`）。
- `--fault-only <scenario>` 只跑一个场景（调试/隔离）。
- 每个场景独立 bvid（`BV1FAULT0001…`），缓存/记录共享同一根，用于验证“标记不丢/重建不清记录”。
- 输出：`online_fault_scenario`（每场景 pass/fail + 耗时）、`online_fault_check`（逐项）、`online_fault_ui_gap`、`online_fault_unverified`、`online_fault_result`（verdict）。
- 探针内建 **UI 冻结看门狗**（30s 无心跳 → `online_fault_hang` + exit 3）与**符号化崩溃栈**（`SetUnhandledExceptionFilter` + DbgHelp；仅探针进程，产品不安装）。

---

## 5. 时间上界与 UI 停顿

| 指标 | 上界 | 实测（窗口级） | 结论 |
|---|---|---|---|
| 断网可读错误 | ≤5s | S2 4,155ms / S3 4,145ms | pass |
| 网络无响应 | ≤5s | S4 4,137ms（stall→error 4,121ms） | pass |
| API 超时 | ≤5s | 4,515–4,529ms（真实 WinHTTP） | pass |
| 未缓存 seek 提示 | ≤100ms | S3 60ms（「缓冲中（目标帧 200）」） | pass |
| 磁盘不足 | ≤5s | S5 5ms | pass |
| 关闭（下载中/seek 中） | ≤1s | 7–8ms / 60–66ms | pass |
| Cancel | ≤100ms | 0–7ms（离线 T9/T13） | pass |
| UI 停顿 >200ms | 0 次 | 全跑 max **73ms**、`over_200=0` | pass |

---

## 6. 失败后可播 / 续传 / 备份 / 刷新 / 损坏 / 磁盘

- **失败后可播**：`Fail` 只让读层 `Fail(EIO)`，不 `Cancel`；已发布前缀读正常、前沿 EIO。窗口 S2 失败后帧 2 落点 + PTS 66,667µs 精确；S3 失败后帧 1 可播；离线 T2/T3 同。
- **断点续传**：S1 同 URL 第 2 请求 offset 409,600；S3 重开 offset 1,228,800；离线 T2 重开 offset=`.part` 长度、hash 一致；10min 压测 17 次瞬时断网后继续。
- **备份切换**：离线 T4 主 403 → 备份 1 次、offset=307,200、只下缺失字节。
- **URL 过期**：离线 T5 刷新 1 次后 offset=307,200；T6 刷新失败 33ms 可读 + `.part` 保留。
- **缓存损坏**：S6/T10 自动 Rebuild；**不把坏数据当缓存命中**（`cache_hit=0`），记录/截图在缓存根外逐字节保留（记录 584B 前后一致）。
- **磁盘满**：S5 在 `CacheStore::Reserve` 预检阶段失败（5ms），不创建/不写坏 `.part`；错误文案含需要/可用字节。
- **DASH 单流失败**：T12 无 `complete` 条目、两路 job 终态、`Close` 后 `job_state=idle`（两套 job/线程已释放）。

---

## 7. 并发 4 读者 10min（正式证据）

命令：`lapian_online_fault_test.exe --readers-minutes 10`（真实 600s）

| 项 | 值 |
|---|---|
| readers | 4（各自 `GrowingCursor`/64KB AVIO，随机 seek + 读；等待超时 60s 仅诊断） |
| 数据 | 合成 8,388,608B；下载 `defer_finalize=true` + `throttle_bytes_per_sec=total/600 ≈ 13.98KB/s` |
| 交错 | 每 20s `Pause()` 1s → `Resume()`（29 次）；开头注入 1 次“交付 128KB 后断连” → 重试续传 |
| ops / waits | **25,907 / 522** |
| deadlocks | **0**（主线程 120s 无 ops 进展判死锁） |
| lost_wakeups | **0**（`GrowingSource::Stats().timeouts=0`） |
| close | **16ms**（Cancel + join 4 reader + job join） |
| published | 8,388,608（完整） |
| retries / pauses | 17 / 29 |
| active_waiters（关闭后） | 0 |

---

## 8. 日志脱敏

- 注入带 `SENTINEL_SESSDATA_8c1d4a` 的请求 + 带 `?deadline=1700000000&upsig=SENTINELSIG123&oi=999` 的签名 URL，触发 403。
- 扫描 `OnlineLastErrorForTest / StatusMessageForTest / OnlineProgressForTest / OnlineStateForTest / OnlineGateDebugForTest` 与缓存 `meta.json`：**无 SESSDATA、无 `deadline=`/`upsig=`、无签名值、无完整 CDN URL**。
- 离线 T14 额外扫描 `.part/meta` 目录；窗口 S10 8/8 通过。
- 外部队列：探针 stdout/stderr 全量日志（`build\t8.13-fault\final2.*.log`）无上述 sentinel；产品错误文案本身只含“网络/HTTP 状态/字节数”。

---

## 9. 回归（2026-10-24，同一工作区）

| 项 | 结果 |
|---|---|
| `ctest` | **35/35**（56.09s；新增 `lapian_online_fault_test` 24.14s，153 项 0 失败） |
| `--online-m2-test` OB-1 | **34/0**；ready 359.2ms、qn80/30.000fps、60s 音画 p95 17.6ms、20/20 seek 落点/PTS 0 失败、内存峰值 212.1MB |
| OB-2 | **34/0**；ready 357.1ms、29.412fps、p95 17.3ms、seek 0 失败、204.6MB |
| OB-3 | **34/0**；ready 361.8ms、25.000fps、p95 20.0ms、seek 0 失败、203.5MB |
| `--online-m1-test --force-durl` OB-1 | **28/0**（close 78ms、qn=64） |
| `--online-quality-test --ui` OB-1/2/3 | **50/0、56/0、50/0**（与 T8.12 基线一致） |
| `--online-index-test` 真实 OB-3 | **27/0**（UI gap 61ms） |
| `--online-index-test` 离线（OB-3 缓存视频+音频） | **22/0** |
| `--online-longpress-test` OB-1（fresh cache） | **54/0**（UI gap 77ms、close 22ms） |
| `--online-cache-manage-test` | **72/0**（workspace）；**98/0**（`--ui`） |
| 本地 `--key-test`（60s 素材） | **77 项 0 失败**（2s 长按 26 步） |
| `bench-regression.ps1 -Suites startup,step1080`（新 exe） | **PASS**：P1 p50/p95 **268.67/282.87ms**、P2 **322.34/330.73ms**、P7 后退 **0.19/0.99/53.82ms**、前进 **0.24/0.36/11.04ms**；0 项超基线 |
| `dist` | **34.3MB / 25 文件**（P12 余量 65.7MB）；便携 zip 14.32MB、setup 11.77MB；无 QtNetwork/TLS/winhttp 导入 |
| 4 读者 10min | **pass**（§7） |

> 说明：M2/quality/index/longpress 走真实网络 + `.secrets\bilibili_sessdata.txt`，用独立根（`build\t8.13-reg\*`）；不写真实 AppData。`--online-cache-manage-test` 只读 ACL 75/0 未重跑（CacheStore 未改，复用 T8.3.1 证据）。

---

## 10. 残余风险与缺口（不冒充通过）

1. **真实 Windows 休眠（S3/S4）未验证**：只做了 `stall_ms=3000` + 读线程停顿的等价注入；QPC/时钟跳变未注入。`online_fault_unverified item=real_windows_sleep` 每次输出。
2. **开发期观察到 1 次崩溃 + 1 次卡住，均未复现**：早期构建上（同一时段有编译/10min 压测并行）出现 `lapian.exe` 0xC0000005（WER offset 0x69790，S6→S7 之间）与一次 UI 卡住（S3 错误等待中）；此后 **20+ 次全量 `--online-fault-test`（含 10 次连续 stress）均 0 崩溃/0 卡住**，未定位根因。已加探针级**符号化崩溃栈 + 30s UI 看门狗**；在定位前**不写“已证明无崩溃”**，标注残余风险。
3. **真实 CDN 故障未逐场景真打**：403/过期/断网以注入为主；真实网络只做正常路径回归 + T8.5/T8.8 历史真实 403/刷新证据。
4. **窗口级 DASH 单流失败未跑**：离线 T12 已覆盖聚合/终态/缓存语义；窗口级 DASH 需要 fake HTTP + 双 URL 脚本，本轮未做。
5. **只读 ACL**：复用 T8.3.1 75/0；本轮未再改 ACL。
6. **seek 侧文案竞态**：断网时 seek 可能先给“到达文件末尾”（后果文案），随后被会话的网络错误覆盖（<100ms）；最终文案正确，但瞬时首条可能不精确。
7. **看门狗阈值**：4s 无写盘即中止；对“服务器静默 >4s 但之后会恢复”的极端慢站点会提前失败（`.part` 保留，可重开续传）。这是为 O7 5s 上界做的取舍。
8. **`kOnlineApiTimeoutMs=4.5s`**：正常请求 100–300ms，M1/M2/quality 全过；极慢网络下会更快失败（可重试）。
9. **UI gap 只在探针内量**：73ms 是窗口级全跑最大值；未做 10min 窗口级 UI gap（并发 10min 是 Session/DownloadJob 级）。
10. **真实 60/4K、HEVC-only** 仍不在 T8.13；不判 M2。

---

## 11. 复现命令与日志

- 窗口级（§4.2）日志：`build\t8.13-fault\final2.out.log` / `final2.err.log`（不入库）。
- 离线：`ctest --test-dir build\release -C Release -R lapian_online_fault_test`；日志 `build\t8.13-fault-unit.log`。
- 10min：`build\t8.13-fault\readers-10min.log`。
- 回归：`build\t8.13-reg\*.log`、`build\t8.13-reg\bench-results2\bench-latest.json`。
- 所有测试根：`build\t8.13-*`；不写真实 AppData / 不提交缓存/截图/记录。

---

## 12. 结论

T8.13 的 12 行故障矩阵全部通过（**10 个窗口级场景 + 15 组离线注入**）：无崩溃/冻结（17 次全量运行；早期 1 次未复现异常见 §10.2）、已缓存可播、重试可续、备份/刷新生效、缓存损坏自动重建、磁盘不足可读、标记 sentinel 不丢、错误 ≤5s、关闭 ≤1s、UI 停顿 >200ms = 0、日志脱敏、4 读者 10min 无死锁。**这是 M2 第 5 项的加固证据，不是 M2 门通过**；M2 门由 T8.14 判定。真实休眠、真实 CDN 故障与窗口级 DASH 单流失败仍是缺口，已明确标注。
