# 拉片软件 0.1.0 发布收口 — 在线预览清晰度/缓存配置化（含 C8 1s 修复）（2026-10-27）

> **这是内部发布收口记录，不是 T7.7/P14 验收，也不是“发布就绪”。**
> 本轮只做 **clean Release → dist/setup/zip → 每用户静默安装 → 安装版冒烟 → 文档**；
> 未改 `src/`/`tests/` 产品代码、未改任何门/阈值/schema、未改安装器形态、未升级 Qt/FFmpeg/CMake。
>
> **结论先说**：clean 构建、打包、每用户静默安装、安装版基础/打开预热/新功能/回归冒烟**核心项全绿**
> （ctest 38/38、timeline 188/0、banner 74/0、key 77/0、dpi 4 档全空、drag P4 95.0/P6 24.5ms、
> 1s/60 1080P+720P 默认配置双向通过、2s/120 1080P 512 三次 1.009–1.018、P6 保证档 80ms、
> legacy parity 61/0、迁移 1920/512、设置页 137/0×2、B 38/0×2、offline 45/0）。
> **仍有缺口**：`--online-quality-test` 因本机 SESSDATA 失效 **blocked**、无 60fps 源、
> 2s/120 的 256MiB/720P 只记录、P6 非保证档窗口只记录、原生 lap_var 比 0.522 只记录、
> 本地参考 0.873 只记录、ctest TEMP 坑、D5 G4 exit code 未回读、4K/60 与登录态 1080P60 未覆盖；
> 逐条见 §6（**原样 9 条**）。
> **不写“发布就绪/T7.7/P14 通过”“quality 42/0 通过”“2s/120 全部通过”“视觉 1.2× 通过”。**

---

## 1. 一句话结论

| 项 | 结果 |
|---|---|
| 代码 HEAD | `7c7f44c`（docs-only；产品/测试代码与 `0539055` 同树：C8 `87660ef`+`961a19b`、D1–D4 `d9c5fa6`…`0539055`） |
| clean Release | `scripts\build-release.ps1 -Clean -Package`，**exit 0 / 262.6s 墙钟**（脚本自报 247.3s；configure 全新目录，无 0xC0000409） |
| 单元测试 | **ctest 38/38**（脚本内 66.25s；手动复跑 **38/38 / 65.54s**，ctest 计时 65.46s，exit 0） |
| timeline | **188 OK / 0 fail**（单独跑，exit 0，1.16s） |
| exe | **3,687,424 B**，SHA256 `588B8D5E858614A4DB394B589312A80FE58A53C04985C7D5780D50777171E54E` |
| dist | **26 文件 / 36,965,192 B / 35.25 MB** |
| stage | **35 文件 / 37,106,158 B / 35.39 MB** |
| setup | **12,636,766 B / 12.05 MB**，SHA256 `AF32B4C3CB4111C69FC435173B2546BF4AEFAAD9F29461F7A0336A6DBE470E78` |
| 便携 zip | **15,438,404 B / 14.72 MB**，SHA256 `21FE4925BED3172539FB00A37A5BD01FD9891422ADA0917456D26F65DAEB3323` |
| 安装（每用户） | 静默安装 **exit 0 / 2.5s**；`User privileges: None`、`Administrative install mode: No`、`Install mode root key: HKEY_CURRENT_USER`、`Need to restart Windows? No` |
| 安装版 exe | **3,687,424 B / `588B8D5E…`，与 dist/build/stage 同 hash** |
| 安装目录 | **37 文件 / 44,108,782 B**（35 负载 + 用户留的 `lapian-3d.exe`/`lapian-3c.exe`；旧 exe 3,587,584 → 新 3,687,424，+99,840 B 恰好对上） |
| settings | `%APPDATA%\拉片软件\settings.json` 安装前/后/冒烟后同为 **137 B / `0D69C495…`**，mtime 未变 |
| 本地冒烟 | banner **74/0**、key **77/0**、dpi 4 档各 **37/0**（problems 空）、drag P4 **95.0fps** / P6 p95 **24.5ms**、loop exit 0、timeline **188/0** |
| 打开预热 | 1080P 缓存命中 `prewarm_stage_ms=139 first=1`、`stage_show_ms=0`、`wait_ready=88ms`；720P 147/0/80 |
| 1s/60 默认 | 1080P **1920×1080 / 512MiB**：正 55/58.82、反 56/59.96（比 1.018/1.019）；720P **1280×720 / 512MiB**：56/59.96 vs 56/60.02（比 1.000/1.001）；双方 ≥58.7fps、floor30=1 |
| 2s/120 512 | 1080P 三次 1.009 / 1.017 / 1.018（presented 与 fps 双比），双方 ≥58.95fps |
| P6 保证档 | 1080P `--drag-start-frame 150` **release+landing=80ms**（78+2，premise 60 帧/2000ms，≤150） |
| legacy 兼容 | `--preview-max-side 1024 --preview-cache-mb 64` parity **61/0**，`legacy_64mib peak=66,355,200 frames=75 limit=75`，两条配置感知门 `ok=1` |
| 设置迁移 | 复制真实旧 settings（无 preview 键）→ **1920/512**、1920×1080、28/0；原件 hash 未变 |
| 设置页 UI | 100% 与 200% 各 **137/0、verdict=pass、`ui-preview-dialog-fits` OK** |
| B 回归 | 720P **38/0**、1080P **38/0**（1080P 菜单 5 档含 112、112 实际取流后切回 80 `cache_hit=1`） |
| offline | **45/0** |
| quality | **blocked**（`sample_block=login_unavailable`、16 项/1 fail、end `verdict=fail`）——**不写成 42/0 通过** |
| 总体判定 | **分发包已生成、安装可用、安装版核心冒烟通过；不写“发布就绪/T7.7/P14 通过”** |

