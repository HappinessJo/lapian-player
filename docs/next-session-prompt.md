# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v14（2026-10-20；T7.1 冷启动埋点与索引后置完成；下一步 T7.2 内存/缓存）

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL + D3D11 + WASAPI；spec.md v0.3 已冻结）。

请先读：
- E:\拉片软件开发\AGENTS.md
- docs\handoff-2026-10-20-t7.1.md（当前交接：冷启动埋点/后台索引/P1/P2）
- docs\handoff-2026-10-20-t6.5.md（状态横幅/优先级/状态栏）
- docs\handoff-2026-10-20-t6.4.md（DPI/布局）
- docs\handoff-2026-10-20-t6.3.md（设置持久化）
- docs\handoff-2026-10-20-t6.2.1.md（长按连发补丁）
- docs\handoff-2026-10-20-t2.9.1.md（反向拖动预览补丁）
- spec.md（重点 §3.1 F1-A4/A5、§3.2 F2-A7、§3.8、§4.1 P1/P2/P10/P11/P12、§4.2、§4.4）
- plan.md、tasks.md（M7 T7.1–T7.8）、docs\adr\ADR-001 到 ADR-006
- docs\perf\T7.1-cold-start.md、docs\perf\T6.5-banners.md、docs\perf\T6.4-dpi.md、docs\perf\T6.3-settings.md

第一步核对，不要相信旧对话的转述：
1. git status --short；git log --oneline -8
2. 读 tasks.md 的 M7（T7.1–T7.8）与 T7.1 的完成说明
3. 跑 ctest（命令见下），确认 19/19

当前基线（以 git log 为准）：
- 代码提交 `887beda` 或更新；本提示词/交接的文档提交在其后；工作区应 clean。
- M0–M4（含 T2.9.1）、T5.1–T5.4、T6.1–T6.5（含 T6.2.1）、T7.1 代码完成；
  ctest 19/19（约 13.89s）；dist 32.45MB / 25 文件。
- 已有：打开/播放/逐帧/seek/长按/帧号/时间码、时间轴缩放滚动/点击拖动/拖动预览、
  WASAPI、IAudioClock+QPC、0.25–2x 变速、A/V offset、200/s 波形、A/B 循环/定帧、
  sidecar 标记/备注、S 截图、F/双击全屏、T6.2 全局键位、T6.3 设置、T6.4 100–200% DPI、
  T6.5 状态横幅（错误/VFR/10-bit/超出保证/扫描中/无音频，唯一横幅 + 其余进状态栏）、
  T2.9.1 反向拖动、T6.2.1 自动重复 KeyRelease 修复、T7.1 启动埋点/后台索引。
- 未完成：T7.2–T7.8；LRU 帧缓存、正式 V1–V10、基线机/冷缓存 P1/P2、4K 真素材、
  10min soak、设备恢复、波形/听感/截图/输入法真人验收。
- 已知缺口：长 GOP 循环 B→A 148–157ms 未达 F4-A2；F6-A8 只有 V9 短 GOP 数字；
  T6.3 真实 %APPDATA% 写入受本会话执行环境限制未验证；T6.4 只用 QT_SCALE_FACTOR
  模拟 100/150/200%，没有真实 Windows 显示缩放、4K、多显示器、真人字体/IME；
  T2.9.1 反向拖动已由用户物理鼠标复测正常，但没有 4K/VFR/正式 P3/P4；
  T6.2.1 已由用户物理键盘复测长按正常；本 agent 只做了 QKeyEvent 合成自动重复 release；
  T6.5 的 VFR/10-bit 是 320×240 合成素材，没有真实 4K VFR/HDR 颜色验收；
  T7.1 的 P1/P2 是开发机暖文件缓存，没有清 OS 缓存、没有基线机/正式 V1。
  不要把用户手测写成正式 P3/P4/P8、F2-A7/F1-A4、P1/P2 或基线机通过。

## 已交付、直接复用（不要重写）

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
  - 旧 GUI 回归用 `SetIndexSynchronousForTest(true)` 保持“打开后立即可导航”；
    不要把这个开关改成产品默认，也不要在产品路径重新同步建索引。
  - `FrameIndex::BuildFromFile(..., cancel)` 每包检查取消；不要改成不可取消的长循环。
  - `--index-test`（12 项）：扫描中 → 就绪 → V9 标记加载；`scripts/startup-bench.ps1`
    做 20 次 P1/P2。
