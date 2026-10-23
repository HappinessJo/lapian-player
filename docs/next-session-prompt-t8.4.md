# 下一个任务：T8.4 可增长读层（v2 在线 · M1 关键路径）

> 给接手 agent 的自包含提示词。目标：**只做 T8.4**，做完一个可审查提交。不要顺手做 T8.5/T8.2.2/T8.6。
> 当前仓库：分支 `master`，HEAD 应为 `6c8eaee`（docs 同步）或其后只含文档的提交；工作树 clean；`ctest` 25/25。

---

## 0. 一句话

实现 `GrowingSource` + 每读者 `Cursor`/`AVIOContext`，让 FFmpeg 能在“边下边读”的 `.part` 上打开、seek、取消；**不发起任何 HTTP**（T8.5 才做取流），**不接 UI**（T8.6）。验收门：**0.3MB 可出首帧、100 次 seek 落点 0、已缓存 seek 零阻塞、取消 ≤100ms、4 读者×10min 无死锁/丢唤醒、关窗 ≤1s、内存有界、无 busy loop**。

## 1. 先读（按顺序，不要凭记忆）

1. `AGENTS.md`（v6.15）：§1 v2 状态、§7 明确不做、§11 协作规则。
2. `docs/spec-v1.0-delta.md` v0.4：O1/O3/O5/O9、§2.5 缓存、§6.1 白名单。
3. `tasks-v2.md` v0.7：**T8.4 行**（“验收标准”列就是 DoD）；T8.5 行只看边界。
4. `plan.md` v2 章节：**§12.2.1 线程 / §12.2.2 多读者 / §12.3.1 输入→首帧 / §12.4 可增长读层**（尤其 §12.4.1 的 AVIO 语义）。
5. `docs/handoff-2026-10-23-t8.3.md` **§6**（与 `CacheStore` 的边界、锁顺序、句柄/淘汰）。
6. `docs/adr/ADR-007-bilibili-online.md`：D2（增长文件 + AVIO）、D4（DASH 双输入）。
7. `src/online/CacheStore.h`：`OpenPart`/`CacheEntry`/`Load`/`EntryDir`。

## 2. 当前状态（T8.0–T8.3 已完成）

| 任务 | 状态 | 关键接口/证据 |
|---|---|---|
| T8.1 解析 | ✅ | `BiliResolver`；`--online-resolve-test` 156/0 |
| T8.2 HTTP/凭据 | ✅ | WinHTTP nav 161–202ms；SESSDATA/DPAPI；`--online-http-selfcheck`、`--online-auth-test` 54/0 |
| T8.3 缓存 | ✅ | `CacheStore`：`OpenPart`（路径 + `existing_bytes`）、`OpenComplete`、`Load`/`Scan`、`Reserve`/`UpdateProgress`/`MarkComplete`、`EvictToQuota`、`Rebuild`/`ClearAll`/`CleanupOrphans`；`lapian_online_cache_test` 366/0、`--online-cache-test` 32/0；提交 `8483e24` |

**未做**：T8.4 本任务、T8.5 取流、T8.2.2 扫码（非门，排 T8.4 后）、T8.6 会话 UI、T8.7 命中/记录。

## 3. 范围

### 做（建议文件名，可按现有风格微调）

- 新增 `src/online/GrowingSource.h/.cpp`，链入 `lapian_online`：
  - `int64_t Size()`：已知总字节（打开前必须已知，否则拒绝进入可增长播放）。
  - `int64_t Published()`：已发布**连续前缀**（MVP 无空洞）。
  - `bool WaitFor(int64_t offset, ...)`：阻塞到 `offset < Published`，或取消/失败；**必须可被 `Cancel()` 唤醒**。
  - `void Publish(int64_t n)`：下载线程调用，单调递增，`n ≤ Size()`。
  - `void Fail(int av_error)`：不可恢复失败，唤醒所有等待者。
  - `void Cancel()`：关窗/换片；唤醒所有等待者。
  - `Stats`：等待次数、累计等待 ns、首帧前等待、seek 等待、取消延迟、发布速率。
  - **不自建线程**；`mutex + condition_variable`；内部锁不得回调上层。
