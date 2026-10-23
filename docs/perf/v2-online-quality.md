# T8.9 证据：DASH 解析与档位（2026-10-24）

- **任务**：[tasks-v2.md](../../tasks-v2.md) **T8.9**（M2 第 1 项）；依据 [spec-v1.0-delta.md](../spec-v1.0-delta.md) v0.4 的 O6/O11/DEC-2/DEC-4、[ADR-007](../adr/ADR-007-bilibili-online.md)。
- **状态**：**T8.9 完成（不等同 M2 门）**。DASH 解析/档位目录/档位菜单（只列示）已实现；**DASH 双输入播放、切档策略、降帧提示、拖动预览、M2 门均未做**（T8.10–T8.14）。
- **一句话**：登录普通用户 3/3 样本在 `playurl(fnval=4048)` 的 `dash.video[]` 里拿到 **1080P AVC（qn=80）**，默认选 AVC；`data.quality` 会撒谎（匿名请求 qn=80 → `quality=64`，dash 只有 16/32），档位判定只信 `dash.video[]`；1080P60/720P60/4K 对普通用户 3/3 不可得，DASH 目录里根本不出现，因此只做了“进菜单、标仅手动、置灰 + 说明 T8.10”，**没有把菜单可选写成保证可达**。
- **基线**：`cbab29b`（T8.3.1）+ T8.9 变更；**M1/M2 门、验收数字、OB 样本未改**。

---

## 1. 结论与判定

| 项 | 结果 |
|---|---|
| 3 条档位矩阵与 S2 一致 | **pass**：OB-1/2/3 的 DASH 目录都含 `qn=80`（1920×1080，AVC），实际 fps **30.000 / 29.412 / 25.000**；与 S2 §2.2 的 `dash 上限 80` 一致（S-3 本来就是 25fps） |
| 登录普通用户 1080P30 3/3 | **3/3 的 1080P 档可得（qn=80）**；严格“30fps”只有 OB-1（30.000）、OB-2 是 29.412（60fps 源的 1/2 副本）、OB-3 是 25fps 源。不写“3/3 都是 30fps” |
| 普通用户 60/4K 不可得 | **记录在案**：3/3 的 `dash.video[]` 都没有 116/74/120；请求 `qn=116`/`qn=120` 也只返回 `quality=80`，目录不变 |
| 1080P60/4K 进菜单（只手动） | **菜单模型 + 真实 QMenu**：合成大会员目录下 1080P60/720P60/4K 都出现，标“仅手动”（4K 另标“可用档：性能/颜色不保证”），T8.9 下置灰并写明“DASH 双输入在 T8.10 接入”；**合成目录只用于 UI 规则验证，不是“可达”证据** |
| 大会员档按 O11/T8.15 条件验收 | **未验证**：本机凭据 `vip_status=0`（普通用户），O11 需要大会员账号，由 T8.15 执行；本文件**不写** 1080P60/4K 通过 |
| HEVC-only 60fps 只手动 | **pass**：目录里只有 `116/HEVC` 时 `DefaultQn()=0`（不自动选），菜单标“1080P60 · HEVC · 60.0fps（仅手动）” |
| 错误 qn 不崩 | **pass**：`qn=116 / 0 / 9999` 均 `code=0`、目录可解析、`DefaultQn=80`；`qn<=0` 被规范化为默认 64，不把 0 发给服务端 |
| 音轨存在 | **pass**：3/3 各有 3 条 `dash.audio[]`（AAC，init/index + 2 backup）；OB-2/OB-3 的 30216 是 HE-AAC（`mp4a.40.5`） |
| 不把 durl 1080p 1/3 当保证 | **记录**：durl(qn=80) 实际 OB-1=720P(64)、OB-2=1080P(80)、OB-3=720P(64) → 1/3 命中，与 S2 一致；**不写成保证** |
| 不把“菜单可选”写成“保证可达” | **pass**：菜单每档带真实返回的 `qn/codecid/fps/init/index/备份数`；DASH 档在 T8.9 置灰并给原因；页脚固定写“菜单可选不等于保证可达” |
| 证据 | 本文件 + `--online-quality-test`（无 GUI/`--ui`）+ `lapian_online_dash_test`（117/0） |

