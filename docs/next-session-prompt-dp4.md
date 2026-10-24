# 下一会话提示词：DP-4 集成/发布

> 直接复制下面整段作为下一个会话的 prompt。  
> 当前代码基线：`d586548`（DP-3）；DP-2 `7454c32`；DP-1 `b51007a`；
> 规划 `bf37ccd`。

---

你是拉片软件（Windows x64，C++20 + Qt 6 + FFmpeg + D3D11VA）的后续 agent。  
本次只做 **DP-4：拖动预览 v2 集成/发布**，不要重做 DP-1/DP-2/DP-3，
也不要顺手改播放、在线预览、验收阈值或加功能。

## 先读

1. `AGENTS.md`（v6.46，DP-3 已完成；重点读 §1 拖动预览条目、§12 文档表）
2. `docs/plan-drag-preview-v2.md`（§6 DP-4、§7 风险/回滚）
3. `docs/tasks-drag-preview-v2.md`（DP-4 的验收/交付；v1.3）
4. `docs/handoff-2026-10-24-dp3.md`（**必读**：DP-3 实现/边界/残余）
5. `docs/perf/drag-preview-dp3.md`（DP-3 证据与数字）
6. 代码：`src/media/DragStrategy.*`、`src/media/PreviewDecoder.*`、
   `src/app/MainWindow.*`、`src/app/DragStrategyTestRunner.*`、
   `src/app/DragTestRunner.*`、`src/app/main.cpp`
7. 脚本：`scripts/build-release.ps1`、`scripts/package.ps1`、
   `scripts/check-imports.ps1`、`scripts/size-report.ps1`、
   `installer.iss`、`scripts/lib/common.ps1`

## 基线（DP-3，提交 `d586548`）

- 代码基线：`d586548`；DP-2 `7454c32`；DP-1 `b51007a`；规划 `bf37ccd`。
- 回归：`ctest` **37/37**、key **77/0**、T8.6.1b **35/0**。
- **dist 未在 DP-3 重建**；当前仍是 DP-2 时期的 **34.48MB/25 文件**。
  DP-4 必须从 `d586548` 的代码重建并记录实际大小/文件数。
- 1080p：`media\测试.mp4`，1920×1080 H.264 30fps、4572 帧、GOP 30。
- DP-3 实测（1080p，默认硬件；`hw_active=1`）：
  - 慢拖 `+1/-1`（33ms/帧）：`target_lag p95=0`；`coarse_presents=0`。
  - 15.6ms 小步：`+1` p95=1 帧、`-1` p95=2 帧（≤2）。
  - `+60`：76.9–80.8fps / 空档 34.7–37.2ms。
  - `-60`：54.4–64.4fps / 空档 23.8–39.3ms（同会话 DP-2 基线
    48.7–58.1fps；历史轻载证据 60.2–69.2）。
  - `-20`：47.3–49.2fps。
  - `-1`：81.6–85.4fps，精确命中 84.7–85.5%，lag p95=2。
  - 远跳硬件前/后：粗帧 **20.8/20.6ms** → 精确 **30.6/30.0ms**；
    `coarse=1` 且先于精确；松手 17.5/18.6ms。
  - 远跳软件前/后：粗帧 40.5/20.1ms → 精确 110.4/219.9ms。
  - `--drag-test`：P4 **94.9**、P6 p95 **18.8**、P7 0.1/11.8、
    P8 12.8、P9 32.2，全 pass。
  - 预览缓存峰值 **244,684,800B = 233.4MiB**；3 次连续拖动峰值不变；
    缩略图本轮观测 ≤432KB（DP-2 观测 ≤28.8MB；预算 64MiB 未变）。
  - 4K `proxy-V4`：69.4fps / 空档 20.1ms / `hw_active=0` /
    `source_too_large`（**只记录，不设通过线**）。
- 强制软件快反向残余（DP-3 未改善，DP-4 不得写成通过）：
  `-20` **9.5fps / 空档 211ms**、`-60` **31.4fps / 50.6ms**；
  `-1` 89.5fps / 命中 94.2%。

## DP-3 关键边界（不要改坏）

1. **DragMode 四模式**：`Exact ≤4 帧`、`Fast 5..299 向前`、
   `Reverse 5..299 向后`、`FarJump ≥300 帧`；阈值在
   `src/media/DragStrategy.h`，探针输出 `drag_mode/reason/step/interval/
   speed/run`。
