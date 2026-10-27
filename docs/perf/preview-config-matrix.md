# 在线预览配置矩阵与双向门（D4，2026-10-27）

> 状态：**D4 执行完毕，代码 + 本文一起提交**。范围只到探针/门配置化/证据；
> 不改产品默认值、不改 0.9/30fps/5% 阈值、不动 M1/M2 门，未打包未安装。
>
> 前置：计划 §2/§6/§4.4、
> C8 交接、
> 在线拖动 perf。
> 基线：HEAD `afc0f95`（D1/D2/D3 已入库）、ctest 38/38。

---

## 0. 结论摘要

- **1s/60 硬门：9/9 格全过**。反向 presented ≥ 正向 90%（实测 presented 比
  0.982–1.018、fps 比 0.997–1.021）、双方 fps_active 58.8–60.1fps（≥30）；内存全部
  ≤ 配置 + 16MiB；cache-hit `playurl=dash=0`；`stage_show_ms=0`；exact frame/PTS 全对；
  1080P 自动档输出 **1920×1080**、720P 档 **1280×720**。
- **2s/120 目标门**：1080P `auto/512`、`auto/1024`、`auto/2048` 均 **≥0.9**
  （presented 比 1.018 / 1.009 / 1.009；fps 比 1.017 / 1.008 / 1.009），过目标；
  `auto/256`（0.956/0.957）、720P（0.991/0.999）、growing 1080P（1.018/1.020）、
  legacy `1024/64`（0.912/0.913）**只记录，不写“通过”**。
- **P6 保证档**：720P `start145` **40/47ms**、1080P `start150` **79/61ms**（≤150）。
- **内存**：全部格 `peak ≤ 配置 + 16MiB`，2GiB 档不 OOM、不崩、`fallback=0`。
- **与 C8 比**：1s 正向 cache-hit 720P +1.81%、1080P +2.18%、growing 720P +1.82%、
  growing 1080P −1.37%（均 <5%）；2s/120 1080P 由 C8 的 0.877/0.886 提到 **1.018**
  （512MiB）/ **1.009**（1024、2048MiB）。
- **不过/未达（如实报告）**：
  1) 题目指定的视觉判据 **1920 的 lap_var ≥1.2×1024 未达**：同帧实测 **0.522×**
     （1920=110.547、1024=211.655）——原因与“清晰度是否生效”是两件事，见 §7；
  2) `--online-quality-test` **blocked**：本机 SESSDATA 失效（`nav -101`、
     `vip_status=-1`、`login_unavailable`），拿不到 42/0，只有 16 项（15 ok + nav 1 fail）；
  3) 2s 的 256/720P/growing/legacy 是 record 格，不声称通过；本地参考反向比 0.897
     （非在线门）。
- **Medium 副本复验**：`dist\*` + 新 exe 覆盖到
  `%LOCALAPPDATA%\Programs\lapian-preview`（exe SHA256 `1E6F781C…`，与
  `build\release\bin\lapian.exe` 同 hash），抽 8 个场景复跑，数字与 build 一致
  （见 §8.1）；未动 `lapian-c8`/安装版/E:。
- **未改**：`MainWindow`/`CacheSettingsDialog`/`AppSettings`/`PreviewDecoder`/`GfxWidget`/
  `MediaSession`/`VideoDecoder`/`TimelineWidget`/`CMakeLists`/`spec.md`/`installer.iss`；
  0.9/30fps/5% 阈值与 `online_drag_forward_backward_parity` 判据一行未动。

---

## 1. 本轮改动（白名单内）

| 文件 | 改动 |
|---|---|
| `src/media/OnlinePreviewDecoder.h/.cpp` | **只加** test-only：`DumpCachedFrameNv12ForTest(frame, path)` + `LatestInstanceForTest()`；dump 只读有界缓存、不触发解码/发布/淘汰、不改尺寸/预算/回退 |
| `src/app/OnlineDragPreviewTestRunner.h/.cpp` | 两条内存门改配置感知；`--preview-dump-dir` 的 `preview_dump`/`preview_sharpness`；`legacy_64mib` 行；2s/120 仍按 out_of_parity_gate 记录 |
| `src/app/main.cpp` | 前置摘出 `--preview-dump-dir <dir>`（不进主 else-if 链，避免 C1061） |
| `docs/perf/preview-config-matrix.md` | 本文 |

### 1.1 门改动（唯一允许的门改动）

