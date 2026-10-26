# UI v2 3C（记录层）交接 — 2026-10-26

- **状态**：3C「记录层」实现完成（非门；M5/M4/M7 与 mark/note/snapshot 退出条件已实测）。旧壳保留、可回退。
- **上游**：[3B 交接](handoff-2026-10-26-ui-v2-3b.md)（直接上游）、[3A 交接](handoff-2026-10-26-ui-v2-3a.md)、
  [UI v2 规划](ui-v2-plan.md)（§2.1/§2.2/§2.3/§4.2/§7.1/§7.6/§7.7/§7.8/§11 A.1/A.5）、
  [视觉基准](ui-v2-visual-reference.md)、[定义 §6.1](ui-v2-definition.md) 的 M4/M5/M7。
- **本轮范围**：`InspectorPanel`（三 tab + 计数徽标 + 折叠 44 + 页脚存储位置）、MarkPanel 内嵌（同 objectName）、
  截图网格（在线 `index.jsonl` / 本地 `snapshots/*.png` + 文件名反解帧号）、信息页（`session_->info()` +
  `TotalFrameCount` 三态）、折叠/聚焦联动。
- **硬边界未动**：`GfxWidget` 呈现、`PreviewDecoder`/`DragStrategy`、`OnlineSession` 状态机、`CacheStore`、
  `media::StatusNotice` 语义、spec §3.7 键位、settings schema、任何门/阈值/性能数字、InfoBar（3D 才删）、
  状态栏 6 控件（3D）、横幅配色（3D）、3B 的 Transport 四档降级与两条滑块。

---

## 1. 一句话结论

检查器从"MarkPanel 自己的表头"升级为 v2 记录层：**头 26（标记/截图/信息三 tab + 计数徽标 + `+` + 折叠）→
stack 三页（既有 MarkPanel 原对象 / 截图网格 / 信息表）→ 页脚存储位置**；折叠 44 变成竖排
"标记 N / 截图 M" + 展开按钮；截图网格读历史文件（在线 `index.jsonl`、本地 `snapshots/*.png` 反解帧号），
点击沿用 T5.2 的"先重绘 UI、再 `singleShot(0)` 精确解码"；信息页直接读 `session_->info()` 与
`TotalFrameCount` 三态（不反解析 InfoBar 文本）。MarkPanel 原对象、objectName 与全部信号一行未改，
`tests/mark_panel_test` 仍能独立构造；`--mark-test`/`--note-test` 断言一行未改（旧壳通过，v2 只新增 2 条
3C 用例）。门禁新旧壳各一遍：`ctest 37/37`、`--dpi-test` 4 DPI problems 全空（v2 每档 37 项检查，新增
检查器折叠/展开/tab 用例）、M4 仍 **64.17/64.17/64.42/56.64/60.80%**、`--banner-test 74/0`、`--key-test 77/0`、
`--mark-test`（v2 3 条新 check 全 1）、`--note-test 17/0`、`--snapshot-test 35/0`、`--fullscreen-test`、
`--loop-test`/`--loop-end-test`、`--drag-test` P4 66.3–67.7fps / P6 p95 15.6–22.3ms、`--drag-strategy-test 7/7`、
`--startup-test` v2 P1 187ms / P2 414ms；M7 四态 `rel=85,13 32x20` 不变。

---

## 2. 改了什么（文件级）

**新增**

| 文件 | 内容 |
|---|---|
| `src/app/ui/InspectorPanel.h/.cpp` | 检查器容器：头 26（三 tab、`v2MarkBadge`/`v2ShotBadge`、`v2InspectorAdd`、`v2InspectorCollapse`）、`v2InspectorStack` 三页、`v2InspectorFooter`+`v2StoreText`、折叠 rail（`v2InspectorRail`+`v2InspectorExpand`）。截图网格用自绘 `ShotTile`（首次绘制才读盘、按需缩放、≤60 张）；信息页 `QTableWidget`（两列、NoFocus）；全 NoFocus。**不含业务**：计数/截图条目/信息行/存储文本都由 MainWindow 推。 |

