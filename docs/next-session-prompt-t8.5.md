# 下一个任务：T8.5 durl 取流（v2 在线 · M1 关键路径）

> 给接手 agent 的自包含提示词。目标：**只做 T8.5**，做完一个可审查提交。
> 不要顺手做 T8.6（会话/UI）、T8.7（缓存命中/记录）、T8.2.2（扫码）；不要改 M1/M2 门与验收数字。
> 当前仓库：分支 `master`，HEAD 应为 `44bba88`（T8.4 完成）或其后只含文档的提交；工作树 clean；`ctest` **26/26**。

---

## 0. 一句话

实现 **durl 单文件取流**：`view`/`playurl(fnval=0)` → 选登录后 720p durl → `CacheStore::Reserve/OpenPart` → **顺序下载 + Range 续传 + 3 备份 URL + 有限重试 + URL 过期刷新** → 每次写盘后 `GrowingSource::Publish` → 关闭写句柄后 `MarkComplete`。**不接 UI、不做 DASH、不做扫码、不改 `CacheStore` 写语义**。

## 1. 先读（按顺序，不要凭记忆）

1. `AGENTS.md` v6.16：§1 v2 状态、§7 明确不做、§11 协作规则。
2. `docs/spec-v1.0-delta.md` v0.4：**O2**（起播/取消）、**O5**（缓存/续传/损坏/磁盘满）、**O6**（档位/实际 fps）、**O7**（断网/URL 过期/备份 URL）、§4.2/§4.4、§6.1 白名单、§9.0 DEC-2/DEC-5。
3. `tasks-v2.md` v0.8：**T8.5 行**（“验收标准”列就是 DoD）；T8.6 行只看边界（不要实现）。
4. `plan.md` v2：**§12.3.1 输入→首帧**、**§12.3.5 失败与恢复**、**§12.2.1 线程**、**§12.4 可增长读层**。
5. `docs/handoff-2026-10-23-t8.4.md` §2/§3/§6（`GrowingSource`/`GrowingCursor`/`GrowingFormat` 接口、EIO/EXIT、发布必须写盘后、锁顺序）。
6. `docs/handoff-2026-10-23-t8.2.md` §1.3/§2（`IHttpClient`、显式 SESSDATA、DPAPI 凭据、日志脱敏）。
7. `docs/handoff-2026-10-23-t8.3.md` §2/§6（`CacheStore` 接口、`.part`→`.mp4`、读句柄 busy、配额/LRU）。
8. 代码：`src/online/GrowingSource.h`、`src/online/CacheStore.h`、`src/online/IHttpClient.h`、`src/online/WinHttpClient.h`、`src/online/BiliResolver.h`、`src/online/CredentialStore.h`、`src/online/BiliAuth.h`。
9. `docs/perf/v2-online-spike.md` §2/§3.1/§3.2/§4.1：durl 档位、Range 206、3 个备份 URL、~120min 过期、0.16–0.3MB 首帧阈值。

## 2. 当前状态（T8.0–T8.4 已完成）

| 任务 | 状态 | 关键接口/证据 |
|---|---|---|
| T8.1 解析 | ✅ | `BiliResolver`/`BiliResolveClient`；`--online-resolve-test` 156/0；OB-1/2/3 元数据 |
| T8.2 HTTP/凭据 | ✅ | WinHTTP nav 161–202ms；SESSDATA/DPAPI；`--online-auth-test` 54/0；Cookie 只发 `api.bilibili.com` |
| T8.3 缓存 | ✅ | `CacheStore::Reserve/OpenPart/UpdateProgress/MarkComplete/OpenComplete`；配额/LRU/续传/损坏；`--online-cache-test` 32/0 |
| T8.4 读层 | ✅ | `GrowingSource`/`GrowingCursor`/`GrowingFormat`；0.3MB 首帧、100/100 seek、取消 0.15ms、4 读者 10min pass；`--online-growing-test` 22/0 |

**未做**：T8.5 本任务、T8.2.2 扫码（非门）、T8.6 会话/UI、T8.7 缓存命中/记录、T8.8 M1 门。

## 3. 范围

### 做（建议文件名，可按现有风格微调）

