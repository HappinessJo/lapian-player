# T8.7 缓存命中 + 在线记录证据（2026-10-23）

- **范围**：只做 T8.7。未做 T8.8 M1 门、T8.6.1a/b、T8.3.1 缓存管理 UI/孤儿清理、DASH/1080P60/4K/清晰度选择/扫码；未改 M1/M2 门、验收数字与本地模式路径。
- **基线**：`6cb4c6d`（T8.6 + 热修）；本任务一个可审查提交（hash 见 `git log -1`）。
- **依据**：tasks-v2 v0.14 T8.7、[delta v0.4](../spec-v1.0-delta.md) O5/O6/O10/A2、T8.6 交接、T8.6 证据。
- **一句话**：完整缓存命中不再走 playurl/DownloadJob，直接打开 `data.mp4`；在线标记/备注按 `bvid+cid` 存 `lapian-online-1`，截图落 `Pictures\拉片软件\<bvid>\` 并追加 `index.jsonl`；清缓存保留记录、重建回访、损坏记录不阻塞；**M1 3/3 720P 仍 blocked（OB-2 360P），本任务不判 M1**。

---

## 1. 环境与样本

| 项 | 值 |
|---|---|
| 机器 | DESKTOP-DA6VQGS，Windows 11，RTX 4060（D3D11VA） |
| 构建 | Release，Qt 6.8.3，FFmpeg 7.1.1 最小构建（file/pipe，无网络） |
| 真实样本 | OB-1 `BV1BMnv6jE5S`/cid `42915922022`，720P30，14,330,392B |
| 工作区缓存 | `build/t8.7-online-cache`、`build/t8.7-real-cache`（E2E 不写用户默认目录） |
| 默认 AppData 验证 | Medium 副本 `%LOCALAPPDATA%\Programs\lapian-t86\lapian.exe`；缓存 `%LOCALAPPDATA%\拉片软件\cache`，记录 `%APPDATA%\拉片软件\online` |
| 首帧口径 | 用户确认/`StartOnlineSession` → 首帧呈现且 UI 可交互（`online_ready_`） |

> 工作区 `E:\拉片软件开发` 带 Low Mandatory Level；从 `dist` 启动是 Low integrity，不能写 Medium 的 AppData。默认路径验收只用 Medium 副本，Low 结果只标注“工作区环境”。

---

## 2. 缓存命中路径

### 2.1 代码路径

```text
MainWindow::BeginOnlineRequest / StartOnlineSession
  → OnlineSession::Start
      → CacheStore::Scan + OpenComplete          // 只读本地；0 网络
      → OnlineSession::EmitCacheHitReady
          → OnlineReadyInfo{ cache_hit=true, cache_mp4_path, qn/codecid/title/duration }
  → MainWindow::ApplyOnlineReady
      → ApplyOnlineCacheHit
          → OpenLocalMedia(cache_mp4_path, OnlineOpenContext{cache_hit=true})
              → MediaSession::Open(...)          // 本地全量索引/seek/步进
              → MarkStore::OpenOnline(bvid,cid)  // 在线记录，不读写视频 sidecar
              → AudioEngine/Waveform 本地打开