- T6.5 状态提示：
  - `media/StatusNotice.h/.cpp`：六类 `StatusNoticeKind`，优先级
    错误 > VFR > 10-bit > 超出保证 > 扫描中 > 无音频；
    `BuildMediaStatusNotices` / `SelectPrimaryNotice` / `SecondaryNoticeSummary`。
  - `InfoBar` 只有一个 `infoBanner`，参与 InfoBar 纵向布局；错误暗红、
    VFR/10-bit/超出保证琥珀、无音频/扫描中蓝灰；`SetError` 在下一次成功打开清除。
  - MainWindow 的 `noticeInfo` 是状态栏常驻“其他提示”标签（最大 240px、tooltip 全量）；
    不要改回 `statusBar()->showMessage`，它会被临时消息冲掉。
  - 不要改优先级/唯一横幅/不遮挡控件的语义；T7 遇到真实后台索引时调用
    `SetTotal(Scanning)`，遇到打开失败调用 `SetError` 即可（T7.1 已接）。
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
  `--ab-test`、`--loop-test`、`--index-test` 都是回归。
- 不改 sidecar schema、MarkStore、MarkPanel 编辑/焦点语义、D3D11 锁、全屏语义、
  设置文件格式、T6.4 断点/侧栏宽度、T2.9.1 目标帧/缓存口径、T6.2.1 release 语义、
  T6.5 的 StatusNotice 优先级与状态栏分工、T7.1 的扫描中/就绪语义。

## 下一步：T7.2 内存预算与缓存上限

目标（spec §4.1 P10/P11；tasks.md T7.2）：
- P10 打开文件暂停 60s ≤250MB；P11 1080p 播放 ≤800MB、4K ≤1.5GB；
  切文件要释放；队列/缓存有界。
- 现状：还没有正式 LRU 帧缓存（T2.9.1 的 32 帧预览缓存约 44MB 是独立的一层，
  不要顺手改）；T7.1 新增了后台索引线程和一份 FrameIndex（每帧 16B + keyframe 向量），
  量一下 4573 帧与 2h 外推。
- 建议顺序：先在空态/V1 暂停 60s/1080p 播放/4K 打开与播放/切文件后各量 working set
  与 private bytes（`GetProcessMemoryInfo`），再按最大头做上限；不要先猜。
- 缓存/队列：帧缓存、D3D11 纹理、波形 bucket、FrameIndex、截图/预览队列；
  切文件/关窗要释放；上限要可测（getter/日志）。
- 不要动 T7.1 的异步索引语义；如果 T7.2 要限制索引内存，只能限制数据结构/生命周期，
  不能把索引改回同步阻塞首帧。
- 保持 build-release.ps1、ctest（19/19）、dist 通过；补交接 + perf 证据。
- 不要提前宣称 P10/P11、基线机、正式 V1/V4 或 2h soak 通过。

## 构建与验证

powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
# 末尾 "Cannot open E:/" 是 windeployqt 警告；以 [OK] 和退出码为准。

$tmp = Join-Path $PWD '.deps\tmp'; $env:TMP=$tmp; $env:TEMP=$tmp
$env:PATH="$PWD\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build\release -C Release --output-on-failure

# T7.1 冷启动（20 次 P1/P2；用 dist，避免 PATH 依赖）
powershell -ExecutionPolicy Bypass -File scripts\startup-bench.ps1 `
  -Exe dist\lapian.exe -File media\proxy\proxy-V1.mp4 -Runs 20 `
  -OutDir build\t7.2-startup-proxy
powershell -ExecutionPolicy Bypass -File scripts\startup-bench.ps1 `
  -Exe dist\lapian.exe -File media\测试.mp4 -Runs 20 `
  -OutDir build\t7.2-startup-1080