- `src/online/PlayUrl.h/.cpp`（或 `BiliPlayUrlClient`）：
  - `BuildPlayUrlUrl(bvid, cid, qn=64)`：`https://api.bilibili.com/x/player/playurl?bvid=...&cid=...&qn=64&fnval=0&fnver=0&fourk=0`。
  - `ParsePlayUrlJson`：`code==0`、`data.quality`（**实际** qn）、`data.format`、`data.durl[]`（`url`/`backup_url[]`/`size`/`length`/`order`）；容错缺字段；错误映射到 `BiliResolveError` 风格。
  - 只发一次 playurl GET；带可选 SESSDATA；不解析 DASH、不猜档位。
- `src/online/DownloadTransport.h` + `src/online/WinHttpDownloadTransport.cpp`（或扩展现有 `WinHttpClient` 的私有实现）：
  - **Range GET**：`Range: bytes=<offset>-`、`Accept-Encoding: identity`；TLS 校验；不跟随重定向；**CDN 请求绝不带 Cookie**。
  - 流式读取（64KB–1MB 块）写入 `.part`；返回 HTTP 状态、`Content-Length`/`Content-Range` 总长/起点、实际字节、错误、耗时。
  - 可取消：`Cancel` 后 ≤100ms 关闭请求/返回；不在 UI 线程。
  - 可注入 fake transport（单测/探针不联网）。
- `src/online/DownloadJob.h/.cpp`：
  - 状态：`idle/preparing/downloading/paused/retrying/refreshing/completed/failed/cancelled`。
  - `Start`/`Pause`/`Cancel`/`Wait`；一个下载线程；不建 UI。
  - 打开 `.part`：`CacheStore::OpenPart` 取 `part_path`/`existing_bytes`；写句柄 `FILE_SHARE_READ|FILE_SHARE_WRITE`（**不带 DELETE**，与 T8.4 reader 共存）。
  - 写后 `GrowingSource::Publish(written)`；每 1–2s `CacheStore::UpdateProgress(written,false)`；暂停/结束 `force=true`。
  - 完成：关闭写句柄 → `MarkComplete`；未完成：保留 `.part`，取消/失败唤醒 reader。
  - 失败：`GrowingSource::Fail(AVERROR(EIO/ENOSPC/...))`；取消：`Cancel()`；都保留 `.part`。
  - 统计：下载字节、耗时、bytes/s、TTFB、Range 请求数、备份切换、重试、刷新、`range_ignored`。
- `src/app/OnlineFetchTestRunner.h/.cpp` + `main.cpp` 分流 + `src/CMakeLists.txt`：
  - `--online-fetch-test`（无 GUI）：fake transport/本地样本，覆盖 Range 206、200 回退、备份切换、403/过期刷新、取消、续传、完成改名、首帧、ffprobe；输出稳定 key=value。
  - 可选 `--real`（`--bvid/--cid/--sessdata-file/--tag`）：真实 playurl + CDN；**不打印 SESSDATA/签名 URL**。
- 单测 `tests/online_fetch_test.cpp` + ctest `lapian_online_fetch_test`（fake transport，不联网；至少覆盖 Range/续传/备份/刷新/取消/总长不一致/无重复字节/MarkComplete）。
- 证据 `docs/perf/v2-online-m1.md` + 交接 `docs/handoff-YYYY-MM-DD-t8.5.md`。

### 不做

- ❌ T8.6 `MediaSession`/`OnlineSession`/缓冲 UI/SeekWait；T8.5 只做“取流 + 缓存 + 读层发布”，首帧用 `GrowingFormat` 探针证明。
- ❌ DASH/双流（T8.9/T8.10）；扫码（T8.2.2）；在线记录/缓存命中（T8.7）。
- ❌ 修改 `CacheStore` 写语义、`meta.json` schema、M1/M2 门/数字、`spec.md`/`tasks.md`/`plan.md` v1 正文。
- ❌ WBI/签名/反爬绕过/账号密码/多账号；playurl 返回 `-400/-403` 时给可读错误并停止，不尝试绕过。

## 4. Durl 取流契约（先按此实现；真实响应以实测为准）