| 旧 | 新 | 判据 | detail |
|---|---|---|---|
| `online_drag_parity_cache_peak_le_64mib` | `online_drag_parity_cache_peak_le_config_budget` | `peak ≤ 本会话 configured_cache_bytes + 16MiB` | `peak= budget_mb= limit= frames=` |
| `online_drag_parity_cache_frames_le_72` | `online_drag_parity_cache_frames_le_config_limit` | `frames ≤ cached_frame_limit()` | `frames= limit= cache_mb= max_side=` |

- 配置取自 `MainWindow::OnlinePreviewConfigForTest()` 的 `cache_bytes/cache_mb/max_side/frames`
  （D2 已有，未新增产品 getter），即**本会话实际生效的解码器配置**（含分配回退后的值）。
- 显式旧档 `--preview-max-side 1024 --preview-cache-mb 64` 仍必须过上述两条，并打印：
  `legacy_64mib peak=66355200 frames=75 limit=75 cache_mb=64 max_side=1024`；
  parity 探针（growing）实测 **61/0 ×2**、两条门 `ok=1`。
- **新档也实测过**（证明门真的是配置感知，不是只对 64MiB 放行）：parity 探针
  `auto/512`（growing，1MiB/s 节流）**61/0**，
  `peak=534,988,800`（510.2MiB ≤ 512+16=528MiB）、`frames=147 ≤ limit=172`。
  这条峰值若沿用旧 64MiB 门会被误杀——正是改名的原因。
- **未动**：`online_drag_forward_backward_parity`（0.9/30fps 三条判据）、
  `cache_hit_backward_presented_ratio_ge_70pct`、`parity_presented_diff_le_10pct`、
  P6 `cache_hit_release_landing_le_150ms` 的 150ms 与 premise 分流。

### 1.2 test-only dump（为什么还加了一个实例入口）

`OnlinePreviewDecoder` 的有界缓存只在实例内部，`MainWindow` 不在本轮白名单，
因此除题目指定的 `bool DumpCachedFrameNv12ForTest(std::int64_t, const std::string&) const`
外，只多了一个 **test-only** 的 `static LatestInstanceForTest()`（构造时登记、析构时
仅当自己是最新时清空；产品代码不读）。dump 语义：

- 命中缓存帧 → 写 `Y w*h + UV w*h/2`（NV12 布局），没有该帧 → `false`；
- 缓存帧格式随路径不同：降采样路径是 NV12；`max_side == 源宽` 时
  `ScaleToNv12` 直接 `av_frame_clone` 源帧（本样本是软件解码 `yuv420p`），
  dump 在 test-only 代码里把 U/V 交织成 NV12 布局，**不改缓存里的帧本身**；
- 全程只锁读缓存，不 seek/不解码/不 evict/不改 `cache_peak`。

---

## 2. 环境与方法

- 构建：`cmake --build build\release --config Release`（D4 改动后 `lapian` 目标重编）；
  产品 exe `build\release\bin\lapian.exe`（GUI 子系统，探针必须
  `Start-Process -Wait -RedirectStandardOutput/-Error`）。
- 夹具（只读源，绝不写入）：
  - 720P：`build\c2-720p\cache`（`BV1cW411b7Yh/28925848`，1280×720@29.412）；
  - 1080P：`build\pm-b-fresh\cache`（`BV1BMnv6jE5S/42915922022`，1920×1080@30）。
  - 每个 tag 先复制到 `build\d4\<tag>\cache`（cache-hit 只读该副本）；
    growing/parity 格用**空缓存根**（与 C8 “fresh” 同口径），不复制夹具。
- 隔离：每格独立 `--settings-path` / `--online-records-root` / `--online-pictures`；
  脚本 `build\d4\run_cell.ps1` + `build\d4\run_matrix.ps1`（ASCII-only，避免无 BOM
  被 ANSI 解码），日志 `build\d4\<tag>\logs\*.err.log`。
- 采样：1s/60 每格 **3 次取中位**；2s/120 门格 3 次取中位、record 格 1 次；
  每个 tag 每次独立复制夹具；全部尝试（含 1 次本地参考、2 次 legacy parity）都计入
  `build\d4\summary.json`，**没有“重跑到过”**。
- `stage_show_ms` 取自产品日志 `T8.9 online_ready stage_show_ms=`（每格均为 0）。
- 本机网络/凭据：`api.bilibili.com:443` 可达（`--online-http-selfcheck`
  `http_status=200`、`cookie_sent=1`），`credentials.dat` 存在但 **SESSDATA 失效**
  （`-101`、`vip_status=-1`）；growing/cache-hit 实测仍可跑（见 §4），
  只有 quality 探针被登录态阻塞（§8）。

