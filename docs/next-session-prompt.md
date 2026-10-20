# 给下个 agent 的新会话提示词（可直接复制）

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL + D3D11 + WASAPI；spec.md v0.3 已冻结）。

请先读：
- E:\拉片软件开发\AGENTS.md
- docs\handoff-2026-10-20-t5.3.md（当前交接）
- docs\handoff-2026-10-20-t5.2.md（标记 UI/跳转接口）
- docs\handoff-2026-10-20-t5.1.md（sidecar 存储接口）
- docs\handoff-2026-10-20-t4.2.md（循环/定帧历史）
- spec.md（v0.3，冻结）、plan.md、tasks.md
- docs\adr\ADR-001 到 ADR-006（尤其 006 持久化、005 帧缓存/预览）
- docs\perf\T5.3-notes.md（T5.3 数字与缺口）

第一步核对，不要相信旧对话的转述：
1. git status --short；git log --oneline -8
2. 读 tasks.md 的 M5（T5.1–T5.4）
3. 跑一次 ctest（命令见下），确认 16/16

当前基线（以 git log 为准）：
- 参考基线：T5.3 代码提交 `c46e989`（文档提交紧随其后；以 git log 为准）；工作区应 clean。
- M0–M4、T5.1 存储层、T5.2 标记 UI、T5.3 备注编辑代码完成；ctest 16/16；dist 32.15MB / 25 文件。
- 已有：打开/播放/逐帧/seek/长按/帧号/时间码、时间轴缩放滚动/点击拖动/拖动预览、
  WASAPI、IAudioClock+QPC、0.25–2x 变速/变调、A/V offset、200/s 波形生成与绘制、
  步进/拖动静音、A/B 色条/循环播放/A=B 定帧；T5.1 sidecar 存储；T5.2 Ctrl+M、
  侧栏标记列表/删除确认/时间轴粉色标记与 5px 吸附；T5.3 标签/备注编辑、
  失焦/Enter/Ctrl+Enter 保存、关窗 flush、焦点隔离。
- 未完成：T5.4 截图、LRU 帧缓存、后台 index worker、全屏/键位/DPI、正式 V1–V8/V10、
  基线机/4K、10min soak、设备拔出恢复、波形/听感真人验收。
- 已知缺口：长 GOP proxy-V1 循环 B→A 148–157ms，未达 F4-A2；F6-A8 只有 V9 1080p
  短 GOP 数字（T5.2 UI p95 6.22ms、画面 p95 30.76ms），不是正式 P9；T5.3 的
  F8-A2/F6-A1 只有程序化输入，中文 IME/真人键盘仍待；不要把短 GOP 数字冒充
  全素材/基线机通过。

## T5.3 已交付的接口（直接复用，不要重写）

- `src/app/MarkPanel.h` / `.cpp`（`lapian_mark_ui`）：
  - `SetMarks/SetStorage/SetWarnings/SelectMark/SelectNearFrame/SelectedMarkId/MarkCount/
    WarningCount/StorageSummary`；
  - T5.3 新增：`FlushEditor()`、`UpdateMarkText(id,label,note)`、`markTextEdited(id,label,note)`、
    `SetEditorTextForTest/EditorLabelForTest/EditorNoteForTest/FocusNoteEditorForTest`；
  - 编辑区控件名：`markEditor`/`markLabelEdit`/`markNoteEdit`/`markEditorTitle`；
  - 标签 Enter 保存；备注 Enter 换行、Ctrl+Enter 保存；失焦/切换选中/关窗都提交；
    编辑框接受除 Esc 外的 `ShortcutOverride`（spec §3.7 焦点隔离）。
- `MainWindow` 内部：
  - `ApplyMarkTextEdited`（SetMarkText + SaveMarks(true)）、`FlushMarkEditor()`；
    已接 `FlushMarksBeforeSwitch/FlushMarksOnClose/ApplyAddMark/ApplyDeleteMark`；
  - 全局快捷键入口都有 `IsTextInputFocused()` 让位（Space/W/M/Ctrl+G/Ctrl+O/循环/速度/缩放/
    Ctrl+M；Delete 在 MarkPanel 内让位）；新增快捷键必须同时加事件过滤与入口检查；
  - T5.3 测试入口：`SetMarkNoteForTest/CommitNoteForTest/MarkTextForTest/
    DeleteMarkWithConfirmForTest/FocusNoteEditorForTest/CloseForTest/
    ResetNotePerfSamplesForTest/NotePerfSummaryForTest`。
- 自动化：`--note-test`（`--file A --file-b B`，A/B 各拷一份 V9；退出码 0/1）；
  `--mark-test` 仍是 T5.2 回归；`--ab-test`、`--loop-test` 不变。
- sidecar 命名仍是 `<视频完整文件名>.lapian.json`；不要改 schema 字段名、不要建 SQLite。

## 下一步：T5.4 截图（tasks.md T5.4）

目标与验收：
- `S` 截取当前显示帧，PNG；不暂停播放；不含 UI；源帧分辨率；按 display matrix
  旋转到显示方向（竖屏 V5 为 1080×1920）。
- 默认视频同目录 `snapshots/`；目录不可写回退用户图片目录；给出提示。
- 命名 `{视频名}_f{帧号}_{HH-MM-SS-FF}.png`；重名自动加序号，不覆盖。
- F6-A3/A4/A5/A7；单测：命名/非法字符；4K 截图不中断播放、P11 不超。

