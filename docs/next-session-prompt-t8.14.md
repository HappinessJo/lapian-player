# 下一个会话提示词：T8.14（M2 门）

- **版本**：v1（2026-10-24，T8.13 收口后；基线 commit `01b98d5`）
- **直接复制本文件给新会话。** 上一会话已完成 **T8.13（网络异常/并发加固）**；新会话**只做 T8.14（M2 门）**，不要做 T8.15（大会员条件验收）/T8.16（打包/隐私）/T8.6.1b（拖动预览），也不要把 T8.13 的注入证据重跑一遍当成 M2 门。
- **当前状态**：**M1 已于 T8.8 通过**；T8.9–T8.13 完成（均非 M2 门）；**T8.14 是 M2 门**；当前 `tasks-v2.md` v0.24、`AGENTS.md` v6.36、`ctest` **35/35**、`dist` **34.3MB/25**。
- **T8.13 基线（不得退化）**：`ctest` 35/35；`--online-m2-test` OB-1/2/3 **3/3 34/0**（ready 359.2/357.1/361.8ms、fps 30.000/29.412/25.000、DASH qn=80、音画 p95 17.6/17.3/20.0ms、内存峰值 212.1/204.6/203.5MB）；M1 `--force-durl` **28/0**；quality `--ui` **50/56/50**；index 真实 **27/0**、离线 **22/0**；长按 **54/0**；缓存管理 **72/0 + 98/0**；key **77/0**；P1/P2 **268.67/322.34ms**（p50）、P7 后退 p50/p95 **0.19/0.99ms**、前进 **0.24/0.36ms**；`--online-fault-test` 10/10 场景 92 项、`lapian_online_fault_test` 153 项（含 4 读者 10min）；dist 34.3MB/25。

## 1. 先读

1. [AGENTS.md](../AGENTS.md)（v6.36：当前状态/v2/约束/文档表）
2. [tasks-v2.md](../tasks-v2.md) 的 **T8.14 行**、**§M2 门（硬条件）**、版本记录 v0.24
3. [spec-v1.0-delta.md](spec-v1.0-delta.md) v0.4：**§0.2、O2/O3/O4/O5/O6/O7/O9、OP1–OP8、DEC-2/DEC-3/DEC-5**；重点是 O2-A1/O2-A3、O3-A1、O4-A1…A8、O5-A2/A5/A7、O7-A7/A9、O9-A1…A6
4. [ADR-007](adr/ADR-007-bilibili-online.md)、[T8.8 M1 交接](handoff-2026-10-23-t8.8-m1.md)（**OP1/OP2/OP3/OP5 的 T8.8 测量方法必须沿用**）
5. [T8.10 证据](perf/v2-online-m2.md)（DASH 双输入/音画/seek/内存，含 §M2 数据但**未判 M2**）、[T8.10 交接](handoff-2026-10-24-t8.10.md)
6. [T8.11 证据](perf/v2-online-index.md)、[T8.12 证据](perf/v2-online-quality.md)、[T8.13 证据](perf/v2-online-faults.md) 与 [T8.13 交接](handoff-2026-10-24-t8.13.md)
7. [T8.6 证据](perf/v2-online-session.md)、[T8.7 证据](perf/v2-online-cache-hit.md)、[T8.5 证据](perf/v2-online-m1.md) §11–§17（M1 门方法与失败矩阵）
8. [T7.5 鲁棒性](perf/T7.5-robustness.md)、[T7.7 验收清单](perf/T7.7-acceptance-checklist.md)（本地 O9/P1/P2/P7 口径）、[T7.8 基准](perf/T7.8-bench.md)
9. 代码入口：
   - `src/app/OnlineM2TestRunner.*`（DASH 双输入/60s 音画/20 seek/内存/缓存命中；`--m2-play-seconds/--m2-seeks/--m2-cache-hit-only/--m2-skip-cache-hit/--force-durl`）
   - `src/app/OnlineM1TestRunner.*`（真实窗口 E2E；输出 `online_m1_first_frame`、`online_m1_p7`、`online_m1_uncached*`、`online_m1_cache_hit`、`online_m1_ui_gap`）
   - `src/app/OnlineOfflineTestRunner.*`（缓存命中 0 网络；`--offline-default-paths`）
   - `src/app/OnlineQualityTestRunner.*`、`OnlineIndexTestRunner.*`、`OnlineLongPressTestRunner.*`、`OnlineFaultTestRunner.*`
   - `src/app/OnlineSession.*`、`src/online/{DownloadJob, GrowingSource, CacheStore, PlayUrl, WinHttp*}.cpp`
   - `scripts/bench-regression.ps1`（P1/P2/P7）、`scripts/package.ps1`（dist/导入表/体积）
   - 上一轮新增：`tests/online_fault_test.cpp`（故障矩阵，可引用，不重做）

