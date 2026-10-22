# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v27（2026-10-22；合成 V10 soak `pass=1`、V1–V9 冒烟 18/18；P7 跨 GOP 边界 2s/1s 未过 80ms；下一步 P7 边界修复/T7.7）
- **用法**：复制下面 ```text 代码块整段发给新 agent；它仍应先读文件、跑 ctest，不要只信摘要。

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL 动态链接 + D3D11 + WASAPI；spec.md v0.3 已冻结）。
工作区：E:\拉片软件开发。

先读（按顺序）：
- AGENTS.md（当前状态 v6.4）
- docs\handoff-2026-10-22-t7.11.md（当前交接：合成 V10 soak 收口、V1–V9 冒烟、P7 边界专项未过）
- docs\perf\T7.11-gop-boundary.md（P7 跨 GOP 边界：2s 145.60ms / 1s 92.65ms、缓存回拷分项）
- docs\perf\T7.10-synthetic-suite.md（合成片单规格、V10 soak 收口、V1–V9 冒烟）
- docs\handoff-2026-10-22-t7.9.md 与 docs\perf\T7.9-p7-seek-handoff.md（T7.9 已完成）
- docs\handoff-2026-10-22-t7.8.md 与 docs\perf\T7.8-bench.md（历史基准）
- tasks.md 的 T7.11/T7.10/T7.9/T7.7/T7.3 行
- spec.md §8 片单、§3.1（F1-A1）、§4.1、§4.4、§4.5
- plan.md、docs\adr\ADR-001–006

第一步核对（不要相信旧对话转述）：
1. git status --short；git log --oneline -8
   预期 HEAD 含 4516d53（T7.11 代码/脚本）、14daf10（T7.10 文档）、
   69e8c61（T7.9 文档）、d1c7369（T7.9 代码）；工作区 clean。
2. 读 tasks.md 的 T7.11/T7.10/T7.9/T7.7/T7.3 行与 AGENTS.md 的“当前状态”。
3. 跑 ctest（命令见下），确认 22/22；dist 25 文件 / 32.65MB；
   out\package setup 11.31MB、zip 13.65MB。
4. scripts\bench\baseline-dev.json 的 label 仍是 dev-2026-10-22-t7.9。
5. media\synthetic-suite\ 有 V1–V10；V8 的 5 个文件应 OK=0。
   media/ 不入库；若目录被清掉，按 T7.10 证据重建。

当前基线（2026-10-22，开发机）：
- T7.10 合成 V10 2h soak 已收口：`v10-synth` 7200s `pass=1`、flags=none；
  WS 162.4→164.8MB（+2.4）、包络斜率 0.16MB/h、handles 691→675、
  threads 56→50、frame_stall=0、playback_stopped=0、underruns=0、
  ui_gap_max 212ms。**只能写“合成 V10 替身 P14 稳定性通过”，不是正式
  V10/P14。**
- V1–V9 合成冒烟 18/18：V1/V2/V4/V5/V6/V7 索引 ready（V5 total=扫描中
  是 VFR 设计口径）；V1 循环/101 标记/截图；V4 4K 截图；V3 HEVC60 hw=1、
  P7 p95 0.0/0.2ms、P8 26/26；V3 软解 5 步；V5 VFR 横幅 + V7 无音频；
  V8 5/5“打开失败”对话框 0.31–0.35s；V9 click ±1 帧（ctest）。
- T7.11 P7 跨 GOP 边界后退专项（`4516d53`）：
  - 2s GOP 1080p（build\test-media\real-gop2s-full.mp4）p95 **145.60ms**，
    20/20 超 80ms；
  - 1s GOP 收紧档（V2/测试.mp4）p95 **92.65ms**，仍超；
  - 长 GOP proxy-V1 340.78ms（可用档单列）；4K proxy-V4 451.53ms（诊断）。
  - 根因：后退第一步同步回拷 24 帧 FrameCache（76–105ms），解码只 12–42ms；
    不含回拷的 seek+解码 31–48ms（2s）/12–23ms（1s）。
  - **这是 T7.7 的 P7 blocker；不得把 80 改宽，也不得用 T7.9 的 0.06ms 代替。**
- T7.9 已完成（普通 seek 后 PgUp）：FrameCacheMode::kSeekHandoff、spot PgUp
  1080p/4K 0.06ms、60 项/0 失败、30/30 容差；基线 dev-2026-10-22-t7.9。
- T7.7 仍被资源阻塞：真实 V1–V10、基线机（4C/8GB/集显/SATA SSD/Win11）、
  冷缓存 F1-A1、Explorer 真双击；没有这些只能写预检。
- T7.4 仍 4K proxy；T7.5 可达 SMB/物理设备丢失/真实 V8 未验；T7.6 F1-A1
  只有热缓存预检。

接下来（优先）：
1. 先处理 P7 跨 GOP 边界缺口（新，优先于 T7.7 的 P7 结论）：
   - 现状：后退第一步同步回拷 24 帧；2s GOP 145.60ms、1s 92.65ms。
   - 可选方向：异步/延后回拷（先呈现目标帧，再后台填缓存；注意解码器
     表面生命周期、FrameCache 线程安全与取消）；或缩小 1080p 回拷窗口并
     评估 P7 p95/基线机代价；或走 spec §9 版本记录改验收口径。
   - 修复后重跑 scripts\gop-boundary-bench.ps1（2s 与 1s 各 20 样本）+ T7.8
     全量；只有有意的性能改动才 -UpdateBaseline。
2. T7.7 全量验收：真实 V1–V10 + 基线机 + 冷缓存 F1-A1 + Explorer 真双击；
   P6/P9 按目标距前关键帧 ≤2s 分类，长 GOP 单列；P7 边界按上一步结果记录。
3. T7.3 正式 P14：有真实 V10 时用真实 V10 复跑 7200s（-NoLoop）；合成 V10
   只作稳定性预检。
4. T7.8 维护：本轮无产品性能改动，不更新基线；不要用 -Quick 写基线；
   不要为漂移改 thresholds.json。
5. 安装器增强仍是候选，不阻塞；需要联网/安装工具先问用户。

常用命令：
- 构建+测试+dist：
  powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
- ctest：
  $env:TMP = Join-Path $PWD '.deps\tmp'; $env:TEMP = $env:TMP
  $env:PATH = (Join-Path $PWD 'third_party\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH
  ctest --test-dir build\release -C Release --output-on-failure
- 基准回归（约 195s、60 项）：
  powershell -ExecutionPolicy Bypass -File scripts\bench-regression.ps1
- P7 跨 GOP 边界专项：
  powershell -ExecutionPolicy Bypass -File scripts\gop-boundary-bench.ps1 `
    -File build\test-media\real-gop2s-full.mp4 -Samples 20 -Tag gop2s-1080p
  # 1s GOP：-File media\synthetic-suite\V2.mp4 -Tag gop1s-1080p
  # 长 GOP：-File media\proxy\proxy-V1.mp4 -Tag gop10s-1080p（N/A 可用档）
