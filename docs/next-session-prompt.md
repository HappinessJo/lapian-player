# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v17（2026-10-21；T7.3 soak 监测完成、30min/1h 探针触发 memory_growth；下一步 V10 2h/慢漂移定位，之后 T7.4）

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL + D3D11 + WASAPI；spec.md v0.3 已冻结）。

请先读：
- E:\拉片软件开发\AGENTS.md
- docs\handoff-2026-10-21-t7.3.md（当前交接：T7.3 soak 监测/慢漂移发现）
- docs\handoff-2026-10-20-t7.2.md（内存预算/帧缓存/切文件释放）
- docs\handoff-2026-10-20-t7.1.md（冷启动埋点/后台索引/P1/P2）
- docs\handoff-2026-10-20-t6.5.md（状态横幅/优先级/状态栏）
- docs\handoff-2026-10-20-t6.4.md（DPI/布局）
- docs\handoff-2026-10-20-t6.3.md（设置持久化）
- docs\handoff-2026-10-20-t6.2.1.md（长按连发补丁）
- docs\handoff-2026-10-20-t2.9.1.md（反向拖动预览补丁）
- spec.md（重点 §3.1 F1-A4/A5、§3.2 F2-A7、§3.8、§4.1 P1/P2/P10/P11/P14、§4.2、§4.4）
- plan.md、tasks.md（M7 T7.1–T7.8）、docs\adr\ADR-001 到 ADR-006
- docs\perf\T7.2.1-loop-renderer.md、docs\perf\T7.2-memory.md、docs\perf\T7.1-cold-start.md、
  docs\perf\T6.5-banners.md、docs\perf\T6.4-dpi.md、docs\perf\T6.3-settings.md

第一步核对，不要相信旧对话的转述：
1. git status --short；git log --oneline -8
2. 读 tasks.md 的 M7（T7.1–T7.8）与 T7.2/T7.2.1 的完成说明
3. 跑 ctest（命令见下），确认 21/21

当前基线（以 git log 为准）：
- 代码提交 `31d0d94` 或更新；本提示词/交接的文档提交在其后；工作区应 clean。
- M0–M4（含 T2.9.1）、T5.1–T5.4、T6.1–T6.5（含 T6.2.1）、T7.1、T7.2、T7.2.1、T7.3 代码完成；
  ctest 21/21（约 15.4s）；dist 32.51MB / 25 文件。
- T7.3 状态：soak 监测/看门狗/下包络已入库；30min 探针 pass=0（WS 107.8→126.1MB）、
  1h 探针 pass=0（105.6→126.4MB），t=2460 第 16 次 B→A 有持续 +7MB 台阶；
  **V10 2h 未做，T7.3 验收未通过**，不要写成 P14 通过。
- 已有：打开/播放/逐帧/seek/长按/帧号/时间码、时间轴缩放滚动/点击拖动/拖动预览、
  WASAPI、IAudioClock+QPC、0.25–2x 变速、A/V offset、200/s 波形、A/B 循环/定帧、
  sidecar 标记/备注、S 截图、F/双击全屏、T6.2 全局键位、T6.3 设置、T6.4 100–200% DPI、
  T6.5 状态横幅（错误/VFR/10-bit/超出保证/扫描中/无音频，唯一横幅 + 其余进状态栏）、
  T2.9.1 反向拖动、T6.2.1 自动重复 KeyRelease 修复、T7.1 启动埋点/后台索引、
  T7.2 有界帧缓存（1080p 64MiB/24、4K 96MiB/8）、--memory-test、
  T7.2.1 B=末帧循环/PgUp 显示尺寸热修（--loop-end-test）。
- 未完成：T7.3–T7.8；正式 V1–V10、基线机/冷缓存 P1/P2、4K 真素材、2h soak、
  设备恢复、波形/听感/截图/输入法真人验收。
