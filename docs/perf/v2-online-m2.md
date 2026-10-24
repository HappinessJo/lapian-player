# T8.14 证据：M2 门（2026-10-24）

- **任务**：[tasks-v2.md](../../tasks-v2.md) **T8.14（M2 门）**；依据 [spec-v1.0-delta.md](../spec-v1.0-delta.md) v0.4 的 O2–O7/O9、OP1–OP8、DEC-2/DEC-5，[ADR-007](../adr/ADR-007-bilibili-online.md)，以及 T8.8 M1 的测量口径。
- **状态**：**T8.14 完成；M2 六条硬条件逐条达标，判定 pass**。**不写“无崩溃/已证明稳定”**：M2 期间复现 1 次未定位的 teardown `0xC0000374` 与 1 次 L-1 seed 卡住，均未复现；见 §9。**不做** T8.15（大会员条件验收）、T8.16（打包/隐私）、T8.6.1b（拖动预览），也不把 4K/1080P60 写进 M2。
- **基线**：`01b98d5`（T8.13）+ T8.14 探针变更；`ctest` 35/35；dist 34.3MB/25。**M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema 未改。**
- **一句话**：普通用户 3/3 `OB` 用 DASH 1080P30（qn=80，30.000/29.412/25.000fps）；OP1 p95 327/366/343ms、OP2 p95 46/57/53ms、OP3 提示 0ms/取消 26ms/落点 0、OP4 300s 0 次 >200ms 停顿、OP5 20/20 p95 111.7ms 0 请求；60s 音画 p95 17.6/17.4/20.0ms、20/20 seek 精确；故障矩阵 10/10；本地 O9 0 TCP/UDP、P1/P2/P7 311.87/390.59/1.47ms、key-test 77/0。

---

## 1. M2 判定与六条硬条件

| # | 硬条件（tasks-v2 §M2 门） | 本轮结果 | 判定 |
|---|---|---|---|
| 1 | 普通用户 3/3 `OB` DASH 1080P30 可播；实际 fps/档位与 S2 一致 | OB-1/2/3 均 `dash=1`、qn=80、AVC、fps 30.000/29.412/25.000；首帧 330.5/365.6/353.6ms | **pass** |
| 2 | OP1 起播、OP2 已缓存 P7、OP3 未缓存行为、OP4 已缓存停顿、OP5 缓存命中按 DEC-2 达标 | OP1 p95 327/366/343ms（20/20/20 有效；另列 1 次 10.4s 网络拖慢与 1 次 resolve 超时）；OP2 p95 46/57/53ms；OP3 提示 0ms、取消 26ms、落点 0；OP4 300s 0 停顿；OP5 20/20 p95 111.7ms | **pass** |
| 3 | DASH 双流 60s 音画 ≤1 帧；20 次 seek 精确；断网/过期/损坏/磁盘满可恢复 | 音画 p95 17.6/17.4/20.0ms（0.51/0.54/0.55 帧）；20/20 landing=0、PTS 0 失败；故障矩阵 10/10 + T8.13 引用 | **pass** |
| 4 | 本地零网络 + P1/P2/P7 + F1–F8 不退化 | O9 0 TCP/UDP（137 次轮询）、P1 p95 311.87ms、P2 390.59ms、P7 1.47/0.53ms、key-test 77/0、ctest 35/35 | **pass**（F1–F8 UI 探针未全重跑，见 §9.5） |
| 5 | **M2 门没有** 4K/1080P60 验收项；T8.15 是 B 的独立条件验收，不是门 | 本轮未做 4K/60；未把 OB-2 的降帧提示写成 60fps 可用 | **pass（条件成立）** |
| 6 | 不把 durl 1080p（S2 1/3）写成 1080p 保证；1080p 结论必须来自 DASH | 所有 1080P/OP 数字来自 DASH qn=80；durl 只保留 M1 回归引用，未用于 M2 数字 | **pass** |

**判定**：**pass（六条硬条件全部达标）**。**稳定性残余风险不冒充通过**：`0xC0000374` 与 L-1 seed 卡住尚未定位/复现（§9.1–§9.2）；T8.13 的“未复现崩溃/卡住”风险在本轮有新的观察，不得写成“无崩溃”。若按“任何未定位崩溃/卡住都不得判门”的更严格口径，应改判 blocked；本轮按 tasks-v2 §M2 门的六条硬条件判定。

---

## 2. 验收环境与样本

- **账号**：登录普通用户，`nav vip_status=0`；SESSDATA 只从 `.secrets\bilibili_sessdata.txt` 读取，未打印/提交。
- **机器/网络**：DESKTOP-DA6VQGS（Ryzen 5 7500F/31.6GB/RTX 4060）；同一时段、同机、同网；DASH 下载 1s 采样 4.99–10.98MB/s。
- **OB 样本**（未改）：

| 编号 | bvid | cid | 时长 | 实际 DASH |
|---|---|---|---:|---|
| OB-1 | `BV1BMnv6jE5S` | `42915922022` | 110.5s | qn=80，AVC/codecid=7，30.000fps，视频 31,057,770B + 音频 1,294,290B |
| OB-2 | `BV1mA411m7pS` | `1004967230` | 95.6s | qn=80，AVC/codecid=7，29.412fps（容器约 30） |
| OB-3 | `BV1GJ411x7h7` | `137649199` | 212.3s | qn=80，AVC/codecid=7，25.000fps，视频 69,759,161B + 音频 5,408,198B |

- **OP4 补充长样本 L-1**（非 OB，不参与 §1 的 3/3）：`BV14Enb6LEDe`/`42979362482`，386s，DASH 1080P AVC **29.970fps**，普通用户可播；全缓存 58,054,247B + 8,657,239B。选择原因：OB-1/2/3 只有 96–213s，无法在不 loop、不 restart 的前提下测“连续 300s”；L-1 提供单段连续 300s，避免把 loop seek 混入停顿计数。**不改 OB 样本。**
- **测试根**：`build\t8.14-m2\*`；Medium 副本 `%LOCALAPPDATA%\Programs\lapian-t814\lapian.exe`（integrity=medium；用于默认 AppData/本地 O9）。工作区 Low 只用于显式 `build\t8.14-m2\*` 缓存根。

---

## 3. 硬条件 #1：3/3 DASH 1080P30

