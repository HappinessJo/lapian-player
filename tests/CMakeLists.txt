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
