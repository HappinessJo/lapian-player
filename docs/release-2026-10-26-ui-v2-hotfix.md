# 拉片软件 0.1.0 发布收口 — UI v2 3D 热修版（2026-10-26）

> **这是 UI v2 3D 热修后的内部发布收口记录，不是 T7.7/P14 验收，也不是“发布就绪”。**
> 本轮只做 **clean Release → dist/setup/zip → 每用户安装 → 安装版冒烟 → 文档**，
> 未改 `src/` 产品代码、未改任何门/阈值/schema、未改安装形态。
>
> **结论先说**：构建、打包、安装、本地冒烟通过；**安装版在线冒烟未全绿**——
> `--online-m1-test` 的 `ui_gap_le_200ms` 在 3 个样本上均超 200ms（216–231ms），
> `--scrub-space-test` 在线 20/20 轮 `playing` 失败（同一现象用热修前二进制 `EE36…`
> 也可复现）；T8.6.1b/fault 仍为既有失败。详见 §5.2 与 §6。
> **用户体感只记体感，不写“验收通过”**：2026-10-26 用户原话“很不错了，很舒服了”。

---

## 1. 一句话结论

| 项 | 结果 |
|---|---|
| 代码 HEAD | `0574766`（docs-only）；源码仍是 4 个热修 commit `534d909`/`b78bbe1`/`6533da0`/`efc6831` |
| clean Release | `scripts\build-release.ps1 -Clean -Package`，**249.6s**（脚本内构建+dist 236.0s，ctest 64.30s） |
| 单元测试 | **ctest 37/37**（64.30s） |
| exe | **3,538,944 B**，SHA256 `E19FC4B30C530FDBEF15D41C598A67CC2AC8F059985CB8ECC2182685B7CA3CE1` |
| dist | **25 文件 / 36,748,616 B / 35.05 MB** |
| setup | **11.99 MB**，SHA256 `957EC30A75E2206EA2384CBE906CF5E820F1FB237353E07538B30D8C7FADB164` |
| 便携 zip | **14.63 MB**，SHA256 `76F9B6FAE3EFF163666631C129549DB04B35D40C53AB8E88483663292E7ED6E0` |
| 安装（每用户） | 静默安装 **exit 0 / 2.18s**，`User privileges: None`、`Administrative install mode: No`、HKCU |
| 安装版 exe | 与 build/dist/stage **同 hash** `E19FC4B3…` |
| 本地冒烟 | banner **74/0**、key **77/0**、dpi 4 DPI×{播放,全屏,窄窗} **problems 空**、空态 4 DPI **空**、drag P4 **95.8fps** / P6 p95 **30.7ms**、startup P1 **809/1047ms** / P2 **813ms** |
| 在线冒烟 | cache-switch **17/0**、parity **61/0**、longpress **54/0**、QR **19/0 + 26/0**；**M1 与 scrub-space 失败**；T8.6.1b **35/1**、fault **92/0（ui_gap 232ms）** 为既有 |
| 总体判定 | **分发包已生成、安装可用；安装版冒烟未全绿，不写“冒烟通过/发布就绪”** |

---

## 2. 交付物与校验

