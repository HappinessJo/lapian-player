# 交接：缓存命中拖动预览 + 清晰度切档修复（2026-10-26）

- **状态**：实现、C1 自测、C2 独立验收（**verdict=pass with gaps**）、发布收口完成；
  安装版冒烟核心项全绿。**非门**，不改 spec 验收值、不改 M1/M2 门、不改缓存 schema。
- **代码提交**：
  - `f2ec2e8 fix(online): cache-hit drag preview uses the real cached file`（3 文件，+177/−5）
  - `b2eaa2e fix(online): cache-hit quality menu refetches catalog on demand`（11 文件，+735/−21）
  - `5c8c390 test(online): scope cache-hit P6 check to guarantee range`（3 文件，+80/−7，仅测试/探针）
  - 文档提交见 `git log` 顶部（只含 `docs/` + `AGENTS.md`）。
- **上游**：[spec.md](../spec.md) v0.4 §4.1（P4 ≥30fps、P6 ≤150ms、P7 ≤80ms、P9 ≤200ms；
  P6/P9 以目标距前关键帧 ≤2s 为前提）、[T8.6.1b 交接](handoff-2026-10-24-t8.6.1b.md)、
  [在线拖动 perf](perf/online-drag-parity.md)、[在线切档 perf](perf/online-quality-switch.md)、
  [T8.7 缓存命中](perf/v2-online-cache-hit.md)。
- **用户边界**：缓存命中打开后要能拖、能看预览；清晰度菜单要能按需拉到真实档位并切档，
  不能因为“已缓存”而只剩 1 档；不碰用户默认缓存/记录/截图/凭据。
- **发布记录**：[release-2026-10-26-cache-hit-fix.md](release-2026-10-26-cache-hit-fix.md)。

---

## 1. 一句话结论

缓存命中的在线会话不再把 `<Pictures>\lapian-online-<bvid>-<cid>.mp4` 这个“展示名”当媒体文件：
预览解码器改用真实缓存 `data.mp4`，**0 帧变为 26/0、presented 54–58、fps 30–60**；
清晰度菜单从“缓存 meta 合成的 1 档”改为**点开菜单才按需拉目录**，720P 3 档、1080P 5 档，
可实际切到非当前档并切回缓存档，开关阶段保持 0 playurl/0 DASH。**长 GOP 1080P 2s/60 的
松手+落地 148–206ms 属 spec 前提之外（out_of_premise），只记录、不写 P6 通过。**

## 2. 问题 A：缓存命中拖动只有 0 帧

### 2.1 现象（修前探针）

- `--online-drag-preview-test --drag-cache-hit`（720P/1080P）：**25 检查 / 3 失败**，
  `presented=0`、`fps_active=0`，画面停在打开帧。
- 诊断行：`open_attempted=1 open_ok=0 open_error=…Illegal byte sequence`、
  `configured_exists=0`，而 `session_path=<Pictures>\lapian-online-<bvid>-<cid>.mp4`、
  `session_exists=0`。

### 2.2 根因

DASH 缓存命中走 growing 会话；`MediaSession::path_utf8()` 返回的是**展示用文件名**
`<Pictures>\lapian-online-<bvid>-<cid>.mp4`（磁盘上不存在，只用于 UI/记录命名）。
在线预览解码器用 `session path` 打开本地文件 → `open` 失败（文件名含中文/非法字节序列，
且路径不存在）→ 预览线程 0 帧。**不是缓存损坏，也不是解码器/硬解问题。**

### 2.3 修复

| 文件 | 内容 |
|---|---|
| `src/app/MainWindow.cpp/.h` | 新增 `CacheHitPreviewPath()`：按 `online_part_path_` → `online_cache_path_` → session path 取真实可读路径；新增 `EnsureCacheHitPreviewDecoder()`：只在 cache-hit、同路径幂等，`OnScrubStarted` 懒建，设备/会话重建后仍用真实路径；预览线程打开真实文件 |
| `src/app/OnlineDragPreviewTestRunner.cpp` | 打印 `configured/session_path/configured_exists/session_exists/open_attempted/open_ok/open_error`，并把 26 项检查里的 P6 断言按 spec 前提分流 |
| `src/app/MainWindow.*`（5c8c390） | 新增 `out_of_premise` 记录：目标距前关键帧 >2s 时只打印 `cache_hit_p6_out_of_premise`，不把长 GOP 当 P6 失败 |
| 测试专用 | `WarmupVideoStageForTest()`（仅 `--drag-cache-hit`）在拖动前预热一次画面页，打印 `online_drag_preview_warmup stage_ms=…`，用于排除首次显示 D3D11 惰性初始化；**不改门值 200ms** |