---

## 2. 发布内容与默认值

### 2.1 提交（本包代码 = `0539055` 树，`7c7f44c` 只加验收文档）

| 提交 | 内容 |
|---|---|
| `87660ef` | C8 test：60Hz 反向拖动度量（同目标率/同跨度） |
| `961a19b` | C8 fix：缓存命中 DASH 预览接到 `OnlinePreviewDecoder`，1s/60 前后 0.93–1.04 |
| `d9c5fa6` | D1 配置核心：`AppSettings` 增加 `preview_resolution`（auto/1080p/720p）与 `preview_cache_mb`（128–2048），纯函数校验/夹取/迁移/保留键 |
| `9751131` | D2 解码器运行时：`OnlinePreviewDecoder` 的 `max_side`/`cache_bytes` 实例化，帧数由预算算；分配失败降 25% 再回退 64MiB/1024 |
| `acea5c5` | D3 设置 UI：设置页“拖动预览”分组（挡位/自定义、估算秒数、下限/上限、“下次打开在线会话生效”） |
| `afc0f95` | 高 DPI 设置对话框适配（200% 下 footer 可达，`dialog-fits`） |
| `0539055` | D4 集成：配置矩阵探针、`preview_config`/`preview_dump` 诊断、两条内存门改**配置感知**并保留 `legacy_64mib` 兼容行 |
| `1676438`/`7c7f44c` | 计划/C8 交接/D5 独立验收文档（不改变二进制内容） |

### 2.2 默认值与范围（已发布的产品行为）

| 项 | 值 |
|---|---|
| 预览分辨率 | **自动（推荐）**：跟随源、最高 1080P、不向上放大；可手动 **1080P**（最高 1920×1080）/ **720P**（最高 1280×720）；不提供 1440P/4K |
| 预览缓存 | 默认 **512 MiB**；五档 **128 / 256 / 512 / 1024 / 2048 MiB**（省内存/均衡/推荐/长拖/高级自定义） |
| 最低有效缓存 | 720P **128MiB**；1080P **256MiB**；低于下限自动抬升 |
| 上限 | **2GiB**，且不超过“当前可用内存 25%”；分配失败降预算/降分辨率并回退 |
| 生效时机 | **下次打开在线会话生效**；拖动中不热切换；只改在线（首次在线 + 缓存命中 DASH），本地 `PreviewDecoder` 未改 |
| 旧设置兼容 | 缺 preview 键 → 默认 auto/512；非法值逐项回退；旧 1024/64 显式配置仍可跑 |

### 2.3 不随本包改变

`spec.md` 验收值、M1/M2 门、0.9/30fps/5% 阈值、`online_drag_forward_backward_parity` 判据、
`cache_hit_backward_presented_ratio_ge_70pct`、P6 150ms/premise 分流、缓存 schema、
在线下载配额默认 20GB、installer.iss/安装器形态均未改。

---

## 3. clean Release 构建与打包

### 3.1 命令与耗时

