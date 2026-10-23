# T8.5 证据：durl 取流（2026-10-23）

- **状态**：**T8.5 实现完成**（playurl → durl → CacheStore → 顺序下载/Range 续传/3 备份/重试/URL 过期刷新 → GrowingSource.Publish → 关闭写句柄后 MarkComplete）。
- **M1 3/3 判定**：**blocked**。登录样本 OB-2（`BV1mA411m7pS` / cid `1004967230`）的 `durl` 实际只给 **360P（qn=16）**，请求 qn=64/80/112 均返回 16；OB-1/OB-3 为 720P（qn=64）。因此 **3/3 720P 不成立**，本文件不写“M1 3/3 通过”。M1 门仍由 T8.8 判定。
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