- 预览解码器仍只读视频 source，不读音频；缓存/线程/有界语义未变（见 T8.6.1b 交接）。
- 本地预览、首次在线增长、durl、缓存写入路径一行未改。

## 3. 问题 B：缓存命中清晰度菜单只有 1 档

### 3.1 现象（修前）

缓存命中打开后菜单只显示“缓存 meta”合成的当前档；无法切到其他档，也无法拉取目录。
根因三处：

1. 缓存 meta 只合成当前档，菜单 1 档；
2. `EmitQuality` 一次性发出，后续目录到达无法替换；
3. `StartQualityWorker` 在 cache-hit 早退，菜单只重建 UI、不请求目录。

### 3.2 修复

| 文件 | 内容 |
|---|---|
| `src/app/OnlineSession.*` | 新增 `RequestQualityCatalog()`：**点开菜单才请求**、幂等；cache-hit 允许 catalog-only `playurl(fnval=4048)`；失败保留旧目录并记 error |
| `src/app/OnlineSession.*` | `EmitQuality(info, replace)`：目录到达后可替换菜单模型（原来只发一次） |
| `src/app/MainWindow.cpp/.h` | 菜单点开时显示 loading、目录到达即时重建菜单、状态提示；切档/切回沿用既有产品入口 |
| `src/online/QualityMenu.*` | 标题按“≤1 档 cache-meta”与“服务端目录”区分：缓存 meta 标题含“缓存命中档位（未联网…”，服务端标题含“实际可用档位” |
| `src/app/OnlineQualitySwitchTestRunner.*`、`tests/online_dash_test.cpp`、`tests/online_fault_test.cpp` | 新的缓存命中菜单/切档探针与单测覆盖 |

- 不改 `OnlineRequest`/`CacheStore` schema、不改质量档策略（默认 AVC≤1080p、HEVC/AV1-only
  60fps 只手动、4K 只手动均未动）。

## 4. 修复后数字（C1 探针 + 父复核；Medium + 隔离根）

### 4.1 A：缓存命中拖动

| 场景 | 结果 |
|---|---|
| 720P 2s/60 ×3（`BV1cW411b7Yh`/`28925848`，qn=64，29.412fps） | **26/0 ×3**；presented 57–58；fps_active 30.02–30.53；first 101–102ms；release+landing **97–111ms**；out_of_premise=1（距 K0 133 帧/4522ms）；exact；playurl/dash=0 |
| 720P 1s/60 | **26/0**；55 presented；fps 60.04；P6 **70ms** |
| 720P 保证档 `--drag-start-frame 145`（145→205，距 K150 55 帧/1870ms） | **26/0**；P6 **47ms**（C1 38/40ms） |
| 1080P（OB-1 `BV1BMnv6jE5S`/`42915922022` 80-7）1s/60 | **26/0**；presented 54–56；fps_active 58.95–61.14；P6 **88–93ms**；与本地/首次在线 presented 差 **5.1–8.5%**（≤10%，C2 判定） |
| 1080P 2s/60（终点 135 距 K0 4.5s） | **26/0**（5c8c390 修 P6 断言口径后）；release+landing **148–203ms** 变动，**out_of_premise=1** |
| 1080P 保证档 `--drag-start-frame 150`（150→210，距 K150 2.0s） | **26/0**；P6 **80ms**（C1 59/79ms） |

- 修前 25/3、presented=0、fps=0；修后 26/0，`open_ok=1`、`configured_exists=1`。
- **Warmup**：C1 在 cache-hit 探针里加 `WarmupVideoStageForTest()` 并打印
  `online_drag_preview_warmup stage_ms=145–259`；C2 判定不掩盖拖动期卡顿——raw
  `stage_show` 同量级 179–264ms，预热后 0；cache-hit 运行 0 次 >200ms 事件，
  `ui_gap` 等于打开阶段 `wait_ready`。门值仍 200ms，未改。

