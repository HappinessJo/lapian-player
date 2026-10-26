# UI v2 3D 热修（实现）交接 — 2026-10-26

- **状态**：用户实机反馈的 4 项问题已修复；P0–P3 各一个独立 commit，**未重开 3D 范围**、
  未改任何门/阈值/性能数字。自动门禁全绿（两个既有已知失败保持原样，见 §7）。
  **用户实机体感待确认：本文件只记"体感"，不写"验收通过"。**
- **上游**：[3D 热修交接（诊断/方案）](handoff-2026-10-26-ui-v2-3d-hotfix.md)、
  [3D 收口交接](handoff-2026-10-26-ui-v2-3d.md)、[3C](handoff-2026-10-26-ui-v2-3c.md)、
  [AGENTS](../AGENTS.md) §1/§9/§11/§12、[UI v2 规划](ui-v2-plan.md) §2.3/§3.2/§7.6/§7.7、
  [视觉基准](ui-v2-visual-reference.md) §1/§3/§5/§6。
- **硬边界（一行未改）**：`GfxWidget` 呈现、`PreviewDecoder`/`DragStrategy`、`OnlineSession`
  状态机、`CacheStore` 语义、`media::StatusNotice` 语义、spec §3.7 键位、settings schema、
  任何门/阈值/性能数字、3B Transport 四档降级、3C 检查器布局。P1 只改"何时切换 store"。

---

## 1. 一句话结论

四个问题分别在**播放意图**（P0）、**缓存 store 生命周期**（P1）、**顶层对话框尺寸**（P2）、
**QSS/控件形态**（P3）层修复：在线 seek 在途时 Space 只记"待播"、落地后补 `Play`；在线
保存缓存目录改成"写 settings + pending，会话结束后换 store"；扫码面板显示后按内容长高并
夹到屏幕、放不下时滚动；清晰度入口变成带 chevron 的 chip 按钮。没有新增 runner 文件
（CMakeLists 未改），没有 per-widget `setStyleSheet`。

---

## 2. 提交与文件（每项可单独 revert）

| 顺序 | commit | 说明 | 文件 |
|---|---|---|---|
| P0 | `534d909` | `fix(ui-v2): resume playback after online seek lands` | `MainWindow.{h,cpp}`、`main.cpp`、`OnlineDragPreviewTestRunner.{h,cpp}` |
| P1 | `b78bbe1` | `feat(ui-v2): allow cache dir change during online (deferred)` | `MainWindow.{h,cpp}`、`CacheSettingsDialog.cpp`、`OnlineCacheManageTestRunner.cpp`、`OnlineDragPreviewTestRunner.{h,cpp}`、`main.cpp` |
| P2 | `6533da0` | `fix(ui-v2): resize online dialog when QR panel shows` | `OnlineResolveDialog.{h,cpp}`、`OnlineQrTestRunner.cpp`、`main.cpp` |
| P3 | `efc6831` | `style(ui-v2): make quality entry a chip with chevron` | `ui/Theme.cpp`、`ui/TopBar.cpp`、`MainWindow.{h,cpp}`、`main.cpp` |

文档：本文件 + [AGENTS §12](../AGENTS.md) 登记（docs commit 见 `git log` 顶部）。

---

## 3. 四项修复

### 3.1 P0 拖动后立刻 Space 卡住（在线增长会话）

**根因（已复现）**：松手 → `OnScrubFinished` → `GoToFrameInternal` 在线分支 `Pause()` 后
`RequestOnlineSeek()` 异步返回；Space 在 seek 落地前 `Play()`；落地回调无条件
`playback_->SetPosition()`，其第一行 `Pause()` 把刚启动的播放停掉。

**改法（方案 B，未用备选 A）**：

- `MainWindow::online_pending_play_`：`TogglePlayback()` 在
  `online_mode_ && !online_cache_hit_ && online_seek_running_` 时只翻转"待播"意图，状态栏
  `定位完成后开始播放` / `已取消播放`，不直接 `Play()`。
