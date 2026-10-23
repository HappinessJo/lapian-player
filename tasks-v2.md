# 拉片软件 v2 在线模式任务拆分（tasks-v2.md）

- **版本**：v0.9（2026-10-23 T8.5 durl 取流完成；M1 3/3 720P blocked（OB-2 实际 360P）；M1/M2 门与数字未改）
- **状态**：**S3 定稿**；**T8.1/T8.2/T8.3/T8.4/T8.5 完成**；下一任务 **T8.2.2**（扫码，非门）；DEC-1…**6** 已拍板；M1/M2 已锁定；B 条件验收保留（非 MVP 门）。
- **依据**：[spec-v1.0-delta.md](docs/spec-v1.0-delta.md) v0.4、[plan.md](plan.md) v2 章节、[ADR-007](docs/adr/ADR-007-bilibili-online.md)、[v2-online-spike.md](docs/perf/v2-online-spike.md)、[product-v2-vision.md](docs/product-v2-vision.md)、[T8.1 证据](docs/perf/v2-online-resolve.md)、[T8.3 证据](docs/perf/v2-online-cache.md)、[T8.4 证据](docs/perf/v2-online-growing.md)、[T8.5 证据](docs/perf/v2-online-m1.md)
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
- T8.6 是第一次“真实窗口跑通”；T8.7 补缓存命中与记录；T8.8 是 M1 门。

**M2**：
`T8.8 → T8.9 → T8.10 → T8.11 → T8.12 → T8.13 → T8.14`

