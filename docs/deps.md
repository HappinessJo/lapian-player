# 依赖与构建环境（T0.1）

> 本文件记录首期实际锁定的工具链、来源与已知问题。**运行期零网络**；构建期允许联网。
> 版本或路径变化时，必须同步更新本文件，并重测 `P12 分发体积`。

## 1. 本机环境（2026-10-19 实测）

| 项 | 值 |
|---|---|
| 系统 | Windows 11 x64，10.0.26200（25H2 级别） |
| 显卡 | NVIDIA GeForce RTX 4060（D3D11 FL 11_1，硬件解码可用） |
| 磁盘 | E: 可用 330GB |
| 说明 | 本机不是 spec §4.1 的基线机（4C/8G/集显/SATA SSD），性能数字不能直接与 P1–P14 对比 |

## 2. 工具链锁定

| 组件 | 版本 | 位置 | 获取方式 |
|---|---|---|---|
| Visual Studio | 2022 Community 17.14.36623.8（MSVC 14.44.35207，Windows SDK 10.0.26100.0） | `E:\VS\C++` | 用户预装 |
| CMake | **3.31.6（便携）** | `third_party\cmake\3.31.6\cmake\data\bin\cmake.exe` | `scripts\fetch-cmake.ps1`（PyPI 轮子） |
| Qt | **6.8.3** msvc2022_64（qtbase + qttools） | `third_party\Qt\6.8.3\msvc2022_64` | `scripts\fetch-qt.ps1`（aqtinstall 3.3.0） |
| FFmpeg | **7.1.1 最小构建**（LGPL-2.1+、shared、`--disable-network`、D3D11VA） | `third_party\ffmpeg\build-msvc\ffmpeg-7.1.1-minimal`（8.6MB） | `scripts\build-ffmpeg.ps1` |
| Python | 3.14.2 | 系统 `py -3` | 用户预装 |
| Catch2 | 推迟到 T1.1 | — | 见 §6 |
| MSYS2 | 2026-10 基础包 + make 4.4.1 / nasm 3.02 | `E:\msys64` | `scripts\build-ffmpeg.ps1` 自动下载（TUNA 镜像） |

> **为什么 CMake 不用系统自带的 4.2.0**：本机 CMake 4.2.0 在 configure 阶段直接崩溃
> （`0xC0000409`）；3.31.6 可正常工作。见 §5。

## 3. 目录布局

```
third_party/            构建依赖（.gitignore，不入库）
  Qt/6.8.3/msvc2022_64/
  ffmpeg/build-msvc/ffmpeg-7.1.1-minimal/   最小构建（include/ lib/ bin/）
  cmake/3.31.6/
build/                  CMake 构建目录（.gitignore）
dist/                   打包产物（.gitignore）
E:\msys64               MSYS2（仅构建 FFmpeg 用）
E:\ffmpeg-build         FFmpeg 源码/中间产物/install
```

## 4. 构建入口

```powershell
# 首次：拉 CMake、Qt、FFmpeg 最小构建（需要网络）
powershell -ExecutionPolicy Bypass -File scripts\fetch-cmake.ps1
powershell -ExecutionPolicy Bypass -File scripts\fetch-qt.ps1
powershell -ExecutionPolicy Bypass -File scripts\build-ffmpeg.ps1

# 一键 Release 构建 + 测试 + 打包 + 自检 + 体积
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1 -Clean
```

产物：`dist\lapian.exe`（空窗口）、`dist\lapian_env_check.exe`（控制台自检）。

## 5. 本机已知问题（会咬人）

