# 拉片软件 0.1.0 发布收口 — 4096 在线预览缓存 + 循环卡死修复 + 工作区记忆（2026-10-27）

> **这是内部发布收口记录，不是 T7.7/P14 验收，也不是“发布就绪”。**
> 本轮只做 **clean Release → dist/setup/zip → 每用户静默安装 → 安装版冒烟 → 文档**；
> 未改 `src/`/`tests/` 产品代码、未改任何门/阈值/schema/`spec.md`、未改 `installer.iss`、
> 未升级 Qt/FFmpeg/CMake。
>
> **结论先说**：clean 构建、打包、每用户静默安装、安装版冒烟**核心项全绿**——
> ctest **38/38**、timeline **188/0**、mark store **281/0**、banner **74/0**、key **77/0**、
> dpi 4 档各 **37/0**（problems 空）、**10s@2GiB 与 20s@4GiB 长跨度门各 3/3 轮 × 36/0 × 8/8**
> （比值 1.015–1.021，peak 1839.1 / 3615.9MiB ≤ 配置+16）、1s@512 **0.982–1.018**、2s@512
> **1.009–1.026**、循环本地 **65/0** / 在线 **32/0**、scrub 本地 **62/0** / 在线 **64/0**、
> 工作区本地 **68/0** + 在线 **32/0** + 新进程恢复、设置页 **149/0 ×2**（dialog-fits OK）、
> B 720P/1080P **38/0 ×2**、offline **45/0**。
> **仍有缺口**：`--online-quality-test` 因本机 SESSDATA 失效 **blocked**（16 项/1 fail，不写 42/0）、
> 无 60fps 源、**P4 的 12 条 gap 原样保留**（§6，含 40s@4096 覆盖 57.5% / close 355–442ms、
> 4096 设置端到端受 25% 护栏等）。
> **不写“发布就绪/T7.7/P14 通过”“quality 42/0 通过”“60fps/4K 已覆盖”。**

---

## 1. 一句话结论

| 项 | 结果 |
|---|---|
| 代码 HEAD | `d819ebf`（docs-only 验收提交；产品/测试代码与 `e1afb33` 同树） |
| clean Release | `scripts\build-release.ps1 -Clean -Package`，**exit 0 / 289.7s 墙钟**（脚本自报 272.9s；全新目录 configure，无 0xC0000409） |
| 单元测试 | **ctest 38/38**（脚本内 70.28s；手动复跑 **38/38 / 69.46s**，wall 69.6s） |
| timeline / mark store | **188 OK / 0 fail**（1.2s）；**281 OK / 0 fail**（0.2s） |
| exe | **3,829,248 B**，SHA256 `823707DD230D4B5B18046A10726389092E4D2776293BCD26D606A10B09DC41E9` |
| dist | **26 文件 / 37,107,016 B / 35.39 MB** |
| stage | **35 文件 / 37,247,982 B / 35.52 MB** |
| setup | **12,671,978 B / 12.08 MB**，SHA256 `C36E85320AEBCFBABBF3AC41010CF674D576F07E4C31408E664B369AED466D3F` |
| 便携 zip | **15,487,271 B / 14.77 MB**，SHA256 `46752202C929B796CC72A27482F3B16297EB6656F958EE387011EE773E5587F2` |
| 安装（每用户） | 静默安装 **exit 0 / 2.34s**；`User privileges: None`、`Administrative install mode: No`、`Install mode root key: HKEY_CURRENT_USER`、`Need to restart Windows? No` |
| 安装版 exe | **3,829,248 B / `823707DD…`，与 dist/build/stage 同 hash** |
| 安装目录 | **37 文件 / 44,250,606 B**（35 负载 + 用户留的 `lapian-3d.exe`/`lapian-3c.exe`；比安装前 +141,824 B，恰好等于 exe 增量） |
| settings | 安装前/后/冒烟后同为 **190 B / `5F5F47E7…`**，mtime 仍 `2026-10-27 10:58:40` |
| 本地冒烟 | banner **74/0**、key **77/0**、dpi 4 档各 **37/0**（problems 空）、drag P4 **94.2/94.4fps** / P6 p95 **27.9/21.3ms**（pass）、loop **65/0** |
| 4096 长跨度 | 10s@2048 3/3 轮 36/0（比 **1.015–1.019**）、20s@4096 3/3 轮 36/0（比 **1.015–1.021**），8/8 长跨度检查全过 |
| 1s/2s 不退化 | 1s@512 三次 **0.982/1.018/1.000**；2s@512 三次 **1.026/1.009/1.026**（均 ≥0.9） |
| 循环 | 本地 **65/0**、在线缓存命中 **32/0**（3 轮 play_enabled=1、end_count=0、帧号前进） |
| 工作区 | 本地 **68/0**（含跨视频）、在线 **32/0**；新进程截图恢复「A=50 B=90（循环关）」 |
| 设置页 UI | 100% 与 200% 各 **149/0、verdict=pass、dialog-fits OK** |
| 回归 | B 720P/1080P **38/0 ×2**、offline **45/0**、quality **blocked**（不写通过） |
| 总体判定 | **分发包已生成、安装可用、安装版核心冒烟通过；不写“发布就绪/T7.7/P14 通过”** |