## 2. 任务边界

**只做 T8.14：M2 门。** 官方验收（tasks-v2 硬条件）：

> 1. 普通用户（非大会员）3/3 `OB` 样本 DASH 1080P30 可播；实际 fps/档位与 S2 一致。
> 2. OP1 起播、OP2 已缓存 P7、OP3 未缓存行为、OP4 已缓存播放停顿、OP5 缓存命中全部按 DEC-2 已拍板值达标。
> 3. DASH 双流 60s 音画偏差 ≤1 帧；20 次 seek 精确；断网/过期/损坏/磁盘满可恢复。
> 4. 本地模式零网络 + P1/P2/P7 + F1–F8 不退化。
> 5. **M2 门没有** 4K/1080P60 验收项；T8.15 是 B 的独立条件验收（非门）；`OB-2` 只证明“降帧提示正确”，不证明 60fps 可用。
> 6. 不把 `durl` 1080p（S2 1/3）写成 1080p 保证；1080p 结论必须来自 DASH。

**验收数字（DEC-2 已拍板）**

| 项 | 口径 | 值 |
|---|---|---|
| OP1 在线起播 | 用户确认→首帧；1080P30；每条 BV **冷缓存 20 次 × 3 条**；下行 ≥1MB/s | **p95 ≤5s**（19/20），记录 p50/速率 |
| OP2 已缓存区单帧步进 | 1080p；±1；每条 20 次；距缓存边界 ≥2s；解码器热 | **p95 ≤80ms** |
| OP3 未缓存区 seek | 提示/取消/落点 | 提示 **≤100ms**“缓冲中（目标帧 N）”；取消 **≤100ms**；落点误差 **0**；不承诺毫秒 |
| OP4 已缓存区播放停顿 | 1080p30 **全缓存、连续 5 分钟** | **>200ms 停顿次数 = 0**（P5 口径：画面无更新且 UI 无响应） |
| OP5 缓存命中打开 | **断网/网络禁用**；同一 BV **20 次** | **p95 ≤2.5s**；0 playurl/0 网络 |
| OP6 本地回归 | — | **P1 ≤1.5s、P2 ≤2.5s、P7 ≤80ms** |
| OP7 内存 | — | 空闲 **≤250MB**；在线 1080p30 播放 **≤800MB** |
| OP8 缓存配额 | DEC-5 | 默认 20GB；300MB 自动 LRU + 20GB 记账；占用 ≤ 配额 |
| O9 本地 | — | 本地打开→播放→步进→标记→截图→关闭 **0 TCP/UDP**；F1–F8 不退化 |

**不做 / 不改**：

- 不新增 4K/60fps 验收；不写“60fps/4K 可用”。
- **不改** M1/M2 门、验收数字、OB 样本、DEC-2/DEC-5、`lapian-cache-1` schema/缓存键/索引格式。
- **不改** T8.13 的加固语义（失败后不 Cancel 读层、4s 看门狗、API 4.5s、缓存损坏自动 Rebuild）、T8.12 档位策略、T8.11 增量索引、T8.10 DASH 双输入、T8.6.1a 异步步进。
- 不把 **注入**（T8.13）写成真实 CDN 可过；不把 **M1 durl** 数字写成 M2 DASH 数字。
- 不做 T8.15/T8.16/T8.6.1b；不顺手加功能。
- 若门不通过：写 **blocked/fail + 具体缺哪一项**，不要“部分通过”“M2 基本通过”。

## 3. 现状与硬事实