命令：`--online-m2-test`，每条独立根、冷缓存、默认 60s 播放 + 20 次 seek。

| 样本 | ready | qn/codecid | dash | 视频 fps | 音频 | 音画 p95 | seek 落点/PTS | 内存峰值 | 缓存命中 |
|---|---:|---:|---:|---:|---|---:|---|---:|---:|
| OB-1 | 330.5ms | 80/7 | 1 | 30.000 | 30280/AAC | 17.6ms | 0/20、0/20 | 204.5MB | 45.7ms |
| OB-2 | 365.6ms | 80/7 | 1 | 29.412 | 30280/AAC | 17.4ms | 0/20、0/20 | 204.6MB | 46.5ms |
| OB-3 | 353.6ms | 80/7 | 1 | 25.000 | 30280/AAC | 20.0ms | 0/20、0/20 | 206.6MB | 47.3ms |

- 3/3 `online_m2_result checks=34 failures=0 verdict=pass`；`playurl_requests=0`（DASH 不请求 durl）。
- OB-2 的 29.412fps 仍触发 DEC-4 横幅/角标（T8.12 证据），本轮未重复判 60fps。
- 音画偏差 p95 0.51/0.54/0.55 帧，max 27.0/24.5/25.2ms（0.76/0.72/0.63 帧）；`av_drift_p95_le_1frame` 与 `max_le_1_5frame` 均通过。
- seek p50/p95/max：OB-1 75.7/111.7/157.3ms；OB-2 77.2/155.6/156.4ms；OB-3 69.2/78.0/91.0ms。落点与 PTS 均 0 失败；seek 后音频从当前帧起 0 失败。
- EOF：跳 `last-2` 后视频 EOF 正常停住，3/3 `video_eof_stops_playback=1`，帧号停在末帧，无死锁。

---

## 4. OP1–OP8

### 4.1 OP1 在线起播（3 条 × 冷缓存 20 次）

- 口径：用户点确认 → 首帧 ready；每条先删该条缓存根；DASH 默认（未 `--force-durl`）；记录 1s 真实下载采样。
- 每条 20 次有效样本；额外失败/拖慢尝试另列，不隐藏。

| 样本 | n | min | p50 | p95 | max | ≤5s | 1s 下行 min/p50 | 判定 |
|---|---:|---:|---:|---:|---:|---:|---|---|
| OB-1 | 20 | 284 | 306 | **327** | 365 | 20/20 | 9.731 / 10.50MB/s | pass |
| OB-2 | 20 | 289 | 310.5 | **365.75** | 456 | 20/20 | 10.24 / 10.739MB/s | pass |
| OB-3 | 20 | 292 | 313 | **343.15** | 346 | 20/20 | 4.985 / 6.743MB/s | pass |

- 额外尝试：OB-1 有 1 次首帧 10,393ms（E2E exit 1）与 1 次 resolve 超时（无首帧，exit 2）；OB-2/OB-3 各有 1 次首帧正常（307/308ms）但 E2E 其它检查失败的尝试。把 OB-1 的 10,393ms 一并计入时，p95 仍 365ms、**20/21 ≤5s**（≥19/20）。
- 下行：产品真实 DASH 下载路径 1s 采样 4.985–10.98MB/s，全部 ≥1MB/s；T8.8 历史 1MB Range 1.59MB/s 仍保留。

### 4.2 OP2 已缓存区单帧步进（3 条 × 20 次 ±1）

- 用完整 DASH 缓存命中路径（视频+音频都 complete，本地全量索引）；每条 20 次 PgUp/PgDn。
- 测试点距缓存边界远大于 2s；解码器热。

| 样本 | 缓存命中首帧 | n | 失败 | p50 | p95 | max | 判定 |
|---|---:|---:|---:|---:|---:|---:|---|
| OB-1 | 72ms | 20 | 0 | 3 | **46ms** | 51ms | pass |
| OB-2 | 69ms | 20 | 0 | 3 | **57ms** | 57ms | pass |
| OB-3 | 74ms | 20 | 0 | 2 | **53ms** | 60ms | pass |

- 3/3 `online_m1_cache_hit cache_hit=1`、`qn=80 dash=1`；严格 ±1，0 失败；p95 全部 ≤80ms。

### 4.3 OP3 未缓存区 seek（提示/取消/落点）

- 冷缓存 + `--m1-throttle-bps 1048576`（只节流视频），保证测试窗口内仍有未缓存区；`--uncached-landing` 在同一缓存根续传并等下载追上。

| 样本 | 目标帧 | 提示 ms | 取消 ms | 回已缓存 | landing | landing 等待 | 判定 |
|---|---:|---:|---:|---|---|---:|---|
| OB-1 | 3194 | 0 | 26 | 帧 161 | 3194=3194 | 21,085ms | pass |
| OB-2 | 2747 | 0 | 26 | 帧 161 | 2747=2747 | 16,018ms | pass |
| OB-3 | 5187 | 0 | 26 | 帧 39 | 5187=5187 | 57,640ms | pass |

- 3/3 prompt 28/0 pass；landing 落点误差 0（只判这一点）。提示/取消均 ≤100ms；DEC-2 不承诺等待毫秒数。
- **观察（不属 OP3 判定项）**：OB-3 慢 landing 运行（57.6s 等待）出现 UI 心跳 max 36,089ms，随后 `ctrl_m`/`snapshot_saved`/`ui_gap_le_200ms` 三项失败；落点仍 0。见 §9.3。OB-1/OB-2 landing 运行 29/0，UI 正常。

### 4.4 OP4 已缓存区 5 分钟播放

- 样本 L-1（386s，DASH 1080P AVC 29.970fps，普通用户），全缓存命中，`--m2-op4 --m2-play-seconds 300`。
- 16ms 心跳；帧号/PTS 变化计数；画面无更新且 UI 无响应 >200ms 记一次；EOF 才 restart（本样本未触发）。

| 指标 | 结果 |
|---|---|
| 缓存命中 ready | **69.4ms**；qn=80、dash=1；playurl/dash/quality/http 全 0 |
| 连续播放 | **300,000ms**（无 restart、无 loop） |
| UI gap max / >200ms 次数 | **21ms / 0** |
| 画面停顿 >200ms（frame_stalls） | **0** |
| 冻结停顿 >200ms（frame+UI 同时） | **0** |
| `stalls_gt_200ms` | **0** |
| 帧变化 / 呈现帧 | 8,988 / 8,989 |
| 音频 underrun | **0** |
| 内存峰值 | **121.9MB**（≤800MB） |
| 结果 | `online_m2_op4` + **17 checks / 0 failures / pass** |

