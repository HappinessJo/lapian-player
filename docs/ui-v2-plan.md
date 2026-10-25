# 拉片软件 UI v2 规划（P1–P8）

- **状态**：规划阶段产物，**待用户批准**。本轮系列**零产品代码改动**（`src/`、`tests/`、门、阈值、schema 全部未动）。
- **产品代码基线**：`352b29f`（P0 发布收口，安装版 0.1.0）。
- **文档基线**：`6e6da0c`（`docs: hand off UI v2 to the planning stage`）。本轮新增/修改：本文件、[ui-v2-proto](ui-v2-proto/index.html)（三档密度 + 无表头变体 + 缩放下限按 spec 收紧）。
- **上游**：[交接单](handoff-2026-10-25-ui-v2-planning.md)、[定义](ui-v2-definition.md)、[四轮反馈与收口](ui-v2-feedback-2026-10-25.md)、[可点原型](ui-v2-proto/index.html)、[原型说明](ui-v2-proto/NOTES.md)。
- **下游**：批准后进 3A/3B/3C/3D（P7）。**本文件不写代码，只定规格**。
- **读法**：§0 一页结论 → §1 五个必答问题 → §4 P3 高度预算（唯一会改门槛的一节）→ §10 需要你拍板的 4 件事。其余按需查。

---

## 0. 一页结论

1. **信息架构按"一个信息一个家"重排**：文件名/规格/模式只在顶栏；时间码（主）+ 帧号/总帧（次）只在 Transport 中央；
   播放状态只在播放按钮 + 状态栏消息；循环数值只在时间轴标题；存储位置只在检查器页脚。
2. **画面区不设 26px 表头**（原型有）。理由：它只重复顶栏已有信息，却要吃掉 720 高窗口的 **3.6pp**；
   保留它会直接把 200% DPI 的画面压到 **51.8% < 55% 门槛**。这是本文件唯一一处**主动偏离原型**的骨架改动。
3. **三档密度按客户区高度取档**（不只是 DPI）：`regular ≥640` / `compact 520–639` / `dense <520`。
   没有这一条，150% DPI（624 高）会比现状**倒退 10.5pp**（69.2%→58.7%）；有了它，150% 回到 64.4%。
4. **高度预算重算，M4 门槛下调**（选项 b：保留 132 时间轴默认 + 重定门槛）：
   100/125% ≥62%、150% ≥62%、200% ≥55%、窄窗口 820×500 ≥59%；同时新增"每个栏位高度 = 令牌 ±2px"的硬断言，
   防止"门槛达标但栏位偷偷长胖"。
5. **空态隐藏 GfxWidget 可行**（Q1）：已实测 Qt 6.8.3 下 `WA_NativeWindow` 子控件 hide/show **不销毁、不重建 HWND**
   （`internalWinId` 三轮不变），交换链绑定不失效；顺带把 D3D11 初始化从 P1 挪到首次打开（P1 更快，P2 不变）。
6. **可拖版块用 QSplitter**（Q2）+ 自定义 `QSplitterHandle`（6px、悬停 accent 细线、双击复位、折叠时禁用）。
7. **时间轴两套坐标的唯一 source of truth = `TimelineModel`**（Q3，`pixels_per_frame` + `scroll`）；
   视图窗滑块与工作区条都是**投影**，只做"手势 → `SetPixelsPerFrame`/`SetScroll`"的换算，不新增第二份视图状态。
8. **InfoBar 删除**（Q4）：通知**语义**（`media::StatusNotice` 的构建/择一/次级摘要）原样保留，
   **控件**换新 `BannerSlot`；`--banner-test` 的 8 个字符串键全部保留，**只有 1 处断言需要改指向**
   （`loop=` 从"循环标签可见"改为"循环开关可见"），其余靠"先搬对象、再换外观"。
9. **`--ui-v2` 用环境变量**（Q5）：`LAPIAN_UI_V2=1`，与既有 `LAPIAN_*` 一族一致；**不新增设置项**（settings schema 不动）。
   旧壳保留到 3D，3D 收口后删除。
10. **测试可寻址性先于视觉**：§7.7 共列 97 个 `objectName`，其中 **17 个沿用旧名**
    （`markPanel`/`markTree`/`markLabelEdit`/`markNoteEdit`/`markStorage`/`timeline`/`volumeSlider`/`videoStage`/
    `infoBanner`/`infoBannerClose`/`onlineFpsBadge`/`onlineQualityButton`/`cacheSettingsButton`/`noticeInfo`/
    `audioInfo`/`waveformInfo`/`statusInfo`）**一律不改**。
11. **改测试清单是硬交付**（附录 A）：`--dpi-test` 阈值（6 项）、`--banner-test` 断言（5 处）、
    `--online-quality-test --ui`（4 个 getter 重新指向）、`--mark-test`/`--note-test`（保持不动 + 新增折叠用例）、
    以及 `tests/timeline_test.cpp` 的 128/72 高度断言（**必须靠"高度策略可切换"保住**）。
12. **回归门禁不变**：ctest 37/37、parity 61/0、T8.6.1b 35/0、key 77/0、A 42/0、local 9/0、`--drag-test` P6 ≤150ms。
    新壳每个小阶段都要**双开跑**（`LAPIAN_UI_V2=1` 与默认），3D 之后只跑新壳。
13. **工时估算 21–29 人日**（3A 4–6 / 3B 6–8 / 3C 3–4 / 3D 5–7 / 验收 3–4）。数字是估算，不是承诺。
14. **需要你拍板 4 件事**（§10）：画面区去表头、M4 新门槛、三档密度自动取档、QSplitter。

---

## 1. 五个必答问题的结论（交接单 §5）

### Q1 空态是否隐藏 `GfxWidget`？

**结论：隐藏。用 `QStackedWidget` 两页（page0 = `GfxWidget`，page1 = Qt 占位卡片），空/加载/错误态切 page1。**

**实测依据（本轮新做，非推测）**：写了一个一次性 Qt 6.8.3 探针（`build/hwnd-probe/probe.cpp`，不入库），
对一个 `WA_NativeWindow + WA_PaintOnScreen` 的子控件做 hide/show 两轮 + 缩到 1×1 + QStackedWidget 翻页，
打印 `internalWinId()` 与 `IsWindow()`：

```
initial        internalWinId=0x271b2a visible=1
after-hide     internalWinId=0x271b2a visible=0  oldStillAlive=1
after-show     internalWinId=0x271b2a visible=1  oldStillAlive=1   hwnd_same_after_cycle=1
after-hide-2   internalWinId=0x271b2a visible=0  oldStillAlive=1
after-show-2   internalWinId=0x271b2a visible=1  oldStillAlive=1   hwnd_same_cycle2=1
resized-1x1    internalWinId=0x271b2a visible=1  hwnd_same_after_resize=1
stack-page1-hidden internalWinId=0x180c7e visible=0 oldStillAlive=1
stack_hwnd_same=1
```

即：**hide 不销毁 HWND**，交换链的 `HWND` 绑定不会失效（`SwapChain::Create(device, winId())` 之后 hide/show 仍是同一个
窗口句柄）。所以"隐藏 → 占位卡片 → 再显示"不会触发交换链重建，也没有"黑屏但 Present 假成功"的路径。

**P1/P2 影响（必须按这个顺序理解）**：
- D3D11 设备/交换链是**惰性创建**的（`GfxWidget::EnsureReady()`，`GfxWidget.cpp:46-88`），第一个触发点是
  `showEvent` 或 `paintEvent`（`GfxWidget.cpp:194-207`）。空态隐藏后，启动期不再创建 D3D11 →
  **P1（窗口可交互 ≤1.5s）只会更快**。
- P2（双击关联文件 → 首帧 ≤2.5s）本来就包含 D3D11 初始化（现在它在启动时做）；挪到打开时做，
  **同一段工作仍在"进程启动 → 首帧"内**，总时长不变。风险不在延后，而在**顺序**：必须"先显示画面页，再取
  `gfx_->d3d_device()`"，否则会用一个隐藏窗口的 HWND 建交换链。
- 新增验收项：`--startup-test` 输出增加 `d3d_init_at_startup=0/1`；3A 实测 P1 与 P2 各 3 次，写进 3A 交接。

**备选（不采用）**：① 不隐藏，占位卡片放画面外（上下条）→ 视线要跨过一整块黑画面，直接违背 G1；
② 常显但把 HWND 缩到 1×1 → 探针证明可行，但会把"1×1 交换链"这种脏状态留在渲染路径里，且 `setMinimumSize(320,180)`
要先改，收益为零；③ 隐藏后每次 `showEvent` 重建交换链 → 改动落在"性能路径"上，明确不做。

**残余风险**：探针只验证了 **HWND 身份**，没有验证 D3D11 交换链 hide/show 后的首次 `ResizeBuffers`/`Present`；
3A 必须用真实素材看"空态 → 打开 → 出现画面"三次，并记录首次 `Present` 耗时（`LAPIAN_TIMING_LOG`）。

---

### Q2 可拖版块用 `QSplitter` 还是自绘？

**结论：用 `QSplitter` + 自定义 `QSplitterHandle`（`PlayerSplitter`/`PlayerSplitHandle`）。**

理由（按权重）：
1. **尺寸分配语义正好是我们想要的**：竖直 splitter 的两个子项 = `(画面+Transport)` 与 `时间轴`，
   `setStretchFactor(0,1)`、`(1,0)` 后，窗口变大时**只有画面变高**，时间轴保持用户/档位给的高度——这正是
   "画面优先、栏位不涨"。自绘要自己写这套分配 + 最小尺寸夹取 + 布局失效时机。
2. **光标、命中、拖拽捕获是现成的**：`SplitHCursor`/`SplitVCursor`（就是原型里那个双向箭头），
   拖拽期间 Qt 自己抓鼠标，不需要 `grabMouse`/`setMouseTracking`。
3. **双击复位、折叠禁用**只需要在 handle 子类里加 20 行（`mouseDoubleClickEvent` + `setEnabled(false)`），
   这两件事本来就是"handle 级别"的，不是 splitter 级别。
4. **主题**：`QSplitter::handle` 只是一个 QWidget，自定义 handle 直接 `paintEvent` 画 2px accent 细线，
   不需要 QSS 技巧；`setHandleWidth(6)` 与原型 6px 一致。

要点（写进 3A 实现约束）：
- `setChildrenCollapsible(false)`（否则拖到底会把时间轴折叠成 0）。
- 时间轴面板：`setMinimumHeight(96)` + `setMaximumHeight(340)`（**不能** `setFixedHeight`，否则拖不动）；
  默认高度按 §4 的档位令牌，用 `setSizes()` 落地。
- 检查器：`setMinimumWidth(44)`、拖动范围 220–560 用**拖动时夹取**实现（不是 `setMaximumWidth`，
  否则折叠时 max=44 会把 220 的下限一起改掉）；折叠 = `setMaximumWidth(44)` + handle `setEnabled(false)`。
- 双击 handle = 恢复**当前档位**的默认值（时间轴 132/112/96，检查器 304），不是硬编码 132/304。
- 记住用户拖过的尺寸：**只在本次会话内存里**（不写 settings，理由见 §4.5）。

**备选（不采用）**：自绘 splitter（约 150–200 行：命中、捕获、双键、光标、夹取、DPI、布局失效）。
只有在 3A 发现 QSplitter 的 handle 无法做像素级样式时，才降级为"薄自绘"（2 个 pane 的布局数学很简单，约 80 行）。

---

### Q3 时间轴两套坐标，谁是 source of truth？（`TimelineModel` 不动）

**结论：`TimelineModel` 是唯一 source of truth，且不需要改它的任何一行。**
"视图窗 = 全片"只是**投影**（读），"画布 = 可见范围"是模型本身的定义（`pixels_per_frame` + `scroll`）。

- 现有 API 已经够用（`src/core/TimelineModel.h`）：
  `FitPixelsPerFrame() = viewport_width / frame_count`、`FrameToX(frame) = frame*ppf − scroll`、
  `FrameAtX(x)`、`MaxScroll() = frame_count*ppf − viewport_width`、`SetPixelsPerFrame`/`SetScroll`（都自带夹取）、
  `ZoomAt(factor, anchor_x)`、`EnsureFrameVisible(frame, margin)`、`BuildTicks()`。
  注意：`FitPixelsPerFrame = W/count`，即"第 N 帧占 `[N, N+1)`"，**最后一格右缘正好落在 W**——所以全片坐标是
  `frame_count` 格，不是 `frame_count−1`。