- seek 落地回调在 `SetPosition()` 之后：若 pending，清标记并 `Play()`。
- 清标记：新 seek（`RequestOnlineSeek`）、`OnScrubStarted`、`BeginOnlineRequest`（含切档）、
  `ShutdownOnlineSession`、`CancelOnlineLoading`、seek 打开/执行失败、线程启动异常。
- 再按一次 Space = 取消待播（不会在落地后突然起播）。

**探针**：`--scrub-space-test`（在线增长：拖动 15→75，松手后**同步立刻**按 Space，20 轮；
每轮断言 `IsPlaying=1`、`PresentedFrameCount` 1s 内增长、`online_seek_running=0`）。
本地变体：`--scrub-space-test --file <media>`（无 `--bvid`）。

**证据（fail-first → pass）**：

| 状态 | 在线 | 本地 |
|---|---|---|
| 修复关闭（临时 `false &&` 分支，同一探针） | `checks=64 failures=20`，20/20 轮 `IsPlaying=0`（每轮 presented 只 +1 后停住） | `checks=62 failures=0`（本地无此 bug） |
| 修复打开 | `checks=64 failures=0`，20/20 轮 `pending=1`、presented 增长（例 4→29、529→555） | `checks=62 failures=0` |

结论：问题只在在线异步 seek 路径；本地 `EndScrubPreview`/`preview_timer_` 顺序无需改。

### 3.2 P1 在线改缓存目录（两阶段，用户已确认）

**根因**：`ApplyCacheSettings` 在线直接拒绝；`CacheSettingsDialog` 在 `session_active_` 禁用
目录/配额/保存并在 `ApplyInternal` 再拒绝；`OnlineSession` 持有旧 `CacheStore*`，直接换 store
会悬空/丢 `.part`（原代码会 `ShutdownOnlineSession`）。

**改法**：

- 在线时：临时 `CacheStore` 校验新 root/配额（`SetRoot`/`SetQuotaBytes`），写 `settings.json`，
  置 `cache_settings_pending_=true`，**不替换** `online_cache_`、**不 Shutdown**，返回成功。
- 新增 `ApplyPendingCacheStoreIfIdle()`：仅 `online_==nullptr && !online_mode_ &&
  !cache_settings_dialog_open_` 时按 `loaded_settings_` 新建 store、`SetRoot`/`SetQuota`、
  替换 `online_cache_`、清 pending；新目录不可用时保留 pending 并提示（下次 idle 重试）。
- 调用点：`ShowCacheSettingsDialog()` 开头、`EnsureOnlineSession()` 开头、
  `RunCacheMaintenance()` 开头；`ShowCacheSettingsDialog()` 的 `exec()` 前后维护
  `cache_settings_dialog_open_`，**对话框打开期间绝不换 store**。离线保存仍立即换 store。
- `CacheSettingsDialog`：`session_active_` 不再禁用目录/配额/保存；提示文案改为
  "当前在线会话继续使用旧目录；新目录/配额在本次会话结束后生效（下次在线打开使用）"；
  保存成功文案在在线时显示 pending。
- 不迁移旧缓存；配额延迟，避免 `EvictToQuota` 清掉正在写的 `.part`。

**测试**：

- `--online-cache-manage-test --ui` 扩到 **113/0**：`ui-active-keeps-save/quota/clear`；
  新增 `deferred-*` 用例——在线保存后当前 root 不变、settings 已写、pending=1、旧目录与
  `.part` 都在；模拟会话结束（`SetOnlineModeForTest(false)`）后新 root 生效、
  pending 清空、旧 `.part` 仍在、新 root ready。
- 新增真实会话探针 `--cache-switch-online-test`（Medium）：在线播放中保存新目录 →
  `cache_switch_result checks=17 failures=0`：播放不中断（presented 22→52）、
  `online_ready_` 保持、`.part` 路径不变、root 不变、pending；结束会话后新 root 生效、
  旧 `.part` 保留；下一次在线会话的 `.part` 落在新 root。

**风险**：旧目录不自动迁移；pending 只在上述 idle 调用点应用（应用重启后直接按 settings 用
新 root）；设置对话框持有旧 store 的窗口期已用 `cache_settings_dialog_open_` 封住。

