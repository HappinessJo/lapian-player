# 拖动预览 v2 任务表（DP 任务，非门）

- **版本**：v1.2（2026-10-24；DP-1/DP-2 完成）
- **计划**：[plan-drag-preview-v2.md](plan-drag-preview-v2.md)
- **基线**：`079f0dd`（本地快拖热修）+ `bf37ccd`（DP-0）；dist 34.48MB/25、
  `ctest` 36/36、key 77/0。
- **性质**：本地拖动预览体验改造；不改 M1/M2/P4/P7 门/数字，不改 OB、缓存
  schema、默认 AVC≤1080p；不是 v2 在线任务。
- **执行方式**：一个任务 = 一个会话 = 一次可审查提交；每任务写证据/交接。

---

## 状态

| 任务 | 状态 | 依赖 |
|---|---|---|
| DP-0 规划/交接 | **完成（2026-10-24）** | — |
| DP-1 硬件预览 + 诊断 | **完成（2026-10-24，1080p；4K 硬解关闭/只记录）** | DP-0 |
| DP-2 反向缓存/预取/远跳粗帧 | **完成（2026-10-24；1080p 硬件默认；强制软件快反向残余）** | DP-1 |
| DP-3 交互策略 + 验收探针 | 待开始 | DP-1/DP-2 |
| DP-4 集成/发布 | 待开始 | DP-1/2/3 |

---

## DP-0 规划/交接（完成）

- 产出：计划、本任务表、交接、下一会话提示词。
- 明确：1080p 先达标，4K best-effort；不做全片缓存；不改主播放/在线预览。
- 不改代码。

## DP-1 硬件预览 + 诊断

### 目标

让本地拖动预览优先走共享 D3D11 设备的硬件解码；失败自动回退软件；把
“真硬解/回退原因/目标滞后”做成可测诊断。

### 进入条件

- 阅读 `AGENTS.md`、`docs/plan-drag-preview-v2.md`、本表、
  `docs/handoff-2026-10-24-drag-preview-plan.md`。
- 基线 `079f0dd`，`ctest` 36/36、key 77/0。
- 先跑一遍 `--drag-fast`/`--drag-test` 复现基线。

### 范围/关键文件

- `src/media/PreviewDecoder.h/.cpp`：可选 D3D11 设备参数；硬件打开/回退；
  硬件帧不 sws；诊断字段。
- `src/app/MainWindow.cpp/.h`：把 `gfx_->d3d_device()` 传入预览；设备生命周期；
  `PreviewDiagnosticsForTest`。
- `src/platform/gfx/VideoRenderer.*`（只读/必要时小改）：确认同设备硬件帧上传。
- `src/app/DragTestRunner.*`、`src/app/main.cpp`：`--drag-fast` 输出
  `hw_active`、`target_lag`、`seeks`、`cache` 等。
- 测试：`tests/preview_decoder_test.cpp` 的软件回退；GUI 探针。

### 硬约束

- 不得在设备不匹配时把硬件帧硬塞给渲染器；失败回退软件。
- 预览线程不能阻塞 UI；不能改主播放解码/时钟。
- 不扩大预览缓存（DP-1 不缓存硬件帧；现有 32 帧软件缓存保留）。
- 不打印凭据/URL；不写默认缓存/记录。
- 不改 P4/P6/P7 数字、M1/M2、OB、schema。

### 验收

- 开发机 `preview_hw_active=1`；强制软件回退仍能出帧。
- `media\测试.mp4`：
  - 正向 +60 帧/16ms：不低于 `079f0dd`（约 60fps/空档 ≤50ms）；
  - 反向 -60：≥42fps/空档 ≤80ms（不低于基线）；
  - 普通 P4 ≥55fps；P6 ≤150ms；无黑帧/崩溃。
- `ctest` 36/36、key 77/0；T8.6.1b 35/0（不改在线路径）。
- 4K 只记录，不设通过线。

### 停止/回退

- 共享设备导致黑帧、崩溃、设备丢失不可解 → 保留软件路径，记录原因，
  不提交“假硬解”。
- 硬件帧格式/上传失败 → 回退软件；DP-1 可以只交付回退+诊断，不强行硬解。

