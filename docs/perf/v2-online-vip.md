# T8.15 大会员可用档条件验收（O11）证据（2026-10-24）

- **版本**：v1.0
- **状态**：**partial（部分通过）**。O11 条件验收，**非 MVP 门**，不阻塞 M1/M2。
  1080P60 AVC 自动、4K SDR 手动、60/4K→1080P30 切档通过；**A3（HEVC/AV1-only
  60fps）无真实样本 → unverified**；720P60（qn=74）两次目录均未返回 → 该子项
  unverified；报告**不写 MVP 通过**。
- **基线**：HEAD `9510e59`（代码基线 `b11e45a`）；开始前 `ctest` 36/36、key
  77/0、longpress 54/0、dist 34.37MB/25。
- **账号**：用户确认的大会员账号；默认 DPAPI 凭据
  （`%APPDATA%\拉片软件\credentials.dat`）只读自检 **`nav vip_status=1`**。
  凭据哈希测试前后一致（SHA-256 `F406C6…2EC33`，484B，未覆盖/未导出）。
- **范围**：O11-A1…A7、OP9、OP10。**不含** HDR/杜比/10-bit 颜色正确、4K
  P7/P9、4K60 性能承诺、720P60 可达承诺。
- **探针**：新增独立 `--online-vip-test`（`src/app/OnlineVipTestRunner.*`）。
  复用 `OnlineSession.requested_qn/requested_codecid`、`QualityMenu`、
  `FrameRatePolicy`；**未改** M2 runner 的 `nav_normal_user` 语义、M1/M2
  门/数字、OB 样本、`lapian-cache-1` schema、默认 AVC ≤1080p。

---

## 1. 结论先行

| ID | 结果 | 证据摘要 |
|---|---|---|
| O11-A1 | **pass** | 大会员 `playurl(fnval=4048)` 实际返回 qn=116/120；档位按钮可见；菜单标 `1080P60 · AVC · 62.5fps`、`（60fps / AVC · 自动优先）`、`4K …（仅手动 · 可用档：性能/颜色不保证）`；页脚“菜单可选不等于保证可达” |
| O11-A2 | **partial** | 1080P60 **AVC** 自动选中（qn=116、codecid=7）；容器 60.000fps / dash 62.500fps（≥59.5）；0 降帧横幅；20 次 PgUp/PgDn 严格 ±1、p95 **47.5ms** ≤80ms（硬解、距缓存边界 179 帧≈2.86s）。**720P60（qn=74）未返回**，该子项 unverified |
| O11-A3 | **unverified** | 所有实测目录的 116 档都含 AVC，没有 HEVC/AV1-only 60fps 真实样本；未伪造。策略/菜单仅在合成目录（T8.9/T8.12 ctest）覆盖。**代码审计：没有“有更高帧率档，可手动切换”的目录感知提示**（详见 §7） |
| O11-A4 | **pass** | 4K SDR 样本 `BV1afnv6GEDA`：只用 `requested_qn=120` 手动打开（自动仍选 116）；AVC/HEVC 4K 均能播放（6–8s，帧 0→299/477）；已缓存区 20/10 次逐帧落点全对、无崩溃；**不承诺 P7/P9/颜色**；HDR 样本只做目录、未用于颜色通过 |
| O11-A5 | **pass** | 116→80：PTS 500,000 → 第 15 帧、fps 62.5→29.412、提示“按 PTS 映射”；标记 1→1、在线记录保留、旧档缓存目录保留、缓存字节只增；120→80 同样通过（PTS 500,000→第 15 帧、fps 60→30） |
| O11-A6 | **report** | 本文写 **partial（部分通过）**；有账号，不写“未验证”也不写“MVP 通过” |
| O11-A7 | **report** | M1/M2 门与数字未改；O11 失败/未验证不影响 M1/M2 判定，O11 也不因 MVP 通过而自动通过 |

**总判定：partial（部分通过）**。可用档结论只写到“大会员条件验收”：不进入
MVP 验收，不改变 1080P30 默认档，不把 4K/60 写成保证。

---

## 2. 账号与凭据（脱敏）

| 项 | 值 |
|---|---|
| 凭据来源 | 默认 DPAPI `%APPDATA%\拉片软件\credentials.dat`（`credential_source=dpapi_default`） |
| nav | `login=1`、`vip_status=1`、`api_code=0`、HTTP 200、73–178ms、`cookie_sent=1` |
| 凭据文件 | 484B，测试前后 SHA-256 相同；未写入、未导出、未打印 SESSDATA/Cookie |
| 进程 | `build\release\bin\lapian.exe`（Low integrity）只读默认凭据 + 显式测试根；未写默认 AppData |