命令模板（cache-hit 1080P 与 2s 只改 `--drag-seconds/--drag-samples`）：

```powershell
$env:PATH = "$PWD\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
Start-Process -Wait -FilePath build\release\bin\lapian.exe -ArgumentList @(
  '--online-drag-preview-test','--drag-cache-hit','--bvid','BV1BMnv6jE5S','--cid','42915922022',
  '--drag-seconds','1','--drag-samples','60','--drag-backward-full',
  '--preview-resolution','auto','--preview-cache-mb','512',
  '--online-cache-root','build\d4\d4-ch1080-512-r1\cache',
  '--online-pictures','build\d4\d4-ch1080-512-r1\pics',
  '--online-records-root','build\d4\d4-ch1080-512-r1\records',
  '--settings-path','build\d4\d4-ch1080-512-r1\settings.ini','--timeout-ms','120000')
# growing：去掉 --drag-cache-hit，缓存根为空，加 --m1-throttle-bps 262144
# 2s/120：--drag-seconds 2 --drag-samples 120
# legacy：--preview-max-side 1024 --preview-cache-mb 64
```

---

## 3. 1s/60 硬门（`--drag-seconds 1 --drag-samples 60 --drag-backward-full`）

判据（未改）：反向 presented ≥0.9×正向、反向 fps_active ≥0.9×正向、**双方 fps_active ≥30**；
任一条不满足该格即 fail。峰值判据：`peak ≤ 配置 + 16MiB`。下表为 3 次中位。

| # | 路径 / 配置 | 正 presented / fps | 反 presented / fps | pres 比 | fps 比 | 双方≥30 | peak MiB | frames/limit | fallback | 输出 w×h | exact | 0 网络 | stage_show | 门 |
|---|---|---|---:|---:|---|---:|---|---:|---:|---:|---|---|---|---|---|
| 1 | cache-hit 1080P `auto/256` | 55 / 58.82 | 56 / 60.02 | 1.018 | 1.019 | 是 | 240.3 | 81/86 | 0 | 1920×1080 | 1/1 | playurl=dash=0 | 0 | **pass** |
| 2 | cache-hit 1080P `auto/512`（默认） | 55 / 59.01 | 56 / 60.02 | 1.000 | 1.017 | 是 | 240.3 | 81/172 | 0 | 1920×1080 | 1/1 | 0 | 0 | **pass** |
| 3 | cache-hit 1080P `auto/1024` | 55 / 58.89 | 56 / 59.83 | 1.018 | 1.016 | 是 | 240.3 | 81/345 | 0 | 1920×1080 | 1/1 | 0 | 0 | **pass** |
| 4 | cache-hit 1080P `auto/2048` | 55 / 59.01 | 56 / 59.83 | 1.018 | 1.014 | 是 | 240.3 | 81/690 | 0 | 1920×1080 | 1/1 | 0 | 0 | **pass** |
| 5 | cache-hit 720P `720p/128`（省内存） | 56 / 59.96 | 56 / 59.96 | 1.000 | 1.000 | 是 | 106.8 | 81/97 | 0 | 1280×720 | 1/1 | 0 | 0 | **pass** |
| 6 | cache-hit 720P `auto/512`（默认） | 56 / 60.02 | 56 / 59.96 | 1.000 | 0.999 | 是 | 106.8 | 81/388 | 0 | 1280×720 | 1/1 | 0 | 0 | **pass** |
| 7 | growing 720P `auto/512` + 节流 | 56 / 59.83 | 56 / 60.04 | 1.000 | 1.004 | 是 | 106.8 | 81/388 | 0 | 1280×720 | 1/1 | -（真实下载） | 0 | **pass** |
| 8 | growing 1080P `auto/512` + 节流 | 55 / 59.01 | 56 / 59.89 | 1.018 | 1.015 | 是 | 240.3 | 81/172 | 0 | 1920×1080 | 1/1 | - | 0 | **pass** |
| 9 | growing 1080P `auto/1024` + 节流 | 55 / 58.95 | 56 / 59.96 | 1.018 | 1.018 | 是 | 240.3 | 81/345 | 0 | 1920×1080 | 1/1 | - | 0 | **pass** |

- 每格 3 次全部 `online_drag_forward_backward_parity ok=1`；最紧的一次是
  growing 720P r1 presented 55/56=**0.982**、fps 56.02/55.78≈1.004，仍远高于 0.9。