### 交付

- 代码/测试/证据 `docs/perf/drag-preview-dp1.md`；
- 交接 `docs/handoff-2026-10-24-dp1.md`；
- 更新 `AGENTS.md` 状态；
- 一个提交；不合并 DP-2。

### 完成记录（2026-10-24）

- 代码：`PreviewDecoder` 共享设备硬解/回退/诊断；`VideoDecoder` 可选表面池；
  `MainWindow` 设备传递/设备重建/`PreviewDiagnosticsForTest`；`GfxWidget`
  上传返回 bool；`--drag-fast` 输出 hw/target_lag/seeks/cache；`--preview-force-software`。
- 1080p：`hw_active=1`；`+60` 63.1–76.3fps/空档 36–46ms；`-60` 56.8–60.7fps/
  空档 32–38ms；P4 64.1fps、P6 16.6–28.6ms、P7/P8/P9 不退化。
- 强制软件：`hw_active=0`/`forced_software`，仍出帧；`target_lag_p50=4161 帧`。
- 4K：硬解实测 8.4fps/空档 372ms，DP-1 改为 `source_too_large` 回退软件，
  只记录、不设通过线。
- 回归：`ctest` 36/36、key 77/0、T8.6.1b 35/0；`lapian_preview_decoder_test`
  覆盖无设备回退/共享设备硬解/同设备硬件帧非黑。
- 证据 [perf/drag-preview-dp1.md](perf/drag-preview-dp1.md)、交接
  [handoff-2026-10-24-dp1.md](handoff-2026-10-24-dp1.md)。
- 口径修正：`--drag-fast`/P4 目标更新从 16ms（本机实际 ~31ms）改为 10ms
  `Qt::PreciseTimer`（实际 ~15.6ms）；`fps` 用移动阶段，`total_fps` 保留 2s。

## DP-2 反向缓存/预取/远跳粗帧

- **下一会话提示词**：[next-session-prompt-dp2.md](next-session-prompt-dp2.md)。

### 目标

反向拖动优先命中已解过的帧；远跳先出低分辨率粗帧；缓存有界。

### 进入条件

- DP-1 完成并验证；硬件路径可回退。
- 读 DP-1 交接/证据。

### 范围

- 新增 `PreviewRingCache`（或 `PreviewCache`）：
  - 有界预算 ≤256MB；按最近/距离淘汰；
  - 支持软件 NV12；硬件帧单独预算或只缓存少量；
  - 反向查询、缓存命中统计。
- 预取：当前/上一 GOP；可取消、低优先级；不阻塞主目标。
- 远跳粗帧：关键帧缩略图（建议 320×180，滑动窗口 + LRU，≤64MB）；
  `first_coarse_ms` 记录。
- 不改 `FrameCache`（正式 64/256MiB）预算语义。

### 验收

- 反向 -60 ≥45fps/空档 ≤60ms；反向 -20 ≥25fps；
- 反向局部拖动 cache_hit_rate ≥70%（口径写入证据）；
- 远跳首粗帧 ≤100ms；松手 P6 ≤150ms；
- 预览缓存峰值 ≤256MB、缩略图 ≤64MB；主播放/在线会话不退化；
- `ctest` 36/36、key 77/0、T8.6.1b 35/0。

### 停止/回退

- 缓存导致内存/显存超限或主播放抖动 → 缩预算/关闭预取，保留 DP-1；
- 粗帧造成明显向前跳 → 丢弃粗帧，保留精确路径。

### 交付

- 代码/测试/证据 `docs/perf/drag-preview-dp2.md`；
- 交接 `docs/handoff-2026-10-24-dp2.md`；一个提交。

### 完成记录（2026-10-24）

- 代码：新增 `PreviewRingCache`（软件 NV12，192MiB）与
  `PreviewThumbnailCache`（320×180，64MiB）；`PreviewDecoder` 精确缓存快路径、
  独立预取线程/缩略图线程、硬件关键帧全分辨率粗帧、`PreviewFrame.coarse/
  from_cache/hardware`；`FrameIndex` 关键帧前后查询；`--drag-fast` 缓存/粗帧/
  预取/分段耗时；`--drag-far-test`、`--drag-fast-cycles`；新增
  `lapian_preview_cache_test`。