### 4.5 OP5 缓存命中（断网模型，20 次）

- Medium 副本 `%LOCALAPPDATA%\Programs\lapian-t814\lapian.exe`（integrity=medium），**默认 AppData 缓存根**；先以 `--m2-default-paths --m2-qn 80` 种入 OB-1 DASH 1080P（视频 31,057,770B + 音频 1,294,290B，`complete=true`），再 `--m2-op5 --m2-default-paths` 注入永不成功的 HTTP 客户端。
- 默认根里原有 qn=64 durl 条目；种子用 `--m2-qn 80` 强制取 DASH，OP5 默认打开选最新的 qn=80 条目。

| 指标 | 结果 |
|---|---|
| n / pass | **20 / 20** |
| min / p50 / p95 / max | 94.7 / **98.6** / **111.7** / 112.7ms |
| ≤2.5s | 20/20 |
| playurl / DASH / quality API / HTTP | **0 / 0 / 0 / 0** |
| netstat（首轮 1 次运行，40ms 轮询） | **0 条 TCP/UDP** |
| 实际缓存 | qn=80 DASH 视频 + `audio-30280` 音频；`dash=1` |

- 第一轮 20 次指标全过，但脚本未捕获第一次进程退出码，按 19/20 记；随后用 `-Wait` 干净重跑 20 次，20/20，采用后者。两次均 `qn=80 dash=1 playurl=0 http=0`。

### 4.6 OP6 本地回归

`scripts\bench-regression.ps1 -Suites startup,step1080`（Medium 副本；`media\proxy\proxy-V1.mp4`；热缓存；**不是** F1-A1/正式 T7.7）：

| 项 | p50 | p95 | 20/20 | 门槛 |
|---|---:|---:|---|---|
| P1 | 295.56ms | **311.87ms** | 是 | ≤1.5s |
| P2 | 357.18ms | **390.59ms** | 是 | ≤2.5s |
| P7 后退 | 0.26ms | **1.47ms**（max 47.41） | 是 | ≤80ms |
| P7 前进 | 0.27ms | **0.53ms**（max 12.94） | 是 | ≤80ms |

`bench-regression`：**PASS**，9 项检查 0 gating 失败、0 项超基线。

### 4.7 OP7 内存

| 场景 | 结果 | 门槛 |
|---|---:|---|
| 空闲（Medium 副本，`--memory-test --memory-mode empty --memory-seconds 10`） | max WS **63.2MB**，`pass=1` | ≤250MB |
| 在线 1080p30 播放（3/3 OB M2 峰值） | **204.5 / 204.6 / 206.6MB** | ≤800MB |
| OP4 L-1 5 分钟 | **121.9MB** | ≤800MB |

### 4.8 OP8 缓存配额

- 默认配额 **20GB**；可配置、LRU、占用 ≤ 配额。
- 本轮 `--online-cache-test`：**32/0 pass**；`--online-cache-manage-test`：**72/0 pass**；`ctest` 内 `lapian_online_cache_test` **396/0**（含音频键/目录/meta/配额/清空保护）。
- DEC-5 引用 T8.3/T8.8：300MB 配额 314,572,800B，占用 283,116,589 → **188,744,706B**（≤配额）；20GB 记账 `10,737,418,599 ≤ 21,474,836,480`。不真填满 20GB（DEC-5-A）。

---

## 5. DASH 双流 60s 音画 / seek / EOF

| 项 | OB-1 | OB-2 | OB-3 | 判定 |
|---|---:|---:|---:|---|
| 音画偏差 p50 | 8.3ms | 8.0ms | 10.0ms | — |
| 音画偏差 p95 | 17.6ms（0.51 帧） | 17.4ms（0.54 帧） | 20.0ms（0.55 帧） | ≤1 帧 pass |
| 音画偏差 max | 27.0ms | 24.5ms | 25.2ms | ≤1.5 帧 pass |
| 20 次 seek landing 失败 | 0 | 0 | 0 | pass |
| 20 次 seek PTS 失败 | 0 | 0 | 0 | pass |
| seek 后音频从当前帧起失败 | 0 | 0 | 0 | pass |
| EOF 停住 | 是 | 是 | 是 | pass |

- 口径同 T8.8：音频主时钟 vs 当前显示帧 PTS（帧中心）；p95 按每条单独算；DASH 1080P GOP=5s，seek 目标只取距前关键帧 ≤2s（P9 口径）。
- 5s GOP 点尾 seek 约 260ms，不属 P9 ≤2s 口径，记录不判失败。

---

## 6. 失败矩阵 / 续传 / 缓存配额（本轮 vs 引用）

### 6.1 本轮复跑

| 项 | 命令 | 结果 |
|---|---|---|
| 窗口级故障矩阵 | `--online-fault-test --fault-set t8.13-core`（注入 transport/PlayUrl，不联网） | **10/10 场景、92 项 0 失败**；UI gap max **74ms**、`over_200=0`；`unverified=1`（真实休眠） |
| 断网重试/已缓存可播/未缓存 seek 断网/看门狗/磁盘满/损坏重建/关窗/休眠注入/脱敏 | 同上 10 场景 | 全 pass；断网 624ms、失败后已缓存可播 4,177ms、未缓存 seek 4,445ms、看门狗 4,152ms、磁盘满 6ms、损坏重建 281ms、关窗 28/280ms、休眠注入 3,204ms、脱敏 222ms |
| 缓存/配额 | `--online-cache-test`；`--online-cache-manage-test` | **32/0**；**72/0** |
| 单测/故障单测 | `ctest` | **35/35**（含 `lapian_online_fault_test` 153 项、`lapian_online_cache_test` 396 项） |

### 6.2 引用历史证据（不冒充本轮新跑）