**修改**

| 文件 | 内容 |
|---|---|
| `src/app/ui/PlayerShell.h/.cpp` | 检查器 host 里换成 InspectorPanel；删掉 3A 的 `inspector_rail_`/`inspector_content_`/`inspector_toggle_` 与 `UpdateInspectorToggle`；`SetInspectorCollapsed(bool)` **对外语义/调用点不变**（host 44/304、h_split、`inspectorCollapsedChanged`），内部改为委托 `InspectorPanel::SetCollapsed`；`ApplyTokens` 下发 `inspector_head/inspector_foot/rail 44`；新增 `inspectorPanel()`；删除 `inspectorContentHost()`（唯一调用点是 MainWindow） |
| `src/app/MarkPanel.h/.cpp` | 新增 `SetEmbeddedMode(bool)`：嵌进检查器时只隐藏自己的标题行（标题/计数/`markStorage`）、`markHint`、树表头，把外边距收 0、行首缩进 6px，并把帧号列改成原型风格（accent `#5b8def` + Consolas 11px 600 左对齐）；MarkStore 自动生成的"帧 N"标签不再重复到标签列（用户自己写的标签照常显示）。**不销毁对象、不改 objectName/信号/公开 API**，默认 `false`（独立构造行为与既有一致） |
| `src/app/MainWindow.h/.cpp` | `BuildShellUi` 用 `AdoptMarkPanel` 收 MarkPanel；`AdoptShellWidgets` 里 `BindAddAction(mark_action_)`、接 `shotActivated`/`tabChanged`；`SyncInspectorChrome()`（计数徽标/页脚/信息页，随 `SyncShellChrome` 走，无新定时器）；`RefreshInspectorShots()`（两路数据源 + 前缀过滤 + 去重 + 排序 + 帧号时间码 + ≤60）；`RefreshInspectorInfo()`（文件/格式/分辨率/视频/帧率/时长/总帧数/音频/旋转/VFR/路径）；`ActivateShot()`（T5.2 顺序）；`EnsureInspectorMarksVisible()`（折叠/非标记 tab → 展开+切标记），在 `FocusNoteEditorForTest` 与 `ActivateMark` 调用；`InspectorStateForTest()`/`SetInspectorCollapsedForTest()`/`SetInspectorTabForTest()`；`UpdateMarkStorageUi` 末尾推页脚；`snapshotSaved` 后刷新截图网格 |
| `src/media/SnapshotNaming.h/.cpp` | 新纯函数 `ParseSnapshotFrameFromName()`（只认 `{base}_f{帧}_{HH-MM-SS-FF}[_序号].png`，贪婪匹配取最后一段，大小写不敏感，失败/溢出返回 -1） |
| `src/app/ui/Tokens.h` | 新 `inspector_foot`（regular 22 / compact·dense 20） |
| `src/app/ui/Theme.cpp` | 新 QSS：检查器头/页脚卡片、徽标、`v2StoreText`（warning 属性 = 回退色）、rail 文本、截图网格/空态/截断说明、信息表（`::item` 虚线底 + 8px 内边距） |
| `src/app/ui/Glyph.h/.cpp` | 新 `kFolder`（页脚存储位置图标） |
| `src/app/main.cpp` | `--dpi-test`（仅新壳）：新增 5 组检查器用例——折叠 44、展开极值 560/220（按"画面列 ≥420"夹取）、截图页、信息页、窄 220；每个相位留 `build/t6.4-dpi-<tag>-inspector-*.png`。`--mark-test` 新增 3C 两条用例（旧壳自动跳过）：折叠后 `MarkCount`/选中不变；折叠态 `FocusNoteEditorForTest()` 自动展开到标记 tab |
| `src/CMakeLists.txt` | 加 `app/ui/InspectorPanel.{h,cpp}` |
| `tests/snapshot_naming_test.cpp` | 新增 `TestParseFrame()`（9 项：序号/7 位帧号/大小写/视频名带伪 `_f` 段/非法命名） |

