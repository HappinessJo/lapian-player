# 交接：在线预览清晰度/缓存配置 — D5 独立验收（2026-10-27）

> **verdict：`pass with gaps`**（全部硬门通过；有环境阻塞与按计划只记录的格子，逐条列在 §12）。
>
> - 验收对象：`master` HEAD `0539055`（`test(online): preview config matrix and bidirectional gates`，
>   含 D1–D4 全部代码；D4 自报见 [preview-config-matrix](perf/preview-config-matrix.md)）。
> - 本 agent **只**新增本文件；未改产品/测试代码、门、阈值、spec、CMake、installer；未打包/安装；
>   未动 `lapian-c8`、C: 安装版、用户 E: 免安装版。
> - 独立复跑：**没有复用 D4 的脚本/日志/夹具**。每个 cell 先 `Copy-Item` 到
>   `build\d5\cells\<tag>\cache`，独立 `--settings-path/--online-cache-root/--online-pictures/
>   --online-records-root`；GUI 子系统 exe 全部 `Start-Process -Wait -RedirectStandardOutput/
>   -RedirectStandardError` 取日志；未动共享夹具。
> - 本轮在线探针 **63 次**（A/B/C/D/E/F/G + 2 次 smoke + 5 次 Medium 副本），
>   每次都有 `checks=28 failures=0`（cache-hit）或 `checks=36 failures=0`（growing），
>   日志全量落在 `build\d5\logs`，汇总 `build\d5\summary.json`。

---

## 0. 一句话数字

- 默认 `auto/512` 1s/60：1080P 正 55–56 / 58.82–60.02，反 55–56 / 60.02–60.09
  （比 1.000–1.018）；720P 正 55–56 / 58.89–60.02，反 56 / 59.89–60.02；峰值 240.27 / 106.79MiB。
- 2s/120 1080P：`512` 比 1.009–1.045、`1024` 1.008–1.026、`2048` 1.008–1.020（全部每轮 ≥0.9）。
- P6 保证档：720P `start145` **47/38ms**、1080P `start150` **80/83ms**（均 ≤150，premise）。
- 2GiB 档 1s：3/3 不崩、不 OOM、`preview_fallback=0`、peak 240.27MiB ≤ 2064MiB。
- 清晰度：同显示尺寸（1024 bilinear→1920）lap_var 比 **6.398×**（判据 ≥1.2）；
  原生比 0.522 只记录（不可跨分辨率比，D4 已说明）。
- 设置端到端：缺 preview 键 → 1920/512；`720p/128` → 1280/128（w×h 1280×720）；
  `1080p/1024` → 1920/1024；运行中改 settings **不热切换**（反向仍 1280/128）。
- 设置页 UI：100% 与 200% 各 **137/0、verdict=pass、dialog-fits OK**；200% footer `保存/关闭` 可达。
- 回归：ctest **38/38**、timeline **188/0**、key **77/0**、banner **74/0**、dpi 4×**37/0**
  （problems 空）、drag P4 **95.4fps** / P6 p95 **25.0ms**、B **38/0 ×2**、offline **45/0**；
  `--online-quality-test` **blocked**（SESSDATA 失效，16 项/1 fail，未写成通过）。

判定：**pass with gaps**——没有达到 fail 条件的项；gap 是环境（凭据）与计划内只记录/未覆盖项。

---

## 1. 基线与环境（可复现性）

| 项 | 值 |
|---|---|
| 仓库 / 分支 / HEAD | `E:\拉片软件开发` / `master` / `0539055`（`git status --porcelain` 干净） |
| 增量构建 | `cmake --build build\release --config Release`，**exit 0，14.5s**；`lapian.exe` 未被重编（mtime 仍 `2026-10-27 02:36:34`） |
| 二进制 | `build\release\bin\lapian.exe` **3,687,424B**，SHA256 `1E6F781C90FC86A24F4EB35DC3723D3223C2A5D8AE24508AECF1A3B7DB744DD7`（与 D4 报告的 `1E6F781C…` 一致） |
| Medium 副本 | `dist\*` → `%LOCALAPPDATA%\Programs\lapian-preview`，再用上面的 exe 覆盖；副本 SHA256 相同；抽 5 个场景复验（§2.2） |
| 720P 夹具（只读） | `build\c2-720p\cache`：`BV1cW411b7Yh/28925848`，qn=64，1280×720@29.412，`data.mp4` 46,714,595B complete |
| 1080P 夹具（只读） | `build\pm-b-fresh\cache`：`BV1BMnv6jE5S/42915922022`，qn=80，1920×1080@30，`data.mp4` 31,057,770B complete |
| 夹具拷贝 | 每 cell 每次运行先复制到 `build\d5\cells\<tag>\cache`；growing 用空 cache 根（`--drag-cache-hit` 缺失时产品会清空该根） |
| 隔离 | 每 cell 独立 `cache/pics/records/settings` 子目录；`--settings-path` 指向 `<cell>\settings`（**未碰 `%APPDATA%\拉片软件`**，其 settings.json 在验收前后 mtime/内容一致） |
| 网络/凭据 | `--online-http-selfcheck`：`http_status=200`、192ms、`cookie_sent=1`、**`vip_status=-1`、`-101`**（SESSDATA 失效，`login_unavailable`）；growing 仍可用：720P `actual_qn=64`、1080P `actual_qn=80`（匿名） |
| 源帧率 | 两个夹具分别 29.412 / 30fps，**没有 60fps 源**（计划 §6.1 要求明确写缺口） |
| 机器内存 | 可用物理内存约 16.5GiB（验收时 17,322,324KB free）→ 25% 上限≈4.0GiB > 2048，故本次 2GiB 不会触发“可用内存 25%”夹取；该分支由单测覆盖 |
| TEMP 注意 | 本项目 `ctest` 需 `TEMP/TMP=<repo>\.deps\tmp`（与 `scripts\build-release.ps1` 的 `Initialize-BuildTemp` 一致）。用默认 `%TEMP%` 时 `QTemporaryDir::isValid()` 在本机为 false，11 个测试假失败（§10.1）——与产品无关，已复现并区分 |

