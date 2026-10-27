# 在线预览缓存长跨度矩阵（P3，2026-10-27）

> 状态：**P3 执行完毕，代码 + 本文一起提交**。范围只到 test-only 探针/门 + 证据；
> 不改产品默认值、不改 0.9/30fps/5% 判据、不改 1s/2s 既有判定与
> `online_drag_forward_backward_parity` 逻辑；未打包、未安装，未动
> `lapian-c8`/安装版/E:。
>
> 前置：[计划](../plan-2026-10-27-preview-cache-cap-4096.md) §2.3/§3 P3/§4、
> [L1 计划](../plan-2026-10-27-loop-workrange-fix.md)（`9e7a6f0`，P3 不改其文件）、
> [C9 发布](../release-2026-10-27-preview-config.md)、
> [D4 配置矩阵](preview-config-matrix.md)。
> 基线：HEAD `9e7a6f0`（P1 上限 4096、P2 设置页范围/本机上限、L1 循环修复均已入库），
> 工作树干净；P3 改动见 §1，构建后 ctest 38/38。

---

## 0. 结论摘要

- **探索格（20s 跨度 + 4096MiB）答案：缓存能填到 1221–1222 / 1380 帧 =
  88.5–88.6% 的 4GiB 预算（peak 3621.9–3624.8MiB），并覆盖 101.8% 的
  1200 帧跨度**。正/反向 `presented` 1140–1180 / 1200（95.0–98.3%）、
  `fps_active` 57.2–59.2 → **20s 被 4GiB 完整覆盖，没有出现“解码吞吐填不满”**。
- **两个门格 3 次全部通过**（每格 3 次 × 8 条 = **24/24**；第 8 条
  `long_span_same_target_rate` 是收尾新增的“反向必须与正向同目标数/同跨度”前置检查）：
  - **10s@2048**：正 584/58.79、反 595/59.99，比 **1.021/1.021**（三次 1.015–1.024）；
    peak 1839.1MiB ≤ 2064MiB；`preview_frames/limit=620/690`（89.9%）；`close=63ms`。
  - **20s@4096**：正 1175/58.94、反 1192/59.85，比 **1.014/1.014**（三次 1.011–1.017）；
    peak 3624.8MiB ≤ 4112MiB；`preview_frames/limit=1222/1380`（88.6%）；
    `close=145ms`。
  - 两格**只用 `--long-span-gate`（不带 `--drag-backward-full`）**复跑：
    `backward_samples == samples`（600/1200），`long_span_same_target_rate ok=1`，
    每轮 **36 checks / 0 fail**。
- **1s/2s 不退化（对照格）**：1s 512/4096 正 55/58.89、反 55/58.89（比 1.000–1.018），
  正向相对 C9 包基线 57.75/58.95 在 **±5% 内**；2s/512 比 **0.965–0.974
  ≥ 0.9**（未到 C9 的 1.009–1.018，见 §5.2 的同机 A/B：C9 二进制现在也只有
  0.973–0.982，属环境漂移，不是 P3 代码）；720P 1s 正 55/60.04（C9 58.95 的 +1.8%）。
- **记录格（按计划只记录、不写“通过”）**：2s@4096 0.965、10s@1024 **0.917**、
  10s@4096 1.003、20s@2048 **0.939**、40s@4096 **0.930**。
- **40s@4096 不宣称覆盖**：2400 帧 × 3,110,400B = **7,464,960,000B（约 6.95GiB）
  > 4GiB**；`frames/limit=1380/1380`（100%）但 **span 覆盖 57.5% < 80%**。
  实测 `presented` 正向 95.5%、反向 89.1% 说明**瓶颈是容量，不是解码吞吐**
  （计划 §5 预设的“解码吞吐限制”在 20s 探索格没有出现）。
- **内存**：29 次矩阵全部 `peak ≤ 配置 + 16MiB`、`fallback=0`、无 OOM/无崩溃；
  4GiB 档峰值最高 **4,292,352,000B（4093.5MiB，余量 18.5MiB）**；
  4096 是**上限不是预分配**（只有长跨度才真吃到）。
- **关闭释放耗时**（围绕 `StopPreviewForTest()`，只记录）：1s **9–10ms**、2s
  **16–18ms**、10s **62–90ms**、20s@2048 136–188ms、20s@4096 **139–149ms**、
  **40s@4096 331–395ms（> 200ms，按计划 §5 记为 gap）**。
- **回归**：`ctest` **38/38**（收尾后 66.48s）；`--loop-test --file media\测试.mp4`
  **41/0**（L1 未受影响）；`--key-test` **77/0**；`--drag-test` P4
  **64.3 / 64.4fps**、P6 p95 **28.6 / 15.3ms**（P7/P8/P9 全 pass）；
  **Medium 副本 5 格复验全过**（§7.3）。

---

## 1. 本轮改动（白名单内）