```

- 命中判断在 **playurl 之前**：`OnlineSession::Start` 先扫描缓存根 `<cache>/<bvid>/<cid>/<qn>-<codecid>/`，只接受 `complete && !broken && 大小匹配` 的条目；多条完整条目按 `last_access_at` 最新、同时间 qn 更高选择。
- 命中后 **不创建 `GrowingSource`/`DownloadJob`/`DownloadTransport`**，不创建/覆盖 `.part`，不发 playurl/CDN 请求。`OnlineReadyInfo.source` 为空，UI 走本地 `MediaSession::Open`（不是 `OpenGrowing`），所以已缓存区的播放/seek/步进/预览按本地口径。
- 命中时仍调用 `CacheStore::OpenComplete`，只更新 `meta.last_access_at`（LRU）；不改 `data.mp4`。
- 缓存损坏（meta/大小不符/文件缺失）：`OpenComplete` 失败或 `Scan` 报 broken 时直接给“缓存损坏，需要重建”，**不联网重下、不覆盖坏数据**；重建入口是 T8.3.1，不在本任务。

### 2.2 复现的旧失败点

实现前用 `tests/online_records_test` 复现：

```text
[FAIL] ready_callback：state=error error=该视频已完整缓存；缓存命中打开将在 T8.7 接入
[FAIL] no_playurl_request：1
```

旧路径在 `OnlineSession::OnPlayUrlDone` 里仍先取 playurl，再 `DownloadJob::Start` 发现 `kCompleted` 后 `Fail`。T8.7 把命中检查提前到 `Start`，上述 11 项测试现在 11/0。

---

## 3. 在线记录（`lapian-online-1`）

### 3.1 位置与格式

- 文件：`%APPDATA%\拉片软件\online\<bvid>-<cid>.lapian.json`；**不在缓存根内**，清缓存/淘汰不会删除。
- schema：`schema="lapian-online-1"`、`schema_version=1`；不复用/不修改 v1 `lapian.marks` sidecar。
- 顶层：`bvid`、`cid`、`qn`、`codecid`、`fps`、`updated_at`、`marks[]`。
- 每条 mark：`id`、`frame`、`pts`、`fps`、`qn`、`codecid`、`label`、`note`、`created_at`、`updated_at`；`fps/qn/codecid` 是**创建该标记时的实际流**。
- 原子写（同目录临时文件 → `FlushFileBuffers` → `ReplaceFile/MoveFileEx`）；损坏 JSON 备份为 `.bak` 后按空记录继续；无标记时删除记录文件；更高 schema 不覆盖。
- 换档：记录键仍是 `bvid+cid`；当前流 fps 与记录顶层 fps 不同时提示，索引就绪后用 PTS 找回当前帧号并提示“已按 PTS 映射”，不静默复用旧帧号。

示例（真实 OB-1，已截断）：

```json
{
  "schema": "lapian-online-1",
  "schema_version": 1,
  "bvid": "BV1BMnv6jE5S",
  "cid": 42915922022,
  "qn": 64,
  "codecid": 7,
  "fps": 30.000011315634925,
  "marks": [{
    "id": "331b3966-...",
    "frame": 100,
    "pts": 3333313,
    "fps": 30.000011315634925,
    "qn": 64,
    "codecid": 7,
    "label": "卡点",
    "note": "T8.7 note 100",
    "created_at": "2026-10-23T11:33:32.572Z",
    "updated_at": "2026-10-23T11:33:32.576Z"
  }]
}
```

### 3.2 截图目录/命名/清单

- 主目录：`%USERPROFILE%\Pictures\拉片软件\<bvid>\`；不可写时回退 `%APPDATA%\拉片软件\online\snapshots\<bvid>\`。
- 命名：`<bvid>_f<frame>_<HH-MM-SS-FF>.png`；重名加 `_1`、`_2`…，不覆盖。
- `index.jsonl` 与 PNG 同目录，PNG 编码/写盘成功后才追加一行；字段：`bvid/cid/qn/codecid/fps/frame/pts/path/saved_at`。追加失败回滚到追加前长度，不写半条；PNG 已成功时只提示清单失败，不回滚截图。
- 示例（真实 OB-1 帧 100）：

```json
{"bvid":"BV1BMnv6jE5S","cid":42915922022,"codecid":7,"fps":30.000011315634925,"frame":100,"path":"C:\\Users\\...\\Pictures\\拉片软件\\BV1BMnv6jE5S\\BV1BMnv6jE5S_f100_00-00-03-10.png","pts":3333313,"qn":64,"saved_at":"2026-10-23T11:45:53.652Z"}
```

---

## 4. 验收结果

### 4.1 单测 / 回归

| 项 | 结果 |
|---|---|
| `ctest` | **30/30**（30.56s；新增 `lapian_online_records_test`） |
| `lapian_online_records_test` | **52 checks / 0 failures**：缓存命中 0 playurl/0 `.part`；记录 schema/字段/原子写/损坏备份/更高 schema 不覆盖/空文件删除；PTS 换档映射；截图目录/命名/清单/AppData 回退；清缓存保留记录 |
| `--online-buffer-test` | **10/0 pass**；关窗 17ms（T8.6 回归） |
| `--online-m1-test` 冷缓存（真实 OB-1） | **27/0 pass**；首帧 619ms；P7 p95 29ms；seek p95 104ms；UI gap 56ms；关窗 258ms |
| `--online-m1-test` 缓存命中（同 BV） | **23/0 pass**；首帧 **70ms**；P7 p95 57ms；seek p95 104ms；UI gap 111ms；关窗 7ms |

> `--online-m1-test` 的 27/0 与 23/0 不是 M1 门；OB-2 仍 360P，不写 M1 通过。

### 4.2 缓存命中/记录 E2E

| 场景 | 结果 |
|---|---|
| `--online-offline-test`（工作区缓存） | **45/0 pass**：命中 0 playurl/0 HTTP；首帧 76–77ms；标记/备注/截图；关窗重开恢复；清缓存记录/截图仍在；重建后记录回访；损坏记录备份后仍可播 |
| `--online-offline-test --keep-cache`（真实 OB-1 缓存，14,330,392B） | **44/0 pass**；首帧 **86ms**；0 playurl/0 HTTP；索引 3314 帧；记录/截图字段与真实流一致 |
| Medium 默认 AppData（3 次） | **31/0 pass ×3**；首帧 **77/74/84ms**（复跑；首轮 80/78/77/74ms）；缓存 `...\AppData\Local\拉片软件\cache\...\data.mp4`；记录 `...\AppData\Roaming\拉片软件\online\...lapian.json`；重开恢复 |
| 默认路径 0 网络（3 次，`netstat -ano` 轮询 40ms） | **0 TCP/UDP 行 ×3**；进程未加载 `winhttp.dll`/`wininet.dll`/`urlmon.dll`/`Qt6Network.dll` |
| 真实缓存来源 | `--online-fetch-test --real`：qn=64/720P、14,330,392B、10.875s、ffprobe pass；随后离线命中 |

默认 AppData 第 1 次运行约 31 项检查（命中/索引/标记/截图/重开）；默认模式不清理用户缓存；该 BV 已有在线记录先备份为 `.t87-backup`，测试结束/失败时恢复，无旧记录则删除测试记录；清缓存/重建/损坏项在独立测试根执行。

### 4.3 本地模式不退化

| 项 | 结果 |
|---|---|
| P1 冷启动（20 次，dist） | p50 **269.45ms** / p95 **296.81ms**，20/20 ≤1500ms |
| P2 打开首帧（20 次，dist） | p50 **334.59ms** / p95 **345.40ms**，20/20 ≤2500ms |
| P7 跨 GOP（2s，20 次，dist） | p50 **58.33ms** / p95 **66.56ms** / max 67.39ms，over 0，verdict=pass |
| 本地零网络（3s） | `netstat` **0 TCP/UDP**；无 winhttp/wininet/urlmon/Qt6Network |
| `check-imports` | 20 文件通过；无 QtNetwork/TLS/winhttp/wininet/urlmon |
| `dist` | **33.68MB / 25 文件**（P12 余量 66.32MB） |

---

## 5. 明确未做/限制

1. **M1 3/3 720P 仍 blocked**：OB-2 实际 qn=16/640×360；T8.7 只做缓存/记录，不判 M1。T8.8 才能判。
2. **未做 T8.3.1**：缓存管理 UI、修改路径/配额、打开缓存目录、启动/定期孤儿清理、清空按钮未接产品；清缓存测试用 `CacheStore::ClearAll` 现有语义。
3. **未做清晰度选择/DASH/1080P60/4K**；缓存命中选“最近访问的完整条目”（同时间 qn 更高），不是用户档位选择。
4. **多 P 缓存探测限制**：`OnlineResolveDialog` 的缓存预检只在输入无 `?p=` 或 P1 时启用；缓存 `meta.json` 没有分P 号，不能为 P2+ 猜分P。带 `?p=N`（N>1）仍走 view；断网时无法离线打开 P2+。
5. **0 网络证据是 netstat 轮询 + 模块检查**，不是 ProcMon/ETW 全程抓包；与 T7.6/T8.6 口径一致。轮询间隔 40ms，理论上可漏极短连接；命中路径代码上不创建 HTTP/Transport，且有 `CountingHttpClient` 计数 0 双证。
6. **默认路径验收只在 Medium 副本**；Low 工作区结果只代表测试根。
7. **在线记录写盘仍在 UI 线程**（与本地标记一致）；首次写入新目录可能受 Defender 扫描影响。缓存命中 E2E 的 mark save 实测 1ms 级，UI gap 111ms；未做异步写盘重构。
8. **损坏记录只覆盖 JSON 损坏/更高 schema**；磁盘只读/ACL 拒绝记录写入时会走已有“标记保存失败”提示，关窗保存失败仍可能弹确认框（与 v1 本地标记语义一致），未改。
9. **截图 `index.jsonl` 追加失败**时 PNG 已保存，只给状态栏提示；未把 PNG 回滚。
10. **真实断网**没有物理拔网/禁用网卡；用“缓存命中 + 计数 HTTP 客户端 + netstat”证明命中路径不联网；T8.13 再做真实断网/403 UI 矩阵。

---

## 6. 复现（最短）

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure

# 工作区缓存命中/记录/清缓存/重建/损坏（用本地样本构造缓存；不联网）
build\release\bin\lapian.exe --online-offline-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --tag t87-offline --timeout-ms 20000

# 真实缓存（先 seed，再离线命中；不打印凭据/签名 URL）
build\release\bin\lapian.exe --online-fetch-test --real --bvid BV1BMnv6jE5S --cid 42915922022 `
  --fetch-root build\t8.7-real-cache --sessdata-file .secrets\bilibili_sessdata.txt --tag t87-real
