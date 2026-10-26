# 拉片软件 0.1.0 发布收口 — 缓存命中拖动/清晰度修复版（2026-10-26）

> **这是缓存命中拖动/清晰度修复后的内部发布收口记录，不是 T7.7/P14 验收，也不是“发布就绪”。**
> 本轮只做 **clean Release → dist/setup/zip → 每用户静默安装 → 安装版冒烟 → 文档**；
> 未改 `src/` 产品代码、未改任何门/阈值/schema、未改安装形态、未升级 Qt/FFmpeg/CMake。
>
> **结论先说**：clean 构建、打包、每用户静默安装、安装版本地/A/B/parity 冒烟核心项**全绿**；
> 仅 **T8.6.1b 既有的 `forward_ui_gap` 1 项**失败（214/223ms，打开阶段，非门基线）。
> 长 GOP 1080P 2s/60 的松手+落地 203–206ms 属 spec **前提之外**，只记录、不写 P6 通过；
> 覆盖缺口与残余见 §6。**不写“发布就绪/T7.7/P14 通过”。**

---

## 1. 一句话结论

| 项 | 结果 |
|---|---|
| 代码 HEAD | `5c8c390`（源码 = `f2ec2e8` + `b2eaa2e` + test-only `5c8c390`；本轮只加 docs 提交） |
| clean Release | `scripts\build-release.ps1 -Clean -Package`，**275.8s**（configure 5.1s；脚本内 ctest 66.53s） |
| 单元测试 | **ctest 38/38**（脚本内 66.53s；手动复跑 64.47s / ctest 64.38s） |
| exe | **3,578,880 B**，SHA256 `DDF2E8E910C912C31E57EA07902A5F0D675B057F8B223C4D52B567583CCB41BE` |
| dist | **26 文件 / 36,856,648 B / 35.15 MB**（含 `SoundTouch.dll`） |
| stage | **35 文件 / 36,997,614 B / 35.28 MB**（含 licenses/README/PRIVACY） |
| setup | **12,608,039 B / 12.02 MB**，SHA256 `DF76A25152DF864770BD6DF123781E2FF6ED0C2B9B2AB242A348F0B2EA86BC8E` |
| 便携 zip | **15,394,377 B / 14.68 MB**，SHA256 `DAC216AFC2D329B00E3123EFB6EBE5D339B8EBA841E5AAF06C410E6D903A01E0` |
| 安装（每用户） | 静默安装 **exit 0 / 2.41s**；`User privileges: None`、`Administrative install mode: No`、HKCU、`Need to restart Windows? No` |
| 安装版 exe | **3,578,880 B / `DDF2E8E9…`，与 build/dist/stage 同 hash** |
| 安装目录 | 37 文件 / **44,000,238 B**（35 负载 + 用户留的 `lapian-3d.exe`/`lapian-3c.exe`） |
| settings | `%APPDATA%\拉片软件\settings.json` 安装前/后同为 **138 B / `973F4F9C…`**，未被安装/冒烟改动 |
| 本地冒烟 | banner **74/0**、key **77/0**、drag P4 **94.2fps** / P6 p95 **22.9ms**、startup **801/1043ms** |
| A 缓存命中拖动 | **26/0**（720P 2s ×2、720P 1s、1080P 1s、1080P 2s、1080P 保证档）；presented 53–57、fps 30.0–60.6 |
| B 缓存命中清晰度 | 720P **38/0**、1080P **38/0**、普通切档 **42/0**、offline **45/0**、M2 op5 **10/0** |
| 对比 | online parity **61/0**、local parity **9/0**；T8.6.1b **35/1（214ms）**、**24/1（223ms）** 既有 |
| 总体判定 | **分发包已生成、安装可用、安装版核心冒烟通过；不写“发布就绪/T7.7/P14 通过”** |

---

## 2. 交付物与校验