**没有判定 M2 门**；没有写 4K/1080P60 可用；没有接 DASH 播放（T8.10）、切档策略（T8.12）、降帧横幅（DEC-4/T8.12）。

---

## 2. 接口事实（2026-10-24 实测，3 条 OB）

### 2.1 请求

```text
https://api.bilibili.com/x/player/playurl
  ?bvid=<BV>&cid=<cid>&qn=80&fnval=4048&fnver=0&fourk=1
```

- `fnval=4048` = 16(DASH) + 64(HDR) + 128(4K) + 256(Dolby audio) + 512(Dolby vision) + 1024(8K) + 2048(AV1)。
- `qn=80` 是**请求上限**（不请求 4K 档），`fourk=1` 只让服务端返回“账号允许的档位梯”；普通用户返回的梯子仍只到 1080P。
- 一次请求返回**完整档位梯**（16/32/64/80 × AVC/HEVC/AV1），不是“只返回请求档”。因此档位菜单不需要逐档请求（T8.10/T8.12 可直接复用）。
- 请求 `qn` 只影响 `data.quality`（服务端“选中档”），**基本不影响** `dash.video[]` 列表。

### 2.2 响应字段（`data.dash`）

| 字段 | 实测 | 说明 |
|---|---|---|
| `dash.duration` | `111 / 96 / 213`（秒） | 与 `data.timelength`（ms）一致；解析为 ms |
| `dash.video[].id` | `16/32/64/80` | **实际 qn**，档位判定唯一依据 |
| `dash.video[].codecid` | `7/12/13` | AVC/HEVC/AV1；同一 qn 三条都在 |
| `dash.video[].width/height` | 640×360 / 852×480 / 1280×720 / 1920×1080 | 1080P 为 1920×1080 |
| `dash.video[].frameRate` | 字符串，如 `"30.000"` | OB-2 的 AVC=29.412、HEVC=30.303、AV1=30.019（**同片不同编码 fps 不同**） |
| `dash.video[].bandwidth` | 1080P AVC 2.12–2.63 Mbps；HEVC/AV1 ≈ 1/3 | 见 §3 |
| `dash.video[].size` | **不存在** | 解析为 0=未知；**不伪造**总字节（T8.10 用 Content-Length/末段 Range 对账） |
| `dash.video[].baseUrl/base_url` | 1 条主 URL | 带签名/过期参数，不写日志/meta |
| `dash.video[].backupUrl` | 2 条 | 主 + 2 备份 = 3 个 URL；全 https |
| `SegmentBase.Initialization` | 如 `0-917` | 闭区间字节范围 |
| `SegmentBase.indexRange` | 如 `918-1245` | 闭区间；**不保证紧接 init**（OB-2 AV1：init `0-1022`、index `1063-1334`；OB-3 AV1：`0-1070`/`1135-1682`） |
| `dash.audio[].id` | `30216/30232/30280` | **不是 qn**；AAC 质量 id |
| `dash.audio[].codecid` | `0` | 响应不给真实 codecid；用 `codecs`（`mp4a.40.2` / `mp4a.40.5`）区分 |
| `dash.audio[].size` | 不存在 | 同上，0=未知 |
| `data.quality` | 80（请求 80）、80（请求 116/120）、**64（匿名请求 80，但 dash 只有 16/32）** | **不可信**，只记录为证据 |
| `data.format` | `flv`（DASH 请求） | 与 durl 的 `mp4`/`mp4720` 不同；不用于档位判定 |
| `dash.dolby` / `dash.flac` | 存在（本 3 条为空） | T8.9 不解析；多音轨/杜比属后续 |

### 2.3 解析实现（`src/online/PlayUrl.*`）

