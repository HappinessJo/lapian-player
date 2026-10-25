# 交接：在线播放修复验收（清晰度切换 + 拖动前后一致/丝滑，2026-10-25）

- **状态**：**partial（部分通过）**。A 清晰度切换 **pass**、B 前后一致 **pass**、
  C 丝滑流畅**稳态 pass、首次拖动冷启动未达门槛**；按任务 §8 不写“完成/丝滑通过”。
  **后续（2026-10-25）**：P0 首次拖动修复后 C **pass（P0 门禁）**，见
  [handoff-2026-10-25-p0.md](handoff-2026-10-25-p0.md) 与
  [tasks-online-parity-fix.md](tasks-online-parity-fix.md) §9；本文件保留当时判定。
- **性质**：验收/文档轮；**本轮不改代码**、不改 M1/M2/P4/P6/P7 门、OB 样本、
  `lapian-cache-1` schema、默认 AVC≤1080p、本地 `PreviewDecoder`/`DragStrategy` 语义。
- **实现提交**：`e1a89c6`（清晰度切换）、`4d89683`（在线拖动前后一致 + 丝滑）；HEAD `4d89683`。
- **验收提交**：`docs: verify online playback parity and quality switch`（只含文档）。
- **任务**：[tasks-online-parity-fix.md](tasks-online-parity-fix.md) v1.2（§8 为本轮结果）。
- **证据**：[online-quality-switch.md](perf/online-quality-switch.md) §10、
  [online-drag-parity.md](perf/online-drag-parity.md) §12。
- **原始日志**：`build/verify-*.log`（`build/` 不入库）。

---

## 1. 改了什么（实现，非本轮）

### 1.1 `e1a89c6` 清晰度切换

- `OnlineSession` 在解析出**实际** `video_key/audio_key` 后、建 `DownloadJob` 前做精确缓存检查；
  两条都完整 → 直接 cache-hit（0 playurl/CDN）；只缺一路 → 复用完整一路、只补缺的一路。
- `DownloadJob::Start` 的 `kCompleted` 降为内部信号，不再报“DASH 条目已完整缓存，但本次未走缓存命中路径”。
- 请求 `codecid` 与缓存不同时下载请求的 codec，不静默复用旧 codec；切档落地后重建档位菜单。
- 新增 `--online-quality-switch-test` 与 4 组缓存切换单测。

### 1.2 `4d89683` 在线拖动

- `OnlinePreviewDecoder` 正/反向统一为“先查精确缓存 → 发布更接近目标且不越过的帧 → 精确后禁止更远帧”，
  目标更新不再中断当前 GOP 解码；有界缓存 72 帧 / ≤64MiB；独立低优先级反向预取；
  远跳 ≥300 帧先发 320×180 粗帧再精确；`Pause/Resume` 保留缓存；`End/Close` 释放。
- `MainWindow`：松手先停预览再精确 seek；未缓存区只置 waiting +“缓冲中（预览）”。
- `VideoDecoder::OpenWithFormat` 新增可选 `thread_count=0`（默认不变；只有在线预取传 2）。
- 新增 `--online-drag-parity-test` / `--local-drag-parity-test` 与诊断字段。

### 1.3 范围核对

两个提交没有改 `src/media/PreviewDecoder.*`、`PreviewRingCache.*`、`DragStrategy.*`、
`src/online/CacheStore.cpp`、`PlayUrl.h`、`tests/drag_*`、OB 样本与 M1/M2/P4/P6/P7 门；
`VideoDecoder` 是追加可选参数、默认 0；本地拖动预览路径未改。

---

## 2. 验证环境

- `scripts\build-release.ps1 -Clean`：222.4s；`ctest` **37/37**（脚本内 62.14s，
  手动 61.21s，TMP/TEMP=`E:\拉片软件开发\.deps\tmp`，Qt `bin` 必须在 PATH）。
- `dist`：**25 文件 / 36,359,496B / 34.68MB**；`lapian.exe` 3,149,824B，
  SHA256 `7A80C9A9B3C865185CEBA1A489FB1AE56635BDDA065E4699C829B0461B7A3761`。
- Medium 副本：`%LOCALAPPDATA%\Programs\拉片软件` 用本次 `dist\*` 覆盖（33 文件），
  `lapian.exe` hash 与 dist 一致；A 与 B/C 都在该副本上跑。
