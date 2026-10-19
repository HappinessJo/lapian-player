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

# T2.9 拖动预览解码器：后台打开、首帧延迟、2s 目标更新下的发布帧率（开发机探针）。
add_executable(lapian_preview_decoder_test preview_decoder_test.cpp)
lapian_apply_common_options(lapian_preview_decoder_test)
target_link_libraries(lapian_preview_decoder_test PRIVATE lapian_media)
lapian_copy_ffmpeg_dlls(lapian_preview_decoder_test)

add_test(NAME lapian_preview_decoder_test COMMAND lapian_preview_decoder_test)
set_tests_properties(lapian_preview_decoder_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  SKIP_RETURN_CODE 77)
