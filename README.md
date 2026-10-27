<div align="center">

# 拉片软件 · lapian-player

**为 MAD / PV 拉片而生的 Windows 帧级审阅器**

本地逐帧 · B 站在线播放与拖动预览 · A-B 循环 · 标记 / 备注 / 截图

[![License](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-0078D4.svg)](#系统要求)
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](#从源码构建)
[![Qt](https://img.shields.io/badge/Qt-6.8.3-41CD52.svg)](#从源码构建)
[![FFmpeg](https://img.shields.io/badge/FFmpeg-7.1.1%20LGPL-007808.svg)](licenses/THIRD-PARTY-NOTICES.md)

[下载](#下载即用) · [快速开始](#快速开始) · [快捷键](#快捷键) · [参与贡献](#参与贡献)

</div>

---

> 把一支 MAD / PV 拆到帧：**看剪辑点、看转场、看构图、看踩点**，然后记下来。

## 为什么做这个

MAD / PV 的功力常常就在 **1–3 帧**之间：切点早一帧还是晚一帧、转场是硬切还是白闪、
构图从中心推到三分线、踩点是鼓点还是反拍、作画在哪一帧开始变形。

普通播放器只能「看个大概」——时间轴按秒跳、暂停有延迟、来回拖会卡。
这个工具的目标是把这些动作变得**可停、可回、可对比、可记录**：

- 逐帧停住，对着参考图看构图；
- A-B 循环 1–2 秒，0.25–0.5x 反复看节奏与作画；
- 把「这里切早了 1 帧」「这个转场是 2 帧白闪」写成标记/备注/截图，留在视频旁边。

它也支持 **B 站在线视频**：不用先下载，直接输入 BV 号就能播、能拖、能逐帧看。

## 它能做什么

### 看 MAD / PV（本地视频）

| 能力 | 说明 |
|---|---|
| 逐帧 | `PgUp` / `PgDn` ±1 帧，长按连续步进；帧号 / `HH:MM:SS:FF` 直接跳 |
| 精确 seek | 松手即落到目标帧（保证档 P6 38–83ms） |
| 变速 | 0.25–2x，音高保持；0.25–0.5x 看节奏，1x 看整体 |
| A-B 循环 | 选 1–2 秒反复看；A=B 时单帧定帧，逐帧对比 |
| 波形 / 时间轴 | 波形对齐鼓点、反拍、台词点 |
| 标记 / 备注 / 截图 | sidecar 持久化（写不进视频目录时自动回退 AppData） |
| 全屏 / 4 档 DPI | 100%–200% 缩放都可用 |

### B 站在线（在线播放 + 拉片）

| 能力 | 说明 |
|---|---|
| 输入即播 | 粘贴 BV 号 / 链接，解析后直接播放（DASH 1080P） |
| 缓存命中 0 网络 | 看过的内容再次打开不再请求网络 |
| 在线拖动预览 | 1080P 源预览输出 **1920×1080**；前进/后退同口径 |
| 走动预览缓存 | 128–4096 MiB 可调，默认「自动 + 512 MiB」，同时受可用内存 25% 约束 |
| 清晰度切档 | 缓存命中也能拉取服务端档位目录 |
| 扫码登录 | SESSDATA 经 DPAPI 加密存储，日志脱敏 |
| 工作区记忆 | A/B 按视频记住，重开自动恢复（循环仍默认关闭） |

## 界面

三段式：**顶栏**（文件 / 模式 / 清晰度 / 设置）· **画面**（D3D11 渲染）· **右侧检查器**（标记 / 截图 / 信息）
+ 底部传输条与时间轴（视图窗 / 工作区两条滑块、波形、播放头）。

视觉规范见 [docs/ui-v2-visual-reference.md](docs/ui-v2-visual-reference.md)；
交互原型见 [docs/ui-v2-proto/index.html](docs/ui-v2-proto/index.html)。

> 截图区欢迎贡献：提交一张**你自己的 MAD / PV 拉片截图**即可（请确保你有权使用该画面）。
> 我们不内置任何视频素材。

## 下载即用

到仓库的 **Releases** 页面下载：

- `Lapian-*-setup.exe`：每用户安装，无需管理员
- `Lapian-*-portable.zip`：解压即用

> 未做代码签名，首次运行会有 SmartScreen 提示。

## 快速开始

1. 打开软件，`Ctrl+O` 选一个本地 MAD / PV；或 `Ctrl+U` 粘贴 B 站 BV 号。
2. 时间轴上拖播放头 = 拖动预览，松手精确落帧。
3. `I` / `O` 设入出点，`L` 开循环，0.25–0.5x 反复看。
4. `S` 截图、右侧加标记/备注。
5. 想看更长的回拖范围：`Ctrl+,` → 「拖动预览」→ 分辨率 1080P、缓存 1 GiB（下次打开在线会话生效）。

## 系统要求

- Windows 10 / 11 x64（依赖 D3D11、WinHTTP、WASAPI）
- 源码构建：Visual Studio 2022（MSVC x64）

## 从源码构建

依赖不入库，全部由脚本拉取/构建（见 [docs/deps.md](docs/deps.md)）：

```powershell
git clone https://github.com/HappinessJo/lapian-player.git
cd lapian-player

powershell -ExecutionPolicy Bypass -File scripts\fetch-cmake.ps1
powershell -ExecutionPolicy Bypass -File scripts\fetch-qt.ps1
powershell -ExecutionPolicy Bypass -File scripts\build-ffmpeg.ps1
powershell -ExecutionPolicy Bypass -File scripts\build-soundtouch.ps1
powershell -ExecutionPolicy Bypass -File scripts\configure.ps1
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1

ctest --test-dir build\release -C Release --output-on-failure
```

- 依赖版本：Qt 6.8.3 / FFmpeg 7.1.1（LGPL、D3D11VA）/ SoundTouch 2.3.3 / CMake 3.31.6
- 测试基线：**38/38**（含 timeline 188 项、mark store 281 项、在线预览/缓存/QR/故障等）
- 打包：`scripts\build-release.ps1 -Clean -Package`

## 快捷键

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

## 性能与验证

数字来自开发机（1080P 缓存命中、60 Hz 目标、同跨度正反拖动），仅供参考：

| 场景 | 结果 |
|---|---|
| 1s / 60 双向 | presented 比 **1.018**，双方 ~59fps，P6 38–83ms |
| 2s / 120（512MiB+） | 由 0.877 提升到 **1.009–1.045** |
| 10s @ 2GiB | **1.021**（峰值 1839MiB） |
| 20s @ 4GiB | **1.013–1.021**（峰值 3.62GiB，≤ 配置+16MiB） |
| 1080P 预览清晰度 | 1920×1080；同显示尺寸边缘能量为 1024 的 **6.4×** |

证据与测量方法：[online-drag-parity](docs/perf/online-drag-parity.md) ·
[preview-cache-long-span](docs/perf/preview-cache-long-span.md) ·
[preview-config-matrix](docs/perf/preview-config-matrix.md)

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

架构决策见 [docs/adr](docs/adr)；产品规格见 [spec.md](spec.md) 与 [docs/spec-v1.0-delta.md](docs/spec-v1.0-delta.md)。

## 已知限制

- 仅支持 Windows（D3D11 / WinHTTP / WASAPI）。
- 在线清晰度受账号权限限制；未登录通常只能低清晰度。
- 4 GiB 是缓存**绝对上限**，实际生效还受「可用内存 25%」约束（可用内存 <16 GiB 时会被夹低）。
- 约 40 秒以上的连续回拖超出现有缓存能力（2400 帧 ≈ 6.95 GiB），只记录、不保证覆盖。
- 没有 60fps 素材的正式验收；4K/60 在线未覆盖。
- 安装包未签名。

## 参与贡献

这是一个**欢迎所有人参与**的项目——不管你是写代码的，还是只是喜欢看 MAD / PV。

**你可以怎么帮：**

- 🐛 **报 Bug**：写清「本地还是在线、素材分辨率/帧率、拖动跨度、现象」。能附日志关键词（`P6`/`P4`/`cache_peak`/`ui_gap`）更好。
- 📊 **性能数据**：不同 CPU/GPU、不同素材（H.264/HEVC、30/60fps、长 GOP）下的拖动帧率与内存，都是宝贵数据。
- 🎨 **使用案例**：用你的 MAD / PV 拉片流程写一篇「我怎么用它拆转场/踩点」，或者贡献一张截图。
- 📝 **文档 / 翻译**：ADR、使用说明、English README、术语表。
- 💻 **代码**：解码/渲染/UI/在线，先看 [CONTRIBUTING.md](CONTRIBUTING.md) 与 [AGENTS.md](AGENTS.md)（工程约定）。

**流程：** Fork → 新建分支 → 改 → `ctest` 全绿 → PR。
**不要提交**：`build/`、`third_party/`、视频素材、任何账号/凭据（`.gitignore` 已覆盖）。
**好上手的起点**：给 README 补截图、补 English 版、把 `docs/perf` 的测量脚本跑一遍并贴结果——这些都算贡献。

## 免责声明

本项目仅用于**个人学习、技术研究与影视拉片**。它不提供、不托管、不分发任何视频内容，也不自带账号或 Cookie。
使用在线功能时请自行确保**遵守 B 站用户协议与相关版权法律**，不要用于批量下载、二次分发或其他侵权用途。
因使用本项目产生的一切后果由使用者自行承担。

## 许可证

以 **GPL-3.0** 发布，见 [LICENSE](LICENSE)。
第三方组件（Qt LGPL-3.0、FFmpeg LGPL-2.1+、SoundTouch LGPL-2.1、qrcodegen MIT）的许可证与源码获取方式见
[licenses/THIRD-PARTY-NOTICES.md](licenses/THIRD-PARTY-NOTICES.md) 与 [licenses/SOURCE-OFFER.md](licenses/SOURCE-OFFER.md)。
