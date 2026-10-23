# 下一个 agent 提示词：T8.8 M1 门（可直接复制）

你是拉片软件 v2 的下一个实现 agent。当前 HEAD 是 `5cd9fc2`（T8.7 缓存命中/在线记录完成），工作区应 clean。**本次只做 T8.8（M1 门审计/报告，必要时做最小取流修正），不做 M2、DASH、清晰度菜单、T8.6.1a/b、T8.3.1、扫码。**

## 0. 先读（不要凭记忆）

按顺序读：

1. `AGENTS.md`（v6.22，协作规则/硬约束）
2. `tasks-v2.md`（v0.14：T8.8 与 M1 门、T8.9–T8.16 不越界）
3. `docs/spec-v1.0-delta.md`（O1/O2/O5/O7/O9、OP1–OP5、OB 片单、DEC-2/DEC-5）
4. `docs/perf/v2-online-spike.md` **§2.2 登录档表**
5. `docs/perf/v2-online-m1.md`（T8.5 的 blocked 证据）
6. `docs/perf/v2-online-session.md`（T8.6 的 3 条 E2E / OP 数字）
7. `docs/perf/v2-online-cache-hit.md`（T8.7 缓存命中/记录）
8. `docs/handoff-2026-10-23-t8.7.md` 与 `docs/handoff-2026-10-23-t8.8-start.md`

## 1. 本次目标

**T8.8 = M1 门：全量验收 + 报告 + 交接，决定是否进 M2。**

当前公开结论是 **M1 blocked（OB-2 360P）**，但本次必须先复核一个参数线索，不能直接抄结论：

- S2 登录档表记录：`durl qn=120 + fourk=1` 时 S-1=64、**S-2=80（1080p）**、S-3=64，即登录后 3/3 ≥720p。
- T8.5 产品实际请求是 `qn=64`，`BuildPlayUrlUrl` 写死 `fourk=0`；`--online-fetch-test --qn-1080` 只映射 qn=80。T8.5 只测了 qn=64/80/112 + fourk=0，没有复现 S2 的 qn=120 + fourk=1。
- 所以 OB-2 的“360P”可能是**请求参数/`fourk` 差异**，不一定是资源限制。先复现矩阵，再决定是修取流还是写 blocked。

**不要**在没有矩阵证据时直接写“M1 blocked”，也不要因为 S2 的 qn=120 给过 1080p 就直接写“M1 通过”。

## 2. 第一件事：durl 参数矩阵（改产品前）

用同一份当前 SESSDATA、同一 cid，记录每次：

`request_qn`、`fourk`、服务端 `quality`、`format`、`size`、`durl_count`、`codecid`、实际宽高/帧率（可 ffprobe）。

至少对 **OB-2**（`BV1mA411m7pS` / cid `1004967230`）做：

| 请求 | fourk |
|---|---|
| qn=64 | 0 |
| qn=64 | 1 |
| qn=80 | 1 |
| qn=112 | 1 |
| qn=120 | 1 |

OB-1（`BV1BMnv6jE5S`/42915922022）和 OB-3（`BV1GJ411x7h7`/137649199）至少做 qn=64/80/120 + fourk=1 对照。

实现方式二选一：

- 在现有 `--online-fetch-test` 加**仅测试用**的 `--qn`/`--fourk` 参数（推荐；复用产品 `FetchPlayUrl`/WinHTTP，不引入 curl 依赖）；或
- 写一个最小探针 runner，仍然走 `BuildPlayUrlUrl`/`OnlineHttpClient`。

不要打印、复制、提交 SESSDATA/Cookie/签名 URL；凭据只从 `.secrets\bilibili_sessdata.txt` 读取，使用 `--sessdata-file`。日志只能出现布尔/计数。

### 判断

