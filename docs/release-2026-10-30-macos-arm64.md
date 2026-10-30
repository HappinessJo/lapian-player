# Lapian v0.1.0-macos.1 — macOS Apple Silicon

## 中文

基于 [HappinessJo/lapian-player](https://github.com/HappinessJo/lapian-player) 的 MIT 源码进行原生 macOS 适配。保留原作者与 Windows 实现，不是对 Windows 安装包的反编译。

### 下载与安装

- `Lapian-0.1.0-macos.1-macos-arm64.dmg`：打开后将 `Lapian.app` 拖入“应用程序”。
- `Lapian-0.1.0-macos.1-macos-arm64.zip`：解压到可写目录后运行 App。
- `SHA256SUMS`：上述两个附件的 SHA-256。GitHub 自动生成的 Source code 是源码归档，不是安装入口。

本版本仅提供 Apple Silicon（arm64）原生构建。当前为 ad-hoc 签名，未经 Apple 公证，也没有 Developer ID。首次运行请核对下载来源和校验值，在 Finder 中右键 App →“打开”，不要关闭系统完整安全性设置。

### 主要变化

- Qt Widgets + FFmpeg 软件帧绘制，CoreAudio / AudioUnit 音频输出与采样时钟。
- libcurl API 和 CDN Range 下载，macOS 钥匙串保存 SESSDATA，POSIX 文件接口与共享读锁。
- 保留逐帧、精确定位、拖动预览、波形、A/B 循环、变速、标记、备注与源分辨率截图。
- 修复在线预热缓存窗口、匿名接口压缩响应、CDN 默认请求头及 HTML 错误页写入缓存的问题。
- 修复主窗口析构时全局焦点回调访问已释放对象的问题。
- 使用中英文 README、macOS 构建/打包脚本及可复现的 FFmpeg 9.0.2 最小 LGPL 构建。

### 本机验证

- 完整 CTest：35/35 通过；4 个可选 VFR、10-bit、Windows 硬解相关用例跳过，未将这些范围写成通过。
- 本地真实窗口：播放、前后逐帧、标记、1280×720 PNG 截图。
- 匿名公开视频接口：38/38 检查通过，覆盖 view、DASH 与 durl 解析；实际可用档位以服务端返回为准。
- 匿名 DASH 端到端：25/25 检查通过，覆盖真实下载、首帧、播放/暂停、20 次双向逐帧、20 次定位、波形、标记、852×480 截图与关闭。
- 上述在线探针记录：逐帧 p95 14ms、已缓存定位 p95 58ms、UI 最大间隔 65ms、关闭 5ms。它们是本机单次测量，不继承 Windows D3D11 性能承诺，也不是所有设备的性能保证。
- 最终 DMG 的匿名 DASH 探针同样为 25/25：逐帧 p95 21ms、已缓存定位 p95 68ms、UI 最大间隔 87ms、关闭 24ms。已直接运行挂载后的 App 验证本地 720p 画面、双向逐帧和 PNG 截图。
- 最终 App 检查 69 个 Mach-O 文件，依赖问题为 0；ad-hoc 签名验证通过，ZIP 检查 217 个成员，DMG 已实际挂载。附件校验值见 `SHA256SUMS`。

### 未覆盖与限制

- 未验证 Intel Mac、Universal Binary、其他 GPU 或所有 macOS 版本；本次环境为 macOS 26.4 arm64。
- 软件解码/绘制不保证 4K/60fps；HDR、10-bit、VFR 和长时间运行不承诺与 Windows 相同结果。
- 真实手机扫码登录未验证；接口、清晰度和服务可用性受 B 站、网络与账号权限影响。
- 不绕过付费、DRM 或地区限制；不包含账号、Cookie 或视频内容。

### 许可与源码

原作者为 HappinessJo。原项目与本 Fork 的源码采用 MIT；历史 GPL 授权不受影响。Qt、FFmpeg、SoundTouch 等第三方组件按各自许可分发。FFmpeg 使用官方 9.0.2 源码的最小 LGPL 动态构建，不使用 Homebrew 的 GPL-enabled 构建。许可与源码获取说明随 App 提供，构建入口为 `scripts/build-ffmpeg-macos.sh`、`scripts/build-macos.sh` 和 `scripts/package-macos.sh`。

## English

A native macOS port of the MIT-licensed [HappinessJo/lapian-player](https://github.com/HappinessJo/lapian-player), preserving the original author and Windows implementation. This is a source port, not a decompiled Windows package.

### Download and installation

- `Lapian-0.1.0-macos.1-macos-arm64.dmg`: open it and drag `Lapian.app` to Applications.
- `Lapian-0.1.0-macos.1-macos-arm64.zip`: extract somewhere writable and run the app.
- `SHA256SUMS`: hashes for both assets. GitHub's automatic Source code archives are source bundles, not installable apps.

This is a native Apple Silicon (arm64) build. It is ad-hoc signed, not notarized by Apple, and has no Developer ID signature. Verify the source and checksum, then right-click the app in Finder and choose Open. Do not disable system-wide security protections.

### What changed

- Qt Widgets rendering of FFmpeg software frames, CoreAudio / AudioUnit output and sample clock.
- libcurl API and CDN Range transfers, macOS Keychain credential storage, POSIX file I/O and shared read locks.
- Frame stepping, exact seeks, scrub previews, waveforms, A/B looping, tempo control, marks, notes and source-resolution snapshots.
- Fixed preview warm-up coverage, anonymous API compression, CDN request headers, HTML error pages entering media caches, and a focus-callback use-after-free during window destruction.
- Chinese and English READMEs, macOS build/package scripts and a reproducible minimal LGPL FFmpeg 9.0.2 build.

### Verified on this Mac

- CTest: 35/35 passed. Four optional VFR, 10-bit and Windows hardware-decoding cases were skipped; those areas are not claimed as verified.
- Real local window: playback, forward/backward stepping, marks and a 1280×720 PNG snapshot.
- Anonymous public-video API probe: 38/38 checks passed for view, DASH and durl parsing. Available quality is determined by the service response.
- Anonymous DASH end-to-end probe: 25/25 checks passed for real download, first frame, play/pause, 20 bidirectional frame steps, 20 seeks, waveform, marks, an 852×480 snapshot and close.
- That run measured frame-step p95 at 14ms, cached-seek p95 at 58ms, maximum UI gap at 65ms and close at 5ms. These are one-Mac measurements, not Windows D3D11 results or a performance guarantee on other devices.
- The final mounted DMG also passed the 25/25 anonymous DASH probe: frame-step p95 21ms, cached-seek p95 68ms, maximum UI gap 87ms and close 24ms. Its local 720p display, bidirectional stepping and PNG snapshot were exercised directly.
- The final app passed checks on 69 Mach-O files with zero dependency problems, ad-hoc signature verification, 217 ZIP members and a real DMG mount. See `SHA256SUMS` for the exact asset hashes.

### Coverage and limitations

- No Intel or Universal Binary. Other GPUs and macOS versions are unverified; this build was tested on macOS 26.4 arm64.
- Software decoding/rendering does not guarantee 4K/60fps. HDR, 10-bit, VFR and long-running sessions do not inherit Windows results.
- Real phone QR login has not been verified. APIs, quality and availability depend on Bilibili, network conditions and account permissions.
- No payment, DRM or regional bypass. No accounts, cookies or video content are bundled.

### License and source

HappinessJo is the original author. The original project and this fork's source are MIT-licensed; earlier GPL grants remain valid. Third-party licenses still apply. FFmpeg is built as a minimal LGPL dynamic library from official 9.0.2 sources, not Homebrew's GPL-enabled build. Notices and source information are included in the app. Reproduce with `scripts/build-ffmpeg-macos.sh`, `scripts/build-macos.sh` and `scripts/package-macos.sh`.