| 场景 | 证据 | 关键数字 |
|---|---|---|
| 断网失败 ≤5s | T8.13 `docs/perf/v2-online-faults.md` §3 | 3,981–4,155ms；错误可读；`.part` 保留；重开续传 |
| 网络无响应看门狗 | T8.13 | 4,121–4,137ms；4s 无写盘中止 |
| API 超时 | T8.13 | 4,515–4,529ms（真实 WinHTTP + 本机静默服务器） |
| 403 → 备份 | T8.13 / T8.5 | 备份切换 1 次、只下缺失字节、offset 307,200 |
| URL 过期 → 刷新 | T8.13 / T8.5 | 刷新 1 次后断点继续；刷新失败保留 `.part` |
| 缓存损坏 → 自动 Rebuild | T8.13 | 截断 1,069,188 vs 3,207,564 → Rebuild → ready 132ms；重开 cache-hit 52ms/0 请求；标记/记录保留 |
| 磁盘不足 | T8.13 | 5ms 可读；不写坏 `.part`；不崩 |
| 下载中杀进程续传 | T8.8 | `.part=5,242,880` → 续传 206×2；最终 26,682,935B，hash 与完整下载一致 |
| 4 读者 10min | T8.13 | ops 25,907、waits 522、`timeouts=0`、close 16ms、`deadlocks=0` |
| 缓存清空保留记录 | T8.7/T8.8 | 清空删除数据；在线记录/截图保留；重建回访恢复标记 |
| 只读 ACL | T8.3.1 | 75/0（本轮未重跑） |

- **真实 CDN 故障**（403/过期/断网逐场景真打）仍不是 M2 门必需；本轮只做正常路径真实网络 + 注入矩阵。**不把注入写成真实 CDN 通过。**
- 真实 Windows 休眠（S3/S4）未验证；窗口级 DASH 单流失败未跑（T8.13 离线 T12 已覆盖语义）。

---

## 7. 本地 O9 + P1/P2/P7 + F1–F8 + 内存

### 7.1 O9 零网络

- Medium 副本，本地打开 `media\测试.mp4`（1802 帧/1080p30），真实键盘驱动：打开→播放→步进→Ctrl+M 标记→S 截图→全屏→关窗。
- **0 TCP / 0 UDP**：9.1s 内 `netstat -ano` 轮询 **137 次**，匹配该进程的行 **0**；未加载 `winhttp/wininet/urlmon/Qt6Network/libcurl`；进程 exit 0。
- O9-A6：运行前后默认 `%LOCALAPPDATA%\拉片软件\cache` 与 `%APPDATA%\拉片软件\online` 文件集合 **0 新增**。
- `--key-test`（`media\测试.mp4`）：**77 项 0 失败**；同进程 marks=11、snapshots=1；2s 长按 27 步。
- 口径：netstat 40ms 轮询 + 模块检查，不是 ProcMon/ETW 全程抓包；与 T7.6/T8.6/T8.7 一致。

### 7.2 P1/P2/P7 / F1–F8

- P1/P2/P7 见 §4.6；P1/P2 是热缓存回归，不写 F1-A1/正式 T7.7 通过。
- `--key-test` 77/0 覆盖 F1–F7 的主要键位/焦点/标记/截图交互；`ctest` 35/35 覆盖 settings/banner/snapshot/timeline 等单测。
- **未重跑**：`--settings-test`（本轮该探针在两次进程持久化检查处卡住，未定位；不是已证实的 product fail）、`--banner-test`、`--dpi-test`、`--snapshot-test`（时间不足）。引用 T8.12/T8.13 的基线（settings/banner/dpi/snapshot 均通过）与 `ctest`；**不写 F1–F8 UI 全过**。

### 7.3 内存

- 空闲 63.2MB（§4.7）；在线 1080p30 峰值 ≤206.6MB；OP4 121.9MB。均低于门槛。

---

## 8. 网络审计 / 脱敏 / 导入表

| 项 | 结果 |
|---|---|
| dist 导入表 | `scripts\check-imports.ps1`：**20 文件通过**；无 `QtNetwork`/TLS/`winhttp`/`wininet`/`urlmon`；仅已知 `Qt6Core → WS2_32`（Qt 运行时固定链接，仅 winsock 初始化） |
| dist 体积 | **34.3MB / 25 文件**（P12 余量 65.7MB；本轮未重新打包，T8.16 负责安装器/隐私） |
| 在线主机 | `api.bilibili.com`（view/playurl/nav）+ CDN；CDN 不带 Cookie（T8.2/T8.8 证据；本轮日志只有 host/path，无 Cookie） |
| 日志脱敏 | 扫描本轮 `build\t8.14-m2` 日志：`SESSDATA=` 0、`deadline=` 0、`upsig=` 0、`auth_key=` 0、`SENTINEL` 0 |
| 缓存 meta | 不含 SESSDATA/Cookie/签名 URL（T8.3/T8.13 证据；本轮 M2/OP5 使用同一 `lapian-cache-1` schema） |

---

## 9. 残余风险、未验证与缺口（不冒充通过）

### 9.1 M2 期间复现的 `0xC0000374`（未定位/未复现）

- 2026-10-24 16:02:31，`lapian.exe` 在 OP1 OB-2 的一次尝试中崩溃；Windows Application Error：`0xc0000374`（heap corruption），faulting module `ntdll.dll` offset `0x118095`，WER report `3729837f-e799-4edb-ad03-87206bfbdce2`（bucket 2288353925148479318）。
- 触发上下文：该次运行先出现真实 `api.bilibili.com` resolve 超时（nav/view `status=0`，2,011/6,024ms），`resolve_result ok=0` 后 M1 探针 `QCoreApplication::exit(2)`；崩溃发生在进程退出路径。**不是 3/3/OP1–OP5 的正常路径**。
- 之后 10 次非法 BV resolve 失败、10 次 `--online-auth-test` fake timeout、10 次正常 M1 stress、60 次 OP1b 尝试（含 2 次 E2E exit1、1 次 resolve exit2）均**未复现**；`--online-fault-test` 10/10 也未复现。**未定位，不能写“无崩溃”。**
- M1 探针已加 `SetUnhandledExceptionFilter` 符号化栈（只探针进程，产品不安装）；若再复现可给出栈。已启用用户级 `LocalDumps`（临时调试设置，收尾移除）。

### 9.2 L-1 seed 卡住（未定位/未复现）

