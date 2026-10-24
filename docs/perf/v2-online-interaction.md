# T8.6.1a 在线长按步进证据（2026-10-24）

- **版本**：v1.3（2026-10-24 追加 **T8.6.1b 在线拖动预览**；v1.2 为 T8.6.1a 用户实机确认）
- **范围**：T8.6.1a 在线长按 PgUp/PgDn（非门）与 **T8.6.1b 在线拖动预览（非门）**。
  T8.6.1b 只做增长会话拖动：已缓存区连续降质预览、松手精确落帧、未缓存区提示/
  取消；缓存命中/本地/durl 不退化。**不做** T8.15、T8.16、4K/1080P60、M1/M2 门。
  **未改 M1/M2 门、验收数字与 OB 样本定义。**
- **提交**：初始实现 commit `3859822`；实机回归 hotfix 一个 commit（hash 见 `git log -1`）；基线 HEAD `5c5dddc`。
- **依据**：[tasks-v2.md](../tasks-v2.md) T8.6.1a、[delta v0.4](../spec-v1.0-delta.md)
  OP2/OP3/OP5/O10、[T8.6 证据](v2-online-session.md) §5、[T8.6.1a 交接](../handoff-2026-10-24-t8.6.1a.md)。
- **一句话**：真实 3 条样本（Medium 副本）各 2 轮、构建目录副本 1 轮，长按 2s
  前进/后退逐帧连续（25–26 帧、0 跳帧），UI 最大间隔 37–77ms，未缓存提示 0ms、
  取消/回到已缓存 4–17ms 调用 + 31–51ms 落点，首末帧边界均停止；本地模式与
  `ctest` 无回退。
- **2026-10-24 实机回归修复（v1.1）**：初始实现提交后，实机首次在线播放发现长按
  只走 1 帧、慢拖只变帧号。已定位并修复两条代码路径（见 §0.1）：未缓存区
  70ms tick 反复取消在途 seek 造成 starvation；物理键仍按住时的伪 `KeyRelease`/
  焦点变化停连发。修复后探针 **54 项/0**，并新增伪 release 用例；**慢拖预览仍属
  T8.6.1b，未在本 hotfix 实现**。
- **2026-10-24 实机确认（v1.2）**：用户确认 C 盘安装版 PgUp/PgDn 长按正常；结合
  54/0、`ctest` 30/30、本地 key-test 77/0，**T8.6.1a 验收闭环**。慢拖预览仍未做。

---

## 0.1 实机回归修复（2026-10-24 晚）

### 根因

1. **未缓存长按 starvation（主因）**：旧 `ApplyOnlineStep` 在“在途未缓存 + 下一帧
   未缓存”时每个 70ms tick 都取消旧等待并重定向。只要在线 seek 慢于 70ms
   （部分索引、慢网、会话重建），每一步都会在落地前被下一步取消；松手后只有
   最后一步落地，表现就是“按住 2 秒只走 1 帧”。暂停/全屏/焦点都不是必要条件。
2. **真实按键伪 release / 焦点停连发**：Qt/Windows 在物理长按时可能插入非
   `autoRepeat` 的 `KeyRelease`；另有 `focusChanged`/窗口失活直接 `StopStepRepeat`。
   这类停止在合成 KeyDown 探针里不会出现，所以初始探针 50/0 未覆盖。
3. **慢拖 clamp/提示**：增长会话 `preview_ == nullptr`，拖动本来只有帧号变化；
   `OnScrubFinished` 又用部分索引长度 clamp，拖到未缓存区会被截到索引末尾。

### 修复

- 未缓存步进改为**单格排队**：不取消在途 seek，落地后再走下一格；提示跟随排队
  目标，不承诺逐帧立即呈现（OP3 不变）。`retargets` 不再增长。
- `KeyRelease` 前查 Windows `GetAsyncKeyState`：物理键仍按住时忽略伪 release；
  下一次 OS 自动重复 KeyPress 在物理键仍按下时恢复连发（边界/seek 失败不恢复）。
- `StopStepRepeat` 增加 reason 日志；`step_start`/`focus_changed`/`step_release_ignored`/
  `online_step_tick`（last/stable/eligible/inflight/queued）可用于实机定位。
- `OnScrubFinished` 增长会话改用 `OnlineLastFrame()`；增长会话不再显示“拖动预览”
  假提示；**拖动预览仍未实现**。

### 证据（用户样本 `BV1PAnt6qEga`/cid `42971761157`，720P，5,169,220B，2327 帧）

