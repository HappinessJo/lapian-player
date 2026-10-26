# 拉片软件 0.1.0 发布收口 — 打开预热 + 缓存命中拖动软解 + 时间轴 auto-pan（2026-10-26）

> **这是内部发布收口记录，不是 T7.7/P14 验收，也不是“发布就绪”。**
> 本轮只做 **clean Release → dist/setup/zip → 每用户静默安装 → 安装版冒烟 → 文档**；
> 未改 `src/`/`tests/` 产品代码、未改任何门/阈值/schema、未改安装器形态、未升级 Qt/FFmpeg/CMake。
>
> **结论先说**：clean 构建、打包、每用户静默安装、安装版本地/A/B/时间轴冒烟**核心项全绿**
> （banner 74/0、key 77/0、dpi 4 档全空、A 27/0 ×4、B 38/0+38/0+42/0、T8.6.1b 35/0、timeline 188/0）。
> **仍有残差/缺口**：1080P `--drag-start-frame 150` 反向 7/30、720P 2s/60 反向 25/30（`out_of_parity_gate=1`）、
> 长 GOP P6 `out_of_premise`、auto-pan 只有 widget 级证据、cache-hit P7/P9/音频 A/V 无直接探针。
> **不写“发布就绪/T7.7/P14 通过”，不写“打开不再卡”，不写“真实鼠标 auto-pan 已验证”。**

---

## 1. 一句话结论

| 项 | 结果 |
|---|---|
| 代码 HEAD | `6daa341`（`4ae315a` 打开预热 + `c04a180` 缓存命中拖动软解 + `6daa341` 时间轴 auto-pan；本轮只加 docs 提交） |
| clean Release | `scripts\build-release.ps1 -Clean -Package`，**282.2s**（脚本自报 265.8s；configure 5.2s；脚本内 ctest 65.82s） |
| 单元测试 | **ctest 38/38**（脚本内 65.82s；手动复跑 **38/38 / 65.29s**，wall 65.39s，exit 0） |
| exe | **3,587,584 B**，SHA256 `FEF8CE1EB082556811DD18C8A8C5DC1E073E2E1C13A25FF6C987425CDB2B9C8C` |
| dist | **26 文件 / 36,865,352 B / 35.16 MB**（含 `SoundTouch.dll`） |
| stage | **35 文件 / 37,006,318 B / 35.29 MB**（含 licenses/README/PRIVACY） |
| setup | **12,609,053 B / 12.02 MB**，SHA256 `D180A3BDD485DBD0F055FEAF5734BA263B958D29DC015EFC9E1EF93C1E262A7A` |
| 便携 zip | **15,398,168 B / 14.68 MB**，SHA256 `DE6113C5A15CCA0508D40E73A526C93CEDD729AC7F68901FD7369BB82BA8BF8D` |
| 安装（每用户） | 静默安装 **exit 0 / 2.1s**；`User privileges: None`、`Administrative install mode: No`、HKCU、`Need to restart Windows? No` |
| 安装版 exe | **3,587,584 B / `FEF8CE1E…`，与 build/dist/stage 同 hash** |
| 安装目录 | **37 文件 / 44,008,942 B**（35 负载 + 用户留的 `lapian-3d.exe`/`lapian-3c.exe`；旧 exe 3,578,880 → 新 3,587,584，+8,704 B 恰好对上） |
| settings | `%APPDATA%\拉片软件\settings.json` 安装前/后/冒烟后同为 **137 B / `0D69C495…`** |
| 本地冒烟 | banner **74/0**、key **77/0**、dpi 4 档各 **37/0**（空态各 40/0）、drag P4 **94.6fps** / P6 p95 **17.1ms**、startup **864.7/876.7/1142.6ms**、timeline **188/0** |
| 打开预热 | `prewarm=143/147ms first=1`、`stage_show_ms=0`、`wait_ready=91ms`、`online_start_ui=157ms`；二次打开/切档 `prewarm=0 first=0` |
| A 缓存命中拖动 | **27/0**（720P 1s/60 ×2、1080P 1s/60 ×2、720P start145、1080P start150、720P 2s/60）；`hw_requested=0/hw_active=0`、`playurl=dash=0` |
| B 缓存命中清晰度 | 720P **38/0**、1080P **38/0**（112 真下载）、普通切档 **42/0**、offline **45/0**、M2 op5 **10/0**、fault **92/0** |
| 对比 | T8.6.1b **35/0**（`forward_ui_gap=160ms`；历史基线 35/1 214–349ms，**只记录**） |
| 总体判定 | **分发包已生成、安装可用、安装版核心冒烟通过；不写“发布就绪/T7.7/P14 通过”** |