- 新增一个**无状态换算层** `TimelineViewport`（`src/app/ui/TimelineViewport.h`，纯函数）：
  - 读：`contentPx = count × ppf`；`viewStartPx = scroll`；`viewPct = scroll/contentPx`；`spanPct = width/contentPx`。
  - 写：`SetViewByFrames(startFrame, spanFrames)` → 先把 `spanFrames` 夹到 `[minSpan, count]`
    （`minSpan = ceil(width / MaxPixelsPerFrame())`，`MaxPixelsPerFrame = max(8, fit)`，
    对应 spec F2"1 帧 ≥8px"；极短片段 `fit > 8` 时下限自然放宽成全片），
    再 `SetPixelsPerFrame(width/spanFrames)` → `SetScroll(startFrame × ppf)`。
  - 视图窗手势：拖左/右端点 = 改 `spanFrames`（锚定另一端）；拖中间 = 平移 `startFrame`；滚轮以指针为中心 =
    `ZoomAt(1.25^steps, x)`；Shift+滚轮 = `SetScroll`。
  - **没有任何"view.start/view.end"成员**：所有量都是当场由模型算出来的，不存在两份状态可以不同步。
- 工作区条（循环区间）与刻度**同轴**：A/B 直接用同一个 `FrameToX()` 映射（这与反馈第二轮第 6 条一致）。
  A/B 的数据源是 `core::LoopRange`（frame 绝对帧号），与视图无关；视图变化只改变它被画在哪儿。
- 全片坐标只出现在两个地方：视图窗滑块的轨道、以及它上面的播放头小标记（`frame/count`）。
- **语义边界（必须写死，否则会悄悄改契约）**：
  - 只有**时间轴画布**（刻度/波形/标记带）上的点击与拖动才"点击即暂停 + 暂停后精确 seek"（spec §3.7 契约，不变）。
  - 两条滑块是**范围编辑**：改缩放、改 A/B、平移 — **不暂停、不 seek、不改变当前帧**。
  - A/B 手柄只在可见范围内可拖；越界时在轨道边缘画一个小三角示意（不隐藏手柄、不自动滚视图）。

---

### Q4 `InfoBar` 拆解后：删除还是保留？

**结论：删除 `InfoBar` 组件（`src/app/InfoBar.{h,cpp}` 在 3D 删除），通知语义整体搬到新的 `BannerSlot`。**

为什么不是"包一层"：`InfoBar` 同时承担 5 个职责（文件条、规格、帧号/时间码、唯一横幅、fps 角标）。v2 把这 5 个
拆到 4 个区之后，剩下的是一个"什么都不剩的空壳"，留着只会诱导后续把信息塞回去（定义 §11-Q9 的判断）。

**必须原样保留的语义（不是控件）**：`lapian::media::BuildMediaStatusNotices` / `SelectPrimaryNotice` /
`SecondaryNoticeSummary` / `SecondaryNoticeTooltip` / `StatusNoticeKind`（`src/media/StatusNotice.*`）**一行不改**。
`BannerSlot` 只负责"把 primary 画出来 + 关闭按钮 + 几何"，即现在的 `InfoBar::ApplyNotices()` 的后半段。

**受影响的测试（逐条给改法，附录 A 有完整版）**：

| 测试 | 现状依赖 | 改法 |
|---|---|---|
| `--banner-test`（T6.5，约 30 条 check） | `BannerStateForTest()` 的 `kind=/visible=/banners=/text=/banner=x,y wxh/info_h=/secondary=/active=`；`InfoBarHeightForTest()`；`BannerUiStateForTest()`；`LayoutProblemsForTest()` 里 info 相关 5 条 | 保留 5 个字符串键；`banner=` 改指 banner 槽几何；`info_h=` 保留为 `BannerSlot` 高度（别名，避免改 ~20 处断言）；`BannerUiStateForTest()` 的 `loop=1` 改由"循环开关控件可见"得出（现在是 `loop_label_->isVisible()`）；删 1 条 `banner-overlaps-frame`（帧读数不在 InfoBar 里了），其余 overlaps 保留 |
| `--dpi-test`（T6.4） | `LayoutProblemsForTest()` 的 `info`/`panel` 阈值；`LayoutStateForTest()` 打印 `info=…`/`info_<label>=…`；`InfoNarrowModeForTest()`/`InfoFileLabelForTest()` | 阈值整段重写（§附录 A.1）；`LayoutStateForTest` 增加 `appbar/transport/banner/status/inspector` 并保留 `stage/gfx/timeline` 键；`InfoFileLabelForTest/InfoNarrowModeForTest` **方法名不变**，内部改读顶栏文件名标签（elide 逻辑搬过去） |
| `--online-quality-test --ui` / `--online-vip-test` | `OnlineFpsBadgeTextForTest()`（`InfoBar::online_fps_badge_`）、`OnlineFrameRateBannerVisibleForTest()`、`OnlineFrameRateBannerCloseVisibleForTest()`、`DismissOnlineFrameRateBannerForTest()`、`BannerStateForTest()` | **断言一行不改**，只改 `MainWindow` 里这 4 个 getter 的指向（`info_bar_` → `shell_->bannerSlot()` / 顶栏 `onlineFpsBadge`）。这就是"先留缝、再换壳"的收益 |
| `--robust-test` | `BannerStateForTest().contains("kind=error")` | 不改（键保留） |
| `--key-test`（77） | 无布局依赖 | 不改。新增约束：所有新按钮 `Qt::NoFocus`，splitter handle 不抢焦点 |
| `tests/timeline_test.cpp`（ctest） | 不涉及 InfoBar | 不改，但见 Q1/附录 A.6 的高度策略约束 |

---

### Q5 `--ui-v2` 开关形态：环境变量 vs 设置项？

**结论：环境变量 `LAPIAN_UI_V2`（`0/unset` = 旧壳，`1` = 新壳），只读一次；不进设置项、不进 settings 文件。**

理由：
1. **settings schema 是冻结契约**（T6.3/F8 的键集合被 `--settings-test` 断言），加一个 `ui_v2` 键要动 schema +
   被 `--settings-expect` 二次进程读取，收益为零。
2. **测试要确定性**：`--dpi-test`/`--banner-test`/`--key-test` 必须能在同一台机器上分别跑两个壳，
   而环境变量天然是"进程级"的；设置项是"用户级"的，跑测试要先改用户状态，不可接受。
3. **语义是"迁移期开关"而不是"用户偏好"**：M9 只要求"可回退到旧壳直到新壳过 M1–M8"，不要求长期双主题/双壳。
   环境变量不会让用户以为"有两种 UI 可选"。
4. 与既有约定一致：`LAPIAN_SETTINGS_LOG`/`LAPIAN_STARTUP_LOG`/`LAPIAN_ONLINE_LOG`/`LAPIAN_SNAPSHOT_LOG`…
   已经是同一族（`MainWindow.cpp:157-158`、`main.cpp`）。

实现约束：
- 在 `main.cpp` 里、**构造 `MainWindow` 之前**读一次，分支只在"构造哪套布局"上；**不允许运行期切换**
  （否则两套控件都要建，冷启动和内存都吃亏）。
- 同时接受命令行 `--ui-v2`（与既有 `--xxx-test` 风格一致，便于脚本）；两者任一为真即新壳。
- 3A–3C：所有门禁**跑两遍**（新壳 + 旧壳）。3D：删除旧布局分支，`LAPIAN_UI_V2` 保留一个版本作为 no-op 别名
  （让旧脚本不炸），下一轮删除。

---

## 2. P1 信息架构定稿

### 2.1 区域与像素（逻辑 px，`regular` 档；`compact`/`dense` 见 §4）

```
系统标题栏（OS 原生，保留；不做无边框）          ← 不在客户区，不参与高度预算

顶栏 TopBar 38             [▣ 文件名（elide）] [1920×1080·30.000fps] [本地·零网络]
                                                                [打开] [在线] [?] [设置]
横幅槽 BannerSlot 0|26      ← 同一时刻只有 1 条；出现时把下面整体推下，不覆盖

工作区（外边距 6；QSplitter 横 handle 6 → 右侧检查器）
├ 左列（QSplitter 纵）
│  ├ 画面区 StageStack      吃满剩余高度（720 下 = 462）
│  │    page0 = GfxWidget（独立 HWND）
│  │    page1 = 空态 / 加载 / 错误 卡片（Qt）
│  ├ Transport 46
│  │    [首][−1][▶][+1][末] │ [A][B] │ 1.0x [🔊][音量滑条]
│  │                00:00:06:23            ← 主读数 26px 等宽
│  │                  帧 203 / 总 4573      ← 次读数
│  │    [缓冲 12/31MB][循环][波形][标记][截图][全屏][▤ 检查器]
│  ├ QSplitter 6（纵，双击复位）
│  └ 时间轴面板 132
│        头 26（工作区/视图/缩放）· 视图窗 13 · 工作区 15 · 画布 78
└ 检查器 Inspector 304      220–560，折叠 44
     头（标记 6 | 截图 3 | 信息）· 列表 · 编辑区 · 页脚（存储位置）

状态栏 24                  [唯一主动消息]        [音频 AAC 48k][缓存 60.75MiB][波形 就绪]
```

### 2.2 控件归属（每个控件只属于一个区）

| 区 | 控件/元素 | 说明 |
|---|---|---|
| 顶栏 | 文件图标 + 文件名（`ElideMiddle`，tooltip 全名） | 唯一的文件名展示处 |
| 顶栏 | 规格 chip：`1920×1080 · h264 · 30.000fps` | 只放"判断是否低于保证档"的三个数；完整属性在检查器信息页 |
| 顶栏 | 模式 chip：`本地·零网络` / `在线·1080P·29.412fps` / `缓存命中·0 网络` / `解析中` / `打开失败` | 颜色+文字双编码（色弱可读）；DEC-4 的 fps 角标内联在此 |
| 顶栏 | `打开`(Ctrl+O) `在线`(Ctrl+U) `?`(键位一览) `设置`(Ctrl+,) | 4 个按钮，均 `setDefaultAction(既有 QAction)` |
| 横幅槽 | 图标 + 文本 + 关闭（仅降帧横幅可关） | 同一时刻 1 条；其余进状态栏消息 |
| 画面区 | `GfxWidget`（独立 HWND）/ 占位卡片 | 卡片的按钮：打开本地 / 在线打开（空态）；重试 / 换清晰度 / 打开本地（错误） |
| Transport | 首帧、−1、播放/暂停、+1、末帧 | 5 个 icon 按钮，tooltip 带键位 |
| Transport | 设 A、设 B | 2 个 icon 按钮 |
| Transport | 速度（文字 `1.0x`）、静音、音量滑条 | 速度按 `[`/`]` 循环 0.25–2x |
| Transport | 时间码 26px 等宽（主读数，可点击输入）+ `帧 203 / 总 4573`（次） | 唯一的主读数；点击=输入帧号/时间码 |
| Transport | 缓冲槽（仅在线 buffering/stalled 时出现：进度条 + `已缓存 12.3/30.9 MB`） | 不放在画面上（HWND 约束） |
| Transport | 循环开关、波形开关、添加标记、截图、全屏、检查器开关 | 6 个 icon 按钮（开关类可 checkable） |
| 时间轴 | 头：`工作区 …·循环开`、`视图 0–4572 · 100%`、`−`/`+`/`适配` | 工作区数值的唯一常显处 |
| 时间轴 | 视图窗滑块（全片坐标） | 缩放/平移 |
| 时间轴 | 工作区滑块（可见范围坐标） | A/B 编辑 |
| 时间轴 | 画布：标尺（时间码标签）+ 标记带 + 波形 + 播放头 | 播放头**任何情况下都画**（越界贴边画箭头） |
| 检查器 | 头（3 个 tab + 计数徽标 + 折叠）、标记列表、编辑区、截图网格、信息表、页脚（存储位置） | 存储位置唯一出现处 |
| 状态栏 | 1 条消息 + 3 个次级指标（音频/缓存/波形） | 现在状态栏有 9 个 permanent widget → 压到 3+1 |

### 2.3 键位可发现性（M5 的 100% 对照表，来自 `spec.md` §3.7 的 17 行）

