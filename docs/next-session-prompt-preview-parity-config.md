# 新会话开工提示词：在线预览清晰度/缓存配置化 + 前后一致

> **用法**：新开一个会话，把本文件内容作为首条消息发给 agent。
> 不要在旧的长会话里继续开工；旧会话只保留规划结论。

---

你是「拉片软件」的新会话 **父 agent / PM**。请先读：

1. `AGENTS.md`（v6.63 规划条目）
2. `docs/plan-2026-10-27-preview-parity-config.md`（**本次唯一执行计划**）
3. `docs/handoff-2026-10-27-c8-forward-backward.md`（**当前代码/数据/安装状态**）
4. `docs/handoff-2026-10-26-cache-hit-open-drag.md`、`spec.md` v0.4 §4.1

不要凭旧对话记忆开工；以这三份文档和实际 git 状态为准。

## 0. 当前状态（必须核对）

- 仓库：`E:\拉片软件开发`，branch `master`，HEAD **`961a19b`**，工作树应干净。
- C8 代码已提交（`87660ef` test + `961a19b` fix），**但未打包、未安装**。
- 当前安装版 C: `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe` = `FEF8CE1E…`（C7，不含 C8）。
- C8 新 exe 只在 `build\release\bin` / `%LOCALAPPDATA%\Programs\lapian-c8`（`99BC1201…`，Medium）。
- 用户 E: 两份免安装副本都旧（`DDF2E8E9…` / `E19FC4B3…`），不要动。
- C8 1s/60 已做到 0.93–1.04、双方 54–60fps；2s/120 1080P 仍 0.877（cache-hit）/0.886（growing），
  在线预览固定软件 1024，明显比本地硬解全分辨率软。
- 最终目标：**在线拖动预览清晰、舒适、流畅，前进/后退随意；不能只顾前进，不能两边一样烂，
  性能不许倒退。**

## 1. 不要做的事

- 不要在本会话重开旧方案；不要改 `TimelineWidget`、`PreviewDecoder` 本地路径、`GfxWidget`、
  `MediaSession`、`VideoDecoder` 语义、CMake/Qt/FFmpeg 版本、installer.iss、spec 验收值、M1/M2 门。
- 不要在 D1–D4 期间重打包/安装；D6 才 clean build + setup/zip + 安装。
- 不要并行开多个写工作区的 agent；一次一个，父复核后再开下一个。
- 不要把 2s/120 0.88 写成通过；不要把“本地短 GOP/在线长 GOP”写成规律。
- 不要覆盖用户 E: 免安装版或默认缓存/记录/截图/凭据。

## 2. 执行方式

父 agent 只做：核对、拆任务、写自包含提示词、串行开子 agent、跑门、收证据、汇报。
**父不写产品代码。** 每个阶段一个 agent：

| 阶段 | Agent | 任务 |
|---|---|---|
| D0 | 父/PM | 核对状态、确认默认值、冻结计划；不写代码 |
| D1 | Agent A | `AppSettings`/`PreviewConfig`：解析、校验、默认、迁移、保留键、纯函数单测 |
| D2 | Agent B | `OnlinePreviewDecoder` 运行时 `max_side/cache_bytes`；MainWindow 传参；分配回退 |
| D3 | Agent C | 设置页“拖动预览”分组、挡位/自定义、估算/下限/上限、持久化、下次生效 |
| D4 | Agent D | 配置矩阵探针、1s/2s 双向门、内存峰值、视觉 + 回归 |
| D5 | Agent E | 独立验收；失败只报告，不改代码 |
| D6 | Agent F | clean build/package/install/冒烟/docs/AGENTS v6.63 |

默认按计划推荐值执行；用户若未反对：
- 分辨率：自动 / 1080P / 720P（自动不向上放大，4K 降到 1080P）；
- 缓存：省内存 720P/128MiB、均衡 1080P/256MiB、推荐 1080P/512MiB、长拖 1080P/1GiB、高级 1080P/2GiB；
- 720P 最低 128MiB，1080P 最低 256MiB；上限 2GiB 且 ≤可用内存 25%；
- 默认自动 + 512MiB；本机可 1080P + 1GiB；
- v1 只改在线预览；本地 `PreviewDecoder` 不动；
- 设置下次打开在线会话生效，不热切换。

## 3. 硬性验收（不可放宽）

