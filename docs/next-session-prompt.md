# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v9（2026-10-20；T6.4 DPI/布局完成，下一步 T6.5 状态横幅）

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL + D3D11 + WASAPI；spec.md v0.3 已冻结）。

请先读：
- E:\拉片软件开发\AGENTS.md
- docs\handoff-2026-10-20-t6.4.md（当前交接：T6.4 DPI/布局）
- docs\handoff-2026-10-20-t6.3.md（设置持久化）
- docs\handoff-2026-10-20-t6.2.md（键位/焦点矩阵）
- docs\handoff-2026-10-20-t6.1.md（全屏/几何/焦点恢复）
- spec.md（重点 §4.3 DPI、§4.2 支持范围、§3.1/§3.2 状态与 VFR、§3.8 F8-A1）
- plan.md、tasks.md、docs\adr\ADR-001 到 ADR-006
- docs\perf\T6.4-dpi.md、docs\perf\T6.3-settings.md、docs\perf\T6.2-keys.md

第一步核对，不要相信旧对话的转述：
1. git status --short；git log --oneline -8
2. 读 tasks.md 的 M6（T6.1–T6.5）
3. 跑 ctest（命令见下），确认 18/18

当前基线（以 git log 为准）：
- HEAD 应为 `8de14cd` 或更新；T6.4 代码 `8de14cd`，交接/perf 文档提交紧随其后；
  工作区应 clean。
- M0–M4、T5.1–T5.4、T6.1–T6.4 代码完成；ctest 18/18（约 10.36s）；
  dist 32.34MB / 25 文件。
- 已有：打开/播放/逐帧/seek/长按/帧号/时间码、时间轴缩放滚动/点击拖动/拖动预览、
  WASAPI、IAudioClock+QPC、0.25–2x 变速、A/V offset、200/s 波形、A/B 循环/定帧、
  sidecar 标记/备注、S 截图、F/双击全屏、T6.2 全局步进/Home/End/+/-、文本/模态让位、
  T6.3 音量/波形跨会话、T6.4 100–200% 响应式侧栏/信息栏与 `--dpi-test`。
- 未完成：T6.5 状态横幅、T7 性能/稳定/交付；LRU 帧缓存、后台 index worker、
  正式 V1–V8/V10、基线机/4K、10min soak、设备恢复、波形/听感/截图/输入法真人验收。
- 已知缺口：长 GOP 循环 B→A 148–157ms 未达 F4-A2；F6-A8 只有 V9 短 GOP 数字，
  不是正式 P9；T6.2 的标记树 Home/End/方向键导航已让位给全局帧键（spec §3.7 决策）；
  T6.3 真实 %APPDATA% 写入受本会话执行环境限制未验证；T6.4 只用 QT_SCALE_FACTOR
  模拟 100/125/150/200%，没有真实 Windows 显示缩放、4K、多显示器、真人字体/IME。
  不要把开发机数字冒充基线机或真实 DPI 通过。

## 已交付、直接复用（不要重写）

- MainWindow 的 qApp 事件过滤器：PgUp/PgDn/←/→（300ms→70ms 单连发）、Home/End、
  +/-；文本输入/模态对话框/Ctrl/Alt/Meta 让位；OS 重复只吞不执行；
  焦点切换/窗口失活/到边界停止。GfxWidget 不再有步进定时器。
- T6.4 布局：
  - 初始窗口按屏幕可用区收缩（100% 1280×720；200% 928×452）；
  - 侧栏 `clamp(窗口宽×0.26, 240, 300)`；
  - InfoBar 在 <760 逻辑宽度换两行（文件名+帧号+时间码 / 格式+总帧），
    文件名 `ElideMiddle`；`InfoBar::FileLabelTextForTest`、`NarrowModeForTest`；
  - `<1000` 隐藏状态栏 Qt/FFmpeg 版本标签（音频/循环/波形/音量/帧号保留）；
  - GfxWidget 最小 320×180；
  - `--dpi-test --dpi-expect-scale <1|1.25|1.5|2> --dpi-tag <名>`（需 `--file`）。
- `core/AppSettings`（`lapian_settings`）：`%APPDATA%\拉片软件\settings.json`，
  schema 1，QSaveFile 原子写；`--settings-test`/`--settings-expect`。
- `--key-test`（60s 短 GOP 合成片，75 项）、`--fullscreen-test`、`--snapshot-test`、
  `--note-test`、`--mark-test`、`--ab-test`、`--loop-test` 都是回归。
- 不改 sidecar schema、MarkStore、MarkPanel 编辑/焦点语义、D3D11 锁、全屏语义、
  设置文件格式、T6.4 的断点/侧栏宽度口径。

## 下一步：T6.5 状态横幅

目标（spec §4.2/§3.1/§3.2；tasks.md T6.5）：
- 覆盖：VFR、超出保证范围、无音频、总帧扫描中、10-bit、错误。
- 验收：F2-A7、F1-A4、§4.2；横幅不遮挡关键控件（时间轴、帧号、播放/音量/循环）。
- 手测 + 自动化；不能因为“提示能用”就宣称 F2-A7/F1-A4 通过。

现状：
- InfoBar 已有 `banner_label_`：VFR 与“总帧数扫描中”已显示；需要统一/补齐
  超出保证范围、无音频、10-bit、错误，并避免与状态栏消息重复。