```powershell
cd 'E:\拉片软件开发'
$env:TMP='E:\拉片软件开发\.deps\tmp'; $env:TEMP=$env:TMP
$logDir='build\release-close-c9'; New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$sw=[System.Diagnostics.Stopwatch]::StartNew()
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-release.ps1 -Clean -Package *> "$logDir\build-clean.log"
$sw.Stop(); "exit=$LASTEXITCODE wall=$($sw.Elapsed.TotalSeconds)"
```

- `-Clean` 删除并全新 configure `build\release`；**未触发**本机 CMake 重配 `0xC0000409`，
  未复用旧目录、未手改 CMake。
- 墙钟 **262.6s**（`BUILD_EXIT=0`）；脚本自报 `Release 构建与打包完成，用时 247.3s`；
  脚本内 ctest **38/38**，`Total Test time (real) = 66.25 sec`。
- 手动复跑（clean 产物）：

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$env:TMP='E:\拉片软件开发\.deps\tmp'; $env:TEMP=$env:TMP
ctest --test-dir build\release -C Release --output-on-failure
```

结果 **38/38**，ctest 计时 **65.46s**，wall **65.54s**，exit 0。
`lapian_timeline_test.exe` 单独跑 **188 OK / 0 FAIL**，exit 0（1.16s）。
- 本轮 ctest 未在仓库根留下 `credentials.dat/pictures/videos`，`git status --porcelain` 为空（只有已知长路径目录的 stderr 警告）。

### 3.2 产物与校验

| 形态 | 文件 | 体积 | SHA-256 |
|---|---|---:|---|
| 程序负载 | `dist\` | 26 文件 / 36,965,192 B / 35.25 MB | exe：`588B8D5E858614A4DB394B589312A80FE58A53C04985C7D5780D50777171E54E` |
| 分发 stage | `out\package\stage\` | 35 文件 / 37,106,158 B / 35.39 MB | 含 licenses/README/PRIVACY |
| 每用户安装包 | `out\package\Lapian-0.1.0-setup.exe` | 12,636,766 B / 12.05 MB | `AF32B4C3CB4111C69FC435173B2546BF4AEFAAD9F29461F7A0336A6DBE470E78` |
| 便携 zip | `out\package\Lapian-0.1.0-portable.zip` | 15,438,404 B / 14.72 MB | `21FE4925BED3172539FB00A37A5BD01FD9891422ADA0917456D26F65DAEB3323` |
| 打包报告 | `out\package\package-report.txt` | — | HEAD `7c7f44c`；dist/stage/zip/setup 体积与 hash |

- **build exe == dist exe == stage exe == 安装版 exe**：均 `588B8D5E…`，3,687,424 B。
- 上一版（C7）exe `FEF8CE1E…`（3,587,584 B）、setup `D180A3BD…`、zip `DE6113C5…`；
  本版 setup/zip 体积分别 +27,713 B / +40,236 B，属代码增量与压缩差异，以本文为准。
- `scripts\check-imports.ps1`（脚本内跑）通过：无 QtNetwork/TLS/winhttp/wininet/urlmon；
  仅已知并接受的 `Qt6Core.dll -> WS2_32`。
- 体积 P12：setup 12.05MB、zip 14.72MB、stage 35.39MB，均 ≤100MB（余量 64.75MB）。

---

## 4. 安装验证（每用户、非管理员、Medium）

- setup 先复制到 C: 用户临时目录（`%LOCALAPPDATA%\Temp\lapian-c9-<stamp>\`）再运行
  （**沿用 C7 做法，避免 E: 盘 SmartScreen**；文件仍是本轮 `out\package` 的新 setup）。
- 静默参数与上次发布一致：

```powershell
Start-Process -Wait -PassThru -FilePath <copy>\Lapian-0.1.0-setup.exe -ArgumentList @(
  '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/NOCLOSEAPPLICATIONS',"/LOG=<copy>\install.log")
