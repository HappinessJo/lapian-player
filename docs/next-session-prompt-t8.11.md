# 下一个会话提示词：T8.11（M2 渐进索引/波形/总帧数）

- **版本**：v1（2026-10-24，T8.10 交接后；基线 commit `b560607`）
- **直接复制本文件给新会话。** 上一会话已完成 **T8.10（DASH 双输入）**；新会话**只做 T8.11**，不要顺带 T8.12/T8.13/T8.14/T8.15/T8.16/T8.6.1b。
- **当前状态**：M1 已于 T8.8 通过；T8.10 完成（M2 第 2 项，非门）；T8.11 未开始；`tasks-v2.md` v0.21；`AGENTS.md` v6.32；`ctest` **32/32**；dist **34.02MB/25**；T8.10 `--online-m2-test` 3/3 **34/0**；M1 `--force-durl` **28/0**、DASH 25/0。

## 1. 先读

1. [AGENTS.md](../AGENTS.md)（当前状态/约束；v6.32）
2. [tasks-v2.md](../tasks-v2.md) **T8.11** 行、M2 门、版本记录（v0.21；门与数字锁定）
3. [spec-v1.0-delta.md](spec-v1.0-delta.md) v0.4 的 O3/O4/OP2/OP3/OP4、DEC-2
4. [ADR-007](adr/ADR-007-bilibili-online.md)、[T8.10 交接](handoff-2026-10-24-t8.10.md)、**[T8.10 证据](perf/v2-online-m2.md)**
5. [T8.6 证据](perf/v2-online-session.md)、[T8.6.1a 交接](handoff-2026-10-24-t8.6.1a.md)（异步 seek/长按状态机，别破坏）
6. 代码：`src/media/OnlineIndexBuilder.*`、`src/media/FrameIndex.*`、`src/app/MainWindow.*`（`StartOnlineIndexBuild`/`ApplyIndexState`/`OnlineFrameCached`/`OnlineLastFrame`/`ContainerTotal`）、`src/audio/WaveformGenerator.*`/`WaveformData.*`、`src/media/MediaSession.*`、`src/app/OnlineSession.*`

## 2. 任务边界

**只做 T8.11：渐进索引 / 波形 / 总帧数。**

- 索引只扫**已缓存前缀**；缓存增长时**增量推进**，不要每 650ms 从头全扫；三态（扫描中/估算/精确）；缓存完成后总帧数精确。
- 波形渐进：已生成区间与帧刻度偏移 ≤1 帧；生成中不阻塞步进/拖动；不要每次增长把音频从头重解。
- UI 停顿：已缓存区操作 >200ms 停顿时长 = 0；未缓存 seek 仍显示“缓冲中（目标帧 N）”。
- **不做**：T8.12 切档/降帧提示、T8.13 故障加固、T8.14 M2 门、T8.15 大会员、T8.16 打包、T8.6.1b 拖动预览。
- **不改** M1/M2 门、验收数字、OB 样本、`lapian-cache-1` schema 语义。
- **不要改** T8.10 的 DASH 双输入、音频缓存键、fMP4 `SeekToKeyframePts`+PTS 校验；索引/波形只针对**视频流**（索引）和**音频流**（波形）。

## 3. 验收（tasks-v2 T8.11）

- **3min 1080P30**：已缓存 **30s 内**索引可用；缓存区 seek 精确（落点 0）；缓存完成后总帧数精确；波形 ≤1 帧；UI 停顿 >200ms = 0；未缓存 seek 显示等待。
- 素材：OB-3 `BV1GJ411x7h7` / `137649199` 实际 **212.261s / 5306 帧 / 25fps**，是最接近 3min 的样本；OB-1 111s/3314 帧、OB-2 95.566s/2867 帧。验收文案仍写“3min 1080P30”，记录实际样本（OB-3 212s、25fps）并注明。
- 新增 `--online-index-test`；证据 `docs/perf/v2-online-index.md`；交接 `docs/handoff-*-t8.11.md`。
- **回归不得退化**：`ctest` 32/32；`--online-m2-test` 3/3 34/0；`--online-m1-test --force-durl` 28/0；`--online-m1-test` 25/0（未缓存专项会跳过）；`--online-quality-test` 3×27/0 + `--ui` 21/0；缓存 396/0、管理 72/0+98/0、key-test 77/0、P1/P2/P7。