| 操作 | 主键 / 备用 | 可见入口 | tooltip 文案 |
|---|---|---|---|
| 播放/暂停 | `Space` | Transport ▶ | `播放 / 暂停（Space）` |
| 前进一帧 | `PgDn` / `→` | Transport +1 | `下一帧（PgDn / →）` |
| 后退一帧 | `PgUp` / `←` | Transport −1 | `上一帧（PgUp / ←）` |
| 首帧/末帧 | `Home`/`End` | Transport 首/末 | `首帧（Home）` / `末帧（End）` |
| 打开文件 | `Ctrl+O` | 顶栏「打开」 | `打开本地文件（Ctrl+O）` |
| 定位到帧号 | `Ctrl+G` | Transport 时间码可点击 | `点击输入时间码或帧号（Ctrl+G）` |
| 设入点/出点 | `I`/`O` | Transport A/B | `设入点 A（I）` / `设出点 B（O）` |
| 循环开关 | `L` | Transport 循环按钮 | `工作区循环开关（L）` |
| 清除 A/B | `Shift+L` | 循环按钮 tooltip + 工作区滑块双击 | `工作区循环开关（L）· 清除 A/B（Shift+L）· 双击轨道恢复全片` |
| 减速/加速 | `[`/`]` | Transport 速度按钮 | `播放速度（[ / ]）` |
| 波形开关 | `W` | Transport 波形按钮 | `波形开关（W）` |
| 静音 | `M` | Transport 静音按钮 | `静音（M）` |
| 添加标记 | `Ctrl+M` | Transport 标记按钮 + 检查器 `+` | `在当前帧添加标记（Ctrl+M）` |
| 截图 | `S` | Transport 截图按钮 | `截图当前显示帧（S）` |
| 全屏 | `F` / 双击画面 | Transport 全屏按钮 | `全屏（F / 双击画面）` |
| 时间轴缩放 | 滚轮 / `+` `-` | 时间轴头 `−`/`+` | `缩小时间轴（− / 滚轮）` 等 |
| 退出全屏 | `Esc` | 全屏时 Transport 全屏按钮 | `退出全屏（Esc）` |

覆盖率 17/17 = 100%。**没有新增键位，没有改键位**（spec §3.7 不动）。

### 2.4 与原型的三处差异（其余一致）

| # | 差异 | 理由 |
|---|---|---|
| D1 | **画面区不设 26px 表头**（原型有 "合成 1 / sample.mp4 / 尺寸 / 适配"） | 只重复顶栏信息；占 3.6pp；保留会让 200% DPI 画面 51.8% < 55% 门槛（§4）。原型留了 `?hdr=0` 变体可直接对比 |
| D2 | 顶栏去掉 App 品牌区（"拉" 方块 + "拉片"）与左侧汉堡菜单 | 应用名在系统标题栏；v1 本来没有菜单，v2 也不加（不预建菜单）；省 60px 且少一个会阻塞首绘的控件 |
| D3 | 清理原型里的讨论用途按钮（`讨论`）、假 `适配` 按钮（100% 缩放属候选池） | 不是产品控件 |

---

## 3. P2 屏幕/状态矩阵

约定：`●`=可见可用，`◐`=可见但禁用/置灰，`—`=不出现。所有状态都要在 100/125/150/200% 与 820×500 下无裁切（§3.3）。

| # | 状态 | 进入 | 顶栏 | 画面 | Transport | 时间轴 | 检查器 | 横幅 / 状态栏 | 验证缝 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | **空** | 启动 / 关闭后 | 文件=`未打开文件`；规格 chip 隐藏；模式 chip=`未打开`（中性灰）；只留「打开/在线」 | page1 卡片：`把片子拖进来，或从这里打开` + 2 按钮 + 保证档提示 | 全部 ◐（读数 `——:——:——:——`、`帧 —`、`总 —`） | 两条滑块 ◐（30% 透明、不可拖）；画布一行 `打开视频后显示帧刻度与波形`；播放头不画 | 折叠 44（或展开但列表空态 + 输入框 ◐） | 无横幅；状态栏 `就绪 · 本地模式（零网络）` | `v2EmptyCard` 可见、`gfx` 不可见、`BannerSlot` 高 0、帧读数占位串 |
| 2 | **本地暂停**（打开后默认） | 打开文件 | 文件名 + 规格 + `本地·零网络` | page0 真实画面（首帧/当前帧） | ● 全部；播放按钮=▶ | ● 全片适配；工作区=全片（循环开） | 展开 304（有标记才自动展开，见 §3.2） | 状态栏 `已打开，停在第 N 帧 · 本地模式（零网络）` | `--dpi-test`/`--key-test`；帧号几何 |
| 3 | **本地播放** | `Space` | 同上（`本地·零网络` 不变） | 画面推进 | 播放按钮=⏸ | 播放头跟随；滚出视图自动滚（40% 余量） | 同上 | 状态栏 `播放中 · 第 N 帧` | `--key-test`、`--loop-test` |
| 4 | **循环 A/B** | `I`/`O`/`L` | 规格不变 | 不变 | 循环按钮 on；A/B 按钮可用 | 工作区条高亮；标题 `工作区 00:00:33:10–00:01:26:20 · 循环开`；区外压暗 + A/B 标签 | 不变 | 状态栏 `循环 1500–2600 · 第 N 帧` | `--loop-test`、`LoopStateForTest` |
| 4b | **A=B 定帧** | `I` 后 `O` 同帧 | 同上 | 画面保持 | 循环 on | 工作区宽度 = 1 帧格（≥1px） | 同上 | 状态栏含 `定帧` | `--loop-test` 既有 9/0 |
| 5 | **标记编辑** | `Ctrl+M` / 点列表行 | 不变 | 跳帧（点列表时先滚视图再落帧） | 不变；**全局快捷键让位** | 标记带显示新旗标；播放头不消失 | 展开、选中该行、标签编辑框聚焦 | 状态栏 `已添加标记 …`；存储页脚显示 sidecar | `--mark-test`、`--note-test`、`FocusNoteEditorForTest` |
| 6 | **全屏** | `F` / 双击画面 | — | 画面吃满（只剩 Transport + 时间轴） | 精简：首/±1/播放/末、时间码、循环、波形、全屏(退出) | 保留（可拖）；头收起 | —（隐藏，不销毁） | 无横幅（隐藏）；无状态栏 | `--fullscreen-test` + 新 `FullScreenChromeForTest()` |
| 7 | **在线解析中** | `Ctrl+U` + 粘贴 BV → 确认 | 文件名位=`BV1… · P1`；模式 chip=`在线·解析中`；规格 chip=`请求中` | page1 卡片：`正在获取播放地址…` + 进度 + `取消`（Ctrl+Shift+U） | ◐ | ◐ | 折叠 44 | 横幅 `在线加载中：…（可取消）`；状态栏 `正在解析 … · 已等 1.2s · Ctrl+Shift+U 取消` | `OnlineStateForTest`（`state=preparing`） |
| 8 | **在线加载**（已拿到 playurl，等首帧） | 解析成功 | 模式 chip=`在线·1080P`；规格 chip 由实际流填充 | page1 卡片：`已拿到播放地址，等待首帧…`；首个可用帧一到就切 page0 | ◐ | ◐（总帧数 `扫描中`） | 折叠 44 | 状态栏 `首帧准备中 · 已等 N ms` | `state=buffering` + `published>0` |
| 9 | **在线缓冲/卡顿** | `kBuffering`/`kStalled` | 模式 chip=`在线·缓冲中`（warn 色） | page0 保持最后一帧（**不切卡片**，避免闪） | ● 除播放（自动暂停，按钮=▶）；缓冲槽出现：进度条 + `已缓存 12.3/30.9 MB` | ●（可拖，拖动按既有在线预览路径） | 保持 | 横幅 `缓冲中：网络不足，已自动暂停（第 N 帧）`；状态栏 `缓冲中：等待第 N+1 帧 · 已等 1.8s` | `--online-buffer-test`、`OnlineProgressForTest` |
| 10 | **在线就绪** | 首帧呈现 | 模式 chip=`在线·1080P·29.412fps`（实际 qn/fps；降帧时 warn 色 + 文案 `降帧`） | page0 正常 | ● 全开 | ●（索引渐进：`总 ≈N` → `总 N`） | 正常展开 | 状态栏 `在线播放 · 第 N 帧 · 缓存 60.75MiB` | `--online-m1/m2-test`、`OnlineFpsBadgeTextForTest` |
| 11 | **缓存命中** | 二次打开同一 BV | 模式 chip=`缓存命中 · 0 网络`（cache 色） | page0（本地文件直开） | ● 全开 | ● 全片 | 正常 | 状态栏 `缓存命中 · 0 网络 · 第 N 帧` | `--online-offline-test` 44/0、`state=cacheHit` |
| 12 | **在线错误** | 解析/取流/磁盘失败 | 模式 chip=`在线·打开失败`（err 色） | page1 错误卡片：`这条打不开` + 原因（含错误码）+ `重试` / `换清晰度` / `打开本地文件` | ◐ | ◐ | 折叠 44 | 唯一横幅（err 色）+ 状态栏一句 | `--online-fault-test`、`BannerStateForTest` |
| 13 | **切档中** | 清晰度菜单选另一档 | 模式 chip=`切档中 → qn=64`（warn 色）；清晰度按钮 ◐ | page0 保持最后一帧（切档完成前不黑屏） | 除播放外 ◐；缓冲槽显示切档进度 | ◐（A/B 与标记保留，切完按 PTS 回位） | 保持 | 横幅 `正在切到 720P…`（不可关）；状态栏 `切档中 · 旧流已停` | `--online-quality-switch-test`（42/0 的 A 段） |
| 14 | **扫码登录** | 在线打开对话框 → 扫码 | 对话框模态，顶栏不变 | 不变 | 不变 | 不变 | 不变 | 对话框内：二维码 + 4 态文案 + 180s 倒计时 + 取消 | `--online-qr-test` 19/0（真实扫码未验证，口径不变） |
| 15 | **设置** | `Ctrl+,` / 顶栏设置 | 不变 | 不变 | 不变 | 不变 | 不变 | `CacheSettingsDialog`（进主题）：目录/配额/打开目录/清空/孤儿清理；危险操作二次确认 | `--settings-test`、T8.3.1 既有用例 |
| 16 | **提示类**（VFR/10-bit/无音频/4K/长 GOP/降帧） | 打开时判定 | 降帧时模式 chip 加 `降帧` 文字 | 不变 | 不变 | 不变 | 信息页可查实际规格 | **只显示优先级最高的一条**；其余进状态栏 `其他提示` 位（可点开 tooltip） | `--banner-test` 全量；`kind=` 语义不变 |
| 17 | **文件拖入悬停** | 拖文件到窗口 | 不变 | page0 上叠加**不到画面**：拖放由 `GfxWidget` 原生接收；空态由 page1 卡片接收 | 不变 | 不变 | 不变 | 状态栏 `松开以打开 …` | 既有 drop 路径（`GfxWidget::dropEvent`）+ 空态卡片 `acceptDrops` |

### 3.1 三档密度（按客户区高度，带 20px 迟滞）

| 档位 | 触发（客户区高 H） | 迟滞 | 顶栏 | Transport | 时间轴 | 状态栏 | 工作区边距 | 表头 |
|---|---|---|---|---|---|---|---|---|
| `regular` | H ≥ 640 | 回落到 `compact` 需 H < 620 | 38 | 46 | 132 | 24 | 6 | 时间轴头 26；检查器头 26 |
| `compact` | 520 ≤ H < 640 | 升 `regular` 需 H > 660；降 `dense` 需 H < 500 | 34 | 40 | 112 | 22 | 4 | 头 24 |
| `dense` | H < 520 | 升 `compact` 需 H > 540 | 30 | 36 | 96 | 20 | 4 | 时间轴**无头**（工作区/视图数值移到 Transport 副行）；检查器头 22 |

- 用户拖过的时间轴高度**优先于**档位默认值（本次会话内记住）；档位切换只在"默认值"上生效。
- 全屏：隐藏顶栏/横幅/状态栏/检查器，Transport 与时间轴按当前档位保留（画面占比 ≥80%）。
- 自动折叠检查器：**只在首次打开媒体时**按窗口宽度决定一次（宽度 < 1000 → 折叠 44），之后完全听用户的。
  空态一律折叠。

### 3.2 DPI × 几何矩阵（客户区逻辑尺寸，来自 T6.4 实测口径 `MainWindow.cpp:782-797`）

| DPI | 客户区（逻辑） | 档位 | 画面高 | 画面占比 | M4 门槛 | 备注 |
|---|---|---|---|---|---|---|
| 100% | 1280×720 | regular | 462 | **64.2%** | ≥62% | 时间轴 132（原型默认） |
| 125% | 1280×720 | regular | 462 | **64.2%** | ≥62% | 与 100% 同几何 |
| 150% | 1248×624 | compact | 402 | **64.4%** | ≥62% | 时间轴 112；**没有 compact 档会掉到 58.7%** |
| 200% | 928×452 | dense | 256 | **56.6%** | ≥55% | 时间轴 96 + 无表头；现状 53.5% |
| 窄窗口 | 820×500 | dense | 304 | **60.8%** | ≥59% | 检查器自动折叠后 Transport 全宽 758 |

