# 下一个会话提示词：T8.16（打包 / 隐私 / 卸载，非 MVP 门）

- **版本**：v1（2026-10-24，T8.14 M2 门收口后；基线 commit `0dc71ae`）
- **直接复制本文件给新会话。** 上一会话已完成 **T8.14（M2 门，pass）**；新会话**只做 T8.16（打包/隐私/卸载）**，不要做 T8.15（大会员条件验收）/T8.6.1b（拖动预览），也不要把 T8.14 的 M2 数字重跑一遍当成 T8.16 的验收。
- **当前状态**：**M1 于 T8.8 通过；M2 于 T8.14 通过（六条硬条件）**；`tasks-v2.md` **v0.25**、`AGENTS.md` **v6.37**、`ctest` **35/35**、`dist` **34.3MB/25**；T8.15 尚未执行（当前凭据是普通用户，无大会员账号；若执行只能写“未验证”）；T8.6.1b 未做；T8.14 残余风险（1 次 teardown `0xC0000374`、1 次 L-1 seed 卡住）**未定位/未复现**，不得写“无崩溃/已证明稳定”。
- **T8.14 基线（不得退化）**：`ctest` 35/35；`--online-m2-test` OB-1/2/3 **3/3 34/0**（ready 330.5/365.6/353.6ms、qn=80/codecid=7、fps 30.000/29.412/25.000、音画 p95 17.6/17.4/20.0ms、内存峰值 204.5/204.6/206.6MB）；OP1 p95 327/366/343ms、OP2 46/57/53ms、OP3 提示 0ms/取消 26ms/落点 0、OP4 300s/0 停顿、OP5 20/20 p95 111.7ms/0 请求；O9 0 TCP/UDP、key-test 77/0；P1/P2/P7 p95 311.87/390.59/1.47/0.53ms；故障矩阵 10/10 92/0；dist 34.3MB/25。

## 1. 先读

1. [AGENTS.md](../AGENTS.md)（v6.37：当前状态/v2/约束/文档表）
2. [tasks-v2.md](../tasks-v2.md) 的 **T8.16 行**、T8.14 行、版本记录 v0.25；**[T8.14 交接](handoff-2026-10-24-t8.14-m2.md)**、**[T8.14 证据](perf/v2-online-m2.md)**
3. [spec-v1.0-delta.md](spec-v1.0-delta.md) v0.4：**O5（缓存与配额）、O7（错误/断网/恢复）、O8（凭据与隐私）、O9（本地零网络）、§6（隐私/合规/网络边界）**；重点 O5-A1/A2/A5/A8、O7-A6/A9、O8-A1/A3/A4/A7、O9-A1–A6
4. [ADR-007](adr/ADR-007-bilibili-online.md)、[T7.6 打包证据](perf/T7.6-packaging.md)（安装/关联/许可/体积/卸载的现状与限制）、[T7.5 鲁棒性](perf/T7.5-robustness.md)、[T7.7 验收清单](perf/T7.7-acceptance-checklist.md)（零网络/安装/卸载/P12 口径）
5. [T8.2 证据](perf/v2-online-http-selfcheck.md)（DPAPI/凭据/脱敏）、[T8.3 证据](perf/v2-online-cache.md)（默认缓存根/配额/清空）、[T8.3.1 证据](perf/v2-online-cache-manage.md)（设置页缓存目录/清空）、[T8.7 证据](perf/v2-online-cache-hit.md)（在线记录/截图在缓存根外）、[T8.13 证据](perf/v2-online-faults.md)（错误/关闭/脱敏）
6. 代码/脚本入口：
   - `scripts/installer.iss`、`scripts/package.ps1`、`scripts/check-imports.ps1`、`scripts/build-release.ps1`、`packaging/README.txt`、`licenses/`
   - `src/online/CacheStore.*`（默认根/配额/清空）、`src/app/CacheManager.*`（目录/配额/孤儿清理）、`src/app/MainWindow.*`（在线入口/隐私说明/凭据 UI）、`src/online/QrLogin*`、`src/app/OnlineResolveDialog.*`（首次隐私说明）
   - T8.14 探针：`--m2-op5 --m2-default-paths`（默认 AppData 缓存命中、0 网络）、`--key-test`、`--memory-test`、`scripts/bench-regression.ps1`
   - 上一轮新增的 `--m2-op4`/`--m2-op5`/`--m2-default-paths`/`--m2-qn`/`--m1-throttle-bps` 只作回归，不要改语义

