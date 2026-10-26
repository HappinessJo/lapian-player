# 交接：在线打开预热 + 缓存命中拖动软解 + 时间轴边缘自动平移（2026-10-26）

- **状态**：实现（C1 自测）+ C2–C6 独立验收（**C6 verdict=pass with gaps**）+ 发布收口完成；
  安装版冒烟核心项全绿（T8.6.1b 本轮 35/0，但**基线为 35/1**，不写“已修好”）。**非门**，不改
  spec 验收值、不改 M1/M2 门、不改缓存 schema、不改安装器形态。
- **代码提交**（本轮只加 docs 提交，`src/`/`tests/` 一行未改）：
  - `4ae315a fix(online): prewarm video stage before cache-hit open`（2 文件，+62/−3）
  - `c04a180 fix(online): use software preview for cached DASH drag`（5 文件，+115/−7）
  - `6daa341 feat(timeline): auto-pan playhead drag at zoomed edges`（3 文件，+681/−12）
  - 上一发布 docs `a5a6386`；本轮文档提交见 `git log` 顶部（只含 `docs/` + `AGENTS.md`）。
- **上游**：[spec.md](../spec.md) v0.4 §4.1（P4 ≥30fps、P6 ≤150ms、P7 ≤80ms、P9 ≤200ms；
  P6/P9 以目标距前关键帧 ≤2s 为前提）、[T8.6.1b 交接](handoff-2026-10-24-t8.6.1b.md)、
  [在线拖动 perf](perf/online-drag-parity.md)（§13、§13.8）、
  [缓存命中修复交接](handoff-2026-10-26-cache-hit-fix.md)。
- **用户两个新诉求**：① 在线打开（尤其缓存命中）点确认后“已就绪又卡一下”，希望把那一下挪到
  “加载中”阶段；② 缓存命中打开后拖动只有 ~9–16fps，比首次在线慢很多；③ 时间轴放大后拖动
  播放头到窗口边缘时视图不跟随，需要手动滚轮（与用户“拖到边缘自动平移”的 AE 习惯不一致）。
- **发布记录**：[release-2026-10-26-cache-hit-open-drag.md](release-2026-10-26-cache-hit-open-drag.md)。

---

## 1. 一句话结论

三件事都是“把已有成本/路径放到正确位置”，没有新增功能面：

1. **打开预热**：`4ae315a` 把首次 `ShowVideo()` 触发的 D3D11 惰性初始化（本机 142–145ms）
   从 `ApplyOnlineReady` 之后提前到“用户点确认 / 解析开始”时付掉——`stage_show_ms` **145 → 0**、
   `wait_ready` **243 → 82–90ms**；**click→ready 总量基本不变**（中位 243ms，+3ms，max +13ms）。
   145ms 是**移到加载中**，不是消失；**不要写“打开不再卡”**。
2. **缓存命中拖动改软解**：`c04a180` 在 `online_cache_hit_ && session_->IsGrowing()`（DASH 缓存
   命中 + growing 会话）时给预览解码器传 `prefer_hardware=false`——720P 反向 **16–17/30 → 27/30
   （27fps）**、1080P **9/30 → 27–28/30（27–28fps）**，正向 fps_active 60/62 不变，
   `hw_requested=0/hw_active=0`、`playurl/dash=0`。durl 缓存命中/本地/增长在线仍硬解优先。
3. **时间轴边缘自动平移**：`6daa341` 在“scrub 拖动 + 已放大（MaxScroll()>0 且非 fit）”时，
   鼠标进入左右 10px 边缘就以 16ms PreciseTimer 按 `min(28, 6+0.35*overshoot)` 逻辑像素滚动，
   并修掉“一次拖到画布外先跳到画布外帧、再被拉回”的回跳。**fit/缩到最小绝不滚动**；
   widget 级测试 **188 OK / 0 fail**。**只有 widget 级证据，没有 MainWindow/真实鼠标/多 DPI 集成探针。**

## 2. C4-1 在线打开预热（`4ae315a`）

### 2.1 根因

在线打开（含缓存命中）原来只在 `ApplyOnlineReady` 里第一次 `ShowVideo()`；这一步触发
`GfxWidget` 的 D3D11 惰性初始化（`d3d_init_at_startup=0`，本机 145–200ms）。用户看到的是
“解析/就绪提示已经出现过，画面页一切换又卡一下”，且这段成本落在 `online_ready` 之后，
把 `wait_ready` 推到 243ms。

