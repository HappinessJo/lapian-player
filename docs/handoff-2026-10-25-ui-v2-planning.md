# 交接：UI v2 规划阶段（2026-10-25）

- **状态**：定义阶段 + 四轮原型讨论**已收口**；规划阶段**未开始**。
- **产品代码基线**：`352b29f`（P0 发布收口）；**本轮系列未改一行产品代码**。
- **文档 HEAD**：`0564817`（`docs: close UI v2 prototype discussion round`）。
- **上一阶段产物**：
  - 定义：[ui-v2-definition.md](ui-v2-definition.md)
  - 线框：[ui-v2-wireframes.md](ui-v2-wireframes.md)
  - 讨论记录（四轮 + 收口）：[ui-v2-feedback-2026-10-25.md](ui-v2-feedback-2026-10-25.md)
  - **可点原型（已入库）**：[ui-v2-proto/index.html](ui-v2-proto/index.html) + [说明](ui-v2-proto/NOTES.md)
- **提示词**：[next-session-prompt-ui-v2-plan.md](next-session-prompt-ui-v2-plan.md)

---

## 0. 一页结论

1. UI v2 = **重做壳（布局/视觉/信息架构），不动引擎**：键位、0 基帧号、
   点击即暂停、拖动降质 + 松手精确、M1/M2/P4/P6/P7 门槛与数字全部不变。
2. 骨架：**顶栏 + 画面 + Transport + 时间轴 + 右侧可折叠检查器 + 状态栏**，
   保留系统标题栏，不做无边框。
3. 视觉：**深色单主题**，蓝 `#5b8def` 为强调色；状态用颜色+文字双编码。
4. 主读数：**时间码 `时:分:秒:帧`**（26px 等宽），帧号退到下一行。
5. 时间轴：**上方视图滑块**（全片导航）+ **下方工作区滑块**（循环区间，与刻度同轴）；
   工作区默认全片、循环默认开。
6. 三个版块**可拖拽调整尺寸**（画面/时间轴/检查器），双击分隔条恢复默认。
7. 播放头**任何情况下不消失**；时间轴光标是普通箭头。
8. 原型已验证的方向 = 规划的事实基础；**规划要产出的是"能落地到 Qt 的规格"，不是再讨论要不要这样**。

---

## 1. 必读（按顺序）

1. [AGENTS.md](../AGENTS.md) §1 当前状态、§9 已知坑、§11 协作规则、§12 文档。
2. [ui-v2-definition.md](ui-v2-definition.md)：范围/原则/成功指标 M1–M9/约束/测试耦合清单/屏幕状态清单。
3. [ui-v2-feedback-2026-10-25.md](ui-v2-feedback-2026-10-25.md)：四轮反馈与定稿口径（**最新口径以它为准**）。
4. [ui-v2-proto/NOTES.md](ui-v2-proto/NOTES.md)：原型能点什么、URL 参数、与真实产品的差距。
5. 代码现状（只读，不改）：
   `src/app/MainWindow.cpp:150-521`（布局/状态栏/action）、
   `src/app/MainWindow.cpp:782-1030`（窗口尺寸/响应式/LayoutProblemsForTest 阈值）、
   `src/app/GfxWidget.h`（独立 HWND）、`src/app/InfoBar.cpp:33-37`（样式表背景未生效的证据）、
   `src/app/MarkPanel.cpp:61-68`（WA_StyledBackground 正例）、
   `src/app/TimelineWidget.{h,cpp}`（现有绘制/命中语义）。
6. 性能/回归基线：[perf/online-drag-parity.md](perf/online-drag-parity.md) §13.8、
   [handoff-2026-10-25-p0-close.md](handoff-2026-10-25-p0-close.md)。

---

## 2. 规划阶段要产出什么

| # | 产物 | 验收口径 |
|---|---|---|
| P1 | 信息架构定稿 | 每个控件归哪个区、哪条信息只出现在一处；与原型一致或说明差异 |
| P2 | 屏幕/状态矩阵 | 空、本地播放/暂停、循环、标记编辑、全屏、在线解析/加载/缓冲/就绪/缓存命中/错误/切档/扫码、设置、DPI 100/125/150/200% 与窄窗口 820×500 |
| P3 | 设计令牌 | 间距、字号阶梯、控件高度、语义色（就绪/缓冲/错误/缓存命中）、focus ring；**必须重算高度预算**（见 §4.1） |
| P4 | 3 屏高保真或可点原型 | 空状态、本地主界面、在线缓冲/错误；可直接改现有 HTML 原型 |
| P5 | 组件清单与复用边界 | 哪些新写（PlayerShell/TopBar/Transport/BannerSlot/StatusBar/Theme）、哪些包一层（MarkPanel/TimelineWidget）、哪些不动（GfxWidget 呈现/PreviewDecoder/DragStrategy/OnlineSession） |
| P6 | 技术拆分 | `PlayerShell`、`Theme`、`--ui-v2` 开关、QSplitter 还是自绘拖拽、原生 HWND 约束、**测试可寻址的 objectName 清单** |
| P7 | 迁移顺序与回归 | 3A 壳层 → 3B 播放层 → 3C 记录层 → 3D 在线/全屏/收口；每阶段的回归清单 |
| P8 | 风险与工时 | 至少覆盖：空态隐藏 HWND 的 P1/P2 风险、主题坑、测试阈值改动、DPI 重排 |

