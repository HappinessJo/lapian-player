# T8.16 打包 / 隐私 / 卸载（2026-10-24）

> 任务：`tasks-v2.md` T8.16（M2 后，**非 MVP 门**）。
> 结论：**pass（非 MVP 门）**。安装、卸载、P12、隐私/许可、安装 0 TCP/UDP、
> Medium 直接启动、默认缓存/凭据可写、卸载缓存清理选项均已实测。
> 本文是开发机证据，不是 MVP/Beta 发布验收；不写“MVP 通过”。
>
> 基线：T8.14（M2 pass）。本轮未改 M1/M2 门、OB 样本、验收数字、
> `lapian-cache-1` schema 或 T8.13/T8.12/T8.11/T8.10/T8.6.1a 语义。

## 1. 环境与产物

| 项 | 值 |
|---|---|
| 日期 | 2026-10-24 |
| 系统 | Windows 11 专业版 10.0.26200 x64（6C/12T、RTX 4060、31.6GB） |
| 用户权限 | 非管理员（安装日志 `User privileges: None`） |
| Inno Setup | 6.7.3（用户级安装，不入库） |
| 安装器 | `scripts/installer.iss`（`PrivilegesRequired=lowest`） |
| 打包 | `scripts/package.ps1`（本轮 `-SkipBuild`，dist 来自 T8.16 构建） |
| 源码 HEAD（打包时） | `b7b576b`（T8.16 工作树未提交） |

最终分发产物（`out/package/`，当次 `package-report.txt`）：

| 形态 | 文件数 | 体积 | SHA-256 |
|---|---:|---:|---|
| `dist` 程序负载 | 25 | 34.32 MB / 35,983,688 B | — |
| `stage`（dist + licenses + README + PRIVACY） | 33 | 34.43 MB / 36,099,182 B | — |
| `Lapian-0.1.0-portable.zip` | 33（压缩） | **14.33 MB / 15,027,779 B** | `6FF4C5DC9243E1CB9E52925CA9A53DCE874228E021C0681915FFBB336662A497` |
| `Lapian-0.1.0-setup.exe` | — | **11.79 MB / 12,359,132 B** | `B32A7FA171E0E3FA22EDAF307EAA483648F6C77C9F042F61D000626048CADF67` |
| 安装后程序目录（`{app}`） | 33 | 34.43 MB / 36,099,182 B | — |
| Inno 卸载器目录（`{localappdata}\拉片软件\uninstaller`） | 2 | 4.47 MB | — |

- **P12：通过**。dist/stage/zip/setup/安装目录均 ≤100MB；最大是 stage/安装目录
  34.43MB。
- **卸载器位置**：Inno 6.7.3 生成的 `unins000.exe` 自身静态导入
  `winhttp.dll`（Inno 卸载器引擎，安装/卸载均不执行网络请求）。为满足
  “安装目录导入表无 winhttp”，`installer.iss` 设置
  `UninstallFilesDir={localappdata}\拉片软件\uninstaller`，把卸载器移出
  `{app}`。因此安装目录只含应用负载，`check-imports.ps1` 通过；卸载器仍在
  `%LOCALAPPDATA%\拉片软件\uninstaller`，卸载后自动删除。**这一残余在 §10
  如实记录**：卸载器二进制仍有静态 winhttp 导入，但未调用/未联网。

## 2. 安装：每用户、Medium、直接启动

setup 从 **C: 本地临时目录**运行（避免 E: 工作区执行策略干扰，见 T7.6 §10）：

```powershell
Lapian-0.1.0-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /LOG=...\install.log
```

结果：

