# 计划：在线预览缓存绝对上限 2048 → 4096 MiB + 设置页可设范围（2026-10-27，未执行）

> **状态：规划冻结，未动代码、未构建、未安装。**
> 前置：`5477688`（C9 包）已发布；默认 auto+512、上限 2048、25% 可用内存夹取、1s/60 各挡位
> 1.000–1.019、2s/120 512/1G/2G 1.009–1.045。
> 本文只做两件事：**绝对上限抬到 4096 MiB**；**设置页明确告诉用户能设多少（范围 + 本机实际上限）**。
>
> 定位：`docs/plan-2026-10-27-preview-parity-config.md` §3.2「上限 2GiB」的**修订**。
> 其余（三档分辨率、五档预设、128/256 下限、默认 auto+512、只改在线、下次会话生效、不热切换）**不变**。

---

## 0. 一句话

- 技术上只是把 4 处上限常量与夹取从 2048 抬到 4096，并保留 `min(4096, 可用内存 25%)` 的实际生效上限；
- **4GiB 只对长跨度回拖有意义**：探针 `--drag-seconds S` = 60S 目标 / 60S 帧；
  30fps 素材下 10s 跨度≈600 帧≈1.78GiB、20s≈1200 帧≈3.56GiB、40s≈2400 帧≈7.1GiB（超 4GiB）；
- 1s/2s 拖动在 512 就已饱和（2s 峰 403MiB，256/512/1024/2048 的 2s 比值都是 1.009–1.045），
  **抬上限不会改善 1–2 秒手感，必须用 10s/20s 长跨度验收**；
- 5 个阶段 / 5 个 agent（P1–P5），串行；父 agent 只做拆任务、复核、跑门、收证据。

---

## 1. 现状（代码事实）

| 位置 | 现内容 | 改动 |
|---|---|---|
| `src/core/AppSettings.h:31` | `kPreviewCacheMaxMb = 2048` | → 4096 |
| `src/core/AppSettings.cpp` | 解析 clamp（143/144）、`ClampPreviewCacheMb`（209）引用该常量 | 无需改代码，随常量生效 |
| `src/media/OnlinePreviewDecoder.cpp:60` | `kMaxCacheBudgetBytes = 2048MiB`，`Configure` 夹取 | → 4096 |
| `src/app/MainWindow.cpp:7769` | 测试覆盖 `env_cache_mb <= 2048` | → 4096（test-only） |
| `src/app/CacheSettingsDialog.cpp:313-314` | spin 范围 128–`kPreviewCacheMaxMb` | 随常量生效；补范围/本机上限文案 |
| `src/app/CacheSettingsDialog.cpp:86-106` | 预设按（分辨率, 缓存）**精确匹配**，否则「高级自定义」 | 默认 auto+512 现在显示成「高级自定义」→ 需修 |
| `tests/app_settings_test.cpp`、`tests/online_preview_decoder_test.cpp` | 16 处 `2048` 断言 | 更新并保留 2048 边界用例 |

用户可感知的缺口：**界面上没有一行字说明 128–2048 的范围，也没有「本机按可用内存 25% 最多 X MiB」**。

---

## 2. 设计（冻结）

### 2.1 上限

- 绝对上限 **4096 MiB**（单实例、硬上限）；
- 实际生效 = `clamp(用户值, 分辨率下限, 4096)` 再按 **可用内存 25%** 夹取；下限仍 720P 128 / auto·1080P 256；
- 25% 夹取**保留**（唯一护栏）；夹取时自动调整并明确提示，不静默；
- 分配失败回退链不变（75% → 1024/64，`fallback=0` 为正常态）；
- 旧 settings（2048 及以下）完全兼容；4096 只在用户显式设置后出现；
- **默认仍 auto + 512**；预设不加 4GiB 项（4096 只经「高级自定义」输入）。

### 2.2 设置页文案（新增，常显）

```
自定义缓存  [ 512 MiB ▲▼]
可设 128–4096 MiB；1080P / 自动最低 256 MiB；本机按可用内存 25% 最多 4096 MiB
当前约可缓存 5.8 秒（1080P / 30fps）
（超上限时）已按可用内存 25% 限制为 2048 MiB
（多实例提示）每个窗口独立占用，同时开多个窗口会叠加
```

- 「本机最多」= `ClampPreviewCacheMb(4096, 当前分辨率, 可用内存, ...)` 的实际值，随分辨率/内存变化刷新；
- 预设下拉新增首项 **「默认（自动 / 512MiB）」**，修掉默认值显示成「高级自定义」的问题；
- 「恢复推荐设置」仍回 auto+512。

### 2.3 探针与门（口径先定清）

