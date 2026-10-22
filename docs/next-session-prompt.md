# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v20（2026-10-22；T7.5 错误与鲁棒性完成，T7.3 P14 仍待 V10）
- **用法**：复制下面 ```text 代码块整段发给新 agent；它仍应先读文件、跑 ctest，不要只信摘要。

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL + D3D11 + WASAPI；spec.md v0.3 已冻结）。
工作区：E:\拉片软件开发。

先读（按顺序）：
- AGENTS.md
- docs\handoff-2026-10-22-t7.5.md（当前交接：T7.5 错误与鲁棒性、缺口）
- docs\perf\T7.5-robustness.md（T7.5 全部数字与口径）
- docs\handoff-2026-10-22-t7.4.md 与 docs\perf\T7.4-4k.md（T7.4 4K proxy 状态）
- docs\handoff-2026-10-21-t7.3.md 与 docs\perf\T7.3-soak.md（T7.3 状态，P14 待 V10）
- spec.md（§4.1 的 4K 放宽档/P14、§4.4、§3 的 F1–F8）
- plan.md、tasks.md（M7 T7.1–T7.8）、docs\adr\ADR-001–006

第一步核对，不相信旧对话转述：
1. git status --short；git log --oneline -8
2. 读 tasks.md 的 T7.5/T7.4/T7.3 行与 AGENTS.md 的“当前状态”
3. 跑 ctest（命令见下），确认 22/22

当前基线（以 git log 为准）：
- HEAD 应包含 T7.5 代码与文档提交（见 git log；T7.4 为 `40bba7f`/`31b3efa`）。
- ctest 22/22（约 15s）；dist 32.63MB / 25 文件。
- T7.5 已完成（开发机、合成/模拟边界，不是 T7.7 正式验收）：
  - `--robust-test` 75 项检查 / 0 失败 / 1 跳过（只读目录另跑），6.84s，
    连续两轮一致；`scripts\robust-readonly.ps1` 真实 ACL 17/17。
  - 坏文件 7 类（缺失/空/垃圾/文本/截断/无视频流/目录）2–34ms 返回可读错误，
    旧会话/旧帧保留；锁文件 32ms；打开后截断再 seek 1.0ms 返回错误。
  - 288 字符中文长路径：打开/跳帧/标记 sidecar/截图通过。
  - 只读目录：标记回退 AppData、截图回退图片目录，无 sidecar。
  - 设备丢失（模拟）：silent → 视频继续（QPC）→ 恢复 playing。
  - 不可达 UNC 3.03s（修复前 21.05s；`PathProbe` 3s 有界预检）。
- 已知缺口：可达 SMB 共享成功打开/写盘未测（本机非管理员，E$ 被拒）；
  物理 USB 设备丢失未测；真实 V8 素材未测；磁盘满/IO 错误未注入；
  网络预检同步阻塞最多 3s（<5s 上界，但不是完全异步）。
- T7.3 仍未通过 P14：90min 单文件 + 1h 子区间循环 pass，但 V10 2h 未做；
  不要写 P14/V10/基线机通过，也不要把 90min 当 2h。
- T7.4 仍只是 4K proxy，不是正式 V4。

接下来做 T7.6（打包/关联/许可/体积），按 tasks.md：
- Inno Setup 每用户安装、ProgID/文件关联（不强制默认）、便携 zip、许可文本、
  卸载清理；P12 ≤100MB（当前 32.63MB，余量充足）。
- 有 V10 时随时插 T7.3 收口：`scripts\soak-bench.ps1 -File <V10> -Seconds 7200
  -Interval 30 -Tag v10-soak -NoLoop`；看 `T7.3 soak_summary` 的 pass/flags。
- T7.8 可并行：把 `--robust-test`/`--drag-test`/`--memory-test` 固化成脚本与阈值 JSON。
- T7.7 全量验收需要 V1–V10 与基线机，不要用 4K proxy 代替 V4。

常用命令：
- 构建+测试+打包：
  powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
- ctest：
  $env:TMP = Join-Path $PWD '.deps\tmp'; $env:TEMP = $env:TMP
  $env:PATH = (Join-Path $PWD 'third_party\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH
  ctest --test-dir build\release -C Release --output-on-failure
- T7.5 鲁棒性（全量 + 真实 ACL 只读目录）：
  dist\lapian.exe --robust-test --robust-tag verify
  powershell -ExecutionPolicy Bypass -File scripts\robust-readonly.ps1 -Tag verify
  可选：--robust-only all|readonly|network、--robust-media <视频>、--robust-root <目录>
- T7.4 探针（需要时复跑；20 样本，走产品拖动/键位/跳转路径）：
  dist\lapian.exe --file media\proxy\proxy-V4.mp4 --drag-test --drag-tag 4k-hard
  dist\lapian.exe --file media\proxy\proxy-V4.mp4 --drag-test --drag-tag 4k-soft `
    --force-software-decode
- 4K 播放内存/步进：
  dist\lapian.exe --file media\proxy\proxy-V4.mp4 --memory-test --memory-mode play `
    --memory-seconds 15 --memory-tag 4k-play
  dist\lapian.exe --file media\proxy\proxy-V4.mp4 --memory-test --memory-mode step `
    --memory-steps 40 --memory-tag 4k-step
- 诊断日志（默认关）：LAPIAN_SEEK_LOG / LAPIAN_STEP_LOG / LAPIAN_PUT_LOG /
  LAPIAN_OPEN_LOG。

已交付、不要重写：
- T7.5：`media/PathProbe` 网络路径 3s 有界预检；`OpenFile` 原生分隔符/目录/
  本地可读性预检；`MarkStore` 长路径 `\\?\` 前缀；`AudioEngine` 设备丢失模拟；
  `--robust-test` 与 `scripts\robust-readonly.ps1`。不要为了“更快”把预检超时
  缩到 <2s 或直接删掉（会回到 21s 冻结）；不要放宽 5s。
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
- §4.4 / T7.5：打开/解码/磁盘错误 5s 内可读错误，不崩溃/冻结；不修改源文件。

不要提前宣称：
- 正式 V4/V1/V5/V10、基线机、冷缓存；4K proxy 不能代替 V4。
- T7.4 软解通过（P6/P9 未过）；P7 “seek 后 PgUp”通过（单样本超限）。
- T7.3 P14/V10；F4-A2 长 GOP；真实 IME/AltGr、F1-A6 真人、200% 真实缩放。
- T7.5 的合成坏文件/模拟设备丢失当成 V8 实物/物理设备；可达 SMB 未验证，
  不要写“网络盘通过”。

工作方式：
- 计划内任务不用逐项确认；只有计划外、不可逆、规格歧义、权限/硬件限制才问。
- 一个任务 = 一次可审查变更；先提交代码，再提交文档（交接 + perf 证据）。
- 每个任务保持 build-release.ps1、ctest、dist 通过；性能任务补 docs/perf/ 数字。
- 只做 F1–F8/P1–P14；spec v0.3 冻结；不重做 T1–T7.5。
- 先给 3–5 行极短计划，再读 `docs\handoff-2026-10-22-t7.5.md` 的 §5 下一步。
```

## 使用说明

- 新 agent 必须先读文件、跑 `ctest`，以 `git log`/`tasks.md` 为准，不要只信这段摘要。
- 若 HEAD 已前进，以最新提交和 `tasks.md` 为准；本提示词只描述 2026-10-22 的状态。
- T7.4 只是 4K proxy 证据；T7.5 是开发机合成/模拟证据；T7.3 的正式收口仍差 V10 2h。