| 形态 | 文件 | 体积 | SHA-256 |
|---|---|---:|---|
| 程序目录 | `dist\` | 26 文件 / 35.15 MB | exe：`DDF2E8E910C912C31E57EA07902A5F0D675B057F8B223C4D52B567583CCB41BE` |
| 便携 zip | `out\package\Lapian-0.1.0-portable.zip` | 14.68 MB | `DAC216AFC2D329B00E3123EFB6EBE5D339B8EBA841E5AAF06C410E6D903A01E0` |
| 每用户安装包 | `out\package\Lapian-0.1.0-setup.exe` | 12.02 MB | `DF76A25152DF864770BD6DF123781E2FF6ED0C2B9B2AB242A348F0B2EA86BC8E` |
| 打包报告 | `out\package\package-report.txt` | — | HEAD `5c8c390`；dist/stage/zip/setup 体积与 hash |

- **build exe == dist exe == stage exe == 安装版 exe**：均 `DDF2E8E9…`，3,578,880 B。
- 上一版（音高保持）exe 为 `E8149B92…`（3,542,528 B）、setup `96C5A28B…`、zip `80404080…`；
  本版 hash 已变，属代码增量 + 链接时间戳；以本文为准。
- `scripts\check-imports.ps1`：**21 个文件，通过，无 QtNetwork/TLS/winhttp/wininet/urlmon**；
  仅已知并接受的 `Qt6Core.dll -> WS2_32`（winsock 初始化）。
- `dist` 是程序负载（26 文件，按 T7.6 设计**不含** licenses/README/PRIVACY）；
  `out\package\stage` 与 zip 根目录 `Lapian-0.1.0\` 含 **35 文件**：
  `licenses\{GPL-3.0,LGPL-2.1,LGPL-3.0,qrcodegen,SoundTouch-COPYING,SOURCE-OFFER,THIRD-PARTY-NOTICES}`、
  `README.txt`、`PRIVACY.txt`、`SoundTouch.dll`。
- 体积：setup/zip/dist 均 ≤100MB（P12）。
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
$logDir = 'build\release-close'; New-Item -ItemType Directory -Force -Path $logDir | Out-Null
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-release.ps1 -Clean -Package *> "$logDir\build-clean.log"
```

- `-Clean` 删除并全新 configure `build\release`（未触发本机已知的 CMake 重配
  `0xC0000409`；`build\c2-*` 夹具未删）。
- configure 5.1s；构建 + dist + 打包合计 **275.8s**（含脚本内 ctest 66.53s）。
- 脚本内 ctest：**38/38**，`Total Test time (real) = 66.53 sec`。
- 手动复跑（按任务环境）：

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$env:TMP='E:\拉片软件开发\.deps\tmp'; $env:TEMP=$env:TMP
ctest --test-dir build\release -C Release --output-on-failure
```

结果：**38/38**，wall **64.47s**，ctest 计时 **64.38s**，exit 0。
- 新 exe：`build\release\bin\lapian.exe`，3,578,880 B，`DDF2E8E9…`；dist/stage 副本同 hash。
- 构建日志：`build\release-close\build-clean.log`；手动 ctest：`build\release-close\ctest-manual.log`。

---

## 4. 安装验证（每用户、非管理员、Medium）

- setup 先复制到 C: 本地：`%TEMP%\lapian-release-2026-10-26-cachehit\Lapian-0.1.0-setup.exe`
  （避免 E: 盘 SmartScreen；未从 E: 直接运行）。
- 静默安装参数（`/NOCLOSEAPPLICATIONS` 避免触碰用户正在运行的 E: 免安装旧版）：

```powershell
& "$env:TEMP\lapian-release-2026-10-26-cachehit\Lapian-0.1.0-setup.exe" `
  /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOCLOSEAPPLICATIONS `
  /LOG="$env:TEMP\lapian-release-2026-10-26-cachehit\install.log"
