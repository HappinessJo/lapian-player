# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v7（2026-10-20；T6.2 键位/焦点矩阵完成，下一步 T6.3 设置持久化）

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL + D3D11 + WASAPI；spec.md v0.3 已冻结）。

请先读：
- E:\拉片软件开发\AGENTS.md
- docs\handoff-2026-10-20-t6.2.md（当前交接：T6.2 键位/焦点矩阵）
- docs\handoff-2026-10-20-t6.1.md（全屏/焦点/几何恢复）
- docs\handoff-2026-10-20-t5.4.md（截图/焦点隔离/Drain 接口）
- docs\handoff-2026-10-20-t5.3.md（备注编辑/焦点隔离）
- docs\handoff-2026-10-20-t5.1.md（sidecar 存储）
- spec.md（v0.3 冻结；重点 §3.7 键位、§3.8 F8-A1）、plan.md、tasks.md
- docs\adr\ADR-001 到 ADR-006（尤其 006 持久化）
- docs\perf\T6.2-keys.md、docs\perf\T6.1-fullscreen.md

第一步核对，不要相信旧对话的转述：
1. git status --short；git log --oneline -8
2. 读 tasks.md 的 M6（T6.1–T6.5）
3. 跑一次 ctest（命令见下），确认 17/17

当前基线（以 git log 为准）：
- 参考基线：T6.2 代码提交 `0dae678`、T6.2 交接/perf 文档提交 `4ee412e`；
  基线校准提交紧随其后，以 git log 为准；工作区应 clean。
- M0–M4、T5.1–T5.4、T6.1、T6.2 代码完成。
- ctest 17/17（约 10.3s）；dist 32.28MB / 25 文件。
- 已有：打开/播放/逐帧/seek/长按/帧号/时间码、时间轴缩放滚动/点击拖动/拖动预览、
  WASAPI、IAudioClock+QPC、0.25–2x 变速/变调、A/V offset、200/s 波形生成与绘制、
  步进/拖动静音、A/B 色条/循环播放/A=B 定帧；sidecar 标记/备注；S 截图；
  F/双击全屏；T6.2 全局步进/Home/End、+/-、文本/模态让位。
- 未完成：T6.3 设置持久化、T6.4 DPI/布局、T6.5 状态横幅、T7 性能/稳定/交付；
  LRU 帧缓存、后台 index worker、正式 V1–V8/V10、基线机/4K、10min soak、
  设备拔出恢复、波形/听感/截图/输入法真人验收。
- 已知缺口：长 GOP proxy-V1 循环 B→A 148–157ms，未达 F4-A2；F6-A8 只有 V9 1080p
  短 GOP 数字，不是正式 P9；T5.4 的 4K/90° 素材是本地合成；F1-A6/T6.2 只有自动化
  （T6.2 用 60s 合成片，75 项检查），真人键盘/IME/AltGr、Ctrl+O 原生对话框仍待；
  标记树 Home/End/方向键导航已让位给全局帧键（spec §3.7 决策）。不要把开发机数字
  冒充基线机通过。

## T6.2 已交付的接口（直接复用，不要重写）

- `MainWindow` 窗口级事件过滤器（安装在 qApp）：
  - 处理 `PgUp/PgDn/←/→`（300ms 后 70ms 单连发源）、`Home/End`、`+`/`-`
    （`Key_Plus`、`Key_Equal+Shift`、numpad；QAction 快捷键已移除）；
  - 文本输入/模态对话框/带 Ctrl/Alt/Meta 的键让位；OS 自动重复只吞不执行；
    焦点切换、窗口失活、到边界都停止。
- `GfxWidget` 不再有步进定时器/信号；只保留 Space/Ctrl+G 本地兜底。
- 测试入口：`PressKeyForTest`、`PressKeyDownForTest`、`PressKeyUpForTest`、
  `PressAutoRepeatKeyForTest`、`FocusMarkTreeForTest`、`FocusVolumeSliderForTest`、
  `KeyStateForTest`、`ShortcutMapForTest`、`StepRepeatActiveForTest` 等。
- 自动化：`--key-test`（需 60s 短片）；`--fullscreen-test`、`--snapshot-test`、
  `--note-test`、`--mark-test`、`--ab-test`、`--loop-test` 都是回归。
- 不改 sidecar schema、MarkStore、MarkPanel 编辑/焦点语义、D3D11 锁、全屏语义。

## 下一步：T6.3 设置持久化（tasks.md T6.3）

