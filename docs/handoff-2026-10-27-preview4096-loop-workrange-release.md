# 交接：4096 预览缓存 + 循环修复 + 工作区记忆 — P5 发布收口（2026-10-27）

- **状态**：P1/P2（4096 上限 + 设置页）、L1（循环卡死/空格失效）、W1（工作区 A/B 记忆）已完成；
  P4 独立验收 `verdict=pass with gaps`；**P5 clean build / 打包 / 每用户静默安装 / 安装版冒烟 / 文档完成**。
  分发包已生成、每用户静默安装可用、安装版核心冒烟通过；**不写“发布就绪/T7.7/P14 通过”**。
- **代码 HEAD**：`d819ebf`（docs-only；产品/测试树 = `e1afb33`）。本轮只提交
  `docs/release-2026-10-27-preview4096-loop-workrange.md`、本文件、`AGENTS.md`（v6.64）。
- **安装版**：`%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`，**3,829,248 B / `823707DD…`**
  （= dist/build/stage 同 hash）；旧 C9 安装版 `588B8D5E…` 已被覆盖，回滚见发布文档 §9。
- **发布记录**：[release-2026-10-27-preview4096-loop-workrange.md](release-2026-10-27-preview4096-loop-workrange.md)。
- **上游**：[计划（循环+工作区）](plan-2026-10-27-loop-workrange-fix.md)、
  [计划（4096）](plan-2026-10-27-preview-cache-cap-4096.md)、[P4 独立验收](handoff-2026-10-27-final-acceptance.md)、
  [W1 交接](handoff-2026-10-27-workrange-persist.md)、
  [上一包 C9 发布](release-2026-10-27-preview-config.md)。

---

## 1. 一句话结论

三块内容一起打包发布：**在线预览缓存绝对上限 2048 → 4096MiB + 设置页可设范围/本机 25% 上限/默认预设**、
**在线循环 seek 失败不再 `Finish()` 禁用播放（失败只关循环；结束态 Space 重播；A=B 定帧+音频暂停）**、
**工作区 A/B 按视频记忆（本地 sidecar / 在线 `<bvid>-<cid>` 记录；恢复 A/B 但循环仍关）**。
安装版复跑：**10s@2GiB 3/3 轮 × 36/0（比 1.015–1.019）、20s@4GiB 3/3 轮 × 36/0（比 1.015–1.021）**、
**1s@512 0.982–1.018、2s@512 1.009–1.026**、**循环本地 65/0 / 在线 32/0**、**scrub 62/0 / 64/0**、
**工作区 68/0 + 新进程恢复**、**设置页 149/0 ×2**、**B 38/0 ×2**、**offline 45/0**；
**quality blocked**（SESSDATA 失效）。ctest **38/38**、timeline **188/0**、mark store **281/0**、
banner **74/0**、key **77/0**、dpi 4 档 **37/0**、drag P4 **94.2–94.4** / P6 **21.3–27.9ms**。
**P4 的 12 条 gap 原样见发布文档 §6。**

---

## 2. 关键数字（安装版，本轮实测）

