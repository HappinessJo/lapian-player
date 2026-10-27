# 计划：循环卡死/空格失效修复 + 工作区 A/B 按视频记忆（2026-10-27）

> **状态：规划冻结；执行排在 4096 迭代（P2）之后，串行单写手。**
> 定位依据见本文件 §1（代码行号），复现证据在修复阶段补。

---

## 0. 顺序（父 agent 排定，串行）

| 序 | 阶段 | Agent | 内容 | 依赖 |
|---|---|---|---|---|
| 0 | P2（进行中） | B | 设置页：范围/本机上限/默认预设 | — |
| 1 | **L1 循环修复** | L1 | 复现 + 修三条链 + 测试（见 §2） | P2 |
| 2 | P3 长跨度矩阵 | C | 10s/20s/40s + 1s/2s 不退化（4096） | L1 |
| 3 | **W1 工作区记忆** | W1 | 按视频存 A/B + 打开恢复 + spec 修订 | L1 |
| 4 | P4 独立验收 | D | 4096 + 循环修复 + 工作区记忆，一份 verdict | P3, W1 |
| 5 | P5 发布 | E | clean build/package/install/冒烟/docs/AGENTS v6.64 | P4 |

> 理由：L1 是用户可感知的严重缺陷且改动小，先修可让后续 P3/P4/P5 都在最终代码上验证；
> W1 改 spec 与存储面，放在 4096 矩阵之后，避免把两件事混进同一轮排错。

---

## 1. 定位结论（代码事实 + 用户实测）

**用户实测补充（2026-10-27）**：只在**在线视频**复现；A/B **不相等也会卡**（相等是原因之一，不是唯一）；
卡住时**帧号完全不动**、**播放按钮置灰点不动**、Space 无效。

- 「播放按钮置灰」只可能来自 `play_action_->setEnabled(false)`，全仓仅三处：`:224` 初始、
  `:2504` 打开媒体重置、`:9269` `OnPlaybackEnded`。媒体已打开且在播，故只能是 **`:9269`**。
- 即：**在线循环 B→A 的同步 seek 失败 3 次 → `Finish()` → `emit reachedEnd` → `OnPlaybackEnded`
  禁用播放 action + 音频暂停 + 状态栏「播放结束」→ 帧号冻结、Space/按钮全失效**。
  A≠B 与 A=B 都能走到这里（失败的是 seek，不是区间本身）；A=B 另有一条独立定帧路径。
- 结论：链 A（禁用播放态）+ 链 C（在线同步 seek 失败）是本次主因；链 B（A=B 定帧不可见）是独立次因。

### 1.1 循环卡死 / 空格异常（三条链）

- **链 A（确定）**：`PlaybackController::Finish()`（`PlaybackController.cpp:597-613`）只置 `playing_=false` 并 `emit reachedEnd()`，
  **不复位 `end_`**；`MainWindow::OnPlaybackEnded()`（`MainWindow.cpp:9267`）执行 `play_action_->setEnabled(false)`。
  全仓 `setEnabled(true)` 只有两处（`:3697` 在线首帧、`:5914` 打开媒体），`GoToFrame`/步进/点时间轴都不恢复
  → **播放结束或循环跳转失败 3 次后，Space 永久失效，只能重开文件**。
- **链 B（高概率）**：A=B 单帧定帧是 spec F4-A9 的设计，但工作区 B 手柄拖到 A（`MainWindow.cpp:7352` 的
  `std::max(frame, loop_.in())`）或平移压成 1 帧就会触发；UI v2 隐藏了 `loop_label_`（`:480`），
  用户看不出 A=B → 观感即「卡死 + 空格只切状态」。另外 `SetLoopRange` 的 freeze 分支
  （`PlaybackController.cpp:135-147`）只 `clock_.Pause()`+`timer_.stop()`，**没有 `audio_->Pause()`**，
  与 F4-A9「无音频输出」不符（需实测定性）。
- **链 C（在线/长 GOP）**：`JumpToLoopStart` → `SeekToFrameInternal` → `session_->SeekToFrame` 在 **UI 线程同步**执行
  （`PlaybackController.cpp:322-361`、`:286-299`），A 未缓存/长 GOP 时会阻塞或失败；失败 3 次 → `Finish()`（接链 A）。

### 1.2 工作区 A/B 不保存

- 不是实现 bug：`spec.md` §3.8 明确「循环开关与播放速度只在本次会话内保持」；`AppSettings` 只有音量/波形/缓存/预览。
- 现成存储面：本地 `<视频>.lapian.json`（`MarkStore.cpp:624`）；在线 `%APPDATA%\拉片软件\online\<bvid>-<cid>.lapian.json`（`:142`）。
- 要做 = 改 spec + 新增按视频持久化（默认全片，不改变「循环开关会话内」）。

---

## 2. L1 修复设计（不改 A=B 语义）

1. **失败态/结束态绝不禁用播放**：`Finish()` 后若会话仍打开，立即重新启用播放 action；
   `Play()` 显式处理结束态——循环开启 → 回 A 播；循环关闭 → 从第 0 帧重新播
   （用户按 Space 是显式动作，不违反“不自动回开头”）。
2. **循环跳转失败不再 `Finish()`**：达到失败上限时**关闭循环**（`SetLoopRange(false,…)`）+
   横幅/状态「循环跳转失败，已关闭循环」，保持可播/可暂停；诊断打
   `loop_jump_failed` / `loop_disabled_after_failures`。
