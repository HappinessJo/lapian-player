# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v28（2026-10-22；T7.11.1 修复 P7 跨 GOP 边界后退，2s 64.13ms/1s 27.11ms；全量 60/0；下一步 T7.7/正式 V10）
- **用法**：复制下面 ```text 代码块整段发给新 agent；它仍应先读文件、跑 ctest，不要只信摘要。

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL 动态链接 + D3D11 + WASAPI；spec.md v0.3 已冻结）。
工作区：E:\拉片软件开发。

先读（按顺序）：
- AGENTS.md（当前状态 v6.5）
- docs\handoff-2026-10-22-t7.11.1.md（当前交接：P7 边界延后回拷修复、全量 60/0）
- docs\perf\T7.11.1-gop-cache-defer.md（修复后数字、方案比较、表面池/回拷顺序）
- docs\perf\T7.11-gop-boundary.md（修复前 145.60/92.65ms 与旧分项，历史证据）
- docs\perf\T7.10-synthetic-suite.md（合成片单规格、V10 soak 收口、V1–V9 冒烟）
- docs\handoff-2026-10-22-t7.9.md 与 docs\perf\T7.9-p7-seek-handoff.md（T7.9 已完成）
- docs\handoff-2026-10-22-t7.8.md 与 docs\perf\T7.8-bench.md（历史基准）
- tasks.md 的 T7.11.1/T7.11/T7.10/T7.7/T7.3 行
- spec.md §8 片单、§3.1（F1-A1）、§4.1、§4.4、§4.5
- plan.md、docs\adr\ADR-001–006

第一步核对（不要相信旧对话转述）：
1. git status --short；git log --oneline -8
   预期 HEAD 含 7af822e（T7.11.1 代码）、aeceb52（T7.11 文档）、
   4516d53（T7.11 代码）、14daf10（T7.10 文档）；工作区 clean。
2. 读 tasks.md 的 T7.11.1/T7.11/T7.7/T7.3 行与 AGENTS.md 的“当前状态”。
3. 跑 ctest（命令见下），确认 22/22；dist 25 文件 / 32.65MB；
   out\package setup 11.31MB、zip 13.66MB。
4. scripts\bench\baseline-dev.json 的 label 仍是 dev-2026-10-22-t7.9。
5. media\synthetic-suite\ 有 V1–V10；V8 的 5 个文件应 OK=0。
   media/ 不入库；若目录被清掉，按 T7.10 证据重建。

当前基线（2026-10-22，开发机）：
- T7.11.1 P7 跨 GOP 边界后退已修复（代码 7af822e）：
  - 2s GOP 1080p（build\test-media\real-gop2s-full.mp4）p95 **64.13ms**
    （p50 60.06 / max 66.87，20/20 落点正确，0 超 80）；
  - 1s GOP 1080p（V2）p95 **27.11ms**（脚本因无 2s GOP 返回 N/A，
    short_gop 组 pass=1）；
  - 1080p60 HEVC 2s（V3）p95 **67.31ms**（补充）；
  - 连续后退 2s/1s：p95 0.03/0.06ms（首步单峰 97.19/40.70ms，1/20 超 80）；
  - 长 GOP proxy-V1 p95 278.12ms（可用档）；4K proxy-V4 2s p95 421.64ms
    （仍超 150，诊断）。
  - 根因：后退第一步同步回拷 24 帧 FrameCache（76–105ms）。修复：窗口只留
    AVFrame 引用（硬解纹理不回拷）→ 目标帧先呈现 → 0ms 定时器 4 帧/片回拷；
    ≤1080p 硬解 extra_hw_frames=32；回拷必须旧帧优先，否则每 4 步掉窗口。
  - 2s 余量只有 15.9ms；**不是 T7.7/基线机**，基线机必须复跑。
- 全量 bench（bench-20261022-220615.json）：60 项/0 gating 失败/0 infra；
  30/30 基线容差；step1080 p95 0.84/max 56.41ms；P6/P9/P11 无回归；
  P1/P2 仍是热缓存。
- T7.10 合成 V10 2h soak `v10-synth` 7200s pass=1（替身 P14，不是正式 P14）；
  V1–V9 冒烟 18/18。
- T7.9 普通 seek 后 PgUp handoff 已完成（spot 0.06ms），与 T7.11.1 的跨 GOP
  专项是两条路径，不要互相代替。
- T7.7 仍被资源阻塞：真实 V1–V10、基线机（4C/8GB/集显/SATA SSD/Win11）、
  冷缓存 F1-A1、Explorer 真双击；没有这些只能写预检。
- T7.4 仍 4K proxy；T7.5 可达 SMB/物理设备丢失/真实 V8 未验；T7.6 F1-A1
  只有热缓存预检。

接下来（优先）：
1. T7.7 全量验收：真实 V1–V10 + 基线机 + 冷缓存 F1-A1 + Explorer 真双击；
   P6/P9 按目标距前关键帧 ≤2s 分类，长 GOP 单列；P7 边界按 T7.11.1 第 3.1
   节复跑 2s/1s，重点看 2s 的 15.9ms 余量；同时记录 iGPU 下
   extra_hw_frames=32 对 P10/P11 的影响。
2. T7.3 正式 P14：有真实 V10 时用真实 V10 复跑 7200s（-NoLoop）；合成 V10
   只作稳定性预检。
3. T7.8 维护：本轮未更新基线；step1080 p95 0.84ms 在容差内，max 改善。
   只有验证过的全量 run 才 -UpdateBaseline，不要用 -Quick，不要为漂移改
   thresholds.json。
4. 若基线机 P7 2s 仍超 80：按 T7.11.1 证据评估“seek 到关键帧后的空闲预解”
   或 D3D11 批量回拷；不要回到同步回拷整段窗口，不要改 80/150。
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
    -File build\test-media\real-gop2s-full.mp4 -Samples 20 -Tag t7111-gop2s
  # 1s GOP：-File media\synthetic-suite\V2.mp4 -Tag t7111-gop1s
  # 1080p60 HEVC：-File media\synthetic-suite\V3.mp4 -Tag t7111-gop2s-hevc60
  # 长 GOP：-File media\proxy\proxy-V1.mp4 -Tag t7111-gop10s（N/A 可用档）
  # 4K：-File media\proxy\proxy-V4.mp4 -Tag t7111-gop2s-4k（N/A 诊断）
- 连续后退/P6/P9：dist\lapian.exe --file <2s 素材> --drag-test --drag-samples 20
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
- T7.11.1：MediaSession 待回拷窗口（clone/命中/取消时机）、MainWindow
  cache_fill_timer_（0ms、4 帧/片、旧帧优先）、≤1080p extra_hw_frames=32、
  FrameCache::Put 持 D3D11ContextMutex。不要改回同步回拷整段窗口；不要删
  “目标在 FrameCache 命中时保留 pending”的逻辑；不要改回新帧优先。
- T7.11：GopBoundaryTestRunner、--gop-boundary-test/-samples/-tag、
  scripts\gop-boundary-bench.ps1。不要为了变绿放宽 80/150，不要把普通
  seek 后单步 0.06ms 当专项。
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
- P7 1080p ≤80ms、4K ≤150ms；跨 GOP 边界 2s 必须 ≤80ms，未达标收紧到 1s。
  T7.11.1 在开发机 2s 64.13ms/1s 27.11ms 通过；**但 T7.7 基线机未复跑，
  不能写“P7 全量通过”**。
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
- T7.11.1 的开发机 P7 边界通过不等于 T7.7/基线机通过；2s 余量 15.9ms。
- 任何“安装包已发布就绪”的结论。

工作方式：
- 计划内任务不用逐项确认；只有计划外、不可逆、规格歧义、权限/硬件限制
  （包括安装 Inno Setup/联网/管理员）才问。
- 一个任务 = 一次可审查变更；先提交代码/脚本，再提交文档（交接 + 证据）。
- 每个任务保持 build-release.ps1、ctest 22/22、dist 通过；打包任务补体积与安装证据。
- 只做 F1–F8/P1–P14；spec v0.3 冻结；不重做 T1–T7.11.1。
- 先给 3–5 行极短计划，再读 docs\handoff-2026-10-22-t7.11.1.md 的 §4。
```

## 使用说明

- 新 agent 必须先读文件、跑 `ctest`，以 `git log`/`tasks.md` 为准，不要只信摘要。
- 当前 HEAD 为 T7.11.1 文档提交（`git log` 首条，含
  `docs/perf/T7.11.1-gop-cache-defer.md`）；T7.11.1 代码 `7af822e`；
  T7.11 文档 `aeceb52`；T7.11 代码 `4516d53`；T7.10 文档 `14daf10`。
- T7.11.1 是开发机 + 派生/合成 2s/1s/1080p60 素材；不是 T7.7，也不是正式 V1。
- T7.10 是合成片单；合成 V10 soak `pass=1` 只是替身 P14 稳定性。
- T7.5 是合成/模拟证据；T7.4 只是 4K proxy；T7.6 安装证据只在本机。
- 基线 label 仍是 `dev-2026-10-22-t7.9`；本轮未更新基线。
- `bench-latest.json` 会被任意一次运行覆盖；引用证据用带时间戳的文件
  `bench-20261022-220615.json`。
- 合成片单在 `media/synthetic-suite/`（gitignore）；若被清掉，按
  `docs/perf/T7.10-synthetic-suite.md` 重建。
