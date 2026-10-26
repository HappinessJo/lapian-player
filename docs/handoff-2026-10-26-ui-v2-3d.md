# UI v2 3D（在线 / 全屏 / 收口）交接 — 2026-10-26

- **状态**：3D 实现完成（非门；单壳）。M1–M9 的自动化部分逐条见 §5；**用户实机体感待确认**
  （本文件只记"体感"，不写"验收通过"）。
- **上游**：[3C 交接](handoff-2026-10-26-ui-v2-3c.md)（直接上游）、[3B](handoff-2026-10-26-ui-v2-3b.md)、
  [3A](handoff-2026-10-26-ui-v2-3a.md)、[UI v2 规划](ui-v2-plan.md)、[视觉基准](ui-v2-visual-reference.md)、
  [定义 §6.1](../docs/ui-v2-definition.md) 的 M1–M9。
- **本轮范围**：顶栏模式 chip（在线状态矩阵）/ 清晰度菜单点击建 / 缓冲文案；BannerSlot 接管唯一横幅；
  全屏 chrome 复核；设置/在线/扫码对话框 + 帮助 + 菜单进 Theme；状态栏压到 1 消息 + 3 指标；
  **删除 `InfoBar` 与旧布局**（单独 commit，可整体 revert 回 3C）；`LAPIAN_UI_V2`/`--ui-v2` 变 no-op；
  `--dpi-test --empty` 补 M3 空态；M2 截屏采样。
- **硬边界未动**：`GfxWidget` 呈现、`PreviewDecoder`/`DragStrategy`、`OnlineSession` 状态机、`CacheStore`、
  `media::StatusNotice` 语义、spec §3.7 键位、settings schema、任何门/阈值/性能数字、3B 的 Transport
  四档降级与两条滑块、3C 的检查器布局（折叠 44/220–560/三 tab/页脚）。

---

## 1. 一句话结论

UI v2 从"新旧两套布局"收口为**一套壳层**：`InfoBar` 的 5 个职责分别搬到 TopBar（文件/规格/模式/fps 角标）、
TransportBar（帧号/时间码/总帧）、BannerSlot（唯一横幅）、状态栏（1 条消息 + 音频/缓存/波形 3 指标）。
顶栏模式 chip 按规划 §3.2 的在线状态矩阵输出实际 qn/fps（不拿 API 标称冒充），DEC-4 角标进 chip；
横幅按原型 `#1b1a13 + #f0cb7a + 左侧图标`；清晰度菜单点击时才建；全屏、对话框与菜单全部进 Theme。
单壳门禁全绿（M1 数字一个没动），M3 空态列补齐；在线 1080P 断言因 SESSDATA 过期（`vip_status=-1`）
未跑，M2/quality/vip 见 §7。

---

## 2. 改了什么（文件级）

**删除**

| 文件 | 内容 |
|---|---|
| `src/app/InfoBar.{h,cpp}` | 5 职责已搬完；CMake 条目删除。**单独 commit**，`git revert` 可整体回 3C。 |

**新增**

| 文件 | 内容 |
|---|---|
| `docs/handoff-2026-10-26-ui-v2-3d.md` | 本文件。 |

**修改**