## 2. 任务边界

**只做 T8.16：打包/隐私/卸载（非 MVP 门，M2 后必须做）。** 官方验收（tasks-v2）：

> 安装/卸载通过；**安装 0 TCP/UDP**；P12 ≤100MB；**卸载不静默留 20GB**；本地零网络仍成立；**安装目录直接双击进程为正常 Medium integrity，可创建默认缓存/凭据**。网络栈 DLL/许可/隐私文本；**卸载缓存清理选项**；**缓存目录说明**；安装到 `%LOCALAPPDATA%\Programs\拉片软件`，正常用户直接双击即可在线，不依赖 DSH 低完整性工作区。

**必须做**：

1. **安装器**：每用户、非管理员、`%LOCALAPPDATA%\Programs\拉片软件`；安装/卸载全程 **0 网络**；安装后直接双击（不从 DSH 工作区、不额外加 Qt PATH）进程为 **Medium integrity**，能写默认 `%LOCALAPPDATA%\拉片软件\cache` 与 `%APPDATA%\拉片软件\credentials.dat`。
2. **网络栈/许可**：安装目录与 dist 导入表无 `QtNetwork`/TLS/`winhttp`/`wininet`/`urlmon`（`winhttp` 只动态加载）；Qt/FFmpeg LGPL 文本、源码获取说明、README 完整。
3. **隐私文本**：首次在线隐私说明 + README/安装包内隐私段落：何时联网（用户提交 BV 后 api.bilibili.com；扫码才 passport）、缓存到哪、Cookie 用途/DPAPI、本地模式 0 网络、无遥测/无自动更新/无崩溃上报、如何清缓存/凭据。
4. **卸载**：新增“清理缓存”选项（默认**不**清理/需明确确认）；卸载默认保留 `%APPDATA%\拉片软件` 的 settings/marks/在线记录、`Pictures\拉片软件` 截图、视频旁 sidecar；缓存清理只删 `%LOCALAPPDATA%\拉片软件\cache`，不删记录/截图/设置；不静默留 20GB。
5. **P12**：dist / stage / portable zip / setup 全部 ≤100MB；`scripts/package.ps1` 的 `out/package/package-report.txt` 记录当次 HEAD/体积/SHA。
6. 回归：`ctest` 35/35、M2 3/3 34/0（或明确引用 T8.14 并说明无产品变更）、key-test 77/0、本地 O9 0 TCP/UDP、P1/P2/P7 不退化；M1/M2 门/数字/OB/`lapian-cache-1` schema 不变。

**不做 / 不改**：

- 不新增 4K/60fps 验收；不写“60fps/4K 可用”；不把 T8.15 混入；不做 T8.6.1b。
- **不改** M1/M2 门、验收数字、OB 样本、DEC-2/DEC-5、`lapian-cache-1` schema/缓存键/索引格式。
- **不改** T8.13 加固语义（失败后不 Cancel 读层、4s 看门狗、API 4.5s、缓存损坏自动 Rebuild）、T8.12 档位、T8.11 增量索引、T8.10 DASH 双输入、T8.6.1a 异步步进。
- 不做遥测/自动更新/在线安装器/管理员安装/强制默认文件关联；卸载默认不删用户记录/截图/设置/marks。
- 不把 T8.14 的残余崩溃/卡住写成“已修复/无崩溃”；T8.16 不负责定位它（若复现先停下记录）。

## 3. 现状与硬事实

### 3.1 安装/打包现状（T7.6）