**没动**：`src/core/*`、`OnlineSession`、`CacheStore`、`media/StatusNotice`、`GfxWidget`、`PreviewDecoder`、
`DragStrategy`、settings schema、测试断言（`tests/mark_panel_test.cpp` 与 `--mark-test`/`--note-test` 既有行一字未改）。

---

## 3. 关键做法（接手前先看这 5 条）

1. **InspectorPanel 只做容器，数据全由 MainWindow 推**（和 3A/3B 的"壳层不读业务"一致）：
   `SetMarkCount/SetShotItems/SetInfoRows/SetStorageText`。`MarkCount` 来自 `mark_panel_->MarkCount()`
   （与 `MarkCountForTest()` 同源），截图数来自历史扫描 + 本次会话，信息页直接读 `session_->info()` 与
   `info_bar_->TotalForTest()`（`TotalFrameCount` 三态：`精确 / ≈估算 / 扫描中`）。
   **刷新全部走既有信号/状态变更**（`SyncShellChrome`、`RefreshMarksUi`、`snapshotSaved`、`tabChanged`），
   **没有新增定时器/后台线程**（3B 删掉的 250ms 轮询保持删除状态）。
2. **MarkPanel "搬不换"（同 3A 的原则）**：原对象进 `v2InspectorStack` page0，`markPanel/markTree/
   markLabelEdit/markNoteEdit/markStorage` 名字不变；嵌入模式**只 hide**标题/自己的存储标签/提示/表头，
   `markStorage` 对象仍在（`StorageSummary()` 仍可读）——页脚 `v2StoreText` 用同一份字符串，**存储位置只有一个来源**。
   `tests/mark_panel_test` 仍独立构造（默认非嵌入），`--mark-test`/`--note-test` 公有 API 不变。
3. **折叠 44 / 展开 304（220–560）**：`PlayerShell::SetInspectorCollapsed` 仍负责 host 宽度、`h_split`
   与信号；`InspectorPanel::SetCollapsed` 负责"rail ↔ 头+stack+页脚"的可见性，并**显式 `mark_panel_->setVisible(false)`**
   （不销毁；`MarkCount`/选中不变）。折叠 rail 竖排 `标 记 / N / 截 图 / M`（原型 .rail），展开按钮
   `v2InspectorExpand`；头部折叠按钮沿用 3A 的 `v2InspectorCollapse`（objectName 不变）。
   MarkPanel 与页面在 220px 时仍可用：tab 在 <300px 自动收图标/内边距（`SetCompact`）。
4. **截图网格两路数据源**：
   - 在线：`<Pictures>/拉片软件/<bvid>/index.jsonl`（每行 `frame/path`；文件不存在则跳过），
     回退 `%APPDATA%\拉片软件\online\snapshots\<bvid>/`；
   - 本地：`<视频目录>/snapshots/*.png` + `<Pictures>/拉片软件/*.png`，用 `ParseSnapshotFrameFromName`
     反解帧号，并按 `sanitize(视频名)_f` 前缀过滤（`V9_*` 不会混进 `测试_*` 的网格）；
   - 共 ≤60 张（按帧号升序取最近 60；徽标显示真实总数，超出时网格底部 `v2ShotNote` 说明）；
   - 缩略图**首次绘制才读盘**（`ShotTile::EnsurePixmap`，`QImageReader` + 缩放到 ≤2× 格子），
     `QScrollArea` 只画可见区域 → 首屏只解码几张小图；**没有线程/定时器**；
   - 点击 `shotActivated(frame)` → `MainWindow::ActivateShot`：先 `Pause`+播放头/帧号重绘，
     再 `QTimer::singleShot(0)` 精确 `GoToFrame`（与 T5.2 标记跳转同一顺序，`navigation_generation_` 取消过期跳转）。
5. **折叠/聚焦联动 + flush 契约**：`InspectorPanel::SetTab`/`SetCollapsed` 在切换/折叠前调用
   `mark_panel_->FlushEditor()`；`FocusNoteEditorForTest()` 与 `ActivateMark()` 先 `EnsureInspectorMarksVisible()`
   （折叠 → 展开；非标记 tab → 切标记）。**默认 tab = 标记**（`--key-test` 的 `focus-tree` 依赖它）。