| 文件 | 内容 |
|---|---|
| `src/app/ui/BannerSlot.{h,cpp}` | 从"reparent InfoBar 的 banner row"改为**自己建** `infoBanner`/`infoBannerClose` + 左侧 `v2BannerIcon`；新增 `SetMedia/SetScanning/SetError/ClearError/SetOnlineFrameRateBanner/ClearOnlineFrameRateBanner/Clear/IsVisibleKind/Dismiss/BannerCloseVisible/KindName/Text/Tooltip/SecondarySummary/SecondaryTooltip/ActiveNoticeKinds`；高度 0/令牌、单行；`kind` 动态属性进全局 QSS。`media::StatusNotice` 一行未改。 |
| `src/app/ui/TopBar.{h,cpp}` | 文件/规格标签 + `onlineFpsBadge` 由 TopBar 自建（不再从 InfoBar 搬）；`SetSpecText`、`SetOnlineFpsBadge/ClearOnlineFpsBadge/FpsBadgeText/FpsBadgeVisible`；角标放进模式 chip 内部；删除 `AdoptFileLabel/AdoptSpecLabel/AdoptFpsBadge`。 |
| `src/app/ui/TransportBar.{h,cpp}` | 自建 `v2Timecode`/`v2FrameNumber`/`v2FrameTotal`；新增 `SetFrameRate/SetFrame/SetTotal/ClearReadouts/FrameText/TotalText/TimecodeText`；删除 `AdoptTimecode/AdoptFrame/AdoptTotal`（`AdoptVolume` 保留）。M7 四态 `rel=85,13 32x14`。 |
| `src/app/MainWindow.{h,cpp}` | 删除 `ui_v2_enabled_/info_bar_`、`BuildLegacyUi`、`LayoutStateLegacy`、`LayoutProblemsLegacy`、所有 `shell_ == nullptr` 分支；`ui_v2` 参数保留但 `(void)`；`UiV2ForTest()` 恒 true；新增 `ApplyMediaInfoToUi/ClearMediaInfoUi/SetFrameForUi/SetTotalForUi/ClearOnlineFrameRateUi/UpdateCacheMetric/TotalForUi`（`total_for_ui_` 三态）；`UpdateShellModeChip` 在线矩阵；`RefreshInspectorInfo()` 改读 `total_for_ui_`；`OnlineFpsBadge* / OnlineFrameRateBanner* / Dismiss` 指向 TopBar/BannerSlot；`BannerStateForTest()` 键序不变、`info_h=` = BannerSlot 高度；`BannerUiStateForTest()` 的 `loop=` 改指 Transport 循环键；状态栏 1+3；`--dpi-test --empty`/`--ui-shots-test` 支持；MarkPanel 首次 current item 预热。 |
| `src/app/CacheSettingsDialog.cpp` | 状态语义色改动态属性 `state=ok/warn/error` + 全局 QSS；对象名 `cacheRootState`/`cacheStatusLabel`；删除 per-widget `setStyleSheet`。 |
| `src/app/OnlineResolveDialog.cpp` | 隐私/状态/清晰度/错误/提示/二维码/登录状态全部改 objectName + 全局 QSS（动态 `state`）；删除 per-widget `setStyleSheet`；二维码白底走 `#onlineQrImage`。 |
| `src/app/ui/Theme.cpp` | 新增横幅（`#v2BannerSlot[kind]`/`#infoBanner`/`#infoBannerClose`/`v2BannerIcon`）、`onlineFpsBadge`、`noticeInfo`/`cacheInfo`、对话框/菜单/帮助 QSS；`ApplyTheme` 无条件应用。 |
| `src/app/main.cpp` | 主题无条件应用；`ExtractUiV2Args` 增加 `--empty`/`--ui-shots-test`；`--dpi-test --empty`（无需 `--file`，4 DPI 空态 + mode/折叠/banner 断言）；`--ui-shots-test` 截主窗/设置/帮助/在线/速度菜单；`--ui-v2` 仍解析。 |
| `src/CMakeLists.txt` | 删除 `app/InfoBar.{cpp,h}`。 |

---

## 3. 6 步做法（按提交顺序）

1. **顶栏模式 chip 收口**：`UpdateShellModeChip()` 输出 `未打开`（neutral）/`本地 · 零网络`（ok）/
   `在线 · 解析中`/`在线 · 缓冲中`/`切档中 → qn`（warn）/`在线 · qn · fps`（cache，实际 qn +
   `FormatOnlineFps(actual)`）/`缓存命中 · 0 网络`/`打开失败`（err）；降帧时 warn + 文案 `· 降帧`。
   `onlineFpsBadge` 同一 QLabel 进 chip，`kind=warn` 走 QSS。清晰度菜单 `quality_menu_` 只在
   `ShowOnlineQualityMenu()` 里建，`OnlineQualityMenuActionsForTest()` 改读菜单模型、
   `TriggerOnlineQualityEntryForTest()` 直接走 `OnOnlineQualityChosen(qn)`（断言一行未改）。
   缓冲槽文案由 3B 的 `v2BufferSlot` 保持 `已缓存 x/y MB`，`--banner-test` 的 `volume=` 语义不变。
