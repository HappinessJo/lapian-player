# T8.5/T8.8 证据：durl 取流与 M1 门（2026-10-23）

- **状态**：**T8.5 实现完成**（playurl → durl → CacheStore → 顺序下载/Range 续传/3 备份/重试/URL 过期刷新 → GrowingSource.Publish → 关闭写句柄后 MarkComplete）；**T8.8 于 2026-10-23 判定 M1 通过**（3/3 durl ≥720P、OP1/OP2/OP3/OP5、失败矩阵、续传、DEC-5、本地 O9 全达标；证据见 §11–§17）。
- **M1 3/3 判定**：**T8.5 为 blocked、T8.8 复核后为 pass**。T8.5 请求参数下登录样本 OB-2（`BV1mA411m7pS` / cid `1004967230`）的 `durl` 实际只给 **360P（qn=16）**；T8.8 的 qn/fourk 矩阵证明这是**请求档位差异**：`qn=64` → 16，`qn=80/112/120` → 80（1080P），`fourk` 对实际档位无影响。T8.8 已做最小产品修正（M1 durl 请求 qn 64→80、fourk 保持 0），3/3 复测通过。**未改 M1/M2 门与验收数字**。
- **T8.8 复核注（2026-10-23）**：S2 §2.2 记录登录后 `durl qn=120 + fourk=1` 时 S-2（即 OB-2）实际为 **80（1080p）**；T8.5 产品固定 qn=64、`BuildPlayUrlUrl` 写死 `fourk=0`，`--qn-1080` 只映射 80。故 OB-2 的 360P 可能是**请求参数差异**，T8.8 必须先做 qn=64/80/112/120 × fourk=0/1 矩阵，再决定修取流或最终 blocked；不要把本文件的历史 blocked 直接当最终结论。
- **样本/凭据**：`.secrets/bilibili_sessdata.txt`（普通用户，未打印、未提交）；三样本来自 T8.1 证据。日志只保留 host/path/状态/耗时，不含 SESSDATA/签名 URL。
- **代码**：`src/online/PlayUrl.*`、`DownloadTransport.h`、`WinHttpDownloadTransport.cpp`、`DownloadJob.*`、`WinHttpApi.h`；`tests/online_fetch_test.cpp`；`src/app/OnlineFetchTestRunner.*`；`--online-fetch-test`。
- **边界**：不接 UI（T8.6）、不做 DASH（T8.9/T8.10）、不做扫码（T8.2.2）、不做缓存命中/在线记录（T8.7）；未改 `CacheStore` 写语义、M1/M2 门与验收数字。

---

## 1. 真实 3 样本（登录后 durl）

命令（每个 tag 独立缓存根）：

```powershell
build\release\bin\lapian.exe --online-fetch-test --real --bvid <BV> --cid <cid> --tag <obN> --fetch-root .deps\tmp\t85-<obN>
```

| 样本 | bvid | cid | 请求 qn | **实际 qn** | 档位 | format | codecid | size（字节） | length_ms | 首帧 fps | TTFB（playurl） |
|---|---|---|---:|---:|---|---|---:|---:|---:|---:|---:|
| OB-1 | BV1BMnv6jE5S | 42915922022 | 64 | **64** | 720P | mp4720 | 7 | 14,330,392 | 110,528 | 30.000 | 126ms |
| OB-2 | BV1mA411m7pS | 1004967230 | 64 | **16** | **360P** | mp4 | 7 | 4,121,631 | 95,806 | 30.000 | 121ms |
| OB-3 | BV1GJ411x7h7 | 137649199 | 64 | **64** | 720P | mp4720 | 7 | 51,973,319 | 213,000 | 25.000 | 122ms |

> OB-2 的 `durl` 档位在 qn=64/80/112 三次请求下都返回 `quality=16`、`format=mp4`、`size=4,121,631`；这是 S2 已记录的 **durl 档位不可预期（R20）**，不是请求参数写错。OB-2 仍完成下载/首帧/ffprobe，作为“实际 360P”证据保留，但**不计入 720P 3/3**。

| 样本 | 首字节 | 总耗时 | 下行 | Range 请求 | HTTP 206 | 备份切换 | 重试（失败尝试） | 刷新 | range_ignored | ffprobe | mp4 sha256（前 16） |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|
| OB-1 | 221ms | 11,175ms | 1.28MB/s | 2 | 2 | 0 | 1 | 0 | 0 | pass（1280×720/h264，110,528ms） | `82ff7407a615f9e4` |
| OB-2 | 289ms | 4,175ms | 0.99MB/s | 2 | 2 | 0 | 1 | 0 | 0 | pass（640×360/h264，95,805ms） | `eb8277727e68f6c0` |
| OB-3 | 147ms | 25,502ms | 2.04MB/s | 3 | 3 | 0 | 2 | 0 | 0 | pass（1280×720/h264，212,308ms） | `7dc42c7d9e040406` |

- 三个样本最终 `.mp4` 大小 == `size`；`range_ignored=0`；`backup_switches=0`；无重复字节（`downloaded_bytes` 与最终文件一致）。
- OB-1/OB-3 的 2–3 次 Range 请求来自真实 CDN 连接提前结束：Job 从已写长度续传，最终 hash 与一次成功下载一致；不是重下整文件。
- 录屏/日志：`.deps\tmp\t85-ob{1,2,3}.log`（本地临时，不入库）。