1. 前进/后退**同目标率、同跨度、同指标**；反向 presented ≥正向 90%、反向 fps_active ≥正向 90%、
   双方 fps_active ≥30。只测前进 = 未完成；“两边都低”= fail。
2. 每个挡位 1s/60（720P/1080P cache-hit）必须过；512MiB+ 挡位 2s/120 1080P 目标 ≥0.9；
   256MiB 的 2s/120 只记录，不写通过。
3. 正向相对 C8 基线下降 >5% = fail；不允许用降低前进换后退。
4. P6 保证档 ≤150ms、打开 `stage_show_ms=0`、`playurl/dash=0`、exact frame/PTS。
5. 内存峰值 ≤配置 + 16MiB；2GiB 分配失败回退不崩；多实例/可用内存 25% 警告。
6. 设置保存/重启/下次会话生效；非法组合自动抬下限/夹上限。
7. 1080P 截图相对 1024 肉眼更清晰；最终精确帧/截图不受预览影响。
8. 回归：ctest **38/38**、key **77/0**、banner **74/0**、dpi 4 档、drag、B 720P/1080P **38/0**、
   普通 42/0、offline 45/0、timeline 188/0；原有 `cache_peak_le_64mib` 改配置感知，
   但保留显式 64MiB 兼容用例。
9. 失败/残差只记录；不静默改阈值/门/spec。

## 4. 构建/测试环境（本机）

```powershell
cd 'E:\拉片软件开发'
$env:TMP='E:\拉片软件开发\.deps\tmp'; $env:TEMP=$env:TMP
& 'E:\拉片软件开发\third_party\cmake\3.31.6\cmake\data\bin\cmake.exe' --build build\release --config Release

$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build\release -C Release --output-on-failure
```

- **只增量构建**；本机 CMake 重配可能 `0xC0000409`，D6 clean build 用脚本。
- ctest 根产物 `credentials.dat/pictures/videos` 提交前删。
- 在线/缓存命中测试必须用 Medium 副本（Low 不能写 AppData）；D2 后复制 `dist\*` 到
  `%LOCALAPPDATA%\Programs\lapian-preview` + 新 exe；不要覆盖安装版。
- 测试夹具：720P `build\c2-720p\cache`（BV1cW411b7Yh/28925848），1080P `build\pm-b-fresh\cache`
  （BV1BMnv6jE5S/42915922022，112 未缓存）；用隔离 cache/pics/records/settings。
- 凭据默认 DPAPI；不打印 SESSDATA/Cookie/URL。
- 用户 E: 实例可能在前台；不要杀/覆盖。

## 5. 每阶段交付

- D1：配置 commit + 单测；旧 settings/坏值/保留键证据。
- D2：运行时配置 commit + 1s 双向/64MiB 兼容证据；不改变本地路径。
- D3：UI commit + 保存/重启/非法组合证据。
- D4：探针/矩阵 commit + 每挡 1s、512+ 2s、内存/视觉/回归表；失败不美化。
- D5：独立 verdict（pass / pass with gaps / fail）+ 原始日志。
- D6：clean build 时间、ctest、dist/setup/zip hash、安装 exit/备份/settings、安装版冒烟、
  handoff/release/AGENTS v6.63；C8 1s 修复随本包一起发。

## 6. 参考数字（C8 基线，不要重算错）

- 1s/60 cache-hit：720P 55/56 = 1.018；1080P 54/56 = 1.037；first-online 1.018/1.000；
  本地软件 0.929；双方 54–60fps。
- 2s/120 cache-hit 1080P：114/100 = **0.877**；growing 114/101 = **0.886**；不写通过。
- 预览帧：1024×576≈0.844MiB；1280×720≈1.318MiB；1920×1080≈2.966MiB。
- 缓存覆盖：1080P 512MiB ≈30fps 5.8s / 60fps 2.9s；1GiB ≈11.5s/5.75s；2GiB ≈23s/11.5s。
- 旧探针反向 30 目标 vs 正向 60 目标不可比；必须 `--drag-backward-full` 同口径。
- C8 未打包；当前安装版仍是 C7 `FEF8CE1E`。

## 7. 第一件事

先核对 `git status`/HEAD、C8 安装状态、计划文档；然后向用户复述：
- 预计 D1–D6、6 个 agent、串行；
- 默认值（自动+512、1080P 下限 256、上限 2GiB/25%）；
- 1s 硬门 + 2s 目标 + 性能不倒退 + 不能两边烂；
等用户确认后，再开 D1。**不要在 D1 前改代码/打包。**
