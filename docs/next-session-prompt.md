# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v24（2026-10-22；T7.8 已完成，HEAD `74a4f0b` 文档 / `74609e1` 提示词；下一步 T7.7，V10 2h 仍待）
- **用法**：复制下面 ```text 代码块整段发给新 agent；它仍应先读文件、跑 ctest，不要只信摘要。

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL 动态链接 + D3D11 + WASAPI；spec.md v0.3 已冻结）。
工作区：E:\拉片软件开发。

先读（按顺序）：
- AGENTS.md（当前状态 v6.1）
- docs\handoff-2026-10-22-t7.8.md（当前交接：T7.8 基准回归、阈值/基线、P7 seek 后 PgUp 缺口）
- docs\perf\T7.8-bench.md（T7.8 8 套件/54 项、26 项同机漂移、P12 哈希、缺口）
- tasks.md 的 T7.8/T7.7/T7.3 行
- docs\handoff-2026-10-22-t7.6.md 与 docs\perf\T7.6-packaging.md（T7.6 打包/关联/许可/体积）
- docs\handoff-2026-10-22-t7.5.md 与 docs\perf\T7.5-robustness.md（T7.5 边界）
- docs\handoff-2026-10-22-t7.4.md 与 docs\perf\T7.4-4k.md（T7.4 4K proxy）
- docs\handoff-2026-10-21-t7.3.md 与 docs\perf\T7.3-soak.md（T7.3 P14 待 V10）
- spec.md §3.1（F1/F1-A1）、§4.1、§4.4、§4.5
- plan.md、docs\adr\ADR-001–006

第一步核对（不要相信旧对话转述）：
1. git status --short；git log --oneline -6
   预期 git log 包含 74a4f0b（T7.8 文档）与 74609e1（T7.8 提示词 v24），
   其前依次为 71ce5fb（thresholds 说明）、756b4a7（build-release 收尾 exit 0）、
   892c5ac（T7.8 脚本/阈值/基线）；若 HEAD 再前进，以 git log 为准；工作区 clean。
2. 读 tasks.md 的 T7.8/T7.7/T7.3 行与 AGENTS.md 的“当前状态”。
3. 跑 ctest（命令见下），确认 22/22。
4. 确认 dist 25 文件 / 32.63MB；out\package 下有 setup 11.31MB、
   便携 zip 13.65MB、package-report.txt（out/ 已 gitignore，不入库）。
5. 确认 scripts\bench\baseline-dev.json 存在，label=dev-2026-10-22；
   scripts\bench\thresholds.json 的阈值仍来自 spec。

当前基线（2026-10-22，开发机）：
- T7.8 已完成（开发机基准回归，不是 T7.7 正式验收）：
  - scripts\bench-regression.ps1 + scripts\bench\thresholds.json +
    scripts\bench\baseline-dev.json；一条命令跑 8 套件：
    startup（P1/P2）、drag1080、drag4k（P4/P6/P7/P8/P9）、
    step1080/step4k（P7 各 40 步）、memory（P10/P11/空态）、
    robust（T7.5）、package（P12/哈希）；-IncludeReadonly 可加 ACL 17/17。
  - 全量 194.8s：8 套件 / 54 项检查 / 0 失败 / 0 infra。
  - 基线 dev-2026-10-22（bench-20261022-183348.json）与验证
    bench-20261022-184030.json：26/26 在容差内；24/26 严格 ±10%，
    2 项 P7 后退 p95 绝对差 0.19/0.60ms 按 5ms 噪声下限。
  - 关键数字：P1 p50 274/P2 328ms（热缓存）、P6 1080p 57.6/4K 145.2ms、
    P7 p95 0.95/2.93ms、P9 p95 61.5/228.0ms、P10 96.3MB、
    P11 1080p 102.2MB/4K 116.9MB、P12 32.63/11.31/13.65MB。
  - 已知缺口：P1/P2 是热缓存；素材是 proxy-V1/proxy-V4；不是基线机；
    seek 后紧接 PgUp 单样本 1080p 372.49ms、4K 205.71ms，仍超 80/150ms
    （T7.4 §9.5 已记录 4K，T7.8 现在也记录 1080p；drag_summary 的 p95 通过
    不掩盖这个单样本）；4K 40 步后退 max 418–427ms 的缓存重填尖峰仍在。
  - 脚本退出码：0 通过；1 spec 失败；2 infra；3 基线漂移；4 诊断（软解）。
    -Quick 只用于调试，不能作为验收证据，也不能写基线。
  - build-release.ps1 已在成功收尾显式 exit 0；E: 上仍可能打印
    `Cannot open E:/`（外部工具 stderr 噪声，不是构建失败）。
- T7.6 已完成（本机安装/卸载边界，不是 T7.7 正式验收）：
  - scripts\package.ps1 + scripts\installer.iss：Inno Setup 6.7.3 每用户安装
    （PrivilegesRequired=lowest，%LOCALAPPDATA%\Programs\拉片软件，无需管理员）。
  - setup 11.31MB；便携 zip 13.65MB（31 文件）；分发负载 32.74MB；
    安装后 33 文件/36.99MB；P12 三种形态均 ≤100MB。
  - ProgID Lapian.Video + .mp4/.mov/.mkv/.webm OpenWithProgids；不设默认，
    UserChoice 未变；ShellExecuteEx 走 ProgID 启动实测通过（窗口 359ms）。
  - 安装后启动预检：ui_ready 290–323ms、first_present 359–400ms（热缓存）；
    --robust-test 75 项/0 失败；便携 zip 同样 75/0。
  - 卸载：程序目录/快捷方式/ProgID/我们写入的关联值清除；settings/marks/sidecar
    保留；共享 OpenWithProgids 键里其它应用的值保留；安装/启动/鲁棒/卸载
    轮询到 0 TCP/UDP。
  - 已知缺口：F1-A1 未正式验收（未冷缓存/未 Explorer 真双击/非基线机）；
    安装器 UI 英文；未签名、无应用图标；Inno 6.7.3 对商业用户有购买许可提示。
  - 本机 E: 被 Windows 视为不受信任位置：setup 从 E: 直接跑会因受限 token
    报 Error 5。测试安装时先把 setup 复制到 C:（真实用户从 Downloads 运行）。
- T7.5 仍是开发机合成/模拟边界：75 项/0 失败、只读 ACL 17/17、坏文件 ≤34ms、
  不可达 UNC 3.03s；可达 SMB、物理设备丢失、真实 V8 未验。
- T7.3 仍未通过 P14：90min 单文件 + 1h 子区间循环 pass，但 V10 2h 未做；
  不要写 P14/V10/基线机通过，也不要把 90min 当 2h。
- T7.4 仍只是 4K proxy，不是正式 V4。

接下来（优先）：
1. T7.7 全量验收：V1–V10（含 V5 旋转）、F1–F8、P1–P14、100%–200% DPI；
   F1-A1 用 Explorer 真双击 + 冷缓存（按 spec §4.1 定义）+ 基线机；
   安装/卸载在基线机复跑。没有片单/基线机时，只能写预检，不能写通过。
   先把 P7 “seek 后紧接 PgUp”的单样本缺口处理掉或明确重新权衡：
   当前 1080p 372ms、4K 206ms 超 80/150ms；不要为了单样本放宽 80/150。
2. 有 V10 时随时插 T7.3 收口：
   scripts\soak-bench.ps1 -File <V10> -Seconds 7200 -Interval 30 -Tag v10-soak -NoLoop
   看 `T7.3 soak_summary` 的 pass/flags；memory_growth 时不要写 P14 通过。
3. T7.8 维护：有意的性能改动完成后再用 -UpdateBaseline 更新基线；
   不要用 -Quick 写基线；不要为了让漂移变绿改 thresholds.json。
   换机时基线会自动跳过（machine_match=false），只做 spec 阈值检查。
4. 安装器增强是候选，不阻塞 T7.7：中文 ChineseSimplified.isl、应用图标、
   代码签名、升级/降级路径。需要联网/安装工具时先问用户，不要静默联网。

常用命令：
- 构建+测试+dist：
  powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
- 构建+测试+dist+安装包/zip（需要 Inno Setup 6.3+）：
  powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1 -Package
- 只打包（使用现有 dist）：
  powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -SkipBuild
  只做便携 zip：-SkipInstaller；只重压缩：-SkipBuild -SkipInstaller
- ctest：
  $env:TMP = Join-Path $PWD '.deps\tmp'; $env:TEMP = $env:TMP
  $env:PATH = (Join-Path $PWD 'third_party\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH
  ctest --test-dir build\release -C Release --output-on-failure
- T7.8 基准回归（全量约 195s；同机对比基线）：
  powershell -ExecutionPolicy Bypass -File scripts\bench-regression.ps1
  只跑部分：-Suites startup,drag1080,package
  快速调试：-Quick -SkipBaseline（不是验收证据）
  软解诊断：-Suites drag1080 -AllowSoftwareDecode（exit 4）
  只读 ACL：-IncludeReadonly
- T7.5 鲁棒性：
  dist\lapian.exe --robust-test --robust-tag verify
  powershell -ExecutionPolicy Bypass -File scripts\robust-readonly.ps1 -Tag verify
- 安装后启动预检（不是 F1-A1）：
  & "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe" --startup-test `
    --file media\proxy\proxy-V1.mp4
