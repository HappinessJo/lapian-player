# DP-4 拖动预览 v2 集成/发布（证据）

- **版本**：v1.0（2026-10-24）
- **状态**：**完成（集成/发布）**。从 `d586548` 代码基线 clean Release 重建，
  `dist` 25 文件 / **34.55MB**（36,227,912B），Medium 副本本地拖动/松手/键位/
  播放验证通过；installer/zip 已生成。强制软件快反向与 4K 仍按残余/只记录，
  不写通过；不改 M1/M2/P4/P7 门或数字。
- **性质**：DP-4 只做回归、dist、Medium 副本与文档；不改代码、阈值、播放、
  在线预览、缓存 schema、默认 AVC≤1080p。
- **代码基线**：`d586548`（DP-3）；`9e584e9` 仅追加 DP-4 提示词文档。
- **交付提交**：见 `git log -1`（本证据、交接与 AGENTS/任务表同一提交）。
- **交接**：handoff-2026-10-24-dp4.md
- **上一轮**：drag-preview-dp3.md
- **任务表**：tasks-drag-preview-v2.md

---

## 1. 结论

1. **基线复跑与 DP-3 交接一致**：`--drag-strategy-test` `all_pass=1`；
   慢拖 +1/-1 `lag p95=0`；`+60` 79.1fps/空档 35.7ms、`-60` 59.6fps/
   35.6ms、`-20` 47.4fps、`-1` 命中 83.6%/lag p95=2；远跳粗帧
   20.5/20.9ms → 精确 30.1/29.1ms；`--drag-test` P4 65.5、P6 21.6、
   P7 0.1/9.8、P8 12.3、P9 39.0（探针值；P4/P7 不作正式验收）。
2. **clean Release 重建成功**：`scripts\build-release.ps1 -Clean` 用时
   **256.4s**；CMake 3.31 重配正常；`ctest` **37/37**（64.40s）。
3. **dist 已重建**：**25 文件 / 36,227,912B = 34.55MB**；比 DP-2 时期
   34.48MB 增加 71,168B，全部来自 `lapian.exe`（DP-3 代码）。无
   `Qt6Network.dll`/`D3dcompiler_47.dll`/`tls`/`networkinformation`/
   `generic`；`check-imports.ps1` 通过（20 个 exe/dll，无
   winhttp/wininet/urlmon），环境自检通过（Qt 6.8.3、FFmpeg 7.1.1、
   D3D11 RTX 4060、FFmpeg 网络协议禁用）。
4. **重建后二进制回归不退化**：策略矩阵 `all_pass=1`；快拖
   `+60` 80.7fps/31.2ms、`-60` 50.4–57.5fps/29.0–38.7ms、
   `-20` 49.8fps、`-1` 82.3fps/命中 84.2%/lag p95=2；远跳粗帧
   21.9/20.5ms → 精确 29.8/40.8ms；`--drag-test` P4 64.5、P6 22.3、
   P7 0.1/16.1、P8 12.9、P9 39.8；key **77/0**；T8.6.1b **35/0**。
5. **P4 回落是计时器环境，不是产品改动**：本轮 P4 **64.5**，与 DP-2 的
   64.1 同量级；DP-3 的 94.9 是系统计时器粒度约 10ms 时的读数。DP-3 交接
   已预告“粒度回到 15.6ms 时 P4 回到约 64fps”。**P4 阈值 30 未改，不写
   P4 正式通过**。
6. **有界**：预览缓存峰值 **244,684,800B = 233.4MiB**（≤256MiB），
   `-1` 多次运行不变；策略矩阵峰值 88,473,600B；缩略图本轮最大观测
   **2,851,200B**（2.72MiB，≤64MiB）；所有探针 `readbacks=0`；无黑帧、
   崩溃、设备丢失、表面池耗尽。
7. **Medium 副本验证**：`%LOCALAPPDATA%\Programs\拉片软件`（stage 整目录
   复制，33 文件 / 36,343,599B）`lapian.exe` SHA256 与 `dist` 一致；
   `--drag-test` P4 65.0、P6 17.7、P7 0.1/14.9、P8 12.4、P9 31.4；
   `--drag-strategy-test` `all_pass=1`；key 77/0；`--loop-test` 完成 3 次
   循环/定帧/恢复，退出码 0。默认凭据/设置/在线记录/缓存 mtime 未变。