3. **在线跳转前预检 + 有界**：A 不在可 seek 窗口（未缓存/超出 stable 前缀）时**不做同步 seek**，
   直接关循环 + 提示「A 尚未缓存，已关闭循环」；能 seek 时记录 `seek_ms`，
   超过 300ms 记 `loop_seek_slow`。目标是把「UI 线程长时间阻塞」降为「可见的降级」。
4. **定帧可见 + 音频停**：进入 A=B 定帧时状态栏/横幅显示「A=B 定帧（L 关闭 / PgDn 离开）」；
   freeze 分支补 `audio_->Pause()`；退出定帧按既有路径恢复。
5. **测试**（扩展 `--loop-test` 与 test-only hook）：
   - `loop-end-space-restarts`：播到文件尾后按 Space 能重新播放（action enabled）；
   - `loop-end-play-enabled`：`Finish()`/`OnPlaybackEnded` 后 `play_action_` 仍 enabled；
   - `loop-fail-disables-loop-not-playback`：`FailLoopSeeksForTest(3)` 后循环关闭、
     播放仍 enabled、**没有** `reachedEnd`、Space 可暂停/继续；
   - `loop-freeze-hint`：A=B 时提示含「定帧」，音频为暂停/静音；
   - `loop-online-cachehit-3rounds`：在线缓存命中会话设 A/B 播 3 轮，帧号持续前进、
     `play_action_` 始终 enabled、无「播放结束」；
   - `loop-freeze-pgdn-exit`：PgDn 离开定帧后 Space 正常（F4-A10 回归）。
6. **门**：`--loop-test` 全过（F4-A1/A2/A3/A9/A10）；ctest 38/38；key 77/0；
   `--scrub-space-test` 在线 64/0、本地 62/0（不得回归 3D 热修）；`--drag-test` P4/P6 不退化。

---

## 3. W1 工作区记忆设计

- 存储：**不新增源文件**（避免 CMake 重配 0xC0000409），在 `src/core/AppSettings.{h,cpp}` 加纯函数 + 小 store：
  - `WorkRange { int in; int out; }`，序列化 `{schema:1, in, out, media:{kind, id, size, mtime}}`；
  - 本地文件：`<视频路径>.lapian-workrange.json`；在线：`%APPDATA%\拉片软件\online\<bvid>-<cid>.lapian-workrange.json`；
  - 原子写（QSaveFile）、写失败静默回退（不阻塞播放）、无目录权限时不崩。
- 读取：打开媒体后、索引就绪时读；校验 `kind/id/size/mtime` 与帧数，`clamp(in/out, 0, last)`；
  非法/缺失/指纹不符 → 全片（两头），不报错。
- 写入：A/B 变化后 **debounce 500ms** 落盘；关闭窗口/切视频前 flush；拖手柄过程不逐帧写。
- 恢复：把区间应用到 `LoopRange` + 工作区条，但**循环开关仍为关**（spec §3.8 不变）；
  状态栏提示「已恢复上次工作区 A=x B=y」。
- spec：§3.8 增加一句「工作区 A/B 按视频记忆（循环开关仍只在本次会话内）」；F8 增加验收条目。
- 测试：纯序列化/校验（坏值、越界、指纹不符）；集成：临时视频路径保存→重开→恢复；在线 bvid/cid 恢复；
  只读目录回退；debounce 合并（模拟连续 5 次变更只写 1 次）。

---

## 4. 验收门（合并后）

- 4096：10s@2G、20s@4G ≥0.9；1s/2s 相对 C9 包不退化；内存 ≤ 配置+16MiB；4096 档不 OOM。
- 循环：F4-A1/A2/A3/A9/A10 全过；播到结尾 Space 可重播；A=B 有可见提示且无音频；
  跳转失败只关循环、不废播放；`--scrub-space-test` 不回归。
- 工作区：保存/重开/换视频/坏文件/只读目录/debounce 全过；默认全片。
- 回归：ctest 38/38、timeline 188/0、key 77/0、banner 74/0、dpi 4 档、drag、B 38/0×2、offline 45/0；
  quality 若 SESSDATA 仍失效按 blocked 记录。
- 发布：安装版冒烟含上述三项；settings 未变；回滚重装 C9 `AF32B4C3…`。

---

## 5. 风险 / 回滚

| 风险 | 缓解 |
|---|---|
| 改 `PlaybackController` 影响既有 F4/长按/拖动后 Space | 逐项跑 `--loop-test`/`--scrub-space-test`/`--drag-test`；只改结束态与失败分支 |
| 在线循环同步 seek 阻塞是结构性问题 | L1 只做「失败不废播放 + 可见提示」，不在本轮改异步 seek；如实测阻塞明显，只记录为残差 |
| 工作区写盘影响拖动性能 | debounce 500ms + 关窗 flush；拖动中不写 |
| 本地 sidecar 在只读目录 | 写失败静默 + 回退 AppData；不阻塞播放 |
| spec 变更 | 本次只把「A/B 记忆」写进 §3.8；不动其它验收值 |

回滚：L1/W1 各自独立 commit；发布回滚重装 C9 setup `AF32B4C3…`（exe `588B8D5E…`）。