| 项 | 修复后 |
|---|---|
| `--online-longpress-test` | **54/0** |
| 已缓存前进 2s | 30→56，26 帧，bad=0 |
| 已缓存后退 2s | 56→30，26 帧，bad=0 |
| 未缓存长按 retargets | **0**（修复前 6） |
| 伪 release 后连发 | 保持 active，继续 8 帧；真正 keyup 停止 |
| auto-repeat KeyPress/Release | 连发不被停 |
| `ctest` | 30/30（30.93s） |
| 本地 `--key-test` | 77/0；2s 27 帧 |

探针用测试钩子模拟“物理键仍按住”的伪 release（`SetStepKeyPhysicalDownForTest`），
真实 `GetAsyncKeyState` 仍是薄 OS 调用；实机键盘确认是可选项，不是代码正确性的前提。
**M1/M2 门、验收数字与 OB 样本未改。**

### 仍未做

- T8.6.1b 拖动预览（依赖 T8.11）。
- 首帧 open 阻塞**已量**：44MB/720P 慢样本 `open_ms=38.07`、`first_frame_ms=27.34`、
  `published=524288/44257060`、online first_frame **634ms**；5MB 样本 706ms。首帧仍
  在 512KB 返回，`OpenGrowing` 不是长等待主因；用户感知的“缓存几十秒”是下载/
  播放缓冲，不改 open 架构，最多改首帧可播提示。


---

## 1. 环境与样本

