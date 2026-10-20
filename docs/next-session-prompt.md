# 给下个 agent 的新会话提示词（可直接复制）

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL + D3D11 + WASAPI；spec.md v0.3 已冻结）。

请先读：
- E:\拉片软件开发\AGENTS.md
- docs\handoff-2026-10-20-t4.2.md（当前交接）
- docs\handoff-2026-10-20-t4.1.md（A/B 数据/UI 历史）
- spec.md（v0.3，冻结）、plan.md、tasks.md
- docs\adr\ADR-001 到 ADR-006（尤其 005 预览/缓存、006 持久化）
- docs\perf\T4.2-loop.md（循环数字与长 GOP 缺口）

第一步核对，不要相信旧对话的转述：
1. git status --short；git log --oneline -8
2. 读 tasks.md 的 M5（T5.1–T5.4）
3. 跑一次 ctest（命令见下），确认 14/14

当前基线（以 git log 为准）：
- 参考 HEAD：caf49df（T4.2 文档提交）；本提示词提交后会再前进一格；工作区应 clean。
- M0–M4 代码完成（T1.1–T4.2）；ctest 14/14；dist 31.97MB / 25 文件。
- 已有：打开/播放/逐帧/seek/长按/帧号/时间码、时间轴缩放滚动/点击拖动/拖动预览、
  WASAPI、IAudioClock+QPC、0.25–2x 变速/变调、A/V offset、200/s 波形生成与绘制、
  步进/拖动静音、A/B 色条/循环播放/A=B 定帧。
- 未完成：T5.1–T5.4（标记/备注/截图/sidecar）、LRU 帧缓存、后台 index worker、
  全屏/键位/DPI、正式 V1–V8/V10、基线机/4K、10min soak、设备拔出恢复、
  波形/听感真人验收。
- 已知缺口：长 GOP 压力片 proxy-V1 的循环 B→A 为 148–157ms，未达 F4-A2 的 1 帧；
  已按可用档记录，优化留 T7。不要把短 GOP 的 13–20ms 冒充全素材通过。

下一步：T5.1 sidecar 存储（先做存储层，不接标记 UI）

目标与验收（tasks.md T5.1）：
- 默认 `<视频名>.lapian.json`，UTF-8、带 schema 版本；含视频标识（规范化路径、
  大小、时长）与 marks 数组（id、frame、pts、label、note、created_at、updated_at）。
- 原子写：同目录临时文件 → flush → ReplaceFile/rename；失败不破坏旧文件。
- 只读/不可写回退：%APPDATA%\拉片软件\marks\<规范化路径哈希>.json；UI 后续显示实际
  存储位置；源目录恢复可写时可迁回 sidecar。
- 打开时 sidecar 与 AppData 同时存在：按 mark id 合并，冲突取 updated_at 较新者；
  保存到当前规范位置。
- 损坏 JSON 备份为 `.bak` 并提示，不阻塞打开；最后一个标记删除后删 sidecar/AppData
  （F6-A6）；VFR 存 frame+pts，重开优先 frame，PTS 不一致时提示并按 PTS 校正。
- 单测：roundtrip、合并、损坏、空、路径哈希/规范化；手测：F6-A6、F8-A2/A3、只读目录。
  F6-A1/A2 的标记 UI 验收留 T5.2，不在 T5.1 提前宣称。
- 参考 ADR-006；不写视频元数据、不建 SQLite、不做最近文件/跨进程锁。

实现建议：
- 优先做纯存储层（可放 src/core 或 src/media，跟随现有分层），不接 MainWindow 的标记
  交互；T5.2 再接 Ctrl+M、侧栏、时间轴标记。
- JSON 可用 Qt Core（QJsonDocument）或项目已有依赖；路径规范化/哈希要有单测。
- 新单测要进 tests/CMakeLists.txt；T5.1 完成后 ctest 至少 15/15。

构建与验证命令：
# 全量构建 + 单测 + dist
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
# 末尾 "Cannot open E:/" 是 windeployqt 警告；以退出码和 [OK] 为准。

# 只跑 ctest（需要 Qt bin 与仓库内 TMP/TEMP）
$tmp = Join-Path $PWD '.deps\tmp'
$env:TMP=$tmp; $env:TEMP=$tmp
$env:PATH="$PWD\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build\release -C Release --output-on-failure

# 手动回归（不是 T5.1 验收，但别弄坏已有功能）
.\dist\lapian.exe --file ".\media\测试.mp4"
# 100 I / 160 O / L 循环；100 I / O / L / Space 定帧；Shift+L 清除
# 自动化入口：--ab-test（proxy-V1）、--loop-test（测试.mp4）、--step-test（V9）

环境/沙箱坑：
- 若构建在 Qt moc 处报 "libuv process spawn failed: operation not permitted"，是沙箱
  禁止命名管道；需要放宽到 danger-full-access 或请用户批准后重跑，不是代码错误。
- 若 MSBuild 报 Temp 访问被拒绝，先使用仓库内 TMP/TEMP（build-release.ps1 会自动做）。
- media/、dist/、build/ 都不入库；没有素材时先向用户确认，不要编造 V1 验收。

工作方式：
- 计划内任务不要逐项找我确认，直接做；只有计划外、不可逆、规格歧义、权限/硬件限制才问。
- 一个任务 = 一次可审查变更；先提交代码，再提交文档（交接 + perf 证据）。
- 每个任务必须保持 scripts\build-release.ps1、ctest、dist 通过；性能任务补 docs/perf/ 数字。
- 只做 F1–F8 / P1–P14；候选池不进首期；技术栈按 ADR-001/004；spec v0.3 冻结，
  改动只走 §9 版本记录。
- 不提交 third_party/、media/、build/、dist/；不要删 docs/perf/ 证据。
- 不要重做 T1–T4；不要提前宣称 F6/F8、F4-A2 长 GOP、正式 V1/基线机通过。
- 如果上下文变长到影响判断，先告诉我“建议开新会话”，并把当前状态写进交接，不要硬撑。

开始时可给一个 3–5 行的极短计划，但不用等我确认；随后直接改文件。先做 T5.1。
```

## 使用说明

- 这段提示词自包含；新 agent 仍应先读文件、跑 `ctest`，不要只信这段摘要。
- 若 HEAD 已不是 `caf49df`，以 `git log` 和当前 `tasks.md` 为准。
- 若 T5.1 已完成，把“下一步”替换为 `tasks.md` 中下一个未完成任务，并先读最新交接。
