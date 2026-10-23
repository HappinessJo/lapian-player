# T8.6 在线会话/UI 证据（2026-10-23）

- **范围**：只做 T8.6 在线会话/UI 集成。未做 T8.7 缓存命中/在线记录、T8.8 M1 门、DASH/1080P60/4K、扫码；未改本地模式路径、M1/M2 门与验收数字。
- **提交**：本任务一个 commit（hash 见 `git log -1`）；基线 HEAD `d2b915c`。
- **相关文档**：[T8.6 启动交接](../handoff-2026-10-23-t8.6-start.md)、[T8.5 交接](../handoff-2026-10-23-t8.5.md)、[T8.4 交接](../handoff-2026-10-23-t8.4.md)、[delta v0.4](../spec-v1.0-delta.md)、[tasks-v2](../tasks-v2.md)。
- **结论**：真实窗口 3 条样本均完成“输入 BV → 解析确认 → 首帧 → 默认暂停 → Space → PgUp/PgDn → seek → 波形 → Ctrl+M → S → 关窗”；OB-2 仍是 360P，**M1 3/3 720P 仍 blocked，T8.6 不冒充 M1 通过**。

---

## 1. 环境与样本

| 项 | 值 |
|---|---|
| 机器 | DESKTOP-DA6VQGS，Windows 11，RTX 4060（D3D11VA 可用） |
| 构建 | Release，Qt 6.8.3，FFmpeg 7.1.1（最小构建，file/pipe 协议） |
| 真实样本 | OB-1 `BV1BMnv6jE5S`/cid `42915922022`（720P30，14,330,392B）<br>OB-2 `BV1mA411m7pS`/cid `1004967230`（**360P，qn=16**，4,121,631B）<br>OB-3 `BV1GJ411x7h7`/cid `137649199`（720P30，51,973,319B） |
| 缓存 | E2E 用 `build/t8.6-online-cache`（DSH 沙箱下 `%LOCALAPPDATA%` 写入被拒；产品默认根见 T8.3） |
| 首帧口径 | 用户点确认 → `online_ready`（首帧已呈现且 UI 可交互）；不含 view 解析 |

---

## 2. 真实窗口 E2E（3 条）

每条命令：

```powershell
build\release\bin\lapian.exe --online-m1-test --bvid <BV> --cid <cid> --tag <tag> --timeout-ms 30000
```

| 项 | OB-1 | OB-2 | OB-3 |
|---|---|---|---|
| 解析/确认 | 通过 | 通过 | 通过 |
| 首帧 | **604ms** | **700ms** | **503ms** |
| qn / 分辨率 | 64 / 1280×720 | **16 / 640×360** | 64 / 1280×720 |
| 首帧后默认暂停 | 通过 | 通过 | 通过 |
| Space 状态变化 | 0ms | 0ms | 0ms |
| 部分索引 stable | 532 帧 | 557 帧 | 196 帧 |
| PgUp/PgDn 20 次严格 ±1 | 0 失败 | 0 失败 | 0 失败 |
| P7 p50 / p95 | 18 / **20ms** | 19 / **20ms** | 23 / **25ms** |
| 已缓存随机 seek 20 次落点 0 | 0 失败 | 0 失败 | 0 失败 |
| seek p50 / p95 | 35 / **79ms** | 29 / **59ms** | 50 / **97ms** |
| 未缓存目标提示 | “缓冲中（目标帧 3194）” 0ms | “缓冲中（目标帧 2747）” 0ms | “缓冲中（目标帧 5186）” 0ms |
| 改 seek 旧等待取消 | UI 0ms | UI 0ms | UI 0ms |
| 波形 | running（渐进） | running（渐进） | running（渐进） |
| Ctrl+M | 1 条（会话内） | 1 条（会话内） | 1 条（会话内） |
| S 截图 | 1280×720 PNG | 640×360 PNG | 1280×720 PNG |
| UI 最大心跳间隔 | 60ms | 61ms | 62ms |
| 关窗 | **645ms** | 18ms | 250ms |
| UI E2E verdict | pass | pass（quality=blocked） | pass |