---

## 2. 交付物与校验

| 形态 | 文件 | 体积 | SHA-256 |
|---|---|---:|---|
| 程序目录 | `dist\` | 26 文件 / 35.16 MB | exe：`FEF8CE1EB082556811DD18C8A8C5DC1E073E2E1C13A25FF6C987425CDB2B9C8C` |
| 便携 zip | `out\package\Lapian-0.1.0-portable.zip` | 14.68 MB | `DE6113C5A15CCA0508D40E73A526C93CEDD729AC7F68901FD7369BB82BA8BF8D` |
| 每用户安装包 | `out\package\Lapian-0.1.0-setup.exe` | 12.02 MB | `D180A3BDD485DBD0F055FEAF5734BA263B958D29DC015EFC9E1EF93C1E262A7A` |
| 打包报告 | `out\package\package-report.txt` | — | HEAD `6daa341`；dist/stage/zip/setup 体积与 hash |

- **build exe == dist exe == stage exe == 安装版 exe**：均 `FEF8CE1E…`，3,587,584 B。
- 上一版（缓存命中修复）exe 为 `DDF2E8E9…`（3,578,880 B）、setup `DF76A251…`、zip `DAC216AF…`；
  本版 hash/体积已变（代码增量 + 链接时间戳），以本文为准。
- `scripts\check-imports.ps1`（构建脚本内跑）：**21 个文件通过，无 QtNetwork/TLS/winhttp/wininet/urlmon**；
  仅已知并接受的 `Qt6Core.dll -> WS2_32`（winsock 初始化）。
- `dist` 是程序负载（26 文件，按 T7.6 设计**不含** licenses/README/PRIVACY）；
  `out\package\stage` 与 zip 根 `Lapian-0.1.0\` 含 **35 文件**：
  `licenses\{GPL-3.0,LGPL-2.1,LGPL-3.0,qrcodegen,SoundTouch-COPYING,SOURCE-OFFER,THIRD-PARTY-NOTICES}`、
  `README.txt`、`PRIVACY.txt`、`SoundTouch.dll`；安装目录同样含这些文件（已逐一核对存在）。
- 体积：setup/zip/dist 均 ≤100MB（P12，余量 64.84 MB）。
- 校验命令：

```powershell
Get-FileHash dist\lapian.exe -Algorithm SHA256
Get-FileHash "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe" -Algorithm SHA256
Get-FileHash out\package\Lapian-0.1.0-setup.exe -Algorithm SHA256
Get-FileHash out\package\Lapian-0.1.0-portable.zip -Algorithm SHA256
```

---

## 3. clean Release 构建

```powershell
cd 'E:\拉片软件开发'
$logDir = 'build\release-close-c7'; New-Item -ItemType Directory -Force -Path $logDir | Out-Null
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-release.ps1 -Clean -Package *> "$logDir\build-clean.log"
```

- `-Clean` 删除并全新 configure `build\release`（**未触发**本机已知的 CMake 重配 `0xC0000409`；
  `build\c2-*`/`build\pm-b-fresh` 等夹具未删）。
- 墙钟 **282.2s**（外层 `Stopwatch`）；脚本自报 `Release 构建与打包完成，用时 265.8s`；
  configure 5.2s + generate 0.5s。
- 脚本内 ctest：**38/38**，`Total Test time (real) = 65.82 sec`。
- 手动复跑（按任务环境）：

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$env:TMP='E:\拉片软件开发\.deps\tmp'; $env:TEMP=$env:TMP
ctest --test-dir build\release -C Release --output-on-failure
```