```text
GET https://api.bilibili.com/x/player/playurl?bvid=<BV>&cid=<cid>&qn=64&fnval=0&fnver=0&fourk=0
Cookie: SESSDATA=<...>        # 只在有凭据时；只发 api.bilibili.com
```

- 成功：`code==0`；`data.quality` = 服务端**实际** qn；`data.format`（如 `mp4720`）；`data.durl[0]`：
  - `url`：主 CDN 地址（带签名/过期参数；**绝不入日志/证据**）
  - `backup_url[]`：备份地址（S2 实测 2 个，共 3 个）
  - `size`：总字节（**必须与 Range/Content-Range 对账**）
  - `length`：时长（ms；可作辅助）
- 失败：`code != 0` 按错误矩阵给可读原因；**`data.durl` 为空/只有 DASH → 不进入可增长播放**，报告“当前样本无 durl”并停止（M1 不降级到 DASH）。
- `qn=64` 请求 720P；**不得相信请求值**，只信 `data.quality`。若实际 `quality<64`，记录实际档位并停止（M1 3/3 720p 不成立）。
- M1 三个 OB 样本（来自 T8.1 证据）：
  - OB-1 `BV1BMnv6jE5S` cid `42915922022`
  - OB-2 `BV1mA411m7pS` cid `1004967230`（60fps 源；普通用户实际约 29.4fps，记录实际）
  - OB-3 `BV1GJ411x7h7` cid `137649199`
- 凭据只从 `.secrets/bilibili_sessdata.txt` 或 `LAPIAN_SESSDATA_FILE` 读取；**不要打印/提交**。

## 5. DoD（tasks-v2 T8.5 验收列，逐条证据）

| # | 验收 | 方法 | 通过条件 |
|---|---|---|---|
| 1 | 3/3 登录样本 720p | 真实 playurl + durl；`data.quality >=64`、`data.durl` 非空；OB-1/2/3 | 3/3 记录实际 qn/format/fps/size；任一失败即停止，不写通过 |
| 2 | 首 0.3MB 出首帧 | 边下边读：发布 ≥307200B 后 `GrowingFormat::Open` + 解码首帧 | open/首帧 ms、实际读字节；记录；失败停止 |
| 3 | 断网 5s 后续传 | fake transport 在 N 字节中断 5s，再 `Range: bytes=<existing>-` | 落点起点 == existing；无重复字节；最终 hash/size 正确 |
| 4 | 杀进程后续传 | 真实下载到一半杀进程；重开同 BV 从 `.part` 长度续传 | existing 正确、`206`、最终文件可 ffprobe；不拼接双份 |
| 5 | 模拟 403/过期刷新 | fake/真实注入 403；刷新 playurl 后继续 | 刷新 ≤1 次成功；失败保留 `.part` + 可读提示；≤5s |
| 6 | 备份切换 | 主 URL 403/断连 → `backup_url[0]`；再失败 → `[1]` | 3 个 URL 用尽前成功；不重复已下载字节；记录 switch 次数 |
| 7 | 无重复/损坏字节 | 对最终文件做 `ffprobe` + 长度/hash；分段写入偏移对账 | `ffprobe` 正常打开；size == total；无重叠/空洞；MarkComplete 后 `.mp4` |
| 8 | Range 语义 | 记录 `206`、`Content-Range` 起点/总长；200 回退单独记录 | 续传必须 206；200 时 truncate 从 0 重下并记 `range_ignored=1` |
| 9 | 取消/暂停 | `Cancel`/`Pause` 在下载/重试/等待中触发 | 取消 ≤100ms 返回；`.part` 保留；不再写；reader 收到 EXIT |
| 10 | 失败可读 | 断网/磁盘满/403/刷新失败 | ≤5s 可读错误；不崩、不冻结；无 Cookie/签名 URL |

**M1 门不在本任务判定**：T8.8 才做 3/3 + OP1/OP2/OP3/OP5 + O9 全量。T8.5 只提供可复现证据。

## 6. 实现要点（按顺序）

