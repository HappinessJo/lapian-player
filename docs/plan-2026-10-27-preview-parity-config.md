# 计划：在线拖动预览清晰度/缓存配置化 + 前后一致性（2026-10-27，未执行）

> **状态：规划冻结，未动产品代码、未构建、未安装。**
> 当前 HEAD `961a19b`；C8 的 1s 前进/后退修复已提交但**未打包**；安装版仍是
> `FEF8CE1E…`（C7，不含 C8）。本轮上下文已很长，**新会话按本文 D1 开始**。
>
> 最终目标：**在线视频（首次在线 + 缓存命中）拖动预览做到清晰、舒适、流畅，
> 前进/后退同目标率、同跨度、同指标一致；不能只顾前进，也不能“两边一样烂”；
> 性能不允许倒退。**

---

## 0. 一句话结论

- C8 已把缓存命中 DASH 的 1s/60 前进/后退做到 **0.93–1.04、双方 54–60fps**；
  但 2s/120 1080P 仍 **0.877（cache-hit）/0.886（growing）**，且在线预览固定软件 1024，
  比本地硬解全分辨率明显软。
- 下一步不是继续拧一个常量，而是把 **预览分辨率 + 预览缓存** 做成用户可选的运行时配置：
  3 个分辨率 × 4 个缓存挡位 + 高级自定义，带最低有效缓存/内存上限/失败回退。
- 预计 **6 个实现/验收 agent（D1–D6）**，父 agent 做 PM/复核；串行，不并行写工作区。

---

## 1. 当前基线（必须先读）

### 1.1 代码与分发状态

| 项 | 状态 |
|---|---|
| 代码 HEAD | `961a19b`（`87660ef` test + `961a19b` fix） |
| C8 1s 修复 | 已提交；`OnlinePreviewEligibleForDrag()` 把 cache-hit DASH 接到 `OnlinePreviewDecoder` |
| 安装版 C: | `FEF8CE1E…`（C7，**不含 C8**） |
| C8 Medium 副本 | `%LOCALAPPDATA%\Programs\lapian-c8\lapian.exe` = `99BC1201…`（3,597,824B） |
| dist/setup/zip | 仍是 C7 旧产物；C8 未打包 |
| E: 免安装版 | 两份都旧：`免安装版\2\…`=`DDF2E8E9…`、`免安装版\…`=`E19FC4B3…`；不要动 |

### 1.2 C8 同 60Hz 数据（详见 [C8 交接](handoff-2026-10-27-c8-forward-backward.md)）

| 场景 | 前进 presented / fps_active | 后退 presented / fps_active | 前/后比 |
|---|---:|---:|---:|
| 缓存命中 720P 1s | 55 / 58.95 | 56 / 59.83 | 1.018 |
| 缓存命中 1080P 1s | 54 / 57.75 | 56 / 60.02 | 1.037 |
| 首次在线 720P 1s | 55 / 58.76 | 56 / 59.77 | 1.018 |
| 首次在线 1080P 1s | 56 / 59.83 | 56 / 59.96 | 1.000 |
| 本地软解 720P 1s | 58.53 | 54.46 | 0.929 |
| 缓存命中 720P 2s | 115 / 59.37 | 107 / 55.30 | 0.930 |
| 缓存命中 1080P 2s | 114 / 58.85 | **100 / 51.60** | **0.877** |
| 首次在线 1080P 2s | 114 / 58.88 | **101 / 52.20** | **0.886** |

- 1s/60 全部达到“反向≥正向 90%、双方≥30fps”；
- 2s/120 1080P 仍低于 90%，只记录，**不写成通过**。
- 旧探针反向只跑 30 个目标、跨度 30 帧，不能用来判断前后差异；C8 的
  `--drag-backward-samples` / `--drag-backward-full` 才是同口径。

### 1.3 清晰度事实（不要再用错口径）

- 本地默认 **硬解，硬件帧不缩放**：1080P 本地预览约 1920×1080，720P 约 1280×720；
- 在线预览固定 **软件 1024**（16:9 1024×576），所以在线确实更软；
- 本地软件预览才降到 1280；本地 `PreviewRingCache` 192MiB，但全 1080P 硬解帧约 3.0MiB/帧，
  只能放约 60 帧；本地顺主要靠短 GOP（测试文件 1s）+ 硬解 + 预取，不是缓存无限；