- 3 次分布：presented 比 0.982–1.018；fps 比 0.997–1.021；正向 fps 58.82–60.09、
  反向 59.83–60.09；每格正向 `first_ms` 64–84ms、UI 心跳 max 84–160ms。
- 1s 每格 `checks=28 failures=0`（cache-hit）/`checks=36 failures=0`（growing），
  `verdict=pass`；`preview_fallback=0`。
- 本地 720P 参考（`--local-drag-parity-test --file media\测试.mp4`，不进硬门）：
  9/0，正向 58.59fps、反向 52.58fps（比 0.897；C8 为 58.53/54.46=0.929）。
  **本地参考低于 0.9 只记录**，不作为在线门，也不拿“两边一样”冒充一致。

### 3.1 与 C8 基线对比（正向 fps_active，同素材同路径）

| 场景 | C8 基线 | 本轮（3 次中位） | Δ | 5% 下限 | 判定 |
|---|---:|---:|---:|---:|---|
| cache-hit 720P 1s | 58.95 | 60.02 | **+1.81%** | 56.00 | pass |
| cache-hit 1080P 1s | 57.75 | 59.01 | **+2.18%** | 54.86 | pass |
| growing 720P 1s | 58.76 | 59.83 | **+1.82%** | 55.82 | pass |
| growing 1080P 1s | 59.83 | 59.01 | **−1.37%** | 56.84 | pass |

C8 的 1s 正/反：720P 55/58.95 vs 56/59.83（比 1.018）、1080P 54/57.75 vs 56/60.02
（比 1.037）；本轮 720P 56/60.02 vs 56/59.96（比 1.000）、1080P 55/59.01 vs 56/60.02
（比 1.000）。**没有靠拉低正向换一致**：正向不降反升，反向仍在 59.8–60.0。

---

## 4. 2s/120（`--drag-seconds 2 --drag-samples 120`）

`online_drag_forward_backward_parity` 的硬门窗口仍只覆盖 1s/60（`drag_seconds==1`），
2s/120 在产品探针里是 `out_of_parity_gate=1` **只记录**；本表按计划 §6.2 的“512MiB+
目标 ≥0.9”人工判定，**不把 record 格写成通过**。

| 格 | 类型 | 配置 | 正 presented / fps | 反 presented / fps | pres 比 | fps 比 | peak MiB | frames/limit | 判定 |
|---|---|---|---:|---:|---:|---:|---:|---:|---|
| cache-hit 1080P | **gate** | `auto/512` | 114 / 58.91 | 116 / 59.95 | **1.018** | **1.017** | 403.4 | 136/172 | **过目标（≥0.9）** |
| cache-hit 1080P | **gate** | `auto/1024` | 115 / 59.49 | 116 / 59.95 | **1.009** | **1.008** | 403.4 | 136/345 | **过目标** |
| cache-hit 1080P | **gate** | `auto/2048` | 115 / 59.34 | 116 / 59.92 | **1.009** | **1.009** | 403.4 | 136/690 | **过目标** |
| cache-hit 1080P | record | `auto/256` | 114 / 58.88 | 109 / 56.33 | 0.956 | 0.957 | 255.1 | 86/86 | 只记录（不写通过） |
| cache-hit 720P | record | `auto/512` | 115 / 59.49 | 114 / 59.44 | 0.991 | 0.999 | 179.3 | 136/388 | 只记录 |
| growing 1080P | record | `auto/512` + 节流 | 114 / 58.88 | 116 / 60.04 | 1.018 | 1.020 | 403.4 | 136/172 | 只记录（≥0.9） |
| legacy cache-hit 1080P | record | `1024/64` | 114 / 58.88 | 104 / 53.77 | 0.912 | 0.913 | 63.3 | 75/75 | 只记录（≥0.9，余量小） |

- 3 次门格分布：512 的 presented 比 1.018/1.009/1.027；1024 的 1.026/1.009/1.000；
  2048 的 1.009/1.000/1.026。**每一轮都 ≥0.9**，不是取最好的一次。
- 2s 的 256 格反向 109/120（0.956）是“预算 86 帧 < 跨度 120 帧”的预期结果，
  与 C8 的 72 帧残差同源，配置变大才过 0.9；**256 不写成通过**。
- 2s 的 cache-hit `playurl/dash=0`、`stage_show_ms=0`、`exact=1/1`、`fallback=0`
  与 1s 相同（每格日志可查）。