### 3.3 P2 扫码二维码显示不全

**根因**：`setMinimumSize(560,560)` + `qr_panel_` 初始隐藏；`OnStartQrLogin` 只 `show()`，
顶层窗口不会因隐藏子控件显示自动长高，二维码下半部分被裁。

**改法**：

- 主体放进 `QScrollArea`（`onlineScroll`，`widgetResizable=true`、`NoFrame`、横向滚动关），
  内容 widget `onlineScrollContent`；最小尺寸 `560×420`。
- `AdjustToContent()`：按内容 `sizeHint` 定高（`420..可用高-48`），宽度 `560..720` 且夹到
  可用宽；`updateGeometry()+layout activate`；位置夹回 `availableGeometry`；QR 面板可见时
  `ensureWidgetVisible(qr_panel_)`，并 `QTimer::singleShot(0)` 再滚一次（布局竞态）。
- `OnStartQrLogin()` / `OnQrRefresh()` show 后调用；`OnQrCancel()` hide 后收缩；
  构造函数末尾也调用（初始窗口不超屏）。
- 100%/125% 按内容长高（无滚动条）；150%/200% 夹到可用高并滚动，二维码 240×240 与
  "刷新/取消"完整可见。

**测试**：`--online-qr-test --ui` 扩到 **26/0**（原 21 + 5）：QR 面板可见、label 240×240、
图像完整可见、刷新/取消完整可见、对话框在屏幕内、截图保存；`PrintKV ui_qr_layout` 输出
`dialog/frame/avail/qr/label/scroll`。100/125/150/200% 各 26/0：

| DPI | dialog | frame | QR/controls | scrollbar |
|---|---|---|---|---|
| 100% | 640×878 | 640×909 / avail 1920×1032 | 1/1 | 0 |
| 125% | 640×778 | 640×803 / avail 1536×826 | 1/1 | 0 |
| 150% | 640×640 | 640×661 / avail 1280×688 | 1/1 | 0 |
| 200% | 640×468 | 640×484 / avail 960×516 | 1/1 | 166 |

截图：`build/hotfix-qr-100.png`、`-125`、`-150`、`-200`（深色主题；200% 可看到右侧滚动条）。

### 3.4 P3 清晰度入口不像按钮

**根因**：`quality_button_` 是 flat `QPushButton`，只显示"清晰度：1080P"；`TopBar` 收养时
只设字体/NoFocus；`Theme` 无 `#onlineQualityButton` 规则。

**改法**：

- `Theme::GlobalStyleSheet()` 增加 `#onlineQualityButton`：`@chipbg` 底 + 1px `@border` +
  10px 圆角 + `padding 0 7px`；hover `@accent` 边框/`@text`；pressed `@border` 底；
  disabled 压暗。**无 per-widget QSS**。
- `TopBar::AdoptQualityButton()`：`setFlat(false)`、`PointingHandCursor`、12px
  `GlyphId::kChevronDown` 图标、`setIconSize(12,12)`、固定高 20（`ApplyTokens` 按
  `tokens.chip` 再校正）。文本改为 `清晰度 1080P`（4 处统一）。
- objectName/getter/NoFocus 不变。

**验证**：`--ui-shots-test` 新增质量 chip 相位（`CaptureQualityChipShotsForTest`）：
1280 宽与 820 窄窗各一张；`TopBarOverflowForTest()` 递归检查顶栏可见子控件是否越界，
任一越界 `--ui-shots-test` 退出码 1。100/125/150/200% 全部 `wide_overflow=count=0`、
`narrow_overflow=count=0`。截图：
`build/ui-v2-shots/hotfix-quality-{100,125,150,200}.png` 与
`hotfix-quality-820-{100,125,150,200}.png`；820 截图里模式 chip
`在线 · 1080P · 29.412fps · 降帧` + 清晰度 chip + 打开/在线/帮助/设置均不溢出。

---

## 4. 新增/扩展测试入口（没有新增文件 → CMakeLists 未改）

