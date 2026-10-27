# W1 交接：工作区 A/B 按视频记忆（2026-10-27）

> 执行计划 [plan-2026-10-27-loop-workrange-fix.md](plan-2026-10-27-loop-workrange-fix.md) §3/§4。
> 基线：`master` HEAD `14407ec`（L1 循环修复已交付，ctest 38/38、工作树干净）。
> 本轮只做「工作区 A/B 按视频记住」；**循环开关仍只在本次会话内保持**，不改播放/标记/预览。
> **未新增源文件、未改 CMakeLists**（避免 0xC0000409 重配），未打包/安装，未动 `lapian-c8`/安装版/E:。

---

## 1. 用户需求与结论

用户实测：工作区滑块（A/B）不保存，每次打开都回到全片两头。根因不是 bug——`spec.md` §3.8 原文
「循环开关与播放速度只在本次会话内保持」，`AppSettings` 只有音量/波形/缓存/预览，没有 A/B 存储面。

本轮：**A/B 按视频持久化 + 打开时恢复**；循环开关不落盘（打开时一律「循环：关」）。
spec 已同步：§3.8 行为定义加一句、F8 新增 **F8-A4**（其余验收值未改）。

---

## 2. 存储与 schema

### 2.1 文件位置（不新增源文件/新文件类型）

| 场景 | 路径 |
|---|---|
| 本地 sidecar | 由 `ResolveMarkPaths(video).sidecar` 派生：`<视频完整文件名>.lapian-workrange.json`（把 `.lapian.json` 后缀换掉） |
| 本地回退 | `ResolveMarkPaths(video).fallback` 同目录：`%APPDATA%\拉片软件\marks\<path_hash>.workrange.json` |
| 在线 | `<online 记录目录>/<bvid>-<cid>.lapian-workrange.json`（`MainWindow::OnlineRecordsRoot()`；默认 `%APPDATA%\拉片软件\online`） |

本地复用 `VideoIdentity`/`ResolveMarkPaths`/`MarkPathHash`，身份规则与标记 sidecar 完全一致；
在线沿用 `<bvid>-<cid>` 命名（与 `lapian-online-1` 记录同目录、不同后缀）。

### 2.2 JSON（schema 固定 1）

```json
{
  "schema": 1,
  "in": 100,
  "out": 160,
  "kind": "local",              // local | online
  "id": "<path_key 或 bvid-cid>",
  "size": 123,                  // 本地：视频字节数；在线 0
  "mtime_ms": 1690000000000,    // 本地：视频 mtime（epoch ms）；在线 0
  "frames": 3000,               // 保存时帧数（帧数变化时按新帧数 clamp）
  "updated_at": "2026-10-27T05:00:00.000Z"   // 仅诊断
}
```

落盘：**QSaveFile 原子写**；sidecar 写失败静默回退 AppData；两条都失败只按
`LAPIAN_WORKRANGE_LOG` 记一行 `W1 workrange_save_failed ...`，**不弹窗、不影响播放**。

### 2.3 校验（纯函数 `ValidateWorkRange`）

| 条件 | 结果 |
|---|---|
| `kind` 与当前场景不符 | 无记录（全片） |
| 本地 `size` 不等，或 `mtime_ms` 差 > 2000ms | 无记录（视频被替换/改过） |
| 在线 `id`（`<bvid>-<cid>`）不符 | 无记录（不串视频） |
| `schema` ≠ 1（含更高版本）、JSON 损坏/为空、根不是对象 | 无记录（更高版本标 `unsupported_schema`，不读） |
| 缺 `id`/`in`/`out`/`frames`，`in<0`，`out<in`，`frames<1`，字段类型不对 | 无记录 |
| `current_frames<=0`（帧数未知） | 无记录 |
| 合法但越界 | **clamp**：`in∈[0,last]`，`out∈[in,last]`，`last=current_frames-1` |
| 恢复结果恰为 `0..last` | 与「无记录」等价：不应用、不提示（保持 A/B 未设置） |

恢复时应用：`loop_.SetIn(in); loop_.SetOut(out); SyncLoopUi();` + 状态栏
「已恢复上次工作区 A=%1 B=%2（循环关）」，**不启用循环**。

---

## 3. 代码改动