1. **纯解析先行**：`PlayUrl` 只做 URL 构造/JSON 解析/错误映射，用固定 JSON fixture 单测；不联网。
2. **Transport 抽象**：`IDownloadTransport::GetRange(url, offset, sink, cancel, result)`；真实 WinHTTP 实现 + fake（内存字节/可注入 403/断连/慢速/忽略 Range）。
3. **DownloadJob**：
   - `Start` 前 `Reserve`（总字节已知）→ `OpenPart`（existing）→ 建 `GrowingSource(total)`（已有 `existing` 时 `Publish(existing)`？**只在文件确有 existing 字节时**；产品语义：续传前缀已发布）。
   - 写循环：`GetRange` → 校验 206/`Content-Range` → 写 `.part` 偏移 → `Publish(written)` → `UpdateProgress`（1s）。
   - 200：若 `existing>0`，truncate 到 0、`Publish(0)`、从 0 重下；记 `range_ignored=1`；不得在旧字节后追加。
   - 总长不一致：`existing + received` 与 `total`/`Content-Range` 不符 → 停止、`Fail(EIO)`、保留 `.part`；不拼接。
   - 403/URL 过期：刷新 playurl（一次）→ 新 URL；失败切备份；重试上限/退避（300ms/1s/2s，最多 3 次/URL）；`Cancel` 打断退避。
   - 完成：`written==total` → close write → `MarkComplete` → `OpenComplete` 验证；probe 用 ffprobe。
4. **0.3MB 首帧探针**：下载线程启动后，主线程 `source->WaitForTimeout(307200-1, ...)`，`GrowingFormat::Open` + `avformat_find_stream_info` + 解码首帧；记录 `open_ms/frame_ms/consumed`；关闭 reader 后再让下载完成（或先 `Cancel` reader）。
5. **统计**：`first_byte_ms`、`downloaded`、`elapsed`、`bps`、`range_requests`、`backup_switches`、`retries`、`refreshes`、`range_ignored`、`ffprobe=pass`。
6. **锁顺序**：`OnlineSession → CacheStore → GrowingSource`；`DownloadJob` 不持有 `CacheStore` 锁去等 reader；写句柄不共享 DELETE。

## 7. 测试与探针

- ctest `lapian_online_fetch_test`（fake transport）：**新增 1 个**；基线 26/26 → **27/27**。不要改既有测试计数。
- 单测场景：Range 206 正常；existing>0 续传；200 回退 truncate；`Content-Range` 起点错/总长错；主 URL 403→备份 2；刷新一次成功/失败；重试退避；取消 ≤100ms；暂停/恢复；完成改名；无重复/空洞；ffprobe（用 `build/test-media/faststart-60s.mp4` 或合成小 mp4）。
- `--online-fetch-test`（无 GUI、离线）：fake transport + 本地 faststart 样本；输出 `verdict=pass`，覆盖上述 + 0.3MB 首帧；**不访问网络**。
- 真实 3/3（有 `.secrets` 时）：
  ```powershell
  build\release\bin\lapian.exe --online-fetch-test --real --bvid BV1BMnv6jE5S --cid 42915922022 --sessdata-file .secrets\bilibili_sessdata.txt --tag ob1
  ```
  逐条记录 actual qn/format/fps/size/TTFB/bps/range/ffprobe；不要打印 URL/凭据。
- 断网/杀进程：
  - fake：transport 在 5s 内断连；断言续传起点/最终 hash。
  - real：下载到约 25% 杀进程；重开同 tag；记录 existing/Range 206/最终 ffprobe。
- 现有回归：`--online-resolve-test` 156/0、`--online-auth-test` 54/0、`--online-cache-test` 32/0、`--online-growing-test` 22/0；`ctest`；本地 P1/P2/P7；`dist`/`check-imports`。