### 1.1 采样方法

- 1s/60 每 cell **3 次**（A/B/C 全部，含 record 格），2s 门格 **3 次**、record 格 **3 次**；
  P6 每 cell **2 次**；F 每档 **2 次**；G 每个场景 **1 次**（G4 为 1 次带中途改文件）；
  Medium 抽 5 个场景各 1 次。
- 每格给**全量分布**（不是只报最好一次）；flaky 时保留全部尝试。
- 统计口径：`online_drag_preview_pass` 的 presented / fps_total / fps_active；
  `online_drag_forward_backward_parity` 的 presented_ratio / fps_ratio / fps_floor_30；
  峰值取 `preview_peak`（字节）；`preview_config` 行取实际生效的 max_side/cache_mb/frames。
- 判定按计划 §6.2/§6.3 与任务书：反向 presented 与 fps_active 都 ≥0.9×正向、双方 ≥30fps、
  正向不低于 C8 基线 5%、峰值 ≤ 配置+16MiB、`preview_fallback=0`、w×h、exact、`stage_show_ms=0`、
  cache-hit `playurl=dash=0`。

---

## 2. A. 默认配置（`--preview-resolution auto --preview-cache-mb 512`）1s/60 双向

命令骨架（其余 cell 只改 tag/夹具/分辨率缓存参数）：

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
Start-Process -Wait -FilePath build\release\bin\lapian.exe -ArgumentList @(
  '--online-drag-preview-test','--drag-cache-hit','--bvid','BV1BMnv6jE5S','--cid','42915922022',
  '--drag-seconds','1','--drag-samples','60','--drag-backward-full',
  '--preview-resolution','auto','--preview-cache-mb','512',
  '--online-cache-root','build\d5\cells\<tag>\cache',
  '--online-pictures','build\d5\cells\<tag>\pics',
  '--online-records-root','build\d5\cells\<tag>\records',
  '--settings-path','build\d5\cells\<tag>\settings','--timeout-ms','150000') `
  -RedirectStandardOutput build\d5\logs\<tag>.out.log -RedirectStandardError build\d5\logs\<tag>.err.log
```

### 2.1 4 个 cell × 3 次（全量）

| cell / run | config | 正 presented / fps_active | 反 presented / fps_active | pres 比 | fps 比 | peak MiB | frames/limit | exact | stage_show | ui_gap |
|---|---|---:|---:|---:|---:|---:|---:|---|---:|---:|
| ch1080-512 r1 | 1920/512 | 56 / 60.02 | 56 / 60.09 | 1.000 | 1.001 | 240.27 | 81/172 | 1/1 | 0 | 113 |
| ch1080-512 r2 | 1920/512 | 55 / 58.82 | 56 / 60.02 | 1.018 | 1.020 | 240.27 | 81/172 | 1/1 | 0 | 91 |
| ch1080-512 r3 | 1920/512 | 55 / 58.95 | 55 / 60.04 | 1.000 | 1.019 | 240.27 | 81/172 | 1/1 | 0 | 109 |
| ch720-512 r1 | 1280/512 | 56 / 60.02 | 56 / 59.96 | 1.000 | 0.999 | 106.79 | 81/388 | 1/1 | 0 | 85 |
| ch720-512 r2 | 1280/512 | 55 / 58.89 | 56 / 60.02 | 1.018 | 1.019 | 106.79 | 81/388 | 1/1 | 0 | 97 |
| ch720-512 r3 | 1280/512 | 56 / 59.83 | 56 / 59.89 | 1.000 | 1.001 | 106.79 | 81/388 | 1/1 | 0 | 85 |
| gr720-512 r1 | 1280/512 | 56 / 59.89 | 56 / 59.96 | 1.000 | 1.001 | 106.79 | 81/388 | 1/1 | 0 | 152 |
| gr720-512 r2 | 1280/512 | 56 / 59.89 | 56 / 59.89 | 1.000 | 1.000 | 106.79 | 81/388 | 1/1 | 0 | 162 |
| gr720-512 r3 | 1280/512 | 56 / 60.02 | 56 / 60.02 | 1.000 | 1.000 | 106.79 | 81/388 | 1/1 | 0 | 169 |
| gr1080-512 r1 | 1920/512 | 55 / 58.89 | 56 / 60.09 | 1.018 | 1.020 | 240.27 | 81/172 | 1/1 | 0 | 164 |
| gr1080-512 r2 | 1920/512 | 55 / 58.89 | 56 / 59.89 | 1.018 | 1.017 | 240.27 | 81/172 | 1/1 | 0 | 166 |
| gr1080-512 r3 | 1920/512 | 55 / 58.95 | 56 / 59.96 | 1.018 | 1.017 | 240.27 | 81/172 | 1/1 | 0 | 161 |

- 4 格全部满足“反向 presented ≥0.9×正向、反向 fps_active ≥0.9×正向、双方 ≥30fps”；
  A 组最紧的一次是 fps 比 **0.999**（720P r1）与 presented 比 1.000，离 0.9 很远。