---

## 4. 门禁数字（新旧壳各一遍）

> 证据日志：`build/ui-v2-gates/3c-gates.log`（整轮）、`3c-gates2.log`（第二遍，捕获失败的记录留档）、
> `ev-*.log`（逐相位、可复核的 stdout/stderr）、`p3c-dpi-v2-*.log`。产品截图：`build/t6.5-banner-v2-*.png`、
> `build/t6.4-dpi-3c3-v2-*-inspector-*.png`、`build/t5.2-marks-ui.png`。

### 4.1 新旧壳对照

| 门 | 旧壳（默认） | 新壳（`LAPIAN_UI_V2=1`） |
|---|---|---|
| `ctest` | — | **37/37**（`lapian_mark_panel_test` 独立构造仍过；`lapian_snapshot_naming_test` 含新反解用例） |
| `--dpi-test` | 4 DPI × 3 相位，problems 空（每档 9 检查） | 4 DPI × 3 相位 + **3C 检查器用例**，problems 空（每档 **37 检查，0 失败**） |
| `--banner-test` | **74/0** | **74/0** |
| `--key-test` | **77/0** | **77/0**（`focus-tree` 通过 = 默认 tab 仍是标记） |
| `--mark-test` | 0 失败（3C 两条自动跳过） | **0 失败** + 新增 3 条 check 全 `ok=1` |
| `--note-test` | 17/0 | 17/0 |
| `--snapshot-test` | 35/0 | 35/0 |
| `--readout-edit-test` | — | 0 失败；M7 四态 `rel=85,13 32x20`；tooltip 表含 `v2InspectorAdd`（= `在当前帧添加标记（Ctrl+M）`） |
| `--fullscreen-test` | 全过（`v2=0 appbar=0 banner=0 status=1 inspector=1 transport=0 timeline=1`） | 全过（`v2=1 appbar=0 banner=0 status=0 inspector=0 transport=1 timeline=1`） |
| `--loop-test` / `--loop-end-test` | 全过 | 全过 |
| `--drag-test` | P4 67.7fps / P6 p95 17.5ms | **tl=132** P4 66.5 / P6 22.3 / P9 38.5；**tl=340** P4 66.3 / P6 15.6 / P9 31.2（P6 ≤150ms） |
| `--drag-strategy-test` | 7/7（release_p95 19.3ms） | **7/7**（release_p95 14.7ms） |
| `--startup-test` P1 / P2 | 263ms / 327ms | **187ms / 414ms**（`d3d_init_at_startup=0`；空态 190ms） |

### 4.5 在线（Medium 副本 `%LOCALAPPDATA%\Programs\拉片软件`）

| 用例 | 结果 |
|---|---|
| `--online-longpress-test --bvid BV1BMnv6jE5S --cid 42915922022` | **54/0**（ui_gap max 46ms、close 19ms、boundary_stops 2、retargets 0） |
| `--online-drag-parity-test … --drag-seconds 1 --drag-samples 60 --m1-throttle-bps 262144` | **61/0 pass**（cold_first 46ms、forward 56.10 / backward 57.20fps、gap 44/32ms、presented_diff 0.017） |
| `--online-drag-preview-test --bvid … --cid …`（T8.6.1b） | **24/0 pass**（ui_gap max 53ms，over_200=0；本轮未复现 3B 的 204–239ms 既有失败） |

> 3C 没改在线路径；这三条用于确认检查器改动没有把在线会话/拖动拖慢（数字与 3B 基线同量级）。
> 注意 3B 交接 §10-5 记录的 `forward_ui_gap_le_200ms` 本机既有偏高在本轮三次运行未复现——按"本轮实测"写，不据此改 3B 的残余结论。


### 4.2 M4（画面占比）与每栏高度（新壳实测，未改动）

