# DP-1 本地拖动预览硬件解码 + 诊断（证据）

- **版本**：v1.0（2026-10-24）
- **状态**：**完成（1080p 保证档）**。DP-1 只做硬件预览/回退/诊断；DP-2/3/4 未做。
- **性质**：拖动预览体验改造，非门；不改 M1/M2/P4/P7 数字、OB 样本、
  `lapian-cache-1` schema、默认 AVC≤1080p、在线预览。
- **代码基线**：`079f0dd`（本地快拖热修）+ `bf37ccd`（DP-0 规划文档）。
- **交付提交**：以 `git log -1` 为准（本证据与代码同一提交）。
- **交接**：[handoff-2026-10-24-dp1.md](../handoff-2026-10-24-dp1.md)
- **上一轮**：[local-drag-fast-2026-10-24.md](local-drag-fast-2026-10-24.md)

---

## 1. 结论

1. 本地拖动预览在 **1080p H.264** 下优先使用 GfxWidget 共享 D3D11 设备硬解，
   `hw_active=1`；硬件帧不经过 `sws_scale`，直接交给 `GfxWidget::SetVideoFrame`
   做同设备纹理拷贝；离屏回读确认不是黑屏。
2. 硬件打开失败/解码失败/帧不是 D3D11/上传失败/设备不匹配都会自动回退软件，
   记录 `preview_hw_fallback` 原因，**没有“假 hw=1”**。
3. `--drag-fast` 新增 `hw_active`、`preview_hw_fallback`、`target_lag_frames/ms`、
   `seeks`、`cache_hits`、`active_fps` 等诊断。**fps 不再单独代表“跟到目标”**：
   软件强制回退时 Present 仍可达 ~74fps，但 `target_lag_p50=4161 帧 / 138.7s`；
   硬件路径同一命令 `target_lag_p50=68 帧 / 2.27s`。
4. 1080p 验收：`+60` 移动阶段 **63–76fps / 空档 36–46ms**；`-60` **57–61fps /
   空档 32–38ms**；P4 **64.1fps**、P6 **16.6–28.6ms**、P7/P8/P9 不退化。
5. `ctest 36/36`、key `77/0`、T8.6.1b `35/0`。4K 硬解按“只记录”处理：
   DP-1 把硬解限定在 ≤1080p，4K 继续原软件预览，记录
   `preview_hw_fallback=source_too_large: 3840x2160`。

## 2. 先做的 spike（临时探针，未提交）

按交接要求先在 `tests/preview_hw_spike.cpp` 写临时探针（验证后删除，不属于
ctest/交付）：

| 项 | 结果 |
|---|---|
| 预览线程用共享设备 `OpenWithD3D11Device(..., true)` | `hw=1`，open 11.5–13.5ms |
| 1080p 硬件帧交给同设备 `VideoRenderer` | 离屏回读 `nonblack=1`，尺寸 1920×1080 |
| 30 帧 GOP 硬件 seek+decode | decode p50 **11.6ms**、p95 15.4ms |
| 硬件帧上传（`SetFrame`） | p50 **0.36ms**、p95 0.02ms（短切片时序） |
| 同时持有 16 张硬件帧再解码 | **卡住/池耗尽**（16 张即超过当前表面池） |

结论：

- 共享设备硬解和同设备纹理上传可行，不黑屏、不崩。
- **不能缓存硬件帧**：D3D11VA 静态表面池有限，DP-1 只保留 `latest` 一张。
- 预览硬解仍走 `D3D11ContextMutex`（与主播放/截图/渲染串行）；预览切片短、
  松手前 `End()` 等待 worker 空闲，因此 1080p 的 Present 空档仍在 50ms 内。

## 3. 实现