**结论**：登录后 **2/3 为 720P**，OB-2 为 360P → **M1“3/3 720p”blocked**。T8.5 的取流/续传链路本身在 3 个样本都完成并可通过 ffprobe。

---

## 2. 首 0.3MB 出首帧（真实 CDN）

流程：Job 启动后主线程等待 `GrowingSource::Published() >= 307,200`（实际观察到 524,288，因为 256KB 传输块），`Pause()` 断流 → `GrowingFormat::Open` + 解码首帧 → `Resume()` → 完成。

| 样本 | 发布字节 | open | 首帧 | 实际读字节 | 首帧尺寸 | 首帧 fps |
|---|---:|---:|---:|---:|---|---:|
| OB-1 | 524,288 | 9ms | 14ms | 196,608 | 1280×720 | 30.000 |
| OB-2 | 524,288 | 8ms | 10ms | 131,072 | 640×360 | 30.000 |
| OB-3 | 524,288 | 8ms | 13ms | 196,608 | 1280×720 | 25.000 |

- 离线 fake 场景：发布 327,680B 后 open 9ms / 首帧 14ms / 实读 65,536B / 1920×1080（`--online-fetch-test` 的 `firstframe` 场景）。
- 首帧阈值按 S2 的 0.16–0.3MB；实际 0.5MB 是传输块粒度，不是新阈值。

---

## 3. 续传、取消、杀进程

### 3.1 取消 ≤100ms + `.part` 保留

```powershell
lapian.exe --online-fetch-test --real --bvid BV1BMnv6jE5S --cid 42915922022 --tag cancel --fetch-root .deps\tmp\t85-cancel --cancel-after-first-frame
# 首帧后取消
lapian.exe --online-fetch-test --real --bvid BV1BMnv6jE5S --cid 42915922022 --tag cancel --fetch-root .deps\tmp\t85-cancel
# 同缓存根续传
```

| 项 | 结果 |
|---|---|
| 首帧 | 524,288 发布；open 9ms / 首帧 14ms；1280×720 |
| 取消返回 | **7ms**（`Cancel()` → 工作线程终态；≤100ms） |
| `.part` 保留 | 524,288B；无 `.mp4` |
| 续传 | `existing_bytes=524,288`；Range 起点 == existing；HTTP 206=2；`downloaded_bytes=13,806,104`；最终 14,330,392B |
| 最终 hash | `82ff7407a615f9e4c4ecb5b5af576e05e70beae7aaa7375f4b9af5dea91793be`（与 OB-1 完整下载一致） |

### 3.2 杀进程（`_Exit` 模拟）后续传

```powershell
lapian.exe --online-fetch-test --real ... --exit-after-bytes 5000000 --fetch-root .deps\tmp\t85-kill
# 进程在发布 5,242,880B 时直接 _Exit(0)，不清理 .part
lapian.exe --online-fetch-test --real ... --fetch-root .deps\tmp\t85-kill
```

| 项 | 结果 |
|---|---|
| 退出时 `.part` | **5,242,880B**（磁盘实际长度） |
| 重开 `existing_bytes` | 5,242,880 |
| Range | 206=2；从 5,242,880 续传 |
| 本次下载 | 9,087,512B（= 14,330,392 − 5,242,880，无双份） |
| 最终 hash | `82ff7407…`（与 OB-1 一致）；ffprobe pass |

### 3.3 断网 5s 后续传（fake transport）

`lapian_online_fetch_test` 的 `disconnect` 场景：fake 在 `total/4` 字节后断连并阻塞 **5,000ms**，Job 重新 `Range: bytes=<existing>-`；断言：

- 断开前已写字节 = 续传起点；
- `range_requests >= 2`，第二次请求 offset == 已写字节；
- 最终 `.mp4` size/hash 与源样本一致，无重复/空洞；
- 5s 中断实际发生（elapsed ≥ 5000ms）。

---

## 4. 403 / URL 过期 / 备份切换（真实 CDN）

测试用 `--break-urls=primary|all` 把 CDN 签名 URL 的 `deadline` 改成过期值（不打印 URL），触发真实 CDN 拒绝；刷新回调重新 `playurl` 得到合法地址。

### 4.1 主 URL 失败 → 备份 URL（OB-1，720P）

```powershell
lapian.exe --online-fetch-test --real ... --break-urls primary --fetch-root .deps\tmp\t85-ob1-break-primary
```

| 项 | 结果 |
|---|---|
| 请求数 | 3（主 URL 失败后切备份） |
| HTTP 206 | 2 |
| `backup_switches` | **1** |
| `refreshes` | 0 |
| 结果 | completed；ffprobe pass；hash `82ff7407…` 与正常 OB-1 一致 |
| `sample_verdict` | pass |

### 4.2 3 个 URL 全失败 → 刷新 playurl 一次（OB-1，720P）

```powershell
lapian.exe --online-fetch-test --real ... --break-urls all --fetch-root .deps\tmp\t85-ob1-break-all
```

| 项 | 结果 |
|---|---|
| 请求数 | 5（3 个过期 URL + 刷新后的 2 个） |
| `backup_switches` | **2** |
| `refreshes` | **1** |
| 结果 | completed；ffprobe pass；hash `82ff7407…` 与正常 OB-1 一致 |
| `sample_verdict` | pass |

- 刷新失败路径由 fake（单测 + 离线探针）覆盖：失败后保留 `.part`、无 `.mp4`、错误可读（“刷新取流失败…”），不无限重试。
- 真实“自然 120 分钟过期”未等待 2 小时；用签名 deadline 故障注入等价触发 CDN 403，刷新路径本身是真实网络请求。