- 2026-10-24 16:21:43 起，L-1 首次 M2 seed（60s 播放、边下边缓存）在 `audio_started` 后无输出 >5 分钟；进程仍在、UI 线程 Wait，手动 kill。缓存 `.part` 已达到 total（58,054,247 / 8,657,239B）但 `complete=false`。
- 同缓存根用 20s 播放复跑：正常 finalize，`complete=true`；随后 OP4 300s 全缓存命中 17/0 pass。**未复现，未定位。**
- 与 T8.13 的“1 次未复现卡住”同类，但本轮是新观察；在 Beta/T8.16 前需要继续观察，不能写“已证明稳定”。

### 9.3 OP3 慢 landing 的 UI 响应

- OB-1/OB-2 landing 29/0；OB-3 慢 landing（1MB/s、57.6s 等待）落点 0，但 UI 心跳 max 36,089ms，随后 Ctrl+M/S 检查失败。OP3 的 DEC-2 判定项只有提示/取消/落点，均通过；**慢等待期间的 UI 响应是未解决缺口**，不写成 OP3 失败、也不写成“UI 全程流畅”。

### 9.4 未验证项

1. 真实 Windows 休眠/恢复（S3/S4）：只做 3s stall/读线程停顿等价注入。
2. 真实 CDN 403/过期/断网逐场景真打：注入 + T8.5/T8.8 历史真实 403/刷新。
3. 窗口级 DASH 单流失败：离线 T12 已覆盖语义，窗口级未跑。
4. `--settings-test` UI 持久化探针本轮卡住，未完成；F1–F8 的 banner/dpi/snapshot UI 探针未重跑。
5. T8.15 大会员 1080P60/4K、T8.16 打包/隐私/安装器、T8.6.1b 拖动预览：未做，不在 M2。

### 9.5 口径与样本

- OP4 用补充长样本 L-1（386s，29.97fps），不是 OB；OB 只有 96–213s，不能单段连续 300s。**不把 L-1 写进 3/3 OB。**
- OP1 的网络拖慢/超时尝试已单列；有效 20 次均 ≤5s；把 10.4s 拖慢计入仍 20/21 ≤5s。
- 60s 音画偏差是应用内时钟测量，不是外部录屏/专业同步仪；不外推为任意片源/声卡。
- 1080P 结论只来自 DASH qn=80；durl 1080p（S2 1/3）未作为保证，也未用于 M2 数字。
- 4K/1080P60 不在 M2；OB-2 的 29.412fps 只证明降帧提示正确，不证明 60fps 可用。

---

## 10. 回归汇总

| 项 | 结果 |
|---|---|
| `ctest` | **35/35** |
| `--online-m2-test` 3/3 | **34/0 ×3** |
| `--online-m1-test` 冷启动 | 60 次有效样本 + 额外尝试；见 §4.1 |
| `--online-m1-test` 缓存命中 | OP2 3/3 `28/0`；first frame 72/69/74ms |
| `--online-cache-test` / `--online-cache-manage-test` | 32/0；72/0 |
| `--online-fault-test` | 10/10 场景、92/0；UI gap 74ms/0 |
| `--key-test`（`media\测试.mp4`） | **77/0** |
| `bench-regression -Suites startup,step1080` | PASS；P1/P2/P7 见 §4.6；0 项超基线 |
| `check-imports` | 20 文件通过；无网络导入 |
| dist | 34.3MB/25（未重新打包；T8.16） |
| 本地空闲内存 | 63.2MB |
| 在线 1080p30 内存 | 204.5/204.6/206.6MB（M2）；121.9MB（OP4） |

---

## 11. 复现（最短）

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure

# 3/3 DASH（每条独立根）
build\release\bin\lapian.exe --online-m2-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --tag t8.14-ob1 --timeout-ms 120000 `
  --online-cache-root build\t8.14-m2\ob1\cache `
  --online-pictures build\t8.14-m2\ob1\pics `
  --online-records-root build\t8.14-m2\ob1\rec `
  --settings-path build\t8.14-m2\ob1\settings
# OB-2: BV1mA411m7pS / 1004967230；OB-3: BV1GJ411x7h7 / 137649199

# OP4：先用 --online-m2-test 种完整缓存，再
build\release\bin\lapian.exe --online-m2-test --m2-op4 --m2-play-seconds 300 `
  --bvid BV14Enb6LEDe --cid 42979362482 --timeout-ms 120000 `
  --online-cache-root build\t8.14-m2\op4\long\cache ...

# OP5：Medium 副本、默认 AppData；先 --m2-default-paths --m2-qn 80 种缓存，再
lapian.exe --online-m2-test --m2-op5 --m2-default-paths `
  --bvid BV1BMnv6jE5S --cid 42915922022 --timeout-ms 120000

# OP3：冷缓存 + 1MB/s 节流；落点加 --uncached-landing
build\release\bin\lapian.exe --online-m1-test --m1-throttle-bps 1048576 ...
```

- 测试根 `build\t8.14-m2\*`；日志/CSV 不入库。OP1 汇总：`build\t8.14-m2\op1b\op1b.csv`；OP5 日志：`build\t8.14-m2\op5\op5.log`。
- 默认 AppData / Medium 证据：`%LOCALAPPDATA%\Programs\lapian-t814\lapian.exe`（integrity=medium）。工作区 Low 只用于显式 `build\t8.14-m2\*`。

---

## 附录 A：T8.10 DASH 双输入历史证据（原文保留）

> 以下为 T8.10 的 DASH 双输入实现/测量记录（2026-10-24，非 M2 门）。保留作历史章节；M2 门判定以上文 §1–§11 为准。


### T8.10 DASH 双输入（2026-10-24）

