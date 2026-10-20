# 给下个 agent 的新会话提示词（可直接复制）

```text
你是「拉片软件」项目的 coding agent。请先读：
- E:\拉片软件开发\AGENTS.md
- docs\handoff-2026-10-20-t4.2.md
- docs\handoff-2026-10-20-t4.1.md（A/B 数据/UI 历史）
- spec.md（v0.3，已冻结）
- plan.md、tasks.md、docs\spec-v0.3-p6p7-proposal.md
- docs\adr\ADR-001 到 ADR-006

先核对当前状态，不要相信旧对话的转述：
1. git status --short / git log --oneline -8
2. 读 tasks.md 的 T5.1–T5.4
3. 跑一次 ctest（需要 Qt bin 与 TMP/TEMP，命令见 AGENTS 或交接）

当前基线（以 git log 为准）：
- 参考 HEAD：2a0bcb5（T4.2 代码与单测）；工作区应为 clean（T4.2 文档提交后会再前进一格）。
- M0 完成；M1/M2/M3/M4（T1.1–T4.2）代码完成；ctest 14/14；dist 31.97MB / 25 文件。
- 已有：打开/播放/逐帧/seek/长按/帧号输入、时间轴缩放滚动、拖动预览、WASAPI、IAudioClock+QPC、0.25–2x 变速/变调、A/V offset、200/s 波形生成与时间轴绘制、步进/拖动静音、A/B 入出点/色条/循环播放/A=B 定帧。
- 未完成：T5.1–T5.4 标记/备注/截图与 sidecar、T2.4 LRU 缓存、T2.2 后台 index worker、正式 V1–V8/V10、基线机/4K、10min soak、设备拔出恢复、波形/听感真人验收。
- 长 GOP 循环跳转 proxy-V1 为 148–157ms，未达 F4-A2 的 1 帧；已按可用档记录，优化留 T7；不要拿它冒充保证档。

下一步按 tasks.md 直接执行：
- T5.1：sidecar 存储；schema、原子写、AppData 回退、按 id+updated_at 合并、损坏备份、空文件删除、frame+pts；单测 roundtrip/合并/损坏/空；手测 F6-A6、F8-A2/A3、只读目录。
- T5.2：标记 UI 与跳转；T5.3 备注自动保存；T5.4 截图。

工作方式：
- 计划内任务不要逐项找我确认，直接做；只有计划外、不可逆、规格歧义、权限/硬件限制时才问。
- 一个任务 = 一次可审查变更；先提交代码，再提交文档（交接 + perf 证据）。
- 每个任务都必须保持 scripts\build-release.ps1、ctest、dist 通过；性能任务必须补 docs/perf/ 数字。
- 只做 F1–F8 / P1–P14；候选池不进首期；技术栈按 ADR-001/004；spec.md v0.3 冻结，改动只走 §9 版本记录。
- 不提交 third_party/、media/、build/、dist/；不要删 docs/perf/ 证据。
- 不要重做 T3.4–T3.7 或 T4.1–T4.2；不要拿开发机单样本冒充正式 V1/基线机验收。
- 如果上下文变长到影响判断，先告诉我“建议开新会话”，并把当前状态写进交接，不要硬撑。

开始时可给一个 3–5 行的极短计划，但不用等我确认；随后直接改文件。先做 T5.1。
```

## 使用说明

- 这段提示词自包含；新 agent 仍应先读文件、跑 `ctest`，不要只信这段摘要。
- 若 HEAD 已不是 `2a0bcb5`，以 `git log` 和当前 `tasks.md` 为准。
- 若 T5.1 已完成，把“下一步”替换为 `tasks.md` 中下一个未完成任务，并先读最新交接。