| 项 | 值 |
|---|---|
| 机器 | DESKTOP-DA6VQGS，Windows 11，RTX 4060（D3D11VA 可用） |
| 构建 | Release，Qt 6.8.3，FFmpeg 7.1.1（最小构建） |
| 在线进程 | **Medium 副本** `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`（本任务新 exe） |
| 工作区副本 | `build\release\bin\lapian.exe`（Low integrity，只作补充复跑） |
| 样本 | OB-1 `BV1BMnv6jE5S`/cid `42915922022`（720P30，14,330,392B，3314 帧）<br>OB-2 `BV1mA411m7pS`/cid `1004967230`（**1080P30**，26,682,935B，2867 帧）<br>OB-3 `BV1GJ411x7h7`/cid `137649199`（720P25，51,973,319B，5306 帧） |
| 下载条件 | 同机同时段；T8.8 完整下载 1.34–2.04MB/s；本任务每条 run 墙钟 9–10s，文档不把“下载完成”写成前提 |
| 测试根 | `build\t8.6.1a-medium-<ob>-r2\`（Medium）、`build\t8.6.1a-<ob>\`（构建目录）；均不入库 |
| 长按口径 | 按下 PgDn/PgUp 保持 **2000ms**（`--longpress-hold-ms` 默认 2000），期间派发事件 |

**增长会话前提**：缓存命中会走本地同步步进，不是本任务对象。探针默认使用新缓存根；
若上一轮已把整片下完，用 `--longpress-fresh-cache` 删除该**测试根**后重跑
（只删显式传入/默认的 `build\t8.6.1a-*` 根，不碰用户默认缓存）。

---

## 2. 实现口径（验收相关）

### 2.1 为什么不能沿用同步检查

T8.6 的 `StartStepRepeat` 在 `GoToFrameForStep` 后比较 `current_frame_` 是否变化：
在线 seek 在 worker 线程完成，`current_frame_` 要等事件回调才更新，第一次按键
就会误判“没走”，从而 `StopStepRepeat()`；70ms 连发因此从未真正开始。T8.6.1a 改为：

- `StartStepRepeat` / 连发定时器 / `StepForward`/`StepBackward` 在增长会话下统一进入
  异步步进状态机（`ApplyOnlineStep`），不再用 `current_frame_` 判边界。
- 每步的目标从“最后一个已请求帧（在途或排队）”±1 推导，而不是从旧画面推导；
  目标落地后（`OnOnlineStepLanded`）才推进队列。
- 本地模式仍走原同步路径；`--key-test` 77/0、2s 27 帧（见 §5）。

### 2.2 已缓存 / 未缓存 / 边界

| 场景 | 行为 |
|---|---|
| 已缓存区 | 单格排队：在途 seek 未完成时到来的 tick 只排 **1** 步；落地后再走，屏幕序列逐帧连续，不合并跳帧。 |
| 未缓存区 | 不承诺逐帧呈现（OP3）：在途 seek 未完成时只排 **1** 格，不取消在途 seek；落地后再走下一格。**不要每 70ms 取消重定向**——慢 seek 会被下一步反复取消，长按会退化成“只走 1 帧”。状态栏“缓冲中（目标帧 N）”跟随排队目标。有在途已缓存步时先让它落地再进入等待，避免跳过已缓存帧。 |
| 边界 | 0 / 末帧（口径与 T8.6 `GoToFrameInternal` 相同：完整索引→容器总帧数→部分索引）立即停止连发；在途的一步仍允许落地；不向越界帧 seek。 |
| 短按 | 单次按键 = ±1；松开只停连发，不取消已请求的那一步；快速双击在途时排 1 格，结果为 ±2，不丢短按。 |
| 外来 seek 在途 | 时间轴/Home/End/标记跳转未落地时，步进从“已请求的目标帧”±1（状态栏显示的就是该目标），并取消旧等待。 |
| seek 失败 | 清空步进状态并停止连发，保留既有错误提示；不按 70ms 重试。 |

### 2.3 会话重建保留部分索引（本任务内含修复）

T8.6 在中断未缓存等待后重建 `MediaSession`，只在 `exact` 目标时把部分索引装回新会话。
未缓存目标 `exact=false`，重建后的会话会短暂没有索引，`CanStepFrames()`/
`OnlineFrameCached()` 失效，在线长按会卡在重建窗口。T8.6.1a 改为：**任何重建都带当前
部分索引**（`RequestOnlineSeek` 的 `index_copy` 不再以 `exact` 为前提）。未改变索引
内容、稳定前缀规则或 CacheStore 写语义。

### 2.4 UI 线程约束

在线重定向/取消仍复用 `RequestOnlineSeek` 的“中断旧 worker → join → 起新 worker”，
join 发生在 UI 线程；单次等待被 25ms 为粒度的 `GrowingCursor` interrupt 打断。实测
长按期间 UI 心跳最大间隔 **37–77ms**（≤200ms），没有把网络等待放到 UI 线程。

---

## 3. 探针 `--online-longpress-test`

新增产品探针（真实网络 + 真实窗口，不注入假数据），修复后 **54 项检查**：

1. 解析确认 → 首帧 → 默认暂停 → 等部分索引 ≥60 帧；
2. 远端未缓存目标：跳转提示 0ms → 单次 PgDn 从目标 +1（提示/≤100ms）→ 按住 600ms
   （提示跟随、**retargets=0**、在途 seek 不被取消）→ 跳回已缓存帧（取消 ≤100ms，
   落点精确 ≤1.5s）；
3. 已缓存区 PgDn 按住 2s：连发期间 active、松开停止、落地轨迹逐帧 +1、≥20 帧；
   期间注入 OS 自动重复 KeyPress/KeyRelease，连发不被停；
4. 已缓存区 PgUp 按住 2s：逐帧 −1、≥20 帧、不碰 0 边界；
5. **伪 KeyRelease**：物理键仍按住时收到非 autoRepeat release，连发保持并继续走帧；
   真正 keyup 后停止；
6. 短按：单次 +1 且 350ms 无残留；快速双击 +2；快速双击 PgUp −2；
7. 0 边界：按住 PgUp 不启动连发、帧号不变、不产生 seek；
8. 末帧边界：先请求 last−2（可能未缓存），按住 PgDn；连发在越界前停止，
   帧号不越过 last；
9. 全程 16ms UI 心跳；关窗 ≤1s。

命令（Medium 副本，OB-1；OB-2/OB-3 换 `--bvid/--cid/--tag`）：

```powershell
& "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe" --online-longpress-test `
  --bvid BV1BMnv6jE5S --cid 42915922022 --tag medium-ob1-r2 --timeout-ms 30000 `
  --online-cache-root build\t8.6.1a-medium-ob1-r2\cache `
  --online-pictures   build\t8.6.1a-medium-ob1-r2\pics `
  --online-records-root build\t8.6.1a-medium-ob1-r2\records `
  --longpress-fresh-cache
```

### 3.1 真实三样本（Medium 副本，第 2 轮，实现完成时 50 checks / 0 failures）

