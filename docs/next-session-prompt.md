# 给下个 agent 的新会话提示词（可直接复制）

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL + D3D11 + WASAPI；spec.md v0.3 已冻结）。

请先读：
- E:\拉片软件开发\AGENTS.md
- docs\handoff-2026-10-20-t5.1.md（当前交接）
- docs\handoff-2026-10-20-t4.2.md（循环/定帧历史）
- docs\handoff-2026-10-20-t4.1.md（A/B 数据/UI 历史）
- spec.md（v0.3，冻结）、plan.md、tasks.md
- docs\adr\ADR-001 到 ADR-006（尤其 005 预览/缓存、006 持久化）
- docs\perf\T4.2-loop.md（循环数字与长 GOP 缺口）

第一步核对，不要相信旧对话的转述：
1. git status --short；git log --oneline -8
2. 读 tasks.md 的 M5（T5.1–T5.4）
3. 跑一次 ctest（命令见下），确认 15/15

当前基线（以 git log 为准）：
- 参考基线：T5.1 文档提交 9132202；本提示词更新后 HEAD 会再前进一格，以 git log 为准；工作区应 clean。
- M0–M4 与 T5.1 代码完成；ctest 15/15；dist 31.97MB / 25 文件。
- 已有：打开/播放/逐帧/seek/长按/帧号/时间码、时间轴缩放滚动/点击拖动/拖动预览、
  WASAPI、IAudioClock+QPC、0.25–2x 变速/变调、A/V offset、200/s 波形生成与绘制、
  步进/拖动静音、A/B 色条/循环播放/A=B 定帧；T5.1 sidecar 存储（schema v1、原子写、
  AppData 回退/迁回、id+updated_at 合并、损坏备份、空文件删除、frame+pts 校正）。
- 未完成：T5.2–T5.4（标记 UI/备注编辑/截图）、LRU 帧缓存、后台 index worker、
  全屏/键位/DPI、正式 V1–V8/V10、基线机/4K、10min soak、设备拔出恢复、
  波形/听感真人验收。
- 已知缺口：长 GOP 压力片 proxy-V1 的循环 B→A 为 148–157ms，未达 F4-A2 的 1 帧；
  已按可用档记录，优化留 T7。不要把短 GOP 的 13–20ms 冒充全素材通过。

## T5.1 已交付的接口（直接复用，不要重写）

- 存储层：`src/core/MarkStore.h` / `.cpp`；静态库 `lapian_marks`；测试
  `tests/mark_store_test.cpp`（161 断言）。
- `MarkStore::Open(video_path_utf8, duration_us, MarkStoreOptions, FrameLookup)`；
  `AddMark(frame, pts_us, label, note, &id)`、`SetMarkText(id, label, note)`、
  `RemoveMark(id)`、`Save()`；`storage_path()`、`using_fallback()`、`warnings()`。
- `MarkStoreOptions.appdata_marks_dir` 是完整的 `marks` 目录；生产留空即
  `%APPDATA%\拉片软件\marks`。
- sidecar 命名是 `<视频完整文件名>.lapian.json`（如 `测试.mp4.lapian.json`），
  不要改成 stem；若要改，先改 `ResolveMarkPaths` 与单测。
- `Open` 可能自动合并 sidecar/AppData 并迁移、写文件；加载后把 warnings 展示出来。
  保存是同步的，100 标记量级可接受；网络盘慢留 T7.5。
- 注意：`lapian_marks` 目前只被测试链接，`src/CMakeLists.txt` 的 `lapian` 目标还没
  链接它；T5.2 要补上。

下一步：T5.2 标记 UI 与跳转（T5.1 只做存储层，未接 MainWindow）

目标与验收（tasks.md T5.2）：
- Ctrl+M 在当前帧添加标记；侧栏列表显示帧号/标签/备注摘要；时间轴显示标记。
- 点击标记跳到该帧并暂停；删除标记需确认；100 个标记仍可操作。
- 打开文件时用 MediaInfo.duration_us + FrameIndex 构造 FrameLookup，调用
  MarkStore::Open；把 storage_path()/using_fallback()/warnings() 显示给用户
  （sidecar、AppData 回退、损坏备份、更高 schema、frame+pts 校正提示）。
- 手测 F6-A1/A2/A8；性能：点击标记 UI 响应 ≤100ms，画面精确到帧按 P9
  （1080p ≤200ms，4K ≤300ms）。F6-A8 未测前不要宣称。
