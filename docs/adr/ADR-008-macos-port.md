# ADR-008：macOS 原生平台适配

- 状态：已接受
- 日期：2026-10-30
- 依据：用户明确要求将本项目移植并发布为可用的 macOS 版本。此前的 Windows-only 首期范围在本轮由用户明确扩展；本 ADR 不 retroactively 改写 Windows 首期验收记录。

## 决定

保留现有 C++20 / Qt Widgets / FFmpeg / SoundTouch 架构和 Windows 实现，在平台边界增加 macOS 实现：

| 边界 | Windows（不变） | macOS |
|---|---|---|
| 视频解码与呈现 | D3D11VA + D3D11 swapchain | FFmpeg 软件解码 + Qt `QImage` / `QPainter` |
| 音频输出 | WASAPI shared mode | CoreAudio / AudioUnit |
| HTTP 与 Range 下载 | WinHTTP | libcurl，复用 `IHttpClient` / `IDownloadTransport` |
| 凭据 | DPAPI | macOS 钥匙串（Security framework） |
| PNG 截图 | WIC | Qt `QImage` PNG 编码 |
| 构建与分发 | MSVC + Inno Setup | Apple Clang + CMake/Ninja + `.app` / DMG |

## 约束

- 首个发布目标为本机可验证的 Apple Silicon（arm64）；不声称 Intel、Universal Binary 或未实机系统版本已验证。
- macOS 软件渲染路径不冒充 D3D11 硬解路径，也不直接继承 Windows 的 P1–P14 性能结论。发布说明必须列出实际验证范围。
- 在线功能仍只允许 HTTPS、禁止凭据发送到 CDN、禁止自动重定向；SESSDATA 只进入系统钥匙串，不写明文文件或日志。
- 保留 MIT 与第三方许可。安装包携带对应许可和源码获取说明。
- 没有 Developer ID 时仅做 ad-hoc 签名，明确标注未公证；不把 ad-hoc 签名描述成 Apple 已验证或免除 Gatekeeper 提示。

## 验证

需要分别记录：编译、CTest、真实应用窗口、最终 `.app` / DMG、远程 Release 下载回读。至少验证本地播放、逐帧前后步进、精确定位、音频/波形、A/B 循环、标记/备注/截图；在线访问与真实扫码分别给出证据或标注未覆盖。