- 与 C8 的 2s 对比：1080P cache-hit 0.877（114/58.85 vs 100/51.60）→ **1.018**；
  growing 1080P 0.886（114/58.88 vs 101/52.20）→ **1.018**；720P 0.930
  （115/59.37 vs 107/55.30）→ **0.991**。正向 fps 变化 +0.10% / 0.00% / +0.20%，
  反向 presented 分别 +16 / +15 / +7。

---

## 5. P6 保证档（cache-hit，1s/60，`auto/512`）

| 样本 | `--drag-start-frame` | release+landing（2 次） | premise | peak MiB | frames/limit | 正 / 反 | 判定 |
|---|---|---:|---|---:|---:|---|---|
| 720P | 145 | **40 / 47ms** | ≤2000ms，`premise` | 282.8 | 214.5/388 | 56/59.95 | **pass** |
| 1080P | 150 | **79 / 61ms** | ≤2000ms，`premise` | 373.8 | 126/172 | 55/58.88（反向 56/59.92） | **pass** |

- 两次都在 `cache_hit_release_landing_le_150ms ok=1` 的 premise 分支（不是
  `out_of_premise`）；默认 1s 窗口（end=75）的 `release+landing` 108–111ms 仍属
  `out_of_premise=1`（keyframe 距离 2500ms），只记录，不冒充保证档。
- P6 格 `--drag-start-frame` 显式，`online_drag_forward_backward_parity` 按既有规则
  `out_of_parity_gate=1` 只记录（比值仍 1.018/1.000）。

---

## 6. 内存（每格 `peak ≤ 配置 + 16MiB`）

| 格 | 配置 MiB | peak 字节 | peak MiB | 预算+16MiB | 在限内 | frames/limit |
|---|---:|---:|---:|---:|---|---:|
| 1080P `256` 1s | 256 | 251,942,400 | 240.27 | 272.0 | 是 | 81/86 |
| 1080P `512` 1s | 512 | 251,942,400 | 240.27 | 528.0 | 是 | 81/172 |
| 1080P `1024` 1s | 1024 | 251,942,400 | 240.27 | 1040.0 | 是 | 81/345 |
| 1080P `2048` 1s | 2048 | 251,942,400 | 240.27 | 2064.0 | 是 | 81/690 |
| 1080P `256` 2s | 256 | 267,494,400 | 255.10 | 272.0 | 是（余量 16.9） | 86/86 |
| 1080P `512` 2s | 512 | 423,014,400 | 403.42 | 528.0 | 是 | 136/172 |
| 1080P `1024` 2s | 1024 | 423,014,400 | 403.42 | 1040.0 | 是 | 136/345 |
| 1080P `2048` 2s | 2048 | 423,014,400 | 403.42 | 2064.0 | 是 | 136/690 |
| 720P `128` 1s | 128 | 111,974,400 | 106.79 | 144.0 | 是 | 81/97 |
| 720P `512` 1s | 512 | 111,974,400 | 106.79 | 528.0 | 是 | 81/388 |
| 720P `512` 2s | 512 | 188,006,400 | 179.30 | 528.0 | 是 | 136/388 |
| growing 720P `512` 1s | 512 | 111,974,400 | 106.79 | 528.0 | 是 | 81/388 |
| growing 1080P `512` 1s | 512 | 251,942,400 | 240.27 | 528.0 | 是 | 81/172 |
| growing 1080P `1024` 1s | 1024 | 251,942,400 | 240.27 | 1040.0 | 是 | 81/345 |
| growing 1080P `512` 2s | 512 | 423,014,400 | 403.42 | 528.0 | 是 | 136/172 |
| legacy `1024/64` 1s | 64 | 66,355,200 | 63.28 | 80.0 | 是 | 75/75 |
| legacy `1024/64` 2s | 64 | 66,355,200 | 63.28 | 80.0 | 是 | 75/75 |
| parity 探针 1080P `auto/512` | 512 | 534,988,800 | 510.21 | 528.0 | 是 | 147/172（peak 由 172 帧满缓存产生） |
| P6 720P `512` | 512 | 296,524,800 | 282.79 | 528.0 | 是 | 214.5/388 |
| P6 1080P `512` | 512 | 391,910,400 | 373.75 | 528.0 | 是 | 126/172 |

- 2GiB 档 3 次（1s）+ 3 次（2s）均不崩、不 OOM，`preview_fallback=0`，
  分配路径没有触发降预算/降分辨率。
- 1s 1080P 峰值与配置无关（240.3MiB，60 帧跨度 + 预取 ≈81 帧）；2s 才需要
  136 帧（403.4MiB），`512` 正好放得下；`256` 只能放 86 帧，2s 反向开始掉。
