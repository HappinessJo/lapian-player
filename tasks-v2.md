# 拉片软件 v2 在线模式任务拆分（tasks-v2.md）

- **版本**：v0.24（2026-10-24 T8.13 网络异常/并发加固完成（M2 第 5 项，非门）；下一任务 **T8.14（M2 门）**；M1/M2 门与数字未改）
- **状态**：**S3 定稿**；**T8.1/T8.2/T8.2.2/T8.3/T8.4/T8.5/T8.6/T8.7/T8.8 完成**；**T8.6.1a 完成 + 实机回归 hotfix + 用户实机确认（非门）**；**T8.3.1 缓存管理完成（非门）**；**T8.9 DASH 解析与档位完成（M2 第 1 项，非门）**；**T8.10 DASH 双输入完成（M2 第 2 项，非门）**；**T8.11 渐进索引/波形/总帧数完成（M2 第 3 项，非门）**；**T8.12 档位策略/手动切档/降帧提示完成（M2 第 4 项，非门）**；**T8.13 网络异常/并发加固完成（M2 第 5 项，非门）**；**M1 门已通过**（T8.8：3/3 durl 实际 ≥720P、OP1/OP2/OP3/OP5、失败矩阵、续传、DEC-5、本地 O9）；下一任务 **T8.14（M2 门）**；新增 **T8.6.1a/T8.6.1b/T8.3.1 非门后续**；DEC-1…**6** 已拍板；M1/M2 已锁定；B 条件验收保留（非 MVP 门）。
- **依据**：[spec-v1.0-delta.md](docs/spec-v1.0-delta.md) v0.4、[plan.md](plan.md) v2 章节、[ADR-007](docs/adr/ADR-007-bilibili-online.md)、[v2-online-spike.md](docs/perf/v2-online-spike.md)、[product-v2-vision.md](docs/product-v2-vision.md)、[T8.1 证据](docs/perf/v2-online-resolve.md)、[T8.3 证据](docs/perf/v2-online-cache.md)、[T8.4 证据](docs/perf/v2-online-growing.md)、[T8.5 证据](docs/perf/v2-online-m1.md)、[T8.2.2 证据](docs/perf/v2-online-qr.md)、[T8.7 证据](docs/perf/v2-online-cache-hit.md)、[T8.9 证据](docs/perf/v2-online-quality.md)、[T8.10 证据](docs/perf/v2-online-m2.md)、[T8.11 证据](docs/perf/v2-online-index.md)、[T8.12 证据](docs/perf/v2-online-quality.md)、[T8.12 交接](docs/handoff-2026-10-24-t8.12.md)、[T8.13 证据](docs/perf/v2-online-faults.md)、[T8.13 交接](docs/handoff-2026-10-24-t8.13.md)
- **范围**：只覆盖在线模式。v1 任务台账 `tasks.md` **只读**；本文件不重复 T0–T7，不改 v1 验收值。
- **硬约束**：4K / 1080P60 / 720P60 **不进任何 M1/M2 验收项**；B 另设 T8.15 独立条件验收（非门）；未缓存区不承诺 P7/P9；本地模式零网络不退化；Cookie 不入库/不回显/不写日志。

## 使用规则

1. 一个任务 = 一次可审查变更（一个提交序列/PR）；不出现“开发整个在线播放器”。
2. 每个任务必须有**独立验收**、**证据落点**、**明确依赖**；证据默认放 `docs/perf/` 或 `docs/handoff-*.md`。
3. 性能任务必须带数字：来自 delta §3 / OP 表；DEC-2 已拍板值直接引用，不把未实测写成已通过。
4. 任务只依赖列出的前置；跨里程碑依赖用 ID 标注。
5. 遇到 spec/ADR 冲突：先写 delta 版本记录或 ADR 评审记录，不在实现里静默改语义。
6. 每个任务完成时：`ctest` 通过、`dist` 可构建、本地模式回归（至少启动/打开/步进）、交接已写。
7. 凭据只放 `.secrets/` 或 DPAPI 存储；测试日志不得出现 SESSDATA/签名 URL。

## 最小闭环与依赖顺序

**M1 最小闭环**：
`T8.0 → T8.1 → T8.3 → T8.4 → T8.5 → T8.6 → T8.7 → T8.8`
`T8.1 → T8.2 → T8.5`

- T8.2 与 T8.3 可并行；T8.4 只依赖 T8.3；T8.5 依赖 T8.2（HTTP）与 T8.4（写入/发布）。
- **T8.2.2（扫码登录，DEC-6）不在 M1/M2 门内**：只依赖 T8.2（已完成），排在 T8.4 之后、T8.6 之前（可与 T8.5 并行）；失败不得影响 M1/M2 判定。
- T8.6 是第一次“真实窗口跑通”；T8.7 补缓存命中与记录；T8.8 是 M1 门（已完成，**M1 通过**，见下）。
- **T8.6.1a/T8.6.1b/T8.3.1 不在 M1/M2 门内**：它们是 T8.6 后根据真实使用反馈补的非门任务，排在 M1 后（T8.6.1b 依赖 T8.11），不得挤进 T8.7/T8.8 验收或推迟 M1。

**M2**：
`T8.8 → T8.9 → T8.10 → T8.11 → T8.12 → T8.13 → T8.14`

- T8.9 只做解析/档位；T8.10 才接双输入；T8.11 补渐进索引/波形；T8.12 补策略/提示；T8.13 加固（已完成）；T8.14 是 M2 门。
- T8.15 可并行（非门）；T8.16 在 M2 后（非 MVP 门）。

```text
M1  T8.0 → T8.1 → T8.3 → T8.4 → T8.5 → T8.6 → T8.7 → T8.8
              └→ T8.2 ───────────┘
M2  T8.9 → T8.10 → T8.11 → T8.12 → T8.13 → T8.14
         └→ T8.15（可选）                    T8.16（M2 后）
```

---

## M1：durl 720p 单文件闭环

