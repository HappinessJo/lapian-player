# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v5（2026-10-20；T5.4 截图完成，下一步 T6.1 全屏）

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL + D3D11 + WASAPI；spec.md v0.3 已冻结）。

请先读：
- E:\拉片软件开发\AGENTS.md
- docs\handoff-2026-10-20-t5.4.md（当前交接：T5.4 截图）
- docs\handoff-2026-10-20-t5.3.md（备注编辑/焦点隔离接口）
- docs\handoff-2026-10-20-t5.2.md（标记 UI/跳转接口）
- docs\handoff-2026-10-20-t5.1.md（sidecar 存储接口）
- docs\handoff-2026-10-20-t4.2.md（循环/定帧历史）
- spec.md（v0.3，冻结）、plan.md、tasks.md
- docs\adr\ADR-001 到 ADR-006（尤其 006 持久化、005 帧缓存/预览）
- docs\perf\T5.4-snapshots.md（T5.4 数字与缺口）

第一步核对，不要相信旧对话的转述：
1. git status --short；git log --oneline -8
2. 读 tasks.md 的 M5（T5.1–T5.4）与 M6（T6.1–T6.5）
3. 跑一次 ctest（命令见下），确认 17/17

当前基线（以 git log 为准）：
- 参考基线：T5.4 代码提交 `01ad8dc`（文档提交紧随其后；以 git log 为准）；工作区应 clean。
- M0–M4、T5.1 存储、T5.2 标记 UI、T5.3 备注、T5.4 截图代码完成；
  ctest 17/17；dist 32.21MB / 25 文件。
- 已有：打开/播放/逐帧/seek/长按/帧号/时间码、时间轴缩放滚动/点击拖动/拖动预览、
  WASAPI、IAudioClock+QPC、0.25–2x 变速/变调、A/V offset、200/s 波形生成与绘制、
  步进/拖动静音、A/B 色条/循环播放/A=B 定帧；T5.1 sidecar；T5.2 Ctrl+M/侧栏/时间轴
  粉色标记/5px 吸附；T5.3 标签/备注编辑、失焦/Enter/Ctrl+Enter 保存、关窗 flush、
  焦点隔离；T5.4 S 截图、当前显示帧、display matrix 旋转、WIC PNG、命名/重名序号、
  目录回退、后台编码。
- 未完成：T6.1 全屏、T6.2 键位/焦点矩阵、T6.3 设置持久化、T6.4 DPI/布局、
  T6.5 状态横幅、T7 性能/稳定/交付；LRU 帧缓存、后台 index worker、正式 V1–V8/V10、
  基线机/4K、10min soak、设备拔出恢复、波形/听感/截图/输入法真人验收。
- 已知缺口：长 GOP proxy-V1 循环 B→A 148–157ms，未达 F4-A2；F6-A8 只有 V9 1080p
  短 GOP 数字（T5.2 UI p95 6.22ms、画面 p95 30.76ms），不是正式 P9；T5.4 的
  4K/90° 素材是本地合成，F6-A3/A4/A5/A7 只有自动化，真实 V4/V5、ACL 只读目录、
  真人键盘/看图与正式 P11 仍待；不要把开发机数字冒充基线机通过。

## T5.4 已交付的接口（直接复用，不要重写）

- `src/media/SnapshotNaming.h/.cpp`（`lapian_snapshot`）：
  - `SanitizeSnapshotBaseName`、`FormatSnapshotTimecode`、`BuildSnapshotBaseName`、
    `SnapshotFileName/SnapshotFilePath`、`ResolveSnapshotDirectory`；
  - 命名 `{视频名}_f{帧号}_{HH-MM-SS-FF}.png`；重名 `_1`、`_2`；主目录 `snapshots/`
    失败回退图片目录 `拉片软件/`。
- `src/app/SnapshotImage.h/.cpp`（`lapian_snapshot`）：
  - `NormalizeSnapshotRotation`、`SnapshotDisplaySize`、`RotateSnapshotImage`；
  - 角度语义与渲染器一致：FFmpeg counterclockwise，90/270 交换宽高。
- `src/app/SnapshotService.h/.cpp`：
  - `Capture(const AVFrame*, Request)` 在 UI 线程只做 `av_frame_ref` 并入有界队列（4）；
  - 工作线程硬解回读 → sws BGRA → 旋转 → WIC PNG → `QFile::NewOnly`；
  - `Drain()` 在切文件/关窗/析构前等待队列；`SetPicturesRoot` 供测试注入回退根。
