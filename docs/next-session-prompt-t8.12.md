# 下一个会话提示词：T8.12（M2 档位策略 / 降帧提示）

- **版本**：v1（2026-10-24，T8.11 收口后；基线 commit `a6cdf6d`）
- **直接复制本文件给新会话。** 上一会话已完成 **T8.11（渐进索引/波形/总帧数）**；新会话**只做 T8.12**，不要顺带 T8.13/T8.14/T8.15/T8.16/T8.6.1b。
- **当前状态**：**M1 已于 T8.8 通过**；T8.9（DASH 解析/档位）、T8.10（DASH 双输入）、T8.11（渐进索引/波形/总帧数）完成，均**不是 M2 门**；T8.12 **未开始**；`tasks-v2.md` v0.22；`AGENTS.md` v6.34；`ctest` **33/33**；`dist` **34.15MB/25**（已重新组装，但工作区 exe 仍是 Low integrity）；T8.11 `--online-index-test` 真实 **27/0**、离线 **22/0**、单测 **28/0**；M2 `--online-m2-test` **3/3 34/0**；M1 `--force-durl` **28/0**；T8.9 `--online-quality-test` 3×**27/0** + `--ui` **21/0**。

## 1. 先读

1. [AGENTS.md](../AGENTS.md)（当前状态/约束；v6.34）
2. [tasks-v2.md](../tasks-v2.md) **T8.12** 行、M2 门、DEC-4、版本记录（v0.22；门与数字锁定）
3. [spec-v1.0-delta.md](spec-v1.0-delta.md) v0.4 的 **§0.2、O6、O11、DEC-4**；重点是 O6-A1…A7
4. [ADR-007](adr/ADR-007-bilibili-online.md)、[T8.9 证据](perf/v2-online-quality.md)、[T8.9 交接](handoff-2026-10-24-t8.9.md)
5. [T8.10 证据](perf/v2-online-m2.md)、[T8.10 交接](handoff-2026-10-24-t8.10.md)
6. **[T8.11 证据](perf/v2-online-index.md)**、**[T8.11 交接](handoff-2026-10-24-t8.11.md)**
7. [T8.6.1a 交接](handoff-2026-10-24-t8.6.1a.md)（异步长按/seek 状态机，不能破坏）
8. 代码：
   - `src/app/MainWindow.cpp`：`CurrentQualityMenuContext`、`OnOnlineQualityChosen`、`RebuildOnlineQualityMenu`、`ApplyOnlineQuality`、`BeginOnlineRequest`/`StartOnlineSession`、`ShutdownOnlineSession`、`ApplyOnlineReady`/`ApplyOnlineCacheHit`、`StartOnlineIndexBuild`、`OnlineFrameCached`/`OnlineLastFrame`、`SetOnlineRecordsRootForTest`
   - `src/app/OnlineSession.h/.cpp`：`OnlineRequest`、`OnlineReadyInfo`、`ResolveOutcome`、`Start`/`StartDashDownload`、`kDashRequestQn`
   - `src/online/QualityMenu.h/.cpp`：`QualityMenuContext`、`BuildQualityMenuModel`
   - `src/online/PlayUrl.h/.cpp`：`DashCatalog`/`QualityTier`/`DefaultQn`/`IsManualOnlyQuality`
   - `src/core/MarkStore.*`：`SetOnlineContext(fps,qn,codecid)`、在线记录
   - `src/media/SnapshotNaming.*` / `SnapshotService.*`：截图清单 fps/qn/codecid
   - `src/app/OnlineQualityTestRunner.*`：`--online-quality-test`（扩展现有探针，不另起一套）

## 2. 任务边界

**只做 T8.12：档位策略与降帧提示。**

必须覆盖：

- 自动选档：默认 AVC、≤1080p；按 O6 顺序 `1080P60 → 720P60 → 1080P30 → 720P30 → 480P30`；普通用户实际只到 1080P30/720P30/480P30。
- 手动切档：1080P30 → 720P30 成功；重新取流、重建索引/波形；按 PTS 保留位置；标记/截图/在线记录不丢；切换后提示“帧号可能变化”。
- 档位权限：登录后只按 `playurl` 实际返回的 `dash.video[]` 列档；无权限档不显示或明确置灰；**实际 qn 以服务端返回为准**；不把“菜单可选”写成“保证可达”。
- 60fps/4K 口径：1080P60/720P60 AVC 可用时可自动；只有 HEVC/AV1 时只进手动菜单，标“60fps / HEVC（手动）”，不自动、不静默降帧；4K 始终只手动，标“可用档：性能/颜色不保证”。
- 实际 fps 可见；DEC-4：起播前 InfoBar 横幅（可关）+ 帧号旁常驻角标；降帧文案明确“帧号与源片不同步/约 1/2”，但不伪造 2:1。
- 标记/截图记录实际 fps/qn/codecid；换档 fps 不同按 PTS 映射并提示。