| 文件 | 改动 |
|---|---|
| `src/media/PreviewDecoder.h/.cpp` | `Configure` 增加借用 `ID3D11Device*`、源尺寸；硬件优先、自动回退；硬件帧不进缓存；固定目标切片（避免边解边追把 Present 拖稀）；`End()` 有界等待；诊断字段/计数器 |
| `src/media/VideoDecoder.h/.cpp` | `OpenWithD3D11Device` 增加 `extra_hw_frames`（预览 8，主播放保持 32/0 默认） |
| `src/app/MainWindow.h/.cpp` | 传入 `gfx_->d3d_device()` 与源尺寸；设备丢失重建后重开；`PreviewDiagnosticsForTest`；预览上传失败触发 `upload_failed` 回退；本地预览 timer 10ms |
| `src/app/GfxWidget.h/.cpp` | `SetVideoFrame` 返回 bool；预览路径 `report_error=false`，失败由预览层记录并回退 |
| `src/app/DragTestRunner.*` | `--drag-fast` 输出 hw/fallback/target_lag/seeks/cache；`active_fps` 与 `total_fps`；P4/快速拖动用 10ms `Qt::PreciseTimer` |
| `src/app/main.cpp` | `--preview-force-software`（只强制预览软解） |
| `tests/preview_decoder_test.cpp` | 无设备回退、共享设备硬解、同设备硬件帧离屏渲染非黑、强制软件回退 |
| `docs/` | 本证据、DP-1 交接、任务/AGENTS 状态 |

### 3.1 硬件帧策略

- **不缓存硬件帧**：`CacheFrame` 只接收软件帧；硬件路径 latest/queue 最多 1–2 张。
- **不缩放硬件帧**：D3D11 纹理不能 `sws_scale`，直接全分辨率上传，由渲染器
  按窗口尺寸采样；画面比软件 720p 预览更清晰。
- **固定目标切片**：硬件一次 `Process` 只服务进入时的目标；到目标就交回 worker。
  否则 `+60` 时目标每 15.6ms 前进 60 帧，会永远补旧帧、Present 变稀。
- **软解回退**：硬件打不开/激活失败/seek/decode 失败/收到非硬件帧/UI 上传失败，
  重开软解并记录原因；回退后本次预览保持软件，直到 `Configure` 重新决定。

### 3.2 设备生命周期

- `PreviewDecoder` 只借用 `ID3D11Device*`，不拥有；`Close()` join 线程并释放所有
  帧（含 `hw_frames_ctx`/设备引用）后才返回。
- `MainWindow` 在切文件/关预览时先 `StopPreview()`（`Close`），再释放 session；
  `preview_` 成员先于 GfxWidget 子窗口析构，满足“预览关闭早于设备销毁”。
- `EnsurePreviewDecoderDevice()`：每次拖动开始/预览 tick 比较设备指针；设备丢失
  重建后 `Close` + `Configure` 重开，新设备可用则继续硬解，否则软解。
- `End()` 在硬件路径有界等待（≤100ms）worker 退出当前切片，避免松手后的主播放
  精确 seek 与预览硬解同时使用同一 D3D11 视频上下文；软件路径不等，保持 P6。

## 4. 诊断字段

`T8.17 drag_fast ...` 新增/语义：

| 字段 | 含义 |
|---|---|
| `hw_requested` | 调用方是否要求/允许硬解（≠实际硬解） |
| `hw_active` | 预览解码器实际走的解码器是 D3D11VA 才为 1 |
| `preview_hw_fallback` | 回退原因；无回退为 `-` |
| `seeks` / `cache_hits` | 本次拖动预览 seek 次数 / 软件预览缓存命中次数 |
| `preview_published` | 预览解码线程发布的帧数（含中间帧） |
| `decoder_reopens` | 预览解码器重开次数（回退/设备重建） |
| `target_lag_frames` / `_ms` | 每次 Present 时 `abs(显示帧 - 当前目标)`；按帧率换算 ms（CFR） |
| `first_present_ms` / `max_gap_ms` | 拖动开始到首张 Present / 相邻 Present 最大空档 |
| `fps` / `total_fps` | **headline fps = 目标仍在移动阶段**；`total_fps` 是整个 2s（含目标撞文件边界后的空闲尾段） |
| `active_seconds` / `active_presents` / `active_fps` / `boundary_hit` | 移动阶段时长/帧数/fps；是否撞到文件边界 |

回退原因取值：`forced_software`（`--preview-force-software`/环境变量）、
`device_unavailable`、`software_requested`、`source_too_large: WxH`、
`hardware_open_failed: ...`、`hardware_inactive`、`hardware_decode_failed: ...`、
`upload_failed`。

## 5. 测量