推导与"为什么是这些数"见 §4.3–4.4。上表每一行都要在 3A 用 `--dpi-test` 的 4 档 + narrow 相位实测复核，
并且**每栏高度必须等于令牌 ±2px**（§4.4 的硬断言）。

### 3.3 窄宽度矩阵（Transport 按**自身宽度**降级，不是视口宽度）

| Transport 宽 | 保留 | 收起 | 说明 |
|---|---|---|---|
| ≥ 760 | 全部 | — | 预算需要 750px（§4.6） |
| 660–759 | 全部按钮 + 读数 + 循环数值 | 音量滑条（静音按钮保留） | 省 78px |
| 560–659 | 播放/±1/首末/读数/循环/波形/标记/截图/全屏/检查器 | A/B 按钮、速度按钮；读数 26→22px | 省 ~106px |
| < 560 | 播放/±1/读数/循环/波形/全屏/检查器 | 首末、标记、截图（都还有快捷键 + 检查器 `+`） | 再省 ~104px（合计 ~210px） |

- 阈值是**预算值**（按 §4.6 的控件宽度累加），3A 用 `QFontMetrics` 实测校准，允许 ±10% 移动，但**优先级顺序不变**。
- 检查器折叠后左列变宽，Transport 自然回到高档位（无额外逻辑）。

---

## 4. P3 设计令牌 + 重算高度预算

### 4.1 为什么必须重算（旧 68% 的来历）

定义文档 §7.2 的预算按 `顶栏 36 + Transport 40 + 时间轴 112 + 状态 24 = 212` 算，得出 1280×720 画面 508（70.6%）。
但那是**原型定稿前**的骨架，和已批准的原型差三处：顶栏 38（+2）、Transport 46（+6）、时间轴 132 + 两条滑块
（132 已含滑块 28，不是 112+28）。更关键的是：旧壳**没有顶栏、没有 Transport**，固定栏只有 `128 时间轴 + 34–53 InfoBar + 28 状态栏 ≈ 190`。
新壳把这 190 换成 258（=38+46+132+24+12 边距+6 分隔），**多 68px = 720 的 9.4pp**。这就是 73.6% → 64.2% 的全部原因。

所以"68% 不成立"不是精度问题，是**骨架变了**。要保住 68%，唯一办法是让时间轴回到 ~96（比现状 128 还矮 25%），
也就是用"看波形卡点"的面积换画面面积——这与用户"时间轴拆两条滑块、默认 132"的定稿相反。

### 4.2 令牌（逻辑 px；Qt 用 `setPixelSize`，见 4.7 的 DPI 校验）

**间距**（2 的倍数，不再出现 3/5/7/9/14 这类原型魔法数）：

| token | 值 | 用途 |
|---|---|---|
| `s1` | 2 | 图标与文字的基线修正、chip 内边距 |
| `s2` | 4 | 面板内元素间距、tab 内边距 |
| `s3` | 6 | 工作区外边距（regular）、按钮组间距、分隔条宽 |
| `s4` | 8 | 面板内边距、按钮左右内边距 |
| `s5` | 12 | 组间距、卡片内边距（纵向） |
| `s6` | 16 | 卡片内边距（横向） |
| `s7` | 24 | 卡片标题与正文之间 |

**字号阶梯**（等宽数字一律 `Consolas` → 回退 `Cascadia Mono` → 回退 `QFontDatabase::FixedFont`；
中文用 `Microsoft YaHei UI` → 回退系统 UI 字体，**不打包字体**）：

| token | pt/px | 字重 | 用途 |
|---|---|---|---|
| `fs-tick` | 9.5px | 400 | 时间轴标尺标签、滑块上的极小数字 |
| `fs-micro` | 10px | 400/700 | 键位角标、标记帧号小条、tab 计数徽标 |
| `fs-meta` | 11px | 400 | 顶栏 chip、状态栏、tooltip、检查器页脚 |
| `fs-body` | 12px | 400 | 正文、按钮文字、列表项、输入框 |
| `fs-title` | 13px | 600 | 顶栏文件名、对话框标题、卡片标题（15px 见 `fs-card`） |
| `fs-card` | 15px | 600 | 空态/错误卡片 `<h3>` |
| `fs-readout` | 26px | 600 | 时间码（`letter-spacing 0.05em`，`tabular-nums`） |
| `fs-readout-sm` | 22px | 600 | Transport <660px 时的时间码 |

**控件高度**：

| token | 值 | 用途 |
|---|---|---|
| `h-play` | 30×34 | 播放按钮（唯一的实心 accent 按钮） |
| `h-btn` | 26 | 普通图标/文字按钮（compact 24 / dense 22） |
| `h-chip` | 20 | 顶栏 chip（compact/dense 18） |
| `h-input` | 22 | 帧号/标签输入框 |
| `h-kbd` | 14 | 键位角标（**只在顶栏"打开/在线"上出现**，其余靠 tooltip） |
| `h-track-zoom` | 4 | 视图窗轨道（拖拽热区 13px 行高） |
| `h-track-work` | 10 | 工作区轨道（拖拽热区 15px 行高） |
| `h-split` | 6 | QSplitter handle |
| `w-inspector` | 304（220–560，折叠 44） | 检查器 |
| `radius` | 2/3/4/8 | kbd / 按钮·输入框 / 面板 / 卡片 |

**语义色**（颜色 + 文字**双编码**，任何语义色不得单独承担信息）：

| 角色 | 前景 | 容器底 | 边框 | 对比度（对 `#17171a`） |
|---|---|---|---|---|
| `accent`（强调/播放/播放头/选区） | `#5b8def` | `#1b2333`(12%) | `#5b8def`(45%) | 5.5:1 |
| `text` | `#e8e8ee` | — | — | 14.7:1 |
| `text-dim` | `#a2a2ad` | — | — | 7.1:1 |
| **`text-meta`（新增）** | `#8a8a95` | — | — | **5.2:1**（≥4.5，可承载正文级信息） |
| `text-mute` | `#6f6f7a` | — | — | 3.6:1 → **只用于禁用/装饰**，不得承载信息 |
| `ok`（就绪/本地零网络） | `#9ed6a1` | `#17201a` | `#2f4a32` | 8.1:1 |
| `warn`（降帧/缓冲/4K） | `#f0cb7a` | `#241f10` | `#5a4a1a` | 10.3:1 |
| `err`（打开失败） | `#f08c93` | `#241417` | `#5a2a2e` | 6.6:1 |
| `cache`（缓存命中/在线就绪） | `#a6d8c4` | `#14211d` | `#2c4a40` | 10.9:1 |
| `wave` | `#6f9df0` @ 55% | — | — | 波形填充；中线 30% |
| `surface` / `surface-2` / `border` | `#17171a` / `#1c1c20` / `#2b2b33` | | | |

> `text-meta` 是相对原型的**修正**：原型 `--text-mute #6f6f7a` 被用在 11px 元信息上，实测 3.6:1 不满足定义 §6.1-M2
> 的"正文 ≥4.5:1"。把承载信息的元信息换成 `#8a8a95`（5.2:1），`#6f6f7a` 只留给禁用态。
> 对比度按 WCAG 相对亮度公式手算，3A 用截屏采样复核（M2 要求顶栏/Transport/检查器/状态栏各至少 1 处）。

**focus ring**（定义 §3.2-3 要求"可见 focus ring"）：

- 可聚焦 = **只允许输入类与列表**：时间码/帧号输入框、标记标签、备注、音量滑条、检查器列表、对话框内控件。
- 工具按钮、通道按钮、分隔条**一律 `Qt::NoFocus`**（理由：现有全局快捷键与"文本框聚焦时让位"的规则依赖焦点位置；
  按钮可聚焦会把 Space/方向键吃进按钮，直接威胁 `--key-test` 77/0）。
- ring 样式：`2px accent @65% + 1px 内缩`；QSS 控件用 `border-color: accent` + `outline` 不可用时改用
  `QWidget::paintEvent` 里画（输入框用 QSS `:focus` 即可）。
- 焦点顺序（`setTabOrder`）：顶栏「打开→在线→?→设置」→ 时间码输入 → 音量 → 检查器 tab → 列表 → 标签 → 备注。

### 4.3 三档令牌与高度预算（逐项推导）

每档的固定栏 = 顶栏 + Transport + 时间轴 + 状态栏；可变量 = 工作区外边距（上下）+ QSplitter handle。

| 项 | regular | compact | dense |
|---|---|---|---|
| 顶栏 | 38 | 34 | 30 |
| 横幅槽（**不常驻**，不参与预算） | 0 \| 26 | 0 \| 24 | 0 \| 22 |
| Transport | 46 | 40 | 36 |
| QSplitter handle（纵） | 6 | 6 | 6 |
| 时间轴面板 | 132 | 112 | 96 |
| 状态栏 | 24 | 22 | 20 |
| 工作区外边距（上下） | 12 | 8 | 8 |
| **固定合计** | **258** | **222** | **196** |
| 画面区 = H − 固定合计 | H−258 | H−222 | H−196 |

代入实测窗口尺寸（客户区）：

| 场景 | H | 档位 | 画面高 | 占比 |
|---|---|---|---|---|
| 100% / 125% | 720 | regular | 462 | **64.2%** |
| 150% | 624 | compact | 402 | **64.4%** |
| 200% | 452 | dense | 256 | **56.6%** |
| 窄窗口 | 820×500 | dense | 304 | **60.8%** |
| 最大化 | 1080 | regular | 822 | 76.1% |

对照（同尺寸、现状旧壳，T6.4 实测）：73.6% / 73.3% / 69.2% / **53.5%** / 58.6%。
即：**100–150% 画面让出 6–10pp 给可见控件；200% 与窄窗口反而变好**（+3.1pp / +2.2pp）。
这是"把 46px Transport 和 38px 顶栏买进来了"的代价，不是白丢。

### 4.4 在"收紧默认 / 下调门槛 / 记住尺寸"里选 (b)：保留 132 默认 + 重定 M4

| 选项 | 做法 | 100% 画面 | 结论 |
|---|---|---|---|
| (a) 收紧默认 | 时间轴默认回 96、Transport 40、顶栏 36 | 69.2% | **能过旧门槛，但代价是时间轴比现状 128 还矮 25%**（波形 44→~8、标尺挤、工作区条要再瘦）。与"两条滑块"的定稿相反。**不采用** |
| **(b) 下调门槛** | 保留 132/46/38/24 定稿令牌 | 64.2% | **采用**：代价写清楚、门槛按算式给、并用"每栏 ±2px"锁住默认值 |
| (c) 记住尺寸 | 首启紧凑、之后按用户拖过的尺寸 | 首启 69.2% 起 | **不采用为默认策略**：① 要新增 settings 键（schema 冻结）；② M4 变成"取决于用户历史"，`--dpi-test` 不可复现；③ 用户拖到 340 时画面占比会掉到 ~35%，没有任何测试会发现；④ 它没有回答"默认该多高"，只是把问题推给用户 |

(c) 里**唯一值得保留**的部分是"本次会话内记住用户拖过的高度"（纯内存，不落盘），已写进 §3.1。

**M4 v2（替代定义 §6.1-M4，只改这一条门槛；其余 M1/M2/M3/M5–M9 不变）**：

| 场景 | 旧门槛 | **新门槛** | 预算值 | 余量 |
|---|---|---|---|---|
| 100% / 125% | ≥68% | **≥62%** | 64.2% | 2.2pp |
| 150% | ≥64% | **≥62%** | 64.4% | 2.4pp |
| 200% | ≥55% | **≥55%（不变）** | 56.6% | 1.6pp |
| 窄窗口 820×500 | ≥60% | **≥59%** | 60.8% | 1.8pp |
| 横幅出现时 | 不算瞬时态 | 同（不算） | 720 下 60.6% | — |

**并且新增一条硬断言（比比例更有效）**：`LayoutProblemsForTest()` 里，当前档位下
`|顶栏−令牌| ≤2`、`|Transport−令牌| ≤2`、`|时间轴−令牌| ≤2`、`|状态栏−令牌| ≤2`、
`|画面−(H−固定合计)| ≤2`；时间轴在用户拖过之后跳过本条（用 `--dpi-test` 的"拖到 340"用例单独验不裁切）。
理由：比例下限只能防"整体膨胀"，防不住"把时间轴默认偷偷从 132 改成 112 再声称达标"。

