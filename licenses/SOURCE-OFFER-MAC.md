# macOS LGPL / 第三方组件源码获取说明

本 macOS 发布保留原项目 MIT 许可。以下组件以动态链接方式随 App 分发；本项目未修改其源码。
组件版本以本次发布构建为准，构建入口为 `scripts/build-macos.sh` 与 `scripts/package-macos.sh`。

## Qt 6.12.0（LGPL-3.0）

- 来源：Homebrew `qt` / `qtbase` bottle，使用 Core/Gui/Widgets 及 macdeployqt。
- 源码与模块：`https://code.qt.io/cgit/qt/`（对应 6.12.0 tag）以及 `https://download.qt.io/official_releases/qt/6.12/6.12.0/submodules/`。
- 构建定义：`https://github.com/Homebrew/homebrew-core/blob/master/Formula/q/qtbase.rb`。
- 许可：`licenses/LGPL-3.0.txt`、`licenses/GPL-3.0.txt`。

## FFmpeg 9.0.2（LGPL-2.1-or-later）

- 来源：FFmpeg 官方源码，使用 `scripts/build-ffmpeg-macos.sh` 构建最小动态库。
- 仅启用本地文件/自定义读层所需的 mov、matroska 解封装与指定解码器；不启用 GPL、nonfree、x264 或 x265。
- 源码：`https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz` 与 `https://git.ffmpeg.org/ffmpeg.git`。
- 源码 SHA-256：`8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`。
- 构建定义：`scripts/build-ffmpeg-macos.sh`；构建输入与 `build/macos-release` 对应。
- 许可：`licenses/LGPL-2.1.txt`、`licenses/FFmpeg-LICENSE.md`。

## SoundTouch 2.4.1（LGPL-2.1）

- 来源：Homebrew `sound-touch` bottle，动态链接 `libSoundTouch.1.dylib`。
- 源码：`https://www.surina.net/soundtouch/` 与 `https://codeberg.org/soundtouch/soundtouch`。
- 构建定义：`https://github.com/Homebrew/homebrew-core/blob/master/Formula/s/sound-touch.rb`。
- 许可：`licenses/SoundTouch-COPYING.txt`。

## libcurl 8.23.0（curl/X11 许可）

- 来源：Homebrew `curl` bottle，动态链接 `libcurl.4.dylib`。
- 源码：`https://curl.se/download/curl-8.23.0.tar.gz` 与 `https://github.com/curl/curl`。
- 构建定义：`https://github.com/Homebrew/homebrew-core/blob/master/Formula/c/curl.rb`。
- 许可：`https://curl.se/docs/copyright.html`。

## 运行时依赖与系统信任

Qt、libcurl 及图像格式插件的动态依赖保留 Homebrew 对应包的原始许可，位于
`licenses/homebrew/`。其上游源码和构建定义可通过 `https://github.com/Homebrew/homebrew-core`
中对应 Formula 及版本获取；本 App 不包含 Homebrew 的 GPL-enabled FFmpeg 或 QML/虚拟键盘组件。
libcurl 使用 Apple SecTrust 与系统信任回退，不依赖 Homebrew 的 CA 文件路径。

## qrcodegen（MIT）

- 来源：原仓库 vendored qrcodegen；版权与许可保留在 `licenses/qrcodegen.txt`。

## 获取承诺

自本版本发布起三年内，任何收到本 macOS 二进制分发的个人或组织均可通过上述公开地址
获取对应源码和构建定义。发布包同时包含本说明及全部许可文本。替换动态库无需修改应用源码，
但仍须遵守各组件的许可与兼容性要求。