- `preview_config max_side=1920 cache_mb=512`（1080P 源）/ `1280 cache_mb=512`（720P 源）；
  `preview_w×h` = 1920×1080 / 1280×720，全部 3/3。
- cache-hit：`cache_hit_no_playurl ok=1 playurl=0`、`cache_hit_no_dash ok=1 dash=0`（3/3），
  `online_drag_preview_pipeline_online ... hw_requested=0 hw_active=0`（3/3，软解）；
  `stage_show_ms=0`（全部）。
- 正向 vs C8 基线（5% 下限）：1080P 58.95 vs 57.75 → **+2.08%**；720P 59.83 vs 58.95 →
  **+1.49%**；growing 720P 59.89 vs 58.76 → **+1.92%**；growing 1080P 58.89 vs 59.83 → **−1.57%**。
  无一项接近 −5%。
- 每格 `checks=28 failures=0`（cache-hit）/`checks=36 failures=0`（growing），`preview_fallback=0`。

### 2.2 Medium 副本抽验（`%LOCALAPPDATA%\Programs\lapian-preview`）

| 场景 | 结果 |
|---|---|
| cache-hit 1080P `auto/512` 1s | 1920/512，正 55 / 58.89，反 56 / 60.02，peak 240.27，比 1.018/1.019，28/0 |
| cache-hit 720P `auto/512` 1s | 1280/512，正 56 / 59.96，反 56 / 60.02，peak 106.79，比 1.000/1.001，28/0 |
| cache-hit 1080P `auto/512` 2s/120 | 正 113 / 58.43，反 116 / 59.95，peak 403.42，比 1.027/1.026，28/0 |
| P6 1080P `start150` | `release+landing=82ms`、premise（2000ms）、28/0 |
| G1 缺 preview 键 settings | 1920/512、1920×1080、28/0 |

Medium 与 build 目录同 hash、数字在 run 级波动内一致，没有“只有 build 目录能过”。

---

## 3. B. 1s/60 挡位矩阵（每格 3 次）

| # | cell | config | 正 presented / fps_active | 反 presented / fps_active | pres 比 | fps 比 | peak MiB | frames/limit | fallback | w×h | 门 |
|---|---|---|---:|---:|---|---:|---:|---|---:|---|---|
| 1 | 1080P `auto/256` | 1920/256 | 55 / 58.82–58.89 | 56 / 59.83–60.02 | 1.018 | 1.017–1.019 | 240.27 | 81/86 | 0 | 1920×1080 | **pass** |
| 2 | 1080P `auto/1024` | 1920/1024 | 55 / 58.76–58.82 | 56 / 59.96–60.09 | 1.018 | 1.019–1.023 | 240.27 | 81/345 | 0 | 1920×1080 | **pass** |
| 3 | 1080P `auto/2048` | 1920/2048 | 55 / 58.89 | 55–56 / 59.83–60.11 | 1.000–1.018 | 1.016–1.021 | 240.27 | 81/690 | 0 | 1920×1080 | **pass** |
| 4 | 720P `720p/128` | 1280/128 | 55–56 / 58.89–60.02 | 56 / 59.89–59.96 | 1.000–1.018 | 0.998–1.018 | 106.79 | 81/97 | 0 | 1280×720 | **pass** |
| 5 | 720P `auto/512` | 1280/512 | 见 §2.1（同一 3 次） | | | | 106.79 | 81/388 | 0 | 1280×720 | **pass** |

- 每格 3/3 次 `online_drag_forward_backward_parity ok=1`、`verdict=pass`、`preview_fallback=0`；
  `peak ≤ 配置+16MiB`：256 档 240.27 ≤ 272（余量 31.7MiB）、1024 档 240.27 ≤ 1040、
  2048 档 240.27 ≤ 2064、128 档 106.79 ≤ 144。
- 256 档 `frames/limit=81/86` 与 D4 一致；1s 峰值与配置无关（60 帧跨度+预取≈81 帧）。

---

## 4. C. 2s/120（`--drag-seconds 2 --drag-samples 120`）

> 产品探针对 2s/120 打 `out_of_parity_gate=1`（硬门窗口只覆盖 1s/60），2s 的“≥0.9”是
> 按计划 §6.2 的人工判定，**不把 record 格写成通过**。

### 4.1 门格（判定 ≥0.9）

| cell | config | 正 presented / fps_active | 反 presented / fps_active | pres 比（3 次） | fps 比（3 次） | peak MiB | frames/limit | 判定 |
|---|---|---|---:|---:|---|---:|---:|---|
| 1080P `auto/512` | 1920/512 | 112–115 / 57.88–59.46 | 115–117 / 59.43–60.50 | 1.017 / 1.045 / 1.009 | 1.017 / 1.045 / 1.009 | 403.42 | 136/172 | **过目标** |
| 1080P `auto/1024` | 1920/1024 | 113–116 / 58.37–59.95 | 115–117 / 59.49–60.47 | 1.026 / 1.009 / 1.018 | 1.026 / 1.008 / 1.019 | 403.42 | 136/345 | **过目标** |
| 1080P `auto/2048` | 1920/2048 | 114–115 / 58.85–59.46 | 116–117 / 59.92–60.40 | 1.009 / 1.018 / 1.017 | 1.008 / 1.020 / 1.017 | 403.42 | 136/690 | **过目标** |

- 每格 **3/3 轮都 ≥0.9**（不是挑最好一次）；门格双方 fps_active 都 ≥57.88；`fallback=0`。