结果：**38/38**，ctest 计时 **65.29s**，wall **65.39s**，exit 0（`build\release-close-c7\ctest-manual.log`）。
- 新 exe：`build\release\bin\lapian.exe`，3,587,584 B，`FEF8CE1E…`；dist/stage 副本同 hash。
- 构建日志：`build\release-close-c7\build-clean.log`；手动 ctest：`build\release-close-c7\ctest-manual.log`。
- ctest 未在仓库根留下 `credentials.dat/pictures/videos`（本轮 TMP/TEMP 已指到 `.deps\tmp`；
  提交前 `git status` 干净，只应有本轮 docs/AGENTS 改动）。

---

## 4. 安装验证（每用户、非管理员、Medium）

- setup 先复制到 C: 本地：`%TEMP%\lapian-release-2026-10-26-cachehit2\Lapian-0.1.0-setup.exe`
  （避免 E: 盘 SmartScreen；未从 E: 直接运行）。
- 静默安装参数（`/NOCLOSEAPPLICATIONS` 避免触碰用户正在运行的 E: 免安装旧版）：

```powershell
& "$env:TEMP\lapian-release-2026-10-26-cachehit2\Lapian-0.1.0-setup.exe" `
  /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOCLOSEAPPLICATIONS `
  /LOG="$env:TEMP\lapian-release-2026-10-26-cachehit2\install.log"
```

