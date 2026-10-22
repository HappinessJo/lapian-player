# 给下个 agent 的新会话提示词（可直接复制）

- **版本**：v21（2026-10-22；T7.6 打包/关联/许可/体积完成，F1-A1 待 T7.7，T7.3 P14 仍待 V10）
- **用法**：复制下面 ```text 代码块整段发给新 agent；它仍应先读文件、跑 ctest，不要只信摘要。

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL + D3D11 + WASAPI；spec.md v0.3 已冻结）。
工作区：E:\拉片软件开发。

先读（按顺序）：
- AGENTS.md
- docs\handoff-2026-10-22-t7.6.md（当前交接：T7.6 打包/关联/许可/体积、缺口）
- docs\perf\T7.6-packaging.md（T7.6 体积/安装/关联/卸载/启动/零网络证据）
- docs\handoff-2026-10-22-t7.5.md 与 docs\perf\T7.5-robustness.md（T7.5 边界与缺口）
- docs\handoff-2026-10-22-t7.4.md 与 docs\perf\T7.4-4k.md（T7.4 4K proxy 状态）
- docs\handoff-2026-10-21-t7.3.md 与 docs\perf\T7.3-soak.md（T7.3 状态，P14 待 V10）
- spec.md（§3.1 F1/F1-A1、§4.1、§4.4、§4.5）
- plan.md、tasks.md（M7 T7.1–T7.8）、docs\adr\ADR-001–006

第一步核对，不相信旧对话转述：
1. git status --short；git log --oneline -8
2. 读 tasks.md 的 T7.6/T7.5/T7.3 行与 AGENTS.md 的“当前状态”
3. 跑 ctest（命令见下），确认 22/22
4. 确认 dist 25 文件 / 32.63MB；HEAD 应包含 T7.6 代码提交。

当前基线（以 git log 为准）：
- T7.6 已完成（开发机、本机安装/卸载边界，不是 T7.7 正式验收）：
  - scripts\package.ps1 + scripts\installer.iss：Inno Setup 6.7.3 每用户安装
    （PrivilegesRequired=lowest，%LOCALAPPDATA%\Programs\拉片软件，无需管理员）。
  - setup 11.31MB；便携 zip 13.65MB（31 文件）；分发负载 32.74MB；
    安装后 33 文件/36.99MB；P12 全部 ≤100MB。
  - ProgID Lapian.Video + .mp4/.mov/.mkv/.webm OpenWithProgids；不设默认，
    UserChoice 未变；ShellExecuteEx 走 ProgID 启动实测通过。
  - 安装后启动预检：ui_ready 290–323ms、first_present 359–400ms（热缓存）；
    --robust-test 75 项/0 失败；便携 zip 同样 75/0。
  - 卸载：程序目录/快捷方式/ProgID/我们的关联值清除；settings/marks/sidecar
    保留；其它应用的 OpenWithProgids 值保留；安装/启动/卸载轮询 0 TCP/UDP。
  - 已知缺口：F1-A1 未正式验收（未冷缓存/未 Explorer 真双击/非基线机）；
    安装器 UI 英文；未签名、无应用图标；Inno 6.7.3 商业许可提示。
  - 本机 E: 被 Windows 视为不受信任位置，setup 从 E: 直接跑会因受限 token
    报 Error 5；测试时先把 setup 复制到 C:（真实用户从 Downloads 运行）。
- T7.5 仍是开发机合成/模拟边界：75 项/0 失败、只读 ACL 17/17、坏文件 ≤34ms、
  不可达 UNC 3.03s；可达 SMB、物理设备丢失、真实 V8 未验。
- T7.3 仍未通过 P14：90min 单文件 + 1h 子区间循环 pass，但 V10 2h 未做；
  不要写 P14/V10/基线机通过，也不要把 90min 当 2h。
- T7.4 仍只是 4K proxy，不是正式 V4。

接下来（优先）：
- T7.7 全量验收：V1–V10（含 V5 旋转）、F1–F8、P1–P14、100%–200% DPI；
  F1-A1 用 Explorer 真双击 + 冷缓存 + 基线机；安装/卸载在基线机复跑。
- T7.8 基准回归脚本：把 --robust-test/--drag-test/--memory-test 固化成脚本与
  阈值 JSON；可把 T7.6 的体积/哈希检查纳入。
- 有 V10 时随时插 T7.3 收口：
  scripts\soak-bench.ps1 -File <V10> -Seconds 7200 -Interval 30 -Tag v10-soak -NoLoop
  看 `T7.3 soak_summary` 的 pass/flags；memory_growth 时不要写 P14 通过。

常用命令：
- 构建+测试+dist：
  powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
- 分发打包（Inno 6.3+；本机 ISCC 在 %LOCALAPPDATA%\Programs\Inno Setup 6）：
  powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -SkipBuild
  只做便携 zip：-SkipInstaller；只重新压缩：-SkipBuild -SkipInstaller
- ctest：
  $env:TMP = Join-Path $PWD '.deps\tmp'; $env:TEMP = $env:TMP
  $env:PATH = (Join-Path $PWD 'third_party\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH
  ctest --test-dir build\release -C Release --output-on-failure
- T7.5 鲁棒性：
  dist\lapian.exe --robust-test --robust-tag verify
  powershell -ExecutionPolicy Bypass -File scripts\robust-readonly.ps1 -Tag verify
- 4K 探针（需要时）：
  dist\lapian.exe --file media\proxy\proxy-V4.mp4 --drag-test --drag-tag 4k-hard
- 安装/卸载验证（先把 setup 复制到 C:）：
  Copy-Item out\package\Lapian-0.1.0-setup.exe "$env:TEMP\t76\" -Force
  & "$env:TEMP\t76\Lapian-0.1.0-setup.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-
  & "$env:LOCALAPPDATA\Programs\拉片软件\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART

已交付、不要重写：
- T7.6：scripts\package.ps1、scripts\installer.iss、packaging\README.txt、
  build-release.ps1 -Package。不要为了“干净”删 OpenWithProgids 的共享键；
  不要在未确认默认应用的情况下写扩展名默认值/UserChoice。
- T7.5：PathProbe 3s 有界预检、OpenFile 原生分隔符/目录/本地可读性预检、
  MarkStore 长路径 \\?\ 前缀、AudioEngine 设备丢失模拟、--robust-test、
  scripts\robust-readonly.ps1。不要缩短网络预检或放宽 5s。
- T7.4 的顺序前进/4K 后退窗口/--drag-test；T7.1 异步索引；T7.2 FrameCache；
  T7.2.1 B=末帧；T7.3 soak 判定。不要为了安装包体积删 Qt/FFmpeg 必需 DLL
  或改技术栈（ADR-001/004）。
- 长 GOP 循环 B→A 148–157ms（F4-A2）留给 T7.7，不要顺手改。

判定口径（不要放宽阈值来变绿）：
- P12 ≤100MB；P1 ≤1.5s、P2 ≤2.5s（F1-A1）；零网络请求；每用户安装无需管理员。
- 4K P4/P6/P7/P8/P9、P14 等数字沿用既有口径，不要在本任务重测或改口径。
- 安装/卸载的“无残留”以程序文件/快捷方式/ProgID 为准；用户数据不删不算残留；
  共享 OpenWithProgids 键里其它应用的值保留不算残留。

不要提前宣称：
- 正式 V4/V1/V5/V10、基线机、冷缓存；4K proxy 不能代替 V4。
- T7.3 P14/V10；T7.4 软解/seek 后 PgUp；T7.5 真实 V8/物理设备丢失/可达 SMB。
- T7.6 的“双击关联”只在本机验证时，不要写成 T7.7 正式 F1-A1 通过；
  安装器 UI 英文/未签名/无图标是已知缺口，不要写成“发布就绪”。

工作方式：
- 计划内任务不用逐项确认；只有计划外、不可逆、规格歧义、权限/硬件限制
  （包括安装 Inno Setup 需要联网/管理员）才问。
- 一个任务 = 一次可审查变更；先提交代码/脚本，再提交文档（交接 + 证据）。
- 每个任务保持 build-release.ps1、ctest 22/22、dist 通过；打包任务补体积与安装证据。
- 只做 F1–F8/P1–P14；spec v0.3 冻结；不重做 T1–T7.6。
- 先给 3–5 行极短计划，再读 docs\handoff-2026-10-22-t7.6.md 的 §5 下一步。
```

## 使用说明

- 新 agent 必须先读文件、跑 `ctest`，以 `git log`/`tasks.md` 为准，不要只信这段摘要。
- 若 HEAD 已前进，以最新提交和 `tasks.md` 为准；本提示词只描述 2026-10-22 的状态。
- T7.5 是开发机合成/模拟证据；T7.4 只是 4K proxy；T7.3 的正式收口仍差 V10 2h。
- T7.6 的安装/卸载证据是开发机本机；F1-A1 的冷缓存/真双击/基线机留 T7.7。
