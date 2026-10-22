# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v26（2026-10-22；T7.9 已完成；T7.10 合成 V1–V10 已生成，V10 2h soak 进行中/待收口；下一步 T7.7）
- **用法**：复制下面 ```text 代码块整段发给新 agent；它仍应先读文件、跑 ctest，不要只信摘要。

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL 动态链接 + D3D11 + WASAPI；spec.md v0.3 已冻结）。
工作区：E:\拉片软件开发。

先读（按顺序）：
- AGENTS.md（当前状态 v6.3）
- docs\handoff-2026-10-22-t7.10.md（当前交接：合成 V1–V10、V10 2h soak 进行中/待收口）
- docs\perf\T7.10-synthetic-suite.md（片单规格、生成方法、soak 采样、边界）
- docs\handoff-2026-10-22-t7.9.md 与 docs\perf\T7.9-p7-seek-handoff.md（T7.9 已完成）
- docs\handoff-2026-10-22-t7.8.md 与 docs\perf\T7.8-bench.md（历史基准）
- tasks.md 的 T7.10/T7.9/T7.7/T7.3 行
- docs\handoff-2026-10-22-t7.6.md / t7.5 / t7.4 / t7.3 与对应 perf（边界）
- spec.md §8 片单、§3.1（F1-A1）、§4.1、§4.4、§4.5
- plan.md、docs\adr\ADR-001–006

第一步核对（不要相信旧对话转述）：
1. git status --short；git log --oneline -8
   预期 HEAD 含 69e8c61（T7.9 文档）与 d1c7369（T7.9 代码/脚本/基线）；
   T7.10 文档提交在其后；工作区 clean。
2. 读 tasks.md 的 T7.10/T7.9/T7.7/T7.3 行与 AGENTS.md 的“当前状态”。
3. 跑 ctest（命令见下），确认 22/22。
4. 确认 dist 25 文件 / 32.63MB；out\package 下 setup 11.31MB、
   便携 zip 13.65MB、package-report.txt。
5. 确认 scripts\bench\baseline-dev.json 的 label=dev-2026-10-22-t7.9
   （T7.9 有意更新；旧 dev-2026-10-22 在 git 历史里）。
6. 确认 media\synthetic-suite\ 有 V1–V10；V8 的 5 个文件应 OK=0。
   这是合成片单，media/ 不入库；若目录被清掉，按 T7.10 证据重建。

当前基线（2026-10-22，开发机）：
- T7.9 已完成（P7 seek 后 PgUp 收口，60 项/0 失败）：
  - `FrameCacheMode::kSeekHandoff`：精确 seek 只保留目标前一帧 AVFrame 引用
    （硬解纹理不回拷），PgUp 直接呈现；spot 目标避开关键帧。
  - spot PgUp 1080p 0.06ms / 4K 0.06ms；PgDn 5.99/39.55ms；
    P6 56.34/143.38ms；P9 p95 61.45/226.78ms。
  - 全量 60 项/0 失败/0 infra；30/30 基线容差，27/30 严格 ±10%。
  - 基线 dev-2026-10-22-t7.9（bench-20261022-191014.json），
    验证 191338.json。不要用旧 label 的 54 项报告冒充当前口径。
  - 1080p 旧 372–460ms 是 proxy-V1 10s GOP 的关键帧边界，不属 80ms 口径；
    跨 GOP 边界专项仍需 T7.7 用真实 V1 记录。
- T7.10（本轮，辅助素材）：
  - media\synthetic-suite\：15 个文件，逻辑 7.01GB；V2/V3/V4/V9/V10 硬链接。
  - V1 1080p24 6:01 AAC；V2 1080p30 152s；V3 1080p HEVC60 20s（proxy）；
    V4 4K30 15s（proxy）；V5 VFR 41.9s（VFR_SUSPECT=1，无音频）；
    V6 1080p24 12min；V7 1080p30 无音频；V8 5 个坏文件；V9 click+白闪 5s；
    V10 2h/7200.11s/172,629 帧/5.23GB。
  - V10 2h soak 于 19:30:18 启动，tag v10-synth，-NoLoop；
    日志 build\soak\v10-synth-20261022-193018.log；预计 21:30 结束。
  - 截至 t=690s：WS 164.9MB、private 215.5MB、handles 687、threads 52、
    frame 16542、playing=1、underruns=0、cache=0、ui_gap=207ms；
    **尚无 T7.3 soak_summary，不写 P14 通过**。
  - 启动时有一条 `[h264] missing picture in access unit`（拼接边界），
    播放继续；若 2h 内出现解码错误/卡顿，先查拼接边界。
- T7.7 仍被资源阻塞：真实 V1–V10、基线机（4C/8GB/集显/SATA SSD/Win11）、
  冷缓存 F1-A1、Explorer 真双击、安装/卸载基线机复跑。没有这些只能写预检。
- T7.3 P14 仍未正式通过：合成 V10 只能写“合成 V10 替身 P14 稳定性”；
- T7.4 仍 4K proxy；T7.5 可达 SMB/物理设备丢失/真实 V8 未验；T7.6 F1-A1 未正式验收。

接下来（优先）：
1. 先收口 V10 soak（不要启动其他 GUI 测试与它并行）：
   查日志 build\soak\v10-synth-20261022-193018.log 的
   `T7.3 soak_summary`。
   - 有 summary：读 pass/flags、baseline_slope_mb_per_h、handles/threads、
     frame_stall_events、ui_gap_max_ms。
     pass=1 且无 memory_growth → “合成 V10 替身 P14 稳定性通过”，
     不写“正式 V10 片单通过”。
     pass=0/memory_growth/卡死 → 保留日志，按 flags 定位，不掩盖。
   - 还在跑：等结束；不要并行跑 GUI。
   - 没有进程/没有 summary：按 T7.10 证据重跑 7200s。
2. soak 结束后，串行跑 V1–V9 冒烟：
   V1/V2/V4/V6 打开/首帧/索引/循环/标记/截图；
   V3 HEVC60 硬解；V5 VFR 横幅/帧号；V7 无音频；V8 错误路径；
   V9 F5-A3 click 对齐。
3. P7 跨 GOP 边界专项：2s GOP 1080p，目标关键帧 → PgUp 前一 GOP 末帧，
   按 80ms；长 GOP 单列可用档。不要拿 T7.9 的 0.06ms 代替。
4. T7.7 全量验收：需要真实 V1–V10 + 基线机 + 冷缓存 + Explorer 真双击；
   没有就只写预检。P6/P9 按目标距前关键帧 ≤2s 分类，长 GOP 单列。
5. 有真实 V10 时，用真实 V10 复跑 7200s，不要只依赖合成 V10。
6. T7.8 维护：没有新的有意的性能改动，不要更新基线；不要用 -Quick 写基线；
   不要为了让漂移变绿改 thresholds.json。
7. 安装器增强仍是候选，不阻塞；需要联网/安装工具先问用户。

常用命令：
- 构建+测试+dist：
  powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
- 只打包：
  powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -SkipBuild
- ctest：
  $env:TMP = Join-Path $PWD '.deps\tmp'; $env:TEMP = $env:TMP
  $env:PATH = (Join-Path $PWD 'third_party\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH
  ctest --test-dir build\release -C Release --output-on-failure
- T7.8/T7.9 基准回归（全量约 195s、60 项）：
  powershell -ExecutionPolicy Bypass -File scripts\bench-regression.ps1
- V10 2h soak（合成或真实 V10 都可用同一脚本，换 -File）：
  powershell -ExecutionPolicy Bypass -File scripts\soak-bench.ps1 `
    -File media\synthetic-suite\V10.mp4 -Seconds 7200 -Interval 30 `
    -Tag v10-synth -NoLoop
- 查 soak 结果：
  Get-ChildItem build\soak -Filter 'v10-*.log' | Sort-Object LastWriteTime -Descending
  Select-String -Path <log> -Pattern 'T7.3 soak_summary|T7.3 soak_sample' | Select-Object -Last 5
- 媒体探测：
  build\release\bin\lapian_media_probe.exe media\synthetic-suite\V5.mp4
- T7.5 鲁棒性：
  dist\lapian.exe --robust-test --robust-tag verify
  powershell -ExecutionPolicy Bypass -File scripts\robust-readonly.ps1 -Tag verify

已交付、不要重写：
- T7.10：media\synthetic-suite\ 与 docs\perf\T7.10-synthetic-suite.md。
  不要为了“更真”把 V3/V4 写成真实 V3/V4；不要重编码/重生成覆盖 V5 的 VFR
  时间戳；不要把 V10 拼接内容写成正式 V10。
- T7.9：FrameCacheMode::kSeekHandoff、MediaSession handoff 引用生命周期、
  DragTestRunner spot 目标避开关键帧、bench 每类 +3 项、单测 handoff 断言。
  不要为了 P7 回拷整段帧（实测 P6 1080p 54.7→128.4ms、4K 143.3→225.8ms）；
  不要放宽 80/150，也不要把普通 seek 后单步当成跨 GOP 边界专项。
- T7.8：scripts\bench-regression.ps1、thresholds.json、baseline-dev.json、
  docs\perf\T7.8-bench.md。阈值只来自 spec；基线漂移 ±10% + 噪声下限；
  1080p 40 样本、4K 20 样本；不要静默改。
- T7.6：scripts\package.ps1、installer.iss、packaging\README.txt、
  build-release.ps1 -Package。不要删共享 OpenWithProgids 键、不要写默认关联/
  UserChoice、不要删用户数据。
- T7.5：PathProbe 3s 有界预检、OpenFile 预检、MarkStore 长路径 \\?\、
  AudioEngine 设备丢失模拟、--robust-test、scripts\robust-readonly.ps1。
- T7.4：顺序前进/4K 后退窗口/--drag-test；不要为了 P7 降质/丢帧或放宽 150ms。
- T7.1 异步索引；T7.2 FrameCache；T7.2.1 B=末帧；T7.3 soak 判定。
- build-release.ps1 末尾的 exit 0 是 E: 工具 stderr 噪声的收尾处理；
  不要删掉，也不要把它当成“忽略构建失败”的依据。
- 不要为了安装包体积删 Qt/FFmpeg 必需 DLL 或改技术栈（ADR-001/004）。

判定口径（不要放宽阈值来变绿）：
- P12 ≤100MB；P1 ≤1.5s、P2 ≤2.5s（F1-A1）；零网络请求；每用户安装无需管理员。
- 4K P4≥20fps、P6≤300ms、P7≤150ms、P8≥12fps 每帧呈现、P9≤300ms、P11≤1.5GB；
  P6/P9 以目标距前关键帧 ≤2s 为前提。
- P14：2h 1080p 连续播放，WS ≤1200MB，无持续增长，不崩溃/卡死；V10 必选。
- T7.9 的 60 项/30 项漂移是“同机可复现”证据，不是 T7.7 片单验收；
  30/30 容差、27/30 严格 ±10%，3 项按 5ms 下限，报告里必须如实写。
- 1080p 旧 372–460ms 是 10s GOP 关键帧边界，不属 P7 80ms 口径；
  4K 2s GOP 的 0.06ms 是普通 seek 后单步，不等于跨 GOP 边界专项通过。

不要提前宣称：
- 正式 V4/V1/V5/V10、基线机、冷缓存；4K proxy 不能代替 V4。
- T7.3 P14/V10；T7.4 软解；T7.5 真实 V8/物理设备丢失/可达 SMB。
- T7.6 的安装/关联只在本机通过；安装器 UI 英文/未签名/无图标是已知缺口。
- T7.9/T7.10 的基准回归/合成片单只覆盖开发机代理/合成素材；不是 T7.7。
- V10 2h soak 没有 summary 前，不写 pass。
- 任何“安装包已发布就绪”的结论。

工作方式：
- 计划内任务不用逐项确认；只有计划外、不可逆、规格歧义、权限/硬件限制
  （包括安装 Inno Setup/联网/管理员）才问。
- 一个任务 = 一次可审查变更；先提交代码/脚本，再提交文档（交接 + 证据）。
- 每个任务保持 build-release.ps1、ctest 22/22、dist 通过；打包任务补体积与安装证据。
- 只做 F1–F8/P1–P14；spec v0.3 冻结；不重做 T1–T7.9。
- 先给 3–5 行极短计划，再读 docs\handoff-2026-10-22-t7.10.md 的 §4 下一步。
```

## 使用说明

- 新 agent 必须先读文件、跑 `ctest`，以 `git log`/`tasks.md` 为准，不要只信摘要。
- 当前 HEAD `69e8c61`（T7.9 文档）；T7.9 代码 `d1c7369`；T7.10 文档提交在其后。
- T7.9 是开发机 + 代理证据；T7.10 是合成片单；T7.5 是合成/模拟证据；
  T7.4 只是 4K proxy；T7.3 的正式收口仍差真实 V10 2h。
- T7.6 的安装/卸载证据是开发机本机；F1-A1 的冷缓存/真双击/基线机留 T7.7。
- 基线 label 是 `dev-2026-10-22-t7.9`，不是旧 `dev-2026-10-22`。
- `bench-latest.json` 会被任意一次运行覆盖；引用证据用带时间戳的文件。
- 合成片单在 `media/synthetic-suite/`（gitignore）；若被清掉，按
  `docs/perf/T7.10-synthetic-suite.md` 重建。