### 2.2 修复

| 位置 | 内容 |
|---|---|
| `src/app/MainWindow.cpp/.h` | 新增 `PrewarmOnlineVideoStage()`：只 `stage_stack_->ShowVideo()` + `gfx_->d3d_device()`，**不打开媒体、不读缓存**；记录 `online_prewarm_stage_ms_` 并按 `LAPIAN_ONLINE_LOG` 打印 `online_prewarm_stage_ms=… first=… stage=…`；`T8.9 online_ready stage_show_ms=… prewarm_stage_ms=…` 追加预热值 |
| `MainWindow::ShowOnlineDialog` | 用户点确认、对话框 `hide()` **之前**预热一次 |
| `MainWindow::BeginOnlineRequest` | `online_->Start()` **之前**再调一次（覆盖 CLI/测试/程序化入口；设备已存在时只返回几条指令） |
| 失败回退 | 预热把画面页从空态切走后 `Start` 同步失败 → `ShowEmpty()` 恢复空态卡片，错误横幅/状态栏语义不变 |

- 空态/本地打开**绝不调用**预热（`--startup-test`、`--dpi-test --empty` 仍 `d3d_init_at_startup=0`）。

### 2.3 数字（隔离缓存 + Medium/安装版；`LAPIAN_ONLINE_LOG=1`）

| 指标 | 修前 | 修后 |
|---|---|---|
| `online_prewarm_stage_ms` | —（无此段） | **142–145**（C1）/ **143–147**（C3/安装版）`first=1 stage=video` |
| `T8.9 online_ready stage_show_ms` | **145** | **0** |
| `wait_ready`（`pump_ms/total_ms`） | **243** | **82–90ms**（安装版 91ms） |
| `online_start_ui`（含 prewarm） | 145 | **153–167ms**（安装版 157ms） |
| click→ready 总量 | 中位 243ms | 中位 **243ms**（+3ms，max +13ms）——**基本不变** |
| 二次打开/切档 | — | `prewarm=0 first=0`、`online_begin_done` 7–8ms（安装版） |

- 空态/本地不预热：`--startup-test`（无文件）与 `--dpi-test --empty` 均 `d3d_init_at_startup=0`，
  且日志中没有 `online_prewarm_stage_ms` 行。
- 失败路径（坏缓存根）：**明确错误、非零退出、无崩溃**——C1 记录 `prewarm=129ms → Start 失败 →
  错误文本正常、无崩溃`；C7 安装版用 `Z:\lapian-bad-cache-root` 复核：`--online-offline-test` 打印
  `在线缓存目录不可用：无法创建缓存目录：Z:/lapian-bad-cache-root`、11 项里 6 项 fail、**exit 2**、
  无崩溃；`--online-drag-preview-test --drag-cache-hit` 同根退出 2（detail
  `缓存命中状态不符：expect=1 actual=0`）。**实际退出码是 2，不是 1**（探针约定），不美化。
- `--online-fault-test`：**92/0**（10/10 场景、`ui_gap=154–155ms`、`unverified=1` 真实休眠）。

### 2.4 表述边界（写文档时必须照抄）

- 145ms 是“**移到加载中（点确认/解析阶段）**”，不是消失；**click→ready 总量基本不变**。
- 不写“打开不再卡”“打开变快 145ms”——用户可感知的只是“就绪后不再二次卡”。
- 预热只在**在线**入口生效；本地打开/空态没有预热，也没有变慢。

## 3. C4-2 缓存命中拖动预览改软解（`c04a180`）

### 3.1 条件

```cpp
// MainWindow::UseSoftwarePreviewForCacheHit()
return online_mode_ && online_cache_hit_ && session_ != nullptr && session_->IsGrowing();
```

- 只对 **DASH 缓存命中（growing 会话）**生效，`EnsureCacheHitPreviewDecoder()` 与
  `EnsurePreviewDecoderDevice()`（设备重建）两处都按同一口径传 `prefer_hardware`。
- durl 缓存命中（本地会话）、本地文件、首次在线增长会话**仍硬解优先**；
  未改 `PreviewDecoder`/`GfxWidget`/`MediaSession`/`VideoDecoder` 语义，只改 `MainWindow` 传参。