| 文件 | 改动 |
|---|---|
| `src/app/OnlineDragPreviewTestRunner.h` | `OnlineDragPreviewTestOptions` 新增 `bool long_span_gate`（test-only） |
| `src/app/OnlineDragPreviewTestRunner.cpp` | `Run()` 里新增长跨度采集/判定/关闭计时：`long_span_record`、`preview_close_ms=`、汇总行追加 `long_span=…`；仅在 `--long-span-gate` 时跑 **8 条** `long_span_*` 检查（含收尾新增的 `long_span_same_target_rate`） |
| `src/app/main.cpp` | 前置参数环（不进主 else-if 链，避免 C1061）解析 `--long-span-gate`；`--drag-seconds` 上限 10→60（test-only）；未显式给 `--drag-samples` 时按 **60×秒** 自动给目标数（60Hz 口径）；**`--long-span-gate` 且未显式给 `--drag-backward-samples` 时自动置“反向与正向同目标数/同跨度”**（等价 `--drag-backward-full`）；拖动预览/parity 探针的 `--timeout-ms` 上限 120000→300000（其它在线探针仍 120000）；`options.long_span_gate` 传递 |
| `docs/perf/preview-cache-long-span.md` | 本文 |

### 1.1 新增 test-only 参数与口径

- `--long-span-gate`：只给门格用。**未开启时探针只打印 `long_span_record …`，
  不做任何判定**（record 格日志里 `gate=0`，不出现 `long_span_*` check 行，
  也不写“通过”）。
- `--drag-seconds S`（口径与计划 §2.3 一致）：
  - 目标数 = **60×S**（60Hz；未显式给 `--drag-samples` 时自动 60×S，1s 仍 60）；
  - 跨度 = **60×S 帧**（`fps×2×S`，30fps 素材下 S 秒目标 = 2S 秒内容）；
  - 单程时长 = S×1000ms；正、反各一遍。
- **反向目标数自动补齐（收尾）**：`--long-span-gate` 且用户未显式给
  `--drag-backward-samples` 时，main.cpp 自动置“反向与正向同目标数/同跨度”
  （等价 `--drag-backward-full`）。显式给了不同值则尊重用户，但
  `long_span_same_target_rate` 会判 **fail**（detail 带 `gate=inactive`），
  不会静默当通过。**原因**：漏 `--drag-backward-full` 时反向只跑默认 30 个目标
  （`backward_samples=30`、`backward_span=±30`），3 条比值检查会以 ~0.025 假失败。
- 不复用 `--online-drag-parity-test`：长跨度全部走 T8.6.1b 的真实窗口拖动探针
  （`--online-drag-preview-test --drag-cache-hit`），只加诊断/门，不改其既有检查。

### 1.2 `long_span` 检查（8 条，仅 `--long-span-gate`）

| 检查名 | 判据 |
|---|---|
| `long_span_same_target_rate` | 反向与正向**同目标数 + 同跨度**（`backward_samples == forward_samples && abs(backward_span) == forward_span`）；不满足判 fail，detail 写 `gate=inactive` 与两个值 |
| `long_span_reverse_presented_ratio_ge_0_9` | 反 presented / 正 presented ≥ 0.9 |
| `long_span_reverse_fps_ratio_ge_0_9` | 反 fps_active / 正 fps_active ≥ 0.9 |
| `long_span_both_fps_active_ge_30` | 正、反 fps_active 都 ≥ 30 |
| `long_span_peak_le_config_budget` | `preview_peak ≤ cache_bytes + 16MiB` |
| `long_span_fallback_zero` | `preview_fallback=0`（无分配回退） |
| `long_span_exact_landing` | 正、反都 `exact_frame=1` 且 `exact_pts=1` |
| `long_span_playurl_dash_zero` | cache-hit 时 `playurl=dash=0`（growing 只记录） |

诊断行（每格必打，含 record 格；含 `backward_span`）：

```
long_span_record mode=cache_hit span_frames=… samples=… backward_samples=… duration_ms=…
  forward_presented=… backward_presented=… presented_ratio=… forward_fps_active=…
  backward_fps_active=… fps_ratio=… fps_floor_30=…
  preview_frames=… limit=… coverage_pct=… span_coverage_pct=…
  preview_peak=… peak_limit=… cache_mb=… max_side=…
  preview_w=… preview_h=… fallback=… exact=… playurl=… dash=…
  backward_span=… preview_close_ms=… gate=0|1
preview_close_ms=… preview_frames_before_close=… preview_peak_before_close=… drag_seconds=…
```

汇总行追加：

```
online_drag_preview_result checks=… failures=… verdict=… <preview stats>
  long_span=1 span_frames=… samples=… coverage_pct=… preview_frames=… limit=… peak=… preview_close_ms=…
```

- `coverage_pct` = `preview_frames / cached_frame_limit`（**4GiB 预算填了多少**）；
- `span_coverage_pct` = `preview_frames / span_frames`（**拖动跨度被缓存覆盖多少**）；
- 两个分母都写出来，避免只挑好看的那个；<80% 时在 §8 明确不宣称覆盖。

### 1.3 未改（逐条）

`playback`/`PlaybackController.*`、`MainWindow.*`、`AppSettings.*`、
`OnlinePreviewDecoder.*`、`CacheSettingsDialog.*`、`PreviewDecoder`、`GfxWidget`、
`MediaSession`、`CMakeLists`、spec、installer、门/阈值（0.9 / 30fps / 5% / 150ms /
配置+16MiB）；1s/2s 既有判据与 `online_drag_forward_backward_parity` 一行未动。

---

## 2. 环境与方法

- 构建：`cmake --build build\release --config Release --target lapian`（增量 10.9s / 收尾 11.9s）；
  产品 exe `build\release\bin\lapian.exe` **3,753,984B**、SHA256
  `84F017B57ECC30212728229357382DEE295BF53C51D67BB84F492234ADF0AC21`（收尾后；
  收尾前 3,753,472B / `611BAB28…`，差异只在 test-only 参数默认与一条新检查）。
