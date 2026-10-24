# 下一个会话提示词：T8.15（大会员可用档条件验收，非门任务）

- **版本**：v1（2026-10-24；T8.6.1b 收口后；当前 HEAD 以 `git log -1` 为准，代码基线 `b11e45a`）
- **直接复制本文件给新会话。** 只做 **T8.15**；不要重做 T8.16、T8.6.1b、M1/M2 门；
  不要把本次写成 MVP 通过。
- **当前状态**：M1（T8.8）与 M2（T8.14）通过；T8.16 pass（非 MVP 门）；T8.6.1b
  pass（非门）；`ctest` **36/36**、key **77/0**、longpress **54/0**、`dist`
  **34.37MB/25**；T8.14 残余 teardown `0xC0000374` / L-1 seed 卡住未定位，不得写
  “无崩溃/已证明稳定”。
- **账号**：2026-10-24 用户确认**已有大会员账号**；当前默认 DPAPI 凭据在 M2 预检中
  观察到 `nav vip_status=1`。这是账号状态，不是 T8.15 通过。不要打印/导出/提交
  SESSDATA 或签名 URL；不要假设 `.secrets/bilibili_sessdata.txt` 是当前大会员凭据，
  也不要覆盖它。
- **一句话**：验证大会员实际能拿到的 1080P60/720P60/4K SDR：高档播放、帧率/编码、
  不静默降帧、已缓存逐帧/P7、4K 手动/能播；逐项写 **pass/partial/fail/unverified**，
  不把条件验收写成 MVP，不因 O11 失败判 M1/M2 失败。

## 1. 先读

1. [AGENTS.md](../AGENTS.md)（v6.39 当前状态/约束/文档表）
2. [tasks-v2.md](../tasks-v2.md) 的 **T8.15 行**、T8.9/T8.12/T8.14 行、v0.27 记录
3. [spec-v1.0-delta.md](spec-v1.0-delta.md) **§0.2、§2.11 O11-A1…A7、§3 OP9/OP10、
   §9.1 B、§10 v0.4**（只引用已拍板口径，不新增验收值）
4. [T8.9 证据](perf/v2-online-quality.md)、[T8.12 证据](perf/v2-online-quality.md)、
   [T8.14 M2 证据](perf/v2-online-m2.md)、[T8.14 交接](handoff-2026-10-24-t8.14-m2.md)、
   [T8.16 交接](handoff-2026-10-24-t8.16.md)、[T8.6.1b 交接](handoff-2026-10-24-t8.6.1b.md)
5. 代码入口：
   - `src/app/MainWindow.*`：`StartOnlineSession`/`BeginOnlineRequest`、
     `ApplyOnlineReady`、`OnOnlineQualityChosen`、`CurrentQualityMenuContext`、
     `ApplyOnlineQuality`、切档/PTS 回位/标记保留；`SetOnlineQualityForTest`；
     `OnlineRequest.requested_qn/requested_codecid`。
   - `src/app/OnlineSession.*`：`Start` 的 `playurl`/DASH 选择；`requested_qn>0`
     只接受实际返回档、缺失明确失败；自动 `DefaultQn` 的 AVC 60fps/HEVC-only/4K 规则。
   - `src/online/QualityMenu.*`、`FrameRatePolicy.*`、`PlayUrl.*`：档位/编码/实际 fps/
     降帧提示规则。
   - 现有探针：`OnlineQualityTestRunner.*`（无 GUI + `--ui`，真实档位目录/菜单）、
     `OnlineM2TestRunner.*`（播放/seek/内存；有 `nav_normal_user` 前置，**大会员会
     被拒**，不要把它改成“大会员通过”）、`OnlineLongPressTestRunner.*`、
     `OnlineDragPreviewTestRunner.*`（T8.6.1b）。
   - 构建：`scripts\build-release.ps1`；T8.16 安装版在
     `%LOCALAPPDATA%\Programs\拉片软件`（Medium）。工作区 `build`/`dist` 是 Low；
     真实默认 AppData/凭据测试必须用安装版或 Medium 副本。

## 2. 任务边界（官方验收）

> T8.15：大会员可用档条件验收（非 MVP 门）。有账号才执行，无账号/无档位写
> “未验证”；不改默认 AVC ≤1080p，不阻塞 M1/M2。

**必须做**：

1. **O11-A1**：`playurl` 实际返回 116/74/120 时，档位菜单可见并标注
   fps/codec/“可用档”；记录实际 qn，不拿 `quality` 字段当真相。
2. **O11-A2**：1080P60/720P60 **AVC 可用**时自动选 116/74；实际 fps ≥59.5；
   **不显示降帧横幅**；已缓存区 20 次 PgUp/PgDn 帧号严格 ±1、p95 ≤80ms（硬解、
   热解码器、距缓存边界 ≥2s）。不达标只降“可用档”，不阻塞 MVP。
3. **O11-A3**：1080P60/720P60 **只有 HEVC/AV1** 时**不自动选**；手动菜单可选并
   标编码/60fps；选中后实际 fps 正确；未选时提示“有更高帧率档，可手动切换”，
   **不静默降到 30fps**。
