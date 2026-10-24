# 下一个会话提示词：T8.10（M2 DASH 双输入）

- **版本**：v1（2026-10-24，T8.9 交接后；基线 commit `a3cf655`）
- **直接复制本文件给新会话。** 上一会话已完成 **T8.9**（DASH 解析与档位，非门）；新会话**只做 T8.10**，不要顺带 T8.11/T8.12/T8.13/T8.14/T8.15/T8.16/T8.6.1b。
- **当前状态**：M1 已于 T8.8 通过；T8.9 完成（M2 第 1 项，非门）；T8.10 未开始；`tasks-v2.md` v0.20；`AGENTS.md` v6.31；`ctest` **32/32**；dist **34.02MB/25**；T8.9 探针 3×27/0、M1 E2E 28/0。

## 1. 先读

1. [AGENTS.md](../AGENTS.md)（当前状态/约束；v6.31）
2. [tasks-v2.md](../tasks-v2.md) **T8.10** 行、M2 门、版本记录（v0.20；门与数字锁定）
3. [spec-v1.0-delta.md](spec-v1.0-delta.md) v0.4（O2/O3/O4/O6/O11、OP1–OP10、DEC-2/DEC-4/DEC-5）
4. [ADR-007](adr/ADR-007-bilibili-online.md)（附条件接受；D1–D5 不改；双输入/DASH 是 D 项核心）
5. [v2-online-spike.md](perf/v2-online-spike.md) §2（档位矩阵）、§3.4（DASH init/index 结构）、§4（A/B 单文件 vs 双流）
6. **[T8.9 证据](perf/v2-online-quality.md)**、**[T8.9 交接](handoff-2026-10-24-t8.9.md)**（现成接口/事实/缺口）
7. [T8.4 证据](perf/v2-online-growing.md)（读层/取消/多读者）、[T8.5 证据](perf/v2-online-m1.md) §1–§10（下载/续传/备份/刷新/首帧）、[T8.6 证据](perf/v2-online-session.md)（线程/索引/波形/seek）、[T8.6.1a 交接](handoff-2026-10-24-t8.6.1a.md)（长按状态机，别破坏）
8. 代码：`src/online/PlayUrl.*`（`DashCatalog`）、`src/app/OnlineSession.*`、`src/online/DownloadJob.*`、`GrowingSource.*`、`CacheStore.*`/`CacheManager.*`、`src/media/MediaSession.*`、`src/audio/AudioEngine.*`、`src/app/PlaybackController.*`、`WaveformGenerator.*`、`src/app/MainWindow.*`、`src/app/main.cpp`

## 2. 任务边界

**只做 T8.10：DASH 双输入。**

- 视频 + 音频各自 `GrowingSource` / `DownloadJob` / `AVFormatContext`；共享时钟；seek fan-out；首帧/首包偏移；单流 EOF；**不 remux**（MVP 路线；remux 只允许作为 spike 对照，不作为产品实现）。
- 默认档：`DashCatalog::DefaultQn()`（AVC、1080P30、跳过 60fps/4K）；音轨取 `BestAudio()`（最高带宽 AAC）。**4K/60 不自动、不写保证**。
- 产品路径：有 DASH 目录时走双输入；DASH 不可用/失败时**保留 durl 回退**（M1 路径不能退化）。
- 音轨选择只取一条（第一条/最高带宽）；多音轨切换不在 v2。
- **不做**：T8.11 渐进索引/波形/总帧数（可复用 T8.6 已有部分索引，但不新增渐进策略）；T8.12 自动选档/手动切档/降帧提示；T8.13 加固；T8.14 M2 门；T8.15 VIP 条件验收；T8.16 打包；T8.6.1b 拖动预览。
- **不改** M1/M2 门、验收数字、OB 样本；不把 1080P60/4K 写成保证；不把大会员档写成 MVP。
- T8.3.1 的缓存管理语义必须保留：清空/孤儿/配额走 `CacheManager`/`CacheStore`；当前会话用 `bvid+cid` 部分键保护（现在要同时保护视频与音频条目）。

### 2.1 音频缓存键（T8.9 明确留给本任务）

T8.9 没有新增下载键，也没有改 `lapian-cache-1` schema。T8.10 **必须显式决定音频条目**：

