# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v13（2026-10-20；T6.5 状态横幅完成；下一步 T7.1 冷启动）

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL + D3D11 + WASAPI；spec.md v0.3 已冻结）。

请先读：
- E:\拉片软件开发\AGENTS.md
- docs\handoff-2026-10-20-t6.5.md（当前交接：状态横幅/优先级/状态栏）
- docs\handoff-2026-10-20-t6.4.md（DPI/布局）
- docs\handoff-2026-10-20-t6.3.md（设置持久化）
- docs\handoff-2026-10-20-t6.2.1.md（长按连发补丁）
- docs\handoff-2026-10-20-t2.9.1.md（反向拖动预览补丁）
- spec.md（重点 §3.1 F1-A4/A5、§3.2 F2-A7、§3.8、§4.1 P1/P2/P10/P11/P12、§4.2、§4.4）
- plan.md、tasks.md（M6 收口、M7 T7.1–T7.8）、docs\adr\ADR-001 到 ADR-006
- docs\perf\T6.5-banners.md、docs\perf\T6.4-dpi.md、docs\perf\T6.3-settings.md

第一步核对，不要相信旧对话的转述：
1. git status --short；git log --oneline -8
2. 读 tasks.md 的 M6（T6.5）与 M7（T7.1–T7.8）
3. 跑 ctest（命令见下），确认 19/19

当前基线（以 git log 为准）：
- HEAD 应为 `dad669a` 或更新；T6.5 代码 `dad669a`，文档提交紧随其后，
  以 git log 为准；工作区应 clean。
- M0–M4（含 T2.9.1）、T5.1–T5.4、T6.1–T6.5（含 T6.2.1）代码完成；
  ctest 19/19（约 14.2s）；dist 32.43MB / 25 文件。
- 已有：打开/播放/逐帧/seek/长按/帧号/时间码、时间轴缩放滚动/点击拖动/拖动预览、
  WASAPI、IAudioClock+QPC、0.25–2x 变速、A/V offset、200/s 波形、A/B 循环/定帧、
  sidecar 标记/备注、S 截图、F/双击全屏、T6.2 全局键位、T6.3 设置、T6.4 100–200% DPI、
  T6.5 状态横幅（错误/VFR/10-bit/超出保证/扫描中/无音频，唯一横幅 + 其余进状态栏）、
  T2.9.1 反向拖动、T6.2.1 自动重复 KeyRelease 修复。
- 未完成：T7 性能/稳定/交付；LRU 帧缓存、后台 index worker、正式 V1–V10、
  基线机/4K、10min soak、设备恢复、波形/听感/截图/输入法真人验收。
- 已知缺口：长 GOP 循环 B→A 148–157ms 未达 F4-A2；F6-A8 只有 V9 短 GOP 数字；
  T6.3 真实 %APPDATA% 写入受本会话执行环境限制未验证；T6.4 只用 QT_SCALE_FACTOR
  模拟 100/150/200%，没有真实 Windows 显示缩放、4K、多显示器、真人字体/IME；
  T2.9.1 反向拖动已由用户物理鼠标复测正常，但没有 4K/VFR/正式 P3/P4；
  T6.2.1 已由用户物理键盘复测长按正常；本 agent 只做了 QKeyEvent 合成自动重复 release；
  T6.5 的 VFR/10-bit 是 320×240 合成素材，没有真实 4K VFR/HDR 颜色验收，
  “总帧扫描中”当前没有真实异步索引，错误横幅只接打开失败。
  不要把用户手测写成正式 P3/P4/P8、F2-A7/F1-A4 或基线机通过。

## 已交付、直接复用（不要重写）