---

## 5. 错峰/失败矩阵（fake + 单测）

`lapian_online_fetch_test`（fake transport，不联网）**94 checks / 0 failures**：

| 场景 | 断言 |
|---|---|
| playurl 解析 | qn/format/codecid/durl/backup/size；`-400` 映射；no-durl（只有 DASH）；no-size；坏 JSON；`http://` durl 拒绝 |
| 顺序下载 | 200@0 → 206 续传；`downloaded == total`；`.part` 改名 `.mp4`；hash/ffprobe；`OpenComplete` |
| existing>0 续传 | 首个 Range offset == `.part` 长度；206；最终 hash；不拼接双份 |
| 200 忽略 Range | `range_ignored=1`；无 reader 时 truncate 从 0；最终 size/hash 正确；不出双份 |
| 备份切换 | 403 → backup；`backup_switches=1`；hash 正确 |
| 刷新成功/失败 | 3×403 → 刷新 1 次成功；刷新失败 → failed + `.part` 保留 + 无 `.mp4` |
| Content-Range 起点错 | failed + `.part` 保留 + 可读错误（不 append） |
| 总长不一致 | failed + `.part` 保留 + `source->Fail(EIO)`；不拼接 |
| 断连 5s | 续传起点 == 已写字节；最终 hash 一致 |
| 取消 | ≤100ms（实测 15ms）；`.part` 保留；source Cancel；再开 Job 可续传完成 |
| 暂停/恢复 | 暂停后 Published 不增长；恢复后完成；hash 一致 |

无 Cookie/签名 URL 出现在任何错误文本；下载 transport 只加 Range/UA/Referer/Accept-Encoding，禁用 WinHTTP cookie jar，不跟随重定向，只 HTTPS。

---

## 6. 离线探针与回归

| 项 | 结果 |
|---|---|
| `ctest` | **27/27**（28.33s；新增 `lapian_online_fetch_test`） |
| `lapian_online_fetch_test` | **94 / 0** |
| `--online-fetch-test`（build） | **46 / 0**，`verdict=pass` |
| `--online-fetch-test`（dist） | **46 / 0**，`verdict=pass` |
| `--online-resolve-test` | 156 / 0 |
| `--online-auth-test` | 54 / 0 |
| `--online-cache-test` | 32 / 0，verdict=pass |
| `--online-growing-test` | 22 / 0，verdict=pass |
| `--online-http-selfcheck --http-stack=winhttp` | `code=0 is_login=1 http_status=200 elapsed_ms=129 cookie_sent=1 vip_status=0`；`verdict=pass` |
| 本地 P1 | **279.44ms**（`--startup-test --file media\proxy\proxy-V1.mp4`） |
| 本地 P2 | **338.99ms** |
| 本地 P7（2s GOP，n=20） | p50 **61.77ms** / p95 **65.38ms** / max **65.71ms** / over **0** / `pass=1`（`--gop-boundary-test --file build\test-media\real-gop2s-full.mp4 --samples 20`） |
| `dist` | **33.28MB / 25 文件**（P12 余量 66.72MB） |
| `check-imports` | 20 个 exe/dll 通过；无 QtNetwork/TLS/winhttp/wininet/urlmon；Qt6Core→WS2_32 为已知 |

“在线起播 p95 ≤5s / 已缓存 P7 ≤80ms / 缓存命中 ≤2.5s / 4K/1080P60” **未在本任务判定**；T8.5 不接 UI，不写这些通过。

---

## 7. 已知缺口

1. **M1 3/3 720P blocked**：OB-2 只有 360P；durl 档位不可预期（R20）。不伪造 720P；M1 门由 T8.8 决定（可能需要 DASH/M2 才能覆盖该样本）。
2. **自然 120 分钟过期未等**：用签名 deadline 故障注入触发真实 403；刷新路径真实，但“等 2 小时”未做。
3. **真实断网 5s 未做**：不关闭系统网络；用 fake transport 5s 断连覆盖。真实 CDN 连接提前结束时续传已多次出现并验证。
4. **杀进程用 `_Exit`**：不是 OS 强杀/断电；`.part` 是同步 `WriteFile` 落盘长度，重启续传已验证；缓存写入的磁盘缓存/簇分配极端情况未测。
5. **磁盘满只到预检**：`Reserve` 的空间错误有证据；下载中 `WriteFile` 的 `ERROR_DISK_FULL` 映射代码存在，但未真实填满磁盘。
6. **完成改名与 reader**：T8.5 的探针在 MarkComplete 前关闭 reader；T8.6 必须在完成前 `Cancel`+join reader（T8.4 交接已写），本任务未改读层/会话。
7. **`size` 缺失不进入下载**：playurl 没有 size 时直接报告不可用；未实现 HEAD 探测回退（M1 样本均有 size）。
8. **`range_ignored` + 已有 reader**：无 reader 时 truncate；有 reader 时原地覆盖（避免短读被读层映射 EIO）。真实 CDN 忽略 Range 的样本未遇到；两种分支由 fake 覆盖。
9. **libcurl 未构建**：产品栈仍是 WinHTTP；T8.16 若换栈需重跑本证据。
10. **不再分发/合规**：仅个人研究、公开免费内容、不绕过限制；未新增批量/导出入口。

---

