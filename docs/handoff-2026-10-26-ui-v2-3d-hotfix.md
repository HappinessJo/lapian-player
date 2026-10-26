# UI v2 3D 热修交接 — 2026-10-26

- **状态**：3D 收口已提交（代码 `b38df22`、文档 `ee2579f`）；**本文件只做交接/规划，未改代码**。
- **上游**：[3D 交接](handoff-2026-10-26-ui-v2-3d.md)、[3C](handoff-2026-10-26-ui-v2-3c.md)、
  [3B](handoff-2026-10-26-ui-v2-3b.md)、[AGENTS](../AGENTS.md) §1/§9/§11/§12。
- **用户反馈（2026-10-26 实机）**：
  1. 在线模式不能改缓存目录，必须退回本地模式；用户已确认接受**两阶段保存**（在线可改，本次会话结束后生效）。
  2. 拖动进度条后**立刻按 Space**，播放卡住；等一拍再按正常。用户强调**流畅度不能回退**。
  3. 扫码登录按钮按下后二维码界面显示不全，拖一下窗口才完整。
  4. 清晰度入口看起来像状态文字，不像可点按钮。
- **下一轮**：**开 1 个新对话**，按 **P0 拖动后 Space 卡住 → P1 在线改缓存目录 → P2 扫码布局 →
  P3 清晰度 chip** 顺序做；每项独立 commit。提示词见
  [next-session-prompt-ui-v2-3d-hotfix.md](next-session-prompt-ui-v2-3d-hotfix.md)。
  本对话作为 3D 收口与诊断记录保留，不再继续改代码。

---

## 1. 当前基线（必须保持）

| 项 | 当前值 |
|---|---|
| 代码/文档 | `b38df22` / `ee2579f` |
| ctest | **37/37** |
| `--banner-test` | **74/0** |
| `--key-test` | **77/0** |
| M4 | **64.17 / 64.17 / 64.42 / 56.64 / 60.80%**（100/125/150/200/窄窗） |
| `--dpi-test` | 4 DPI × {播放,全屏,窄窗} problems 空；`--empty` 4 DPI 空态 problems 空 |
| `--drag-test` | P4 **64.5fps** / P6 p95 **19.9ms**（tl=132） |
| `--drag-strategy-test` | **7/7** |
| `--local-drag-parity-test` | **9/0** |
| 在线 M1（匿名） | OB-1 24/0、OB-2 24/0（复跑）、OB-3 25/0；qn=32 `blocked-quality`（凭据过期） |
| 在线 parity | **61/0**（`--m1-throttle-bps 262144`） |
| 在线 longpress | **54/0**（`--longpress-fresh-cache`） |
| T8.6.1b | **24/1**，`forward_ui_gap` 206–228ms（3C v2 同机 A/B 213ms，既有本机基线） |
| 在线 offline/cache/fault | 45/0 / pass / 92/0（fault `ui_gap=220ms` 既有基线） |
| QR | 19/0；`--ui` 21/0（需干净 `%TEMP%`） |

**外部阻塞（不影响本轮 4 项修复，但影响 1080P 断言）**：SESSDATA 过期
（`--online-http-selfcheck` → `code=-101 is_login=0 vip_status=-1`），A 42/0、`--online-m2-test`、
`--online-quality-test --ui`、`--online-vip-test --ui` 未跑；若用户提供新凭据，在同一个新对话补跑。

---

## 2. 四项问题的定位与修复方案

### 2.1 拖动后立刻 Space 卡住（P0）

**根因链**：

1. 时间轴松手 → `TimelineWidget::mouseReleaseEvent` 同步 `emit scrubFinished`。
2. `MainWindow::OnScrubFinished()` → `GoToFrame(target)`。
3. 在线增长分支（`GoToFrameInternal`，`online_ready_ && !online_cache_hit_`）先 `playback_->Pause()`，
   然后 `RequestOnlineSeek()` **异步**开始并立即返回。
4. 用户立刻按 Space → `TogglePlayback()` 看到暂停，`playback_->Play()` 启动播放。
5. 几十毫秒后 seek 落地，回调**无条件** `playback_->SetPosition(target, pts)`；
   `PlaybackController::SetPosition()` 第一行 `Pause()`，刚启动的播放又被停掉 → 画面卡住。
6. 等一拍再按：seek 已落地，`SetPosition` 的 `Pause` 无副作用，Space 才 `Play()`，所以正常。