- 已知缺口：长 GOP 循环 B→A 148–157ms 未达 F4-A2；F6-A8 只有 V9 短 GOP 数字；
  T6.3 真实 %APPDATA% 写入受本会话执行环境限制未验证；T6.4 只用 QT_SCALE_FACTOR
  模拟 100/150/200%，没有真实 Windows 显示缩放、4K、多显示器、真人字体/IME；
  T2.9.1 反向拖动已由用户物理鼠标复测正常，但没有 4K/VFR/正式 P3/P4；
  T6.2.1 已由用户物理键盘复测长按正常；本 agent 只做了 QKeyEvent 合成自动重复 release；
  T6.5 的 VFR/10-bit 是 320×240 合成素材，没有真实 4K VFR/HDR 颜色验收；
  T7.1 的 P1/P2 是开发机暖文件缓存，没有清 OS 缓存、没有基线机/正式 V1；
  T7.2 的 P10/P11 是开发机暖缓存、152s 1080p 代理、4K 450 帧代理，不是 V1/V4；
  4K 步进 p95 ~228ms 未达 P7 150ms；连续步进高水位 1080p 333.7MB / 4K 593.7MB；
  T7.2.1 的 B=末帧循环和渲染尺寸已自动化（V9/1080p/4K 5/6/6 次 B→A，
  1080p 纹理 1920×1088/渲染 1920×1080），但 PgUp 画幅人眼观感、0.25x 长跑
  和真实 V1/V10 仍未复测。
  不要把用户手测写成正式 P3/P4/P8、F2-A7/F1-A4、P1/P2、F4-A1/A2 或基线机通过。

## 已交付、直接复用（不要重写）

- T7.2 内存预算/有界缓存：
  - `src/media/FrameCache.*`：软件帧 LRU，`SetLimits(max_bytes,max_entries)`；
    1080p 64MiB/24（实际 21 帧 65.3MB）、4K 96MiB/8（99.5MB）；
    D3D11VA 硬件帧 `av_hwframe_transfer_data` 回拷 NV12/P010，不长期持有解码表面。
  - `FrameCacheMode`：`kNone`（普通跳转/前进步进/播放循环）与
    `kBackwardWindow`（后退步进预热最后 N 帧）；**不要改回 target-only**：
    实测普通跳转每次多 3MB 回拷，mark-test 精确 p95 31.55→71.12ms。
  - `MediaSession::SeekToFrame(..., mode)`、`EnsureSequentialPosition`：
    缓存命中后 `decoder_stale_=true`，起播前必须重新定位；`Open/Close/InstallIndex`
    清缓存；`last_hit_frame_` 生命周期到下一次 Seek/Close。
  - `MainWindow::GoToFrameForStep`：后退 kBackwardWindow，前进 kNone；
    `StepBackward/StepForward/StartStepRepeat/连发` 全走它。
  - `--memory-test`：empty/pause/play/step/switch；`--memory-seconds`、
    `--memory-steps`、`--memory-switch-cycles`、`--memory-tag`；
    `GetProcessMemoryInfo` 的 WS/private/peak/handles；产品异步索引路径。
  - 波形 4h 硬上限（`WaveformData::kMaxBuckets=2,880,000`、`byte_size()`）。
  - `lapian_frame_cache_test`（约 50 断言）；`FrameIndexEntry==16B` 静态断言。
  - **不要改** P10 的“打开并暂停 60s”口径、不要拿步进高水位当 P10/P11、
    不要宣称 4K P7 通过；T7.4 再处理 4K 步进。
- T7.2.1 B=末帧循环/PgUp 画幅热修：
  - B=末帧没有 B+1：`loop_out_reached_` + EOF 用 `last_decoded/current` + 一帧墙钟
    hold（按 `frame_duration/rate` 缩放）兜底；跳转失败最多重试 3 次。
  - `--loop-end-test`：A=last-20、B=last，5s 后要求 jumps≥3、playing=1、loop=1。
  - 1080p D3D11VA 纹理是 1920×1088、显示 1920×1080；`UploadHardwareFrame`
    按显示尺寸建纹理并只拷有效区域，缓存回拷前后渲染尺寸必须一致。
  - **不要改回**用 `D3D11_TEXTURE2D_DESC` 尺寸做 fit；不要把 hold 改成固定短超时
    （0.25x 会提前跳）。用户物理画幅复测仍未做，别写成正式验收。