| 项 | OB-1 | OB-2 | OB-3 |
|---|---:|---:|---:|
| 首帧 | 628ms | 458ms | 450ms |
| qn / 档位 | 64 / 720P | **80 / 1080P** | 64 / 720P |
| 末帧 `last` | 3313 | 2866 | 5305 |
| 未缓存目标 | 3193 | 2746 | 5185 |
| 未缓存跳转提示/UI | 0ms | 0ms | 0ms |
| 未缓存**步进**提示/UI | 0ms（目标 3194） | 0ms（2747） | 0ms（5186） |
| 未缓存重定向次数（600ms 按住，修复前语义） | 6 | 6 | 6 |
| 取消旧等待（改 seek 调用） | 4ms | 5ms | 4ms |
| 取消→回到已缓存 30 帧 | **31ms** | **51ms** | **37ms** |
| 已缓存前进 2s | 30→56，**26 帧**，bad=0 | 30→55，**25 帧**，bad=0 | 30→56，**26 帧**，bad=0 |
| 已缓存后退 2s | 56→30，**26 帧**，bad=0 | 55→29，**26 帧**，bad=0 | 56→30，**26 帧**，bad=0 |
| 短按 | +1 / 无残留 / 双击 +2 / 双击 −2 | 同左 | 同左 |
| 0 边界 | 停止、frame=0、0 seek | 同左 | 同左 |
| 末帧边界 | 停止，boundary=2，frame≤last | 同左 | 同左 |
| UI 心跳最大间隔 | **46ms** | **77ms** | **51ms** |
| 关窗 | 543ms | 496ms | 377ms |
| 判定 | **50/0 pass**（修复前） | **50/0 pass**（修复前） | **50/0 pass**（修复前） |

说明：

- **无跳帧**：轨迹分别 `31,32,…,56` / `55,54,…,30`，`bad=0`；逐帧 ±1，无重复、
  无跨帧。OB-2 前进为 25 帧而非 26：有一次 tick 因 seek 未落地被“单格排队”规则
  丢弃（不跳帧，只是少走一步）；仍 ≥20 帧，方向正确。这是有意的背压，不是丢键。
- **方向正确**：前进轨迹严格递增、后退严格递减；短按与双击在同一次 run 内验证。
- **UI 不停顿**：16ms 心跳在长按/重定向/回到缓存期间最大 46–77ms；没有 >200ms 事件。
- **未缓存**：提示和取消调用的 0–5ms 是 UI 立即返回；真正落点按 OP3 不承诺毫秒数，
  只保证落到目标（本探针在改回缓存区时 31–51ms 落到 30 帧）。

### 3.2 首轮与构建目录副本

| 批次 | 样本 | checks | UI gap | 取消调用 | 前进/后退 | 关窗 |
|---|---|---:|---:|---:|---|---:|
| Medium 第 1 轮（49 checks） | OB-1 | 49/0 | 37ms | 17ms | 26 / 26 | 587ms |
| | OB-2 | 49/0 | 55ms | 11ms | 26 / 26 | 519ms |
| | OB-3 | 49/0 | 38ms | 4ms | 26 / 26 | 381ms |
| 构建目录副本（Low） | OB-1 | 49/0 | 43ms | 0ms* | 26 / 26 | 618ms |
| | OB-2 | 49/0 | 56ms | 24ms | 26 / 26 | 395ms |
| | OB-3 | 49/0 | 42ms | 3ms | 26 / 26 | 393ms |

\* 构建目录副本的 0ms 是旧等待尚未进入阻塞读时立即被取消；不作为“取消上限”数字，
以 Medium 第 1/2 轮的 4–17ms 为准。

第 1 轮没有 `back_to_cached_le_1500ms` 检查，所以是 49；第 2 轮为 50。两轮均 0 失败。
首轮/复跑都使用 `--longpress-fresh-cache`，保证是增长会话。

### 3.3 实机回归修复后复跑（用户样本，54 checks）

| 项 | 值 |
|---|---|
| 样本 | `BV1PAnt6qEga`/cid `42971761157`，720P，5,169,220B，2327 帧 |
| 首帧 / qn | 706ms / 64（720P） |
| 索引前缀（开始时） | stable=193，container=2327 |
| 已缓存前进 / 后退 | 30→56 / 56→30，各 26 帧，bad=0 |
| 未缓存长按 retargets | **0**（修复前 6） |
| 伪 release | 连发保持，继续 8 帧；真正 keyup 停止 |
| auto-repeat | 连发保持 |
| 结果 | **54/0 pass** |
| `ctest` | 30/30（30.93s） |
| 本地 `--key-test` | 77/0；2s 27 帧 |

修复前的 50/0 只覆盖“合成 KeyDown + 已缓存区”，不覆盖“慢 seek 未缓存长按”和
“伪 release”；这两项是实机 1 帧的根因，现已加入探针。

**验证陷阱**：用户前两次手动复现用的是 Start Menu `lapian.lnk`，目标为
`dist\lapian.exe`（2026-10-23 23:30），**早于 T8.6.1a commit（2026-10-24 01:10）**，
即旧 T8.6 同步步进代码；因此“改完仍 1 帧”是启动错二进制，不是本次 hotfix 无效。
已重指安装路径并用 `c7e063d` exe 替换 `dist\lapian.exe`。此后所有手动验收先核对
`Get-Process lapian | Select Path,StartTime`。