### 3.1 已完成能力（可直接复用）

- 3/3 DASH 1080P30 已在 T8.10/T8.12 验证：OB-1/2/3 实际 qn=80 AVC，fps **30.000/29.412/25.000**；OB-2 29.412 触发 DEC-4 横幅+角标（合法，不阻塞 M2）。
- DASH 双输入 60s 音画偏差 p95 17.1/18.2/22.0ms（≤1 帧）、20/20 seek 帧号+PTS 精确、EOF 末帧、缓存命中 45–46ms/0 网络（T8.10）。
- OP1/OP2/OP3/OP5 的 **T8.8 方法**可直接套用（见 `docs/perf/v2-online-m1.md` §14）：OP1 用 `--online-m1-test` 20 次冷缓存、解析 `online_m1_first_frame`；OP2 用完整缓存命中路径 `online_m1_p7`（20 次 ±1）；OP3 用 `online_m1_uncached`/`online_m1_cancel_wait`/`--uncached-landing`；OP5 用 Medium 默认 AppData + 永不成功 HTTP 客户端 20 次。
- T8.13 故障矩阵（断网/超时/403/过期/损坏/磁盘满/DASH 单流失败/4 读者 10min）已覆盖“断网/过期/损坏/磁盘满可恢复”；M2 门可**引用**，不要重跑全部注入场景。
- 本地 P1/P2/P7 有 `scripts/bench-regression.ps1 -Suites startup,step1080`；本地零网络有 T8.6/T8.8 的 Medium 副本 + netstat/`GetExtendedTcpTable` 方法。

### 3.2 已知缺口（T8.14 必须处理或明确 blocked）

1. **OP4 没有专用探针**：现有 `--online-m2-test` 不统计“全缓存连续 5 分钟 >200ms 停顿”。可选最小改动：给 `OnlineM2TestRunner` 增加 OP4 模式（`--m2-op4` 或在 `--m2-cache-hit-only` 下播放 300s + UI 心跳/帧更新计数），或在 `--online-longpress-test`/新 `--online-op4-test` 里做；**只加探针，不改产品语义/门/数字**。不得跳过 OP4 写“按设计应通过”。
2. **OP1 的 DASH 属性**：`--online-m1-test` 不打印 `dash=1`。要么在 OP1 前用 `--online-m2-test` 证明 3/3 DASH 1080P30（硬条件 #1），要么给 M1 探针补一行 `dash`/`dash_fps`（探针输出可以加，不改产品行为）；OP1 数字必须来自 DASH 路径。
3. **真实休眠**（T8.13 未验证）、**真实 CDN 故障**（只做正常路径 + T8.5/T8.8 历史 403/刷新证据）、**窗口级 DASH 单流失败**（只有离线注入 T12）仍不是 M2 门必需项，但报告要列“未验证”，不得写成通过。
4. **T8.13 开发期 1 次崩溃 + 1 次卡住未复现**（20+ 次全量运行无复现）：M2 门报告要保留这条残余风险；若 M2 期间复现，先停下定位，不要带病判门。
5. **seek 侧首条文案竞态**（断网时可能先“到达文件末尾”，随后被网络错误覆盖）：M2 只按 OP3 的提示/取消/落点判定，不把该文案当失败。
6. **只读 ACL 75/0** 是 T8.3.1 历史证据；M2 可复用，若要重跑用 `scripts/cache-manage-readonly.ps1`。

### 3.3 环境/执行坑

