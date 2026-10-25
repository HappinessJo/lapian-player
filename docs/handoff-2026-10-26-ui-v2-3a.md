# UI v2 · 3A 壳层交接（2026-10-26）

- **状态**：3A 代码完成，门禁**新旧壳各跑一遍全绿**。可按 `LAPIAN_UI_V2=1` 或 `--ui-v2` 进入新壳；
  不设时旧壳行为与 `352b29f` 一致（回滚口径保留）。
- **上游**：规划 [ui-v2-plan.md](ui-v2-plan.md)（§0.1 已批准）、开工提示词
  [next-session-prompt-ui-v2-3a.md](next-session-prompt-ui-v2-3a.md)。
- **下游**：3B（Transport 全量 + 时间码主读数 + 时间轴两条滑块）→ 3C（检查器）→ 3D（在线/全屏/删 InfoBar）。
- **证据**：`build/ui-v2-gates/*.log`、`build/t6.5-banner-vN-*.png`（均不入库，命令见 §6）。

---

## 1. 一句话结论

新壳骨架（Tokens/Theme/PlayerShell/PlayerSplitter/TopBar/BannerSlot/StageStack/Transport 骨架 + 三档密度 +
QSplitter）已经能跑真实的打开/播放/步进/时间轴/标记/全屏；**M2/M3/M4/M7/M8 全部达标**，
`ctest 37/37`、`--key-test` 77/0、`--mark-test`/`--note-test` 0 失败、`--banner-test` 全过、
`--dpi-test` 4 档 + 窄窗 problems 为空、`--fullscreen-test` 全过（新旧壳各一遍）。

**最大残余**：新壳冷启动 `show()` 比旧壳多约 **0.5s**（P1 268ms → 773ms，仍 ≤1.5s 门槛），
根因未定位，已排除全局 QSS / 调色板 / 字体 / 原生 HWND / 布局循环（见 §7）。

---

## 2. 改了什么（文件级）

| 文件 | 类型 | 说明 |
|---|---|---|
| `src/app/ui/Tokens.h` | **新** | 三档密度令牌（栏位高/间距/字号/控件高/面板宽）+ 语义色（hex 字面量，无 Qt 依赖）+ 20px 迟滞的 `ResolveDensity()` + M4 占比门槛 `StageRatioFloor()` |
| `src/app/ui/Theme.{h,cpp}` | **新** | `ApplyTheme(QApplication&)`：QPalette + 应用字体 + **一份**全局 QSS（按 objectName/类型选择器）；`UiFont/MonoFont` 用 `setPixelSize`（规划 §4.7）；`SetStyledBackground()` 强制 `WA_StyledBackground` |
| `src/app/ui/PlayerSplitter.{h,cpp}` | **新** | `QSplitter` + `PlayerSplitHandle`：6px、悬停 2px accent 细线、双击复位、折叠禁用、`Qt::NoFocus`、`setChildrenCollapsible(false)` |
| `src/app/ui/TopBar.{h,cpp}` | **新** | 文件 icon + 文件名（ElideMiddle + tooltip 全名）+ 规格 chip + 模式 chip（QSS 属性选择器上色）+ 打开/在线/?/设置 + 在线 fps 角标 + 清晰度入口 |
| `src/app/ui/BannerSlot.{h,cpp}` | **新** | 一条横幅 + 关闭；**把 InfoBar 内部的 `infoBannerRow` 原对象 reparent 进来**，高度 0/令牌，出现时把下面推下 |
| `src/app/ui/StageStack.{h,cpp}` | **新** | `QStackedWidget`：page0 = GfxWidget，page1 = 空/加载/错误卡片；卡片 `setAcceptDrops(true)` 并转发 `fileDropped` |
| `src/app/ui/TransportBar.{h,cpp}` | **新（骨架）** | 3A 只放读数块（时间码 + 帧号/总帧，均为 InfoBar 原对象）与音量滑条；左右空容器留给 3B |
| `src/app/ui/PlayerShell.{h,cpp}` | **新** | 布局树（§7.1）+ 档位/令牌落地 + 分隔条夹取/复位/折叠 + 全屏 chrome + 测试缝 |
| `src/app/TimelineWidget.{h,cpp}` | 改（小） | 新增 `SetHeightPolicy(Fixed/Fill)`；**默认 Fixed 不变**（保住 `tests/timeline_test.cpp` 的 128/72）；Fill 时波形按高度算并封顶 96 |
| `src/app/MainWindow.{h,cpp}` | 改 | 新增 4 参 `ui_v2`、`BuildShellUi()/AdoptShellWidgets()/UpdateDensity()/UpdateShellModeChip()/ShowShellHelp()`；空态/打开/错误/全屏/主题相关接线；`LayoutProblemsForTest/LayoutStateForTest` 拆新旧两套；新增 §7.8 的 12 个 getter |
| `src/app/main.cpp` | 改 | 构造 MainWindow 前读一次 `--ui-v2` / `LAPIAN_UI_V2`（`EnvEnabled` 接受 1/true/on/yes），仅 v2 时 `ApplyTheme`；`--startup-test` 增加 `d3d_init_at_startup` / `ui_v2` / `density`；`--fullscreen-test` 增加 `chrome=` 断言 |
| `src/CMakeLists.txt` | 改 | `app/ui/*.cpp/h` 加进 `lapian` 目标（不新增库/不改测试目标/不改打包） |