- 帧大小（NV12）：1024×576 ≈0.844MiB；1280×720 ≈1.318MiB；1920×1080 ≈2.966MiB；
- GOP 是编码属性：本地测试文件约 1s，这个在线缓存约 5.07s；**不是“本地都短/在线都长”**。
  缓存/本地播放不会改变 GOP；同一个 `data.mp4` 用本地模式打开仍然是 5s GOP。

### 1.4 当前预览缓存与配置位置

- `OnlinePreviewDecoder`：`kPreviewMaxSide=1024`、`kMaxCachedFrames=72`、
  `kCacheBudgetBytes=64MiB`（编译期常量），72 帧峰值约 60.75MiB。
- `PreviewDecoder`（本地/durl）：软件最长边 1280，环形缓存 192MiB，空闲裁剪 32MiB；
  本地硬解帧不缩放。
- `AppSettings` 目前只有 volume/waveform/cache_root/cache_quota_gb；设置入口是
  `CacheSettingsDialog` + `ShowCacheSettingsDialog()`（Ctrl+,）。
- C8 没改 `OnlinePreviewDecoder`/`PreviewDecoder` 常量，也没改 settings schema。

### 1.5 现有测试/门

- `online_drag_forward_backward_parity`：在线 growing + cache-hit，同 60Hz/60 目标、1s、
  未自定义起点；反向 presented/fps ≥0.9×正向，且双方 fps_active≥30。
- `cache_hit_backward_presented_ratio_ge_70pct`：保留旧门。
- 现有 parity 测试有 `cache_peak_le_64mib`；默认值改高后必须改成**配置感知**，
  不能拿旧 64MiB 门误杀，也不能把旧 64MiB 兼容档删除。
- ctest 基线 **38/38**；key 77/0、banner 74/0、dpi 4 档、drag、B 38/0、quality 42/0、
  offline 45/0、打开预热 stage_show=0、timeline 188/0。

---

## 2. 需求与硬规则

### 2.1 用户目标

**清晰、舒适、流畅地前后随意拖动**，包括：

- 短拖（1s/60）和较长回拖（2s/120，后续争取更长）；
- 缓存命中、首次在线（增长会话）；
- 1080P 源要明显比 1024 清楚，接近本地硬解观感；
- 不能因为加缓存/改分辨率让前进变慢、打开变慢、P6 退化或内存爆掉。

### 2.2 前后一致（硬要求）

1. 前进/后退必须**同一目标率、同一跨度、同一指标**验收；只测前进 = 未完成。
2. **反向 presented ≥ 正向 90%**、**反向 fps_active ≥ 正向 90%**。
3. **前进和后退都必须 fps_active ≥30**；“两边都 15fps 所以一致”直接 fail。
4. 不允许为了反向把正向拉低；如果反向追不上，记录残差，不能静默通过。
5. 1s/60 是**每个挡位**的硬门；2s/120 是 512MiB+ 挡位的目标门，256MiB 只记录。
6. 长 GOP 超出 P6 前提的样本只记 `out_of_premise`，不写成保证档通过。

### 2.3 性能不倒退（硬要求）

以 C8 当前二进制为基线：

- 1s/60 前进 fps_active 不得低于基线 5%（同素材/同路径）；
- 1s/60 后退不得低于基线，且必须满足 2.2；
- P6 保证档 ≤150ms；打开 `stage_show_ms=0`、`wait_ready`、B/quality/offline/timeline 不退化；
- 默认设置（自动+512MiB）必须过 1s/60 双向；2s/120 1080P 争取 ≥0.9；
- 2GiB 挡位不得 OOM/崩溃；分配失败必须回退；
- 视觉：1080P 挡位相对 1024 必须肉眼更清晰；最终精确帧/截图不受影响。

### 2.4 非目标（本计划不做）