2. **Exact 不用粗帧**：Begin/UpdateTarget/缩略图线程三处都关闭；
   切回 Exact 时丢弃未呈现粗帧。
3. **FarJump 先粗后精**：粗帧未取走时精确帧进 `deferred_exact`，
   粗帧呈现后下一 tick 替换；`End/Begin/UpdateTarget/Close` 清暂存。
4. **精确目标已呈现后禁粗帧**：判据
   `last_delivered_frame == target_frame && !last_delivered_coarse`；
   不能放宽成“粗帧更早就可以”，否则远跳反向会把画面拉回目标之前。
5. 硬件帧不缓存、不逐目标回拷；`readbacks=0`；粗帧仍是关键帧 clone
   或软件 320×180。
6. 缓存预算：32 帧热缓存（≤48MiB）+ 环缓存 192MiB + 缩略图 64MiB；
   `End` 后收窄 32/16MiB；不做全片/无界缓存。
7. 松手 `End + GoToFrame` 精确；P6 阈值 ≤150ms 不变。
8. 4K 继续软件预览（`source_too_large`），不打开 4K 硬解，只记录。

## DP-4 目标

1. **全量回归（代码不变优先）**：
   - `ctest` 37/37、key 77/0、T8.6.1b 35/0；
   - `--drag-strategy-test`、`--drag-fast` +60/-60/-20/-1、
     `--drag-far-test`、`--drag-test` 复跑；
   - P4/P6/P7/P8/P9 阈值不变；4K 只记录；
   - 内存/表面池不退化：预览缓存 ≤256MiB、缩略图 ≤64MiB、
     `readbacks=0`。
2. **重建 dist**：
   - 用 `scripts\build-release.ps1`（它会清 `CMakeCache.txt`、用
     `.deps\tmp`、构建、跑 ctest、组装 dist、windeployqt、
     环境自检、`check-imports.ps1`、`size-report.ps1`）；
   - 记录实际体积/25 文件（预期约 34.5MB，以实际为准）；
   - 确认没有 `Qt6Network.dll`/`D3dcompiler_47.dll`/`tls` 等；
   - 需要 zip/setup 时再跑 `-Package`；不改 installer 逻辑，
     除非发现明确的打包回归并单独说明。
3. **Medium 副本验证**：
   - 从 `%LOCALAPPDATA%\Programs\拉片软件`（installer 或整目录复制）
     运行，**不要**用工作区 `build\release\bin` 的 Low integrity 进程
     验证默认 AppData/缓存路径；
   - 打开 1080p、拖动、松手精确、键位、`--drag-test`/本地播放；
   - 本地模式不写默认缓存/记录/凭据（在线相关默认路径只做既有回归，
     不打印 SESSDATA/Cookie/签名 URL）。
4. **文档/提交**：
   - `docs/perf/drag-preview-dp4.md`；
   - `docs/handoff-2026-10-24-dp4.md`；
   - 更新 `AGENTS.md`（v6.47，如实写完成/partial/blocked）与
     `docs/tasks-drag-preview-v2.md`（v1.4，DP-4 状态）；
   - 一个可审查提交；dist 是 gitignore 产物，不提交二进制；
     工作区干净。

## 验收（DP-4）

- `ctest` **37/37**、key **77/0**、T8.6.1b **35/0**；
- `--drag-test` P4/P6/P7/P8/P9 不退化（P4 ≥30、P6 ≤150ms、
  P7 p95 ≤80ms、P8 ≥12fps、P9 ≤200ms；4K 用放宽档但只记录）；
- DP-1/2/3 指标复跑不出现明显回归：
  慢拖 p95 ≤2 帧、远跳粗帧 ≤100ms、P6 ≤150ms、缓存峰值 ≤256MiB；
- dist 重建成功，体积/25 文件/导入检查/环境自检通过；
- Medium 副本可启动并完成本地拖动/松手/键位；
- 4K 只记录，不写 4K 通过；不写 M1/M2/P4/P7 通过。

## 停止/回退

- 任何主播放、在线预览、精确松手、P4/P6/P7 回归 → **不要发布**；
  先定位是否为 DP-1/2/3 改动，必要时回滚对应策略，保留其余；
- dist/安装包失败 → 记录具体错误，不要手改生成的 dist 冒充通过；
- 4K/HEVC 仍不达标 → 只记录，不改 1080p 门、不设通过；
- 预取/缓存导致内存、UI 空档、表面池变差 → 缩预算/关预取，
  不突破 256/64MiB。