- 安装/卸载验证（先把 setup 复制到 C:）：
  Copy-Item out\package\Lapian-0.1.0-setup.exe "$env:TEMP\t76\" -Force
  & "$env:TEMP\t76\Lapian-0.1.0-setup.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-
  & "$env:LOCALAPPDATA\Programs\拉片软件\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART

已交付、不要重写：
- T7.8：scripts\bench-regression.ps1、scripts\bench\thresholds.json、
  scripts\bench\baseline-dev.json、docs\perf\T7.8-bench.md。
  阈值只来自 spec；基线漂移用 ±10% + 噪声下限；P9/步进用 p50、P6 1080p
  用 10ms 下限的原因写在 thresholds.json 的 baseline.notes，不要静默改。
  1080p 40 样本、4K 20 样本；1080p 的 40 样本是稳定性选择，不是放宽 spec。
- T7.6：scripts\package.ps1、scripts\installer.iss、packaging\README.txt、
  build-release.ps1 -Package。不要为了“干净”删共享 OpenWithProgids 键；
  不要在未确认默认应用的情况下写扩展名默认值/UserChoice；不要删用户数据。
- T7.5：PathProbe 3s 有界预检、OpenFile 原生分隔符/目录/本地可读性预检、
  MarkStore 长路径 \\?\ 前缀、AudioEngine 设备丢失模拟、--robust-test、
  scripts\robust-readonly.ps1。不要缩短网络预检或放宽 5s。