- 不重编码/不改在线视频 GOP；不承诺“5s GOP 任意跳都零重解”；
- 不做 1440P/4K 在线预览（1080P 封顶）；
- 不在 v1 改本地 `PreviewDecoder`（本地硬解路径保持；只改在线）；
- 不改 M1/M2 门、spec v0.4 验收值、安装器形态、缓存 schema、默认下载缓存配额 20GB；
- 不把“感觉流畅”当验收；必须有同口径数字。

---

## 3. 产品规格（建议默认，D1 前可拍板）

### 3.1 预览分辨率（3 档）

| 选项 | 实际输出 | 说明 |
|---|---|---|
| **自动（推荐）** | 跟随源，最高 1080P | 720P 源保持 720P，1080P 源 1080P，4K 降到 1080P；**不向上放大** |
| **1080P** | 最高 1920×1080 | 1080P 源原生清晰；720P 源仍 720P |
| **720P** | 最高 1280×720 | 省内存；1080P 源降采样 |

- 不提供 1440P/4K；4K 源只到 1080P。
- 用户选 1080P 但源 720P 时，UI 明确提示“源 720P，实际仍 720P”，不浪费内存。

### 3.2 缓存挡位（4 档 + 自定义）

| 挡位 | 分辨率 | 缓存 | 1080P 30fps 覆盖 | 1080P 60fps 覆盖 |
|---|---|---:|---:|---:|
| 省内存 | 720P | 128MiB | 约 3.2s | 约 1.6s |
| 均衡 | 1080P | 256MiB | 约 2.9s | 约 1.4s |
| **推荐** | 1080P | 512MiB | 约 5.8s | 约 2.9s |
| 长拖 | 1080P | 1GiB | 约 11.5s | 约 5.75s |
| 高级自定义 | ≤1080P | 最高 2GiB | 约 23s | 约 11.5s |

- **最低有效缓存**：720P 128MiB；1080P 256MiB；低于下限自动抬升/禁止保存。
- **上限**：2GiB，且不超过“当前可用内存 25%”；每实例独立，开多个实例要提示。
- **默认**：自动分辨率 + 512MiB；本机（可用约 17.7GB）建议手动 1080P + 1GiB。
- 512MiB 是通用甜点：1080P 30fps 覆盖 5s GOP，60fps 约 2.9s；
  1GiB 覆盖 60fps 约 5.75s；2GiB 为高级长拖。
- 设置**下次打开在线会话生效**；拖动中不热切换。

### 3.3 UI 文案（设置页“拖动预览”）

```
预览分辨率：自动（推荐） / 1080P / 720P
预览缓存：  省内存 / 均衡 / 推荐 / 长拖 / 自定义
            自定义：____ MiB（128–2048）
当前约可缓存 5.8 秒（1080P / 30fps）
1080P 最低需要 256MiB，已自动调整
源分辨率 720P，实际预览仍为 720P
```

- 显示估算秒数、当前实际分辨率、下限/上限警告；
- 提供“恢复推荐设置”；
- 修改后提示“下次打开在线会话生效”。

---

## 4. 技术设计

### 4.1 配置模型

新增（建议）`lapian::media::PreviewConfig` 或放在 `AppSettings` 的纯函数层：

```
struct PreviewConfig {
  int max_side;        // 1280 / 1920
  size_t cache_bytes;  // 128MiB..2GiB
  bool follow_source;  // auto 模式
};
```

- `AppSettings` 加两个可持久化键：
  - `preview_resolution`：`auto | 1080p | 720p`
  - `preview_cache_mb`：整数 128–2048（或 preset id + custom）
- 解析/校验是纯函数：非法值逐项回退默认；旧 settings 缺键用默认；未知键继续忽略。
- 不需要改 schema 版本，除非现有解析器拒绝新键；若必须 bump，必须保留旧文件兼容。

### 4.2 解码器运行时配置

`OnlinePreviewDecoder`：

- `kPreviewMaxSide` → 实例 `max_side`；
- `kMaxCachedFrames` / `kCacheBudgetBytes` → 实例 `cache_bytes`；
- 帧数由 `cache_bytes / FrameBytes(frame)` 算，不再写死 72；
- `Configure`/构造时传入 `PreviewConfig`；缓存淘汰按字节上限；
- 分配失败：降 25% 预算重试一次，再失败回退 64MiB/1024，并记录；
- 不改缓存帧格式（NV12）与反向预取语义；不改变精确帧/主播放路径。