- `Cursor`：每读者独立 `pos` + 只读文件句柄 + 自己的 `AVIOContext`（`avio_alloc_context`，64KB 缓冲，`write_flag=0`）。
- 打开器：从 `.part` 只读打开，`FILE_SHARE_READ | FILE_SHARE_WRITE`（**不要 `FILE_SHARE_DELETE`**；这样 `EvictToQuota` 才能返回 busy + `evict_pending`）。
- AVIO 回调：
  - `read_packet`：`pos >= Size()` → `AVERROR_EOF`；未发布 → `WaitFor` 后继续；取消 → `AVERROR_EXIT`；失败 → `AVERROR(EIO)`（**绝不能把失败返回 EOF**）。
  - `seek`：支持 `SEEK_SET/CUR/END` 与 `AVSEEK_SIZE`（返回 `Size()`）；只改 `Cursor.pos`，**不触发网络**。
  - `AVFormatContext` 设 `AVFMT_FLAG_CUSTOM_IO` 并把 `pb` 指向自定义 AVIO；不要交给 FFmpeg 释放。
- `MediaSession` 的“从 GrowingSource 打开”入口**本任务可以不做**；若做，只加最小入口、不改本地路径分支。T8.4 的验收用独立测试/探针完成。
- 单测 `tests/growing_source_test.cpp` + ctest `lapian_growing_source_test`。
- 产品探针 `--online-growing-test`（无 GUI、**离线**、合成/真实样本前缀、fake publisher），输出稳定 `key=value`。
- 证据 `docs/perf/v2-online-growing.md` + 交接 `docs/handoff-YYYY-MM-DD-t8.4.md`。

### 不做

- ❌ HTTP/Range/playurl/下载/重试/备份 URL（T8.5）。
- ❌ `MediaSession` 完整集成、缓冲 UI、OnlineSession 状态机（T8.6）。
- ❌ 修改 `CacheStore` 的写语义；❌ 改 `spec.md`/`tasks.md`/M1/M2 门/验收数字；❌ 改 v1 冻结文件。
- ❌ DASH/双流、扫码登录、自动读浏览器 Cookie。

## 4. 硬约束（违反即返工）

1. **锁顺序**：`OnlineSession → CacheStore → GrowingSource`；读层内不得调用 `CacheStore`，`CacheStore` 也不得回调读层。
2. **每读者独立**：独立 `AVIOContext`、独立 `Cursor`、独立文件句柄；共享的只有 `GrowingSource` 的范围/条件变量。
3. **取消语义**：`Cancel()` 置位 + `notify_all`；阻塞中的 `WaitFor` 立即返回 `AVERROR_EXIT`；取消到所有读者退出 ≤100ms（不含解码本身）。
4. **失败 ≠ EOF**：下载失败返回 `AVERROR(EIO)`；只有 `pos >= Size()` 才能 `AVERROR_EOF`。
5. **无 busy loop**：等待必须 `condition_variable::wait`（带谓词），禁止 sleep/spin 轮询。
6. **int64**：所有偏移/大小用 `int64_t`；测试至少覆盖 4GiB−1 的算术（稀疏文件，不真写 4GB）。
7. **总大小未知拒绝**：`Size() <= 0` 或未知时，打开器返回可读错误，不进入可增长播放。
8. **句柄生命周期**：读者先 `Cancel` + join，再销毁 `GrowingSource`；析构时断言无等待者。
9. **本地零网络**：不新增任何网络调用；`check-imports.ps1` 导入表不得新增网络 DLL。
10. **不伪造数字**：0.3MB/10min/取消延迟都必须实测；达不到就写“未达 + 实测值”，并按 §7 停下提问。

## 5. DoD（来自 tasks-v2 T8.4，逐条给证据）