## 4. 现状与硬事实（T8.10 交接）

- `OnlineIndexBuilder`（T8.6）现在**每次从文件起点重建到 `Published()` 前沿**，末尾留 `tail_guard_frames=48`；返回 `index + spans + stable_frames + complete`。`MainWindow::StartOnlineIndexBuild` 约每 **650ms** 跑一次，且 `online_seek_running_` 时跳过安装。
- 索引只 demux 视频容器；DASH 视频是独立 `GrowingSource`（`online_part_path_`/`growing_source_for_index()`），音频是另一个 source（`online_audio_source_`/`online_audio_part_path_`）；**索引绝不能扫音频容器**。
- 波形已用独立音频 `GrowingSource`（`WaveformGenerator::StartGrowing(audio_source, audio_part_path, 0, av_offset_us/1e6)`）；DASH `av_offset_us = -视频首帧 PTS`（OB-3=-21ms）。T8.11 只改“渐进/不重复解码”策略，不改 source/offset。
- DASH 1080P 的 GOP 是 **5s**；`SeekToKeyframePts` 修过 fMP4 落前一 GOP 的 bug，**不要回退**。P9 只按“目标距关键帧 ≤2s”考核；点尾 5s 距离 ~260ms 不在门槛内。
- `online_index_complete_` + `online_stable_frames_` 决定 `OnlineLastFrame`；`ContainerTotal` 用容器 duration×fps；OB-2 的 API `frameRate=29.412` 与容器 30fps（2867 帧/95.566s）不一致，帧号数学用容器值。
- 当前下载很快（31MB/2.7s），M2 测试里“未缓存”专项会因整片下完而跳过；T8.11 要专门做**慢增长/增量**测试，不能靠“下载太快”掩盖“反复全扫”。

## 5. 建议实现路线

**先测再改**：用 OB-1/OB-3 记录当前每 650ms 重建的 CPU/耗时、索引帧数、波形进度、UI gap；再用一个受控的慢 `GrowingSource`（离线测试，按 1–2MB/s `Publish`）复现“边下边索引”。

1. **增量索引**：给 `OnlineIndexBuilder` 增加“从上次已解析字节/帧继续”的入口（或新 `IncrementalOnlineIndexBuilder`）；保留“稳定前缀”语义（末尾 tail guard）；只对新增字节 demux，追加 `FrameIndexEntry` 与 `spans`；`stable_frames` 单调递增；禁止在已暴露前缀上改变 PTS（失败则不安装）。
2. **总帧数三态**：`Scanning`（未覆盖）/`Estimated`（容器 duration×fps）/`Exact`（索引 complete 且帧数一致）；缓存完成后 `Exact`；不要在增长中把估算值当精确值。OB-2 的 API/容器 fps 差异记录，不要用 API fps 算精确帧数。
3. **波形渐进**：确认 `WaveformGenerator` 是否每次读完当前前沿就停/重读；改成“追加 bucket + 保留已生成前缀”，只在数据增长后继续解码；时间轴 bucket 映射偏移 ≤1 帧（用 `av_offset_us`）。不要占用 UI 线程。
4. **UI**：索引/波形安装只在 GUI 线程、小步合并；避免一次替换大数组造成 >200ms；`online_seek_running_` 时仍跳过安装；未缓存 seek 走既有等待提示/取消。
5. **测试**：`--online-index-test`（真实 3min OB-3 + 受控慢增长离线 case）；记录 30s 索引可用时间、索引重建次数/耗时、缓存区 seek 落点、完成后总帧数、波形偏移、UI gap、未缓存提示；单测覆盖增量边界/重复帧/尾部稳定前缀/complete。

## 6. 工作区与测试约束