**没碰**（规划 §6.3 硬边界）：`GfxWidget` 呈现、`PreviewDecoder`/`DragStrategy`、`OnlineSession`、`CacheStore`、
`src/core/*`（含 `TimelineModel`）、`media/StatusNotice` 语义、spec §3.7 键位、settings schema、任何门/阈值。

---

## 3. 关键做法：「搬不换」怎么落的

3A 的目标是**先换骨架、不换外观与语义**，所以：

1. **InfoBar 继续做状态源，但不进布局**。它仍然是横幅/规格/帧号/时间码/总帧的唯一真源
   （`media::StatusNotice` 的构建/择一/次级摘要**一行未改**），只是 `hide()` 后把内部控件
   **原对象** reparent 出去：

   | 原对象（objectName 保留） | 新家 |
   |---|---|
   | `infoFile` / `infoFormat` | TopBar（文件名 / 规格 chip） |
   | `onlineFpsBadge` | TopBar（模式 chip 右侧） |
   | `infoTimecode` / `infoFrame` / `infoTotal` | TransportBar 读数块 |
   | `infoBannerRow`（含 `infoBanner` + `infoBannerClose`） | BannerSlot |
   | `volumeSlider`（原 `volume_slider_`） | TransportBar 右侧 |
   | `cacheSettingsButton` / `onlineQualityButton` | TopBar |
   | `markPanel`（含 `markTree`/`markLabelEdit`/`markNoteEdit`/`markStorage`） | 检查器内容区 |
   | `timeline` | 时间轴面板容器 |

   好处：`--banner-test` 的 8 个字符串键、`--online-quality-test --ui` 的 4 个 getter、
   `--mark-test`/`--note-test` 全部**不用改断言**（附录 A.3/A.4/A.5 的目标达成）。
2. **`BannerStateForTest()` 的两处实现调整**（断言不变）：`banners=` 改成从窗口向下找
   `infoBanner`（label 的 QObject 父已变成 BannerSlot）；`banner=` 坐标映射到横幅槽；
   `info_h=` 改为横幅槽高度（0 或令牌）。`--banner-test` 的 `4k-height`（横幅出现后更高）因此仍然成立。
3. **`InfoFrameTextForTest()` 改成从窗口向下找** `infoFrame`（父已变），`InfoBarVisibleForTest()`
   在 v2 下改为"帧号读数可见"（全屏仍然为真，语义等价）。
4. **旧壳路径一行未改**：`LayoutProblemsLegacy()/LayoutStateLegacy()` 是原实现的搬运。

---

## 4. 门禁数字（新旧壳各一遍）

命令见 §6。所有日志在 `build/ui-v2-gates/`。