2. **BannerSlot 接管横幅**：`SetMedia/SetScanning/SetError/SetOnlineFrameRateBanner` → 内部
   `BuildMediaStatusNotices + SelectPrimaryNotice`（语义原样）→ 只画 primary；关闭按钮仅降帧；
   `IsVisibleKind(kFrameRateDrop)`/`Dismiss()`；`infoBanner`/`infoBannerClose` 名字不变，`banners=` 恒 1。
   配色 prototype `.banner`：底 `#1b1a13`、字 `#f0cb7a`、左图标（error/info 走语义变体）。
3. **全屏 + 对话框进主题**：`PlayerShell::SetFullScreenChrome` 已有（顶栏/横幅/状态栏/检查器隐藏，
   Transport/时间轴保留，`Esc`/`F`/双击语义不变）；`CacheSettingsDialog`/`OnlineResolveDialog`/帮助
   `QMessageBox`/清晰度与速度 `QMenu` 全部走 `Theme::GlobalStyleSheet()` + 令牌；只保留三处动态色
   （fps 角标/危险按钮/banner kind），均以 objectName + 动态属性实现，无 per-widget QSS。
4. **状态栏压缩**：`noticeInfo`（1 条消息）+ `audioInfo`/`cacheInfo`（新）/`waveformInfo`；
   `speed_label_`/`volume_slider_`/`loop_label_`/`statusInfo` 不再 `addPermanentWidget`（对象保留，
   仍被旧日志/`SyncLoopUi` 写入）；`LayoutProblemsV2` 的 status 列表同步为 4 个。
5. **删除 InfoBar 与旧布局**：5 职责搬完后删除文件/CMake/成员/旧函数/旧分支；`RefreshInspectorInfo()`
   改读 `total_for_ui_`（精确 / ≈估算 / 扫描中三态仍由 `TotalFrameCount` 决定）；`LayoutStateV2` 删除
   `info=`/`info_h=` 别名；`--dpi-test` 脚本按新键；`LAPIAN_UI_V2`/`--ui-v2` 仍解析但 no-op。
6. **空态 × DPI**：`--dpi-test --empty` 不需要 `--file`，4 DPI 各跑空态 + 全屏 + 窄窗；每相位
   `LayoutProblemsForTest()` 为空，并额外断言 `mode=未打开`、检查器折叠 44、`banner_h=0`。

---

## 4. 单壳 vs 双壳（收口对照）

| 项 | 3C（双壳） | 3D（单壳） |
|---|---|---|
| 开关 | `LAPIAN_UI_V2`/`--ui-v2` 选旧/新布局 | 仍解析，**无效果**；`UiV2ForTest()` 恒 true |
| 文件/规格 | InfoBar 的 `infoFile/infoFormat` 被 reparent | TopBar 自建同名标签 |
| 帧号/时间码/总帧 | InfoBar 的 `v2FrameNumber/v2Timecode/v2FrameTotal` 被 reparent | Transport 自建同名标签 |
| 唯一横幅 | InfoBar 的 `infoBannerRow` 被 reparent 到 BannerSlot | BannerSlot 自建 `infoBanner/infoBannerClose` |
| 通知语义 | media::StatusNotice | **一行未改** |
| 状态栏 | 6 个 permanent widget | 1 消息 + 3 指标（4 个） |
| 模式 chip | 4 档（neutral/local/online/warn/error） | 按 §3.2 矩阵 + 实际 qn/fps/降帧 |
| 收口 | 旧壳可回退 | `git revert` 删除 commit 回 3C |

---

## 5. M1–M9 证据（自动化部分）

> 单壳、100% DPI（除特别注明）；命令与日志见 §8。**用户实机体感待确认**。