说明：

- OB-1 关窗 645ms 是本次截图/标记/下载收尾与 reader join 的最差样本，仍 ≤1s；OB-2/OB-3 为 18/250ms。
- OB-2 的 640×360 截图尺寸来自**当前流**，不是源片；M1 门不因此通过。
- 首帧前均 <1s，因此 O2-A2 的“>1s 在 1s 内出缓冲提示”未触发；缓冲提示路径由 §5 的慢源专项覆盖。
- 每条日志在 `build/t8.6-online-m1-*.log`（不提交），关键行：
  - `online_m1_first_frame ...`
  - `online_m1_p7 ...`
  - `online_m1_cached_seek ...`
  - `online_m1_ui_gap ...`
  - `online_m1_close ...`
  - `online_m1_quality ... gate=blocked-quality`（OB-2）

---

## 3. 冷缓存首帧 20 次 × 3 条

每条样本每次先删除 `build/t8.6-online-cache`，再跑完整 E2E；统计 `online_m1_first_frame ms`。

| 样本 | n | min | p50 | p95 | max | ≤5s |
|---|---:|---:|---:|---:|---:|---:|
| OB-1（720P） | 20 | 591ms | **605ms** | **624ms** | 644ms | 20/20 |
| OB-2（360P） | 20 | 690ms | **698ms** | **725ms** | 726ms | 20/20 |
| OB-3（720P） | 20 | 409ms | **422ms** | **459ms** | 498ms | 20/20 |

- DEC-2 口径：每条 p95 ≤5s、19/20；三条均 **20/20**。
- 实际下行速率和每次 `published` 见各 run 日志；每条冷跑仍完成 P7/seek/标记/截图/关窗。
- 统计脚本/CSV：`build/t8.6-cold/ob{1,2,3}-summary.csv`（build 忽略，不入库）。

---

## 4. 取消与缓冲耗尽

### 4.1 缓冲中取消

`--online-m1-test --cancel-only`：确认后 80ms（仍 `state=buffering`）调用产品取消路径。

| 项 | 结果 |
|---|---|
| 取消调用 | **19ms**（build，dist 快照 2ms） |
| 状态 | `已取消在线加载；缓存 .part 已保留，可再次打开续传` |
| `.part` | 保留（0 字节也保留 meta/part） |
| UI | 回到空态，`OnlineMode=0` |

### 4.2 缓冲耗尽 → 自动暂停 → 恢复

`--online-buffer-test --buffer-media build\test-media\faststart-720p.mp4`（本地 fake CDN：先 1.5MB，停 8s，再补齐）。

| 项 | 结果 |
|---|---|
| 首帧/默认暂停 | pass |
| 播放中耗尽 | **第 145 帧**进入缓冲 |
| 提示 | `缓冲中：第 145 帧（自动暂停） · 等待下载` |
| 恢复 | 从 **第 145 帧**恢复（不黑屏/不跳帧） |
| 恢复后 | 145 → 174（继续推进） |
| 关窗 | 18ms |
| finalize | 关闭后 `data.mp4` 完整落盘（3,207,564B，meta `complete=true`） |
| verdict | **pass 10/10** |

实现：播放前 1MB 视频余量闸门 + 音频 Pump 失速 400ms 看门狗；两者都只触发“自动暂停/提示/恢复”，不在 UI 线程等网络。

---

## 5. 媒体层/线程/部分索引

### 5.1 0.3MB 首帧（真实 OB-1）

- `GrowingFormat::Open` + `ProbeFormat`（跳过包级 VFR 扫描）+ `VideoDecoder::OpenWithFormat` + 首帧：
  - open（含 find_stream_info）**32–39ms**；
  - probe **4–10ms**；
  - decoder open **0.8–1.0ms**；
  - first frame **27–30ms**；
  - 首帧日志示例：`published=524288/14330392`（DownloadJob 以 512KB 块发布，阈值 300KB，因此实际在 512KB 打开）。
