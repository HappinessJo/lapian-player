拉片软件 — Windows 帧级参考片审阅器
=====================================

运行
----
双击 lapian.exe。Release 包已带 VC 运行库，无需另装。
安装版默认安装到 %LOCALAPPDATA%\Programs\拉片软件，只装当前用户，不要求
管理员。便携版解压即用，不注册文件关联；需要 .mp4/.mov/.mkv/.webm 的
“打开方式”入口，请使用安装包。

变速音频
--------
0.5x / 0.75x / 1.25x / 1.5x / 2x 播放保持音高不变（SoundTouch 时间拉伸）；
0.25x 仍随速度变调（首期不做 0.25x 音高保持）。1x 不经过时间拉伸。

在线模式
--------
按 Ctrl+U 输入 BV 号或 B 站视频链接，核对标题/UP/时长/分P 后确认。在线播放
只处理公开免费内容；不绕过付费、DRM 或地区限制。普通用户的实际清晰度以
B 站返回为准；1080P60/4K 等档位不构成保证。

数据与隐私
----------
- 本地模式运行期不发起网络请求，不检查更新，不上传崩溃报告，也没有遥测。
- 在线模式只在提交 BV/URL 后访问 api.bilibili.com；只有点击“扫码登录”才访问
  passport.bilibili.com；视频/音频从 playurl 返回的 B 站 CDN 下载，CDN 不带
  Cookie。网络请求由 Windows WinHTTP 发出，可能遵循系统代理设置。
- 在线缓存默认在 %LOCALAPPDATA%\拉片软件\cache，默认配额 20 GiB；可在
  “设置 — 在线缓存”中查看/修改目录、配额、清空缓存或清理孤儿条目。清空缓存
  不会删除在线记录、截图或标记。
- 标记/备注默认写在视频同目录 <视频名>.lapian.json；目录只读时回退到
  %APPDATA%\拉片软件\marks\。
- 设置保存在 %APPDATA%\拉片软件\settings.json。
- 在线记录保存在 %APPDATA%\拉片软件\online\<bvid>-<cid>.lapian.json。
- 截图写到 %USERPROFILE%\Pictures\拉片软件\<bvid>\；图片目录不可写时回退到
  %APPDATA%\拉片软件\online\snapshots。
- 登录只保存 SESSDATA，使用 Windows DPAPI 加密保存在
  %APPDATA%\拉片软件\credentials.dat；只发送给 api.bilibili.com，不写入日志/
  缓存/截图；可在在线打开对话框或设置中清除。

卸载与清理
----------
卸载程序不会删除上述用户数据，也不会删除视频旁的 sidecar 与 snapshots。
卸载时会询问是否“同时删除在线缓存”，默认选“否”。只有明确确认（或静默卸载
时显式传入 /CLEARCACHE）才会删除默认缓存目录
%LOCALAPPDATA%\拉片软件\cache；不会删除在线记录、截图、设置、凭据或 marks。
自定义缓存目录不会被卸载程序清理，需要手动处理。

支持范围
--------
Windows 10 21H2+ / Windows 11 x64。
保证档：1080p H.264/H.265 8-bit SDR、≤60fps、MP4/MOV/MKV，要求 D3D11 + 硬解。
可用档：4K ≤30fps、VP9/AV1/WebM 能播则播；长 GOP 仍帧精确，性能按可用档。

许可
----
licenses\ 内包含 LGPL-2.1、LGPL-3.0、GPL-3.0 全文、THIRD-PARTY-NOTICES.md
与 SOURCE-OFFER.md，另有本隐私说明 PRIVACY.txt。Qt 与 FFmpeg 均为动态链接，
可用同名 DLL 替换；本项目未修改其源码，源码获取方式见 SOURCE-OFFER.md。