`MainWindow`：

- 打开在线会话时读 `loaded_settings_`，构造 `PreviewConfig`，传给 `EnsureOnlinePreviewStarted` 创建的解码器；
- `PreviewConfigForSession()` 解析 auto→实际源分辨率（session info）；
- 设置改变只写下一次会话配置，不重建当前 decoder；
- 测试钩子输出实际 `max_side/cache_bytes/frames/actual_cache_peak`。

### 4.3 设置 UI / 持久化

- 建议扩展 `CacheSettingsDialog` 的“拖动预览”分组（Ctrl+, 同一入口），不新增主菜单；
- 回调 `apply` 扩展为同时保存 cache 与 preview 设置；`AppSettings` 原子写；
- 现有 `MergeVolumeWaveform` 等保留键路径必须保留新键（防止保存音量时冲掉）；
- `ApplySettings` 只在下次在线会话生效；
- UI 测试 hooks：`PreviewResolutionForTest()`、`PreviewCacheMbForTest()`、
  `PreviewEstimateTextForTest()`、`SetPreview...ForTest()`、`ApplyForTest()`。

### 4.4 测试与探针

- 复用 `OnlineDragPreviewTestRunner`，加：
  - `--preview-resolution 720|1080|auto`
  - `--preview-cache-mb N`
  - 输出 `preview_config max_side=... cache_mb=... cache_frames=... peak=...`
- 不新增 CMake 源文件优先；若必须新增测试文件，单独 CMake 提交并保持默认 38 项不破。
- `cache_peak_le_64mib` 改为 `cache_peak_le_config_budget`（默认 512 时按 512 断言）；
  保留一个显式 64MiB 兼容用例，证明旧档仍可跑。

### 4.5 文件地图（预计）

| 文件 | 改动 |
|---|---|
| `src/core/AppSettings.{h,cpp}` | 新键、解析/序列化/校验/迁移、保留键 |
| `src/media/OnlinePreviewDecoder.{h,cpp}` | 运行时 max_side/cache_bytes；分配回退；诊断 |
| `src/app/MainWindow.{h,cpp}` | 读设置→config；打开会话/预览创建；诊断钩子 |
| `src/app/CacheSettingsDialog.{h,cpp}` | “拖动预览”分组、估算/下限/上限、保存回调 |
| `src/app/OnlineDragPreviewTestRunner.{h,cpp}` | 配置参数、矩阵、峰值/双向门 |
| `src/app/main.cpp` | 新 CLI 参数解析（仅测试） |
| `tests/*` 现有 settings/decoder/integration 测试 | 配置/边界/持久化/回退 |
| `docs/` | 实现交接、验收、release、AGENTS v6.63 |

**不改**：`PreviewDecoder`（本地 v1）、`GfxWidget`、`MediaSession`、`VideoDecoder` 语义、
`TimelineWidget`、CMake 依赖版本、installer.iss、spec 验收值、M1/M2 门。

---

## 5. 阶段与 agent 安排（串行，6 个 agent）

> 父 agent 做 PM：每阶段先给自包含提示词、只开一个实现 agent、收证据、跑门、再开下一个。
> 同一工作区禁止并行写。

| 阶段 | Agent | 目标 | 主要产出 | 退出条件 |
|---|---|---|---|---|
| **D0** | 父/PM | 冻结计划、基线、默认值；本文 | 本文 + C8 交接 + 新会话提示词 | 用户确认默认值；不改代码 |
| **D1** | **A（配置核心）** | `AppSettings`/`PreviewConfig` 校验、默认、迁移、保留键；纯函数单测 | 配置模型 + 单测；无 UI | 单测过；旧 settings 不丢；默认 512/auto；边界 clamp |
| **D2** | **B（解码器运行时）** | `OnlinePreviewDecoder` max_side/cache_bytes 运行时化；MainWindow 传参；分配回退 | 解码器 + 产品接线；诊断 | 旧 64MiB 显式配置能跑；新 512/1G 可跑；无 OOM；1s 双向不退化 |
| **D3** | **C（设置 UI）** | 设置页分组、挡位/自定义、估算/下限/上限、持久化、下次生效 | UI + settings 保存 | UI 可保存/重启；非法组合自动修正；不热切换 |
| **D4** | **D（集成/性能）** | 配置矩阵探针、1s/2s 双向门、内存峰值、视觉截图、回归 | 数据 + 硬门 | 每挡 1s 通过；512+ 2s 目标；默认不退化；内存峰值合规 |
| **D5** | **E（独立验收）** | 独立复跑全部场景；不接受“只前进/两边烂” | verdict | pass / pass with gaps / fail；失败不改代码 |
| **D6** | **F（发布收口）** | clean build、dist/setup/zip、安装、安装版冒烟、docs/AGENTS | 新包 + release | 安装版通过；C8+D1–D4 一起打包；回滚/文档完整 |

