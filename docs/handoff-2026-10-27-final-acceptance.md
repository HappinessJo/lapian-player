# 交接：4096 长跨度 + 循环修复 + 工作区记忆 — P4 独立验收（2026-10-27）

> **verdict：`pass with gaps`**
>
> - 硬门全过：10s@2GiB / 20s@4GiB 长跨度门 3/3 轮 × 8/8 条；1s/2s 反向比 1.000–1.028
>   （同机 C9 二进制 1.000–1.036）、双方 fps ≥58.2；4096 档不崩不 OOM、peak ≤ 配置+16MiB；
>   循环修复后「播放禁用 + 帧冻结」未复现（本地 65/0、在线 32/0）；工作区保存/恢复/指纹/清空
>   全过且不串视频；设置页 100%/200% 各 **149/0**；回归 ctest **38/38** 等全绿。
> - gap 全部是环境/计划内记录项：`--online-quality-test` 因 SESSDATA 失效 **blocked**、
>   40s@4GiB `span_coverage 57.5%`（容量限制，只记录）、40s 关闭释放 **355/442ms >200ms**、
>   无 60fps 源、4096 设置端到端在本次可用内存 14.7GiB 下被 25% 护栏夹到 3763MiB 等，逐条见 §11。
>
> 验收对象：`master` HEAD **`e1afb33`**（工作树干净）。本 agent **只新增本文件**；未改产品/测试/
> 门/阈值/spec/CMake/installer；未打包/安装；未动 `lapian-c8`、C: 安装版、用户 E: 免安装版。
>
> 独立性：自建 `build\p4\…`；每个 cell 先 `Copy-Item` 夹具（**不写共享夹具**）；独立
> `--settings-path/--online-cache-root/--online-pictures/--online-records-root`；GUI exe 一律
> `Start-Process -Wait -RedirectStandardOutput/-RedirectStandardError`；**没有复用实现者脚本/日志**。

---

## 0. 一句话数字

- 10s@2048 门：正 presented 579–585 / fps 58.28–58.88，反 591–597 / 59.49–60.09，
  比 **1.021–1.023**；peak 1839.1MiB ≤ 2064；`preview_close_ms` 79–80。
- 20s@4096 门：正 1171–1177 / 58.74–59.04，反 1192–1196 / 59.79–59.99，比 **1.013–1.021**；
  peak 3621.9–3624.8MiB ≤ 4112；`preview_close_ms` 160–164。
- 1s@512：比 **1.018–1.020**，正 58.82–58.89fps（同机 C9 二进制 1.018–1.037 / 57.82–58.76）。
- 2s@512：比 **1.000–1.028**，正 58.37–59.95fps（同机 C9 二进制 1.000–1.036 / 57.85–59.37）。
- 40s@4096 记录：比 0.929/0.939，`frames/limit=1380/1380`，`span_coverage 57.5%`，
  peak 4093.5MiB ≤ 4112，close **355/442ms**（>200 gap）。
- 循环：本地 `--loop-test` **65/0**、在线缓存命中 **32/0**；用户失败形态
  `disabled=1 playing=1 play_enabled=1 end_count==baseline`（**无 reachedEnd**）。
- 工作区：本地 sidecar `in=50/out=90` 新进程恢复（截图「已恢复上次工作区 A=50 B=90（循环关）」）；
  size/mtime 不符不恢复；在线 `BV1BMnv6jE5S-42915922022.lapian-workrange.json` 重开 `loop=0`。
- 设置页：100% 与 200% 各 **149/0、verdict=pass、dialog-fits OK**；范围文本含 4096/本机 25%/多实例；
  默认预设为「默认（自动 / 512MiB）」。
- 回归：ctest **38/38**（68.97s）、timeline **188/0**、mark store **281/0**、key **77/0**、
  banner **74/0**、dpi 4×**37/0**（problems 空）、drag P4 **94.0fps**/P6 **17.9ms**、
  B 720P/1080P **38/0 ×2**、offline **45/0**、quality **blocked**（16 项/1 fail，不写通过）。

---

## 1. 基线与环境