### 4.2 B：缓存命中清晰度菜单/切档

| 场景 | 结果 |
|---|---|
| 720P 用户场景（`BV1cW411b7Yh`/`28925848`） | **38/0**；打开 `cache_hit=1 / playurl=0 / dash=0 / quality=0`；初始 1 档 + 缓存 meta 标题；点开菜单 loading + **1 次 catalog（HTTP 200，129ms）**；3 档、非当前 enabled=2、服务端标题；**64→32 实际 qn/codecid 变、cache_hit=0**，PTS **1,068,000 不变**，mark/record/snapshot 保留；**32→64 cache_hit=1 / playurl=dash=0** |
| **1080P 关键场景**（OB-1） | **38/0**；80 打开 `cache_hit=1`、**0 网络**；catalog 1 次（200，169ms）、**5 档含 112**；切 112 **实际下载**（published **4.44M/48.46M**）、qn=112/7、cache_hit=0、PTS/mark/record/snapshot 保留；切回 80 `cache_hit=1 / playurl=dash=0` |
| 普通切档（新鲜缓存） | **42/0**（80 下载 → 112 → 80 缓存命中） |
| offline workspace（规范 3.2MB 媒体） | **45/0**；大文件 46MB/201MB 的 **45/9** 是既有 `clear_cache busy` 级联，pre-A/B 的 3C 二进制同样 45/9，**不追** |
| M2 op5（缓存命中 0 网络） | **10/0** |
| fault | **254/0** |

## 5. C2 独立验收

- **verdict=pass with gaps**（C2 独立跑，不引用 C1 数字；隔离根 + Medium）。
- A 三路（1s/60）：本地 presented **59 / 58.47fps / P6 89ms**；首次在线 parity
  **61/0、59 / 58.53fps / P6 95ms**；缓存命中 1080P **54–56 / 58.95–61.14fps / P6 88–93ms**；
  presented 差 5.1–8.5%（≤10%）。
- B：720P **38/0**、1080P **38/0**、普通切档 **42/0**、offline **45/0**、M2 op5 **10/0**、
  fault **254/0**。
- 门禁：`ctest 38/38`（父 63.83s、C2 68.28s）、`--key-test 77/0`、`--banner-test 74/0`；
  `--drag-test` P4 **90.1** / P6 **27.4** / P7 **0.4/10.7** / P8 **12.8** / P9 **31.7**
  （父基线 95.8/56.9/0.3/10.4/31.7 量级；同机波动，不算退化）；local parity **9/0**。
- T8.6.1b 首次在线：1s/60 **35/1**（`forward_ui_gap` 327ms）、2s/60 **24/1**（349ms）；
  失败打印在 `cached_window` 之前 = **打开阶段**，既有非门基线；**不修、不掩饰**。
- 一次父刚重建后首次 720P cache-hit 首跑出现 `first_ms=1210 / ui_gap=296 / 26/1`，
  随后 3/3 恢复 101ms/86ms；判为构建后首次运行/系统负载瞬态；**若后续冒烟再遇到，
  记录原始数字，不改门**。

## 6. 覆盖缺口（C2 明确未验证）

1. **cache-hit P7/P9 无直接探针**：只有同素材本地代理近似（720P：P7 0.1/1.7、P9 47.1；
   1080P：P7 0.3/2.9、P9 67.4）；不写“缓存命中 P7/P9 已验收”。
2. **cache-hit 音频只验证打开/就绪**（48k、`pitch_preserve=1`），没有播放 underrun /
   A/V 偏差测量。
3. **首次在线 60s A/V** 被 M2 普通用户前置 blocked（默认凭据是大会员，M2 的普通用户断言
   不成立），本轮未跑。
4. **cached→cached 切回经真实菜单打开路径无直接探针**；1080P 112 在本次场景只到
   “实际取流/落地”后切回（published 4.44M/48.46M），**未验证 112 完整下载完成后的再次命中**。