- 夹具（**只读源，绝不写入**）：`build\pm-b-fresh\cache`
  （`BV1BMnv6jE5S / 42915922022`，1080P 1920×1080@30、111s、`80-7` 完整 DASH，
  `data.mp4` 31,057,770B）。每个 tag **先复制**到 `build\p3\<tag>\cache`。
- 隔离：每格独立 `--settings-path` / `--online-pictures` / `--online-records-root`；
  每格每次运行重新复制夹具（29 次矩阵 × 31MB，不写共享夹具）。
- GUI 子系统 exe 一律 `Start-Process -Wait -RedirectStandardOutput/-RedirectStandardError`
  取日志；`--timeout-ms 300000`（拖动预览探针上限，其它探针仍 120000）。
- 次数：1s/2s/10s/20s 每格 **3 次**、40s **2 次**（计划 §3 允许），共 **29 次矩阵运行**；
  另做 6 次同机 A/B（§5.2）、5 次 Medium 副本（§7.3）、1 次 720P 对照、1 次
  “不给 `--drag-samples` 自动 60×秒”验证、1 次 growing 1s 冒烟（§7.2）、
  **收尾后 6 次“只带 `--long-span-gate`”门格复跑 + 1 次负向（显式
  `--drag-backward-samples 30`）+ 2 次 Medium 门格复跑**，共 **52 次**。
- 真实窗口 + 真实缓存文件；不注入假数据。cache-hit 全程 0 网络（见 `playurl/dash=0`）。

命令模板（1080P cache-hit；门格加 `--long-span-gate`；40s 用 `--drag-seconds 40
--drag-samples 2400`）：

```powershell
$env:PATH = "$PWD\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
Start-Process -Wait -FilePath build\release\bin\lapian.exe -ArgumentList @(
  '--online-drag-preview-test','--drag-cache-hit',
  '--bvid','BV1BMnv6jE5S','--cid','42915922022',
  '--drag-seconds','20','--drag-samples','1200','--drag-backward-full',
  '--preview-resolution','auto','--preview-cache-mb','4096',
  '--online-cache-root','build\p3\p3-20s-4096-r1\cache',
  '--online-pictures','build\p3\p3-20s-4096-r1\pics',
  '--online-records-root','build\p3\p3-20s-4096-r1\records',
  '--settings-path','build\p3\p3-20s-4096-r1\settings',
  '--timeout-ms','300000') -RedirectStandardOutput ... -RedirectStandardError ...
```

---

## 3. 探索格：20s 跨度 + 4096MiB（先回答“能填多少”）

3 次原始值（1080P cache-hit，正/反各 1200 个目标、跨度 1200 帧）：

| # | 正 presented / fps | 反 presented / fps | 比 | `preview_frames/limit` | `coverage_pct` | `span_coverage_pct` | peak (B / MiB) | close |
|---|---:|---:|---|---:|---:|---:|---:|---:|
| r1 | 1169 / 58.64 | 1180 / 59.20 | 1.009 / 1.010 | 1221/1380 | 88.5% | 101.8% | 3,797,798,400 / 3621.9 | 151ms |
| r2 | 1140 / 57.19 | 1158 / 58.19 | 1.016 / 1.017 | 1222/1380 | 88.6% | 101.8% | 3,800,908,800 / 3624.8 | 155ms |
| r3 | 1149 / 57.63 | 1156 / 58.09 | 1.006 / 1.008 | 1222/1380 | 88.6% | 101.8% | 3,800,908,800 / 3624.8 | 151ms |
| 中位 | 1149 / 57.63 | 1158 / 58.19 | 1.009 / 1.010 | 1222/1380 | **88.6%** | **101.8%** | 3,800,908,800 / **3624.8** | **151ms** |

**回答“拖动 20s 时缓存到底能填多少”**：

1. **按预算**：能填 **88.5–88.6%**（1221–1222 / 1380 帧，peak 3621.9–3624.8MiB）。
   没用满的 ~158 帧是“没被拖到的帧”：一次拖动只解码目标两侧需要/预取的帧，
   20s 跨度只产生 1200 帧 + 约 22 帧预取窗口，所以 4GiB 预算不会被填满；
   **这不是分配失败**（`fallback=0`），也不是吞吐掉队。
2. **按跨度**：**101.8%**（1222 > 1200），即 4GiB **足以放下整个 20s 跨度**，
   反向回拖能命中缓存（反 presented 1156–1180，比正还略高）。
3. **吞吐**：`fps_active` 正 57.2–58.6 / 反 58.1–59.2，与 1s/10s 的 58.6–59.0 同量级，
   说明 60Hz 目标下探针本身约 58fps 呈现，**长跨度没有额外掉速**。
4. 因此 §5 计划里担心的 **“4GiB 的收益受解码吞吐限制”在 20s 格没有发生**：
   两个覆盖率都 ≥80%，20s@4096 可以算“已覆盖”。
   **但 40s 格（§4.3/§8）确实 <80%，原因不是吞吐而是容量（2400 帧 ≈ 7.0GiB > 4GiB），
   40s 不宣称覆盖。**