| 项 | 值 |
|---|---|
| 仓库 / 分支 / HEAD | `E:\拉片软件开发` / `master` / `e1afb33`（验收前 `git status --porcelain` 空） |
| 增量构建 | `cmake --build build\release --config Release --target lapian`，**exit 0**（已最新） |
| 产品 exe | `build\release\bin\lapian.exe` **3,829,248B**，SHA256 `80EC7FD4…B4B4F`（记前 8 位） |
| Medium 副本 | `dist\*` → `%LOCALAPPDATA%\Programs\lapian-preview`，再用上面的 exe 覆盖；副本 exe 同 hash `80EC7FD4…` |
| C9 对照二进制 | `dist\lapian.exe` **3,687,424B** / SHA256 `588B8D5E…1E54E`（只读，未改 dist） |
| 1080P 夹具（只读） | `build\pm-b-fresh\cache`：`BV1BMnv6jE5S/42915922022`，qn=80，1920×1080@30，`80-7/data.mp4` 31,057,770B |
| 720P 夹具（只读，B 用） | `build\c2-720p\cache`：`BV1cW411b7Yh/28925848`，qn=64，1280×720@29.412 |
| 本地媒体 | `media\测试.mp4`（201,407,930B，1920×1080@30，4573 帧）；跨进程用 `build\p4\video.mp4`（副本） |
| 机器内存 | 验收开始可用物理 **16,534MiB**（25%≈4133MiB，4096 可设满）；验收末尾约 **15.3GiB**（见 §11.5） |
| 网络/凭据 | `--online-http-selfcheck`：`http_status=200`、83ms、`cookie_sent=1`、**`vip_status=-1`、`-101`**（SESSDATA 失效；匿名仍可 growing/目录） |
| TEMP | `TEMP/TMP=E:\拉片软件开发\.deps\tmp`（与项目构建脚本一致） |
| 采样 | 每门格 **3 次**、record 格 1–2 次（计划允许）、C9 A/B 各 3 次；全部尝试进表，不挑最好一次 |

### 1.1 本轮运行次数

A 矩阵 **23 次**（门格 3+3、不退化 3+3、1s/2s 的 C9 对照 3、记录格 2+2+2+2），C9 同机 A/B
另 **1s×3 + 2s×3（显式 60/120 目标）**，Medium 副本 **2 次**，B **4 次**，C **1 次 loop-test +
5 次 dpi 截图探针**，D **2 次 UI + 1 次 settings e2e**，E **14 项**（ctest/timeline/markstore/
key/banner/dpi×4/drag/B×2/offline/quality/selfcheck）。共约 **59 次**进程运行。

---

## 2. A. 4096 长跨度（自跑，`--online-drag-preview-test --drag-cache-hit`）

命令骨架（门格**只带 `--long-span-gate`、不带 `--drag-backward-full`**；由 main.cpp 自动补齐反向目标数）：

```powershell
Start-Process -Wait -FilePath build\release\bin\lapian.exe -ArgumentList @(
  '--online-drag-preview-test','--drag-cache-hit','--bvid','BV1BMnv6jE5S','--cid','42915922022',
  '--drag-seconds','20','--preview-resolution','auto','--preview-cache-mb','4096',
  '--long-span-gate',
  '--online-cache-root','build\p4\cells\a-gate-20s-4096-r1\cache',
  '--online-pictures','build\p4\cells\a-gate-20s-4096-r1\pics',
  '--online-records-root','build\p4\cells\a-gate-20s-4096-r1\records',
  '--settings-path','build\p4\cells\a-gate-20s-4096-r1\settings','--timeout-ms','300000') `
  -RedirectStandardOutput build\p4\logs\a-gate-20s-4096-r1.out.log `
  -RedirectStandardError  build\p4\logs\a-gate-20s-4096-r1.err.log
```

### 2.1 门格（每轮 36 checks / 0 fail = 28 既有 + 8 条 long_span）

**10s@2048（3/3 轮全过，24/24 条）**

| # | 正 presented / fps_active | 反 presented / fps_active | pres 比 | fps 比 | 双方≥30 | peak MiB | frames/limit | 预算覆盖 | 跨度覆盖 | close | checks |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| r1 | 582 / 58.57 | 595 / 59.89 | 1.022 | 1.023 | 1 | 1839.1 | 620/690 | 89.9% | 103.3% | 80ms | 36/0 |
| r2 | 579 / 58.28 | 591 / 59.49 | 1.021 | 1.021 | 1 | 1839.1 | 620/690 | 89.9% | 103.3% | 79ms | 36/0 |
| r3 | 585 / 58.88 | 597 / 60.09 | 1.021 | 1.021 | 1 | 1839.1 | 620/690 | 89.9% | 103.3% | 80ms | 36/0 |

- `backward_samples == forward_samples = 600`、`backward_span=-600`（`long_span_same_target_rate ok=1` 每轮）。
- `preview_w×h=1920×1080`、`fallback=0`、`exact=1`、`playurl=dash=0`（每轮）。
- 峰值 1,928,448,000B（1839.1MiB）≤ 2,164,260,864B（2064MiB），余量 224.9MiB。

**20s@4096（3/3 轮全过，24/24 条）**

| # | 正 presented / fps_active | 反 presented / fps_active | pres 比 | fps 比 | 双方≥30 | peak MiB | frames/limit | 预算覆盖 | 跨度覆盖 | close | checks |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| r1 | 1177 / 59.04 | 1193 / 59.84 | 1.014 | 1.014 | 1 | 3624.8 | 1222/1380 | 88.6% | 101.8% | 162ms | 36/0 |
| r2 | 1171 / 58.74 | 1196 / 59.99 | 1.021 | 1.021 | 1 | 3624.8 | 1222/1380 | 88.6% | 101.8% | 160ms | 36/0 |
| r3 | 1177 / 59.04 | 1192 / 59.79 | 1.013 | 1.013 | 1 | 3621.9 | 1221/1380 | 88.5% | 101.8% | 164ms | 36/0 |