| 检查项 | 结果 |
|---|---|
| 退出码 | **0** |
| `User privileges` | **None** |
| `Administrative install mode` | **No** |
| `Install mode root key` | `HKEY_CURRENT_USER` |
| 安装目录 | `%LOCALAPPDATA%\Programs\拉片软件` |
| `Installation process succeeded` | 是 |
| `{app}` 内容 | 33 文件；`lapian.exe`、Qt/FFmpeg/VC DLL、plugins、`README.txt`、`PRIVACY.txt`、`licenses\`（6 文件） |
| `{app}\unins000.*` | **不存在**（在外部卸载器目录） |
| 快捷方式 | 开始菜单/桌面由安装器创建；不强制默认关联 |

**直接启动/Medium**：用 `ProcessStartInfo` 直接启动安装目录的 `lapian.exe`，
工作目录 `C:\Windows\Temp`，`PATH` 只有 `C:\Windows\System32;C:\Windows`
（不含 Qt/bin，也不是 DSH 工作区）：

| 项 | 结果 |
|---|---|
| 进程路径 | `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe` |
| 主窗口 | `拉片软件`（有 HWND） |
| 进程完整性 | **medium**（`OpenProcessToken` + `TokenIntegrityLevel` 读取） |
| 退出 | 正常关窗，退出码 0 |

这与工作区 `dist`/`build` 的 Low integrity 副本不同；本轮所有默认 AppData/
凭据证据均来自这个安装版。

## 3. 安装 0 TCP/UDP

监控方法：启动 setup 后，用 `netstat -ano` 以约 40ms 间隔轮询（实际循环含
`Get-CimInstance` 进程树刷新，采样间隔约 250–300ms）；按 setup 根进程及
子进程 PID 过滤 TCP/UDP 行；同时检查安装日志无 URL/更新检查，静态导入表
无网络栈。

| 运行 | 采样数 | TCP | UDP | setup 退出码 |
|---|---:|---:|---:|---:|
| 首次安装（旧 `UninstallFilesDir`） | 2 | 0 | 0 | 0 |
| 重装（改进监控） | 12 | 0 | 0 | 0 |
| 最终布局安装 | 13 | 0 | 0 | 0 |
| 最终布局重装 | 13 | 0 | 0 | 0 |

- **结论：安装过程未观测到 TCP/UDP**（12–13 次采样 × 3 个安装周期）。
- 局限：不是管理员级 ProcMon/防火墙抓包；轮询可能漏掉极短连接。结论是
  “未观测到 + 静态导入断言 + 安装日志无网络动作”，不是抓包级证明。
- 安装日志中没有 `http://`/`https://`/`api.bilibili.com`/update/download
  行；Inno 脚本无任何下载/更新逻辑。

## 4. 默认缓存与凭据可写

### 4.1 默认缓存命中（OP5）

安装版、默认 AppData、注入“一旦调用即失败”的 HTTP 客户端：

```powershell
lapian.exe --online-m2-test --m2-op5 --m2-default-paths `
  --bvid BV1BMnv6jE5S --cid 42915922022 --timeout-ms 120000