5. **1s/2s 的 `coverage_pct` 只有 5.9–79.1%**（`1s@4096` 5.9%、`2s@4096` 9.9%）：
   这是“跨度只用到 60–136 帧”的必然结果（计划 §0：4GiB 对 1–2s 无收益），
   **不是吞吐限制，也不宣称这两格“覆盖 4GiB”**；它们只作“不退化对照”。

---

## 4. 门格数据（`--long-span-gate`，每格 3 次 × 8 条）

判据（与计划 §4 一致，未放宽）：反 presented ≥0.9×正、反 fps_active ≥0.9×正、
双方 fps_active ≥30、`peak ≤ 配置+16MiB`、`fallback=0`、exact 1/1、
cache-hit `playurl=dash=0`；**前置条件：反向与正向同目标数 + 同跨度**
（`long_span_same_target_rate`，收尾新增）。

> 口径说明：下表是**收尾后**的复跑——命令**只带 `--long-span-gate`、不带
> `--drag-backward-full`**，由 main.cpp 自动补齐反向目标数，因此同时验证了
> “漏参数不会再假失败”。每轮 `checks=36 failures=0`（28 项既有 + 8 项 long_span）。
> 收尾前带 `--drag-backward-full` 的 3 次运行是 `7/7`、`35 checks`，比值同样
> 1.021/1.021（10s）与 1.009/1.010（20s），日志保留在 `build\p3\p3-10s-2048-*` /
> `build\p3\p3-20s-4096-*`。

### 4.1 10s@2048 — **gate，3/3 通过（24/24 条）**

| # | 正 presented / fps | 反 presented / fps | pres 比 | fps 比 | floor30 | peak MiB | frames/limit | span cov | close | 结果 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| r1 | 585 / 58.87 | 594 / 59.79 | 1.015 | 1.016 | 1 | 1839.1 | 620/690（89.9%） | 103.3% | 64ms | 8/8 |
| r2 | 581 / 58.48 | 595 / 59.99 | 1.024 | 1.026 | 1 | 1839.1 | 620/690 | 103.3% | 62ms | 8/8 |
| r3 | 584 / 58.79 | 596 / 59.98 | 1.021 | 1.020 | 1 | 1839.1 | 620/690 | 103.3% | 63ms | 8/8 |
| 中位 | 584 / 58.79 | 595 / 59.99 | **1.021** | **1.021** | 1 | 1839.1 | 620/690 | 103.3% | **63ms** | **pass** |

- `backward_samples=forward_samples=600`、`backward_span=-600`、
  `long_span_same_target_rate ok=1`（每轮）。
- `preview_w×h=1920×1080`、`fallback=0`、`exact=1`、`playurl=dash=0`（每轮）。
- 峰值 1,928,448,000B（1839.1MiB）≤ 2,164,260,864B（2064MiB），余量 224.9MiB。

### 4.2 20s@4096 — **gate，3/3 通过（24/24 条）**

| # | 正 presented / fps | 反 presented / fps | pres 比 | fps 比 | floor30 | peak MiB | frames/limit | span cov | close | 结果 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| r1 | 1176 / 58.99 | 1192 / 59.85 | 1.014 | 1.015 | 1 | 3624.8 | 1222/1380（88.6%） | 101.8% | 139ms | 8/8 |
| r2 | 1173 / 58.83 | 1193 / 59.85 | 1.017 | 1.017 | 1 | 3624.8 | 1222/1380 | 101.8% | 149ms | 8/8 |
| r3 | 1175 / 58.94 | 1188 / 59.60 | 1.011 | 1.011 | 1 | 3621.9 | 1221/1380（88.5%） | 101.8% | 145ms | 8/8 |
| 中位 | 1175 / 58.94 | 1192 / 59.85 | **1.014** | **1.014** | 1 | 3624.8 | 1222/1380 | 101.8% | **145ms** | **pass** |

- `backward_samples=forward_samples=1200`、`backward_span=-1200`、
  `long_span_same_target_rate ok=1`（每轮）。
- 峰值 3,800,908,800B（3624.8MiB）≤ 4,311,744,512B（4112MiB），余量 487.2MiB。
- `fallback=0`、`exact=1`、`playurl=dash=0`、`preview_w×h=1920×1080`（每轮）。

### 4.3 门格检查逐条（6 次收尾复跑内 48 条 long_span check 全 ok）

| 检查 | 10s@2048（3 次） | 20s@4096（3 次） |
|---|---|---|
| `long_span_same_target_rate` | 600 vs 600 / span ±600 → ok×3 | 1200 vs 1200 / span ±1200 → ok×3 |
| `long_span_reverse_presented_ratio_ge_0_9` | 1.015 / 1.024 / 1.021 → ok×3 | 1.014 / 1.017 / 1.011 → ok×3 |
| `long_span_reverse_fps_ratio_ge_0_9` | 1.016 / 1.026 / 1.020 → ok×3 | 1.015 / 1.017 / 1.011 → ok×3 |
| `long_span_both_fps_active_ge_30` | 58.48–59.99 → ok×3 | 58.83–59.85 → ok×3 |
| `long_span_peak_le_config_budget` | 1839.1 ≤ 2064MiB → ok×3 | 3621.9–3624.8 ≤ 4112MiB → ok×3 |
| `long_span_fallback_zero` | 0 → ok×3 | 0 → ok×3 |
| `long_span_exact_landing` | 1/1 → ok×3 | 1/1 → ok×3 |
| `long_span_playurl_dash_zero` | 0/0 → ok×3 | 0/0 → ok×3 |