| 门 | 结果 |
|---|---|
| **M1 回归** | `ctest 37/37`；`--key-test 77/0`；`--mark-test 0`；`--note-test 17/0`；`--snapshot-test 35/0`；`--readout-edit-test 28/0`（M5 17/17 tooltip）；`--banner-test 74/0`；drag P4 64.5fps / P6 p95 19.9ms；strategy 7/7；startup P1 759–857ms / P2 988–1100ms；本地 `--local-drag-parity-test` **9/0**；在线 M1 三样本 **OB-1 24/0、OB-2 24/0（首跑 25/1 `ui_gap` 后复跑通过）、OB-3 25/0**；parity 61/0、longpress 54/0、T8.6.1b 24/1（本机既有 `forward_ui_gap` 206–228ms，3C v2 A/B 213ms；多次复跑未到 200 以内） |
| **M2 对比度** | 截屏采样：顶栏 14.66、Transport 14.66、检查器 13.92、状态栏 14.66、横幅 11.24、菜单 13.92、设置 15.80、在线对话框 6.72:1（全 ≥4.5） |
| **M3 DPI** | 4 DPI × {播放, 全屏, 窄窗} `LayoutProblemsForTest()` 全空；`--dpi-test --empty` 4 DPI 空态全空 + mode=`未打开`/折叠 44/banner 0；每栏=令牌±2；检查器 304/220–560/折叠 44 |
| **M4 画面占比** | **64.17 / 64.17 / 64.42 / 56.64 / 60.80%**（100/125/150/200/窄窗；门槛 62/62/62/55/59）——与 3A/3B/3C **一个数字没动** |
| **M5 键位** | `--readout-edit-test` 运行期 tooltip 全表 17/17；`--key-test 77/0` |
| **M6 单横幅** | `--banner-test` 74/0；`banners=1`、`kind/visible/text/secondary/active` 全过；横幅出现时只位移（`stage-height` 用画面+横幅算稳态） |
| **M7 帧号常驻** | 四态 `rel=85,13 32x14`（暂停/播放/拖动/全屏完全一致） |
| **M8 冷启动** | P1 759–857ms（≤1.5s）、P2 988–1100ms（≤2.5s）、`d3d_init_at_startup=0`；**代价**：MarkPanel 首次 current item 预热（见 §7-3） |
| **M9 可回退** | 删除 commit 单独一个；`git revert <commit>` 可整体回到 3C 双壳 |

---

## 6. 视觉自检（原型 vs 产品）

- 原型渲图：`build/ui-v2-shots/proto-paused.png`（§2 命令）。
- 产品：`build/ui-v2-shots/3d-main.png`（1280×720，`--ui-shots-test` 自抓）、
  `build/t6.5-banner-3dfinal2-*.png`（横幅 8 态）、`build/ui-v2-shots/3d-dialog-settings.png`、
  `3d-dialog-online.png`、`3d-dialog-help.png`、`3d-menu-speed.png`。

| 区域 | 已对齐 | 仍缺 |
|---|---|---|
| 顶栏 | 文件图标+名、规格 chip、模式 chip（语义色+图标）、fps 角标进 chip、打开/在线键位角标、?/设置 | 在线真实 1080P 文案未截图（凭据过期） |
| 横幅 | 26px、`#1b1a13` 底、`#f0cb7a` 字、左侧图标、右侧 ×（仅降帧）、单行 | —— |
| 状态栏 | 1 条消息 + 音频/缓存/波形 3 指标；`speed/loop/volume/statusInfo` 已退出 | 缓存指标用发布字节数（不查 CacheStore 锁） |
| 全屏 | 顶栏/横幅/状态栏/检查器隐藏；Transport+时间轴保留；几何/焦点恢复 | —— |
| 对话框 | 设置/在线/扫码/帮助全深色；二维码白底；不再浅色混入 | 间距/分组仍是 3A 以前的结构 |
| 菜单 | 速度菜单深色、勾选可见；清晰度菜单同 QSS | 清晰度菜单真实 1080P 未截图 |

---

## 7. 残余与风险（按优先级）