---

## 4. 逐项对照 T8.6.1a 验收

| 验收（tasks-v2） | 证据 | 判定 |
|---|---|---|
| 已缓存区：长按 2s 连续前进/后退，方向正确、无跳帧 | 3 样本×2 轮：25–26 帧/2s，轨迹 bad=0；前进递增、后退递减 | **pass** |
| 无 UI >200ms | 16ms 心跳最大 37–77ms；0 次 >200ms 事件 | **pass** |
| 到边界停止 | 0 边界：不启动连发、0 seek；末帧：连发停止、frame≤last | **pass** |
| 短按行为不回退 | 单次 ±1、350ms 无残留；快速双击 ±2（不丢键） | **pass** |
| 未缓存区“缓冲中（目标帧 N）”且可取消 | 提示 0ms；取消调用 4–17ms；回到已缓存 31–51ms；修复后 600ms 按住 retargets=0，慢 seek 不被反复取消 | **pass**（毫秒不承诺，OP3） |
| 不阻塞 UI / 不把网络等待放 UI | 状态机 + 既有 worker；心跳见上 | **pass** |
| 证据落 `docs/perf/v2-online-interaction.md` | 本文件 | **pass** |

**不改变 M1/M2**：本任务不是门；没有改 qn/样本/OP 数字，没有把未缓存步进写成 P7 或
1080P 保证。OB-2 的 1080P 只说明“当前流可逐帧步进”，不是 M2/DASH 结论。
**2026-10-24 实机回归 hotfix** 只修异步步进状态机/伪 release/拖动 clamp，不改任何
门或数字；修复后 54/0，仍未重跑三样本全矩阵（历史 50/0 仍作为修复前记录保留）。

---

## 5. 回归

| 项 | 结果 |
|---|---|
| `ctest` | **30/30**（30.93s，hotfix 后复跑） |
| 本地 `--key-test`（`build\t6.2-key\A.mp4`） | **77/0**；2s 27 帧；本地同步步进语义未变 |
| 在线 M1 E2E（OB-1，新缓存） | **28/0 pass**；首帧 657ms；P7 p50/p95 13/18ms；cached seek p50/p95 24/74ms；UI gap 66ms；关窗 38ms |
| 在线 M1 E2E + 未缓存落点 | **29/0 pass**；未缓存目标 3194 → 3194（7758ms）；P7 p95 18ms；UI gap 56ms |
| `--online-buffer-test` | **10/0 pass**；第 145 帧耗尽/恢复 145→174；关窗 11ms |
| 本地 P1（20 次，proxy-V1） | p50 265.16ms / p95 **279.03ms**，20/20 ≤1500ms |
| 本地 P2（20 次，proxy-V1） | p50 321.24ms / p95 **361.57ms**，20/20 ≤2500ms |
| 本地 P7（2s GOP，20 次） | p50 58.26ms / p95 **65.52ms** / max 65.81ms / over 0，pass |
| 会话重建索引 | 未缓存取消→回缓存后仍在 10s 内精确落到 30 帧；M1 未缓存落点 29/0 |

P1/P2/P7 与 T8.8 基线（342.92/376.14/64.79ms）同量级；本任务没有把在线初始化放进
启动路径，也没有改本地索引/解码。

---

## 6. 失败样本与缺口

1. **未缓存区不逐帧呈现**：按住时单格目标是“最新目标”，会跳过中间未缓存帧；
   OP3 只要求提示/取消和落点，本任务没有把它写成“未缓存逐帧”。已缓存区才承诺
   逐帧连续（轨迹证据）。
2. **弱网/限速未做**：真实 CDN 下行 1.3–2.0MB/s，远高于素材码率，正常前进几乎
   追不上未缓存边界；未缓存场景用“远端跳转 + 步进”构造，没有做 netsh/代理限速，
   也没有验证 <0.5x 实时网络的持续长按。
3. **只测 2s**：没有 30s 长按/浸泡；轨迹上限 512 帧（本任务 25–26 帧，余量足够）。
4. **4K / 1080P60 / DASH 未测**：本任务不依赖 M2；OB-2 的 1080P30 只作真实样本。
5. **单格背压会少走一步**：OB-2 第 2 轮 25 帧而非 26；这是刻意的“不跳帧”取舍
   （宁可少走，不跨帧）。如果后续要求严格 14 帧/s，需要另立任务，不在本任务改口。