| 文件 | +/− | 内容 |
|---|---|---|
| `src/core/MarkStore.h` | +101 | `WorkRange`/`WorkRangeKind`/`WorkRangeRecord`/`WorkRangePaths`、`ResolveWorkRangePaths`、`OnlineWorkRangePath`、`SerializeWorkRangeFile`/`ParseWorkRangeFile`、`ValidateWorkRange`、小 store `WorkRangeStore`（Open/OpenOnline/Restore/Save/Remove） |
| `src/core/MarkStore.cpp` | +494 | 上述实现；QSaveFile 原子写 + AppData 回退；在线路径；`LAPIAN_WORKRANGE_LOG` |
| `src/app/MainWindow.h` | +28 | 5 个测试钩子 + 私有 `MaybeRestoreWorkRange`/`EnsureWorkRangeStore`/`OnWorkRangeChanged`/`ScheduleWorkRangeSave`/`FlushWorkRange`/`ResetWorkRangeState`/`RemoveStoredWorkRange`/`CurrentWorkRangeFrames`/`WorkRangeOwner` + 成员（store/500ms 定时器/loaded/dirty/saves/path/owner） |
| `src/app/MainWindow.cpp` | +289 | 恢复/防抖/落盘/清除/测试钩子；接入 `ApplyIndexState`、`ApplyLoopIn/Out`、`ApplyWorkBarIn/Out/Pan`、`ApplyLoopClear`、`OpenLocalMedia`、`ShowEmptyState`、`ShutdownOnlineSession`、`closeEvent`、在线 `ApplyOnlineReady` |
| `src/app/main.cpp` | +410 | `--loop-test` 本地新增 5 个相位（24 检查）+ 可选 `--file-b` 跨视频（+3）；在线缓存命中相位新增 10 检查 |
| `tests/mark_store_test.cpp` | +379 | 109 条 W1 断言（路径/roundtrip/坏值/校验 clamp/store/回退/在线） |
| `spec.md` | +2 | §3.8 一句 + F8-A4 |

`MarkStore` 的标记语义/schema/合并规则一行未动；`PlaybackController`/`AppSettings`/
`OnlinePreviewDecoder`/`CacheSettingsDialog`/`PreviewDecoder`/`GfxWidget`/`MediaSession`/
`VideoDecoder`/`TimelineWidget`/`CMakeLists`/installer 均未改。

### 3.1 MainWindow 行为

- **恢复时机**：`ApplyIndexState()` 里 `SyncLoopUi()` 之后 `MaybeRestoreWorkRange()`；
  守卫 `work_range_loaded_`（每会话一次）+ `CanEditLoop()`（索引就绪且非空）。
  - 在线增长会话（含 DASH 缓存命中）要等 `online_index_complete_` 或索引帧数 ≥ 记录帧数
    才恢复，避免把 B clamp 到「还没下载完」的短索引；等不到（始终不完整）则本会话不恢复。
  - 本会话内用户已先改 A/B（`work_range_dirty_`）→ 不用旧记录覆盖。
- **身份切换**：`WorkRangeOwner()` = `local:<session path>` / `online:<bvid>-<cid>`；
  身份变化时重开 store，避免「在线 A → 本地 X → 在线 A」复用错记录。
- **写盘触发**：只对 A/B 变化——`I/O`、工作区手柄拖 A/B、平移、`Shift+L`/双击工作区（删除）。
  A/B 不完整（只有 A）时 flush 会**删除**旧记录，避免下次恢复过期 B。
- **防抖**：`OnWorkRangeChanged` 标 dirty + 500ms **单次** `QTimer`（重复 start 重置），
  到点 `FlushWorkRange()`；一次拖动只落最后一次。`WorkRangeSaveCountForTest()` 只在
  **成功写文件**时 +1（删除不算）。
- **flush 点**：切视频（确认换会话后、替换 session 前）、关在线会话（`ShutdownOnlineSession`）、
  进空态（`ShowEmptyState`，会 `loop_.Clear()`）、关窗（`closeEvent`）、测试钩子。
- **循环开关**：不写盘；新会话一律 `loop_.Clear()`（本地 `OpenLocalMedia`、在线
  `ApplyOnlineReady` 非清晰度切换分支）。在线**清晰度切换**（同 bvid+cid 且
  `online_switch_pending_`）保留当前内存 A/B（切档前已 flush），不重新恢复。
- **测试钩子**（`MainWindow` 公共）：`WorkRangeStateForTest()`（`a=/b=/frames=/has_record=/
  saves=/path=` + `online=/dirty=/loaded=`）、`WorkRangeSaveCountForTest()`、
  `FlushWorkRangeForTest()`、`SetWorkRangeForTest(in,out)`（走真实 A/B 变更路径 + 防抖）、
  `ClearWorkRangeForTest()`（只删该视频记录，不改内存 A/B）。

