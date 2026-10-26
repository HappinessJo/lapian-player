# 交接：在线预览清晰度/缓存配置化 — D6 发布收口（2026-10-27）

- **状态**：D0–D5 完成（D5 `verdict=pass with gaps`），D6 clean build/打包/安装/安装版冒烟/文档完成。
  分发包已生成、每用户静默安装可用、安装版核心冒烟通过；**不写“发布就绪/T7.7/P14 通过”**。
- **代码 HEAD**：`7c7f44c`（docs-only；产品/测试树 = `0539055`）。本轮只提交
  `docs/release-2026-10-27-preview-config.md`、本文件、`AGENTS.md`（v6.63 改为已发布）。
- **安装版**：`%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`，**3,687,424 B / `588B8D5E…`**
  （= dist/build/stage 同 hash）；旧 C7 安装版 `FEF8CE1E…` 已被覆盖，回滚见发布文档 §9。
- **发布记录**：[release-2026-10-27-preview-config.md](release-2026-10-27-preview-config.md)。
- **上游**：[plan §5/§6](plan-2026-10-27-preview-parity-config.md)、
  [D4 matrix](perf/preview-config-matrix.md)、
  [D5 独立验收](handoff-2026-10-27-preview-config-acceptance.md)、
  [C8 交接](handoff-2026-10-27-c8-forward-backward.md)、
  [C4/C5 发布](release-2026-10-26-cache-hit-open-drag.md)。

---

## 1. 一句话结论

C8 的 1s/60 修复与 D1–D4 的“自动/1080P/720P + 五档缓存（默认 auto/512）”一起打包发布。
安装版复跑：**1s/60 1080P 与 720P 默认配置双向通过**（1080P 正 55/58.82、反 56/59.96，
比 1.018/1.019；720P 56/59.96 vs 56/60.02，比 1.000/1.001）；**2s/120 1080P 512 三次
1.009–1.018**（D5 范围 1.009–1.045）；**1080P 预览 1920×1080**、720P 1280×720；
**P6 保证档 80ms**；**legacy 1024/64 parity 61/0**；**设置迁移 1920/512**；
**设置页 137/0 ×2**；**B 38/0 ×2**、**offline 45/0**；**quality blocked**（SESSDATA 失效）。
ctest **38/38**、timeline **188/0**、banner **74/0**、key **77/0**、dpi 4 档 **37/0**、
drag P4 **95.0** / P6 **24.5ms**、loop exit 0。**9 条 gap 原样见发布文档 §6。**

---

## 2. 关键数字（安装版，本轮实测）

| 项 | 值 |
|---|---|
| clean 构建 | `build-release.ps1 -Clean -Package`，exit 0，**262.6s 墙钟**（脚本 247.3s） |
| ctest | **38/38**（脚本 66.25s；手动 65.54s / ctest 65.46s） |
| timeline | **188 OK / 0 fail**（1.16s） |
| exe | **3,687,424 B**，`588B8D5E858614A4DB394B589312A80FE58A53C04985C7D5780D50777171E54E` |
| dist / stage | **26 文件 / 36,965,192 B**；**35 文件 / 37,106,158 B** |
| setup / zip | **12,636,766 B / `AF32B4C3…`**；**15,438,404 B / `21FE4925…`** |
| 静默安装 | **exit 0 / 2.5s**；`User privileges: None`、`Administrative install mode: No`、HKCU；安装版 exe == dist；settings `0D69C495…` 未变；备份 `99084199…`/`5863DD0A…` 保留；`lapian-c8` `99BC1201…` 未动 |
| 打开预热 | 1080P `prewarm=139ms first=1 stage_show=0 wait_ready=88`；720P 147/0/80 |
| 1s/60 1080P 默认 | 1920/512、1920×1080、正 55/58.82、反 56/59.96、比 1.018/1.019、峰值 240.27MiB（81/172）、28/0 |
| 1s/60 720P 默认 | 1280/512、1280×720、56/59.96 vs 56/60.02、比 1.000/1.001、峰值 106.79MiB（81/388）、28/0 |
| 2s/120 1080P 512 | 三次 1.009 / 1.017 / 1.018（presented 与 fps 双比）、峰值 403.42MiB（136/172）、28/0 |
| P6 保证档 | 1080P start150 **release+landing=80ms**（premise 60 帧/2000ms）、峰值 447.90MiB、28/0 |
| legacy parity | **61/0**、`legacy_64mib peak=66,355,200 frames=75 limit=75`、两条配置感知门 ok=1 |
| 设置迁移 | 旧 settings（无 preview 键）→ **1920/512**、1920×1080、28/0；原件 hash 未变 |
| 设置页 UI | 100% / 200% 各 **137/0、pass、dialog-fits OK** |
| B / offline | 720P **38/0**、1080P **38/0**；offline **45/0** |
| quality | **blocked**：`nav_login ok=0`、`vip_status=-1`、`sample_block=login_unavailable`，16 项/1 fail、end `verdict=fail` |
| 基础 | banner **74/0**、key **77/0**、dpi **37/0 ×4**（problems 空）、drag P4 **95.0**/P6 **24.5**/P7 0.0/15.1/P8 12.9/P9 30.9、loop exit 0、timeline **188/0** |