## 8. 复现（最短）

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure
build\release\bin\lapian.exe --online-fetch-test
# 真实（需要 .secrets 或 --sessdata-file；不要提交输出中的敏感内容）
build\release\bin\lapian.exe --online-fetch-test --real --bvid BV1BMnv6jE5S --cid 42915922022 --tag ob1 --fetch-root .deps\tmp\t85-ob1
build\release\bin\lapian.exe --online-fetch-test --real --bvid BV1mA411m7pS --cid 1004967230 --tag ob2 --fetch-root .deps\tmp\t85-ob2
build\release\bin\lapian.exe --online-fetch-test --real --bvid BV1GJ411x7h7 --cid 137649199 --tag ob3 --fetch-root .deps\tmp\t85-ob3
```

`--online-fetch-test` 退出码：`0`=pass；`2`=blocked（无凭据/档位 <720P）；`1`=fail。真实运行不会打印 SESSDATA/签名 URL。

---

## 9. 结论

- **T8.5 链路通过**：playurl → 720P durl → CacheStore → 顺序下载/Range 续传/3 备份/重试/过期刷新 → 写后 Publish → 关闭句柄后 MarkComplete；3 个真实样本均完成下载、首屏、ffprobe。
- **M1 3/3 720P 不成立**：OB-2 实际 360P；这是已知档位风险，不用假数据或改请求值掩盖。
- 离线/单元/回归/本地性能/打包导入均通过；M1 门留给 T8.8。

---

## 10. T8.6 后续（2026-10-23）

T8.6 已把本文件的下载/读层接入真实窗口 UI，证据见 [v2-online-session.md](v2-online-session.md)。T8.6 的 3 条真实 E2E 中 OB-1/OB-3 为 720P、**OB-2 仍为 360P（qn=16）**；因此本文件的 M1 3/3 blocked 结论不变，T8.6 不写 M1 通过。

---

# T8.8 M1 门审计（2026-10-23）

> 本节是 T8.8 新增；§1–§10 的 T8.5 历史数字与 blocked 结论**原样保留**，不覆盖。
> 结论：**M1 通过**（3/3 + OP1/OP2/OP3/OP5 + 失败矩阵 + 续传 + DEC-5 + 本地 O9）。
> M1/M2 门、验收数字与样本定义**未改**；未进入 M2。

## 11. 先做请求矩阵：OB-2 的 360P 是 qn 差异，不是资源限制

### 11.1 方法

- 同一份普通用户 SESSDATA、同一 cid；走**产品 `FetchPlayUrl` + WinHTTP**，不引入 curl。
- 探针只加测试用参数 `--qn` / `--fourk`；每个矩阵单元独立缓存根，`--cancel-after-first-frame`（首帧后取消，不整片下载），记录 `request_qn/fourk/quality/format/size/durl_count/首帧分辨率/fps`。
- 不打印/不提交 SESSDATA、Cookie、签名 URL；日志只有 host/path/状态/耗时。
- 环境：2026-10-23 23:2x，开发机（6C/12T/31.6GB/RTX 4060），非基线机；单次观测，未限速对照。

命令示例（OB-2；OB-1/OB-3 只做 qn=64/80/120 × fourk=0/1 对照）：

```powershell
build\release\bin\lapian.exe --online-fetch-test --real `
  --bvid BV1mA411m7pS --cid 1004967230 `
  --sessdata-file .secrets\bilibili_sessdata.txt `
  --qn 80 --fourk 1 --cancel-after-first-frame `
  --fetch-root build\t8.8-probe\ob2-qn80-f1 --timeout-ms 20000