**退出条件**：用户批准 P2/P3/P4 与 P7 迁移顺序；**仍不写产品代码**（可改原型）。

---

## 3. 已定口径（不要再讨论）

1. 范围 = 布局 + 视觉 + 信息架构；交互模型与键位不改（`spec.md` §3.7）。
2. 深色单主题；强调色蓝；状态语义色 + 文字双编码。
3. 时间码为主读数（`00:00:06:23`），帧号次级（默认**不补零**，`203`）。
4. 时间轴两条滑块：上=视图窗（全片导航），下=工作区（循环区间，与刻度同轴）；
   工作区默认 `A=0 / B=末帧 / 循环开`；循环开关在 Transport 波形旁。
5. 播放头常驻：跳帧自动滚视图、越界贴边画箭头；时间轴光标默认箭头。
6. 版块可拖：时间轴顶边 96–340px（画面留 ≥170px）；检查器 220–560px（画面留 ≥520px）；
   双击恢复默认（132 / 304）。
7. Transport 按**自身宽度**降级（不是按视口）：<1180 收音量、<1000 收 A/B 与循环数值。
8. 时间轴拉高时波形封顶 96px，其余留白（给多轨/标记泳道）。
9. 空状态：隐藏 `GfxWidget` 用 Qt 占位卡片（**此条仍需微基准验证 P1/P2**，见 §5）。

### 3.1 原型里用的默认值（规划可沿用，要改需说明）

| 项 | 默认 | 备注 |
|---|---|---|
| 帧号格式 | `203`（不补零） | 若改补零，只影响格式化 |
| 工作区数值 | 时间轴标题栏 `工作区 00:00:33:10–00:01:26:20 · 循环开` | 不在滑块两端常显 |
| 检查器 | 默认展开 304px；可折叠 44px | 折叠时禁用分隔条 |
| 顶栏按钮 | "打开/在线"图标 + 文字 + 快捷键角标；设置/帮助纯图标 | 可切纯图标 |
| 波形色 | 蓝 `#6f9df0`（随强调色微调） | 也可改中性灰 |

---

## 4. 约束（规划必须写进文档）

### 4.1 高度预算要**重算**（定义文档 §7.2 已过期）

定义阶段按"时间轴 112px"算过一版预算（顶栏 36 / Transport 40 / 时间轴 112 / 状态 24，
合计 212；M4：100/125% ≥68%、150% ≥64%、200% ≥55%、窄 ≥60%）。
**原型落地后不再成立**：顶栏 38、Transport 46、新增两条滑块 28、
时间轴默认 132（96–340 可拖）、状态 24。

按原型令牌重算（1280×720 客户区）：
`720 − (38+46+132+24+28+≈12 边距) ≈ 440`，画面占高 **≈61%**，
**低于原 M4 的 68%**。规划阶段必须在下面三条里做选择并写清理由：

- (a) 收紧默认：时间轴回落到 ~112、Transport 40、顶栏 36 → 约 68%；
- (b) 保留 132 默认，把 M4 门槛调到 ~60–62%（并说明为什么画面让位给时间轴）；
- (c) 做成"记住用户拖过的尺寸"，首启用紧凑默认、之后按用户习惯。

**不要照抄 68%**，也不要不改门槛就改布局。

### 4.2 硬约束

1. 视频区是独立 HWND（`WA_NativeWindow` / `WA_PaintOnScreen`）——控制一律在画面外；
   空态/错误态要遮住画面得先隐藏 HWND（P1/P2 需实测）。
2. Qt Widgets + 冷启动：不引入 QML/Electron；不预建菜单、不预热对话框
   （已知反例：`QPushButton::setMenu()` 首次绘制阻塞 ~620ms，现用"点击时建菜单"）。