- 工作区 `dist\lapian.exe` / `build\release\bin\lapian.exe` 是 **Low integrity**：默认 `%LOCALAPPDATA%`/`%APPDATA%` 写不进去。**OP5/本地 O9/默认 AppData 证据必须用 Medium 副本**（`%LOCALAPPDATA%\Programs\拉片软件` 安装版或整目录复制）；测试根用 `build\t8.14-*`。
- 构建前 `$env:TMP/$env:TEMP = <repo>\.deps\tmp`；重配用 `scripts\configure.ps1`（直接 cmake 重配可能 0xC0000409）。
- GUI 探针用 `Start-Process -RedirectStandardOutput/-RedirectStandardError`，不要 `2>&1 |` 配 `$ErrorActionPreference=Stop`。
- 真实在线凭据：`.secrets\bilibili_sessdata.txt`；日志不得打印 SESSDATA/签名 URL；不提交缓存/截图/记录。
- **DASH 默认开启**；`--force-durl` 只用于 M1 回归，不要用于 M2 门数字。
- 每个 tag 独立缓存根；OP1 每次先删该条缓存根；OP2/OP5 需先跑一次完整缓存（或复用已完整缓存），并确认 cache-hit 语义。
- 单次 `--online-m2-test` 约 3–4 分钟（60s 播放 + 20 seek）；OP1 60 次 × `--online-m1-test` 约 10–15 分钟，串行执行，避免并发抢带宽/UI。

## 4. 建议执行路线（按门顺序，先硬条件后指标）

### 阶段 0：确认基线

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure   # 必须 35/35
```

### 阶段 1：硬条件 #1 —— 3/3 DASH 1080P30

三条各跑一次 `--online-m2-test`（独立根，冷缓存），确认 `dash=1`、`qn=80`、fps 30.000/29.412/25.000、音画 p95 ≤1 帧、20/20 seek 精确、EOF、内存 ≤800MB：

```powershell
build\release\bin\lapian.exe --online-m2-test --bvid BV1BMnv6jE5S --cid 42915922022 --tag m2-ob1 `
  --timeout-ms 120000 --online-cache-root build\t8.14-m2\ob1-cache `
  --online-pictures build\t8.14-m2\ob1-pics --online-records-root build\t8.14-m2\ob1-rec `
  --settings-path build\t8.14-m2\ob1-settings
# OB-2: BV1mA411m7pS / 1004967230；OB-3: BV1GJ411x7h7 / 137649199
```

解析 `online_m2_first`（ready_ms/qn/fps/dash）、`online_m2_av_drift`、`online_m2_seek`、`online_m2_memory`、`online_m2_result`。**durl 1080p 不能替代**。

### 阶段 2：OP1 起播（3 条 × 冷缓存 20 次）

- 每条 20 次：删除该条缓存根 → `--online-m1-test`（DASH 默认）→ 解析 `online_m1_first_frame ms=`，同时记录 `online_m1_quality qn=80`。
- 建议 `LAPIAN_ONLINE_LOG=1` 仅用于诊断；OP1 计时从 ClickConfirm 到 `online_ready`，与 T8.8 同口径。
- 记录 min/p50/p95/max、≤5s 次数（要求 19/20），并采样 1MB Range 下行（playurl+CDN）证明 ≥1MB/s；写 CSV（`build\t8.14-m2\op1-summary.csv`，不入库）。
- 若 60 次耗时过长：允许 3 条各 20 次串行，中途休息；**不得减少到 10 次**。

### 阶段 3：OP2 已缓存区单帧步进（3 条 × 20 次 ±1）

- 先让每条完整缓存（同一缓存根跑一次 `--online-m2-test` 或让下载完成并 finalize；确认 `cache_hit` 路径）。
- 再跑 `--online-m1-test`（缓存命中）→ 解析 `online_m1_p7 n=20 ... p50/p95/max`；要求 p95 ≤80ms、严格 ±1、距缓存边界 ≥2s（1080P30 完整缓存天然满足）。
- DASH 1080P30 是 M2 的 OP2 口径；**不要用 M1 的 720P 数字顶替**（可并列展示）。

### 阶段 4：OP3 未缓存 seek（提示/取消/落点）

- 用增长中会话（冷缓存）跑 `--online-m1-test`：解析 `online_m1_uncached target=... ui_ms=`（≤100ms）、`online_m1_cancel_wait`（≤100ms）、取消后回已缓存目标。
- 落点用一次 `--online-m1-test --uncached-landing`（慢，允许整片下完；只验落点误差 0）。
- 也可用 `--online-longpress-test`（54/0：未缓存提示 0ms、取消 4–17ms、回缓存 31–51ms）作为补充，但 OP3 主数字用 M1 路径。

### 阶段 5：OP4 已缓存播放 5 分钟（当前缺探针，必须补）

