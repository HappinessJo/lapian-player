# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v8（2026-10-20；T6.3 设置持久化完成，下一步 T6.4 DPI/布局）

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL + D3D11 + WASAPI；spec.md v0.3 已冻结）。

请先读：
- E:\拉片软件开发\AGENTS.md
- docs\handoff-2026-10-20-t6.3.md（当前交接：T6.3 设置持久化）
- docs\handoff-2026-10-20-t6.2.md（键位/焦点矩阵）
- docs\handoff-2026-10-20-t6.1.md（全屏/几何/焦点恢复）
- docs\handoff-2026-10-20-t5.4.md（截图/焦点隔离/Drain 接口）
- spec.md（重点 §3.8 F8-A1、§3.7 键位、§4.3 DPI）、plan.md、tasks.md
- docs\adr\ADR-001 到 ADR-006（尤其 006 持久化）
- docs\perf\T6.3-settings.md、docs\perf\T6.2-keys.md、docs\perf\T6.1-fullscreen.md

第一步核对，不要相信旧对话的转述：
1. git status --short；git log --oneline -8
2. 读 tasks.md 的 M6（T6.1–T6.5）
3. 跑 ctest（命令见下），确认 18/18

当前基线（以 git log 为准）：
- HEAD 应为 `105dbcf` 或更新；T6.3 代码 `105dbcf`，交接/perf 文档提交紧随其后；
  工作区应 clean。
- M0–M4、T5.1–T5.4、T6.1、T6.2、T6.3 代码完成；ctest 18/18（约 10.35s）；
  dist 32.31MB / 25 文件。
- 已有：打开/播放/逐帧/seek/长按/帧号/时间码、时间轴缩放滚动/点击拖动/拖动预览、
  WASAPI、IAudioClock+QPC、0.25–2x 变速、A/V offset、200/s 波形、A/B 循环/定帧、
  sidecar 标记/备注、S 截图、F/双击全屏、T6.2 全局步进/Home/End/+/-、文本/模态让位、
  T6.3 音量/波形跨会话（%APPDATA%\拉片软件\settings.json）。
- 未完成：T6.4 DPI/布局、T6.5 状态横幅、T7 性能/稳定/交付；LRU 帧缓存、
  后台 index worker、正式 V1–V8/V10、基线机/4K、10min soak、设备恢复、
  波形/听感/截图/输入法真人验收。
- 已知缺口：长 GOP 循环 B→A 148–157ms 未达 F4-A2；F6-A8 只有 V9 短 GOP 数字，
  不是正式 P9；F1-A6/T6.2 只有自动化；T6.2 的标记树 Home/End/方向键导航已让位给
  全局帧键（spec §3.7 决策）；T6.3 的真实 %APPDATA% 写入受本会话执行环境限制
  未验证（真实默认路径读取已通过），F8-A1 真实用户重启仍要手测。不要把开发机数字
  冒充基线机通过。

## 已交付、直接复用（不要重写）

- MainWindow 的 qApp 事件过滤器：PgUp/PgDn/←/→（300ms→70ms 单连发）、Home/End、
  +/-；文本输入/模态对话框/Ctrl/Alt/Meta 让位；OS 重复只吞不执行；
  焦点切换/窗口失活/到边界停止。GfxWidget 不再有步进定时器。
- `core/AppSettings`（`lapian_settings`）：
  - `AppSettings { volume, waveform_visible }`、纯函数 Parse/Serialize；
  - `SettingsStore` 默认 `%APPDATA%\拉片软件\settings.json`，schema 1，
    QSaveFile 原子写，64 KiB 上限，失败静默；
  - MainWindow 构造 UI 后、首个文件前加载；音量/波形 250ms 去抖 + 关窗/析构 flush；
    循环/速度/静音不落盘。
- 测试入口：`--settings-path <dir>`、`--settings-test`、`--settings-expect <v> <w>`；
  MainWindow 的 `SetVolumeForTest`、`SetWaveformVisibleForTest`、`FlushSettingsForTest`、
  `ReloadSettingsForTest`、`SettingsStateForTest` 等。
- `--key-test`（60s 短 GOP 合成片，75 项）、`--fullscreen-test`、`--snapshot-test`、
  `--note-test`、`--mark-test`、`--ab-test`、`--loop-test` 都是回归。
- 不改 sidecar schema、MarkStore、MarkPanel 编辑/焦点语义、D3D11 锁、全屏语义、
  设置文件格式（要加字段先走 spec §9 版本记录 + 迁移单测）。

## 下一步：T6.4 DPI/布局

目标（spec §4.3 / tasks.md T6.4）：
- 100%–200% 缩放；1080p 150% 不裁切/不重叠；时间轴文字、状态栏控件、
  信息栏与侧栏在全屏/普通窗口下都不裁切。
- 手测：§4.3；截图对比；单测可覆盖几何/像素换算的纯函数。
- 不做 T6.5 状态横幅；不改 T6.3 设置格式；不重做 T6.1 全屏语义。

现状：
- 开发机 1920×1080 @100%；`QWidget`/时间轴使用逻辑像素，`GfxWidget` 交换链用
  `devicePixelRatioF()`（T6.1 已断言 swap==控件×DPR）。
- 重点看：MainWindow 固定宽度侧栏 300、状态栏若干 `QLabel` + 110px 音量滑块、
  时间轴刻度文字/波形带高度、全屏几何保存/恢复、`SnapshotDisplaySizeForTest` 的
  DPR 语义。
- 可用 `QT_SCALE_FACTOR`/`QT_ENABLE_HIGHDPI_SCALING` 或改显示缩放做离屏/窗口验证；
  明确“不是真人 DPI 验收”，不要宣称 150%/200% 通过直到有截图与手测。

