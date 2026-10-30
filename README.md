# 拉片软件 · lapian-player

[English README](README.en.md) · [原作者 / Upstream](https://github.com/HappinessJo/lapian-player) · [Releases](https://github.com/luoboruodai/lapian-player/releases)

**为 MAD / PV 拉片而生的逐帧审阅器，现已提供原生 macOS Apple Silicon 构建。**

本地逐帧 · B 站在线播放与拖动预览 · A-B 循环 · 标记 / 备注 / 截图

[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-macOS%20arm64%20%2B%20Windows%20x64-blue.svg)](#平台与下载)
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](#从源码构建)
[![Qt](https://img.shields.io/badge/Qt-6.12.0-41CD52.svg)](#从源码构建)
[![FFmpeg](https://img.shields.io/badge/FFmpeg-9.0.2%20LGPL-007808.svg)](licenses/THIRD-PARTY-NOTICES.md)

> 把一支 MAD / PV 拆到帧：看剪辑点、转场、构图和踩点，然后记下来。
> 本仓库是 [HappinessJo/lapian-player](https://github.com/HappinessJo/lapian-player) 的 Fork。
> 原作者是 **HappinessJo**；本 Fork 增加 macOS 原生适配、构建发布流程和中英文文档。

## 平台与下载

| 平台 | 当前交付 | 说明 |
|---|---|---|
| macOS Apple Silicon | `Lapian-*-macos-arm64.dmg` / `.zip` | 原生 arm64，本机实际构建和验证 |
| Windows 10/11 x64 | 上游安装器与便携包 | 继续使用原作者的 D3D11 / WinHTTP / WASAPI 实现 |

macOS 安装：

1. 下载 DMG，打开后将 `Lapian.app` 拖入“应用程序”；也可使用 ZIP。
2. 当前包为 **ad-hoc 签名，未经 Apple 公证**。首次运行请核对 Release 中的 SHA-256，
   然后在 Finder 中右键 App →“打开”。不要关闭系统完整安全性设置。
3. 不支持 Intel Mac，也不包含 Universal Binary。其他 macOS 版本和硬件尚未逐一验证。

GitHub 自动生成的 **Source code** 是源码归档，不是可安装应用；请使用上述 DMG/ZIP 附件。

## 功能

### 本地视频

- `PgUp` / `PgDn` 或方向键逐帧；帧号与 `HH:MM:SS:FF` 时间码定位。
- 时间轴缩放、拖动预览，松手精确落帧。
- 0.25–2x 变速，A-B 循环和单帧定帧。
- 音频波形、音量、静音、全屏。
- 标记、备注、源分辨率 PNG 截图及 sidecar 持久化。

### B 站在线

- 输入 BV 号或链接，解析后播放并缓存，支持拖动预览和逐帧。
- DASH 清晰度、缓存命中、A/B 工作区记忆。
- 官方网页二维码登录；SESSDATA 安全保存在系统凭据存储中。
- 只处理公开内容与用户自带凭据，不绕过付费、DRM 或地区限制。

## 快速开始

1. `Cmd+O`（界面也显示 `Ctrl+O`）打开本地视频；`Cmd/Ctrl+U` 打开 B 站链接。
2. `Space` 播放/暂停，`PgUp/PgDn` 逐帧。
3. `I` / `O` 设置 A/B，`L` 开关循环，`Shift+L` 清除。
4. `W` 切换波形，`M` 静音，`S` 截图，`Ctrl/Cmd+M` 添加标记。
5. `[` / `]` 调整速度，`F` 或双击画面进入全屏。

数据位置：

- 标记和备注：优先视频旁的 `<视频名>.lapian.json`；不可写时回退到
  `~/Library/Application Support/拉片软件/marks`。
- 在线缓存：`~/Library/Caches/拉片软件/cache`，可在设置中修改或清空。
- 凭据：macOS 钥匙串；不会以明文写入标记文件。
- 删除 App 不会自动删除设置、记录、截图或缓存。

## macOS 适配实现

- **视频**：FFmpeg 软件解码 + Qt `QImage` / `QPainter`，保留旋转和窗口适配。
- **音频**：CoreAudio / AudioUnit，保留音频主时钟、变速与波形。
- **网络**：libcurl，保留 HTTPS、证书校验、Range 续传、取消与无重定向约束。
- **凭据**：macOS 钥匙串；CDN 不发送 SESSDATA。
- **文件与缓存**：POSIX 文件接口和共享读锁，正在读取的缓存不会被误淘汰。
- **打包**：CMake + Ninja + macdeployqt，生成独立 `.app`、DMG 和 ZIP。

平台决策记录见 [ADR-008](docs/adr/ADR-008-macos-port.md)。

## 从源码构建

macOS 依赖：Homebrew、Apple Clang、CMake、Ninja、Qt、SoundTouch、libcurl。
打包检查另需 Python 3。当前发布构建使用 Apple Clang 21、Qt 6.12.0、FFmpeg 9.0.2、
SoundTouch 2.4.1、libcurl 8.23.0。FFmpeg 由脚本从官方源码构建最小 LGPL 动态库，
不使用 Homebrew 中带 GPL 组件的 FFmpeg 构建。

```bash
git clone https://github.com/luoboruodai/lapian-player.git
cd lapian-player
brew install cmake ninja qt sound-touch curl python

./scripts/build-macos.sh
./scripts/package-macos.sh
```

产物位于 `build/macos-dist/`。`BUILD_DIR`、`VERSION` 和 `MACDEPLOYQT` 可通过环境变量覆盖。
发布前请在本机执行测试并核对 App 的依赖路径、签名和 DMG 挂载结果。

Windows 的原始构建方式和 Visual Studio 2022 工具链见
[原作者仓库](https://github.com/HappinessJo/lapian-player) 与 [docs/deps.md](docs/deps.md)。

## 验证与已知限制

本次 macOS 验证包括真实窗口播放、双向逐帧、标记、720p PNG 截图，以及
**35/35 CTest**（另有 4 个可选 VFR/10-bit/Windows 硬解用例跳过）。
匿名公开视频接口探针 **38/38**、DASH 端到端探针 **25/25** 通过，覆盖真实下载、
首帧、逐帧、定位、波形、标记、852×480 截图与关闭。
Warm-only 冷启动预热覆盖 `[frame−61, frame+10]`。
详见[本版本发布说明](docs/release-2026-10-30-macos-arm64.md)。

- 仅提供 Apple Silicon 构建；未验证 Intel、其他 GPU、所有 macOS 版本。
- 软件解码与绘制不继承 Windows D3D11 的性能承诺；4K/60fps 不做保证。
- 安装包未经 Apple 公证；没有 Developer ID 签名。
- 在线清晰度和服务可用性受 B 站接口、网络和账号权限影响；真实扫码登录需要用户手机复核。
- 可选 VFR、10-bit 和部分 Windows 专属探针未覆盖；这些范围没有写成通过。
- 不承诺所有视频格式、所有环境或长时间运行都完全没有问题。

## 参与贡献与署名

原作者：**HappinessJo**。本 Fork 的 macOS 适配在其 MIT 许可项目基础上完成。
欢迎通过 Issue / PR 提供 macOS 实机结果、问题复现步骤、构建修复和双语文档改进。

流程：Fork → 分支 → 修改 → `ctest` → PR。不要提交 `build/`、`third_party/`、
视频素材、账号、Cookie 或任何凭据。

## 免责声明

仅用于个人学习、技术研究与影视拉片。本项目不提供、不托管、不分发视频内容，也不自带
账号或 Cookie。使用在线功能时请遵守 B 站用户协议与相关版权法律，不要用于批量下载、
二次分发或其他侵权用途。

## 许可证

以 **MIT** 发布，见 [LICENSE](LICENSE)，保留 HappinessJo 的原始版权声明。
历史版本 v0.1.0 及更早的 GPL 授权不受影响。

第三方组件仍按各自许可分发：Qt（LGPL-3.0）、FFmpeg（LGPL-2.1+）、
SoundTouch（LGPL-2.1）、libcurl（curl/X11）、qrcodegen（MIT）。
请保留 `licenses/`、`licenses/SOURCE-OFFER.md` 与 `licenses/SOURCE-OFFER-MAC.md`。
