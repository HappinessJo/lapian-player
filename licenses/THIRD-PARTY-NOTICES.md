# 第三方组件与许可（首期）

本文件记录拉片软件在构建与分发中用到的第三方组件、许可类型与合规义务。
完整许可文本见 `licenses/`；LGPL 组件的源码获取说明见 `SOURCE-OFFER.md`。

> 原则（ADR-004）：不引入 GPL/AGPL 组件；LGPL 组件一律动态链接；随包附许可文本
> 与源码获取说明；H.264/H.265 专利许可按“随包分发、不额外付费”处理并在发布说明
> 中记录风险。

## 运行期组件（随包分发）

| 组件 | 版本 | 许可 | 链接方式 | 许可文本 |
|---|---|---|---|---|
| Qt（Core/Gui/Widgets） | 6.8.3 | LGPL-3.0 / GPL-3.0 双许可，本项目按 LGPL-3.0 使用 | 动态链接（DLL） | `licenses/LGPL-3.0.txt`、`licenses/GPL-3.0.txt` |
| FFmpeg（avformat/avcodec/avutil/swscale/swresample） | 7.1.x（最小构建，LGPL，`--disable-gpl`） | LGPL-2.1-or-later | 动态链接（DLL） | `licenses/LGPL-2.1.txt` |
| Visual C++ 运行库（vcruntime140、msvcp140 等） | VS 2022 17.14 | Microsoft 可再发行组件条款 | 动态链接 | 随安装包附微软再发行许可 |
| qrcodegen（Project Nayuki QR-Code-generator，vendored 源码） | commit `3c6d0b3`（2026-08-31） | MIT | 静态编译进主程序（无新增 DLL） | `licenses/qrcodegen.txt` |

## 仅构建期组件（不随包分发）

| 组件 | 版本 | 许可 | 用途 |
|---|---|---|---|
| Catch2 | v3.7.1 | BSL-1.0 | 单元测试 |
| CMake | 3.31.6（便携） | BSD-3-Clause | 构建 |
| aqtinstall | 3.3.0 | MIT | 构建期下载 Qt |
| Python | 3.14.2 | PSF-2.0 | 构建脚本 |
| Inno Setup | 6.7.3 | Inno Setup License | 编译 Windows 安装包（不随包分发；商业用户按其许可请求购买） |

## 系统组件（不随包分发）

D3D11 / DXGI / D3D11VA / WASAPI / WIC 均来自 Windows SDK，随操作系统提供，
不属于再分发内容。

## 合规要点

1. **动态链接**：Qt 与 FFmpeg 均以 DLL 形式随包分发，用户可用自己的同名 DLL 替换；
   安装包不包含静态库。
2. **无 GPL 污染**：FFmpeg 最小构建不启用 `--enable-gpl`、`--enable-nonfree`；
   不链接 libx264 等 GPL 组件。打开文件只用 file 协议。qrcodegen 为 MIT，
   仅本地渲染登录二维码，不联网、不读取远程资源。
3. **许可文本随包**：安装包与便携 zip 内包含 `licenses/` 全文与 `SOURCE-OFFER.md`。
4. **源码可获取**：Qt 与 FFmpeg 的对应版本源码地址见 `SOURCE-OFFER.md`；
   本项目未修改这两个组件的源码。
5. **专利风险**：H.264/H.265 解码涉及专利池许可；首期按“随包分发、不额外付费”
   处理，风险记录在 `plan.md` R14 与发布说明中。