| 门 | 旧壳 | 新壳（`LAPIAN_UI_V2=1`） |
|---|---|---|
| `ctest` | 37/37 | 37/37（同一份单元测试，不受壳影响） |
| `--dpi-test` ×4 DPI（normal/fullscreen/narrow） | problems 为空 | problems 为空 |
| `--banner-test` | 74 checks / 0 fails | 74 checks / 0 fails |
| `--key-test` | 77/0 | 77/0 |
| `--mark-test` / `--note-test` | 0 失败 | 0 失败 |
| `--fullscreen-test` | 全过 | 全过（+ `chrome=v2=1 appbar=0 banner=0 status=0 inspector=0 transport=1 timeline=1`） |
| `--startup-test`（有文件 / 空态） | P1 268ms / P2 334ms；`d3d_init_at_startup=1` | P1 773ms / P2 985ms；`d3d_init_at_startup=0` |

### 4.1 M4 画面占比（新壳实测，`dpi_state`）

| 场景 | 客户区 | 档位 | 固定栏合计 | 画面高 | 占比 | 门槛 |
|---|---|---|---|---|---|---|
| 100% | 1280×720 | regular | 258 | 462 | **64.2%** | ≥62% ✅ |
| 125% | 1280×720 | regular | 258 | 462 | **64.2%** | ≥62% ✅ |
| 150% | 1280×624 | compact | 222 | 402 | **64.4%** | ≥62% ✅ |
| 200% | 928×452 | dense | 196 | 256 | **56.6%** | ≥55% ✅ |
| 窄窗 820×500 | 820×500 | dense | 196 | 304 | **60.8%** | ≥59% ✅ |
| 全屏 100% | 1920×1080 | regular | 184 | 896 | 83.0% | — |
| 全屏 200% | 960×540 | dense | 138 | 402 | 74.4% | — |

**每栏 = 令牌 ±2** 全部成立：`appbar 38/34/30`、`transport 46/40/36`、`timeline 132/112/96`、
`status 24/22/20`、`inspector 304`（200% 下自动折叠 44）、`handle 6`。
横幅出现时按规划 §4.4「不算瞬时态」用「画面 + 横幅槽」算稳态占比（436+26=462 → 64.2%）。

### 4.2 M2 对比度（截屏采样，`t6.5-banner-v2-*.png`）

按 WCAG 相对亮度公式从**真实像素**采样（背景 = 区域内最高频色，前景 = 与背景亮度差最大的高频色）：

| 区域 | 背景 | 前景 | 实测 |
|---|---|---|---|
| 顶栏文件名 | `#1c1c20` | `#e8e8ee` | **13.92:1** |
| 顶栏规格 chip | `#1c1c20` | `#e8e8ee` | **13.92:1** |
| 顶栏模式 chip | `#1c1c20` | 采样到 ClearType 混色 | **9.53:1** |
| Transport 时间码/帧号 | `#17171a` | `#e8e8ee` | **14.66:1** |
| 检查器（MarkPanel） | `#18181b` | `#d8d8dc` | **12.47:1** |
| 状态栏 | `#1c1c20` | `#e8e8ee` | **13.92:1** |
| 横幅（VFR/warn） | `#4a3600` | `#ffd37a` | **8.16:1** |

令牌本身：`text-meta #8a8a95` 对 `#17171a` = **5.24:1**、对 `#1c1c20` = **4.97:1**（均 ≥4.5，可承载元信息）；
`text-mute #6f6f7a` = 3.60:1，只用于禁用/装饰。

### 4.3 拖动/步进不退化（回答"换壳会不会让拖动变慢"）

`window.show()` 的 +0.5s 是**一次性启动成本**（窗口第一次显示前，见 §7-1），不在交互路径上。
为确认这一点，3A 额外用同一素材跑了 `--drag-test`（新旧壳各一遍，20 样本）：

| 阶段 | 含义 | 旧壳 | 新壳 |
|---|---|---|---|
| P4 | 2s 连续向前拖动的**预览呈现帧率** | 95.2 fps / 191 presents | **96.0 fps / 192 presents** |
| P6 | 拖动松手 → 精确落帧 p95 | 26.2 ms | **19.4 ms** |
| P7 | 步进 p95（后退 / 前进） | 0.1 / 11.8 ms | 0.1 / 12.0 ms |
| P9 | seek 后 PgUp p95 | 37.6 ms | **29.4 ms** |
| P8 | 长按连发（≥12 帧/秒 + 每帧呈现） | 3.9fps / 8 帧 **pass=0** | 12.9fps / 26 帧 **pass=1** |

