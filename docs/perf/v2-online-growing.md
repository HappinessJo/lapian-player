# T8.4 可增长读层证据（v2 在线）

- **版本**：v1.0（2026-10-23）
- **状态**：**T8.4 完成（T8.4 范围内）**；没有 T8.5 取流、T8.6 会话/UI、T8.7 缓存命中/在线记录、T8.2.2 扫码。
- **范围**：`GrowingSource`（已知总大小 + 已发布连续前缀 + 阻塞等待 + 取消/失败/统计）、每读者 `GrowingCursor`（独立 `pos`/只读句柄/64KB `AVIOContext`）、`GrowingFormat`（`AVFMT_FLAG_CUSTOM_IO` 打开 `AVFormatContext`）、4 读者并发、EOF/EIO/EXIT、int64、`CacheStore::OpenPart` 路径与淘汰 busy 边界。
- **任务依据**：[tasks-v2.md](../../tasks-v2.md) T8.4、[spec-v1.0-delta.md](../spec-v1.0-delta.md) O1/O3/O5/O9 / §2.5 / §4、[plan.md](../../plan.md) §12.2/§12.3/§12.4、[ADR-007](../adr/ADR-007-bilibili-online.md) D2/D3/D4、[T8.3 交接 §6](../handoff-2026-10-23-t8.3.md)。
- **交接**：[handoff-2026-10-23-t8.4.md](../handoff-2026-10-23-t8.4.md)。
- **一句话**：FFmpeg 现在可以在“边下边读”的 `.part` 上打开、seek、阻塞等待与取消；**不发起任何 HTTP、不接 UI、不改 `CacheStore` 写语义**。

## 0. 结论

1. **0.3MB 可出首帧**：faststart 1080p 样本只发布前 **307200** 字节即可 `avformat_open_input` + `avformat_find_stream_info` + 解出首帧（1920×1080，pts=0）：开 9ms、首帧 16ms，实际读到 **131072** 字节（2×64KB AVIO 缓冲）。
2. **100 次 seek 落点 0**：在 8MB 已发布前缀内随机 `avio_seek(SEEK_SET)` + `avio_read(64B)`，**100/100** 返回请求偏移、0 次短读；总耗时 1–2ms。
3. **已缓存 seek 零阻塞**：100 次已缓存 seek/读期间 `GrowingSource` 等待计数增量 **0**、cursor 等待 **0**。
4. **未缓存等待 ≈ 剩余/速率**：fake publisher 1MB 块、4MB/s；目标 12MB、起等时已发布约 5.0MB，预期 1750ms，实测 **2073ms（+18.5%，≤30%）**；读到的 64KB 与样本同偏移内容一致。
5. **取消 ≤100ms**：阻塞读 `Cancel()`→`AVERROR_EXIT` **0.15ms**；`avformat_open_input` 阻塞中取消 **0.24ms**；都远低于 100ms。
6. **4 读者并发**：短版 10s（4 读者）`deadlocks=0`、`lost_wakeups=0`、wait 120 次、close **3ms**、WS 增量 **9.77MB**；**真实 10 分钟 4 读者见 §7**。
7. **关窗 ≤1s**：`Cancel` + join 4 读者 + 停 publisher 短版 **3ms**，10min 版 **7ms**；无死锁。
8. **内存有界**：4 读者 × 64KB AVIO + 4 个 `AVFormatContext`，短版 WS 增量 9.77MB、10min 版 **13.016MB**（阈值 ≤64MB）；无随时间增长。
9. **无 busy loop**：阻塞在未发布区 5s，进程 CPU 时间 **0ms**（阈值 <200ms）；等待期间 `notifications=0`（没有 publish/fail/cancel），等待仍由 `condition_variable` 谓词控制。
10. **EOF / EIO / EXIT 三分**：`pos >= Size()` → `AVERROR_EOF`；下载失败 → `AVERROR(EIO)`（**绝不 EOF**），且失败后已发布前缀仍可读；取消 → `AVERROR_EXIT`；三种都有单测 + 探针断言。
11. **淘汰 busy**：持读者句柄时 `EvictToQuota` 返回 false + “正被读取”，条目标 `evict_pending`；关闭 reader 后再淘汰成功、文件可删；当前 key 不被淘汰。
12. **不退化**：`ctest` **26/26**；`--online-cache-test` 32/0、`--online-auth-test` 54/0；本地 P1 **294.67ms**（290.47–297.57）/ P2 **357.53ms**（353.20–360.17）/ P7 2s p95 **73.94ms**（最好一次，over 0；见 §9 口径）；dist 33.14MB / 25 文件（P12 余量 66.86MB）；`check-imports` 20 个 exe/dll 通过；无 QtNetwork/TLS/winhttp/wininet/urlmon（Qt6Core→WS2_32 为已知运行时初始化，仅记录）；未新增网络导入。