- 根因：这条素材的硬解预览反向只有 9–16fps（每次从 K 重解 + 硬解小队列），软解路径
  （与首次在线 `OnlinePreviewDecoder` 同族）能到 27fps。

### 3.2 数字（1s/60 默认 A 窗口，30 个反向目标；隔离 cache）

| 场景 | 修前（硬解） | 修后（软解） |
|---|---|---|
| 720P（`BV1cW411b7Yh`/`28925848` qn=64、29.412fps） | 反向 **16–17/30**、16–17fps | 反向 **27/30**、**27fps**；正向 `fps_active≈60` |
| 1080P（OB-1 `BV1BMnv6jE5S`/`42915922022` 80-7） | 反向 **9/30**、8.8–8.9fps | 反向 **27–28/30**、**27–28fps**；正向 `fps_active≈62` |
| 首次在线 T8.6.1b 反向（对照） | — | 28/30、27.97fps（缓存命中与它差 **≤3.6–5.6%**） |
| 硬解请求 | `hw_requested=1` | **`hw_requested=0 / hw_active=0`**、`fallback=software_requested` |
| 网络 | — | `playurl=0 / dash=0`（缓存命中打开 0 网络） |

- **P6（松手→精确落帧）**：默认 1s/60 窗口的目标距前关键帧 >2s（720P 2550ms、1080P 2500ms），
  按 spec 前提只记录：720P **52–54ms**、1080P **82–85ms**（安装版）。
- **保证档 P6（目标距前关键帧 ≤2s）**：
  - 720P `--drag-start-frame 145`（145→205，距 K150 55 帧/1870ms）：**39ms**（安装版 37ms）
  - 1080P `--drag-start-frame 150`（150→210，60 帧/2000ms）：**66ms**（安装版 73ms）
  - 均 ≤150ms。
- **硬门** `cache_hit_backward_presented_ratio_ge_70pct` 只在“cache-hit + 1s + 未自定义
  `--drag-start-frame`”生效：720P **0.900**、1080P **0.900–0.933** → pass。
- `--drag-wait-ms 2000`（打开后等 2s 再拖）与立即拖结果相同（first 84/103、反向 28/27.94 vs 27），
  说明波形/索引不是首拖变慢的原因；**本轮未延后波形生成**，该参数仅 test-only 诊断保留。

### 3.3 已知残差（**不写通过**）

| 残差 | 实测 | 旧硬解基线 |
|---|---|---|
| 1080P `--drag-start-frame 150` 反向 | **8/30、7.99fps**（C7 安装版 7/30、6.99fps） | 13/30、12.87fps |
| 720P 2s/60 反向（30 个目标摊到 2s） | **26/30、12.99fps**（C7 安装版 25/30、12.50fps） | — |

- 两者都 `out_of_parity_gate=1` + `cache_hit_backward_ratio_note`（`not_gated_outside_A_default_window`），
  **非硬门**；成因是既有软解路径限制（跨拖动缓存约 8 帧、1080P start150 每次从 K 重解约 60 帧），
  不是本轮劣化（旧硬解同样 <70%）。
- 长 GOP 1080P 2s/60 的 P6 仍是 `out_of_premise`（安装版 84–90ms 量级），只记录。

## 4. C5 时间轴边缘自动平移（`6daa341`）

### 4.1 启动条件与停止路径

- 启动：`scrubbing_ && width()>0 && model_.frame_count()>0 && model_.MaxScroll()>0 && !model_.IsAtFit()`
  ——**fit/缩到最小绝不滚动**，拖出边界只把目标 `clamp` 到首/末帧。
- 左右边缘 **10 逻辑像素**；`QTimer` 16ms `Qt::PreciseTimer`；`parent=this`（控件销毁即不再 tick）。
- 步长 `min(28, 6 + 0.35*overshoot)`（overshoot = 鼠标超出边缘触发线的距离）；进入边缘**先同步走一步**，
  之后由定时器推进；到内容首/尾（`scroll` 夹取 / 不滚动）即停。
- 松手：**保留最后一次 tick 的目标帧**（不再按画布外 x 重算）；`mousePressEvent` 不启动 auto-pan
  （必须真的拖进/越过边缘）。
- 停 timer 的入口：`SetFrameCount` / `Clear` / `FitToWindow` / `SetPixelsPerFrame` / `SetViewWindow` /
  外部 `SetScrollPixels`（滚轮、滑块、面板）/ `ApplyZoom` / `resizeEvent`；auto-pan 自己的 tick
  用 `auto_pan_scrolling_` 标记豁免，避免“外部滚动打断”规则把自己停掉。