| 核对项 | 结果 |
|---|---|
| 退出码 / 用时 | **0 / 2.1s**（`Start-Process -Wait -PassThru` 实测；日志窗口 22:58:57.925→22:58:59.159，约 1.23s 在 Setup 内） |
| 安装日志 | `User privileges: None`、`Administrative install mode: No`、`Install mode root key: HKEY_CURRENT_USER`、`64-bit install mode: Yes`、`Installation process succeeded`、`Need to restart Windows? No` |
| 安装目录 | `%LOCALAPPDATA%\Programs\拉片软件`；**37 文件 / 44,008,942 B**（35 负载 + 用户留的 `lapian-3d.exe`/`lapian-3c.exe`） |
| 安装版 exe | 3,587,584 B，`FEF8CE1E…`，**与 dist 相同** |
| Start 菜单 | 组 `…\Start Menu\Programs\拉片软件\`：`拉片软件.lnk` → `…\Programs\拉片软件\lapian.exe`；`第三方许可与源码说明.lnk` → `…\Programs\拉片软件\licenses`；`卸载 拉片软件.lnk` → `%LOCALAPPDATA%\拉片软件\uninstaller\unins000.exe` |
| 桌面快捷方式 | `%USERPROFILE%\Desktop\拉片软件.lnk` → `…\Programs\拉片软件\lapian.exe` |
| 用户备份 | `lapian-3d.exe`（`99084199…`，3,495,424 B）与 `lapian-3c.exe`（`5863DD0A…`，3,507,200 B）hash 与安装前一致，未被删除 |
| settings | 安装前/后/冒烟后同为 **137 B / `0D69C4957838F7DF5228EC9D2BBB0F617B2F4FA0CC2E4585D0AA9EC2010A3D1C`** |
| 许可/说明 | 安装目录含 `README.txt`、`PRIVACY.txt`、`licenses\SoundTouch-COPYING.txt`、`THIRD-PARTY-NOTICES.md`、`licenses\GPL-3.0.txt`、`SoundTouch.dll` |
| 卸载器 | `%LOCALAPPDATA%\拉片软件\uninstaller\unins000.exe`（4,452,610 B，在 `{app}` 之外） |

- 安装前 C: 安装版未运行；用户 E: 免安装版 **PID 34276 仍在运行**
  （`E:\开源软件尝试\自制拉片软件\免安装版\2\Lapian-0.1.0\lapian.exe`，exe `DDF2E8E9…`），
  另一份 E: 副本 `…\免安装版\Lapian-0.1.0\lapian.exe`（`E19FC4B3…`）未运行；
  **E: 目录/进程本轮未覆盖、未删除、未强杀**（安装前后 hash/mtime 未变）。
- 安装目录原有 `lapian.exe`（`DDF2E8E9…`）被覆盖为新 hash；`lapian-3d.exe`/`lapian-3c.exe` 原样保留。
- **坑**：PowerShell 对 GUI 子系统 exe 用 `&` **不会等待**、也拿不到 `$LASTEXITCODE`
  （首次用 `&` 调用时 setup 正常完成但外层 0.16s 就返回）；测时长/退出码必须
  `Start-Process -Wait -PassThru`。

---

## 5. 安装版冒烟

被测程序一律为 `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`（Medium 完整性），从仓库根运行；
本地测试写 `build\release-close-c7\settings`，在线测试写
`build\release-close-c7\online\<case>\{cache,records,pics,settings}`，不碰用户默认缓存/记录/截图/凭据。
日志在 `build\release-close-c7\logs\`。缓存夹具为 `build\c2-720p`/`build\pm-b-fresh` 的**只读复制**
（已复制进各隔离 `cache\`；`pm-b-fresh` 的 `112-7` 只有 `data.part`，因此 1080P 切 112 会真实下载）。

### 5.1 本地（安装版）

| 项 | 结果 |
|---|---|
| `--banner-test --file media\测试.mp4` | **74/0**（`banner_summary checks=74 fails=0 dpr=1.00 info_h=22`，10.1s） |
| `--key-test --file media\测试.mp4` | **77/0**（`key_check ok=1` ×77，`ok=0` ×0；本轮无焦点 flake，11.1s） |
| `--dpi-test` ×4（`QT_SCALE_FACTOR` 1 / 1.25 / 1.5 / 2 + `--dpi-expect-scale`） | 每档 **37 项 dpi_check 全 ok=1**、无 `ok=0`/FAIL；`dpi_state` 的 `split=tl=340/198`、`tier=regular/compact/dense` 随档位变化；5.0–5.1s / 档 |
| `--dpi-test --empty` ×4 | 每档 **40 项全过**；`dpi_empty mode=未打开 collapsed=1 banner_h=0` |
| `--drag-test --drag-samples 20` | P4 **94.6fps**（190 presents/2.01s）、P6 p95 **17.1ms**、P7 back **0.1** / fwd **9.2**、P8 **13.0fps / 26 帧**、P9 p95 **37.7ms**；全部 pass |
| `--startup-test --file` | window_shown **864.67ms**、ui_ready **876.65ms**、first_present **1142.59ms**；`d3d_init_at_startup=0` |
| `--startup-test`（无文件） | `d3d_init_at_startup=0`，无 `online_prewarm_stage_ms` 行（空态不预热） |
| `lapian_timeline_test.exe`（`build\release\Release\`，PATH 加 Qt） | **188 `[ OK ]` / 0 FAIL**，exit 0 |

- drag 与 C6 基线（P4 94.6 / P6 21.0 / P7 0.0/10.4 / P8 12.9 / P9 34.4）同量级，不改阈值。
- dpi 截图落在 `build\t6.4-dpi-c7-*.png`（构建目录，不入库）；`media\测试.mp4.lapian.json` 与
  `media\snapshots` 被 dpi 用例写过（既有行为、已 gitignore，未入库）。

### 5.2 打开预热（安装版，`LAPIAN_ONLINE_LOG=1`）

| 用例 | 关键行 |
|---|---|
| 720P 缓存命中打开（`BV1cW411b7Yh`/`28925848`，`--quality-cache-hit-menu`） | `online_prewarm_stage_ms=143 first=1 stage=video`；`T8.9 online_ready stage_show_ms=0 prewarm_stage_ms=143`；`online_start_ui ms=157 active=1`；`wait_ready total_ms=91 ready=1` |
| 第二次/切档（同一次运行） | `online_prewarm_stage_ms=0 first=0`（切换 64→32、32→64 各一次，`online_begin_done` 7/66ms） |
| 1080P 缓存命中打开（`BV1BMnv6jE5S`/`42915922022`） | `online_prewarm_stage_ms=147 first=1`；`stage_show_ms=0`；`online_start_ui=157`；`wait_ready total_ms=91`；随后切档 `prewarm=0 first=0` |
| 空态/本地不预热 | `--startup-test`（无文件）、`--dpi-test --empty`：`d3d_init_at_startup=0`，无 prewarm 行 |
| 失败路径（坏缓存根 `Z:\lapian-bad-cache-root`） | `--online-offline-test`：`在线缓存目录不可用：无法创建缓存目录：Z:/lapian-bad-cache-root`，11 项中 6 项 fail，**exit 2**、无崩溃；`--online-drag-preview-test --drag-cache-hit` 同根 exit 2（`缓存命中状态不符：expect=1 actual=0`） |
| `--online-fault-test --fault-set t8.13-core` | **92/0**（10/10 场景）、`ui_gap=155ms over_200=0`、`unverified=1`（真实休眠）、19.1s |

- **表述边界**：145ms 是**移到加载中（点确认/解析阶段）**，不是消失；click→ready 总量基本不变
  （C4 中位 243ms，+3ms，max +13ms）。**不得写“打开不再卡”**。
- A 拖动探针会自己预热画面页（`online_drag_preview_warmup stage_ms=165`），所以 A 日志里
  `online_prewarm_stage_ms=0 first=0` 属预期；**打开预热的证据看本节 B/quality-switch 日志**。

### 5.3 A：缓存命中拖动（安装版）

| 场景 | checks | 正向 fps_active | P6（松手+落地） | 反向 presented | 网络 |
|---|---:|---:|---|---|---|
| 720P 1s/60 第 1 次 | **27/0** | 60.11（presented 56） | **53ms**（`out_of_premise`，K 距 2550ms） | **28/30 = 0.933**，27.92fps | `playurl=0 dash=0` |
| 720P 1s/60 第 2 次 | **27/0** | 60.04（presented 56） | **52ms**（`out_of_premise`） | **27/30 = 0.900**，27.00fps | 同上 |
| 1080P 1s/60 第 1 次 | **27/0** | 63.39（presented 58） | **85ms**（`out_of_premise`，2500ms） | **27/30 = 0.900**，26.97fps | 同上 |
| 1080P 1s/60 第 2 次 | **27/0** | 63.39（presented 58） | **84ms**（`out_of_premise`） | **28/30 = 0.933**，27.94fps | 同上 |
| **720P 保证档 `--drag-start-frame 145`** | **27/0** | 72.05（presented 66） | **37ms**（`premise=le_2000ms`，55 帧/1870ms） | 21/30 = 0.700，20.98fps（`out_of_parity_gate=1`） | 同上 |
| **1080P 保证档 `--drag-start-frame 150`** | **27/0** | 100.33（presented 92） | **73ms**（`premise=le_2000ms`，60 帧/2000ms） | **7/30 = 0.233**，6.99fps（`out_of_parity_gate=1`） | 同上 |
| 720P 2s/60 | **27/0** | 36.23（presented 71） | **90ms**（`out_of_premise`，4522ms） | **25/30 = 0.833**，12.50fps（`out_of_parity_gate=1`） | 同上 |

- 所有 A 场景：`cache_hit=1`、`exact_frame/exact_pts=1`、`stale=0`、`hw_requested=0/hw_active=0`、
  `fallback=software_requested`、`configured_exists=1`（打开真实 `data.mp4`）、`session_exists=0`（展示名不落盘）、
  `cache_hit_backward_presented_ratio_ge_70pct` 在 A 默认窗口 ok=1。
- **残差（不写通过）**：1080P `start150` 反向 **7/30（C4 8/30）**、720P 2s/60 反向 **25/30（C4 26/30）**；
  两者 `out_of_parity_gate=1` + `cache_hit_backward_ratio_note`，非硬门；长 GOP 2s/60 的 P6 是
  `out_of_premise`，**不得写“1080P 缓存命中 P6 保证档外通过”**。

### 5.4 B：缓存命中清晰度菜单/切档 与相关套件（安装版）

| 用例 | 结果 | 关键数字 |
|---|---|---|
| 720P `--quality-cache-hit-menu` | **38/0** | 打开 `cache_hit=1 / playurl=0 / dash=0 / quality_requests=0`、qn=64；切 32 实际变 qn/codecid；切回 64 `cache_hit=1`；3s |
| 1080P `--quality-cache-hit-menu` | **38/0** | 80 打开 `cache_hit=1`、**0 网络**；catalog **1 次（HTTP 200，143ms）、5 档含 112 / enabled_other=4**；切 112 **真实 CDN 下载**（`published=4,964,306/48,462,784`）、qn=112/codecid=7、`cache_hit=0`、PTS **1,000,000 不变**、marks=1；切回 80 `cache_hit=1 / playurl=0 / dash=0`；3s |
| 普通切档 `--quality-switch-high 112 --quality-switch-back 80`（新鲜缓存） | **42/0** | 80 → 112（真下载）→ 80 缓存命中；`switch_back_zero_dash_requests/zero_playurl_requests`、PTS mapped、marks/record/snapshot 保留；9.1s |
| `--online-offline-test`（规范 3.2MB `build\test-media\faststart-720p.mp4`） | **45/0** | 工作区缓存命中 0 网络 |
| `--online-m2-test --m2-op5`（1080P 80 缓存命中） | **10/0** | `ready_ms=250.4`、qn=80、`playurl=0 / dash_requests=0 / quality_requests=0 / http=0` |
| `--online-fault-test --fault-set t8.13-core` | **92/0** | 10/10 场景、`ui_gap=155ms`、`unverified=1` |
| T8.6.1b 首次在线拖动（**不带** `--drag-cache-hit`，1s/60） | **35/0** | `forward_ui_gap max_gap_ms=160 over=0`、`ui_gap max=160 over_200=0`；**历史基线 35/1（214–349ms），只记录、不宣称修好** |

- 1080P 的 112 只验证到“实际取流并落地”（随后切回取消），**未等 112 完整下载完成**；
  完整 112 下载由普通切档 42/0 覆盖。
- 720P/1080P 的 B 与 §5.2 打开预热是**同一次运行**（`--quality-cache-hit-menu`），两套断言都取自该日志。

### 5.5 时间轴 auto-pan（安装版，widget 级）

| 项 | 结果 |
|---|---|
| `lapian_timeline_test.exe` | **188 OK / 0 fail**（含右拖 `scroll 8000→8716.5 / current 1025→1139`、左拖 `8000→7736 / 1025→967`、no-backjump 1053→1105 / 996→944、**fit `scroll 0→0 / view 0..1999 / active=0`**、内容末尾 `15600→15600 / current 1999` 不循环、timer 停止路径） |
| `--key-test` 时间轴点击路径 | 77/0（在 §5.1 的 77 项内） |

- **安装版没有 auto-pan 的 CLI 集成探针**：只有 widget 级（合成 `QMouseEvent` + `AutoPanStepForTest`）。
- **不写“真实鼠标已验证”“MainWindow 集成通过”**；真实窗口/物理鼠标/多 DPI 拖动未验。

---

## 6. 残余 / 未验证 / 风险

1. **1080P `--drag-start-frame 150` 反向 7/30（6.99fps）** 与 **720P 2s/60 反向 25/30（12.50fps）**：
   既有软解路径限制（跨拖动缓存约 8 帧、start150 每次从 K 重解约 60 帧），`out_of_parity_gate=1`，
   非硬门；**不得写成通过**。旧硬解基线同样 <70%（13/30、0.43）。
2. **长 GOP P6 前提**：默认 1s/60（K 距 2500–2550ms）与 2s/60（4522ms）的 P6 是 `out_of_premise`，
   只记录；保证档内 **37ms（720P start145）/ 73ms（1080P start150）**。
3. **auto-pan 只有 widget 级证据**：无 MainWindow 集成/真实窗口/物理鼠标/多 DPI 拖拽探针。
4. **cache-hit P7/P9 无直接探针**、**cache-hit 音频只验证打开/就绪**（48k、`pitch_preserve=1`），
   无 underrun / A-V 偏差；**cached→cached 经真实菜单回切**无直接探针。
5. **prewarm 失败恢复空态无专门探针**：只有坏缓存根下“明确错误/exit 2/无崩溃”的间接证据，
   没有断言“画面页回到空态卡片”。
6. **T8.6.1b**：本轮 35/0（160ms），但历史基线是 35/1（214–349ms）；既有打开阶段 UI gap，
   **不写“已修复”**。
7. **fault `unverified=1`（真实 Windows 休眠 S3–S4）**、**无符号化崩溃 dump**（沿用 P0 记录）。
8. **本轮未跑**：M1/M2 全量、quality 1080P 目录矩阵、longpress、QR、online parity、local parity、
   scrub-space、fullscreen/loop、mark/note/snapshot、cache-switch 等门禁；这些沿用 C6 与其他轮次结果
   （ctest/key/banner/dpi/drag/startup/timeline/online A/B 已复跑）。**不要把本记录当成完整门禁清单**。
9. **无代码签名、安装器 UI 英文、无自定义图标**；未在干净机/Win10 21H2 复验；未做 Explorer 真双击与冷缓存 F1-A1。
10. **用户体感未收集**：本轮没有新的用户实机反馈；不写“验收通过”。

---

## 7. 复现 / 校验命令

```powershell
cd 'E:\拉片软件开发'
$exe = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"
$media = 'media\测试.mp4'
$root = 'build\release-close-c7'

