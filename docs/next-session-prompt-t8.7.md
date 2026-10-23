# 下一会话提示词：T8.7 缓存命中 + 在线记录

你是拉片软件 v2 的下一个实现 agent。**本会话只做 T8.7，不做 T8.8，不做 T8.6.1，不做 T8.3.1，不做 DASH/清晰度选择。** 先读完本文再动代码。

## 0. 开始前必读

按顺序读：

1. [AGENTS.md](AGENTS.md)（项目上下文与协作规则）
2. [tasks-v2.md](tasks-v2.md) **v0.13**：T8.7 行、M1/M2 门、新增非门任务
3. [docs/spec-v1.0-delta.md](docs/spec-v1.0-delta.md)：O5 缓存、O10 记录、A2 记录键
4. [docs/handoff-2026-10-23-t8.6.md](docs/handoff-2026-10-23-t8.6.md)：T8.6 接口/线程/缺口
5. [docs/perf/v2-online-session.md](docs/perf/v2-online-session.md)：T8.6 证据，尤其 §9.1 环境校正

不要凭记忆改接口；以代码和这些文档为准。

## 1. 当前状态

- T8.6 已完成并提交：`14e8feb`。
- 后续热修：`602e5aa` 凭据目录、`a031461` 环境文档、`f35c5cf` 本地拖第二个文件。
- 工作树应保持 clean；开始前先 `git status`。
- **T8.7 尚未开始**。缓存命中/在线记录仍是缺口：
  - 完整缓存再次打开仍会走下载/报错路径；
  - 在线标记是会话级，关窗不保留；
  - 截图目录/命名/`index.jsonl` 未正式化；
  - 清缓存保留在线记录未接产品。
- **M1 3/3 720P 仍 blocked**：OB-2 返回 360P（qn=16）。T8.7 不判 M1，不伪造 3/3。
- T8.6 的 3 条 E2E 使用 `build/t8.6-online-cache`；默认 AppData 路径的验证要用下面的 Medium 副本。

## 2. 运行环境（非常重要）

当前 DSH 工作区 `E:\拉片软件开发` 带 Low Mandatory Level。直接运行 `dist\lapian.exe` 会得到 Low integrity 进程，**不能写 `%APPDATA%`/`%LOCALAPPDATA%`**，会误报“无法创建缓存/凭据目录”。

默认路径/凭据/完整 E2E 请使用 Medium 副本：

```text
C:\Users\HappinessJo\AppData\Local\Programs\lapian-t86\lapian.exe
```

规则：

- 不要双击 `E:\拉片软件开发\dist\lapian.exe` 做默认路径验收。
- 不要用“以管理员身份运行”绕过；它改变安全上下文，不是产品路径。
- 构建/ctest 可以在 `build\release` 跑；构建后把新的 `lapian.exe` 复制到 Medium 副本再测默认 AppData。
- Medium 副本已用真实 SESSDATA 验证保存/重启解密；不要打印、提交或把 `.secrets` 内容写进日志。
- Defender 可能对未签名 exe 提示一次；点“仍要运行”，不要关闭防护。

验证默认缓存根是否可写：

```text
C:\Users\HappinessJo\AppData\Local\拉片软件\cache
```

验证默认凭据：

```text
C:\Users\HappinessJo\AppData\Roaming\拉片软件\credentials.dat
```

## 3. T8.7 任务范围

按 tasks-v2 T8.7 行：

> 缓存命中 + 在线记录：断网按缓存打开（0 请求）；`lapian-online-1` 标记/备注；截图目录/命名/`index.jsonl`；清缓存保留记录；换档映射。

验收：

1. 断网打开同 BV：**0 TCP/UDP**，首帧 ≤2.5s（DEC-2 已拍板）。
2. 标记/备注/截图关窗重开恢复；键为 bvid+cid（delta A2）。
3. 清缓存后记录仍在。
4. 重建缓存后记录可回访。
5. 损坏记录不阻塞播放。
6. 已缓存区播放/seek/步进不回退；本地模式零网络、P1/P2/P7 不退化。
7. 完整缓存命中不得再创建/覆盖 `.part`，不得发起 playurl/CDN 请求。

## 4. 代码入口提示

先读再改，不要假设：

- `src/online/CacheStore.h/.cpp`
  - `OpenComplete()` 已存在（T8.7 注释），先接它；
  - `MarkComplete()` 完成改名；
  - `ClearAll()` 保留 records sentinel 的语义已有单测；
  - `CleanupOrphans()` 目前产品未调用，**不要在本任务顺手接**；那是 T8.3.1。
- `src/app/OnlineSession.h/.cpp`
  - 当前完整缓存命中路径会报错/仍走下载；接 `OpenComplete` 后应进入“缓存命中”状态，不启动 DownloadJob。
- `src/app/MainWindow.cpp`
  - 在线会话/标记/截图入口；现有标记是会话级；
  - 截图临时落在图片目录的 `snapshots/`，T8.7 正式化目录/命名/`index.jsonl`。