**修复（推荐 B）**：

- 加 `online_pending_play_`。
- `TogglePlayback()`：当 `online_mode_ && !online_cache_hit_ && online_seek_running_` 时，
  不立即 `Play()`，只切换 pending 意图，状态栏提示“定位完成后开始播放 / 已取消播放”。
- seek 落地回调：`SetPosition` 之后，如果 pending 为真，清标记并 `playback_->Play()`。
- 清理点：新 seek、`OnScrubStarted`、`BeginOnlineRequest`、`ShutdownOnlineSession`、
  `CancelOnlineLoading`、seek 失败/取消、切档；再按一次 Space 取消 pending。
- 备选 A：落地前记录 `was_playing`，`SetPosition` 后补 `Play()`；改动小但会有一次 `Pause/Play`，
  可能有极短音频重启，B 不生效再用。

**测试**：

- 新增 `--scrub-space-test`（先写探针让它失败）：
  - 在线增长会话拖动 15→75，松手后立刻 `PressSpaceKeyForTest`；20 次；
  - 断言 `IsPlaying=1`、`PresentedFrameCount` 1s 内增长、`online_seek_running=0`；
  - 本地文件同操作变体（如果本地也复现，单独查 `EndScrubPreview`/`preview_timer_` 事件顺序）。
- 回归：`--online-drag-parity-test 61/0`、T8.6.1b 不低于 24/1、P4/P6 不低于基线、`--key-test 77/0`。
- 不改任何门/阈值/性能数字。

### 2.2 在线模式改缓存目录（P1，已确认两阶段方案）

**根因**：

- `MainWindow::ApplyCacheSettings()` 在 `online_mode_` 时直接拒绝；
- `CacheSettingsDialog::Refresh()` 在 `session_active_` 时禁用目录/默认/配额/保存；
  `ApplyInternal()` 还会再拒绝；
- `OnlineSession` 用旧 `CacheStore*` 构造，当前 `.part`/下载/读层都指向旧目录，
  在线直接替换 `online_cache_` 会悬空/丢 `.part`；现有代码即使放开也会 `ShutdownOnlineSession(false)`，
  等于强制结束当前在线视频。

**修复（两阶段保存）**：

1. `CacheSettingsDialog`：`session_active_` 时不再禁用目录/配额/保存；顶部提示
   “当前在线会话继续使用旧目录；新目录/配额在本次会话结束后生效（下次在线打开使用）。”
   `ApplyInternal()` 不再因 `session_active_` 拒绝；保存成功显示 pending 文案。
2. `MainWindow::ApplyCacheSettings()` 在线时：
   - 用临时 `CacheStore` 校验新 root/配额（不碰当前 store）；
   - 写 `settings.json`；置 `cache_settings_pending_=true`；
   - **不替换** `online_cache_`，**不调用** `ShutdownOnlineSession`，返回成功。
3. 新增 `ApplyPendingCacheStoreIfIdle()`：
   - 仅 `online_==nullptr && !online_mode_` 执行；
   - 按 `loaded_settings_` 新建 `CacheStore`、`SetRoot`/`SetQuotaBytes`、替换 `online_cache_`、
     `online_cache_ready_=true`；
   - 调用点：`ShowCacheSettingsDialog()` 开头、`EnsureOnlineSession()` 开头、缓存维护入口。
4. 不迁移旧缓存；当前会话 `.part` 在旧目录完成；旧目录可手动清。配额同样延迟，避免
   `EvictToQuota` 清掉正在写的 `.part`。
5. 风险：dialog 持有旧 `CacheStore*`，**绝不在 dialog 打开期间替换 store**；只在 idle 时换。

**测试**：

- 扩展 `--online-cache-manage-test`：在线态保存 → 当前 root 不变、settings 已写、pending 提示；
  结束会话/下一次会话前 → 新 root 生效、旧目录缓存未删；
- 手动 Medium：在线播放 → Ctrl+, 改目录保存 → 播放不中断；关闭在线 → 重开在线 → 新目录生成缓存；
- 回归：`--online-offline-test 45/0`、`--online-cache-test`、cache manage 72/0+98/0、M1/parity/T8.6.1b 不变。

### 2.3 扫码二维码显示不全（P2）