| 入口 | 说明 |
|---|---|
| `--scrub-space-test` | P0；在线增长 20 轮（需 `--bvid/--cid`，Medium）；只给 `--file` 走本地对照。 |
| `--cache-switch-online-test` | P1；真实在线会话中保存缓存目录（需 `--bvid/--cid`，Medium）。 |
| `--online-cache-manage-test --ui` | P1；113 检查（含 `deferred-*`）；仍可无 `--ui` 跑纯存储用例。 |
| `--online-qr-test --ui` | P2；26 检查（QR 240×240/按钮/夹屏/截图）；截图写入 `build/hotfix-qr-<dpr>.png`。 |
| `--ui-shots-test` | P3；追加 1280/820 质量 chip 截图与顶栏溢出断言（失败时退出码 1）。 |

---

## 5. 门禁（最终 exe，Medium 在线；与热修交接 §1 基线对比）

- exe：`build/release/bin/lapian.exe`，SHA256 `EE366C0A950B455068480EA2397EE80F1FDE62BE360103A3B4C58E85109A52CF`；
  Medium 副本 `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe` 同 hash。
- 基线备份：`lapian-3d.exe`（Medium）、`build/release/bin/lapian-3d-backup.exe`（b38df22）。

### 本地

| 项 | 最终 | 基线 | 结论 |
|---|---|---|---|
| `ctest` | **37/37**（63.05s） | 37/37 | 不变 |
| `--banner-test` | **74/0** | 74/0 | 不变 |
| `--key-test` | **77/0** | 77/0 | 不变 |
| mark / note | **0 失败**（3/3、17/17） | 0 | 不变 |
| `--snapshot-test` | **35/0** | 35/0 | 不变 |
| `--readout-edit-test` | **28/0**，M7 四态 `rel=85,13 32x14` | 同 | 不变 |
| fullscreen / loop / loop-end | 全过 | 全过 | 不变 |
| `--drag-test` P4 / P6 | **95.6–95.9fps / p95 21.6–24.1ms** | 历史 64.5 / 19.9；**同机 A/B b38df22：95.6–96.1 / 23.5–25.1** | P4 不降；P6 同机 A/B 不差（绝对 19.9 是历史低负载值） |
| `--drag-strategy-test` | **7/7**（release_p95 25.5） | 7/7 | 不变 |
| `--startup-test` | P1 **812ms**、P2 **1052ms** | 759–857 / 988–1100 | 在基线内 |
| `--local-drag-parity-test` | **9/0** | 9/0 | 不变 |
| `--dpi-test` 4 DPI×{播放,全屏,窄窗} + `--empty` | problems 全空 | 同 | 不变 |
| M4 | **64.17 / 64.17 / 64.42 / 56.64 / 60.80%** | 同 | 一个数没动 |
| cache manage `--ui` | **113/0** | 72/0+98/0 | 只增不减 |
| `--scrub-space-test --file` | **62/0** | — | 本地对照 |
| `--ui-shots-test` 4 DPI | exit 0，overflow=0 | — | P3 |

### 在线（Medium，匿名；SESSDATA 过期仍 `vip_status=-1`）

| 项 | 最终 | 基线 | 结论 |
|---|---|---|---|
| M1 OB-1/2/3 | **24/0、24/0、24/0**（qn=32 `blocked-quality`） | 24/0 ×3 | 不变 |
| `--online-drag-parity-test` | **61/0**（58.53/58.47fps、gap 32/32ms、presented_diff 0.000） | 61/0 | 不变 |
| `--online-longpress-test` | **54/0**（ui_gap 67ms） | 54/0 | 不变 |
| T8.6.1b `--online-drag-preview-test` | **35 检查 / 1 失败**：`forward_ui_gap=225ms`（>200） | 基线 24/1，206–228ms | **既有本机基线**，非本轮回归 |
| `--online-offline-test` | **45/0** | 45/0 | 不变 |
| `--online-cache-test` | **32/0 pass** | 32/0 | 不变 |
| `--online-fault-test` | **92/0（10/10）**，`ui_gap=203ms` >200 → verdict fail | 92/0，220ms 既有 | **既有本机基线** |
| QR / QR `--ui` | **19/0 / 26/0**（4 DPI 均 26/0） | 19/0 / 21/0 | 只增不减 |
| `--scrub-space-test` 在线 | **64/0**（20/20 轮） | — | P0 修复证据 |
| `--cache-switch-online-test` | **17/0** | — | P1 修复证据 |