### 4.2 record 格（只记录，不写通过）

| cell | config | 正 presented / fps_active | 反 presented / fps_active | pres 比（3 次） | fps 比（3 次） | peak MiB | frames/limit |
|---|---|---|---:|---:|---|---:|---:|
| 1080P `auto/256` | 1920/256 | 114–115 / 58.88–59.49 | **107–109 / 55.30–56.30** | 0.930 / 0.939 / 0.956 | 0.929 / 0.948 / 0.956 | 255.10 | 86/86 |
| 720P `auto/512` | 1280/512 | 113–116 / 58.98–59.89 | 116 / 59.89–59.92 | 1.000 / 1.027 / 1.000 | 1.000 / 1.016 / 1.001 | 179.30 | 136/388 |

- 256 档反向 107 帧是“预算 86 帧 < 跨度 120 帧”的预期残差，**不写成通过**；
  720P 2s 按计划同样只记录。
- 2s 每格 `playurl/dash=0`、`stage_show_ms=0`、`exact=1/1`，P6 落在 `out_of_premise=1`
  （目标距关键帧 4500/4522ms），release+landing 149–202ms **只记录**，不是保证档。

---

## 5. D. P6 保证档（cache-hit，`auto/512`，`--drag-start-frame`）

| 样本 | start | 第 1 次 | 第 2 次 | premise | peak MiB | frames/limit | 正 / 反 | 判定 |
|---|---:|---:|---:|---|---:|---:|---|---|
| 720P | 145 | **44+3=47ms** | **36+2=38ms** | `premise=le_2000ms`，55 帧/1870ms | 271.58 | 206/388 | 57/60.96 vs 56/59.89（0.982） | **pass** |
| 1080P | 150 | **77+3=80ms** | **80+3=83ms** | `premise=le_2000ms`，60 帧/2000ms | 344.09 / 367.82 | 116/172、124/172 | 55/58.89–58.95 vs 56/60.09–59.96 | **pass** |

- 两次都在 `cache_hit_release_landing_le_150ms ok=1` 的 **premise 分支**（不是 `out_of_premise`），
  全部 ≤150ms；峰值 ≤528MiB（512+16）。
- 对照：默认 1s 窗口（end=75，关键帧距离 2500/2550ms）release+landing 82–110ms 属
  `out_of_premise=1`，只记录；2s 窗口同理（§4）。
- `start_frame_explicit=1` 使这两格 `online_drag_forward_backward_parity` 只记录（比值仍 0.982/1.018）。

---

## 6. E. 内存 / 回退 / D1 纯函数边界

### 6.1 峰值 ≤ 配置 + 16MiB（全量）

| 格 | 配置 MiB | peak 字节 | peak MiB | 上限（配置+16） | 余量 | frames/limit |
|---|---:|---:|---:|---:|---:|---|
| 1080P `256` 1s ×3 | 256 | 251,942,400 | 240.27 | 272.0 | 31.7 | 81/86 |
| 1080P `512` 1s ×3 | 512 | 251,942,400 | 240.27 | 528.0 | 287.7 | 81/172 |
| 1080P `1024` 1s ×3 | 1024 | 251,942,400 | 240.27 | 1040.0 | 799.7 | 81/345 |
| 1080P `2048` 1s ×3 | 2048 | 251,942,400 | 240.27 | 2064.0 | 1823.7 | 81/690 |
| 1080P `256` 2s ×3 | 256 | 267,494,400 | 255.10 | 272.0 | 16.9 | 86/86 |
| 1080P `512` 2s ×3 | 512 | 423,014,400 | 403.42 | 528.0 | 124.6 | 136/172 |
| 1080P `1024` 2s ×3 | 1024 | 423,014,400 | 403.42 | 1040.0 | 636.6 | 136/345 |
| 1080P `2048` 2s ×3 | 2048 | 423,014,400 | 403.42 | 2064.0 | 1660.6 | 136/690 |
| 720P `128` 1s ×3 | 128 | 111,974,400 | 106.79 | 144.0 | 37.2 | 81/97 |
| 720P `512` 1s ×3 | 512 | 111,974,400 | 106.79 | 528.0 | 421.2 | 81/388 |
| 720P `512` 2s ×3 | 512 | 188,006,400 | 179.30 | 528.0 | 348.7 | 136/388 |
| growing 720P `512` 1s ×3 | 512 | 111,974,400 | 106.79 | 528.0 | 421.2 | 81/388 |
| growing 1080P `512` 1s ×3 | 512 | 251,942,400 | 240.27 | 528.0 | 287.7 | 81/172 |
| P6 720P `512` ×2 | 512 | 284,774,400 | 271.58 | 528.0 | 256.4 | 206/388 |
| P6 1080P `512` ×2 | 512 | 360,806,400 / 385,689,600 | 344.09 / 367.82 | 528.0 | 183.9 / 160.2 | 116/172、124/172 |
| 设置 `720p/128`（G2） | 128 | 117,504,000 | 112.06 | 144.0 | 32.0 | 85/97 |
| 设置 `1080p/100→256`（E） | 256 | 264,384,000 | 252.14 | 272.0 | **19.9** | 85/86 |

- 1s 1080P 峰值与配置无关（60 帧跨度+预取≈81 帧）；2s 需要 136 帧（403.42MiB），512 刚好放得下，
  256 只能放 86 帧导致反向掉（§4.2）。