# 本地（GUI 探针必须 Start-Process -Wait；PowerShell 对 GUI exe 用 & 不会等待）
Start-Process $exe -Wait -RedirectStandardOutput "$root\logs\local\banner.out.log" -RedirectStandardError "$root\logs\local\banner.err.log" `
  -ArgumentList '--banner-test','--file',$media,'--banner-tag','c7','--settings-path',"$root\settings"
Start-Process $exe -Wait -ArgumentList '--key-test','--file',$media,'--settings-path',"$root\settings"
Start-Process $exe -Wait -ArgumentList '--drag-test','--file',$media,'--settings-path',"$root\settings",'--drag-samples','20'
Start-Process $exe -Wait -ArgumentList '--startup-test','--file',$media,'--settings-path',"$root\settings"
$env:QT_SCALE_FACTOR='1.25'   # 再跑 1 / 1.5 / 2
Start-Process $exe -Wait -ArgumentList '--dpi-test','--file',$media,'--dpi-tag','c7-1.25','--dpi-expect-scale','1.25','--settings-path',"$root\settings"
Start-Process $exe -Wait -ArgumentList '--dpi-test','--empty','--dpi-tag','c7e-1','--dpi-expect-scale','1','--settings-path',"$root\settings"

# 打开预热 + B 720P（LAPIAN_ONLINE_LOG=1 才打印 prewarm/ready 行）
$env:LAPIAN_ONLINE_LOG='1'
Start-Process $exe -Wait -ArgumentList '--online-quality-switch-test','--quality-cache-hit-menu','--bvid','BV1cW411b7Yh','--cid','28925848','--timeout-ms','60000',
  '--online-cache-root',"$root\online\b720\cache",'--online-pictures',"$root\online\b720\pics",
  '--online-records-root',"$root\online\b720\records",'--settings-path',"$root\online\b720\settings"

# A：缓存命中拖动（720P/1080P 1s/60；保证档 start145/150）
Start-Process $exe -Wait -ArgumentList '--online-drag-preview-test','--bvid','BV1cW411b7Yh','--cid','28925848',
  '--drag-seconds','1','--drag-samples','60','--drag-cache-hit','--timeout-ms','90000',
  '--online-cache-root',"$root\online\a720\cache",'--online-pictures',"$root\online\a720\pics",
  '--online-records-root',"$root\online\a720\records",'--settings-path',"$root\online\a720\settings"

# 时间轴 widget 级
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
& build\release\Release\lapian_timeline_test.exe
```