- `kDashFnval = 4048`、`kDashRequestQn = 80`；`BuildDashPlayUrlUrl()` 与 M1 的 `BuildPlayUrlUrl()`（fnval=0）分开，M1 URL 字节不变。
- `DashStream / DashAudioStream / QualityTier / DashCatalog`：qn/codecid/fps/带宽/size(0=未知)/init/index 闭区间/主+备份 URL/mime/codecs/startWithSap。
- 单条坏字段只跳过该条并计数（`skipped_video_entries`/`skipped_audio_entries`），dash 缺失或视频为空才整体失败（`no-dash`/`no-video`），保证“错误 qn/坏数据不崩”。
- `RebuildDashTiers()`：按 qn 降序归并，变体按 AVC→HEVC→AV1 排序，展示字段取 AVC。
- `DefaultQn()`：O6 顺序 `1080P60→720P60→1080P30→720P30→480P30`，**只选 AVC**、**跳过只手动档**；没有 AVC 时回退最高非手动档；只有手动档 → 0（不自动）。
- `IsManualOnlyQuality(qn)`：116/74（60fps）与 ≥120（4K/HDR/Dolby/8K）为只手动；112（1080P+）不是只手动，但默认排在 80 之后（避免默认选更高码率）。
- `src/online/QualityMenu.*`：菜单模型（header/tier/note），只依赖 Qt Core，便于单测；T8.9 `dash_playback_available=false`，DASH 档全部置灰并给原因。

---

## 3. 3 条档位矩阵（登录普通用户；`vip_status=0`）

`--online-quality-test`，每条：nav → view → DASH(fnval=4048,qn=80,fourk=1) → durl(qn=80,fourk=0) → 匿名 DASH。3 条 **27/0、39/0、verdict=pass**。

| 样本 | 标题（截断） | DASH 梯（qn:编码） | 1080P AVC | 实际 fps | 1080P AVC 带宽 | init/index | audio |
|---|---|---|---|---|---|---|---|
| **OB-1** `BV1BMnv6jE5S` cid 42915922022 | 在超市后门吸烟的二人【静止系MAD】 | 16/32/64/80 × AVC,HEVC,AV1 | 1920×1080 AVC | **30.000** | 2,249,181 | `0-917` / `918-1245` | 30280 93,657 · 30216 65,740 · 30232 93,657（均 `mp4a.40.2`） |
| **OB-2** `BV1mA411m7pS` cid 1004967230 | 【静止画MAD】-よふかしのうた…[UHD 60FPS] | 16/32/64/80 × AVC,HEVC,AV1 | 1920×1080 AVC | **29.412** | 2,124,719 | `0-996` / `997-1256` | 30280 103,675 · 30216 48,728（`mp4a.40.5`）· 30232 103,675 |
| **OB-3** `BV1GJ411x7h7` cid 137649199 | 【官方 MV】Never Gonna Give You Up | 16/32/64/80 × AVC,HEVC,AV1 | 1920×1080 AVC | **25.000** | 2,629,444 | `0-1008` / `1009-1544` | 30280 203,786 · 30216 43,962（`mp4a.40.5`）· 30232 102,931 |

其他实测：

| 样本 | `claimed quality`（请求 80） | 在档位列表 | `accept_quality` | 默认 qn/codecid | 116/74/120 | durl(qn=80) 实际 | durl 字节/format |
|---|---|---|---|---|---|---|---|
| OB-1 | 80 | 是 | 112/80/64/32/16 | **80 / 7** | 无 / 无 / 无 | **64（720P）** | 14,330,392 · `mp4720` |
| OB-2 | 80 | 是 | 116/80/64/32/16 | **80 / 7** | 无 / 无 / 无 | **80（1080P）** | 26,682,935 · `mp4` |
| OB-3 | 80 | 是 | 112/80/64/32/16 | **80 / 7** | 无 / 无 / 无 | **64（720P）** | 51,973,319 · `mp4720` |

- **durl 1080p 只有 1/3（OB-2）**：与 S2 “1/3 样本”一致；不写成保证，也不作为 M2 证据。
- **普通用户 60/4K 3/3 不可得**：`dash.video[]` 没有 116/74/120；请求 `qn=116`/`qn=120` 仍 `quality=80`。
- **同片不同编码 fps 不同**（OB-2）：AVC 29.412 / HEVC 30.303 / AV1 30.019；默认选 AVC，所以默认流 fps=29.412（与 T8.6/T8.8 的观测一致）。

### 3.1 1080P 各编码（AVC 默认，HEVC/AV1 只解析）