| 形态 | 文件 | 体积 | SHA-256 |
|---|---|---:|---|
| 程序目录 | `dist\` | 25 文件 / 35.05 MB | exe：`E19FC4B30C530FDBEF15D41C598A67CC2AC8F059985CB8ECC2182685B7CA3CE1` |
| 便携 zip | `out\package\Lapian-0.1.0-portable.zip` | 14.63 MB | `76F9B6FAE3EFF163666631C129549DB04B35D40C53AB8E88483663292E7ED6E0` |
| 每用户安装包 | `out\package\Lapian-0.1.0-setup.exe` | 11.99 MB | `957EC30A75E2206EA2384CBE906CF5E820F1FB237353E07538B30D8C7FADB164` |
| 打包报告 | `out\package\package-report.txt` | — | 记录 HEAD `0574766`、dist/stage/zip/setup 体积与 hash |

- **build exe == dist exe == stage exe == 安装版 exe**：均
  `E19FC4B30C530FDBEF15D41C598A67CC2AC8F059985CB8ECC2182685B7CA3CE1`，3,538,944 B。
- clean 重建的 hash 与热修时的 `EE366C0A…`（3,538,432 B）不同，属链接时间戳差异；
  本轮以新 hash 为准（4 个代码 commit 未变）。
- 校验命令：

```powershell
Get-FileHash dist\lapian.exe -Algorithm SHA256
Get-FileHash "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe" -Algorithm SHA256
Get-FileHash out\package\Lapian-0.1.0-setup.exe -Algorithm SHA256
Get-FileHash out\package\Lapian-0.1.0-portable.zip -Algorithm SHA256
```

- **许可/说明放置**：`dist` 是程序负载（25 文件，按 T7.6 设计**不含** licenses/README/PRIVACY）；
  `out\package\stage` 与 zip 根目录 `Lapian-0.1.0\` 含 **33 文件**，包括
  `licenses\{GPL-3.0,LGPL-2.1,LGPL-3.0,qrcodegen,SOURCE-OFFER,THIRD-PARTY-NOTICES}`、
  `README.txt`、`PRIVACY.txt`；安装包按 stage 安装，安装目录同样含这些文件。
- `check-imports.ps1`：20 个文件，无 QtNetwork/TLS/winhttp/wininet/urlmon；
  `lapian_env_check.exe`：Qt 6.8.3 / FFmpeg 7.1.1 / D3D11 硬件（RTX 4060）。
- 体积：setup/zip/dist 均 ≤100MB（P12）。

---

## 3. clean Release 构建

```powershell
cd 'E:\拉片软件开发'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-release.ps1 -Clean -Package
```

- 构建目录被 `-Clean` 整目录删除后全新 configure，未触发本机已知的 CMake 重配
  `0xC0000409`；configure 用时 5.1s。
- 构建 + dist 用时 **236.0s**；含打包总用时 **249.6s**（PowerShell 5.1）。
- `ctest --test-dir build\release -C Release`：**37/37 通过**，总耗时 **64.30s**。
- 新 exe：`build\release\bin\lapian.exe`，3,538,944 B，`E19FC4B3…`；
  dist/stage 副本同 hash。
- 构建日志：`build\release-close\build-clean.log`（完整 stdout/stderr）。

---

## 4. 安装验证（每用户、非管理员）

- setup 先复制到 C: 本地：`%TEMP%\lapian-release-2026-10-26\Lapian-0.1.0-setup.exe`
  （避免 E: 盘 SmartScreen；未从 E: 直接运行）。
- 静默安装参数：

```powershell
& "$env:TEMP\lapian-release-2026-10-26\Lapian-0.1.0-setup.exe" `
  /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- `
  /LOG="$env:TEMP\lapian-release-2026-10-26\install.log"