- `MainWindow`：
  - `displayed_frame_` + `RememberDisplayedFrame`（OpenFile/GoToFrame/OnFrameReady）；
  - `S` QAction、`CaptureSnapshot`、`IsTextInputFocused` 让位；拖动预览中按 S 提示
    “松开后再截图”，不截降采样预览；
  - 测试入口：`CaptureSnapshotForTest`、`PressSnapshotKeyForTest`、
    `DrainSnapshotsForTest`、`ClearFocusForTest`、`SnapshotStateForTest`、
    `SnapshotSavedPathsForTest`、`SnapshotDisplaySizeForTest`、`SnapshotPerfSummaryForTest`。
- D3D11 锁：`src/platform/gfx/D3D11Context.h` 的递归锁由 VideoDecoder（仅硬解）、
  GfxWidget（SetFrame/Present/Resize）和截图回读共用。改全屏/交换链时不要绕开它。
- 自动化：`--snapshot-test`（`--file A --file-b B`；可 `--snapshot-pictures <dir>`）；
  `--note-test`/`--mark-test` 仍是 T5.3/T5.2 回归；`--ab-test`、`--loop-test` 不变。
- 不改 sidecar schema、MarkStore、MarkPanel 编辑/焦点语义。

## 下一步：T6.1 全屏（tasks.md T6.1）

目标与验收：
- `F` 或双击画面进入/退出全屏；`Esc` 退出全屏。
- 保持画面比例；时间轴、帧号/时间码仍可用；全屏下 `S` 仍截当前显示帧。
- 1080p/4K 切换、交换链重建、焦点不丢。
- F1-A6 与 F7 相关手测；不要碰 spec v0.3。

实现建议：
- 用 `showFullScreen()/showNormal()` + 保存/恢复 geometry/window state；视频区
  `GfxWidget` 的 native HWND 与 D3D11 交换链尺寸要跟随 resize，`UpdateSwapChainSize`
  已加锁。
- `Esc` 的优先级：编辑框当前放行 Esc（T5.3 决策），全屏退出要避开文本输入；
  先看 `IsTextInputFocused()`，不要让 Esc 在备注框里退全屏。
- `F`/双击入口加 `IsTextInputFocused()` 让位；`F` 是新全局快捷键，必须同时加
  MarkPanel 的 `ShortcutOverride` 层与 MainWindow 入口检查。
- 双击用 `GfxWidget::mouseDoubleClickEvent` 发信号，不要吞掉单击拖动逻辑。
- 全屏切换期间截图：`SnapshotService` 的硬解回读锁不能与 `PresentFrame` 死锁；
  切换前/后都可用 `S` 手测。
- 时间轴/信息栏全屏布局：不要遮住播放头/帧号；保持现有时间轴可点击/拖动/缩放。

验收/回归：
- 全屏/退出后画面比例、帧号、时间轴、Space、S、S 焦点隔离仍正常。
- `--snapshot-test` 1080p + 旋转 B 退出码 0；4K 退出码 0；`--note-test`/`--mark-test`
  退出码 0；`ctest` 17/17；`dist` ≤100MB。
- 不宣称 F1-A6 的正式 V1/基线机通过，除非真的跑。

构建与验证命令：
# 全量构建 + 单测 + dist
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
# 末尾 "Cannot open E:/" 是 windeployqt 警告；以退出码和 [OK] 为准。

# 只跑 ctest（需要 Qt bin 与仓库内 TMP/TEMP）
$tmp = Join-Path $PWD '.deps\tmp'
$env:TMP=$tmp; $env:TEMP=$tmp
$env:PATH="$PWD\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build\release -C Release --output-on-failure

# T5.4 截图回归（V9 拷到 build；B 加 90° display matrix）
$dir = Join-Path $PWD 'build\t5.4-snap'
Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $dir | Out-Null
Copy-Item '.\media\V9.mp4' (Join-Path $dir 'A.mp4')
& '.\third_party\tools\ffmpeg-gpl\bin\ffmpeg.exe' -y -display_rotation 90 -i .\media\V9.mp4 -c copy (Join-Path $dir 'B.mp4')
$p = Start-Process -FilePath (Join-Path $PWD 'dist\lapian.exe') `
  -ArgumentList @('--file',(Join-Path $dir 'A.mp4'),'--file-b',(Join-Path $dir 'B.mp4'),'--snapshot-test') `
  -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t5.4-snapshot.log')