**不做**：

- T8.13 网络/并发故障加固；T8.14 M2 门；T8.15 大会员条件验收；T8.16 打包/安装；T8.6.1b 拖动预览。
- 不新增账号体系/弹幕/评论/云端同步；不绕过付费/DRM/地区限制。
- **不改** M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema/缓存键格式/索引磁盘格式。
- **不改** T8.11 的增量索引、T8.10 的 DASH 双输入/音频缓存键/fMP4 `SeekToKeyframePts`、T8.6.1a 的异步步进/长按状态机。

## 3. 验收（tasks-v2 T8.12 + O6）

- **O6-A1**：3 条 OB 显示的档位/编码/fps 与 `playurl` 实际返回一致；不出现“请求 1080P60 成功”的假象。
- **O6-A2**：OB-2（60fps 源）普通用户：实际 fps ≈29.4；起播前出现降帧提示；学习过程中持续可见（DEC-4：横幅 + 帧号旁角标）；文案明确“帧号与源片不同步”。
- **O6-A3**：手动 1080P30 → 720P30：重新取流成功；帧号/索引重建；标记仍在；切换提示可见。
- **O6-A4**：4K 只出现在手动菜单，标“可用档”；默认不选；不进入 M1/M2 验收。普通用户不可得时写“不可得/未验证”，不伪造。
- **O6-A5**：同一 BV 切档后 fps 不同：按 PTS 映射标记并提示；不得静默声称帧号一致。
- **O6-A6**：在线截图清单记录 bvid/cid/qn/codecid/fps/frame/pts/路径/时间；缓存清空后清单仍可读。
- **O6-A7**：未登录：显示“未登录，最高约 480P30（以实际返回为准）”；不承诺 720p/1080p。
- **T8.12 行**：OB-2 实际 ≈29.4fps + 提示 + “帧号与源片不同步”；真 1080P60 不显示降帧；HEVC-only 60fps 手动菜单标“60fps/HEVC”，不静默降帧；手动 1080→720 成功；标记保留；不出现假 60fps；4K 只手动/可用档；**不得把“菜单可选”写成“保证可达”**。
- 证据：扩展 `--online-quality-test`；更新 `docs/perf/v2-online-quality.md`；交接 `docs/handoff-*-t8.12.md`。

## 4. 现状与硬事实（开始前必须知道）

### 4.1 当前代码状态

- T8.9 的档位菜单**只能列示，不能切档**：
  - `MainWindow::CurrentQualityMenuContext()` 里 `dash_playback_available = false`；
  - `MainWindow::OnOnlineQualityChosen(qn)` 对非当前档只显示“DASH 档位切换尚未接入（T8.12）”，**不重取流**；
  - `QualityMenu` 在 `dash_playback_available=false` 时把其它档全部置灰。
- T8.10 已经能播放 DASH 双输入，但 `OnlineRequest` **没有 requested qn 字段**；`OnlineSession.cpp` 有三处硬编码 `kDashRequestQn`（resolve、quality worker、DASH download 刷新）。T8.12 可以加 `requested_qn`（默认 0 → 用 `kDashRequestQn`），但要保持 M1 durl URL 字节不变。
- `OnlineReadyInfo` 同时有：
  - `fps`：容器/流 `frame_rate`（OB-2 可能是 30.000）；
  - `dash_fps`：DASH 目录的实际 fps（OB-2 = 29.412）；
  - `qn/codecid`：实际返回。
- `MainWindow` 当前 `online_fps_` 取 `session_->info().video.frame_rate`，`online_dash_fps_` 取目录 fps；UI 的降帧提示用 `source_fps < 50.0`，**这个条件太宽**：OB-1 的 30.000 和 OB-3 的 25.000 都会命中，产生噪音。T8.12 要替换，不要沿用。
- 截图/记录：`MarkStore::SetOnlineContext(fps, qn, codecid)` 已在 T8.7 接入；`SnapshotNaming` 已带 qn/codecid/fps；切换后必须用**新流的实际值**更新，不能停在上一个档。
- T8.11 的增量索引/波形：切换档位是**新会话**，必须停旧 reader/download，再重建索引/波形；不要复用旧 `IncrementalOnlineIndexBuilder` 的 offset。
- T8.9 已知坑：状态栏不要用 `QPushButton::setMenu()`（首次 Paint 阻塞 ~620ms）；当前是普通按钮 + 按需 `QMenu`，保持。
- 工作区 exe 是 **Low integrity**；在线默认缓存/凭据写 `%LOCALAPPDATA%` 会失败。测试用显式测试根；默认路径只在 Medium 副本验证，或明确写“未重跑”。