8. **打包**：portable zip **14.43MB**、setup **11.86MB**，stage 33 文件 /
   34.66MB；P12 上限 100MB 通过。setup 从 E: 启动被主机 SmartScreen/策略
   拦截，本轮 Medium 验证按任务允许使用整目录复制，**不写“安装器本轮已
   重装”**。
9. **强制软件快反向仍是残余**：`-20` **10.5fps/空档 190.1ms**、
   `-60` **33.4fps/49.9ms**，与 DP-2/DP-3 同量级，未达 25/45fps 体验
   目标；`-1` 89.7fps/命中 94.7%。**不写成通过。**
10. **4K 只记录**：`proxy-V4` 67.6fps/空档 11.5ms、`hw_active=0`、
    `source_too_large`；不设通过线，不写 4K 通过。
11. **未改门**：M1/M2、P4/P6/P7 阈值、OB 样本、`lapian-cache-1` schema、
    默认 AVC≤1080p、`OnlinePreviewDecoder`/T8.6.1b 语义均未改。

## 2. 环境与复现

- 开发机：AMD Ryzen 5 7500F / 31.6GB / RTX 4060（同 DP-1/DP-2/DP-3）。
- 素材：`media\测试.mp4`（1920×1080 H.264 30fps、4572 帧、GOP 30）；
  探针用 `build\dp1-test.mp4` ASCII hardlink（临时，不提交）。
- 4K：`media\proxy\proxy-V4.mp4`（3840×2160，只记录）。
- 本轮系统计时器粒度约 15.6ms（P4 64.5 与 DP-2 64.1 同量级）；
  不是 DP-3 的约 10ms 环境。不要拿两种环境的 P4 直接对比成产品变化。
- **ctest 临时目录**：工作区 exe 继承 Low integrity，不能写系统 `%TEMP%`
  （Medium）；手动跑 `ctest` 时必须把 `TMP`/`TEMP` 指到工作区
  `.deps\tmp`，否则 11 个文件类测试会以“无法创建临时目录”失败。
  `build-release.ps1` 已内置这一设置；本轮脚本内 `ctest` 37/37。
- E: 被 Windows 视为不受信任位置；脚本需
  `powershell.exe -ExecutionPolicy Bypass -File ...` 运行。

```powershell
$env:PATH = "E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$exe = 'build\release\bin\lapian.exe'
New-Item -ItemType HardLink -Path build\dp1-test.mp4 -Value media\测试.mp4 -ErrorAction SilentlyContinue

# 基线/重建后矩阵
& $exe --file build\dp1-test.mp4 --drag-strategy-test
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step 60
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -60
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -20
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -1
& $exe --file build\dp1-test.mp4 --drag-far-test
& $exe --file build\dp1-test.mp4 --drag-test --drag-samples 20
& $exe --file build\dp1-test.mp4 --key-test

# 强制软件残余 / 4K 只记录
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -20 --preview-force-software
& $exe --file build\dp1-test.mp4 --drag-fast --drag-fast-step -60 --preview-force-software
& $exe --file media\proxy\proxy-V4.mp4 --drag-fast --drag-fast-step 60

# 在线拖动预览回归（非门；真实网络 + 独立根）
& $exe --online-drag-preview-test --bvid BV1BMnv6jE5S --cid 42915922022 `
  --online-cache-root build\t8.6.1b-cache `
  --online-pictures build\t8.6.1b-pictures `
  --online-records-root build\t8.6.1b-records `
  --drag-seconds 1 --drag-samples 60 --m1-throttle-bps 120000

# ctest（手动时必须设置工作区 TEMP）
$env:TMP = 'E:\拉片软件开发\.deps\tmp'; $env:TEMP = $env:TMP
& 'third_party\cmake\3.31.6\cmake\data\bin\ctest.exe' `
  --test-dir build\release -C Release --output-on-failure