- T8.9 只做解析/档位；T8.10 才接双输入；T8.11 补渐进索引/波形；T8.12 补策略/提示；T8.13 加固；T8.14 是 M2 门。
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
| **T8.2.2**（DEC-6 拍板；非门） | 扫码登录：`passport.bilibili.com` 二维码 `generate`/`poll`；二维码本地渲染（vendored MIT 单文件，不引入 WebEngine/新 DLL）；状态机（等待/已扫待确认/过期/成功/失败）、180s 过期可刷新、可取消；成功只提取 SESSDATA → 复用 `CredentialStore`（DPAPI）+ nav 自检；失败回退 SESSDATA 粘贴（含剪贴板一键/图文引导）；**两入口并存，粘贴不是过渡、不降级** | T8.2 | 单测（fake HTTP）：4 态、过期、取消、坏 JSON、412/429 风控码、只提取 SESSDATA、日志无凭据；`--online-qr-test`（无 GUI、不写真账号）；真实扫码 1 次（用户手机）成功且 nav 通过；过期/取消 ≤100ms；失败不显示假登录态；本地模式仍 0 请求 | `--online-qr-test`；`docs/perf/v2-online-qr.md`；`docs/handoff-*-t8.2.2.md`；`lapian_online_qr_test` | 风控 412/429；网页接口变动；二维码依赖许可；真实扫码样本有限 |
| **T8.3**（完成 2026-10-23） | 缓存存储与配额：缓存根/目录结构；`meta.json`；`.part`→`.mp4` 原子改名；20GB 可配置；LRU；占用/清空；空间预检；孤儿清理；续传 | T8.1 | 单测（300MB 配额）：LRU 顺序、当前不淘汰、总占用 ≤配额；杀进程后续传；损坏检测/重建；清空保留在线记录；磁盘满 ≤5s 可读错误；路径/配额跨会话。**结果**：`lapian_online_cache_test` 366/0/0 skip；300MB 占用 283,116,589→188,744,706（配额 314,572,800、只淘汰最旧 90MB）；20GB 记账 21→16→10 GiB（`10,737,418,599 ≤ 21,474,836,480`）；续传 5,242,880；8 类损坏不可播放；清空保存 records sentinel；空间预检 0.082–0.115ms；长路径 301/312 字符；meta 无明文 | `lapian_online_cache_test`；`--online-cache-test`（32/0，verdict=pass）；`docs/perf/v2-online-cache.md`；`docs/handoff-2026-10-23-t8.3.md` | 文件锁/杀软；长路径；磁盘空间计算 |
| **T8.4**（完成 2026-10-23） | 可增长读层（关键路径）：`GrowingSource` + 每读者 `Cursor`/AVIO；已知总大小；阻塞读/发布；取消；EOF/错误语义；多读者；统计 | T8.3 | 0.3MB 可出首帧；100 次 seek 落点 0；已缓存 seek 零阻塞；未缓存等待 ≈剩余/速率（记录）；取消 ≤100ms；4 读者×10min 无死锁/丢唤醒；关窗 ≤1s；内存有界；无 busy loop。**结果**：0.3MB→首帧 1920×1080（open 9ms/首帧 16ms/实读 131072B）；100/100 seek 落点 0、已缓存等待 0；未缓存等待 2073ms vs 预期 1750ms（+18.5% ≤30%）；取消 0.15ms/0.24ms；4 读者 10min `deadlocks=0/lost_wakeups=0`（短版 wait 120 次、close 3ms、WS +9.77MB）；busy CPU 0ms/5s；EOF/EIO/EXIT 与淘汰 busy 通过；`ctest` 26/26、cache 32/0、auth 54/0 | `lapian_growing_source_test`（157/0）；`--online-growing-test`（22/0，verdict=pass）；`docs/perf/v2-online-growing.md`；`docs/handoff-2026-10-23-t8.4.md` | 死锁/丢唤醒；FFmpeg EOF/seek；锁竞争；int64 |
| **T8.5**（完成 2026-10-23；M1 3/3 blocked） | durl 取流：`playurl(fnval=0)`；M1 档位（720p/可用最高）；顺序下载；Range 续传；3 备份 URL；重试/退避；URL 过期刷新；吞吐统计；暂停/取消；完成改名 | T8.2、T8.4 | 3/3 登录样本 720p；首 0.3MB 出首帧；断网 5s 后续传；杀进程后续传；模拟 403/过期刷新；备份切换；无重复/损坏字节；产物可被 ffprobe 正常打开。**结果**：OB-1/OB-3=720P（qn=64）；**OB-2 实际 360P（qn=16；qn=64/80/112 均为 16）→ 3/3 720P blocked，不写通过**；三样本均完成下载+首帧（8–9/10–14ms）+ffprobe；真实取消 7ms、`.part` 续传 206、`_Exit` 后 5,242,880B 续传；真实 403 故障注入：备份切换 1 次 / 刷新 1 次均完成且 hash 一致；`lapian_online_fetch_test` 94/0、`--online-fetch-test` 46/0、`ctest` 27/27 | `docs/perf/v2-online-m1.md`；`docs/handoff-2026-10-23-t8.5.md`；故障日志 | durl 档位不可预期；Range 被忽略；CDN 限速；120min 过期；M1 3/3 前提未复现 |
| **T8.6** | 在线会话集成：`OnlineSession` 状态机；缓冲 UI/进度/取消；已缓存 seek/步进；未缓存等待；波形渐进；错误提示；关闭；**本地路径不动** | T8.4、T8.5 | E2E 3 条：输入→确认→首帧→Space→PgUp/PgDn→seek→波形→Ctrl+M→S→关窗；已缓存 P7 p95 ≤80ms（DEC-2 已拍板）；未缓存显示等待并可取消；UI 停顿 >200ms = 0；本地零网络 + P1/P2/P7 不退化 | `--online-m1-test`；`docs/perf/v2-online-m1.md`；截图/录屏 | 引擎 EOF/seek 假设；UI 竞态；索引等待下载；波形阻塞 |
| **T8.7** | 缓存命中 + 在线记录：断网按缓存打开（0 请求）；`lapian-online-1` 标记/备注；截图目录/命名/`index.jsonl`；清缓存保留记录；换档映射 | T8.3、T8.6 | 断网打开同 BV：0 TCP/UDP、首帧 ≤2.5s（DEC-2 已拍板）；标记/备注/截图恢复；清缓存后记录仍在；重建缓存后记录可回访；损坏记录不阻塞播放 | `--online-offline-test`；ProcMon/ETW；`docs/perf/v2-online-cache-hit.md` | 隐式 API 调用；目录权限；截图命名冲突 |
| **T8.8** | **M1 门**：全量验收 + 报告 + 交接；决定是否进 M2 | T8.1–T8.7 | 3/3 720p；OP1/OP2/OP3/OP5 按 DEC-2 已拍板值达标；失败矩阵（O1/O7）通过；缓存配额按 DEC-5 已拍板；本地 O9 全过；无 P0/P1 缺陷；ADR-007 附条件接受已记录 | `docs/perf/v2-online-m1.md`；`docs/handoff-*-m1.md` | 网络方差；样本不足；durl 720p 需登录样本 3/3 |