**根因**：`OnlineResolveDialog` 构造时 `setMinimumSize(560,560)`，`qr_panel_` 初始 `hide()`；
`OnStartQrLogin()` 只 `qr_panel_->show()`，没有 `layout()->activate()` / `adjustSize()`。
顶层对话框不会因隐藏子控件显示自动长高，二维码下半部分被裁；拖动窗口触发 resize 才重排。

**修复**：

- `OnStartQrLogin()` / `OnQrRefresh()` 在 `show()` 后：
  `updateGeometry(); layout()->activate(); adjustSize();`
- 目标高度夹到当前屏幕 `availableGeometry()`；150%/200% 仍放不下时，把对话框主体放进
  `QScrollArea`（`widgetResizable=true`，无边框），保证二维码与“刷新/取消”可滚动可见。
- 测试：`--online-qr-test --ui` 增加 QR 面板可见、二维码 240×240 完整、对话框不超屏的断言；
  100/125/150/200% 各截扫码态图。
- 只影响对话框，不影响播放。

### 2.4 清晰度入口不像按钮（P3）

**根因**：`quality_button_` 是 `QPushButton` + `setFlat(true)`，只显示“清晰度：1080P”；
`TopBar::AdoptQualityButton()` 只设字体/NoFocus；`Theme` 无 `#onlineQualityButton` 规则。

**修复**：

- `Theme::GlobalStyleSheet()` 增加 `#onlineQualityButton` chip 样式：
  `surface3` 底、1px 边框、10px 圆角、`padding 0 7px`、hover accent 边框、pressed 加深、disabled 压暗。
- `TopBar::AdoptQualityButton()`：`setFlat(false)`、`setCursor(PointingHandCursor)`、
  12px `GlyphId::kChevronDown` 图标；文本保留“清晰度 1080P”。
- 不新增 per-widget `setStyleSheet`；objectName/getter/NoFocus 不变。
- 验证：100/150/200% 截图；在线 820 窄窗顶栏不溢出；`--key-test 77/0`、`--dpi-test` 不受影响。

---

## 3. 执行顺序与提交

| 顺序 | 问题 | 关键 commit |
|---|---|---|
| 0 | 基线 + 复现 | 无代码；跑 §1 基线；确认 Space 卡住是在线还是本地 |
| 1 | 拖动后 Space 卡住（P0） | `fix(ui-v2): resume playback after online seek lands` |
| 2 | 在线改缓存目录（P1） | `feat(ui-v2): allow cache dir change during online (deferred)` |
| 3 | 扫码布局（P2） | `fix(ui-v2): resize online dialog when QR panel shows` |
| 4 | 清晰度 chip（P3） | `style(ui-v2): make quality entry a chip with chevron` |
| 5 | 全量回归 + 文档 | 更新 3D 交接或新建 hotfix 交接；登记 AGENTS §12 |

**每项做完**：跑受影响的最小门禁 + 与基线对比；**全部做完**再跑 §1 全量。任何 P4/P6/T8.6.1b 退化
→ 停在该 commit，先修或回退，不改阈值。

**构建注意**：若新增测试 runner 文件会改 `CMakeLists`，本机重配可能崩（0xC0000409）→ 按 3B/3C 做法
把 `build\release` 整目录改名后全新 configure；**优先扩展现有 runner**，避免新增文件。

**边界**：不改 `GfxWidget` 呈现、`PreviewDecoder`/`DragStrategy`、`OnlineSession` 状态机、
`CacheStore` 语义、spec §3.7 键位、settings schema、`media::StatusNotice` 语义、任何门/阈值/
性能数字、3B 的四档降级、3C 的检查器布局。`#2` 只改“何时切换 store”，不改 CacheStore 行为。

---

## 4. 需要用户确认/提供

- 新 SESSDATA（可选；只影响 1080P 的 A/M2/quality/vip 断言；四项热修本身匿名可测）。
- 修完后实机体感确认（只记“顺畅/不顺畅”，不写“验收通过”）。
- 若本地文件也能复现 #2 的 Space 卡住，必须在开始修 #1 时告诉我，路径不同。

---

## 5. 验收

- 四项问题按 §2 的验收标准全部复现→修复→回归；
- `ctest 37/37`，§1 的本地/在线基线数字不退；
- 新测试 `--scrub-space-test` 20/20；cache manage 新用例通过；QR 4 DPI 截图完整；
  quality chip 截图/窄窗通过；
- 文档更新、每项独立 commit、可单独 revert。