### 4.2 回跳修复

边缘拖动时目标帧**先**把 x `clamp` 到 `[0, width-1]` 再调用 `FrameAtPosition`（方向/overshoot 仍用
原始 x）：一次 `x = width + 100` 的远拖不会再“先跳到画布外帧、再被第一次 tick 拉回”，
播放头/目标帧单调推进。`no-backjump` 用例实测右拖 current 1053→…→1105 单调、左拖 996→…→944 单调，
松手不回跳。

### 4.3 测试（widget 级，188 OK / 0 fail）

| 用例 | 实测 |
|---|---|
| 右拖（放大后拖到右边缘） | `scroll 8000 → 8716.5`、`current 1025 → 1139` 单调增、`view 1089..1139` |
| 左拖 | `scroll 8000 → 7736`、`current 1025 → 967` 单调减 |
| no-backjump | 右 current 1053→…→1105、左 996→…→944；松手不回跳 |
| **fit / 缩到最小** | `scroll 0→0`、`view 0..1999`、`active=0`、current clamp 0/1999 |
| 内容末尾 | `scroll 15600→15600`、`current 1999`、不循环 |
| timer 停止路径 | 全部通过（上列每个入口各一条） |

- `lapian_timeline_test`：**188 `[ OK ]` / 0 FAIL**（ctest #32 1.21s；发布收口手动复跑 exit 0）。
- `--key-test` 的时间轴点击路径仍 **77/0**（时间轴点击/拖动交互未被 auto-pan 破坏）。

### 4.4 覆盖边界（**必须照写**）

- **只到 widget 级**：合成 `QMouseEvent` + `AutoPanStepForTest()`（与 16ms tick 同一函数）。
- **没有** MainWindow 集成探针、没有真实窗口/物理鼠标拖动、没有多 DPI/窄窗真实几何探针
  （`resizeEvent` 只验证“停 timer”，未验证真实拖动中 resize）。
- 因此**不写“真实鼠标已验证”“MainWindow 集成通过”**。

## 5. C6 独立验收（verdict=pass with gaps）

- 门禁：`ctest` **38/38**（64.91s）、timeline **188/0**、key **77/0**、banner **74/0**、
  dpi 各 **37/0**（空态 39–40/0）、drag **P4 94.6 / P6 21.0ms / P7 0.0/10.4 / P8 12.9 / P9 34.4**、
  drag strategy **7/7**、local parity **9/0**。
- B：720P **38/0 ×2**、1080P **38/0**、普通切档 **42/0**、offline **45/0**、M2 op5 **10/0**、
  fault **92/0**、T8.6.1b **35/0**（基线 35/1，**只记录、不宣称修好**）。
- 判 pass with gaps 的理由：三件事的用户路径都实测有效、硬门都过；但 auto-pan 只有 widget 级证据，
  cache-hit P7/P9 与音频 A/V 无直接探针，残差（start150/2s 反向、长 GOP P6）按 spec 前提外记录。

## 6. 覆盖缺口（C6 + 发布收口本轮未验证）

1. **auto-pan 无 MainWindow 集成/真实鼠标/多 DPI 探针**（见 §4.4）。
2. **cache-hit P7/P9 无直接探针**（只有同素材本地代理近似，见缓存命中修复交接 §6）。
3. **cache-hit 音频只验证打开/就绪**（48k、`pitch_preserve=1`），无 underrun / A-V 偏差。
4. **1080P `--drag-start-frame 150` 反向 7–8/30、720P 2s/60 反向 25–26/30** 是已知残差（§3.3）。
5. **默认 1s/60 窗口 P6 的 premise >2s**（2550/2500ms），只记录、不当保证档数字。
6. **prewarm 失败恢复空态无专门探针**：代码路径（`online_prewarm_switched_stage_` → `ShowEmpty()`）
   只有坏缓存根下的“错误文本正常、无崩溃”间接证据，没有断言“画面页确实回到空态卡片”。
7. **fault 的 `unverified=1`（真实 Windows 休眠/S3–S4）**、**无符号化崩溃 dump**（沿用 P0 记录）。
8. 本轮发布收口**未跑**：M1/M2 全量、quality 1080P 目录矩阵、longpress、QR、online parity、
   scrub-space、fullscreen/loop、mark/note/snapshot、cache-switch 等门禁（沿用 C6 与其他轮次结果）。