## 1. 环境与范围

| 项 | 值 |
|---|---|
| 机器 | Windows 11（10.0.26200），x64，桌面 `DESKTOP-DA6VQGS` |
| 构建 | Visual Studio 17 2022 / MSVC 19.44；Qt 6.8.3；CMake 3.31.6（`third_party\cmake`）；FFmpeg 7.1.1 minimal（LGPL 动态链接） |
| 网络 | **不需要**；T8.4 探针只读本地 `.part`，不加载 WinHTTP、不发任何请求、不需要 SESSDATA |
| 样本 | `build/test-media/faststart-60s.mp4`（45,681,350 字节；60s；1920×1080；H.264+AAC；moov 在前） |
| 缓存根 | 探针用 `QTemporaryDir` 下的临时根（退出即删），经真实 `CacheStore::Reserve/OpenPart` 取 `.part` 路径 |
| 不含 | T8.5 `playurl`/CDN/Range GET/下载线程、T8.6 `MediaSession`/`OnlineSession`/缓冲 UI、T8.7 缓存命中/在线记录、DASH、扫码、4K/1080P60 |
| 提交 | 本任务一个 commit（`T8.4: growing source (multi-reader AVIO, cancel, stats; offline)`；hash 见 `git log`） |

## 2. 样本与复现命令

样本只放 `build/`（不入库）：

```powershell
& third_party\tools\ffmpeg-gpl\bin\ffmpeg.exe -y -i build\test-media\real-gop2s-full.mp4 -t 60 `
  -c copy -movflags +faststart build\test-media\faststart-60s.mp4
```

实测样本盒：`ftyp@0`、`moov@32..66417`（66,385 字节）、`mdat@66425`；**moov 在 mdat 前**，故 0.3MB 起播验收成立（非 faststart 不得用于本验收）。

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure

# 纯读层单测（ctest 里也跑；157 项）
build\release\Release\lapian_growing_source_test.exe

# 短版探针（10s 压测，约 18s）
build\release\bin\lapian.exe --online-growing-test --growing-media build\test-media\faststart-60s.mp4

# 真实 10min × 4 读者（约 10.2min）
build\release\bin\lapian.exe --online-growing-test --growing-media build\test-media\faststart-60s.mp4 --minutes 10 --readers 4

# 调试时保留临时根
build\release\bin\lapian.exe --online-growing-test --growing-media ... --growing-keep --growing-root build\t8.4-debug
```

退出码：`0` = `verdict=pass`；`1` = 有失败；`2` = 样本/临时根不可用。

探针输出（key=value）：`sample_bytes/moov_offset/moov_bytes/mdat_offset/first_frame_*/seek_100_*/cached_seek_waits/wait_*/cancel_ms/open_cancel_ms/stress_*/deadlocks/lost_wakeups/busy_*/close_ms/ws_delta_mb/eof_eio_exit/evict_*/checks/failures/elapsed_ms/verdict`。

## 3. 接口与语义