所有日志只出现 `cookie_sent=1` 标志；**0** SESSDATA 值、0 Cookie 值、0 签名 URL
（扫描 `build\t8.15-*.log`；只有 `vip_nav` 的布尔标志命中关键字，无凭据内容）。

---

## 3. 样本与真实目录（只信 `dash.video[]`）

### 3.1 OB-2：`BV1mA411m7pS` / cid `1004967230`（96s）

标题 `【静止画MAD】-よふかしのうた by fvrein [UHD 60FPS][CC字幕]`；大会员下
`playurl` 实际返回 15 条视频流 / 3 条音频。`qn=80`（自动请求）与 `qn=120`
（最大请求）返回**同一档位梯**：

| qn | 编码 | codecid | fps | 尺寸 | 带宽 | 手动? |
|---|---|---|---|---|---|---|
| **116** | **AVC** | 7 | **62.500** | 1920x1080 | 4,221,714 | 否（AVC 60 可自动） |
| 116 | HEVC | 12 | 58.824 | 1920x1080 | 673,600 | 否（同档有 AVC） |
| 116 | AV1 | 13 | 60.150 | 1920x1080 | 888,215 | 否（同档有 AVC） |
| 80 | AVC | 7 | 29.412 | 1920x1080 | 2,124,719 | 否 |
| 64 | AVC | 7 | 29.412 | 1280x720 | 1,426,660 | 否 |
| 32/16 | AVC/HEVC/AV1 | — | 29–30 | 852x480 / 640x360 | — | 否 |

- `DefaultQn()=116`、`DefaultStream()=AVC(7)`。
- **qn=74（720P60）不存在**；**qn=120（4K）不存在**。
- 与普通用户（T8.9/T8.12）相比：普通用户 3/3 只到 qn=80；同一凭据换成大会员
  后 OB-2 真实出现 1080P60 AVC。`quality` 字段仍不可信（本响应 `quality=80`，
  而目录有 116）。

### 3.2 4K SDR：`BV1afnv6GEDA` / cid `42913040245`（27s，2160x3840 竖屏）

| qn | 编码 | codecid | fps | 尺寸 | 带宽 | 手动? |
|---|---|---|---|---|---|---|
| **120** | AVC | 7 | 60.000 | 2160x3840 | 25,901,155 | **是（仅手动）** |
| 120 | HEVC | 12 | 60.000 | 2160x3840 | 5,192,401 | 是 |
| 120 | AV1 | 13 | 60.000 | 2160x3840 | 5,364,494 | 是 |
| 116 | AVC | 7 | 60.000 | 1080x1920 | 4,021,330 | 否 |
| 80 | AVC | 7 | 30.000 | 1080x1920 | 2,192,505 | 否 |

- 自动 `DefaultQn()=116`，**不会自动选 4K**；`requested_qn=120` 可手动打开。
- 该样本是 **4K60 SDR**（强于“4K ≤30fps 可用档”）；OP10 只要求能开/能播/
  已缓存逐帧不崩，**不把 4K60 写进性能承诺**，也不承诺颜色。
- 4K 档在 `qn=80` 请求里也返回（`auto80_has_4k=1`），但产品默认档仍是 116；
  “返回”不等于“自动选”，符合 O6/O11 只手动。

### 3.3 其他候选

| 样本 | 结果 |
|---|---|
| `BV1dJjv6PEWF`（4K HDR 高码率测试样片，15P/2399s） | 目录有 qn=120 3840x2160@59.932（AVC/HEVC/AV1）；**HDR，不作为 SDR/颜色通过样本**，只证明账号可取 4K |
| `BV1e1h46LEHk`（4K 修复） | `view` 62002 稿件不可见 → 不测试 |

---

## 4. A1/A2：1080P60 AVC 自动 + OP9

命令（默认 DPAPI 凭据；显式测试根由 `--online-vip-test` 默认使用）：

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
build\release\bin\lapian.exe --online-vip-test --vip-catalog-only `
  --bvid BV1mA411m7pS --tag ob2
build\release\bin\lapian.exe --online-vip-test `
  --bvid BV1mA411m7pS --tag ob2-auto --vip-qn 0 --vip-expect-auto 1