## 7. 残余 / 风险

1. **残差反向帧率**（§3.3）不得外推成“缓存命中拖动已和首次在线一致”。
2. **T8.6.1b `forward_ui_gap`**：本轮安装版 35/0（160ms），但历史基线为 35/1（214–349ms）；
   属既有打开阶段 UI gap，**不写“已修复”**。
3. **auto-pan 手感（步长/边缘宽度/16ms）未做用户实机确认**；只在合成事件下验证。
4. **长 GOP + 软解**的组合仍是可用档：1080P start150 反向 7fps 是既有软解路径限制。
5. **无代码签名、安装器 UI 英文**（沿用 T7.6）；未在干净机/Win10 21H2 复验。

## 8. 回滚

- 代码回滚（三个提交**必须一起回退**；只回退 test-only/软解会让比值断言与产品行为不一致）：

```powershell
git revert 6daa341 c04a180 4ae315a
# 文档提交单独回退（按实际 hash）
git revert <docs-commit>
```

- 分发回滚：重装上一版 setup `DF76A251…`（12.02MB，缓存命中修复版）→ 安装版 exe `DDF2E8E9…`；
  本轮 setup `D180A3BD…` / zip `DE6113C5…` 是预热+软解+auto-pan 的新产物，回滚后停止分发。
- auto-pan 可单独回退 `6daa341`（与在线两项无依赖）；`4ae315a` 与 `c04a180` 相互独立但也应成对复验。

## 9. 复现（安装版，隔离根）

```powershell
cd 'E:\拉片软件开发'
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$exe = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"
$root = 'build\release-close-c7'

# C4-1 打开预热（720P 缓存命中 + 菜单/切档；LAPIAN_ONLINE_LOG=1 才有预热行）
$env:LAPIAN_ONLINE_LOG='1'
& $exe --online-quality-switch-test --quality-cache-hit-menu `
  --bvid BV1cW411b7Yh --cid 28925848 --timeout-ms 60000 `
  --online-cache-root "$root\online\b720\cache" --online-pictures "$root\online\b720\pics" `
  --online-records-root "$root\online\b720\records" --settings-path "$root\online\b720\settings"

# C4-2 A：缓存命中拖动（1s/60 默认窗口）
& $exe --online-drag-preview-test --bvid BV1cW411b7Yh --cid 28925848 `
  --drag-seconds 1 --drag-samples 60 --drag-cache-hit --timeout-ms 90000 `
  --online-cache-root "$root\online\a720\cache" --online-pictures "$root\online\a720\pics" `
  --online-records-root "$root\online\a720\records" --settings-path "$root\online\a720\settings"

# C4-2 保证档 P6（720P start145 / 1080P start150）
& $exe --online-drag-preview-test --bvid BV1cW411b7Yh --cid 28925848 `
  --drag-seconds 1 --drag-samples 60 --drag-start-frame 145 --drag-cache-hit --timeout-ms 90000 `
  --online-cache-root "$root\online\a720\cache" --online-pictures "$root\online\a720\pics" `
  --online-records-root "$root\online\a720\records" --settings-path "$root\online\a720\settings"
& $exe --online-drag-preview-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --drag-seconds 1 --drag-samples 60 --drag-start-frame 150 --drag-cache-hit --timeout-ms 90000 `
  --online-cache-root "$root\online\a1080\cache" --online-pictures "$root\online\a1080\pics" `
  --online-records-root "$root\online\a1080\records" --settings-path "$root\online\a1080\settings"

# C5 时间轴 auto-pan（widget 级）
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
& build\release\Release\lapian_timeline_test.exe   # 188 OK / 0 fail
```

- 坑：GUI 探针必须 `Start-Process -Wait -RedirectStandardOutput/-Error`（PowerShell 对 GUI 子系统
  exe 用 `&` **不会等待**，也拿不到 exit code）；缓存命中拖动必须用**完整**缓存条目
  （`data.mp4` + `meta.json`）；A 探针会自己预热画面页（`online_drag_preview_warmup`），所以 A 日志里
  `online_prewarm_stage_ms=0 first=0` 属预期——**打开预热的证据要读 B/quality-switch 的日志**。