### 4.5 时间轴内部预算（画布怎么分）

| 项 | regular(132) | compact(112) | dense(96) |
|---|---|---|---|
| 面板头（工作区/视图/缩放按钮） | 26 | 24 | **0**（数值移到 Transport 副行） |
| 视图窗滑块行 | 13 | 12 | 12 |
| 工作区滑块行 | 15 | 14 | 14 |
| 画布 | 78 | 62 | 70 |
| — 标尺 | 22 | 20 | 20 |
| — 标记带 | 12 | 10 | 8 |
| — 波形 | 44 | 32 | 42 |
| 用户拉到 340 时 | 画布 286 → 波形**封顶 96**，余 155 留白（给以后多轨/标记泳道） | | |

- 波形封顶 96 是定稿口径；拉高后多出的高度**留白而不是把波形拉伸**（否则波形变巨大色块）。
- 画布宽 < 240 时（检查器拉到 560 + 窄窗口）标尺标签改画 `mm:ss:ff`（去掉小时位），避免标签互相压。

### 4.6 控件宽度预算（Transport 降级用）

| 元素 | 预算宽 | 备注 |
|---|---|---|
| icon 按钮 | 26 | 26×26，内边距 0 |
| 播放按钮 | 34 | 实心 accent |
| 时间码 26px（11 字符等宽） | 186 | 26×0.6×11 ≈ 172 + 14 内边距 |
| 副行 `帧 203 / 总 4573` | 110 | 不决定宽度 |
| 读数块 | **214** | `max(186,110) + 28` |
| 音量滑条 | 78 | 可收起 |
| 速度按钮 | 34 | `1.0x` |
| A/B 两个按钮 | 52 | |
| 首/末/±1/播放 五个 | 138 | 26×4 + 34 |
| 右侧 6 个按钮 | 156 | 循环/波形/标记/截图/全屏/检查器 |
| 分隔符 4 条 | 36 | 1px + 左右 4px |
| 内边距 | 16 | 8+8 |
| **全量合计** | **750** | 与 §3.3 的 760 阈值一致（留 10px 余量） |

检查器展开时（左列宽 = 窗口宽 − 12 边距 − 6 handle − 304）：
1280 → 958（全量，无需降级）；820 折叠检查器后 → 758（全量边缘，落 660–759 档收音量）。
→ 结论：**只要检查器在窄窗口自动折叠，Transport 在 820 宽下也只需要收一个音量滑条**，A/B 按钮不必消失。

### 4.7 字号与 DPI 的落地校验（不能把 px 当承诺）

- 令牌以**逻辑 px** 定义；Qt 6 用 `QFont::setPixelSize(token)`。高 DPI 下 Qt 按 `devicePixelRatio` 缩放，
  逻辑尺寸不变（这是 v2 的目标）。
- **3A 第一条验收**：`--dpi-test` 在 100/125/150/200% 各拍一张，断言"时间码控件高度 = 26±2 逻辑 px"、
  "顶栏 = 38±2"、"Transport = 46±2"。若发现 `setPixelSize` 被当成物理像素（读数在 200% 下不变大），
  则改用 pt（`px × 0.75`）并重跑；这一步在 3A 第一天做完，不留到 3D。
- 字体回退：`Consolas` 在 Win10/11 必有；`Microsoft YaHei UI` 在 Win10 21H2+ 必有。**不打包字体**（体积 + 许可）。
  若某个中文回退到雅黑以外导致行高变化：以 `QFontMetrics` 实测行高反推控件高（只允许 ±2px），字号令牌不动。

---

## 5. P4 三屏原型（已可点，本轮已更新）

原型仍是 `docs/ui-v2-proto/index.html`（HTML，非 Qt）。本轮对原型做了 8 处**为规划服务**的改动（不改产品）：

| # | 改动 | 为什么 |
|---|---|---|
| 1 | 新增 `density-dense` 档（30/36/96/20 + 收起画面与时间轴表头），`compact` 改成 34/40/112/22 | 原型的 short-height 场景是**坏的**：928×452 下 `.panel.video` 只有 **19px**、820×500 只有 67px（实测），因为固定高度徽章撑破了 `.workspace`。三档令牌是修复方案，原型必须先能演示 |
| 2 | 按窗口高度**自动取档**（`<520` dense / `520–639` compact / `≥640` regular，可手动锁定） | 与 §3.1 的行为一致，便于在不同窗口尺寸截图 |
| 3 | 新增 `?hdr=0`：隐藏画面区 26px 表头 | 演示 P1 的 D1 决定（无表头变体），可直接对比 3.6pp 差异 |
| 4 | 视图窗缩放下限从"8 帧"改为 `画布宽/8`（即 1 帧 8px） | 原型的 8 帧下限 = 150px/帧，**超出 spec F2 的 8px/帧上限 19 倍**；规划以 spec 为准 |
| 5 | `.workspace { overflow:hidden }` + 表头高度改令牌变量 | 修短高度溢出（时间轴压到状态栏上） |
| 6 | 帧号**不补零**（读数 `帧 203`、标记列表 `203`） | 定稿口径是"默认不补零"（反馈 §7），原型还在补零 |
| 7 | 空/加载/错误态**折叠检查器**（rail 只留折叠按钮 + 标记/截图计数）；折叠时隐藏存储页脚 | 与 §3.1"空态一律折叠"一致 |
| 8 | 空态 Transport 的播放按钮也置灰 | 空态"全部 ◐"的口径，原来只有读数/音量变灰 |

**三屏（可直接打开；`hdr=0` 为规划推荐变体）**：

| 屏 | URL | 本轮截图（`build/ui-v2-shots/`，不入库） |
|---|---|---|
| 空态 | `docs/ui-v2-proto/index.html?mode=empty&hdr=0&density=comfortable` | `empty.png`（1280×750） |
| 本地主界面（暂停在 203 帧） | `...?mode=paused&hdr=0&density=comfortable` | `paused.png` |
| 在线缓冲 | `...?mode=buffering&hdr=0&density=comfortable` | `buffering.png` |
| 在线错误 | `...?mode=error&hdr=0&density=comfortable` | `error.png` |
| 在线加载 | `...?mode=loading&hdr=0&density=comfortable` | `loading.png` |
| 200% DPI（dense 档） | `...?mode=paused&hdr=0&density=dense` | `dense-452.png`（928×482） |
| 窄窗口（dense 档） | `...?mode=paused&hdr=0&density=dense` | `narrow-820.png`（820×530） |
| 时间轴拉满（340，波形封顶 96） | `...?mode=paused&hdr=0&tl=340` | `timeline-340.png` |
| 对照：保留表头 | `...?mode=paused&hdr=1&density=comfortable` | `paused-with-header.png` |

> 截图尺寸 = 内容区 1280×750（**含原型自己的 30px 模拟标题栏**，即产品客户区 720）。
> 所以截图里画面占可见高度 462/750 ≈ 61.6%，而产品（客户区 720、无模拟标题栏）是 64.2%——**不要拿截图比例去对 M4**。

**本轮自检（可复现，命令见附录 C）**：

- `?smoke=1`（全部状态轮播 + 7 个标记点击 + 两条分隔条拖拽）：
  `playhead_offview=0 | drag_tl=40 | drag_ins=60 | ins_var=364px`，无 `ERR:`。
- `?debug=1` 实测几何（fresh profile，`--force-device-scale-factor=1`；**dump-dom 模式下本机浏览器外框会吃掉 24×151**，
  所以窗口 1280×871 / 928×603 / 820×651 分别对应视口 720 / 452 / 500 高）：

| 窗口（dump-dom） | 视口 | `.panel.video` | `.panel.timeline` | `#inspector` |
|---|---|---|---|---|
| 1280×871 | 720 高 | 438（含 26 表头） | 132 | 304×628 |
| 928×603 | 452 高 | 228（dense，无表头） | 96 | 304×372 |
| 820×651 | 500 高 | 276（dense，无表头） | 96 | 304×420 |

> 原型自己的数字**低于** §4.3 的预算（452 下 228 vs 预算 256，720 下 438 vs 预算 462），差 24–28px，
> 原因：模拟系统标题栏 30px、多一道 6px 分隔条、`.workspace` 的 padding 被 flex 子项吃掉。
> **M4 只认 Qt 实测**（§4.4 的断言），原型数字只用于"方向对不对"，不写进任何门禁。

**原型 ≠ 产品（重申，别拿它当承诺）**：

1. 字体度量、滚动条、行高、DPI 行为都会在 Qt 里变；画面是静帧；波形是合成占位；假按钮只弹提示条；
   数值（4573 帧 / 30fps / 第 203 帧 / BV 号 / 清晰度）全是示例。
2. **检查器"按窗口宽度 <1000 自动折叠"只在产品里做**：原型只在"无媒体"时折叠，宽窗口/窄窗口都保持展开，
   所以 `narrow-820.png` 里检查器是展开的（产品会折成 44，画面因此再宽 260px）。
3. **窄宽度降级**：原型靠 `.tgroup { overflow:hidden }` 把控件裁掉；产品必须**显式隐藏**（按 §3.3 的优先级），
   不能靠裁剪——裁剪会让"半个按钮"露出来，也没有可断言的可见性。
4. 原型的滑块轨道/把手尺寸是 CSS 像素，Qt 里要有 4–6px 的拖拽热区（视觉 4px、热区 ≥10px）。

---

## 6. P5 组件清单与复用边界

### 6.1 分类总表

| 组件 | 类别 | 说明 | 预计规模 |
|---|---|---|---|
| `Theme`（`ui/Theme.{h,cpp}`） | **新写** | 令牌常量 + `QPalette` + 单份 QSS 模板 + 应用字体；`ApplyTheme()` 只调一次 | ~260 行 |
| `Tokens`（`ui/Tokens.h`） | **新写** | 纯常量（高度/间距/字号/色值），无 Qt 依赖，可单测 | ~90 行 |
| `PlayerShell`（`ui/PlayerShell.{h,cpp}`） | **新写** | 只做布局与区域访问器；不含业务逻辑 | ~320 行 |
| `TopBar` | **新写** | 文件/规格/模式 chip + 4 个动作按钮（`setDefaultAction`） | ~220 行 |
| `TransportBar` | **新写** | 3 组 + 读数 + 缓冲槽；按自身宽度降级 | ~420 行 |
| `TimelinePanel` | **新写** | 头 + 视图窗滑块 + 工作区滑块 + 承载 `TimelineWidget` | ~360 行 |
| `RangeBar`（`ui/RangeBar.{h,cpp}`） | **新写** | 两条滑块共用：单值/区间两种模式、拖端/拖中/点空白、滚轮 | ~240 行 |
| `InspectorPanel` | **新写** | 3 tab + 折叠 + 页脚；**内嵌现有 `MarkPanel`** | ~260 行 |
| `BannerSlot` | **新写** | 一条横幅 + 关闭；复用 `media::StatusNotice` 选择逻辑 | ~180 行 |
| `StageStack` | **新写** | `QStackedWidget`（gfx / 占位卡片）+ 空/加载/错误卡片 | ~240 行 |
| `PlayerSplitter` / `PlayerSplitHandle` | **新写** | 6px、悬停 accent 细线、双击复位、折叠禁用 | ~120 行 |
| `Glyph`（图标） | **新写** | `qrc` 内 2× PNG（16 逻辑 px）+ 缺失回退文字 | 资源 + ~40 行 |
| `TimelineWidget` | **改（小）** | ① 高度策略可切换（默认 Fixed，保持 ctest 断言；v2 用 Fill）② 配色走 `TimelineColors`（默认=现值）③ 播放头越界贴边箭头 ④ 去掉内部横向滚动条（视图窗滑块取代） | ~657 行内改 ~90 行 |
| `MarkPanel` | **包一层** | 不动结构/objectName/信号；配色表改为从 `Theme` 取（v2 关时用现值） | 改 ~30 行 |
| `InfoBar` | **删除**（3D） | 职责拆到 TopBar / Transport / BannerSlot / 状态栏 | 删 483 行 |
| `GfxWidget` | **不动** | 只被 hide/show（Q1 已实测安全） | 0 |
| `PreviewDecoder` / `DragStrategy` / `OnlineSession` / `CacheStore` | **不动** | 硬约束 | 0 |
| `PlaybackController` / `MarkStore` / `SnapshotService` / 音频 / 波形 | **不动** | 被壳层调用 | 0 |

### 6.2 依赖方向（防止壳层反向污染引擎）