```

| 核对项 | 结果 |
|---|---|
| 退出码 / 用时 | **0 / 2.5s**（`Start-Process -Wait -PassThru` + `Stopwatch`） |
| 安装日志 | `User privileges: None`、`Administrative install mode: No`、`Install mode root key: HKEY_CURRENT_USER`、`64-bit install mode: Yes`、`Installation process succeeded`、`Need to restart Windows? No` |
| 安装目录 | `%LOCALAPPDATA%\Programs\拉片软件`；**37 文件 / 44,108,782 B**（比安装前 +99,840 B，等于 exe 增量） |
| 安装版 exe | 3,687,424 B / `588B8D5E…`，**与 `dist\lapian.exe` 相同** |
| Start 菜单 | `拉片软件.lnk` → `…\Programs\拉片软件\lapian.exe`；`第三方许可与源码说明.lnk` → `…\Programs\拉片软件\licenses`；`卸载 拉片软件.lnk` → `%LOCALAPPDATA%\拉片软件\uninstaller\unins000.exe` |
| 桌面快捷方式 | `拉片软件.lnk` → `…\Programs\拉片软件\lapian.exe` |
| 用户备份 | `lapian-3d.exe`（`99084199…`，3,495,424 B）与 `lapian-3c.exe`（`5863DD0A…`，3,507,200 B）hash 与安装前一致，未被删除 |
| settings | 安装前/后/冒烟后同为 **137 B / `0D69C4957838F7DF5228EC9D2BBB0F617B2F4FA0CC2E4585D0AA9EC2010A3D1C`**，mtime 仍 `2026-10-26 21:27:25`；未读/未打印凭据内容 |
| 旁路目录 | `%LOCALAPPDATA%\Programs\lapian-c8\lapian.exe` hash `99BC1201…` 安装前后一致；用户 E: 免安装版未覆盖、未删除、未强杀 |

---

## 5. 安装版冒烟

被测程序一律为 `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`（Medium 完整性），从仓库根运行。
所有运行显式隔离：`--settings-path build\c9-smoke\settings\<tag>`、
`--online-cache-root build\c9-smoke\cache\<tag>`、`--online-pictures`、`--online-records-root`；
夹具先 `Copy-Item` 到 `build\c9-smoke\cache\*`（**不写共享夹具** `build\c2-720p`/`build\pm-b-fresh`）；
GUI 子系统 exe 全部 `Start-Process -Wait -RedirectStandardOutput/-RedirectStandardError` 取日志。
日志在 `build\c9-smoke\logs\`，汇总 `build\c9-smoke\online-runs.json`/`online-extra.json`。

### 5.1 本地（安装版）

| 项 | 结果 |
|---|---|
| `--banner-test --file media\测试.mp4` | **74/0**（`banner_summary checks=74 fails=0`，11.1s） |
| `--key-test --file media\测试.mp4` | **77/0**（`key_check ok=1`×77、`ok=0`×0，11.1s） |
| `--dpi-test` ×4（`QT_SCALE_FACTOR` 1 / 1.25 / 1.5 / 2 + `--dpi-expect-scale`） | 每档 **37 项 ok=1 / 0 fail / `dpi_problem` 0**（5.1 / 5.1 / 5.0 / 4.0s） |
| `--drag-test --drag-samples 20` | P4 **95.0fps**（190 presents/2.00s）、P6 p95 **24.5ms**（pass）、P7 back 0.0 / fwd 15.1、P8 12.9fps/26 帧、P9 p95 30.9ms；全部 pass |
| `--loop-test --file media\测试.mp4` | **exit 0**，17 个 `T4.2 loop_test` 步骤（loop/frozen/refreeze/loop-off-resume/paused），无崩溃 |
| `lapian_timeline_test.exe` | **188 `[ OK ]` / 0 FAIL**，exit 0（2s） |

### 5.2 打开预热（安装版，`LAPIAN_ONLINE_LOG=1`，cache-hit 打开）

| 用例 | 关键行 |
|---|---|
| 1080P 缓存命中（`BV1BMnv6jE5S`/`42915922022`，`--quality-cache-hit-menu`） | `online_prewarm_stage_ms=139 first=1 stage=video`；`online_ready stage_show_ms=0 prewarm_stage_ms=139`；`wait_ready pump_ms=88 total_ms=88 ready=1`；随后切档 `prewarm=0 first=0` |
| 720P 缓存命中（`BV1cW411b7Yh`/`28925848`） | `online_prewarm_stage_ms=147 first=1`；`stage_show_ms=0`；`wait_ready=80` |
| A 拖动探针 | 探针自预热 `online_drag_preview_warmup`，日志里 `prewarm=0 first=0` 属预期；打开预热证据看本节 B 日志 |

### 5.3 新功能：1s/60 默认配置（`auto/512`，每格独立夹具，3 次取中位）

| 路径 | 实际 config | 正 presented / fps_active | 反 presented / fps_active | pres 比 | fps 比 | floor30 | peak | frames/limit | w×h | exact | 网络/硬解 |
|---|---|---|---:|---:|---|---:|---:|---:|---|---|---|---|---|
| cache-hit 1080P **默认** | **1920 / 512** | 55 / **58.82** | 56 / **59.96** | **1.018** | **1.019** | 1 | 240.27MiB | 81/172 | **1920×1080** | 1/1 | `playurl=dash=0`、`hw_requested=hw_active=0` |
| cache-hit 720P **默认** | **1280 / 512** | 56 / **59.96** | 56 / **60.02** | **1.000** | **1.001** | 1 | 106.79MiB | 81/388 | **1280×720** | 1/1 | 同上 |

- 每格 **28 checks / 0 failures / verdict=pass**、`preview_fallback=0`；三次分布
  1080P 比 1.018/1.018/1.018、720P 比 0.982–1.000（最紧一次 0.982，仍远高于 0.9）。
- `stage_show_ms=0`、`wait_ready=76–94ms`；`preview_config max_side=… cache_mb=512 frames=…` 与源分辨率一致（**默认值 auto/512 生效**）。
- 峰值 ≤ 配置+16MiB：240.27 ≤ 528、106.79 ≤ 528。

### 5.4 2s/120（`auto/512`，3 次，人工按计划 §6.2 判定）

| run | 正 presented / fps_active | 反 presented / fps_active | pres 比 | fps 比 | peak | frames/limit |
|---|---|---:|---:|---|---:|---:|---|
| r1 | 115 / 59.40 | 116 / 60.01 | 1.009 | 1.010 | 403.42MiB | 136/172 |
| r2 | 115 / 59.46 | 117 / 60.50 | 1.017 | 1.017 | 403.42MiB | 136/172 |
| r3 | 114 / 58.95 | 116 / 59.98 | 1.018 | 1.018 | 403.42MiB | 136/172 |

- 3/3 轮 presented 与 fps 双比都 ≥0.9，双方 ≥58.95fps；product 探针对 2s 打
  `out_of_parity_gate=1`（硬门窗口只覆盖 1s/60），**本文的“≥0.9”是人工判定，不写成产品硬门通过**。
- 2s 的 P6 `release+landing=202/217/200ms` 属 `out_of_premise`（关键帧距离 4500ms 级），**只记录**。

### 5.5 P6 保证档（cache-hit 1080P，`--drag-start-frame 150`）

- `cache_hit_release_landing_le_150ms ok=1 detail=premise=le_2000ms release_ms=78 landing_ms=2 release+landing=80 keyframe_distance_frames=60 keyframe_distance_ms=2000.0`；
- **80ms ≤150ms**，落在保证档 premise 分支；峰值 447.90MiB ≤528、frames 151/172；
- 该格 `--drag-start-frame` 显式，反向默认 30 目标（28/30）属 `out_of_parity_gate=1`，**不当作双向门**。

### 5.6 legacy 1024/64 兼容 parity（growing，安装版）

- `--online-drag-parity-test --preview-max-side 1024 --preview-cache-mb 64 --m1-throttle-bps 262144`
  → **61 checks / 0 failures / verdict=pass**（139.9s，真实节流下载）。
- 两条配置感知门 `ok=1`：
  `legacy_64mib peak=66355200 frames=75 limit=75 cache_mb=64 max_side=1024`；
  `online_drag_parity_cache_peak_le_config_budget ok=1 detail=peak=66355200 budget_mb=64 limit=83886080 frames=75`；
  `online_drag_parity_cache_frames_le_config_limit ok=1 detail=frames=75 limit=75 cache_mb=64 max_side=1024`。

### 5.7 设置迁移（旧 settings 缺 preview 键）

- 把用户真实 `%APPDATA%\拉片软件\settings.json` **复制**到 `build\c9-smoke\migrate\settings.json`
  （`--settings-path` 指向该目录；仅 137B、无 `preview` 键，未改原件）。
- 1080P 1s/60 结果：`preview_config max_side=1920 cache_mb=512 frames=172`、
  `preview_w=1920 preview_h=1080`、**28/0**；原 settings hash 前后一致（`0D69C495…`）。

### 5.8 设置页 UI（`--online-cache-manage-test --ui`）

| 档 | checks | failures | verdict | `ui-preview-dialog-fits` | 窗口尺寸（可用区） | scroll |
|---|---:|---:|---|---|---|---:|
| 100% | **137** | **0** | **pass** | **OK** | 640×582（1920×1032） | 0 |
| 200%（`QT_SCALE_FACTOR=2`） | **137** | **0** | **pass** | **OK** | 640×464（960×516） | 1 |

- 两档 `ui_preview=pass`；`ui-preview-default-auto-512`、`ui-preview-loads-settings`、
  `ui-preview-no-hot-switch`、`ui-preview-720p-no-lift` 均 `[ OK ]`；200% 下 footer 可达（滚动区生效）。

### 5.9 回归（安装版）

| 用例 | 结果 | 关键数字 |
|---|---|---|
| B 720P `--online-quality-switch-test --quality-cache-hit-menu` | **38/0 pass** | 打开 `cache_hit=1 playurl=0 dash=0 quality_requests=0` qn=64；切 32→64 回 `cache_hit=1`、`marks=1` |
| B 1080P 同上 | **38/0 pass** | 80 打开 0 网络；菜单 **5 档 / enabled_other=4 / pick 112**；112 实际取流（`video_total=47,168,494B`，未等完整下载）后切回 80 `cache_hit=1 playurl=0 dash=0`、PTS 映射、marks=1 |
| `--online-offline-test`（workspace，3.2MB faststart） | **45/0 pass** | cache-hit 0 网络、记录/截图/重开/清缓存断言 |
| `--online-quality-test` | **blocked**（16 项 / 1 fail，end `verdict=fail`） | `nav_login ok=0`、`nav_vip_status=-1`、`sample_block=login_unavailable`；**不写 42/0 通过** |
| `--online-drag-parity-test`（legacy 64） | **61/0 pass** | 见 §5.6 |

---

## 6. D5 独立验收 gap 清单（**原样 9 条，不改写**）

> 以下逐条引自 [D5 独立验收](handoff-2026-10-27-preview-config-acceptance.md) §12；
> D5 的 verdict 是 **`pass with gaps`**。本发布收口没有消除这些 gap，只是复跑了核心项。

1. **`--online-quality-test`（42/0 档位矩阵）被本机凭据阻塞**：`nav -101`、
   `vip_status=-1`、`cookie_sent=1`、`sample_block=login_unavailable`；
   本次 16 项 / 1 fail、sample `verdict=blocked`、end `verdict=fail`。
   **这是已知 gap，不写成通过**；需要有效 SESSDATA 才能补 1080P60/4K 档位断言。
2. **没有 60fps 源**：两个夹具 29.412/30fps，计划 §6.1 的“60fps 源单独记录”无法覆盖；
   本次所有“60 目标/秒”是拖动目标率，不是源帧率。
3. **2s/120 的 256MiB 与 720P 是 record 格**：256 反向 0.929/0.948/0.956、720P 1.000–1.027，
   按计划**不写“通过”**；2s 判定本身也属人工口径（产品探针 `out_of_parity_gate=1`）。
4. **P6 非保证档窗口只记录**：默认 1s 窗口 82–110ms、2s 窗口 149–202ms 都是
   `out_of_premise=1`（关键帧距离 2500–4522ms），不代表保证档失败。
5. **视觉原生 lap_var 比 0.522 只记录**：跨分辨率不可比；判据用的是同显示尺寸 6.398×
   （任务书口径）。如果后续有人拿 0.522 当“清晰度没生效”，是误读。
6. **本地 720P 参考反向比 0.873 只记录**（非在线门；C8 0.929、D4 0.897）。
7. **ctest 的环境依赖**：必须 `TEMP/TMP=<repo>\.deps\tmp`（与项目构建脚本一致）；
   默认 `%TEMP%` 下本机 `QTemporaryDir` 失效会让 11 项假失败（§10.1）。
   这不是产品失败，但**任何不设 TEMP 的复跑都会看到“38 项里 11 项失败”**，需要按此区分。
8. **G4 进程 exit code 未回读**（脚本 `WaitForExit` 后 `ExitCode` 为空），
   证据用日志 `checks=28 failures=0 verdict=pass` + “中途改 settings 后配置未变”。
9. **未覆盖**：真实 4K/60fps、登录态 1080P60/4K、多实例并发 2GiB、长 GOP 超过 2s 的
   保证档外推；growing 只用匿名 qn=64/80 实际跑通，未验证登录态差异。

---

## 7. 与 D5 数字的差异（超过噪声的要指出）

D5 数字见 [acceptance](handoff-2026-10-27-preview-config-acceptance.md)。本轮安装版（3 次中位）：

| 格 | D5 正 / 反（fps_active） | 本轮正 / 反（fps_active） | Δfps | 结论 |
|---|---|---|---|---|
| cache-hit 1080P 1s `auto/512` | 55/58.95、56/60.02 | 55/58.82、56/59.96 | −0.13 / −0.06 | 噪声 |
| cache-hit 720P 1s `auto/512` | 56/59.83、56/59.96 | 56/59.96、56/60.02 | +0.13 / +0.06 | 噪声 |
| 2s/120 1080P `512` | 正 112–115/57.88–59.46、反 115–117/59.43–60.50（D5 范围） | 115/59.40、116/60.01 | 在范围内 | 噪声 |
| P6 1080P `start150` | 80/83ms（中位 81.5） | **80ms** | −1.5ms | 噪声，两次都 ≤150 |
| legacy 1024/64 parity | D4 `peak=66,355,200 frames=75` | 同值 | 0 | 一致 |
| drag P6 p95 | 25.0ms | 24.5ms | −0.5ms | 噪声 |
| ctest | 38/38（65.22s） | 38/38（65.54s） | +0.32s | 噪声 |
| 设置页 200% | 640×464 / scroll=1 | 640×464 / scroll=1 | 0 | 一致 |

- **没有方向性退化**：1s 正向 58.82/59.96、2s 512 三次 1.009–1.018、P6 80ms、峰值全部在
  配置+16MiB 内；与 D5 的差异都在 run 级噪声内。
- 安装耗时 2.5s（C7 2.1s，+0.4s）属安装器/文件系统波动，不作为性能结论。

---

## 8. 残余风险 / 不要宣称

1. **quality 42/0 未复现（blocked）**：本机 SESSDATA 失效；1080P60/4K 档位断言缺证据。
2. **无 60fps 源**：所有“60”是拖动目标率；60fps 源下的预览行为未验证。
3. **2s/120 的 256MiB/720P 只记录**；2s 本身是人工口径（产品探针 `out_of_parity_gate=1`）。
4. **P6 非保证档窗口只记录**（1s 默认窗口 82–110ms、2s 149–202ms，`out_of_premise`）。
5. **视觉 lap_var 原生比 0.522 只记录**：跨分辨率不可比；同显示尺寸 6.398× 才是判据。
6. **本地 720P 参考反向 0.873** 只记录（非在线门）。
7. **ctest 必须设 `TEMP/TMP`**，否则本机 11 项假失败（`QTemporaryDir` 环境问题）。
8. **D5 G4 exit code 未回读**（旧证据缺口，本轮未重跑热切换）。
9. **未覆盖**：真实 4K/60fps、登录态 1080P60/4K、多实例并发 2GiB、长 GOP >2s 的保证档外推、
   真实窗口/物理鼠标/多 DPI 的拖动体感、无符号化崩溃 dump、代码签名/干净机复验。
10. **用户体感未收集**：本轮没有新的用户实机反馈；不写“验收通过”。

**不要写**：
- 不要写“发布就绪/T7.7/P14 通过”“安装版冒烟全绿”。
- 不要写“quality 42/0 通过”“2s/120 全部通过”“视觉 lap_var 1.2× 通过”。
- 不要写“1080P 缓存命中 P6 保证档外通过”“60fps 源已验证”“4K/60 已覆盖”。
- 不要写“本地拖动反向已达标”（0.873 只记录）。
- 不要把“安装版 1s/60 通过”外推成“所有在线拖动形状通过”。

---

## 9. 回滚

- **分发回滚**：重装 C7 setup
  `D180A3BDD485DBD0F055FEAF5734BA263B958D29DC015EFC9E1EF93C1E262A7A`
  （12,609,053 B）→ 安装版 exe `FEF8CE1E…`（3,587,584 B）；回滚后停止分发本包。
- **代码回滚**（D1–D4 + 高 DPI 适配，按相反顺序；D1–D4 可单独 revert）：

```powershell
git revert afc0f95 0539055 acea5c5 9751131 d9c5fa6
# 若要连 C8 一起回退：再 git revert 961a19b 87660ef
# 文档提交单独回退（本轮 docs/AGENTS 提交）
```

- 兼容性：只回退 D1–D4 会保留 C8 1s 修复；只回退 C8 会保留配置化默认（auto/512），
  1s/60 反向仍应 ≥0.9。回滚后请重跑 `ctest` 与安装版 1s/60 双向。
- 旧 64MiB/1024 显式配置仍可用（`--preview-max-side 1024 --preview-cache-mb 64`），
  不需要回滚设置文件；旧 settings 无 preview 键时默认 auto/512。

---

## 10. 复现与证据路径

- 构建日志：`build\release-close-c9\build-clean.log`；手动 ctest：`build\release-close-c9\ctest-manual.log`；
  timeline：`build\release-close-c9\timeline.log`；构建汇总：`build\release-close-c9\build-summary.txt`。
- 安装：`build\c9-smoke\pre-install.json`、`post-install.json`、`%LOCALAPPDATA%\Temp\lapian-c9-<stamp>\install.log`。
- 冒烟：`build\c9-smoke\logs\local\*`、`build\c9-smoke\logs\online\*`；
  汇总 `build\c9-smoke\local-summary.tsv`、`online-runs.json`、`online-extra.json`。
- 设置迁移：`build\c9-smoke\migrate\settings.json`（真实 settings 的只读复制）。
- 关键命令骨架（安装版；GUI 探针必须 `Start-Process -Wait -RedirectStandardOutput/-RedirectStandardError`）：

```powershell
cd 'E:\拉片软件开发'
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$exe = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"