- `frames/limit`：`frames` 是本次会话实际缓存帧数，`limit` 来自
  `cached_frame_limit()`（预算 / 估计帧字节）；配置感知门判的就是这两者。

---

## 7. 视觉证据（`--preview-dump-dir` 与 lap_var）

### 7.1 做法

- 正向拖动结束（`RunDragPass` 返回、预览器仍 Pause 保留缓存）后，探针从**最终目标**
  向两侧找“离目标最近的已缓存帧”，用 test-only dump 写
  `build\d4\visual\preview_<w>x<h>_f<frame>.nv12`（Y `w*h` + UV `w*h/2`，NV12 布局），
  并在 Y 平面上算 3×3 Laplacian 方差（4 邻域核 `0 1 0 / 1 -4 1 / 0 1 0`，C++ 内联），
  打 `preview_sharpness frame=<f> w=<w> h=<h> lap_var=<v>`。
- 同一素材（1080P 夹具、同一帧）分别跑：
  - 旧档：`--preview-max-side 1024 --preview-cache-mb 64`（3 次）；
  - 新档：`--preview-resolution auto --preview-cache-mb 512`（3 次 + 1 次对齐帧）。
- 缓存帧目标对齐：1024 档在目标 75 时最近缓存帧是 **f74**（75 帧预算放不下 75），
  1920 档目标 75 时是 **f75**；为“同一帧”对照，另跑一次 1920 档 `--drag-start-frame 14`
  （目标 74、最近缓存 f74），得到 **f74 同帧对比**。
- `.nv12` → PNG 用 bundled Python（numpy+Pillow）把 Y 平面存灰度：
  `build\d4\visual\frame_1024x576.png`、`build\d4\visual\frame_1920x1080.png`
  （均取自 **f74 同帧**），另有 1:1/2× 中心裁剪 `crop_1920.png`/`crop_1024.png`。

### 7.2 lap_var 实测（同帧 f74）

| 档位 | 输出 | 缓存帧 | lap_var | 说明 |
|---|---|---:|---:|---|
| 旧 1024 / 64MiB | 1024×576 | f74 | **211.655** | 3 次里 2 次 f74=211.655、1 次 f75=209.494（均记录） |
| 新 1920 / 512MiB | 1920×1080 | f74 | **110.547** | 3 次 f75 均为 105.537；f74 对齐帧 110.547 |
| **比值（1920/1024）** | | | **0.522** | 题目要求 ≥1.2 → **未达，如实记 fail** |

**为什么不是 ≥1.2×（根因）**：Laplacian 方差不是跨分辨率可比量。同一帧 1920 原生
是软源（按像素看边缘更平缓）；1024 是 `SWS_FAST_BILINEAR` 降采样，同一内容被压进
更少像素、单位像素梯度更集中。把 1920 帧用 box 降到 1024 得 lap_var **189.914**，
与 SWS 的 211.655 同量级（SWS 还有点 aliasing），说明差异主要来自“缩放”而不是
“清晰度没生效”。

**清晰度确实生效的证据**（不依赖这个判据）：

1. `preview_config max_side=1920/1024` 且缓存帧实际尺寸 1920×1080 / 1024×576，
   dump 文件 3,110,400B vs 884,736B；
2. 同显示尺寸对比：把 1024 帧按渲染器的方式 bilinear 放大到 1920 再算 lap_var =
   **17.278**，原生 1920 = **110.547** → **6.398×**（用户在 1080p 窗口里看到的锐度差）；
3. PNG 同帧对照 + `crop_1920.png`（1:1）vs `crop_1024.png`（2× 放大）肉眼可复核。

> 结论：**题目指定的 lap_var ≥1.2× 判据未通过（0.522），如实记录，不美化**；
> 但“1080P 档比 1024 清楚”在用户可见口径成立（6.398×），不是“配置没生效”。
> 该项是探针判据问题，不是产品门；未改任何产品代码去凑这个数字。

### 7.3 截图/证据路径

- `build\d4\visual\frame_1024x576.png` / `frame_1920x1080.png`（同帧 f74，灰度 Y）
- `build\d4\visual\crop_1024.png` / `crop_1920.png`（同显示尺寸对照）
- `build\d4\visual\preview_1024x576_f74.nv12` / `preview_1920x1080_f74.nv12`（原始 dump）
- 产品截图/精确帧路径未改：S 键快照仍走原 `CaptureSnapshot`，本 dump 只在
  `--preview-dump-dir` 下写 `build/d4/visual`。