| 样本 | AVC | HEVC | AV1 |
|---|---|---|---|
| OB-1 | 2,249,181 bps · 30.000 | 830,236 · 30.000 | 740,565 · 30.000 |
| OB-2 | 2,124,719 · 29.412 | 426,952 · 30.303 | 401,614 · 30.019 |
| OB-3 | 2,629,444 · 25.000 | 749,418 · 25.000 | 603,657 · 25.000 |

### 3.2 匿名对照（无 Cookie，同一 cid，qn=80/fourk=1）

| 样本 | dash 梯 | 默认 qn | `claimed quality` | quality 在档位列表 | audio |
|---|---|---|---|---|---|
| OB-1 | 16/32 × AVC,HEVC,AV1 | 32 | **64** | **否**（无 64 档） | 3 条 |
| OB-2 | 16/32 | 32 | **64** | **否** | 3 条 |
| OB-3 | 16/32 | 32 | **64** | **否** | 3 条 |

→ **`quality` 字段会撒谎**（S2 的结论在 T8.9 复现）：匿名请求 qn=80 返回 `quality=64`，但 `dash.video[]` 只有 16/32。解析层只用 `dash.video[].id` 定档，`quality` 只写入 `claimed_quality` 供证据。

---

## 4. 错误 qn / 健壮性

OB-1，同一 cid，`--odd-qn`（3 条额外请求）：

| 请求 qn | HTTP | code | `claimed quality` | 目录 | `DefaultQn` | 崩溃 |
|---|---|---|---|---|---|---|
| 116（1080P60） | 200 | 0 | 80 | 16/32/64/80 | 80 | 否 |
| 0 | 200 | 0 | 80 | 16/32/64/80 | 80 | 否（URL 里规范化为 64，不发字面 0） |
| 9999 | 200 | 0 | 80 | 16/32/64/80 | 80 | 否 |

- `qn=116` 不会“成功拿到 1080P60”：`quality=80` 且目录无 116。
- 坏字段（缺 id / 缺 codecid 但 codecs 可推断 / 非 https URL / 坏 range）由单测覆盖：跳过或保留但 `urls_ok=false`/`init_ok=false`，不整体失败。

---

## 5. 档位菜单（产品路径）

### 5.1 真实窗口 `--online-quality-test --ui`（OB-1）

```text
online_quality_ui_state current_qn=64 quality=ok=1 cache_hit=0 fetched=1 http_status=200 elapsed_ms=88 requests=1
online_quality_ui_requests durl=1 quality=1
online_quality_ui_result checks=21 failures=0 verdict=pass
```

- 首帧后异步取一次 `fnval=4048`（**+1 个 API 请求**；失败不影响播放）；OP1 关键路径无变化（见 §7）。
- 状态栏“清晰度”按钮在首帧后可见；菜单来自实际返回：
  - `1080P · AVC · 30.0fps · 1920x1080`——**列示但置灰**（原因“DASH 双输入在 T8.10 接入”）。
  - `720P · AVC · 30.0fps · 1280x720（当前播放）`——勾选、可点；点击只提示“已经是当前播放档位：720P”，**不重取流**。
  - 480P/360P 同样置灰；页脚“DASH 档位仅列示：T8.9 不做 DASH 播放/切档（T8.10/T8.12）；菜单可选不等于保证可达”。
- 点击非当前档（360P）被拒绝，tooltip 里带真实 `init 0-916 / index 917-1244 / 备份 URL 2 个` 与 T8.10 原因。
- 合成大会员目录（80 AVC + 116 HEVC + 120 HEVC）注入同一个真实窗口：`1080P60 …（仅手动）`、`4K …（仅手动）（可用档：性能/颜色不保证）` 均出现在 QMenu 且 `enabled=0`。**这是 UI 规则验证，不是可达证据。**

### 5.2 缓存命中

- 完整缓存命中走 `CacheEntry`（bvid+cid+qn+codecid）构造单档目录：**0 网络**，菜单 header 写“缓存命中档位（未联网；来自缓存 meta）”；meta 没有 fps/分辨率时不伪造（只显示档位名/编码）。

### 5.3 一个实测坑（给 T8.12/T8.15）

