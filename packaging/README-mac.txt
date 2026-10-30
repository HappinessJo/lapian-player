拉片软件 Lapian — macOS 安装与使用 / Installation & Use
====================================================

中文
----
1. 打开 DMG，将 Lapian.app 拖到“应用程序”。ZIP 用户可解压到任意可写位置。
2. 本包仅支持 Apple Silicon（arm64）。未提供 Intel / Universal Binary。
3. 当前包为 ad-hoc 签名，未经过 Apple 公证。首次运行若出现 Gatekeeper 提示，
   请确认下载来源和 SHA256SUMS 后，在 Finder 中右键 Lapian.app → 打开。
   不要关闭系统完整安全性设置。
4. 打开本地视频：Cmd+O（界面也显示 Ctrl+O）；B 站在线：Cmd/Ctrl+U。
   Space 播放/暂停；PgUp/PgDn 逐帧；I/O 设置 A/B；L 循环；S 截图。
5. 标记和备注默认写在视频旁的 <视频名>.lapian.json；不可写时回退到
   ~/Library/Application Support/拉片软件/marks。
6. 在线缓存位于 ~/Library/Caches/拉片软件/cache，可在设置中修改或清空。
7. SESSDATA 保存在 macOS 钥匙串，标记文件不含明文；只向 api.bilibili.com 发送。
8. 删除 App 不会自动删除设置、标记、截图或缓存。请按需手动清理上述目录。

English
-------
1. Open the DMG and drag Lapian.app to Applications. The ZIP can be extracted
   anywhere writable.
2. This build supports Apple Silicon (arm64) only. No Intel / Universal build
   is included.
3. The app is ad-hoc signed and is not notarized by Apple. If Gatekeeper warns
   you, verify the source and SHA256SUMS, then right-click Lapian.app in Finder
   and choose Open. Do not disable system-wide security protections.
4. Open a local video with Cmd+O (the UI also shows Ctrl+O), or a Bilibili URL
   with Cmd/Ctrl+U. Space: play/pause. PgUp/PgDn: frame step. I/O: A/B range.
   L: loop. S: source-resolution PNG snapshot.
5. Marks and notes are stored next to the video as <video>.lapian.json, with
   fallback to ~/Library/Application Support/拉片软件/marks when read-only.
6. Online cache lives in ~/Library/Caches/拉片软件/cache and can be changed or
   cleared in Settings.
7. SESSDATA is stored in the macOS Keychain, never as plaintext in the marker
   file, and is sent only to api.bilibili.com.
8. Deleting the app does not delete settings, marks, snapshots or cache.
   Remove those directories manually only if you no longer need them.

Version / 版本: v0.1.0-macos.1
Release / 发布: https://github.com/luoboruodai/lapian-player/releases/tag/v0.1.0-macos.1
Build source / 源码: https://github.com/luoboruodai/lapian-player/tree/v0.1.0-macos.1
Upstream / 原作者: https://github.com/HappinessJo/lapian-player
License: MIT plus third-party licenses in Contents/Resources/licenses.