**Step 0 基线复跑**（同会话，b38df22）：M1 24/0×3、parity 61/0、longpress 54/0、
drag-preview 35/1（225ms）、fault 92/0（208ms）、offline 45/0、cache 32/0、QR 19/0+21/0；
本地 ctest 37/37、banner 74/0、key 77/0、M4 不变、drag P4 95.7/P6 20.4、local parity 9/0。

---

## 6. 视觉证据

- 扫码态：`build/hotfix-qr-100.png`、`-125`、`-150`、`-200`（200% 右侧滚动条，QR 240×240 +
  刷新/取消完整可见）。
- 清晰度 chip：`build/ui-v2-shots/hotfix-quality-{100,125,150,200}.png`（1280/928）、
  `hotfix-quality-820-{100,125,150,200}.png`（820 窄窗）。
- 3D 主窗/对话框/菜单证据仍在 `build/ui-v2-shots/3d-*.png`（`--ui-shots-test` 重新生成）。

---

## 7. 与热修交接的偏差 / 解释

1. **P0 未用备选 A**（落地前记 `was_playing` + `SetPosition` 后补 Play）：B 已通过 20/20，
   且不会产生一次 `Pause/Play` 音频重启。
2. **P1 的"手动 Medium 验证"用真实会话探针替代**：`--cache-switch-online-test` 走完整
   `OpenOnlineDialogForTest → StartOnline → ApplyCacheSettings → Shutdown → 再次在线` 产品路径，
   断言播放不中断、store/.part 不变、下次会话落新根；没有另写测试替身。
3. **P2 不是"只在 150%/200% 才套滚动区"**：始终套 `QScrollArea`，100%/125% 由
   `AdjustToContent()` 长到内容高度（无滚动条），150%/200% 夹到可用高后滚动。这样只维护
   一条布局路径，避免运行期 reparent。
4. **P2 最小高度 560→420**：否则 200% 可用逻辑高 ~516 时窗口无法夹进屏幕；内容高度由
   `AdjustToContent()` 决定。
5. **P3 chevron 是 QPushButton 的左侧 icon**（Qt 不原生支持"文字左、icon 右"而不改自绘/QSS）；
   仍是要求里的 12px `kChevronDown`，文本 `清晰度 1080P`，objectName/getter/NoFocus 不变。
6. **T8.6.1b 显示 35 检查（不是 24）**：默认增长会话含已缓存/未缓存/短拖/关窗四段；失败
   仍只有 1 条 `forward_ui_gap`（225ms），与基线 24/1 的 1 条同源。
7. **`--ui-shots-test` 现在会因顶栏溢出退出码 1**：这是 P3 新增的断言，不再总是 0。

---

## 8. 残余 / 风险

1. **SESSDATA 过期**（`--online-http-selfcheck` → `code=-101 vip_status=-1`）：A/M2/quality/vip
   1080P 断言本轮未跑；四项热修本身匿名可测。用户给新凭据后在同一环境补跑。
2. **T8.6.1b `forward_ui_gap` 225ms / fault `ui_gap` 203ms**：与 3C/3D 同机基线同源
   （206–228 / 220ms），不因本轮四个 commit 改阈值或隐藏。若要做绿需单独查 UI 线程阻塞。
3. **P2 200% 滚动态**：进入扫码时 `ensureWidgetVisible` 会把顶部说明/输入滚出视口（这是
   设计目标：保证二维码与刷新/取消可见）；用户可向上滚动回到输入区。
4. **P1 旧缓存不迁移**：旧目录文件原样保留，需要用户手动清；`cache_settings_pending_`
   只在内存，应用重启后直接按已写入的 settings 使用新 root（不会再走 pending）。