### D1 配置核心（Agent A）

- 先读 `AppSettings`、`SettingsStore`、`CacheSettingsDialog`、`OnlinePreviewDecoder` 常量。
- 加默认/范围/纯函数测试；不碰 UI/decoder。
- 交付 commit：`feat(settings): add preview resolution/cache config`（或拆分）。
- 父复核：旧 settings 读取、未知键、坏值回退、保存音量不冲掉新键。

### D2 解码器（Agent B）

- 只改 `OnlinePreviewDecoder` 运行时配置 + MainWindow 传参；不改 `PreviewDecoder`。
- 现有 72/64MiB 作为兼容默认之一；新增 512/1024/2048。
- 必须测：分配失败回退、缓存峰值、1s 双向、exact、0 网络。
- 交付 commit：`feat(online): configurable preview resolution and cache`。

### D3 UI（Agent C）

- 扩展设置对话框；不新增主菜单；保留 Ctrl+,。
- 估算按实际源 fps/resolution；显示“下次打开生效”。
- 测试 settings 持久化/重启/非法值/自动抬下限。
- 交付 commit：`feat(settings): preview quality/cache presets UI`。

### D4 集成（Agent D）

- 配置矩阵：720P/1080P × 1s/2s × 128/256/512/1024/2048（按挡位裁剪）。
- 1s/60 每个挡位硬门；2s/120 512+ 目标门；256 只记录；
- 与 C8 基线对比，前进下降 >5% 即 fail；
- 输出内存峰值、首帧、P6、0 网络、cache 命中、`hw`。
- 交付 commit：`test(online): preview config matrix and bidirectional gates`。

### D5 独立验收（Agent E）

- 不看实现者结论，独立复跑 D4 矩阵 + UI + 真实窗口；
- 重点：1080P 是否肉眼更清晰；默认 512 是否 1s 前后一致；2G 是否 OOM；
- 失败只报，不修；verdict 写文档。

### D6 发布（Agent F）

- clean `build-release.ps1 -Clean -Package`；ctest 38/38；
- 安装版冒烟：打开预热、1s/2s 双向、B/quality/offline、banner/key/dpi/drag、timeline；
- 文档：handoff/release/AGENTS v6.63；C8 1s 修复随本包一起发布；
- 不改代码/门/阈值；只 docs 提交。

---

## 6. 测试矩阵与验收门

### 6.1 配置矩阵（最小）

| 路径 | 分辨率 | 缓存 | 场景 |
|---|---|---|---|
| cache-hit 720P | auto/720 | 128/512 | 1s/60、2s/120 |
| cache-hit 1080P | auto/1080 | 256/512/1024/2048 | 1s/60、2s/120 |
| growing 720P | auto/720 | 512 | 1s/60、2s/120（节流缓存） |
| growing 1080P | auto/1080 | 512/1024 | 1s/60、2s/120 |
| local 720P（参考） | — | — | 1s/60 前进/后退（9/0） |

- 每个样本至少 3 次，取中位；记录 presented、fps_total、fps_active、first、P6、exact、
  cache peak、`hw`、`cache_frames`、playurl/dash。
- 2s/120 的 256MiB 只记录，不写成通过；512/1G/2G 目标 ≥0.9。
- 30fps/60fps 源分别记录；没有 60fps 源时明确写缺口。

### 6.2 硬门

