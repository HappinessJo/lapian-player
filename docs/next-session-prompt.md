# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v19（2026-10-22；T7.4 4K proxy 硬解一轮达标，T7.3 P14 仍待 V10）
- **用法**：复制下面 ```text 代码块整段发给新 agent；它仍应先读文件、跑 ctest，不要只信摘要。

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL + D3D11 + WASAPI；spec.md v0.3 已冻结）。
工作区：E:\拉片软件开发。

先读（按顺序）：
- AGENTS.md
- docs\handoff-2026-10-22-t7.4.md（当前交接：T7.4 4K 路径、硬/软解两轮、缺口）
- docs\perf\T7.4-4k.md（T7.4 全部数字与口径）
- docs\handoff-2026-10-21-t7.3.md 与 docs\perf\T7.3-soak.md（T7.3 状态，P14 待 V10）
- spec.md（§4.1 的 4K 放宽档/P14、§4.4、§3 的 F1–F8）
- plan.md、tasks.md（M7 T7.1–T7.8）、docs\adr\ADR-001–006

第一步核对，不相信旧对话转述：
1. git status --short；git log --oneline -8
2. 读 tasks.md 的 T7.4 行、T7.3 行与 AGENTS.md 的“当前状态”
3. 跑 ctest（命令见下），确认 21/21

当前基线（以 git log 为准）：
- HEAD 应包含 `40bba7f`（T7.4 代码）与 `31b3efa`（T7.4 文档）；工作区应 clean。
- ctest 21/21（约 15s）；dist 32.54MB / 25 文件。
- T7.4 已完成（4K proxy，不是正式 V4）：
  - 硬解 4K（proxy-V4，20 样本 p95）：P4 60.8/61.0fps、P6 142.6/143.4ms、
    P7 后退 1.86/1.67ms（max 410ms，1/20 超）、前进 4.34/4.40ms、
    P8 13.0fps 26/26 Present、P9 225.4/226.8ms、P11 播放 WS max 140.1MB。
  - 40 步 `--memory-test step`：后退 p95 2.00/1.76ms、前进 4.12/4.15ms，
    WS 高水位 683.6MB（4K 后退窗口 256MiB/20 帧）。
  - 已知缺口：seek 后 PgUp 单样本 201–213ms >150ms（精确 seek 不预热后退缓存）；
    软解一轮 P6 301.7ms、P9 320.6ms 未过（spec 软解不保证）。
- T7.3 仍未通过 P14：90min 单文件 + 1h 子区间循环 pass，但 V10 2h 未做；
  不要写 P14/V10/基线机通过，也不要把 90min 当 2h。

接下来做 T7.5（错误与鲁棒性）或 T7.6（打包/关联），按 tasks.md：
- T7.5：V8、损坏文件、只读目录、设备丢失、中文/长路径、网络盘；5s 内响应，
  无崩溃/冻结。不依赖 V4/V10，可直接做。
- T7.6：Inno Setup 每用户安装、ProgID/关联、便携 zip、许可文本、卸载清理；
  P12 ≤100MB（当前 32.54MB，余量充足）。
- 有 V10 时随时插 T7.3 收口：`scripts\soak-bench.ps1 -File <V10> -Seconds 7200
  -Interval 30 -Tag v10-soak -NoLoop`；看 `T7.3 soak_summary` 的 pass/flags。
- T7.7 全量验收需要 V1–V10 与基线机，不要用 4K proxy 代替 V4。

常用命令：
- 构建+测试+打包：
  powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
- ctest：
  $env:TMP = Join-Path $PWD '.deps\tmp'; $env:TEMP = $env:TMP
  $env:PATH = (Join-Path $PWD 'third_party\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH
  ctest --test-dir build\release -C Release --output-on-failure
- T7.4 探针（需要时复跑；20 样本，走产品拖动/键位/跳转路径）：
  dist\lapian.exe --file media\proxy\proxy-V4.mp4 --drag-test --drag-tag 4k-hard
  dist\lapian.exe --file media\proxy\proxy-V4.mp4 --drag-test --drag-tag 4k-soft `
    --force-software-decode
- 4K 播放内存/步进：
  dist\lapian.exe --file media\proxy\proxy-V4.mp4 --memory-test --memory-mode play `
    --memory-seconds 15 --memory-tag 4k-play
  dist\lapian.exe --file media\proxy\proxy-V4.mp4 --memory-test --memory-mode step `
    --memory-steps 40 --memory-tag 4k-step
- 诊断日志（默认关）：LAPIAN_SEEK_LOG / LAPIAN_STEP_LOG / LAPIAN_PUT_LOG。

已交付、不要重写：
- T7.4：`MediaSession::sequential_frame_` 顺序前进快路径、4K 后退窗口
  256MiB/20、`--drag-test`/`--drag-samples`/`--drag-tag`/
  `--force-software-decode`。不要为了 P7 降质/丢帧或放宽 150ms。
- T7.1 的异步索引；T7.2 的 FrameCache 语义（仍只缓存后退步进的软件 NV12 帧）；
  T7.2.1 的 B=末帧/显示尺寸；T7.3 的 soak 判定。
- 长 GOP 循环 B→A 148–157ms（F4-A2）仍是已知缺口，留给 T7.7；不要顺手改。

判定口径（不要放宽阈值来变绿）：
- 4K P4 ≥20fps、P6 ≤300ms、P7 ≤150ms、P9 ≤300ms、P8 ≥12fps 且每帧呈现、
  P11 ≤1.5GB；P6/P9 以目标距前关键帧 ≤2s 为前提。
- P7 的 p95 允许 1/20 超限，但必须同时报告 max 与尖峰；4K 2s GOP 下缓存重填
  ~410ms 是已知成本，不是“无卡顿”。
- P14：2h 1080p 连续播放，WS ≤1200MB，无持续增长，不崩溃/卡死；V10 必选。

不要提前宣称：
- 正式 V4/V1/V5/V10、基线机、冷缓存；4K proxy 不能代替 V4。
- T7.4 软解通过（P6/P9 未过）；P7 “seek 后 PgUp”通过（单样本超限）。
- T7.3 P14/V10；F4-A2 长 GOP；真实 IME/AltGr、F1-A6 真人、200% 真实缩放。

工作方式：
- 计划内任务不用逐项确认；只有计划外、不可逆、规格歧义、权限/硬件限制才问。
- 一个任务 = 一次可审查变更；先提交代码，再提交文档（交接 + perf 证据）。
- 每个任务保持 build-release.ps1、ctest、dist 通过；性能任务补 docs/perf/ 数字。
- 只做 F1–F8/P1–P14；spec v0.3 冻结；不重做 T1–T7.4。
- 先给 3–5 行极短计划，再读 `docs\handoff-2026-10-22-t7.4.md` 的 §5 下一步。
```

## 使用说明

- 新 agent 必须先读文件、跑 `ctest`，以 `git log`/`tasks.md` 为准，不要只信这段摘要。
- 若 HEAD 已前进，以最新提交和 `tasks.md` 为准；本提示词只描述 2026-10-22 的状态。
- T7.4 只是 4K proxy 证据；T7.3 的正式收口仍差 V10 2h，两者不要混写。
