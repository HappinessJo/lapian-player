# 给下个 agent 的新会话提示词（可直接复制）

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL + D3D11 + WASAPI；spec.md v0.3 已冻结）。

请先读：
- E:\拉片软件开发\AGENTS.md
- docs\handoff-2026-10-20-t5.2.md（当前交接）
- docs\handoff-2026-10-20-t5.1.md（sidecar 存储接口）
- docs\handoff-2026-10-20-t4.2.md（循环/定帧历史）
- spec.md（v0.3，冻结）、plan.md、tasks.md
- docs\adr\ADR-001 到 ADR-006（尤其 006 持久化）
- docs\perf\T5.2-marks.md（T5.2 数字与缺口）

第一步核对，不要相信旧对话的转述：
1. git status --short；git log --oneline -8
2. 读 tasks.md 的 M5（T5.1–T5.4）
3. 跑一次 ctest（命令见下），确认 16/16

当前基线（以 git log 为准）：
- 参考基线：T5.2 代码提交 8ff61b7；本提示词更新后 HEAD 会再前进，以 git log 为准；工作区应 clean。
- M0–M4、T5.1 存储层与 T5.2 标记 UI 代码完成；ctest 16/16；dist 32.11MB / 25 文件。
- 已有：打开/播放/逐帧/seek/长按/帧号/时间码、时间轴缩放滚动/点击拖动/拖动预览、
  WASAPI、IAudioClock+QPC、0.25–2x 变速/变调、A/V offset、200/s 波形生成与绘制、
  步进/拖动静音、A/B 色条/循环播放/A=B 定帧；T5.1 sidecar 存储；T5.2 Ctrl+M、
  侧栏标记列表（帧/标签/备注摘要）、点击跳转+暂停、删除确认、时间轴粉色标记与
  5px 吸附、存储位置/回退/警告展示、切文件/关窗 flush。
- 未完成：T5.3 备注编辑/自动保存、T5.4 截图、LRU 帧缓存、后台 index worker、
  全屏/键位/DPI、正式 V1–V8/V10、基线机/4K、10min soak、设备拔出恢复、波形/听感真人验收。
- 已知缺口：长 GOP proxy-V1 循环 B→A 148–157ms，未达 F4-A2；T5.2 的 F6-A8 只有
  V9 1080p 短 GOP 数字（UI p95 5.91ms，画面 p95 31.34ms），不是正式 P9；不要把
  短 GOP 数字冒充全素材/基线机通过。

## T5.2 已交付的接口（直接复用，不要重写）

- `src/app/MarkPanel.h` / `.cpp`；静态库 `lapian_mark_ui`，测试 `tests/mark_panel_test.cpp`。
  - `SetMarks(vector<core::Mark>)`、`SetStorage(path, using_fallback)`、`SetWarnings(...)`；
  - `SelectMark(id)`、`SelectNearFrame(frame)`、`SelectedMarkId()`、`MarkCount()`、
    `WarningCount()`、`StorageSummary()`；
  - 信号 `markActivated(id, frame)`、`deleteRequested(id)`；Delete 键/右键菜单删除。
- `MainWindow` 内部：
  - `MarkStore::Open` 已用 `MediaInfo.duration_us` + `FrameIndex` 构造 `FrameLookup`；
  - `marks_`（`unique_ptr<MarkStore>`）、`marks_dirty_`、`SaveMarks(show_errors)`、
    `RefreshMarksUi(select_id)`、`UpdateMarkStorageUi()`、
    `FlushMarksBeforeSwitch()`、`FlushMarksOnClose()`、`ShowMarkSaveFailure()`、
    `ShowMarkWarnings()`；
  - 点击标记走 `ActivateMark(id, frame)`：先选中/播放头/帧号更新并 `repaint()`，
    再 `QTimer::singleShot(0)` 精确 `GoToFrame`；`navigation_generation_` 负责取消。
  - 删除确认：`ApplyDeleteMark(id, confirm=true)`，默认 No。
- `TimelineWidget`：`SetMarks(vector<int64_t>)`、`ClearMarks()`、`MarkFrameAtX()`；
  标记绘制不依赖 MarkStore，同帧去重、只画可见区。
- 自动化：`--mark-test`（`--file A --file-b B`，A/B 各拷一份 V9）；
  `MainWindow` 暴露 `AddMarkForTest/DeleteMarkForTest/ActivateMarkForTest/
  MarkStateForTest/MarkCountForTest/MarkFrameAtForTest/MarkPerfSummaryForTest` 等。
- sidecar 命名仍是 `<视频完整文件名>.lapian.json`；不要改 schema 字段名、不要建 SQLite。