- **任务**：[tasks-v2.md](../../tasks-v2.md) **T8.10**（M2 第 2 项）；依据 [spec-v1.0-delta.md](../spec-v1.0-delta.md) v0.4 的 O2/O3/O4/O6、OP1–OP7、DEC-2/DEC-4，[ADR-007](../adr/ADR-007-bilibili-online.md) D3/D4，[T8.9 证据](v2-online-quality.md)。
- **状态**：**T8.10 完成（不等同 M2 门）**。DASH 视频+音频双输入、共享音频主时钟、seek fan-out、单流 EOF、durl 回退、DASH 缓存命中已实现；**未做** T8.11 渐进索引/波形、T8.12 切档策略/降帧提示、T8.13 故障加固、T8.14 M2 门、T8.15 大会员条件验收、T8.16 打包、T8.6.1b 拖动预览。
- **基线**：`a3cf655` + T8.10 变更；**M1/M2 门、验收数字、OB 样本未改**。
- **一句话**：登录普通用户 3/3 用 `playurl(fnval=4048)` 的 1080P AVC（qn=80）+ AAC（30280）双流播放；首帧 322/369/365ms、60s 音画偏差 p95 17.1/18.2/22.0ms（≤1 帧）、20/20 seek 落点与 PTS 都精确、视频 EOF 正常停住、内存峰值 211.5/209.7/201.7MB、DASH 缓存命中 0 网络 45–46ms；`--force-durl` 回退 28/0。

---

## 1. 先做 spike（3 条真实 OB）

工具：`src/tools/dash_spike/main.cpp`（开发工具，不入分发；控制台）。命令：

```powershell
build\release\bin\lapian_dash_spike.exe --bvid BV1BMnv6jE5S --cid 42915922022 --prefetch-mb 12 --seek-seconds 20
```

| 问题 | 实测（OB-1/2/3） | 结论 |
|---|---|---|
| DASH 总字节 | 主 URL `Range: bytes=0-0` 均 **206 + Content-Range**：视频 31,057,770 / 25,381,371 / 69,759,161；音频 1,294,290 / 1,240,382 / 5,408,198；探测 16–92ms | 用 Range 探测，**不伪造 size**；服务端忽略 Range 时用 Content-Length 兜底 |
| init/index | `0-917`/`918-1245`、`0-996`/`997-1256`、`0-1008`/`1009-1544`；顺序下载 0..total 已覆盖，不需要单独拼接 | 不用单独请求 init/index；总字节探测只看响应头 |
| 两个 GrowingFormat | 视频/音频 `avformat_open_input` 均成功（10ms/0ms）；视频首帧 8–12ms；音频首包 PTS=0；容器 duration 110.466/95.566/212.261s；音频 AAC 48k/44.1k 立体声 | 不 remux 可行 |
| seek fan-out | 视频 `SeekToKeyframePts` 到 15s 落 15.000s；音频 `AudioDecoder::SeekTo` 20s 落 20.0000s | 双流各自 seek 可行 |
| EOF/时钟 | 音频容器时长比视频长 0–62ms；视频 EOF 由 `reachedEnd` 停止，音频随后 EOF，不阻塞 | 单流 EOF 不阻塞 |

**spike 抓到的关键 bug**：fMP4（DASH）的 `av_seek_frame` 会把“正好是关键帧”的时间戳落到**前一个关键帧**（5s GOP 实测早 5s）。`MediaSession::SeekToFrame` 按“从索引关键帧数 N 帧”返回，导致 seek 落点 PTS 比目标早一整个 GOP；帧号仍被写成目标帧，肉眼/旧断言看不出来。修复见 §3.4。

---

## 2. 实现

### 2.1 双输入路径

- `OnlineSession::Start` → 缓存命中（DASH 需要视频+音频两个条目）→ 否则 `StartResolveWorker`：
  1. `playurl(fnval=4048,qn=80,fourk=1)` 拿目录；
  2. `DefaultStream()`（AVC/非手动档）+ `BestAudio()`（最高带宽 AAC）；
  3. 两条流各做 `Range: bytes=0-0` 总字节探测；
  4. 成功 → 两个 `DownloadPlan`/`CacheKey`/`GrowingSource`/`DownloadJob`；失败 → 记录原因并回退 `playurl(fnval=0,qn=80,fourk=0)` 的 durl 单文件（M1 路径）。
- 视频与音频各自 `GrowingSource`、`DownloadJob`、`AVFormatContext`；`defer_finalize` 两个 job 都完成才落盘。
- ready 阈值：视频 300KB、音频 96KB（≈7–8s AAC），两者都到才发 ready，避免首帧后等音轨。
- 音频主时钟（IAudioClock）继续沿用；seek 时暂停视频与音频，落地后 `PlayFrom(当前帧媒体秒)` 让音频从当前帧起。
- `MediaSession::OpenGrowingWithOptions` 只把视频容器作为会话；音频 overlay 提供 `has_audio/stream_index=0/codec=aac`，`av_offset_us = -视频首帧 PTS`（OB-3 首帧 21ms → -21ms；OB-1/2 = 0）。
- `AudioEngine`/`WaveformGenerator` 用音频 `GrowingSource` + `audio-<id>` 路径，stream_index=0。
- `durl` 回退：`--force-durl` 可强制；产品在 DASH 目录/音频/总字节任一失败时自动回退，并记录 `DashFallbackReasonForTest()`。

### 2.2 音频缓存键（T8.9 留给本任务）

`lapian-cache-1` **schema 值不变**，新增可选字段：