状态栏里用 `QPushButton::setMenu()` 做菜单按钮，**首次绘制阻塞 UI ~620ms**：M1 E2E 的 `ui_gap` 从基线 62ms 变成 679ms，`first_frame` 656→1276ms；事件过滤器定位到 `QEvent::Paint` + `QPushButton`。改为“普通按钮 + 点击时按需创建/弹出 QMenu”后恢复（gap 59ms、first_frame 640ms）。后续加档位/设置菜单时不要用 `QPushButton::setMenu()` 放在状态栏。

---

## 6. 离线单测与探针

| 项 | 结果 |
|---|---|
| `lapian_online_dash_test`（新增 ctest） | **117/0**：URL/fnval、字段解析、quality 不可信、坏字段跳过/无音轨、档位顺序/AVC 默认/60fps+4K 只手动、菜单模型（置灰/说明/4K 可用档/缓存 header/空目录不伪造） |
| `ctest` | **32/32**（31 + 新增 1；31.62s） |
| `--online-quality-test`（无 GUI，OB-1/2/3） | 各 **27/0、39/0、verdict=pass** |
| `--online-quality-test --odd-qn`（OB-1） | **33/0、45/0、verdict=pass** |
| `--online-quality-test --anonymous`（OB-1） | **18/0、32/0、verdict=pass**（只到 480P；`quality` 撒谎） |
| `--online-quality-test --ui`（OB-1） | **21/0、verdict=pass** |
| `dist` 里的同一探针（匿名） | 18/0、verdict=pass（打包二进制含 T8.9） |

---

## 7. 回归（不得退化）

| 项 | T8.9 后 | 基线/说明 |
|---|---|---|
| `--online-m1-test`（OB-1，全新缓存根，产品路径） | **28/0 pass**；first_frame **640ms**；P7 p95 **29ms**；已缓存 seek p95 103ms；UI gap **59ms**；close 377ms；snapshot 1280×720 | T8.3.1 dist 二进制同机同法：28/0、first_frame 656ms、UI gap 62ms → 未退化 |
| M1 E2E 额外请求 | durl playurl 1 + 档位目录 1（首帧后） | 首帧/OP1 关键路径不新增请求 |
| `--online-cache-manage-test` | workspace **72/0**、`--ui` **98/0**；verdict=pass | 与 T8.3.1 相同 |
| 本地 `--key-test`（`build\t6.2-key\A.mp4`） | **77 项、0 失败**（2s 27 帧） | 与 T8.3.1 相同 |
| 本地 P1/P2（3 次，`media\V9.mp4`） | ui_ready **299.78 / 297.53 / 277.40ms**；first_present **344.53 / 343.53 / 324.77ms** | T8.3.1：278.75–284.70 / 333.63–343.29ms；同量级 |
| `dist` | **34.02MB / 25 文件**（P12 余量 65.98MB） | T8.3.1：33.87MB/25；+0.15MB（新代码） |
| `check-imports` | 20 文件通过；无 QtNetwork/TLS/winhttp/wininet/urlmon | 不变 |
| 真实 AppData/缓存/记录 | **未动**（所有探针用 `build\t8.9-*` 显式根；未在真实默认根跑清空/清理） | 遵守 T8.3.1 约束 |

**没有**用 durl 1080p 1/3 或“菜单里有 4K”冒充 M2；**没有**把 M1 门重新判定（M1 仍是 T8.8 结论）。

---

## 8. 未验证 / 已知缺口

