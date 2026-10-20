# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v6（2026-10-20；T6.1 全屏完成，下一步 T6.2 键位/焦点矩阵）

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL + D3D11 + WASAPI；spec.md v0.3 已冻结）。

请先读：
- E:\拉片软件开发\AGENTS.md
- docs\handoff-2026-10-20-t6.1.md（当前交接：T6.1 全屏）
- docs\handoff-2026-10-20-t5.4.md（截图/焦点隔离/Drain 接口）
- docs\handoff-2026-10-20-t5.3.md（备注编辑/焦点隔离）
- docs\handoff-2026-10-20-t5.2.md（标记 UI/跳转）
- docs\handoff-2026-10-20-t5.1.md（sidecar 存储）
- docs\handoff-2026-10-20-t4.2.md（循环/定帧）
- spec.md（v0.3，冻结；重点 §3.1 F1-A6、§3.7 F7 全表）、plan.md、tasks.md
- docs\adr\ADR-001 到 ADR-006（尤其 002 时钟、005 帧缓存/预览、006 持久化）
- docs\perf\T6.1-fullscreen.md（T6.1 数字与缺口）、docs\perf\T5.4-snapshots.md

第一步核对，不要相信旧对话的转述：
1. git status --short；git log --oneline -8
2. 读 tasks.md 的 M6（T6.1–T6.5）
3. 跑一次 ctest（命令见下），确认 17/17

当前基线（以 git log 为准）：
- 参考基线：T6.1 代码提交 `478296a`；T6.1 交接/perf 文档提交紧随其后，以 git log 为准；
  工作区应 clean。
- M0–M4、T5.1 存储、T5.2 标记 UI、T5.3 备注、T5.4 截图、T6.1 全屏代码完成。
- ctest 17/17（约 10.3s）；dist 32.25MB / 25 文件。
- 已有：打开/播放/逐帧/seek/长按/帧号/时间码、时间轴缩放滚动/点击拖动/拖动预览、
  WASAPI、IAudioClock+QPC、0.25–2x 变速/变调、A/V offset、200/s 波形生成与绘制、
  步进/拖动静音、A/B 色条/循环播放/A=B 定帧；T5.1 sidecar；T5.2 Ctrl+M/侧栏/
  时间轴粉色标记/5px 吸附；T5.3 标签/备注编辑、失焦/Enter/Ctrl+Enter 保存、
  关窗 flush、焦点隔离；T5.4 S 截图、当前显示帧、display matrix 旋转、WIC PNG、
  命名/重名序号、目录回退、后台编码；T6.1 F/双击全屏、Esc 退出、几何/最大化/焦点
  恢复、全屏保留时间轴/信息栏/侧栏、全屏 S、交换链跟随 resize。
- 未完成：T6.2 键位/焦点矩阵、T6.3 设置持久化、T6.4 DPI/布局、T6.5 状态横幅、
  T7 性能/稳定/交付；LRU 帧缓存、后台 index worker、正式 V1–V8/V10、基线机/4K、
  10min soak、设备拔出恢复、波形/听感/截图/输入法真人验收。
- 已知缺口：长 GOP proxy-V1 循环 B→A 148–157ms，未达 F4-A2；F6-A8 只有 V9 1080p
  短 GOP 数字（T5.2 UI p95 6.22ms、画面 p95 30.76ms），不是正式 P9；T5.4 的
  4K/90° 素材是本地合成，F6-A3/A4/A5/A7 只有自动化；F1-A6 只有 T6.1 自动化
  （开发机 1920×1080 @100%），真实 V1/V4/V5、ACL 只读目录、真人键盘/看图与正式 P11
  仍待；DPI 150/200%、多显示器、全屏中播放切换都没测。不要把开发机数字冒充基线机通过。

## T6.1 已交付的接口（直接复用，不要重写）

- `src/app/MainWindow.h/.cpp`：
  - `fullscreen_action_`（`F`，`WindowShortcut`）→ `ToggleFullScreen()`；
  - `exit_fullscreen_action_`（`Esc`，`WindowShortcut`）只在
    `isFullScreen() && !IsTextInputFocused()` 时启用；`QApplication::focusChanged`
    会实时更新；
  - `EnterFullScreen/ExitFullScreen` 保存/恢复 `saveGeometry()`、`isMaximized()`、
    焦点（`QPointer`）；`MainWindow::keyPressEvent` 是 Esc 的兜底；
  - 测试入口：`ToggleFullScreenForTest`、`PressFullScreenKeyForTest`、
    `PressEscapeKeyForTest`、`PressSpaceKeyForTest`、`FocusVideoForTest`、
    `DoubleClickVideoForTest`、`IsFullScreenForTest`、`IsMaximizedForTest`、
    `FullScreenStateForTest`、`SwapChainSizeForTest`、`ClickTimelineForTest` 等。
- `src/app/GfxWidget.h/.cpp`：`mouseDoubleClickEvent` → `fullScreenToggleRequested()`；
  `swapChainSize()`；resize 仍走 `UpdateSwapChainSize` + `D3D11ContextMutex`。