- **2GiB 档**（`auto/2048`）1s 3 次：不 OOM、不崩、`preview_fallback=0`、`frames=81/690`、
  peak 240.27MiB ≤ 2064MiB；2s 3 次同样 `fallback=0`、peak 403.42MiB。
- 所有 63 次探针的 `preview_fallback` 都是 0；没有触发降预算/降分辨率回退。

### 6.2 D1 纯函数边界（只读单测结论 + 探针现象核对，未改代码）

`lapian_app_settings_test`（`ctest` 内一项，TEMP 正常时全过）包含 D1 新增/相关的断言：
`preview-clamp-memory-quarter`、`preview-clamp-memory-below-min`、`preview-clamp-memory-unknown`、
`preview-clamp-null-limited-pointer`、`preview-clamp-720p-min`、`preview-min-mb-by-max-side`、
`preview-min-mb-by-resolution`、`preview-resolve-auto-4k-source`、
`preview-resolve-auto-720p-source-cache-bytes`、`preview-resolve-720p-downscales-1080p-source`、
`preview-resolve-1080p-native`、`preview-resolve-1080p-source-limited`、
`preview-resolve-unknown-source`、`preview-resolve-memory-limited`、`preview-resolve-lifted-to-min`、
`preview-parse-key-case-insensitive`、`preview-parse-key-invalid-fallback`、
`preview-parse-key-custom-fallback`、`preview-merge-preserves-preview-keys`、
`preview-key-canonical`、`preview-store-save-ok`、`preview-store-save-invalid-cache`、
`serialize-clamp-high/low`、`parse-clamp-*` 等，全部 `[ OK ]`。

探针现象核对（写 settings 文件、无 CLI 覆盖，`preview_config` 实测）：

| 设置（settings.json） | 期望（D1 规则） | 实测 |
|---|---|---|
| `1080p` + `preview_cache_mb=100` | 抬到 1080P 下限 256 | **256**（frames 86） |
| `1080p` + `4096` | 夹到上限 2048 | **2048**（frames 690） |
| `720p` + `64` | 720P 下限 128，长边 1280 | **1280/128**（frames 97，输出 1280×720） |
| `auto` + `100` | 保守下限 256 | **256** |
| `preview_resolution="4k"`、`preview_cache_mb="big"` | 非法逐项回退默认 | **auto/512**（frames 172） |
| `720p` + `128` | 不主动上调 | **1280/128**（§8 G2） |

> 可用内存 25% 夹取本次不会出现（16.5GiB 的 25%≈4.1GiB > 2048），只在单测
> `preview-clamp-memory-quarter` / `preview-resolve-memory-limited` 覆盖；没有为了凑现象去改内存。

---

## 7. F. 清晰度（自己 dump，同一帧 f74）

做法：cache-hit 1080P，`--drag-start-frame 14`（目标 74），两档各 2 次，dump 各 2 份；
1024 档 `--preview-max-side 1024 --preview-cache-mb 64`，1920 档 `--preview-resolution auto
--preview-cache-mb 512`；`--preview-dump-dir build\d5\visual\f{1024,1920}-rN`。

| 档位 | config | dump 帧 | 尺寸 | `preview_sharpness lap_var`（2 次） |
|---|---|---:|---|---:|
| 旧 1024/64 | max_side=1024 cache_mb=64 frames=75 | f74（target_delta=0） | 1024×576 | **211.655 / 211.655** |
| 新 auto/512 | max_side=1920 cache_mb=512 frames=172 | f74（target_delta=0） | 1920×1080 | **110.547 / 110.547** |

- **原生比值**（只记录，跨分辨率不可比）：110.547 / 211.655 = **0.522**。
- **同显示尺寸判据**（任务书）：1024×576 帧 bilinear 升到 1920×1080 后
  lap_var = **17.278**，与 1920 原生 110.547 比 = **6.398× ≥ 1.2 → 通过**。
- 旁证：1920 原生 box 降到 1024 的 lap_var = 189.914，与 SWS 降采样的 211.655 同量级，
  说明 0.522 的差异主要来自“缩放”，不是“配置没生效”。