---

## 2. 发布内容与默认值

### 2.1 提交链（本包产品/测试代码 = `e1afb33` 树，`d819ebf` 只加验收文档）

| 提交 | 内容 |
|---|---|
| `22faadd` | **P1 4096 上限**：`AppSettings` 预览缓存硬上限 2048 → **4096**、默认 512、720P 下限 128 / 1080P/自动下限 256、25% 可用内存夹取；`OnlinePreviewDecoder` 同步上限；`app_settings_test` / `online_preview_decoder_test` 覆盖 4096 |
| `e33b0d6` | **P2 设置页**：设置页“拖动预览”分组显示**可设范围/本机上限（可用内存 25%）/多实例占用说明**，预设保留 128/256/512/1024/2048 + 自定义 4096，默认预设「默认（自动 / 512MiB）」，4096 保存/重载/内存夹取用例 |
| `3c06afc` | **长跨度矩阵（test-only）**：`--drag-seconds` 自动 60×S 目标数、`--long-span-gate` 8 条检查、`long_span_record` 记录 |
| `14407ec` | **长跨度门同目标率（test-only）**：正/反向 samples/span 必须相同，避免“目标率不同”误判 |
| `9e7a6f0` | **L1 循环卡死/空格失效修复**：在线 seek 失败 3 次不再 `Finish()`（不再 `reachedEnd`/禁用播放），失败只关循环 + 提示；结束态 Space 可重播；A=B 有定帧提示且音频暂停 |
| `d14c91c` | **W1 存储层**：`WorkRange`/`WorkRangeStore`（本地 sidecar `<视频文件名>.lapian-workrange.json`、AppData 回退、在线 `<bvid>-<cid>.lapian-workrange.json`）、schema 1、`ValidateWorkRange`、QSaveFile 原子写；`mark_store_test` +109 条 |
| `e1afb33` | **W1 行为**：打开时恢复 A/B（循环仍关）、500ms 防抖、指纹不符/清除、切视频不串；`--loop-test` 新增 24 + 在线 10 检查；`spec.md` §3.8 + F8-A4 |
| `39b27ea` / `6a06dc3` | 计划文档（循环+工作区 / 4096 上限），不改变二进制 |
| `d819ebf` | P4 独立验收（`verdict=pass with gaps`，本包 gap 来源） |

### 2.2 默认值与范围（已发布的产品行为）

| 项 | 值 |
|---|---|
| 预览分辨率 | **自动（推荐）**：跟随源、最高 1080P、不向上放大；可手动 **1080P**（≤1920×1080）/ **720P**（≤1280×720）；不提供 1440P/4K |
| 预览缓存默认 | **512 MiB** |
| 可设范围 | **128–4096 MiB**（预设 128/256/512/1024/2048 + 自定义）；**1080P/自动下限 256MiB、720P 下限 128MiB**，低于下限自动抬升 |
| 上限 | **4096MiB 硬上限**，且同时 ≤“当前可用内存 **25%**”；分配失败降预算/降分辨率并回退 |
| 生效时机 | **下次打开在线会话生效**；拖动中不热切换；只改在线（首次在线 + 缓存命中 DASH），本地 `PreviewDecoder` 未改 |
| 循环 | 循环开关**不落盘**；A=B 定帧提示「A=B 定帧（L 关闭 / PgDn 离开）」且音频暂停 |
| 工作区 | **A/B 按视频记忆**（本地 sidecar / 在线记录目录 `<bvid>-<cid>.lapian-workrange.json`）；打开时恢复 A/B、循环仍为关；全片 `0..last` 记录等价于无记录；指纹（size/mtime）不符不恢复 |
| 旧设置兼容 | 缺 preview 键 → 默认 auto/512；非法值逐项回退；旧 1024/64 显式配置仍可跑；旧版本忽略 `*.lapian-workrange.json` |

### 2.3 不随本包改变

`spec.md` 验收值（v0.4，仅 W1 加 §3.8 一句 + F8-A4）、M1/M2 门、0.9/30fps/5% 阈值、
`online_drag_forward_backward_parity` 判据、`cache_hit_backward_presented_ratio_ge_70pct`、
P6 150ms/premise 分流、缓存 schema、在线下载配额默认 20GB、`installer.iss`/安装器形态均未改。

---

## 3. clean Release 构建与打包

### 3.1 命令与耗时

```powershell
cd 'E:\拉片软件开发'
$env:TMP='E:\拉片软件开发\.deps\tmp'; $env:TEMP=$env:TMP
$sw=[System.Diagnostics.Stopwatch]::StartNew()
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-release.ps1 -Clean -Package *> build\release-close-p5\build-clean.log
$sw.Stop(); "exit=$LASTEXITCODE wall=$($sw.Elapsed.TotalSeconds)"
```

- `-Clean` 删除并全新 configure `build\release`；**未触发**本机 CMake 重配 `0xC0000409`，
  未复用旧目录、未手改 CMake。
- 墙钟 **289.7s**（`BUILD_EXIT=0`）；脚本自报 `Release 构建与打包完成，用时 272.9s`；
  脚本内 ctest **38/38**，`Total Test time (real) = 70.28 sec`。构建耗时比 C9 的 262.6s
  慢 27.1s，属构建机 run 级波动，不作为产品性能结论。