- **若 qn=80+fourk=1 或 qn=120+fourk=1 给 OB-2 ≥64**：这是可修的请求参数问题，不是外部 blocked。做最小产品改动，让 M1 durl 请求拿到“可用最高”，然后重跑 3/3。
- **若所有组合仍为 16**：确认是资源/账号限制，写 M1 blocked，停止，不进 M2。
- **若只有 qn=120 给 80**：记录实际返回；产品层要加 M1 不超过 1080p 的保护，避免大会员账号意外触发 4K 下载；不得把 4K 写进 M1。

## 3. 可能的最小代码改动

先定位：

- `src/online/PlayUrl.cpp` `BuildPlayUrlUrl`：当前 `fnval=0`、`fourk=0` 写死。
- `src/online/PlayUrl.h/.cpp` `FetchPlayUrl(..., int qn, ...)`：qn 已是参数。
- `src/app/OnlineSession.cpp` `StartPlayUrlWorker`：当前固定 `64`。
- `src/app/OnlineFetchTestRunner.cpp`：`--qn-1080` 只映射 80；可加 probe 参数。

建议：

1. 给 `BuildPlayUrlUrl`/`FetchPlayUrl` 增加显式 `fourk`（默认保持旧行为），不要静默改变所有调用方。
2. `OnlineSession` 请求 M1 可用最高档（根据矩阵选 qn=80/120；优先能拿 80 的最小请求）。
3. 若实际返回 qn>80，M1 层回退/限制到 ≤1080p；M1 不验收 4K/1080P60。
4. 改完 `lapian_online_fetch_test` 与 `lapian_online_m1_test` 要能覆盖新参数；默认行为保持兼容。
5. **不要**顺手做 DASH、清晰度选择、`quality` 菜单、4K；那是 T8.9+。

## 4. M1 门必须验什么

按 `tasks-v2.md` M1 门与 `docs/spec-v1.0-delta.md` OP 表：

### 4.1 3/3 durl ≥720P

- 登录普通用户 SESSDATA；3 条 OB 全部实际 `quality >= 64`。
- 每条的下载产物可播、ffprobe 通过；记录 qn/format/size/codecid/fps/实际分辨率。
- 结论写成“3/3 ≥720P”，不要写“3/3 720P”如果 OB-2 实际是 1080p；也不要写 1080P 保证。

### 4.2 OP1–OP3、OP5

- OP1：每条 BV 清该条缓存，20 次确认→首帧，p95 ≤5s，19/20；M1 用实际 durl 档（≥720P；OB-2 若 1080p 记录），**1080P30 的 OP1 口径留 M2**；记录 p50/下行速率。
- OP2：已缓存实际 durl 档（M1 ≥720P；1080p 口径留 M2），±1，每条 20 次，距缓存边界 ≥2s，p95 ≤80ms。
- OP3：未缓存 seek 显示“缓冲中（目标帧 N）”≤100ms；取消 ≤100ms；落点误差 0。
- OP5：断网/禁用网络，同一 BV 20 次缓存命中，p95 ≤2.5s；0 请求。复用 T8.7 `CacheHit`/`--online-offline-test`。

### 4.3 失败矩阵（O1/O7）

至少：格式不支持、BV 不存在、不可见/私密、未登录、断网/超时、磁盘满、缓存损坏。5s 内可读错误、不崩溃；已缓存区可播；记录/缓存不被破坏。

### 4.4 恢复与配额

- 杀进程后续传（Range）、无重复/损坏字节；清缓存保留记录/截图；重建记录回访（T8.7 已验，可复跑关键项）。
- DEC-5：300MB LRU + 20GB 记账单测按已拍板口径引用；不真填 20GB。
- 不在本任务做 T8.3.1 缓存管理 UI。

### 4.5 本地 O9

- 零网络：0 TCP/UDP，无 `winhttp/wininet/urlmon/Qt6Network`。
- P1 ≤1.5s、P2 ≤2.5s、P7 本地 ≤80ms；`ctest` 全绿；关键本地 E2E/`--robust-test` 不退化。
- 记录 dist/P12、内存。

### 4.6 缺陷/ADR

- 无 P0/P1；已知 P2/P3 写交接。
- ADR-007 的“附条件接受”记录保留，不改 D1–D5。