```

| 核对项 | 结果 |
|---|---|
| 退出码 / 用时 | **0 / 2.41s**（等进程退出） |
| 安装日志 | `User privileges: None`、`Administrative install mode: No`、`Install mode root key: HKEY_CURRENT_USER`、64-bit Yes、`Installation process succeeded`、`Need to restart Windows? No` |
| 安装目录 | `%LOCALAPPDATA%\Programs\拉片软件`；**37 文件 / 44,000,238 B**（35 负载 + 用户留的 `lapian-3d.exe`/`lapian-3c.exe`） |
| 安装版 exe | 3,578,880 B，`DDF2E8E9…`，**与 dist 相同** |
| Start 菜单 | `拉片软件.lnk` → `…\Programs\拉片软件\lapian.exe`；`第三方许可与源码说明.lnk` → `…\Programs\拉片软件\licenses`；`卸载 拉片软件.lnk` → `%LOCALAPPDATA%\拉片软件\uninstaller\unins000.exe` |
| 桌面快捷方式 | `拉片软件.lnk` → `…\Programs\拉片软件\lapian.exe` |
| 用户备份 | `lapian-3d.exe`（`99084199…`）与 `lapian-3c.exe`（`5863DD0A…`）hash 与安装前一致，未被删除 |
| settings | 安装前/后同为 **138 B / `973F4F9C4FE7BF0EAD889A5439D75F09A7CC3509D64522BE562149B7E5B3F91F`**；冒烟后仍相同 |
| 许可/说明 | 安装目录含 `README.txt`、`PRIVACY.txt`、`licenses\SoundTouch-COPYING.txt`、`THIRD-PARTY-NOTICES.md`、`SoundTouch.dll` |
| 卸载器 | `%LOCALAPPDATA%\拉片软件\uninstaller\unins000.exe`（4,452,610 B，在 `{app}` 之外） |

- 安装前 C: 安装版未运行（只有用户在跑的 E: 免安装旧版 PID 29312，
  `E:\开源软件尝试\自制拉片软件\免安装版\Lapian-0.1.0\lapian.exe`）；安装后 PID 29312
  **仍在运行**，路径未变。E: 目录本轮**未覆盖、未删除、未强杀**。
- 安装目录原有 `lapian.exe`（`E8149B92…`，音高保持版）被覆盖为新 hash；
  `lapian-3d.exe`/`lapian-3c.exe` 原样保留。

---

## 5. 安装版冒烟

被测程序一律为 `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`（Medium 完整性），
从仓库根运行；本地测试写 `build\release-close\settings`，在线测试写
`build\release-close\online\<case>\{cache,records,pics,settings}`，不碰用户默认
缓存/记录/截图/凭据。日志在 `build\release-close\logs\c3\`。
缓存夹具为 `build\c2-720p`/`build\c2-1080p`/`build\c1-qs` 的**只读复制**。

### 5.1 本地（安装版）

| 项 | 结果 |
|---|---|
| `--banner-test` | **74/0**（`banner_summary checks=74 fails=0`，`info_h=22`） |
| `--key-test` | **77/0**（`key_check ok=1` ×77，`ok=0` ×0；本轮无焦点 flake） |
| `--drag-test --drag-samples 20` | P4 **94.2fps**（189 presents/2.01s）、P6 p95 **22.9ms**、P7 back **0.1**/fwd **12.9**、P8 **12.9fps/26 帧**、P9 p95 **42.1ms**；全部 pass |
| `--startup-test --file` | window_shown **801.36ms**、ui_ready **810.76ms**、first_present **1042.97ms**；`d3d_init_at_startup=0` |

- drag 与父/C2 基线（90.1/27.4/0.4/10.7/12.8/31.7）同量级：P4/P6/P8 更好，P7 前进
  12.9ms 仍在 80ms 内，P9 42.1ms 在 200ms 内；属同机波动，不改阈值/数字。

### 5.2 A：缓存命中拖动（安装版）

| 场景 | checks | presented | fps_active | first | release+landing | P6 前提 | ui_gap max |
|---|---|---:|---:|---:|---|---|---:|
| 720P 2s/60 第 1 次 | **26/0** | 57 | 30.03 | 102ms | 112+2=**114ms** | out_of_premise=1（133 帧/4522ms） | 118ms |
| 720P 2s/60 第 2 次 | **26/0** | 57 | 30.00 | 101ms | 111+2=**113ms** | out_of_premise=1（133/4522ms） | 111ms |
| 720P 1s/60 | **26/0** | 55 | 60.11 | 85ms | 63+3=**66ms** | out_of_premise=1（75 帧/2550ms） | 86ms |
| 1080P 1s/60 | **26/0** | 55 | 60.24 | 88ms | 105+3=**108ms** | out_of_premise=1（75/2500ms） | 115ms |
| 1080P 2s/60 | **26/0** | 57 | 30.45 | 129ms | 200+3=**203ms** | out_of_premise=1（135/4500ms） | 205ms（含 1 次 >200ms 事件） |
| 1080P 保证档 `--drag-start-frame 150` | **26/0** | 53 | 60.57 | 126ms | 75+3=**78ms** | **在前提内**（60 帧/2000ms） | 89ms |

- 所有 A 场景：`cache_hit=1`、`exact_frame=1`、`exact_pts=1`、`stale=0`、
  `playurl=0`、`dash=0`；`configured_exists=1`（打开真实 `data.mp4`），
  `session_exists=0`（展示名不落盘）。
- 720P 2s 的 P6 是 spec 前提之外的长 GOP 值；**1080P 2s 首次复跑 206ms / 二次 203ms**，
  同属 `out_of_premise`。**保证档内 P6 78ms**（C1 59–79ms、C2 80ms 量级）。
- 1080P 2s 两次运行各出现 1 次约 205–218ms 的 `ui_gap_event`（打开/拖动起始），
  forward 拖动窗口内 `ui_gap_max` 87–97ms、结果仍 26/0；该事件不在门禁检查内，
  按原始数字记录，不改门。
- 父描述的“刚重建后首次 720P 首跑 first_ms=1210 / ui_gap=296 / 26/1”瞬态**本轮未复现**：
  安装后第 1 次 720P 2s 即为 102ms / 118ms / 26/0。
- `WarmupVideoStageForTest()` 仅 `--drag-cache-hit` 生效；安装版
  `online_drag_preview_warmup stage_ms=143–157`（`first_open_d3d_init`），
  之后 `stage_show_ms=0`、`wait_ready=79–83ms`。

### 5.3 B：缓存命中清晰度菜单/切档（安装版）

| 用例 | 结果 | 关键数字 |
|---|---|---|
| 720P `--quality-cache-hit-menu`（`BV1cW411b7Yh`/`28925848`） | **38/0** | 打开 `cache_hit=1 / playurl=0 / dash=0 / quality=0`，qn=64；初始 1 档 + 缓存 meta 标题；菜单 loading + **1 次 catalog（HTTP 200，121ms）**；**3 档 / enabled_other=2 / 服务端标题**；**64→32 实际 qn=32/codecid=7、cache_hit=0**，PTS **1,068,000 不变**，mark/record/snapshot 保留；**32→64 cache_hit=1 / playurl=0 / dash=0** |
| 1080P `--quality-cache-hit-menu`（`BV1BMnv6jE5S`/`42915922022`） | **38/0** | 80 打开 `cache_hit=1`、**0 网络**；**catalog 1 次（200，123ms）、5 档含 112 / enabled_other=4**；切 112 **真实 CDN 下载**（`30112.m4s`，落地时 `published=3,653,586/48,462,784`，C2 为 4.44M/48.46M），qn=112/codecid=7、cache_hit=0，PTS **1,000,000 不变**，mark/record/snapshot 保留；切回 80 `cache_hit=1 / playurl=0 / dash=0` |
| 普通切档 `--quality-switch-high 112 --quality-switch-back 80` | **42/0** | 新鲜缓存；初始 80（dash=1）、112 完整下载（47,168,494 B）、切回 80 缓存命中、0 playurl/0 DASH；3 个完整缓存条目 |
| `--online-offline-test`（规范 3.2MB `build\test-media\faststart-720p.mp4`） | **45/0** | cache-hit 0 网络；大文件 46MB/201MB 的 45/9 是既有 `clear_cache busy` 级联，**本轮不追** |
| `--online-m2-test --m2-op5` | **10/0** | ready 240ms、qn=80、`playurl=0 / dash=0 / quality=0 / http=0` |

- 720P 38 项 check 全过；`quality_switch_cache_hit phase=open/menu_fetched/switch/back`
  与 `quality_switch_result checks=38 failures=0 verdict=pass` 在日志中。
- 1080P 的 112 只验证到“实际取流并落地”（随后切回取消），**未等 112 完整下载完成**；
  完整 112 下载由普通切档 42/0 用例覆盖（112 条目 47,168,494 B complete）。

### 5.4 本地/首次在线对比（安装版）

| 用例 | 结果 | 关键数字 |
|---|---|---|
| `--local-drag-parity-test --file <OB-1 80 data.mp4>` | **9/0** | forward 58.94fps / first 21ms / gap 23ms；backward 14.25fps（该场景 `ok=0` 字段不参与 9 项判定）；ui_gap 24ms |
| `--online-drag-parity-test`（1s/60，1MiB/s） | **61/0** | cold_first **1.00ms**；forward **58.65fps** / backward **58.47fps**；gap 32/33ms；presented_diff **0.000**；post_seek first_delivered 48/49ms；ui_gap 18ms；cache 命中 59；`post_seek_immediate max_response 822ms`（既有残余，不门禁） |
| `--online-drag-preview-test`（不带 cache-hit，1s/60） | **35/1** | 失败项 `forward_ui_gap_le_200ms` **214ms**；失败打印在 `cached_window` 之前 = 打开阶段（`wait_ready pump=206ms`），既有非门基线；其余 34 项全过（forward 55 presented / 58.89fps / exact 75） |
| 同上 2s/60 | **24/1** | `forward_ui_gap_le_200ms` **223ms**；既有基线（父/C2 349ms），不修、不掩饰 |

---

## 6. 残余 / 未验证 / 风险

1. **长 GOP P6 前提**：1080P 2s/60（目标距 K0 4.5s）松手+落地 **203–206ms**，超出
   spec P6“目标距前关键帧 ≤2s”前提；`5c8c390` 已按前提分流（`out_of_premise` 只打印），
   **不得写“1080P 缓存命中 P6 保证档外通过”**。保证档内 P6 **78ms**（范围 47–80ms）。
2. **C2 覆盖缺口**（照写）：cache-hit **P7/P9 无直接探针**（同素材本地代理 720P
   P7 0.1/1.7、P9 47.1；1080P 0.3/2.9、67.4）；cache-hit **音频只验证打开/就绪**
   （48k、`pitch_preserve=1`），无 underrun/A-V 偏差；**首次在线 60s A/V** 被 M2
   普通用户前置 blocked（默认凭据大会员）；**cached→cached 切回经真实菜单打开路径
   无直接探针**；1080P 112 未验证“完整下载后再次命中”（本轮落地即切回）。
3. **T8.6.1b 既有 `forward_ui_gap`**：安装版 **214/223ms** >200ms，失败在打开阶段
   （`cached_window` 之前），父/C2 为 327/349ms；非门基线，本轮不修。
4. **构建后首次运行瞬态**：父一次刚重建后首跑 720P 出现 `first_ms=1210 / ui_gap=296 / 26/1`，
   随后 3/3 恢复；本轮安装版未复现。若后续复现，记录原始数字，不改门。
5. **offline 大文件 45/9**：既有 `clear_cache busy` 级联，pre-A/B 的 3C 二进制同样，
   本轮不追；规范 3.2MB 媒体 45/0。
6. **本轮未跑**：M1/M2 全量、quality 1080P 目录矩阵、longpress、QR、fault、scrub-space、
   dpi、fullscreen、mark/note/snapshot 等门禁；这些沿用父/C2 本轮结果（ctest/key/banner/
   drag/parity 已复跑）。**不要把本记录当成完整门禁清单**。
7. **无代码签名、安装器 UI 英文、无自定义图标**；未在干净机/Win10 21H2 复验；
   未做 Explorer 真双击与冷缓存 F1-A1。
8. **用户体感**：本轮未收集新体感；不写“验收通过”。

---

## 7. 复现 / 校验命令

```powershell
cd 'E:\拉片软件开发'
$exe = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"
$media = 'media\测试.mp4'
$sp = 'build\release-close\settings'