```

结果（`build\t8.15-auto-ob2.log`）：

- `checks=36 failures=0`；`actual_qn=116 actual_codecid=7`；
  `actual_fps=62.500`（容器 `60.000`）、`drop=0`、`banner=0`、`hw=1`。
- 档位菜单：`1080P60 · AVC · 62.5fps · 1920x1080（60fps / AVC · 自动优先）
  （当前播放）`；按钮可见；`checked=1`。
- P7：`n=20 failures=0 p50=45.0 p95=47.5 max=56.0`，硬解；
  `stable=210 start=30 margin_frames=179`（62.5fps 下 ≈2.86s ≥2s）。
- 状态栏：`在线 · 1080P60 · 1920x1080 · 实际 62.5fps（容器 60fps）`。
- 无降帧横幅（扫描中/超范围横幅不算降帧；`OnlineFrameRateDrop=false`）。

**判定**：1080P60 AVC 子项 pass；`qn=74` 目录不存在，720P60 子项 unverified，
因此 A2 写 **partial**。OP9 的 1080P60 条件满足（p95 47.5ms）。

---

## 5. A4/OP10：4K 只手动、能播、逐帧不崩

```powershell
# 直接手动选 4K AVC（requested_qn=120, codecid=7）
build\release\bin\lapian.exe --online-vip-test --bvid BV1afnv6GEDA `
  --tag ob2-4k-avc --vip-qn 120 --vip-codecid 7 --vip-play-seconds 8 --vip-seeks 20
# 手动 4K HEVC（测试钩子强制 codecid=12；产品菜单按 tier 首选编码）
build\release\bin\lapian.exe --online-vip-test --bvid BV1afnv6GEDA `
  --tag ob2-4k-hevc --vip-qn 120 --vip-codecid 12 --vip-play-seconds 6 `
  --vip-seeks 10 --vip-switch-qn 0
```

| 轮次 | 结果 | 播放 | 逐帧（OP10 无门槛） |
|---|---|---|---|
| 4K AVC（首跑） | A4 pass；整体 27 项/1 失败（失败在 A5 切档，见 §6.3） | 8s，帧 0→477 | 20/20 落点精确，p50 51.0 / p95 60.0 / max 61.0ms，hw=1 |
| 4K AVC（复跑） | `checks=34 failures=0` | 6s，帧 0→358 | 10/10，p50 52.0 / p95 77.3 / max 80.0ms，hw=1 |
| 4K HEVC | `checks=23 failures=0` | 5s，帧 0→299 | 10/10，p50 29.0 / p95 31.0 / max 31.0ms，hw=1 |
| 4K cache-hit（120-7 完整缓存） | A4 pass | 6s，帧 0→358 | 10/10，p50 2.5 / p95 138.9 / max 188.0ms（**4K 不设门槛，只记录**） |

- 菜单：`4K · AVC · 60.0fps · 2160x3840（仅手动 · 可用档：性能/颜色不保证）
  （当前播放）`；自动轮 `actual_qn=116`，**没有自动选 120**。
- 状态栏显示 `4K · 2160x3840 · 实际 60fps`；T6.5 横幅
  `超出保证范围（2160×3840）：仍可播放，性能不保证`（不是降帧横幅）。
- HEVC 4K 是测试钩子强制 `codecid=12`；产品菜单每个 qn 只列首选编码
  （本样本首选 AVC），因此该轮菜单只能证明“tier 存在”，不能证明产品可让用户
  选 HEVC 编码。**不写“4K HEVC 用户可选”**。
- **不承诺 P7/P9**：上面的毫秒数只作记录；4K cache-hit p95 138.9ms 不作为失败，
  也不作为通过门槛。
- HDR/10-bit 颜色：未测、不承诺；4K HDR 候选只做目录。

**判定**：A4 pass（4K SDR AVC 手动；HEVC 解码/逐帧通过；无崩溃）。颜色与 4K
性能不在承诺内。

---

## 6. A5：高档切回 1080P30（缓存/记录/标记/PTS）

### 6.1 1080P60 → 1080P30（OB-2，自动轮）

- 切档点击 134ms；`fps 62.5 → 29.412，按 PTS 映射`；
- PTS 500,000 → 第 15 帧（旧第 30 帧）；当前位置 PTS 500,000 不变；
- 标记 1→1，标记 PTS 仍 500,000；在线记录文件保留；
- 缓存文件 4→6、字节 16,183,494→41,565,324；旧档目录 `116-7` 仍在。

### 6.2 4K → 1080P30（4K 样本复跑）

- 切档点击 29–118ms；`4K → 1080P`；
- 提示 `已切换到 1080P（qn=80）；原 PTS 映射到第 15 帧（旧第 30 帧，帧号可能
  变化）；fps 60 → 30，按 PTS 映射`；
- 标记 1→1、记录保留、旧档 `120-7` 目录保留；缓存 10→12 文件、
  124,815,722→132,014,969B。

### 6.3 一次未复现的 4K 切档失败

4K 首跑在 120→80 时新档 `80-7` 第一次下载写入返回
`error=写入缓存文件失败 bytes=0`，随后 `下载已取消`，切档未落地（`state=ready`
但 qn 仍 120）。同一命令复跑通过（见 §6.2）。另一次 4K HEVC 首开在音频已缓存时
报 `DASH 条目已完整缓存，但本次未走缓存命中路径；请重新打开`，清音频后复跑通过。
两次都**不是崩溃/卡死**，未定位；不写“无失败/已证明稳定”，作为残余风险记录。