- `SnapshotService`：工作线程**先发** `snapshotSaved/snapshotFailed` **再清 idle**；
  `Drain()+processEvents` 可确定拿到结果。不要回退这个顺序（4K 会偶发空路径）。
- 自动化：`--fullscreen-test`（可 `--file A --file-b 4K`）；`--snapshot-test`、
  `--note-test`、`--mark-test`、`--ab-test`、`--loop-test` 都是回归。
- 不要改 sidecar schema、MarkStore、MarkPanel 编辑/焦点语义、D3D11 锁。

## 下一步：T6.2 键位与焦点矩阵（tasks.md T6.2）

目标与验收：
- 按 spec §3.7 全表逐项核对：Space、PgUp/PgDn、←/→、Home/End、Ctrl+O、Ctrl+G、
  I/O、L、Shift+L、`[`/`]`、W、M、Ctrl+M、S、F、滚轮/`+`/`-`、Esc。
- 文本框（帧号输入、标签/备注编辑）聚焦时，除 Esc/Enter 外不触发全局快捷键；
  Esc 在全屏时仍不应在备注框里退全屏（T6.1 决策）。
- 忽略 OS 按键重复；无冲突、无双触发；长按步进仍是 300ms 后 70ms（每帧呈现）。
- 手测：F7 全表矩阵、中文输入法、AltGr；自动化可加 `--key-test`，但真人/IME 仍单列。
- 不碰 spec v0.3；F7 没有编号 F7-A 条款，验收就是 §3.7 表 + 两条行为定义。

已知缺口（T6.2 要处理）：
- `Home`/`End` 目前没有入口（首帧/末帧）。
- `PgUp/PgDn/←/→` 只在 `GfxWidget` 有焦点时生效；焦点在时间轴/侧栏/状态栏时可能
  不响应。全局化时保留 T2.5 的 300ms/70ms 与忽略 OS 重复。
- 全屏 `Esc` 的 QAction 只在全屏启用；非全屏 Esc 应保持无操作，不要顺手加语义。
- MarkPanel 的 `ShortcutOverride` 接受编辑框上除 Esc 外的所有键；新增快捷键必须
  同时加 MarkPanel 层与 MainWindow 的 `IsTextInputFocused()` 检查，双层保险。
- 备注 `Ctrl+Enter` 是提交；不要让 `Ctrl+...` 全局快捷键在编辑框里抢走它。
- `QKeySequence("+")`/`"-"` 在部分键盘布局需要 Shift；AltGr 在 Windows 上可能
  表现为 Ctrl+Alt，不要把它误判成快捷键。

实现建议：
- 优先用 `QAction` + `WindowShortcut` 统一处理全局键，避免每个控件各写一份；
  步进的长按连发保留在一个地方（可抽到 MainWindow 或保留 GfxWidget + 事件过滤器），
  不要让 QAction 和 GfxWidget 双触发（T2.5 的教训）。
- 时间轴有滚轮缩放、拖动；点击时间轴不抢焦点（`NoFocus`），全局快捷键应仍可用。
- 测试用真实 `QKeyEvent` 发到 `QApplication::focusWidget()`（T6.1 已证明能触发
  QAction），并覆盖“编辑框聚焦时 F/S/W/M/I/O/L/Shift+L/空格/方向键让位、Esc 语义”。
- 若要加 `--key-test`，输出 `T6.2 key_check <name> ok=0/1`，退出码 0/1；状态至少含
  frame、playing、fullscreen、focus、loop A/B、waveform、speed、volume/muted。
- 每任务保持 build/ctest/dist 通过；文档写 F7 矩阵哪些是自动化、哪些仍待真人/IME。

构建与验证命令：
# 全量构建 + 单测 + dist
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
# 末尾 "Cannot open E:/" 是 windeployqt 警告；以退出码和 [OK] 为准。

# 只跑 ctest（需要 Qt bin 与仓库内 TMP/TEMP）
$tmp = Join-Path $PWD '.deps\tmp'
$env:TMP=$tmp; $env:TEMP=$tmp
$env:PATH="$PWD\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build\release -C Release --output-on-failure

# T6.1 全屏回归（1080p / +4K；4K 样本 build\t5.4-4k\K.mp4，或按 T5.4 交接生成）
$dir = Join-Path $PWD 'build\t6.1-reg'
New-Item -ItemType Directory -Path $dir -Force | Out-Null
Copy-Item '.\media\V9.mp4' (Join-Path $dir 'A.mp4')
Copy-Item '.\build\t5.4-4k\K.mp4' (Join-Path $dir 'K.mp4')
$p = Start-Process -FilePath (Join-Path $PWD 'dist\lapian.exe') `
  -ArgumentList @('--file',(Join-Path $dir 'A.mp4'),'--fullscreen-test') `
  -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t6.2-fullscreen.log')
Write-Host "exit=$($p.ExitCode)"
$p = Start-Process -FilePath (Join-Path $PWD 'dist\lapian.exe') `
  -ArgumentList @('--file',(Join-Path $dir 'A.mp4'),'--file-b',(Join-Path $dir 'K.mp4'),'--fullscreen-test') `
  -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t6.2-fullscreen-4k.log')