**负向验证（证明“显式不同值不会被静默当通过”）**：10s@2048 + `--long-span-gate`
+ **显式** `--drag-backward-samples 30`（1 次）→
`long_span_same_target_rate ok=0 detail=gate=inactive forward_samples=600
backward_samples=30 forward_span=600 backward_span=-600`，另有 3 条比值/floor 检查
也 ok=0，**`checks=36 failures=4 verdict=fail`、exit=1**；反向 presented 29、
fps_active 3.11、比 0.050/0.053（这正是漏参数时的假失败值，收尾后由自动补齐避免）。

---

## 5. 记录格（只记录，**不写“通过”**）

`long_span_record` 里 `gate=0`：不产生 `long_span_*` check 行、汇总行仍是
`checks=28 failures=0`（既有 28 项），`verdict=pass` 只代表**既有检查**通过，
**不代表**长跨度达标。

### 5.1 记录格数据（每格 3 次；40s 2 次）

| 格 | 正 presented / fps | 反 presented / fps | pres 比 | fps 比 | floor30 | peak MiB | frames/limit | 预算覆盖 | 跨度覆盖 | 1920×1080 | fallback | exact | playurl/dash | close |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|---:|---|---:|
| 2s@4096 | 115 / 59.43 | 111 / 58.42 | **0.965** | 0.982 | 1 | 403.4 | 136/1380 | 9.9% | 113.3% | 是 | 0 | 1 | 0/0 | 17ms |
| 10s@1024 | 575 / 57.88 | 527 / **53.15** | **0.917** | 0.918 | 1 | 1023.4 | 345/345 | 100% | **57.5%** | 是 | 0 | 1 | 0/0 | 87ms |
| 10s@4096 | 581 / 58.49 | 579 / 58.28 | 1.003 | 1.007 | 1 | 1839.1 | 620/1380 | 44.9% | 103.3% | 是 | 0 | 1 | 0/0 | 83ms |
| 20s@2048 | 1152 / 57.78 | 1081 / **54.28** | **0.939** | 0.940 | 1 | 2046.8 | 690/690 | 100% | **57.5%** | 是 | 0 | 1 | 0/0 | 143ms |
| 40s@4096 | 2314 / 57.96 | 2152 / 53.92 | **0.930** | 0.931 | 1 | 4093.5 | 1380/1380 | 100% | **57.5%** | 是 | 0 | 1 | 0/0 | **363ms** |

- 2s@4096 三轮：0.965 / 0.965 / 0.948（fps 比 0.982 / 0.982 / 0.965）；
  三轮都 ≥0.9，**但按计划是 record 格，不写通过**。
- 10s@1024 三轮：0.915 / 0.917 / 0.933（fps 0.918 / 0.918 / 0.933）；
  预算只能放 345 帧 < 600 帧跨度 → **反向 fps 掉到 53.0–54.9**，但 ratio 仍 ≥0.9。
- 10s@4096 三轮：0.988 / 1.003 / 1.026；620 帧（103.3% 跨度）足够，
  预算覆盖只有 44.9%（10s 用不到 4GiB）。
- 20s@2048 三轮：0.939 / 0.937 / 0.944；690 帧 = 预算上限 < 1200 帧跨度 →
  反向 54.2–54.3fps；**2048 不能覆盖 20s**。
- 40s@4096 两次：r1 正 2336/58.52、反 2138/53.59（比 0.915/0.916）；
  r2 正 2292/57.39、反 2165/54.24（比 0.945/0.945）；1380 帧 = 预算上限
  < 2400 帧跨度 → **57.5% 跨度覆盖，4GiB 覆盖不了 40s**（见 §8）。

### 5.2 1s / 2s 不退化对比（任务 §4 口径）

| 对照格 | 本轮（3 次中位） | C9 包基线 | 判定 |
|---|---|---|---|
| 1s@512 1080P | 正 **55 / 58.89**、反 55 / 58.89；比 1.000 / 1.000（单次最高 1.018 / 1.020） | 正 55 / **57.75**（D5 复跑 C9 为 58.95）、反 56 / 60.02；比 1.018 | **不退化**：forward fps 相对 57.75 **+1.97%**（±5% 内）、相对 58.95 −0.10%；反向 ≥0.9 |
| 1s@4096 1080P | 正 55 / 58.89、反 55 / 58.89；比 1.000 / 1.000 | 同上 | **不退化**（4096 对 1s 无影响：只有 81 帧，`coverage_pct=5.9%`） |
| 2s@512 1080P | 正 114 / 58.95、反 111 / 58.42；比 **0.974 / 0.992**（三轮 0.974 / 0.974 / 0.965） | 正 115 / 59.40、反 116 / 60.01；比 **1.009–1.018** | **≥0.9 未退到门槛以下**，但比 C9 低 3.5–5.3 个百分点，见下方 A/B |
| 720P 1s@512（对照，1 次） | 正 55 / **60.04**、反 55 / 59.01；比 1.000 / 0.983 | 正 56 / **58.95**、反 56 / 59.96 | forward fps **+1.85%**（±5% 内） |

**2s 漂移的同机 A/B（判断是不是 P3 代码引起）**：用 C9 发布包里的 `dist\lapian.exe`
（3,687,424B，与 `build\release\bin\lapian.exe` 同夹具、同参数、同会话）与本轮 build
各跑 3 次 2s/512：