**判定**：A5 pass（60→30 与 4K→30 均有成功证据）；上述瞬时失败不掩盖，也不
据此改 M2/OP9 数字。

---

## 7. A3：HEVC/AV1-only 60fps（无真实样本）

- 实测 OB-2、4K SDR、4K HDR 的 116 档都含 AVC → **没有“只有 HEVC/AV1”的真实
  60fps 样本**；不伪造 A3 通过。
- 策略/菜单的合成覆盖已在 T8.9/T8.12：HEVC-only 60fps `DefaultQn=0`、菜单
  `60fps / HEVC（手动）`、不自动；4K 仅手动。ctest `lapian_online_dash_test`
  125 项、`lapian_online_frame_rate_test` 27 项仍通过。
- **代码审计缺口**：`FrameRatePolicy` 只在“已选 60 档”或“标题声明 60fps”时提示
  降帧；它不读取目录里的 116/74 档。O11-A3 要求的
  “有更高帧率档，可手动切换”提示在源码中 **0 命中**。若真实遇到
  HEVC/AV1-only 60 + 1080P30 自动回退，当前产品不会主动提示“可手动切 60”，
  只会由菜单列出该档。**A3 仍写 unverified（无样本）；该提示子项按代码审计不达标，
  拿到样本后预计 partial/fail。**
- 本次**未改** `FrameRatePolicy`/默认策略；需要补提示时应另立任务，不塞进
  T8.15 的只读条件验收。

---

## 8. 回归与审计

| 项 | 结果 |
|---|---|
| `ctest` | **36/36 pass**（61.49s；TMP 指向工作区 `.deps\tmp`，与 build-release 一致；系统 TMP 对 Low 进程不可写会造成误报） |
| 本地 `--key-test` | **77/0** |
| P1/P2/P7（热缓存，`bench-regression -Suites startup,step1080`） | PASS，0 超差：P1 p50/p95 **291.61/306.98ms**、P2 **355.4/372.79ms**（20/20）；P7 back p50/p95/max **0.21/0.9/52.27ms**、fwd **0.24/0.37/9.58ms** |
| T8.6.1b 预览 | **引用** `b11e45a`（本轮未改 `MainWindow`/`OnlinePreviewDecoder` 预览路径；只加只读 getter） |
| M1/M2 | **引用 T8.8/T8.14**，未重跑成新门；未改 `OnlineSession`/验收数字 |
| `dist` | **34.48MB / 25 文件**（新增探针 +0.11MB）；P12 ≤100MB；网络导入检查通过（无 QtNetwork/TLS/winhttp/wininet/urlmon） |
| 默认数据 | 未写默认缓存/在线记录；默认缓存最新 17:24、记录 17:25（均早于本轮）；凭据哈希不变 |
| 脱敏 | 0 SESSDATA/Cookie 值/签名 URL；只有 `cookie_sent=1` 标志 |

---

## 9. 未验证 / 不承诺

1. **720P60（qn=74）**：OB-2 与 4K 样本都没返回；A2 的 720P60 子项 unverified。
2. **HEVC/AV1-only 60fps**：无真实样本；A3 unverified；提示缺口见 §7。
3. **4K HDR/杜比/10-bit 颜色**：不测颜色正确；HDR 样本只做目录。
4. **4K P7/P9 性能**：不设门槛、不承诺；只记录能播/逐帧不崩。
5. **4K60 性能**：样本是 4K60；不写进可用档性能承诺。
6. **切档瞬时失败**：§6.3 两次一次性失败未复现，残余风险；不写“无崩溃/已证明稳定”。
7. **真实用户手动路径**：4K 通过 `requested_qn=120` 直接打开 + 菜单动作触发；
   4K HEVC 的“编码选择”不是产品 UI 能力（菜单按 tier 首选编码）。
8. T8.14 残余 teardown `0xC0000374`/seed 卡住未定位；本轮未新增同类观察，但
   不能据此写“无崩溃”。

---

## 10. 判定与边界

- **T8.15 = partial（部分通过）**；**不是 MVP 通过**，不改变 M1/M2 门/数字。
- 结论只覆盖大会员可用档：1080P60 AVC 自动 + OP9 1080P60 p95 47.5ms；4K SDR
  仅手动、能播/逐帧；60/4K→1080P30 切档保留缓存/记录/标记/PTS。
- 不写“1080P60/4K 对普通用户可达”，不写“HEVC/AV1-only 60 已验收”，不写
  “4K 颜色/性能已保证”。