1. **`QTreeWidget` 第一次 `setCurrentItem` 是一次性 ~600ms 的 Qt 字体/样式初始化**（本机实测
   580–650ms）。不预热会落在第一个 Ctrl+M / 截图跳转 / 在线首帧上，使 `--online-m1-test` 的
   `ui_gap_le_200ms` 变成 600ms。现做法：`BuildShellUi` 里用假标记 `SetMarks + SelectMark + SetMarks({})`
   预热，P1 759–857ms / P2 988–1100ms（门槛内，但比 3C 的 187/414 慢）。**未定位 Qt 内部原因**；
   不能用 `setUpdatesEnabled(false)` / 屏蔽信号 / 关 QSS 消除（都试过）。
2. **在线 M2 / quality / vip 1080P 断言未跑**：Medium 默认凭据 `nav vip_status=-1`（SESSDATA 过期）；
   `--online-http-selfcheck --sessdata-file .secrets\bilibili_sessdata.txt` 实测
   `code=-101 is_login=0 cookie_sent=1 vip_status=-1`，确认是凭据失效而非代码。
   `--online-quality-test --ui` 只到 `ui_login ok=0`、`ui_normal_user ok=0`；`--online-m2-test`
   同样 `nav_login/nav_normal_user ok=0`。M1 仍可跑（匿名 qn=32、`blocked-quality` 只记录不判失败）。
3. **T8.6.1b `forward_ui_gap_le_200ms` 206–228ms**：3C v2 A/B 213ms，3B 已记录 204–239ms 本机基线；
   本轮 **24/1**（多次复跑：206/220/222/223/228ms）。诊断：gap 事件在 forward 相位 **at_ms≈563ms**
   处（一次 211ms），不是首帧/打开；`cmd /c start /high` 高优先级运行仍 225ms；关 QSS/`setUpdatesEnabled`
   在单选路径的试验也不消除。按既有基线记录、不改阈值。原任务写“本轮 24/0”，在当前机器/后台负载下
   未复现；若关掉后台负载（本机有 Chrome/Steam++/QQMusic 等）可能回到 24/0，但这不改变代码结论。
4. **`--online-fault-test` ui_gap 220ms > 200**：scenarios 10/10、checks 92/0，但 `ui_gap` 判失败；
   3C v2 同机 1009ms（3 个 gap 事件），属既有基线。**fault 测试的 `build/t8.13-fault` 必须先清**，
   否则 `cache_corrupt_rebuild`/`mark_after_reopen` 会因旧 fixture 假失败。
5. **`--online-qr-test` 依赖干净 `%TEMP%`**：脏 TEMP 会让 `nav -101 no credential save` 假失败
   （QTemporaryDir 复用/残留）；用全新 TEMP 目录后 19/0 + `--ui` 21/0。
6. **模式 chip 长文本在窄窗口可能挤**：`在线 · 1080P · 29.412fps · 降帧` + 清晰度按钮 + 4 个顶栏按钮；
   `TopBar::minimumSizeHint()` 仍返回 240，但布局最小宽度由子控件决定。本地 820 窄窗已覆盖，
   **在线 820 未截图/未断言**。
7. **`cacheInfo` 用 `online_published_bytes_`**（视频流已发布字节）而非 `CacheStore::UsageBytes()`：
   后者要拿缓存锁，实测与下载线程抢锁会把 M1 ui_gap 拖到 600ms；本地模式显示 `缓存 —`。
8. **`--ui-shots-test` 是新增测试入口**（`CaptureThemeShotsForTest`），只用于 M2 截图；产品路径不调用。
9. **`MarkPanel` 仍是既有观感**（行内删除/选中 accent 竖条缺），与 3C 相同；3D 未动。

---

## 8. 复现命令