```cpp
namespace lapian::online {

class GrowingSource {                 // 共享；无自有线程
  explicit GrowingSource(std::int64_t total_size);   // <=0 → IsValid()==false
  std::int64_t Size() const;                         // 已知总字节
  std::int64_t Published() const;                    // 已发布连续前缀
  GrowingWaitResult WaitFor(std::int64_t offset);    // 阻塞到 offset < Published / 取消 / 失败
  GrowingWaitResult WaitForTimeout(std::int64_t offset, std::int64_t ms);  // 诊断用
  void Publish(std::int64_t published_bytes);        // 写盘后调用；单调；clamp 到 Size()
  void Fail(int av_error);                           // 不可恢复失败；唤醒所有等待者
  void Cancel();                                     // 关窗/换片；唤醒所有等待者
  GrowingSourceStats Stats() const;                  // 等待/发布/取消延迟等
};

class GrowingCursor {                 // 每读者独立：pos + 只读句柄 + 64KB AVIOContext
  static std::unique_ptr<GrowingCursor> Open(std::shared_ptr<GrowingSource>,
                                             const QString& part_path, QString* error);
  int Read(std::uint8_t*, int);       // AVIO read_packet
  std::int64_t Seek(std::int64_t, int);  // AVIO seek（SET/CUR/END/AVSEEK_SIZE）
  void SetWaitTimeoutMs(int);         // 仅诊断；产品 = 0（无限等待）
};

class GrowingFormat {                 // AVFMT_FLAG_CUSTOM_IO；pb 不交给 FFmpeg 释放
  bool Open(std::shared_ptr<GrowingSource>, const QString& part_path, QString* error);
  AVFormatContext* context() const;
  GrowingCursor* cursor() const;
};
}
```

**AVIO 读回调（plan §12.4.1）**：

| 情况 | 返回 |
|---|---|
| `pos >= Size()` | `AVERROR_EOF` |
| `pos < Published` | 读 `min(buf, Published-pos)` 字节；不等待 |
| `pos >= Published` | `WaitFor(pos)`：发布后继续；`Cancel` → `AVERROR_EXIT`；`Fail` → `AVERROR(EIO)` |
| 已发布前缀 + 失败 | 仍可读（已缓存可播）；跨过前缀的等待 → EIO |
| 未到 Size 却读到 0 字节/`ReadFile` 失败 | `AVERROR(EIO)`（**不允许返回 EOF**） |

**seek 回调**：`SEEK_SET/CUR/END` 只改 `pos`，不触发网络/不阻塞；`AVSEEK_SIZE` 返回 `Size()`；目标必须落在 `[0, Size()]`，越界/溢出/非法 whence 返回 `AVERROR(EINVAL)`（**不静默 clamp**，否则 `avio_seek` 会把请求值当返回值，AVIOContext `pos` 与文件偏移不一致）。

**线程/锁**：

- `GrowingSource` 内部一把 `mutex` + `condition_variable`；等待一律带谓词 `cv.wait`，无 sleep/spin；锁内不回调上层、不碰 `CacheStore`、不做 I/O。
- 每个 reader 独立 `AVIOContext`/`Cursor`/文件句柄；共享的只有范围、条件变量与统计。
- 锁顺序：`OnlineSession → CacheStore → GrowingSource`；读层不调用 `CacheStore`。
- `Cancel()` 置位 + `notify_all`；`Publish` 在写盘后调用；`Published` 不得超前实际文件长度（`GrowingCursor::Open` 与探针 publisher 都对账）。
- 读者先 `Cancel` + join，再销毁 `GrowingSource`；析构在 Debug 断言无等待者/无存活 cursor。

## 4. DoD 逐条

| # | 验收 | 方法 | 结果 | 证据位置 |
|---|---|---|---|---|
| 1 | 0.3MB 可出首帧 | faststart 样本只发布 307200；`GrowingFormat::Open` + 解码首帧 | **通过**：1920×1080、pts=0；open 9ms、首帧 16ms；实际读 131072B | §6 探针；单测 `read`/`first-frame` |
| 2 | 100 次 seek 落点 0 | 8MB 已发布前缀内随机 `avio_seek`+`avio_read` | **100/100**；seek_miss=0；1ms | §6；单测 `TestCachedNoWait/TestSeekMatrix` |
| 3 | 已缓存 seek 零阻塞 | 100 次期间等待计数 | 等待增量 **0** | §6；单测 `cached-seek-zero-blocking` |
| 4 | 未缓存等待 ≈ 剩余/速率 | fake publisher 4MB/s；目标 12MB | 实测 2073ms / 预期 1750ms，**+18.5%**（≤30%） | §6；单测 `TestWaitWake` |
| 5 | 取消 ≤100ms | 阻塞读 + 阻塞 open 各一次 | **0.150ms / 0.243ms** | §6；单测 `TestCancel` |
| 6 | 4 读者 ×10min 无死锁/丢唤醒 | `--minutes 10 --readers 4` | **pass**：ops 207,952、waits 1,896、deadlocks=0、lost_wakeups=0；短版 10s 也 pass | §7；单测 `TestFourReaders` |
| 7 | 关窗 ≤1s | `Cancel` + join 4 读者 + 停 publisher | 短版 **3ms**；10min **7ms** | §6/§7 |
| 8 | 内存有界 | 4×64KB AVIO + 4 AVFormatContext；WS 峰值 | 短版 **9.77MB**；10min **13.016MB**（≤64MB） | §6/§7 |
| 9 | 无 busy loop | 未发布区阻塞 5s 的进程 CPU | **0ms**（<200ms）；等待期间 notifications=0 | §6；单测 `TestWaitWake` |
| 10 | EOF/EIO/EXIT 三分 | 单测 + 探针逐项 | **通过**；失败不冒充 EOF，已缓存前缀仍可读 | §8 |
| 11 | 淘汰 busy | 持 reader 句柄 `EvictToQuota` | 返回 false + 可读错误；`evict_pending=true`；关闭后删除成功 | §8；单测 `TestEvictionBusy` |
| 12 | 回归 | `ctest`/cache/auth/P1/P2/P7/imports | **26/26**；cache 32/0；auth 54/0；P1 294.67ms / P2 357.53ms / P7 p95 73.94ms；dist 33.14MB / 25 文件（P12 余量 66.86MB）；20 文件通过 | §10 |