- 单测（`lapian_online_m1_test`，faststart-1080p）：0.3MB 前缀 open **9–10ms**、probe 4–5ms、first frame 5–6ms。
- 0.3MB 后 **不调用** `avformat_find_stream_info`/VFR 包扫描的重复阻塞路径。

### 5.2 部分索引

- 后台 `BuildOnlineIndex` 只 demux 已发布前缀，前沿用 100ms `GrowingCursor` 超时结束；下载增长后每 ~650ms 重建；完整下载后才包含全部帧。
- 未完整时只暴露“稳定前缀”，末尾保留 48 帧不暴露，避免 B 帧/PTS 排序导致已显示帧号漂移；安装前逐帧比较旧索引前缀 PTS，不一致则拒绝安装。
- 帧号/落点：已缓存区走 `MediaSession::SeekToFrame`（v1 精确路径）；未覆盖目标用 CFR 估算 PTS + `SeekToEstimatedPts`，解码后校验与目标 PTS 偏差 ≤半帧，偏差大则报错，不冒充目标帧。
- 未缓存 seek 的阻塞读在独立 worker；新目标/关窗通过 `GrowingCursor` 单读者 interrupt（25ms 粒度）取消，不取消共享 `GrowingSource`。
- 实测：已缓存 P7 p95 20–25ms；已缓存随机 seek p95 59–97ms；未缓存目标提示 0ms、旧等待取消 UI 0ms、落点 0。

### 5.3 完成前 finalize / reader

- `DownloadJob` 新增默认关闭的 `defer_finalize`：写完/关写句柄/`Publish(total)` 后进入 `kAwaitingFinalize`，不 rename。
- `OnlineSession::FinalizeAfterReadersStopped()` 在 MainWindow 停掉音频/波形/索引/seek/MediaSession 之后调用，内部仍走 `CacheStore::MarkComplete`。
- 缓冲专项关闭后得到 `data.mp4` + `meta.complete=true`，证明 reader 先停、rename 不 busy；未改 CacheStore 写语义或 reader 的 `FILE_SHARE_DELETE`。

### 5.4 错误/取消

- `lapian_online_m1_test` 的 fake 失败传输：error callback **≤5s**、`kError`、`.part` 保留；不崩溃。
- 真实 403/备份/刷新/续传由 T8.5 证据覆盖；T8.6 只验证 UI 错误回调与缓存保留。

---

## 6. 本地回归

### 6.1 ctest

```text
100% tests passed, 0 tests failed out of 29
```

新增 `lapian_online_m1_test`：**48 checks / 0 failures**（含 OpenGrowing/首帧、部分索引、cached/uncached seek、interrupt、OnlineSession ready/finalize/cancel/error）。

### 6.2 P1/P2（同机、同素材、20 次）

同机 `dist\lapian.exe`（T8.5 基线）与本次 `build\release\bin\lapian.exe` 对比：

| 项 | 基线 dist p95 | 本次 p95 | 判定 |
|---|---:|---:|---|
| P1 | 285.31ms | **290.68ms** | pass 20/20（≤1500ms；+1.9%） |
| P2 | 344.55ms | **347.43ms** | pass 20/20（≤2500ms；+0.8%） |

- 同机二次复跑（287.70/345.40）在噪声范围内；未把在线模块初始化放进启动路径。
- 本地模式零网络：运行本地文件 3s，`netstat` 0 TCP/UDP 行，进程未加载 `winhttp/wininet/urlmon/libcurl`；`check-imports` 20 文件通过。

### 6.3 P7（本地 2s GOP）

`scripts\gop-boundary-bench.ps1 -File build\test-media\real-gop2s-full.mp4 -Samples 20`：

```text
2s_n=20 2s_p50_ms=60.17 2s_p95_ms=66.59 2s_max_ms=67.62 2s_over=0
verdict=pass
```

基线 65.20ms（交接数字）；本次 +1.39ms，仍 ≤80ms。

### 6.4 既有在线探针