6. **UI 心跳是 16ms 定时器**：只证明没有 >200ms 事件循环停顿；不测渲染帧率/掉帧。
7. **关窗时可能仍有远端等待**：末帧边界测试会留下 last 的等待，关窗路径取消并
   join（377–618ms）；没有等它真正落地。
8. **Medium 副本只测显式测试根**：没有用默认 `%LOCALAPPDATA%\拉片软件\cache` 做本
   探针（缓存命中会是本地同步路径）；默认根/缓存命中由 T8.7/T8.8 覆盖，本任务未改。
9. **真实扫码/大会员/4K**：不涉及；T8.15 仍“未验证”。
10. **探针不是 ctest**：`--online-longpress-test` 需要真实网络/凭据，未加入 `ctest`；
    ctest 的 30 项保持无网络/GUI。

---

## 7. 最短复现

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure

# Medium 副本（正常用户路径）；先用 install/复制把新 lapian.exe 放到安装目录
$exe = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"
& $exe --online-longpress-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --tag medium-ob1 --timeout-ms 30000 `
  --online-cache-root build\t8.6.1a-medium-ob1\cache `
  --online-pictures build\t8.6.1a-medium-ob1\pics `
  --online-records-root build\t8.6.1a-medium-ob1\records `
  --longpress-fresh-cache
# OB-2 / OB-3 换 bvid/cid/tag；复跑必须重新 --longpress-fresh-cache
```

关键日志行：`online_longpress_result checks=54 failures=0 verdict=pass`、
`online_longpress_cached dir=...`、`online_longpress_uncached ...`、
`online_longpress_ui_gap max_ms=...`、`online_longpress_close ms=...`。

---

## 8. T8.6.1b 在线拖动预览（2026-10-24，非门）

### 8.1 结论

增长会话（`online_ready_ && !online_cache_hit_`）拖动时，已缓存区不再只变帧号：
独立 `OnlinePreviewDecoder` 用第二个 `GrowingCursor` 读取 `.part`，按稳定前缀索引
解码、降采样到最长边约 1280 的 NV12 并连续发布；松手先停预览，再走既有精确
seek。DASH 1080P30 样本 1s/60 样本慢拖 **55 presented、fps_total 54.95、
fps_active 61.18**，UI gap **69ms/0 次 >200ms**，stale 0，松手 **frame 75 /
PTS 2,500,000 精确**；未缓存目标 3312 提示 **0ms**、`waiting=1`、拖回恢复、
松手/新目标取消 0ms；缓存命中、durl、本地拖动均未退化；拖动中 `S` 被拦截；
拖动中关窗 56ms（DASH）/9ms（缓存命中）/76ms（durl）。`ctest` **36/36**。

**判定：pass（非门任务）**。没有改 M1/M2 门、验收数字、OB 样本或
`lapian-cache-1`；没有把“预览可用”写成清晰度保证；T8.15 未执行。

### 8.2 实现边界

| 项 | 做法 |
|---|---|
| 读层 | 新增 `src/media/OnlinePreviewDecoder.*`：独立 `GrowingSource::GrowingCursor`/`GrowingFormat` + 独立 `VideoDecoder::OpenWithFormat`（软件解码），只读视频 `.part`，不碰 `MediaSession` 解码器、`online_seek_*`、`online_step_*` |
| 帧映射 | GUI 在拖动开始时拷贝当前稳定 `FrameIndex` 快照；索引增长时按“目标超出快照或 +30 帧”节流重发快照；worker 只持 `shared_ptr<const FrameIndex>` |
| 目标合并 | `latest target + generation`；新目标只覆盖最新值，旧 generation 在解码切片点退出；正向连续发布中间帧，反向只发布目标帧 |
| 缓存/内存 | 最新帧 1 张 + 最近 24 张降采样帧（约 33MB @720p NV12），`End/Close` 释放；`SwsContext` 复用 |
| 未缓存 | `OnlineFrameCached` 为 false 时只标记 waiting，不 seek/不阻塞读；显示「缓冲中（预览）：目标帧 N」；下载追上后下一 tick 重新尝试 |
| 松手 | `OnScrubFinished` 先 `EndScrubPreview`（interrupt + join）再 `GoToFrame(target)`；精确 seek 仍走 `RequestOnlineSeek`，预览帧不参与落点 |
| 关窗/切档 | `ShutdownOnlineSession` 先 `online_->Cancel()` 唤醒 reader，再 `StopPreview`；预览句柄在 `.part`→`.mp4` finalize 前关闭 |
| 本地/缓存命中 | 仍用原 `PreviewDecoder`（路径 + `FrameIndex`），32 帧缓存/只发布目标帧/丢弃过期帧语义未改；增长会话不创建本地预览器 |
| 截图 | 复用 `preview_timer_->isActive()` 守卫：拖动中 `S` 只提示「拖动预览中，松开后再截图」 |
| DASH | 只读视频 `source`；音频仍由既有 `AudioEngine/WaveformGenerator` 处理，不擦洗、不建第二下载 |

### 8.3 环境与命令

- 机器：DESKTOP-DA6VQGS / AMD Ryzen 5 7500F 6-Core / 31.6GB / RTX 4060；
  构建目录 `build\release\bin\lapian.exe`（Low integrity，测试根）。
- DASH 样本：`BV1BMnv6jE5S` / cid `42915922022`（DASH 1080P AVC qn=80，
  111s，视频 31,057,770B，音频 1,294,290B，30fps）。
- durl 样本：同 BV/cid，`--force-durl`，qn=64 720P，14,330,392B。
- 缓存命中样本：`BV1PAnt6qEga` / cid `42971761157`，720P qn=64，
  5,169,220B / 2327 帧（完整 `data.mp4`）。

```powershell
$env:PATH = "$qt\bin;$env:PATH"