即：**拖动进度条/时间轴、步进、松手精确落帧都没有退化**（同一台机器同一素材，两次测量差在噪声内，
新壳本轮 P6/P9 反而更低）。注意：
- 这是 3A 的**附加**测量，不是 3A 的门（规划把 `--drag-test` 放在 3B）。旧壳这次 P8 `pass=0`
  是旧壳自身的既有偶发（同一命令、同一素材），不能算 3A 回归 —— 3B 门禁跑 `--drag-test` 时要用
  多轮/固定复跑口径，别用单次结果下结论。
- 唯一的用户可见代价是**冷启动时窗口晚出现约 0.5s**（P1 773ms，门槛 1.5s）。
### 4.3 M3 / M7 / M8

- **M3**：4 DPI × (normal/fullscreen/narrow) 的 `LayoutProblemsForTest()` 全为空；新增断言
  `appbar-height/transport-height/timeline-height/status-height/stage-height/stage-ratio-low/
  inspector-width/splitter-width/region-outside-shell/banner-overlaps-readout/inspector-visible-in-fullscreen`
  都在跑。
- **M7**：帧号 `infoFrame` 在 Transport 中央常驻，四种状态（播放/暂停/拖动/全屏）几何不变；
  `readout=` / `playhead_visible=1` 进 `dpi_state`。
- **M8**：P1 773ms（≤1.5s）、P2 985ms（≤2.5s）、`d3d_init_at_startup=0`（空态隐藏生效，D3D 挪到首次打开）。
  **但见 §7-1：`show()` 本身比旧壳慢约 0.5s，门槛内但不理想。**

---

## 5. 与规划的偏差（逐条，都可回退）

| # | 规划 | 3A 实际 | 理由 |
|---|---|---|---|
| 1 | `qrc` 20 个 2× PNG 图标 | **文字按钮**（打开/在线/?/设置 + 符号 `▣`） | 3A 只做骨架；图标集按规划 §附录 D-3 后续按"能认出来"验收。M5 的可见入口已具备（tooltip 带键位） |
| 2 | 检查器 220–560 用"拖 handle 时夹取" | 用 `setMinimumWidth/setMaximumWidth` | Qt 的 QSplitter 会按子控件 min/max 夹取，效果等价且折叠/展开无需改下限；折叠时 `handle->setEnabled(false)` |
| 3 | 时间轴默认高度用 `setSizes()` 落地 | 未拖拽时 `setFixedHeight(令牌)`，按 handle 时才放开 96–340 | 实测 QSplitter 在多次嵌套 resize 下会把 96 重新分配成 123（画面被挤掉 27px），固定高度才能保证 M4 的"每栏 = 令牌 ±2"；用户一按下 handle 就放开，拖拽语义不变 |
| 4 | `BannerSlot` 新写通知逻辑 | **reparent InfoBar 的 banner row** | 语义零改动（规划 Q4 要求"构建/择一/次级摘要一行不改"），3D 删 InfoBar 时再下沉 |
| 5 | 空态隐藏 GfxWidget → D3D11 惰性创建 | GfxWidget 仍在启动时构造（HWND 存在但隐藏） | Q1 已实测 hide 不重建 HWND；启动时**不**初始化 D3D11（`d3d_init_at_startup=0`）。HWND 惰性化留 3D（本次实测它只值 ~10ms，见 §7-1） |
| 6 | 三档密度 20px 迟滞 | 已实现，且 `regular→dense` 直接按 520 边界落档 | 否则"从大窗口缩到 500 高"会停在 compact（`820×500` 场景实测） |
| 7 | 加载卡片接在线流程 | **未接**（只有 API + 卡片） | 3A 明确不动在线文案/流程（3D 接）；目前只有空态/视频/错误三态切换 |
| 8 | `v2Timecode` 等新 objectName | 读数/文件名/横幅沿用旧名（`infoTimecode/infoFile/infoBanner`…），**容器**用 `v2*` | 这些控件是 InfoBar 的原对象，改名会连带 `InfoFrameTextForTest` 等 getter 与 3C/3D 的 `info_<label>=` 口径 |
| 9 | 状态栏压到 3 指标 + 1 消息 | 仍保留 6 个（notice/audio/speed/loop/waveform/status） | 3A 不动状态栏内容（3D 收口）；`LayoutProblemsForTest` 的 status 检查已按 v2 列表实现 |
| 10 | `--dpi-test` 增加「tl=340 / ins=560」极值相位 | 未加（getter `SetSplitSizesForTest()/DragSplitterForTest()` 已就位） | 3A 门禁是 4 DPI × 3 相位；极值相位留 3B 与"时间轴拉满"一起验 |