4. **O11-A4**：4K **只手动**；能打开、播放、已缓存区逐帧不崩；**不承诺 P7/P9**；
   HDR/10-bit 颜色不保证。
5. **O11-A5**：从 4K/60 切回 1080P30：缓存/在线记录不丢；标记按 bvid+cid 保留；
   fps 不同按 PTS 映射并提示。
6. **O11-A6/A7**：报告逐项写 pass/partial/fail/unverified；**不得写 MVP 通过**；
   O11 失败不影响 M1/M2，O11 也不因 MVP 通过而自动通过。
7. **OP9**：1080P60/720P60 已缓存 P7 条件 p95 ≤80ms；**OP10**：4K 不设性能门槛，
   只记录能打开/播放/逐帧不崩。

**不做 / 不改**：

- 不重做 T8.16 安装/卸载/隐私；不卸载现有安装版；不清真实 `%LOCALAPPDATA%\拉片软件\cache`、
  `%APPDATA%\拉片软件` 记录/凭据/截图。
- 不改 M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema、T8.13 加固语义、
  T8.12 默认档位策略、T8.11 索引、T8.10 DASH 双输入、T8.6.1a 步进、T8.6.1b 预览。
- 不把 1080P60/4K 写成 MVP 通过；不把“菜单可选/能播”写成清晰度/性能保证；
  不做 HDR/杜比/10-bit 颜色正确、4K P7/P9、4K60 承诺。
- 不使用付费/DRM/地区限制内容作为通过样本；不绕过限制。
- 不在日志/文档/命令历史出现 SESSDATA、Cookie、签名 URL。

## 3. 现状与硬事实

### 3.1 账号

- 用户已确认大会员；先用**只读** `nav` 确认 `vip_status=1`（可记录 `vip_type`，不记录
  凭据细节）。若 `vip_status` 仍为 0/凭据失效：**停下**请用户重新登录，不要用普通
  账号冒充。
- 默认 DPAPI 凭据在 `%APPDATA%\拉片软件\credentials.dat`；构建目录 Low 进程可读，
  但不能写默认 AppData。T8.15 用显式测试根；若要改默认凭据/默认缓存，用安装版
  Medium 或 Medium 副本。
- 测试前后只读核对凭据文件大小/SHA-256 未变；不覆盖、不导出。

### 3.2 已有档位事实

- T8.9/T8.12 在**普通用户**下 3/3 DASH 1080P30（qn=80 AVC）；116/74/120 3/3
  不可得；`quality` 字段可能撒谎（匿名 qn=80 → quality=64，dash 只有 16/32）。
- 只有服务端 `dash.video[]` 的实际 qn/codecid/fps 算数；`requested_qn` 缺失时
  `OnlineSession` 明确失败，不自动换档。
- 大会员下 116/74/120 **是否返回、什么编码、什么 fps 尚未实测**。不要从“已登录
  VIP”推断档位可达；必须拉真实目录。

### 3.3 探针现状

- `--online-quality-test` 可做真实 `playurl(fnval=4048)` 档位目录/durl 对照，
  `--ui` 可验证菜单/合成 60/4K 规则/手动 1080→720；**没有**高档实际播放/seek/P7。
- `--online-m2-test` 的 `nav_normal_user` 前置会拒绝 `vip_status=1`；不要为了 T8.15
  改 M2 语义。建议新增独立 **`--online-vip-test`**（或等价 runner），输出逐项 A1–A7。
- `--m2-qn` 可设置 `Request.requested_qn` 的测试入口可复用思路，但 T8.15 需要
  自己记录实际 qn/codecid/fps、是否自动、菜单标注、降帧横幅、P7。

## 4. 建议实现路线

1. **先摸档位**：对 OB-2 `BV1mA411m7pS`（96s，源标题声明 60fps；普通用户
   实际 29.412fps）与 1–2 条公开 4K SDR 候选跑真实档位目录；记录 BVID/CID、
   `dash.video[]` 的 qn/codecid/fps/尺寸/带宽、实际 `playurl` 返回。OB-2 若只有
   1080P60 HEVC/AV1 就按 A3 走；不要假设 AVC，也不要把标题 60fps 当成 VIP 可达。
2. **新增 `--online-vip-test`**：
   - 选项：`--vip-qn 116|74|120|0`、`--vip-codecid 0|7|12|13`、
     `--vip-expect-auto 0|1`、`--vip-play-seconds`、`--vip-seeks`、
     `--vip-cache-root/--vip-pictures/--vip-records`。
   - 用产品 `StartOnlineSession`/`OnlineRequest` 启动；`requested_qn=0` 验证自动
     选档；`requested_qn>0` 验证手动档/实际返回。
   - 记录/断言：实际 qn、codecid、fps（≥59.5 for 60fps）、是否降帧横幅、菜单
     文案（fps/codec/可用档/手动）、4K 是否只手动、高档播放/seek、切回 1080P30
     后缓存/记录/标记/PTS 映射。
   - P7：选距缓存边界 ≥2s 的已缓存点，20 次 PgUp/PgDn 严格 ±1，记录 p50/p95/max；
     >80ms 标 partial/可用档，不改 OP9/M2 门。