---

## 4. 门（自己跑，实际值）

环境：`TEMP/TMP=E:\拉片软件开发\.deps\tmp`；Qt `third_party\Qt\6.8.3\msvc2022_64\bin` +
`build\release\bin` 进 PATH；GUI 子系统 exe 全部 `Start-Process -Wait
-RedirectStandardOutput/-RedirectStandardError`。日志在 `build\W1\logs\`。

### 4.1 ctest **38/38**（66.59s）

`lapian_mark_store_test`：**281 OK / 0 FAIL**，其中 **W1 109 条**（基线 172 → +109）。

### 4.2 本地 `--loop-test --file media\测试.mp4`：**65 检查 / 0 失败**（原 41 + 新增 24）

5 个相位实际值（`media\测试.mp4`，4573 帧，`E:\拉片软件开发\media\测试.mp4.lapian-workrange.json`）：

| 相位 | 实际值 |
|---|---|
| `workrange-full-range-default` | 无记录 → `has_record=0 a=0 b=4572 frames=4573`，`loop=0 has_in=0`，**文件不存在** |
| `workrange-save-reload` | 设 100/160 + flush → `saves 5→6`、`has_record=1`、文件存在、JSON `in=100 out=160`；重开同一视频 → `a=100 b=160 has_record=1`、`loop=0 has_in=1 has_out=1`、状态栏「已恢复上次工作区 A=100 B=160（循环关）」 |
| `workrange-debounce` | 连续 5 次改 A/B（101…105 / 161…165）不手动 flush → 等 750ms：`saves 6→7 delta=1`，状态 `a=105 b=165`，JSON `in=105 out=165` |
| `workrange-fingerprint-mismatch` | 写错 `size`（+7）→ 重开 `has_record=0 a=0 b=4572`；再写错 `mtime_ms`（−60s）→ 重开同样不恢复 |
| `workrange-clear-removes` | 设 120/180 + flush（文件存在）→ `Shift+L`（`ClearLoopForTest`）→ **文件不存在**、`loop=0 has_in=0`；重开 → `has_record=0 a=0 b=4572` |

可选跨视频证据（`--file-b media\V9.mp4`，**68 检查 / 0 失败**，自清理不依赖历史状态）：

- `workrange-switch-video-b-default`：打开 B（V9，120 帧）→ `has_record=0 a=0 b=119`；
- B 存 `30..60` → 回 A：`a=50 b=90 has_record=1`（A 的记录没被 B 串）；
- 再开 B：`a=30 b=60 has_record=1`（B 的记录没被 A 串）。

### 4.3 在线缓存命中 `--loop-test --loop-online-cache-hit`：**32 检查 / 0 失败**（原 22 + 新增 10）

夹具 `build\pm-b-fresh\cache`（`BV1BMnv6jE5S/42915922022`，qn=80，1080P，`80-7/data.mp4`
31,057,770B complete + `audio-30280/data.mp4`）复制到 `build\W1\online\cache`（不写共享夹具）；
记录/图片/设置隔离在 `build\W1\online\{records,pics,settings}`。

- `loop_online ready=1 cache_hit=1 index=1 frames=3314`（现有相位全过）；
- 设 A=100/B=160 + flush → `has_record=1 saves=2`，
  `path=…\build\W1\online\records\BV1BMnv6jE5S-42915922022.lapian-workrange.json`，文件存在，
  `playurl=0 dash=0`；保存时 `loop=1`（证明循环开关不落盘）；
- `StartOnlineCacheHitForTest` 开新会话 → `ready=1 cache_hit=1 index=1 frames=3314`、
  `has_record=1 a=100 b=160`、**`loop=0 has_in=1 has_out=1`**、`playurl=0 dash=0`。
- 32 项含既有 `loop-online-*`（帧号在 100..160、jumps 递增、播放可用、无「播放结束」、
  无 loop-disabled）。

### 4.4 Medium 副本在线探针（打包布局）

`dist\*` → `%LOCALAPPDATA%\Programs\lapian-preview`，再用 `build\release\bin\lapian.exe`
覆盖（未动 `lapian-c8`/安装版/E:）。副本 exe SHA256
`80EC7FD4453E71F3CE67477BB58A7C66D260F242C506E1BA6CCB95A9B16A4B4F`（3,822,080B）。
同一在线缓存命中相位：**32/0**，`workrange_online_reload` 与 build 版一致
（`frames=3314 a=100 b=160 loop=0 playurl=0 dash=0`），记录/图片/设置隔离在
`build\W1\medium\{cache,records,pics,settings}`。

### 4.5 回归（最终二进制）

| 门 | 实际值 |
|---|---|
| `ctest` | **38/38**（66.59s） |
| `--key-test --file media\测试.mp4` | **77/0** |
| `--banner-test --file media\测试.mp4` | **74/0**（`banner_summary checks=74 fails=0 dpr=1.00 info_h=22`） |
| `--dpi-test` 96/120/144/192 | 每档 **36 检查 / 0 失败**（`normal/fullscreen/narrow-layout` 与 `inspector-*` 全过；layout problems 空） |
| `--drag-test --file media\测试.mp4` | P4 **96.8fps**（194 presents）、P6 p95 **22.8ms**、P7 0.1/17.1ms、P8 26 帧、P9 p95 39.4ms、全 pass（同机上一轮 95.7fps/23.1ms，未退化） |

### 4.6 只读/写入失败回退证据

- 单测 `TestWorkRangeStore`：把 **sidecar 路径用目录占住**（模拟源目录不可写）→
  `Save` 自动写 `%APPDATA%\...\marks\<hash>.workrange.json`，`using_fallback=1`，
  sidecar 目录保留；重开从回退读回 A/B；`Remove` 删回退但不删目录（返回 false + error）。
- 单测 `TestLongPathAtomicWrite` 风格的长路径：>260 字符中文路径的 sidecar/回退路径派生正确、
  `WorkRangeStore::Open` 不崩、无记录也不写文件。
- 写失败/损坏一律「按无记录 + 默认全片」，不弹窗、不阻塞播放。

---

## 5. 残余风险 / 已知缺口

1. **首次在线增长会话**若索引始终不完整（下载很慢/中途取消），本会话不恢复 A/B（等索引覆盖
   记录帧数或完整；下次完整后恢复）。这是为了不把 B clamp 到尚未下载的短索引。
2. **schema 更高**的工作区文件按「无记录」处理；之后用户改 A/B 会以 schema 1 覆盖它
   （不像标记 sidecar 会 block_save）。工作区非关键数据，未做覆盖保护。
3. **>MAX_PATH 本地 sidecar**：原子写走 `QSaveFile`，超长路径可能写失败并回退 AppData；
   单测只覆盖路径派生/不崩/回退可写场景，未在真实 >MAX_PATH 只读目录上验。
4. **进程崩溃**：500ms 防抖窗口内崩溃会丢最后一次 A/B（spec 首期不做崩溃恢复）。
5. **扫描中/无索引**时 A/B 不可编辑（`CanEditLoop()` false），此时没有写盘路径；
   产品上这期间也没有可改的 A/B。
6. **在线清晰度切换**保留内存 A/B（切档前已 flush）。若切档前 flush 失败（磁盘问题），
   切档后仍用内存值，重开文件可能回到旧记录。
7. `--loop-test` 重复跑时启动会先恢复上次记录（测试在 36.5s 显式清除），断言不受影响；
   但下一次跑的首段会短暂显示上次 A/B。需要在测试开头就清除时可再加一步。
8. F8-A4「换视频不串」的端到端证据只在 `--file-b` 变体（68 项）；默认门（65 项）不含跨视频，
   另由单测「不同视频 sidecar/回退路径都不同」+ owner 重置保证。

## 6. 回滚

工作区记忆与标记/循环互不影响：回滚只需 revert 本文件对应的两个 commit
（`feat(core): work range sidecar store` / `feat(loop): restore per-video work range`），
旧版忽略 `*.lapian-workrange.json`/`<hash>.workrange.json` 即可（不会被误读为标记）。
`spec.md` 的两行（§3.8 + F8-A4）一并回滚。

## 7. 证据文件

- 本地 loop 门：`build\W1\logs\loop-err3.log`（65/0）；跨视频：`loop-cross-err.log`（68/0）
- 在线 loop 门：`build\W1\logs\loop-online-err4.log`（32/0）
- Medium 在线：`build\W1\logs\medium-online-err2.log`（32/0）
- key/banner/dpi/drag：`build\W1\logs\{key2,banner2,dpi2-*,drag2}-err.log`
- 单测：`ctest --test-dir build\release -C Release`（`lapian_mark_store_test` 281/0）
