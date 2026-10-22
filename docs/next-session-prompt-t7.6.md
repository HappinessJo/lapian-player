# 给下个 agent 的 T7.6 提示词（可直接复制）

> 状态：T7.6 已完成（2026-10-22，代码提交 `1b349a7`）；本文件保留为任务说明。
> 当前交接见 [handoff-2026-10-22-t7.6.md](handoff-2026-10-22-t7.6.md)。

- **版本**：v1（2026-10-22；T7.5 已收口，下一步 T7.6 打包/关联/许可/体积）
- **基线**：`0887022`（T7.5 文档）/ `db9e004`（T7.5 代码），工作区 clean；
  `ctest` 22/22；`dist` 32.63MB / 25 文件；T7.3 P14 仍待 V10，T7.4 仍只是 4K proxy。
- **用法**：复制下面 ```text 代码块整段发给新 agent；它仍应先读文件、跑 ctest。

```text
你是「拉片软件」项目的 coding agent。项目是 Windows 桌面端的帧级参考片审阅器
（C++20 + Qt 6 Widgets + FFmpeg LGPL 动态链接 + D3D11 + WASAPI；spec.md v0.3 已冻结）。
工作区：E:\拉片软件开发。

先读（按顺序）：
- AGENTS.md
- docs\handoff-2026-10-22-t7.5.md（当前交接：T7.5 错误与鲁棒性、缺口）
- docs\perf\T7.5-robustness.md（T7.5 数字与口径）
- spec.md §3.1（F1 打开/分发形态/F1-A1）、§4.4（稳定性）、§4.5（轻量化/P12/安装包）
- tasks.md 的 T7.6 行、T7.5/T7.3 行
- docs\handoff-2026-10-22-t7.4.md 与 docs\perf\T7.4-4k.md（T7.4 4K proxy 背景）
- plan.md、docs\adr\ADR-001/004/006（技术栈、轻量化、持久化边界）

第一步核对，不相信旧对话转述：
1. git status --short；git log --oneline -6
2. 读 tasks.md 的 T7.6/T7.5/T7.3 行与 AGENTS.md 的“当前状态”
3. 跑 ctest（命令见下），确认 22/22
4. 确认 dist 25 文件 / 32.63MB 左右；HEAD 应包含 `db9e004` 与 `0887022`

当前基线（以 git log 为准）：
- T7.5 已完成（开发机、合成/模拟边界）：`--robust-test` 75 项/0 失败、
  只读 ACL 17/17、坏文件 ≤34ms、不可达 UNC 3.03s（修复前 21s）、
  288 字符长路径 sidecar/截图通过、设备丢失模拟恢复。
- 已知缺口：可达 SMB 成功路径、物理 USB 设备丢失、真实 V8、磁盘满未测；
  网络预检同步阻塞最多 3s（<5s 上界）。不要把 T7.5 写成 V8/物理设备/可达共享通过。
- T7.3 仍未通过 P14：90min 单文件 + 1h 子区间循环 pass，但 V10 2h 未做；
  不要写 P14/V10/基线机通过，也不要把 90min 当 2h。
- T7.4 仍只是 4K proxy，不是正式 V4。

接下来做 T7.6（打包/关联/许可/体积），按 tasks.md 与 spec §4.5：
- Inno Setup **每用户安装**（不要求管理员；默认目录用 {localappdata}\Programs\拉片软件
  或 Inno 的 per-user 等价目录），安装/卸载不联网。
- 注册 ProgID 与文件关联（至少 .mp4/.mov/.mkv/.webm；**不强制设为默认应用**，
  由用户在“打开方式”里确认）；便携 zip 可选但要做。
- 许可文本进安装包：licenses\LGPL-2.1.txt、LGPL-3.0.txt、GPL-3.0.txt、
  THIRD-PARTY-NOTICES.md、SOURCE-OFFER.md（按实际依赖取舍，不要漏 LGPL 文本）。
- 卸载清理：程序文件、开始菜单/桌面快捷方式、注册表 ProgID/关联；**不要删除用户的
  sidecar、%APPDATA%\拉片软件\marks、settings.json**（属用户数据），在文档写清边界。
- P12 ≤100MB（当前 32.63MB，余量充足）；安装后仍零网络请求（check-imports.ps1
  已断言不存在 Qt6Network.dll 等）。
- F1-A1：双击已关联文件冷启动到首帧 ≤2.5s、窗口可交互 ≤1.5s；本机安装只能作为
  预检，正式 F1-A1 要基线机/真实双击，不能把本机结果写成 T7.7 通过。