- 1080p 硬件：`-60` 60.2–69.2fps/空档 25.2–33.0ms、`-20` 45.7–48.7fps、
  `+60` 68.7–79.5fps/26.2–48.0ms、`-1` 局部反向命中 84.8–86.4%；
  远跳首帧 20–41ms（软件粗帧 19.9–40.7ms；硬件精确首帧 20–21ms）；
  P4 64.1fps、P6 16.8ms、P7/P8/P9 不退化。
- 有界：预览缓存峰值 233.4MiB、缩略图 ≤28.8MB；3 次拖动峰值不变。
- 回归：`ctest` 37/37（36 既有 + 新增）、key 77/0、T8.6.1b 35/0；
  4K 只记录（`source_too_large`）。
- 残余：强制软件快反向 `-20` 9.9–11.4fps、`-60` 30.3–33.0fps，未达
  25/45；已在证据中如实记录，不阻塞默认硬件验收。
- 证据 [perf/drag-preview-dp2.md](perf/drag-preview-dp2.md)、交接
  [handoff-2026-10-24-dp2.md](handoff-2026-10-24-dp2.md)。

## DP-3 交互策略 + 验收探针

### 目标

把“快拖近似、慢拖精确、松手精确、远跳先粗后精”写成可测策略；
新增 `target_lag` 等诊断，避免只看 fps。

### 范围

- `PreviewController`/`MainWindow` 拖动策略：按目标变化速度/距离切换；
- `--drag-fast` 输出 `target_lag_frames/ms`、`first_coarse_ms`、`cache_hit`、
  `hw_active`；
- `PreviewDiagnosticsForTest`：目标帧/显示帧/是否粗帧/命中/seek；
- 慢拖、反向、远跳、松手测试矩阵。

### 验收

- 慢拖目标滞后 ≤2 帧；松手 P6 ≤150ms；
- 快拖不冻住，空档达标；粗帧不被当最终帧；
- 远跳先粗后精；诊断字段可区分精确/粗帧/缓存/硬解；
- `ctest` 36/36、key 77/0、T8.6.1b 35/0；
- 4K 只记录。

### 交付

- 代码/测试/证据 `docs/perf/drag-preview-dp3.md`；
- 交接 `docs/handoff-2026-10-24-dp3.md`；一个提交。

## DP-4 集成/发布

### 目标

全量回归、dist、Medium 副本、证据/交接；确认没有门/数字被改。

### 验收

- `ctest` 36/36、key 77/0、T8.6.1b 35/0；
- 本地 `--drag-test` P4/P6/P7/P8/P9 不退化；
- DP-1/2/3 指标复跑；
- `dist` 重建、Medium 副本验证、工作区干净；
- 文档明确 1080p 体验与 4K 残余；不写 M1/M2/P4/P7 通过。

### 交付

- `docs/perf/drag-preview-dp4.md`、`docs/handoff-2026-10-24-dp4.md`；
- 发布提交/证据；最终交接。

---

## 通用命令

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$exe = 'build\release\bin\lapian.exe'

& $exe --file 'E:\拉片软件开发\media\测试.mp4' --drag-fast --drag-fast-step 60
& $exe --file 'E:\拉片软件开发\media\测试.mp4' --drag-fast --drag-fast-step -60
& $exe --file 'E:\拉片软件开发\media\测试.mp4' --drag-test --drag-samples 20
ctest --test-dir build\release -C Release --output-on-failure
```

> 中文路径若在探针 argv 下打不开，可在 `build\` 建 ASCII hardlink 后传
> hardlink 路径；不要把临时 hardlink 提交。

## 禁止

- 改 M1/M2 门/数字、P4/P6/P7 验收值、OB 样本、`lapian-cache-1` schema、
  默认 AVC≤1080p；
- 把“拖动中近似帧”写成精确帧；
- 无界缓存/全片缓存；
- 改 `OnlinePreviewDecoder` 或在线缓存语义；
- 打印/提交 SESSDATA、Cookie、签名 URL；
- 写默认缓存/记录/凭据。