# 1) DASH 增长会话：已缓存慢拖 + 未缓存 + 反向 + 首帧/极短 + 关窗
& .\build\release\bin\lapian.exe --online-drag-preview-test `
  --bvid BV1BMnv6jE5S --cid 42915922022 `
  --online-cache-root build\t8.6.1b-cache `
  --online-pictures build\t8.6.1b-pictures `
  --online-records-root build\t8.6.1b-records `
  --drag-seconds 1 --drag-samples 60 --m1-throttle-bps 120000

# 2) 缓存命中（复用完整缓存，不重新下载）
& .\build\release\bin\lapian.exe --online-drag-preview-test `
  --bvid BV1PAnt6qEga --cid 42971761157 --drag-cache-hit `
  --online-cache-root build\t8.6.1a-online-cache `
  --online-pictures build\t8.6.1b-pictures-cachehit `
  --online-records-root build\t8.6.1b-records-cachehit `
  --drag-seconds 1 --drag-samples 60

# 3) durl 回归
& .\build\release\bin\lapian.exe --online-drag-preview-test `
  --bvid BV1BMnv6jE5S --cid 42915922022 --force-durl `
  --online-cache-root build\t8.6.1b-durl-cache `
  --online-pictures build\t8.6.1b-durl-pictures `
  --online-records-root build\t8.6.1b-durl-records `
  --drag-seconds 1 --drag-samples 60 --m1-throttle-bps 120000
```

### 8.4 已缓存慢拖（DASH 1080P30，最终轮）

| 指标 | forward | backward |
|---|---|---|
| 拖动 | 15→75，1s/60 样本 | 75→45，1s/30 样本 |
| presented | **55** | 10（无 ≥30 要求） |
| fps_total | **54.95** | 10.00 |
| fps_active | **61.18** | 32.26 |
| 首张预览 | 102ms | 690ms |
| UI gap | **69ms / 0 次 >200ms** | 69ms / 0 |
| stale / dropped | **0 / 0** | **0 / 0** |
| 松手 → 精确落地 | release 6ms，landing 99ms，**frame 75 / PTS 2,500,000** | release 4ms，landing 59ms，**frame 45 / PTS 1,500,000** |
| 预览停止 | yes | yes |

探针检查：33→35 项（新增首帧/极短反向），**35/0 verdict=pass**；关窗 56ms；
`snapshot_blocked_during_drag=1`；无预览错误。

### 8.5 未缓存/取消（DASH 与 durl）

| 项 | DASH | durl |
|---|---|---|
| 未缓存目标 | 3312（`last-1`，stable 121） | 3312（stable 132） |
| 「缓冲中（预览）」 | 0ms，含目标帧号 | 0ms |
| `waiting` | 1（无新帧） | 1 |
| 拖回已缓存 | waiting=0，presented 0→1 | waiting=0，presented 0→1 |
| 未缓存松手 | UI 0ms，预览停止 | UI 0ms，预览停止 |
| 新目标取消 | UI 0ms，落点 frame 15 | UI 0ms，落点 frame 15 |
| UI gap | 69ms / 0 次 >200 | 66ms / 0 次 >200 |