1. **大会员档（1080P60/720P60/4K）**：本机 `vip_status=0`，`dash.video[]` 无 116/74/120 → **不可得/未验证**。4K 的真实请求、HEVC/AV1 解码、HDR 颜色均未验证；O11/T8.15 才能做。
2. **DASH 播放未接**：T8.9 只解析/列档；DASH 视频+音频双输入、`GrowingSource`/`DownloadJob`、`init/index` Range 使用、共享时钟、seek fan-out 都是 **T8.10**。菜单里 DASH 档置灰是**有意**的，不是 bug。
3. **`size` 未知**：DASH 响应不返回总字节；T8.9 记 0，不伪造。T8.10 需要 Content-Length/末段 Range 或按分片对账。
4. **`quality`/`accept_quality` 不可信**：只记录。真实档位一律看 `dash.video[].id`；S2 与本次匿名对照都复现了撒谎。
5. **音频只解析第一层 `dash.audio[]`**：`dolby`/`flac` 未解析；多音轨切换是后续（不在 v2 MVP）。
6. **只测 3 条 OB + 1 次匿名/错误 qn**：别的稿件可能出现不同梯子（例如只有 HEVC、只有 480P、无音轨）。解析层对“无音频/坏条目/只有手动档”有单测，但**没有真实无音轨样本**（OB-6 未选）。
7. **档位目录是首帧后的额外请求**：失败只提示“档位获取失败（不影响播放）”；未做失败重试/缓存；OP1 未做 20×3 复测（T8.9 未改 OP1 口径，M1 E2E first_frame 无退化）。
8. **`data.quality` 与 durl 档位的对应关系**：durl 的 `quality` 与 DASH 目录可能不同（OB-1 durl=64、DASH 默认=80）。T8.9 菜单勾选的是**当前 durl 播放档**，不假装已在播 DASH 1080P。
9. **UI 只在 OB-1 跑**（OB-2/OB-3 是无 GUI 探针）；菜单/按钮在 100%/200% DPI 下的布局未单独复测（沿用 T6.4 的响应式布局）。
10. **`--no-quality-catalog` 仅诊断用**：关闭首帧后目录请求，用于 A/B 回归；不是产品开关。
11. **缓存命中 0 网络未复测**：需要完整缓存 + `--online-offline-test`；代码路径上缓存命中不启动目录线程（`EmitCacheHitReady` 用 meta 构造单档），本次未下载完整样本复测。
12. **目录请求失败/限频未压测**：只验证正常路径；断网/412/429 下“失败不影响播放”只有代码路径，没有 20 次重复。

---

## 9. 复现（最短）

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure

# 3 条档位矩阵（只从 .secrets 读 SESSDATA，不打印）
build\release\bin\lapian.exe --online-quality-test --bvid BV1BMnv6jE5S --cid 42915922022 --tag t8.9-ob1
build\release\bin\lapian.exe --online-quality-test --bvid BV1mA411m7pS --cid 1004967230 --tag t8.9-ob2
build\release\bin\lapian.exe --online-quality-test --bvid BV1GJ411x7h7 --cid 137649199   --tag t8.9-ob3

# 错误 qn / 匿名对照
build\release\bin\lapian.exe --online-quality-test --bvid BV1BMnv6jE5S --cid 42915922022 --tag t8.9-ob1-odd --odd-qn
build\release\bin\lapian.exe --online-quality-test --bvid BV1BMnv6jE5S --cid 42915922022 --tag t8.9-anon --anonymous

# 真实窗口 + 档位菜单（显式测试根，不写真实 AppData）
build\release\bin\lapian.exe --online-quality-test --ui --bvid BV1BMnv6jE5S --cid 42915922022 `
  --tag t8.9-ob1 --timeout-ms 30000 `
  --online-cache-root build\t8.9-ob1\cache --online-pictures build\t8.9-ob1\pics `
  --online-records-root build\t8.9-ob1\records

# M1 回归（同 T8.8 口径）
build\release\bin\lapian.exe --online-m1-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --tag t8.9-m1 --timeout-ms 30000 --online-cache-root build\t8.9-m1\cache `
  --online-pictures build\t8.9-m1\pics --online-records-root build\t8.9-m1\records
```

退出码：`0`=pass，`1`=fail，`2`=blocked（无凭据/登录失效/非普通用户）。

---

## 10. 下一步

- **T8.10 DASH 双输入**：视频/音频各自 `GrowingSource`/`DownloadJob`/`AVFormatContext`；用 `dash.video[]/audio[]` 的 init/index Range 建缓存键（音频单独键由 T8.10 决定）；共享时钟；seek fan-out；单流 EOF；不 remux。
- T8.12 才能把菜单里的 DASH 档变成可切（并加 60fps 降帧提示/DEC-4）；T8.15 才有大会员条件验收。
- 本文件所有数字只代表 **2026-10-24 03:2x–03:5x（Asia/Shanghai）** 的观测；接口/档位策略随时可能变。
