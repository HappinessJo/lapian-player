# 交接：P0 发布收口与稳定性（2026-10-25）

- **状态**：**已完成（2026-10-25）**。T2–T5 串行执行完毕；代码 `925b8d5` + `08484ee`，
  文档提交见 `git log -1 -- docs/handoff-2026-10-25-p0-close.md`。结果见 §0。
- **代码基线**：`d7f62d6` 起；收口后代码 HEAD `08484ee`（P0 代码 `3084e33` + `c912770` + `35fa8be`；P0 文档 `24fe056`）。
- **P0 结论**：A/B/C pass（P0 范围）；在线 parity **61/0**、cold first_publish **26–28ms**、
  post-seek 后第一次拖 delivered **48ms**、稳态 **58.5fps**、cache **60.75MiB/72**；
  `ctest` 37/37、key 77/0、T8.6.1b 35/0、A 42/0、本地 9/0。
- **用户反馈**：2026-10-25 18:33 实机使用后“现在的流畅度就做的很好了”——**体感确认，非量化**；
  不覆盖 4K/60、`post_seek_immediate` 残余。
- **任务/证据**：[tasks-online-parity-fix.md](tasks-online-parity-fix.md) §9、
  [online-drag-parity.md](perf/online-drag-parity.md) §13、
  [handoff-2026-10-25-p0.md](handoff-2026-10-25-p0.md)。

---

## 0. 执行结果（2026-10-25）

| 任务 | 结果 | 关键证据 |
|---|---|---|
| T1 用户确认核对 | 已核对 | task §9.8 / handoff p0 §3.3 均为“体感确认、非量化”，未改写为验收 |
| T2 新根首包写入失败 | **已定位并修复** `925b8d5` | 3 新根修复前各 2 条 `bytes=0 ... 写入缓存文件失败`（在 `online_dash_keys` 前），修复后各 0 条；根因是 DASH 总长探测 `FirstChunkSink` 返回 false，不是写盘；`headers_only` 只读响应头；parity 61/0 |
| T3 preview decoder 偶发崩溃 | **已复现并修复** `08484ee` | 修复前隔离 2/20 失败（0xC0000374、0xC0000005）、全量 ctest 首跑失败（avutil）；根因 `CloneExactLocked` 两处无锁调用；修复后隔离 **50/50**、全量 ctest **3×37/37** |
| T4 clean 打包/安装 | **pass** | 242s、ctest 37/37、dist 25 文件/36,392,776B/34.71MB、exe `8344A2BB…`、setup 11.9MB、zip 14.49MB；静默安装 2s、hash/快捷方式一致、settings 未动 |
| T5 回归/文档 | **pass（P0 范围）** | parity **61/0**、T8.6.1b **35/0**、A **42/0**、key **77/0**、local **9/0**、drag P6 18.0ms、strategy **7/7**；文档更新 task §9.9 / perf §13.8 / AGENTS v6.50 |

- **产物**：`out\package\Lapian-0.1.0-setup.exe`（SHA256 `C6F3B075…`）、
  `out\package\Lapian-0.1.0-portable.zip`（SHA256 `8420FEA5…`）；
  安装版 `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe` 与 dist 同 hash。
- **环境备注**：本机直接执行 `.ps1` 被 ExecutionPolicy 拦，改用
  `powershell -ExecutionPolicy Bypass -File`；setup 从 `%TEMP%` 静默安装，
  未被 SmartScreen 拦截；未清空/覆盖默认缓存与在线记录。
- **残余（不得写成全量验收/全绿）**：preview 崩溃无符号化 dump（按契约+压测收口，
  低概率残余未知）；4K/60 与 `post_seek_immediate` 未覆盖；用户体感非量化。
  详见 [tasks-online-parity-fix.md](tasks-online-parity-fix.md) §9.9.5、
  [online-drag-parity.md](perf/online-drag-parity.md) §13.8。

---

## 1. 本轮目标（只做收口，不重开 P0）

1. 记录并固化用户实机确认（本会话已写入 task §9.8 / handoff §3.3）。
2. 查清并修复“新缓存根首包写入失败”（有把握就修；没把握就写清根因/证据）。
3. 查 `lapian_preview_decoder_test` 偶发 `0xc0000374`（能复现就定位；不能复现就如实记录）。
4. 重新打 setup/zip、安装/覆盖 Medium 副本并复验。
5. 全量回归 + 文档/AGENTS 更新 + 提交。