| 二进制 | r1 pres/fps 比 | r2 | r3 | 正 / 反 presented |
|---|---|---|---|---|
| C9 `dist\lapian.exe` | 0.982 / 1.000 | 0.982 / 1.000 | 0.973 / 0.991 | 113 / 111、113 / 111、113 / 110 |
| 本轮 build | 0.965 / 0.982 | 0.956 / 0.967 | 0.966 / 0.983 | 114 / 110、114 / 109、116 / 112 |

- C9 二进制**现在**同机也只有 **0.973–0.982**，达不到它发布时的 1.009–1.018 →
  说明 2s 的比值漂移是**机器/会话级**（同机不同时段），不是 P3 代码：P3 只在
  拖动结束后读统计 + 关预览，**在正/反拖动期间一行不跑**。
- 本例仍按“2s/120 512 不得 <0.9”判 **不退化（0.965–0.974）**，但**不写“与 C9 完全一致”**。

---

## 6. 内存与关闭释放

### 6.1 内存（`peak ≤ 配置 + 16MiB`；`fallback=0`）

| 格 | 配置 MiB | peak B | peak MiB | 预算+16MiB | 余量 MiB | frames/limit | 结果 |
|---|---:|---:|---:|---:|---:|---:|---|
| 1s@512 | 512 | 251,942,400 | 240.27 | 528 | 287.7 | 81/172 | 在限内 |
| 1s@4096 | 4096 | 251,942,400 | 240.27 | 4112 | 3871.7 | 81/1380 | 在限内 |
| 2s@512 | 512 | 423,014,400 | 403.42 | 528 | 124.6 | 136/172 | 在限内 |
| 2s@4096 | 4096 | 423,014,400 | 403.42 | 4112 | 3708.6 | 136/1380 | 在限内 |
| 10s@1024 | 1024 | 1,073,088,000 | 1023.4 | 1040 | 16.6 | 345/345 | 在限内（100% 预算） |
| 10s@2048 | 2048 | 1,928,448,000 | 1839.1 | 2064 | 224.9 | 620/690 | 在限内 |
| 10s@4096 | 4096 | 1,928,448,000 | 1839.1 | 4112 | 2272.9 | 620/1380 | 在限内 |
| 20s@2048 | 2048 | 2,146,176,000 | 2046.8 | 2064 | 17.2 | 690/690 | 在限内（100% 预算） |
| 20s@4096 | 4096 | 3,800,908,800 | 3624.8 | 4112 | 487.2 | 1222/1380 | 在限内 |
| 40s@4096 | 4096 | 4,292,352,000 | 4093.5 | 4112 | **18.5** | 1380/1380 | 在限内（100% 预算） |

- 29 次矩阵运行 `fallback=0` 全成立；**4GiB 档不 OOM、不崩**（20s/40s 各自跑完
  并正常退出）；4096 档峰值只到 3.8–4.29GB，因为缓存是**按需增长**不是预分配。
- 2GB/4GB 档满预算时余量只有 16.6–18.5MiB，属于“刚好 ≤ 配置+16MiB”；
  没有一次超限。
- 说明：这里量的是**预览解码器缓存字节**（`OnlinePreviewCachePeakBytesForTest`），
  不是进程工作集；进程 RSS 未单独采集（见 §8 缺口 5）。

### 6.2 关闭释放耗时（围绕 `StopPreviewForTest()`，只记录不判定）

| 格 | 关闭前 frames | `preview_close_ms` | 备注 |
|---|---:|---:|---|
| 1s@512 / 1s@4096 | 81 | **9–10 / 10** | 与预算无关（只缓存 81 帧） |
| 2s@512 / 2s@4096 | 136 | **16–17 / 16–18** | |
| 10s@1024 | 345 | **79–90** | |
| 10s@2048 / 10s@4096 | 620 | **62–64**（收尾复跑）/ **80–87** | |
| 20s@2048 | 690 | **136–188** | |
| 20s@4096 | 1222 | **139–149**（收尾复跑；收尾前 151–155） | < 200ms |
| 40s@4096 | 1380 | **331–395** | **> 200ms → gap（§8 第 2 条）** |

- 释放耗时随帧数增长（1s 10ms → 40s 331–395ms），4GiB 满缓存关闭约 **0.33–0.40s**；
  计划 §5 的口径是“>200ms 记为缺口”，故 40s 记 gap，20s（139–149ms）不算。
- 该测量在长拖结束后立刻进行（预览器仍 Pause 且缓存保留），随后短拖/关窗路径
  会重建预览器；1s/2s 的既有 28 项检查未因此失败。

---

## 7. 回归（P3 改动后实测）

### 7.1 构建与单测

| 项 | 结果 | 备注 |
|---|---|---|
| `cmake --build build\release --config Release --target lapian` | 成功（收尾后 11.9s） | 只有 `main.cpp` / `OnlineDragPreviewTestRunner.cpp` 重编 |
| `ctest --test-dir build\release -C Release` | **38/38 pass**（收尾后 66.48s） | `TEMP/TMP=E:\拉片软件开发\.deps\tmp`、Qt `bin` 在 PATH |

### 7.2 既有探针（`media\测试.mp4`）

