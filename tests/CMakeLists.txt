# T0.1 冒烟测试：零依赖，只验证 Qt 链接与 CTest 通路。
# Catch2 与真正的帧数学单测在 T1.1 引入。
add_executable(lapian_smoke smoke.cpp)
lapian_apply_common_options(lapian_smoke)
target_compile_definitions(lapian_smoke PRIVATE LAPIAN_VERSION="${PROJECT_VERSION}")
target_link_libraries(lapian_smoke PRIVATE Qt6::Core)

add_test(NAME lapian_smoke COMMAND lapian_smoke)

# T2.1 CFR 帧映射/总帧数/NDF 时间码测试；纯 C++，无 Qt/FFmpeg 依赖。
add_executable(lapian_frame_math_test frame_math_test.cpp)
lapian_apply_common_options(lapian_frame_math_test)
target_link_libraries(lapian_frame_math_test PRIVATE lapian_framemath)
add_test(NAME lapian_frame_math_test COMMAND lapian_frame_math_test)

# T3.2 媒体时钟（QPC 回退/rate/seek/暂停）测试；注入墙上时间，纯 C++。
add_executable(lapian_clock_test clock_test.cpp)
lapian_apply_common_options(lapian_clock_test)
target_link_libraries(lapian_clock_test PRIVATE lapian_framemath)
add_test(NAME lapian_clock_test COMMAND lapian_clock_test)

# T4.1 A/B 区间：入出点校验、I 晚于 B 清 B、循环开关、清除。纯 C++。
add_executable(lapian_loop_range_test loop_range_test.cpp)
lapian_apply_common_options(lapian_loop_range_test)
target_link_libraries(lapian_loop_range_test PRIVATE lapian_framemath)
add_test(NAME lapian_loop_range_test COMMAND lapian_loop_range_test)