### 4.2 真实数据（T8.9/T8.10/T8.11，登录普通用户 `vip_status=0`）

| 样本 | 标题/源 | DASH 档位 | 默认实际 | 备注 |
|---|---|---|---|---|
| OB-1 `BV1BMnv6jE5S` / `42915922022` | 30fps MAD，111s | 16/32/64/80 × AVC/HEVC/AV1 | 1080P AVC qn=80，**30.000fps** | 主流程；`accept_quality=112/80/64/32/16` |
| OB-2 `BV1mA411m7pS` / `1004967230` | 60fps 源，96s | 同上 | 1080P AVC qn=80，**29.412fps** | **降帧提示唯一样本**；同片 HEVC 30.303 / AV1 30.019；普通用户没有 116/74/120 |
| OB-3 `BV1GJ411x7h7` / `137649199` | 25fps，213s | 同上 | 1080P AVC qn=80，**25.000fps** | 长样本；T8.11 的 5306 帧 |

- 普通用户 **3/3 只有 1080P AVC qn=80**；`116/74/120` 在 `dash.video[]` 里不存在；请求 `qn=116/120` 仍返回 `quality=80`，目录不变。
- `data.quality` **会撒谎**：匿名请求 qn=80 返回 `quality=64`，但 `dash.video[]` 只有 16/32。档位判定只信 `dash.video[].id`。
- `dash.video[].size` 不存在；总字节用 Content-Length/Range。
- durl 1080P 只 1/3 命中（OB-1/OB-3=720P，OB-2=1080P）；不要写成保证。
- DASH GOP 5s；P9 只按“目标距关键帧 ≤2s”考核；点尾 ~260ms 不在门槛内。
- OB-3 `av_offset_us=-21ms`；波形/T8.11 数字不要回退。

### 4.3 fps 口径（T8.12 必须先定，否则会返工）

建议默认：

1. **帧号/索引/seek/记录里的 `frame` 继续用容器显示帧序号**（T8.11 现状），不要用 API 29.412 重算总帧数；OB-2 的 2866/2867 和 M2 数字不能变。
2. **界面显示实际流 fps 用 `dash_fps`**（playurl 实际返回），容器 fps 不同时在详情/证据里并列，例如“流 29.41fps（容器 30.00）”。
3. **降帧提示只在有证据时出现**：
   - 选中 60fps 档但实际流 <59.5（T8.15/大会员场景）；
   - OB-2 这类 `dash_fps≈29.412` 且已知源为 60fps 的样本；
   - 没有源 fps 证据时只能写“可能降帧”，不能写死 2:1。
   - **普通 30.000/25.000 不显示降帧**；不要沿用 `fps < 50`。
4. 如果实现发现必须改“已暴露帧号”的语义（例如把 OB-2 总帧数改成 2811），**停下来问用户**；这会改 M2 数字和已有标记/截图。
5. 如果无法可靠判定源 60fps，按 O6-A2 对 OB-2 做保守提示，并在证据里写清“只有 OB-2 一条 60fps 源样本；不推广为所有 29.97”。

## 5. 建议实现路线

1. **菜单/权限**
   - `CurrentQualityMenuContext.dash_playback_available` 改为 `online_dash_ && online_ready_`（或等价条件），让非当前档可点；
   - 只对 `dash.video[]` 实际条目建菜单；请求/实际 qn 不一致时显示实际；
   - 保留“仅手动/4K 可用档/HEVC 60fps”标签；页脚继续“菜单可选不等于保证可达”；
   - 未登录时明确“最高约 480P30（以实际返回为准）”，不要只写“受限”。
2. **切档**
   - `OnlineRequest` 增加 `requested_qn`（默认 0→80）；`OnlineSession` 用它替换硬编码，但 durl/M1 路径不变；
   - `OnOnlineQualityChosen` 捕获当前 PTS/帧，确认 fps 可能变化（fps 不同必须提示），停旧索引/波形/reader/download，启动新档会话；
   - 新档若是缓存命中，直接打开；否则重新 resolve/download；重建索引/波形；按 PTS seek 回原位置；
   - 旧缓存条目保留，不删；新档用自己的 `<qn>-<codecid>`/`audio-<id>` 键；
   - 取消/关窗/切档失败要回到旧档或可读错误，不能留下两套 reader/线程。
3. **标记/截图/记录**
   - 切档前把标记按 bvid+cid+PTS 保留；新索引建立后按 PTS 映射回帧；
   - `SetOnlineContext` 更新到新 qn/codecid/fps；截图清单写当前流值；
   - 提示“换档后帧号可能变化”，不要静默复用旧帧号。