## 5. 环境/安全硬约束

- 工作区可能是 Low integrity，**不能写 Medium AppData**。默认路径验收只用 Medium 副本 `%LOCALAPPDATA%\Programs\lapian-t86\lapian.exe`；不要用 `dist\lapian.exe` 冒充默认路径，不要用管理员绕过。
- 测试根用 `build/t8.8-*`；默认路径测试若写用户在线记录，先备份、结束恢复（参考 T8.7 runner 的 `.t87-backup`）。
- `.secrets/`、缓存、截图、在线记录、build 产物不提交；`git status` 必须 clean。
- 不打印/提交 `SESSDATA`、Cookie、签名 URL；日志脱敏。
- 不改 `spec.md` v0.3、`tasks.md`、M1/M2 门或数字；要改门先问用户并走版本记录。

## 6. 判定与停止条件

- **3/3 ≥720P 且 OP1–OP5、失败矩阵、O9 全达标**：写 M1 pass 报告；**先让用户确认是否进 M2**，不要自行开始 T8.9。
- **仍 2/3**：写 M1 blocked 报告并停止；不进 M2；给用户三个选项：
  1. 换同规格 60fps OB 样本（OB-2 只保留降帧证据，不计 3/3 720p）；
  2. 用户明确接受 durl 2/3 并修改 M1 门（版本记录，不能静默）；
  3. 用户明确同意跳过 M1、直接 M2 DASH（高风险，agent 不自行决定）。
- **无凭据/网络/样本**：写 blocked/未验证，不伪造数字。
- 任何需要改门/数字/样本集的决定：先问用户。

## 7. 交付物

1. 必要的最小代码修正 + 单测/探针（仅 M1 门所需）。
2. `docs/perf/v2-online-m1.md` 的 T8.8 门报告（保留 T8.5 历史 blocked 证据，新增复核/最终判定；不要覆盖历史）。
3. `docs/handoff-2026-10-23-t8.8-m1.md`（pass 或 blocked 都写：结论、数字、缺口、下一步）。
4. `tasks-v2.md` 更新 T8.8 状态；blocked 时明确“未进 M2”；不改门/数字。
5. `AGENTS.md` 当前状态/证据/交接指针；**只有 3/3 + OP 全达标才写 M1 通过**。
6. 一个可审查 commit，`git status` clean。

## 8. 建议命令（按实际环境调整）

```powershell
$env:TMP = (Join-Path (Get-Location) 'build\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
$cmake = (Get-Command cmake).Source
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure

# durl 参数矩阵（先用测试参数，不要打印 SESSDATA）
build\release\bin\lapian.exe --online-fetch-test --real `
  --bvid BV1mA411m7pS --cid 1004967230 `
  --sessdata-file .secrets\bilibili_sessdata.txt --tag t88-matrix

# 3 条 M1 E2E（逐条跑；当前 `--online-m1-test` 没有 `--sessdata-file`，
# 凭据由产品凭据存储提供：先正常登录/粘贴 SESSDATA，再跑测试）
build\release\bin\lapian.exe --online-m1-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --tag t88-ob1 --timeout-ms 30000
build\release\bin\lapian.exe --online-m1-test --bvid BV1mA411m7pS --cid 1004967230 `
  --tag t88-ob2 --timeout-ms 30000
build\release\bin\lapian.exe --online-m1-test --bvid BV1GJ411x7h7 --cid 137649199 `
  --tag t88-ob3 --timeout-ms 30000
```

注意：`--online-m1-test` 当前不直接接受 `--sessdata-file`；不要为了图省事把 SESSDATA 放命令行明文。要么先通过产品凭据 UI 登录，要么给 runner 增加从文件读取的受控参数并在日志中脱敏。

开始前先输出一行判断：

`T88_START head=5cd9fc2 m1=blocked_candidate first=probe_qn_fourk`

然后按“矩阵 → 最小修正 → 3/3 → 全门 → 报告/交接 → commit”的顺序做。不要跳过矩阵直接改门，也不要跳过门直接做 M2。