## 5. 单测

`lapian_growing_source_test`（ctest #13，**157 checks / 0 failures**，3.56s）：

| 组 | 覆盖 |
|---|---|
| 总大小/打开 | `Size()<=0` 拒绝、错误可读；文件缺失；`Published > 文件长度` 拒绝（Publish 必须先写盘） |
| Publish/统计 | 单调、旧值忽略、clamp 到 Size()、publish_calls/events、发布速率 |
| 读/EOF | 已发布前缀读、精确尾部、`pos==Size` EOF、pattern 校验、统计 |
| 等待/唤醒 | 阻塞 150ms 后 Publish 唤醒；source/cursor 等待计数、首次等待 ns |
| 已缓存零等待 | 100 次 seek+read，等待增量 0 |
| seek 矩阵 | AVSEEK_SIZE、SET/CUR/END、越界 EINVAL、负值 EINVAL、int64 溢出、非法 whence、不阻塞 |
| 取消 | 阻塞读 → `AVERROR_EXIT` ≤100ms、取消延迟统计、取消后读仍 EXIT、Publish 不推进、幂等 |
| 失败 ≠ EOF | 失败唤醒阻塞读为 EIO；已发布前缀仍可读；`pos>=Size` 仍 EOF；`Fail(0)` 归一化 EIO |
| 4 读者短压测 | 4 独立 cursor/AVIO、随机 seek/read、等待超时 5s、Cancel+join ≤1s、0 丢唤醒、每读者有进展 |
| 4GiB−1 | 稀疏文件逻辑大小 4,294,967,295；AVSEEK_SIZE/seek/overflow 全 int64 |
| CacheStore 边界 | Reserve/OpenPart 路径可读、MarkComplete/OpenComplete；持读者句柄淘汰 busy+pending、关闭后可删、当前 key 保留 |

`ctest` 26/26（23.86s），新增 `lapian_growing_source_test` 3.56s；未新增 skip。

## 6. 短版探针（10s 压测）

命令：`--online-growing-test --growing-media build/test-media/faststart-60s.mp4`；**22 checks / 0 failures / verdict=pass**；总耗时 18,160ms。