- 证据 PNG（`build\d5\visual\`）：`frame_1024x576.png`、`frame_1920x1080.png`、
  `frame_1024_upscaled_to_1920.png`、`crop_native_1920.png`、`crop_legacy_upscaled.png`、
  `side_by_side_1920_vs_upscaled1024.png`、`text_crop_1920_vs_upscaled.png`、`text_crop_2x.png`。
- **亲眼复核**（1080P 显示尺寸下 1:1 对比）：1920 原生的发丝、耳机/耳廓轮廓、纸杯杯口、
  台灯光晕边缘、右上角“忆雪猫/bilibili”水印都锐利；1024 升采样版同位置边缘发虚、发丝糊成块、
  右下“23:59”数字边缘明显发软并有轻微重影。**结论：1080P 档在同显示尺寸下确实更清晰**，
  不是“配置没生效”。
- 精确帧/截图路径未受影响：每格 `exact_frame=1 exact_pts=1`，dump 只读预览缓存。

---

## 8. G. 设置端到端（隔离 `--settings-path`，不碰 `%APPDATA%`）

settings 文件（每场景独立目录）：

```json
{"schema":1,"volume":46,"waveform":false,"cache_root":"D:/d5-cache","cache_quota_gb":20}
```

| # | 场景 | settings 变更 | 期望 | 实测 |
|---|---|---|---|---|
| G1 | 迁移（**不含** preview 键） | 上面的 JSON | 默认 auto/512 → 1920/512 | `max_side=1920 cache_mb=512 frames=172`，w×h 1920×1080，正 55/58.89 反 56/59.96，28/0 |
| G2 | 生效（720P/128） | `"preview_resolution":"720p","preview_cache_mb":128` | 1280/128 | `max_side=1280 cache_mb=128 frames=97`，`preview_w=1280 preview_h=720`，peak 112.06≤144，正 54/57.75 反 55/60.11，28/0 |
| G3 | 再写 1080P/1024（新进程） | `"1080p"/1024` | 1920/1024 | `max_side=1920 cache_mb=1024 frames=345`，w×h 1920×1080，28/0 |
| G4 | 不热切换（同进程中途改文件） | 起进程时 720p/128，看到正向 `online_drag_preview_pass` 后把文件改成 1080p/2048 | 反向仍是旧配置 | 反向仍 1280/128、w×h 1280×720、peak 108.11MiB；最终 `preview_config`/result 仍 1280/128；文件落盘为 1080p/2048（下次会话才生效） |

- G1–G3 运行前先写 settings、运行后文件未被产品改写（v1 键 volume/waveform/cache_root/quota 保留）；
  `preview_config` 只出现一次，说明一次会话只解析一次。
- G4 方法：`Start-Process`（不 -Wait）→ 100ms 轮询 err 日志，命中 `label=forward` 后立即
  `WriteAllText` 改 settings（时间戳 02:52:37.61）→ 等进程结束。反向 pass 行仍在旧配置下，
  没有第二次 `preview_config/hw` 重新配置；这是“不热切换”的直接证据。
  （脚本回读 exit code 为空，判定以日志 `checks=28 failures=0 verdict=pass` 为准。）

---

## 9. H. 设置页 UI（`--online-cache-manage-test --ui`）

命令：隔离 `--manage-root/--manage-records-root/--manage-settings-root`，并
`LAPIAN_UI_DIALOG_SHOT=build\d5\visual\dialog-{100,200}.png`；200% 档 `QT_SCALE_FACTOR=2`。

| 档 | checks | failures | verdict | `ui-preview-dialog-fits` | 窗口尺寸（可用区） | scroll | 截图 |
|---|---:|---:|---|---|---|---:|---|
| 100% | **137** | **0** | **pass** | OK | 640×582（1920×1032） | 0 | `dialog-100.png` |
| 200% | **137** | **0** | **pass** | OK | 640×464（960×516） | 1 | `dialog-200.png` |

- 两档 `ui_preview=pass`；`ui-preview-default-auto-512`、`ui-preview-loads-settings`、
  `ui-preview-no-hot-switch` 均 `[ OK ]`。
- **亲眼确认 footer 可达**：200% 截图底部能看到“保存/关闭”，滚动区在内容超屏时生效但 footer 固定；
  另查看了 `build\d3b\dialog-200.png`（同为 640×464，footer 可见）。
- 200% 的“拖动预览”分组完整：`预览分辨率 自动（推荐）`、`预览缓存 高级自定义`、
  `自定义缓存 512 MiB`、`当前约可缓存 5.8 秒（1080P / 30fps）`、`恢复推荐设置`、
  `修改后下次打开在线会话生效`。

---

## 10. I. 回归（D5 改动后同一二进制）

| 项 | 结果 | 备注 |
|---|---|---|
| `ctest` | **38/38 pass**（65.22s） | 需 `TEMP/TMP=<repo>\.deps\tmp`，见 §10.1 |
| `lapian_timeline_test` | **188 OK / 0 fail** | 含 fit/2h 波形重绘 perf |
| `--key-test --file media\测试.mp4` | **77 / 0**（11.1s） | 隔离 settings（默认 volume=100/waveform=on） |
| `--banner-test --banner-tag d5` | **74 / 0**（10.1s） | |
| `--dpi-test` 100/125/150/200% | 每档 **37 项 / 0 fail**，`dpi_problem` **各 0** | `QT_SCALE_FACTOR` + `--dpi-expect-scale` |
| `--drag-test --drag-samples 20` | P4 **95.4fps**、P6 p95 **25.0ms**（pass）、P7 back 0.0 / fwd 12.9、P8 12.8、P9 34.8 | 与 D4 的 95.6/21.6 同量级 |
| B 720P `--online-quality-switch-test --quality-cache-hit-menu` | **38 / 0** | 64→32→64，`cache_hit=1 playurl=0 dash=0`，PTS 保留 |
| B 1080P 同上 | **38 / 0** | 80→**112（真实下载 47,168,494B）**→80，`cache_hit=1` |
| `--online-offline-test`（workspace） | **45 / 0** | 3.2MB faststart，cache-hit 0 网络 |
| `--online-quality-test` | **blocked**（16 项 / 1 fail，end `verdict=fail`） | `nav_login ok=0`、`-101`、`vip_status=-1`、`sample_block=login_unavailable`；**未写成通过** |
| 本地 720P 参考 `--local-drag-parity-test` | 9/0，正 58.94 / 反 51.49（**0.873**） | 非在线门，只记录（D4 0.897） |

### 10.1 ctest 的 TEMP 环境坑（区分真假失败）

- 第一次用默认 `%TEMP%`（`C:\Users\…\AppData\Local\Temp`）跑 `ctest`：**11 项失败**
  （mark_store、app_settings、online_cache、cache_manage、growing_source、online_fetch、
  online_records、online_qr、online_fault、snapshot_naming、media_probe），且 `online_fetch`
  耗时 71.5s。
- 逐个复现：`lapian_app_settings_test` 只有 `[FAIL] temp-dir-valid` / `[FAIL] preview-temp-dir-valid`
  ——`QTemporaryDir::isValid()==false`；把 `TEMP/TMP` 指到仓库内目录后同一 exe **全部通过**。
  根因是**本机默认用户 TEMP 下 QTemporaryDir 失效**（环境），不是 D1/D2/D4 代码。
- 按 `scripts\build-release.ps1` 的 `Initialize-BuildTemp`（`TEMP=<repo>\.deps\tmp`）复跑：
  **38/38 pass**，65.22s。
- 该假失败运行还在仓库根留下 `credentials.dat`、`pictures/`、`videos/`（无效临时目录被当成
  相对路径），已删除；验收结束时 `git status --porcelain` 为空。

---

## 11. 与 D4 自报数字的差异（超过噪声的要指出）

D4 自报见 [preview-config-matrix](perf/preview-config-matrix.md)。逐格对照（3 次中位）：

| 格 | D4 正 / 反（fps_active） | D5 正 / 反（fps_active） | Δfps（正/反） | 结论 |
|---|---|---|---|---|
| ch1080 `auto/512` 1s | 55/59.01、56/60.02 | 55/58.95、56/60.04 | −0.06 / +0.02 | 噪声 |
| ch720 `auto/512` 1s | 56/60.02、56/59.96 | 56/59.83、56/59.96 | −0.19 / 0 | 噪声 |
| growing 720 `512` | 56/59.83、56/60.04 | 56/59.89、56/59.96 | +0.06 / −0.08 | 噪声 |
| growing 1080 `512` | 55/59.01、56/59.89 | 55/58.89、56/59.96 | −0.12 / +0.07 | 噪声 |
| ch1080 `256` 1s | 55/58.82、56/60.02 | 55/58.82、56/59.89 | 0 / −0.13 | 噪声 |
| ch1080 `1024` 1s | 55/58.89、56/59.83 | 55/58.76、56/60.02 | −0.13 / +0.19 | 噪声 |
| ch1080 `2048` 1s | 55/59.01、56/59.83 | 55/58.89、55–56/59.96 | −0.12 / +0.13 | 噪声 |
| ch720 `720p/128` | 56/59.96、56/59.96 | 55–56/59.96、56/59.96 | 0 / 0 | 一致 |
| 2s `512` | 114/58.91、116/59.95 | 114/58.91、116/60.50 | 0 / **+0.55** | 噪声（<1fps） |
| 2s `1024` | 115/59.49、116/59.95 | 114/58.95、116/60.40 | −0.54 / **+0.45** | 噪声（<1fps） |
| 2s `2048` | 115/59.34、116/59.92 | 115/59.37、116/60.01 | +0.03 / +0.09 | 噪声 |
| 2s `256`（record） | 114/58.88、109/56.33（0.956） | 114/58.91、**107/55.85（0.939）** | ≈0 / −0.48 | 反向 presented −2；仍 record，不写通过 |
| 2s 720 `512`（record） | 115/59.49、114/59.44 | 116/59.89、116/59.92 | +0.40 / +0.48 | 噪声 |
| P6 720 `start145` | 40/47ms | 47/38ms | 中位 43.5 vs 42.5 | 一致 |
| P6 1080 `start150` | 79/61ms | **80/83ms** | 中位 70 vs 81.5（**+11.5ms**） | 两次都 ≤150；D4 自身两次差 18ms，属 run 级波动；**不构成 fail**，但这是本次最大单项偏移 |
| P6 峰值 | 282.79 / 373.75MiB | 271.58 / 344.09–367.82MiB | −11…−30MiB | 缓存填充时机差异，均在 528 内 |
| drag P6 p95 | 21.6ms | 25.0ms | +3.4ms | 均 pass（阈值 150ms） |
| 本地参考反向比 | 0.897（52.58/58.59） | 0.873（51.49/58.94） | −0.024 | 非在线门，只记录 |
| F 同帧 lap_var | 211.655 / 110.547 / 17.278 / 6.398 / 189.914 | **完全相同**（3 位小数） | 0 | 确定性解码 |
| ctest | 38/38（65.1s） | 38/38（65.22s） | — | 一致（D5 需 TEMP 覆盖，见 §10.1） |

**没有任何方向上的退化**：1s 正向全部在 C8 基线 +1.5%…+2.1%（growing 1080P −1.57%），
2s 512+ 全部 ≥1.008，P6 全部 ≤150ms，2GiB 全绿。与 D4 的差异都在 run 级噪声内
（最大单项是 P6 1080P 的 +11.5ms 中位偏移，但两次样本都 ≤83ms 且 D4 自己的样本就跨 18ms）。

---

## 12. Gap 清单（逐条，不掩饰）

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

## 13. 判定

| 计划 §6.2 硬门 | 结果 |
|---|---|
| 1. 每个 1s/60 挡位：反正 presented/fps ≥0.9、双方 ≥30fps | **pass**（A 4 格 + B 4 格 + 720P/512 复用，全部 3/3 轮） |
| 2. 512/1G/2G：2s/120 1080P ≥0.9 | **pass**（1.017 / 1.018 / 1.017，每格 3/3） |
| 3. 正向相对 C8 不降 >5% | **pass**（+2.08% / +1.49% / +1.92% / −1.57%） |
| 4. P6 保证档 ≤150ms；`stage_show_ms=0`；0 网络 | **pass**（47/38、80/83ms；全部 stage_show=0；cache-hit playurl=dash=0） |
| 5. peak ≤ 配置+16MiB；2GiB 回退不崩 | **pass**（§6.1；fallback=0） |
| 6. 设置保存/重启/下次会话生效；非法组合自动修正 | **pass**（G1–G4 + E 边界） |
| 7. 视觉同显示尺寸 ≥1.2×；精确帧不受影响 | **pass**（6.398×；exact 1/1） |
| 8. 回归 ctest 38/38、key 77/0、banner 74/0、dpi 4 档、drag、B 38/0、quality 42/0、offline 45/0、timeline 188/0 | **pass**（除 quality **blocked** — 已知 gap） |

**结论：`pass with gaps`。** 没有 1s 反向 <0.9、没有任一方 <30fps、没有正向降 >5%、
没有 2s 512+ <0.9、没有 2GiB 崩/OOM、没有设置不生效、没有同显示尺寸 <1.2×、没有 P6 >150ms、
没有内存超 配置+16MiB；不存在“两边一样低”“只测前进”“把 2s/256 写成通过”的情况。

---

## 14. 复现命令与原始证据

### 14.1 原始日志

- 全部探针日志：`build\d5\logs\<tag>.err.log`（**80 个 err 日志**；其中 63 次是
  A/B/C/D/E/F/G/Medium 在线探针（含 2 次 smoke），其余为回归）。
- 汇总：`build\d5\summary.json`（`build\d5\collect.py` 生成，含每 run 的 config/正反/presented/
  fps/比值/峰值/frames/fallback/w×h/exact/stage_show/P6/verbatim 失败行）。
- 计算表：`build\d5\analyze.py` 输出每格中位与判定；`build\d5\visual_analyze.py` 输出清晰度比值。
- 视觉证据：`build\d5\visual\`（NV12 dump + PNG + 同显示尺寸/裁切对照）。
- 夹具/设置/记录：`build\d5\cells\<tag>\{cache,pics,records,settings}\`。

### 14.2 复现命令

```powershell
# 0) 构建并确认二进制
$env:TEMP = 'E:\拉片软件开发\.deps\tmp'; $env:TMP = $env:TEMP
& third_party\cmake\3.31.6\cmake\data\bin\cmake.exe --build build\release --config Release
(Get-FileHash build\release\bin\lapian.exe -Algorithm SHA256).Hash   # 1E6F781C…