**非目标**：不改 100ms/50ms/150ms/70% 门、不改 `lapian-cache-1` schema、不改本地
`PreviewDecoder`/`DragStrategy` 语义、不重开 M1/M2/P4/P6/P7、不上硬解、不做
`post_seek_immediate` 修复、不做 4K/60 专项、不做 UI 重构。UI 重构另开设计会话。

## 2. 任务顺序（串行，一项一提交）

### T1（本会话已完成，新会话核对）
- task §9.8、handoff §3.3、本文件已写入用户确认；不要把它写成“量化验收通过”。

### T2 新缓存根首包写入失败

**现象**：新缓存根第一次运行，视频/音频首包各一次：
`T8.5 cdn_done ... status=206 ... bytes=0 ... error=写入缓存文件失败`，随后重试成功；
不影响最终数据，但属首次打开 UX 瑕疵。证据日志：
`build\p0-before.log`、`p0-diag.log`、`p0-after-s3.log`、`p0-after-s3b.log`
（搜 `写入缓存文件失败`）。

**复现**：用全新根跑一次在线 parity（命令见 §4），1MiB/s；看视频 `30080` / 音频 `30280`
首包。三个全新根各跑一次，统计是否会复现。注意：同一根第二次通常不再出现。

**排查范围**：`src/online/DownloadJob.cpp`、`src/online/CacheStore.cpp`、
`src/online/WinHttpDownloadTransport.cpp`，以及 `.part` 创建/写句柄/`Publish` 顺序；
DASH 视频+音频并行写同一新目录是重点。

**验收**：
- 至少 3 个全新缓存根不再出现 `bytes=0 ... 写入缓存文件失败`；或
- 若非产品 bug（外部/杀软/文件系统），给出可复现证据与用户可接受的结论；
- 不改变缓存键/schema/配额语义；`ctest` 37/37。
- 提交：`fix: avoid first-write failure on a new online cache root`（或 `docs:` 若只定位）。

### T3 `lapian_preview_decoder_test` 偶发 0xc0000374

**已知**：S2/S3 各一次全量 ctest 中该测试以 `0xc0000374` 退出；隔离复跑/后续全量通过；
S4 clean 构建未出现。该测试是本地 `PreviewDecoder`，P0 无 diff。

**复现**：
```powershell
ctest --test-dir build\release -C Release -R lapian_preview_decoder_test `
  --repeat until-fail:50 --output-on-failure
# 再跑全量 3 次
1..3 | ForEach-Object { ctest --test-dir build\release -C Release --output-on-failure }
```
若崩溃：抓 WER/Application 日志或 dump，确认是 teardown/线程/AVFrame 释放还是测试进程
退出路径；只有隔离验证通过才修。**不能复现就写“未复现的残余”，不要写“已修复/无崩溃”。**

**验收**：50 次隔离 + 3 次全量 ctest 0 崩溃；或明确 blocked + 证据。
提交：`fix: stabilize local preview decoder teardown`（仅在实际修复时）。

### T4 打包 / 安装 / Medium 复验

```powershell
scripts\build-release.ps1 -Clean -Package
```
- 记录 clean 构建耗时、`ctest` 37/37、dist 文件数/字节/MB、exe SHA256。
- `out\package` 生成 `Lapian-<ver>-setup.exe`、`Lapian-<ver>-portable.zip`；记录大小/hash。
- 安装 setup（每用户，保留 settings/marks/sidecar）或把 `dist\*` 覆盖到
  `%LOCALAPPDATA%\Programs\拉片软件`；核对 Start 菜单/桌面快捷方式 target 与新 exe hash。
- 注意：E: 工作区可能触发 SmartScreen/“Cannot open E:/” warning；安装失败要如实记录。
- 启动安装版，打开 `media\测试.mp4` 与在线样本各一次，确认不是旧二进制。

**验收**：安装版 exe hash 与 dist 一致；P0 parity 61/0、T8.6.1b 35/0、key 77/0、
A 42/0、本地 9/0；缓存 ≤64MiB；无新增错误横幅。

### T5 回归 / 文档 / 提交

- 全量：`ctest` 37/37；P0 parity 61/0；T8.6.1b 35/0；key 77/0；A 42/0；local 9/0；
  `--drag-test` P6 ≤150ms；`--drag-strategy-test` 如仍 6/7，如实记录（既有 flake）。
- 更新：task §9（收口结果）、perf §13（首写/崩溃结论）、handoff p0、本文件、AGENTS v6.50。
- 提交：每个代码修复一个 commit；文档单独 `docs: close P0 release`。
- 不提交 `build/`、`out/`、`dist/` 二进制、缓存/记录/截图、凭据。

## 3. 约束（必须遵守）

- 在线测试只用 **Medium 副本**：`%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`；
  不要用 `build\release\bin` / `dist` 里的 exe 跑在线（Low integrity 无法写 AppData）。
- 测试缓存/设置放 `build\...` 新根，不要清空/覆盖用户默认缓存与在线记录。
- 不打印/提交 SESSDATA、Cookie、签名 URL；不把 `0xc0000374` 写成“无崩溃”。
- ctest 用 `TEMP/TMP=E:\拉片软件开发\.deps\tmp`，Qt bin 在 PATH。
- 不改门/阈值/schema/本地预览/在线预览语义；有歧义先问用户，不静默放宽。
- 首写失败若只是外部原因，先记录根因和用户影响，不要为“全绿”伪造修复。

## 4. 常用环境与命令

```powershell
cd 'E:\拉片软件开发'
$env:PATH = "$PWD\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$env:TEMP = "$PWD\.deps\tmp"; $env:TMP = $env:TEMP
$exe = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"