| ID | 目标与范围 | 依赖 | 验收（单测/手测/性能） | 证据/产物 | 风险 |
|---|---|---|---|---|---|
| **T8.0**（完成 2026-10-23） | S3 收口：DEC-1…DEC-5 拍板落地；ADR-007 附条件接受；delta v0.3/tasks v0.3；锁定 M1 范围 | S3 文档 | 5 项决策已记录；ADR-007 评审记录已写；delta/tasks v0.3；tasks ID 不变；**不写代码** | 决策表（delta §9.1）；ADR 评审记录；`docs/handoff-2026-10-23-s3.md` | 无 |
| **T8.1**（完成 2026-10-23） | 解析层：BV/URL 归一化与本地校验；`view` 元数据（标题/UP/时长/分P）；错误码映射；回显确认；未登录提示 | T8.0 | 单测：BV/URL 解析、分P、非法输入；手测：OB-1/2/3 元数据；OB-5 五类失败码；非法输入 ≤100ms 且 0 网络；串片样本必须回显标题/时长；解析取消 ≤100ms（DEC-2 已拍板） | `--online-resolve-test`；`docs/perf/v2-online-resolve.md` | API 变动；串片；多P 歧义 |
| **T8.2**（完成 2026-10-23） | HTTP 栈自检 + 凭据：`IHttpClient` 抽象；WinHTTP 优先、libcurl 备选；带 SESSDATA 的 `nav` 自检；DPAPI 存储/清除；日志脱敏；-101 映射；DEC-3 时机 | T8.1 | **门：至少一种产品实际栈 nav `code=0/isLogin=true`**（WinHTTP 通过：5 次 161–202ms，`cookie_sent=1`，`vip_status=0`）；否则停线重决策；磁盘无明文；哨兵 Cookie 不出现在日志/弹窗；清除后按匿名；超时/取消可用；TLS 校验开启 | `--online-http-selfcheck`；`docs/perf/v2-online-http-selfcheck.md`；DPAPI 单测（`lapian_online_auth_test` 104/0）；`docs/handoff-2026-10-23-t8.2.md` | 两种栈都 -101；代理/TLS；libcurl 体积/许可（未构建，T8.16 再定） |
| **T8.2.2**（DEC-6 拍板；非门；完成 2026-10-23，真实扫码未验证） | 扫码登录：`passport.bilibili.com` 二维码 `generate`/`poll`；二维码本地渲染（vendored MIT 单文件，不引入 WebEngine/新 DLL）；状态机（等待/已扫待确认/过期/成功/失败）、180s 过期可刷新、可取消；成功只提取 SESSDATA → 复用 `CredentialStore`（DPAPI）+ nav 自检；失败回退 SESSDATA 粘贴（含剪贴板一键/图文引导）；**两入口并存，粘贴不是过渡、不降级** | T8.2 | 单测（fake HTTP）：4 态、过期、取消、坏 JSON、412/429 风控码、只提取 SESSDATA、日志无凭据；`--online-qr-test`（无 GUI、不写真账号）；真实扫码 1 次（用户手机）成功且 nav 通过；过期/取消 ≤100ms；失败不显示假登录态；本地模式仍 0 请求。**结果**：`lapian_online_qr_test` 115/0；`--online-qr-test` 19/0、`--ui` 21/0、dist 同；取消 16ms/0ms；QR v4/33 modules/246px/quiet 4；真实 generate/poll 字段实测（key 32、poll 86101、cookie_sent=0）；**真实扫码未验证（无手机），不写 O8-A8**；日志哨兵 0；本地零网络 0 请求 | `--online-qr-test`；`docs/perf/v2-online-qr.md`；`docs/handoff-2026-10-23-t8.2.2.md`；`lapian_online_qr_test` | 风控 412/429；网页接口变动；二维码依赖许可；真实扫码样本有限 |
| **T8.3**（完成 2026-10-23） | 缓存存储与配额：缓存根/目录结构；`meta.json`；`.part`→`.mp4` 原子改名；20GB 可配置；LRU；占用/清空；空间预检；孤儿清理；续传 | T8.1 | 单测（300MB 配额）：LRU 顺序、当前不淘汰、总占用 ≤配额；杀进程后续传；损坏检测/重建；清空保留在线记录；磁盘满 ≤5s 可读错误；路径/配额跨会话。**结果**：`lapian_online_cache_test` 366/0/0 skip；300MB 占用 283,116,589→188,744,706（配额 314,572,800、只淘汰最旧 90MB）；20GB 记账 21→16→10 GiB（`10,737,418,599 ≤ 21,474,836,480`）；续传 5,242,880；8 类损坏不可播放；清空保存 records sentinel；空间预检 0.082–0.115ms；长路径 301/312 字符；meta 无明文 | `lapian_online_cache_test`；`--online-cache-test`（32/0，verdict=pass）；`docs/perf/v2-online-cache.md`；`docs/handoff-2026-10-23-t8.3.md` | 文件锁/杀软；长路径；磁盘空间计算 |
| **T8.4**（完成 2026-10-23） | 可增长读层（关键路径）：`GrowingSource` + 每读者 `Cursor`/AVIO；已知总大小；阻塞读/发布；取消；EOF/错误语义；多读者；统计 | T8.3 | 0.3MB 可出首帧；100 次 seek 落点 0；已缓存 seek 零阻塞；未缓存等待 ≈剩余/速率（记录）；取消 ≤100ms；4 读者×10min 无死锁/丢唤醒；关窗 ≤1s；内存有界；无 busy loop。**结果**：0.3MB→首帧 1920×1080（open 9ms/首帧 16ms/实读 131072B）；100/100 seek 落点 0、已缓存等待 0；未缓存等待 2073ms vs 预期 1750ms（+18.5% ≤30%）；取消 0.15ms/0.24ms；4 读者 10min `deadlocks=0/lost_wakeups=0`（短版 wait 120 次、close 3ms、WS +9.77MB）；busy CPU 0ms/5s；EOF/EIO/EXIT 与淘汰 busy 通过；`ctest` 26/26、cache 32/0、auth 54/0 | `lapian_growing_source_test`（157/0）；`--online-growing-test`（22/0，verdict=pass）；`docs/perf/v2-online-growing.md`；`docs/handoff-2026-10-23-t8.4.md` | 死锁/丢唤醒；FFmpeg EOF/seek；锁竞争；int64 |
| **T8.5**（完成 2026-10-23；M1 3/3 blocked） | durl 取流：`playurl(fnval=0)`；M1 档位（720p/可用最高）；顺序下载；Range 续传；3 备份 URL；重试/退避；URL 过期刷新；吞吐统计；暂停/取消；完成改名 | T8.2、T8.4 | 3/3 登录样本 720p；首 0.3MB 出首帧；断网 5s 后续传；杀进程后续传；模拟 403/过期刷新；备份切换；无重复/损坏字节；产物可被 ffprobe 正常打开。**结果**：OB-1/OB-3=720P（qn=64）；**OB-2 实际 360P（qn=16；qn=64/80/112 均为 16）→ 3/3 720P blocked，不写通过**；三样本均完成下载+首帧（8–9/10–14ms）+ffprobe；真实取消 7ms、`.part` 续传 206、`_Exit` 后 5,242,880B 续传；真实 403 故障注入：备份切换 1 次 / 刷新 1 次均完成且 hash 一致；`lapian_online_fetch_test` 94/0、`--online-fetch-test` 46/0、`ctest` 27/27 | `docs/perf/v2-online-m1.md`；`docs/handoff-2026-10-23-t8.5.md`；故障日志 | durl 档位不可预期；Range 被忽略；CDN 限速；120min 过期；M1 3/3 前提未复现 |
| **T8.6**（完成 2026-10-23；M1 3/3 仍 blocked） | 在线会话集成：`OnlineSession` 状态机；缓冲 UI/进度/取消；已缓存 seek/步进；未缓存等待；波形渐进；错误提示；关闭；**本地路径不动** | T8.4、T8.5 | E2E 3 条：输入→确认→首帧→Space→PgUp/PgDn→seek→波形→Ctrl+M→S→关窗；已缓存 P7 p95 ≤80ms（DEC-2 已拍板）；未缓存显示等待并可取消；UI 停顿 >200ms = 0；本地零网络 + P1/P2/P7 不退化。**结果**：3 条真实 E2E 全过（OB-2 qn=16 → quality=blocked）；P7 p95 20/20/25ms；seek p95 79/59/97ms；UI gap 60/61/62ms；冷缓存首帧 20 次×3 条 p95 624/725/459ms，20/20≤5s；缓冲取消 19ms、缓冲耗尽自动暂停/恢复 pass；关窗 18–645ms；`ctest` 29/29；本地 P1/P2 290.68/347.43ms、P7 66.59ms；dist 33.57MB/25；M1 3/3 720P 仍 blocked（OB-2 360P），本任务不判 M1 | `--online-m1-test`；`--online-buffer-test`；`lapian_online_m1_test`（48/0）；`docs/perf/v2-online-session.md`；`docs/handoff-2026-10-23-t8.6.md`；截图 `build/t8.6-online-pictures/snapshots/` | 引擎 EOF/seek 假设；路径-only 媒体层；部分索引；UI 线程阻塞；完成改名 vs reader；波形阻塞 |
| **T8.7**（完成 2026-10-23；M1 3/3 仍 blocked） | 缓存命中 + 在线记录：断网按缓存打开（0 请求）；`lapian-online-1` 标记/备注；截图目录/命名/`index.jsonl`；清缓存保留记录；换档映射 | T8.3、T8.6 | 断网打开同 BV：0 TCP/UDP、首帧 ≤2.5s（DEC-2 已拍板）；标记/备注/截图恢复；清缓存后记录仍在；重建缓存后记录可回访；损坏记录不阻塞播放。**结果**：`OnlineSession` 命中 `CacheStore::OpenComplete` 直接打开 `data.mp4`（0 playurl/CDN/`.part`）；`lapian-online-1` 存 `%APPDATA%\拉片软件\online\<bvid>-<cid>.lapian.json`；截图 `Pictures\拉片软件\<bvid>\` + `index.jsonl`；`--online-offline-test` workspace 45/0、真实缓存 44/0、Medium 默认 AppData 31/0×3（首帧 74–84ms，netstat 0 TCP/UDP，无 winhttp/wininet/urlmon）；`lapian_online_records_test` 52/0；M1 3/3 720P 仍 blocked（OB-2 360P），本任务不判 M1 | `--online-offline-test`；`lapian_online_records_test`；`docs/perf/v2-online-cache-hit.md`；`docs/handoff-2026-10-23-t8.7.md` | 隐式 API 调用；目录权限；截图命名冲突 |
| **T8.8**（完成 2026-10-23；**M1 通过**） | **M1 门**：全量验收 + 报告 + 交接；决定是否进 M2 | T8.1–T8.7 | 3/3 720p；OP1/OP2/OP3/OP5 按 DEC-2 已拍板值达标；失败矩阵（O1/O7）通过；缓存配额按 DEC-5 已拍板；本地 O9 全过；无 P0/P1 缺陷；ADR-007 附条件接受已记录。**结果**：请求矩阵证明 OB-2 的 360P 是 qn=64 导致（qn=80/112/120 → 80/1080P，fourk 无影响）；最小修正 M1 durl 请求 qn 64→80、fourk=0、刷新同档；3/3 实际 quality=64/**80**/64，产物 ffprobe pass；E2E 29/0×3；OP1 p95 648/532/527ms（20/20×3）、OP2 p95 34/51/34ms（20/20×3）、OP3 提示/取消 0ms + 落点 0（3/3）、OP5 p95 95ms + 0 请求（20/20）；杀进程续传 5,242,880B/hash 一致；清缓存保留记录 45/0；DEC-5 300MB→188,744,706B、20GB 记账 10,737,418,599 ≤ 21,474,836,480；本地零网络、P1 342.92/P2 376.14/P7 64.79ms、`ctest` 30/30、dist 33.69MB/25；**未进 M2** | `docs/perf/v2-online-m1.md` §11–§17；`docs/handoff-2026-10-23-t8.8-m1.md` | 网络方差；durl 档位不可预期；不得写 1080P 保证 |

**M1 门（硬条件）**：

1. 普通用户登录后，3 条 `OB` 样本 `durl` 720p 全部可播（S2：登录后 3/3 均 ≥720p）。
2. 在线起播按 OP1；已缓存 P7 按 OP2；未缓存行为按 OP3；缓存命中按 OP5。
3. 本地模式零网络 + P1/P2/P7 不退化（O9）。
4. 失败矩阵（格式/不存在/不可见/未登录/断网/磁盘满）5s 内可读，不崩。
5. 缓存/记录可恢复：杀进程续传、清缓存不丢记录。
6. **没有** 4K/1080P60 验收项；**没有** 未缓存区 80ms 承诺。
7. **扫码登录（T8.2.2）不是 M1 门**：M1 的“登录后 3/3 720p”用 SESSDATA 粘贴即可满足；T8.2.2 失败/未做不影响 M1 判定。

> **2026-10-23 T8.5 复测记录（不改本门）**：OB-2（`BV1mA411m7pS`/cid `1004967230`）的 `durl` 在请求 qn=64/80/112 时实际都返回 `quality=16`（360P）；OB-1/OB-3 为 720P。因此第 1 条“3/3 720p”的 S2 前提本次未复现，T8.5 如实写 **blocked**，不伪造；是否调整 M1 门由 T8.8/用户决定，本文件未改门与数字。
>
> **2026-10-23 T8.6 复测记录（不改本门）**：真实窗口 3 条 E2E 均通过；OB-2 仍为 qn=16/640×360，因此 T8.6 仍写 **M1 3/3 720P blocked**，不把“能播/能操作”当 M1 通过。T8.6 的冷缓存首帧、P7/seek、缓冲/取消、关窗等数字见 `docs/perf/v2-online-session.md`；M1 门仍由 T8.8 判。
>
> **2026-10-23 T8.8 门审计（本门通过；门/数字/样本未改）**：qn/fourk 矩阵 20 单元证明 OB-2 的 360P 来自请求 `qn=64`：`qn=64`（fourk=0/1）→16，`qn=80/112/120`（fourk=0/1）→80（1080P）；`fourk` 无影响。最小修正为 M1 durl 请求 `qn=80`、`fourk=0`、刷新同档。复测：OB-1/2/3 实际 qn=64/**80**/64、ffprobe pass；OP1 冷缓存 p95 **648/532/527ms**（20/20×3）、OP2 已缓存 ±1 p95 **34/51/34ms**（20/20×3）、OP3 提示/取消 0ms + 落点 0（3/3）、OP5 断网模型 p95 **95ms** + 0 请求（20/20）；失败矩阵、杀进程续传、清缓存保留记录、DEC-5、本地 O9 通过。**判定 M1 通过**；未把 durl 1080P 写成保证，未进 M2。

---

## M2：DASH 双输入 1080p

| ID | 目标与范围 | 依赖 | 验收（单测/手测/性能） | 证据/产物 | 风险 |
|---|---|---|---|---|---|
| **T8.9**（**完成 2026-10-24**） | DASH 解析与档位：`playurl(fnval=4048)`；`dash.video[]/audio[]`；qn/codecid/fps/带宽/size/init/index；备份 URL；AVC 默认；档位顺序；4K 仅手动；档位菜单；`quality` 字段不可信 | T8.8 | 3 条档位矩阵与 S2 一致（登录普通用户 1080P30 3/3）；普通用户 60/4K 不可得记录在案；**大会员档按 O11/T8.15 条件验收，不写成 MVP**；1080P60/4K 进菜单；HEVC-only 60fps 只手动；错误 qn 不崩；音轨存在；不把 durl 1080p 1/3 当保证。**结果**：3/3 `dash.video[]` 有 1080P AVC qn=80（fps 30.000/29.412/25.000，与 S2 一致）；默认 AVC；`quality` 撒谎复现（匿名 qn=80 → quality=64，dash 只有 16/32），档位只信 `dash.video[]`；普通用户 116/74/120 3/3 不可得（请求 116/120 仍返回 80）；1080P60/4K 只在合成大会员目录进菜单、标仅手动/可用档、置灰并写 T8.10；HEVC-only 60fps `DefaultQn=0`；qn=116/0/9999 不崩；3/3 有 3 条 AAC（含 HE-AAC）；durl(qn=80) 1080p 仅 1/3；`lapian_online_dash_test` 117/0、`ctest` 32/32、`--online-quality-test` 3×27/0 + `--ui` 21/0、M1 E2E 28/0（first_frame 614ms/UI gap 53ms）、缓存管理 72/0+98/0、key-test 77/0、dist 34.02MB/25；**未判 M2** | `--online-quality-test`（含 `--ui`/`--odd-qn`/`--anonymous`）；`lapian_online_dash_test`；`docs/perf/v2-online-quality.md`；`docs/handoff-2026-10-24-t8.9.md` | API/WBI 变动；编码只有 HEVC/AV1；档位撒谎；状态栏 `QPushButton::setMenu` 绘制阻塞（已规避） |
| **T8.10**（**完成 2026-10-24**） | DASH 双输入：视频+音频各自 `GrowingSource`/`DownloadJob`/`AVFormatContext`；共享时钟；seek fan-out；首帧/首包偏移；单流 EOF；不 remux | T8.4、T8.9 | 3/3 1080P30 首帧/播放；60s 音画偏差 ≤1 帧（日志）；20 次 seek 落点 0；seek 后音频从当前帧起；无死锁；内存 ≤800MB。**结果**：3/3 DASH 1080P AVC qn=80 + AAC 30280；首帧 322.2/369.0/365.1ms；60s 音画偏差 p95 **17.1/18.2/22.0ms**（max 25.4/26.0/27.5，≤1 帧）；20 次已缓存 seek（距关键帧 ≤2s）**landing 0/20 + PTS 0/20**，p95 129.9/110.3/105.0ms；seek 后音频 0/20 failures；视频 EOF 末帧停住；内存峰值 **211.5/209.7/201.7MB**；关窗 21–23ms；DASH 缓存命中 45–46ms、playurl/DASH/quality **0 网络**；`--force-durl` M2 8/0、M1 28/0；DASH M1 25/0（未缓存专项因下载完成显式跳过）；`ctest` 32/32、`lapian_online_cache_test` **396/0**、缓存管理 72/0+98/0、key-test 77/0、T8.9 探针 3×27/0+`--ui` 21/0、P1 291.41–293.89ms、P2 341.10–371.70ms、P7 0.85/0.35ms；**新增音频缓存键 `audio-<id>`（旧 meta 兼容）**；**修复 fMP4 seek 落前一 GOP（PTS 校验）**；**未判 M2** | `--online-m2-test`（含 `--force-durl`/`--m2-cache-hit-only`）；`lapian_dash_spike`；`docs/perf/v2-online-m2.md`；`docs/handoff-2026-10-24-t8.10.md` | 5s GOP 点尾 seek ~260ms（P9 ≤2s 外）；LRU 可能拆散视频/音频；OB-2 API fps 29.412 vs 容器 30；多音轨/无音轨 DASH-only 未验；默认 AppData/Medium 未重跑 |
| **T8.11**（**完成 2026-10-24**） | 渐进索引/波形/总帧数：只扫已缓存前缀；三态；缓存增长增量推进；波形渐进；不反复全扫；不阻塞 | T8.10 | 3min 1080P30：已缓存 30s 内索引可用；缓存区 seek 精确；缓存完成后总帧数精确；波形 ≤1 帧；UI 停顿 >200ms = 0；未缓存 seek 显示等待。**结果**：OB-3（1080P25/212.261s/5306 帧）视频 2MB/s 慢增长下，首次索引 128ms、30s 内容 6.209s（published 13.1MB/69.8MB，stable 780/750）、20/20 已缓存 seek 落点 0（p95 56.2ms）、完成后 `exact:5306`（容器估算 5307）、波形 42,462 buckets/偏移 0.525 帧/末端 0.50 帧、UI gap **69ms**、未缓存提示 0ms/取消 26ms；增量 `steps=285/opens=1/reopens=0/read_bytes=69,759,161/read_ratio=1.000/max_step 8.86ms`；离线 2MB/s case **22/0**（30s 6.084s、stable 752、落后 240KB、read_ratio 1.000、max step 5.36ms；旧路径 30s 前缀单次重建 166ms）；单测 **28/0**；`ctest` **33/33**、M2 34/0、M1 `--force-durl` 28/0、quality 3×27/0+`--ui` 21/0、缓存 396/0、管理 75/0+98/0、key 77/0；**未判 M2** | `--online-index-test`（真实 OB-3 + `--index-offline-video/-audio --index-throttle-bps`）；`lapian_online_index_test`；`docs/perf/v2-online-index.md`；`docs/handoff-2026-10-24-t8.11.md` | 索引重启；重复帧；总帧数跳变；CPU 争抢。**已缓解**：前沿 guard + resume_pos 重开；tail guard 48 + 前缀 PTS 校验；estimate→exact 的 1 帧差已记录 |
| **T8.12**（**完成 2026-10-24**） | 档位策略与降帧提示：自动选档（AVC ≤1080p，1080P60→720P60→1080P30→720P30→480P30；AVC 60fps 可自动，HEVC/AV1-only 60fps 只手动）；手动切档（重新取流/索引/波形，PTS 回位）；按权限列档（只列 playurl 实际返回；无权限不显示/置灰；实际 qn 以服务端为准）；DEC-4 实际 fps/降帧提示；标记/截图记录实际上下文；按 PTS 映射；B 的 1080P60/4K 口径 | T8.9、T8.10 | OB-2 实际 ≈29.4fps + 提示（DEC-4）+ 文案“帧号与源片不同步”；真 1080P60 不显示降帧；HEVC-only 60fps 手动菜单标“60fps/HEVC”，不静默降帧；手动 1080→720 成功；标记保留；不出现假 60fps；4K 只手动/可用档；**不得把“菜单可选”写成“保证可达”**。**结果**：普通用户 3/3 实际 qn=80 AVC（30.000/29.412/25.000），真实目录 3/3 无 116/74/120；OB-2 29.412fps（容器 30）→ 起播横幅“帧号与源片不同步/约 1/2”+ 常驻角标“29.412fps · 降帧”（横幅可关，角标保留）；OB-1/2/3 手动 1080→720 成功（qn=64）、PTS 1,000,000→第 30 帧、标记 1→1、截图记录 qn64/codecid7/fps29.412/frame30/pts1,000,000、菜单点击 61/75/76ms；普通 30/25 不误报；真 60fps/4K 仅合成 UI 规则（AVC60 自动优先、HEVC-only 60fps 手动、4K 仅手动可用档），**不可得/未验证**；`lapian_online_dash_test` 125/0、`lapian_online_frame_rate_test` 27/0、`ctest` 34/34、quality 3×45/46/45、UI 50/56/50、M2 34/0×3、M1 28/0、index 27/22/28、长按 54/0、缓存 396/0、管理 75/0+72/0+98/0、key 77/0；dist 34.21MB/25；**M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema 未改；未判 M2** | `--online-quality-test`（含 `--ui` 真实 OB-1/2/3）；`lapian_online_frame_rate_test`；`docs/perf/v2-online-quality.md`；`docs/handoff-2026-10-24-t8.12.md` | 判定不可靠；提示噪音；用户忽略；标记错位；把可选档写成保证；真 60/4K 不可得 |
| **T8.13**（**完成 2026-10-24**；M2 第 5 项，非门） | 网络异常/并发加固：断网、超时、403、URL 过期、缓存损坏、磁盘满、并发读、关闭阻塞、休眠恢复 | T8.6、T8.10 | ≥10 个故障场景：无崩溃/冻结；已缓存可播；重试可续；标记不丢；错误 ≤5s；日志脱敏；并发 4 读者 10min 无死锁。**结果**：新增 `lapian_online_fault_test`（ctest，15 组 **153 项 0 失败**）与 `--online-fault-test`（真实窗口，**10/10 场景 92 项 0 失败**，`unverified=1`）；12 行矩阵全过：断网 3,981–4,155ms、看门狗 4,121–4,137ms、API 超时 4,515–4,529ms（真实 WinHTTP）、磁盘不足 5ms、关闭 7–66ms、UI gap max **73ms/0 次 >200ms**；失败后已缓存帧 2 落点/PTS 精确、前沿 EIO；同 URL 续传 offset 409,600、重开 1,228,800；403→备份 1 次只下缺失字节；过期刷新 1 次、刷新失败保留 `.part`；缓存截断（1.07MB vs 3.21MB）→ **自动 Rebuild** → ready 132ms、重开 cache-hit 52ms/0 请求、标记 1、记录 584B 保留；4 读者真实 **600s**：ops 25,907、waits 522、`timeouts=0`、close **16ms**；脱敏无 SESSDATA/`deadline=`/`upsig=`/签名值/完整 URL；修 4 个产品缺陷（失败后不 Cancel 读层、4s 网络无响应看门狗、API 超时 4.5s、缓存损坏自动重建）；`ctest` **35/35**、M2 **3/3 34/0**、M1 `--force-durl` **28/0**、quality UI **50/56/50**、index **27/0+22/0**、长按 **54/0**、管理 **72/0+98/0**、key **77/0**、P1/P2 **268.67/322.34ms**、P7 0.19/0.24ms、dist **34.3MB/25**；**真实 Windows 休眠未验证**；**开发期 1 次未复现崩溃/1 次未复现卡住已列残余风险**；**M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema 未改；未判 M2** | `--online-fault-test`；`lapian_online_fault_test`（`--readers-minutes 10`）；`docs/perf/v2-online-faults.md`；`docs/handoff-2026-10-24-t8.13.md` | 故障注入真实度；休眠/真实 CDN 未验；未复现崩溃残余风险 |
| **T8.14** | **M2 门**：全量验收 + 本地回归 + 网络审计 + 报告/交接 | T8.9–T8.13 | 3/3 普通用户 1080P30；OP1–OP5；失败矩阵；缓存配额；本地 O9 + P1/P2/P7；F1–F8 回归；内存；**无** 4K/60 验收 | `docs/perf/v2-online-m2.md`；`docs/handoff-*-m2.md` | 时间；网络；M2 滑期 |
| **T8.15**（B 条件验收，非 MVP 门） | 大会员可用档：1080P60/720P60/4K SDR 条件验收；有账号才执行，无账号写“未验证”；不改默认 AVC ≤1080p，不阻塞 M1/M2 | T8.9、T8.12、T8.14（可并行） | O11-A1…A7；1080P60/720P60 真 60fps/不显示降帧/已缓存 P7 条件 ≤80ms；HEVC-only 60fps 只手动；4K 只手动/能播/逐帧不崩/不承诺 P7/P9/颜色；无账号 → “未验证”；不得写成 MVP 通过 | `docs/perf/v2-online-vip.md`；O11 报告 | 无大会员账号；4K HEVC/AV1/HDR 颜色；把条件验收当 MVP |
| **T8.16**（M2 后，非 MVP 门） | 打包/隐私/卸载：网络栈 DLL/许可/隐私文本；卸载缓存清理选项；安装 0 网络；P12；缓存目录说明；**安装到 `%LOCALAPPDATA%\Programs\拉片软件`，正常用户直接双击即可在线，不依赖 DSH 低完整性工作区** | T8.14 | 安装/卸载通过；安装 0 TCP/UDP；P12 ≤100MB；卸载不静默留 20GB；本地零网络仍成立；**安装目录直接双击进程为正常 Medium integrity，可创建默认缓存/凭据** | 更新 `docs/perf/T7.6-packaging.md`；安装器证据 | 体积；杀软；缓存残留；低完整性工作区被误当安装环境 |

**M2 门（硬条件）**：

1. 普通用户（非大会员）3/3 `OB` 样本 DASH 1080P30 可播；实际 fps/档位与 S2 一致。
2. OP1 起播、OP2 已缓存 P7、OP3 未缓存行为、OP4 已缓存播放停顿、OP5 缓存命中全部按 DEC-2 已拍板值达标。
3. DASH 双流 60s 音画偏差 ≤1 帧；20 次 seek 精确；断网/过期/损坏/磁盘满可恢复。
4. 本地模式零网络 + P1/P2/P7 + F1–F8 不退化。
5. **M2 门没有** 4K/1080P60 验收项；T8.15 是 B 的独立条件验收（非门）；`OB-2` 只证明“降帧提示正确”，不证明 60fps 可用。
6. 不把 `durl` 1080p（S2 1/3）写成 1080p 保证；1080p 结论必须来自 DASH。

---

## M1/M2 后续非门任务（v0.13 新增）

> 来源：2026-10-23 真实使用反馈。以下任务**不改变 M1/M2 门、验收数字和 T8.6 已完成范围**；M1 前只做 T8.7/T8.8，下列任务默认排在 M1 后。任务 ID 不插入也不重排已有 T8.x。

| ID | 目标与范围 | 依赖 | 验收（单测/手测/性能） | 证据/产物 | 风险 |
|---|---|---|---|---|---|
| **T8.6.1a**（非门；M1 后；**完成 2026-10-24；实机回归 hotfix 同日；用户实机确认**） | 在线长按 PgUp/PgDn 连续步进：把同步 `current_frame_` 检查改为异步步进队列；处理 seek 合并/取消；边界停止；未缓存区显示等待，不阻塞 UI | T8.6 | 已缓存区：长按 2s 连续前进/后退，方向正确、无跳帧、无 UI >200ms；到边界停止；短按行为不回退；未缓存区有“缓冲中（目标帧 N）”且可取消。**结果**：在线走“目标帧→异步 seek→落地→单格排队”状态机；2s 25–26 帧、轨迹 bad=0、UI gap 37–77ms；0/末帧边界停止；短按 ±1/快速双击 ±2；未缓存提示 0ms、取消 4–17ms、回缓存落点 31–51ms；真实 3 样本×2 轮（Medium）+构建目录 3 轮均 49–50/0；重建会话保留部分索引；**实机发现未缓存每 70ms 取消在途 seek 造成 1 帧 starvation、伪 release/焦点误停连发，已修**：未缓存改单格排队（retargets=0）、物理键 release 守卫/auto-repeat 恢复、拖动 clamp/提示；用户样本 720P 54/0（前进/后退 26/26、伪 release 8 帧）、ctest 30/30、本地 key-test 77/0；**用户确认 C 盘安装版长按正常，验收闭环**；**M1/M2 门与数字未改** | `--online-longpress-test`（**54 项/样本**）；`docs/perf/v2-online-interaction.md` v1.1；`docs/handoff-2026-10-24-t8.6.1a.md` | 异步 seek 取消/合并；长按抖动；UI 线程阻塞；真实键盘伪 release |
| **T8.6.1b**（非门；T8.11 后） | 在线拖动预览：基于 `GrowingSource` 的降采样预览或异步 seek 预览；拖动中连续更新，松手精确落帧；未缓存区显示等待 | T8.6.1a、T8.11 | 已缓存区慢拖画面连续更新 ≥30fps（降质预览）；松手精确落帧；未缓存区不冻结 UI、可取消；不改变本地预览语义 | `--online-drag-preview-test`；`docs/perf/v2-online-interaction.md` | 预览解码阻塞；未缓存区拖动；内存/GPU 表面 |
| **T8.3.1**（非门；M1 后；**完成 2026-10-24**） | 缓存管理：设置中自选缓存目录、修改配额、打开缓存目录、清空缓存、启动/定期孤儿清理；清缓存保留在线记录（T8.7 记录在缓存根外） | T8.3、T8.6 | 改目录/配额跨会话生效；清空后占用 0、记录仍在；孤儿 `.part` 可清理；不误删当前会话/记录；磁盘满/只读路径可读错误。**结果**：新增 `CacheManager` + 设置对话框（状态栏“设置”/Ctrl+,）：目录/配额保存到 settings.json 并跨会话，改目录不迁移旧缓存；清空支持 bvid+cid 部分键保护当前会话；在线记录/截图 sentinel 在缓存根外逐字节保留；启动后 3s + 每 6h 清理孤儿/过期未完成/evict_pending 并回收配额；只读 ACL 错误“缓存目录不可写（只读或权限不足）”、磁盘满 2PB 预检 0.11–0.29ms；`--online-cache-manage-test` workspace 72/0（无 UI）/98/0（`--ui`）、Medium 默认 AppData **101/0**（integrity=medium）、`lapian_online_cache_manage_test` 73/0、`ctest` 31/31、只读脚本 75/0、dist 33.87MB/25；真实默认缓存/记录/设置与备份逐字节一致（仅只读摘要，未在真实根跑破坏性清理）；**M1/M2 门与数字未改** | `--online-cache-manage-test`；`lapian_online_cache_manage_test`；`scripts/cache-manage-readonly.ps1`；`docs/perf/v2-online-cache-manage.md`；`docs/handoff-2026-10-24-t8.3.1.md` | 目录权限；误删；记录/缓存耦合；低完整性环境 |
| **T8.12 澄清**（原任务注记，不新增 ID） | 登录后按账号实际允许档位显示可选清晰度；实际 qn 以服务端返回为准；不得伪造 4K/60；无权限档位不显示或明确置灰 | T8.9/T8.10 | 与 T8.12 原验收合并；只加“按权限列档/实际 qn”说明，不改 M2 门与数字 | 并入 `v2-online-quality.md` | 把“可选”写成“保证”；4K/60 条件验收混入 M2 |
| **T8.16 澄清**（原任务注记，不新增 ID） | 安装到 `%LOCALAPPDATA%\Programs\拉片软件`，正常用户直接双击即可在线；不依赖 DSH 工作区 Low integrity；卸载缓存清理选项保留 | T8.14 | 与 T8.16 原验收合并；加“直接双击/非工作区运行”验证；不改 P12/门 | 并入 T8.16 安装器证据 | 低完整性工作区被误当安装环境 |

**非门后续排期**：

- T8.6.1a：**已完成（2026-10-24）**；T8.8 之后、M2 前完成，未插到 T8.7/T8.8 前。
- T8.3.1：**已完成（2026-10-24）**；T8.8 之后、M2 前完成；先做“清空/孤儿/目录”最小闭环，再做配额 UI。
- T8.6.1b：T8.11 之后；依赖渐进索引，不能提前。
- T8.12/T8.16 只做澄清注记，不改变原任务范围。

**明确不做**：不为“登录后任意选 4K/1080P60”新增任务（T8.9/T8.12/T8.15 已覆盖）；不为“直接双击权限”新增功能任务（T8.16 安装器解决）；不把测试环境 Low integrity 当产品功能任务。

---

## 两周排期（DEC-1 已拍板：M1→M2）

> 详细见 [plan.md](plan.md) §12.12。**前提已满足**：DEC-1…6 已拍板、ADR-007 附条件接受；**无缓冲日**；M1 可独立交付，M2 可滑。

| 天 | 任务 | 门/证据 |
|---|---|---|
| D0（已完成） | T8.0 | 5 项拍板已落地；ADR-007 附条件接受；delta/tasks v0.3 |
| D1 | T8.1 | `v2-online-resolve.md` |
| D1–D2 | T8.2 | **HTTP 自检门**；`v2-online-http-selfcheck.md` |
| D2 | T8.3（完成） | `v2-online-cache.md` |
| D2–D3 | T8.4 | `v2-online-growing.md`（关键路径） |
| D3–D4（与 T8.5 并行） | T8.2.2（非门，DEC-6） | 扫码登录；`v2-online-qr.md`；`--online-qr-test` |
| D3–D4 | T8.5 | `v2-online-m1.md` |
| D4–D5 | T8.6 | E2E 3 条；`--online-m1-test` |
| D5 | T8.7 + T8.8 | **M1 门**；M1 交接 |
| D6 | T8.9 | `v2-online-quality.md` |
| D6–D7 | T8.10 | 1080P30 3/3；`v2-online-m2.md` |
| D8（完成） | T8.11 | `v2-online-index.md` |
| D9 | T8.12 | 降帧提示；截图 |
| D9–D10 | T8.13 + T8.14 | **M2 门**；M2 交接 |
| 并行/之后 | T8.15 / T8.16 | T8.15：B 条件验收（0.5–1 天，非门；无账号则未验证）；T8.16：打包（M2 后） |
| M1 后（非门） | T8.6.1a / T8.3.1 | 在线长按/缓存管理；不挡 T8.8，可与 M2 并行 |
| T8.11 后（非门） | T8.6.1b | 在线拖动预览；依赖渐进索引 |

**切 line**：先砍 T8.15/4K 手动；再砍 T8.12 手动切档；再砍 T8.13 部分注入；**不砍** T8.4/T8.6/T8.10/本地回归/O9。若 D8 结束 T8.10 仍不稳定，M2 顺延第 3 周，M1 先交付。

---

## 任务完成定义（v2 DoD）

- 代码审查通过；改动范围与任务一致，无顺带功能。
- 有单测的写单测；无单测的写清手测步骤/结果；性能任务附 `docs/perf/v2-*.md`（机器/网络/样本/次数/p50/p95）。
- 每个任务至少复查：本地启动 P1、打开 P2、步进 P7（热缓存）；不得回归 v1。
- 无 Cookie/签名 URL 入日志、入库、入 meta；`.secrets/` 不入库。
- 不新增 spec 未定义的在线行为；变更走 delta 版本记录。
- 不把 4K/1080P60 写成 MVP 验收；B 条件验收按 O11 单独报告；不把未缓存区写成 80ms；不把 durl 1080p 当保证。
- 无大会员账号时，T8.15 写“未验证”，不得用普通账号冒充、不得写成通过。
- 完成任务后写交接（决策、数字、缺口、下一步）。

## 审核门

1. **DEC-1…DEC-6 已拍板；ADR-007 附条件接受**；T8.2/T8.4 通过后回填评审记录。
2. M1 门通过后才进 M2；M2 门通过后才谈 Beta/发布。
3. 每个里程碑必须有：性能证据、失败矩阵、本地回归、网络审计（本地 0 请求）、缓存/记录恢复证据。
4. 任何“4K 可达/60fps 可达”的结论只能进 T8.15（B 条件验收，非门），不得写成 MVP 通过。

## 决策 → 任务映射（已拍板）

| 决策 | 影响任务 | 拍板结果 |
|---|---|---|
| **B 大会员档条件验收** | T8.9/T8.12/T8.15、O11/OP9/OP10 | 已拍板：条件验收，非 MVP 门 |
| **DEC-1 M1/M2 顺序** | 排期、T8.0 | 已拍板 A：M1 → M2 |
| **DEC-2 验收数字** | T8.6/T8.8/T8.14、delta OP 表 | 已拍板：建议值全部接受 |
| **DEC-3 登录自检时机** | T8.2、O8/O9 | 已拍板 A：首次在线打开 |
| **DEC-4 60fps 提示形态** | T8.12、O6 | 已拍板 A：横幅 + 常驻角标 |
| **DEC-5 配额验收方式** | T8.3/T8.8 | 已拍板 A：300MB + 20GB 记账单测 |
| **DEC-6 扫码登录** | T8.2.2、O8/§6.1、vision §3/§5 | 已拍板 B：官方网页二维码流程，非门，失败回退 SESSDATA 粘贴 |

## 版本记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1-draft | 2026-10-23 | S3 初稿：M1 T8.0–T8.8、M2 T8.9–T8.16；每项含依赖/验收/证据；两周排期；DEC 映射 |
| v0.2 | 2026-10-23 | 应用 B：T8.15 从“可选诊断”升级为“大会员可用档条件验收（非 MVP 门）”；T8.9/T8.12/M2 门补 1080P60/4K 口径 |
| v0.3 | 2026-10-23 | T8.0 完成：DEC-1…5 拍板落地；ADR-007 附条件接受；M1/M2 锁定；T8.1 待开工 |
| v0.4 | 2026-10-23 | T8.1 完成：BV/URL 归一化、匿名 view、分P、错误映射、回显确认、`--online-resolve-test`；证据 `docs/perf/v2-online-resolve.md`；M1/M2 门与验收数字未改 |
| v0.5 | 2026-10-23 | T8.2 完成：WinHTTP nav `code=0/isLogin=true`（普通用户，161–202ms）；SESSDATA/DPAPI/凭据 UI/DEC-3/脱敏；`--online-http-selfcheck`、`--online-auth-test`；libcurl 未构建；M1/M2 门与验收数字未改 |
| v0.6 | 2026-10-23 | T8.3 完成：`CacheStore`（默认根/默认 20GiB/meta/.part→.mp4/配额 LRU/续传/损坏重建/清空/孤儿/长路径）；300MB LRU + 20GB 记账单测；`lapian_online_cache_test` 366/0、`--online-cache-test` 32/0；settings 新增 `cache_root/cache_quota_gb`；M1/M2 门与验收数字未改 |
| v0.7 | 2026-10-23 | **DEC-6 拍板**：扫码登录并入 v2，新增 **T8.2.2**（非 M1/M2 门；依赖 T8.2；排 T8.4 之后、可与 T8.5 并行）；只走官方网页二维码流程、只保留 SESSDATA、失败回退粘贴；M1/M2 门与全部数字未改 |
| v0.8 | 2026-10-23 | T8.4 完成：`GrowingSource` + 每读者 `GrowingCursor`/`AVIOContext` + `GrowingFormat`；0.3MB 首帧、100/100 seek、已缓存零阻塞、未缓存等待 +18.5%、取消 0.15/0.24ms、4 读者 10min `deadlocks=0/lost_wakeups=0`、close/WS 有界、无 busy loop；EOF/EIO/EXIT 与淘汰 busy；`lapian_growing_source_test` 157/0、`--online-growing-test` 22/0；证据 `docs/perf/v2-online-growing.md`；M1/M2 门与验收数字未改 |
| v0.9 | 2026-10-23 | T8.5 完成：`PlayUrl`（playurl/fnval=0/durl）、`DownloadTransport`（Range/无 Cookie）、`DownloadJob`（续传/3 备份/重试/过期刷新/发布/MarkComplete）；真实 3 样本均下载+首帧+ffprobe，**OB-2 仅 360P → M1 3/3 720P blocked**；取消 7ms、`_Exit` 后续传、真实 403 备份切换/刷新一次通过；`lapian_online_fetch_test` 94/0、`--online-fetch-test` 46/0、`ctest` 27/27；证据 `docs/perf/v2-online-m1.md`；M1/M2 门与验收数字未改 |
| v0.10 | 2026-10-23 | T8.2.2 完成（非门）：`QrLogin`（官方 generate/poll、4 态、SESSDATA-only）、`QrCode` + vendored MIT qrcodegen、`QrLoginController`（1.5s/180s/取消/nav 门/DPAPI）、`OnlineResolveDialog` 扫码入口（粘贴保留）；`lapian_online_qr_test` 115/0、`--online-qr-test` 19/0（`--ui` 21/0，dist 同）；真实 generate/poll 字段实测；**真实扫码未验证**；`ctest` 28/28；证据 `docs/perf/v2-online-qr.md`；M1/M2 门与验收数字未改 |
| v0.11 | 2026-10-23 | T8.6 启动交接/提示词入库（**T8.6 未开始**）：新增 `docs/handoff-2026-10-23-t8.6-start.md`、`docs/next-session-prompt-t8.6.md`；记录路径-only 媒体层、部分索引、UI 线程阻塞、完成改名 vs reader、音频/波形五个硬缺口；建议先 spike 再按阶段 1/2/3；T8.6 验收列与 M1/M2 门/数字未改 |
| v0.12 | 2026-10-23 | T8.6 完成：`OnlineSession` + MainWindow 在线入口、`OpenGrowing`/`OpenWithFormat`/`ProbeFormat`、部分索引/异步 seek/未缓存等待、波形渐进、Ctrl+M/S、defer finalize/关窗；真实 3 条 E2E、冷缓存首帧 20×3（p95 624/725/459ms）、P7 p95 20/20/25ms、seek p95 79/59/97ms、UI gap 60/61/62ms、缓冲耗尽/取消/错误通过；`ctest` 29/29；本地 P1/P2 290.68/347.43ms、P7 66.59ms；dist 33.57MB/25；**OB-2 仍 360P，M1 3/3 720P blocked，不判 M1**；证据 `docs/perf/v2-online-session.md`、交接 `docs/handoff-2026-10-23-t8.6.md`；M1/M2 门与验收数字未改 |
| v0.13 | 2026-10-23 | T8.6 真实使用反馈收口：新增非门后续 **T8.6.1a 在线长按步进**、**T8.6.1b 在线拖动预览**（依赖 T8.11）、**T8.3.1 缓存管理 UI/清空/孤儿清理**；T8.12 补“按账号允许档位列档、实际 qn 以服务端为准”；T8.16 补“安装到 `%LOCALAPPDATA%\Programs` 后直接双击、不依赖 Low integrity 工作区”；M1/M2 门、T8.7/T8.8 顺序与全部验收数字未改；下一任务仍 T8.7 |
| v0.14 | 2026-10-23 | T8.7 完成：完整缓存命中 `OpenComplete` 直接打开 `data.mp4`（0 playurl/CDN/`.part`）；`lapian-online-1` 记录（bvid+cid、逐条 fps/qn/codecid、原子写/损坏备份/空删除）；截图 `Pictures\拉片软件\<bvid>\` + `index.jsonl`；清缓存保留记录、重建回访、损坏不阻塞、换档按 PTS 映射；`lapian_online_records_test` 52/0；`--online-offline-test` workspace 45/0、真实缓存 44/0、Medium 默认 AppData 31/0×3（首帧 74–84ms、0 TCP/UDP、无 winhttp/wininet/urlmon）；`ctest` 30/30；本地 P1/P2 296.81/345.40ms、P7 跨 GOP 66.56ms；dist 33.68MB/25；**OB-2 仍 360P，M1 3/3 720P blocked，不判 M1**；证据 `docs/perf/v2-online-cache-hit.md`、交接 `docs/handoff-2026-10-23-t8.7.md`；M1/M2 门与验收数字未改；下一任务 T8.8 |
| v0.15 | 2026-10-23 | T8.8 完成、**M1 通过**：qn/fourk 矩阵（20 单元）证明 OB-2 的 360P 是请求 qn=64 导致（qn=80/112/120 → 80/1080P，fourk 无影响）；最小产品修正 M1 durl 请求 qn 64→80、fourk=0、刷新同档（`kM1RequestQn`）；3/3 实际 qn=64/80/64、ffprobe pass；E2E 29/0×3；OP1 p95 648/532/527ms（20/20×3）、OP2 p95 34/51/34ms（20/20×3）、OP3 提示/取消 0ms + 落点 0（3/3）、OP5 p95 95ms + 0 请求（20/20）；杀进程续传 5,242,880B/hash 一致、清缓存保留记录 45/0；DEC-5 300MB→188,744,706B、20GB 记账 10,737,418,599 ≤ 21,474,836,480；本地零网络、P1 342.92/P2 376.14/P7 64.79ms、`ctest` 30/30、dist 33.69MB/25；证据 `docs/perf/v2-online-m1.md` §11–§17、交接 `docs/handoff-2026-10-23-t8.8-m1.md`；**M1/M2 门、验收数字、OB 样本未改；未进 M2**；下一任务 T8.9（等用户确认） |
| v0.16 | 2026-10-24 | T8.6.1a 完成（非门）：在线长按改异步步进状态机（取消同步 `current_frame_` 边界检查；已缓存单格排队不跳帧、未缓存最新目标重定向/取消；首末帧停止；短按/快速双击；外来 seek 从已请求目标 ±1）；重建会话总是带部分索引；`--online-longpress-test`（50 项/样本）+ `docs/perf/v2-online-interaction.md`；Medium 副本 3 样本×2 轮 49–50/0（2s 25–26 帧、bad=0、UI gap 37–77ms、未缓存提示 0ms、取消 4–17ms、回缓存 31–51ms、关窗 377–587ms）；本地 key-test 77/0、`ctest` 30/30、M1 E2E 28/0 与 landing 29/0、buffer 10/0、P1/P2/P7 279.03/361.57/65.52ms；**M1/M2 门、验收数字、OB 样本未改；未进 T8.9/M2** |
| v0.17 | 2026-10-24 | T8.6.1a 实机回归 hotfix（非门）：实机首次在线播放长按只走 1 帧、慢拖只变帧号。根因：未缓存每 70ms tick 取消在途 seek（慢 seek 永远落不了地）+ 物理键仍按住时的伪 `KeyRelease`/焦点停连发 + 拖动 partial-index clamp。修复：未缓存改单格排队（retargets=0，落地再走）、`GetAsyncKeyState` 伪 release 守卫、auto-repeat 恢复、`StopStepRepeat(reason)`/`step_start`/`focus_changed` 日志、拖动 clamp/提示；`--online-longpress-test` 增至 **54 项**（伪 release 8 帧）；用户样本 720P 54/0（前进/后退 26/26、bad=0）、`ctest` 30/30、本地 key-test 77/0；**慢拖预览仍属 T8.6.1b，首次 open 阻塞另测；M1/M2 门与数字未改；未进 T8.9/M2** |
| v0.18 | 2026-10-24 | **T8.6.1a 验收闭环**：用户实机确认 C 盘安装版 PgUp/PgDn 长按正常；结合 54/0、ctest 30/30、key-test 77/0，非门任务收口。首次 open 实测 44MB 首帧 634ms/512KB，不是 open 阻塞。**下一任务 T8.9（M2）已确认**，只做 DASH 解析/档位；T8.6.1b 仍等 T8.11；M1/M2 门、验收数字、OB 样本未改。 |
| v0.19 | 2026-10-24 | T8.3.1 缓存管理完成（非门）：`CacheManager` + 设置对话框（状态栏“设置”/Ctrl+,，自选目录/配额/打开目录/清空/清理孤儿）；`CacheStore::ClearAll` 支持 bvid+cid 部分键保护当前会话、`CleanupOrphans` 返回条目数、`CheckDirectoryWritable` 可读错误；启动 3s + 每 6h 清理并回收配额；改目录不迁移、在线记录/截图在缓存根外；`--online-cache-manage-test` workspace 72/0（无 UI）/98/0（`--ui`）、Medium 默认 AppData **101/0**（integrity=medium，默认根/记录根只读摘要，真实数据与备份一致）、`lapian_online_cache_manage_test` 73/0、`ctest` 31/31、只读 ACL 75/0、dist 33.87MB/25、本地 key-test 77/0、P1/P2 278.75–284.70/333.63–343.29ms；**M1/M2 门、验收数字、OB 样本未改；未进 T8.9/M2** |
| v0.20 | 2026-10-24 | **T8.9 DASH 解析与档位完成（M2 第 1 项，非门）**：`PlayUrl` 增 `fnval=4048` 请求、`dash.video[]/audio[]` 解析（qn/codecid/fps/带宽/size=未知/init+index 闭区间/主+2 备份 URL）、`DashCatalog`/`DefaultQn`（AVC、O6 顺序、跳过只手动）、`RebuildDashTiers`、`IsManualOnlyQuality`；`QualityMenu` 菜单模型（DASH 档 T8.9 置灰并写 T8.10、60fps/4K 标仅手动、4K 标可用档、页脚“菜单可选不等于保证可达”）；`OnlineSession` 首帧后异步取一次目录（缓存命中 0 网络）、`MainWindow` 状态栏“清晰度”按需弹出菜单；`--online-quality-test`（无 GUI/`--ui`/`--odd-qn`/`--anonymous`）；`lapian_online_dash_test` 117/0、`ctest` 32/32、3 条 **27/0×3**（登录普通用户 3/3 DASH 1080P AVC qn=80，fps 30.000/29.412/25.000；116/74/120 3/3 不可得）、`--ui` 21/0、M1 E2E 28/0（first_frame 614ms/UI gap 53ms，基线 656/62ms）、缓存管理 72/0+98/0、key-test 77/0、P1/P2 299.78–277.40/344.53–324.77ms、dist 34.02MB/25；**M1/M2 门、验收数字、OB 样本未改；未做 T8.10、不判 M2**；下一任务 T8.10 |
| v0.21 | 2026-10-24 | **T8.10 DASH 双输入完成（M2 第 2 项，非门）**：视频/音频各自 `GrowingSource`/`DownloadJob`/`AVFormatContext`；`OnlineSession` resolve（DASH 目录→默认 AVC+BestAudio→Range 总字节→失败回退 durl）、双 ready（视频 300KB/音频 96KB）、双 finalize、DASH 缓存命中；音频缓存键 `audio-<id>` + meta `kind/audio_id/container=dash`（`lapian-cache-1` 不变、旧 meta 兼容）；`MediaSession::OpenGrowingWithOptions` + `av_offset_us`；**修复 fMP4 `av_seek_frame` 落前一 GOP**（`VideoDecoder::SeekToKeyframePts` + 落点 PTS 校验）；`--online-m2-test` 3/3 **34/0**（首帧 322.2/369.0/365.1ms、60s 音画偏差 p95 17.1/18.2/22.0ms、20/20 seek 帧号+PTS 精确、EOF 末帧停住、内存峰值 211.5/209.7/201.7MB、缓存命中 45–46ms/0 网络）；`--force-durl` M2 8/0、M1 28/0；DASH M1 25/0（未缓存专项因下载完成跳过）；`ctest` 32/32、`lapian_online_cache_test` 396/0、缓存管理 72/0+98/0、key-test 77/0、T8.9 3×27/0+`--ui` 21/0、P1 291.41–293.89ms、P2 341.10–371.70ms、P7 0.85/0.35ms；**M1/M2 门、验收数字、OB 样本未改；未判 M2**；下一任务 T8.11 |
| v0.22 | 2026-10-24 | **T8.11 渐进索引/波形/总帧数完成（M2 第 3 项，非门）**：新增 `IncrementalOnlineIndexBuilder`（只扫已发布前缀、256KB 前沿 guard、frontier `ETIMEDOUT` 用 `resume_pos` 重开跳包、`Snapshot()` 才排序/去重、tail guard 48、`OnlineIndexStats`）；`BuildOnlineIndex` 改为一次性包装；`MainWindow` 索引线程 40ms 步进/250ms 安装节流、seek 期间 pending 补装、总帧数 `Scanning/Estimated/Exact`、波形 bucket 增长时重绘；`DownloadOptions.throttle_bytes_per_sec` + `OnlineSession::SetDownloadThrottleForTest`（只节流视频）；`--online-index-test`（真实 OB-3 + 离线 1–2MB/s）与 `lapian_online_index_test`；OB-3（1080P25/212.261s/5306 帧）真实在线 **27/0**：首次索引 128ms、30s 内容 6.209s（published 13.1MB/69.8MB，stable 780/750）、20/20 已缓存 seek 0 落点/p95 56.2ms、未缓存提示 0ms/取消 26ms、完成 exact 5306（容器估算 5307）、波形 42,462 buckets/偏移 0.525 帧、UI gap 58ms、关窗 22ms；增量 `steps=285/opens=1/reopens=0/read_bytes=69,759,161/read_ratio=1.000/max_step 8.86ms`；离线 2MB/s **22/0**（30s 6.084s、stable 752、落后 240KB、旧路径 30s 前缀单次重建 166ms、max step 5.36ms）；单测 **28/0**；`ctest` **33/33**、M2 34/0、M1 `--force-durl` 28/0、quality 3×27/0+`--ui` 21/0、缓存 396/0、管理 75/0+98/0、key 77/0、P1 276.98–299.32ms、P2 322.65–327.44ms、P7 0.85/0.45ms；**M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema 未改；未判 M2**；下一任务 T8.12 |
| v0.23 | 2026-10-24 | **T8.12 档位策略/手动切档/降帧提示完成（M2 第 4 项，非门）**：`OnlineRequest.requested_qn/requested_codecid`（默认 0→80，M1 URL 字节不变）、DASH 请求档缺失明确失败、缓存命中匹配请求档；`DefaultQn` 允许 AVC 60fps 自动、HEVC/AV1-only 60fps 只手动、4K 始终只手动；`QualityMenuContext` 权限列档/实际 qn/未登录 480P；`FrameRatePolicy` 标题 60fps 证据、dash_fps 实际值、普通 30/25 不误报、无证据只“可能降帧”；`OnlineSession` requested qn 刷新；`MainWindow` 切档（停旧流/重取/索引波形/PTS 回位/标记保留/实际上下文）；`InfoBar` DEC-4 可关横幅 + 常驻角标；真实 OB-2 29.412fps 横幅/角标，OB-1/2/3 1080→720 成功，截图清单 qn64/codecid7/fps29.412；`ctest` 34/34、dash 125/0、frame-rate 27/0、quality 3×45/46/45、UI 50/56/50、M2 34/0×3、M1 28/0、index 27/22/28、长按 54/0、缓存 396/0、管理 75/0+72/0+98/0、key 77/0、P1/P2 262.79/309.08ms、P7 0.74/0.44ms、dist 34.21MB/25；**真 60/4K 不可得/未验证；M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema 未改；未判 M2**；下一任务 T8.13 |
| v0.24 | 2026-10-24 | **T8.13 网络异常/并发加固完成（M2 第 5 项，非门）**：新增 `lapian_online_fault_test`（ctest，15 组 **153 项 0 失败**）与 `--online-fault-test`（真实窗口，**10/10 场景 92 项 0 失败**、`unverified=1`）；修 4 个产品缺陷：①下载失败后不再 `Cancel` 读层（已缓存前缀可读、前沿 EIO，S2 失败后帧 2 落点/PTS 精确）②`DownloadJob::stats().last_progress_ms` + `AbortStalledAttempt()` + `OnlineSession::Poll` 4s 网络无响应看门狗（错误 **4,121–4,155ms**、`.part` 保留）③`kOnlineApiTimeoutMs=4500` 统一 API 超时（真实 WinHTTP 静默服务器 **4,515–4,529ms**）④缓存损坏（截断 1.07MB vs 3.21MB）自动 `Rebuild` 后重下（ready 132ms、重开 cache-hit 52ms/0 请求、标记 1、记录 584B 保留）；12 行矩阵全过：断网 3,981–4,155ms、磁盘不足 5ms、关闭 7–66ms、UI gap max **73ms/0 次 >200ms**、续传 offset 409,600/1,228,800、403→备份 1 次只下缺失字节、过期刷新 1 次、脱敏无 SESSDATA/签名 URL；4 读者真实 **600s**：ops 25,907、waits 522、`timeouts=0`、close **16ms**；`ctest` **35/35**、M2 **3/3 34/0**、M1 `--force-durl` **28/0**、quality UI **50/56/50**、index **27/0+22/0**、长按 **54/0**、管理 **72/0+98/0**、key **77/0**、P1/P2 **268.67/322.34ms**、P7 0.19/0.24ms、dist **34.3MB/25**；**真实 Windows 休眠未验证**；**开发期 1 次未复现崩溃/1 次未复现卡住（20+ 次全量运行无复现，已列残余风险）**；**M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema 未改；未判 M2**；下一任务 **T8.14（M2 门）** |