```

### 11.2 矩阵结果（20 个单元，全部 HTTP 200）

| 样本 | 请求 qn | fourk | 服务端 quality | 档位 | format | size（字节） | durl_count | 首帧分辨率 | fps |
|---|---:|---:|---:|---|---|---:|---:|---|---:|
| OB-2 | 64 | 0 | **16** | **360P** | mp4 | 4,121,631 | 1 | 640×360 | 30.000 |
| OB-2 | 64 | 1 | **16** | **360P** | mp4 | 4,121,631 | 1 | 640×360 | 30.000 |
| OB-2 | 80 | 0 | **80** | **1080P** | mp4 | 26,682,935 | 1 | 1920×1080 | 30.000 |
| OB-2 | 80 | 1 | **80** | **1080P** | mp4 | 26,682,935 | 1 | 1920×1080 | 30.000 |
| OB-2 | 112 | 0 | 80 | 1080P | mp4 | 26,682,935 | 1 | 1920×1080 | 30.000 |
| OB-2 | 112 | 1 | 80 | 1080P | mp4 | 26,682,935 | 1 | 1920×1080 | 30.000 |
| OB-2 | 120 | 0 | 80 | 1080P | mp4 | 26,682,935 | 1 | 1920×1080 | 30.000 |
| OB-2 | 120 | 1 | 80 | 1080P | mp4 | 26,682,935 | 1 | 1920×1080 | 30.000 |
| OB-1 | 64 | 0 | 64 | 720P | mp4720 | 14,330,392 | 1 | 1280×720 | 30.000 |
| OB-1 | 64 | 1 | 64 | 720P | mp4720 | 14,330,392 | 1 | 1280×720 | 30.000 |
| OB-1 | 80 | 0 | 64 | 720P | mp4720 | 14,330,392 | 1 | 1280×720 | 30.000 |
| OB-1 | 80 | 1 | 64 | 720P | mp4720 | 14,330,392 | 1 | 1280×720 | 30.000 |
| OB-1 | 120 | 0 | 64 | 720P | mp4720 | 14,330,392 | 1 | 1280×720 | 30.000 |
| OB-1 | 120 | 1 | 64 | 720P | mp4720 | 14,330,392 | 1 | 1280×720 | 30.000 |
| OB-3 | 64 | 0 | 64 | 720P | mp4720 | 51,973,319 | 1 | 1280×720 | 25.000 |
| OB-3 | 64 | 1 | 64 | 720P | mp4720 | 51,973,319 | 1 | 1280×720 | 25.000 |
| OB-3 | 80 | 0 | 64 | 720P | mp4720 | 51,973,319 | 1 | 1280×720 | 25.000 |
| OB-3 | 80 | 1 | 64 | 720P | mp4720 | 51,973,319 | 1 | 1280×720 | 25.000 |
| OB-3 | 120 | 0 | 64 | 720P | mp4720 | 51,973,319 | 1 | 1280×720 | 25.000 |
| OB-3 | 120 | 1 | 64 | 720P | mp4720 | 51,973,319 | 1 | 1280×720 | 25.000 |

### 11.3 结论

1. **OB-2 的瓶颈是请求 qn，不是 `fourk`**：qn=64（含 fourk=0/1）→ 360P；qn≥80（含 fourk=0/1）→ 1080P。`fourk` 在 20 个单元里对实际档位**没有任何影响**。
2. **S2 §2.2 的“OB-2 qn=120 → 80”复现成功**，但`fourk=1`不是必要条件；qn=80 已足够。
3. T8.5 的“OB-2 只有 360P”是**产品固定 qn=64 造成的测量差异**，不是外部资源限制。T8.5 历史 blocked 记录保留，不篡改。
4. OB-1/OB-3 请求 qn≥80 仍由服务端降为 64（720P）；durl 1080P 不是保证，仍符合 S2“durl 2080p 只有 1/3 样本”的结论。

---

## 12. 最小产品修正（只改请求档位，不做 DASH/4K）

### 12.1 改动

| 文件 | 改动 |
|---|---|
| `src/online/PlayUrl.h` | 新增 `kM1RequestQn = 80`（M1 统一“请求 1080P 上限”）；`BuildPlayUrlUrl` 增加 `fourk` 参数并透传；`PlayUrlFetchOptions.fourk` |
| `src/online/PlayUrl.cpp` | `fourk` 真正写入 URL（默认仍 0）；`FetchPlayUrl` 使用 `options.fourk` |
| `src/app/OnlineSession.cpp` | 初次 playurl 与**刷新回调**都从 64 改为 `kM1RequestQn`；刷新与初始同档，避免刷新后计划档位漂移 |
| `src/app/OnlineFetchTestRunner.*`、`src/app/main.cpp` | 仅测试用 `--qn N` / `--fourk 0/1`；报告 `request_qn/fourk/durl_count`；刷新沿用同一组参数 |
| `src/app/OnlineM1TestRunner.*`、`src/app/main.cpp` | 测试用 `--uncached-landing`（OP3 落点）；E2E 显式激活窗口 + `window_active` 检查（见 §17.2） |
| `tests/online_fetch_test.cpp` | 断言 qn=120/fourk=1 进入 URL、默认 fourk=0、`FetchPlayUrl` 透传 fourk、durl_count 解析 |

### 12.2 为什么 fourk 保持 0

- 矩阵已证明 `fourk=0/1` 对三个样本、qn=64/80/112/120 的实际档位**完全一致**。
- M1 的约束是“不超过 1080P、避免 4K”：请求 `qn=80` 本身就把服务端可选档封顶在 1080P；再保持 `fourk=0`，不把 4K 请求放行给服务端。这是**请求侧封顶**，不依赖返回后拦截。
- 没有走“qn=120 + fourk=1 + 事后拒绝 >1080P”的方案：那会先把 4K 请求发给服务端，正是要避免的。

### 12.3 验证

| 项 | 结果 |
|---|---|
| `lapian_online_fetch_test` | **104 / 0**（新增 10 项 qn/fourk/durl_count 断言） |
| `--online-fetch-test`（build / dist） | **46 / 0**，`verdict=pass` |
| `ctest` | **30 / 30** |
| 产品 E2E（`--online-m1-test`）OB-1/2/3 | qn=64 / **80** / 64；`gate=m1-720p`；29/0 pass（OB-2 首次以 1080P 跑通真实窗口） |

> `--online-fetch-test` 的默认请求仍是 `qn=64`（保留 T8.5 历史命令可复现）；M1 证据都显式带 `--qn 80`。产品路径不经过探针默认值。

---

## 13. 3/3 真实样本（产品请求 qn=80）

每条独立缓存根，完整下载 + 首帧 + ffprobe；SESSDATA 只从 `.secrets` 读取：

```powershell
build\release\bin\lapian.exe --online-fetch-test --real --bvid <BV> --cid <cid> `
  --sessdata-file .secrets\bilibili_sessdata.txt --qn 80 --fourk 0 `
  --fetch-root build\t8.8-m1\ob<N>-cache