- V10 soak：
  powershell -ExecutionPolicy Bypass -File scripts\soak-bench.ps1 `
    -File media\synthetic-suite\V10.mp4 -Seconds 7200 -Interval 30 `
    -Tag v10-synth -NoLoop
- 查 soak：
  Get-ChildItem build\soak -Filter 'v10-*.log' | Sort-Object LastWriteTime -Descending
  Select-String -Path <log> -Pattern 'T7.3 soak_summary|T7.3 soak_sample' | Select-Object -Last 5
- 媒体探测：
  build\release\bin\lapian_media_probe.exe media\synthetic-suite\V5.mp4

已交付、不要重写：
- T7.11：GopBoundaryTestRunner、--gop-boundary-test/-samples/-tag、
  scripts\gop-boundary-bench.ps1、docs\perf\T7.11-gop-boundary.md。
  不要为了变绿放宽 80/150，不要把普通 seek 后单步 0.06ms 当专项。
- T7.10：media\synthetic-suite\ 与 docs\perf\T7.10-synthetic-suite.md；
  不要把 V3/V4 写成真实 V3/V4，不要重生成覆盖 V5 的 VFR 时间戳，
  不要把 V10 拼接内容写成正式 V10。
- T7.9：kSeekHandoff、handoff 引用生命周期、spot 目标避开关键帧、
  bench 每类 +3 项；不要为 P7 回拷整段帧。