- MediaInfo/MediaProbe 已提供 codec、分辨率、fps、bit depth/10-bit、has_audio、
  rotation、VFR suspect；错误路径在 OpenFile/解码/音频设备。
- 状态栏已有 `statusBar()->showMessage` 与永久标签；不要把横幅做成会遮挡
  时间轴/帧号的浮层。

建议：
- 先列出状态来源与优先级（错误 > 10-bit/HDR > 超出保证范围 > VFR > 无音频 > 扫描中），
  同一时刻只显示最重要的一条；其余进状态栏/提示，不叠 banner。
- 横幅放在 InfoBar 内、参与布局（不要覆盖控件）；文案短，提供 tooltip/详情。
- 纯状态判定抽函数做单测；UI 用 `--banner-test` 或复用 `--dpi-test` 检查
  InfoBar 高度/不遮挡；至少覆盖 VFR、无音频、10-bit、超范围、错误。
- 不改 T6.4 的 InfoBar 单行/两行断点；新增横幅时确认 100%/200% 都放得下。
- 保持 build-release.ps1、ctest（18/18）、dist 通过；补交接 + perf 证据。

## 构建与验证

powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
# 末尾 "Cannot open E:/" 是 windeployqt 警告；以退出码和 [OK] 为准。

$tmp = Join-Path $PWD '.deps\tmp'; $env:TMP=$tmp; $env:TEMP=$tmp
$env:PATH="$PWD\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build\release -C Release --output-on-failure

# T6.4 DPI 回归（QT_SCALE_FACTOR 模拟）
$exe = Join-Path $PWD 'dist\lapian.exe'
$video = Join-Path $PWD 'build\t6.2-final\fs.mp4'
foreach ($spec in @(@('1','100'),@('1.5','150'),@('2','200'))) {
  $env:QT_SCALE_FACTOR=$spec[0]
  Start-Process -FilePath $exe -ArgumentList @('--file',$video,'--dpi-test',
    '--dpi-expect-scale',$spec[0],'--dpi-tag',$spec[1],
    '--settings-path',(Join-Path $PWD 'build\t6.5-dpi-settings')) -Wait -PassThru
}
Remove-Item Env:QT_SCALE_FACTOR

# T6.2 键位回归（60s 短片；没有则用 ffmpeg 生成）
$p = Start-Process -FilePath $exe -ArgumentList @('--file',(Join-Path $PWD 'build\t6.3-key\A.mp4'),'--key-test','--settings-path',(Join-Path $PWD 'build\t6.5-key-settings')) -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t6.5-key.log')
Write-Host "key exit=$($p.ExitCode)"

# T6.1/T5.4 回归：--fullscreen-test / --snapshot-test；note/mark 必须给 --file-b
# T6.3 回归：--settings-test + --settings-expect；ab/loop 用 media\测试.mp4

# 手动：100%/150%/200% 真实显示设置走查；VFR/无音频/10-bit/超范围/错误横幅

## 环境/沙箱坑

- 本会话执行环境拒绝非系统进程写 `%APPDATA%`：直接启动的 `lapian*.exe`/Qt 测试是
  Low 完整性级别，CreateFile/CreateDirectory 返回 5/2；Medium IL 计划任务里 `cmd`
  能写，但 `lapian*.exe` 仍被拒。T6.3 用重定向 APPDATA 验证；真实写入手测。
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
- QT_SCALE_FACTOR 只验证 Qt 逻辑布局，不等于 Windows 显示缩放；真实 DPI 手测单列。

工作方式：
- 计划内任务不用逐项确认；只有计划外、不可逆、规格歧义、权限/硬件限制才问。
- 一个任务 = 一次可审查变更；先提交代码，再提交文档（交接 + perf 证据）。
- 每个任务保持 build-release.ps1、ctest、dist 通过；性能任务补 docs/perf/ 数字。
- 只做 F1–F8/P1–P14；spec v0.3 冻结；不重做 T1–T6.4；
  不提前宣称真人键盘/IME、F1-A6 真人、F6/F8、F8-A1 真实 %APPDATA%、
  T6.4 真实显示缩放/多显示器、F4-A2 长 GOP、正式 V1/基线机通过。
- 上下文过长先告诉我“建议开新会话”，并把状态写进交接。

先给 3–5 行极短计划，然后直接做 T6.5。
```

## 使用说明

- 这段提示词自包含；新 agent 仍应先读文件、跑 `ctest`，不要只信这段摘要。
- 若 HEAD 已前进（交接/提示词提交），以 `git log` 和当前 `tasks.md` 为准。
- 若 T6.5 已完成，把“下一步”替换为 T7，并先读最新交接。
- T5.1 sidecar、T5.2 `ActivateMark` deferred 解码、T5.3 编辑 flush/焦点让位、
  T5.4 `displayed_frame_`/`SnapshotService`、T6.1 全屏、T6.2 窗口级键位过滤器、
  T6.3 `AppSettings`/`SettingsStore`、T6.4 响应式 InfoBar/侧栏都是已审查路径；
  不要重写。
- T6.5 是状态横幅的唯一主目标；不要顺带做 T7 或设置字段迁移。
