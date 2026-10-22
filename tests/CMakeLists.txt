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
target_link_libraries(lapian_preview_decoder_test PRIVATE lapian_media)
lapian_copy_ffmpeg_dlls(lapian_preview_decoder_test)

add_test(NAME lapian_preview_decoder_test COMMAND lapian_preview_decoder_test)
set_tests_properties(lapian_preview_decoder_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  SKIP_RETURN_CODE 77)