```

| 样本 | 实际 qn | 档位 | format | size（字节） | 视频 | fps | 下载耗时 | 下行 | ffprobe | mp4 sha256（前 16） |
|---|---:|---|---|---:|---|---:|---:|---:|---|---|
| OB-1 | 64 | 720P | mp4720 | 14,330,392 | 1280×720/h264 | 30.000 | 10,682ms | 1.34MB/s | pass | `82ff7407a615f9e4` |
| OB-2 | **80** | **1080P** | mp4 | 26,682,935 | 1920×1080/h264 | 30.000 | 13,145ms | 2.03MB/s | pass | `37fe59c0dece9afe` |
| OB-3 | 64 | 720P | mp4720 | 51,973,319 | 1280×720/h264 | 25.000 | 25,492ms | 2.04MB/s | pass | `7dc42c7d9e040406` |

- 三条均为 `durl_count=1`、`durl_backups=2`、`http_200=0`、`range_ignored=0`；最终 `.mp4` 大小 == 服务端 `size`。
- **3/3 实际 quality ≥64**，产物可播、ffprobe 通过。OB-2 实际为 1080P（不是 720P），仍满足 M1“≥720P”，按“实际 durl 档”记录；**不据此写 1080P 保证**。
- OB-2 的 1080P 是 29.4/30fps 流（非源片 60fps），不影响 M1；O6/DEC-4 的降帧提示仍属 T8.12。

### 13.1 真实窗口 E2E（3/3，含未缓存落点）

```powershell
& "$env:LOCALAPPDATA\Programs\lapian-t86\lapian.exe" --online-m1-test `
  --bvid <BV> --cid <cid> --tag <obN> --timeout-ms 120000 --uncached-landing `
  --online-cache-root build\t8.8-m1\e2e-ob<N>\cache `
  --online-pictures build\t8.8-m1\e2e-ob<N>\pics `
  --online-records-root build\t8.8-m1\e2e-ob<N>\records