- `--drag-seconds S` = 60S 个目标、跨度 60S 帧、60Hz；长跨度运行墙钟约 S 秒×2（正+反）；
- 现有 `online_drag_forward_backward_parity` 只在 1s/60 判定（不变）；
- 长跨度用**同一口径**记录 `presented_ratio / fps_ratio / preview_peak / preview_frames / limit`，
  新增 `long_span` 诊断行（不改现有 0.9/30fps 判据，只为长跨度新增显式检查，见 §4）。

---

## 3. 阶段与 agent（5 个，串行）

| 阶段 | Agent | 内容 | 退出门 |
|---|---|---|---|
| **P0** | 父/PM | 冻结本方案、拍板 §6 决策、不改代码 | 用户确认 |
| **P1** | A（上限核心） | core 常量 4096；解码器硬上限 4096；MainWindow 覆盖上限；单测（4096 边界、25% 夹取、旧 2048 兼容、越界） | 单测全过；ctest 38/38；无 UI 改动 |
| **P2** | B（设置页可读性） | 范围/本机上限/多实例文案；「默认（自动/512）」预设；4096 输入；超限提示；UI 自动化 | 对话框 4 DPI 可达；manage-UI 全过；保存/重载/非法修正 |
| **P3** | C（长跨度探针+矩阵） | `long_span` 检查与诊断；10s/20s/40s 矩阵；1s/2s 不退化；峰值/覆盖率/回退；数据文档 | 长跨度门 + 1s/2s 无退化；失败如实记 |
| **P4** | D（独立验收） | 不看实现者结论，独立复跑 P3 关键格 + UI + 设置端到端；verdict | pass / pass with gaps / fail |
| **P5** | E（发布收口） | clean build → dist/setup/zip → 安装 → 安装版冒烟 → docs/AGENTS v6.64 | 安装版通过；回滚/文档完整 |

> 若想提速可把 P1+P2 合成一个 agent（4 个），但配置与 UI 的复核边界会变粗；**建议保持 5 个**。

### P1 细节（Agent A）
- 只改：`src/core/AppSettings.h/.cpp`、`src/media/OnlinePreviewDecoder.h/.cpp`、`src/app/MainWindow.cpp`（1 行覆盖上限）、两个测试文件；
- 保留 `2048` 作为普通合法值用例；新增 4096 边界、4096+25% 夹取、`{99999}` → 4096；
- 解码器 `Configure({1920, 4096MiB})` 帧上限约 1380，`cache_budget_bytes` 不变式仍成立；
- 不改默认值、不改淘汰/回退/发布语义。

### P2 细节（Agent B）
- 只改：`CacheSettingsDialog.h/.cpp`、`OnlineCacheManageTestRunner.cpp`（test-only）、必要时 `Theme.cpp`（1 行）；
- 文案必须动态：本机上限随 `SetPreviewContext(available_memory_mb)` 变；
- 新增/更新 ≥8 条 `ui-preview-*` 断言：范围文本含 4096、本机上限含实际值、默认项显示「默认」、4096 保存/重载、4096 被 25% 夹取时提示、4 DPI 下 footer 可达；
- 不热切换（保存不重建 decoder）。

### P3 细节（Agent C）
- 先做**探索性一格**：20s 跨度 + 4GiB，报告 `preview_frames/limit` 与 `preview_peak`——先回答「拖动 20s 时缓存到底能填多少」，再决定矩阵口径；
- 矩阵（每格 3 次，1080P cache-hit；growing 若凭据可用）：
  | 跨度 | 预算 | 类型 |
  |---|---|---|
  | 1s | 512（对照） | 硬门（不退化） |
  | 2s | 512 / 4G | 硬门 / 记录 |
  | 10s | 1G / 2G / 4G | 2G 硬门 + 记录 |
  | 20s | 2G / 4G | 4G 硬门 + 记录 |
  | 40s | 4G | 只记录（2400 帧≈7.1GiB，超上限） |
- `long_span` 检查：反向 presented ≥0.9×正向、反向 fps ≥0.9×正向、双方 fps_active ≥30、
  `preview_peak ≤ 配置+16MiB`、`fallback=0`、exact 1/1、`preview_w×h=1920×1080`、cache-hit `playurl=dash=0`；
- 40s 记录值 + 关闭会话释放耗时（End/Close 释放 4GiB 是否会卡 UI）；
- 1s/60 全挡位与 2s/120 512 复跑，确认相对 C9 包（1.009–1.045、1.000–1.019）无方向性退化。

### P4 细节（Agent D）
- 独立目录/夹具/日志；重跑 1s 512、2s 512、10s 2G、20s 4G、40s 4G、P6、内存、UI 4 DPI、设置端到端；
- 重点验证「4GiB 真的落盘并生效」（`preview_config cache_mb=4096`）与「25% 夹取后本机上限显示正确」；
- 只写 verdict 文档，不改代码。

