# 复制 FFmpeg 运行时 DLL 到目标目录（构建后即可直接运行）
function(lapian_copy_ffmpeg_dlls target)
  if(LAPIAN_FFMPEG_RUNTIME_DLLS)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${LAPIAN_FFMPEG_RUNTIME_DLLS}
              "$<TARGET_FILE_DIR:${target}>"
      COMMENT "复制 FFmpeg 运行时 DLL")
  endif()
endfunction()

# 可执行文件统一输出到 build/bin，打包脚本不依赖生成器的默认布局
set(LAPIAN_RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
function(lapian_set_output_dir target)
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${LAPIAN_RUNTIME_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${LAPIAN_RUNTIME_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${LAPIAN_RUNTIME_OUTPUT_DIR}")
endfunction()

# 不含 Qt 的底层库（D3D11），可被测试与自检程序复用
add_library(lapian_core STATIC
  platform/gfx/D3D11Device.cpp
  platform/gfx/D3D11Device.h
  platform/gfx/SwapChain.cpp
  platform/gfx/SwapChain.h
  platform/gfx/VideoRenderer.cpp
  platform/gfx/VideoRenderer.h
  platform/gfx/VideoRenderer.hlsl
  platform/gfx/VideoShaders.h)
lapian_apply_common_options(lapian_core)
target_include_directories(lapian_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lapian_core PUBLIC d3d11 dxgi lapian::ffmpeg)
# 着色器已离线用 fxc 编译进 VideoShaders.h；不要让 MSBuild 再按默认 main 入口编译 .hlsl。
set_source_files_properties(platform/gfx/VideoRenderer.hlsl PROPERTIES HEADER_FILE_ONLY TRUE)

# T2.1 帧数学核心：纯 C++，不依赖 Qt/FFmpeg，先单测再接到 UI/解码。
add_library(lapian_framemath STATIC
  core/FrameMath.cpp
  core/FrameMath.h)
lapian_apply_common_options(lapian_framemath)
target_include_directories(lapian_framemath PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# T1.1 媒体探测核心：纯 C++ + FFmpeg，不依赖 Qt，便于单测与后续复用。
add_library(lapian_media STATIC
  media/MediaInfo.h
  media/MediaProbe.cpp
  media/MediaProbe.h
  media/VideoDecoder.cpp
  media/VideoDecoder.h)
lapian_apply_common_options(lapian_media)
target_include_directories(lapian_media PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lapian_media PUBLIC lapian::ffmpeg)

# 产品可执行程序（T0.1：空窗口 + D3D11 清屏）
qt_add_executable(lapian WIN32
  app/main.cpp
  app/MainWindow.cpp
  app/MainWindow.h
  app/GfxWidget.cpp
  app/GfxWidget.h)
lapian_apply_common_options(lapian)
target_include_directories(lapian PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(lapian PRIVATE LAPIAN_VERSION="${PROJECT_VERSION}")
target_link_libraries(lapian PRIVATE lapian_core lapian_media lapian::ffmpeg Qt6::Widgets)
lapian_copy_ffmpeg_dlls(lapian)
lapian_set_output_dir(lapian)

# 环境自检（控制台）：Qt/FFmpeg/D3D11 版本与可用性，构建脚本据此判定
add_executable(lapian_env_check
  tools/env_check/main.cpp)
lapian_apply_common_options(lapian_env_check)
target_include_directories(lapian_env_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(lapian_env_check PRIVATE LAPIAN_VERSION="${PROJECT_VERSION}")
target_link_libraries(lapian_env_check PRIVATE lapian_core lapian::ffmpeg Qt6::Core)
lapian_copy_ffmpeg_dlls(lapian_env_check)
lapian_set_output_dir(lapian_env_check)

# T1.1 媒体探测开发工具（控制台；只读元数据，不渲染、不解码、不入分发）
add_executable(lapian_media_probe
  tools/media_probe/main.cpp)
lapian_apply_common_options(lapian_media_probe)
target_link_libraries(lapian_media_probe PRIVATE lapian_media)
lapian_copy_ffmpeg_dlls(lapian_media_probe)
lapian_set_output_dir(lapian_media_probe)

# T1.2 解码探针（控制台；只解码首帧/N 帧并打印元数据，不渲染、不入分发）
add_executable(lapian_decode_probe
  tools/decode_probe/main.cpp)
lapian_apply_common_options(lapian_decode_probe)
target_link_libraries(lapian_decode_probe PRIVATE lapian_media)
lapian_copy_ffmpeg_dlls(lapian_decode_probe)
lapian_set_output_dir(lapian_decode_probe)

# T0.3 精确 seek 探针（控制台；不渲染、不缓存，只测关键帧回退+解码到目标帧的成本）
add_executable(lapian_seek_probe
  tools/seek_probe/main.cpp)
lapian_apply_common_options(lapian_seek_probe)
target_link_libraries(lapian_seek_probe PRIVATE lapian::ffmpeg)
lapian_copy_ffmpeg_dlls(lapian_seek_probe)
lapian_set_output_dir(lapian_seek_probe)