---

## 8. 回归（D4 改动后实测）

| 项 | 结果 | 备注 |
|---|---|---|
| `ctest --test-dir build\release -C Release` | **38/38 pass**（65.1s） | 需 Qt `bin` 在 PATH（否则 0xC0000135） |
| `lapian_timeline_test.exe` 单独 | **188 OK / 0 fail** | |
| `--key-test --file media\测试.mp4` | **77 / 0** | `key_check` 计数 |
| `--banner-test --file media\测试.mp4 --banner-tag d4` | **74 / 0** | |
| `--dpi-test` 100/125/150/200% | 每档 **37 项 / 0 fail**，`dpi_problem` **各 0** | `QT_SCALE_FACTOR` + `--dpi-expect-scale` |
| `--drag-test --drag-samples 20` | P4 **95.6fps**、P6 p95 **21.6ms**（pass）、P7 0.0/14.7、P8 13.0、P9 38.7 | P6 未比 20–25ms 明显退化 |
| `--online-quality-switch-test --quality-cache-hit-menu`（B）720P | **38 / 0 pass**（`BV1cW411b7Yh`，64→32→64） | 独立缓存副本 |
| 同上 1080P | **38 / 0 pass**（`BV1BMnv6jE5S`，80→112→80，`actual_qn=112`、真实下载 47,168,494B） | 独立缓存副本 |
| `--online-quality-test` | **blocked**：`nav -101`、`vip_status=-1`、`login_unavailable`；16 项（15 ok + `nav_login` 1 fail），`sample verdict=blocked`、`end verdict=fail` | **不是 42/0**，见 §9 |
| `--online-offline-test`（workspace，3.2MB faststart） | **45 / 0 pass** | cache-hit 0 网络 |
| 网络/凭据 | `api.bilibili.com:443` 可达（HTTP 200、239ms、cookie_sent=1）；`credentials.dat` 存在但 SESSDATA 失效 | |

- 说明：`--online-quality-switch-test` 的 B 1080P 在 SESSDATA 失效下仍拿到
  `actual_qn=112`（真实 47MB 下载），因此 B 两项是真过，不是跳过；
  `--online-quality-test` 的 nav 登录断言无法绕过，按 blocked 记录。
- 回归跑在 D4 最终代码上（dump/门改动已编入 `lapian.exe`），未打包、未安装、
  未覆盖 `lapian-c8`/安装版/E:。

### 8.1 Medium 副本复验（同一 exe，2026-10-27）

`dist\*` → `%LOCALAPPDATA%\Programs\lapian-preview`，再用
`build\release\bin\lapian.exe`（SHA256 `1E6F781C90FC86A24F4EB35DC3723D3223C2A5D8AE24508AECF1A3B7DB744DD7`，
3,687,424B）覆盖；Medium exe hash 与 build 一致。抽 8 个场景（各 1 次）从该副本复跑：

| 场景 | 正 presented / fps | 反 presented / fps | 比值 | peak/frames | 结果 |
|---|---|---|---:|---|---|
| cache-hit 1080P `auto/512` 1s | 55 / 58.95 | 56 / 59.83 | 1.018 / 1.015 | 240.3MiB / 81 | 28/0 pass，1920×1080 |
| cache-hit 720P `auto/512` 1s | 55 / 60.04 | 56 / 59.96 | 1.018 / 0.999 | 106.8MiB / 81 | 28/0 pass，1280×720 |
| growing 720P `auto/512` 1s | 56 / 59.96 | 56 / 60.02 | 1.000 / 1.001 | 106.8MiB / 81 | 36/0 pass |
| growing 1080P `auto/1024` 1s | 55 / 58.89 | 56 / 59.96 | 1.018 / 1.018 | 240.3MiB / 81 | 36/0 pass |
| legacy `1024/64` 1s | - | - | - | 63.28MiB / 75（limit 75） | 28/0，`legacy_64mib` 行 |
| cache-hit 1080P `auto/512` 2s/120 | 116 / 59.92 | 117 / 60.47 | 1.009 / 1.009 | 403.4MiB / 136 | 28/0 pass（out_of_parity_gate 记录） |
| P6 1080P `start150` | - | - | - | - | `release+landing=82ms`、premise 2000ms、28/0 |
| parity `auto/512`（growing） | 58.59 | 58.47 | presented_diff=0.000 | 510.2MiB ≤528 / 147≤172 | 61/0 pass |