- `src/core/MarkStore.h/.cpp`
  - 本地标记的原子写/合并/损坏备份可参考；在线记录不要直接复用本地 sidecar 路径语义。
- 现有测试/runner：
  - `tests/online_cache_test.cpp`、`tests/online_m1_test.cpp`
  - `--online-m1-test`、`--online-buffer-test`、`--online-cache-test`
  - 可新增 `--online-offline-test` / record runner；不要改已有公共接口语义。

**记录位置**：按 delta O10/A2，在线记录存 `%APPDATA%`，按 bvid+cid；不要存缓存根内，否则清缓存会丢。具体文件名/目录按现有 AppData 约定和 delta，不要自创第二套。

## 5. 建议顺序

1. **复现**：用现有 `--online-m1-test` 对同 BV 跑完一次，再跑一次，确认完整缓存命中当前失败点；记录日志。
2. **缓存命中**：`OnlineSession` 先查 `OpenComplete`；命中则直接 `OpenGrowing`/`OpenWithFormat`，跳过 playurl/DownloadJob；UI 显示“缓存命中”。
3. **在线记录**：新增记录存储（bvid+cid → 标记/备注/截图清单）；关窗 flush；启动/打开同 BV 恢复。
4. **截图**：正式目录/命名/`index.jsonl`；与缓存清空解耦。
5. **测试**：断网 0 请求、首帧、标记/备注/截图恢复、清缓存保留、重建回访、损坏记录不阻塞。
6. **证据/交接**：`docs/perf/v2-online-cache-hit.md`、`docs/handoff-2026-10-23-t8.7.md`；更新 tasks-v2 下一任务（只改状态/版本，不改门）。
7. **提交**：一个可审查提交序列；结束时 `git status` clean。

## 6. 硬约束

- 不改 M1/M2 门、验收数字、T8.7 验收口径。
- 不做 T8.8 M1 门；不写“M1 通过”。
- 不做 T8.6.1a/b（在线长按/拖动预览）；不塞进 T8.7。
- 不做 T8.3.1（缓存管理 UI/清空按钮/孤儿清理）；本任务只做命中 + 记录，清缓存用现有 `ClearAll` 测试语义。
- 不做 DASH/1080P/4K/清晰度选择；T8.9/T8.12/T8.15 另做。
- 不改扫码登录、HTTP 栈、playurl 语义。
- 不把 4K/1080P60 写成 MVP；不伪造 OB-2 720P。
- Cookie/签名 URL 不入库、不回显、不写日志；`.secrets` 不入库。
- 默认路径测试必须从 Medium 副本跑；Low 工作区结果只能标注为“工作区环境”，不能当默认 AppData 验收。
- 遇到 spec/ADR 冲突：停止并写清楚冲突点，不要静默改。

## 7. 验收命令建议

```powershell
# 构建 + 单测
$env:TMP = (Join-Path (Get-Location) '.deps\tmp'); $env:TEMP=$env:TMP
$qt = (Resolve-Path 'third_party\Qt\6.8.3\msvc2022_64').Path
$env:PATH = "$qt\bin;$env:PATH"
$cmake = (Resolve-Path 'third_party\cmake\3.31.6\cmake\data\bin\cmake.exe').Path
& $cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure
```

```powershell
# 默认路径用 Medium 副本（构建后复制新 exe）
$dest = "$env:LOCALAPPDATA\Programs\lapian-t86"
Copy-Item build\release\bin\lapian.exe "$dest\lapian.exe" -Force
& "$dest\lapian.exe" --online-m1-test --bvid BV1BMnv6jE5S --cid 42915922022 --tag t87-cache
```

断网测试：先正常缓存完成，再禁用网络/断网，重开同 BV；用 ProcMon/ETW 或现有网络计数器确认 **0 TCP/UDP**，记录首帧时间。不要用“看起来没请求”代替证据。

## 8. 停止条件

遇到以下情况先停，不要猜：

- `OpenComplete` 需要改 CacheStore 公共语义/目录结构；
- 记录位置与 delta/现有 AppData 约定冲突；
- 完整缓存命中仍触发 playurl，且无法在不改 T8.5/T8.6 语义下解决；
- 默认 AppData 在 Medium 副本仍不可写（先查完整性/ACL，不要用管理员绕过）；
- 断网测试无法复现 0 请求；
- 需要动 DASH/清晰度/QR/缓存管理 UI 才能继续。

停下时写清楚：现象、日志、已尝试、阻塞点、需要用户拍板的选项。

## 9. 交付

- 代码 + 测试 + `docs/perf/v2-online-cache-hit.md` + `docs/handoff-2026-10-23-t8.7.md`。
- tasks-v2 状态更新（T8.7 完成/下一任务 T8.8），**不改 M1/M2 门与数字**。
- 一个清晰提交序列；结束时 clean。
- 交接必须写：缓存命中路径、记录格式/位置、测试数字、未验证项、T8.8 需要什么。