| 场景 | 客户区 | 档位 | 顶栏 | Transport | 时间轴 | 状态栏 | 画面 | 占比 | 门槛 |
|---|---|---|---|---|---|---|---|---|---|
| 100% | 1280×720 | regular | 38 | 46 | 132 | 24 | 462 | **64.17%** | ≥62% |
| 125% | 1280×720 | regular | 38 | 46 | 132 | 24 | 462 | **64.17%** | ≥62% |
| 150% | 1248×624 | compact | 34 | 40 | 112 | 22 | 402 | **64.42%** | ≥62% |
| 200% | 928×452 | dense | 30 | 36 | 96 | 20 | 256 | **56.64%** | ≥55% |
| 窄窗口 | 820×500 | dense | 30 | 36 | 96 | 20 | 304 | **60.80%** | ≥59% |

与 3B 完全一致（M4 一个数字没动）；`LayoutProblemsForTest()` 的"每栏 = 令牌 ±2"逐项通过。
检查器不参与纵向预算（宽度方向），所以 M4 与 3A/3B 相同是预期结果。

### 4.3 检查器几何（`--dpi-test` 的 `dpi_inspector` 证据）

| 场景 | 折叠 | 展开默认 | 极值宽 | 极值窄 | tab 页 |
|---|---|---|---|---|---|
| 100% / 125% / 150% | **44** | 304 | **560**（±2） | **220**（±2） | marks / shots(page1) / info(page2) |
| 200%（928 宽，开盘自动折叠） | **44** | （展开后 486） | **486**（= 画面列保持 420 的夹取值） | **220** | 同上 |

- 每组极值都跑 `LayoutProblemsForTest()`：`problems` 为空（`inspector-width` 分支同时覆盖 44±1 与 220–560）。
- 截图页懒建证据：59 张历史图时 `tiles=59 grid=550x5260 viewport=598`（容器自己声明两列最小高度，
  `QScrollArea` 才滚而不是压行）；清空历史后 5 张时 `tiles=5`；空态显示"还没有截图（S）"且不铺空网格。
- 信息页 11 行：文件/格式/分辨率/视频/帧率/时长/总帧数/音频/旋转/可变帧率/路径（`info=11`）。

### 4.4 3C 新增用例（逐条）

| 用例 | 断言 | 实测 |
|---|---|---|
| `--mark-test` ① | 折叠后 `MarkStateForTest()` 完全不变（count/selected/frame）且 `InspectorCollapsedForTest()=1` | `collapse-keeps-marks ok=1`、`collapse-inspector ok=1` |
| `--mark-test` ② | 折叠态 `FocusNoteEditorForTest()` 返回 true，且自动展开、tab 回到 `marks` | `focus-note-auto-expand ok=1` |
| `--dpi-test` 折叠 | 44±1、rail=1、布局空 | `inspector-collapsed-{layout,state,shot} ok=1` |
| `--dpi-test` 极值 | 560/220（或按可用宽夹取）±2、布局空 | `inspector-wide-* ok=1`、`inspector-narrow-* ok=1` |
| `--dpi-test` tab | 截图页 page1 / 信息页 page2 + `info>0`，切回标记 | `inspector-shots-* ok=1`、`inspector-info-* ok=1` |

> 备注：`--snapshot-test` 首次全量跑出现 `three-files ok=0`（该检查要求截图目录恰好 3 个 PNG）。
> 原因是 `media/snapshots/` 里有前几轮遗留的 64 个 PNG（测试资产目录，未被 git 跟踪）。
> 清空该目录后 **35/0** 通过；这是测试隔离问题，不是 3C 回归（已写进 §10-4）。

---

## 5. M5：17 条键位可发现性（仍 17/17）

3B 的 17 条全部不变；`--readout-edit-test` 运行期 tooltip 表新增了检查器入口，逐条证据：

| # | 操作 | 键 | 可见入口 | 工具 tip | 结果 |
|---|---|---|---|---|---|
| 13 | 添加标记 | Ctrl+M | `v2MarkButton` **+** `v2InspectorAdd`（第二入口） | 两者都是 `在当前帧添加标记（Ctrl+M）` | ✅ |