- T7.1 冷启动/索引后置：
  - main 的 `--startup-test`：阶段日志 `app_created/window_created/window_shown/
    ui_ready/open_begin/open_returned/first_present`；无文件在 ui_ready 后退出，
    有文件在 first_present 后 80ms 退出；10s 超时 exit 3。
  - `GfxWidget::framePresented`：Present 成功后发一次（锁外）；不要改成 SetVideoFrame
    时发，否则 P2 会少算一次绘制。
  - `MediaSession::Open(..., bool build_index)`：产品路径 false（首帧优先），
    `BuildIndexNow`/`InstallIndex`；`MediaOpenTimings` 只做测量。
  - MainWindow 后台索引：generation + `std::atomic<bool>` cancel + `std::thread` join；
    扫描中 `SetTotal(Scanning)`、时间轴/步进/跳转/缩放/标记禁用；
    就绪后 `SetTotal(Exact/Estimated)`、时间轴/导航恢复、标记与预览器才加载。
  - **VFR 总帧数保持“扫描中”**（T6.5 口径），不要因为索引完成改成 Exact。
  - 旧 GUI 回归用 `SetIndexSynchronousForTest(true)`；不要改成产品默认。
  - `FrameIndex::BuildFromFile(..., cancel)` 每包检查取消。
  - `--index-test`（12 项）；`scripts/startup-bench.ps1` 做 20 次 P1/P2。
- T6.5 状态提示：
  - `media/StatusNotice.h/.cpp` 六类，优先级
    错误 > VFR > 10-bit > 超出保证 > 扫描中 > 无音频；
    `BuildMediaStatusNotices` / `SelectPrimaryNotice` / `SecondaryNoticeSummary`。
  - `InfoBar` 只有一个 `infoBanner`；MainWindow 的 `noticeInfo` 是状态栏常驻标签；
    不要改回 `statusBar()->showMessage`。
- MainWindow 的 qApp 事件过滤器：PgUp/PgDn/←/→（300ms→70ms 单连发）、Home/End、
  +/-；文本输入/模态对话框/Ctrl/Alt/Meta 让位；OS 重复只吞不执行；
  焦点切换/窗口失活/到边界停止。GfxWidget 不再有步进定时器。
- 预览反向拖动（T2.9.1）：PreviewFrame 带 frame_index/pts；反向只发布目标帧；
  32 帧降采样缓存（约 44MB）；松手仍 End + 精确 GoToFrame。
- 长按连发（T6.2.1）：KeyRelease 只在 `!isAutoRepeat()` 时 StopStepRepeat()。
- T6.4 布局：100% 1280×720；200% 928×452；侧栏 240–300；InfoBar <760 两行；
  文件名 ElideMiddle；GfxWidget 最小 320×180。
- `core/AppSettings`：`%APPDATA%\拉片软件\settings.json`，schema 1，QSaveFile；
  `--settings-test` / `--settings-expect`。
- `--banner-test`（74 项）、`--dpi-test`（9 项/档）、`--key-test`（77 项）、
  `--fullscreen-test`（+4K 48 项）、`--snapshot-test`（41 项）、`--note-test`（18 项）、
  `--mark-test`（101 标记）、`--ab-test`、`--loop-test`、`--loop-end-test`、
  `--index-test` 都是回归。
- 不改 sidecar schema、MarkStore、MarkPanel 编辑/焦点语义、D3D11 锁、全屏语义、
  设置文件格式、T6.4 断点/侧栏宽度、T2.9.1 目标帧/缓存口径、T6.2.1 release 语义、
  T6.5 的 StatusNotice 优先级与状态栏分工、T7.1 的扫描中/就绪语义、T7.2 的缓存策略、
  T7.2.1 的 B 到达/渲染显示尺寸语义。

## 下一步：T7.3 2 小时稳定性

目标（spec §4.1 P14；tasks.md T7.3）：
- P14：2h 1080p 连续播放，内存 ≤1.2GB，无持续增长；不崩溃、不卡死。
- 先确认 V10 素材；没有 2h 片时不要编造，先用现有素材做短时探针并在报告里
  明确“不是 V10/P14”。
- 复用 `ProcessMemory::SampleProcessMemory`，增加长时间播放监测：
  每 30–60s 采 WS/private/handles/线程数/帧号/是否卡死；建议
  `--memory-mode soak --memory-seconds 7200` 或独立脚本驱动 `--play`。
- 观察：索引线程/波形线程/预览线程/截图队列是否退出；句柄/线程是否单调增长；
  纯播放路径 `FrameCache` 应为空；音频 underrun/设备恢复另列。
- 不要改 T7.1 异步索引、T7.2 缓存语义；只加监测/生命周期修复。
- 保持 build-release.ps1、ctest（20/20）、dist 通过；补交接 + perf 证据。
- 不提前宣称 P14/V10/基线机通过。