# 1) 单格（A/B/C/D 都用它；tag/秒数/配置改参数）
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
Start-Process -Wait -FilePath build\release\bin\lapian.exe -ArgumentList @(
  '--online-drag-preview-test','--drag-cache-hit','--bvid','BV1BMnv6jE5S','--cid','42915922022',
  '--drag-seconds','1','--drag-samples','60','--drag-backward-full',
  '--preview-resolution','auto','--preview-cache-mb','512',
  '--online-cache-root','build\d5\cells\t\cache',
  '--online-pictures','build\d5\cells\t\pics',
  '--online-records-root','build\d5\cells\t\records',
  '--settings-path','build\d5\cells\t\settings','--timeout-ms','150000') `
  -RedirectStandardOutput build\d5\logs\t.out.log -RedirectStandardError build\d5\logs\t.err.log

# 2) 本 agent 的批量脚本（build 下，未入库）
& .\build\d5\run_matrix.ps1            # A/B/C/D 43 次
& .\build\d5\run_visual.ps1            # F 4 次
& .\build\d5\run_hotswitch.ps1         # G4
& .\build\d5\run_regress.ps1 -Steps key,banner,dpi,drag,b,offline,quality
& .\build\d5\run_cell.ps1 -Tag x -Mode cachehit -Fixture 1080 -Bvid BV1BMnv6jE5S -Cid 42915922022 `
    -SettingsJson '{"schema":1,"volume":46,"waveform":false,"cache_root":"D:/d5-cache","cache_quota_gb":20,"preview_resolution":"720p","preview_cache_mb":128}'