| 项 | 值 |
|---|---|
| clean 构建 | `build-release.ps1 -Clean -Package`，exit 0，**289.7s 墙钟**（脚本 272.9s；ctest 脚本内 70.28s） |
| ctest | **38/38**（手动 69.46s / wall 69.6s） |
| timeline / mark store | **188 OK / 0 fail**（1.2s）；**281 OK / 0 fail**（0.2s） |
| exe | **3,829,248 B**，`823707DD230D4B5B18046A10726389092E4D2776293BCD26D606A10B09DC41E9` |
| dist / stage | **26 文件 / 37,107,016 B**；**35 文件 / 37,247,982 B** |
| setup / zip | **12,671,978 B / `C36E8532…`**；**15,487,271 B / `46752202…`** |
| 静默安装 | **exit 0 / 2.34s**；`User privileges: None`、`Administrative install mode: No`、HKCU；安装版 exe == dist；settings `5F5F47E7…`（190B）未变；备份 `99084199…`/`5863DD0A…` 保留；`lapian-c8` `99BC1201…`、`lapian-preview` `80EC7FD4…` 未动 |
| 10s@2048 门 | 正 583–584 / 58.67–58.79、反 593–594 / 59.68–59.77，比 **1.015–1.019**，peak **1839.1MiB**（620/690），close 92–97ms，36/0 ×3 |
| 20s@4096 门 | 正 1168–1171 / 58.59–58.74、反 1187–1196 / 59.54–59.99，比 **1.015–1.021**，peak **3615.9MiB**（1219/1380），close **184/190/233ms**，36/0 ×3 |
| 1s / 2s @512 | 1s **0.982/1.018/1.000**（28/0 ×3，peak 240.27MiB）；2s **1.026/1.009/1.026**（28/0 ×3，peak 403.42MiB） |
| 循环 | 本地 **65/0**（结束态 Space 重播、失败只关循环、A=B 定帧+音频停、在线预检拦截）；在线 **32/0**（frame 132/127/122、jumps 1→2→3、play_enabled=1、end_count=0） |
| scrub | 本地 **62/0**、在线 **64/0**（各 24.2s） |
| 工作区 | 本地 **68/0**（sidecar 50/90 与 30/60 不串）；在线 **32/0**（records 下 `BV1BMnv6jE5S-42915922022.lapian-workrange.json`、重开 `a=100 b=160 loop=0`）；新进程截图「已恢复上次工作区 A=50 B=90（循环关）」 |
| 设置页 UI | 100% / 200% 各 **149/0、pass、dialog-fits OK** |
| 回归 | B 720P **38/0**、B 1080P **38/0**、offline **45/0**；quality **blocked**（`nav -101`、`vip_status=-1`、16 项/1 fail、exit 1） |
| 4096 设置端到端 | 隔离 settings 写 1080p/4096 → 生效 **4011MiB**（25% 护栏；settings 保留 4096） |
| 基础 | banner **74/0**、key **77/0**、dpi **37/0 ×4**（problems 空）、drag P4 **94.4**/P6 **21.3**、loop **65/0** |

---

## 3. 复现（安装版，隔离根）

所有运行显式隔离，不写用户 settings/缓存/记录/截图；夹具先复制到 `build\P5-smoke\cache\*`。
GUI 探针必须 `Start-Process -Wait -RedirectStandardOutput/-RedirectStandardError`。