- `backward_samples == forward_samples = 1200`、`backward_span=-1200`（每轮 ok=1）。
- 峰值 3,800,908,800B（3624.8MiB）/ 3,797,798,400B（3621.9MiB）≤ 4,311,744,512B（4112MiB），
  余量 487.2MiB。
- `fallback=0`、`exact=1`、`playurl=dash=0`、`preview_w×h=1920×1080`（每轮）。

**8 条 long_span 检查逐条**（6 轮 = 48/48 ok）：`same_target_rate`、`reverse_presented_ratio_ge_0_9`、
`reverse_fps_ratio_ge_0_9`、`both_fps_active_ge_30`、`peak_le_config_budget`、`fallback_zero`、
`exact_landing`、`playurl_dash_zero`——全部 ok=1（日志 `online_drag_preview_check … ok=1`）。

### 2.2 不退化（1s/2s 硬门）与 C9 同机 A/B

| 格 / 二进制 | # | 正 presented / fps | 反 presented / fps | pres 比 | fps 比 | peak MiB | frames/limit | close |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1s@512 新 build | r1 | 55 / 58.82 | 56 / 59.96 | 1.018 | 1.019 | 240.27 | 81/172 | 10ms |
| 1s@512 新 build | r2 | 55 / 58.82 | 56 / 60.02 | 1.018 | 1.020 | 240.27 | 81/172 | 9ms |
| 1s@512 新 build | r3 | 55 / 58.89 | 56 / 60.02 | 1.018 | 1.019 | 240.27 | 81/172 | 9ms |
| 1s@512 **C9** | r1 | 54 / 57.82 | 56 / 59.83 | 1.037 | 1.035 | 240.27 | 81/172 | — |
| 1s@512 **C9** | r2 | 55 / 58.76 | 56 / 59.96 | 1.018 | 1.020 | 240.27 | 81/172 | — |
| 1s@512 **C9** | r3 | 55 / 58.70 | 56 / 59.83 | 1.018 | 1.019 | 240.27 | 81/172 | — |
| 2s@512 新 build | r1 | 116 / 59.95 | 116 / 59.95 | 1.000 | 1.000 | 403.42 | 136/172 | 17ms |
| 2s@512 新 build | r2 | 114 / 58.98 | 116 / 59.95 | 1.018 | 1.016 | 403.42 | 136/172 | 17ms |
| 2s@512 新 build | r3 | 113 / 58.37 | 116 / 60.01 | 1.027 | 1.028 | 403.42 | 136/172 | 16ms |
| 2s@512 **C9** | r1 | 112 / 57.85 | 116 / 59.95 | 1.036 | 1.036 | 403.42 | 136/172 | — |
| 2s@512 **C9** | r2 | 115 / 59.37 | 115 / 59.49 | 1.000 | 1.002 | 403.42 | 136/172 | — |
| 2s@512 **C9** | r3 | 114 / 58.98 | 116 / 59.86 | 1.018 | 1.015 | 403.42 | 136/172 | — |

- **1s** 本轮 1.018–1.020，与 C9 发布值 1.018 同值；正向 fps 58.82–58.89，对比 C9 同机
  57.82–58.76（**+0.1%…+1.9%**），对比 D5/C9 发布 57.75（**+1.9%**），**无 >5% 下降**。
- **2s** 本轮 1.000–1.028，≥0.9 硬门；正向 fps 58.37–59.95，对比 C9 同机 57.85–59.37
  （**+0.9%…+1.0%**），对比 C9 发布 1.009–1.018 的比值差异 <3 个百分点，**无方向性退化**。
- 对照口径：新 build 另跑 1 次 **显式 `--drag-samples 120`** 的 2s（1.018/1.017，136→140/172），
  与自动 60×S 的结果同档，说明 2s 差异不是“目标数不同”。
- **C9 的默认目标数陷阱（测量注记）**：C9 二进制没有 “`--drag-seconds` 自动 60×S” 逻辑，
  不显式给 `--drag-samples` 时 2s 只有 60 个目标（30Hz），实测比 0.935–0.952、`fps_active≈30–32`；
  这是**目标率**差异，不是产品退化。故 C9 A/B 必须显式 `--drag-samples`（本轮用 120）。

### 2.3 记录格（**只记录，不写通过**；`gate=0`，checks=28/0 只代表既有 28 项）