| # | 验收 | 方法（建议） |
|---|---|---|
| 1 | 0.3MB 可出首帧 | 用 **faststart** 样本（moov 在前）；只发布前 307200 字节，打开+解码首帧成功；记录实际所需字节与 ms |
| 2 | 100 次 seek 落点 0 | 在已发布前缀内随机 seek，`avio_seek` 返回值 == 请求偏移；100/100 |
| 3 | 已缓存 seek 零阻塞 | `WaitFor` 在已发布区立即返回；等待计数不增加 |
| 4 | 未缓存等待 ≈ 剩余/速率 | fake publisher 固定速率（如 4MB/s），实测等待与 `(offset-Published)/rate` 误差 ≤30%，记录 |
| 5 | 取消 ≤100ms | 读者阻塞在未发布区 → `Cancel()` → 记录到 `AVERROR_EXIT` 的 ms；≤100 |
| 6 | 4 读者×10min 无死锁/丢唤醒 | `--online-growing-test --minutes 10 --readers 4`；0 死锁、0 丢唤醒、0 busy loop；ctest 里只跑短版（≤30s） |
| 7 | 关窗 ≤1s | 4 读者 + publisher → `Cancel` + join 全部 ≤1s |
| 8 | 内存有界 | 4 读者 × 64KB AVIO + 统计；进程 WS 增量记录（建议 ≤64MB） |
| 9 | 无 busy loop | 等待 5s 期间进程 CPU 时间 <0.2s（或等待次数 == 唤醒次数） |
| 10 | EOF/EIO/EXIT 三分 | 单测逐项断言 |
| 11 | 淘汰 busy | 持读者句柄时 `EvictToQuota` 返回 busy + `evict_pending`；释放后可删 |
| 12 | 回归 | `ctest` 26/26；本地 P1/P2/P7 不退化；`check-imports.ps1` 20 文件不变 |

## 6. 测试与证据要求

- 单测：`lapian_growing_source_test`（纯 C++/Qt Core，无网络、无 GUI、临时目录；复用 `tests/online_cache_test.cpp` 的稀疏文件/独占句柄 helper）。
- 探针：`--online-growing-test`，建议输出：
  ```
  root=... expected=... published=... first_frame_bytes=... first_frame_ms=...
  reads=... seek_100_miss=0 wait_ms=... wait_expected_ms=...
  cancel_ms=... readers=4 minutes=... deadlocks=0 lost_wakeups=0 busy_loops=0
  peak_ws_mb=... verdict=pass
  ```
- 证据文档 `docs/perf/v2-online-growing.md`：环境、API、单测表、0.3MB、100 seek、等待/速率、取消、4 读者 10min、关窗、内存、无 busy loop、EOF/EIO/EXIT、淘汰 busy、回归、缺口、复现命令。
- 交接 `docs/handoff-YYYY-MM-DD-t8.4.md`：状态/提交/接口/数字/决定/缺口/T8.5 与 T8.6 需要。
- 文档同步：`tasks-v2.md` T8.4 行标完成 + 版本 v0.8；`AGENTS.md` v6.16（状态 + 文档表 + 变更记录）。**不改门、不改数字。**

## 7. 停止条件（命中就写 blocked 证据并提问，不要硬做）

1. `CacheStore` 契约不足（例如需要读层先拿写句柄/需要改 `OpenPart` 语义）。
2. 0.3MB 首帧在 faststart 样本上达不到（先报告实际阈值与 moov 位置，问是否调整验收，不要伪造）。
3. 取消无法 ≤100ms（FFmpeg 阻塞点不可唤醒），需改设计或改门。
4. 需要新增网络/新依赖/提权/改 O9 或 M1/M2。
5. T8.4 与 T8.5 边界冲突（例如必须先实现 HTTP Range 才能测）。