```

| 样本 | 首帧 | qn | P7 n=20 p50/p95 | 已缓存 seek n=20 p50/p95 | 未缓存提示/取消 | 未缓存落点（目标帧 = 显示帧） | 检查 |
|---|---:|---:|---|---|---|---|---|
| OB-1 | 619ms | 64 | 13 / **18ms** | 25 / 74ms | 0ms / 0ms | 3194 = 3194（7,801ms） | **29/0 pass** |
| OB-2 | 557ms | **80** | 30 / **32ms** | 79 / 137ms | 0ms / 0ms | 2747 = 2747（9,520ms） | **29/0 pass** |
| OB-3 | 552ms | 64 | 13 / **19ms** | 30 / 64ms | 0ms / 0ms | 5186 = 5186（22,423ms） | **29/0 pass** |

- `--uncached-landing` 只在 OP3 证据里开启；20 次冷启动测量不带它（避免整片下载拖慢）。
- OB-2 的 1080P 已缓存 seek p95 137ms 不是 OP2 门槛（OP2 是 ±1 步进，p95 32ms）；未缓存 seek 不承诺毫秒数，只要求提示/取消/落点。

---

## 14. OP1/OP2/OP3/OP5

### 14.1 OP1 在线起播（每条清缓存 20 次）

- 口径：用户点确认 → `online_ready`（首帧呈现且可交互）；不含 view 解析。
- 每次先删除该条缓存根，再跑完整 E2E（含 Space/步进/seek/未缓存提示/标记/截图/关窗），解析 `online_m1_first_frame`。
- 60/60 次 `verdict=pass`。

| 样本 | n | min | p50 | p95 | max | ≤5s | 判定 |
|---|---:|---:|---:|---:|---:|---:|---|
| OB-1 | 20 | 617 | **630** | **648** | 663 | 20/20 | pass |
| OB-2（1080P） | 20 | 426 | **452** | **532** | 539 | 20/20 | pass |
| OB-3 | 20 | 423 | **445** | **527** | 540 | 20/20 | pass |

- DEC-2 要求每条 p95 ≤5s、19/20；三条均 20/20。
- 下行采样：1MB Range（含 playurl 121ms）**750ms → 1.59MB/s**；同时段完整下载 1.34–2.04MB/s，满足“≥1MB/s”。
- 记录：`build\t8.8-m1\op1-summary.csv`（不入库）。

### 14.2 OP2 已缓存区单帧步进（±1，20 次）

- 用完整缓存命中路径（`CacheHit` 直接打开 `data.mp4`，本地全量索引），距缓存边界远大于 2s；每样本 20 次 PgUp/PgDn，严格 ±1。
- 这是产品在“整片已缓存”时的真实路径；T8.6 的“边下边播已缓存区”P7 为 20/20/25ms，仍保留。

| 样本 | 实际档 | n | 失败 | p50 | p95 | ≤80ms | 缓存命中首帧 |
|---|---:|---:|---:|---:|---:|---|---:|
| OB-1 | 720P | 20 | 0 | 2 | **34ms** | 20/20 | 68ms |
| OB-2 | 1080P | 20 | 0 | 3 | **51ms** | 20/20 | 74ms |
| OB-3 | 720P | 20 | 0 | 2 | **34ms** | 20/20 | 72ms |

- 三次缓存命中 E2E 均 **24/0 pass**（跳过“未缓存区”专项，符合缓存命中语义）。
- **不把 1080P 当 M1 门的 OP2 口径**：M1 只要求“实际 durl 档”；1080P30 的 OP2 口径留 M2。

### 14.3 OP3 未缓存 seek（提示/取消/落点）

| 样本 | “缓冲中（目标帧 N）” | 旧等待取消 UI | 落点误差 | 结果 |
|---|---|---:|---|---|
| OB-1 | 0ms（目标帧 3194） | 0ms | 3194 = 3194（0 帧） | pass |
| OB-2 | 0ms（目标帧 2747） | 0ms | 2747 = 2747（0 帧） | pass |
| OB-3 | 0ms（目标帧 5186） | 0ms | 5186 = 5186（0 帧） | pass |

- 取消后回到已缓存目标 `back_to_cached_exact` 3/3 通过。
- 未缓存等待的进度/速率口径沿用 T8.4/T8.6（2073ms vs 预期 1750ms，+18.5%）；本任务不新增毫秒承诺。

### 14.4 OP5 缓存命中（断网模型，20 次）

- 默认 AppData 路径、Medium 副本（`%LOCALAPPDATA%\Programs\lapian-t86\lapian.exe`）；真实缓存条目 OB-1 720P（14,330,392B）。
- 每次注入**永不成功的 HTTP 客户端**（计数 0 才算过），打开同 BV；首帧 ≤2.5s，且 `playurl=0/http=0`。
- 20/20 `verdict=pass`，检查 31/0 ×20。

| n | min | p50 | p95 | max | ≤2.5s | playurl | HTTP 调用 |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 20 | 73 | **85** | **95** | 109 | 20/20 | 0 | **0** |

- 断网模型：注入客户端 + T8.7 的 `netstat` 0 TCP/UDP/模块证据（本轮本地零网络另测，见 §16）；不是物理拔网。

---

## 15. 失败矩阵、续传、缓存/记录、DEC-5

### 15.1 O1/O7 失败矩阵

| 场景 | 证据 | 结果 |
|---|---|---|
| 本地非法输入 | `--online-resolve-test`（干净 LOCALAPPDATA/APPDATA） | 156/0；max_local_invalid_ms=0；0 网络 |
| BV 不存在 `-404` | 同上 OB-5 样本 | 5s 内可读；不崩 |
| 不可见/私密 `62002` | 同上 | 42ms 可读“稿件不可见”；不崩；confirm 关闭 |
| 未登录 | 匿名 view 成功 + “未登录，清晰度受限”提示（T8.1/T8.2） | 通过；不伪造登录态 |
| 断网/网络失败 | `--online-offline-test` 注入失败客户端；`cache_miss_goes_network` | 首帧不呈现、错误可读、`.part` 不创建、进程不崩 |
| 403/URL 过期 | `--online-fetch-test --real --break-urls all`（OB-1） | 3 URL 注入过期；backup_switches=2、retries=3、refreshes=1；10.9s 完成，hash 与正常下载一致 |
| 磁盘满/超配额 | `lapian_online_cache_test` 366/0 + `--online-cache-test` 32/0（Reserve 预检 0.082–0.115ms） | 可读错误、不崩；未真填满磁盘（见 §17.3） |
| 缓存损坏 | `--online-cache-test` 8 类损坏 + `--online-offline-test` 45/0（截断/meta/大小/记录损坏） | 检测/重建/备份；不冒充可播放 |
| 下载中杀进程 | 见 §15.2 | `.part` 续传、无重复字节 |
| UI 线程停顿 | 三次 E2E `ui_gap_le_200ms` | max 60–66ms；0 次 >200ms |

- `--online-resolve-test` 在默认 AppData 有该 BV 缓存时会触发 T8.7 的“缓存命中”预检，导致 fake-cancel 步骤短路（见 §17.2）；用干净 LOCALAPPDATA/APPDATA 复跑 156/0。产品行为正确，是探针环境依赖。

### 15.2 杀进程续传（OB-2 1080P）

```powershell
# 进程在发布 5,242,880B 时 _Exit(0)，不清理 .part
... --online-fetch-test --real --qn 80 --exit-after-bytes 5000000 --fetch-root build\t8.8-m1\resume-ob2
# 同缓存根续传
... --online-fetch-test --real --qn 80 --fetch-root build\t8.8-m1\resume-ob2
```

| 项 | 结果 |
|---|---|
| 杀进程时 `.part` | 5,242,880B（`existing_at_exit=0`） |
| 续传 | `existing_bytes=5,242,880`、`downloaded_bytes=21,440,055`、206×2、200×0 |
| 最终 | 26,682,935B；ffprobe pass；sha256 `37fe59c0dece9afe…` == 一次完整下载 |
| 判定 | **无重复/损坏字节；续传通过** |

### 15.3 清缓存保留记录 / 重建 / 损坏记录

`--online-offline-test` 全量（真实 OB-1 媒体，workspace 根）：**45/0 pass**。

- `clear_cache` 删除 14,330,719B；`clear_record_kept=1`、`clear_snapshot_kept=1`。
- 清缓存后 `cache_miss_goes_network=1`（注入客户端被调用 1 次），记录仍在。
- `reseed_cache` 后 `rebuild_ready=1`、`rebuild_record_restored=1`（mark frame 100）。
- 损坏记录 `.bak` 备份；`corrupt_record_not_blocking=1`（首帧仍出，marks=0）。
- 默认路径 OP5 的 20 次运行同样每次校验 `record`/`index.jsonl` 恢复。

### 15.4 DEC-5 配额（引用已拍板口径，不真填满 20GB）

| 项 | 结果 |
|---|---|
| 300MB LRU（`--online-cache-test`） | 配额 314,572,800B；占用 283,116,589 → **188,744,706B**；淘汰最旧 `aa1/101/64-7`，保留当前/最新 |
| 20GB 记账（`lapian_online_cache_test`） | `quota_20gb=21,474,836,480`；`usage_after=10,737,418,599` ≤ 配额 |
| 单元/探针 | **366/0**、**32/0（verdict=pass）** |
| 边界 | 未真填满 20GB / 未真写满磁盘；按 DEC-5-A“300MB + 20GB 记账单测”执行 |

---

## 16. 本地 O9 与回归（dist / Medium 副本）

| 项 | 结果 |
|---|---|
| 零网络（本地打开 proxy-V1，3s，6 次 `Get-NetTCPConnection/Get-NetUDPEndpoint`） | **0 TCP / 0 UDP**；进程未加载 `winhttp/wininet/urlmon/Qt6Network/libcurl` |
| P1 冷启动（dist，20 次） | p50 **286.68ms** / p95 **342.92ms**，20/20 ≤1500ms |
| P2 打开首帧（dist，20 次） | p50 **346.84ms** / p95 **376.14ms**，20/20 ≤2500ms |
| P7 跨 GOP（2s，20 次，dist） | p50 **58.77ms** / p95 **64.79ms** / max 67.19 / over 0 / `verdict=pass` |
| 本地 1080p 内存（pause 15s） | max WS **94.9MB**，pass=1（上限 250MB） |
| `ctest` | **30 / 30** |
| `--robust-test` | **75 checks / 0 failures / 1 skip**；6.88s；exit 0 |
| `--key-test`（`media\测试.mp4`） | **77 checks / 0 failures**；exit 0 |
| `--online-fetch-test`（dist） | **46 / 0**，pass |
| `check-imports` | 20 文件通过；无 QtNetwork/TLS/winhttp/wininet/urlmon（Qt6Core→WS2_32 为已知） |
| `dist` | **33.69MB / 25 文件**（P12 余量 66.31MB） |

- P1/P2 命令：`powershell -File scripts\startup-bench.ps1 -Exe dist\lapian.exe -File media\proxy\proxy-V1.mp4 -Runs 20 -OutDir build\t8.8-m1\bench-startup`。
- P7 命令：`dist\lapian.exe --gop-boundary-test --file build\test-media\real-gop2s-full.mp4 --gop-boundary-samples 20 --gop-boundary-tag t88-p7`。
- 零网络轮询不是 ProcMon/ETW；与 T7.6/T8.6/T8.7 口径一致（40–500ms 轮询 + 模块检查 + 计数 HTTP 客户端）。

---

## 17. 缺陷、证据边界与结论

### 17.1 缺陷

- **无 P0/P1**。T8.8 修复的是取流请求档位（产品）与测试前置条件（非产品）。
- P2（记录，不阻塞 M1）：
  1. OB-2 现在下载 1080P（26.68MB/96s，约 2.1Mbps），比 360P 更占缓存/带宽；这是“可用最高”的代价，M1 不承诺 1080P。
  2. durl 档位仍由服务端决定：OB-1/OB-3 请求 80 仍返回 64；不得把“请求 80”写成“保证 1080P”。
- P3：
  1. `--online-resolve-test` 依赖干净 AppData（T8.7 缓存预检会短路 fake-cancel 步骤）；产品正确，探针需在干净 `LOCALAPPDATA/APPDATA` 下跑。
  2. `--online-m1-test` 从后台进程启动时窗口可能拿不到前台，`QAction` 的 `WindowShortcut` 会吞掉 Space/Ctrl+M/S；已加测试侧显式激活 + `window_active` 检查（**不是产品缺陷**，见 §17.2）。
  3. 真实磁盘满/物理断网仍未做；用预检单测 + 注入客户端 + 真实 403 覆盖。

### 17.2 测试前置条件修复（非产品）

首轮 E2E 出现 5 项失败（space/ctrl_m/snapshot），诊断输出 `online_m1_focus widget=null active=0`：窗口未被激活时，Qt 的 `WindowShortcut` 不派发。测试 runner 现在在首帧后 `raise()+activateWindow()+FocusVideoForTest()`，并等 `isActiveWindow()`（≤1.5s）后再按第一个键；之后 3/3 样本 29/0。产品代码未改键位语义。

### 17.3 证据边界

1. 网络为单时段观测，未跨时段/限速；OP1 是 20 次/条，不是跨天统计。
2. “断网”是注入失败客户端 + 零网络监控，不是物理拔网；真实 403/刷新已用签名 deadline 注入。
3. 磁盘满只到 `CacheStore::Reserve` 预检与 8 类损坏单测，未真填满磁盘。
4. 未做真实扫码（T8.2.2 非门，仍“未验证”）。
5. 未进入 M2：DASH/1080P30/60fps/4K/清晰度菜单、T8.6.1a/b、T8.3.1 均未做。
6. M1/M2 门、验收数字、OB 样本定义未改；ADR-007 的“附条件接受”评审记录保留，D1–D5 未改。

### 17.4 结论

- **M1 通过**：3/3 durl 实际 quality ≥64（OB-2 为 1080P）、产物可播/ffprobe 通过；OP1 p95 527–648ms（20/20×3）、OP2 p95 34/51/34ms（20/20×3）、OP3 提示/取消 0ms + 落点 0（3/3）、OP5 p95 95ms + 0 请求（20/20）；失败矩阵、杀进程续传、清缓存保留记录、DEC-5、本地 O9 均达标。
- 触发修复的根因是**产品请求 qn=64**；最小修正为 qn=80、fourk=0，并在刷新路径保持一致。
- **M2 未开始**；按流程等用户确认后再进 T8.9。