- T6.5 状态提示：
  - `media/StatusNotice.h/.cpp`：六类 `StatusNoticeKind`，优先级
    错误 > VFR > 10-bit > 超出保证 > 扫描中 > 无音频；
    `BuildMediaStatusNotices` / `SelectPrimaryNotice` / `SecondaryNoticeSummary`。
  - `InfoBar` 只有一个 `infoBanner`，参与 InfoBar 纵向布局；错误暗红、
    VFR/10-bit/超出保证琥珀、无音频/扫描中蓝灰；`SetError` 在下一次成功打开清除。
  - MainWindow 的 `noticeInfo` 是状态栏常驻“其他提示”标签（最大 240px、tooltip 全量）；
    不要改回 `statusBar()->showMessage`，它会被临时消息冲掉。
  - `MediaProbe` 已提供 `VideoStreamInfo.bit_depth`；`--banner-test` 覆盖真实
    4K/VFR/10-bit/无音频/错误 + 合成六类 + 820 窄窗。
  - 不要改优先级/唯一横幅/不遮挡控件的语义；T7 遇到真实后台索引时调用
    `SetTotal(Scanning)`，遇到打开失败调用 `SetError` 即可。
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
  `--fullscreen-test`、`--snapshot-test`、`--note-test`、`--mark-test`、
  `--ab-test`、`--loop-test` 都是回归。
- 不改 sidecar schema、MarkStore、MarkPanel 编辑/焦点语义、D3D11 锁、全屏语义、
  设置文件格式、T6.4 断点/侧栏宽度、T2.9.1 目标帧/缓存口径、T6.2.1 release 语义、
  T6.5 的 StatusNotice 优先级与状态栏分工。

## 下一步：T7.1 冷启动优化与测量

目标（spec §4.1 P1/P2；tasks.md T7.1）：
- P1 冷启动到窗口可交互 ≤1.5s；P2 双击已关联文件到首帧 ≤2.5s；
  20 次至少 19 次达标；报告进 docs/perf/T7.1-cold-start.md。
- 现状：MainWindow 构造里已 `ApplyInitialWindowSize` + 空态先出；T1.1 探测、
  T2.2 索引、T3.5 波形都在打开路径；T6.5 的状态横幅不阻塞首帧。
- 建议：先加启动埋点（进程启动/窗口 show/空态首帧/文件打开/首帧呈现），
  再按测量结果做懒初始化（D3D11 设备、波形、预览器、索引），不要先猜。
- 不改 T6.5 横幅语义；如果后台 index worker 出现，接 `SetTotal(Scanning)`
  并在索引完成后 `SetTotal(Exact/Estimated)`。
- 不碰 T2.9.1 的 32 帧预览缓存；T7.2 的正式 LRU 是另一件事。
- 保持 build-release.ps1、ctest（19/19）、dist 通过；补交接 + perf 证据。
- 不要提前宣称 P1/P2 达标、真实基线机、F2-A7/F1-A4 正式片单。

## 构建与验证

powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
# 末尾 "Cannot open E:/" 是 windeployqt 警告；以退出码和 [OK] 为准。

$tmp = Join-Path $PWD '.deps\tmp'; $env:TMP=$tmp; $env:TEMP=$tmp
$env:PATH="$PWD\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build\release -C Release --output-on-failure

# T6.5 状态横幅回归（100% / 200%，内部含 820 窄窗；74 项）
$v1 = (Resolve-Path 'media\proxy\proxy-V1.mp4').Path
& build\release\bin\lapian.exe --file $v1 --banner-test --banner-tag 100
$env:QT_SCALE_FACTOR='2'
& build\release\bin\lapian.exe --file $v1 --banner-test --banner-tag 200 --dpi-expect-scale 2
Remove-Item Env:QT_SCALE_FACTOR
# 可选 --banner-vfr / --banner-tenbit / --banner-noaudio 指向真实素材；
# 默认取 build\test-media\{vfr,10bit,noaudio}.mp4，4K 默认 media\proxy\proxy-V4.mp4。