- 建议：**按流分目录 + meta 标 `kind`**，例如 `<缓存根>\<bvid>\<cid>\<video-qn>-<codecid>\` 与 `<缓存根>\<bvid>\<cid>\audio-<audio_id>\`（或 `a<audio_id>`）。`CacheStore::EntryDir/Reserve/OpenPart/MarkComplete` 需要接受“流类型 + 流 id”，`CacheKey` 增可选 `kind/audio_id`；**缺省 kind=video，保持旧目录/旧 meta 可读**（向后兼容）。
- 不建议把音频塞进 `qn/codecid`：会与视频键语义冲突、LRU/清空/重建不可读。
- `ClearAll`/`CleanupOrphans`/`EvictToQuota` 的 `keep` 现在只传视频键；T8.10 要能保护**当前 bvid+cid 的视频+音频**（部分键已支持 bvid+cid，但入口只传一个 key——需要多键或 `bvid+cid` 保护模式，测试覆盖）。
- 视频/音频两个 `.part`（或视频 `.mp4` + 音频 `.mp4`）都计入配额；缓存命中要两个流都完整才可直接本地打开，否则按未完成走可增长读层或重新取流。
- schema 变更必须：向后兼容（旧条目可读/可清）、`lapian_online_cache_test`/`online_cache_manage_test` 新用例、`--online-cache-manage-test` 不退化。

## 3. 验收（tasks-v2 T8.10）

- **3/3 1080P30 首帧/播放**：OB-1/2/3，登录普通用户，DASH 视频 qn=80 AVC + AAC 音频；实际 fps 记 **30.000 / 29.412 / 25.000**（不要写“3/3 都是 30fps”）。
- **60s 音画偏差 ≤1 帧**（日志）：音画对齐以音频主时钟为准（ADR-002），记录首包 offset、累计漂移 p95/max；给出判定帧号（<=1 帧）。
- **20 次 seek 落点 0**：已缓存区；目标距前关键帧 ≤2s（P9 口径）；未缓存区按 O4 只显示等待/取消，不承诺毫秒。
- **seek 后音频从当前帧起**：不得回放旧位置/从头播；音频 EOF/视频 EOF 分开处理。
- **无死锁**；内存 **≤800MB**（OP7），记录工作集峰值。
- 新增 `--online-m2-test`（可拆 `--ui`）；证据 `docs/perf/v2-online-m2.md`（T8.10 部分）；交接 `docs/handoff-*-t8.10.md`。
- **回归不得退化**：M1 E2E 28/0（first_frame/UI gap 同量级）、缓存命中 0 网络（`--online-offline-test`）、本地 P1/P2/P7、`ctest`（当前 32/32）、dist（当前 34.02MB/25）、T8.9 的 `--online-quality-test` 3×27/0 + `--ui` 21/0。
- 不把“双输入能播”写成 M2 门通过；M2 门是 T8.14。

## 4. 代码入口（先读再改）

- **`src/online/PlayUrl.*`**：T8.9 已给 `DashCatalog/DashStream/DashAudioStream/QualityTier`、`DefaultQn/DefaultStream/FindStream/BestAudio`、`init_range/index_range`、`backup_urls`、`size=0(未知)`、`IsManualOnlyQuality`。**不要重新解析 JSON**；直接复用。
- **`src/app/OnlineSession.*`**：目前是“一次 durl playurl → 单 `DownloadPlan` → 单 `GrowingSource` + 单 `DownloadJob` → ready/quality 线程”。T8.10 需要：
  - `Start()` 先查缓存命中（视频+音频都要命中才能直接本地播放；否则取 DASH 目录）；
  - 取 `DashCatalog` 后用 `BuildDashPlan(video, audio)`（两个 `DownloadPlan`，各自 total/urls/qn|audio_id/codecid）；
  - ready 阈值：视频 + 音频各自达到最小可 probe 量（建议视频 0.3MB、音频 ~64–128KB 或各自首帧可解；实测决定，不写死），或者两者都 ready 才发 ready（避免首帧后音轨等待）；
  - 保持 `Cancel/Close/Join` 生命周期与 generation；quality 目录线程可保留；
  - 失败回退 durl：DASH 目录/下载/打开任一失败时，记录原因并可切回 durl（不要静默假装 DASH）。
- **`src/online/DownloadJob.*`**：保持单流一 job 的语义，**两个 job**（视频/音频）各自 `CacheStore::Reserve/OpenPart/UpdateProgress/MarkComplete`；`defer_finalize` 需要两个都 finalize；刷新回调要分别按视频/音频档位重取 playurl；`PartSink`/Range/备份/重试逻辑不要重写。
- **`src/online/GrowingSource.*`**：每流一个；**total 必须 >0**（`AVSEEK_SIZE`）。DASH 没有 `size`：
  - 先做 Range 探测：`Range: bytes=0-0`（或 `bytes=0-1`）看是否 `206` + `Content-Range: bytes 0-0/<total>`；备份 URL 同样；拿不到 total 时**不得伪造**（记录 blocker/降级策略，见 §6）。
  - 顺序下载从 0 开始即可覆盖 init+index（都在文件头）；不需要为 init/index 单独拼接字节。
- **`src/media/MediaSession.*`**：`OpenGrowing` 只接受单 `GrowingSource`/单路径；T8.10 需要“视频 GrowingFormat + 独立音频 GrowingFormat”的双输入组合（例如 `OpenGrowingDual(video_source, video_path, audio_source, audio_path, device, ...)`），本地 `OpenLocal`/`OpenWithFormat` 路径不变；`info()` 里视频/音频来自两个容器，`av_offset_us` 需要重新定义（两流首包 PTS 差值）。
- **`src/audio/AudioEngine.*`**：`OpenGrowing` 目前从同一容器取 audio stream；DASH 音频是独立容器（stream_index=0、无视频），需要新入口或让 MediaSession 传入音频 `AVFormatContext`/`GrowingFormat`；音频主时钟（IAudioClock）保持。
- **`src/app/PlaybackController.*`**：音频主时钟 + QPC 回退；双流时视频跟随音频；seek 时 fan-out 到两个流；播放/暂停/EOF 语义（视频结束为准，音频结束后静音不阻塞）。
- **`src/app/MainWindow.*`**：`ApplyOnlineReady`/在线索引（`StartOnlineIndexBuild`）/seek worker/长按状态机/波形/截图/在线记录都假设单 session；T8.10 要保证它们只作用于**当前视频流**，音频流不参与索引/波形（波形来自音频流，见 T8.11 的渐进波形；T8.10 用现有波形路径对音频流生成即可，不新增策略）。
- **`src/online/CacheStore.*` / `CacheManager.*`**：音频键/目录/meta（见 §2.1）；清空/孤儿/配额/重建/当前会话保护都要覆盖；`CurrentOnlineCacheKey` 只传视频键 → 扩展为多键/`bvid+cid` 保护。
- **`src/app/main.cpp`**：新增 `--online-m2-test`（bvid/cid/tag/timeout/cache-root/pictures/records-root；复用 `.secrets`/`LAPIAN_SESSDATA_FILE`/`--sessdata-file`）；显式测试根，不写真实 AppData。

## 5. 工作区与测试约束

- **构建只在工作区**：`scripts\configure.ps1` + `build\release`；产物 `build\release\bin\lapian.exe`。
- 在线探针用显式测试根（不写真实 AppData）：

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure

build\release\bin\lapian.exe --online-m2-test `
  --bvid ... --cid ... --tag t8.10-... --timeout-ms 30000 `
  --online-cache-root build\t8.10-...\cache `
  --online-pictures build\t8.10-...\pics `
  --online-records-root build\t8.10-...\records
```

