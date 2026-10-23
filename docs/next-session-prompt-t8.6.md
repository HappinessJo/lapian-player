# 下一个任务：T8.6 在线会话/UI（v2 在线 · M1 关键路径）

> 给接手 agent 的自包含提示词。目标：**只做 T8.6**，做完一个可审查提交（或经用户同意拆成 T8.6a/b/c）。
> 不要顺手做 T8.7（缓存命中/在线记录）、T8.8（M1 门）、DASH/1080P（T8.9+）、扫码（T8.2.2 已完成）。
> **不要改本地模式路径、M1/M2 门或验收数字；不要把 OB-2 的 360P 写成 720P。**
> 当前仓库：`master`，HEAD `a88b7a4`（T8.2.2），前一提交 `7df62fd`（T8.5）；工作树 clean；`ctest` **28/28**；`dist` 33.42MB/25 文件。

---

## 0. 一句话

把已经完成的 T8.5 下载/读层真正接到 UI：**输入 BV → 确认 → playurl → CacheStore + GrowingSource + DownloadJob → 等 0.3MB → GrowingFormat/MediaSession 首帧 → 默认暂停 → Space/PgUp/PgDn/seek/波形/Ctrl+M/S → 关窗**。本地模式必须原样可用。

**先读**：`docs/handoff-2026-10-23-t8.6-start.md`（启动交接，含架构缺口/推荐方案/阶段划分）。本提示词是执行版；两者一起读。

---

## 1. 先读（按顺序，不要凭记忆）

1. `AGENTS.md` v6.19：§1 v2 状态、§7 明确不做、§11 协作规则。
2. `docs/spec-v1.0-delta.md` v0.4：**O2**（起播/缓冲/取消）、**O3**（已缓存 seek/步进/波形/标记/截图）、**O4**（未缓存等待/取消/断网）、**O6**（档位/fps/换档提示）、**O9**（本地零网络）、§6.1 白名单、§9.0 DEC-2（5s/80ms/2.5s/100ms）。
3. `tasks-v2.md` v0.11：**T8.6 行**（验收列=DoD）；T8.7/T8.8 行只看边界。
4. `plan.md` v2：**§12.2.1 线程**、**§12.3.1 输入→首帧**、**§12.3.2 已缓存/未缓存 seek**、**§12.3.3 缓冲耗尽**、**§12.3.4 完成/关闭**、**§12.4 可增长读层**。
5. `docs/handoff-2026-10-23-t8.6-start.md` §2/§3/§4/§5（本任务核心交接）。
6. `docs/handoff-2026-10-23-t8.5.md` §2/§3/§6（DownloadJob 契约、完成前停 reader、T8.6 需要）。
7. `docs/handoff-2026-10-23-t8.4.md` §2/§3/§6（GrowingSource/GrowingCursor/GrowingFormat、EOF/EIO/EXIT、锁顺序、多读者）。
8. 代码：
   - `src/app/MainWindow.cpp`：`ShowOnlineDialog()` 的 `confirmed` 只显示状态栏；`OpenFile()` 是本地路径唯一入口。
   - `src/app/OnlineResolveDialog.h/.cpp`：`confirmed(bvid,cid,page)`；`video_` 里有标题/UP/分P；没有 width/height/fps。
   - `src/media/MediaSession.h/.cpp`、`MediaProbe.h/.cpp`、`VideoDecoder.h/.cpp`：全部按路径 `avformat_open_input`。
   - `src/audio/AudioDecoder.h`、`WaveformGenerator.h`：同样按路径打开。
   - `src/media/FrameIndex.h/.cpp`：`BuildFromFile` 全文件 demux；无部分索引。
   - `src/online/GrowingSource.h`、`DownloadJob.h`、`PlayUrl.h`、`CacheStore.h`。
   - `src/app/PlaybackController.h`/`GfxWidget.h`：v1 在 UI 线程解码/呈现。

---

## 2. 当前状态