| 字段 | 视频条目 | 音频条目 | 旧 meta |
|---|---|---|---|
| 目录 | `<root>\<bvid>\<cid>\<qn>-<codecid>\` | `<root>\<bvid>\<cid>\audio-<audio_id>\` | 同旧 |
| `kind` | 不写（缺省 video） | `"audio"` | 缺省 video |
| `audio_id` | 配套音频 id（DASH 时写；durl 不写） | 音频 id | 无 |
| `container` | `"dash"`（DASH 视频单流）；durl 不写 | 不写 | 无 |

- `CacheKey` 增 `kind`（默认 video）+ `audio_id`（默认 0）；`IsValidCacheKey`：音频只接受 `audio_id>0, qn=0, codecid=0`，视频拒绝 `audio_id>0`。
- `EntryDir`/`CacheKeyLabel`/`Scan` 认识 `audio-<id>`；旧目录/旧 meta 完全可读、可清、可重建。
- 当前会话保护：`MatchesProtectedKey` 在 keep 是视频键时同时保护同 bvid+cid 的音频条目；`ClearAll`/`CleanupOrphans`/`EvictToQuota` 都覆盖（`--online-cache-manage-test` 也加了音频保留用例）。
- 缓存命中要求视频+音频都 complete；缺音频时明确报“需要清空该视频缓存后重下”，不静默无声播放、不偷偷联网。
- DASH 缓存命中仍用两个 `GrowingSource`（完整文件 `data.mp4` 全部 Publish），复用同一双输入读路径，0 playurl/CDN/`.part`。

### 2.3 关键代码

| 文件 | 内容 |
|---|---|
| `src/app/OnlineSession.*` | resolve（DASH→总字节→回退 durl）、双 plan/source/job、双 ready、双 finalize、DASH 缓存命中、refresh 按流重取 |
| `src/online/CacheStore.*` | `CacheStreamKind`、`CacheKey.kind/audio_id`、`audio-<id>` 目录、meta kind/audio_id/container、保护/扫描/清理 |
| `src/online/DownloadTransport.h` / `WinHttpDownloadTransport.cpp` | `DownloadRequest.range_end`（`bytes=0-0` 探测；默认 -1 保持 T8.5 行为） |
| `src/media/MediaSession.*` | `GrowingOpenOptions`（dash/fps/duration/audio overlay）、`OpenGrowingWithOptions`、`av_offset_us`、seek PTS 校验 |
| `src/media/VideoDecoder.*` | `SeekToKeyframePts`（`avformat_seek_file` min=ts=max） |
| `src/app/MainWindow.*` | DASH ready 打开、音频/波形独立源、seek 重建带 overlay、DASH cache-hit、M2 hooks |
| `src/app/OnlineM2TestRunner.*` | `--online-m2-test` 真实窗口验收 |
| `src/app/main.cpp` | `--online-m2-test` / `--force-durl` / `--m2-*`；M1 回归可强制 durl |

### 2.4 seek 落点修复（核心）

- 根因：fMP4 的 `av_seek_frame` 落在目标关键帧**前一个** GOP；`SeekToFrame` 从索引关键帧数 `frame-keyframe` 帧，于是返回早一个 GOP 的 PTS（OB-1 frame 3311 期望 110.366688s，实际 105.366688s；frame 1103 期望 36.766688s，实际 31.766688s），帧号却写成目标帧。
- 修复：`VideoDecoder::SeekToKeyframePts` 用 `avformat_seek_file(stream, keyframe_pts, keyframe_pts, keyframe_pts, BACKWARD)`；`SeekToFrame` 优先用它，失败退回 `SeekToPts`；`decode_range` 在返回目标帧前校验 `|info.pts_us - index.PtsOfFrame(frame)| ≤ 1s`，不一致就失败走从 0 解码，绝不把错误帧当目标帧。
- 验证：`--online-m2-test` 20/20 seek **PTS exact**；点尾 seek `expected=110366688 actual=110366688`；EOF 帧号 3313（不再漂到 3463）；`ctest` 32/32、本地 P7 0.85/0.35ms、key-test 77/0。

---

## 3. 验收（真实 3 条 OB，登录普通用户 `vip_status=0`）

命令（每条一份独立测试根）：

```powershell
build\release\bin\lapian.exe --online-m2-test `
  --bvid <BV> --cid <cid> --tag t8.10-final-<ob> --timeout-ms 30000 `
  --online-cache-root build\t8.10-final-<ob>\cache `
  --online-pictures build\t8.10-final-<ob>\pics `
  --online-records-root build\t8.10-final-<ob>\records