| 格 | # | 正 presented / fps | 反 presented / fps | pres 比 | fps 比 | peak MiB | frames/limit | 预算覆盖 | 跨度覆盖 | close |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 2s@4096 | r1 | 116 / 59.92 | 116 / 59.86 | 1.000 | 0.999 | 403.42 | 136/1380 | 9.9% | 113.3% | 17ms |
| 2s@4096 | r2 | 115 / 59.43 | 117 / 60.47 | 1.017 | 1.017 | 403.42 | 136/1380 | 9.9% | 113.3% | 17ms |
| 10s@1024 | r1 | 585 / 58.88 | 549 / 55.26 | 0.938 | 0.939 | 1023.4 | 345/345 | 100% | 57.5% | 81ms |
| 10s@1024 | r2 | 587 / 59.08 | 548 / 55.16 | 0.934 | 0.934 | 1023.4 | 345/345 | 100% | 57.5% | 79ms |
| 20s@2048 | r1 | 1166 / 58.49 | 1119 / 56.13 | 0.960 | 0.960 | 2046.8 | 690/690 | 100% | 57.5% | 156ms |
| 20s@2048 | r2 | 1177 / 59.04 | 1115 / 55.94 | 0.947 | 0.947 | 2046.8 | 690/690 | 100% | 57.5% | 161ms |
| 40s@4096 | r1 | 2322 / 58.14 | 2157 / 54.06 | 0.929 | 0.930 | 4093.5 | 1380/1380 | 100% | **57.5%** | **442ms** |
| 40s@4096 | r2 | 2346 / 58.75 | 2204 / 55.19 | 0.939 | 0.939 | 4093.5 | 1380/1380 | 100% | **57.5%** | **355ms** |

- **40s 覆盖不足已核实**：4GiB 只能放 1380 帧，跨度 2400 帧 → `span_coverage_pct=57.5% < 80%`；
  反向 fps 掉到 54–55 但比值仍 0.929–0.939（容量限制，不是解码吞吐）。
- 10s@1024、20s@2048 同理是预算受限（345/690 帧 < 600/1200 帧跨度），只记录。
- 2s@4096 预算覆盖 9.9%（跨度只 120 帧），**不表示 4GiB 被用满**。

### 2.4 内存 / 关闭释放

| 格 | 配置 MiB | peak 字节 | peak MiB | 上限（配置+16） | 余量 MiB | fallback |
|---|---:|---:|---:|---:|---:|---:|
| 10s@2048 | 2048 | 1,928,448,000 | 1839.1 | 2064 | 224.9 | 0 |
| 20s@4096 | 4096 | 3,800,908,800 | 3624.8 | 4112 | 487.2 | 0 |
| 20s@4096 | 4096 | 3,797,798,400 | 3621.9 | 4112 | 490.1 | 0 |
| 10s@1024 | 1024 | 1,073,088,000 | 1023.4 | 1040 | 16.6 | 0 |
| 20s@2048 | 2048 | 2,146,176,000 | 2046.8 | 2064 | 17.2 | 0 |
| 40s@4096 | 4096 | 4,292,352,000 | 4093.5 | 4112 | **18.5** | 0 |
| 1s@512 / 2s@512 / 2s@4096 | 512/512/4096 | 251,942,400 / 423,014,400 | 240.27 / 403.42 | 528/528/4112 | — | 0 |

- 全部运行 `fallback=0`；**4GiB 档无 OOM、无崩溃**（所有运行 exit=0）。
- 观察（非门）：20s@4096 运行中 `lapian.exe` 进程工作集约 4.13GB、40s@4096 约 4.34GB（单点采样，
  不是全量 RSS 审计；探针口径仍是预览缓存字节）。
- 关闭释放：1s 9–10ms、2s 16–17ms、10s 79–81ms、20s@2048 156–161ms、20s@4096 160–164ms、
  **40s@4096 355/442ms（>200ms → gap）**。

### 2.5 Medium 副本（打包布局，只抽 2 格）

| 场景 | 结果 |
|---|---|
| `m-1s-512-r1` | 1920/512，正 55/58.76、反 56/59.89，比 1.018/1.019，peak 240.27，28/0，close 10ms |
| `m-gate-10s-2048-r1` | 1920/2048，正 581/58.46、反 596/59.97，比 1.026/1.026，peak 1839.1，**36/0（8/8 gate）**，close 113ms |

副本 exe hash 与 build 相同；没有“只有 build 目录能过”。

---

## 3. B. 循环卡死 / 空格失效修复（用户场景）

### 3.1 本地 `--loop-test --file media\测试.mp4`：**65 checks / 0 fail**（46.4s）

| 相位 | 实测 |
|---|---|
| 3 次循环后（A=100/B=160） | `jumps≥3`、`loop=1 a=100 b=160` 全过 |
| 结束态 | `loop_end_probe end_count=1 baseline=0 play_enabled=1 text=播放 status=播放结束（Space 从头播放）` → `space-after-end frame=14 playing=1`（**Space 可重播**） |
| **用户失败形态**（`SetFailLoopSeeksForTest(3)`） | `loop_fail_probe disabled=1 playing=1 play_enabled=1 end_count=1 baseline=1 loop=0 a=200 b=260` → **没有 reachedEnd**；随后 Space 暂停/继续 2 条 check 全过 |
| A=B 定帧 | `status=A=B 定帧（L 关闭 / PgDn 离开） · 帧 100（画面保持、音频静音）`、`frozen=1 playing=1 audio_clock=0 audio_state=3 audio_pos=11.577` → **音频暂停**；PgDn 离开 101、Space 回 100、关循环恢复播放全过 |
| 在线预检拦截（gate=false） | `disabled=2 playing=1 play_enabled=1 jumps=4 baseline_jumps=4 end_count=1 status=A 尚未缓存，已关闭循环` → 不做同步 seek、只关循环 |