- `scripts/installer.iss`：Inno Setup 6、`PrivilegesRequired=lowest`、默认 `{localappdata}\Programs\拉片软件`；ProgID + 4 扩展名 `OpenWithProgids`（不设默认）；`[UninstallDelete]` 目前只 `dirifempty {app}`，**没有缓存清理选项**；卸载不碰 `%APPDATA%`/sidecar。
- `scripts/package.ps1`：组装 `dist` + `licenses\` + `README.txt` → stage → portable zip + setup；拒绝任一形态 >100MB；输出 `out/package/package-report.txt`。
- T7.6 实测：dist 32.63MB/25、stage 32.74MB/31、portable 13.65MB、setup 11.31MB、安装后 36.99MB；Inno 6.7.3（用户级安装，不入库）。
- T8.14 的 `dist` 是 **34.3MB/25**（含 T8.14 探针代码），T8.16 重新打包后按当次 `package-report.txt` 记录。
- T7.6 的历史限制：E: 盘路径下安装器曾被 Windows 安全策略拦（从 C: 本地路径运行正常）；本轮优先把 setup/日志放 C: 临时目录，不要误判为安装器缺陷。
- **T8.14 的 Medium 副本是手工整目录复制 + Qt PATH**（`lapian-t814`），**不能作为 T8.16 的安装证据**；T8.16 必须用真实 setup 安装到 `%LOCALAPPDATA%\Programs\拉片软件`，直接双击，不依赖 PATH/工作区。

### 3.2 默认路径/隐私硬事实

- 缓存根默认 `%LOCALAPPDATA%\拉片软件\cache`；在线记录 `%APPDATA%\拉片软件\online\<bvid>-<cid>.lapian.json`；截图 `%USERPROFILE%\Pictures\拉片软件\<bvid>\`（不可写回退 `%APPDATA%\拉片软件\online\snapshots`）；settings `%APPDATA%\拉片软件\settings.json`；凭据 `%APPDATA%\拉片软件\credentials.dat`（DPAPI）。
- 本地模式：启动/打开本地文件/设置/标记/截图/关闭 **0 TCP/UDP**；在线模块懒加载，不输入 BV 不初始化网络栈。
- 在线网络白名单：`api.bilibili.com`（view/playurl/nav）；扫码才 `passport.bilibili.com`；CDN 不带 Cookie。日志/错误/崩溃信息不得含 SESSDATA、Cookie、签名 URL。
- 首次在线隐私说明已在 `OnlineResolveDialog`；T8.16 要把它与 README/安装包文本对齐，不要只靠弹窗。
- T8.14 默认 AppData 已有 OB-1 DASH 缓存（OP5 种子：qn=80 视频 + audio-30280）。可用安装版 `--m2-op5 --m2-default-paths` 证明“默认缓存可打开、0 网络”；若需重新种，用 `--online-m2-test --m2-default-paths --m2-qn 80 --m2-play-seconds 20`，不要清用户记录/截图。

### 3.3 环境/执行坑

- 工作区 `dist\lapian.exe` / `build\release\bin\lapian.exe` 是 **Low integrity**；安装版才是 Medium。所有“默认 AppData/凭据/缓存”证据必须用安装版，不能用工作区 exe。
- Inno Setup：`ISCC.exe` 通常在 `%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe`；`package.ps1` 会找；找不到时写 blocked，不要伪造 setup。
- 构建前 `$env:TMP/$env:TEMP = <repo>\.deps\tmp`；重配用 `scripts\configure.ps1`；GUI 探针用 `Start-Process -RedirectStandardOutput/-RedirectStandardError`。
- 安装/卸载 0 网络：用 `netstat -ano`/`GetExtendedTcpTable` 轮询 + 安装日志 + 导入表；不要用“没看到 UI 联网”当证据。
- 卸载测试不要直接对真实默认缓存做破坏性删除；先备份/用合成小缓存/测试根；若不能安全验证，写“未真删真实缓存”，不要写“已清理 20GB”。
- T8.14 残余风险：1 次 teardown `0xC0000374`、1 次 L-1 seed 卡住，均未定位；T8.16 报告保留，不要写“无崩溃/已证明稳定”。

## 4. 建议执行路线

### 阶段 0：基线

```powershell
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure   # 35/35
git status --short                                               # clean
```

### 阶段 1：打包/许可/导入表/P12

1. 运行 `scripts/package.ps1`（需要 Inno Setup；产物 `out/package/`）；记录 dist/stage/zip/setup 体积、文件数、SHA、HEAD。
2. `scripts/check-imports.ps1` 检查 dist 与安装目录：无 QtNetwork/TLS/winhttp/wininet/urlmon；`Qt6Core → WS2_32` 已知允许。
3. 检查 `licenses/`（Qt/FFmpeg LGPL、源码获取说明）、`README.txt`、安装包内隐私段落；缺什么补什么（只文档/打包，不改播放器）。
4. P12：所有形态 ≤100MB；若 >100MB 先停，不要删许可/功能凑体积。

### 阶段 2：每用户安装 + Medium + 默认路径

1. 静默安装（从 C: 本地路径；日志放 `build\t8.16-install\`）：
   `Lapian-<ver>-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /LOG=...`
2. 验证：退出码 0、`User privileges: None`、`Administrative install mode: No`、HKCU；安装目录 `%LOCALAPPDATA%\Programs\拉片软件`；`icacls lapian.exe` 显示 **Medium Mandatory Level**。
3. **直接双击/直接启动安装版**（不设置 Qt PATH、不从工作区启动）：进程能写默认缓存/凭据。用 `--m2-op5 --m2-default-paths` 或等价探针验证默认缓存命中（ready、qn=80、0 playurl/0 HTTP）；用一次真实在线打开（OB-1，短播放/或已有缓存）验证 `%LOCALAPPDATA%\拉片软件\cache` 与 `%APPDATA%\拉片软件\credentials.dat` 可写。
4. 安装 0 TCP/UDP：轮询 netstat/GetExtendedTcpTable（40ms 级）+ 安装日志无 URL/更新检查 + 导入表。
5. 若安装器需要管理员/装到 Program Files/进程不是 Medium/默认路径不可写 → **blocked**，不要用“手工复制”冒充安装。

### 阶段 3：隐私/许可/缓存目录说明

1. README/安装包隐私文本必须写清：联网时机与主机、缓存位置/配额/清空、Cookie/DPAPI、本地模式 0 网络、无遥测/自动更新/崩溃上报、卸载保留/清理策略。
2. 首次在线隐私说明与 README 对齐；确认本地打开不弹在线隐私、不联网。
3. 设置页/状态栏的缓存目录/配额/清空/孤儿清理说明与 README 一致（T8.3.1 已有功能，不重做）。
4. 扫描日志/错误/安装日志：无 SESSDATA/Cookie/签名 URL/SENTINEL。

### 阶段 4：卸载 + 缓存清理选项

1. 在 `installer.iss` 增加卸载时的“清理缓存”选项（例如 `[Code] InitializeUninstall` 询问，默认否；或独立 `[UninstallRun]` + 明确确认）；**只删缓存**，不删在线记录/截图/设置/marks/sidecar。
2. 安全测试：先备份默认缓存或使用合成小缓存；测试两条路径：
   - 默认卸载（不清理）：程序目录消失，`%APPDATA%\拉片软件`/截图/sidecar 保留，缓存保留；
   - 选择清理：`%LOCALAPPDATA%\拉片软件\cache` 删除，记录/截图/设置/marks 保留。
3. 卸载 0 TCP/UDP；退出码 0；卸载日志无网络；**不静默留 20GB**（清理选项可见/可确认）。
4. 若无法安全测试真实 20GB 缓存，写“逻辑/小缓存验证，未真删真实 20GB”，不要写“已清理 20GB”。

### 阶段 5：回归

- `ctest` 35/35；M2 3/3 **34/0**（或明确引用 T8.14 + 说明无产品变更）；`--key-test` 77/0；本地 O9 0 TCP/UDP（安装版，本地文件打开→播放→步进→标记→截图→关闭）；P1/P2/P7 `scripts/bench-regression.ps1 -Suites startup,step1080`（热缓存口径，不写 F1-A1）。
- 默认缓存/凭据/设置跨安装/卸载保留；安装版首次在线仍正常。
- dist/安装目录导入表、P12、脱敏复查。

### 阶段 6：报告/交接/版本

- 更新 `docs/perf/T7.6-packaging.md`（T8.16 章节：安装/卸载/隐私/许可/P12/0 网络/Medium/缓存清理选项）；或新建 `docs/perf/v2-online-packaging.md` 并在 T7.6 文档链接。
- 新建 `docs/handoff-2026-10-24-t8.16.md`：结论、数字、证据路径、缺口、下一步（T8.15 未验证/T8.6.1b）。
- `tasks-v2.md` 升 **v0.26**：T8.16 行写结果/证据/判定；版本记录加一条；`AGENTS.md` 升 **v6.38**（T8.16 结论、安装/隐私/卸载、下一任务）。
- 一个可审查 commit；`git status` clean；**T8.16 非 MVP 门，不写“MVP 通过”**。

## 5. 停止条件

- 安装器要求管理员/装到 Program Files/进程不是 Medium/默认缓存或凭据不可写 → blocked，具体缺哪一项。
- 安装/卸载出现任何 TCP/UDP；P12 >100MB；安装包/安装目录出现 QtNetwork/TLS/winhttp/wininet/urlmon 导入 → blocked/fail。
- 卸载默认删除在线记录/截图/设置/marks/sidecar；或“清理缓存”会误删用户数据 → 停下改设计，不要静默删。
- 需要改 M1/M2 门、验收数字、OB、`lapian-cache-1` schema、T8.13 加固/T8.12 档位/T8.11 索引/T8.10 DASH/T8.6.1a 步进语义才能做 T8.16 → 停下问。
- 真实安装/Inno 工具不具备（无 ISCC、E: 路径被安全策略拦且无法从 C: 运行）→ 写 blocked/未验证，不伪造 setup/安装日志。
- T8.14 的 teardown 崩溃/L-1 seed 卡住复现 → 先停下记录，不带病做安装验收。

## 6. 现成接口/探针速查

| 用途 | 入口/输出 |
|---|---|
| 打包/体积/许可 | `scripts/package.ps1`；`out/package/package-report.txt`；`licenses/`、`README.txt` |
| 安装/卸载 | `scripts/installer.iss`；`Lapian-<ver>-setup.exe /VERYSILENT ... /LOG=...` |
| 导入表 | `scripts/check-imports.ps1`（dist/安装目录） |
| 默认缓存/0 网络 | `--m2-op5 --m2-default-paths`（安装版；qn=80、playurl=0/http=0） |
| 默认缓存种入 | `--online-m2-test --m2-default-paths --m2-qn 80 --m2-play-seconds 20`（安装版） |
| 本地零网络 | `--key-test --file media\测试.mp4` + netstat/GetExtendedTcpTable；`--memory-test` |
| 缓存管理 | 设置页/`CacheManager`；`--online-cache-manage-test`；`scripts/cache-manage-readonly.ps1` |
| 回归 | `ctest` 35/35；`scripts/bench-regression.ps1 -Suites startup,step1080`；T8.14 M2 3/3 |
| Integrity | `icacls <exe>`（Medium Mandatory Level）；进程从安装目录直接启动 |

## 7. 完成时的报告（按此清单写）

1. **T8.16 判定**：pass / blocked / fail；非 MVP 门，不写“MVP 通过”。
2. **安装**：setup 体积/SHA、静默安装退出码/日志、安装目录、Medium integrity、直接双击/无 PATH 启动、默认缓存/凭据可写、安装 0 TCP/UDP。
3. **P12/许可/导入表**：dist/stage/zip/setup 体积 ≤100MB；Qt/FFmpeg 许可/源码说明；无 QtNetwork/TLS/winhttp/wininet/urlmon。
4. **隐私**：README/首次在线说明/缓存目录说明；联网主机、缓存位置、Cookie/DPAPI、本地 0 网络、无遥测；日志脱敏扫描。
5. **卸载**：默认保留记录/截图/设置/marks/sidecar；清理缓存选项行为（默认否、确认、只删缓存）；卸载 0 TCP/UDP；不静默留 20GB（或明确未真删 20GB）。
6. **回归**：ctest 35/35、M2 3/3 34/0（或引用 T8.14）、key 77/0、O9 0 TCP/UDP、P1/P2/P7、dist/导入表。
7. **缺口/未验证**：真实 20GB 缓存清理、无签名/英文安装器 UI、T8.14 残余崩溃/卡住、T8.15（无大会员账号→未验证）、T8.6.1b。
8. 下一步：T8.15（大会员条件验收，无账号写未验证）/T8.6.1b（拖动预览）。