| 任务 | 状态 | 关键接口/证据 |
|---|---|---|
| T8.1 解析 | ✅ | `BiliResolver`/`BiliResolveClient`；`--online-resolve-test` 156/0 |
| T8.2 HTTP/凭据 | ✅ | WinHTTP/DPAPI/nav；`--online-auth-test` 54/0 |
| T8.3 缓存 | ✅ | `CacheStore` Reserve/OpenPart/UpdateProgress/MarkComplete/OpenComplete/SetMetadata；32/0 |
| T8.4 读层 | ✅ | `GrowingSource/Cursor/GrowingFormat`；0.3MB 首帧、取消 0.15ms、4 读者 10min；22/0 |
| T8.5 下载 | ✅ | `PlayUrl`/`DownloadTransport`/`DownloadJob`；真实 OB-1/OB-3 720P，**OB-2 360P → M1 3/3 blocked**；46/0 |
| T8.2.2 扫码 | ✅（非门） | 登录 UI 已接；真实扫码未验证；不影响 T8.6 |
| T8.6 本任务 | **待做** | 本文 + 启动交接 |

**当前 UI 的事实**：`MainWindow::ShowOnlineDialog()` 的 `confirmed` 只显示“取流将在 T8.5 接入”；点确认不下载、不播放。`OpenFile()` 仍只走本地 `MediaSession::Open(path)`。

---

## 3. 范围

### 做（按阶段，允许先做最小闭环）

1. **`OnlineSession` 状态机**
   - `Idle/Resolving/Preparing/Downloading/Buffering/Ready/Playing/Paused/SeekingWait/Failed/Cancelled/Completed`（名字可按现有风格调）。
   - `Confirm(bvid,cid,page)`：`FetchPlayUrl(qn=64, fnval=0)` → 校验 `quality>=64`（低于 64 记录实际档位并 blocked/failed，不伪造）→ `DownloadPlan`。
   - `CacheKey{bvid,cid,data.quality,data.codecid}`；`CacheMetadata` 写 title/owner/duration；width/height/fps 在探测后补。
   - `DownloadJob` 的 refresh 回调：一次 `FetchPlayUrl`，带凭据；不打印签名 URL/Cookie。
   - `Cancel/Close`：置 token、停 reader、`Wait`/join；`.part` 保留；≤100ms UI 可操作。
2. **首帧**
   - `GrowingSource(total)` → `DownloadJob::Start`；
   - 等 `Published >= 307200`（T8.5 实测 0.3MB；实际可能按 256KB 块到 524288）→ `MediaSession::OpenGrowing`/`GrowingFormat` → 首帧；
   - **首帧后默认暂停**，`Space` 播放/暂停 ≤100ms；
   - 等待期间 UI 必须可交互，显示“缓冲中”+进度；取消 ≤100ms。
3. **播放/导航**
   - 已缓存区：PgUp/PgDn 严格 ±1、seek 精确；缓存 P7 p95 ≤80ms；
   - 未缓存：≤100ms 显示“缓冲中（目标帧 N）”，画面保持；可取消旧等待；数据到后落点误差 0；不冒充完整索引。
   - 缓冲耗尽：自动暂停 + 提示；恢复后从当前帧继续；不黑屏/跳帧。
4. **索引/波形**
   - 部分索引覆盖已下载前缀；下载增长时增量/周期推进；UI 显示“扫描中/已缓存 N 帧”，不要显示假全片总帧数。
   - 波形渐进：已生成区间与帧刻度偏移 ≤1 帧；生成中不阻塞步进/拖动。
5. **音频**
   - M1 durl 是带音轨 MP4；Space 播放应有音频（或明确记录无法完成并报告，不要静默静音）。
   - 需要第二个 `GrowingFormat`/cursor；不要共享 cursor/FFmpeg context。
6. **标记/截图**
   - E2E 要 `Ctrl+M` 与 `S`；T8.6 至少做会话内可用；完整在线记录/目录/清单是 T8.7。
   - 不要污染本地 sidecar；用在线标识（如 `lapian-online-1`）或缓存 key。
7. **UI/关闭**
   - 显示标题/BV/分P/实际档位/缓冲状态/下载速率/已缓存字节/错误；
   - 关窗：取消下载、停 reader、join、≤1s 退出；不挂起；
   - 本地模式零网络、P1/P2/P7 不退化。

### 不做

- ❌ T8.7 缓存命中/断网打开/在线记录/`index.jsonl`；T8.8 M1 门；T8.9 DASH；T8.10 双流。
- ❌ 1080P60/4K/大会员档；不把 1080P60 当 MVP 验收。
- ❌ 改 `CacheStore` 写语义、`.part` 连续前缀、`FILE_SHARE_DELETE`；不改本地 `OpenFile` 行为。
- ❌ 改 M1/M2 门/验收数字；不改 `spec.md`/`tasks.md`/`plan.md` v1 正文。
- ❌ 用“先下完整片再打开”、假 720P、假索引、UI 阻塞换通过。