`v2InspectorAdd` 通过 `Glyph::BindToolAction` 绑既有 `mark_action_`（enabled 单一真源），
`v2TabMarks/v2TabShots/v2TabInfo` 的 tooltip 是页说明（不含键位，不算 M5 条目）；
`v2InspectorExpand`=`展开检查器`、`v2InspectorCollapse`=`折叠检查器`、`v2InspectorToggle`（Transport）保持不变。
**17/17 = 100%**，`--key-test 77/0` 证明没有新快捷键/焦点回归。

---

## 6. M7：帧号常驻（未动）

`--readout-edit-test` 四态：`rel=85,13 32x20`（暂停/播放/拖动/全屏完全一致），
`readout_summary fails=0 tier=full w=958 need=736 ... split=tl=132 ins=304`。
检查器宽度变化不影响 Transport 的读数居中（M7 只看 Transport 内部几何）。

---

## 7. 视觉自检（原型 `docs/ui-v2-proto/plan.html` vs 产品截图）

- 原型渲图：`build/ui-v2-shots/proto-paused.png`（2026-10-26 重渲，§2 命令；1280×900）。
- 产品：`build/t6.5-banner-v2-baseline.png`（1280×720）、`build/t6.4-dpi-3c3-v2-1-inspector-{wide,shots,info,narrow}.png`、
  `build/t5.2-marks-ui.png`（100 标记）。
- **检查器对照图**：`build/ui-v2-shots/3c-vs-proto-inspector.png`（上=原型，下=产品 1280×720；
  构建产物，不入库；§2 的渲图 + 截图裁剪即可复现）。

| 区域 | 已对齐 | 仍缺（后续/收口） |
|---|---|---|
| 检查器头 | 26px、三 tab（图标+文字+计数徽标：`标记 100 / 截图 5 / 信息`）、active = 白字 + 顶部 2px accent + 右侧 1px 分隔、右侧 `+` 与 `>`、底边线 | —（220px 时自动收图标，属有意降级） |
| 标记页 | 列表卡片（帧号 accent 等宽左对齐、标签/备注三列、行分隔、选中 accent 底）、编辑区（标签输入 + 备注框 + 提示）、页脚存储位置 | 原型有**行内 hover 删除按钮**与**选中行左侧 2px accent 竖条**；现在仍是 MarkPanel 的右键/Delete 删除、选中只有底色（保留既有对象/测试，未改行为） |
| 截图页 | 两列缩略图卡片（1px 边框 + 3px 圆角、16:9）、`f<帧号>` + `HH:MM:SS:FF` 说明、滚动、空态"还没有截图（S）"、>60 截断说明 | 原型 caption 是"第 N 帧"（本阶段按任务要求显示 `f<帧号>`）；无行内删除 |
| 信息页 | 两列 `kvs` 风格、标签 `text-meta`、值等宽、行间虚线、11 行、可滚动 | 原型无"文件/路径"之外的顺序差异；本实现把"文件"放第一行（原型也是），路径在最后 |
| 折叠 rail | 44px、展开按钮 + 竖排"标记 N / 截图 M" | — |
| 页脚 | 文件夹图标 + `存储：sidecar · …（未写入）`、text-meta、回退时 warn 色 | 原型在截图页有"截图目录"独立页脚；本实现按规划 §2.2 收敛为**唯一常显页脚**（有意偏差，见 §8-2） |

---

## 8. 与规划的偏差（逐条，都可回退）