```

| 核对项 | 结果 |
|---|---|
| 退出码 / 用时 | **0 / 2.18s**（等进程退出） |
| 安装日志 | `User privileges: None`、`Administrative install mode: No`、`Install mode root key: HKEY_CURRENT_USER`、64-bit Yes、`Need to restart Windows? No` |
| 安装目录 | `%LOCALAPPDATA%\Programs\拉片软件`；**35 文件 / 43,866,927 B**（33 个负载文件 + 用户留的 `lapian-3d.exe`/`lapian-3c.exe`） |
| 安装版 exe | 3,538,944 B，`E19FC4B3…`，**与 dist 相同** |
| Start 菜单 | `拉片软件.lnk` → `…\Programs\拉片软件\lapian.exe`；`第三方许可与源码说明.lnk` → `…\licenses`；`卸载 拉片软件.lnk` → `…\拉片软件\uninstaller\unins000.exe` |
| 桌面快捷方式 | `拉片软件.lnk` → `…\Programs\拉片软件\lapian.exe` |
| 用户备份 | `lapian-3d.exe`（3,495,424 B，`99084199…`）与 `lapian-3c.exe`（3,507,200 B，`5863DD0A…`）哈希与安装前一致，未被删除 |
| settings | `%APPDATA%\拉片软件\settings.json` 安装前/后同为 **78 B / `84CBE14D31918C509503E132F04C380A2626DBA5CC2B0C95A109772ED02709D5`**，安装未清设置 |
| 卸载器位置 | `%LOCALAPPDATA%\拉片软件\uninstaller\unins000.exe`（不在 `{app}` 内） |

- 安装前有一个正在播放的 Medium 实例（PID 27724，标题为在线视频）；先
  `CloseMainWindow()` 正常关闭（15s 内退出），再安装。关闭前后 settings hash 相同。
- 安装目录原有热修手工副本 `lapian.exe`（`EE36…`）被 setup 覆盖为 **新 dist hash**；
  用户自留的 `lapian-3d.exe`/`lapian-3c.exe` 原样保留。

---

## 5. 安装版冒烟

被测程序一律为 `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`（Medium 完整性），
从仓库根运行；本地测试写 `build\release-close\settings`，在线测试写
`build\release-close\online\<case>\{cache,records,pics,settings}`，不碰用户默认缓存/记录/截图。
日志在 `build\release-close\logs\`。

### 5.1 本地（安装版）

| 项 | 结果 |
|---|---|
| `--banner-test` | **74/0**，`banner_summary checks=74 fails=0`，`info_h=22` |
| `--key-test` | **77/0**（77 条 `key_check`，无 `ok=0`） |
| `--dpi-test` 100/125/150/200% | 每档 **37 检查 / 0 失败**，`dpi_env scale=1.00/1.25/1.50/2.00`，problems 空 |
| `--dpi-test --empty` 100/125/150/200% | 每档 **40 检查 / 0 失败**，`mode=未打开`、`collapsed=1`、`banner_h=0` |
| `--drag-test` | P4 **95.8fps**（192 presents/2.00s）、P6 p95 **30.7ms**（阈值 150，pass）、P7 back 0.04/fwd 17.1、P8 12.9fps/26 帧 pass、P9 p95 31.0ms；cache hits=0/misses=1 |
| `--startup-test --file` | P1：window_shown **809.25ms**、ui_ready 816.42ms、first_present **1047.37ms**；`d3d_init_at_startup=0` |
| `--startup-test`（无文件） | P2：window_shown **813.10ms**、ui_ready 818.64ms |

- P6 p95 30.7ms 比热修会话的 21.6–24.1ms 慢，但远低于 150ms 门槛，属同机负载差异；
  本轮不据此改阈值/数字。

### 5.2 在线（安装版、Medium、匿名；SESSDATA 仍过期）

| 用例 | 结果 | 说明 |
|---|---|---|
| `--online-m1-test` OB-1 | **25 检查 / 1 失败**：`ui_gap_le_200ms ok=0 max_gap_ms=217`；复跑 **215**、三跑 **216** | 其余 24 项全过（qn=80、首帧 487ms、截图 1920×1080、关闭 24ms） |
| `--online-m1-test` OB-2（`BV1mA411m7pS`/`1004967230`） | **28 / 1**：`ui_gap 231ms` | 首帧 841ms、qn=116 |
| `--online-m1-test` OB-3（`BV1GJ411x7h7`/`137649199`） | **28 / 1**：`ui_gap 222ms` | 首帧 803ms、qn=80 |
| **A/B：旧二进制 `EE36…`**（Medium 副本，同机同时刻） | **25 / 1**：`ui_gap 238ms` | 旧 exe 同样失败 → 非新构建回归 |
| `--scrub-space-test` 在线 | **64 检查 / 20 失败**：20/20 轮 `scrub_space_playing_i ok=0`（`pending=0`、`presented +1`、`seek_idle=1`） | P0 用例未通过 |
| **A/B：旧二进制 `EE36…`** | **64 / 20**，同样 20 轮 `playing` 失败 | 旧 exe 同样失败 |
| `--scrub-space-test --file` 本地 | **62/0 pass** | 本地路径正常 |
| `--cache-switch-online-test` | **17/0 pass** | 播放不中断、`.part`/root 不变、pending；结束会话后新 root 生效、下次会话落新 root |
| `--online-drag-parity-test` | **61/0 pass** | cold_first 2ms、forward 56.94fps / backward 56.66fps、gap 32/32ms、presented_diff 0.000、post_seek first_delivered 63/46ms、ui_gap 32ms、cache 60.75MiB/72 帧；`post_seek_immediate` max_response 770ms（既有残余） |
| `--online-longpress-test` | **54/0 pass** | ui_gap 78ms |
| `--online-qr-test` | **19/0 pass** | 干净 TEMP |
| `--online-qr-test --ui` | **26/0 pass** | `dialog=640x878`、QR `240x240`、刷新/取消可见、`within=1`、`bar=0` |
| `--online-drag-preview-test`（T8.6.1b） | **35 / 1**：`forward_ui_gap_le_200ms ok=0 max_gap_ms=248` | **既有失败**，基线 206–228ms；其余 34 项全过（forward fps_active 55.14、精确落点 75、截图等） |
| `--online-fault-test` | **92 / 0 检查、10/10 场景 pass**，但 `ui_gap_ms=232` >200 → `verdict=fail` | **既有失败**，基线 220/203ms；先清 `build\t8.13-fault` |

- 在线测试前 `--online-http-selfcheck` 仍为 `code=-101 is_login=0 vip_status=-1`
  （凭据失效），因此 **A/M2/quality/vip 1080P 断言本轮未跑**；M1 的 qn 断言
  只按测试输出记录（OB-1/2/3 为 80/116/80），不据此写“1080P 验收通过”。
- `scrub_space` 与 M1 的失败均**未改阈值、未改产品代码**；按“只记录、不改阈值”处理。

---

## 6. 已知残余 / 风险

1. **M1 `ui_gap` 216–231ms（阈值 200ms）**：3 个样本均失败；同一 `EE36…` 旧二进制
   同机同时刻为 238ms，说明是**本机/网络时序**而非新构建回归。与 T8.6.1b
   206–228ms、fault ui_gap 220/203ms 同属一个既有 UI-gap 问题；本轮不修、不隐藏。
2. **`--scrub-space-test` 在线 20/20 `playing` 失败**：`pending=0`、presented 只 +1、
   seek_idle=1，表现是“Space 后播放没有持续”；**旧二进制 `EE36…` 同样 64/20 失败**，
   本地 `--scrub-space-test --file` 62/0 通过。属本轮新观察到的时序/测试窗口问题，
   **不能写成 P0 已复验通过**；是否重开 P0 由用户决定（本轮禁止改产品代码）。
3. **T8.6.1b**：35 检查 / 1 失败，`forward_ui_gap=248ms`（基线 206–228ms）；不改变阈值。
4. **fault**：92/0 检查、10/10 场景 pass，`ui_gap=232ms` 导致 verdict fail（基线 220/203ms）。
5. **SESSDATA 过期**：`code=-101 / is_login=0 / vip_status=-1`，A/M2/quality/vip 1080P
   未跑；用户给新凭据后在同一环境补跑，不进本轮发布收口。
6. **dist 不含 licenses/README/PRIVACY**（T7.6 设计；程序负载 25 文件）；
   许可与说明在 stage/zip/setup/安装目录（33 文件）。
7. **无代码签名、安装器 UI 英文、无自定义图标**；未在干净机/Win10 21H2 复验；
   未做 Explorer 真双击与冷缓存 F1-A1。
8. **用户体感**：2026-10-26“很不错了，很舒服了”——**只记体感，不写“验收通过”**，
   也不覆盖上述量化失败。

---

## 7. 复现 / 校验命令

```powershell
cd 'E:\拉片软件开发'
$exe = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"
$media = 'media\测试.mp4'
$sp = 'build\release-close\settings'