# 1s/60 默认（1080P / 720P）：不要传 --preview-resolution/--preview-cache-mb
Start-Process -Wait -FilePath $exe -ArgumentList @(
  '--online-drag-preview-test','--drag-cache-hit','--bvid','BV1BMnv6jE5S','--cid','42915922022',
  '--drag-seconds','1','--drag-samples','60','--drag-backward-full',
  '--online-cache-root','build\c9-smoke\cache\ch1080-1s-r1',
  '--online-pictures','build\c9-smoke\pics\ch1080-1s-r1',
  '--online-records-root','build\c9-smoke\records\ch1080-1s-r1',
  '--settings-path','build\c9-smoke\settings\ch1080-1s-r1','--timeout-ms','150000') `
  -RedirectStandardOutput 'build\c9-smoke\logs\online\ch1080-1s-r1.out.log' `
  -RedirectStandardError  'build\c9-smoke\logs\online\ch1080-1s-r1.err.log'

# legacy 1024/64 parity（growing）
Start-Process -Wait -FilePath $exe -ArgumentList @(
  '--online-drag-parity-test','--drag-seconds','1','--drag-samples','60','--drag-backward-full',
  '--m1-throttle-bps','262144','--preview-max-side','1024','--preview-cache-mb','64',
  '--bvid','BV1BMnv6jE5S','--cid','42915922022',
  '--online-cache-root','build\c9-smoke\cache\legacy-parity',
  '--online-pictures','build\c9-smoke\pics\legacy-parity',
  '--online-records-root','build\c9-smoke\records\legacy-parity',
  '--settings-path','build\c9-smoke\settings\legacy-parity','--timeout-ms','300000') `
  -RedirectStandardOutput 'build\c9-smoke\logs\online\legacy-parity.out.log' `
  -RedirectStandardError  'build\c9-smoke\logs\online\legacy-parity.err.log'

# 设置页 UI（100% / 200%）
$env:QT_SCALE_FACTOR='2'
Start-Process -Wait -FilePath $exe -ArgumentList @(
  '--online-cache-manage-test','--ui','--manage-root','build\c9-smoke\cache\ui-2',
  '--manage-records-root','build\c9-smoke\records\ui-2',
  '--manage-settings-root','build\c9-smoke\settings\ui-2') `
  -RedirectStandardOutput 'build\c9-smoke\logs\online\ui-2.out.log' `
  -RedirectStandardError  'build\c9-smoke\logs\online\ui-2.err.log'
```

- 坑（沿用）：GUI 子系统 exe 用 `&` 不会等待、也拿不到 exit code；必须
  `Start-Process -Wait -RedirectStandardOutput/-RedirectStandardError`。缓存命中拖动必须
  `data.mp4` 完整；1080P 切 112 前缓存根只能有 `112-7\data.part`（会续传真下载）。
  ctest 必须 `TEMP/TMP=<repo>\.deps\tmp`。legacy parity 是 growing + 真实下载（本轮约 140s），
  不要用缓存命中的速度预期它。