```powershell
cd 'E:\拉片软件开发'
$env:PATH = 'E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;' + $env:PATH
$env:TMP = 'E:\拉片软件开发\.deps\tmp'; $env:TEMP = $env:TMP
$cmake = 'E:\拉片软件开发\third_party\cmake\3.31.6\cmake\data\bin\cmake.exe'
& $cmake --build build\release --config Release
& 'E:\拉片软件开发\third_party\cmake\3.31.6\cmake\data\bin\ctest.exe' --test-dir build\release -C Release

$exe = '.\build\release\bin\lapian.exe'; $media = 'media\测试.mp4'; $sp = 'build\ui-v2-gates\settings-3d'
$env:QT_SCALE_FACTOR='1'
& $exe --banner-test --file $media --banner-tag 3dfinal --settings-path $sp
& $exe --dpi-test --file $media --dpi-tag 3d --dpi-expect-scale 1
& $exe --dpi-test --empty --dpi-tag 3de --dpi-expect-scale 1
& $exe --key-test --file $media --settings-path $sp
& $exe --mark-test --file $media --settings-path $sp
& $exe --note-test --file $media --settings-path $sp
& $exe --snapshot-test --file $media --settings-path $sp --snapshot-pictures build\ui-v2-gates\pics-3d
& $exe --readout-edit-test --file $media --settings-path $sp
& $exe --fullscreen-test --file $media --settings-path $sp --snapshot-pictures build\ui-v2-gates\pics-fs
& $exe --loop-test --file $media --settings-path $sp
& $exe --loop-end-test --file $media --settings-path $sp
& $exe --drag-test --file $media --settings-path $sp --drag-samples 20
& $exe --drag-strategy-test --file $media --settings-path $sp
& $exe --startup-test --file $media --settings-path $sp
& $exe --startup-test --settings-path $sp
& $exe --ui-shots-test --file $media --settings-path $sp   # build/ui-v2-shots/3d-*.png
```

- **在线（Medium 副本 `%LOCALAPPDATA%\Programs\拉片软件`；本轮已把 exe 覆盖为 3D，旧 3C 备份
  `lapian-3c.exe` 留在同目录）**：
  ```powershell
  $online = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"
  & $online --online-m1-test --bvid BV1BMnv6jE5S --cid 42915922022 --tag 3d-ob1 --timeout-ms 40000
  & $online --online-longpress-test --bvid BV1BMnv6jE5S --cid 42915922022 --longpress-fresh-cache
  & $online --online-drag-parity-test --bvid BV1BMnv6jE5S --cid 42915922022 `
      --drag-seconds 1 --drag-samples 60 --m1-throttle-bps 262144
  & $online --online-drag-preview-test --bvid BV1BMnv6jE5S --cid 42915922022
  & $online --online-offline-test --bvid BV1BMnv6jE5S --cid 42915922022 --tag 3d-offline --timeout-ms 20000
  & $online --online-cache-test --tag 3d-cache
  & $online --online-qr-test           # 需要干净 %TEMP%
  & $online --online-qr-test --ui
  & $online --online-fault-test        # 先清 build\t8.13-fault
  ```
- **坑**：`--snapshot-test` 前清 `media\snapshots`；`--mark-test` 后删
  `media\测试.mp4.lapian.json` 再跑 note；`--dpi-test` 的 tag 参数是 `--dpi-tag`；
  `--online-qr-test` 前清空系统 `%TEMP%`；`--online-fault-test` 前清 `build\t8.13-fault`。
- **本机 CMake**：本轮删文件后按 3B/3C 做法把 `build\release` 改名 `build\release-3c` 后全新 configure
  （**注意：改名后老 exe 在 `release-3c\bin`，可做 A/B**）。重配命令：
  `-G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=<Qt> -DLAPIAN_BUILD_TESTS=ON`。

---

## 9. 回滚

- 代码提交 `b38df22`（`feat(ui-v2): 3D online/fullscreen closeout - delete InfoBar and legacy shell`）
  是**单个提交**：`git revert b38df22` 可整体回到 3C 代码（双壳 + `LAPIAN_UI_V2` 生效）。
  文档提交只改 AGENTS/规划/视觉基准/本交接，不参与回滚。
- 若只想恢复旧壳临时验证：3C 二进制在 `%LOCALAPPDATA%\Programs\拉片软件\lapian-3c.exe` 与
  `build\release-3c\bin\lapian.exe`；后者是 Low integrity，在线用例必须用 Medium 副本。