- T7.8：bench-regression.ps1、thresholds.json、baseline-dev.json；
  阈值只来自 spec；基线 ±10% + 噪声下限；不要静默改。
- T7.6：package.ps1、installer.iss、packaging\README.txt；不要删共享
  OpenWithProgids 键、不要写默认关联/UserChoice、不要删用户数据。
- T7.5：PathProbe 3s 预检、长路径 \\?\、设备丢失模拟、--robust-test。
- T7.4：顺序前进/4K 后退窗口/--drag-test；不要降质/丢帧或放宽 150ms。
- T7.1 异步索引；T7.2 FrameCache；T7.2.1 B=末帧；T7.3 soak 判定；
  build-release.ps1 末尾 exit 0 是 E: stderr 噪声处理，不要删。

判定口径（不要放宽阈值来变绿）：
- P12 ≤100MB；P1 ≤1.5s、P2 ≤2.5s（F1-A1）；零网络请求；每用户安装无需管理员。
- P7 1080p ≤80ms、4K ≤150ms；**跨 GOP 边界 2s 目前 145.60ms 未过，
  1s 收紧档 92.65ms 也未过**；不要写成通过。
- 4K P4≥20fps、P6≤300ms、P8≥12fps 每帧呈现、P9≤300ms、P11≤1.5GB；
  P6/P9 以目标距前关键帧 ≤2s 为前提。
- P14：2h 1080p 连续播放，WS ≤1200MB，无持续增长，不崩溃/卡死；V10 必选。
- T7.9 的 60 项/30 项漂移是“同机可复现”证据，不是 T7.7 片单验收；
  30/30 容差、27/30 严格 ±10%，3 项按 5ms 下限，报告里必须如实写。

不要提前宣称：
- 正式 V4/V1/V5/V10、基线机、冷缓存；4K proxy 不能代替 V4。
- T7.3 正式 P14/V10；合成 V10 只能写替身稳定性。
- T7.4 软解；T7.5 真实 V8/物理设备丢失/可达 SMB。
- T7.6 的安装/关联只在本机通过；安装器 UI 英文/未签名/无图标是已知缺口。
- T7.11 的 P7 边界通过；它实测超 80ms。
- 任何“安装包已发布就绪”的结论。

工作方式：
- 计划内任务不用逐项确认；只有计划外、不可逆、规格歧义、权限/硬件限制
  （包括安装 Inno Setup/联网/管理员）才问。
- 一个任务 = 一次可审查变更；先提交代码/脚本，再提交文档（交接 + 证据）。
- 每个任务保持 build-release.ps1、ctest 22/22、dist 通过；打包任务补体积与安装证据。
- 只做 F1–F8/P1–P14；spec v0.3 冻结；不重做 T1–T7.11。
- 先给 3–5 行极短计划，再读 docs\handoff-2026-10-22-t7.11.md 的 §4。
```

## 使用说明

- 新 agent 必须先读文件、跑 `ctest`，以 `git log`/`tasks.md` 为准，不要只信摘要。
- 当前 HEAD 为 T7.11 文档提交（`git log` 首条，含 `docs/perf/T7.11-gop-boundary.md`）；
  T7.11 代码/脚本 `4516d53`；T7.10 文档 `14daf10`；T7.9 代码 `d1c7369`。
- T7.11 是开发机 + 派生 2s/1s GOP 素材；不是 T7.7，也不是正式 V1。
- T7.10 是合成片单；合成 V10 soak `pass=1` 只是替身 P14 稳定性。
- T7.5 是合成/模拟证据；T7.4 只是 4K proxy；T7.6 安装证据只在本机。
- 基线 label 仍是 `dev-2026-10-22-t7.9`；本轮未更新基线。
- `bench-latest.json` 会被任意一次运行覆盖；引用证据用带时间戳的文件。
- 合成片单在 `media/synthetic-suite/`（gitignore）；若被清掉，按
  `docs/perf/T7.10-synthetic-suite.md` 重建。