- 目标：1080P30 **全缓存**、连续播放 **300s**，`>200ms` 画面/UI 停顿次数 = 0。
- 最小实现建议（只加探针）：
  - 在 `OnlineM2TestRunner` 加 `--m2-op4`（或 `--m2-cache-hit-only --m2-play-seconds 300 --m2-op4`）：缓存命中打开后播放 300s，用 16ms 心跳 + 帧号/PTS 变化计数；记录 `ui_gap_max`、`frame_stall_count(>200ms)`、`underruns`、内存；
  - 或新增 `--online-op4-test`，复用 `OnlineM2TestRunner` 的窗口/缓存/心跳代码；
  - **不改**播放器/缓存/门；新增探针后 `ctest` 仍 35/35（不需要新增 ctest）。
- 结果写 `op4_ui_gap_max_ms`、`op4_stalls_gt_200ms=0`、`op4_frames_advanced`、`op4_underruns`、`op4_memory_mb`。

### 阶段 6：OP5 缓存命中（断网模型，20 次）

- Medium 副本 + 默认 AppData（或显式默认根）+ 完整缓存条目（1080P30 DASH 视频+音频双条目）。
- 每次注入**永不成功的 HTTP 客户端**（或物理断网/防火墙），打开同 BV；解析离线探针的首帧与 `playurl=0/http=0`（`--online-offline-test --offline-default-paths` 或等价探针）。
- 要求 20/20、p95 ≤2.5s、0 playurl、0 CDN 请求；记录 min/p50/p95/max。
- **不能**用工作区 Low integrity 的默认 AppData 结果；写清副本路径与 integrity=medium。

### 阶段 7：失败矩阵/续传/缓存配额（引用 + 必要复跑）

- 引用 T8.13：断网/看门狗/403/过期/损坏/磁盘满/DASH 单流失败/4 读者 10min；引用 T8.1/T8.2/T8.5 的真实 -400/-404/62002/-101/412/超时/真实 403/刷新/杀进程续传。
- M2 门只要求“可恢复”，不要求重跑全部注入；报告里分“本轮复跑”与“引用历史证据”两列，避免把引用写成新跑。
- 缓存配额 DEC-5：`lapian_online_cache_test`（ctest 内 396/0）与 `--online-cache-test` 32/0；300MB LRU + 20GB 记账引用 T8.3/T8.8 证据即可；如需新鲜数字，跑 `--online-cache-manage-test`（72/0 + 98/0）。

### 阶段 8：本地 O9 + P1/P2/P7 + F1–F8

- 零网络：Medium 副本，本地打开→播放→步进→标记→截图→关闭；用 `netstat`/`GetExtendedTcpTable` 证明 0 TCP/UDP（方法见 T8.6/T8.8/T8.7 证据）；同时确认本地不写在线目录（O9-A6）。
- P1/P2/P7：`scripts/bench-regression.ps1 -Suites startup,step1080`（P1/P2 热缓存口径，不写 F1-A1/正式 T7.7）；记录 p50/p95 与基线漂移。
- F1–F8 回归：至少 `--key-test --file build\test-media\faststart-60s.mp4`（77/0）、`--settings-test`、`--banner-test`、`--dpi-test`、`--snapshot-test` 中可执行项；若时间不足，明确写“未重跑”并引用 T8.12/T8.13 基线，不能写“全过”。
- 内存 OP7：空闲 ≤250MB（`--startup-test`/`--memory-test` 的空闲采样或进程 WS）；在线 1080p30 ≤800MB（阶段 1 的 `online_m2_memory`）。
- 网络审计：`dist` 导入表无 QtNetwork/TLS/winhttp/wininet/urlmon（`scripts/check-imports.ps1` 或 package 报告）；在线只访问 api.bilibili.com + CDN；Cookie 只发 api.bilibili.com；日志无 SESSDATA/签名 URL。

### 阶段 9：报告/交接/版本