- **默认路径**（Medium，`--online-offline-test --offline-default-paths --offline-keep-cache`）：
  默认缓存 `%LOCALAPPDATA%\拉片软件\cache\BV1BMnv6jE5S\42915922022\80-7` 命中
  （`published=31057770/31057770`、`cache_hit=1`、0 playurl/HTTP、首帧 95ms）；
  默认记录 `%APPDATA%\拉片软件\online\…lapian.json` 可读（schema `lapian-online-1`）并可重开；
  默认截图目录写入成功。探针 **31 项/3 失败**（`mp4_size_matches`、
  `record_mark_fields`、`snapshot_index_fields`），原因是默认目录里 T8.7 遗留记录/截图索引与
  探针期望字段不一致（fixture 不匹配），**不写 31/0、不写默认路径全过**；默认缓存/在线记录
  未被本次验收清空或改写（记录 mtime 仍 17:25）。
- 样本 OB-1 `BV1BMnv6jE5S` / cid `42915922022`（111s，DASH 1080P AVC 30.000fps，3313 帧，5s GOP）；
  默认 DPAPI 凭据，`nav vip_status=1`；未打印/提交 SESSDATA、Cookie、签名 URL。

---

## 3. 验证数字

### 3.1 A 清晰度切换（`--online-quality-switch-test`）

`checks=42 failures=0 verdict=pass`。

| 步 | 结果 |
|---|---|
| 初始 | qn=80 / codecid=7 / fps=30.000；dash_requests=1、playurl=0 |
| 80→112 | landed=1；qn=112 / codecid=7；PTS 1,000,000→1,000,000、第 30 帧；标记 1→1；记录/截图保留；音频复用，只下载 112 视频 47,168,494B |
| 112→80 | landed=1；qn=80 / codecid=7；`cache_hit=1`、dash=0、playurl=0；PTS 1,000,000、第 30 帧；标记/记录/截图保留；三条缓存 complete |
| 误导错误 | 两次切档 error 均空，`no_misleading_complete_error=1` |

### 3.2 B 前后一致 + C 丝滑（`--online-drag-parity-test`）

`checks=45 failures=0`；两组数字（完整表见 perf §12）：

| 场景 | presented/updates | first_ms | max_gap | active_fps | landing |
|---|---|---|---|---|---|
| 正向 15→75 | **57 / 60** | 17 | 33 | **56.21** | 75 / 2,500,000 精确 |
| 反向 75→15 | **57 / 60** | 31 | 33 | **56.72** | 15 / 500,000 精确 |
| 缩放 +10 / −10 | 9/10 / 9/10 | 63 / 63 | 63 / 76 | — | 49 / 41 精确 |
| 远跳 +/− | 2/1 / 2/1 | 31 / 17（粗帧） | — | — | 3000 / 15 精确，精确帧 1695/142ms |

- presented 差 **0.0%**；两边都精确；不是“两边一样差”。
- 缓存峰值 60.75MiB/72 帧；UI gap 32ms；未缓存提示 0ms 并可恢复。
- **冷启动首次拖动 75→65：first 224ms、max_missing 6** → C 的 100ms/“连续 2 个”门槛未达。

### 3.3 本地基线（`--local-drag-parity-test`，1080p）

正向 59 presented / 58.53fps / 空档 21ms / first 17ms；
反向 57 / 57.00fps / 空档 45ms / first 45ms。
在线/本地 = **96.0% / 99.5%**（C 门槛 70%）。

### 3.4 回归

| 项 | 结果 |
|---|---|
| ctest | **37/37** |
| key（Medium） | **77/0** |
| T8.6.1b（Medium，增长会话） | **35/0**（正向 48/56.60fps/首帧 152ms/UI 62ms；反向 27/30.10fps/103ms；landing 75/45 精确；关窗 37ms） |
| `--drag-test --drag-samples 20` | P4 65.6、P6 p95 15.9、P7 0.0/12.6、P8 12.8、P9 33.5；Medium P6 17.7 |
| `--drag-strategy-test` | 3 次中 2 次 `all_pass=1`；1 次 fast_forward `max_gap=51.5ms`（阈值 50）6/7，复跑通过 |
| `--drag-far-test` | 粗帧 13.9/19.8ms → 精确 21.0/29.8ms；release p95 13.3ms；readbacks=0 |
| P6 | 15.9/17.7ms ≤150ms，不退化 |
| 预览缓存 | 在线 60.75MiB；本地策略峰值 84.4MiB + 缩略图 432KB；readbacks=0 |
| 4K（只记录） | `proxy-V4` P4 63.5、P6 p95 144.6、P7 2.0/4.7、P9 229.7、缓存峰值 237.3MiB；不写通过 |

---

## 4. 前后对比（用户现象）