实现建议：
- 截图来源必须是“按键瞬间已显示的帧”，不要重新 seek；优先从 GfxWidget/MediaSession
  持有的当前 AVFrame 做 CPU 拷贝（NV12/P010 → RGBA 或直接 WIC 编码），不要在 UI 线程
  做 4K 回读+编码；写盘/编码放后台线程，按键路径只抓引用/拷贝。
- 旋转用 `MediaInfo.rotation.counterclockwise_degrees`（T1.3 已有渲染路径），
  输出尺寸按显示方向；WIC PNG（Windows Imaging Component），不要引入新第三方库。
- 命名/序号/非法字符抽成纯函数单测；目录回退与只读目录要有可测入口。
- 自动化建议 `--snapshot-test`：V9 帧 100 按 3 次（3 个文件、不覆盖、PNG 尺寸 1920×1080）、
  文件名含 `f100`、图像无 UI；再跑 `--note-test`/`--mark-test` 防回归。
- 不宣称 F6-A3/A4/A5/A7 的 4K/竖屏/只读目录真人验收，除非真的跑了。

构建与验证命令：
# 全量构建 + 单测 + dist
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
# 末尾 "Cannot open E:/" 是 windeployqt 警告；以退出码和 [OK] 为准。

# 只跑 ctest（需要 Qt bin 与仓库内 TMP/TEMP）
$tmp = Join-Path $PWD '.deps\tmp'
$env:TMP=$tmp; $env:TEMP=$tmp
$env:PATH="$PWD\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build\release -C Release --output-on-failure

# T5.3 备注自动化（V9 拷到 build，避免污染 media；GUI 程序用 Start-Process 抓 stderr）
$dir = Join-Path $PWD 'build\t5.4-notes'
Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $dir | Out-Null
Copy-Item '.\media\V9.mp4' (Join-Path $dir 'A.mp4')
Copy-Item '.\media\V9.mp4' (Join-Path $dir 'B.mp4')
$p = Start-Process -FilePath (Join-Path $PWD 'dist\lapian.exe') `
  -ArgumentList @('--file',(Join-Path $dir 'A.mp4'),'--file-b',(Join-Path $dir 'B.mp4'),'--note-test') `
  -Wait -PassThru -RedirectStandardError (Join-Path $PWD 'build\t5.4-note.log')
Write-Host "exit=$($p.ExitCode)"

# T5.2 标记回归同样用 build\t5.4-marks 的 A/B 副本 + --mark-test。

# 手动回归
.\dist\lapian.exe --file ".\media\测试.mp4"
# 100 I / 160 O / L 循环；100 I / O / L / Space 定帧；Shift+L 清除
# 自动化入口：--ab-test（proxy-V1）、--loop-test（测试.mp4）、--step-test（V9，不自动退出，外部限时）

环境/沙箱坑：
- 若构建在 Qt moc 处报 "libuv process spawn failed: operation not permitted"，是沙箱
  禁止命名管道；需要放宽到 danger-full-access 或请用户批准后重跑，不是代码错误。
- 若 MSBuild 报 Temp 访问被拒绝，先使用仓库内 TMP/TEMP（build-release.ps1 会自动做）。
- Windows main() 的 argv 是 ANSI 代码页；中文路径不要用 argv 传，改用 Qt
  QCoreApplication::arguments() 或环境变量。
- media/、dist/、build/ 都不入库；没有素材时先向用户确认，不要编造 V1 验收。
- `--step-test` 不会自动退出；回归时用 Start-Process + 3s 后 Stop-Process，或只跑 ctest。
- 截图 `QWidget::grab()` 抓不到 GfxWidget 的 D3D11 原生 HWND，视频区会是黑的，不是播放失败。
- `dist\lapian.exe` 是 GUI 子系统程序；直接 `&` 运行可能拿不到 stderr/退出码，自动化用
  Start-Process + RedirectStandardError + PassThru。
- 控制台是 GBK 时中文日志会花屏；日志本身是 UTF-8，用
  `[System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes(path))` 读。

工作方式：
- 计划内任务不要逐项找我确认，直接做；只有计划外、不可逆、规格歧义、权限/硬件限制才问。
- 一个任务 = 一次可审查变更；先提交代码，再提交文档（交接 + perf 证据）。
- 每个任务必须保持 scripts\build-release.ps1、ctest、dist 通过；性能任务补 docs/perf/ 数字。
- 只做 F1–F8 / P1–P14；候选池不进首期；技术栈按 ADR-001/004；spec v0.3 冻结，
  改动只走 §9 版本记录。
- 不提交 third_party/、media/、build/、dist/；不要删 docs/perf/ 证据。
- 不要重做 T1–T5.3；不要提前宣称 F6/F8、F4-A2 长 GOP、正式 V1/基线机通过。
- 如果上下文变长到影响判断，先告诉我“建议开新会话”，并把当前状态写进交接，不要硬撑。

开始时可给一个 3–5 行的极短计划，但不用等我确认；随后直接改文件。先做 T5.4。
```

## 使用说明

- 这段提示词自包含；新 agent 仍应先读文件、跑 `ctest`，不要只信这段摘要。
- 若 HEAD 已前进（交接/提示词提交），以 `git log` 和当前 `tasks.md` 为准。
- 若 T5.4 已完成，把“下一步”替换为 `tasks.md` 中下一个未完成任务（M6），并先读最新交接。
- T5.1 的 sidecar、T5.2 的 `ActivateMark` deferred 解码、T5.3 的编辑 flush 与焦点让位都是
  已审查路径；不要为截图重写 MarkStore、MarkPanel 或改变关窗保存顺序。