---

## 4. 核心架构问题（必须先解决/先 spike）

### 4.1 媒体层只接受路径

- `MediaSession::Open(path)` → `ProbeMedia(path)` + `VideoDecoder::OpenWithD3D11Device(path)`；两者各自 `avformat_open_input`。
- `AudioDecoder::Open(path)`、`WaveformGenerator::Start(path)`、`PreviewDecoder`、`FrameIndex::BuildFromFile(path)` 同样。
- `GrowingFormat` 已能打开自定义 AVIO context，但**没有消费者**。
- **不要**直接传 `.part` 路径：FFmpeg 到当前 EOF 会 EOF，未发布区不会阻塞，会把下载中误判为文件结束。

**推荐方案 A：注入 `AVFormatContext`**

- `MediaProbe` 抽出 `ProbeFormat(AVFormatContext*, ...)`（非拥有，复用现有解析）。
- `VideoDecoder::OpenWithFormat(AVFormatContext*, stream_index, ...)`：非拥有，decoder 不关闭 context。
- `MediaSession::OpenGrowing(shared_ptr<GrowingSource>, part_path, device, ...)`：`GrowingFormat::Open` → `ProbeFormat` → `VideoDecoder::OpenWithFormat` → 首帧；`MediaSession` 持有 `GrowingFormat`/source，生命周期长于 decoder；Close 顺序反析构。
- 音频/波形同理：`AudioDecoder::OpenWithFormat`、`WaveformGenerator::StartWithFormat`，或各自独立 `GrowingFormat`/cursor；**一个 FFmpeg context 只属于一个线程**。
- 备选 B：FFmpeg 自定义协议 `lapianpart:`（少改 path API，但全局注册/生命周期/测试隔离风险）；只有 A 被证伪才考虑。
- **拒绝 C：先下完整片再 `Open(path)`**（违背 5s 起播/0.3MB 首帧）。

### 4.2 部分索引

- `FrameIndex::BuildFromFile` 全文件；`FrameIndex` 无部分状态。
- 不能等整片下完才允许 PgUp/seek；也不能把当前前缀帧数冒充全片。
- 建议：后台从已发布前缀建部分索引（`BuildFromFormat` 或从 entries），下载增长时增量/周期推进；UI 标“扫描中”；已缓存区走 v1 精确 seek；目标超出索引 → `SeekingWait`。
- 索引不可靠时按 T2.3 降级/明确提示，不伪造精确。

### 4.3 UI 线程阻塞

- `GrowingCursor::Read` 在未发布区阻塞；`MediaSession::SeekToFrame` 可能触发。
- T8.6 要求 UI 停顿 >200ms = 0；未缓存 seek ≤100ms 显示缓冲。
- 先 spike：
  1. 0.3MB 后在**同一线程** `GrowingFormat::Open`+首帧：记录 open/probe/frame/实际读取/阻塞。T8.5 实测 open 8–9ms、首帧 10–14ms；若这里也快，首帧可留在 UI（但等待 0.3MB 必须异步）。
  2. UI 线程 seek 到未发布区，测阻塞；>200ms 必须媒体 worker/异步 seek。
- 可考虑给 `FrameIndexEntry` 加 `pos`，用“目标字节已 Published”做前置等待；但需验证，不要猜。
- 硬规则：UI 不等网络/磁盘/阻塞读；结果带 generation，旧结果不覆盖新目标。

### 4.4 完成改名 vs reader

- `DownloadJob::FinishCompleted()` 写完立刻 `MarkComplete`（rename）；reader 句柄不带 DELETE，reader 未关会 rename 失败 → Job `Fail(EIO)` + source `Fail`，正在播会断。
- 可选方案（推荐）：给 `DownloadJob` 加**默认关闭**的 `defer_mark_complete`/finalize 钩子：写完/关写句柄/`Publish(total)` 后进入 `kAwaitingFinalize`；`OnlineSession` 在 UI 线程停 reader 后调用 `FinalizeComplete()`，内部仍走 `CacheStore::MarkComplete`。T8.5 探针默认行为不变。
- 不改 `CacheStore` 写语义/reader 的 `FILE_SHARE_DELETE`；不用“rename 失败重试”掩盖 reader 未停。
- 若不想改 `DownloadJob`，必须先证明“完成前 reader 已关闭”的时序，否则会踩 busy。