1. **PowerShell 5.1 按 ANSI 读脚本**：`scripts\*.ps1` 必须保存为 UTF-8 **带 BOM**，否则中文注释会破坏解析。编辑脚本后要重加 BOM。
2. **执行策略**：未签名脚本默认被拦；运行前用 `-ExecutionPolicy Bypass`（或在当前进程 `Set-ExecutionPolicy -Scope Process Bypass`）。
3. **CMake 4.2.0 崩溃**；且 CMake 3.31 读取已有 `CMakeCache.txt` 重配时崩溃（中文路径 + Win11 26200）。脚本在配置前删除 `CMakeCache.txt` 规避，保留目标文件继续增量编译。
4. **CMake FetchContent 不可用**：本机 CMake 捕获 `git` 子进程管道会崩溃（`0xC0000409`），因此不引入 FetchContent；依赖一律用脚本预取到 `third_party\`。
5. **Python 3.14 的 `tempfile.mkdtemp`**：本机以 `0o700` 建目录后连本人也不可写，导致 pip/aqt 解包失败。`scripts\python\sitecustomize.py` 把 `mkdtemp` 改为 `0o777`（仅构建期，置于 `PYTHONPATH`）。
6. **aqtinstall 需要命名管道**（多进程下载）：在受限沙箱下会失败；需在放行（完全权限）模式下运行 `fetch-qt.ps1`。
7. **`windeployqt` 会多带 QtNetwork/TLS/网络插件与 D3dcompiler_47**：构建脚本在部署后删除，并由 `check-imports.ps1` 断言不存在。
8. **FFmpeg + MSVC 构建**（`scripts\build-ffmpeg.ps1` 已处理）：
   - 必须用 `bash -c` 而不是 `bash -lc`，否则 MSYS2 的 `/etc/profile` 重置 PATH，找不到 `cl`；
   - MSYS2 的 `/usr/bin/link.exe` 会遮蔽 MSVC 链接器，构建前改名；
   - `--disable-autodetect` 会关掉 D3D11VA，必须显式 `--enable-d3d11va`；
   - 中文 VS 的 `CC_IDENT` 写进 `config.h` 后会让 RC 编译器报 `RC2001`，configure 后改写为 ASCII（`VSLANG=1033` 对 cl 横幅无效）；
   - MSYS2 的 `make install` 把 `.lib/.def` 放在 `bin/`，脚本归位到 `lib/`（CMake 也同时兼容 `bin/`）；
   - **必须同时启用 `d3d11va2`**：只开 `d3d11va`（vld）时，自定义 `get_format` 选 `AV_PIX_FMT_D3D11` 会报 `Invalid pixfmt for hwaccel`；探针优先使用 `d3d11va2` 的 `AV_PIX_FMT_D3D11`；
   - **改变组件集后必须 `make clean`**：MSVC 的依赖跟踪可能不重建 `avcodec`，产出仍是旧 DLL；
   - `build\release\bin` 的 FFmpeg DLL 只在目标重新链接时由 POST_BUILD 复制；切换 FFmpeg 后要手动同步或重新构建。

## 6. 依赖策略

- **Qt**：只链接 Core/Gui/Widgets；不用 QML/WebEngine/Multimedia。版本锁 6.8 LTS 系列。
- **FFmpeg**：LGPL-2.1+、动态链接、最小构建，`--disable-network`，不启用 GPL/Nonfree；只保留 file 协议、mov/matroska 解封装、H.264/HEVC/VP9/AV1 与 AAC/MP3/Opus/PCM 解码、D3D11VA、swscale/swresample。
- **Catch2**：T1.1 引入帧数学单测时再加；T0.1 用零依赖冒烟测试（`tests\smoke.cpp`）保证 CTest 通路。
- **升级规则**：依赖升级必须单独成一个任务，更新本文件 + 重测 P12 体积 + 重新跑 `check-imports.ps1`。

## 7. 运行期零网络

- `scripts\check-imports.ps1`：发布包不得含 `Qt6Network.dll`/`tls`/`networkinformation`；不得导入 `winhttp/wininet/urlmon/wsock32`；`ws2_32` 仅允许 `Qt6Core.dll`（Qt 运行时固定链接）与阶段 A 的预编译 FFmpeg（阶段 B 后必须消除）。
- 手工验收：ProcMon 过滤 `Process Name=lapian.exe`、`Operation contains TCP/UDP`，启动并空窗口 60s，应 0 事件。