4. **降帧 UI（DEC-4）**
   - 起播 InfoBar 横幅（可关闭）+ 帧号旁常驻角标；只出现一套，不要重复；
   - 角标不可关；横幅关闭后角标仍可见；
   - 文案保守、短；不要阻断操作；恢复后角标消失；
   - 注意 InfoBar 高度/不遮时间轴/不与 T6.5 横幅重叠；不要用 `QPushButton::setMenu()`。
5. **测试**
   - 扩展 `--online-quality-test`：档位权限/实际 qn、手动 1080→720、PTS 映射、标记保留、fps 角标、无假 60、4K 只手动；
   - 真实 OB-1/2/3；OB-2 的降帧提示必须单独断言；
   - 合成 60/4K 目录只验证 UI 规则，明确“不是可达证据”；
   - 新增纯函数单测（菜单/降帧判定）避免 GUI 依赖；`ctest` 保持全绿。

## 6. 回归与测试命令

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure
```

基线（不得退化）：

- `ctest` **33/33**（T8.12 新增测试后应 >33）。
- `--online-m2-test` OB-1/2/3 **3/3 34/0**；M1 `--force-durl` **28/0**。
- `--online-quality-test` 3×**27/0** + `--ui` **21/0**。
- T8.11 `--online-index-test` 真实 **27/0**、离线 **22/0**、单测 **28/0**。
- 缓存 396/0、管理 75/0+98/0、key 77/0、P1/P2/P7。
- 测试根用 `build\t8.12-*`；不写真实 AppData；不提交缓存/截图/记录；日志不打印 SESSDATA/签名 URL。
- 真实在线需要 `.secrets\bilibili_sessdata.txt`；无凭据时按匿名档，不伪造 1080P60/4K。

## 7. 停止条件

遇到以下情况先停下记录/问用户，不要偷偷扩任务：

- 要改 M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema/缓存键/索引格式；
- 要改已暴露的帧号语义（API fps 重算/改总帧数），或会让 T8.11 的 5306/2866 变化；
- 切档需要大改线程模型、无法保证标记/截图 PTS 映射、或切档后 UI >200ms 卡顿；
- 没有真实 60fps/4K 档却要求写“通过”；
- 60fps/4K 档只能在合成目录里出现，却要写成“可达”；
- 发现必须把 60fps/4K 纳入 M2 才能继续（不应发生；那是 T8.15 非门条件验收）。

## 8. 完成时

- 一个可审查 commit；`git status` clean。
- 更新 `docs/perf/v2-online-quality.md`（T8.12：权限/自动档/手动 1080→720/PTS 映射/标记/降帧横幅+角标/OB-2 29.4/无假 60/4K 未验证/缺口）、`docs/handoff-2026-10-24-t8.12.md`、`tasks-v2.md` v0.23、`AGENTS.md` v6.35。
- 报告：档位与实际 qn、OB-2 29.412 + 提示、1080→720 成功、标记保留、fps 记录、4K/60 未验证项、回归数字、缺口。
- **不要把 T8.12 写成 M2 通过**；M2 门是 T8.14。**不要把 60/4K 写成 MVP 保证**；那是 T8.15 条件验收。
- 建议下一步：T8.13（网络/并发加固），然后 T8.14（M2 门）。

## 9. 交接给你的现成接口/事实

- `DashCatalog`/`QualityTier`：qn/codecid/fps/width/height/bandwidth/init/index/urls；`DefaultQn()` 已按 O6 顺序、只选 AVC、跳过只手动档。
- `QualityMenuContext`：`current_qn`/`current_codecid`/`current_from_cache`/`dash_playback_available`/`error`。
- `OnlineReadyInfo`：`qn`/`codecid`/`fps`（容器）/`dash_fps`（目录）/`dash_duration_ms`/`audio_id`/`playurl`。
- `OnlineSession::Start` 目前硬编码 `kDashRequestQn`；`OnlineRequest` 无 qn 字段。
- `MarkStore::OpenOnline`/`SetOnlineContext`、`SnapshotNaming` 已记录在线 fps/qn/codecid（T8.7）。
- T8.11 的 `IncrementalOnlineIndexBuilder`、`InstallOnlineIndexSnapshot`/`InstallPendingOnlineIndex` 可复用；切档是新会话，重新建 builder。
- 回归基线：T8.11 真实在线 27/0（首帧 493.1ms、首次索引 128ms、30s 6.209s、seek p95 56.2ms、exact 5306、UI gap 58ms、read_ratio 1.000、max_step 10.70ms）；M2 34/0×3；M1 28/0；ctest 33/33。
- 未验证/缺口：普通用户无 60/4K；真实 60fps 只能等 T8.15；60fps 提示形态只能在 OB-2/合成目录验证；默认 AppData/Medium 未在 T8.12 重跑前先记录。