| 现象 | 修复前 | 本次 |
|---|---|---|
| 切 1080P+ 报“已完整缓存…请重新打开” | 报错、无法切档 | qn=112/codecid=7 落地，PTS/标记/记录保留 |
| 切回 1080P30 | 到不了 | `cache_hit=1`，0 dash/playurl |
| 未缩放反向拖动 | 14 presented、首帧 551ms、冻住 | **57 presented、31ms、33ms 空档** |
| 正向拖动 | 55 presented、首帧 101ms | **57 presented、17ms、33ms 空档** |
| 最大缩放 ±1 | 正向闪烁/反向冻住 | 每步 63ms、无闪烁、9/10 有响应 |
| 松手 | — | 前后都精确 landing |

修复前数字来自 [online-drag-parity.md](perf/online-drag-parity.md) §1（`e1a89c6` 基线复现）。

---

## 5. 残余（不写成通过）

1. **首次拖动冷启动**：224ms、连续 6 个目标无新画面（5s GOP + 预览缓存空）；
   T8.6.1b 首次正向 152ms。这是 C 判 partial 的原因。要收口需要会话就绪前预热预览
   解码器/首帧，或由用户明确把“首次拖动”列为 C 例外。
2. **探针字段缺陷**：`response_p95_ms` 恒 0（`responses` 未填充），`landing_frame` 打印 PTS；
   本轮用 `max_response` 与 `landing_*_ok` 替代，不引用这两个字段。
3. **新缓存根首次写盘**：四次新根运行都出现一次 `cdn_done bytes=0 error=写入缓存文件失败`，
   随后重试成功；`DownloadJob`/`CacheStore`/`WinHttpDownloadTransport` 本轮未改，建议另立任务。
4. **未缓存区**只测了“拖入提示/拖回恢复/松手精确”，未做下载追上后的长时间稳定性。
5. **4K/HEVC、1080P60、大会员高码率拖动**未验；4K 只记录。
6. **“视频完整/音频缺失”提示**只有单测 + 代码路径，真实窗口未目视；
   音频可听性未由无人值守探针验证。
7. **黑帧**无埋点，不能写“已证明无黑帧”；本轮无崩溃/设备丢失/表面池事件。
8. **默认路径探针 31/3**：缓存命中/记录读取/截图写入正常，但 `mp4_size_matches`、
   `record_mark_fields`、`snapshot_index_fields` 与 T8.7 遗留默认记录/截图索引不一致；
   不写“默认路径全过”。

---

## 6. 不要做 / 下一步

- 不要把本次写成“完成/丝滑通过”；C 仍差首次拖动冷启动。
- 不要为通过而放宽 100ms/50ms/150ms/70% 门槛，也不要把 `response_p95=0` 当证据。
- 不要改 M1/M2/P4/P6/P7、OB、schema、默认 AVC≤1080p、本地 `PreviewDecoder`/`DragStrategy`。
- 不要提交 `build/` 日志、缓存/记录/截图、`credentials.dat`、`dist/out` 二进制。
- 下一步（需用户拍板）：
  1. 预热预览解码器（会话 ready 前解首帧/当前目标）或明确首次拖动例外；
  2. 修 `OnlineDragPreviewTestRunner` 的 `response_p95`/`landing_frame` 字段；
  3. 单独查新缓存根首写失败（并行 DASH 两路 + 新目录）；
  4. 未缓存区长等待、4K/60 专项另立任务。

---

## 7. 复跑命令（最短）

```powershell
$env:TMP = 'E:\拉片软件开发\.deps\tmp'; $env:TEMP = $env:TMP
$env:PATH = 'E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;' + $env:PATH
$exe = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"

# A
& $exe --online-quality-switch-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --quality-switch-high 112 --quality-switch-back 80 --quality-switch-wait-ms 120000 `
  --online-cache-root build\verify-t8.17\cache --online-pictures build\verify-t8.17\pics `
  --online-records-root build\verify-t8.17\records --settings-path build\verify-t8.17\settings

# B/C（新缓存根；会先删除该根）
& $exe --online-drag-parity-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --drag-seconds 1 --drag-samples 60 --m1-throttle-bps 1048576 `
  --online-cache-root build\verify-parity\cache --online-pictures build\verify-parity\pics `
  --online-records-root build\verify-parity\records --settings-path build\verify-parity\settings

# 本地基线 / 回归
& $exe --local-drag-parity-test --file 'media\测试.mp4'
& $exe --drag-test --drag-samples 20 --file 'media\测试.mp4'
& $exe --drag-strategy-test --file 'media\测试.mp4'
& $exe --drag-far-test --file 'media\测试.mp4'
& $exe --key-test --file 'media\测试.mp4'

# ctest（Qt bin 必须在 PATH）
ctest --test-dir build\release -C Release --output-on-failure
```