目标与验收（spec §3.8）：
- 音量、波形开关跨会话记忆（写 `%APPDATA%`）；循环开关与播放速度只在本次会话保持。
- 配置损坏/非法值回退默认，不崩溃、不弹错误。
- 验收：**F8-A1** 调整音量、关闭波形后退出并重开：音量与波形状态恢复。
- 单测：配置读写、缺省、非法/损坏、未知键；手测：真实重启一次。
- 不碰 spec v0.3；不改循环/速度的会话内语义。

现状（先核对）：
- `src/app/main.cpp` 已设置 `QCoreApplication::setOrganizationName("拉片软件")`、
  `setApplicationName("拉片软件")`，T6.3 可直接用 `QStandardPaths::AppConfigLocation`。
- `MainWindow` 里 `volume_ = 100`、`waveform_visible_ = true`；`volume_slider_`、
  `waveform_action_`、`ToggleWaveform`、`OnVolumeChanged`、`AudioEngine::SetVolume`、
  `TimelineWidget::SetWaveformVisible` 是接入口。
- 当前没有持久化；关窗也不保存设置。

实现建议：
- 用 INI/JSON 文件放 `QStandardPaths::writableLocation(AppConfigLocation)`（不要用注册表；
  spec 明确 `%APPDATA%`），原子写（临时文件 + rename），失败静默回退默认。
- 读取时 clamp：音量 0–100 整数；波形 bool；未知键忽略；文件缺失/损坏/权限失败都回退。
- 应用时机：构造 UI 后、首次打开文件前把音量/波形应用到控件/音频/时间轴；
  变更时立即保存（音量拖动可防抖）或关窗保存，二选一即可，但要保证 F8-A1。
- **循环与速度不要写配置**；不要顺手加最近文件/窗口几何/主题（spec 未定义）。
- 测试注入：`--settings-path <dir>` 或 `SetSettingsRootForTest`（Unicode 路径用 setter，
  不用 qputenv）；`--settings-test` 可在一个进程内验证默认/写读/非法/损坏，
  再配合 PowerShell 两次启动验证 F8-A1。
- 单测：把“解析/校验/损坏回退”抽成纯函数，放 `lapian_framemath` 或新小库；
  不要为了测试把 Qt Widgets 拖进无 UI 单测。
- 注意冷启动：读配置不能阻塞窗口显示；不要新增网络/第三方库。

构建与验证命令：
# 全量构建 + 单测 + dist
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
# 末尾 "Cannot open E:/" 是 windeployqt 警告；以退出码和 [OK] 为准。

# 只跑 ctest（需要 Qt bin 与仓库内 TMP/TEMP）
$tmp = Join-Path $PWD '.deps\tmp'
$env:TMP=$tmp; $env:TEMP=$tmp
$env:PATH="$PWD\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build\release -C Release --output-on-failure

# T6.2 键位回归（60s 短片；不存在则先生成）
$key = Join-Path $PWD 'build\t6.3-key\A.mp4'
New-Item -ItemType Directory -Path (Split-Path $key) -Force | Out-Null
if (-not (Test-Path $key)) {
  & '.\third_party\tools\ffmpeg-gpl\bin\ffmpeg.exe' -y -stream_loop 11 -i .\media\V9.mp4 -t 60 `
    -c:v libx264 -preset ultrafast -crf 28 -g 24 -keyint_min 24 -sc_threshold 0 `
    -pix_fmt yuv420p -c:a aac -b:a 64k $key
}
$p = Start-Process -FilePath (Join-Path $PWD 'dist\lapian.exe') `
  -ArgumentList @('--file',$key,'--key-test') `
  -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t6.3-key.log')
Write-Host "exit=$($p.ExitCode)"