环境注意：
- 本机当前没有检测到 Inno Setup（`iscc` 不在 PATH，Program Files 也没有）。
  先检查 winget/choco 或已下载的安装包；**需要联网安装软件时按“权限/环境限制”先问用户**，
  不要静默联网，也不要把安装包/第三方二进制提交进仓库（dist/、out/ 已 gitignore）。
- 新增打包脚本放 scripts\（例如 package.ps1 / installer.iss），要求可重复执行；
  安装包与便携 zip 输出到 dist/ 或 out/，不污染源码树。

验证建议：
- 安装到当前用户目录 → 启动 lapian.exe → 检查注册表 ProgID/关联 → 用
  `Start-Process <mp4>` 或 Explorer 双击测关联（Explorer 行为可标注手测）；
- 卸载后检查程序目录/快捷方式/ProgID 残留；
- 便携 zip 解压到临时目录可直接运行；
- 安装/卸载期间用进程/网络工具确认无网络请求；
- 安装前后 `dist` 体积（P12）与 `check-imports.ps1`；
- 全程保持 build-release.ps1、ctest 22/22、dist 打包通过；文档补 perf/交接。

有 V10 时随时插 T7.3 收口：
  scripts\soak-bench.ps1 -File <V10> -Seconds 7200 -Interval 30 -Tag v10-soak -NoLoop
看 `T7.3 soak_summary` 的 pass/flags；memory_growth 时不要写 P14 通过。

常用命令：
- 构建+测试+打包：
  powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
- ctest：
  $env:TMP = Join-Path $PWD '.deps\tmp'; $env:TEMP = $env:TMP
  $env:PATH = (Join-Path $PWD 'third_party\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH
  ctest --test-dir build\release -C Release --output-on-failure
- T7.5 回归（改打包/安装前先跑一遍）：
  dist\lapian.exe --robust-test --robust-tag verify
  powershell -ExecutionPolicy Bypass -File scripts\robust-readonly.ps1 -Tag verify
- 4K 回归（可选，确认安装包内 exe 未退化）：
  dist\lapian.exe --file media\proxy\proxy-V4.mp4 --drag-test --drag-tag 4k-hard

已交付、不要重写：
- T7.5：`PathProbe` 3s 有界预检、OpenFile 原生分隔符/目录/本地可读性预检、
  MarkStore 长路径 `\\?\` 前缀、AudioEngine 设备丢失模拟、`--robust-test`、
  `scripts\robust-readonly.ps1`。不要缩短网络预检或放宽 5s。
- T7.4 的顺序前进/4K 后退窗口/`--drag-test`；T7.1 异步索引；T7.2 FrameCache；
  T7.2.1 B=末帧；T7.3 soak 判定。不要为了安装包体积删 Qt/FFmpeg 必需 DLL
  或改技术栈（ADR-001/004）。
- 长 GOP 循环 B→A 148–157ms（F4-A2）留给 T7.7，不要顺手改。

判定口径（不要放宽阈值来变绿）：
- P12 ≤100MB；P1 ≤1.5s、P2 ≤2.5s（F1-A1）；零网络请求；每用户安装无需管理员。
- 4K P4/P6/P7/P8/P9、P14 等数字沿用既有口径，不要在本任务重测或改口径。
- 安装/卸载的“无残留”以程序文件/快捷方式/ProgID 为准；用户数据不删不算残留。

不要提前宣称：
- 正式 V4/V1/V5/V10、基线机、冷缓存；4K proxy 不能代替 V4。
- T7.3 P14/V10；T7.4 软解/seek 后 PgUp；T7.5 真实 V8/物理设备丢失/可达 SMB。
- T7.6 的“双击关联”只在本机验证时，不要写成 T7.7 正式 F1-A1 通过。

工作方式：
- 计划内任务不用逐项确认；只有计划外、不可逆、规格歧义、权限/硬件限制
  （包括安装 Inno Setup 需要联网/管理员）才问。
- 一个任务 = 一次可审查变更；先提交代码/脚本，再提交文档（交接 + 证据）。
- 每个任务保持 build-release.ps1、ctest 22/22、dist 通过；打包任务补体积与安装证据。
- 只做 F1–F8/P1–P14；spec v0.3 冻结；不重做 T1–T7.5。
- 先给 3–5 行极短计划，再读 `docs\handoff-2026-10-22-t7.5.md` 的 §5 下一步。
```

## 使用说明

- 基线提交与数字来自 2026-10-22：`db9e004`（T7.5 代码）、`0887022`（文档）；
  若 HEAD 已前进，以 `git log`/`tasks.md` 为准。
- T7.6 的第一个现实问题是 Inno Setup 是否可用；不要把“没装工具”变成静默联网。
- T7.3 的 V10 2h 与 T7.4 的正式 V4 都不因 T7.6 完成而通过。