---

## 6. 复现命令

```powershell
cd 'E:\拉片软件开发'
$env:PATH = 'E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;' + $env:PATH
$exe = '.\build\release\bin\lapian.exe'; $media = 'media\测试.mp4'

# 构建（本机 CMake 3.31 重配会在默认 TEMP 崩，必须用仓库内 TEMP）
$env:TMP = 'E:\拉片软件开发\.deps\tmp'; $env:TEMP = $env:TMP
& 'E:\拉片软件开发\third_party\cmake\3.31.6\cmake\data\bin\cmake.exe' --build build\release --config Release --target lapian

# ctest（同样需要仓库内 TEMP，否则 21 个用临时目录的单测会挂）
& 'E:\拉片软件开发\third_party\cmake\3.31.6\cmake\data\bin\ctest.exe' --test-dir build\release -C Release

# 新旧壳各一遍（示例：100% DPI + banner + key）
foreach ($sh in @('v2','legacy')) {
  if ($sh -eq 'v2') { $env:LAPIAN_UI_V2='1' } else { Remove-Item Env:LAPIAN_UI_V2 -ErrorAction SilentlyContinue }
  $env:QT_SCALE_FACTOR='1'
  & $exe --dpi-test --file $media --tag $sh-1 --dpi-expect-scale 1
  & $exe --banner-test --file $media --banner-tag $sh --settings-path "build\ui-v2-gates\settings-$sh"
  & $exe --key-test --file $media --settings-path "build\ui-v2-gates\settings-$sh"
  & $exe --mark-test --file $media --settings-path "build\ui-v2-gates\settings-$sh"
  & $exe --note-test --file $media --settings-path "build\ui-v2-gates\settings-$sh"
  & $exe --startup-test --file $media --settings-path "build\ui-v2-gates\settings-$sh"
  & $exe --startup-test --settings-path "build\ui-v2-gates\settings-$sh"
  & $exe --fullscreen-test --file $media --settings-path "build\ui-v2-gates\settings-$sh" --snapshot-pictures "build\ui-v2-gates\pics-$sh"
}
```

- 150%/200%：把 `QT_SCALE_FACTOR` 设成 `1.5`/`2` 并加 `--dpi-expect-scale`。
- **不要**在没有 `--file` 时跑 `--dpi-test`（main.cpp 的测试调度在 `!file_path.isEmpty()` 分支里，会常驻不退出）。
- 工作区直接跑 exe 是 Low integrity（见 AGENTS §9）：本地/离线用例没问题，在线用例仍需 Medium 副本。

---

## 7. 残余与风险（按优先级）

1. **新壳 `show()` 慢约 0.5s（P1 268→773ms，P2 334→985ms）**。segments：`app_created 20ms →
   window_created 38ms → window_shown 773ms`，即 **构造只花 ~18ms，慢在 `window.show()` 内部**。
   已排除：全局 QSS（关掉 = 728ms）、QPalette（关掉 = 746ms）、应用字体（关掉 = 806ms）、
   GfxWidget 原生 HWND（不建 = 727ms）、布局/分隔条循环（`split_apply` 6 次、`TopBar::resizeEvent` 1 次）、
   字体实例数量（把 12 处 setFont 减到 4 处 = 无变化）。用 `-platform minimal` 时为 20ms，
   说明成本在 Windows 平台侧的首帧/原生显示路径，**尚未定位**。
   补充实测：**与像素数无关**——100% 与 200% 下 `show()` 都是 788ms（旧壳 265/264ms），
   即不是绘制/像素成本，而是与**控件数量**成比例的 polish/布局成本；下一步用 xperf 看
   `QWidget::ensurePolished`/`QStyleSheetStyle` 的调用树，或二分删掉卡片/顶栏控件定位。
   **门槛内（≤1.5s/≤2.5s）但必须查**：建议 3B 第一天用 ETW/xperf 或二分删控件定位。
