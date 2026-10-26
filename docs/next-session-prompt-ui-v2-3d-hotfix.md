# 新会话提示词：UI v2 3D 热修（Space 卡住 / 缓存目录 / 扫码 / 清晰度）

- **状态**：待执行（2026-10-26 起草）。
- **当前代码**：`b38df22`（3D 收口）；**本阶段只做热修，不重开 3D 范围**。
- **上游**：[热修交接](handoff-2026-10-26-ui-v2-3d-hotfix.md)（定位/方案/基线都在里面）、
  [3D 交接](handoff-2026-10-26-ui-v2-3d.md)、[视觉基准](ui-v2-visual-reference.md)。
- **下一轮**：只开 **1 个新对话**；按 **P0 拖动后 Space 卡住 → P1 在线改缓存目录 → P2 扫码布局 →
  P3 清晰度 chip** 顺序做，每项独立 commit。

---

## 提示词（复制到新会话）

```text
你是拉片软件 UI v2 的「3D 热修」实现 agent。3D 收口已完成并提交（代码 b38df22、文档 ee2579f），
本轮只修 4 个用户实机反馈的问题，不重开 3D 范围、不改任何门/阈值/性能数字。退出条件：4 项修复 +
新探针通过 + §基线回归不退 + 文档/commit。用户已明确接受“在线改缓存目录”的两阶段方案
（在线可保存，本次会话结束后生效，下次在线打开使用）。

先读（按顺序，别跳）：
- docs/handoff-2026-10-26-ui-v2-3d-hotfix.md（**直接上游，先读这个**）：四项根因/修复方案/基线/测试/边界
- docs/handoff-2026-10-26-ui-v2-3d.md（3D 收口、单壳、提交 b38df22）
- AGENTS.md：§1（UI v2 3A/3.5/3B/3C/3D 条目）、§9 已知坑、§11 协作规则、§12 文档表
- docs/ui-v2-plan.md §2.3/§3.2/§7.6/§7.7、docs/ui-v2-visual-reference.md §1/§3/§5/§6
- 代码（只读，动手前通读）：
  src/app/MainWindow.cpp/.h：OnScrubFinished/OnScrubTargetChanged/OnScrubStarted、GoToFrame/
    GoToFrameInternal、RequestOnlineSeek 的落地回调（SetPosition 那段）、TogglePlayback、
    ApplyCacheSettings、ShowCacheSettingsDialog、EnsureOnlineSession、ShutdownOnlineSession
  src/app/PlaybackController.cpp：SetPosition（第一行 Pause）/Play/Toggle
  src/app/TimelineWidget.cpp：mouseReleaseEvent → scrubFinished（同步）
  src/app/CacheSettingsDialog.cpp/.h：session_active_ 禁用逻辑、ApplyInternal、UpdateSummaryUi
  src/app/OnlineResolveDialog.cpp：OnStartQrLogin/OnQrRefresh、qr_panel_/qr_image_label_ 布局
  src/app/ui/TopBar.cpp/.h：AdoptQualityButton；src/app/ui/Theme.cpp：GlobalStyleSheet
  src/app/ui/Glyph.h：kChevronDown
  src/app/main.cpp：测试分发；已有 --online-* / --dpi-test / --ui-shots-test 入口
  src/media/StatusNotice.*、src/online/CacheStore.*、src/app/OnlineSession.*：只读，不改语义

硬边界（违反即停）：
- 不改 GfxWidget 呈现、PreviewDecoder/DragStrategy、OnlineSession 状态机、CacheStore 语义、
  media::StatusNotice 语义、spec §3.7 键位、settings schema、任何门/阈值/性能数字、
  3B 的 Transport 四档降级、3C 的检查器布局（折叠 44/220–560/三 tab/页脚）。
- 不改 P4/P6/T8.6.1b 的门槛；不让 P4/P6/在线 parity/T8.6.1b 比基线差。
- 不写 per-widget setStyleSheet；主题只走 Theme::GlobalStyleSheet() + 令牌。
- 在线用例必须用 Medium 副本 `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe`；工作区 exe 是 Low integrity。

当前基线（每项修完都要对）：
ctest 37/37；--banner-test 74/0；--key-test 77/0；
M4 64.17/64.17/64.42/56.64/60.80%；--dpi-test 4 DPI×{播放,全屏,窄窗}+--empty problems 空；
--drag-test P4 64.5fps / P6 p95 19.9ms；--drag-strategy-test 7/7；--local-drag-parity-test 9/0；
在线 M1 24/0（匿名 qn=32 blocked-quality）、parity 61/0、longpress 54/0、T8.6.1b 24/1
（forward_ui_gap 206–228ms，3C v2 同机 213ms，既有本机基线）；offline 45/0、cache pass、fault 92/0。
SESSDATA 已过期（--online-http-selfcheck → code=-101/vip_status=-1），A/M2/quality/vip 1080P 断言
先不跑；用户给新凭据再补，不要为它改门。

执行顺序（每项一个独立 commit；先复现/写探针，再修）：

0) 基线 + 复现（不改代码）：
   - 拉当前代码，跑一遍上面基线；确认 #1 的 Space 卡住是在线增长会话还是本地文件（用户说是实机，
     但务必自己确认；本地也能复现则路径不同，先停）。
   - 保底：现有 3C/3D exe 备份；Medium 副本用当前 b38df22 覆盖，记 hash。

1) P0 拖动后立刻 Space 卡住：
   - 根因：在线增长会话松手 → OnScrubFinished → GoToFrame 走异步 RequestOnlineSeek；
     Space 在 seek 落地前 Play()；落地回调无条件 SetPosition()，其第一行 Pause() 把播放停掉。
     等一拍再按则 seek 已落地，所以正常。
   - 先加 --scrub-space-test（在线增长，拖动 15→75，松手后立刻 PressSpaceKeyForTest，20 次；
     断言 IsPlaying=1、PresentedFrameCount 1s 内增长、online_seek_running=0；加本地变体）。
     先让它失败；不要为了测试改产品行为。
   - 修：加 online_pending_play_；TogglePlayback 在 online_mode_ && !online_cache_hit_ &&
     online_seek_running_ 时只记 pending，不立即 Play；落地 SetPosition 后若 pending 则 Play 并清标记；
     新 seek/OnScrubStarted/BeginOnlineRequest/ShutdownOnlineSession/CancelOnlineLoading/失败/取消/切档
     清标记；再按一次 Space 取消 pending。状态栏给“定位完成后开始播放/已取消播放”。
   - 备选 A（只在 B 不生效时）：落地前记 was_playing，SetPosition 后补 Play；会有一次 Pause/Play，
     可能有极短音频重启。
   - 验收：新探针 20/20；parity 61/0；T8.6.1b 不低于 24/1；P4/P6 不低于基线；key 77/0；ctest 37/37。
     如果本地也复现，另查 EndScrubPreview/preview_timer_ 与 GoToFrame 的事件顺序，不要硬套在线方案。

2) P1 在线改缓存目录（两阶段，用户已确认）：
   - 根因：ApplyCacheSettings 在 online_mode_ 直接拒绝；CacheSettingsDialog 在 session_active_ 禁用
     目录/配额/保存并在 ApplyInternal 再拒绝；OnlineSession 持有旧 CacheStore*，当前 .part 在旧目录，
     在线直接换 store 会悬空/丢 .part，且现有代码会 ShutdownOnlineSession（等于强制结束在线视频）。
   - 修：在线时允许编辑/保存；临时 CacheStore 校验新 root/配额；写 settings.json；置
     cache_settings_pending_=true；不替换 online_cache_、不 Shutdown；返回成功。
     新增 ApplyPendingCacheStoreIfIdle()：仅 online_==nullptr && !online_mode_ 时按 loaded_settings_
     新建 Store/SetRoot/SetQuota/替换；调用点 ShowCacheSettingsDialog 开头、EnsureOnlineSession 开头、
     缓存维护入口。配额同样延迟，不 Evict 当前 .part；旧目录不迁移。
     CacheSettingsDialog 文案改 pending：“当前在线会话继续使用旧目录；新目录/配额在本次会话结束后
     生效（下次在线打开使用）”，保存后不要写“已生效”。
   - 测试：扩展 --online-cache-manage-test：在线保存后当前 root 不变、settings 已写、pending 提示；
     结束会话/下一次会话前新 root 生效、旧目录缓存未删；当前 .part 不被清。手动 Medium：在线播放 →
     Ctrl+, 改目录 → 播放不中断 → 关闭在线 → 重开在线 → 新目录生成缓存。
   - 风险：dialog 持有旧 CacheStore*，绝不在 dialog 打开期间替换 store。

3) P2 扫码二维码显示不全：
   - 根因：对话框 setMinimumSize(560,560)+qr_panel_ 初始 hide；OnStartQrLogin 只 show()，
     没有 layout()->activate()/adjustSize()；顶层窗口不会自动长高，二维码被裁，拖窗口才重排。
   - 修：OnStartQrLogin/OnQrRefresh 在 show() 后 updateGeometry+layout activate+adjustSize，
     高度夹到屏幕 availableGeometry；150%/200% 放不下时把主体放进 QScrollArea
     （widgetResizable=true，无边框），保证二维码和刷新/取消可见。
   - 测试：--online-qr-test --ui 增加 QR 面板可见、二维码 240×240 完整、对话框不超屏断言；
     100/125/150/200% 各一张扫码态截图。

4) P3 清晰度入口：
   - 根因：quality_button_ 是 flat QPushButton，只显示“清晰度：1080P”；TopBar 收养时只设字体/
     NoFocus；Theme 没有 #onlineQualityButton 样式。
   - 修：Theme 加 #onlineQualityButton chip 样式（surface3 底、1px 边框、10px 圆角、padding 0 7px、
     hover accent 边框、pressed 加深、disabled 压暗）；TopBar::AdoptQualityButton setFlat(false)、
     PointingHandCursor、12px kChevronDown 图标；文本保留“清晰度 1080P”；不新增 per-widget QSS，
     objectName/getter/NoFocus 不变。
   - 验证：100/150/200% 截图；在线 820 窄窗不溢出；key 77/0；dpi 不变。

收口：
- 每项独立 commit；最后更新 docs/handoff-2026-10-26-ui-v2-3d.md 或新建 hotfix 交接，
  登记 AGENTS §12；用户实机体感确认只记“体感”，不写验收通过。
- 全量回归：ctest、dpi 4×+empty、banner、key、mark/note/snapshot、readout、fullscreen/loop、
  drag P4/P6、strategy、startup；在线 M1/parity/longpress/T8.6.1b/offline/cache（Medium）。
  任何数字退化 → 停在该 commit 先修/回退，绝不改阈值。
- 构建坑：新增 runner 文件会改 CMakeLists，本机重配可能崩（0xC0000409）；优先扩展现有 runner，
  否则按 3B/3C 把 build\release 整目录改名后全新 configure。
- 工具坑：GUI 探针不要用 `$out = & exe ... 2>&1`（会挂进程）；用管道。
  中文源文件不要用 Get-Content -Raw/Set-Content 改写。
```
