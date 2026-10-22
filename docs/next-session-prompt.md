# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v22（2026-10-22；T7.6 已完成，基线 `1b349a7`/`81942ca`；下一步 T7.7/T7.8，V10 2h 仍待）
- **用法**：复制下面 ```text 代码块整段发给新 agent；它仍应先读文件、跑 ctest，不要只信摘要。

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL 动态链接 + D3D11 + WASAPI；spec.md v0.3 已冻结）。
工作区：E:\拉片软件开发。

先读（按顺序）：
- AGENTS.md（当前状态 v6.0）
- docs\handoff-2026-10-22-t7.6.md（当前交接：T7.6 打包/关联/许可/体积、缺口）
- docs\perf\T7.6-packaging.md（T7.6 体积/安装/关联/卸载/启动/零网络证据）
- tasks.md 的 T7.6/T7.7/T7.8/T7.3 行
- docs\handoff-2026-10-22-t7.5.md 与 docs\perf\T7.5-robustness.md（T7.5 边界）
- docs\handoff-2026-10-22-t7.4.md 与 docs\perf\T7.4-4k.md（T7.4 4K proxy）
- docs\handoff-2026-10-21-t7.3.md 与 docs\perf\T7.3-soak.md（T7.3 P14 待 V10）
- spec.md §3.1（F1/F1-A1）、§4.1、§4.4、§4.5
- plan.md、docs\adr\ADR-001–006

第一步核对（不要相信旧对话转述）：
1. git status --short；git log --oneline -6
   预期 git log 包含 81942ca（T7.6 文档）与 1b349a7（T7.6 代码/脚本），
   工作区 clean；提示词更新提交可能在其后，以 git log/tasks.md 为准。
2. 读 tasks.md 的 T7.6/T7.7/T7.3 行与 AGENTS.md 的“当前状态”。
3. 跑 ctest（命令见下），确认 22/22。
4. 确认 dist 25 文件 / 32.63MB；out\package 下有 setup 11.31MB、
   便携 zip 13.65MB、package-report.txt（out/ 已 gitignore，不入库）。

当前基线（2026-10-22，开发机）：
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
2. T7.8 基准回归脚本：把 --robust-test/--drag-test/--memory-test 固化成脚本与
   阈值 JSON；可把 T7.6 的体积/哈希检查纳入。T7.8 不需要等 V10。
3. 有 V10 时随时插 T7.3 收口：
   scripts\soak-bench.ps1 -File <V10> -Seconds 7200 -Interval 30 -Tag v10-soak -NoLoop
   看 `T7.3 soak_summary` 的 pass/flags；memory_growth 时不要写 P14 通过。
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
- T7.6：scripts\package.ps1、scripts\installer.iss、packaging\README.txt、
  build-release.ps1 -Package。不要为了“干净”删共享 OpenWithProgids 键；
  不要在未确认默认应用的情况下写扩展名默认值/UserChoice；不要删用户数据。
- T7.5：PathProbe 3s 有界预检、OpenFile 原生分隔符/目录/本地可读性预检、
  MarkStore 长路径 \\?\ 前缀、AudioEngine 设备丢失模拟、--robust-test、
  scripts\robust-readonly.ps1。不要缩短网络预检或放宽 5s。
- T7.4：顺序前进/4K 后退窗口/--drag-test；不要为了 P7 降质/丢帧或放宽 150ms。
- T7.1 异步索引；T7.2 FrameCache；T7.2.1 B=末帧；T7.3 soak 判定。
- 长 GOP 循环 B→A 148–157ms（F4-A2）留给 T7.7，不要顺手改。
- 不要为了安装包体积删 Qt/FFmpeg 必需 DLL 或改技术栈（ADR-001/004）。

判定口径（不要放宽阈值来变绿）：
- P12 ≤100MB；P1 ≤1.5s、P2 ≤2.5s（F1-A1）；零网络请求；每用户安装无需管理员。
- 4K P4≥20fps、P6≤300ms、P7≤150ms、P8≥12fps 每帧呈现、P9≤300ms、P11≤1.5GB；
  P6/P9 以目标距前关键帧 ≤2s 为前提。
- P14：2h 1080p 连续播放，WS ≤1200MB，无持续增长，不崩溃/卡死；V10 必选。
- 安装/卸载“无残留”以程序文件/快捷方式/ProgID 为准；用户数据不删不算残留；
  共享 OpenWithProgids 键里其它应用的值保留不算残留。
- T7.6 的热缓存启动数字不能替代 F1-A1；F1-A1 必须冷缓存 + Explorer 真双击 +
  基线机。不要把 4K proxy 当 V4，也不要把 90min soak 当 V10 2h。

不要提前宣称：
- 正式 V4/V1/V5/V10、基线机、冷缓存；4K proxy 不能代替 V4。
- T7.3 P14/V10；T7.4 软解/seek 后 PgUp；T7.5 真实 V8/物理设备丢失/可达 SMB。
- T7.6 的安装/关联只在本机通过；安装器 UI 英文/未签名/无图标是已知缺口。
- 任何“安装包已发布就绪”的结论；目前只有开发机分发包与预检证据。

工作方式：
- 计划内任务不用逐项确认；只有计划外、不可逆、规格歧义、权限/硬件限制
  （包括安装 Inno Setup/联网/管理员）才问。
- 一个任务 = 一次可审查变更；先提交代码/脚本，再提交文档（交接 + 证据）。
- 每个任务保持 build-release.ps1、ctest 22/22、dist 通过；打包任务补体积与安装证据。
- 只做 F1–F8/P1–P14；spec v0.3 冻结；不重做 T1–T7.6。
- 先给 3–5 行极短计划，再读 docs\handoff-2026-10-22-t7.6.md 的 §5 下一步。
```

## 使用说明

- 新 agent 必须先读文件、跑 `ctest`，以 `git log`/`tasks.md` 为准，不要只信这段摘要。
- 当前基线 `1b349a7`（T7.6 代码）/ `81942ca`（T7.6 文档）；若 HEAD 已前进，
  以最新提交和 `tasks.md` 为准。
- T7.5 是开发机合成/模拟证据；T7.4 只是 4K proxy；T7.3 的正式收口仍差 V10 2h。
- T7.6 的安装/卸载证据是开发机本机；F1-A1 的冷缓存/真双击/基线机留 T7.7。