3. 键位/帧语义是契约（`spec.md` §3.7）；改键位要单独走 spec 版本记录。
4. 性能路径不能碰：`GfxWidget` 呈现、`PreviewDecoder`/`DragStrategy`、
   `OnlineSession` 状态机、`CacheStore`。
5. 回归门禁不随 UI 改：`ctest 37/37`、P0 parity `61/0`、T8.6.1b `35/0`、
   key `77/0`、A `42/0`、local `9/0`、`--drag-test` P6 ≤150ms。

### 4.3 测试耦合（定义文档 §7.1 已列，规划要落到具体改法）

`--dpi-test` 的 `LayoutProblemsForTest()` 阈值（stage ≥420、gfx ≥320×120、
timeline ≥72、info ≥30、status ≥20、panel 240–300）会因骨架变化失效；
`--banner-test` 依赖 InfoBar 内部结构；`--mark-test`/`--note-test` 依赖 MarkPanel
的 objectName；`--online-quality-test --ui` 依赖清晰度按钮位置。
规划必须给出：**哪些阈值改成什么、哪些 objectName 保持不变、新增哪些测试缝**。

---

## 5. 规划阶段必须先回答的 5 个问题

1. **空态是否隐藏 `GfxWidget`？** 隐藏最简单，但可能触发交换链重建影响 P1/P2；
   需一次微基准，或改走"占位卡片放在画面外"的备选。
2. **可拖版块用 `QSplitter` 还是自绘？** QSplitter 省事但与深色主题/像素级间距要调；
   自绘可控但要自己处理光标、双击复位、键盘焦点。
3. **时间轴两套坐标**（视图窗=全片、工作区/画布=可见范围）在 Qt 里怎么组织？
   原型是 DOM+canvas 两套百分比；Qt 里要明确谁是 source of truth（`TimelineModel` 不动）。
4. **信息条拆解后 `InfoBar` 怎么处置？** 建议删除，文件名/规格进顶栏、
   帧号/时间码进 Transport、提示进 banner 槽；需列出受影响的测试。
5. **`--ui-v2` 开关形态？** 环境变量 vs 设置项；建议先用环境变量（对测试友好），
   旧壳保留到 3D 收口后删除。

---

## 6. 环境与命令

```powershell
cd 'E:\拉片软件开发'
# 打开原型（双击也行）
start docs\ui-v2-proto\index.html

# 无头截图（改完原型自己验一眼）
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$uri  = ([System.Uri]((Resolve-Path 'docs\ui-v2-proto\index.html').Path)).AbsoluteUri
& $edge --headless=new --disable-gpu --hide-scrollbars --window-size=1600,1000 `
  --force-device-scale-factor=1 --screenshot=build\proto-check.png "$uri?work=1000,2600&tl=220&ins=430"

# 原型自检（把结果写进 DOM 的 #smoke）
& $edge --headless=new --disable-gpu --virtual-time-budget=5000 --dump-dom "$uri?smoke=1"
```

- 回归/性能仍按 P0 交接的命令：`ctest --test-dir build\release -C Release`、
  `--key-test`、`--drag-test`、`--online-drag-parity-test`（在线只用 Medium 副本）。
- 基线截屏仍在 `build/ui-baseline/`（不入库；若被清掉，用 `--dpi-test` 重拍，
  注意**必须带 `--file`**，否则 GUI 定时器不启动）。

---

## 7. 坑（这轮踩过的，别重复）

1. **`PrintWindow` 遇到活动 D3D11 子窗口会挂起**；抓真实画面用屏幕抓取，且窗口要置顶。
2. `QWidget::grab()` 抓不到视频区（独立 HWND），DPI 截图的视频区永远是黑的。
3. 工作区/`--goto` 在文件刚打开时不生效（索引未就绪）；取帧改用
   `ui-v2-proto/wpf-frame.ps1`（WPF MediaPlayer，原生 1920×1080）。
4. **`InfoBar` 的样式表背景没生效**（缺 `WA_StyledBackground`），实测对比度
   1.28:1——新壳所有自绘容器都要显式设，否则重演。
5. 宽度过渡（折叠检查器）期间重绘会错位；原型用 `ResizeObserver` 兜底，
   Qt 里对应的是 `resizeEvent`/布局时机。
6. 原型是 HTML，不是 Qt：字体度量、滚动条、DPI 缩放都会变，规划别把像素当承诺。

---

## 8. 下一步

1. 规划会话：写 `docs/ui-v2-plan.md`（P1–P8），仍不写产品代码。
2. 用户批准后：3A 壳层（`PlayerShell`/`Theme`/空状态/对话框）→ 3B 播放层 →
   3C 记录层 → 3D 在线/全屏/收口，每阶段一个可审查 commit + 全量回归。