1. 每个挡位 1s/60：正/反 presented 比 ≥0.9、fps_active 比 ≥0.9、双方 fps_active≥30。
2. 512/1G/2G：2s/120 1080P ≥0.9（若源 60fps；30fps 也记录）。
3. 正向相对 C8 基线下降 ≤5%。
4. P6 保证档 ≤150ms；打开 `stage_show_ms=0`；0 网络。
5. 内存峰值 ≤配置 + 16MiB；2GiB 分配失败有回退，不崩。
6. 设置保存/重启/下次会话生效；非法组合自动修正。
7. 视觉：1080P 截图 vs 1024 截图，文字/边缘明显更清晰；最终精确帧不受影响。
8. 回归：ctest 38/38、key 77/0、banner 74/0、dpi 4 档、drag、B 38/0、quality 42/0、
   offline 45/0、timeline 188/0。

### 6.3 Fail 条件

- 任一挡位 1s 反向 <0.9 或双方任一方 <30fps；
- 前进比 C8 基线下降 >5%，或用“把前进拉低”换后退；
- 2GiB OOM/崩溃；设置不生效/热切换卡死；
- 默认 512 不敌 C8 64/72（同场景）；
- 视觉 1080P 与 1024 无差别（说明没真正生效）；
- 把 2s/120 0.88 写成“通过”。

---

## 7. 风险与回滚

| 风险 | 缓解 |
|---|---|
| 1080P 软件解码 CPU 高 | 先 512/1G 实测；60fps 源单独记录；必要时默认 512 而非 1G |
| 2GiB 多实例内存叠加 | 每实例 2GiB 上限 + 可用内存 25% + 警告；默认 512 |
| 默认从 64 改 512 导致旧测试/低配机波动 | 显式 64MiB 兼容用例；默认 512；8GB 可降 256 |
| 5s GOP 长后退仍重解 | 1G/2G 覆盖更长；不承诺零重解；2s 目标 |
| 设置只改 UI 不生效 | D4 必须 CLI 配置 + 会话生效验证 |
| 本地预览被误改 | v1 只改在线；本地 `PreviewDecoder` 不动 |
| `cache_peak_le_64mib` 误杀 | 改配置感知门，保留旧 64 用例 |
| 分配失败/崩溃 | try/catch + 降预算/降分辨率 + 回退 64 |

回滚：

- 每个阶段独立 commit；D1–D4 可单独 revert；
- 发布回滚：重装 C7 setup `D180A3BD…`（exe `FEF8CE1E…`）；
- C8 未打包，无需分发回滚；D 轮代码回滚用 `git revert`。

---

## 8. 需要拍板的默认值（建议）

1. **默认**：自动分辨率 + **512MiB**（推荐）；本机可手动 1080P + 1GiB。
2. **1080P 最低**：**256MiB**（60fps 约 1.4s；2s@60 需 512）。
3. **上限**：**2GiB + 可用内存 25%**（每实例）。
4. **范围**：v1 只改在线（首次在线 + cache-hit DASH）；本地不改。
5. **2s/120**：512MiB+ 作为目标门；256MiB 只记录。

若用户要改，先改本文再开 D1。

---

## 9. 证据与入口

- [C8 交接（前进/后退数据、清晰度事实、当前安装状态）](handoff-2026-10-27-c8-forward-backward.md)
- [C4/C5 交接（打开预热/缓存命中软解/时间轴）](handoff-2026-10-26-cache-hit-open-drag.md)
- [C4/C5 发布](release-2026-10-26-cache-hit-open-drag.md)
- [在线拖动 perf](perf/online-drag-parity.md)
- [新会话开工提示词](next-session-prompt-preview-parity-config.md)
- 关键代码：`src/media/OnlinePreviewDecoder.cpp`（48–64）、`src/media/PreviewDecoder.cpp`（46–62）、
  `src/core/AppSettings.h`、`src/app/CacheSettingsDialog.{h,cpp}`、`src/app/MainWindow.cpp`
  （`OnlinePreviewEligibleForDrag` / `EnsureOnlinePreviewStarted` / `ShowCacheSettingsDialog`）。
- C8 日志：`build\c8\ship-*`、`build\c8\final-*`；C8 Medium：`%LOCALAPPDATA%\Programs\lapian-c8`。