## 构建与验证

powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
# 末尾 "Cannot open E:/" 是 windeployqt 警告；以 [OK] 和退出码为准。

$tmp = Join-Path $PWD '.deps\tmp'; $env:TMP=$tmp; $env:TEMP=$tmp
$env:PATH="$PWD\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build\release -C Release --output-on-failure

# T7.2 内存探针（5s 烟测；完整 60s 见 docs/perf/T7.2-memory.md）
$p = Start-Process -FilePath (Join-Path $PWD 'dist\lapian.exe') -ArgumentList @(
  '--file',(Join-Path $PWD 'build\t7.2-media\test1080.mp4'),
  '--memory-test','--memory-mode','pause','--memory-seconds','5',
  '--memory-tag','smoke','--settings-path',(Join-Path $PWD 'build\t7.3-mem-settings')) `
  -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t7.3-mem.log')
Write-Host "mem exit=$($p.ExitCode)"

# T7.1 异步索引回归（12 项）
dist\lapian.exe --file media\测试.mp4 --file-b media\V9.mp4 --index-test `
  --settings-path build\t7.3-index-settings

# T6.2/T6.2.1 键位回归（77 项、0 失败；2s 长按 27 帧）
$p = Start-Process -FilePath (Join-Path $PWD 'dist\lapian.exe') -ArgumentList @(
  '--file',(Join-Path $PWD 'media\测试.mp4'),'--key-test',
  '--settings-path',(Join-Path $PWD 'build\t7.3-key-settings')) `
  -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t7.3-key.log')
Write-Host "key exit=$($p.ExitCode)"

# T7.2.1 B=末帧循环回归（5s；期望 jumps>=3、playing=1）
foreach ($f in @('media\V9.mp4','media\测试.mp4','media\proxy\proxy-V4.mp4')) {
  $p = Start-Process -FilePath (Join-Path $PWD 'dist\lapian.exe') -ArgumentList @(
    '--file',(Join-Path $PWD $f),'--loop-end-test',
    '--settings-path',(Join-Path $PWD 'build\t7.3-loop-settings')) `
    -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t7.3-loop-end.log')
  Write-Host "loop-end $f exit=$($p.ExitCode)"
}

# T6.5/T6.4/T6.1/T5.4/T6.3：见 T7.2 交接 §3.5；
# T7.1 冷启动：scripts\startup-bench.ps1，20 次看 19/20。

# 手动：打开 152s 1080p，后退 30 帧看缓存命中（LAPIAN_CACHE_LOG=1），
# 再按 Space 确认从当前帧起播；切 4K 再切回，确认缓存上限切换/导航正常。

## 环境/沙箱坑

- 控制台测试的 Windows main() argv 是 ANSI：中文路径会乱码；用工作区相对 ASCII
  路径或 QCoreApplication::arguments()。**Windows PowerShell 5.1 的 Start-Process
  传中文参数也会按 ANSI 转码**（日志里 `娴嬭瘯`）；1080p 测量用 ASCII 硬链接
  `build\t7.2-media\test1080.mp4` 指向 `media\测试.mp4`。
- 本会话执行环境拒绝非系统进程写 `%APPDATA%`：直接启动的 `lapian*.exe`/Qt 测试是
  Low 完整性级别，CreateFile/CreateDirectory 返回 5/2；T6.3 用重定向 APPDATA 验证。
- moc 报 "libuv process spawn failed: operation not permitted"：沙箱禁命名管道，
  不是代码错误；需 danger-full-access。
- MSBuild Temp 拒绝：用仓库内 TMP/TEMP（build-release.ps1 已处理）。
- media/、dist/、build/ 不入库；素材缺失先问，不要编造验收。
- dist\lapian.exe 是 GUI 程序：Start-Process + RedirectStandardError + PassThru。
- 日志 UTF-8；控制台 GBK 时用 ReadAllBytes + UTF8.GetString 读。
- QWidget::grab() 抓不到 GfxWidget 的 D3D11 原生 HWND，视频区黑是正常限制。
- 程序化 QKeyEvent 能触发 QAction/过滤器，但不是物理键盘；IME/AltGr 仍要真人测。
- QT_SCALE_FACTOR 只验证 Qt 逻辑布局，不等于 Windows 显示缩放；真实 DPI 手测单列。
- 连续跑多个 GUI 回归时，snapshot 的 S 键测试可能因窗口焦点失败；逐个串行并给
  焦点稳定时间，不要把一次焦点 flake 当产品回归。