# T5.1 标记 sidecar：路径规范化/哈希、roundtrip、合并、损坏备份、空文件、
# AppData 回退/迁回、frame+pts 校正、原子写失败保护。只依赖 Qt Core。
add_executable(lapian_mark_store_test mark_store_test.cpp)
lapian_apply_common_options(lapian_mark_store_test)
target_link_libraries(lapian_mark_store_test PRIVATE lapian_marks)
add_test(NAME lapian_mark_store_test COMMAND lapian_mark_store_test)
set_tests_properties(lapian_mark_store_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T6.3 设置持久化：默认值、写读、非法值/损坏/未知键、Unicode 路径、
# 原子覆盖写、不可读/不可写回退。只依赖 Qt Core。
add_executable(lapian_app_settings_test app_settings_test.cpp)
lapian_apply_common_options(lapian_app_settings_test)
target_link_libraries(lapian_app_settings_test PRIVATE lapian_settings)
add_test(NAME lapian_app_settings_test COMMAND lapian_app_settings_test)
set_tests_properties(lapian_app_settings_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T6.5 状态提示：六类提示生成、优先级、唯一横幅与状态栏摘要。纯 C++。
add_executable(lapian_status_notice_test status_notice_test.cpp)
lapian_apply_common_options(lapian_status_notice_test)
target_link_libraries(lapian_status_notice_test PRIVATE lapian_media)
lapian_copy_ffmpeg_dlls(lapian_status_notice_test)
add_test(NAME lapian_status_notice_test COMMAND lapian_status_notice_test)
set_tests_properties(lapian_status_notice_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T7.3 浸泡分析：稳态窗口斜率/增长率/句柄/线程/帧停顿/缓存/UI 心跳判定。纯 C++。
add_executable(lapian_soak_test soak_stats_test.cpp)
lapian_apply_common_options(lapian_soak_test)
target_link_libraries(lapian_soak_test PRIVATE lapian_soak)
add_test(NAME lapian_soak_test COMMAND lapian_soak_test)

# T7.5 网络路径预检：UNC/映射盘识别、缺失/可达判定、不可达主机的有界等待。
add_executable(lapian_path_probe_test path_probe_test.cpp)
lapian_apply_common_options(lapian_path_probe_test)
target_link_libraries(lapian_path_probe_test PRIVATE lapian_media)
lapian_copy_ffmpeg_dlls(lapian_path_probe_test)
add_test(NAME lapian_path_probe_test COMMAND lapian_path_probe_test)
set_tests_properties(lapian_path_probe_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T8.1 在线解析：BV/URL 归一化、view JSON、错误码、非法 0 请求、取消；
# WinHttpClient 用本机慢服务器验证成功/超时/取消 ≤100ms/无 Cookie 头。
add_executable(lapian_online_resolve_test online_resolve_test.cpp)
lapian_apply_common_options(lapian_online_resolve_test)
target_link_libraries(lapian_online_resolve_test PRIVATE lapian_online ws2_32)
add_test(NAME lapian_online_resolve_test COMMAND lapian_online_resolve_test)
set_tests_properties(lapian_online_resolve_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T8.2 凭据/nav：DPAPI 往返/无明文/损坏回退/清除、SESSDATA 校验、
# nav 成功/-101/412/429/超时/取消/脱敏、Cookie 只允许 API host。
add_executable(lapian_online_auth_test online_auth_test.cpp)
lapian_apply_common_options(lapian_online_auth_test)
target_link_libraries(lapian_online_auth_test PRIVATE lapian_online)
add_test(NAME lapian_online_auth_test COMMAND lapian_online_auth_test)
set_tests_properties(lapian_online_auth_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T8.3 缓存存储与配额：默认根/meta/.part→.mp4/续传/300MB LRU/当前保护/
# 20GB 记账/损坏重建/清空保留记录/磁盘满预检/跨会话/长路径/无明文。纯本地存储。
add_executable(lapian_online_cache_test online_cache_test.cpp)
lapian_apply_common_options(lapian_online_cache_test)
target_link_libraries(lapian_online_cache_test PRIVATE lapian_online lapian_settings)
add_test(NAME lapian_online_cache_test COMMAND lapian_online_cache_test)
set_tests_properties(lapian_online_cache_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T8.3.1 缓存管理：默认根/记录根、目录/配额跨会话、清空保留当前会话（部分键）、
# 在线记录在缓存根外、孤儿清理、磁盘满/非法路径可读错误。纯本地存储。
add_executable(lapian_online_cache_manage_test online_cache_manage_test.cpp)
lapian_apply_common_options(lapian_online_cache_manage_test)
target_link_libraries(lapian_online_cache_manage_test PRIVATE lapian_online lapian_settings)
add_test(NAME lapian_online_cache_manage_test COMMAND lapian_online_cache_manage_test)
set_tests_properties(lapian_online_cache_manage_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T8.4 可增长读层：Publish/Published、每读者 AVIO/文件句柄、阻塞读/唤醒、
# Cancel ≤100ms、EOF/EIO/EXIT、seek/AVSEEK_SIZE、4 读者短压测、4GiB−1 int64、
# CacheStore OpenPart/淘汰 busy 边界。纯本地，无网络/GUI。
add_executable(lapian_growing_source_test growing_source_test.cpp)
lapian_apply_common_options(lapian_growing_source_test)
target_link_libraries(lapian_growing_source_test PRIVATE lapian_online lapian::ffmpeg)
lapian_copy_ffmpeg_dlls(lapian_growing_source_test)
add_test(NAME lapian_growing_source_test COMMAND lapian_growing_source_test)
set_tests_properties(lapian_growing_source_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T8.5 durl 取流：playurl 解析、Range/续传、200 回退、备份/刷新、取消/暂停、
# 总长/起点不一致、MarkComplete 与 ffprobe；fake transport，不联网。
add_executable(lapian_online_fetch_test online_fetch_test.cpp)
lapian_apply_common_options(lapian_online_fetch_test)
target_link_libraries(lapian_online_fetch_test PRIVATE lapian_online lapian::ffmpeg)
lapian_copy_ffmpeg_dlls(lapian_online_fetch_test)
add_test(NAME lapian_online_fetch_test COMMAND lapian_online_fetch_test)
set_tests_properties(lapian_online_fetch_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  SKIP_RETURN_CODE 77)

# T8.6 在线媒体层：0.3MB OpenGrowing/首帧、部分索引、已缓存精确 seek、
# 未缓存 seek 单读者中断 ≤100ms。无 GUI/网络；素材缺失返回 77。
add_executable(lapian_online_m1_test online_m1_test.cpp)
lapian_apply_common_options(lapian_online_m1_test)
target_link_libraries(lapian_online_m1_test PRIVATE lapian_media lapian_online lapian::ffmpeg)
lapian_copy_ffmpeg_dlls(lapian_online_m1_test)
add_test(NAME lapian_online_m1_test COMMAND lapian_online_m1_test)
set_tests_properties(lapian_online_m1_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  SKIP_RETURN_CODE 77)

# T8.7 缓存命中与在线记录：完整缓存直接打开（0 playurl/0 .part）、
# lapian-online-1 记录/损坏/清缓存保留/重建回访、截图目录/命名/index.jsonl。
add_executable(lapian_online_records_test online_records_test.cpp)
lapian_apply_common_options(lapian_online_records_test)
target_link_libraries(lapian_online_records_test PRIVATE lapian_online lapian_marks lapian_snapshot)
add_test(NAME lapian_online_records_test COMMAND lapian_online_records_test)
set_tests_properties(lapian_online_records_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T8.2.2 扫码登录：QR 本地渲染、generate/poll 解析、4 态/过期/取消、
# 只提取 SESSDATA、DPAPI、nav 通过才 success；fake HTTP，无网络/GUI。
add_executable(lapian_online_qr_test online_qr_test.cpp)
lapian_apply_common_options(lapian_online_qr_test)
target_link_libraries(lapian_online_qr_test PRIVATE lapian_online lapian::ffmpeg)
lapian_copy_ffmpeg_dlls(lapian_online_qr_test)
add_test(NAME lapian_online_qr_test COMMAND lapian_online_qr_test)
set_tests_properties(lapian_online_qr_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T8.9 DASH 解析与档位：fnval=4048 URL、dash.video[]/audio[] 字段、
# quality 不可信、AVC 默认/档位顺序、60fps+4K 只手动、坏字段/无音轨不崩、
# 档位菜单模型（只列示/置灰 T8.10/不把可选写成保证可达）。纯离线。
add_executable(lapian_online_dash_test online_dash_test.cpp)
lapian_apply_common_options(lapian_online_dash_test)
target_link_libraries(lapian_online_dash_test PRIVATE lapian_online)
add_test(NAME lapian_online_dash_test COMMAND lapian_online_dash_test)
set_tests_properties(lapian_online_dash_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T8.12 实际 fps/降帧提示策略：标题证据、普通 30/25 不误报、OB-2 60→29.412
# 的“帧号与源片不同步/约 1/2”、HEVC-only 60fps 未选时的“可能降帧”、真 60 不提示。
add_executable(lapian_online_frame_rate_test online_frame_rate_test.cpp)
lapian_apply_common_options(lapian_online_frame_rate_test)
target_link_libraries(lapian_online_frame_rate_test PRIVATE lapian_online)
add_test(NAME lapian_online_frame_rate_test COMMAND lapian_online_frame_rate_test)
set_tests_properties(lapian_online_frame_rate_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T8.13 网络异常/并发加固：断网续传/全线路失败/未缓存 seek 断网/主 CDN 403/
# URL 过期刷新/刷新失败/API 错误矩阵/真实 WinHTTP 4.5s 超时/网络无响应看门狗/
# 缓存损坏自动重建/磁盘不足/DASH 单流失败/关闭阻塞/日志脱敏/4 读者并发。
# 全部注入（fake HTTP/transport + 本机 127.0.0.1 静默服务器），不联网、无 GUI。
add_executable(lapian_online_fault_test
  online_fault_test.cpp
  ${CMAKE_SOURCE_DIR}/src/app/OnlineSession.cpp
  ${CMAKE_SOURCE_DIR}/src/app/OnlineSession.h)
lapian_apply_common_options(lapian_online_fault_test)
target_link_libraries(lapian_online_fault_test PRIVATE lapian_online lapian::ffmpeg ws2_32)
lapian_copy_ffmpeg_dlls(lapian_online_fault_test)
add_test(NAME lapian_online_fault_test COMMAND lapian_online_fault_test)
set_tests_properties(lapian_online_fault_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T8.11 渐进索引/波形：真实 OB-3 缓存按 256KB 受控增长（等价 1–2MB/s），
# 验证只扫已发布前缀/stable 单调/前缀 PTS 不变/30s 覆盖/完成精确/读字节比；
# 独立音频慢增长验证 bucket 只追加、偏移 ≤1 帧。素材缺失返回 77。
add_executable(lapian_online_index_test online_index_test.cpp)
lapian_apply_common_options(lapian_online_index_test)
target_link_libraries(lapian_online_index_test PRIVATE lapian_media lapian_audio)
lapian_copy_ffmpeg_dlls(lapian_online_index_test)
add_test(NAME lapian_online_index_test COMMAND lapian_online_index_test)
set_tests_properties(lapian_online_index_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  SKIP_RETURN_CODE 77)

# T5.2 标记侧栏：100 条列表、点击/删除信号、存储位置/回退/警告展示、离屏性能。
add_executable(lapian_mark_panel_test mark_panel_test.cpp)
lapian_apply_common_options(lapian_mark_panel_test)
target_link_libraries(lapian_mark_panel_test PRIVATE lapian_mark_ui)
add_test(NAME lapian_mark_panel_test COMMAND lapian_mark_panel_test)
set_tests_properties(lapian_mark_panel_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen" SKIP_RETURN_CODE 77)

# T5.4 截图命名/非法字符/重名序号/目录回退/display matrix 旋转。
add_executable(lapian_snapshot_naming_test snapshot_naming_test.cpp)
lapian_apply_common_options(lapian_snapshot_naming_test)
target_link_libraries(lapian_snapshot_naming_test PRIVATE lapian_snapshot)
add_test(NAME lapian_snapshot_naming_test COMMAND lapian_snapshot_naming_test)
set_tests_properties(lapian_snapshot_naming_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T1.1 媒体探测测试：零依赖断言；媒体素材缺失时返回 77，由 CTest 标记 SKIP。
add_executable(lapian_media_probe_test media_probe_test.cpp)
lapian_apply_common_options(lapian_media_probe_test)
target_link_libraries(lapian_media_probe_test PRIVATE lapian_media)
lapian_copy_ffmpeg_dlls(lapian_media_probe_test)

add_test(NAME lapian_media_probe_test COMMAND lapian_media_probe_test)
set_tests_properties(lapian_media_probe_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  SKIP_RETURN_CODE 77)

# T1.2 视频解码测试：素材/硬解缺失时返回 77，由 CTest 标记 SKIP。
add_executable(lapian_video_decoder_test video_decoder_test.cpp)
lapian_apply_common_options(lapian_video_decoder_test)
target_link_libraries(lapian_video_decoder_test PRIVATE lapian_media)
lapian_copy_ffmpeg_dlls(lapian_video_decoder_test)

add_test(NAME lapian_video_decoder_test COMMAND lapian_video_decoder_test)
set_tests_properties(lapian_video_decoder_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  SKIP_RETURN_CODE 77)

# T1.3 离屏渲染测试：D3D11 RTV 回读，不建窗口。
add_executable(lapian_video_renderer_test video_renderer_test.cpp)
lapian_apply_common_options(lapian_video_renderer_test)
target_link_libraries(lapian_video_renderer_test PRIVATE lapian_core lapian_media)
lapian_copy_ffmpeg_dlls(lapian_video_renderer_test)

add_test(NAME lapian_video_renderer_test COMMAND lapian_video_renderer_test)
set_tests_properties(lapian_video_renderer_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  SKIP_RETURN_CODE 77)

# T1.4 媒体会话切换：失败保留旧会话。
add_executable(lapian_media_session_test media_session_test.cpp)
lapian_apply_common_options(lapian_media_session_test)
target_link_libraries(lapian_media_session_test PRIVATE lapian_core lapian_media)
lapian_copy_ffmpeg_dlls(lapian_media_session_test)

add_test(NAME lapian_media_session_test COMMAND lapian_media_session_test)
set_tests_properties(lapian_media_session_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  SKIP_RETURN_CODE 77)

# T7.2 帧缓存：字节预算、条目上限、LRU、克隆独立性、分辨率预算。合成 NV12，无素材依赖。
add_executable(lapian_frame_cache_test frame_cache_test.cpp)
lapian_apply_common_options(lapian_frame_cache_test)
target_link_libraries(lapian_frame_cache_test PRIVATE lapian_media)
lapian_copy_ffmpeg_dlls(lapian_frame_cache_test)
add_test(NAME lapian_frame_cache_test COMMAND lapian_frame_cache_test)
set_tests_properties(lapian_frame_cache_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T2.2 PTS 索引：纯索引单测 + 真实文件 demux。
add_executable(lapian_frame_index_test frame_index_test.cpp)
lapian_apply_common_options(lapian_frame_index_test)
target_link_libraries(lapian_frame_index_test PRIVATE lapian_media)
lapian_copy_ffmpeg_dlls(lapian_frame_index_test)

add_test(NAME lapian_frame_index_test COMMAND lapian_frame_index_test)
set_tests_properties(lapian_frame_index_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  SKIP_RETURN_CODE 77)

# T2.3 精确 seek：关键帧 backward + 目标帧 PTS + 降级计数。
add_executable(lapian_seek_engine_test seek_engine_test.cpp)
lapian_apply_common_options(lapian_seek_engine_test)
target_link_libraries(lapian_seek_engine_test PRIVATE lapian_core lapian_media)
lapian_copy_ffmpeg_dlls(lapian_seek_engine_test)

add_test(NAME lapian_seek_engine_test COMMAND lapian_seek_engine_test)
set_tests_properties(lapian_seek_engine_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  SKIP_RETURN_CODE 77)

# T2.7 时间轴几何 + 43.2 万帧离屏绘制基准（offscreen，避免弹窗）。
add_executable(lapian_timeline_test timeline_test.cpp)
lapian_apply_common_options(lapian_timeline_test)
target_link_libraries(lapian_timeline_test PRIVATE lapian_timeline)

add_test(NAME lapian_timeline_test COMMAND lapian_timeline_test)
set_tests_properties(lapian_timeline_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  SKIP_RETURN_CODE 77)

# T3.1 音频：音量映射/环形缓冲/格式转换 + V9 AAC 解码与 seek。
add_executable(lapian_audio_test audio_test.cpp)
lapian_apply_common_options(lapian_audio_test)
target_link_libraries(lapian_audio_test PRIVATE lapian_audio)
lapian_copy_ffmpeg_dlls(lapian_audio_test)

add_test(NAME lapian_audio_test COMMAND lapian_audio_test)
set_tests_properties(lapian_audio_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  SKIP_RETURN_CODE 77)

# T3.5 波形生成：WaveformData 边界 + V9 后台生成/峰值 bucket。
add_executable(lapian_waveform_test waveform_test.cpp)
lapian_apply_common_options(lapian_waveform_test)
target_link_libraries(lapian_waveform_test PRIVATE lapian_audio)
lapian_copy_ffmpeg_dlls(lapian_waveform_test)

add_test(NAME lapian_waveform_test COMMAND lapian_waveform_test)
set_tests_properties(lapian_waveform_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  SKIP_RETURN_CODE 77)

# T2.9 拖动预览解码器：后台打开、首帧延迟、2s 目标更新下的发布帧率（开发机探针）。
add_executable(lapian_preview_decoder_test preview_decoder_test.cpp)
lapian_apply_common_options(lapian_preview_decoder_test)
target_link_libraries(lapian_preview_decoder_test PRIVATE lapian_core lapian_media)
lapian_copy_ffmpeg_dlls(lapian_preview_decoder_test)

add_test(NAME lapian_preview_decoder_test COMMAND lapian_preview_decoder_test)
set_tests_properties(lapian_preview_decoder_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  SKIP_RETURN_CODE 77)

# DP-2 预览环缓存/关键帧缩略图缓存：预算、LRU、窗口裁剪、最近关键帧查询。
add_executable(lapian_preview_cache_test preview_cache_test.cpp)
lapian_apply_common_options(lapian_preview_cache_test)
target_link_libraries(lapian_preview_cache_test PRIVATE lapian_media)
lapian_copy_ffmpeg_dlls(lapian_preview_cache_test)

add_test(NAME lapian_preview_cache_test COMMAND lapian_preview_cache_test)
set_tests_properties(lapian_preview_cache_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# T8.6.1b 在线拖动预览解码器：GrowingSource 独立读层、降采样、最新目标合并、
# 反向缓存、过期帧丢弃、未缓存 waiting、End 有界。素材缺失返回 77。
add_executable(lapian_online_preview_test online_preview_decoder_test.cpp)
lapian_apply_common_options(lapian_online_preview_test)
target_link_libraries(lapian_online_preview_test PRIVATE lapian_media lapian_online lapian::ffmpeg)
lapian_copy_ffmpeg_dlls(lapian_online_preview_test)

add_test(NAME lapian_online_preview_test COMMAND lapian_online_preview_test)
set_tests_properties(lapian_online_preview_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  SKIP_RETURN_CODE 77)

# T3.3b tempo change without pitch change (SoundTouch).
add_executable(lapian_time_stretch_test time_stretch_test.cpp)
lapian_apply_common_options(lapian_time_stretch_test)
target_link_libraries(lapian_time_stretch_test PRIVATE lapian_audio)
lapian_copy_ffmpeg_dlls(lapian_time_stretch_test)
lapian_copy_soundtouch_dll(lapian_time_stretch_test)
add_test(NAME lapian_time_stretch_test COMMAND lapian_time_stretch_test)
