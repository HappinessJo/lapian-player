# 新会话提示词：DP-1 本地拖动预览硬件解码

你是拉片软件（Windows x64，C++20 + Qt 6 + FFmpeg + D3D11VA）的后续 agent。
本次只做 **DP-1：本地拖动预览硬件解码 + 诊断**，不要做 DP-2/DP-3/DP-4。

## 先读

1. `AGENTS.md`（最新 v6.43）
2. `docs/plan-drag-preview-v2.md`
3. `docs/tasks-drag-preview-v2.md`
4. `docs/handoff-2026-10-24-drag-preview-plan.md`
5. `docs/handoff-2026-10-24-drag-fast.md`
6. 相关代码：`src/media/PreviewDecoder.*`、`src/media/VideoDecoder.*`、
   `src/app/MainWindow.cpp`、`src/app/GfxWidget.*`、
   `src/platform/gfx/VideoRenderer.cpp`、`src/app/DragTestRunner.*`、
   `tests/preview_decoder_test.cpp`

## 基线

- 代码基线：`079f0dd`（本地快拖热修）；DP-0 规划/交接已提交，以 `git log -1` 为准
- dist：34.48MB / 25 文件；`ctest` 36/36、key 77/0、T8.6.1b 35/0
- 当前预览是**软件解码**：`PreviewDecoder` 的 `VideoDecoder` 用
  `Open(..., prefer_hardware=false)`；主播放的 D3D11 硬解不覆盖预览。
- `media\测试.mp4`：1920×1080 H.264 30fps、4573 帧、GOP 30 帧。
  - 软件 seek p50 26.5ms / p95 77.9ms
  - 硬件 seek p50 12.8ms / p95 29.4ms
  - 当前 `--drag-fast +60`：约 60.0–61.0fps / 最大空档 32ms
  - 当前 `--drag-fast -60`：约 42.7fps / 48.5ms
  - 当前反向 `-20`：11.5fps / 191.8ms（DP-1 不要求修复，DP-2 处理）

**重要**：fps 只代表 Present 帧率，不代表显示帧等于鼠标目标；DP-1 要加
`target_lag` 诊断，不要用 60fps 冒充“跟手”。

## 目标

让本地拖动预览优先使用**共享 `GfxWidget` D3D11 设备**的硬件解码；失败自动
回退软件；不黑屏、不崩溃、不设备冲突。DP-1 不扩大预览缓存、不做缩略图/预取。

## 先做最小 spike（不要直接改产品）

1. 在预览线程用
   `VideoDecoder::OpenWithD3D11Device(path, stream, gfx_->d3d_device(), true, &err)`
   打开；确认 `HardwareActive()==true`。
2. 解一帧硬件帧，交给 `GfxWidget::SetVideoFrame`，确认同设备纹理能显示、
   不黑、不崩。
3. 再决定 `PreviewDecoder::Configure` 是否加可选 `ID3D11Device*` 参数。
4. 设备为空/打开失败/纹理不匹配 → 回退软件并记录原因；禁止“假 hw=1”。

关键约束：

- `VideoRenderer::UploadHardwareFrame` 只接受**同一个 D3D11 设备**的纹理；
  `AV_PIX_FMT_D3D11` 或 `hw_frames_ctx` 不能走 `sws_scale`。
- 设备生命周期由 `MainWindow`/`GfxWidget` 保证；预览关闭必须早于设备销毁。
- 预览线程不能阻塞 UI；不改主播放解码/时钟/在线路径。
- 不改 `OnlinePreviewDecoder`、T8.6.1b、M1/M2/P4/P6/P7 门/数字、OB、
  `lapian-cache-1` schema、默认 AVC≤1080p。
- 不打印/提交 SESSDATA、Cookie、签名 URL；不写默认缓存/记录/凭据。
- 不扩大预览缓存；DP-1 硬件帧先不缓存（或只保留极少量），DP-2 再统一预算。

## 诊断

给 `--drag-fast` 增加并输出：

- `hw_active`（预览是否真硬解）与回退原因；
- `target_lag_frames` / `target_lag_ms`（Present 时显示帧 vs 当前目标）；
- `first_present_ms`、`max_gap_ms`、`seeks`、`cache_hits`；
- 必要时给 `MainWindow` 加 `PreviewDiagnosticsForTest`。

不要只看 fps；要能区分“画面在动”和“画面跟到目标”。

## 验收（DP-1）

- 开发机 `hw_active=1`；强制软件回退仍能出帧。
- `--drag-fast +60`：不低于基线（约 60fps / 空档 ≤50ms）。
- `--drag-fast -60`：≥42fps / 空档 ≤80ms（不低于基线）。
- 普通 `--drag-test`：P4 ≥55fps、P6 ≤150ms、P7/P8/P9 不退化。
- 无黑帧、无崩溃、无设备丢失；T8.6.1b 35/0、`ctest` 36/36、key 77/0。
- 4K/HEVC 只记录，不设通过线；不因 4K 失败阻塞 1080p。

## 停止/回退

- 共享设备导致黑帧/崩溃/设备丢失且无法解决：保留软件路径，DP-1 只交付
  “回退 + 诊断 + 失败原因”，不强行提交硬解。
- 硬件帧格式/上传失败：自动回退软件，记录 `preview_hw_fallback`。
- 任何主播放/在线回归：回滚 DP-1 改动。

## 交付

- 代码 + 测试 + 证据 `docs/perf/drag-preview-dp1.md`；
- 交接 `docs/handoff-2026-10-24-dp1.md`；
- 更新 `AGENTS.md` 状态（v6.44，DP-1 完成/partial/blocked 如实写）；
- 一个可审查提交，不混合 DP-2；工作区干净。

## 复现命令

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$exe = 'build\release\bin\lapian.exe'

& $exe --file 'E:\拉片软件开发\media\测试.mp4' --drag-fast --drag-fast-step 60
& $exe --file 'E:\拉片软件开发\media\测试.mp4' --drag-fast --drag-fast-step -60
& $exe --file 'E:\拉片软件开发\media\测试.mp4' --drag-test --drag-samples 20
ctest --test-dir build\release -C Release --output-on-failure
```

中文路径若在探针 argv 下打不开，可在 `build\` 建 ASCII hardlink 后传 hardlink
路径；临时 hardlink 不要提交。