```

### 3.1 3/3 首帧/播放

| 样本 | 首帧 ready | qn | 视频流 fps（API/容器） | 音频 | 视频总字节 | 音频总字节 | 索引帧数 |
|---|---|---|---|---|---|---|---|
| OB-1 | **322.2ms** | 80 | **30.000 / 30** | 30280 AAC | 31,057,770 | 1,294,290 | 3314 |
| OB-2 | **369.0ms** | 80 | **29.412 / 30**（API 与容器不同，见 §5） | 30280 AAC | 25,381,371 | 1,240,382 | 2867 |
| OB-3 | **365.1ms** | 80 | **25.000 / 25** | 30280 AAC | 69,759,161 | 5,408,198 | 5306 |

- 3/3 `dash_dual=1`、`qn=80`、音频 id/字节均有效；`playurl_requests=0`（DASH 路径不请求 durl）。
- 默认 1080P AVC；60fps/4K 仍未出现在普通用户目录（T8.9 结论未变）。

### 3.2 60s 音画偏差（≤1 帧）

口径：每 ~20ms 采样 `IAudioClock` 与当前显示帧 PTS（帧中心 = PTS + 半帧）；`drift = audio_clock − frame_center`。音频主时钟驱动视频帧选择。

| 样本 | n | p50 | p95 | max | 判定 |
|---|---|---|---|---|---|
| OB-1 | 2844 | 8.7ms | **17.1ms** | 25.4ms | p95 0.51 帧，max 0.76 帧 |
| OB-2 | 2836 | 8.0ms | **18.2ms** | 26.0ms | p95 0.54 帧，max 0.76 帧 |
| OB-3 | 2850 | 9.0ms | **22.0ms** | 27.5ms | p95 0.55 帧，max 0.69 帧 |

3/3 `av_drift_p95_le_1frame` 与 `av_drift_max_le_1_5frame` 均通过；未见随时间累积（60s 内 p50 稳定）。OB-3 视频首帧 21ms 的 `av_offset_us=-21000` 已生效，没有出现“音频早 21ms 逐渐漂移”。

### 3.3 20 次已缓存 seek（落点 0）

目标随机分布在已缓存区，**只取距前关键帧 ≤2s**（P9 口径；DASH 1080P GOP=5s，长 GOP 不在本门槛）。

| 样本 | landing failures | PTS failures | p50 | p95 | max | 音频从当前帧起 failures |
|---|---|---|---|---|---|---|
| OB-1 | **0/20** | **0/20** | 77.3ms | 129.9ms | 170.3ms | 0/20 |
| OB-2 | **0/20** | **0/20** | 77.9ms | 110.3ms | 124.8ms | 0/20 |
| OB-3 | **0/20** | **0/20** | 76.5ms | 105.0ms | 137.4ms | 0/20 |

- landing = `当前帧号 == 目标帧号`；PTS = `|当前 PTS − 索引 PTS| ≤1ms`。
- 每次 seek 后播放 250ms，`AudioClock ≈ 目标 PTS + 0.25s`（容差 0.55s），验证不是旧位置/从头播。
- 5s GOP 点尾（距关键帧 149 帧）seek 约 260ms；**不属于 P9 ≤2s 口径**，记录不判失败。

### 3.4 单流 EOF

跳到 `last-2` 帧播放到结尾：3/3 在 ≤6s 内 `OnlinePlaying=false`，帧号停在末帧（OB-1 3313、OB-2 2866、OB-3 5305），PTS 与索引一致；未出现音频仍在播放导致播放器不结束/死锁。

### 3.5 内存 / 关窗 / 缓存命中

| 样本 | 峰值 working set | 关窗 | 缓存命中 ready | 0 网络（playurl/DASH/quality API） |
|---|---|---|---|---|
| OB-1 | **211.5MB** | 22ms | **46.2ms** | 0/0/0 |
| OB-2 | **209.7MB** | 23ms | **45.2ms** | 0/0/0 |
| OB-3 | **201.7MB** | 21ms | **45.0ms** | 0/0/0 |

- OP7 在线 1080p 内存 ≤800MB：3/3 通过（余量 >588MB）。
- 缓存命中仍是 DASH 双输入（`dash=1`、qn=80），两个完整 `data.mp4` 通过 `GrowingSource` 全量 Publish；0 playurl/CDN/`.part`。

### 3.6 durl 回退（M1 不退化）

- `--online-m2-test --force-durl`（OB-1）：8/0，`dash=0`、qn=64、首帧 669ms、播放推进；`playurl_requests=1`。
- `--online-m1-test --force-durl`（OB-1）：**28/0**；首帧 619ms、P7 p95 25ms、已缓存 seek p95 43ms、UI gap 57ms、未缓存提示检查保留。
- 不强制 durl 时的 `--online-m1-test`（DASH 路径）：**25/0**；首帧 330ms、P7 p95 68ms、已缓存 seek p95 95ms、UI gap 102ms；3 个“未缓存”检查因 31MB 在 2.7s 内下完、已无未缓存区而显式跳过（不是失败）。

---

## 4. 回归

| 项 | 结果 |
|---|---|
| `ctest` | **32/32**（31.80s） |
| `lapian_online_cache_test` | **396/0**（新增音频键/目录/meta/清空保护/配额保护/坏 kind 用例） |
| `lapian_online_cache_manage_test` | 通过（新增当前会话音频保留） |
| `--online-cache-manage-test` | workspace **72/0**、`--ui` **98/0** |
| `--online-quality-test` 3 条 | 各 27/0（总 39/0），3/3 pass；菜单文案改为“T8.12 切档”（不再写 T8.10 未接入） |
| `--online-quality-test --ui` | **21/0**；当前 DASH 1080P 勾选，其它档置灰写明 T8.12 |
| 本地 `--key-test` | **77/0**（`media\测试.mp4`；proxy-V1 只有 719 帧，不满足 `last-index>1000`，不是回归） |
| 本地 P1 | 291.41 / 293.89 / 293.74ms |
| 本地 P2 | 371.70 / 350.17 / 341.10ms |
| 本地 P7 step | 后退 p95 **0.85ms**、前进 **0.35ms**；内存 272.8MB |
| `dist` | 未重新打包；仍 34.02MB/25 文件（打包属 T8.16） |

---

## 5. 已知缺口 / 未验证

1. **OB-2 的 API fps 与容器/索引 fps 不一致**：API `frameRate=29.412`，fMP4 容器 `avg_frame_rate=30/1`，索引 2867 帧 / 95.566s = **30.0fps**。T8.10 记录 API 值用于展示/证据，帧号数学用容器值（否则总帧数会少 56 帧）。T8.12/DEC-4 需要明确“实际 fps”口径。
2. **DASH 无 size、只探测主 URL**：3/3 主 URL `206` 成功；备份 URL 的 Range 总字节未逐条测（T8.13 故障切换时再验）。服务端忽略 Range 的 Content-Length 兜底只有离线/合成路径，没有真实 CDN 反例。
3. **缓存命中要求视频+音频都完整**：目前 LRU 是条目级，理论上可能只淘汰音频；此时 T8.10 明确报“缓存不完整，请清空该视频缓存后重下”，不自动补下音频（T8.13 加固）。
4. **多音轨/杜比/FLAC 未做**：只选 `BestAudio()` 一条 AAC；`dash.dolby`/`flac` 未解析。
5. **无音轨的 DASH-only 样本未验**：目录无音频时回退 durl；若该稿件也没有 durl，则失败。OB 样本 3/3 都有音频/ durl。
6. **未缓存 seek 仍按 O4 等待**：T8.10 只验已缓存区落点；未缓存区提示/取消/落点由既有 T8.6 路径覆盖，未在 5s GOP 上重跑 20 次。
7. **60s 音画偏差是应用内时钟测量**：不是外部录屏/专业同步仪；p95/max 已记录，不能外推为“任意片源/任意声卡 ≤1 帧”。
8. **默认 AppData / Medium 副本未在 T8.10 重跑**：所有 M2 运行使用工作区显式测试根（Low integrity 可写）；T8.16 安装版默认路径才验证 `%LOCALAPPDATA%`。
9. **仅 OB-1/2/3、普通用户**：大会员 1080P60/4K 仍未验（T8.15）；`--online-m2-test` 为 GUI 探针，无无 GUI 版本。
10. **T8.11 未做**：索引/波形仍是“缓存增长后重扫”的 T8.6 策略；60s 测试里下载很快完成，未覆盖“边下边索引”的极端慢网。

---

## 6. 复现（最短）

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure

build\release\bin\lapian.exe --online-m2-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --tag t8.10-final-ob1 --timeout-ms 30000 `
  --online-cache-root build\t8.10-final-ob1\cache `
  --online-pictures build\t8.10-final-ob1\pics `
  --online-records-root build\t8.10-final-ob1\records
# OB-2: BV1mA411m7pS / 1004967230；OB-3: BV1GJ411x7h7 / 137649199

# 回退
build\release\bin\lapian.exe --online-m2-test --force-durl --bvid BV1BMnv6jE5S --cid 42915922022 ...
build\release\bin\lapian.exe --online-m1-test --force-durl --bvid BV1BMnv6jE5S --cid 42915922022 ...
```

`--m2-play-seconds` / `--m2-seeks` 可缩短 smoke（正式数字用默认 60/20）；`--m2-cache-hit-only` 只验已有完整缓存的 0 网络/首帧。