# 3) ctest（必须设 TEMP，见 §10.1）
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
& third_party\cmake\3.31.6\cmake\data\bin\ctest.exe --test-dir build\release -C Release --output-on-failure

# 4) 设置页 UI（100% / 200%）
$env:LAPIAN_UI_DIALOG_SHOT = 'build\d5\visual\dialog-100.png'
Start-Process -Wait -FilePath build\release\bin\lapian.exe -ArgumentList @(
  '--online-cache-manage-test','--ui',
  '--manage-root','build\d5\cells\h-ui100\cache',
  '--manage-records-root','build\d5\cells\h-ui100\records',
  '--manage-settings-root','build\d5\cells\h-ui100\settings') `
  -RedirectStandardOutput build\d5\logs\h-ui100.out.log -RedirectStandardError build\d5\logs\h-ui100.err.log
```

> 注意：`build\release\bin\lapian.exe` 是 GUI 子系统且 Qt 运行库不在同目录，
> 必须把 `third_party\Qt\6.8.3\msvc2022_64\bin` 加进 `PATH`，并用
> `Start-Process -RedirectStandardOutput/-RedirectStandardError` 才能拿到日志。

### 14.3 边界声明

- 本轮**没有**改任何产品/测试代码、门/阈值/spec/CMake/installer；只新增本 docs 文件。
- 没有打包、没有安装；`lapian-c8`、C: 安装版、用户 E: 免安装版均未触碰。
- `%APPDATA%\拉片软件\settings.json` 验收前后内容/mtime 一致（未碰用户配置）；
  唯一一次误读是未隔离 settings 的 key 试跑（§10.1 前），已改为 `--settings-path` 隔离，
  并确认该次未写回用户 settings。