# clean Release + dist
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts\build-release.ps1 -Clean
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts\package.ps1 -SkipBuild
```

## 3. 基线复跑（DP-3 构建 `d586548`，重建前）

先确认现有 `build\release` 与 DP-3 交接一致，只记录、不改代码。

| 场景 | 结果 |
|---|---|
| `--drag-strategy-test` | `all_pass=1`；慢拖 +1/-1 lag p95 **0**；fast +60 active 63.4/空档 26.1ms；fast -60 63.7/22.7ms；reverse -20 40.3fps/55.2ms；远跳粗帧 16/15ms → 精确 30/30ms；release p95 16.1ms |
| `+60` | 79.1fps / 空档 35.7ms |
| `-60` | 59.6fps / 空档 35.6ms |
| `-20` | 47.4fps / 空档 56.6ms |
| `-1` | 77.3fps / 命中 83.6% / lag p95 2 / 峰值 244,684,800B |
| `--drag-far-test` | 粗帧 20.5/20.9ms；精确 30.1/29.1ms；release 15.2/18.2ms |
| `--drag-test` | P4 65.5、P6 21.6、P7 0.1/9.8、P8 12.3、P9 39.0（探针值；P4/P7 不作正式验收） |
| `--key-test` | **77/0** |
| `ctest` | **37/37**（62s；首次未设工作区 TEMP 时 11 项文件测试失败，设 `.deps\tmp` 后全过） |
| T8.6.1b 增长会话 | **35/0 pass**；UI gap 80ms；forward 52 presented/59.16 active，first 121ms，landing 216ms |
| T8.6.1b 缓存命中 | **25/0 pass**（缓存命中分支项数较少）；0 playurl/0 DASH |

结论：DP-3 基线可复现，数字与交接同量级；P4 65.5 是 15.6ms 计时器
环境的读数，不是 DP-4 改了 P4。

## 4. clean Release 构建

命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts\build-release.ps1 -Clean
```

结果：

| 项 | 结果 |
|---|---|
| 配置 | CMake 3.31.6 / VS 17 2022 x64 / Qt 6.8.3 / FFmpeg 7.1.1 |
| 构建 | **256.4s**（含 ctest/dist/windeployqt/自检） |
| ctest | **37/37**，64.40s |
| 环境自检 | `lapian 0.1.0`；Qt 编译/运行 6.8.3；FFmpeg 7.1.1；D3D11 RTX 4060 FL 11_1；网络协议禁用 |
| 导入检查 | **20 个 exe/dll**，无 QtNetwork/TLS/winhttp/wininet/urlmon；`Qt6Core.dll -> WS2_32` 为已知 Qt 运行时初始化 |
| dist | **25 文件 / 36,227,912B / 34.55MB**；P12 余量 65.45MB |

> `build-release.ps1` 的收尾 `Cannot open E:/` 是本机 E: 不受信任的已知
> 警告；关键步骤退出码均由脚本检查，最终 `exit 0`。

## 5. dist 验收

- 文件数 **25**、总字节 **36,227,912**（34.55MB）。
- `lapian.exe` 3,018,240B，
  SHA256 `4A8956C36177F67CC92106EED6DD5BE099724EA14D2648078A527C09177239D6`。
- 与重建前 dist（34.48MB/25）相比：**+71,168B**，差值全部来自
  `lapian.exe`（2,947,072 → 3,018,240），其余 DLL/插件字节未变。