| # | 规划/任务 | 实际 | 理由 |
|---|---|---|---|
| 1 | §7.7 的 objectName 清单没有 `+`/展开/空态 | 追加 `v2InspectorAdd`、`v2InspectorExpand`、`v2ShotEmpty`、`v2ShotNote` 并写进 §7.7 | `+` 是 M5 第 13 行的第二入口、展开按钮是折叠态唯一出口、后两个是空态/截断断言的可寻址对象；都是"稳定可寻址"需要 |
| 2 | 原型截图页有独立 `.ins-foot`（截图目录），标记页另有存储页脚 | 收敛为**一个** `v2InspectorFooter`（所有 tab 常显） | 规划 §2.2："存储位置唯一出现处"；任务也要求页脚是"唯一常显处" |
| 3 | 任务写"折叠 = `mark_panel_->setVisible(false)`" | 折叠 = rail 显示 + `body_` 隐藏 + 显式 `mark_panel_->setVisible(false)`；展开反向 | 语义完全一致（只 hide），多一步显式调用是为了满足字面契约与 `isVisible` 断言 |
| 4 | 原型 caption "第 N 帧" | 网格 caption `f<帧号>`（任务要求） | 任务原文"每格显示缩略图 + `f<帧号>`" |
| 5 | 截图网格"首屏 ≤60 张" | 最多铺 60 张（按帧号取最近 60），徽标显示真实总数，底部 `v2ShotNote` 说明 | 控制首屏成本；不引入分页/线程 |
| 6 | 200% DPI 展开极值 560 | 实际 486（夹取到"画面列 ≥420"） | 与 `PlayerShell::ApplySplitSizes` 的既有夹取一致；用例按可用宽计算期望值并打印 |
| 7 | 规划没写 tab 在 220px 怎么放 | <300px 自动收图标/内边距（`SetCompact`） | 220–560 的极值要求；不改令牌 |
| 8 | 规划 §7.6 "对话框/菜单点击时建" | 信息表/截图网格都在构造时建（数据在打开后才填） | 它们是常驻区域不是对话框；信息表 11 行只在有媒体时填，空态只有占位行 |

---

## 9. 复现命令

```powershell
cd 'E:\拉片软件开发'
$env:PATH = 'E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;' + $env:PATH
$env:TMP = 'E:\拉片软件开发\.deps\tmp'; $env:TEMP = $env:TMP
$cmake = 'E:\拉片软件开发\third_party\cmake\3.31.6\cmake\data\bin\cmake.exe'

# 构建（**本轮改了 src/CMakeLists.txt**：本机重配会崩，见 3B §10-1；做法是
# 把 build\release 改名成 build\release-3b 后全新 configure，见 §10-1）
& $cmake --build build\release --config Release
& 'E:\拉片软件开发\third_party\cmake\3.31.6\cmake\data\bin\ctest.exe' --test-dir build\release -C Release

# 单跑（新壳）
$env:LAPIAN_UI_V2 = '1'; $env:QT_SCALE_FACTOR = '1'
$exe = '.\build\release\bin\lapian.exe'; $sp = 'build\ui-v2-gates\settings-3c-v2'
& $exe --dpi-test --file media\测试.mp4 --dpi-tag 3c-v2-1 --dpi-expect-scale 1   # 含 3C 检查器用例
& $exe --mark-test --file media\测试.mp4 --settings-path $sp                     # 含 3C 两条新用例
& $exe --readout-edit-test --file media\测试.mp4 --settings-path $sp             # M7 + tooltip 全表（含 v2InspectorAdd）
& $exe --snapshot-test --file media\测试.mp4 --settings-path $sp --snapshot-pictures build\ui-v2-gates\pics-3c-v2
```

- **注意**：`--snapshot-test` 前要清 `media\snapshots`（该检查要求目录里恰好 3 个 PNG；历史遗留会误报 `three-files`）。
- 旧壳：不设 `LAPIAN_UI_V2` 跑同一组即可（3C 检查器用例自动跳过）。
- `--dpi-test` 的 tag 参数是 **`--dpi-tag`**（3B 的脚本写成了 `--tag`，导致 3B 截图名是 `x`；本轮已改）。
- 在线（Medium 副本，工作区 exe 是 Low integrity，见 AGENTS §9）：
  `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`
  - `--online-longpress-test --bvid BV1BMnv6jE5S --cid 42915922022` → **54/0**
  - `--online-drag-parity-test --bvid … --cid … --drag-seconds 1 --drag-samples 60 --m1-throttle-bps 262144` → **61/0**
  - `--online-drag-preview-test --bvid … --cid …`（T8.6.1b 增长会话相位）→ 见 §4.5/§10-5