- 手动复跑（clean 产物，`TEMP/TMP` 必须设置）：

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build\release -C Release --output-on-failure
```

结果 **38/38**，ctest 计时 **69.46s**，wall **69.6s**，exit 0
（`build\release-close-p5\ctest-manual.log`）。
`build\release\Release\lapian_timeline_test.exe` 单独跑 **188 OK / 0 FAIL**（1.2s）；
`lapian_mark_store_test.exe` **281 OK / 0 FAIL**（0.2s）。
- 本轮 ctest/冒烟未在仓库根留下 `credentials.dat/pictures/videos`；`git status --porcelain`
  为空（只有已知长路径目录的 stderr 警告）。

### 3.2 产物与校验

| 形态 | 文件 | 体积 | SHA-256 |
|---|---|---:|---|
| 程序负载 | `dist\` | 26 文件 / 37,107,016 B / 35.39 MB | exe：`823707DD230D4B5B18046A10726389092E4D2776293BCD26D606A10B09DC41E9` |
| 分发 stage | `out\package\stage\` | 35 文件 / 37,247,982 B / 35.52 MB | 含 licenses/README/PRIVACY |
| 每用户安装包 | `out\package\Lapian-0.1.0-setup.exe` | 12,671,978 B / 12.08 MB | `C36E85320AEBCFBABBF3AC41010CF674D576F07E4C31408E664B369AED466D3F` |
| 便携 zip | `out\package\Lapian-0.1.0-portable.zip` | 15,487,271 B / 14.77 MB | `46752202C929B796CC72A27482F3B16297EB6656F958EE387011EE773E5587F2` |
| 打包报告 | `out\package\package-report.txt` | — | HEAD `d819ebf`；dist/stage/zip/setup 体积与 hash |

- **build exe == dist exe == stage exe == 安装版 exe**：均 `823707DD…`，3,829,248 B。
- 与 C9 比：exe +141,824 B（3,687,424 → 3,829,248）、dist +141,824 B、stage +141,824 B；
  setup +35,212 B（12,636,766 → 12,671,978）、zip +48,867 B（15,438,404 → 15,487,271），
  属代码增量（4096 + 循环 + 工作区）与压缩差异。
- **与 P4 验收二进制的关系**：P4 的 `80EC7FD4…` 也是 3,829,248 B（同一源码树，`d819ebf`
  仅加验收文档）；本包 clean 重建 hash 不同是 MSVC 链接时间戳/调试目录导致，
  **不是源码差异**；安装版一致性按本包 `823707DD…` 校验。
- `scripts\check-imports.ps1`（脚本内跑）通过：无 QtNetwork/TLS/winhttp/wininet/urlmon；
  仅已知并接受的 `Qt6Core.dll -> WS2_32`。
- 体积 P12：setup 12.08MB、zip 14.77MB、stage 35.52MB，均 ≤100MB（余量 64.48MB）。

---

## 4. 安装验证（每用户、非管理员、Medium）

- setup 先复制到 C: 用户临时目录（`%LOCALAPPDATA%\Temp\lapian-p5-20261027-135600\`）再运行
  （**沿用 C9 做法，避免 E: 盘 SmartScreen**；文件仍是本轮 `out\package` 的新 setup）。
- 静默参数与上次发布一致：

```powershell
Start-Process -Wait -PassThru -FilePath <copy>\Lapian-0.1.0-setup.exe -ArgumentList @(
  '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/NOCLOSEAPPLICATIONS',"/LOG=<copy>\install.log")