### 3.2 在线缓存命中 `--loop-test --loop-online-cache-hit`：**32 checks / 0 fail**（13.1s）

夹具复制到 `build\p4\cells\b-loop-online\cache`，记录/图片/设置隔离。

- `loop_online ready=1 cache_hit=1 index=1 frames=3314`；
- 三轮采样（每轮都发生回绕，帧号回落到 100..160 区间内；`jumps` 1→2→3 证明每轮都在前进、
  `loop-online-jumps-advance ok=1`；播放始终 enabled、**end_count=0**）：

| 采样 | frame | jumps | play_enabled | playing | end_count | loop |
|---|---:|---:|---:|---:|---:|---|
| sample-1 | 128 | 1 | 1 | 1 | 0 | loop=1 a=100 b=160 |
| sample-2 | 122 | 2 | 1 | 1 | 0 | loop=1 a=100 b=160 |
| sample-3 | 116 | 3 | 1 | 1 | 0 | loop=1 a=100 b=160 |

- `loop_online loop_disabled=0`；`playurl=0 dash=0`（cache-hit 0 网络）；Space 暂停/继续各 1 条 check 通过。
- **结论：用户「只在线、A≠B 也卡、帧号不动、播放按钮灰色」的失败形态未复现**（每轮都播放并回绕、
  jumps 递增、play action 全程 enabled、无 reachedEnd、无 loop-disabled）。

### 3.3 3D 热修不回归：`--scrub-space-test`

| 场景 | 结果 |
|---|---|
| 本地 | `scrub_space_result mode=local checks=62 failures=0 verdict=pass`（24.2s） |
| 在线 growing（空缓存根，真实取流） | `scrub_space_result mode=online checks=64 failures=0 verdict=pass`（24.2s） |

---

## 4. C. 工作区 A/B 按视频记忆

### 4.1 本地 `--loop-test`（65/0 内含 W1 相位）

| 相位 | 实测 |
|---|---|
| full-range-default | 无记录 → `has_record=0 a=0 b=4572 frames=4573`、`loop=0 has_in=0`、文件不存在 |
| save-reload | 设 100/160 + flush → `saves 5→6`、文件存在、JSON `in=100 out=160`；重开 → `a=100 b=160 has_record=1 loop=0`、状态「已恢复上次工作区 A=100 B=160（循环关）」 |
| debounce | 连续 5 次改（101…105/161…165）→ `saves delta=1`、`a=105 b=165`、JSON `105/165` |
| fingerprint-mismatch | 写错 `size`(+7) → `has_record=0 a=0 b=4572`；再写错 `mtime_ms`(−60s) → 同样不恢复 |
| clear | Shift+L → 文件删除、`loop=0 has_in=0`；重开 → `has_record=0 a=0 b=4572` |

### 4.2 跨进程（本地 `build\p4\video.mp4`，副本，不写 media/）

1. **保存**：`--loop-test --file build\p4\video.mp4 --file-b build\p4\video-b.mp4`（68/0）结束留下：
   - `video.mp4.lapian-workrange.json`：`schema=1 in=50 out=90 kind=local size=201407930
     mtime_ms=1787034902434 frames=4573`；
   - `video-b.mp4.lapian-workrange.json`：`in=1143 out=2286 frames=4573`（B 自己 1/4..1/2）——
     **A/B 不串视频**。
2. **新进程恢复**：`--dpi-test --file build\p4\video.mp4`（产品窗口 `grab()` 截图；
   原生视频区在黑底，UI chrome 完整）：
   - 状态栏：**「已恢复上次工作区 A=50 B=90（循环关）」**；
   - 时间轴头：**「工作区 50–90 · 循环关」**、A/B 手柄在第 50/90 帧；
   - 即新进程 `has_record=1 a=50 b=90`、**循环仍为关**（无程序化 getter 时的可视/文本证据，见 §11.7）。
3. **手工 schema-1 seed A=200/B=260**（size/mtime/frames 与真实文件一致）→ 新进程截图：
   `status=已恢复上次工作区 A=200 B=260（循环关）`、头「工作区 200–260 · 循环关」。
4. **size 不符**（+7）→ 新进程：无恢复提示、头「工作区 全片 · 循环关」、状态「已打开，停在 第 0 帧」。
5. **mtime 不符**（文件 mtime −60s）→ 同上（截图与 size 不符完全相同，sha256 一致）。
6. **全片 0..4572** 记录 → 与“无记录”等价：不恢复、不提示。

### 4.3 在线（`--loop-test --loop-online-cache-hit`，32/0）

- 保存：`workrange_online saved a=100 b=160 frames=3314 has_record=1 saves=2
  path=…\records\BV1BMnv6jE5S-42915922022.lapian-workrange.json online=1`，
  `playurl=0 dash=0`；保存时 `loop=1`（证明**循环开关不落盘**）。