3. **A3 编码约束**：HEVC/AV1-only 60fps 时 `requested_qn=0` 必须不自动选；手动
   `requested_qn=116/74 + codecid=12/13` 成功后才算“手动可选”；失败/服务端缺档
   写 fail/unverified，不伪造。
4. **4K**：只允许手动；`requested_qn=120`；能打开/播放/已缓存逐帧不崩即可；不跑
   P7/P9 门槛；HDR 只标“颜色不保证”。无 4K 样本 → A4 `unverified`。
5. **A5**：高档 → 1080P30 切档；检查缓存/在线记录/标记保留、fps 变化提示、PTS
   映射；不能只看菜单。
6. **报告**：`docs/perf/v2-online-vip.md` 逐项 A1–A7；verdict 用
   `pass/partial/fail/unverified`，绝不能写“MVP 通过”。

## 5. 测试与证据（最低要求）

命令示例（按实际参数调整）：

```powershell
$exe = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"  # 或 build\release\bin\lapian.exe + 显式测试根
& $exe --online-vip-test --bvid BV1mA411m7pS --vip-qn 0 --vip-expect-auto 1 `
  --vip-cache-root build\t8.15-cache --vip-pictures build\t8.15-pics `
  --vip-records build\t8.15-records

& $exe --online-vip-test --bvid BV1mA411m7pS --vip-qn 116 --vip-codecid 12 `
  --vip-play-seconds 20 --vip-seeks 20 ...
```

证据至少覆盖：

1. 账号确认：`nav vip_status=1`；实际 qn/codecid/fps；无 SESSDATA/签名 URL。
2. AVC 60 自动（若可用）：116/74、fps≥59.5、无降帧横幅、P7 20 次。
3. HEVC/AV1-only 60：不自动、手动菜单标注、手动选中后实际 fps；未选提示。
4. 4K：手动、能播、已缓存逐帧不崩；无 P7/P9/颜色承诺。
5. 高档→1080P30：缓存/记录/标记保留、PTS 映射提示。
6. 失败样本/未验证项必须写；不达 OP9 写 partial/可用档。
7. 日志 0 SESSDATA/Cookie/签名 URL；真实缓存/记录/凭据未被破坏。

回归：

- `ctest` 36/36（若新增单测记录新总数）；本地 `--key-test` 77/0。
- 若只改探针/质量测试、不碰 `MainWindow` 预览路径：可引用 T8.6.1b `b11e45a`
  的 36/36；若碰 `MainWindow`/预览，至少重跑一次 `--online-drag-preview-test`
  的 DASH 或 cache-hit 用例。
- M1/M2 不重跑成新门；引用 T8.8/T8.14。若碰 `OnlineSession` 请求/切档，跑 1 条
  原有 M2 普通用户样本并注明“不是 M2 重跑”。
- `scripts\bench-regression.ps1 -Suites startup,step1080` 热缓存 P1/P2/P7；漂移要写。
- `dist` 可构建；日志脱敏。

## 6. 停止条件

- 无大会员凭据 / `vip_status` 不是 1 / 无法在 Medium 环境读写默认凭据 → **停下**
  请用户重登；不要用普通账号冒充。
- 服务端不返回 116/74/120：先换公开样本；仍无 → `unverified`；不得强行用
  `requested_qn` 或 `quality` 字段伪造实际档位。
- 4K HEVC/AV1 崩溃/卡死/无法逐帧：先记录并停，不带病验收；不写“无崩溃/已证明稳定”。
- 需要改 M1/M2 门、验收数字、OB 样本、默认 AVC ≤1080p、cache schema、T8.13 加固、
  T8.12 默认策略、T8.11 索引、T8.10 DASH、T8.6.1a/b 语义才能继续 → **停下问**。
- 4K HDR/杜比/10-bit 颜色、4K P7/P9、4K60：不在范围，只能写未验证/不承诺。

## 7. 完成时报告

1. **T8.15 判定**：pass/partial/fail/unverified；**非 MVP 门**，不写 MVP 通过。
2. **A1–A7 表**：每项结果/数字/样本；AVC vs HEVC/AV1、自动 vs 手动、实际 fps。
3. **P7/OP9**：20 次 p50/p95/max、是否 ≤80ms；不达标降“可用档”。
4. **4K/OP10**：能否打开/播放/逐帧；不写 P7/P9/颜色承诺；无样本写 unverified。
5. **切档/A5**：缓存/记录/标记/PTS 映射。
6. **回归**：ctest/key/T8.6.1b（或引用）/M2（或引用）/P1/P2/P7/脱敏；M1/M2 门与
   数字未改。
7. **文档**：`docs/perf/v2-online-vip.md`、`docs/handoff-2026-10-24-t8.15.md`、
   `tasks-v2.md` v0.29、`AGENTS.md` v6.41；一个可审查 commit，`git status` clean。
8. **下一步**：T8.15 完成后 v2 条件验收清空；若 partial/fail，列缺口/是否另立任务，
   不改 M1/M2 门。