| 项 | 结果 | 备注 |
|---|---|---|
| `--loop-test --file media\测试.mp4` | **exit 0，41 `loop_check ok=1` / 0 fail**（38.3s） | L1 未受影响：`loop_test_summary failures=0`、`after-3-loops frame=119`、`space-after-end frame=13 playing=1` |
| `--key-test --file media\测试.mp4` | **77/0**（10.1s） | 无 flake |
| `--drag-test --drag-samples 20`（2 次） | P4 **64.3 / 64.4fps**，P6 p95 **28.6 / 15.3ms**，P7 0.0/13.9 与 0.0/12.1，P8 12.3/12.7（pass），P9 39.9/37.2 | P4 与近期 64.5–96（本机计时器粒度）同档；P6 在 15–29ms 区间、阈值 150ms |
| growing 1s@512（1 次，`--m1-throttle-bps 262144`，空缓存） | 正 54/59.93、反 53/58.95，比 0.981/0.984 | **36/0 pass**：`preview_close_ms=8` 后 `RunUncachedPass` 仍全过，说明新增的“拖动结束关预览”不影响增长会话尾段 |

### 7.3 Medium 副本复验

`dist\*` → `%LOCALAPPDATA%\Programs\lapian-preview`，再用收尾后的
`build\release\bin\lapian.exe` 覆盖（**Medium exe SHA256 与 build 一致**：
`84F017B5…ADF0AC21`、3,753,984B）。从该副本重跑 5 格（**不写共享夹具**，
仍复制到 `build\p3\medium-*`）：

| 场景 | 正 presented / fps | 反 presented / fps | 比 | frames/limit | peak MiB | close | 结果 |
|---|---:|---:|---:|---:|---:|---:|---|
| 1s@512 | 55 / 58.89 | 56 / 59.89 | 1.018 / 1.017 | 81/172 | 240.3 | 8ms | 28/0 pass |
| 2s@512 | 115 / 59.49 | 109 / 57.37 | 0.948 / 0.964 | 136/172 | 403.4 | 13ms | 28/0 pass |
| 10s@2048 **gate** | 588 / 59.18 | 595 / 59.90 | 1.012 / 1.012 | 620/690 | 1839.1 | 65ms | **36/0，8/8 gate** |
| 20s@4096 **gate** | 1173 / 58.84 | 1189 / 59.64 | 1.014 / 1.014 | 1222/1380 | 3624.8 | 151ms | **36/0，8/8 gate** |
| 40s@4096 记录 | 2293 / 57.42 | 2169 / 54.34 | 0.946 / 0.946 | 1380/1380 | 4093.5 | 324ms | 28/0（record，不写通过） |

结论：Medium 副本与 build 目录同 hash、同结论；未出现“只有 build 目录能过”。
1s/2s/40s 三行沿用同一代码路径的收尾前 build（`611BAB28…`）结果；10s/20s 两个
gate 行与 §4 一样用**收尾后** exe（`84F017B5…`、3,753,984B，Medium 与 build 同
hash）复跑，均为 36/0、8/8（含 `long_span_same_target_rate ok=1`）。

---

## 8. 未达 / 失败 / 残余（如实）

1. **40s@4096：4GiB 覆盖不了 2400 帧跨度**。`span_coverage_pct=57.5% < 80%`，
   按计划 §4.3 只记录、**不写通过、不宣称覆盖**。原因：2400 帧 × 3,110,400B =
   7,464,960,000B ≈ 6.95GiB > 4GiB（`frames/limit=100%`）。实测正向 presented
   95.5–97.3%、反向 89.1–90.2%，**瓶颈是容量不是解码吞吐**——所以本轮的结论是
   “4GiB 的收益在 40s 受**容量**限制”，而不是计划 §5 预设的“解码吞吐限制”。
   （20s 探索格两个覆盖率都 ≥80%，没有出现该限制。）
2. **关闭释放 4GiB 超 200ms**：40s@4096 `preview_close_ms=331–395ms`（20s 为
   139–149ms）。只记录；按计划 §5 记为 gap，不是门。
3. **2s/512 比 C9 低 3.5–5.3 个百分点**（本轮 0.965–0.974 vs C9 1.009–1.018）：
   仍在 ≥0.9 门内；同机 A/B 显示 **C9 自己的二进制现在也只有 0.973–0.982**，
   属环境/会话漂移；如实写“未达 C9 发布值”，不写成 P3 回归也不写成完全一致。
4. **10s@1024、20s@2048 是预算受限**：`frames/limit=100%` 但 span 覆盖 57.5%
   （345/690 帧 < 600/1200 帧跨度），反向 fps 掉到 53–54fps；这两格是 record，
   **不写通过**。
5. **进程工作集未单独采集**：§6.1 的 peak 是预览解码器缓存字节（探针既有口径），
   不是进程 RSS；4GiB 档完整内存占用（缓存 + 解码器 + Qt）未量。
6. **growing 路径未跑长跨度**：矩阵按计划 §3 P3 只做 1080P **cache-hit**；
   `--long-span-gate` 对 growing 只把 `long_span_playurl_dash_zero` 恒真，
   未做真实增长会话的长跨度证据。
7. **1s/2s 只有 1080P 全矩阵 + 720P 1s 一次对照**：720P 2s、720P 长跨度未跑
   （本轮范围外）。
8. `--online-quality-test`（SESSDATA 失效）/4K/60fps 等不在本轮范围，未跑、
   不写通过。