```
main.cpp ──> MainWindow（控制器，保留）──> PlayerShell（视图根）
                                   ├── 各子组件（只 emit 用户意图 / 暴露 widget）
                                   └── Theme（只被读）
core/ media/ online/ platform/  ←── 一行都不反向依赖 app/ui
```

- `app/ui/*` **不得** include `MainWindow.h`、不得直接调 `PlaybackController`；只通过构造函数接收
  `QAction*` 与回调 `std::function`，或 emit 信号给 `MainWindow` 接线。
- `Theme`/`Tokens` 不得依赖 `MainWindow`/`GfxWidget`，方便单测（`tokens_test` 可断言"三档令牌单调"）。

### 6.3 明确的"不动"清单（写进 3A 的 review checklist）

`src/platform/gfx/*`（呈现）、`src/app/PreviewDecoder.*`、`src/app/DragStrategy.*`、`src/app/OnlineSession.*`、
`src/online/*`（含 `CacheStore`）、`src/media/StatusNotice.*`（只搬调用方）、`src/core/*`（含 `TimelineModel`、`LoopRange`）、
`src/app/main.cpp` 的测试分发逻辑（只加开关与 getter）。

---

## 7. P6 技术拆分

### 7.1 布局树与类职责（`PlayerShell`）

```
QMainWindow（MainWindow，控制器：保留全部业务逻辑）
└ centralWidget = PlayerShell
   ├ TopBar                     h=令牌
   ├ BannerSlot                 h=0|令牌（在布局里，不叠放）
   └ workspace QWidget          margins=令牌/2
      └ QHBoxLayout
         ├ PlayerSplitter(H) stretch=1   handle=6
         │   ├ PlayerSplitter(V)         handle=6
         │   │   ├ QWidget(videoColumn)  stretch=1
         │   │   │   ├ StageStack        stretch=1
         │   │   │   └ TransportBar      h=令牌
         │   │   └ TimelinePanel         min 96 / max 340
         │   └ InspectorPanel            min 220 / max 560 / 折叠 44
         └ （状态栏在 QMainWindow::statusBar()，不在 PlayerShell 内）
```

`PlayerShell` 的公开面（**只有这些**，多余的不加）：

```cpp
class PlayerShell : public QWidget {
 public:
  explicit PlayerShell(QWidget* parent = nullptr);
  TopBar* topBar();  BannerSlot* banner();  StageStack* stage();
  TransportBar* transport();  TimelinePanel* timeline();  InspectorPanel* inspector();
  PlayerSplitter* vSplit();  PlayerSplitter* hSplit();

  void SetDensity(Density tier);          // regular/compact/dense，带 20px 迟滞
  Density density() const;
  void ResetSplitSizes();                 // 双击分隔条 → 当前档位默认值
  void SetInspectorCollapsed(bool collapsed);
  bool inspector_collapsed() const;
  void ApplyTokens();                     // 令牌 → 各控件 min/max/固定高
};
```

约定：
- `PlayerShell` **不持有** `QAction`（由 `MainWindow` 建好后交给 `TopBar/TransportBar` 的
  `BindActions(...)`），因此动作的 `enabled/checked/text` 是唯一真源（`QToolButton::setDefaultAction`）。
- `PlayerShell` **不读** `PlaybackController`/`MarkStore`；`MainWindow` 通过 `TransportBar::SetPlayState()`
  这类窄接口推送状态（`{idle, paused, playing, buffering}`）。
- 尺寸只由 `ApplyTokens()` 与用户拖拽改变；`resizeEvent` 只做"档位判定 + 宽度降级"两件事。

### 7.2 `Theme` 落地（只做一次，避免为每个控件写样式表）

- `Tokens.h`：`constexpr` 常量（无 Qt），供 `Theme` 与单测共用。
- `Theme::Apply(QApplication&)`：① 设 `QPalette`（`Window/Base/Text/Button/Highlight/Disabled` 全给）
  ② 设应用字体 ③ 设**一份**全局 QSS（`QWidget{...}` 起手，用 `QString::arg` 注入令牌）④ 设
  `QToolTip`/`QMenu`/`QScrollBar`/`QDialog`/`QMessageBox`/`QLineEdit`/`QTextEdit`/`QComboBox`/`QSlider`/`QSplitter::handle`。
- **铁律**：任何自绘容器（`QWidget` 子类）必须 `setAttribute(Qt::WA_StyledBackground, true)`，
  否则重演 `InfoBar` 的 1.28:1（`InfoBar.cpp:33-37` 只设了 `setStyleSheet`；正例 `MarkPanel.cpp:61-68`）。
  这条写成 3A 的 review checklist + M2 的截屏采样。
- **禁止**：per-widget `setStyleSheet`（除 3 处动态色：fps 角标 warn、危险按钮、banner kind）。
  理由：每多一个 per-widget 样式表，Qt 就在对应控件上多一次样式解析 + polish，且会打断全局继承。
- 不用 `QGraphicsEffect`（阴影/模糊）——冷启动与 UI 线程都不划算；卡片阴影用 1px 边框 + 深色底模拟。
- 不引入 `Qt6Svg`（体积）；图标走 `qrc` 里的 2× PNG（16 逻辑 px）。

### 7.3 开关与共存（Q5 的落地）

- `main.cpp`：`const bool ui_v2 = HasArgument("--ui-v2") || EnvEnabled("LAPIAN_UI_V2");`
  （`EnvEnabled` 接受 `1/true/on`，`0/false/off` 视为关；未设视为旧壳。）
- `MainWindow` 构造函数里 `if (ui_v2_enabled_) { build shell } else { build legacy layout }`。
  **两套布局互斥构建**，不共存：旧壳路径原样保留到 3D 删除。
- 3A–3C 期间：**所有门禁跑两遍**（`$env:LAPIAN_UI_V2=1` + 不设）。3D 删旧壳后开关变 no-op。

### 7.4 QSplitter 接口（Q2 的落地）

| 需求 | 实现 |
|---|---|
| 时间轴 96–340 | 面板 `setMinimumHeight(96)`/`setMaximumHeight(340)`；默认高度用 `setSizes()` |
| 画面留 ≥170 | 视频列 `setMinimumHeight(170 + Transport)` |
| 检查器 220–560 | 拖动时在 handle 的 `moveSplitter()` 里夹取（不改 min/max 宽度，避免和折叠冲突） |
| 折叠 44 | `setMaximumWidth(44)` + 横向 handle `setEnabled(false)` + 展开时恢复上次宽度 |
| 双击复位 | `PlayerSplitHandle::mouseDoubleClickEvent` → `emit resetRequested()` → `PlayerShell::ResetSplitSizes()` |
| 悬停细线 | handle `paintEvent` 画 2px accent @65%，`enterEvent/leaveEvent` 触发 `update()` |
| 光标 | QSplitter 自带 `SplitHCursor`/`SplitVCursor`（双向箭头） |
| 不抢焦点 | handle `setFocusPolicy(Qt::NoFocus)`；`setChildrenCollapsible(false)` |
| 拖拽不重绘整窗 | handle 拖拽时只 `setSizes`；面板内部 `resizeEvent` 各自处理（画面区是 HWND，见 7.5） |

### 7.5 原生 HWND 约束（4 条硬规则）

1. **控制一律在画面外**：不做浮层控制条/进度条/角标（定义 §5-P3）。唯一的"画面上的 Qt 内容"是
   `StageStack` 的 page1（空/加载/错误卡片）——它出现时 `GfxWidget` 已被切走（Q1 实测安全）。
2. **首帧顺序**：先 `stage_->ShowVideo()`（切 page0、`gfx_->show()`），**再** `gfx_->d3d_device()`；
   否则会用一个隐藏窗口的 HWND 建交换链。
3. **不要对 `GfxWidget` 做几何动画或高频 resize**：拖分隔条时它的高度每帧变，`resizeEvent` 会走
   `UpdateSwapChainSize()`→`ResizeBuffers`（`GfxWidget.cpp:90-106`）。3A/3B 各测一次"拖分隔条时 P4 帧率"，
   若 <30fps：拖拽期间把视频列改成"松手才 resize"（`setUpdatesEnabled(false)` 不够，要用占位黑底 + 松手 `resize`）。
4. **拖放**：`GfxWidget` 自己接 `dragEnter/drop`（`GfxWidget.cpp:244-267`）；空态卡片必须也 `setAcceptDrops(true)`，
   否则空态下拖文件没反应（这是 v2 新增的洞）。

### 7.6 冷启动纪律

- **不预建菜单**：顶栏 4 个按钮 + Transport 全部按钮都是普通 `QToolButton`；`清晰度`菜单仍在点击时建
  （`QPushButton::setMenu` 首绘 ~620ms 的反例，`MainWindow.cpp:494-508`）。帮助对话框、设置对话框、在线对话框
  **都在点击时构造**。
- 图标：`qrc` 里 20 个 2× PNG（每个 <1KB），`QIcon` 在首次绘制时加载；**不建 `QIcon` 缓存单例**（无必要）。
- `Theme::Apply()` 只做 palette + 一次 QSS 字符串拼接（<2ms）；不在启动时遍历控件设样式。
- 启动期不 `setStyleSheet` 到子控件（会触发 polish 风暴）；子控件只允许用 `objectName` 选择器命中全局 QSS。

### 7.7 测试可寻址 objectName 清单（新壳）

**新增（供 `--dpi-test`/`--banner-test`/`--key-test`/新单测使用）**：

| 区域 | objectName |
|---|---|
| 壳 | `v2PlayerShell`、`v2Workspace`、`v2VSplit`、`v2HSplit` |
| 顶栏 | `v2TopBar`、`v2FileIcon`、`v2FileLabel`、`v2SpecChip`、`v2ModeChip`、`onlineFpsBadge`*、`v2OpenButton`、`v2OnlineButton`、`onlineQualityButton`*、`cacheSettingsButton`*、`v2HelpButton` |
| 横幅 | `v2BannerSlot`、`v2BannerIcon`、`infoBanner`*（label 名沿用）、`infoBannerClose`* |
| 画面 | `videoStage`*（StageStack 容器）、`videoGfx`（GfxWidget）、`v2StageEmpty`、`v2EmptyTitle`、`v2EmptyOpen`、`v2EmptyOnline`、`v2StageError`、`v2ErrorReason`、`v2ErrorRetry`、`v2ErrorQuality`、`v2ErrorOpenLocal`、`v2StageLoading`、`v2LoadingProgress`、`v2LoadingCancel` |
| Transport | `v2Transport`、`v2TransportLeft`、`v2TransportReadout`、`v2TransportRight`、`v2PlayButton`、`v2StepBack`、`v2StepFwd`、`v2ToStart`、`v2ToEnd`、`v2SetInButton`、`v2SetOutButton`、`v2SpeedButton`、`v2MuteButton`、`volumeSlider`*、`v2BufferSlot`、`v2BufferText`、`v2LoopButton`、`v2WaveButton`、`v2MarkButton`、`v2SnapButton`、`v2FullScreenButton`、`v2InspectorToggle`、`v2Timecode`、`v2FrameNumber`、`v2FrameTotal` |
| 时间轴 | `v2TimelinePanel`、`v2TimelineHead`、`v2TimelineMeta`、`v2ViewMeta`、`v2ZoomIn`、`v2ZoomOut`、`v2ZoomFit`、`v2ViewBar`、`v2ViewHandleLeft`、`v2ViewHandleRight`、`v2WorkBar`、`v2WorkHandleLeft`、`v2WorkHandleRight`、`timeline`* |
| 检查器 | `v2Inspector`、`v2InspectorHead`、`v2TabMarks`、`v2TabShots`、`v2TabInfo`、`v2MarkBadge`、`v2ShotBadge`、`v2InspectorCollapse`、`v2InspectorRail`、`v2InspectorStack`、`v2ShotGrid`、`v2InfoTable`、`v2InspectorFooter`、`v2StoreText`、`markPanel`*、`markTree`*、`markLabelEdit`*、`markNoteEdit`*、`markStorage`* |
| 状态栏 | `v2StatusMessage`、`noticeInfo`*、`audioInfo`*、`cacheInfo`（新）、`waveformInfo`*、`statusInfo`* |

> `*` = **沿用旧名**（测试/日志已按名读，不改；共 17 项）。
> `timelineScrollBar` 不在清单里：3B 删除时间轴内部横向滚动条（视图窗滑块取代）。
> 已 grep 确认 `src/` 与 `tests/` 都没有按这个名字取控件（只在 `TimelineWidget.cpp:41` 设置过一次）。

### 7.8 新增测试缝（getter，全部只读、无副作用）