环境：开发机；`media\测试.mp4`（1080p H.264 30fps、4573 帧、GOP 30）。
探针用 `build\dp1-test.mp4` ASCII hardlink（临时，不提交）。

### 5.1 基线（`079f0dd`，旧探针）

| 命令 | 结果 |
|---|---|
| `--drag-fast --drag-fast-step 60` | 61.0fps / 空档 32.0ms |
| `--drag-fast --drag-fast-step -60` | 42.2fps / 空档 49.2ms |

> **探针计时口径修正**：旧探针用 `QTimer::singleShot(16)`，本机 Windows 定时器
> 粒度 15.6ms，实际约 **31ms** 才推一次目标；软件预览有大量积压帧，仍能
> 60fps Present，掩盖了目标更新变慢。DP-1 为让 `target_lag` 有意义，快速/P4
> 改用 `10ms + Qt::PreciseTimer`（落在 15.6ms 粒度，约 64Hz）。因此下面的数字
> 与旧探针不是完全同一节奏；旧基线仍作为“不低于”参照，且 `active_fps` 单独报告。

### 5.2 1080p 硬件预览（最终）

| 场景 | fps（移动阶段） | max_gap_ms | first_present_ms | target_lag p50 | target_lag p95 |
|---|---:|---:|---:|---:|---:|
| `+60` | **61.0–76.3**（多次中位 65–68） | **36.0–46.2** | 30.5–61.1 | 68 帧 / 2.27s | 120 帧 / 4.0s |
| `-60` | **56.8–60.7** | **31.9–38.2** | 27.8–31.2 | 60–120 帧 / 2–4s | 120 帧 / 4.0s |
| `+3`（慢拖） | 60.6 | 32.6 | 19.9 | **0 帧** | 0 帧（max 3 / 100ms） |

代表输出：

```text
T8.17 drag_fast ... step_frames=60 seconds=2.00 presents=81 fps=68.3
 total_fps=40.5 active_seconds=1.14 active_presents=78 active_fps=68.3
 boundary_hit=1 first_present_ms=43.3 max_gap_ms=36.0 hw_requested=1
 hw_active=1 preview_hw_fallback=- seeks=76 cache_hits=0
 preview_published=826 decoder_reopens=0 target_lag_samples=81
 target_lag_frames_p50=68.0 target_lag_frames_p95=120.0
 target_lag_frames_max=123.0 target_lag_ms_p50=2266.7
 target_lag_ms_p95=4000.0 target_lag_ms_max=4100.0
```

说明：`+60` 每 15.6ms 目标前进 60 帧（约 3846 帧/秒），一次“seek 到关键帧 +
解到目标”约 10–15ms，因此显示帧天然落后约一档（~68 帧 / 2.3s）；`-60` 无硬件
缓存，反向每一步都要 seek，落后 1–2 个 GOP。DP-1 **不设快拖 target_lag 硬门**，
只把真实滞后记录出来；DP-2 再做反向缓存/粗帧。

### 5.3 普通 `--drag-test`

```text
T7.4 drag_summary tag=hard hw=1 display=1920x1080 p4_fps=64.1
 p4_presents=129 p4_s=2.01 p4_pass=1 p6_p95=16.6 p6_pass=1
 p7_back_p95=0.1 p7_fwd_p95=10.4 p7_pass=1 p8_fps=12.8
 p8_frames=26 p8_presents=26 p8_pass=1 p9_p95=38.4 p9_prefix_max=26
 p9_pass=1 cache=count=0 ...
```

P4 连续 3 次 **64.1fps**；P6 16.6–28.6ms；P7 后退/前进 p95 0.1/10.4ms；
P8 12.8fps（每帧呈现）；P9 38.4ms。均不低于基线，未改阈值。

### 5.4 强制软件回退

```powershell
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step 60 --preview-force-software
```

```text
DP-1 preview_decode_mode forced=software
... presents=158 fps=74.0 total_fps=78.9 active_seconds=0.80
 active_presents=59 active_fps=74.0 max_gap_ms=30.4 hw_requested=1
 hw_active=0 preview_hw_fallback=forced_software seeks=2 cache_hits=0
 preview_published=232 target_lag_frames_p50=4161.0
 target_lag_ms_p50=138700.0
```

