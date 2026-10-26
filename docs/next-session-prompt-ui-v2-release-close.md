# 新会话提示词：UI v2 3D 热修 · 发布收口（release closeout）

- **状态**：待执行（2026-10-26 起草）。
- **当前代码**：HEAD `1e407ad`（4 个代码 commit `534d909`/`b78bbe1`/`6533da0`/`efc6831` 之后，
  只有文档提交）；四项热修已完成并通过全量回归。
- **本轮范围**：只做**构建 → 打包 → 安装 → 安装版冒烟 → 发布文档**；**不改产品代码、不改门/阈值/
  schema、不重开 3D/热修范围**。若打包/安装失败且根因在产品代码，停下来报告，不静默热修。
- **上游**：[热修实现交接](handoff-2026-10-26-ui-v2-3d-hotfix-close.md)（修复/门禁/证据）、
  [3D 收口](handoff-2026-10-26-ui-v2-3d.md)、[上次发布收口](handoff-2026-10-25-p0-close.md)（流程/格式）、
  [预览发布说明](preview-release-2026-10-22.md)、[T8.16 安装/卸载](handoff-2026-10-24-t8.16.md)。

---

## 提示词（复制到新会话）

```text
你是拉片软件「UI v2 3D 热修 · 发布收口」agent。四项用户实机反馈已经修完并通过全量回归
（P0 拖动后 Space、P1 在线改缓存目录、P2 扫码布局、P3 清晰度 chip），现在只做发布收口：
把当前 HEAD 变成可安装/可分发的版本，留下可复现证据。**不改产品代码、不改任何门/阈值/
schema、不重开 3D 范围**；如果构建/打包/安装失败且根因在产品代码，停下来报告，不要静默热修。

先读（按顺序）：
- AGENTS.md：§1（3D/热修条目）、§9 已知坑、§11 协作规则、§12 文档表
- docs/handoff-2026-10-26-ui-v2-3d-hotfix-close.md（本轮修复/门禁/证据，先读这个）
- docs/handoff-2026-10-26-ui-v2-3d.md（3D 收口）
- docs/handoff-2026-10-25-p0-close.md（上一次发布收口的完整流程/命令/格式，照它做）
- docs/preview-release-2026-10-22.md（发布说明格式/产物表）
- scripts/build-release.ps1、scripts/package.ps1、scripts/installer.iss
- docs/handoff-2026-10-24-t8.16.md（静默安装/卸载参数、快捷方式/设置核对）

当前状态（开工先核对，别假设）：
- git HEAD 应为 1e407ad（或之后仅 docs 提交）；`git status` 应干净。
- 4 个代码 commit：534d909（P0 待播/落地补 Play）、b78bbe1（P1 两阶段缓存设置）、
  6533da0（P2 扫码长高/夹屏/滚动）、efc6831（P3 清晰度 chip）；文档 c23003d/1e407ad。
- 已构建 exe（热修后）：build\release\bin\lapian.exe 3,538,432B，
  SHA256 EE366C0A950B455068480EA2397EE80F1FDE62BE360103A3B4C58E85109A52CF；
  Medium 副本 %LOCALAPPDATA%\Programs\拉片软件\lapian.exe 同 hash。
- 热修全量回归已完成（发布前基线，供对照）：ctest 37/37、--banner-test 74/0、--key-test 77/0、
  mark/note/snapshot/readout 不变、--dpi-test 4 DPI×{播放,全屏,窄窗}+空态 problems 空、
  M4 64.17/64.17/64.42/56.64/60.80%、drag 同机 A/B 不差（hotfix P4 95.6–95.9 / P6 21.6–24.1ms
  vs b38df22 95.6–96.1 / 23.5–25.1ms）、strategy 7/7、startup 812/1052ms、local parity 9/0；
  在线（Medium）M1 24/0×3、parity 61/0、longpress 54/0、scrub-space 64/0、
  cache-switch 17/0、offline 45/0、cache 32/0、QR 19/0+26/0。
- 既有已知失败（不要当回归、不要改阈值）：T8.6.1b 35/1（forward_ui_gap 225ms，基线 206–228ms）、
  fault 92/0 但 ui_gap 203ms（基线 220ms）。SESSDATA 已过期（vip_status=-1），A/M2/quality/vip
  1080P 本轮不跑；用户给新凭据再补。
- 用户体感："很不错了，很舒服了"（只记体感，不写"验收通过"）。

硬边界（违反即停）：
- 不改 src/ 产品代码；只允许改 docs/ 与本轮发布脚本的调用（脚本本身不改）。
- 不改门/阈值/性能数字/schema/spec §3.7 键位；不改 3B/3C/3D 语义。
- 不升级 Qt/FFmpeg/CMake；不改 installer.iss 的安装形态（每用户、%LOCALAPPDATA%）。
- 在线用例必须用 Medium 安装版 exe；工作区 exe 是 Low integrity。
- 不打印 SESSDATA/Cookie/签名 URL；不清用户缓存/记录/截图。

任务（按顺序，每步留日志与证据）：

0) 预检（不改代码）：
   - git status/HEAD、git log --oneline -8；确认 4 个代码 commit 都在。
   - 核对源文件 mtime vs obj：若 build\release 里 CacheSettingsDialog.obj 等比源文件新，
     可能踩过"恢复备份 mtime 旧 → MSBuild 跳过编译"的坑；本轮 -Clean 会重配，但仍先记录。
   - 记录 %APPDATA%\拉片软件\settings.json 与 %LOCALAPPDATA%\Programs\拉片软件 当前
     exe hash/文件清单，便于安装前后对比。

1) clean Release 重建（含单测）：
   - `scripts\build-release.ps1 -Clean -Package`（PowerShell 5.1；工作目录仓库根）。
     它会删 build\release、全新 configure、Release 构建、ctest、生成 dist。
   - 若 configure 崩 0xC0000409：按 AGENTS §9 把旧目录改名后全新配置，别在旧目录反复重试。
   - 记录：构建耗时、ctest 结果（应 37/37）、新 exe 路径/大小/SHA256。
     注意 clean 重建的 exe hash 可能与 EE36 不同（链接时间戳），以实际新 hash 为准；
     要求"build exe == dist exe == 安装版 exe"。

2) 打包产物核对：
   - `out\package\Lapian-<ver>-setup.exe`、`Lapian-<ver>-portable.zip`（package.ps1 生成）。
   - 记录：dist 文件数/总字节/MB、exe hash、setup/zip 大小与 SHA256；
     检查 licenses/SOURCE-OFFER/README 是否在 dist 与 zip 内。
   - 若 `-Package` 没自动生成 setup/zip，再单独跑 `scripts\package.ps1`。

3) 安装验证（每用户、非管理员）：
   - 把 setup 复制到 C: 本地 `%TEMP%\lapian-release-<date>\`（避免 E: SmartScreen）。
   - `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /LOG="%TEMP%\...\install.log"`；
     等进程退出，记录 exit code（应为 0）与日志里的
     `User privileges`/`Administrative install mode`（应为 None/No）。
   - 核对：安装目录 `%LOCALAPPDATA%\Programs\拉片软件\lapian.exe` hash == dist exe hash；
     Start 菜单 `拉片软件` 组快捷方式 target == 安装 exe；桌面快捷方式（若安装了该任务）同样；
     `%APPDATA%\拉片软件\settings.json` hash 与预检一致（安装不清设置）；
     不删除安装目录里用户留的 lapian-3d.exe/lapian-3c.exe 备份（安装器不动它们即可）。
   - 说明：安装目录里已有热修手工副本，setup 覆盖 lapian.exe 后 hash 应变为**新 dist hash**。

4) 安装版冒烟（用 %LOCALAPPDATA%\Programs\拉片软件\lapian.exe，从仓库根运行）：
   - 本地：`--banner-test 74/0`、`--key-test 77/0`、`--dpi-test` 4 DPI×{播放,全屏,窄窗}
     + `--dpi-test --empty` problems 空、`--drag-test` P4/P6 记录；`--startup-test` 记录。
   - 在线（Medium）：`--online-m1-test` 24/0、`--scrub-space-test` 64/0、
     `--cache-switch-online-test` 17/0；建议再跑 `--online-drag-parity-test` 61/0、
     `--online-longpress-test` 54/0、`--online-qr-test` 19/0 + `--ui` 26/0。
   - 已知失败只记录、不改阈值：T8.6.1b 35/1（forward_ui_gap ~225ms）、fault 92/0（ui_gap ~203ms）。
   - 坑：QR 用干净 %TEMP%；fault 前清 build\t8.13-fault；snapshot 前清 media\snapshots；
     mark-test 前清 media\测试.mp4.lapian.json；GUI 探针用管道，不要 `$out = & exe ... 2>&1`。

5) 发布文档 + 提交：
   - 新建 `docs/release-2026-10-26-ui-v2-hotfix.md`（或按 preview-release 格式更新）：
     版本 0.1.0、代码 HEAD/4 个 commit、构建耗时、ctest、dist 文件数/大小/exe hash、
     setup/zip 大小+SHA256、安装验证（权限/退出码/hash/快捷方式/settings）、安装版冒烟数字、
     已知残余（T8.6.1b 35/1、fault ui_gap、SESSDATA 过期未跑 1080P、用户体感只记体感）。
   - 更新 AGENTS §12（新发布文档 + 本轮 v6.58 摘要）与 AGENTS 顶部"最后更新"。
   - 只提交 docs；`git status` 干净；报告 commit hash。

退出条件：
- clean 构建成功 + ctest 37/37；
- dist/setup/zip 产物与 hash 记录完整；
- 安装版 exe hash == dist exe hash，快捷方式/settings 核对通过；
- 安装版冒烟通过（已知失败按原样记录）；
- 发布文档 + AGENTS §12 已提交，git 干净；
- 报告里写清"用户体感只记体感，不写验收通过"。
```

---

## 交接备注（给起草者/下一个 agent）

- 本轮**不要**顺手做 3E 视觉项、UI gap 专项、在线 1080P 补测；那些是发布收口之后的事。
- 如果用户中途给了新 SESSDATA，也只影响发布后的 1080P 补测，不要把它塞进发布收口范围。
- 发布收口的价值是"用户能装上新版"；宁可少跑一项非关键在线用例，也不要把未验证的安装包
  写成"已验证"。