---

## 5. 实现顺序（建议）

### 阶段 1：最小在线闭环（先证明架构，先不碰 seek/音频/波形）
1. `MediaSession::OpenGrowing` + `VideoDecoder::OpenWithFormat` + `ProbeFormat`；离线用 T8.5 下载好的 `.part` 做首帧 spike。
2. `OnlineSession`：Confirm→playurl→plan→cache→source→job；等 0.3MB→OpenGrowing→首帧；默认暂停；Space；Close。
3. `MainWindow`：`confirmed` 不再只显示状态栏，启动 OnlineSession；显示标题/BV/缓冲；关闭取消/join。
4. 真实 OB-1 一条跑通：首帧 ≤5s、UI 不冻、关窗 ≤1s、`.part` 保留/续传。
5. **检查点**：若此阶段必须重构 `PlaybackController` 线程模型/改 4+ 路径 API，停下来向用户报告，建议拆 T8.6a/b/c；不要硬塞。

### 阶段 2：导航/缓冲/索引
- 部分索引、已缓存 PgUp/PgDn/seek、未缓存 SeekingWait、进度/取消、缓冲耗尽自动暂停/恢复；
- cached P7 p95 ≤80ms；UI 停顿 0；O4-A1..A6。

### 阶段 3：音频/波形/标记/截图/收尾
- 音频跟随；波形渐进；Ctrl+M/S；完成前 finalize；错误矩阵/断网/关窗；
- 3 条 E2E；本地回归；证据/交接/commit。

---

## 6. DoD（tasks-v2 T8.6 验收列，逐条证据）

| # | 验收 | 方法 | 通过条件 |
|---|---|---|---|
| 1 | E2E 3 条 | 输入→确认→首帧→Space→PgUp/PgDn→seek→波形→Ctrl+M→S→关窗 | 每条可操作；失败写清哪条 blocked |
| 2 | 首帧 | 3 条冷缓存，每条 20 次 | p95 ≤5s（19/20，DEC-2）；记录 p50/下行 |
| 3 | 首帧默认暂停 | 首帧后不自动播放；Space | 状态变化 ≤100ms |
| 4 | 缓冲提示 | 首帧前 >1s | 1s 内出现“缓冲中”+进度；无白屏/未响应 |
| 5 | 缓冲取消 | 等待/下载中 Cancel | ≤100ms 可操作；`.part` 保留；可续传 |
| 6 | 已缓存 P7 | 已缓存区 20 次 PgUp/PgDn | 严格 ±1；p95 ≤80ms（DEC-2） |
| 7 | 已缓存 seek | 20 次随机 seek（距关键帧 ≤2s） | 落点误差 0；p95 ≤200ms；索引未覆盖显示扫描中 |
| 8 | 未缓存 seek | 20 次未缓存 | ≤100ms 显示“缓冲中（目标帧 N）”；取消旧等待；落点 0 |
| 9 | 缓冲耗尽 | 播放中限制带宽/暂停下载 | 自动暂停+提示；恢复从当前帧；不黑屏/跳帧 |
| 10 | 波形 | 已生成区间 | 帧刻度偏移 ≤1 帧；生成中不阻塞步进/拖动 |
| 11 | 标记/截图 | Ctrl+M/S | 会话内可用；PNG 当前流分辨率/无 UI；持久化边界写清（T8.7） |
| 12 | 关窗 | 下载/等待/播放中关闭 | ≤1s 退出；无死锁；reader 先停；rename 不 busy |
| 13 | 错误 | 断网/403/刷新失败/磁盘满 | ≤5s 可读；已缓存可播；无 Cookie/签名 URL |
| 14 | 本地零网络/回归 | `--startup-test` + 本地打开 | 0 请求；P1/P2/P7 不退化（当前 274.43/331.18/65.20ms） |
| 15 | 构建/分发 | ctest/dist/check-imports | ctest 29/29；无新网络导入；dist 体积在 P12 内 |

**M1 门不在本任务判定**：T8.8 才做 3/3 720P + 全量；**OB-2 360P 仍 blocked**，T8.6 不能改这个结论。

---