```

| 核对项 | 结果 |
|---|---|
| 退出码 / 用时 | **0 / 2.34s**（`Start-Process -Wait -PassThru` + `Stopwatch`） |
| 安装日志 | `User privileges: None`、`Administrative install mode: No`、`Install mode root key: HKEY_CURRENT_USER`、`64-bit install mode: Yes`、`Installation process succeeded`、`Need to restart Windows? No` |
| 安装目录 | `%LOCALAPPDATA%\Programs\拉片软件`；**37 文件 / 44,250,606 B**（安装前 44,108,782 B，+141,824 B = exe 增量） |
| 安装版 exe | 3,829,248 B / `823707DD…`，**与 `dist\lapian.exe` 相同** |
| Start 菜单 | `拉片软件.lnk` → `…\Programs\拉片软件\lapian.exe`；`第三方许可与源码说明.lnk` → `…\Programs\拉片软件\licenses`；`卸载 拉片软件.lnk` → `%LOCALAPPDATA%\拉片软件\uninstaller\unins000.exe` |
| 桌面快捷方式 | `拉片软件.lnk` → `…\Programs\拉片软件\lapian.exe` |
| 用户备份 | `lapian-3d.exe`（`99084199…`，3,495,424 B）与 `lapian-3c.exe`（`5863DD0A…`，3,507,200 B）hash 与安装前一致，未被删除 |
| settings | 安装前/后/冒烟后同为 **190 B / `5F5F47E7…`**，mtime 仍 `2026-10-27 10:58:40`；未读/未打印凭据内容 |
| 旁路目录 | `%LOCALAPPDATA%\Programs\lapian-c8\lapian.exe` `99BC1201…`、`%LOCALAPPDATA%\Programs\lapian-preview\lapian.exe` `80EC7FD4…` 安装前后一致；用户 E: 免安装版未覆盖、未删除、未强杀 |

---

## 5. 安装版冒烟

被测程序一律为 `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`（Medium 完整性），从仓库根运行。
所有运行显式隔离：`--settings-path build\P5-smoke\settings\<tag>`、
`--online-cache-root build\P5-smoke\cache\<tag>`、`--online-pictures`、`--online-records-root`；
夹具先 `Copy-Item` 到 `build\P5-smoke\cache\*`（**不写共享夹具** `build\c2-720p`/`build\pm-b-fresh`）；
GUI 子系统 exe 全部 `Start-Process -Wait -RedirectStandardOutput/-RedirectStandardError` 取日志。
日志在 `build\P5-smoke\logs\`。

### 5.1 本地（安装版）

| 项 | 结果 |
|---|---|
| `--banner-test --file media\测试.mp4` | **74/0**（`banner_summary checks=74 fails=0 dpr=1.00 info_h=22`，10.1s） |
| `--key-test --file media\测试.mp4` | **77/0**（`ok=1`×77、`ok=0`×0，11.1s） |
| `--dpi-test` ×4（`QT_SCALE_FACTOR` 1 / 1.25 / 1.5 / 2 + `--dpi-expect-scale`） | 每档 **37 项 ok=1 / 0 fail / `dpi_problem` 0**（5.0 / 5.0 / 5.1 / 5.1s） |
| `--drag-test --drag-samples 20` | P4 **94.4fps**（189 presents/2.00s）、P6 p95 **21.3ms**、P7 back 0.2 / fwd 9.6、P8 13.0fps/26 帧、P9 p95 31.5ms；全部 pass（同机前一发 P4 94.2 / P6 27.9） |
| `--loop-test --file media\测试.mp4` | **65 checks / 0 fail**（46.4s，含 W1 5 相位；见 §5.4） |

### 5.2 4096 长跨度门（`--online-drag-preview-test --drag-cache-hit --long-span-gate`，1080P cache-hit 夹具）

每轮 **36 checks / 0 fail**（28 既有 + 8 条 long_span 全 `ok=1`）；`preview_w×h=1920×1080`、
`fallback=0`、`exact=1`、`playurl=dash=0`；`backward_samples == forward_samples`。

**10s@2048（3/3 轮）**

| # | 正 presented / fps_active | 反 presented / fps_active | pres 比 | fps 比 | 双方≥30 | peak MiB | frames/limit | 预算/跨度覆盖 | close |
|---|---|---:|---:|---:|---:|---:|---:|---|---:|
| r1 | 583 / 58.67 | 594 / 59.77 | 1.019 | 1.019 | 1 | 1839.1 | 620/690 | 89.9% / 103.3% | 97ms |
| r2 | 584 / 58.78 | 593 / 59.69 | 1.015 | 1.016 | 1 | 1839.1 | 620/690 | 89.9% / 103.3% | 92ms |
| r3 | 584 / 58.79 | 593 / 59.68 | 1.015 | 1.015 | 1 | 1839.1 | 620/690 | 89.9% / 103.3% | 93ms |

**20s@4096（3/3 轮）**

| # | 正 presented / fps_active | 反 presented / fps_active | pres 比 | fps 比 | 双方≥30 | peak MiB | frames/limit | 预算/跨度覆盖 | close |
|---|---|---:|---:|---:|---:|---:|---:|---|---:|
| r1 | 1171 / 58.74 | 1196 / 59.99 | 1.021 | 1.021 | 1 | 3615.9 | 1219/1380 | 88.3% / 101.6% | **233ms** |
| r2 | 1168 / 58.59 | 1192 / 59.79 | 1.021 | 1.020 | 1 | 3615.9 | 1219/1380 | 88.3% / 101.6% | 184ms |
| r3 | 1169 / 58.63 | 1187 / 59.54 | 1.015 | 1.015 | 1 | 3615.9 | 1219/1380 | 88.3% / 101.6% | 190ms |

- 8 条 long_span 检查 = `same_target_rate`、`reverse_presented_ratio_ge_0_9`、
  `reverse_fps_ratio_ge_0_9`、`both_fps_active_ge_30`、`peak_le_config_budget`、`fallback_zero`、
  `exact_landing`、`playurl_dash_zero`；6 轮共 **48/48 ok=1**。
- 峰值 1,928,448,000B（1839.1MiB）≤ 2,164,260,864B（2064MiB）；
  3,791,577,600B（3615.9MiB）≤ 4,311,744,512B（4112MiB），余量 496.1MiB。
- **4GiB 档无 OOM、无崩溃**（全部 exit=0）；`--preview-cache-mb 4096` 真实生效
  （`preview_config cache_mb=4096`、`limit=1380`）。

### 5.3 不退化（1s/2s，`--drag-backward-full`）

| 格 | # | 正 presented / fps | 反 presented / fps | pres 比 | fps 比 | peak MiB | frames/limit | close | checks |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1s@512 | r1 | 56 / 60.02 | 55 / 59.98 | **0.982** | 0.999 | 240.27 | 81/172 | 12ms | 28/0 |
| 1s@512 | r2 | 55 / 58.76 | 56 / 59.83 | 1.018 | 1.018 | 240.27 | 81/172 | 12ms | 28/0 |
| 1s@512 | r3 | 56 / 59.89 | 56 / 59.83 | 1.000 | 0.999 | 240.27 | 81/172 | 13ms | 28/0 |
| 2s@512 | r1 | 114 / 58.91 | 117 / 60.50 | 1.026 | 1.027 | 403.42 | 136/172 | 19ms | 28/0 |
| 2s@512 | r2 | 116 / 59.95 | 117 / 60.53 | 1.009 | 1.010 | 403.42 | 136/172 | 20ms | 28/0 |
| 2s@512 | r3 | 114 / 58.95 | 117 / 60.47 | 1.026 | 1.026 | 403.42 | 136/172 | 20ms | 28/0 |

- 1s 三次 **0.982 / 1.018 / 1.000**，全部 ≥0.9；正向 fps 58.76–60.02，对比 C9 同机
  57.82–58.76 **无 >5% 下降**；2s 三次 **1.009–1.026**，双方 ≥58.91fps。
- 2s 的产品探针仍标 `out_of_parity_gate=1`（硬门窗口只覆盖 1s/60），本文的“≥0.9”是人工判定，
  **不写成产品硬门通过**。

### 5.4 循环（用户场景）

| 场景 | 结果 |
|---|---|
| 本地 `--loop-test --file media\测试.mp4` | **65 checks / 0 fail**（46.4s） |
| 结束态 | `loop_end_probe end_count=1 baseline=0 play_enabled=1 text=播放 status=播放结束（Space 从头播放）` → `space-after-end frame=14 playing=1`（**Space 可重播**） |
| **用户失败形态**（`SetFailLoopSeeksForTest(3)`） | `loop_fail_probe disabled=1 playing=1 play_enabled=1 end_count=1 baseline=1 loop=0 a=200 b=260` → **没有 reachedEnd**；随后 Space 暂停/继续全过 |
| A=B 定帧 | `status=A=B 定帧（L 关闭 / PgDn 离开） · 帧 100（画面保持、音频静音）`、`frozen=1 playing=1 audio_clock=0 audio_state=3` → **音频暂停**；PgDn 离开、Space 回 100、关循环恢复播放全过 |
| 在线预检拦截（gate=false） | `disabled=2 playing=1 play_enabled=1 jumps=4 baseline_jumps=4 end_count=1 status=A 尚未缓存，已关闭循环` → 不做同步 seek、只关循环 |
| 在线缓存命中 `--loop-test --loop-online-cache-hit` | **32 checks / 0 fail**（13.1s）：`online-sample-1/2/3` frame **132 / 127 / 122**、jumps **1→2→3**、`play_enabled=1`、`end_count=0`、`loop_disabled=0`、`playurl=0 dash=0` |

### 5.5 工作区 A/B 按视频记忆

| 场景 | 结果 |
|---|---|
| 本地 `--loop-test` 5 相位 | 无记录 → 全片；保存 100/160 → 重开 `has_record=1 a=100 b=160 loop=0`、状态「已恢复上次工作区 A=100 B=160（循环关）」；连续改 5 次 → `saves delta=1`（105/165）；写错 size → 不恢复；写错 mtime → 不恢复；Shift+L 清除 → 文件不存在、重开无记录 |
| 本地跨视频 `--file-b build\P5-smoke\workrange\video-b.mp4` | **68 checks / 0 fail**（48.4s）；sidecar `video.mp4` = `in=50 out=90`、`video-b.mp4` = `in=30 out=60`，**A/B 不串视频** |
| 跨进程恢复（新进程 `--dpi-test --file build\P5-smoke\workrange\video.mp4`） | 截图 `build\t6.4-dpi-p5-restore-normal.png`：状态栏**「已恢复上次工作区 A=50 B=90（循环关）」**、时间轴头**「工作区 50–90 · 循环关」**、A/B 手柄在 50/90；**循环仍为关** |
| 在线 `--loop-test --loop-online-cache-hit` | **32 checks / 0 fail**；`W1 workrange_online saved a=100 b=160 frames=3314 has_record=1 saves=2 path=…\records\loop-online\BV1BMnv6jE5S-42915922022.lapian-workrange.json online=1`、`playurl=0 dash=0`（保存时 loop=1，证明循环开关不落盘）；新会话 `workrange_online_reload … a=100 b=160 loop=0 has_in=1 has_out=1 playurl=0 dash=0` |
| 记录文件实测 | `{"schema":1,"in":100,"out":160,"kind":"online","id":"BV1BMnv6jE5S-42915922022","size":0,"mtime_ms":0,"frames":3314}`（211 B） |
| 不污染 | 仓库 `media\` 无 `*workrange*` 残留；`%APPDATA%\拉片软件\online\` 仍是原有 2 个 `*.lapian.json`；settings 未变 |

### 5.6 设置页 UI（`--online-cache-manage-test --ui`）

| 档 | checks | failures | verdict | dialog-fits | 截图 |
|---|---:|---:|---|---|---|
| 100% | **149** | **0** | **pass** | `ui-preview-dialog-fits` / `-4096` OK | `build\P5-smoke\logs\ui-100.png` |
| 200%（`QT_SCALE_FACTOR=2`） | **149** | **0** | **pass** | 同上 OK | `build\P5-smoke\logs\ui-200.png` |

- 两档 `ui_preview=pass`；默认预设「默认（自动 / 512MiB）」、4096 保存/重载/内存夹取、
  范围文本（128–4096、1080P/自动 256 下限、本机 25% 上限、多实例叠加）全 `[ OK ]`。

### 5.7 回归（安装版）

| 用例 | 结果 | 关键数字 |
|---|---|---|
| B 720P `--online-quality-switch-test --quality-cache-hit-menu` | **38/0 pass** | 菜单 3 档（64/32/16）、pick 32（852×480，真实取流 23,686,274B）→ 切回 64 `cache_hit=1 playurl=0 dash=0`、PTS/标记保留 |
| B 1080P 同上 | **38/0 pass** | 菜单 **5 档 / enabled_other=4 / pick 112**（`published=4,702,162 / total=48,462,784`，未等完整）→ 切回 80 `cache_hit=1` |
| `--online-offline-test`（workspace） | **45/0 pass** | 缓存命中 0 网络、记录/截图/重开/清缓存断言 |
| `--online-quality-test` | **blocked**（16 项 / 1 fail，exit 1） | `nav_login ok=0`、`nav_vip_status=-1`、`sample_block=login_unavailable`；**不写 42/0 通过** |
| `--online-drag-parity-test`（legacy 64） | 本轮未跑 | C9 **61/0**（P4 未跑该格）；本包未改变该路径，见 §8 残余 |

### 5.8 4096 设置端到端（记录，不是门）

- 隔离 settings 写 `1080p / 4096` 后启动 cache-hit 1s：`preview_config max_side=1920
  cache_mb=4011 frames=1352 effective_cache_bytes=4205838336`；settings 文件仍保留 `4096`。
- 本次冒烟时可用内存约 15.7GiB，**25% 护栏把 4096 夹到 4011MiB**（P4 时 14.7GiB → 3763MiB）。
  这是**既有护栏**，不是回归；4096 真能生效由探针 `--preview-cache-mb 4096` 证明
  （20s 格 `preview_config cache_mb=4096`、`limit=1380`、peak 3615.9MiB）。

---

## 6. P4 独立验收 gap 清单（**原样 12 条，不改写**）

> 以下逐条引自 [P4 独立验收](handoff-2026-10-27-final-acceptance.md) §11；P4 verdict 是
> **`pass with gaps`**。本发布收口没有消除这些 gap，只复跑了核心项。

1. **`--online-quality-test` 被本机凭据阻塞**：`nav -101`、`vip_status=-1`、`cookie_sent=1`、
   `sample_block=login_unavailable`；16 项 / 1 fail、sample `verdict=blocked`、end `verdict=fail`。
   **不写成通过**；补 1080P60/4K 断言需有效 SESSDATA。
2. **40s@4096 关闭释放 355/442ms > 200ms**（计划 §5 口径记为 gap）。容量受限的 record 格，
   不写通过。
3. **40s@4096 覆盖不足**：`span_coverage_pct=57.5% < 80%`（4GiB 只能放 1380/2400 帧）；
   `10s@1024`、`20s@2048` 同样 57.5%（预算受限）。这些都是 record 格，**不宣称覆盖**。
4. **没有 60fps 源**：两个夹具 29.412/30fps；本轮所有“60/120 目标/秒”是拖动目标率，不是源帧率。
5. **4096 设置端到端未落 4096**：验收末尾机器可用约 15.3GiB（产品 `GlobalMemoryStatusEx`
   读到约 14.7GiB，<16GiB），产品按 25% 护栏夹到 **3763MiB**（settings 仍写 4096）。4096 生效由
   test-only `--preview-cache-mb 4096` 探针（`preview_config cache_mb=4096`、`frames=1380`、
   peak 4093.5MiB）与 UI 的 4096 保存/重载用例证明；本项按“既有护栏”记录，不判 fail。
6. **进程 RSS 未系统采集**：§2.4 的 peak 是预览解码器缓存字节（探针口径）；进程工作集只有单点采样
   （4.13/4.34GB）。
7. **跨进程工作区没有程序化 getter**：新进程里 `WorkRangeStateForTest()` 只在 `--loop-test` 内可达，
   而该探针在 36.5s 会先清除记录，无法用于“打开即读”。改用产品自己的窗口截图（状态栏文本 +
   “工作区 A–B”头 + A/B 手柄）与 sidecar JSON 作为证据；**未覆盖**只读目录的真实端到端
   （只读回退由 W1 单测 `lapian_mark_store_test` 覆盖）。
8. **C9 A/B 的口径陷阱**：C9 没有自动 60×S，必须显式 `--drag-samples 60/120`；否则 2s 只有
   30Hz 目标率，比值会掉到 0.935–0.952（本轮已按显式目标数重跑）。
9. **growing 长跨度未跑**：矩阵按计划只做 1080P cache-hit；`long_span_playurl_dash_zero` 对
   growing 只记录。
10. **20s@4096 关闭 160–164ms 比 P3 的 139–149ms 高 11–25ms**（仍 <200ms，只记录）。
11. **未覆盖**：4K/60fps、登录态清晰度、720P 长跨度、多实例并发 4GiB、真实 >2s GOP 保证档外推；
    `--online-quality-test` 的 42/0 矩阵需要有效凭据。
12. **Medium 副本只抽 2 格**（1s + 10s gate），没有复跑完整矩阵；副本 exe 与 build 同 hash。

> 本包新增记录（不改变上述 gap 原文）：20s@4096 关闭本次 184–233ms（r1 **233ms > 200ms**），
> 见 §7；4096 设置端到端本次夹到 4011MiB，见 §5.8。

---

## 7. 与 P4 数字的差异（超过噪声的要指出）

| 项 | P4 验收 | 本轮安装版 | 结论 |
|---|---|---|---|
| 10s@2048 比值 | 1.021–1.023 | **1.015–1.019** | −0.004…−0.008，run 级噪声；仍 ≥0.9 |
| 20s@4096 比值 | 1.013–1.021 | **1.015–1.021** | 一致 |
| 20s@4096 关闭 | 160–164ms | **184 / 190 / 233ms** | **>噪声**；一次 233ms >200ms，只记录、不写通过 |
| 10s@2048 关闭 | 79–80ms | 92–97ms | +13…+18ms，run 级；仍 <200ms |
| 1s@512 比值 | 1.018–1.020 | **0.982 / 1.018 / 1.000** | r1 为发布序列最低，仍 ≥0.9；C9 同机 1.018–1.037 |
| 2s@512 比值 | 1.000–1.028 | **1.009 / 1.026 / 1.026** | 一致 |
| 20s@4096 peak | 3621.9–3624.8MiB（1221/1222 帧） | **3615.9MiB（1219 帧）** | −6…−9MiB，噪声 |
| 10s@2048 peak | 1839.1MiB | 1839.1MiB | 一致 |
| loop 本地/在线 | 65/0、32/0 | 65/0、32/0 | 一致 |
| scrub 本地/在线 | 62/0、64/0 | 62/0、64/0 | 一致 |
| 工作区跨进程 | 截图 + sidecar | 截图 + sidecar | 一致（独立重做） |
| UI checks | 149/0 ×2 | 149/0 ×2 | 一致 |
| drag P4 / P6 | 94.0 / 17.9ms | **94.2–94.4 / 21.3–27.9ms** | P6 高 3.4–10ms，20 样本 p95 波动；均 ≤150ms 且 pass=1 |
| B / offline | 38/0 ×2、45/0 | 38/0 ×2、45/0 | 一致 |
| ctest | 38/38（68.97s） | 38/38（69.46s） | 噪声 |
| 安装版 exe | 80EC7FD4…（3,829,248 B） | **823707DD…（3,829,248 B）** | 同源码树 clean 重建，hash 差异来自链接时间戳，**不是代码差异** |

**超噪声/需指出的**：20s@4096 关闭释放（233/184/190 vs 160–164ms，一次 >200ms）、
P6 p95（21.3–27.9 vs 17.9ms）、1s@512 r1 比值 0.982（序列最低）。三者都不影响硬门。

---

## 8. 残余风险 / 不要宣称

1. **quality 42/0 未复现（blocked）**：本机 SESSDATA 失效；1080P60/4K 档位断言缺证据。
2. **无 60fps 源**：所有“60”是拖动目标率；60fps 源下的预览行为未验证。
3. **未覆盖**：真实 4K/60fps、登录态 1080P60/4K、多实例并发 4GiB、720P 长跨度、
   growing 长跨度（P4 gap 9）、真实 >2s GOP 保证档外推、真实窗口/物理鼠标拖动体感、
   无符号化崩溃 dump、代码签名/干净机复验、Explorer 真双击。
4. **P4 的 40s@4096 记录未复跑**：覆盖 57.5%、峰值 4093.5MiB（余量 18.5）、关闭 355/442ms
   均沿用 P4 证据；本包只跑 10s/20s 门。
5. **进程 RSS 未系统采集**（P4 gap 6）；peak 仍是预览解码器缓存字节。
6. **4096 设置端到端受 25% 护栏**：本次 15.7GiB 可用 → 夹到 4011MiB；settings 保留 4096，
   “设置页选 4096 就一定跑满 4GiB”不成立。
7. **20s@4096 关闭 233ms >200ms**（r1）为本轮新增观察；不写“关闭释放全部 <200ms”。
8. **C9 A/B 口径陷阱仍在**（P4 gap 8）：C9 必须显式 `--drag-samples`；本轮未重跑 C9 对照。
9. **跨进程工作区证据是截图**，无程序化 getter（P4 gap 7）。
10. **Medium 副本只抽 2 格**（P4 gap 12）——本包安装版即 Medium，未另做副本矩阵。
11. **用户体感未收集**：本轮没有新的用户实机反馈；不写“验收通过”。
12. **legacy 1024/64 parity 本轮未重跑**（C9 61/0）；该路径本包未改。

**不要写**：
- 不要写“发布就绪/T7.7/P14 通过”“安装版冒烟全绿”。
- 不要写“quality 42/0 通过”“60fps 源已验证”“4K/60 已覆盖”。
- 不要写“4096 设置端到端已跑满 4GiB”“40s@4096 覆盖已达标”“关闭释放全部 <200ms”。
- 不要把“10s/20s 长跨度门通过”外推成“所有跨度/所有清晰度通过”。
- 不要把 1s 的 0.982 写成“1s 稳定 1.0+”。

---

## 9. 回滚

- **分发回滚**：重装 C9 setup
  `AF32B4C3CB4111C69FC435173B2546BF4AEFAAD9F29461F7A0336A6DBE470E78`
  （12,636,766 B）→ 安装版 exe `588B8D5E…`（3,687,424 B）；回滚后停止分发本包。
- **代码回滚**（按相反顺序；W1 的存储层与行为可分开回退）：

```powershell
git revert e1afb33 d14c91c 9e7a6f0 14407ec 3c06afc e33b0d6 22faadd
# 计划/验收文档提交单独回退：39b27ea 6a06dc3 d819ebf 与本轮 docs 提交
```

- 旧版本忽略 `*.lapian-workrange.json` / `<hash>.workrange.json`，回滚不需要清理记录文件；
  settings 里 4096 在旧版会被校验夹回 2048（不会崩）。
- 回滚后请重跑 `ctest`、安装版 1s/2s 双向、循环本地/在线、工作区恢复。

---

## 10. 复现与证据路径

- 构建日志：`build\release-close-p5\build-clean.log`；手动 ctest：`build\release-close-p5\ctest-manual.log`；
  timeline：`build\release-close-p5\timeline.log`；mark store：`build\release-close-p5\markstore.log`。
- 安装：`build\P5-smoke-preinstall.json`、`%LOCALAPPDATA%\Temp\lapian-p5-20261027-135600\install.log`。
- 冒烟：`build\P5-smoke\logs\local\*`、`build\P5-smoke\logs\online\*`；
  夹具/设置/记录：`build\P5-smoke\{cache,settings,records,pics}\<tag>`；
  跨进程截图：`build\t6.4-dpi-p5-restore-normal.png`；设置页截图：`build\P5-smoke\logs\ui-{100,200}.png`。
- 关键命令骨架（安装版；GUI 探针必须 `Start-Process -Wait -RedirectStandardOutput/-RedirectStandardError`）：

```powershell
cd 'E:\拉片软件开发'
$env:TMP='E:\拉片软件开发\.deps\tmp'; $env:TEMP=$env:TMP
$exe = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"