# 本地
& $exe --banner-test --file $media --banner-tag c3 --settings-path $sp
& $exe --key-test --file $media --settings-path $sp
& $exe --drag-test --file $media --settings-path $sp --drag-samples 20
& $exe --startup-test --file $media --settings-path $sp

# A：缓存命中拖动（720P；缓存根为 c2-720p 只读复制）
& $exe --online-drag-preview-test --bvid BV1cW411b7Yh --cid 28925848 `
  --drag-seconds 2 --drag-samples 60 --drag-cache-hit --timeout-ms 60000 `
  --online-cache-root build\release-close\online\a720\cache `
  --online-pictures build\release-close\online\a720\pics `
  --online-records-root build\release-close\online\a720\records `
  --settings-path build\release-close\online\a720\settings

# B：720P / 1080P 缓存命中菜单
& $exe --online-quality-switch-test --quality-cache-hit-menu `
  --bvid BV1cW411b7Yh --cid 28925848 --timeout-ms 60000 `
  --online-cache-root build\release-close\online\b720\cache `
  --online-pictures build\release-close\online\b720\pics `
  --online-records-root build\release-close\online\b720\records `
  --settings-path build\release-close\online\b720\settings

& $exe --online-quality-switch-test --quality-cache-hit-menu `
  --bvid BV1BMnv6jE5S --cid 42915922022 --timeout-ms 60000 `
  --online-cache-root build\release-close\online\b1080\cache `
  --online-pictures build\release-close\online\b1080\pics `
  --online-records-root build\release-close\online\b1080\records `
  --settings-path build\release-close\online\b1080\settings

# 对比
& $exe --online-drag-parity-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --drag-seconds 1 --drag-samples 60 --m1-throttle-bps 1048576 --timeout-ms 60000 `
  --online-cache-root build\release-close\online\parity-online\cache `
  --online-pictures build\release-close\online\parity-online\pics `
  --online-records-root build\release-close\online\parity-online\records `
  --settings-path build\release-close\online\parity-online\settings
& $exe --local-drag-parity-test --file build\release-close\media\ob1-80.mp4 `
  --settings-path build\release-close\online\local-parity\settings