- `docs/perf/v2-online-m2.md` 升 **v2.0**：保留 T8.10 的 DASH 双输入证据（历史章节），新增 **M2 门章节**：硬条件逐条、OP1–OP8 表、失败矩阵（本轮/引用分列）、本地 O9、P1/P2/P7、内存、网络审计、通过/blocked 判定、缺口。
- 新建 `docs/handoff-2026-10-24-t8.14-m2.md`：结论、数字、证据路径、缺口、下一任务（T8.15/T8.16/T8.6.1b 的先后）。
- `tasks-v2.md` 升 **v0.25**：T8.14 行写结果/证据/判定；版本记录加一条；`AGENTS.md` 升 **v6.37**（M2 门结论、数字、下一任务）。
- 一个可审查 commit；`git status` clean；**不把 M2 写成“通过”除非 6 条硬条件全部满足**。

## 5. 停止条件（先停下记录/问用户，不要偷偷扩任务）

- 3/3 中有任一条拿不到 DASH 1080P30（qn<80/回落 durl/实际 fps 与 S2 不一致）→ 先记录，判 **blocked**，不要拿 M1 durl 数字补。
- OP1/OP2/OP5 p95 超门槛，或 OP4 出现 >200ms 停顿，且不是环境/带宽原因 → 判 fail/blocked，不调门/不删样本。
- 需要改 M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema/缓存键/索引格式 → 停下问。
- 需要改 T8.13 加固/T8.12 档位/T8.11 索引/T8.10 DASH/T8.6.1a 步进语义才能过门 → 停下问。
- 真实网络/账号不具备（SESSDATA 失效、风控、网络不可达）→ 写“未验证/blocked”，不伪造。
- 要求把 4K/1080P60 写进 M2 门，或把 T8.15 混入 → 拒绝并指出 tasks-v2 §M2 门第 5 条。

## 6. 现成接口/探针速查

| 用途 | 入口/输出 |
|---|---|
| DASH 3/3 + 60s 音画 + 20 seek + 内存 | `--online-m2-test`（`online_m2_first/av_drift/seek/memory/result`；`--force-durl` 走 M1） |
| 真实窗口 E2E/OP1/OP2/OP3 | `--online-m1-test`（`online_m1_first_frame/p7/uncached*/cache_hit/result`；`--uncached-landing` 只验落点） |
| 长按（已缓存/未缓存/边界） | `--online-longpress-test`（54 项/样本；`online_longpress_ui_gap/close`） |
| 档位菜单/实际 fps | `--online-quality-test --ui`（50/56/50；`online_quality_ui_*`） |
| 渐进索引/波形/总帧数 | `--online-index-test`（真实 27/0、离线 22/0） |
| 离线/缓存命中 0 网络 | `--online-offline-test`（`--offline-default-paths`；`offline_cache_hit`） |
| 故障注入矩阵/看门狗/磁盘满/损坏 | `--online-fault-test`（10 场景 92 项）；`lapian_online_fault_test`（153 项，`--readers-minutes 10`） |
| 本地 P1/P2/P7 | `scripts/bench-regression.ps1 -Suites startup,step1080` |
| dist/导入表/体积 | `scripts/package.ps1`（34.3MB/25、无 QtNetwork/TLS/winhttp） |
| P1/P2/P7 基线 | P1/P2 p50 268.67/322.34ms；P7 后退 0.19/0.99ms、前进 0.24/0.36ms |

## 7. 完成时的报告（按此清单写）

1. **M2 判定**：pass / blocked / fail（六条硬条件逐条）；**不写“基本通过”**。
2. 3/3 样本：bvid/cid、qn/codecid/fps、DASH 证据、ready_ms。
3. OP1–OP8 表：n、p50/p95/max、门槛、pass/fail；OP4 的 5min 停顿/UI gap/内存。
4. 失败矩阵/续传/刷新/备份/损坏/磁盘满：本轮 vs 引用，分开列。
5. 本地 O9：0 TCP/UDP 证据、P1/P2/P7、F1–F8 哪些跑/哪些引用、内存空闲/在线。
6. 网络审计/脱敏：导入表、Cookie 边界、日志扫描。
7. 回归数字与缺口；未验证项（真实休眠、真实 CDN 故障、窗口级 DASH 单流、T8.13 未复现崩溃/卡住）。
8. 下一步建议：T8.15（大会员条件验收，非门）/T8.16（打包/隐私）/T8.6.1b（拖动预览）。