- mark/note 测试前清 media 下的临时 `.lapian.json`，避免旧测试标记影响“count=1”断言；
  测试后清掉（media/测试.mp4.lapian.json、media/snapshots、根目录 .lapian.json）。
- 测试.mp4 是 152s 1080p，索引/首帧解码会产生大量 H.264 "missing picture" 日志；
  解析 `T7.1`/`T7.2` 行即可，不要把它当崩溃。
- `--startup-test` 有文件时在 first_present 后 80ms 就退出，后台索引可能还没完成；
  要看索引完成状态用 `--index-test` 或 `LAPIAN_STARTUP_LOG=1`。
- `--index-test` 的扫描中观察窗是 120ms；如果机器快到索引先完成，会打印
  `index_test scan_too_fast` 并跳过扫描断言，不要把它当失败，但要在报告里说明。
- 4K proxy 只有 450 帧（约 15s）；`--memory-mode play` 20s/60s 样本是播完保持末帧，
  不是持续播放 60s；要看播放中数字用 5s/10s。
- `--loop-end-test` 用 A=last-20、B=last，5s 后要求 `jumps>=3`；1080p 约 6 次。
  它不替代 `--loop-test`（B=160），也不替代用户物理画幅复测。
- 1080p D3D11VA 纹理常为 1920×1088、显示 1920×1080；`lapian_video_renderer_test`
  会打印 texture/frame/renderer 三个尺寸，渲染尺寸必须等于显示尺寸。
- 1080p 连续 200 步进后 WS 333.7MB > P10 250MB，但 P10 口径是“打开并暂停 60s”
  （89.3MB）；不要把步进高水位写成 P10 失败/通过。
- 4K 步进 p95 ~228ms 未达 P7 150ms；T7.4 处理，不要写成 4K P7 通过。

工作方式：
- 计划内任务不用逐项确认；只有计划外、不可逆、规格歧义、权限/硬件限制才问。
- 一个任务 = 一次可审查变更；先提交代码，再提交文档（交接 + perf 证据）。
- 每个任务保持 build-release.ps1、ctest、dist 通过；性能任务补 docs/perf/ 数字。
- 只做 F1–F8/P1–P14；spec v0.3 冻结；不重做 T1–T7.2.1；T7.3 只在其上做浸泡/定位；
  不提前宣称 IME/AltGr、F1-A6 真人、F6/F8、F8-A1 真实 %APPDATA%、
  4K/VFR/正式 P3/P4/P8、T6.2.1 硬件级自动化、T6.4 真实显示缩放/多显示器、
  T6.5 真实 VFR/10-bit/HDR、T7.1 冷缓存/基线机/正式 V1 P1/P2、
  T7.2 正式 P10/P11 或 4K P7、T7.3 P14/V10、F4-A2 长 GOP 或正式 V1/基线机通过。
- 上下文过长先告诉我“建议开新会话”，并把状态写进交接。

先给 3–5 行极短计划，然后读 `docs/handoff-2026-10-21-t7.3.md`，继续 T7.3 收口：
有 V10 就跑 7200s soak；没有就做慢漂移定位（LAPIAN_AUDIO_LOG、无音频/禁用波形分段），
并如实写“不是 V10/P14”。
```

## 使用说明

- 这段提示词自包含；新 agent 仍应先读文件、跑 `ctest`，不要只信这段摘要。
- 若 HEAD 已前进（交接/提示词提交），以 `git log` 和当前 `tasks.md` 为准。
- T7.3 soak 代码完成但 30min/1h 探针触发 memory_growth；下一步先 V10 2h/慢漂移定位，
  再 T7.4 4K、T7.5 鲁棒性、T7.6 打包/关联、T7.7 全量验收、T7.8 基线脚本。
- T7.1 的 `--startup-test`/`--index-test`/同步测试开关、T7.2 的 `FrameCache`/
  `--memory-test`/缓存策略、T7.2.1 的 B 到达/显示尺寸、T7.3 的 soak 模式/下包络
  不要重写；T7.3 只在定位清楚后改判定或修慢漂移，并补修复前后对比。