# 4096 门格（只带 --long-span-gate、不带 --drag-backward-full；目标数自动 60×S）
Start-Process -Wait -FilePath $exe -ArgumentList @(
  '--online-drag-preview-test','--drag-cache-hit','--bvid','BV1BMnv6jE5S','--cid','42915922022',
  '--drag-seconds','20','--preview-resolution','auto','--preview-cache-mb','4096','--long-span-gate',
  '--online-cache-root','build\P5-smoke\cache\g20s-r1',
  '--online-pictures','build\P5-smoke\pics\g20s-r1',
  '--online-records-root','build\P5-smoke\records\g20s-r1',
  '--settings-path','build\P5-smoke\settings\g20s-r1','--timeout-ms','300000') `
  -RedirectStandardOutput 'build\P5-smoke\logs\online\g20s-r1.out.log' `
  -RedirectStandardError  'build\P5-smoke\logs\online\g20s-r1.err.log'

# 1s/2s 不退化：--drag-seconds 1|2 --preview-cache-mb 512 --drag-backward-full（不带 --long-span-gate）

# 循环（本地 / 在线缓存命中）
#   --loop-test --file media\测试.mp4
#   --loop-test --loop-online-cache-hit --bvid BV1BMnv6jE5S --cid 42915922022 --online-cache-root <夹具>

# 工作区跨进程：--loop-test --file build\P5-smoke\workrange\video.mp4 --file-b ...video-b.mp4
#              新进程 --dpi-test --file build\P5-smoke\workrange\video.mp4 --dpi-tag p5-restore

# 设置页 UI（100% / 200%）
$env:QT_SCALE_FACTOR='2'
Start-Process -Wait -FilePath $exe -ArgumentList @(
  '--online-cache-manage-test','--ui',
  '--manage-root','build\P5-smoke\cache\ui-200',
  '--manage-records-root','build\P5-smoke\records\ui-200',
  '--manage-settings-root','build\P5-smoke\settings\ui-200') ...
```

- 坑（沿用）：GUI 子系统 exe 用 `&` 不会等待、也拿不到 exit code；必须
  `Start-Process -Wait -RedirectStandardOutput/-RedirectStandardError`。`--settings-path` 是
  **目录**（内含 `settings.json`）。缓存命中拖动必须 `data.mp4` 完整；1080P 切 112 需要
  `112-7\data.part` 才能续传真下载。ctest 必须 `TEMP/TMP=<repo>\.deps\tmp`（否则本机 11 项假失败）。
  `--online-cache-manage-test --ui` 的结果在 **stdout**（不是 stderr）。