```

- 安装日志：`%TEMP%\lapian-release-2026-10-26-cachehit\install.log`
- 构建日志：`build\release-close\build-clean.log`
- 冒烟日志：`build\release-close\logs\c3\*.log`
- 坑（沿用）：GUI 探针要用 `Start-Process -Wait -RedirectStandardOutput/-Error`
  或重定向到文件，**不要 `$out = & exe 2>&1`**（会挂）；`--online-quality-switch-test`
  的 `quality_switch_result` 在 **stdout**，检查项/HTTP/CDN 在 **stderr**；
  缓存命中拖动的缓存条目必须 `data.mp4` 完整；1080P 切 112 前缓存根不能已有 112-7。

---

## 8. 不要宣称

- 不要写“发布就绪”“T7.7/P14 通过”“安装版冒烟全绿”——T8.6.1b 仍有 1 项既有失败。
- 不要写“1080P 缓存命中 P6 保证档外通过”：2s/60 是 `out_of_premise`，只记录。
- 不要写“cache-hit P7/P9/音频/60s A-V 已验证”：C2 明确未验证（§6.2）。
- 不要把用户的 E: 免安装版写成“已升级”：本轮未动 E: 目录/进程，需用户自行替换。
- 不要把“T8.6.1b 214/223ms 比父 327/349ms 低”写成“已修复”：是既有非门基线波动，未改代码。

## 9. 用户如何用新包

1. **正常安装（推荐）**：双击 `out\package\Lapian-0.1.0-setup.exe`，按向导装到
   `%LOCALAPPDATA%\Programs\拉片软件`（每用户，不需要管理员）。
2. **静默安装**：

```powershell
& '.\out\package\Lapian-0.1.0-setup.exe' /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-
```

3. **便携版**：解压 `out\package\Lapian-0.1.0-portable.zip` 到任意可写目录，运行
   `Lapian-0.1.0\lapian.exe`（本机从 E: 直接运行 exe 可能被 SmartScreen 拦，
   可先复制到 C: 再运行）。
4. **E: 免安装旧版**：用户当前正在运行的 `E:\开源软件尝试\自制拉片软件\免安装版\Lapian-0.1.0\lapian.exe`
   **本轮未覆盖、未删除、未强杀**；要用新版请用户自行关闭旧进程后用新 zip 替换该目录
   （本轮不代做）。
5. **回滚**：重装上一版 setup `96C5A28B…`（12.02MB，音高保持版，exe `E8149B92…`）；
   代码回滚见 [handoff §8](handoff-2026-10-26-cache-hit-fix.md#8-回滚)。