| getter | 返回 | 用途 |
|---|---|---|
| `ShellStateForTest()` | `tier=regular appbar=38,0,1280,38 transport=… timeline=… status=… banner=0 stage=462x1236 inspector=304 collapsed=0` | 一行看全部几何（`--dpi-test` 用它替代 `LayoutStateForTest` 的一半职责） |
| `DensityNameForTest()` | `regular/compact/dense` | 档位断言 |
| `GfxVisibleForTest()` / `StagePageForTest()` | `0/1`、`video/empty/error/loading` | Q1 的 hide/show 断言 |
| `BannerSlotHeightForTest()` / `BannerSlotGeometryForTest()` | int / QRect | 替代 `InfoBarHeightForTest` |
| `ReadoutTextForTest()` / `FrameNumberTextForTest()` | 时间码串 / 帧号串 | 主/次读数断言（M7） |
| `ReadoutGeometryForTest()` | QRect | M7 "位置不变" |
| `TimelineViewFramesForTest()` | `first,last` | 视图窗与画布同源断言（Q3） |
| `TimelinePlayheadVisibleForTest()` | bool | 播放头常驻（回归 T5.2 那类 bug） |
| `SplitterStateForTest()` | `tl=132 ins=304` | 拖拽/复位断言 |
| `SetSplitSizesForTest(int tl, int ins)` | — | 走与 handle 相同的夹取路径（96–340 / 220–560），断言极值不裁切 |
| `DragSplitterForTest(bool vertical, int delta)` | — | 合成 `QMouseEvent` 打到 handle（同 `PressTimelineForTest` 的做法），端到端验拖动 |
| `FullScreenChromeForTest()` | `appbar=0 banner=0 status=0 inspector=0 transport=1 timeline=1` | 全屏 |
| `InspectorCollapsedForTest()` | bool | 折叠 |

### 7.9 构建改动

- 新增 `src/app/ui/` 目录（10 个 .h/.cpp）+ `resources/icons.qrc`（20 个 2× PNG）+ `src/app/ui/CMakeLists` 片段。
- `CMakeLists.txt`：把 `src/app/ui/*.cpp` 加入 `lapian_app`（或现有目标），`qt_add_resources` 加图标。
  **不改**测试目标、不改打包脚本、不改 installer（图标走 qrc，不进 dist 文件数，`size-report` 增量 <30KB）。
- `check-imports.ps1` 不受影响（不引入新 DLL）。

---

## 8. P7 迁移顺序与回归清单

原则：**每阶段一个可审查 commit + 全量回归 + 双开（新旧壳）验证**；每阶段的"门"全绿才进下一阶段；
任何阶段发现要动 §6.3 的"不动"清单，**停下来先改计划**。

### 3A 壳层（4–6 人日）

做：
1. `Tokens`/`Theme`/`PlayerShell` 骨架 + `LAPIAN_UI_V2` 开关（先只搬不换：旧 `TimelineWidget`/`MarkPanel`/
   `volume_slider_`/`notice_label_`/`audio_label_`/`waveform_label_`/`status_label_`/`cache_settings_button_`/
   `quality_button_` **原对象 reparent** 进新区）。
2. `StageStack` + 空/错误/加载卡片 + `GfxWidget` hide/show（Q1）。
3. 三档密度 + `ApplyTokens()` + 分隔条（QSplitter + 自定义 handle）。
4. `objectName` + 新 getter + **改 `LayoutProblemsForTest()`/`LayoutStateForTest()`**（附录 A.1/A.2）。
5. `--startup-test` 加 `d3d_init_at_startup`；`--fullscreen-test` 加 `FullScreenChromeForTest`。

门（**新旧壳各一遍**）：
```
ctest --test-dir build\release -C Release            # 37/37
lapian.exe --dpi-test --file <1080p> --tag uiv2      # 4 DPI × {normal,fullscreen,narrow}，problems 为空
lapian.exe --banner-test ...                          # 全量 check 通过
lapian.exe --startup-test --file <1080p>              # P1/P2 + d3d 字段
lapian.exe --key-test --file <1080p>                  # 77/0（搬控件最容易破的就是它）
lapian.exe --mark-test / --note-test                  # 0 失败（MarkPanel 未改结构）
```
退出：M2（无浅色混入 + 对比度采样）、M3（DPI 无裁切）、M4（新门槛 + 令牌断言）、M7（帧号常驻）、M8（冷启动）。

### 3B 播放层（6–8 人日）

做：`TransportBar` 全量控件 + 时间码主读数 + 帧号输入（内联，`Ctrl+G` 保留）+ 缓冲槽 + 宽度降级；
`TimelinePanel` + `RangeBar`（视图窗/工作区）+ 播放头常驻/越界箭头 + 缩放下限 8px/帧 + 波形 Fill（封顶 96）+
去内部横向滚动条 + `TimelineWidget` 高度策略开关（附录 A.6）。

门：
```
--key-test 77/0         --drag-test（P4/P6/P7/P8/P9，P6 ≤150ms）
--drag-strategy-test 7/7  --loop-test / --loop-end-test
--dpi-test（含档位切换 + 拖到 340 的用例）
--online-longpress-test（Medium 副本，54/0）
T8.6.1b 35/0           --online-drag-parity-test 61/0（Medium）
```
退出：M5（17/17 实测）、M7、P6 ≤150ms、P4 不退化（"时间轴拉高/拉低"两种几何各测一次）。

### 3C 记录层（3–4 人日）

做：`InspectorPanel`（3 tab + 计数徽标 + 折叠 44 + 页脚存储）+ `MarkPanel` 内嵌（同 objectName）+
截图网格 + 折叠/聚焦联动（`FocusNoteEditorForTest` 自动展开）+ 空态置灰。

门：`--mark-test`、`--note-test`、`--snapshot-test`（T5.4）、`--key-test`、`--dpi-test`（折叠态 × 4 DPI）、ctest。
退出：M6（单横幅、不遮挡）、折叠态不破坏 mark/note 断言。

### 3D 在线 / 全屏 / 收口（5–7 人日）

做：顶栏模式 chip（本地/在线/缓存命中/解析/错误/降帧）+ 清晰度按钮与菜单（点击建）+ 缓冲槽文案；
全屏 chrome；设置/在线/扫码对话框进主题；**删除 `InfoBar` 与旧布局分支**；`LAPIAN_UI_V2` 变 no-op；
`--banner-test`/`--online-quality-test --ui` 的重新指向（附录 A.3/A.4）。

门：全量 + 在线（Medium 副本）：`--online-m1-test`、`--online-m2-test`、`--online-offline-test` 44/0、
`--online-quality-test --ui`、`--online-drag-parity-test` 61/0、`--online-qr-test` 19/0、`--online-fault-test`、
`--online-cache-test` 32/0；`dist` 体积；`--drag-test`；`ctest`。
退出：**M1–M9 全过**（M1 回归门禁一个数字都不动）+ 用户实机体感确认（记为体感，不写"验收通过"）。

### 回滚

- 3A–3C：`LAPIAN_UI_V2=0`（或不设）→ 旧壳，功能等价。
- 3D：删旧壳的 commit 单独一个，出问题整体 revert 那一个 commit 即可回到 3C 状态。

---

## 9. P8 风险与工时

### 9.1 风险表

| # | 风险 | 概率/影响 | 应对（已写进哪一节） | 验证 |
|---|---|---|---|---|
| R1 | 空态 hide/show 后首次 `Present`/`ResizeBuffers` 失败（探针只验了 HWND 身份） | 中/高 | §1-Q1 备选：改成"常显 + 1×1"或"空态不隐藏、卡片下移"；3A 第一天用真实素材验三次 | `LAPIAN_TIMING_LOG` 首次 Present 耗时 + 画面可见性 |
| R2 | 某个容器漏 `WA_StyledBackground` → 重演 1.28:1 | 高/中 | §7.2 铁律 + review checklist | M2 截屏采样（4 区域各 ≥1 处） |
| R3 | DPI 下字号/行高变化导致栏位高度漂移 | 高/中 | §4.7 第一条验收；令牌 ±2px 断言 | `--dpi-test` ×4 |
| R4 | 档位在 640/520 边界抖动 | 中/低 | §3.1 20px 迟滞 | `DragSplitterForTest` + 手动 resize |
| R5 | 搬控件破 `--key-test` 77（Space/方向键被按钮吃掉） | 中/高 | §7.7 全部按钮 `NoFocus`；焦点规则不变 | 3A 门 |
| R6 | 拖分隔条时 `ResizeBuffers` 风暴 → P4 掉帧 | 中/中 | §7.5-3：必要时"松手才 resize" | 3B 拖拽中 P4 |
| R7 | 删 `InfoBar` 破 4 个测试家族 | 高/中 | 附录 A 逐条；3A 先搬后删 | 每阶段全量 |
| R8 | `TimelineWidget` 高度策略改动破 `tests/timeline_test.cpp` 128/72 | 高/高 | 附录 A.6：默认 Fixed，v2 显式 Fill | ctest 37/37 |
| R9 | QSplitter handle 样式/双击在深色下不达预期 | 中/低 | §7.4；备选薄自绘（80 行） | 3A 截屏 |
| R10 | 全局 QSS 触发 polish 风暴 → 冷启动变慢 | 中/中 | §7.6：单份 QSS + 禁 per-widget | `--startup-test` P1 |
| R11 | 时间轴拉高后重绘面积 4×（340px），波形每帧重画 | 中/低 | 波形按像素列聚合（现有实现已是 O(宽)）；拉高时留白不放大波形 | 拖到 340 时测 UI 心跳 |
| R12 | 在线状态文案与既有证据不一致（改字导致日志 diff） | 中/低 | 文案表（§3）**不改既有句式**，只换位置；新增文案限"位置说明" | `--online-*` 日志 diff |
| R13 | 深色主题下系统文件对话框是浅色（割裂） | 高/低 | 接受（定义 §10 已定：系统对话框保持原生） | 人工 |

### 9.2 工时估算（人日，含自测；**估算不是承诺**）

| 阶段 | 内容 | 估算 |
|---|---|---|
| 3A | Theme/Tokens/Shell/StageStack/分隔条/三档/测试缝 | 4–6 |
| 3B | Transport + 读数 + 时间轴面板 + 两条滑块 + 降级 | 6–8 |
| 3C | 检查器 + 内嵌 MarkPanel + 截图网格 + 折叠联动 | 3–4 |
| 3D | 在线壳/全屏/对话框/删 InfoBar/收口 | 5–7 |
| 验收 | 4 DPI 截屏、P1/P2/P4/P6 复测、在线回归、dist/安装 | 3–4 |
| **合计** | | **21–29** |

（4 个小阶段合计 18–25 人日；把"验收"单列后总账 21–29，与 §0-13 一致。）

---

## 10. 需要你拍板的 4 件事（批准后进 3A）

| # | 决策 | 我的建议 | 若不接受，替代方案 |
|---|---|---|---|
| 1 | **画面区去掉 26px 表头**（P1-D1） | 去掉：信息不重复、+3.6pp、200% 下才能过 55% | 保留：M4 只能改成 60.6/60.6/51.8/56.4——200% 会低于定义里的 55%，且 100% 只比 200% 好 11pp |
| 2 | **M4 新门槛 62/62/55/59**（§4.4） | 接受新门槛 + "每栏 ±2px"硬断言 | 收紧默认（时间轴 96）→ 100% 回到 69.2%，但波形只剩 ~10px |
| 3 | **三档密度按客户区高度自动取档**（§3.1） | 接受：150% DPI 不倒退的前提 | 只按 DPI 分档 → 窗口被用户缩小到 500 高时回到"挤扁" |
| 4 | **QSplitter + 自定义 handle**（Q2） | 接受 | 自绘 splitter（+1–1.5 人日，可控但重复造轮子） |

另外三条**不需要拍板但要知会**：顶栏去掉品牌区与汉堡菜单（P1-D2）；`text-mute` 降级为禁用色、新增
`text-meta`（§4.2）；`LAPIAN_UI_V2` 环境变量（Q5）。

---

## 11. 附录

### A. 测试耦合的逐条改法（硬交付）

#### A.1 `LayoutProblemsForTest()`（`MainWindow.cpp:971-1131`）