- 基线回归（不得退化）：`ctest` 32/32；`--online-m1-test` 28/0；`--online-cache-manage-test` workspace 72/0、`--ui` 98/0；本地 `--key-test` 77/0；`--online-quality-test` 3×27/0 + `--ui` 21/0；dist 34.02MB/25。
- **默认路径/真实 UI 只在 Medium 副本验证**：`%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`；先备份真实 `cache`/`online`/`settings.json` 与原 exe，结束恢复；不要用工作区 exe 冒充默认路径。
- 工作区 exe 是 Low integrity；默认 UI 在线缓存写 Medium AppData 会失败，Low 结果不代表正常用户。
- 凭据只从 `.secrets`/产品 UI 读取；日志不打印 SESSDATA/签名 URL；不提交 `.secrets`、缓存、截图、记录。
- T8.9 的坑：状态栏 `QPushButton::setMenu()` 首次绘制会阻塞 UI ~620ms；本任务若动档位/设置菜单，沿用“普通按钮 + 点击时按需弹出 QMenu”。

## 6. 先做 spike（建议，最多半天）

**不要直接写双输入**。先用真实 3 条 OB 验证以下 5 件事，把结果写进 `docs/perf/v2-online-m2.md` 的 spike 小节：

1. **DASH 总字节**：对视频/音频主 URL + 备份 URL 发 `Range: bytes=0-0`，看 `206` + `Content-Range: bytes 0-0/<total>` 是否稳定；`Content-Length`/HEAD 是否可用；三个 CDN 是否一致。拿不到 total 时，`GrowingSource` 的 `AVSEEK_SIZE` 怎么填？是否必须改成“未知大小 + 逐段增长”模式（会影响 seek/索引，属于本任务范围，要评估）。
2. **init/index Range**：`bytes=0-916`、`bytes=917-1244`（OB-1）能否 206；init/index 是否必需单独请求（顺序下载 0..total 已覆盖，但首帧阈值/断点续传要知道）。
3. **两个 `GrowingFormat`**：视频 fMP4（AVC）+ 音频 fMP4（AAC）分别 `avformat_open_input`/首帧/首包是否可行；音频容器 `stream_index=0`、无视频；`av_offset_us` 用两流首包 PTS 计算是否稳定（OB-1/2/3 各记录一次）。
4. **seek fan-out**：视频 seek 到帧 N 时音频如何对齐（`av_seek_frame` 到同 PTS / 按音频包时间戳丢弃到 N / 重新打开音频）；20 次 seek 落点 0 + 音频从当前帧起的可行性。
5. **EOF/时钟**：视频结束、音频先结束/后结束时播放器行为；音频主时钟（IAudioClock）在双流下的可用性；暂停/步进时音频静音（沿用 ADR-002）是否保持。

