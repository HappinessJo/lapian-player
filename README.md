# 拉片软件（lapian-player）

Windows 上的**帧级拉片 / 逐帧审阅器**：本地视频与 B 站在线视频都能逐帧看、设 A-B 循环、做标记备注截图；
在线拖动预览支持 1920×1080 与可配置缓存（128–4096 MiB）。

> 仅供个人学习与研究使用。请遵守 B 站用户协议与版权法；本项目**不附带任何视频内容、账号或凭据**。

---

## 功能

**本地视频**
- 精确 seek、逐帧步进（±1）、长按连续步进、帧号 / `HH:MM:SS:FF` 跳转
- 0.25–2x 变速（SoundTouch 音高保持）、A-B 循环（A=B 时单帧定帧）、波形
- 标记 / 备注 / 截图（sidecar 持久化，可回退 AppData）
- 全屏、4 档 DPI、深浅一致的 UI v2 主题

**在线视频（B 站）**
- BV 号 / 链接解析、DASH 1080P、缓存命中时 0 网络播放
- 增长会话（边下边播）与缓存命中的拖动预览，前进/后退同口径
- 清晰度切档、扫码登录（SESSDATA 经 DPAPI 加密存储，日志脱敏）

**预览**
- 1080P 源拖动预览输出 **1920×1080**（旧版固定 1024×576）
- 预览缓存 128–4096 MiB，默认「自动 + 512 MiB」，同时受**可用内存 25%** 约束
- 工作区 A/B **按视频记忆**（循环开关仍只在本次会话内保持）

## 系统要求

- Windows 10 / 11 x64（依赖 D3D11、WinHTTP、WASAPI）
- 源码构建：Visual Studio 2022（MSVC x64）+ 脚本拉取的 Qt / FFmpeg / SoundTouch / CMake

## 获取

- **安装版 / 便携版**：到仓库 Releases 下载 `Lapian-*-setup.exe` 或 `Lapian-*-portable.zip`。
  未做代码签名，首次运行会有 SmartScreen 提示。
- **源码构建**：见下。

## 构建

依赖不入库，全部由脚本拉取/构建（详见 [docs/deps.md](docs/deps.md)）：

```powershell
git clone https://github.com/<your-name>/lapian-player.git
cd lapian-player

powershell -ExecutionPolicy Bypass -File scripts\fetch-cmake.ps1
powershell -ExecutionPolicy Bypass -File scripts\fetch-qt.ps1
powershell -ExecutionPolicy Bypass -File scripts\build-ffmpeg.ps1
powershell -ExecutionPolicy Bypass -File scripts\build-soundtouch.ps1
powershell -ExecutionPolicy Bypass -File scripts\configure.ps1
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1

ctest --test-dir build\release -C Release --output-on-failure
```

- 当前测试基线：**38/38**（含 timeline 188 项、mark store 281 项、在线预览/缓存/QR/故障等）。
- 打包：`scripts\build-release.ps1 -Clean -Package`。

## 使用

打开软件后 `Ctrl+O` 打开本地视频，`Ctrl+U` 输入 BV 号打开在线视频。

| 快捷键 | 作用 |
|---|---|
| `Space` | 播放 / 暂停 |
| `PgUp` / `PgDn` | 上一帧 / 下一帧 |
| `I` / `O` | 设入点 A / 出点 B |
| `L` / `Shift+L` | 循环开关 / 清除 A-B |
| `M` | 静音 |
| `S` | 截图 |
| `Ctrl+O` / `Ctrl+U` | 打开本地 / 在线 |
| `Ctrl+,` | 设置（缓存目录、配额、拖动预览分辨率与缓存） |

- 在时间轴上按住拖动 = 拖动预览；松手精确落到目标帧。
- 设置里的「拖动预览」修改后**下次打开在线会话**生效（不热切换）。
- 工作区 A/B 会按视频自动记住；重开同一视频恢复，循环仍默认关闭。

## 项目结构

```
src/app       MainWindow、UI v2（Theme/PlayerShell/TopBar/TransportBar/...）、测试 runner
src/core      设置、标记/工作区持久化、时间轴模型、帧数学
src/media     解码、seek、帧索引/缓存、预览解码器（本地 + 在线）
src/online    B 站解析、DASH、缓存、增长读层、扫码、WinHTTP
src/platform  D3D11 渲染与交换链
src/audio     WASAPI、波形、变速
tests         38 项单元/集成测试
scripts       依赖拉取、构建、打包、基准
docs          ADR、依赖说明、规格增量、性能证据
licenses      GPL / LGPL / 第三方许可证与源码获取说明
```

架构决策见 [docs/adr](docs/adr)；产品规格见 [spec.md](spec.md)、[docs/spec-v1.0-delta.md](docs/spec-v1.0-delta.md)。

## 性能与验证

以下数字来自开发机（1080P 缓存命中、60 Hz 目标、同跨度正反拖动），仅供参考：

- 1s/60 双向：presented 比 **1.018**、双方 ~59fps、P6 38–83ms
- 2s/120（512MiB+）：由 0.877 提升到 **1.009–1.045**
- 长跨度：10s@2GiB **1.021**、20s@4GiB **1.013–1.021**（峰值 3.62GiB，≤ 配置+16MiB）
- 证据：[docs/perf/online-drag-parity.md](docs/perf/online-drag-parity.md)、
  [docs/perf/preview-cache-long-span.md](docs/perf/preview-cache-long-span.md)、
  [docs/perf/preview-config-matrix.md](docs/perf/preview-config-matrix.md)

## 已知限制

- 仅支持 Windows（D3D11 / WinHTTP / WASAPI）。
- 在线清晰度受账号权限限制；未登录通常只能低清晰度。
- 4 GiB 是**绝对上限**，实际生效还受「可用内存 25%」约束（可用内存 <16 GiB 时会被夹低）。
- 约 40 秒以上的连续回拖超出现有缓存上限（2400 帧 ≈ 6.95 GiB），只做记录、不保证覆盖。
- 没有 60fps 素材的正式验收；4K/60 在线未覆盖。
- 安装包未签名。

## 免责声明

本项目仅用于**个人学习、技术研究与影视拉片**。它不提供、不托管、不分发任何视频内容；不自带账号或 Cookie。
使用在线功能时请自行确保**遵守 B 站用户协议与相关版权法律**，不要用于批量下载、二次分发或其他侵权用途。
因使用本项目产生的一切后果由使用者自行承担。

## 许可证

本项目以 **GPL-3.0** 发布，见 [LICENSE](LICENSE)。
第三方组件（Qt LGPL-3.0、FFmpeg LGPL-2.1+、SoundTouch LGPL-2.1、qrcodegen MIT）的许可证与源码获取方式见
[licenses/THIRD-PARTY-NOTICES.md](licenses/THIRD-PARTY-NOTICES.md) 与 [licenses/SOURCE-OFFER.md](licenses/SOURCE-OFFER.md)。
