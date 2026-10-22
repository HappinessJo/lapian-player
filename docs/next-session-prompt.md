# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v18（2026-10-22；T7.3 90min 单文件 + 1h 子区间循环均 pass，无可复现增长；只差 V10 2h）
- **用法**：复制下面 ```text 代码块整段发给新 agent；它仍应先读文件、跑 ctest，不要只信摘要。

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL + D3D11 + WASAPI；spec.md v0.3 已冻结）。
工作区：E:\拉片软件开发。

先读（按顺序）：
- AGENTS.md
- docs\handoff-2026-10-21-t7.3.md（当前交接，含 2026-10-22 的 90min/1h 结果）
- docs\perf\T7.3-soak.md（T7.3 全部数字与结论）
- spec.md（§4.1 P14、§4.4、§3 的 F1–F8）
- plan.md、tasks.md（M7 T7.1–T7.8）、docs\adr\ADR-001–006
- 需要时再读 T7.2.1/T7.2/T7.1 交接；不要重读全部历史交接。

第一步核对，不相信旧对话转述：
1. git status --short；git log --oneline -8
2. 读 tasks.md 的 T7.3 行与 AGENTS.md 的“当前状态”
3. 跑 ctest（命令见下），确认 21/21

当前基线（以 git log 为准）：
- 代码提交：`5dfbe06`（--memory-loop-frames）或更新；T7.3 相关还有 `188c63e`/
  `31d0d94`/`ed61ba7`/`852b68a`。文档提交 `af72c0b` 或更新；工作区应 clean。
- ctest 21/21（约 15s）；dist 32.52MB / 25 文件。
- T7.3 已完成：
  - `v10-1p5h`：1h34m 1080p 素材，单文件连续播放 90min，
    `--memory-no-loop`，pass=1；WS 150.9→153.1MB（+2.1），斜率 1.22MB/h，
    loop_jumps=0，cache 0，underrun 0。
  - `loopseg3600`：同一 1.5h 文件，只循环前 3650 帧（≈152s），1h，
    `--memory-loop-frames 3650`，pass=1；WS 156.0→159.9MB（+3.9），
    斜率 0.79MB/h，23 次 B→A，cache 0，underrun 0。
  - 之前 152s 短文件循环 30min/1h 的 +18.2/+20.8MB 没有复现；多次复测
    +3.0/+4.8/+7.4 都 pass。当前没有可复现的确定性泄漏；异常根因未定位。
- **T7.3 仍未通过 P14**：只有 1.5h 素材，不是 V10 2h；没有 V10 7200s 数据。
  不要把 90min 当 2h，不要写 P14/V10/基线机通过。

接下来只做这三件事之一（按优先级）：
1. 如果用户提供 V10（2h 1080p 真素材）：
   跑单文件连续播放，显式 -NoLoop：
   powershell -ExecutionPolicy Bypass -File scripts\soak-bench.ps1 `
     -File <V10路径> -Seconds 7200 -Interval 30 -Tag v10-soak -NoLoop
   看 `T7.3 soak_summary` 的 pass/ws_max/growth/baseline_growth/flags；
   通过则补 P14 证据（注明开发机/暖缓存/非基线机），失败则按 flags 定位。
2. 没有 V10：不要再重复 30min/60min/90min 长跑；现有证据已足够说明
   “无可复现持续增长”。直接问用户：是等 V10，还是带 caveat 先开 T7.4。
   如果用户同意先开 T7.4，就做 4K 路径；T7.3 保持“P14 待 V10”未关闭。
3. 只有异常再次出现时才定位：加 D3D11 表面池/句柄/ETW 计数，不要盲目改
   产品循环/音频代码；先证明是每次 B→A 的分配，再改。

常用命令：
- 构建+测试+打包：
  powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
- ctest（需要 TMP/TEMP 和 Qt bin 在 PATH）：
  $env:TMP = Join-Path $PWD '.deps\tmp'; $env:TEMP = $env:TMP
  $env:PATH = (Join-Path $PWD 'third_party\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH
  ctest --test-dir build\release -C Release --output-on-failure
- 子区间循环（同一长文件只循环前 N 帧，1h 示例）：
  powershell -ExecutionPolicy Bypass -File scripts\soak-bench.ps1 `
    -File media\测试2.mp4 -Seconds 3600 -Interval 30 -Tag loopseg3600 -LoopFrames 3650
- 改 `scripts\soak-bench.ps1` 时必须保留 UTF-8 BOM，否则 Windows PowerShell 5.1
  会解析失败；改完用 Parser::ParseFile 验证。

已交付、不要重写：
- `--memory-mode soak`、`SoakStats` 5min 下包络、UI 心跳/30s 看门狗、
  `--memory-interval`、`--memory-no-loop`、`--memory-loop-frames`、
  `scripts\soak-bench.ps1`、`lapian_soak_test`。
- T7.1 的异步索引/`--startup-test`/`--index-test`；T7.2 的 `FrameCache`/
  `--memory-test`/缓存策略；T7.2.1 的 B=末帧/显示尺寸；不要改这些语义。

判定口径（不要放宽阈值来变绿）：
- P14：2h 1080p 连续播放，WS ≤1200MB，无持续增长，不崩溃/卡死。
- 自动判据：5 分钟下包络（每块取最低 WS）做线性拟合；斜率 >16MB/h 且
  增量 ≥12MB 才判 memory_growth。小幅正斜率/一次性预热不算。
- 90min/1h 的 pass 只是“没有可复现增长”，不是 P14 通过。

不要提前宣称：
- IME/AltGr、F1-A6 真人、F6/F8、F8-A1 真实 %APPDATA%、
  4K/VFR/正式 P3/P4/P8、T6.2.1 硬件级自动化、T6.4 真实显示缩放/多显示器、
  T6.5 真实 VFR/10-bit/HDR、T7.1 冷缓存/基线机/正式 V1 P1/P2、
  T7.2 正式 P10/P11 或 4K P7、T7.3 P14/V10、F4-A2 长 GOP 或基线机通过。
- 已知缺口：T7.2 的 4K 步进 p95 ~228ms（P7 150ms 未达）；长 GOP 循环
  B→A 148–157ms（F4-A2 未达）；这些留给 T7.4/T7.7。

工作方式：
- 计划内任务不用逐项确认；只有计划外、不可逆、规格歧义、权限/硬件限制才问。
- 一个任务 = 一次可审查变更；先提交代码，再提交文档（交接 + perf 证据）。
- 每个任务保持 build-release.ps1、ctest、dist 通过；性能任务补 docs/perf/ 数字。
- 只做 F1–F8/P1–P14；spec v0.3 冻结；不重做 T1–T7.2.1。
- 先给 3–5 行极短计划，然后读 `docs\handoff-2026-10-21-t7.3.md`，按上面
  第 1/2 条继续；不要重复已经做过的短时 soak。
```

## 使用说明

- 新 agent 必须先读文件、跑 `ctest`，以 `git log`/`tasks.md` 为准，不要只信这段摘要。
- 若 HEAD 已前进，以最新提交和 `tasks.md` 为准；本提示词只描述 2026-10-22 的状态。
- T7.3 的正式收口只差 V10 2h；没有 V10 时不要继续堆短时探针，也不要把
  90min/1h 写成 P14。