5. **P1 设置对话框的离线"立即生效"语义未改**：离线保存仍在 `callbacks.apply` 内换 store；
   "对话框打开期间绝不换 store"只约束 P1 的延迟应用路径（`cache_settings_dialog_open_`）。
6. **构建坑（本轮遇到）**：用 `Copy-Item` 从备份恢复源文件会保留旧 mtime，MSBuild 可能
   认为 object 更新而**跳过编译**（P1 的 `CacheSettingsDialog.cpp` 曾因此让中间二进制仍是
   旧行为）。恢复文件后必须 `touch` 该文件或 `cmake --build --clean-first`；最终 exe 已
   `touch` + 重建并复验 113/0。
7. **`--mark-test` 前也要清 sidecar**：Step 0 曾因 `media/测试.mp4.lapian.json` 残留导致
   `focus-note-auto-expand` 假失败；清掉后 0 失败（热修交接只写了 note 前清）。
8. 用户实机体感（拖动后 Space、在线改目录、扫码、清晰度按钮）**待确认**；本文件不写
   "验收通过"。

---

## 9. 复现命令

```powershell
cd 'E:\拉片软件开发'
$env:PATH = 'E:\拉片软件开发\third_party\Qt\6.8.3\msvc2022_64\bin;' + $env:PATH
$env:TMP = 'E:\拉片软件开发\.deps\tmp'; $env:TEMP = $env:TMP
$cmake = 'E:\拉片软件开发\third_party\cmake\3.31.6\cmake\data\bin\cmake.exe'
& $cmake --build build\release --config Release
& 'E:\拉片软件开发\third_party\cmake\3.31.6\cmake\data\bin\ctest.exe' --test-dir build\release -C Release

$exe = '.\build\release\bin\lapian.exe'; $media = 'media\测试.mp4'
$sp = 'build\hotfix-gates\settings-final'
& $exe --scrub-space-test --file $media --settings-path $sp          # 62/0
& $exe --online-cache-manage-test --ui                              # 113/0
& $exe --online-qr-test --ui                                        # 26/0 + build/hotfix-qr-100.png
& $exe --ui-shots-test --file $media --settings-path $sp            # 质量 chip + overflow=0
& $exe --dpi-test --file $media --dpi-tag hotfix --dpi-expect-scale 1
& $exe --dpi-test --empty --dpi-tag hotfix-e --dpi-expect-scale 1
```

- 在线（Medium `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`，从仓库根运行）：
  ```powershell
  $online = "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"
  & $online --scrub-space-test --bvid BV1BMnv6jE5S --cid 42915922022 --timeout-ms 60000
  & $online --cache-switch-online-test --bvid BV1BMnv6jE5S --cid 42915922022 --timeout-ms 60000
  & $online --online-drag-parity-test --bvid BV1BMnv6jE5S --cid 42915922022 --drag-seconds 1 --drag-samples 60 --m1-throttle-bps 262144
  & $online --online-longpress-test --bvid BV1BMnv6jE5S --cid 42915922022 --longpress-fresh-cache
  ```
- 坑：`--online-qr-test` 用系统 `%TEMP%`（先清空/用新目录）；`--online-fault-test` 前清
  `build\t8.13-fault`；`--snapshot-test` 前清 `media\snapshots`；`--mark-test` 前也清
  `media\测试.mp4.lapian.json`。

---

## 10. 回滚

四个代码 commit 各自独立，可单独 `git revert`：

- `534d909` → 回到"拖动后立刻 Space 会被 seek 落地 Pause 掉"的旧行为（P1–P3 不受影响）；
- `b78bbe1` → 在线保存缓存目录重新被拒绝（对话框/`ApplyCacheSettings` 同时回退）；
- `6533da0` → 扫码面板恢复裁切（二维码/按钮依赖拖动窗口才完整）；
- `efc6831` → 清晰度入口恢复 flat 文本按钮（Theme/TopBar/截图断言回退）。

文档 commit 只改 `docs/` 与本文件，不参与代码回滚。