Write-Host "exit=$($p.ExitCode)"

# T5.4 截图回归（V9 拷到 build；B 加 90° display matrix；用独立副本避免 sidecar 串）
$snap = Join-Path $PWD 'build\t6.2-snap'
New-Item -ItemType Directory -Path $snap -Force | Out-Null
Copy-Item '.\media\V9.mp4' (Join-Path $snap 'A.mp4')
& '.\third_party\tools\ffmpeg-gpl\bin\ffmpeg.exe' -y -display_rotation 90 `
  -i .\media\V9.mp4 -c copy (Join-Path $snap 'B.mp4')
$p = Start-Process -FilePath (Join-Path $PWD 'dist\lapian.exe') `
  -ArgumentList @('--file',(Join-Path $snap 'A.mp4'),'--file-b',(Join-Path $snap 'B.mp4'),'--snapshot-test') `
  -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t6.2-snapshot.log')
Write-Host "exit=$($p.ExitCode)"

# T5.2/T5.3 回归必须给 --file-b（否则 note/mark 测试会在仓库根写 .lapian.json）
$p = Start-Process -FilePath (Join-Path $PWD 'dist\lapian.exe') `
  -ArgumentList @('--file',(Join-Path $snap 'A.mp4'),'--file-b',(Join-Path $snap 'B.mp4'),'--note-test') `
  -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t6.2-note.log')
Write-Host "exit=$($p.ExitCode)"

# 手动回归
.\dist\lapian.exe --file ".\media\测试.mp4"
# 100 I / 160 O / L 循环；100 I / O / L / Space 定帧；Shift+L 清除
# 自动化入口：--ab-test、--loop-test（测试.mp4，会自动退出）；--step-test 不会自动退出

环境/沙箱坑：
- 构建在 Qt moc 处报 "libuv process spawn failed: operation not permitted"：沙箱禁止
  命名管道，需放宽到 danger-full-access 或请用户批准，不是代码错误。
- MSBuild 报 Temp 访问被拒绝：使用仓库内 TMP/TEMP（build-release.ps1 会自动做）。
- Windows main() 的 argv 是 ANSI 代码页；中文路径用 QCoreApplication::arguments()
  或显式 UTF-8。测试图片根用 `--snapshot-pictures`/setter，不用 qputenv。
- media/、dist/、build/ 都不入库；没有素材时先向用户确认，不要编造 V1 验收。
- `--step-test` 不会自动退出；用 Start-Process + 3s 后 Stop-Process，或只跑 ctest。
- 截图 QWidget::grab() 抓不到 GfxWidget 的 D3D11 原生 HWND，视频区会是黑的，
  不是播放失败。
- dist\lapian.exe 是 GUI 子系统程序；自动化用 Start-Process +
  RedirectStandardError + PassThru。
- 控制台是 GBK 时中文日志会花屏；日志是 UTF-8，用
  [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes(path)) 读。
- 截图工作线程与解码/渲染共用 D3D11 立即上下文，已用 D3D11ContextMutex 串行化；
  改全屏/交换链不要绕开这把锁。
- 程序化 QKeyEvent 能触发 QAction（T6.1 的 F/S 已证明），但**不是物理键盘**；
  中文输入法/AltGr/键盘布局必须真人手测并单列缺口。

工作方式：
- 计划内任务不要逐项找我确认，直接做；只有计划外、不可逆、规格歧义、权限/硬件限制才问。
- 一个任务 = 一次可审查变更；先提交代码，再提交文档（交接 + perf 证据）。
- 每个任务必须保持 scripts\build-release.ps1、ctest、dist 通过；性能任务补 docs/perf/ 数字。
- 只做 F1–F8 / P1–P14；候选池不进首期；技术栈按 ADR-001/004；spec v0.3 冻结，
  改动只走 §9 版本记录。
- 不提交 third_party/、media/、build/、dist/；不要删 docs/perf/ 证据。
- 不要重做 T1–T6.1；不要提前宣称 F1-A6 真人、F6/F8、F4-A2 长 GOP、正式 V1/基线机通过。
- 如果上下文变长到影响判断，先告诉我“建议开新会话”，并把当前状态写进交接，不要硬撑。

开始时可给一个 3–5 行的极短计划，但不用等我确认；随后直接改文件。先做 T6.2。
```

## 使用说明

- 这段提示词自包含；新 agent 仍应先读文件、跑 `ctest`，不要只信这段摘要。
- 若 HEAD 已前进（交接/提示词提交），以 `git log` 和当前 `tasks.md` 为准。
- 若 T6.2 已完成，把“下一步”替换为 T6.3，并先读最新交接。
- T5.1 的 sidecar、T5.2 的 `ActivateMark` deferred 解码、T5.3 的编辑 flush/焦点让位、
  T5.4 的 `displayed_frame_`/`SnapshotService`、T6.1 的 F/Esc/几何/焦点恢复都是
  已审查路径；不要重写。
- 键位矩阵是 T6.2 的唯一主目标；不要顺带做 T6.3 的设置持久化或 T6.4 的 DPI 改造。