- F8-A2/A3 的 UI 验收主要在 T5.3（备注编辑/关窗保存）；T5.2 先保证标记增删保存、
  切文件/关窗前 flush。F6-A6 在 UI 路径再确认一次。
- 参考 ADR-006；不改 schema 字段名、不建 SQLite、不做最近文件/跨进程锁。

实现建议：
- 复用 src/core/MarkStore.h；不要为了 UI 重写 JSON/原子写。`MarkStore::Open` 的
  `FrameLookup` 用 `FrameIndex::Entry(frame)->pts_us`、`FindExactPts(pts, &frame)` 包装。
- 侧栏可用 QListWidget/QTreeWidget；保持主画面/时间轴焦点逻辑；文本框聚焦时
  Ctrl+M 等全局键让位（spec §3.7）。
- TimelineWidget 加标记绘制：只画可见区，100 标记不能掉帧；点击标记的命中测试
  与现有 A/B/播放头不冲突。
- 切换文件/关窗前调用 Save；保存失败要提示，不静默丢标记。实际存储位置放状态栏
  或侧栏顶部，回退时明确写“%APPDATA% 回退”。
- 标记默认标签“帧 N”；备注先留空，T5.3 再接编辑与失焦/回车保存。

构建与验证命令：
# 全量构建 + 单测 + dist
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
# 末尾 "Cannot open E:/" 是 windeployqt 警告；以退出码和 [OK] 为准。

# 只跑 ctest（需要 Qt bin 与仓库内 TMP/TEMP）
$tmp = Join-Path $PWD '.deps\tmp'
$env:TMP=$tmp; $env:TEMP=$tmp
$env:PATH="$PWD\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build\release -C Release --output-on-failure

# 手动回归（不是 T5.2 验收，但别弄坏已有功能）
.\dist\lapian.exe --file ".\media\测试.mp4"
# 100 I / 160 O / L 循环；100 I / O / L / Space 定帧；Shift+L 清除
# 自动化入口：--ab-test（proxy-V1）、--loop-test（测试.mp4）、--step-test（V9）

环境/沙箱坑：
- 若构建在 Qt moc 处报 "libuv process spawn failed: operation not permitted"，是沙箱
  禁止命名管道；需要放宽到 danger-full-access 或请用户批准后重跑，不是代码错误。
- 若 MSBuild 报 Temp 访问被拒绝，先使用仓库内 TMP/TEMP（build-release.ps1 会自动做）。
- Windows main() 的 argv 是 ANSI 代码页；中文路径不要用 argv 传，改用 Qt
  QCoreApplication::arguments() 或环境变量（T5.1 手测入口已踩过）。
- media/、dist/、build/ 都不入库；没有素材时先向用户确认，不要编造 V1 验收。

工作方式：
- 计划内任务不要逐项找我确认，直接做；只有计划外、不可逆、规格歧义、权限/硬件限制才问。
- 一个任务 = 一次可审查变更；先提交代码，再提交文档（交接 + perf 证据）。
- 每个任务必须保持 scripts\build-release.ps1、ctest、dist 通过；性能任务补 docs/perf/ 数字。
- 只做 F1–F8 / P1–P14；候选池不进首期；技术栈按 ADR-001/004；spec v0.3 冻结，
  改动只走 §9 版本记录。
- 不提交 third_party/、media/、build/、dist/；不要删 docs/perf/ 证据。
- 不要重做 T1–T5.1；不要提前宣称 F6/F8、F4-A2 长 GOP、正式 V1/基线机通过。
- 如果上下文变长到影响判断，先告诉我“建议开新会话”，并把当前状态写进交接，不要硬撑。

开始时可给一个 3–5 行的极短计划，但不用等我确认；随后直接改文件。先做 T5.2。
```

## 使用说明

- 这段提示词自包含；新 agent 仍应先读文件、跑 `ctest`，不要只信这段摘要。
- 若 HEAD 已前进（交接/提示词提交），以 `git log` 和当前 `tasks.md` 为准。
- 若 T5.2 已完成，把“下一步”替换为 `tasks.md` 中下一个未完成任务，并先读最新交接。
- T5.1 交接的 sidecar 命名决策（含扩展名）如要改，先改 `ResolveMarkPaths` 与单测，
  再改 T5.2 的路径显示；不要在 UI 层绕过存储层。