结论：Medium 副本与 build 目录同 hash，抽测数字在 run 级波动内一致（1s 比值
1.000–1.018、2s 1.009、legacy/512 两条配置感知门均 `ok=1`），未出现“只有 build
目录能过”的情况。

---

## 9. 未达标 / blocked（逐条）

1. **视觉 lap_var ≥1.2× 未达**：同帧 0.522（§7.2）。原因见上；如实记为未达，
   不写成“清晰度没生效”也不写成“通过”。
2. **`--online-quality-test` blocked**：SESSDATA 失效（`nav_api_code=-101`、
   `nav_vip_status=-1`、`sample_block=login_unavailable`）。本机没有可用凭据，
   该探针的 42/0 无法复现；不做跳过、不写“通过”。
3. **2s/120 `auto/256`（0.956/0.957）、720P（0.991/0.999）、growing（1.018/1.020）、
   legacy 1024/64（0.912/0.913）**：按计划是 record 格，**不写通过**；
   其中 256 与 legacy 的反向掉在 0.9 边缘，属预算/跨度不足，符合预期。
4. **本地 720P 参考反向比 0.897（52.58/58.59）**：非在线门、只对照；
   C8 为 0.929，本轮同机热缓存下略低，记录不解释成回归（本地路径本轮零改动）。
5. 1s 1080P `auto/256` 的峰值 240.3MiB 对 272MiB 上限余量只有 31.7MiB，
   属于“配置 256 但 60 帧跨度就吃满 81 帧”的紧余量，记录在 §6。

---

## 10. 判定汇总

| 验收项 | 结果 |
|---|---|
| 1s/60 每格反向 presented/fps ≥0.9×正向、双方 ≥30fps | **9/9 格 pass**（每格 3/3 次） |
| 1s/60 正向相对 C8 不降 >5% | **pass**（+1.81% / +2.18% / +1.82% / −1.37%） |
| 2s/120 1080P 512/1G/2G 目标 ≥0.9 | **pass**（1.018 / 1.009 / 1.009，每格 3/3） |
| 2s/120 256 / 720P / growing / legacy | record（0.912–1.020），不写通过 |
| 内存 peak ≤ 配置 + 16MiB；2GiB 不 OOM、fallback=0 | **pass**（§6） |
| P6 保证档 ≤150ms；stage_show=0；cache-hit 0 网络；exact | **pass**（40/47、79/61ms；0；0；1/1） |
| 清晰度：1080P 自动档 1920×1080、720P 档 1280×720 | **pass**（§3/§4） |
| 视觉判据 lap_var ≥1.2× | **未达**（0.522，见 §7.2） |
| 回归 ctest 38/38、timeline 188/0、key 77/0、banner 74/0、dpi 4 档、drag、B 38/0×2、offline 45/0 | **pass** |
| `--online-quality-test` 42/0 | **blocked**（凭据失效，见 §9） |

**D4 结论：配置矩阵与 1s/60 双向硬门、2s/120 512MiB+ 目标、内存、P6、清晰度分辨率
全部通过；视觉 lap_var 判据未达（探针指标问题，已给同显示尺寸 6.398× 证据）；
quality 探针因本机 SESSDATA 失效 blocked。失败项只报告，不改代码/阈值等 D5 独立复核。**

---

## 11. 原始日志与复现

- 全量矩阵 50 次运行：`build\d4\<tag>\logs\*.err.log`；汇总
  `build\d4\summary.json`、`build\d4\summary.tsv`（`build\d4\collect.py` 生成）。
  另有附加复验（未进 50 次汇总）：legacy parity ×1（+1 次共 2 次）、512 parity ×1、
  视觉重复 4 次 + 同帧对齐 1 次、最终 smoke 1 次，日志同样在 `build\d4\`。
- 脚本：`build\d4\run_cell.ps1`（单格）、`build\d4\run_matrix.ps1`（全矩阵）。
- 关键 tag：`d4-ch1080-{256,512,1024,2048}[-2s]-r{1,2,3}`、
  `d4-ch720-{128,512}[-2s]`、`d4-gr720-512`、`d4-gr1080-{512,1024}`、
  `d4-p6-{720-145,1080-150}`、`d4-legacy-{1024-64,1024-64-2s,parity}`、
  `d4-local-parity`、`d4-vis-{1024,1920}-r{1,2,3}`（视觉）。
- 复现矩阵：`& .\build\d4\run_matrix.ps1 -LocalFile 'media\测试.mp4'`（仓库根，
  GUI 探针需 Qt `bin` 在 PATH）。