建议：
- 先测量现状：在 125/150/200% 下抓窗口/全屏布局截图，记录最小宽度、裁切控件、
  时间轴文字重叠；再决定是改布局 stretch/最小宽度还是 DPI 换算。
- 纯几何换算（像素↔逻辑、时间轴文字步长）抽函数做单测；不要为了测试引入
  Qt Widgets 到无 UI 单测。
- 保持 build-release.ps1、ctest（当前 18/18）、dist 通过；每任务补交接 + perf 证据。

## 构建与验证

powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
# 末尾 "Cannot open E:/" 是 windeployqt 警告；以退出码和 [OK] 为准。

$tmp = Join-Path $PWD '.deps\tmp'; $env:TMP=$tmp; $env:TEMP=$tmp
$env:PATH="$PWD\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build\release -C Release --output-on-failure

# T6.3 F8-A1 两进程（用工作区根，避免碰真实 %APPDATA%）
$exe = Join-Path $PWD 'dist\lapian.exe'
$video = Join-Path $PWD 'media\V9.mp4'
$root = Join-Path $PWD 'build\t6.4-settings'
$p1 = Start-Process -FilePath $exe -ArgumentList @('--file',$video,'--settings-test','--settings-path',$root) -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t6.4-settings-1.log')
$p2 = Start-Process -FilePath $exe -ArgumentList @('--file',$video,'--settings-expect','55','0','--settings-path',$root) -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t6.4-settings-2.log')
Write-Host "settings exit=$($p1.ExitCode)/$($p2.ExitCode)"

# T6.2 键位回归（60s 短片；没有则用 ffmpeg 生成）
$p = Start-Process -FilePath $exe -ArgumentList @('--file',(Join-Path $PWD 'build\t6.3-key\A.mp4'),'--key-test','--settings-path',(Join-Path $PWD 'build\t6.4-key-settings')) -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t6.4-key.log')
Write-Host "key exit=$($p.ExitCode)"

# 全屏/截图/note/mark/ab/loop 回归：
# --fullscreen-test / --snapshot-test；note/mark 必须给 --file-b
# --ab-test / --loop-test 用 media\测试.mp4

# 手动：改音量 + W 关波形 → 退出 → 重开（F8-A1）；150%/200% 布局走查

## 环境/沙箱坑

- **本会话的执行环境拒绝非系统进程写 `%APPDATA%`**：直接启动的 `lapian*.exe`/Qt 控制台
  测试是 Low 完整性级别（S-1-16-4096），`CreateFile`/`CreateDirectory` 分别返回 5/2；
  换 Medium IL 计划任务后 `cmd` 能写，但 `lapian*.exe` 与 Qt 控制台测试仍被拒（同一任务、
  同一路径）。这是执行环境拦截，不是设置代码路径。T6.3 用重定向 `APPDATA` 到工作区做
  两进程 F8-A1；真实 `%APPDATA%` 写入需普通用户 shell/安装版手测。不要为了“测试通过”
  把设置改写到工作区或注册表。
- moc 报 "libuv process spawn failed: operation not permitted"：沙箱禁命名管道，
  不是代码错误；需 danger-full-access。
- MSBuild Temp 拒绝：用仓库内 TMP/TEMP（build-release.ps1 已处理）。
- Windows main() argv 是 ANSI 代码页；中文路径用 QCoreApplication::arguments()
  或显式 UTF-8；配置/图片根用 setter/参数，不用 qputenv。
- media/、dist/、build/ 不入库；素材缺失先问，不要编造验收。
- dist\lapian.exe 是 GUI 程序：Start-Process + RedirectStandardError + PassThru。
- 日志 UTF-8；控制台 GBK 时用 ReadAllBytes + UTF8.GetString 读。
- QWidget::grab() 抓不到 GfxWidget 的 D3D11 原生 HWND，视频区黑是正常限制。
- 程序化 QKeyEvent 能触发 QAction/过滤器，但不是物理键盘；IME/AltGr 仍要真人测。
- 读配置不能阻塞窗口显示；关窗保存不要塞进 SnapshotService::Drain 的等待链。

工作方式：
- 计划内任务不用逐项确认；只有计划外、不可逆、规格歧义、权限/硬件限制才问。
- 一个任务 = 一次可审查变更；先提交代码，再提交文档（交接 + perf 证据）。
- 每个任务保持 build-release.ps1、ctest、dist 通过；性能任务补 docs/perf/ 数字。
- 只做 F1–F8/P1–P14；spec v0.3 冻结；不重做 T1–T6.3；
  不提前宣称真人键盘/IME、F1-A6 真人、F6/F8、F8-A1 真实 %APPDATA%、
  F4-A2 长 GOP、正式 V1/基线机通过。
- 上下文过长先告诉我“建议开新会话”，并把状态写进交接。

先给 3–5 行极短计划，然后直接做 T6.4。
```

## 使用说明

- 这段提示词自包含；新 agent 仍应先读文件、跑 `ctest`，不要只信这段摘要。
- 若 HEAD 已前进（交接/提示词提交），以 `git log` 和当前 `tasks.md` 为准。
- 若 T6.4 已完成，把“下一步”替换为 T6.5，并先读最新交接。
- T5.1 sidecar、T5.2 `ActivateMark` deferred 解码、T5.3 编辑 flush/焦点让位、
  T5.4 `displayed_frame_`/`SnapshotService`、T6.1 全屏、T6.2 窗口级键位过滤器、
  T6.3 `AppSettings`/`SettingsStore` 都是已审查路径；不要重写。
- T6.4 是 DPI/布局的唯一主目标；不要顺带做 T6.5 状态横幅或设置字段迁移。