未缓存区只承诺“提示/取消/不冻结”，不承诺帧率或 P7/P9；未缓存目标不做
`SeekToEstimatedPts` 预览，只有松手后的精确 seek 会按既有路径尝试。

### 8.6 缓存命中与 durl

| 项 | 缓存命中 720P | durl 720P |
|---|---|---|
| presented / fps_total / fps_active | **56 / 56.00 / 59.89** | **55 / 55.00 / 60.04** |
| 松手落点 | frame 75 / PTS 3,125,000（release 39ms，landing 2ms） | frame 76 / PTS 2,533,313（release 4ms，landing 55ms） |
| UI gap | 67ms / 0 次 >200 | 66ms / 0 次 >200 |
| stale | 0 | 0（backward dropped=1，未呈现） |
| 网络 | playurl=0、DASH=0 | 0 额外缓存命中请求；durl 路径 |
| 关窗 | 9ms | 76ms |
| checks | **25/0 pass** | **35/0 pass** |

缓存命中仍走本地 `PreviewDecoder`；本地 `--drag-test`
（`media/proxy/proxy-V1.mp4`，1080p24）**p4_fps 61.8、p4_presents 124、
p6 p95 51.0ms、p7 back/fwd p95 0.2/10.5ms、p8 26 帧、p9 p95 52.2ms，
pass**。`S` 拖动中拦截、首帧/极短反向均通过。

### 8.7 单测与回归

| 项 | 结果 |
|---|---|
| `lapian_online_preview_test`（新增 ctest） | pass；独立读层 open 19.72ms、2s 正向拖动 **62.98fps/126 帧**、反向 40 步精确、未缓存 waiting、End 3.18ms |
| `ctest` | **36/36**（60.71s；含新增在线预览单测） |
| 本地 `--key-test` | **77/0**；2s 27 帧 |
| 在线 `--online-longpress-test`（`BV1BMnv6jE5S`，fresh cache） | **54/0 pass**；前进/后退 26/26、bad=0、UI gap 60ms、关窗 24ms |
| 本地 `--drag-test` | p4 61.8fps / p4 124 presents、p6 p95 51.0、p7 p95 0.2/10.5、p8 26 帧、p9 p95 52.2，pass |
| M2 | 1 条尝试被 `nav_normal_user` 拒绝（当前默认凭据 `vip_status=1`），未进入 34 项；**引用 T8.14 3/3 34/0，不写 M2 重新通过** |
| P1/P2/P7（`bench-regression -Suites startup,step1080`，final dist） | **PASS exit 0，0 超差**：P1 p50/p95 **275.64/288.02ms**、P2 **332.66/341.7ms**（20/20）；P7 back p50/p95/max **0.20/0.90/54.24ms**、fwd **0.23/0.37/10.08ms** |

### 8.8 失败样本与缺口

1. **反向预览帧率低**：DASH 反向 10 presented/1s（cache 24 帧耗尽后按关键帧重解），
   只验证单调/无 stale/精确落帧，不承诺 ≥30fps；正向已缓存区才是 ≥30fps 验收面。
2. **未缓存区不承诺帧率**：只提示/取消/不冻结；未缓存目标不做预览 seek。
3. **真 4K/1080P60 未测**：样本是 DASH 1080P30；4K/60 仍属 T8.15/T7.4，不写可用。
4. **软件解码**：预览用软解 + swscale；HEVC/AV1 高码率/弱 CPU 下未测吞吐；
   预览最长边 1280，不是源分辨率，不是精确帧。
5. **切档中拖动未测**：产品路径有 `StopPreview`，但探针未在拖动中触发清晰度切档。
6. **旋转在线样本未单测**：tick 使用 `session_->info().rotation`，与本地预览同一
   rotation 来源；没有专门的在线旋转样本。
7. **UI gap 是 16ms 心跳**：只证明没有 >200ms 事件循环停顿，不测渲染掉帧/GPU。
8. **P1/P2 首轮热缓存噪声**：首轮同机基线 p50 漂移 +11.76%/+13.15%（exit 3），
   复跑 final dist **PASS exit 0、0 超差**（P1 275.64/288.02ms、P2 332.66/341.7ms、
   P7 back 0.90ms/fwd 0.37ms）；不写“零退化”，以复跑为准。
9. **M2 未重跑**：当前默认凭据是 `vip_status=1`，M2 runner 普通用户前置失败；
   只引用 T8.14，不把本次写成 M2 证据。
10. **T8.14 残余风险保留**：未定位 teardown `0xC0000374`、L-1 seed 卡住仍列风险，
    不写“无崩溃/已证明稳定”。