- 记录文件实测：`{"schema":1,"in":100,"out":160,"kind":"online","id":"BV1BMnv6jE5S-42915922022",
  "size":0,"mtime_ms":0,"frames":3314}`。
- 新会话：`workrange_online_reload ready=1 cache_hit=1 index=1 frames=3314 has_record=1 a=100 b=160
  loop=frame=0 loop=0 has_in=1 a=100 has_out=1 b=160 playurl=0 dash=0` —— **恢复 A/B、循环仍关、0 网络**。

### 4.4 不污染

| 检查 | 结果 |
|---|---|
| 仓库 `media\` 无 sidecar 残留 | `Get-ChildItem media -Filter '*workrange*'` → **空**（65/0 测试自清理） |
| 用户 `%APPDATA%\拉片软件\settings.json` | SHA256 **`5F5F47E7…` 验收前后一致**（mtime 仍 2026-10-27 10:58，早于本轮） |
| `%APPDATA%\拉片软件\credentials.dat` | hash/mtime 未变（2026-10-26 16:23） |
| `%APPDATA%\拉片软件\online\` | 仍是原有 2 个 `*.lapian.json`（mtime 10-24 / 10-26），**没有新记录** |
| `%APPDATA%\拉片软件\marks\`（workrange 回退目录） | **不存在**（sidecar 均写在 `build\p4` 内） |
| `%LOCALAPPDATA%\拉片软件\cache`（用户 C: 缓存） | mtime 2026-10-27 03:23（早于本轮），内容未变 |
| `git status --porcelain` | 验收结束仍为空（本文件提交前） |

---

## 5. D. 设置页 UI（`--online-cache-manage-test --ui`）

命令：隔离 `--manage-root/--manage-records-root/--manage-settings-root`；
`LAPIAN_UI_DIALOG_SHOT=build\p4\logs\d-ui-{100,200}.png`；200% 另加 `QT_SCALE_FACTOR=2`。

| 档 | checks | failures | verdict | dialog-fits | 窗口尺寸（可用区） | scroll | 4096 对话框 |
|---|---:|---:|---|---|---|---:|---|
| 100% | **149** | **0** | **pass** | OK | 640×633（1920×1032） | 0 | 640×646 fits |
| 200% | **149** | **0** | **pass** | OK | 640×464（960×516） | 1 | 640×464 fits |

- `ui_preview=pass`（两档）；`ui-preview-dialog-fits`、`ui-preview-dialog-fits-4096` 均 OK。
- 文本实测（200% 截图 + KV）：
  - 「可设 128–4096 MiB；1080P / 自动最低 256 MiB；本机按可用内存 25% 最多 4096 MiB」；
  - 「每个窗口独立占用，同时开多个窗口会叠加」；
  - 可用内存给 8192MB 的上下文 → 「本机按可用内存 25% 最多 **2048** MiB」（护栏随内存变化）；
  - 预览缓存预设显示 **「默认（自动 / 512MiB）」**（选中 `res=0 cache_mb=512`），不再是「高级自定义」；
  - 200% footer **保存 / 关闭可见且可点**（截图 `d-ui-200.png`），滚动区生效。
- 4096 保存/重载：`ui-preview-max-4096`、`ui-preview-max-4096-persists`（落盘 1080p/4096 并读回）、
  `ui-preview-settings-4096-roundtrip`、`ui-preview-4096-reload`、`ui-preview-4096-memory-clamp`
  （8192MB → 2048）、范围文本 4 条，全 `[ OK ]`。
- **设置端到端（settings → 下次会话）**：settings.json 写 `1080p/4096` 后启动 cache-hit 1s：
  `preview_config max_side=1920 cache_mb=3763 frames=1268 effective_cache_bytes=3945791488`，
  文件仍保留 4096 —— 本次验收时产品读到可用内存约 14.7GiB（<16GiB），**25% 护栏把 4096 夹到 3763MiB**
  （既有护栏，不是回归；见 §11.5）。4096 真能生效由探针 `--preview-cache-mb 4096` 证明：
  20s/40s 格 `preview_config cache_mb=4096`、`frames/limit=1380`、peak 4093.5MiB。

---

## 6. E. 回归（同一二进制）

| 项 | 结果 | 备注 |
|---|---|---|
| `ctest` | **38/38 pass**（68.97s） | `TEMP/TMP=.deps\tmp` |
| `lapian_timeline_test` | **188 OK / 0 fail** | |
| `lapian_mark_store_test` | **281 OK / 0 fail** | 含 W1 109 条 |
| `--key-test` | **77 / 0**（11.1s） | 隔离 settings |
| `--banner-test` | **74 / 0**（10.1s） | `banner_summary checks=74 fails=0` |
| `--dpi-test` 4 档（100/125/150/200%） | 每档 **37/0**，`dpi_problem` **各 0** | 截图 4 张/档 |
| `--drag-test --drag-samples 20` | P4 **94.0fps**（188 presents）、P6 p95 **17.9ms**、P7 0.0/11.7、P8 12.9fps/26 帧、P9 32.7 | 全部 pass=1 |
| B 720P `--online-quality-switch-test --quality-cache-hit-menu` | **38 / 0** | 64→32（480P，真实下载 23,686,274B）→64，`cache_hit=1 playurl=0 dash=0`，PTS/标记/记录/截图保留 |
| B 1080P 同上 | **38 / 0** | 80→**112（真实下载 48,462,784B）**→80，`cache_hit=1` |
| `--online-offline-test`（workspace） | **45 / 0** | 需 `--bvid/--cid`（本轮补上；不带会 `missing_bvid_cid` exit 2） |
| `--online-quality-test` | **blocked**（16 项 / 1 fail，sample `verdict=blocked`，end `verdict=fail`） | `nav -101`、`vip_status=-1`、`cookie_sent=1`、`sample_block=login_unavailable`；**未写 42/0** |
| `--online-http-selfcheck` | `http_status=200`、83ms、`cookie_sent=1`、`vip_status=-1`、`verdict=fail` | 网络可达，凭据失效 |

---

## 7. 与实现者自报（P3/L1/W1）的差异

| 项 | 自报（P3/L1/W1） | 本轮 P4 | 结论 |
|---|---|---|---|
| 10s@2048 比值 | 1.015–1.024 | 1.021–1.023 | 噪声内 |
| 20s@4096 比值 | 1.009–1.021 | 1.013–1.021 | 噪声内 |
| 20s@4096 关闭 | 139–149ms | **160–164ms（高 11–25ms）** | 仍 <200ms；run 级偏移，记录 |
| 40s@4096 关闭 | 331–395ms | **355/442ms** | 两次都 >200；一次超自报上限 47ms（仍判 gap） |
| 2s@512 比值 | **0.965–0.974**（并归因环境漂移） | **1.000–1.028**；同机 C9 1.000–1.036 | **未复现其漂移**：现在 C9 自己也是 ~1.0+，说明 P3 那次是会话级环境；不构成回归 |
| 1s@512 比值 | 1.000–1.018 | 1.018–1.020 | 噪声内（C9 同机 1.018–1.037） |
| 10s@1024 比值 | 0.915–0.933 | 0.934–0.938 | 略好，仍在 record |
| 20s@2048 比值 | 0.937–0.944 | 0.947–0.960 | 略好，仍在 record |
| 40s@4096 比例/覆盖 | 0.915–0.945 / 57.5% | 0.929–0.939 / 57.5% | 一致 |
| 峰值 | 最高 4093.5MiB、余量 18.5 | 最高 4093.5MiB、余量 18.5 | 一致 |
| 循环 local | L1 41/0 → W1 后 65/0 | **65/0** | 一致 |
| 循环 online | 32/0 | **32/0** | 一致 |
| 工作区跨进程 | 单测 + loop-test 重开 | 另做**真新进程** dpi 截图 + sidecar | 一致（更强） |
| UI checks | P2 自报 149（D5 时 137） | **149/0 ×2** | 一致 |
| ctest | 38/38 | **38/38** | 一致 |

**超噪声/需指出的只有三项**：2s 环境漂移这次不出现（1.000–1.028 vs 0.965–0.974）、
20s 关闭高 11–25ms、40s 关闭 442ms（单次）。均不影响硬门。

---

## 8. 判定

| 计划 §4 硬门 | 结果 |
|---|---|
| 1. 1s/60 不退化：反向 ≥0.9、双方 ≥30fps、正向相对 C9 不降 >5% | **pass**（1.018–1.020；58.82–58.89fps；C9 同机同档） |
| 2. 2s/120 512 ≥0.9 | **pass**（1.000–1.028，3/3 轮） |
| 3. 长跨度新门：10s@2GiB ≥0.9、20s@4GiB ≥0.9 | **pass**（1.021–1.023 / 1.013–1.021，各 3/3 轮 × 8/8 条） |
| 4. 内存 peak ≤ 配置+16MiB；4096 不 OOM/不崩；fallback=0 | **pass**（全部；最高 4093.5MiB，余量 18.5MiB） |
| 5. UI 范围/本机上限/多实例/4096/默认预设/4 DPI | **pass**（149/0 ×2；4096 生效见 §5） |
| 6. 兼容 2048 等旧值 | **pass**（ctest app_settings 全过；UI 用例） |
| 7. 回归 ctest 38/38、timeline 188/0、key 77/0、banner 74/0、dpi 4 档、drag、B 38/0×2、offline 45/0 | **pass**（quality blocked，环境） |
| 循环：F4-A1/A2/A3/A9/A10、结束态 Space、失败只关循环、A=B 提示+无音频、在线 3 轮、scrub-space 不回归 | **pass**（65/0、32/0、62/0、64/0） |
| 工作区：保存/重开/换视频/坏文件/只读目录/debounce/默认全片 | **pass**（§4；只读回退由单测覆盖，见 §11.7） |

**Fail 条件逐项**：1s/2s 反向 <0.9 或 <30fps **无**；正向比 C9 降 >5% **无**；
10s@2G 或 20s@4G <0.9 **无**；4096 档 OOM/崩溃 **无**；循环后仍「播放禁用+帧冻结」**无**；
工作区保存后不恢复/串视频 **无**；UI 不生效 **无**。

> **verdict：`pass with gaps`**。硬门全过；gap 是环境阻塞（SESSDATA）与计划内只记录/未覆盖项（§11）。

---

## 9. 复现命令

```powershell
# 0) 增量构建 + 二进制
$env:TEMP='E:\拉片软件开发\.deps\tmp'; $env:TMP=$env:TEMP
& third_party\cmake\3.31.6\cmake\data\bin\cmake.exe --build build\release --config Release --target lapian
(Get-FileHash build\release\bin\lapian.exe -Algorithm SHA256).Hash   # 80EC7FD4…B4B4F