- 安装日志：`%TEMP%\lapian-release-2026-10-26-cachehit2\install.log`（首次）/
  `install-2.log`（`Start-Process -Wait` 计时那次）；构建日志：`build\release-close-c7\build-clean.log`；
  冒烟日志：`build\release-close-c7\logs\{local,empty,online}\*.log`。
- 坑（沿用）：GUI 探针要 `Start-Process -Wait -RedirectStandardOutput/-Error`（`*>` 只重定向文件、
  但 `&` 对 GUI exe 不会等待，日志会 0 字节且拿不到 exit code）；`--online-quality-switch-test`
  的 `quality_switch_result` 在 **stdout**，检查项/HTTP/CDN 在 **stderr**；缓存命中拖动必须
  `data.mp4` 完整；1080P 切 112 前缓存根不能有**完整** 112-7（只有 `data.part` 会续传真下载）。

---

## 8. 不要宣称

- 不要写“发布就绪”“T7.7/P14 通过”“安装版冒烟全绿”——仍有 §6 的残差与覆盖缺口。
- 不要写“打开不再卡”“打开变快 145ms”：145ms 是**移到加载中**，click→ready 总量基本不变。
- 不要写“1080P 缓存命中 P6 保证档外通过”：2s/60 与默认 1s/60 的 P6 是 `out_of_premise`，只记录。
- 不要写“1080P start150 反向通过”“720P 2s/60 反向通过”：7/30、25/30 是已知残差。
- 不要写“真实鼠标 auto-pan 已验证”：只有 widget 级合成事件。
- 不要写“cache-hit P7/P9/音频/60s A-V 已验证”：无直接探针。
- 不要把“T8.6.1b 本轮 35/0”写成“已修复”：历史基线 35/1，属既有非门基线波动，未改代码。
- 不要把用户的 E: 免安装版写成“已升级”：本轮未动 E: 目录/进程（两份副本 hash/mtime 未变）。

