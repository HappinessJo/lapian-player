# LGPL 组件源码获取说明（written offer）

拉片软件在运行期动态链接以下 LGPL 组件，并按 LGPL 的要求提供对应源码的获取方式。
**本项目未修改这些组件的源码。**

## Qt 6.8.3（LGPL-3.0）

- 二进制来源：Qt 官方在线仓库（`https://download.qt.io/online/qtsdkrepository/`），
  本项目使用 `msvc2022_64` 构建，仅 Core/Gui/Widgets 与 `qttools` 中的部署工具。
- 源码：`https://download.qt.io/official_releases/qt/6.8/6.8.3/submodules/`
  以及 `https://code.qt.io/cgit/qt/`（按模块 tag，例如 `v6.8.3`）。
- 许可文本：见 `licenses/LGPL-3.0.txt` 与 `licenses/GPL-3.0.txt`。

## FFmpeg 7.1.x（LGPL-2.1-or-later，最小构建）

- 二进制来源：本项目 `scripts/build-ffmpeg.ps1` 从 FFmpeg 官方源码自行构建，
  configure 选项见 `docs/deps.md`；产物不启用 GPL/Nonfree 组件。
- 源码：`https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz`（对应版本的官方发布包）。
- 构建配置：随发布包提供 `docs/deps.md` 中的完整 configure 选项，便于任何人用
  相同源码复现二进制。
- 许可文本：见 `licenses/LGPL-2.1.txt`。

## 获取承诺

自本版本发布起三年内，任何收到本软件二进制分发的个人或组织，均可通过上述公开
地址获取对应源码；如需离线副本，可联系发布者索取（不收取超出介质与邮寄成本的
费用）。替换上述 DLL 不需要修改本软件。

> 发布时应把本文件、`licenses/` 全文与安装包放在一起；安装包与便携 zip 均需包含。