9. **除 20s@4096 外没有“已覆盖”结论**：1s/2s（预算覆盖 5.9–79.1%）、
   10s@1024（span 57.5%）、20s@2048（span 57.5%）、40s@4096（span 57.5%）都 <80%；
   原因是**跨度帧数少于预算**（1s/2s）或**预算帧数少于跨度**（10s@1024 /
   20s@2048 / 40s@4096），不是解码吞吐限制（presented 57–98%）。
   这些格只记录，不写通过/覆盖。
10. **收尾前的“漏 `--drag-backward-full` 假失败”陷阱已修**（本轮收尾）：
    `--long-span-gate` 现在会自动补齐反向目标数；即使显式给了不同值，也会由
    `long_span_same_target_rate` 判 fail（`gate=inactive`），不会静默通过。
    负向验证见 §4.3（`checks=36 failures=4 verdict=fail`）。

---

## 9. 判定汇总

| 验收项 | 结果 |
|---|---|
| 探索格 20s@4096：预算覆盖 / 跨度覆盖 | **88.5–88.6% / 101.8%**（均 ≥80%，20s 算覆盖） |
| 10s@2048 长跨度门（同目标率 + 反向比、fps 比、双方 ≥30、peak、fallback、exact、0 网络） | **pass**，3/3 次 × **8/8** 条（36/0/轮） |
| 20s@4096 长跨度门（同上） | **pass**，3/3 次 × **8/8** 条（36/0/轮） |
| 显式反向目标数不匹配（负向验证） | **fail（按设计）**：`gate=inactive`、`checks=36 failures=4 verdict=fail`，不静默通过 |
| 40s@4096 | **record，不写通过**（span 覆盖 57.5% <80%，容量限制） |
| 1s/512、1s/4096 对照不退化 | **pass**（比 1.000–1.018，正向 fps 58.82–58.95 在 C9 57.75/58.95 的 ±5% 内） |
| 2s/512 对照 ≥0.9 | **pass**（0.965–0.974；未达 C9 1.009–1.018，已给同机 A/B 解释） |
| 2s@4096 / 10s@1024 / 10s@4096 / 20s@2048 / 40s@4096 | **record**（0.917–1.003），不写通过 |
| 内存 `peak ≤ 配置+16MiB`、`fallback=0`、4GiB 不 OOM/不崩 | **pass**（29/29；最高 4093.5MiB，余量 18.5MiB） |
| 关闭释放 4GiB 记录 | **20s 139–149ms；40s 331–395ms（>200ms gap）** |
| 回归 ctest 38/38、loop 41/0、key 77/0、drag P4/P6、Medium 5 格 | **pass** |
| 未改产品/阈值/1s 2s 判据/`online_drag_forward_backward_parity` | **是**（§1.3） |

**P3 结论：长跨度探针与诊断已落地；两个计划门格（10s@2GiB、20s@4GiB）全过，
20s 由 4GiB 完整覆盖；1s/2s 对照无方向性退化（2s 比值下降已用同机 A/B 归因环境）；
40s 只记录并明确“4GiB 覆盖不了 40s（容量限制）”，未写通过。收尾后长跨度门强制
“同目标率 + 同跨度”（自动补齐 + 显式检查），漏参数不会再假失败或静默通过。
失败/缺口按 §8 如实保留。**

---

## 10. 原始日志与复现

- 矩阵 29 次运行：`build\p3\p3-<cell>-r<n>\logs\run.err.log`；汇总
  `build\p3\matrix.tsv` / `build\p3\matrix.json`（脚本 `build\p3\run_matrix.ps1`、
  `build\p3\collect.ps1`，均 ASCII-only）。
- 关键 tag：`p3-1s-512-r{1,2,3}`、`p3-1s-4096-r{1,2,3}`、`p3-2s-512-r{1,2,3}`、
  `p3-2s-4096-r{1,2,3}`、`p3-10s-1024-r{1,2,3}`、`p3-10s-2048-r{1,2,3}`（gate）、
  `p3-10s-4096-r{1,2,3}`、`p3-20s-2048-r{1,2,3}`、`p3-20s-4096-r{1,2,3}`（gate）、
  `p3-40s-4096-r{1,2}`。
- 收尾复跑：`build\p3\fix-10s-2048-r{1,2,3}`、`build\p3\fix-20s-4096-r{1,2,3}`
  （**只带 `--long-span-gate`，不带 `--drag-backward-full`**）、
  `build\p3\fix-negative-explicit-30`（显式 `--drag-backward-samples 30` → fail）。
- 附加：`build\p3\ab-2s512-{c9,p3}-r{1,2,3}`（§5.2 A/B）、
  `build\p3\medium-{1s,2s,10s,20s,40s}-*`（§7.3）、`build\p3\720p-1s-512-r1`、
  `build\p3\auto-samples-2s`（不给 `--drag-samples` 时 `samples=120` 验证）、
  `build\p3\growing-1s-512-r1`（§7.2 growing 冒烟）、
  `build\p3\regress\{loop,key,drag,drag2}.err.log`。
- 复现全矩阵（仓库根，GUI 探针需 Qt `bin` 在 PATH）：
  `powershell -NoProfile -ExecutionPolicy Bypass -File .\build\p3\run_matrix.ps1`
  （约 11 分钟；每格先复制夹具、隔离 settings/records/pictures）。
- 只复跑 20s@4096 探索/门格：
  `powershell -NoProfile -ExecutionPolicy Bypass -File .\build\p3\run_matrix.ps1 -Only '20s-4096'`。