## 下一步：T5.3 备注编辑与自动保存（tasks.md T5.3）

目标与验收：
- 侧栏可编辑标记标签与备注；失焦或回车即时保存；备注允许为空；纯文本。
- 焦点隔离：文本框聚焦时，除 Esc/Enter 外不触发全局快捷键（spec §3.7）。
- F8-A2：编辑备注后直接关窗，重开文件内容已保存；单测：空备注、覆盖保存；关窗不丢。
- 不改变 T5.1 sidecar/schema；不新增“保存”按钮；不做导出/分享。

实现建议：
- 在 MarkPanel 底部加详情编辑区：标签 QLineEdit + 备注 QPlainTextEdit；
  选中标记时载入，切换标记/清空前 flush。标签 Enter 保存；备注建议
  Ctrl+Enter 保存、Enter 换行（规格“回车保存”与多行备注有歧义，写进交接，
  不要静默选；若按 Enter 保存，则备注不能多行）。
- 保存调用 `MarkStore::SetMarkText(id, label, note)`，更新 `marks_dirty_`，
  走 `SaveMarks(true)`；可选 200–300ms 防抖，但失焦/回车/切文件/关窗必须 flush。
- 列表行只更新当前行文本，避免每次按键重建 100 行；保持选中与滚动位置。
- 焦点隔离：`IsTextInputFocused` 已覆盖 QLineEdit/QTextEdit/QPlainTextEdit；
  但 `TogglePlayback`（Space）、`ToggleMute`、`ShowGotoDialog` 等全局 QAction
  还没有统一让位，T5.3 要补，否则备注输入时按空格会播放。
- 自动化：MainWindow 加 `SetMarkNoteForTest(id 或 index, label, note)`、
  `CommitNoteForTest`、`CloseForTest`/`window.close()`；序列“编辑→关窗→重开→
  内容一致”；再跑 `--mark-test` 防回归。
- F6-A1 的备注部分、F6-A2 的确认对话框真人路径放到 T5.3 一起补；仍不宣称
  F6-A8 正式 P9、长 GOP F4-A2、正式 V1/基线机通过。

构建与验证命令：
# 全量构建 + 单测 + dist
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
# 末尾 "Cannot open E:/" 是 windeployqt 警告；以退出码和 [OK] 为准。

# 只跑 ctest（需要 Qt bin 与仓库内 TMP/TEMP）
$tmp = Join-Path $PWD '.deps\tmp'
$env:TMP=$tmp; $env:TEMP=$tmp
$env:PATH="$PWD\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build\release -C Release --output-on-failure

# T5.2 标记自动化（V9 拷到 build，避免污染 media）
$dir = Join-Path $PWD 'build\t5.3-marks'
Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $dir | Out-Null
Copy-Item '.\media\V9.mp4' (Join-Path $dir 'A.mp4')
Copy-Item '.\media\V9.mp4' (Join-Path $dir 'B.mp4')
.\dist\lapian.exe --file (Join-Path $dir 'A.mp4') `
  --file-b (Join-Path $dir 'B.mp4') --mark-test

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

工作方式：
- 计划内任务不要逐项找我确认，直接做；只有计划外、不可逆、规格歧义、权限/硬件限制才问。
- 一个任务 = 一次可审查变更；先提交代码，再提交文档（交接 + perf 证据）。
- 每个任务必须保持 scripts\build-release.ps1、ctest、dist 通过；性能任务补 docs/perf/ 数字。
- 只做 F1–F8 / P1–P14；候选池不进首期；技术栈按 ADR-001/004；spec v0.3 冻结，
  改动只走 §9 版本记录。
- 不提交 third_party/、media/、build/、dist/；不要删 docs/perf/ 证据。
- 不要重做 T1–T5.2；不要提前宣称 F6/F8、F4-A2 长 GOP、正式 V1/基线机通过。
- 如果上下文变长到影响判断，先告诉我“建议开新会话”，并把当前状态写进交接，不要硬撑。

开始时可给一个 3–5 行的极短计划，但不用等我确认；随后直接改文件。先做 T5.3。
```

## 使用说明

- 这段提示词自包含；新 agent 仍应先读文件、跑 `ctest`，不要只信这段摘要。
- 若 HEAD 已前进（交接/提示词提交），以 `git log` 和当前 `tasks.md` 为准。
- 若 T5.3 已完成，把“下一步”替换为 `tasks.md` 中下一个未完成任务，并先读最新交接。
- T5.2 的 sidecar 命名、`MarkPanel` 信号与 `ActivateMark` 的 deferred 解码是已审查路径；
  不要为 T5.3 的编辑方便重写 MarkStore 或改 sidecar 字段。