# T6.1/T5.4 回归（V9 拷到 build；B 加 90° display matrix；4K 用合成 K）
$dir = Join-Path $PWD 'build\t6.3-reg'
New-Item -ItemType Directory -Path $dir -Force | Out-Null
Copy-Item '.\media\V9.mp4' (Join-Path $dir 'A.mp4')
Copy-Item '.\build\t5.4-4k\K.mp4' (Join-Path $dir 'K.mp4')
& '.\third_party\tools\ffmpeg-gpl\bin\ffmpeg.exe' -y -display_rotation 90 `
  -i .\media\V9.mp4 -c copy (Join-Path $dir 'B.mp4')
$p = Start-Process -FilePath (Join-Path $PWD 'dist\lapian.exe') `
  -ArgumentList @('--file',(Join-Path $dir 'A.mp4'),'--file-b',(Join-Path $dir 'K.mp4'),'--fullscreen-test') `
  -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t6.3-fullscreen-4k.log')
Write-Host "exit=$($p.ExitCode)"
$p = Start-Process -FilePath (Join-Path $PWD 'dist\lapian.exe') `
  -ArgumentList @('--file',(Join-Path $dir 'A.mp4'),'--file-b',(Join-Path $dir 'B.mp4'),'--snapshot-test') `
  -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t6.3-snapshot.log')
Write-Host "exit=$($p.ExitCode)"

# T5.2/T5.3 回归必须给 --file-b（否则 note/mark 会在仓库根写 .lapian.json）
# --ab-test / --loop-test 用 media\测试.mp4

# 手动回归
.\dist\lapian.exe --file ".\media\测试.mp4"
# 调整音量 + W 关波形 → 退出 → 重开（F8-A1）
# 100 I / 160 O / L 循环；100 I / O / L / Space 定帧；Shift+L 清除

环境/沙箱坑：
- 构建在 Qt moc 处报 "libuv process spawn failed: operation not permitted"：沙箱禁止
  命名管道，需放宽到 danger-full-access 或请用户批准，不是代码错误。
- MSBuild 报 Temp 访问被拒绝：使用仓库内 TMP/TEMP（build-release.ps1 会自动做）。
- Windows main() 的 argv 是 ANSI 代码页；中文路径用 QCoreApplication::arguments()
  或显式 UTF-8。配置/图片测试根用 setter 或 `--settings-path`，不要用 qputenv。
- media/、dist/、build/ 都不入库；没有素材时先向用户确认，不要编造 V1 验收。
- `--step-test` 不会自动退出；用 Start-Process + 3s 后 Stop-Process，或只跑 ctest。
- 截图 QWidget::grab() 抓不到 GfxWidget 的 D3D11 原生 HWND，视频区会是黑的。
- dist\lapian.exe 是 GUI 子系统程序；自动化用 Start-Process +
  RedirectStandardError + PassThru。
- 控制台是 GBK 时中文日志会花屏；日志是 UTF-8，用
  [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes(path)) 读。
- 程序化 QKeyEvent 能触发 QAction/事件过滤器，但不是物理键盘；IME/AltGr 仍要真人测。
- 配置路径可能含中文（组织名/应用名都是「拉片软件」）；用 Qt API 拼路径，
  不要在测试里用 ANSI 字符串或 qputenv。
- 读写配置失败不能阻塞关窗；关窗已有 sidecar/截图 flush，别把设置保存塞进
  `Drain()` 的等待链里。

工作方式：
- 计划内任务不要逐项找我确认，直接做；只有计划外、不可逆、规格歧义、权限/硬件限制才问。
- 一个任务 = 一次可审查变更；先提交代码，再提交文档（交接 + perf 证据）。
- 每个任务必须保持 scripts\build-release.ps1、ctest、dist 通过；性能任务补 docs/perf/ 数字。
- 只做 F1–F8 / P1–P14；候选池不进首期；技术栈按 ADR-001/004；spec v0.3 冻结，
  改动只走 §9 版本记录。
- 不提交 third_party/、media/、build/、dist/；不要删 docs/perf/ 证据。
- 不要重做 T1–T6.2；不要提前宣称 T6.2 真人键盘/IME、F1-A6 真人、F6/F8、F4-A2 长 GOP、
  正式 V1/基线机通过。
- 如果上下文变长到影响判断，先告诉我“建议开新会话”，并把当前状态写进交接，不要硬撑。

开始时可给一个 3–5 行的极短计划，但不用等我确认；随后直接改文件。先做 T6.3。
```

## 使用说明

- 这段提示词自包含；新 agent 仍应先读文件、跑 `ctest`，不要只信这段摘要。
- 若 HEAD 已前进（交接/提示词提交），以 `git log` 和当前 `tasks.md` 为准。
- 若 T6.3 已完成，把“下一步”替换为 T6.4，并先读最新交接。
- T5.1 的 sidecar、T5.2 的 `ActivateMark` deferred 解码、T5.3 的编辑 flush/焦点让位、
  T5.4 的 `displayed_frame_`/`SnapshotService`、T6.1 的全屏、T6.2 的窗口级键位过滤器
  都是已审查路径；不要重写。
- 设置持久化是 T6.3 的唯一主目标；不要顺带做 T6.4 的 DPI 改造或 T6.5 的状态横幅。