## 先做（不要直接打包）

1. 读文档，确认 `git status` 干净、HEAD 是 `d586548`（或其后仅文档提交）。
2. 用现有 `build\release` 先复跑 DP-3 矩阵和 `--drag-test`，
   确认数字与交接一致；先记录，不改代码。
3. 跑 `scripts\build-release.ps1` 做一次干净 Release 构建 + ctest + dist；
   如 CMake 3.31 重配崩溃，脚本已删除 `CMakeCache.txt` 并用
   `.deps\tmp`；手动构建时用 `build\tmp-build2` 或 `--fresh`。
4. 验证 dist/Medium 副本，再跑 `-Package`（如需）和最终回归。
5. 最后写证据/交接、更新 AGENTS/任务表、提交。

## 复现命令

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$exe = 'build\release\bin\lapian.exe'

# 中文路径在探针 argv 下可能打不开；建 ASCII hardlink（不要提交）
New-Item -ItemType HardLink -Path build\dp1-test.mp4 -Value media\测试.mp4 -ErrorAction SilentlyContinue

& $exe --file build\dp1-test.mp4 --drag-strategy-test
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step 60
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -60
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -20
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -1
& $exe --file build\dp1-test.mp4 --drag-far-test
& $exe --file build\dp1-test.mp4 --drag-test --drag-samples 20
& $exe --file build\dp1-test.mp4 --key-test
& $exe --file media\proxy\proxy-V4.mp4 --drag-fast --drag-fast-step 60  # 4K 只记录

# 推荐发布构建：配置/构建/ctest/dist/env check/imports/size 一次完成
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
# 需要便携 zip/setup 时
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1 -Package
```

## 测量注意（DP-3 踩过的坑）

- PowerShell 的 `2>` 会把长 stderr 按控制台宽度插换行，导致探针行被拆断；
  用 `Start-Process -RedirectStandardError` 或 `cmd /c "... 2> file"` 取原始行。
- P4 的 10ms `Qt::PreciseTimer` 受系统计时器粒度影响：DP-3 本轮约
  10ms（P4 94.9），DP-2 时约 15.6ms（P4 64.1）。**不要把两种环境的 P4
  直接对比成产品提升**；阈值 30 不变。
- `-60` 有 5–10fps 的机器负载波动（AfterFX 等桌面程序）；报告时写清
  同会话基线，不要只挑最好看的一组。
- 慢拖门只认 `+1/-1`；`+3`/15.6ms 的 p95≈3 是采样效应，不写通过。
- `--drag-strategy-test` 退出码 0 表示探针跑完（证据模式），通过与否看
  `all_pass=1` / 各场景 `pass=1`；基础设施失败才是 2。

## 交付

- 代码（仅必要的回归修复）+ `docs/perf/drag-preview-dp4.md`；
- `docs/handoff-2026-10-24-dp4.md`；
- 更新 `AGENTS.md` v6.47、`docs/tasks-drag-preview-v2.md` v1.4；
- 一个可审查提交；工作区干净；dist 重建但不提交二进制；
- 最终答复写实际 dist 体积/25 文件、ctest/key/T8.6.1b、drag-test 数字、
  Medium 验证与 4K 残余；**不写 M1/M2/P4/P7 通过、不写 4K 通过**。

## 不要做

- 不要重做 DP-1 共享设备硬解/4K 策略，不要打开 4K 硬解；
- 不要重做 DP-2 缓存/预取/粗帧，不要重做 DP-3 四模式（除非回归修复）；
- 不要改 P4/P6/P7 阈值、M1/M2 门、OB、`lapian-cache-1` schema、
  `OnlinePreviewDecoder`/在线预览；
- 不要缓存十几张硬件帧；不要无界/全片缓存；
- 不要打印/提交 SESSDATA、Cookie、签名 URL；不写默认缓存/记录/凭据；
- 不要写 4K 通过，不要写 M1/M2/P4/P7 通过；
- 不要手改 dist/安装包冒充通过；不要把 `build\dp1-test.mp4`、
  测量日志、缓存/记录目录提交。

---

## 建议的交付顺序（可选）

1. 基线复跑（只记录）；
2. `build-release.ps1` 干净构建 + ctest；
3. dist/导入/体积检查；
4. Medium 副本本地拖动/松手/键位；
5. 最终 DP-1/2/3 指标复跑；
6. 证据/交接/AGENTS/任务表；
7. 单提交、清工作区。