**spike 结论决定实现路线**：

- 推荐：两个 `DownloadJob` + 两个 `GrowingSource` + `MediaSession::OpenGrowingDual`（视频/音频各自 `AVFormatContext`）+ 音频主时钟 + 视频 seek fan-out。
- 若 spike 证明“必须 remux 才能播”或“拿不到 total 且无法在 T8.10 内改 `GrowingSource`”：**记录 blocker，先停下问用户**（这是选型/依赖变更），不要自行扩任务或偷偷改成“全量下载再播”。

## 7. 停止条件

- 要改 M1/M2 门、验收数字、OB 样本：先停下问用户。
- 必须做不兼容的 `lapian-cache-1` schema 变更/迁移：先列出影响面（旧缓存、清空/孤儿/配额、记录/截图）、迁移/回滚方案，问用户。
- DASH/playurl 接口变动、WBI/风控要求新增认证或绕过：停下记录，不要猜。
- 大会员/60/4K 账号不可得：写“未验证/不可得”，不要写成通过；T8.15 才做。
- 发现必须做 T8.11（渐进索引/波形）才能让双输入 1080P 稳定：先记录 blocker；T8.10 可复用 T8.6 已有部分索引，不得自行扩 T8.11。
- 不要用“下载完整文件再播”“remux 成单文件”“durl 1080P 1/3”冒充双输入验收。

## 8. 完成时

- 一个可审查 commit；`git status` clean。
- 更新 `docs/perf/v2-online-m2.md`（T8.10 部分：3/3 首帧/fps、A/V 偏差、20 seek、内存、缓存键/schema、durl 回退、未验证项）、T8.10 交接 `docs/handoff-*-t8.10.md`、`tasks-v2.md`（v0.21）与 `AGENTS.md`（v6.32）。
- 报告：3/3 首帧/播放、实际 fps（30.000/29.412/25.000）、音画偏差（≤1 帧的判定与样本）、20 次 seek 落点、内存峰值、音频缓存键与 schema 变化、durl 回退路径、未验证/缺口、下一步 **T8.11（渐进索引/波形/总帧数）**。

## 9. T8.9 留下的现成接口与事实（直接用，别重做）

- 请求：`BuildDashPlayUrlUrl()`（`fnval=4048`、`qn=80`、`fourk=1`）；一次返回完整档位梯；`data.quality` 不可信，只信 `dash.video[].id`。
- 目录：`DashCatalog`（`videos/audios/tiers/duration_ms/claimed_quality`）、`DefaultQn()`（AVC、O6 顺序、跳过 116/74/≥120）、`DefaultStream()`、`FindStream(qn,codecid)`、`BestAudio()`、`RebuildDashTiers`。
- 字段：`DashStream.qn/codecid/width/height/fps/bandwidth/size=0/init_range/index_range/url/backup_urls/urls_ok/init_ok`；`DashAudioStream.id/codecid(=0)/bandwidth/init_range/index_range/...`。
- 实测（2026-10-24，3 条 OB）：1080P AVC qn=80 3/3；fps 30.000/29.412/25.000；1080P AVC 带宽 2.12–2.63Mbps；HEVC/AV1 ≈1/3；音频 3 条 AAC（30280/30232/30216；OB-2/3 的 30216 是 HE-AAC）；每条流主 URL+2 备份；init/index 是闭区间且**不保证相邻**（OB-2 AV1：init `0-1022`、index `1063-1334`）；`dash.size` 不存在。
- 产品路径：`OnlineSession` 首帧后异步取目录（+1 个 API 请求，失败不影响播放）；缓存命中用 meta 构造单档、0 网络；`MainWindow` 状态栏“清晰度”菜单只列示、DASH 档置灰（`QualityMenuContext::dash_playback_available=false`）；T8.10 在双输入可用后置 true（切档策略仍属 T8.12）。
- 探针：`--online-quality-test`（无 GUI/`--ui`/`--odd-qn`/`--anonymous`）；单测 `lapian_online_dash_test`（117/0）。
- 回归基线：M1 E2E 28/0（first_frame 640ms/UI gap 59ms）；缓存管理 72/0+98/0；key-test 77/0；P1/P2 299.78–277.40/344.53–324.77ms；dist 34.02MB/25；`ctest` 32/32。
- 未验证/缺口（T8.10 不要重复宣称）：大会员 1080P60/720P60/4K 不可得；DASH 播放未接；DASH 无 size；目录请求失败/限频未压测；缓存命中 0 网络在 T8.9 未重跑（T8.10 必须补测）。
