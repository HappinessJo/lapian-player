拉片软件 — Windows 帧级参考片审阅器
=====================================

运行
----
双击 lapian.exe。Release 包已带 VC 运行库，无需另装。
便携版不注册文件关联；需要 .mp4/.mov/.mkv/.webm 的“打开方式”入口，
请使用安装包。

支持范围
--------
Windows 10 21H2+ / Windows 11 x64。
保证档：1080p H.264/H.265 8-bit SDR、≤60fps、MP4/MOV/MKV，要求 D3D11 + 硬解。
可用档：4K ≤30fps、VP9/AV1/WebM 能播则播；长 GOP 仍帧精确，性能按可用档。

数据与隐私
----------
- 不修改源视频；运行期不发起任何网络请求，不检查更新。
- 标记/备注默认写在视频同目录 <视频名>.lapian.json；目录只读时回退到
  %APPDATA%\拉片软件\marks\。
- 设置保存在 %APPDATA%\拉片软件\settings.json。
- 截图写到视频同目录 snapshots\；不可写时回退系统图片目录。
- 卸载程序不会删除上述用户数据，也不会删除视频旁的 sidecar 与 snapshots。
  需要彻底清理时请手动删除这些位置。

许可
----
licenses\ 内包含 LGPL-2.1、LGPL-3.0、GPL-3.0 全文、THIRD-PARTY-NOTICES.md
与 SOURCE-OFFER.md。Qt 与 FFmpeg 均为动态链接，可用同名 DLL 替换；
本项目未修改其源码，源码获取方式见 SOURCE-OFFER.md。
