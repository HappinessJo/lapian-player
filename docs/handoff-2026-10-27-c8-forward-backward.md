# 交接：C8 在线拖动前进/后退一致性（2026-10-27）

- **状态**：代码已完成并提交；**未打包、未安装、未发布**。本文只记录事实与边界，后续按
  [预览配置化计划](plan-2026-10-27-preview-parity-config.md) 执行。
- **代码 HEAD**：`961a19b`（父 `add571c` 文档；代码链 `4ae315a` → `c04a180` → `6daa341` → `87660ef` → `961a19b`）。
- **当前安装版**：`%LOCALAPPDATA%\Programs\拉片软件\lapian.exe` = `FEF8CE1E…`（C7，**不含 C8**）。
- **C8 新 exe**：`build\release\bin\lapian.exe` / `%LOCALAPPDATA%\Programs\lapian-c8\lapian.exe`
  = `99BC1201…`（3,597,824B，Medium）；`dist/setup/zip` 仍是 C7 的旧产物。
- **用户 E: 免安装版**：两份都未动，均不含 C8：
  - `免安装版\2\Lapian-0.1.0\lapian.exe` = `DDF2E8E9…`（用户可能在跑）；
  - `免安装版\Lapian-0.1.0\lapian.exe` = `E19FC4B3…`。

---

## 1. 用户问题

1. 缓存播放“往前拖几乎无卡顿，往后拖大概 1 秒多卡一次”。
2. 后来扩大到：“不只是缓存，只要在线播放都是后退不如前进；前后要一致，而且不能前后一样烂。”
3. 最终目标：**清晰、舒适、流畅地前后随意拖动**。

## 2. 先修正测量口径

旧 `--online-drag-preview-test` 的反向只跑 `min(30, drag_samples)` 个目标、跨度也只有 30 帧；
正向是 60 个目标/60 帧。两边 fps 天生不可比。

C8 新增 test-only：

- `--drag-backward-samples N`
- `--drag-backward-full`

`N == drag_samples` 时，反向与正向**同目标率、同跨度、同目标数**，才可用于前后对比。
**旧口径“后退只有前进一半”不能用于判断产品退化。**

## 3. 修复前后 60Hz 同口径数据

### 3.1 1s/60（presented / fps_active）

| 路径 | 修复前 前进 | 修复前 后退 | 修复后 前进 | 修复后 后退 | 修复后 前/后比 |
|---|---:|---:|---:|---:|---:|
| 缓存命中 720P | 55 / 60.04 | 52 / 56.77 | 55 / 58.95 | 56 / 59.83 | **1.018** |
| 缓存命中 1080P | 58 / 63.25 | **45 / 49.13** | 54 / 57.75 | 56 / 60.02 | **1.037** |
| 首次在线 720P | 55 / 58.76 | 56 / 59.77 | 不变 | 不变 | 1.018 |
| 首次在线 1080P | 56 / 59.83 | 56 / 59.96 | 不变 | 不变 | 1.000 |
| 本地软解 720P | 58.53 | 54.46 | 不变 | 不变 | 0.929 |

- 修复前缓存命中 **1080P 1s 确实退化**（0.776）；720P 轻度退化（0.945）。
- 首次在线/本地没有明显退化。
- 修复后 1s 三路都达到 0.93–1.04，双方 fps_active≥30；父复核 720P/1080P 均 28/0、**1.018/1.015**。

### 3.2 2s/120（同 60Hz；只记录，不冒充通过）

| 路径 | 前进 | 后退 | 前/后比 | 修复前 |
|---|---:|---:|---:|---:|
| 缓存命中 720P | 115 / 59.37 | 107 / 55.30 | 0.930 | 93 / 114 = 0.816 |
| 缓存命中 1080P | 114 / 58.85 | **100 / 51.60** | **0.877** | 59 / 118 = 0.492 |
| 首次在线 720P | 115 / 59.37 | 107 / 55.30 | 0.930 | 不变 |
| 首次在线 1080P | 114 / 58.88 | **101 / 52.20** | **0.886** | 不变 |