```powershell
cd 'E:\拉片软件开发'
$env:TMP='E:\拉片软件开发\.deps\tmp'; $env:TEMP=$env:TMP
$exe = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"
# 脚本（ASCII-only，参数从命令行传中文路径）：
#   build\P5-smoke\run_drag_cell.ps1 -Exe $exe -Tag g10s-r1 -Seconds 10 -CacheMb 2048 -Gate
#   build\P5-smoke\run_drag_cell.ps1 -Exe $exe -Tag b1s-r1  -Seconds 1  -CacheMb 512  -BackwardFull
#   build\P5-smoke\run_local_cell.ps1 -Exe $exe -Tag loop-local  -Mode loop-local  -MediaPath 'media\测试.mp4'
#   build\P5-smoke\run_local_cell.ps1 -Exe $exe -Tag loop-online -Mode loop-online -Bvid BV1BMnv6jE5S -Cid 42915922022
#   build\P5-smoke\run_local_cell.ps1 -Exe $exe -Tag scrub-local -Mode scrub-local -MediaPath 'media\测试.mp4'
#   build\P5-smoke\run_local_cell.ps1 -Exe $exe -Tag workrange-cross -Mode loop-local `
#       -MediaPath 'build\P5-smoke\workrange\video.mp4' -FileB 'build\P5-smoke\workrange\video-b.mp4'
```

单格命令、设置页 UI、B/offline/quality 的完整骨架见 [release §10](release-2026-10-27-preview4096-loop-workrange.md)。

关键坑：
- **`--online-cache-manage-test --ui` 的结果在 stdout**（不是 stderr）；其它 GUI 探针在 stderr。
- `--settings-path` 是**目录**（内含 `settings.json`）；`--preview-cache-mb` 是 test-only 环境变量覆盖，
  不带它时才走 settings + 25% 护栏。
- 缓存命中拖动必须 `data.mp4` 完整；1080P 切 112 需要 `112-7\data.part` 才能续传真下载。
- `--loop-test` 本地 65 项含 W1 5 相位（会先清同视频记录）；重复跑首段可能短暂显示上次 A/B。
- ctest 必须设 `TEMP/TMP`；默认 `%TEMP%` 下本机 11 项假失败（`QTemporaryDir` 环境问题）。
- 安装版 exe 是 GUI 子系统，Qt 运行库在同目录，不需要额外 PATH；构建树的
  `lapian_timeline_test.exe`/`lapian_mark_store_test.exe` 在 `build\release\Release\`。

---

## 4. 未做 / 未覆盖（下一次会话入口）

1. **quality 42/0 未复现（blocked）**：需要有效 SESSDATA；补 1080P60/4K 档位断言。
2. **无 60fps 源**：所有“60”是拖动目标率；需要 60fps 素材重跑 1s/2s 矩阵。
3. **P4 的 40s@4096 记录未复跑**：覆盖 57.5%、peak 4093.5MiB（余量 18.5）、close 355/442ms
   沿用 P4 证据；本包只跑 10s/20s 门。
4. **20s@4096 关闭 233ms >200ms**（r1）为本轮新观察；关闭释放未全部 <200ms。
5. **4096 设置端到端受 25% 护栏**：本次 4011MiB（P4 3763MiB）；settings 保留 4096。
6. **进程 RSS 未系统采集**；peak 是预览解码器缓存字节（探针口径）。
7. **growing 长跨度未跑**、**C9 A/B 口径陷阱**（需显式 `--drag-samples`）未重跑。
8. **legacy 1024/64 parity 未重跑**（C9 61/0）；本包未改该路径。
9. **未覆盖**：真实 4K/60fps、登录态 1080P60/4K、多实例并发 4GiB、720P 长跨度、
   真实 >2s GOP 保证档外推、真实窗口/物理鼠标/多 DPI 拖动体感、无符号化崩溃 dump、
   代码签名/干净机复验、Explorer 真双击。
10. **用户体感未收集**：本轮没有新的用户实机反馈。

---

## 5. 与 C9 / 计划的衔接

- **C9 包**（`7c7f44c`，exe `588B8D5E…`）的 auto/1080P/720P + 五档缓存（默认 auto/512）仍是本包默认值；
  本包只把**硬上限 2048 → 4096**、设置页补范围/本机上限/默认预设，并把 D5 的 1s/2s 基线作为不退化对照。
- **计划（4096）**：P1 配置 `22faadd`、P2 设置页 `e33b0d6`、长跨度矩阵 `3c06afc`、同目标率门 `14407ec`；
  P4 验收 `d819ebf` 判 `pass with gaps`；P5 只做构建/打包/安装/冒烟/文档，未改门/阈值。
- **计划（循环+工作区）**：L1 `9e7a6f0`、W1 `d14c91c`+`e1afb33`；spec 仅加 §3.8 一句 + F8-A4。
- 下一步优先级：① 有效 SESSDATA 重跑 quality；② 找 60fps 源重跑 1s/2s/长跨度；
  ③ 40s@4096 与 growing 长跨度；④ 关闭释放 233ms 复现/定位；⑤ 真实体感/真实鼠标。
- **回滚**：重装 C9 setup `AF32B4C3…`（12,636,766 B，exe `588B8D5E…` 3,687,424 B）；
  代码 `git revert e1afb33 d14c91c 9e7a6f0 14407ec 3c06afc e33b0d6 22faadd`。详见发布文档 §9。