- 禁止项：`Qt6Network.dll`、`D3dcompiler_47.dll`、`tls\`、
  `networkinformation\`、`generic\` 均不存在。
- `size-report.ps1`：主程序 2.91MB、Qt 运行库 20.93MB、FFmpeg 7.24MB、
  VC 运行库 1.76MB、Qt 插件 1.71MB。

## 6. 重建后二进制回归

### 6.1 策略矩阵

`--drag-strategy-test`：`all_pass=1`，7 个场景全 pass。

| 场景 | 模式 | presents | active fps | 空档 | lag p95 | 粗帧 | 松手 ms |
|---|---|---:|---:|---:|---:|---:|---:|
| slow_forward +1/33ms | exact | 61 | 28.0 | 49.3 | **0** | 0 | 13.4 |
| slow_backward -1/33ms | exact | 61 | 20.8 | 75.0 | **0** | 0 | 15.6 |
| fast_forward +60 | fast | 82 | 61.3 | 39.4 | 66 | 0 | 10.3 |
| fast_backward -60 | reverse | 74 | 57.6 | 44.2 | 60 | 0 | 12.5 |
| reverse_20 -20 | reverse | 85 | 42.2 | 58.4 | 20 | 0 | 26.1 |
| far_forward 300→4272 | far_jump | 3 | — | 15.2 | 0 | **1** | 13.5 |
| far_backward 4272→1524 | far_jump | 2 | — | 15.8 | 0 | **1** | 18.2 |

summary：`cases=7 release_samples=11 release_p95=26.1 release_pass=1
cache_peak_bytes=88473600 thumbnail_peak_bytes=432000 all_pass=1`。

### 6.2 快拖/反向/局部反向

| 场景 | 次数 | active fps | 空档 | lag p95 | 命中 | 峰值 |
|---|---:|---|---:|---:|---:|---:|
| `+60` | 1 | **80.7** | 31.2ms | 180 | 0% | 2,764,800B |
| `-60` | 3 | **50.4 / 55.5 / 57.5** | 37.0 / 29.0 / 38.7ms | 120 | 0% | 2,764,800B |
| `-20` | 1 | **49.8** | 38.3ms | 60 | 0% | 0B |
| `-1` | 1 | **82.3** | 59.5ms | **2** | **84.2%** | 244,684,800B |

> `-60` 本轮 50.4–57.5fps，低于 DP-3 记录的 54.4–64.4fps，但仍 ≥45
> 体验目标，且与 DP-2 同会话 48.7–58.1fps 同量级；本轮同时观测到 P4
> 回到 64.5（15.6ms 计时器环境），判断为环境/负载差异，不写成产品提升
> 或门通过。`-1` 命中 84.2% 与 DP-3 84.7–85.5% 同量级。

### 6.3 远跳

```text
T8.17 drag_far ... forward_coarse_ms=21.9 forward_exact_ms=29.8
backward_coarse_ms=20.5 backward_exact_ms=40.8
forward_release_ms=19.8 backward_release_ms=22.9
coarse_published=2 cache_peak_bytes=81561600 readbacks=0
```

粗帧均 ≤100ms 且先于精确；精确均 ≤150ms；`coarse_after_exact=0`；
松手仍走 `End + GoToFrame`。

### 6.4 `--drag-test`（仅探针，不写 P4/P7 正式通过）

| 指标 | 本轮 | 探针阈值 | 结果 |
|---|---:|---:|---|
| P4 | **64.5fps** | ≥30 | pass=1（环境读数，不作门） |
| P6 release p95 | **22.3ms** | ≤150 | pass=1 |
| P7 backward p95 | **0.04ms** | ≤80 | pass=1（不作门） |
| P7 forward p95 | **16.1ms** | ≤80 | pass=1（不作门） |
| P8 | **12.9fps** | ≥12 | pass=1 |
| P9 jump p95 | **39.8ms** | ≤200 | pass=1 |
| P7 seek 后 PgUp | 0.09/9.00ms | ≤80 | 记录（T7.9 专项） |

`p4_presents=129/2.00s`、`p8_frames=26/presents=26`；
不写“P4/P7 正式验收通过”。

### 6.5 键位与在线预览回归

- `--key-test`：**77/0**（退出码 0）。
- T8.6.1b 增长会话：**35/0 pass**；UI gap **93ms**/0 次 >200ms；
  forward 52 presented、59.02 active fps、first 120ms、release 6ms、
  landing **124ms**；backward 9 presented、32.61 active、first 724ms、
  landing 84ms；未缓存提示 0ms、取消 0ms、关窗 97ms；
  `exact_frame=1/exact_pts=1/stale=0/dropped=0`。
- T8.6.1b 缓存命中：**25/0 pass**；0 playurl、0 DASH。

## 7. 内存/表面池

| 项 | 本轮 | 预算 |
|---|---:|---:|
| 预览缓存峰值（`-1`） | **244,684,800B = 233.4MiB** | ≤256MiB |
| 策略矩阵峰值 | 88,473,600B | ≤256MiB |
| 远跳峰值 | 81,561,600B | ≤256MiB |
| 缩略图峰值 | 2,851,200B（2.72MiB；矩阵 432,000B） | ≤64MiB |
| `readbacks` | 0（全部探针） | 硬件帧不逐目标回拷 |
| 连续 `-1` 峰值 | 244,684,800B 不增长 | 有界 |

无黑帧、崩溃、设备丢失、表面池耗尽。

## 8. Medium 副本验证

- 位置：`%LOCALAPPDATA%\Programs\拉片软件`。
- 负载：`out\package\stage` 整目录复制（33 文件 / 36,343,599B，
  含 dist 25 文件 + licenses/README/PRIVACY）；`lapian.exe` SHA256
  `4A8956...39D6` 与 `dist` 一致 → 是 DP-4 新构建，不是旧版。
- 进程可写 Medium 路径：`--key-test --settings-path` 指向安装目录，
  写出 `settings.json`（78B）并 77/0；排除 Low integrity 进程误判。

| 验证 | 结果 |
|---|---|
| `--drag-test --drag-samples 20` | P4 **65.0**、P6 **17.7**、P7 0.1/14.9、P8 **12.4**、P9 **31.4**（探针值；P4/P7 不作正式验收） |
| `--drag-strategy-test` | `all_pass=1`；release p95 17.1ms；矩阵峰值 88,473,600B |
| `--key-test` | **77/0**；设置写入 Medium 自定义根 |
| `--loop-test` | 3 次循环后 loop-off、A=B 定帧 5s、步进解除、恢复播放；退出码 0 |
| 1080p 打开/拖动/松手 | 由上述真实窗口探针覆盖；`exact_frame/exact_pts` 均=1 |
| 默认凭据/设置/在线记录/缓存 | mtime 未变；cache 30 文件/377,228,845B 未变 |

本机默认路径未因本地模式写入缓存/记录/凭据；没有打印 SESSDATA/Cookie/
签名 URL。

**installer 说明**：`Lapian-0.1.0-setup.exe` 已由 Inno Setup 6.7.3 编译
成功（11.86MB），但从 E: 启动被主机 SmartScreen/策略拦截（`smartscreen`
进程挂起、无安装日志）。按任务允许改用整目录复制做 Medium 验证；
**不写“本轮安装器已重装/卸载验证通过”**，T7.6 的安装器结论不因本轮
重开。

## 9. 打包

| 产物 | 体积 | SHA256 |
|---|---:|---|
| `dist` | 25 文件 / 34.55MB | — |
| stage | 33 文件 / 34.66MB | — |
| `Lapian-0.1.0-portable.zip` | 14.43MB | `1928A0D242021B74FAB277914A30D1ED216776CDF3A1D5CCBD83EA34B73E0613` |
| `Lapian-0.1.0-setup.exe` | 11.86MB | `2BC6129D85A06E5ACA8977122902F7F22E9C7B3F6FD63AA42674FC52FC43F1BA` |

`package-report.txt`：源码 HEAD `9e584e9`；P12 每个分发形态均 ≤100MB；
无网络导入检查通过；stage 自检通过。

## 10. 强制软件残余（不写成通过）

| 场景 | 本轮强制软件 | DP-3 | 结论 |
|---|---:|---:|---|
| `-20` | **10.5fps / 空档 190.1ms / lag p95 360 帧** | 9.5fps / 211.0ms | 同量级，未达 25fps |
| `-60` | **33.4fps / 空档 49.9ms** | 31.4fps / 50.6ms | 同量级，未达 45fps |
| `-1` | 89.7fps / 命中 94.7% / lag p95 0 | 89.5fps / 94.2% | 缓存/预取受益 |

1080p 软件解码单帧约 5–8ms，15.6ms 后退 20–60 帧的精确路径物理追不上；
DP-4 没有用粗帧伪装精确帧，也没有为软件路径降低 1080p 门。

## 11. 4K（只记录，不设通过线）

`media\proxy\proxy-V4.mp4`（3840×2160）：

```text
T8.17 drag_fast ... step_frames=60 presents=198 active_fps=67.6
max_gap_ms=11.5 hw_requested=1 hw_active=0
preview_hw_fallback=source_too_large: 3840x2160
target_lag_frames_p95=312.0 cache_peak_bytes=44236800
```

4K 继续软件预览；不打开 4K 硬解、不改 1080p 门、不写 4K 通过。

## 12. 边界/未做

1. 未改代码、`PreviewDecoder`/`DragStrategy`/`OnlinePreviewDecoder`、
   缓存 schema、M1/M2、P4/P6/P7 阈值、OB、默认 AVC≤1080p。
2. 未重跑 M1/M2 正式门；T8.6.1b 只是非门回归。
3. 未做强制软件快反向优化、4K 硬解、HEVC 4K。
4. 未在本轮重装 installer；Medium 用任务允许的整目录复制。
5. 未打印/提交凭据、Cookie、签名 URL；未写默认缓存/记录/凭据。
6. 本轮 `-60` 与 P4 受 15.6ms 计时器/机器负载影响；数字只作同环境
   回归，不作跨会话产品提升。