- 2s/120 1080P 仍低于 90%：缓存命中 0.877、首次在线 0.886；双方仍有 51–59fps。
- 根因是长 GOP（该在线缓存约 5.07s）+ 预览有界缓存/填充/淘汰 + 软解。
  不要写成“2s 已通过”；这是后续配置化的目标之一。

### 3.3 其他数据

- 缓存命中预览：`hw_requested=0/hw_active=0`、软解、`playurl=dash=0`、exact frame/PTS。
- 预览缓存峰值约 63.7MB / 72 帧（OnlinePreviewDecoder 旧配置）。
- P6 保证档：720P `start145` **42ms**、1080P `start150` **64ms**（≤150）；
  默认 1s 窗口 53–54 / 86–89ms 属 `out_of_premise`，只记录。
- C8 新门：`online_drag_forward_backward_parity`，仅在线 growing/cache-hit、同 60Hz/60 目标、同跨度、
  未自定义起点时生效；判据：反向 presented ≥0.9×正向、反向 fps_active ≥0.9×正向、双方 fps_active≥30。
  “两边一起低”直接 fail。
- 回归：ctest **38/38**、key 77/0、banner 74/0、dpi 4 档 37/0、drag P4 94.6/P6 18.3ms、
  B 38/0、普通 42/0、offline 45/0、m2 op5 10/0、T8.6.1b 36/0（基线 35/1，只记录）。

## 4. 代码改动（C8）

- `87660ef test(online): measure 60Hz backward drag`
  - `OnlineDragPreviewTestRunner` 加 `--drag-backward-samples` / `--drag-backward-full`；
  - 反向与正向同跨度；新增 `online_drag_forward_backward_parity`。
- `961a19b fix(online): cache-hit drag forward/backward parity`
  - `MainWindow::OnlinePreviewEligibleForDrag()`：在线 growing + cache-hit DASH 都走
    `OnlinePreviewDecoder`（72 帧、反向预取、Pause 保留）；
  - `EnsureCacheHitPreviewDecoder()` 在 DASH cache-hit 时不再建本地 `PreviewDecoder`；
  - durl 缓存命中/本地仍走本地 `PreviewDecoder`；
  - `OnlineStepEligible()`（步进/seek）未改。
- **未改** `OnlinePreviewDecoder` / `PreviewDecoder` 的常量、CMake、installer、spec、门/阈值/schema。
- `--online-drag-parity-test` 不加节流会因缓存下完而假失败；用 `--m1-throttle-bps 262144`。

## 5. 清晰度事实（重要，避免再混口径）

- 本地默认 **硬解，硬件帧不缩放**：1080P 源预览基本就是 1920×1080；720P 源是 1280×720。
- 本地软件预览才降到 **1280**；本地 `PreviewRingCache` 192MiB，但按全 1080P 硬解帧约 3.0MiB/帧，
  可放约 60 帧；本地顺主要靠**短 GOP（测试文件 30 帧 GOP）+ 硬解 + 预取**，不是“缓存无限”。
- 在线预览固定 **软件 1024**（16:9 1024×576），所以确实比本地全分辨率软。
- 帧大小：1024×576 NV12 ≈0.844MiB；1280×720 ≈1.318MiB；1920×1080 ≈2.966MiB。
- 之前“本地 192MiB=145 帧/2.4s”只在“软件 1280”口径成立；不能当作本地硬解的全分辨率口径。
- 本地与在线 GOP 没有必然关系：本地测试文件 1s GOP，这个在线缓存 5s GOP；GOP 是编码属性，
  缓存/本地播放都不会改变它。不要把“本地短/在线长”写成规律。

## 6. 当前可见状态与风险

- C8 代码未进安装包；用户看到的 C: 安装版仍是旧版，E: 两份也是旧版。
- C8 的 1s 修复已可用 Medium 副本 `%LOCALAPPDATA%\Programs\lapian-c8\lapian.exe` 体感；
  2s/120 1080P 仍有 0.877/0.886 残差。
- 后续配置化（分辨率/缓存挡位）按 [计划](plan-2026-10-27-preview-parity-config.md) 做，
  不要在本会话继续动代码。
- 回滚：`git revert 961a19b 87660ef`（代码）；C8 未打包，无需分发回滚。