- T7.4：顺序前进/4K 后退窗口/--drag-test；不要为了 P7 降质/丢帧或放宽 150ms。
- T7.1 异步索引；T7.2 FrameCache；T7.2.1 B=末帧；T7.3 soak 判定。
- 长 GOP 循环 B→A 148–157ms（F4-A2）留给 T7.7，不要顺手改。
- build-release.ps1 末尾的 `exit 0` 是 E: 工具 stderr 噪声的收尾处理；
  不要删掉，也不要把它当成“忽略构建失败”的依据（关键步骤由 Invoke-Tool 检查）。
- 不要为了安装包体积删 Qt/FFmpeg 必需 DLL 或改技术栈（ADR-001/004）。

判定口径（不要放宽阈值来变绿）：
- P12 ≤100MB；P1 ≤1.5s、P2 ≤2.5s（F1-A1）；零网络请求；每用户安装无需管理员。
- 4K P4≥20fps、P6≤300ms、P7≤150ms、P8≥12fps 每帧呈现、P9≤300ms、P11≤1.5GB；
  P6/P9 以目标距前关键帧 ≤2s 为前提。
- P14：2h 1080p 连续播放，WS ≤1200MB，无持续增长，不崩溃/卡死；V10 必选。
- 安装/卸载“无残留”以程序文件/快捷方式/ProgID 为准；用户数据不删不算残留；
  共享 OpenWithProgids 键里其它应用的值保留不算残留。
- T7.6 的热缓存启动数字不能替代 F1-A1；F1-A1 必须冷缓存 + Explorer 真双击 +
  基线机。T7.8 的 P1/P2 同样是热缓存，也不能替代 F1-A1。
- T7.8 的 26 项漂移是“同机可复现”证据，不是 T7.7 片单验收；24/26 严格 ±10%、
  2 项按 5ms 下限，报告里必须如实写，不要写成“26/26 严格 ±10%”。

不要提前宣称：
- 正式 V4/V1/V5/V10、基线机、冷缓存；4K proxy 不能代替 V4。
- T7.3 P14/V10；T7.4 软解/seek 后 PgUp；T7.5 真实 V8/物理设备丢失/可达 SMB。
- T7.6 的安装/关联只在本机通过；安装器 UI 英文/未签名/无图标是已知缺口。
- T7.8 的基准回归只覆盖 1080p/4K 代理与 P1–P12 的一部分；不是 T7.7，
  也不是正式 V1–V10 片单报告；seek 后 PgUp 单样本仍超限。
- 任何“安装包已发布就绪”的结论；目前只有开发机分发包与预检证据。

工作方式：
- 计划内任务不用逐项确认；只有计划外、不可逆、规格歧义、权限/硬件限制
  （包括安装 Inno Setup/联网/管理员）才问。
- 一个任务 = 一次可审查变更；先提交代码/脚本，再提交文档（交接 + 证据）。
- 每个任务保持 build-release.ps1、ctest 22/22、dist 通过；打包任务补体积与安装证据。
- 只做 F1–F8/P1–P14；spec v0.3 冻结；不重做 T1–T7.8。
- 先给 3–5 行极短计划，再读 docs\handoff-2026-10-22-t7.8.md 的 §5 下一步。
```

## 使用说明

- 新 agent 必须先读文件、跑 `ctest`，以 `git log`/`tasks.md` 为准，不要只信这段摘要。
- 当前 HEAD `74a4f0b`（T7.8 文档）/`74609e1`（提示词 v24）；T7.8 代码/基线 `892c5ac`，构建收尾
  `756b4a7`，thresholds 说明 `71ce5fb`。若 HEAD 已前进，以最新提交和
  `tasks.md` 为准。
- T7.8 是开发机基准回归（热缓存、代理素材、非基线机）；T7.5 是合成/模拟证据；
  T7.4 只是 4K proxy；T7.3 的正式收口仍差 V10 2h。
- T7.6 的安装/卸载证据是开发机本机；F1-A1 的冷缓存/真双击/基线机留 T7.7。
- T7.8 的 `bench-latest.json` 会被任意一次运行覆盖；引用证据用带时间戳的文件。