# 本地
& $exe --banner-test --file $media --banner-tag relclose --settings-path $sp
& $exe --key-test --file $media --settings-path $sp
& $exe --dpi-test --file $media --dpi-tag relclose --dpi-expect-scale 1 --settings-path $sp
& $exe --dpi-test --empty --dpi-tag relclose-e --dpi-expect-scale 1 --settings-path $sp
& $exe --drag-test --file $media --settings-path $sp --drag-samples 20
& $exe --startup-test --file $media --settings-path $sp

# 在线（Medium；隔离 roots）
& $exe --online-m1-test --bvid BV1BMnv6jE5S --cid 42915922022 --timeout-ms 60000 `
  --online-cache-root build\release-close\online\m1\cache `
  --online-records-root build\release-close\online\m1\records `
  --online-pictures build\release-close\online\m1\pics --settings-path build\release-close\online\m1\settings
& $exe --scrub-space-test --bvid BV1BMnv6jE5S --cid 42915922022 --timeout-ms 60000
& $exe --cache-switch-online-test --bvid BV1BMnv6jE5S --cid 42915922022 --timeout-ms 60000
& $exe --online-drag-parity-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --drag-seconds 1 --drag-samples 60 --m1-throttle-bps 262144
& $exe --online-longpress-test --bvid BV1BMnv6jE5S --cid 42915922022 --longpress-fresh-cache
& $exe --online-qr-test            # 干净 %TEMP%
& $exe --online-qr-test --ui
```

- 安装日志：`%TEMP%\lapian-release-2026-10-26\install.log`
- 构建日志：`build\release-close\build-clean.log`
- 冒烟日志：`build\release-close\logs\*.log`
- 坑（沿用）：`--snapshot-test` 前清 `media\snapshots`；`--mark-test` 前清
  `media\测试.mp4.lapian.json`；`--online-qr-test` 用干净 `%TEMP%`；
  `--online-fault-test` 前清 `build\t8.13-fault`；GUI 探针用管道输出，避免
  `$out = & exe ... 2>&1`。

---

## 8. 不要宣称

- 不要写“发布就绪”“安装版冒烟全绿”“M1 通过”“scrub-space 通过”“P0 复验通过”。
- 不要把“旧二进制 `EE36…` 同样失败”写成“问题不存在”；它只证明**不是新构建回归**。
- 不要把用户“很不错了，很舒服了”写成验收通过。
- 不要把本轮 setup/zip 当成 T7.7/P14 或 MVP/Beta 通过；正式验收资源仍缺。
- 不要在没有新 SESSDATA 的情况下写 1080P/quality/vip 通过。

## 9. 下一步（按优先级）

1. **用户决定发布方式**：若接受“构建/安装/本地冒烟通过 + 两项在线 ui_gap/scrub-space
   时序残余”，可直接分发 setup/zip；若不接受，另立任务查
   `scrub_space` 的 P0 复验窗口与 M1 `ui_gap`（需改产品代码，不能在发布收口里静默热修）。
2. 有新 SESSDATA 后补 A/M2/quality/vip 1080P 断言（同一 Medium 安装版）。
3. 正式对外发布前补代码签名、安装器中文 UI、干净机冷缓存/Explorer 双击。