软件回退仍出帧、不黑、不崩；但 `target_lag_p50=4161 帧 / 138.7s`，正好证明
“fps 高 ≠ 跟到目标”。普通 `--drag-test --preview-force-software` P4=61.4fps。

### 5.5 4K（只记录，不设通过线）

`media\proxy\proxy-V4.mp4`（3840×2160）：

- 硬解在 4K 上每次 seek 解整 GOP，`+60` 只有 **8.4fps / 空档 372.6ms**（spike 后
  首次尝试），明显差于原软件预览。
- DP-1 因此把预览硬解限定在 **≤1920×1088**；4K 保持原软件路径：
  `hw_active=0`、`preview_hw_fallback=source_too_large: 3840x2160`，
  `+60` 68.5–69.4fps / 空档 19.8–20.4ms（软件帧滞后 p50 217–276 帧 / 7.2–9.2s，仅记录）。
- 不写 4K 通过；不因 4K 阻塞 1080p。HEVC 4K 未单独测试。

## 6. 回归

| 项 | 结果 |
|---|---|
| `ctest` | **36/36**（64.1s） |
| `lapian_preview_decoder_test` | **全部通过**：无设备回退/`device_unavailable`、共享设备 `hw_active=1`、硬件帧全分辨率、同设备离屏渲染**非背景像素**、强制软件 `software_requested` |
| key-test | **77/0** |
| T8.6.1b `--online-drag-preview-test` | **35/0 pass**（DASH 缓存路径；`OnlinePreviewDecoder` 未改） |
| `--drag-test` | P4/P6/P7/P8/P9 全 pass，数字见 §5.3 |
| 设备丢失 | 代码路径 `EnsurePreviewDecoderDevice` + 自动回退；未做物理设备丢失实测（T7.5 的模拟设备丢失仍引用旧结论） |

环境注意：本机从 DSH 工作区直接跑 ctest 时，Qt `QTemporaryDir` 对默认短路径
`TEMP` 失败（11 个测试报“无法创建临时目录”）；把 `TEMP/TMP` 指到
`build\tmp-ctest` 后 **36/36**。这是执行环境问题，不是产品改动。

## 7. 残余/风险

1. **快拖 target_lag 仍有 2–4s**：`+60/-60` 的目标速度远超单次“seek+解码”成本；
   DP-1 只保证不冻住、不退化、空档达标，并把滞后如实记录。DP-2 的反向缓存/
   预取/粗帧才是降滞后的手段。
2. **反向无硬件缓存**：为保护 D3D11VA 表面池，DP-1 不缓存硬件帧；`cache_hits=0`，
   `seeks` 约 76–77/2s。DP-2 统一预算后再考虑少量硬件帧。
3. **4K 硬解关闭**：4K 走原软件预览（`source_too_large`）；4K 硬解需要不同策略
   （更少 seek/粗帧/预算），归 DP-3/DP-4 评估。
4. **D3D11 全局锁**：预览硬解仍与主播放/截图/渲染串行；1080p 切片短 + `End()`
   等待，实测 Present 空档 ≤46ms。若后续把预览切片拉长，需要重新评估 UI 阻塞。
5. **探针节奏改变**：`--drag-fast`/P4 目标更新从 16ms（实际 31ms）改为 10ms
   PreciseTimer（实际 15.6ms）；旧基线数字不能逐位比较，已用 `active_fps`/
   `total_fps` 区分。P4 阈值未改。
6. **4K/HEVC 未验收**；不写 4K 通过。

## 8. 复现

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$exe = 'build\release\bin\lapian.exe'

# 中文路径在探针 argv 下可能打不开；临时 hardlink（不要提交）
New-Item -ItemType HardLink -Path build\dp1-test.mp4 -Value media\测试.mp4

& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step 60
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -60
& $exe --file build\dp1-test.mp4 --drag-test --drag-samples 20
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step 60 --preview-force-software
& $exe --file media\proxy\proxy-V4.mp4 --drag-fast --drag-fast-step 60   # 4K 只记录

# ctest：本机需把 TEMP/TMP 指到可写 ASCII 路径
$env:TEMP = (Resolve-Path build).Path + '\tmp-ctest'; $env:TMP = $env:TEMP
ctest --test-dir build\release -C Release --output-on-failure
```