---

## 10. 残余与风险（按优先级）

1. **本机 CMake 重配仍会崩（`0xC0000409`）**。本轮因为加 `InspectorPanel.{h,cpp}` 必须改
   `src/CMakeLists.txt`：处理方式同 3B——`build\release` 改名 `build\release-3b` 后全新 configure
   （`-G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=<Qt> -DLAPIAN_BUILD_TESTS=ON`）。
   `build\release-3b` 保留 3B 二进制做 A/B；**不要**在旧目录里反复重试。
2. **标记列表仍是 MarkPanel 的既有观感**：嵌入模式下帧号已改成原型那种 accent 等宽左对齐，MarkStore
   自动生成的"帧 N"标签不再重复到标签列；但行内没有 hover 删除按钮、选中行没有左侧 accent 竖条。
   这是"原对象保留 + 既有单测断言"的直接代价；若要精修，需要新开一条"MarkPanel 行渲染"任务并同步
   `tests/mark_panel_test.cpp` 的口径。
3. **截图网格 >60 张只显示最近 60**（徽标是真实总数，底部有说明）。没有分页/虚拟列表；
   若要"滚动加载更多"，需要重新评估"不新增定时器/线程"的约束。
4. **`--snapshot-test` 依赖干净的 `media/snapshots`**（见 §4.4 备注）。这是 T5.4 既有测试把
   "目录里恰好 3 个文件"写死，与本阶段无关；门禁脚本应先清目录。
5. **`--online-drag-preview-test`（T8.6.1b）** 的 `forward_ui_gap_le_200ms` 在本机 3A.5 起就
   204–239ms（既有环境基线，3B 已记录）；本轮只记录实际结果，不改阈值、不写"通过"。
6. **在线截图网格的"历史扫描"只在打开/切到截图页/截图保存后发生**：运行中由其它进程往
   `index.jsonl` 追加不会自动出现；再切一次截图 tab 即可（无轮询是 3B 的既定方向）。
7. **页面切换时 `QStackedWidget` 会重建/回收截图 tile**（≤60 个 widget），切页有 ~几 ms 的抖动；
   未做 widget 复用池（避免过早优化）。
8. **`v2InspectorPanel` 等 5 个内部 objectName 已删除**（只保留 §7.7 + 8-1 的 4 个追加），
   免得清单膨胀；需要定位页面/滚动区时用 `v2InspectorStack` 的子控件层级。
9. **`--dpi-test` 的 `dpi_state panel=`** 在折叠态显示的是 MarkPanel 的旧几何（100x30，因为被 hide）；
   诊断键语义未变，但没有断言依赖它（`LayoutProblemsV2` 用 host 宽）。

---

## 11. 3D 入口（已经留好的缝）

- **检查器**：`InspectorPanel::SetTab/SetCollapsed/SetShotItems/SetInfoRows/SetStorageText` 都是窄接口；
  3D 若加"在线截图目录/清理截图/行内删除"，在 InspectorPanel 外面接（不要让它读 `OnlineSession`/`CacheStore`）。
- **存储页脚**：`UpdateMarkStorageUi()` 是唯一推送点（`StorageSummary()` + `using_fallback()`）；
  3D 删 InfoBar 时把 `StorageSummary` 的来源换成新状态源即可，页脚对象不用动。
- **截图网格**：`RefreshInspectorShots()` 是唯一扫描入口；`ParseSnapshotFrameFromName` 是纯函数，
  3D 若要支持"按 cid/分P 分目录"只需换目录拼接与前缀。
- **测试缝**：`InspectorStateForTest()`（一行含 collapsed/tab/page/计数/tiles/store）、
  `SetInspectorCollapsedForTest()`、`SetInspectorTabForTest()`；`--dpi-test` 的 3C 用例可直接扩
  （每个相位都已留 `build/t6.4-dpi-<tag>-inspector-*.png`）。
- **`--banner-test` 的 `loop=` 仍指向状态栏 `loop_label_`**（3D 才改指 Transport 循环键，见规划 A.3）；
  本阶段没有动它。