## 7. 残余 / 风险

1. **长 GOP 1080P 2s/60 松手+落地 148–203ms（安装版本轮 203–206ms）**：目标距 K0 4.5s，
   超出 spec P6“目标距前关键帧 ≤2s”的前提；5c8c390 已按前提分流（`out_of_premise` 只打印），
   **保证档内 P6 47–80ms（安装版 76–78ms）**。不得写“1080P 缓存命中 P6 保证档外通过”。
2. **构建后首次运行瞬态**（见 §5）：不改变阈值；记录原始数字，复跑对照。
3. **T8.6.1b `forward_ui_gap` 327/349ms（C3 安装版 214/223ms）**：既有打开阶段 UI gap，
   非门，本轮不修。
4. **offline 大文件 45/9**：既有 `clear_cache busy` 级联，pre-A/B 3C 二进制同样；规范
   3.2MB 媒体 45/0，不追大文件。
5. **C2 覆盖缺口**（见 §6）：不要把“缓存命中 26/0”外推成 P7/P9/音频/60s A-V 全部通过。
6. **无符号化崩溃 dump**（沿用 P0 记录）：本轮未观察到崩溃/挂起，但不写“永不崩溃”。

## 8. 回滚

- 三个提交必须**一起回退**（只回退 test-only 会让 out-of-premise 检查重新变成假红）：

```powershell
git revert 5c8c390 b2eaa2e f2ec2e8
# 或者回到 A/B 之前
git checkout 8c4728d -- src/ tests/   # 不推荐：会丢后续文档
```

- 分发回滚：上一版 setup `96C5A28B…`（12.02MB，音高保持版）可重装回旧安装版；
  旧安装版 exe `E8149B92…` 不含本轮 A/B 修复。
- 本轮 setup `DF76A251…` / zip `DAC216AF…` 是修复后新产物；回滚后不要继续分发这两个包。

## 9. 复现

```powershell
cd 'E:\拉片软件开发'
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$env:TMP='E:\拉片软件开发\.deps\tmp'; $env:TEMP=$env:TMP
$exe = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"   # 或 build\release\bin\lapian.exe

# A：缓存命中拖动（先用完整 64-7 缓存；隔离根）
& $exe --online-drag-preview-test --bvid BV1cW411b7Yh --cid 28925848 `
  --drag-seconds 2 --drag-samples 60 --drag-cache-hit --timeout-ms 60000 `
  --online-cache-root build\release-close\online\a720\cache `
  --online-pictures build\release-close\online\a720\pics `
  --online-records-root build\release-close\online\a720\records `
  --settings-path build\release-close\online\a720\settings

# A：1080P 保证档（距 K150 2s）
& $exe --online-drag-preview-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --drag-seconds 1 --drag-samples 60 --drag-start-frame 150 --drag-cache-hit `
  --online-cache-root build\release-close\online\a1080\cache `
  --online-pictures build\release-close\online\a1080\pics `
  --online-records-root build\release-close\online\a1080\records `
  --settings-path build\release-close\online\a1080\settings --timeout-ms 60000

# B：缓存命中菜单（720P 先放 64-7 完整缓存；1080P 只放 80-7+audio 以验证 112 真下载）
& $exe --online-quality-switch-test --quality-cache-hit-menu `
  --bvid BV1BMnv6jE5S --cid 42915922022 --timeout-ms 60000 `
  --quality-switch-wait-ms 120000 `
  --online-cache-root build\release-close\online\b1080\cache `
  --online-pictures build\release-close\online\b1080\pics `
  --online-records-root build\release-close\online\b1080\records `
  --settings-path build\release-close\online\b1080\settings
```

- 安装版日志：`build\release-close\logs\c3\*.log`；构建：`build\release-close\build-clean.log`；
  安装：`%TEMP%\lapian-release-2026-10-26-cachehit\install.log`。
- 坑：缓存命中探针必须用**完整**缓存条目（`data.mp4` + `meta.json`）；只放
  `data.part` 会走 `not_complete_video`；1080P 切 112 前要确保 112-7 不在缓存根，
  否则不会真下载；`--drag-cache-hit` 不清缓存，普通在线探针会清。