## 8. 环境与命令

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP = $env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure
```

基线（改代码前先跑）：

```powershell
build\release\bin\lapian.exe --online-cache-test        # 32/0, verdict=pass
build\release\bin\lapian.exe --online-resolve-test      # 156/0（联网）
build\release\bin\lapian.exe --online-auth-test         # 54/0（不联网）
```

faststart 样本（仓库没有 ffmpeg.exe 产品运行时；构建期工具在 `third_party\tools\ffmpeg-gpl`，仅测试用、不要提交大文件）：

```powershell
New-Item -ItemType Directory -Force build\test-media | Out-Null
& third_party\tools\ffmpeg-gpl\bin\ffmpeg.exe -y -i media\real-gop2s-full.mp4 -t 8 `
  -c copy -movflags +faststart build\test-media\faststart-720p.mp4
# 校验 moov 在前：-movflags +faststart 已保证；如需确认可看 ffmpeg -v trace 或 ffprobe
```

> 若 `media\real-gop2s-full.mp4` 不适合，换 `media\测试.mp4`；样本只放 `build\`（不入库）。

## 9. 提交与文档规范

- **一个任务一个 commit**：`T8.4: growing source (multi-reader AVIO, cancel, stats; offline)`；代码 + 测试 + 证据 + 交接 + tasks/AGENTS 一起提交。
- 不提交 `.secrets/`、不提交大样本、不提交 `build/`。
- 提交前：`git status` 只应包含本任务文件；`ctest` 26/26；`--online-growing-test` verdict=pass（短版）。
- 完成后可在交接里写“下一任务 T8.5”，并把 `T8.4` 行标完成、版本升 `tasks-v2 v0.8`、`AGENTS v6.16`。

## 10. 已知坑（省你半天）

1. **FFmpeg 会把“读不到”当文件结束**：失败必须 `AVERROR(EIO)`，不能 `AVERROR_EOF`；否则容器解析出半截索引，seek 全错。
2. **moov 位置**：非 faststart 的 mp4，0.3MB 内根本打不开；先确认样本 `moov` 在前，否则 0.3MB 验收会误判为读层失败。
3. **`avio_seek` 的 `AVSEEK_SIZE`**：必须返回 `Size()`；否则 FFmpeg 会认为不可 seek，帧精确退化。
4. **共享句柄 ≠ 共享游标**：多个读者共用一个 `AVIOContext` 会互相踩 `pos`；必须每读者一个。
5. **Windows 共享删除**：读者带 `FILE_SHARE_DELETE` 会让 `EvictToQuota` 误删正在读的文件；不带则淘汰返回 busy，这是预期。
6. **publisher 与 reader 的竞态**：`Publish` 必须在写盘后调用；`Published` 不得超前于实际文件长度（可 `GetFileSizeEx` 对账）。
7. **取消后再 publish**：`Cancel` 后 publisher 继续写没有意义；测试里要断言“取消后不再有新发布进入等待者”。
8. **测试时长**：10min 压测放探针，不进 ctest；ctest 短版用 4 读者 × 10s 即可，但证据文档必须有一次真实 10min。
9. **别改 `CacheStore`**：需要新字段就先在交接里记录，T8.5 再一起改；T8.4 只消费 `OpenPart` 的路径/长度。
10. **别把读层塞进 UI 线程**：`WaitFor` 只能在解码/索引/波形线程调用。

## 11. 交付清单

- [ ] `src/online/GrowingSource.h/.cpp`（链入 `lapian_online`）
- [ ] `tests/growing_source_test.cpp` + ctest `lapian_growing_source_test`
- [ ] `--online-growing-test`（`src/app/OnlineGrowingTestRunner.*` + `main.cpp` 分流 + `src/CMakeLists.txt`）
- [ ] `docs/perf/v2-online-growing.md`
- [ ] `docs/handoff-YYYY-MM-DD-t8.4.md`
- [ ] `tasks-v2.md` T8.4 完成 + v0.8；`AGENTS.md` v6.16
- [ ] `ctest` 26/26；短版探针 `verdict=pass`；10min 证据；本地 P1/P2/P7 回归；`check-imports.ps1` 不变
- [ ] 工作树 clean，一个 commit