---

## 3. 复现（安装版，隔离根）

所有运行显式隔离，不写用户 settings/缓存/记录/截图；夹具先复制到 `build\c9-smoke\cache\*`。
GUI 探针必须 `Start-Process -Wait -RedirectStandardOutput/-RedirectStandardError`。

```powershell
cd 'E:\拉片软件开发'
$env:TMP='E:\拉片软件开发\.deps\tmp'; $env:TEMP=$env:TMP
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$exe = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"
# 脚本（ASCII-only，PS 5.1 可读；参数从命令行传中文路径）
# build\c9-smoke\run_local.ps1 -ExePath $exe -MediaPath 'media\测试.mp4' -QtBin <Qt bin>
# build\c9-smoke\run_online.ps1 -ExePath $exe -UserSettingsFile "$env:APPDATA\拉片软件\settings.json" -Phase core|long|regress
```

单格命令、legacy parity、设置页 UI 的完整骨架见 [release §10](release-2026-10-27-preview-config.md)。

关键坑：
- `--settings-path` 是**目录**（内含 `settings.json`），不是文件；迁移测试指向
  `build\c9-smoke\migrate`（里面放复制的 `settings.json`）。
- `--online-drag-preview-test --drag-cache-hit` 不会清缓存根；`--online-drag-parity-test`
  （不带 `--drag-cache-hit`）会清缓存根并走 growing + 真实下载（legacy parity 本轮约 140s）。
- ctest 必须设 `TEMP/TMP`；默认 `%TEMP%` 下本机 11 项假失败（`QTemporaryDir` 环境问题）。
- 安装版 exe 是 GUI 子系统，Qt 运行库在同目录，不需要额外 PATH；只有构建树的
  `lapian_timeline_test.exe` 需要 Qt `bin` 在 PATH。

---

## 4. 未做 / 未覆盖（下一次会话入口）

1. **quality 42/0 未复现（blocked）**：需要有效 SESSDATA；补 1080P60/4K 档位断言。
2. **无 60fps 源**：所有“60”是拖动目标率；需要 60fps 素材重跑 1s/2s 矩阵。
3. **2s/120 的 256MiB 与 720P 只记录**；2s 判定是人工口径（探针 `out_of_parity_gate=1`）。
4. **P6 非保证档窗口只记录**（默认 1s 窗口 82–110ms、2s 149–202ms，`out_of_premise`）。
5. **视觉原生 lap_var 0.522 只记录**（跨分辨率不可比；同显示尺寸 6.398× 为判据）。
6. **本地 720P 参考反向 0.873 只记录**（非在线门）。
7. **D5 G4 热切换 exit code 未回读**：本轮未重跑“运行中改 settings 不热切换”，
   只有 D5 的日志证据（`checks=28 failures=0 verdict=pass` + 配置未变）。
8. **未覆盖**：真实 4K/60fps、登录态 1080P60/4K、多实例并发 2GiB、长 GOP >2s 的保证档外推、
   真实窗口/物理鼠标/多 DPI 拖动体感、无符号化崩溃 dump、代码签名/干净机复验、Explorer 真双击。
9. **用户体感未收集**：本轮没有新的用户实机反馈。

---

## 5. 与 C8 / plan 的衔接

- **C8**（`87660ef`/`961a19b`）的“缓存命中 DASH 1s/60 前后 0.93–1.04”随本包发布；
  D1–D4 没有回退 C8，只是把预览解码器改成运行时 `max_side/cache_bytes`，默认 1920/512。
- **plan §5 D6 的退出条件**已满足：clean `-Clean -Package`、ctest 38/38、安装版冒烟
  （预热、1s/2s 双向、B/quality/offline、banner/key/dpi/drag、timeline）、文档
  handoff/release/AGENTS v6.63、C8+D1–D4 一起打包；**quality 是已知环境 gap，不是 D6 漏做**。
- **plan §6.2 硬门在 D5 全部通过**（除 quality blocked 与视觉 lap_var 判据未达——后者是探针
  指标问题，不是产品门）；D6 未改任何门/阈值/spec/installer。
- 下一步若要继续，按优先级：① 有效 SESSDATA 重跑 quality；② 找 60fps 源重跑矩阵；
  ③ 2s/120 的 256MiB/720P 与 2GiB 多实例；④ 真实体感/真实鼠标；⑤ 4K/60 与登录态。
- **回滚**：重装 C7 setup `D180A3BD…`（exe `FEF8CE1E…`）；代码 `git revert afc0f95 0539055
  acea5c5 9751131 d9c5fa6`（C8 另加 `961a19b 87660ef`）。详见发布文档 §9。