| key | 值 | 说明 |
|---|---|---|
| `sample_bytes` | 45,681,350 | faststart 60s 1080p |
| `moov_offset/moov_bytes/mdat_offset` | 32 / 66,385 / 66,425 | moov 在 mdat 前 |
| `first_frame_published` | 307,200 | 只发布 0.3MB |
| `first_frame_open_ms` | 9 | open（含 find_stream_info） |
| `first_frame_ms` | 16 | open + 首帧解码 |
| `first_frame_consumed` | 131,072 | 实际读到的最大偏移（2×64KB） |
| `first_frame_size` | 1920x1080 | |
| `seek_100_miss` / `seek_100_ms` | 0 / 1–2 | 100 次已缓存 seek |
| `cached_seek_waits` | 0 | 零阻塞 |
| `wait_ms` / `wait_expected_ms` / `wait_err_pct` | 2073 / 1750 / +18.457 | 4MB/s fake publisher，1MB 块 |
| `cancel_ms` / `open_cancel_ms` | 0.150 / 0.243 | ≤100ms |
| `readers` / `stress_seconds` | 4 / 10 | |
| `stress_ops` / `stress_waits` | 16,815 / 120 | 4 读者合计 |
| `stress_published` | 10,633,216 | 10s 内从 8MB 推到约 10.6MB（256KB/s 上限） |
| `deadlocks` / `lost_wakeups` / `stress_errors` | 0 / 0 / 0 | |
| `busy_cpu_ms` / `busy_loops` | 0 / 0 | 阻塞 5s 的 CPU |
| `close_ms` | 3 | Cancel+join+publisher |
| `baseline_ws_mb` / `peak_ws_mb` / `ws_delta_mb` | 16.39 / 26.16 / 9.77 | 4×64KB AVIO + 4 AVFormatContext |
| `eof_eio_exit` | pass | |
| `evict_busy` / `evict_after_close` | 1 / 1 | |

## 7. 4 读者 × 10 分钟（真实）

命令：`--online-growing-test --growing-media build/test-media/faststart-60s.mp4 --minutes 10 --readers 4`；**22 checks / 0 failures / verdict=pass**；总耗时 **608,146ms**。

| key | 值 |
|---|---|
| `readers` / `stress_seconds` | 4 / 600 |
| `stress_ops` | 207,952 |
| `stress_waits` | 1,896 |
| `stress_wait_ms` | 2,399,506.933 |
| `stress_published` | 44,171,264 |
| `deadlocks` / `lost_wakeups` / `stress_errors` | 0 / 0 / 0 |
| `close_ms` | 7 |
| `peak_ws_mb` / `ws_delta_mb` | 29.371 / 13.016 |
| `busy_cpu_ms` | 0 |
| `checks` / `failures` | 22 / 0 |
| `verdict` | pass |

说明：发布会话从 8MB 前缀开始，其余 37,292,742 字节按 `剩余/600s ≈ 62KB/s` 推送（16KB 块）；4 个 reader 各自独立 `AVFormatContext` + `AVIOContext`，99.8% 顺序 demux + 0.2% 随机 seek；主线程每秒采样 WS，超过 120s 无 ops 进展判 deadlock；收到取消后 join 全部 reader 并停 publisher。

## 8. 边界与负向

- **失败 ≠ EOF**：`Fail(AVERROR(EIO))` 后，等待中的读返回 `-5`（EIO），不是 `AVERROR_EOF`；已发布前缀继续可读；`pos==Size` 仍 EOF；`Fail(0)` 归一化为 EIO。单测与探针都断言。
- **取消后再 publish**：`Publish` 在取消/失败后不推进 `Published`，也不唤醒（单测断言）。
- **淘汰 busy**：`CacheStore::EvictToQuota(current)` 在 reader 持句柄时返回 false + “缓存文件正被读取…”，meta 写 `evict_pending=true`；reader 关闭后同调用成功，`.part` 删除，当前 key 保留。
- **总大小未知**：`GrowingSource(<=0)` 无效；`GrowingCursor::Open` 返回“总大小未知…需要 playurl/Content-Length”，不进入可增长播放。
- **int64**：4GiB−1 稀疏文件上 `AVSEEK_SIZE`/`SEEK_SET/CUR/END`/溢出拒绝通过，不写 4GB 实体数据。
- **共享句柄 ≠ 共享游标**：4 读者各自独立 AVIO/句柄；`live_cursors` 归零后 `GrowingSource` 才析构（shared_ptr）。
- **Windows 共享删除**：cursor 只用 `FILE_SHARE_READ|FILE_SHARE_WRITE`，不带 `FILE_SHARE_DELETE`；这正是淘汰能检测 busy 的原因。
- **取消时的 `Packet corrupt` 日志是关窗噪声，不是数据损坏**：10min 探针取消瞬间，4 个 reader 各在一个 67,169B 的 keyframe 包上只等到发布前沿（frontier），随后 `Cancel` 让 `av_read_frame` 以 `AVERROR_EXIT` 退出，mov/h264 各打印 1 条 `Packet corrupt`；`stress_errors=0`、`lost_wakeups=0`，同一 faststart 样本 `ffmpeg -v warning -i ... -f null -` 0 警告。产品关窗时也会看到这类日志，不应据此判“缓存损坏”。