# 1) A 门格（10s@2048 / 20s@4096；只带 --long-span-gate）
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
& .\build\p4\run_cell.ps1 -Tag a-gate-10s-2048-r1 -Seconds 10 -CacheMb 2048 -Gate
& .\build\p4\run_cell.ps1 -Tag a-gate-20s-4096-r1 -Seconds 20 -CacheMb 4096 -Gate

# 2) 1s/2s 不退化 + C9 同机 A/B（C9 必须显式目标数）
& .\build\p4\run_cell.ps1 -Tag a-1s-512-r1 -Seconds 1 -CacheMb 512 -BackwardFull
& .\build\p4\run_cell.ps1 -Tag a-2s-512-r1 -Seconds 2 -CacheMb 512 -BackwardFull
& .\build\p4\run_cell.ps1 -Tag a-2s-512-c9-r1 -Seconds 2 -CacheMb 512 -BackwardFull -Samples 120 `
    -Exe (Join-Path $PWD 'dist\lapian.exe')

# 3) 整表（A 23 次，约 10 分钟）
& .\build\p4\run_matrix_a.ps1

# 4) B 循环/空格
& .\build\p4\run_b.ps1

# 5) C 工作区跨进程（loop-test 造 sidecar + dpi-test 窗口截图）
& .\build\p4\run_c.ps1
& .\build\p4\run_c2.ps1

# 6) D 设置页 100% / 200%
& .\build\p4\run_d.ps1

# 7) E 回归
& .\build\p4\run_e.ps1

# 8) ctest（必须设 TEMP）
& third_party\cmake\3.31.6\cmake\data\bin\ctest.exe --test-dir build\release -C Release --output-on-failure
```