**M1 门（硬条件）**：

1. 普通用户登录后，3 条 `OB` 样本 `durl` 720p 全部可播（S2：登录后 3/3 均 ≥720p）。
2. 在线起播按 OP1；已缓存 P7 按 OP2；未缓存行为按 OP3；缓存命中按 OP5。
3. 本地模式零网络 + P1/P2/P7 不退化（O9）。
4. 失败矩阵（格式/不存在/不可见/未登录/断网/磁盘满）5s 内可读，不崩。
5. 缓存/记录可恢复：杀进程续传、清缓存不丢记录。
6. **没有** 4K/1080P60 验收项；**没有** 未缓存区 80ms 承诺。
7. **扫码登录（T8.2.2）不是 M1 门**：M1 的“登录后 3/3 720p”用 SESSDATA 粘贴即可满足；T8.2.2 失败/未做不影响 M1 判定。

> **2026-10-23 T8.5 复测记录（不改本门）**：OB-2（`BV1mA411m7pS`/cid `1004967230`）的 `durl` 在请求 qn=64/80/112 时实际都返回 `quality=16`（360P）；OB-1/OB-3 为 720P。因此第 1 条“3/3 720p”的 S2 前提本次未复现，T8.5 如实写 **blocked**，不伪造；是否调整 M1 门由 T8.8/用户决定，本文件未改门与数字。

---

## M2：DASH 双输入 1080p