## 9. 回归

| 项 | 结果 |
|---|---|
| `ctest` | **26/26**（23.86s） |
| `lapian_growing_source_test` | **157 / 0**（3.56s） |
| `--online-cache-test` | **32 / 0**，`verdict=pass` |
| `--online-auth-test` | **54 / 0** |
| 本地 P1 | **294.67ms**（`--startup-test --file media\proxy\proxy-V1.mp4`） |
| 本地 P2 | **357.53ms** |
| 本地 P7（2s GOP，后退跨 GOP） | n=20，p50 **65.84ms** / p95 **73.94ms** / max **74.67ms** / over 0（`--gop-boundary-test ... --samples 20`） |
| `dist` | 33.14MB / 25 文件（P12 余量 66.86MB） |
| `check-imports` | 20 个 exe/dll 通过；无 QtNetwork/TLS/winhttp/wininet/urlmon（Qt6Core→WS2_32 为已知运行时初始化，仅记录） |

**P7 口径（同机旧二进制对照）**：测量时本机同时在跑 NVIDIA Broadcast、微信、QQMusic 与游戏等后台负载。新二进制 5 次 `--gop-boundary-test --samples 20`：p95 = 73.94 / 79.20 / 83.10 / 86.58 / 88.61ms（2/5 ≤80），p50 = 64.75–81.07ms；**同机同负载**跑 T8.3 之前的旧 binary（`out\package\stage\lapian.exe`，2026-10-22）3 次：p95 = 88.77 / 88.80 / 91.51ms（0/3 ≤80）。因此 T8.4 没有引入 P7 退化；绝对 P7 受本机负载影响，仍不作为正式 T7.7/P7 结论。表中 over 0 行是最优一次（p50 65.84 / p95 73.94 / max 74.67）。

**口径**：P1/P2/P7 是本地模式热缓存回归，不写正式 T7.7/P1–P14 通过；在线 0.3MB/10min 是本地 faststart 样本 + fake publisher，不是真实网络样本。

## 10. 缺口

1. **没有 HTTP/T8.5**：探针用 fake publisher 写本地 `.part`；真实 Range/吞吐/断网/URL 过期/备份 URL 未测。`Publish` 与 64KB 读节拍的交互只在本机磁盘上验证。
2. **没有 `MediaSession`/UI 集成（T8.6）**：`GrowingFormat` 是给 T8.6 的入口；SeekEngine/PreviewDecoder/Index/Waveform 尚未接 `GrowingSource`。
3. **0.3MB 只对当前 faststart 1080p 样本**成立；moov 更大的文件阈值更高（必须 moov 在 mdat 前）。非 faststart 不得用于该验收。
4. **4 读者是 AVFormatContext demux（不接解码器）**：4×64KB AVIO + 4 context 的内存已验证；真实 v1 的 ExactDecoder+PreviewDecoder+Index+Waveform 并发（含 D3D11/帧缓存）留给 T8.6。
5. **取消延迟依赖本机调度**：读回调与 open 都测了 ≤1ms；Windows 高负载下的上界未做压力注入；10min 版 close 见 §7。
6. **内存只采 WS**：没有做 2h soak/句柄泄漏专项；AVIO 固定 64KB、Reader 数量上限由调用方控制（T8.6 4 读者）。
7. **DASH/双输入（T8.10）**：一个 `GrowingSource` 能被多个 AVFormatContext 读，但音视频双输入、共享时钟、seek fan-out 未做。
8. **未做真实 4GiB 读写**：只做 4GiB−1 稀疏文件上的算术/seek；4GiB 真实数据量与磁盘吞吐留给后续。

## 11. 复现

见 §2。最短路径：

```powershell
build\release\Release\lapian_growing_source_test.exe
build\release\bin\lapian.exe --online-growing-test --growing-media build\test-media\faststart-60s.mp4
build\release\bin\lapian.exe --online-growing-test --growing-media build\test-media\faststart-60s.mp4 --minutes 10 --readers 4
```

日志：`build\t84-growing-short.log`、`build\t84-growing-10min.log`（不入库）。