| 现有检查 | 处置 |
|---|---|
| `fullscreen-size` / `window-outside-screen` / `min-width-exceeds-screen` / `min-height-exceeds-screen` | 保留 |
| `missing-widgets` | 保留，判据从 `stage_/mark_panel_` 扩到 `shell_/stage_/transport_/timeline_/inspector_` |
| `stage-outside-central` / `panel-outside-central` / `stage-panel-overlap` | 保留（判据换成新区域） |
| `panel-width`（240–300） | **替换** `inspector-width`：展开 220–560，折叠 44±1 |
| `stage-too-narrow`（<420） | 保留（画面列宽 ≥420） |
| `gfx-too-small`（<320×120） | 保留（HWND 下限不变） |
| `timeline-too-small`（<72） | **改为 `<96`**；新增 `timeline-too-tall`（>342，仅用户未拖拽时） |
| `info-bar-too-small`（<30） | **删除** |
| `info-label-outside` / `info-label-narrow` / `info-label-overlap` / `file-label-clipped` | **删除**（随 InfoBar） |
| `banner-clipped` / `banner-text-clipped` | 保留判据但改实现：单行 elide 后 `elided == 原文` 时视为未裁切；换行不再允许 |
| `banner-overlaps-frame` | **删除**；新增 `banner-overlaps-readout`（banner 在 workspace 之上，理论上永不重叠 → 防回归） |
| `banner-overlaps-timeline` / `banner-overlaps-status` | 保留 |
| `status-widget-outside/narrow/overlap` | 保留，列表从 7 个减到 4 个（`noticeInfo/audioInfo/cacheInfo/waveformInfo` + `statusInfo`） |
| — | **新增** `appbar-height`（\|appbar−令牌\|>2）、`transport-height`、`timeline-height`、`status-height`、`stage-height`（\|画面−(H−固定合计)\|>2）、`splitter-width`（≠6±1）、`density-unknown` |

#### A.2 `LayoutStateForTest()`（`MainWindow.cpp:909-969`）

保留全部旧键（`dpr/window/frame/available/min/stage/panel/gfx/timeline/status`），**新增**：
`tier=`、`appbar=`、`transport=`、`banner=`、`inspector=`、`readout=`、`playhead_visible=`。
`info=` 与 `info_<label>=` 键在 3A 保留为 `banner=` 的别名输出，3D 删除（避免一次断掉旧日志对比）。
`InfoFileLabelForTest()`/`InfoFileNameForTest()`/`InfoNarrowModeForTest()` **方法名不改**，内部改读顶栏
（elide 逻辑搬到 `TopBar::UpdateFileText()`，行为与 `InfoBar::UpdateFileText()` 一致：`ElideMiddle` + tooltip 全名）。

#### A.3 `BannerStateForTest()` / `BannerUiStateForTest()`（`MainWindow.cpp:850-881`）

`BannerStateForTest()` 保留键与顺序：`kind= visible= banners= text= banner=x,y wxh info_h= secondary= active=`。
- `banners=` 仍用 `findChildren<QLabel*>("infoBanner").size()`（BannerSlot 里的 label 沿用 `infoBanner` 名）→ 恒为 1。
- `banner=` 改成 `BannerSlot` 内 label 的几何（映射到 BannerSlot 坐标）。
- `info_h=` 改为 `BannerSlot` 高度（0 或令牌）。**名字不改**，避免改 ~20 处断言；3D 时可加注释说明语义已变。
- `BannerUiStateForTest()` 的 6 个键：
  - `timeline=%1`：`timeline_->isVisible()`（不变）
  - `frame=%1`：`InfoFrameTextForTest()` 继续返回 `帧 N`（由 Transport 副行拼出 `帧 203`），断言 `frame=帧` 不变
  - `play=%1`：`play_action_->isEnabled()`（不变）
  - `volume=%1`：`volume_slider_->isVisible()`（**对象不变**，只是从状态栏搬到 Transport）
  - `loop=%1`：改为 `loop_toggle_button_->isVisible()`（原 `loop_label_->isVisible()`）；**这是唯一需要动的 1 行**
  - `notice=%1`：`notice_label_->isVisible()`（不变）

#### A.4 `--online-quality-test --ui` / `--online-vip-test`

断言**一行不改**（`OnlineQualityTestRunner.cpp:884-949` 等全部通过 `MainWindow` getter 访问）。只改 4 个 getter 的内部指向：

| getter | 现状 | 改为 |
|---|---|---|
| `OnlineFpsBadgeTextForTest()` / `OnlineFpsBadgeVisibleForTest()` | `info_bar_->online_fps_badge_` | 顶栏 `v2ModeChip` 内的 `onlineFpsBadge`（同一 QLabel 对象 reparent 过去，**状态逻辑不动**） |
| `OnlineFrameRateBannerVisibleForTest()` | `info_bar_->BannerVisibleForTest()` | `shell_->banner()->IsVisibleKind(kFrameRateDrop)` |
| `OnlineFrameRateBannerCloseVisibleForTest()` | `info_bar_->` | 同上（BannerSlot 的关闭按钮可见性） |
| `DismissOnlineFrameRateBannerForTest()` | `info_bar_->` | `shell_->banner()->Dismiss()`（语义不变：只关当前降帧横幅，角标保留） |
| `OnlineQualityButtonVisibleForTest()` | `quality_button_->isVisible()` | 不变（对象 reparent 到顶栏） |

#### A.5 `--mark-test` / `--note-test`

- **不改**：两者只用 `MarkStateForTest()`/`MarkCountForTest()`/`MarkTextForTest()`/`AddMarkForTest()`/
  `ActivateMarkForTest()`/`DeleteMarkForTest()`/`SetMarkNoteForTest()`/`FocusNoteEditorForTest()`（均走
  `MainWindow`/`MarkPanel` 公有 API，**不读 objectName**）。要求：`MarkPanel` 实例保持同一对象、
  `markTree/markLabelEdit/markNoteEdit/markStorage` 名字不变、折叠时**不销毁**（`setVisible(false)`）。
- **新增 2 条**（3C）：① 折叠检查器后 `MarkCountForTest()` 与选中 id 不变；② `FocusNoteEditorForTest()` 在折叠态
  会自动展开检查器（新行为，写进计划而非悄悄实现）。

#### A.6 `tests/timeline_test.cpp`（ctest 内，**最容易踩**）

现状断言：`SetWaveformSource` 后 `widget.height() == 128`（72+56）、`SetWaveformVisible(false)` 后 `== 72`、
`ClearWaveform()` 后 `== 72`（`tests/timeline_test.cpp:237/258/261`）。
→ **`TimelineWidget` 的高度策略必须可切换**：新增 `SetHeightPolicy(Fixed|Fill)`，默认 `Fixed`（行为与今天完全一致，
单测不动）；`TimelinePanel` 用 `Fill`（`Expanding/Expanding`、`RulerHeight()`/`WaveformHeight()` 从 `height()` 算、
波形封顶 96）。若直接改默认值，ctest 37/37 必挂。

#### A.7 `--dpi-test` 阈值汇总（旧 → 新）

| 断言 | 旧 | 新 |
|---|---|---|
| stage 宽 | ≥420 | ≥420 |
| gfx | ≥320×120 | ≥320×120 |
| timeline 高 | ≥72 | ≥96 |
| info 高 | ≥30 | **删除**（改 banner 槽 0 或 24–32） |
| status 高 | ≥20 | ≥20 |
| panel 宽 | 240–300 | inspector 220–560 / 折叠 44±1 |
| 画面占比 | — | 新 M4（62/62/55/59） |
| 每栏高度 | — | 令牌 ±2 |
| 相位 | normal/fullscreen/narrow | 不变（**建议加第 4 相位**：`tl=340` 与 `ins=560`，验"拖到极值不裁切"） |

#### A.8 `--startup-test` / `--fullscreen-test` 增量

- `--startup-test`：新增 `d3d_init_at_startup=%d`（0 = 空态隐藏生效）、`p1_ms`、`p2_ms` 原样打印。**不改任何阈值**。
- `--fullscreen-test`：新增 `chrome=%s`（`FullScreenChromeForTest()`），断言"顶栏/横幅/状态栏/检查器隐藏、
  Transport 与时间轴仍在"。原 `IsFullScreenForTest` 断言不动。

### B. 与原型/定义文档的差异清单（逐条可查）

| 项 | 原型/定义 | 本计划 | 原因 |
|---|---|---|---|
| 画面区表头 | 26px "合成 1" 表头 | **去掉** | 信息重复、3.6pp、200% 门槛（§2.4-D1、§4.4） |
| 顶栏品牌区/汉堡菜单 | 有 | **去掉** | 应用名在标题栏；不加菜单（§2.4-D2） |
| 密度档 | 2 档（舒适/紧凑） | **3 档**（regular/compact/dense，按高度自动） | 150%/200% 不倒退（§3.1） |
| 时间轴缩放下限 | 8 帧 | **画布宽/8**（1 帧 8px） | spec F2（§1-Q3） |
| M4 门槛 | 68/64/55/60 | **62/62/55/59** + 令牌断言 | §4.4 |
| `InfoBar` | 存在 | **删除**，语义进 BannerSlot | §1-Q4 |
| 状态栏 9 个 widget | 9 | **1 条消息 + 3 指标** | 定义 §11-Q8 |
| `text-mute` 承载元信息 | 3.6:1 | 新增 `text-meta`（5.2:1） | 定义 §6.1-M2 |
| 检查器默认 | 展开 304 | 空态折叠；有媒体且宽度 ≥1000 展开一次 | 定义 §11-Q2（收窄成"只自动决定一次"） |
| 时间轴横向滚动条 | 有（14px） | **去掉**（视图窗滑块取代） | §1-Q3（少一条重复的导航） |

### C. 证据与复现命令

```powershell
cd 'E:\拉片软件开发'

# 1) 原型自检（应打印 playhead_offview=0，无 ERR:）
$edge='C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$uri = ([System.Uri]((Resolve-Path 'docs\ui-v2-proto\index.html').Path)).AbsoluteUri
& $edge --headless=new --disable-gpu --force-device-scale-factor=1 --virtual-time-budget=5000 `
  --dump-dom "$uri?smoke=1" | Select-String 'id="smoke"'

# 2) 原型几何（换尺寸看档位；注意浏览器视口 ≠ --window-size，先看 #debug 的 appbar 高度反推）
& $edge --headless=new --disable-gpu --force-device-scale-factor=1 --window-size=1280,871 `
  --virtual-time-budget=5000 --dump-dom "$uri?debug=1&density=comfortable" | Select-String 'id="debug"'

# 3) 三屏截图（落到 build/，不入库）
& $edge --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=1 `
  --window-size=1280,871 --screenshot=build\ui-v2-empty.png "$uri?mode=empty&hdr=0"
# 同法：mode=paused / mode=buffering / mode=error / mode=paused&density=dense

# 4) 新壳回归（3A 起；两条都跑）
$env:LAPIAN_UI_V2='1'; .\build\release\bin\lapian.exe --dpi-test --file media\测试.mp4 --tag uiv2
Remove-Item Env:LAPIAN_UI_V2; .\build\release\bin\lapian.exe --dpi-test --file media\测试.mp4 --tag legacy

# 5) 在线回归只用 Medium 副本（工作区直接跑是 Low integrity，会写不进 %APPDATA%）
& "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe" --online-m1-test ...
```

**本轮实测记录（可复现）**：

| 证据 | 值 | 方式 |
|---|---|---|
| HWND hide/show 是否重建 | 不重建（`hwnd_same_after_cycle=1`、`stack_hwnd_same=1`） | `build/hwnd-probe/probe.cpp`（Qt 6.8.3 + MSVC，一次性探针，不入库） |
| 原型 smoke | `playhead_offview=0`、`drag_tl=40`、`drag_ins=60`、无 `ERR:` | `?smoke=1` dump-dom |
| 原型短高度缺陷（修前） | 928×452 → `.panel.video=19px`；820×500 → `67px` | `?debug=1` dump-dom |
| 三档修复后 | 452 → 228（dense，无表头）；500 → 276；720 → 438（含表头） | 同上（fresh profile） |

> 这些是**原型/探针**数字，不是产品数字。产品数字只在 3A–3D 的回归里产生，且必须带命令与日志。

### D. 本文件未覆盖（进 3A 前的待办，不阻塞批准）

1. 4 DPI 的**基线截屏重拍**（定义 §9.3 欠账）：必须用 Medium 副本，否则红色"缓存目录不可用"横幅是环境噪声。
2. 全屏相位 `IsFullScreenForTest` 的 flake（定义 §9.2）——3A 重跑，若仍不稳定，把该相位改为独立进程。
3. 图标集的最终样式（20 个 16px 图标）：3A 里按"能认出来 + 与 tooltip 一致"验收，不做美术评审。
4. `RangeBar` 是否需要键盘操作（方向键微调 A/B）：**本轮不做**（不在 spec §3.7 里），进候选池。
