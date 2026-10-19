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