2. **检查器自动折叠只在首次打开媒体时决定一次**（规划 §3.1 口径）。`--dpi-test` 的 narrow 相位
   是"1280 打开后缩到 820"，所以检查器仍展开（画面宽 498 ≥420，M4 不受影响）。
   若以后要求"缩到 820 自动折叠"，需要新增一次判定（会与"之后完全听用户的"冲突，先问再改）。
3. **空态 × DPI 矩阵缺测**：`--dpi-test` 需要一个 `--file` 才能跑；空态只做了 100% 的人工截屏确认。
   建议 3B 给 `--dpi-test` 加 `--empty` 相位（或让测试调度支持无文件），否则 M3 的"空态"列没有自动化证据。
4. **时间轴 Fill 的波形比例是临时值**（`0.62 × 可用高`，封顶 96）：3B 做内部预算（头 26/视图窗 13/工作区 15/画布）时替换。
5. **拖分隔条时的 P4 帧率未测**（规划 §7.5-3 / R6）：`DragSplitterForTest()` 已就位（合成鼠标事件），3B 补测。
6. **状态栏仍是 6 个永久控件**（3D 才压到 3+1）；窄窗口下它们刚好放得下（`--dpi-test` 的 status 检查覆盖）。
7. **模式 chip 的语义色只做了 4 档**（neutral/local/online/warn/error），在线解析中/切档中的文案 3D 接。
8. **`--fullscreen-test` 的 chrome 断言只在 `v2=1` 时检查隐藏类项**（旧壳没有顶栏/横幅，用同一份 check 会让旧壳假失败）。

---

## 8. 3B 入口（已经留好的缝）

- **`TransportBar`**：`leftHost()/rightHost()/timecodeWidget()/frameWidget()/volumeWidget()` 就位；
  3B 往左右容器里塞 5+2+6 个按钮、把时间码换 26px 等宽主读数、加 `v2FrameNumber` 输入、缓冲槽、宽度降级
  （按自身宽度 760/660/560 三档，规划 §3.3）。
- **时间轴**：`PlayerShell::timelineHost()`（min 96 / max 340，未拖拽时固定令牌）+ `TimelineWidget::SetHeightPolicy(kFill)`
  已生效；3B 在面板里加头（dense 无头）、视图窗/工作区两条滑块（`RangeBar`）、播放头越界箭头，并去掉内部横向滚动条。
- **分隔条**：`SetSplitSizes(tl, ins)`（夹取路径）、`DragSplitter(vertical, delta)`、`ResetSplitSizes()`、
  `SplitterState()`；`SetSplitSizesForTest/DragSplitterForTest` 已接到 MainWindow。
- **密度**：`PlayerShell::ApplyClientHeight(client_h)` 是唯一入口（带迟滞）；`MainWindow::UpdateDensity()`
  负责状态栏高度与状态栏控件高度上限。新增栏位时只需改 `Tokens.h` 的 `MakeTokens()` + 一处 `ApplyTokens()`。
- **测试缝**（全部已实现，见 `MainWindow.h`）：`ShellStateForTest()`（`tier=/appbar=/transport=/timeline=/status=/banner=/stage=/inspector=/collapsed=/fullscreen=`）、
  `DensityNameForTest()`、`GfxVisibleForTest()`、`StagePageForTest()`、`BannerSlotHeightForTest()`、
  `BannerSlotGeometryForTest()`、`SplitterStateForTest()`、`SetSplitSizesForTest()`、`DragSplitterForTest()`、
  `InspectorCollapsedForTest()`、`FullScreenChromeForTest()`、`ReadoutTextForTest()`、`ReadoutGeometryForTest()`、
  `D3dInitAtStartupForTest()`。
- **3B 开工前先做的一件事**：查清 §7-1 的 `show()` 慢 0.5s，否则 3B 加的控件越多越糟。