# T6.4 DPI 回归（QT_SCALE_FACTOR 模拟；100/150/200 即可）
$exe = Join-Path $PWD 'dist\lapian.exe'
foreach ($spec in @(@('1','100'),@('1.5','150'),@('2','200'))) {
  $env:QT_SCALE_FACTOR=$spec[0]
  Start-Process -FilePath $exe -ArgumentList @('--file',$v1,'--dpi-test',
    '--dpi-expect-scale',$spec[0],'--dpi-tag',$spec[1],
    '--settings-path',(Join-Path $PWD 'build\t7.1-dpi-settings')) -Wait -PassThru
}
Remove-Item Env:QT_SCALE_FACTOR

# T6.2/T6.2.1 键位回归（60s 短片；期望 77 项、0 失败；2s 长按 27 帧）
$p = Start-Process -FilePath $exe -ArgumentList @('--file',(Join-Path $PWD 'media\测试.mp4'),
  '--key-test','--settings-path',(Join-Path $PWD 'build\t7.1-key-settings')) `
  -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t7.1-key.log')
Write-Host "key exit=$($p.ExitCode)"

# T6.1/T5.4 回归：--fullscreen-test / --snapshot-test；note/mark 必须给 --file-b；
# snapshot 旋转用 build\t5.4-probe\rot-in.mp4（1920×1080、90° display matrix）。
# T6.3 回归：--settings-test + --settings-expect；ab/loop 用 media\测试.mp4 或 proxy-V1。

# 手动：打开 VFR/无音频/10-bit/4K/损坏文件，看同一时刻只有一条横幅、
# 状态栏有其余提示；100%/200% 真实显示缩放下不裁切；物理键盘/鼠标常规回归。

## 环境/沙箱坑

- 本会话执行环境拒绝非系统进程写 `%APPDATA%`：直接启动的 `lapian*.exe`/Qt 测试是
  Low 完整性级别，CreateFile/CreateDirectory 返回 5/2；T6.3 用重定向 APPDATA 验证。
- 控制台测试的 Windows main() argv 是 ANSI：中文路径会乱码；用工作区相对 ASCII
  路径或 QCoreApplication::arguments()。
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
- mark/note 测试前清 media 下的临时 `.lapian.json`，避免旧测试标记影响“count=1”断言。

工作方式：
- 计划内任务不用逐项确认；只有计划外、不可逆、规格歧义、权限/硬件限制才问。
- 一个任务 = 一次可审查变更；先提交代码，再提交文档（交接 + perf 证据）。
- 每个任务保持 build-release.ps1、ctest、dist 通过；性能任务补 docs/perf/ 数字。
- 只做 F1–F8/P1–P14；spec v0.3 冻结；不重做 T1–T6.5（T2.9.1、T6.2.1、T6.5 已完成）；
  不提前宣称 IME/AltGr、F1-A6 真人、F6/F8、F8-A1 真实 %APPDATA%、
  4K/VFR/正式 P3/P4/P8、T6.2.1 硬件级自动化、T6.4 真实显示缩放/多显示器、
  T6.5 真实 VFR/10-bit/HDR、F4-A2 长 GOP 或正式 V1/基线机通过。
- 上下文过长先告诉我“建议开新会话”，并把状态写进交接。

先给 3–5 行极短计划，然后直接做 T7.1。
```

## 使用说明

- 这段提示词自包含；新 agent 仍应先读文件、跑 `ctest`，不要只信这段摘要。
- 若 HEAD 已前进（交接/提示词提交），以 `git log` 和当前 `tasks.md` 为准。
- T6.5 已完成；下一步是 T7.1 冷启动，然后 T7.2 内存/缓存、T7.3 2h 稳定、
  T7.4 4K、T7.5 错误/鲁棒、T7.6 打包/关联、T7.7 全量验收、T7.8 基准脚本。
- T6.5 的 `StatusNotice`/唯一横幅/状态栏分工、T2.9.1 预览目标帧/缓存、
  T6.2.1 自动重复 KeyRelease 判断都是已审查路径；不要重写。