- 构建只在工作区：`scripts\configure.ps1` + `build\release`；产物 `build\release\bin\lapian.exe`。
- 在线探针用显式测试根，不写真实 AppData：

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\configure.ps1
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure
```

- 基线回归命令（数值见 T8.10 交接 §3）：
  `--online-m2-test` 3 条；`--online-m1-test --force-durl`；`--online-quality-test` 3 条 + `--ui`；`--online-cache-manage-test`（workspace/`--ui`）；`--online-cache-test`；本地 `--key-test` + `--memory-mode step`。
- 工作区 exe 是 Low integrity；默认 UI 在线缓存写 Medium AppData 会失败。默认路径只在 Medium 副本验证；T8.11 若不做默认路径，明确写“未重跑”。
- 凭据只从 `.secrets`/产品 UI 读取；日志不打印 SESSDATA/签名 URL；不提交 `.secrets`、缓存、截图、记录。
- 视频/音频 URL 会过期；测试根用 `build\t8.11-*`，结束不提交。

## 7. 停止条件

- 要改 M1/M2 门、验收数字、OB 样本：先停下问用户。
- 要改 `lapian-cache-1` schema/索引磁盘格式/缓存键：先列影响面与向后兼容方案，问用户。
- 发现“增量索引”必须改变帧号语义（例如已暴露前缀 PTS 变化）、或必须换播放器/解码路径：记录 blocker，先停下。
- 慢增长下索引/波形仍阻塞 UI >200ms，且需要改线程模型/大重构：先记录，不要偷偷扩 T8.12/T8.13。
- 大会员/60/4K 不可得：写“不可得/未验证”，不要写成通过。

## 8. 完成时

- 一个可审查 commit；`git status` clean。
- 更新 `docs/perf/v2-online-index.md`（T8.11：30s 索引可用、增量次数/耗时、seek 落点、总帧数三态、波形偏移、UI gap、缓冲提示、未验证项）、T8.11 交接 `docs/handoff-*-t8.11.md`、`tasks-v2.md`（v0.22）与 `AGENTS.md`（v6.33）。
- 报告：索引可用时间/帧数、缓存区 seek 0 落点、完成后精确总帧数、波形 ≤1 帧、UI >200ms 停顿 0、未缓存提示、回归数字（M2 34/0×3、M1 28/0、ctest 32/32 等）、缺口、下一步 **T8.12（档位策略/降帧提示）**。
- **不要把 T8.11 的“索引/波形完成”写成 M2 通过**；M2 门是 T8.14。

## 9. T8.10 留下的现成接口/事实（直接用）

- 双输入：`OnlineReadyInfo.source/part_path`（视频）、`audio_source/audio_part_path/audio_id/audio_total_bytes/dash_fps/dash_duration_ms`。
- 缓存键：视频 `<qn>-<codecid>`，音频 `audio-<id>`；`CacheEntry.dash/audio_id`；`CacheMetadata.dash/audio_id`；`CacheStore::Scan` 同时返回视频/音频。
- 索引：`OnlineIndexBuilder::BuildOnlineIndex(source, part, video_stream_index, tail_guard, cancel)`；`MediaSession::InstallOnlineIndex`；`MainWindow` 的 `online_spans_`/`online_index_complete_`/`online_stable_frames_`/`OnlineFrameCached`/`OnlineLastFrame`。
- 波形：`WaveformGenerator::StartGrowing(source, part, 0, av_offset_seconds)`；`WaveformData::bucket_count/MinMaxRange`；`TimelineWidget::SetWaveformSource`。
- 回归基线：M2 34/0×3（首帧 322.2/369.0/365.1ms、A/V p95 17.1/18.2/22.0ms、seek 20/20 帧号+PTS、内存 211.5/209.7/201.7MB、缓存命中 45–46ms/0 网络）；M1 durl 28/0、DASH 25/0；ctest 32/32；缓存 396/0；管理 72/98；key 77/0；P1 291–294ms、P2 341–372ms、P7 0.85/0.35ms。
- 未验证/缺口：OB-2 API fps 29.412 vs 容器 30；LRU 条目级可能拆散视频/音频；多音轨/无音轨 DASH-only；默认 AppData/Medium 未在 T8.10 重跑；5s GOP 点尾 seek ~260ms（不属 P9 ≤2s）。