| 探针 | 结果 |
|---|---|
| `--online-fetch-test` | 46 / 0，pass |
| `--online-qr-test` | 19 / 0，pass |
| `--online-qr-test --ui` | 21 / 0，pass |
| `--online-auth-test` | 54 / 0 |
| `--online-resolve-test` | 156 / 0 |
| `--online-cache-test` | 32 / 0，pass |
| `--online-growing-test` | 22 / 0，pass（18.0s） |

---

## 7. 分发

- `scripts\build-release.ps1 -SkipTests`：**33.57MB / 25 文件**（P12 上限 100MB，余量 66.43MB）。
- `scripts\check-imports.ps1`：20 个 exe/dll 通过；无 QtNetwork/TLS/winhttp/wininet/urlmon。
- dist 冒烟：`dist\lapian.exe --online-m1-test --cancel-only ...` pass（2ms）。

---

## 8. 明确未做/限制

1. **M1 3/3 720P 仍 blocked**：OB-2 实际 qn=16/640×360；T8.6 只证明“能播/能操作”，T8.8 才判 M1。
2. **缓存命中（完整 `.mp4` 打开）未做**：`DownloadJob::Start` 的 `already_complete` 在 T8.6 显示明确错误，T8.7 接 `OpenComplete`/0 请求。
3. **在线标记/截图持久化未做**：T8.6 只做会话内标记（`MarkStore::OpenSessionOnly`，不落盘）与图片目录 PNG；完整 `lapian-online-1`/`index.jsonl` 是 T8.7。
4. **部分索引只暴露稳定前缀**：未完整下载时末尾 48 帧不进入索引；超出索引的 CFR 目标用估算 PTS 并校验半帧，VFR/无帧率时不冒充精确。这是 T8.6 的诚实限制。
5. **未缓存 seek 的等待不承诺毫秒数**：只承诺 ≤100ms 提示/取消与落点 0；远目标实际等待取决于下载。
6. **真实断网/403 UI 复跑未做**：底层由 T8.5 真实 403/刷新覆盖；T8.6 用 fake 失败传输验证 UI error/`.part`。
7. **DSH 沙箱不能写 `%LOCALAPPDATA%`**：E2E 用 `build/t8.6-online-cache`；默认缓存根仍由 T8.3 的 CacheStore 单测覆盖。
8. **未做 DASH/1080P60/4K/扫码**：不在 T8.6。
9. **OB-2 首帧前的 `h264 missing picture` 日志**：来自 B 站 360P 流的空/短 access unit，首帧/seek/截图均正确；未作为质量通过依据。

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

build\release\bin\lapian.exe --online-m1-test --bvid BV1BMnv6jE5S --cid 42915922022 --tag ob1 --timeout-ms 30000
build\release\bin\lapian.exe --online-m1-test --bvid BV1mA411m7pS --cid 1004967230 --tag ob2 --timeout-ms 30000
build\release\bin\lapian.exe --online-m1-test --bvid BV1GJ411x7h7 --cid 137649199 --tag ob3 --timeout-ms 30000
build\release\bin\lapian.exe --online-m1-test --cancel-only --bvid BV1BMnv6jE5S --cid 42915922022
build\release\bin\lapian.exe --online-buffer-test --buffer-media build\test-media\faststart-720p.mp4
```

冷缓存 20 次：每次先 `Remove-Item -Recurse -Force build\t8.6-online-cache`，再跑完整 E2E，解析 `online_m1_first_frame ms=`。

---

## 10. 结论

T8.6 在真实窗口完成 3 条 E2E、60 次冷缓存首帧（20/20/20 均 ≤5s）、已缓存 P7/seek/未缓存提示/取消、缓冲耗尽自动暂停恢复、波形/Ctrl+M/S、完成前 finalize 与关窗 ≤1s；本地 P1/P2/P7 不退化，ctest 29/29，dist/check-imports 通过。**M1 门未判，OB-2 仍 360P，M1 3/3 720P 仍 blocked。**