# P0 parity（新根、1MiB/s）
& $exe --online-drag-parity-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --tag p0-close --timeout-ms 90000 --drag-seconds 1 --drag-samples 60 `
  --m1-throttle-bps 1048576 `
  --online-cache-root build\p0-close\cache --online-pictures build\p0-close\pics `
  --online-records-root build\p0-close\records --settings-path build\p0-close\settings

# T8.6.1b
& $exe --online-drag-preview-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --drag-seconds 1 --drag-samples 60 --m1-throttle-bps 120000 `
  --online-cache-root build\p0-close\t861b-cache --online-pictures build\p0-close\t861b-pics `
  --online-records-root build\p0-close\t861b-records --settings-path build\p0-close\t861b-settings

& $exe --key-test --file 'media\测试.mp4'
& $exe --local-drag-parity-test --file 'media\测试.mp4'
ctest --test-dir build\release -C Release --output-on-failure
```

- 日志：`build\p0-close-*.log`、`build\p0-verify-*.log`；`build/` 不入库。
- 当前 exe：3,182,592B，SHA256 `D2CDEBDF637F055B1550417DEF5A0FBA17246C9AFFFBBF0ACD5508A6661ACEDB`。
- dist：25 文件 / 36,392,264B / 34.71MB（P0 时）。

## 5. 新对话提示词（直接复制）

```text
你是拉片软件 P0 发布收口的执行 agent。先读：
- AGENTS.md（重点 §1 在线拖动 P0、§9 已知坑、§11 协作规则）
- docs/handoff-2026-10-25-p0-close.md（本任务单）
- docs/tasks-online-parity-fix.md §9
- docs/perf/online-drag-parity.md §13
- docs/handoff-2026-10-25-p0.md

基线：代码 HEAD 24fe056，P0 已 pass（parity 61/0、cold first_publish 26–28ms、
post-seek delivered 48ms、稳态 58.5fps；ctest 37/37、key 77/0、T8.6.1b 35/0）。
用户已实机体感确认“流畅度很好”，但这是非量化确认，不覆盖 4K/60 与
post_seek_immediate 残余。

本轮只做 P0 发布收口，不重开 P0 门禁，不改本地预览/门/阈值/schema，
不上硬解，不做 4K/60 与 UI 重构。按 handoff 的 T2→T3→T4→T5 串行执行；
每项先复现/取证，再改代码，一项一提交；没把握就记录 blocked，不要伪造“全绿”。

重点：
1) 新缓存根首包 bytes=0 写入缓存文件失败：3 个全新根复现/修复，
   范围 DownloadJob/CacheStore/WinHttpDownloadTransport，不改缓存 schema；
2) lapian_preview_decoder_test 偶发 0xc0000374：50 次隔离 + 3 次全量 ctest，
   能复现再修，不能复现写“未复现残余”，不写“无崩溃”；
3) scripts\build-release.ps1 -Clean -Package，安装 setup/覆盖 Medium 副本，
   核对 Start 菜单/桌面快捷方式与 exe hash；
4) 回归：ctest 37/37、P0 parity 61/0、T8.6.1b 35/0、key 77/0、A 42/0、local 9/0；
5) 更新 task/perf/handoff/AGENTS v6.50 并提交文档。

在线测试只用 %LOCALAPPDATA%\Programs\拉片软件\lapian.exe（Medium 副本），
测试根放 build\，不要清空默认缓存/记录，不打印 SESSDATA/Cookie/签名 URL。
```