# T7.1 异步索引回归（12 项）
dist\lapian.exe --file media\测试.mp4 --file-b media\V9.mp4 --index-test `
  --settings-path build\t7.2-index-settings

# T6.5 状态横幅回归（100% / 200%，内部含 820 窄窗；74 项）
$v1 = (Resolve-Path 'media\proxy\proxy-V1.mp4').Path
& dist\lapian.exe --file $v1 --banner-test --banner-tag 100
$env:QT_SCALE_FACTOR='2'
& dist\lapian.exe --file $v1 --banner-test --banner-tag 200 --dpi-expect-scale 2
Remove-Item Env:QT_SCALE_FACTOR

# T6.4 DPI 回归（QT_SCALE_FACTOR 模拟；100/150/200）
foreach ($spec in @(@('1','100'),@('1.5','150'),@('2','200'))) {
  $env:QT_SCALE_FACTOR=$spec[0]
  Start-Process -FilePath (Join-Path $PWD 'dist\lapian.exe') -ArgumentList @('--file',$v1,'--dpi-test',
    '--dpi-expect-scale',$spec[0],'--dpi-tag',$spec[1],
    '--settings-path',(Join-Path $PWD 'build\t7.2-dpi-settings')) -Wait -PassThru
}
Remove-Item Env:QT_SCALE_FACTOR

# T6.2/T6.2.1 键位回归（dist；期望 77 项、0 失败；2s 长按 27 帧）
$p = Start-Process -FilePath (Join-Path $PWD 'dist\lapian.exe') -ArgumentList @('--file',(Join-Path $PWD 'media\测试.mp4'),
  '--key-test','--settings-path',(Join-Path $PWD 'build\t7.2-key-settings')) `
  -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t7.2-key.log')
Write-Host "key exit=$($p.ExitCode)"

# T6.1/T5.4：--fullscreen-test / --snapshot-test；note/mark 给 --file-b；
# snapshot 旋转用 build\t5.4-probe\rot-in.mp4（1920×1080、90° display matrix）。
# T6.3：--settings-test + --settings-expect；ab/loop 用 media\测试.mp4 或 proxy-V1。

# 手动：打开 152s 1080p，看首帧、扫描中横幅、时间轴短暂禁用、索引后恢复；
# 打开 VFR 确认仍是 VFR + 扫描中；开关文件确认标记/预览/导航正常。

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
- mark/note 测试前清 media 下的临时 `.lapian.json`，避免旧测试标记影响“count=1”断言；
  测试后清掉（media/测试.mp4.lapian.json、media/snapshots、根目录 .lapian.json）。
- 测试.mp4 是 152s 1080p，索引/首帧解码会产生大量 H.264 "missing picture" 日志；
  解析 `T7.1` 行即可，不要把它当崩溃。
- `--startup-test` 有文件时在 first_present 后 80ms 就退出，后台索引可能还没完成；
  要看索引完成状态用 `--index-test` 或 `LAPIAN_STARTUP_LOG=1`。
- `--index-test` 的扫描中观察窗是 120ms；如果机器快到索引先完成，会打印
  `index_test scan_too_fast` 并跳过扫描断言，不要把它当失败，但要在报告里说明。

工作方式：
- 计划内任务不用逐项确认；只有计划外、不可逆、规格歧义、权限/硬件限制才问。
- 一个任务 = 一次可审查变更；先提交代码，再提交文档（交接 + perf 证据）。
- 每个任务保持 build-release.ps1、ctest、dist 通过；性能任务补 docs/perf/ 数字。
- 只做 F1–F8/P1–P14；spec v0.3 冻结；不重做 T1–T7.1；
  不提前宣称 IME/AltGr、F1-A6 真人、F6/F8、F8-A1 真实 %APPDATA%、
  4K/VFR/正式 P3/P4/P8、T6.2.1 硬件级自动化、T6.4 真实显示缩放/多显示器、
  T6.5 真实 VFR/10-bit/HDR、T7.1 冷缓存/基线机/正式 V1 P1/P2、
  F4-A2 长 GOP 或正式 V1/基线机通过。
- 上下文过长先告诉我“建议开新会话”，并把状态写进交接。

先给 3–5 行极短计划，然后直接做 T7.2。
```

## 使用说明

- 这段提示词自包含；新 agent 仍应先读文件、跑 `ctest`，不要只信这段摘要。
- 若 HEAD 已前进（交接/提示词提交），以 `git log` 和当前 `tasks.md` 为准。
- T7.1 已完成；下一步 T7.2 内存/缓存，然后 T7.3 2h 稳定、T7.4 4K、T7.5 鲁棒性、
  T7.6 打包/关联、T7.7 全量验收、T7.8 基线脚本。
- T7.1 的 `--startup-test`/`--index-test`/同步测试开关不要重写；T7.2 只在其上做
  内存/缓存增量。