build\release\bin\lapian.exe --online-offline-test --offline-keep-cache --online-cache-root build\t8.7-real-cache `
  --bvid BV1BMnv6jE5S --cid 42915922022 --offline-media build\t8.7-real-media.mp4 --tag t87-real-hit
```

Medium 默认路径（先复制 `build\release\bin\lapian.exe` 到 `%LOCALAPPDATA%\Programs\lapian-t86`）：

```powershell
& "$env:LOCALAPPDATA\Programs\lapian-t86\lapian.exe" --online-offline-test --offline-keep-cache `
  --offline-default-paths --bvid BV1BMnv6jE5S --cid 42915922022 `
  --offline-media build\t8.7-real-media.mp4 --tag t87-default
```

---

## 7. 结论

T8.7 范围内完成：完整缓存命中 0 网络直接打开 `data.mp4`；`lapian-online-1` 记录/损坏备份/清缓存保留/重建回访/换档 PTS 映射；截图目录/命名/`index.jsonl`；工作区与 Medium 默认 AppData 的命中/记录/重开/0 TCP/UDP 均通过；本地 P1/P2/P7 与零网络不退化；`ctest` 30/30、dist 33.68MB/25。**M1 3/3 720P 仍 blocked，OB-2 仍 360P；T8.7 不判 M1。**