## 8. 证据与命令

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure
build\release\bin\lapian.exe --online-fetch-test
# 有凭据/网络时再跑 --real；没有就写 blocked，不伪造 3/3
```

- 证据 `docs/perf/v2-online-m1.md`：3 样本表（actual qn/format/fps/size/TTFB/bps/Range/ffprobe）、首帧、续传（fake/real）、403/过期/备份、失败矩阵、回归。
- 交接 `docs/handoff-YYYY-MM-DD-t8.5.md`：接口、数字、缺口、T8.6/T8.7 需要。
- 文档同步：`tasks-v2.md` T8.5 完成 + v0.9；`AGENTS.md` v6.17；一个 commit。
- 提交前：`git status` clean、`ctest 27/27`、`--online-fetch-test verdict=pass`、`dist`/`check-imports` 不变。

## 9. 已知坑（务必优先处理）

1. **durl 档位不可预期**：请求 qn=64 可能返回更低；只信 `data.quality`，不伪造 720p。3/3 不成立就停止。
2. **`size` 可能缺失/不准**：用 `Content-Range`/HEAD 对账；总长不一致时停止，不拼接。
3. **Range 被忽略返回 200**：必须 truncate `.part` 从 0 重下并记 `range_ignored=1`；否则会重复字节/花屏。
4. **备份 URL 不是“换一个就从头”**：同一文件续传从 existing 起，`Content-Range` 起点必须 == existing；总长不同则失败。
5. **URL 只活 ~120min**：不要日志打印签名 URL；403 刷新 playurl 一次；刷新失败保留缓存并提示。
6. **`Publish` 必须在 WriteFile 之后**：先写盘再发布；`Published` 不得超前文件长度（T8.4 会拒绝）。
7. **完成前不能把 `.part` 当 `.mp4`**：close 写句柄 → `MarkComplete`；reader 先 `Cancel`+join（reader 句柄不带 DELETE，改名会 busy）。
8. **磁盘满/写入失败**：`Fail(AVERROR(ENOSPC/EIO))`；≤5s 可读；保留已缓存前缀。
9. **重试有上限**：3 次/URL + 退避；不能无限重试到成功；`Cancel` 必须打断退避。
10. **UI 线程不下载/不等待**：T8.5 的 DownloadJob 在工作线程；T8.6 才接 UI。
11. **CDN 不带 Cookie**：`IHttpClient` 的显式 SESSDATA 只发 `api.bilibili.com`；下载 transport 不传 SESSDATA。
12. **3/3 依赖真实凭据/网络**：没有 `.secrets`、断网、playurl 只有 DASH、档位 <720p → 写 blocked/失败原因，**不要用旧样本/假数据冒充**。

## 10. 停止条件（遇到即停，报告而不是硬做）

1. `.secrets/bilibili_sessdata.txt` 不存在且无 `LAPIAN_SESSDATA_FILE`：真实 3/3 无法做；完成离线部分并写“真实样本 blocked（无凭据）”。
2. 网络不可达 / B 站返回 412/429/风控：不绕过、不重试到成功；报告失败矩阵。
3. playurl 3 个样本中任一 `data.durl` 为空或 `quality<64`：M1 3/3 不成立；记录实际档位，停止“通过”声明。
4. CDN 忽略 Range 且无法在 200 回退路径保证“无重复字节”：记录 `range_ignored`，若最终 ffprobe/长度/hash 不过，停止。
5. 需要 WBI/签名/登录密码/验证码/DRM/地区绕过才能取流：明确停止，不实现绕过。
6. 真实杀进程/断网测试会破坏本机环境：用 fake + 可恢复脚本；不要为了测试关掉系统网络。

## 11. 交付清单

- [ ] `src/online/PlayUrl.h/.cpp`（解析/URL/错误）
- [ ] `src/online/DownloadTransport.h` + WinHTTP 实现（Range/取消/流式/无 Cookie）
- [ ] `src/online/DownloadJob.h/.cpp`（续传/备份/重试/刷新/完成）
- [ ] `tests/online_fetch_test.cpp` + ctest `lapian_online_fetch_test`
- [ ] `--online-fetch-test`（离线 fake + 可选 `--real`）
- [ ] `docs/perf/v2-online-m1.md` + `docs/handoff-*-t8.5.md`
- [ ] `tasks-v2.md` T8.5 完成 + v0.9；`AGENTS.md` v6.17
- [ ] `ctest` 27/27；离线探针 `verdict=pass`；真实 3/3（有凭据时）或明确 blocked；本地 P1/P2/P7 回归；dist/check-imports
- [ ] 工作树 clean，一个 commit