| ID | 目标与范围 | 依赖 | 验收（单测/手测/性能） | 证据/产物 | 风险 |
|---|---|---|---|---|---|
| **T8.9** | DASH 解析与档位：`playurl(fnval=4048)`；`dash.video[]/audio[]`；qn/codecid/fps/带宽/size/init/index；备份 URL；AVC 默认；档位顺序；4K 仅手动；档位菜单；`quality` 字段不可信 | T8.8 | 3 条档位矩阵与 S2 一致（登录普通用户 1080P30 3/3）；普通用户 60/4K 不可得记录在案；**大会员档按 O11/T8.15 条件验收，不写成 MVP**；1080P60/4K 进菜单；HEVC-only 60fps 只手动；错误 qn 不崩；音轨存在；不把 durl 1080p 1/3 当保证 | `--online-quality-test`；`docs/perf/v2-online-quality.md` | API/WBI 变动；编码只有 HEVC/AV1；档位撒谎 |
| **T8.10** | DASH 双输入：视频+音频各自 `GrowingSource`/`DownloadJob`/`AVFormatContext`；共享时钟；seek fan-out；首帧/首包偏移；单流 EOF；不 remux | T8.4、T8.9 | 3/3 1080P30 首帧/播放；60s 音画偏差 ≤1 帧（日志）；20 次 seek 落点 0；seek 后音频从当前帧起；无死锁；内存 ≤800MB | `--online-m2-test`；`docs/perf/v2-online-m2.md`；A/V 日志 | 时间轴对齐；音频时钟；双缓冲；seek 风暴 |
| **T8.11** | 渐进索引/波形/总帧数：只扫已缓存前缀；三态；缓存增长增量推进；波形渐进；不反复全扫；不阻塞 | T8.10 | 3min 1080P30：已缓存 30s 内索引可用；缓存区 seek 精确；缓存完成后总帧数精确；波形 ≤1 帧；UI 停顿 >200ms = 0；未缓存 seek 显示等待 | `--online-index-test`；`docs/perf/v2-online-index.md` | 索引重启；重复帧；总帧数跳变；CPU 争抢 |
| **T8.12** | 档位策略与降帧提示：自动选档；手动切档；60fps 降帧提示（DEC-4）；实际 fps 可见；标记/截图记录 fps；换档按 PTS 映射；**B 的 1080P60/4K 口径** | T8.9、T8.10 | OB-2 实际 ≈29.4fps + 提示（按 DEC-4 形态）+ 文案“帧号与源片不同步”；真 1080P60 不显示降帧；HEVC-only 60fps 手动菜单标“60fps/HEVC”，不静默降帧；手动 1080→720 成功；标记保留；不出现假 60fps；4K 只手动/可用档 | `--online-quality-test`；截图；`docs/perf/v2-online-quality.md` | 判定不可靠；提示噪音；用户忽略；标记错位 |
| **T8.13** | 网络异常/并发加固：断网、超时、403、URL 过期、缓存损坏、磁盘满、并发读、关闭阻塞、休眠恢复 | T8.6、T8.10 | ≥10 个故障场景：无崩溃/冻结；已缓存可播；重试可续；标记不丢；错误 ≤5s；日志脱敏；并发 4 读者 10min 无死锁 | `--online-fault-test`；`docs/perf/v2-online-faults.md` | 故障注入不稳定；CDN 行为不可控 |
| **T8.14** | **M2 门**：全量验收 + 本地回归 + 网络审计 + 报告/交接 | T8.9–T8.13 | 3/3 普通用户 1080P30；OP1–OP5；失败矩阵；缓存配额；本地 O9 + P1/P2/P7；F1–F8 回归；内存；**无** 4K/60 验收 | `docs/perf/v2-online-m2.md`；`docs/handoff-*-m2.md` | 时间；网络；M2 滑期 |
| **T8.15**（B 条件验收，非 MVP 门） | 大会员可用档：1080P60/720P60/4K SDR 条件验收；有账号才执行，无账号写“未验证”；不改默认 AVC ≤1080p，不阻塞 M1/M2 | T8.9、T8.12、T8.14（可并行） | O11-A1…A7；1080P60/720P60 真 60fps/不显示降帧/已缓存 P7 条件 ≤80ms；HEVC-only 60fps 只手动；4K 只手动/能播/逐帧不崩/不承诺 P7/P9/颜色；无账号 → “未验证”；不得写成 MVP 通过 | `docs/perf/v2-online-vip.md`；O11 报告 | 无大会员账号；4K HEVC/AV1/HDR 颜色；把条件验收当 MVP |
| **T8.16**（M2 后，非 MVP 门） | 打包/隐私/卸载：网络栈 DLL/许可/隐私文本；卸载缓存清理选项；安装 0 网络；P12；缓存目录说明 | T8.14 | 安装/卸载通过；安装 0 TCP/UDP；P12 ≤100MB；卸载不静默留 20GB；本地零网络仍成立 | 更新 `docs/perf/T7.6-packaging.md`；安装器证据 | 体积；杀软；缓存残留 |

**M2 门（硬条件）**：

1. 普通用户（非大会员）3/3 `OB` 样本 DASH 1080P30 可播；实际 fps/档位与 S2 一致。
2. OP1 起播、OP2 已缓存 P7、OP3 未缓存行为、OP4 已缓存播放停顿、OP5 缓存命中全部按 DEC-2 已拍板值达标。
3. DASH 双流 60s 音画偏差 ≤1 帧；20 次 seek 精确；断网/过期/损坏/磁盘满可恢复。
4. 本地模式零网络 + P1/P2/P7 + F1–F8 不退化。
5. **M2 门没有** 4K/1080P60 验收项；T8.15 是 B 的独立条件验收（非门）；`OB-2` 只证明“降帧提示正确”，不证明 60fps 可用。
6. 不把 `durl` 1080p（S2 1/3）写成 1080p 保证；1080p 结论必须来自 DASH。

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
| D8 | T8.11 | `v2-online-index.md` |
| D9 | T8.12 | 降帧提示；截图 |
| D9–D10 | T8.13 + T8.14 | **M2 门**；M2 交接 |
| 并行/之后 | T8.15 / T8.16 | T8.15：B 条件验收（0.5–1 天，非门；无账号则未验证）；T8.16：打包（M2 后） |

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
