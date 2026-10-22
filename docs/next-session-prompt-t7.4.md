# 给下个 agent 的 T7.4 提示词（可直接复制）

> **已执行（2026-10-22）**：T7.4 已完成，代码 `40bba7f`，结果见
> [T7.4 交接](handoff-2026-10-22-t7.4.md) 与 [perf](perf/T7.4-4k.md)。
> 本文保留为历史提示词；下一个任务用 [next-session-prompt.md](next-session-prompt.md)（v19）。

- **版本**：v1（2026-10-22；T7.3 保持“P14 待 V10”，用户决定先开 T7.4 4K 路径）
- **用法**：复制下面 ```text 代码块整段发给新 agent；它仍应先读文件、跑 ctest。

```text
你是「拉片软件」项目的 coding agent。本次任务：**T7.4 4K 路径**，不是 T7.3 收口。
项目是 Windows 桌面端的帧级参考片审阅器（C++20 + Qt 6 Widgets + FFmpeg LGPL +
D3D11 + WASAPI；spec.md v0.3 已冻结）。工作区：E:\拉片软件开发。

先读：
- AGENTS.md
- docs\handoff-2026-10-21-t7.3.md（T7.3 当前状态，含 2026-10-22 结论）
- docs\perf\T7.3-soak.md（T7.3 数字）
- docs\perf\T7.2-memory.md（4K 内存/后退步进现有数字）
- docs\perf\T2.9-preview.md、docs\perf\T2.10-drag-release.md（拖动预览/松手）
- docs\handoff-2026-10-20-t2.9.1.md（反向拖动、32 帧缓存）
- tasks.md 的 T7.4 行、spec.md §4.1 P4/P6/P7/P8/P9/P11 与 §4.2 4K 可用档
- plan.md、ADR-001/004/005

第一步核对（不要相信旧对话转述）：
1. git status --short；git log --oneline -8
2. 读 tasks.md T7.4 行和 spec §4.1 的 4K 放宽档
3. 跑 ctest（命令见下），确认 21/21

当前基线：
- 代码 `5dfbe06` 或更新；文档 `f924f1f` 或更新；工作区应 clean。
- ctest 21/21（约 15s）；dist 32.52MB / 25 文件。
- T7.3 状态：90min 单文件 + 1h 子区间循环均 pass，无可复现增长；
  但 **V10 2h 未做，P14 未通过**。T7.3 保持未关闭，不要写 P14/V10 通过。
- 用户已明确决定：先开 T7.4，带着“T7.3 P14 待 V10”的 caveat。

T7.4 验收口径（spec §4.1）：
- 4K（3840×2160）可用档，目标距前关键帧 ≤2s；
- P4 拖动预览 ≥20fps；
- P6 松手精确 ≤300ms；
- P7 步进 p95 ≤150ms；
- P8 长按 ≥12 帧/秒，每帧呈现；
- P9 跳转 ≤300ms；
- P11 4K 播放内存 ≤1.5GB；
- 硬解与软解各一轮；报告必须写“4K proxy，不是正式 V4/基线机”。

现有素材（没有正式 V4）：
- `media\proxy\proxy-V4.mp4`：3840×2160 H.264 8-bit 30fps，
  450 帧 / 15s，AAC，约 2s GOP。T7.4 主用。
- `build\t5.4-4k\K.mp4`：3840×2160 H.264 24fps，120 帧 / 5s，AAC；
  短烟测用。
- `build\t7.2-media\test1080.mp4`：1080p 对照。
- 没有真实 V4；不要写成正式 V4 通过。

已知数字/风险：
- T7.2：4K 播放 WS 128.7MB（P11 1.5GB 内），但 **4K 40 次后退步进
  p95 228.09ms > P7 150ms**；这是 T7.4 必须处理或如实记录的主要缺口。
- T2.9/T2.10：1080p 拖动预览约 50fps；4K 软件预览可能低于 30fps，
  spec 的 4K P4 放宽到 ≥20fps，需实测。
- 4K 索引/波形/播放首帧已有测试路径；不要改 T7.1/T7.2/T7.2.1 语义。

实施顺序（建议）：
1. 先跑 4K 基线：`--memory-test play` / `--memory-test step` 各一轮硬解，
   记录 WS、P7、P11、帧推进；确认 228ms 是否可复现。
2. 找到/补“强制软解”测试开关：优先 `--force-software-decode` 或
   `LAPIAN_FORCE_SOFTWARE=1`，只影响测试路径，默认硬解不变；用日志确认
   实际解码器（D3D11VA / software）。
3. 测 P4/P6/P8/P9：现有 CLI 没有完整 4K 拖动 harness；可加**测试专用**
   `--drag-test`（4K 目标扫掠，记录 preview FPS、松手精确耗时、长按帧数），
   不改产品拖动语义；或复用 T2.9/T2.10 探针并补 4K 参数。
4. 如果 4K P7 仍 >150ms：优先检查后退步进的 `FrameCache` 预热/回拷路径
   （T7.2 的 96MiB/8 帧、backward window），做小步优化；不要用降质/丢帧
   伪装 P7。优化前后各跑 40 次后退，补 p95。
5. 硬解/软解各一轮；软解允许 P7 不达标，但报告要分开写，不混成“4K 通过”。
6. 更新 `docs/perf/T7.4-4k.md` + 交接；代码先提交，文档后提交；保持
   build/ctest/dist 通过。

常用命令：
- 构建+测试+打包：
  powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
- ctest：
  $env:TMP = Join-Path $PWD '.deps\tmp'; $env:TEMP = $env:TMP
  $env:PATH = (Join-Path $PWD 'third_party\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH
  ctest --test-dir build\release -C Release --output-on-failure
- 4K 播放内存：
  dist\lapian.exe --file media\proxy\proxy-V4.mp4 --memory-test --memory-mode play `
    --memory-seconds 15 --memory-tag 4k-play
- 4K 后退步进：
  dist\lapian.exe --file media\proxy\proxy-V4.mp4 --memory-test --memory-mode step `
    --memory-steps 40 --memory-tag 4k-step
- 4K 循环/长跑仍用 `scripts\soak-bench.ps1`（需要 UTF-8 BOM；改脚本必须保留）。

不要：
- 不要重开 T7.1/T7.2/T7.2.1/T7.3 的判定；不要写 P14/V10/基线机通过。
- 不要为了 P7 数字放宽阈值；4K P7 150ms 是 spec 的放宽档，不是可选项。
- 不要把 15s proxy-V4 写成正式 V4；不要把软解/硬解结果混在一起。
- 不要改产品默认硬解、缓存语义或 B 到达/显示尺寸；测试开关只影响测试。
- 不要在没有 20 样本/p95 的情况下写 P4/P6/P7/P8/P9 通过。

工作方式：
- 计划内任务不用逐项确认；只有计划外、不可逆、规格歧义、权限/硬件限制才问。
- 一个任务 = 一次可审查变更；先代码，后文档（交接 + perf 证据）。
- 每个任务保持 build-release.ps1、ctest、dist 通过。
- 先给 3–5 行极短计划，然后读 `docs\perf\T7.2-memory.md` 的 4K 数字，
  从“复现 4K P7 228ms”开始；不要重复 T7.3 的 1080p soak。
```

## 使用说明

- 新 agent 必须先读文件、跑 `ctest`，以 `git log`/`tasks.md` 为准。
- T7.4 的正式验收仍受“无真实 V4/基线机”限制；报告要区分 proxy 与正式片单。
- 4K P7 是已知主要缺口，优先复现和优化；不要用 UI 降质掩盖。