Write-Host "exit=$($p.ExitCode)"

# 4K 截图回归（合成样本，不是 V4）
$p = Start-Process -FilePath (Join-Path $PWD 'dist\lapian.exe') `
  -ArgumentList @('--file',(Join-Path $PWD 'build\t5.4-4k\K.mp4'),'--snapshot-test') `
  -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t5.4-snapshot-4k.log')
Write-Host "exit=$($p.ExitCode)"

# T5.3/T5.2 回归（V9 副本 + --note-test / --mark-test）
# 手动回归
.\dist\lapian.exe --file ".\media\测试.mp4"
# 100 I / 160 O / L 循环；100 I / O / L / Space 定帧；Shift+L 清除
# 自动化入口：--ab-test（测试.mp4）、--loop-test（测试.mp4）、--step-test（V9，不自动退出，外部限时）

环境/沙箱坑：
- 若构建在 Qt moc 处报 "libuv process spawn failed: operation not permitted"，是沙箱
  禁止命名管道；需要放宽到 danger-full-access 或请用户批准后重跑，不是代码错误。
- 若 MSBuild 报 Temp 访问被拒绝，先使用仓库内 TMP/TEMP（build-release.ps1 会自动做）。
- Windows main() 的 argv 是 ANSI 代码页；中文路径不要用 argv 直接传，改用
  QCoreApplication::arguments() 或 UTF-8 显式转换。T5.4 测试图片根用
  `--snapshot-pictures`/setter 传，不用 qputenv（qputenv 对中文路径不可靠）。
- media/、dist/、build/ 都不入库；没有素材时先向用户确认，不要编造 V1 验收。
- `--step-test` 不会自动退出；回归时用 Start-Process + 3s 后 Stop-Process，或只跑 ctest。
- 截图 `QWidget::grab()` 抓不到 GfxWidget 的 D3D11 原生 HWND，视频区会是黑的，不是播放失败。
- `dist\lapian.exe` 是 GUI 子系统程序；直接 `&` 运行可能拿不到 stderr/退出码，自动化用
  Start-Process + RedirectStandardError + PassThru。
- 控制台是 GBK 时中文日志会花屏；日志本身是 UTF-8，用
  `[System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes(path))` 读。
- 截图工作线程与解码/渲染共用 D3D11 立即上下文，已用 `D3D11ContextMutex` 串行化；
  改全屏/交换链时不要绕开这把锁，否则会出现随机黑帧/设备丢失。

工作方式：
- 计划内任务不要逐项找我确认，直接做；只有计划外、不可逆、规格歧义、权限/硬件限制才问。
- 一个任务 = 一次可审查变更；先提交代码，再提交文档（交接 + perf 证据）。
- 每个任务必须保持 scripts\build-release.ps1、ctest、dist 通过；性能任务补 docs/perf/ 数字。
- 只做 F1–F8 / P1–P14；候选池不进首期；技术栈按 ADR-001/004；spec v0.3 冻结，
  改动只走 §9 版本记录。
- 不提交 third_party/、media/、build/、dist/；不要删 docs/perf/ 证据。
- 不要重做 T1–T5.4；不要提前宣称 F6/F8、F4-A2 长 GOP、正式 V1/基线机通过。
- 如果上下文变长到影响判断，先告诉我“建议开新会话”，并把当前状态写进交接，不要硬撑。

开始时可给一个 3–5 行的极短计划，但不用等我确认；随后直接改文件。先做 T6.1。
```

## 使用说明

- 这段提示词自包含；新 agent 仍应先读文件、跑 `ctest`，不要只信这段摘要。
- 若 HEAD 已前进（交接/提示词提交），以 `git log` 和当前 `tasks.md` 为准。
- 若 T6.1 已完成，把“下一步”替换为 T6.2，并先读最新交接。
- T5.1 的 sidecar、T5.2 的 `ActivateMark` deferred 解码、T5.3 的编辑 flush/焦点让位、
  T5.4 的 `displayed_frame_`/`SnapshotService`/D3D11 锁都是已审查路径；不要重写。
- 全屏是 T6.1 的唯一主目标；不要顺带做 T6.2 的完整键位矩阵或 T6.4 的 DPI 改造。