## 9. 用户如何用新包

1. **正常安装（推荐）**：双击 `out\package\Lapian-0.1.0-setup.exe`，按向导装到
   `%LOCALAPPDATA%\Programs\拉片软件`（每用户，不需要管理员）。
2. **静默安装**：

```powershell
& '.\out\package\Lapian-0.1.0-setup.exe' /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-
```

3. **便携版**：解压 `out\package\Lapian-0.1.0-portable.zip` 到任意可写目录，运行
   `Lapian-0.1.0\lapian.exe`（本机从 E: 直接运行 exe 可能被 SmartScreen 拦，可先复制到 C: 再运行）。
4. **E: 免安装旧版**：用户正在运行的是 `E:\开源软件尝试\自制拉片软件\免安装版\2\Lapian-0.1.0\lapian.exe`
   （`DDF2E8E9…`，PID 34276）；另有 `…\免安装版\Lapian-0.1.0\lapian.exe`（`E19FC4B3…`）未运行。
   **本轮未覆盖、未删除、未强杀**；要用新版请用户自行关闭旧进程后用新 zip 替换（本轮不代做）。
5. **回滚**：重装上一版 setup `DF76A251…`（12.02MB，缓存命中修复版，exe `DDF2E8E9…`）；
   代码回滚 `git revert 6daa341 c04a180 4ae315a` + 回退本轮 docs 提交
   （见 [handoff §8](handoff-2026-10-26-cache-hit-open-drag.md#8-回滚)）。