## 7. 测试/命令

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure
```

新增 `--online-m1-test`（无 GUI 优先）：
- fake/本地 `.part`：首帧、缓冲、取消、部分索引、seek、关窗；
- 可选 `--real`：3 条真实（有 `.secrets` 时），不打印签名 URL/Cookie；
- 3 条：OB-1 `BV1BMnv6jE5S` cid `42915922022`；OB-2 `BV1mA411m7pS` cid `1004967230`（360P blocked）；OB-3 `BV1GJ411x7h7` cid `137649199`。
- 现有回归：fetch 46/0、qr 19/0（UI 21/0）、auth 54/0、resolve 156/0、cache 32/0、growing 22/0；`dist`/`check-imports`。

证据：
- `docs/perf/v2-online-m1.md` 更新 T8.6 章节（或 `docs/perf/v2-online-session.md`）；
- 截图/录屏（首帧/缓冲/已缓存 seek）；
- `docs/handoff-2026-10-23-t8.6.md`；
- `tasks-v2.md` T8.6 完成 + 版本；`AGENTS.md` 版本；一个 commit。

---

## 8. 已知坑（优先看）

1. **不要传 `.part` 路径**：未发布区不会阻塞，FFmpeg 会 EOF；必须 custom AVIO/context。
2. **不要先下完再播**：5s/0.3MB 首帧不成立。
3. **不要 UI 线程阻塞**：0.3MB 等待、未缓存 seek、首次 probe 都可能卡；先量。
4. **不要让 reader 与 rename 抢**：完成前停 reader/延迟 finalize；reader 无 DELETE。
5. **不要伪造索引/总帧数**：部分索引标“扫描中”；不可靠按 T2.3 处理。
6. **不要静默静音/去掉波形**：音频/波形是 DoD；做不了就报告/拆阶段。
7. **不要每 100ms 重扫全片波形**：渐进合并/节流，后台线程。
8. **不要改 CacheStore 写语义**；不要改本地 `OpenFile`。
9. **不要把 OB-2 360P 写成 720P**；M1 3/3 仍 blocked。
10. **不要打印签名 URL/SESSDATA**；下载 refresh 只传 host/path/status。
11. **关闭/取消要 join**：否则析构时线程还活着；关窗 ≤1s。
12. **首帧后默认暂停**：Space 才播放；不要自动播放。
13. **本地零网络**：在线代码不得在启动/本地打开时初始化下载/网络。
14. **ctest 基线 28/28**：新增 1 个 → 29/29，不要改旧计数。

---

## 9. 停止条件

1. 0.3MB 后 `GrowingFormat::Open`/probe 阻塞 >200ms 且无法异步化：停下报告，不要加大等待阈值硬过。
2. 未缓存 seek 必须 UI 线程阻塞 >200ms：停下，提出媒体 worker/拆分方案，不要冻 UI。
3. 部分索引无法保证帧号/落点：不冒充精确；报告限制，必要时缩范围。
4. 完成 rename 与 reader 无法安全协调：停下改 finalize 钩子；不要改 reader 共享/`CacheStore`。
5. 音频/波形无法在 T8.6 内完成：报告并建议拆 T8.6c；不要静默砍。
6. 需要 DASH/1080P60/4K/扫码/在线记录才能继续：停止；不在 T8.6。
7. 真实样本/凭据不足：离线/fake + 已有 `.part` 做阶段 1；真实 3 条写 blocked，不伪造。
8. 本地 P1/P2/P7 退化：停止；本地路径是硬约束。

---

## 10. 交付清单

- [ ] `MediaSession::OpenGrowing` / `VideoDecoder::OpenWithFormat` / `ProbeFormat`（或经证伪的备选）
- [ ] `OnlineSession` 状态机 + MainWindow 确认后真正起播
- [ ] 部分索引/已缓存 seek/未缓存 SeekingWait/缓冲 UI/取消
- [ ] 音频/波形渐进；Ctrl+M/S（会话内）
- [ ] 完成前 reader/finalize；关窗 ≤1s；错误矩阵
- [ ] `--online-m1-test` + 单测（ctest 29/29）
- [ ] 证据/交接/AGENTS/tasks 同步；本地零网络/P1/P2/P7；dist/check-imports
- [ ] 一个 commit；工作树 clean

**如果用户同意拆分**：T8.6a（首帧/播放/关闭）、T8.6b（索引/seek/缓冲）、T8.6c（音频/波形/标记/截图）；每个阶段单独验收/提交，不要在一个 commit 里塞到不可审查。