### P5 细节（Agent E）
- clean `build-release.ps1 -Clean -Package`（`TEMP/TMP` 指向 `.deps\tmp`）；ctest 38/38；timeline 188/0；
- 静默安装、安装版==dist、settings 未变、备份保留、不动 C8/E:；
- 安装版冒烟：1s/2s 对照 + 10s/20s/40s 长跨度 + P6 + 内存 + 设置页 4 DPI + legacy 64MiB parity；
- 文档：release/handoff、`AGENTS.md` v6.64、回滚（重装 C9 `AF32B4C3…` / exe `588B8D5E…`；revert 本链）。

---

## 4. 验收门（不放宽）

1. **1s/60 不退化**：各挡位反向 ≥0.9×正向、双方 ≥30fps；正向相对 C9 包不降 >5%。
2. **2s/120 不退化**：512/1G/2G ≥0.9；4G 记录。
3. **长跨度新门**：10s@2GiB ≥0.9、20s@4GiB ≥0.9（同口径双向、双方 ≥30fps）；
   40s@4GiB 只记录（超 4GiB 覆盖能力，不写通过）。
4. **内存**：每格 `peak ≤ 配置+16MiB`；4096 档不 OOM/不崩、`fallback=0`；关闭会话释放 4GiB 不卡 UI（记录耗时）。
5. **UI**：范围/本机上限/多实例提示正确；4096 可保存并**下次会话**生效（`preview_config cache_mb=4096`）；
   超 25% 自动夹取并提示；默认 auto+512 显示为「默认」而不是「高级自定义」；4 DPI 下可滚动、footer 可达。
6. **兼容**：旧 settings（含 2048/512/无 preview 键）不受影响；`2048` 仍是合法值。
7. **回归**：ctest **38/38**、key 77/0、banner 74/0、dpi 4 档、drag、B 38/0×2、offline 45/0、timeline 188/0；
   `--online-quality-test` 若 SESSDATA 仍失效按 **blocked** 记录（不写 42/0）。

**Fail 条件**：任一 1s 挡位 <0.9 或 <30fps；正向降 >5%；10s@2G 或 20s@4G <0.9；
4096 档 OOM/崩溃；4096 保存后不生效或显示与本机上限不符；长跨度峰值超 配置+16MiB。

---

## 5. 风险

| 风险 | 说明 | 缓解 |
|---|---|---|
| 4GiB 是**上限不是预分配** | 只有真拖到才吃；短拖看不到任何变化 | 验收只用长跨度；短拖不宣称提升 |
| 解码吞吐可能填不满长跨度 | 20s 跨度需 1200 帧，软解+缩放在 20s 内能否填满未证 | P3 先做探索格，先报 `frames/limit` 覆盖率再定结论 |
| 25% 夹取在多数机器上限制 4096 | 16GB 机器可用 8GB → 2GiB；8GB → 1GiB | UI 常显「本机最多 X MiB」；夹取提示 |
| 进程常驻/多实例 | 吃满时 ~4.5GB；两窗 8–9GB | 只作高级项、默认 512；多实例提示；25% 护栏 |
| 关闭释放 4GiB 卡顿 | End/Close 释放大缓存 | P3 记录关闭耗时；>200ms 记为缺口 |
| spec P11 口径 | P11 是本地播放 ≤800MB，不覆盖在线预览缓存 | 文档写明；不把 4GiB 当默认 |
| 与旧计划冲突 | 旧计划 §3.2 上限 2GiB | 本文即修订；发布文档注明 |

**回滚**：每个阶段独立 commit；发布回滚重装 C9 setup `AF32B4C3…`（exe `588B8D5E…`）；
代码回滚 `git revert` 本链 P1–P2。

---

## 6. 需要拍板（默认值）

1. **绝对上限 4096 MiB**，25% 夹取保留（本机可用 ~17GB → 4096 可设满）——**默认同意**。
2. **不给 4GiB 做预设**，只在「高级自定义」可填——**默认同意**（防误选）。
3. **长跨度门**：10s@2GiB、20s@4GiB 为硬门，40s 只记录——**默认同意**；
   若 P3 探索格显示解码吞吐填不满，则如实记 fail/未达，不改门。
4. 超 25% 时**自动夹取 + 明确提示**（不禁止保存）——**默认同意**。

---

## 7. 证据与产物（预计）

- 代码：P1 `feat(settings): raise preview cache cap to 4096`；P2 `feat(settings): show preview cache range and machine cap`；
  P3 `test(online): long-span preview cache matrix`；
- 文档：`docs/perf/preview-cache-long-span.md`（P3）、`docs/handoff-…-acceptance.md`（P4）、
  `docs/release-2026-10-27-preview-cache-4096.md` + `AGENTS.md` v6.64（P5）；
- 证据目录：`build\p1\…`、`build\p3\logs\…`、`build\p4\…`、`build\release-close-p\…`（均不入库）。