```

| 项 | 结果 |
|---|---|
| checks | **10 / 0 / pass** |
| ready | **120.7ms**（另一次 138.0ms） |
| 实际 qn | **80**（DASH） |
| playurl / DASH / quality / HTTP | **0 / 0 / 0 / 0** |
| 默认缓存根 | `%LOCALAPPDATA%\拉片软件\cache` |
| 网络监控 | 8 采样，TCP 0 / UDP 0 |

### 4.2 默认 AppData 写权限与缓存管理

安装版运行：

```powershell
lapian.exe --online-cache-manage-test --manage-default-paths
```

| 项 | 结果 |
|---|---|
| integrity | **medium** |
| checks | **75 / 0 / pass**（`ui=skip`、`maintenance=skip`） |
| 默认根 | `C:/Users/.../AppData/Local/拉片软件/cache` |
| 默认记录根 | `C:/Users/.../AppData/Roaming/拉片软件/online` |
| 默认缓存条目/占用 | 13 / 338,893,051 B（只读扫描） |
| 写入 | 在 `%LOCALAPPDATA%\拉片软件` 与 `%APPDATA%\拉片软件` 下创建唯一测试子目录，测试后清理；无残留 `*t8.3.1-test*` |

### 4.3 默认凭据写入（DPAPI）

为避免覆盖真实凭据，先备份 `%APPDATA%\拉片软件\credentials.dat`，再用安装版
UI（UIAutomation 打开 Ctrl+U 在线对话框）保存合成 SESSDATA
`T816_SENTINEL_9f3c1a7e`，验证后恢复原文件。

| 检查项 | 结果 |
|---|---|
| 保存按钮 | 安装版 UI `onlineCredentialSave` 可用 |
| 文件 | `%APPDATA%\拉片软件\credentials.dat` |
| magic / size | `LAPCRED1` / 292 B |
| 明文包含 sentinel | **否** |
| DPAPI 解密（entropy `lapian-credentials-v1`，CurrentUser） | **等于 sentinel** |
| SHA-256（测试写入） | `DEA4646CB7F6F4FBCE92DC6CA5244E926818BA2E5158DAB1FFC1D6D7979700BC` |
| 恢复原文件 | SHA-256 回到 `F406C6E93A1689324B0086135A8A319E2083065F26E37A93136505425742EC33` |

结论：安装版为 **Medium**，可直接创建默认缓存目录，可通过产品 UI 写入
`%APPDATA%\拉片软件\credentials.dat`（DPAPI，无明文）。凭据只保存 SESSDATA；
测试后已恢复真实凭据。

## 5. 隐私 / 许可 / 缓存目录说明

### 5.1 文本落点

- `packaging/PRIVACY.txt`：安装前 `InfoBeforeFile` 页面展示，并随 `{app}`
  安装；写入时机/主机、缓存位置/配额/清空、Cookie/DPAPI、本地 0 网络、
  无遥测/自动更新/崩溃上报、卸载保留/清理。
- `packaging/README.txt`：在线模式、数据位置、卸载 `/CLEARCACHE`、许可；
  随 `{app}` 和便携 zip 分发。
- `licenses\`（6 文件）：`LGPL-2.1.txt`、`LGPL-3.0.txt`、`GPL-3.0.txt`、
  `qrcodegen.txt`、`THIRD-PARTY-NOTICES.md`、`SOURCE-OFFER.md`。
  `THIRD-PARTY-NOTICES.md` 新增 WinHTTP 为 Windows 系统组件（不随包分发）。
- 首次在线隐私说明（`OnlineResolveDialog`）补上：
  - 在线缓存默认 `%LOCALAPPDATA%\拉片软件\cache`，可在设置中清空；
  - **无遥测、无自动更新、无崩溃上报**；
  - 保留原有 `api.bilibili.com`/`passport.bilibili.com`、DPAPI、本地不联网。
  本地打开不会弹在线隐私说明，也不会联网。

### 5.2 网络主机与本地模式

- 在线：`api.bilibili.com`（view/playurl/nav/quality）；扫码才
  `passport.bilibili.com`（generate/poll，不带 Cookie）；视频/音频来自
  playurl 返回的 B 站 CDN，CDN 不带 Cookie；网络由 Windows WinHTTP 发出，
  可能遵循系统代理。
- 本地：启动/打开本地文件/播放/步进/波形/标记/截图/设置/关闭 0 TCP/UDP；
  未输入 BV 并确认前不初始化在线网络栈。

### 5.3 脱敏复查

- 本轮 `build/t8.16-install/*` 的安装/卸载/OP5/key-test 日志扫描：
  `SESSDATA|deadline=|upsig=|sign=|token=|Cookie:` **0 命中**；合成 sentinel
  未出现在日志。
- 默认缓存 `meta.json` 扫描：无 SESSDATA/签名参数；仍是 `lapian-cache-1`。

## 6. 卸载：默认保留 / 清理缓存选项

### 6.1 默认卸载（无 `/CLEARCACHE`）

```powershell
unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=...
```

| 检查项 | 结果 |
|---|---|
| 退出码 | **0** |
| `{app}` | 已删除 |
| 外部卸载器目录 | 已删除 |
| 桌面/开始菜单快捷方式、ProgID、Applications、OpenWithProgids 值 | 已清理（不碰其它应用值） |
| 卸载注册表项 | 已删除 |
| 在线缓存 | **保留**：26 文件 / 338,893,051 B，前后文件数与字节一致 |
| `settings.json` | 保留；SHA-256 前后一致 |
| `credentials.dat` | 保留；SHA-256 前后一致 |
| 在线记录（marks） | 保留；SHA-256 前后一致 |
| `Pictures\拉片软件` 截图 sentinel | 保留；SHA-256 前后一致 |
| 视频旁 sidecar sentinel | 保留；SHA-256 前后一致 |
| 卸载网络 | 6–7 采样，TCP 0 / UDP 0 |
| 卸载日志 | `T8.16: silent uninstall without /CLEARCACHE; online cache preserved.` |

### 6.2 显式清理缓存（`/CLEARCACHE`）

为不破坏真实缓存，先把真实 `cache`（26 文件 / 338,893,051 B）移到同卷
`cache.t8.16-real2`，再在默认位置放合成缓存（2 文件 / 59 B），然后：

```powershell
unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLEARCACHE /LOG=...
```

| 检查项 | 结果 |
|---|---|
| 退出码 | **0** |
| 合成缓存 `%LOCALAPPDATA%\拉片软件\cache` | **已删除** |
| 真实缓存备份 | 26 文件 / 338,893,051 B，未动；测试后移回默认位置 |
| `settings.json` / `credentials.dat` / 在线记录 / 截图 / sidecar | SHA-256 全部与卸载前一致 |
| 卸载网络 | 6 采样，TCP 0 / UDP 0 |
| 卸载日志 | `/CLEARCACHE specified` + `removing default online cache: ...\cache`，无 cleanup warning |

### 6.3 交互式选项（默认“否”）

不走 `/SILENT` 启动卸载器时，`InitializeUninstall` 先弹确认框：

```text
是否同时删除在线缓存？

默认选“否”，保留缓存。
选“是”只删除：
C:\Users\...\AppData\Local\拉片软件\cache

不会删除在线记录、截图、设置、凭据、marks 或 sidecar。
```

按钮为“是(Y)/否(N)”，代码使用 `MB_YESNO or MB_DEFBUTTON2`（默认“否”）。
本轮用 UIAutomation 读取了完整文本，点击“否”，再在标准
“Are you sure you want to completely remove 拉片软件...”确认框点击“否”取消；
程序与缓存均保留。**交互式“是”未单独执行**（清理路径已由 `/CLEARCACHE`
实测）。

### 6.4 20GB 口径

**未真删真实 20GB**。本轮用合成小缓存验证删除路径，真实默认缓存
（338.9MB）用移动/恢复方式保护，未做破坏性删除。因此只写“缓存清理逻辑与
小缓存实测通过”，不写“已清理 20GB”。卸载器默认不清理，用户必须明确
选择“是”或传 `/CLEARCACHE`，不存在静默留下 20GB 的行为。

## 7. 网络栈 / 导入表

| 范围 | 结果 |
|---|---|
| `dist`（25 文件） | `build-release.ps1` 的 `check-imports.ps1` 通过；无 QtNetwork/TLS/winhttp/wininet/urlmon；仅 `Qt6Core.dll -> WS2_32`（Qt 固定链接，已知接受） |
| `stage`（33 文件） | 打包时 `check-imports.ps1` 通过（20 个 exe/dll） |
| `Lapian-0.1.0-setup.exe` | `check-imports.ps1 -DistDir out\package` 通过（1 个 exe；无网络导入） |
| 安装目录 `{app}`（33 文件） | `check-imports.ps1` 通过（20 个 exe/dll）；无 QtNetwork/TLS/winhttp/wininet/urlmon；仅 Qt6Core -> WS2_32 |
| Inno 卸载器 | 在 `{app}` 外；`dumpbin` 仍显示静态 `winhttp.dll` 导入（Inno 6.7.3 引擎），安装/卸载实测 0 TCP/UDP；见 §10 |

- 应用使用 WinHTTP 的方式是运行时动态加载（T8.2/T8.14 已证）；发布包不含
  `Qt6Network.dll`、`tls\`、`networkinformation\`。
- setup 本身是 Inno 引导器，导入表无 winhttp；其内嵌的卸载器在安装后才释放
  到外部目录。

## 8. 回归

| 项 | 结果 |
|---|---|
| `ctest` | **35/35**（T8.16 构建，最终复跑见 `build/t8.16-ctest-final.log`） |
| M2 3/3 | **引用 T8.14**：3/3 34/0、qn=80、fps 30.000/29.412/25.000、ready 330.5/365.6/353.6ms；本轮只改隐私 UI 文案，不改在线会话/网络/缓存语义，未重跑 3/3 |
| OP5（安装版默认 AppData） | 10/0，ready 120.7ms，qn=80，0 playurl/DASH/quality/HTTP；网络 0 TCP/UDP |
| 本地 O9 | 安装版 `--key-test --file media\测试.mp4`：**77 检查 / 0 失败**；65 次采样，TCP 0 / UDP 0 |
| P1/P2/P7 | `bench-regression.ps1 -Suites startup,step1080`：P1 p50/p95 **276.96/308.81ms**、P2 **338.35/361ms**（20/20）；P7 back p50/p95/max **0.2/1.63/51.64ms**、fwd **0.23/0.34/10.43ms**；基线 4 项 0 超差，`exit 0`（热缓存口径，不是 F1-A1） |
| 导入表 | dist/stage/setup/安装目录通过（见 §7） |
| 用户数据 | 卸载前后 settings/credentials/在线记录/截图/sidecar SHA-256 一致；测试改写的 `meta.last_access_at`/在线记录 `updated_at` 已恢复原值（记录 SHA-256 回到基线 `79A53A8F...`） |

## 9. 变更清单

- `scripts/installer.iss`：`InfoBeforeFile` 隐私页；`[Code]`
  `InitializeUninstall`/`CurUninstallStepChanged` 缓存清理确认；默认否；
  `/CLEARCACHE` 显式清理；`UninstallFilesDir` 移出 `{app}`。
- `packaging/PRIVACY.txt`（新增）；`packaging/README.txt`（在线/隐私/卸载）。
- `scripts/package.ps1`：要求并复制 `PRIVACY.txt`；BOM 校验；报告记录隐私/
  卸载口径。
- `licenses/THIRD-PARTY-NOTICES.md`：WinHTTP 系统组件说明。
- `src/app/OnlineResolveDialog.cpp`：首次在线隐私说明补缓存位置/无遥测/
  无自动更新/无崩溃上报。
- 未改：M1/M2 门、OB 样本、验收数字、`lapian-cache-1` schema、T8.13
  加固、T8.12 档位、T8.11 索引、T8.10 DASH、T8.6.1a 步进。

## 10. 缺口 / 未验证

1. **未真删真实 20GB 缓存**：只用合成小缓存验证清理路径；真实默认缓存
   （338.9MB）保护后恢复。
2. **Inno 卸载器静态 WINHTTP 导入**：`unins000.exe` 在 `{app}` 外，安装/
   卸载 0 TCP/UDP；但二进制导入表仍含 winhttp。若验收把卸载器也算入
   “安装目录网络栈”，这一条应视为残余/不满足，需换卸载器方案。
3. **安装器 UI 英文、未签名、无自定义图标**（沿用 T7.6）；杀软误报未验证。
4. **未用 Explorer 真双击**：用直接 `ProcessStartInfo` 从 C: 工作目录启动，
   未走 Explorer 的用户双击路径；不是 F1-A1 正式验收。
5. **未测**：真实 20GB 配额清理、安装时 App 正在运行、覆盖升级跨版本、
   无重启分支、Windows 10 21H2 实机、干净机器冷缓存。
6. **T8.14 残余风险保留**：1 次 teardown `0xC0000374`、1 次 L-1 seed 卡住
   未定位/未复现；本轮不写“无崩溃/已证明稳定”。
7. **T8.15 未做**（无大会员账号 → 未验证）；**T8.6.1b 未做**。
8. 安装/卸载 0 网络是 netstat 轮询 + 静态导入 + 日志，不是抓包级证明。

## 11. 复现命令（PowerShell）

```powershell
# 构建 + ctest + dist（构建机会重配 CMake）
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1

# 分发打包（Inno 6.3+；本机 ISCC 在 %LOCALAPPDATA%\Programs\Inno Setup 6）
powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -SkipBuild

# 安装（务必先把 setup 复制到 C: 本地目录）
Copy-Item out\package\Lapian-0.1.0-setup.exe "$env:TEMP\t8.16\" -Force
& "$env:TEMP\t8.16\Lapian-0.1.0-setup.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /LOG="$env:TEMP\t8.16\install.log"

# 安装版默认缓存命中（不带 Qt PATH；工作目录可设为 C:\Windows\Temp）
& "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe" --online-m2-test --m2-op5 --m2-default-paths `
  --bvid BV1BMnv6jE5S --cid 42915922022 --timeout-ms 120000

# 默认卸载（保留缓存/记录/截图/设置）
& "$env:LOCALAPPDATA\拉片软件\uninstaller\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART

# 明确清理默认缓存（会删 %LOCALAPPDATA%\拉片软件\cache）
& "$env:LOCALAPPDATA\拉片软件\uninstaller\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLEARCACHE

# 回归
$qt=(Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH="$qt\bin;$env:PATH"
ctest --test-dir build\release -C Release --output-on-failure
powershell -ExecutionPolicy Bypass -File scripts\bench-regression.ps1 -Suites startup,step1080
```