---

## 10. 证据路径

- 全部探针日志：`build\p4\logs\<tag>.{out,err}.log`；A 汇总 `build\p4\logs\matrix-a.tsv`；
  解析脚本 `build\p4\parse_logs.py`、`build\p4\summarize_a.py`。
- A 门格：`build\p4\logs\a-gate-10s-2048-r{1,2,3}.err.log`、
  `a-gate-20s-4096-r{1,2,3}.err.log`（每行 `long_span_record …` + 36 `online_drag_preview_check`）。
- 不退化 / C9 A/B：`a-1s-512-r{1,2,3}`、`a-2s-512-r{1,2,3}`、`a-2s-512-c9-r{1,2,3}`、
  `a-2s-512-c9s120-r{1,2,3}`、`a-1s-512-c9-r{1,2,3}`。
- 记录格：`a-2s-4096-r{1,2}`、`a-10s-1024-r{1,2}`、`a-20s-2048-r{1,2}`、`a-40s-4096-r{1,2}`。
- Medium：`m-1s-512-r1`、`m-gate-10s-2048-r1`。
- B：`b-loop-local`、`b-loop-online`、`b-scrub-local`、`b-scrub-online`。
- C：`c-cross-loop`（68/0 + sidecar）、`build\p4\video.mp4.lapian-workrange.json`、
  `build\p4\video-b.mp4.lapian-workrange.json`、截图 `c-restore-shot.png`、`p4c-seed-normal.png`、
  `p4c-size-mismatch-normal.png`、`p4c-mtime-mismatch-normal.png`、`p4c-full-range-normal.png`。
- D：`d-ui-100`、`d-ui-200`（`checks=149 failures=0 verdict=pass`）、截图 `d-ui-100.png`/`d-ui-200.png`、
  `d-4096-e2e`（`preview_config … cache_mb=3763`）。
- E：`e-ctest`、`e-timeline`、`e-markstore`、`e-key`、`e-banner`、`e-dpi-{1,1_25,1_5,2}`、
  `e-drag`、`e-quality-switch-b720`、`e-quality-switch-b1080`、`e-offline`、`e-quality`、`e-selfcheck`。
- 夹具/设置/记录：`build\p4\cells\<tag>\{cache,pics,records,settings}`（每次运行前重新复制 1080P 夹具）。

---

## 11. Gap 清单（逐条，不掩饰）

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
