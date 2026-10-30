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

# Windows 使用 D3D11；macOS 由 GfxWidget 的 Qt 软件渲染路径取代。
if(WIN32)
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
else()
  add_library(lapian_core INTERFACE)
  target_include_directories(lapian_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(lapian_core INTERFACE lapian::ffmpeg)
endif()

# T2.1 帧数学核心：纯 C++，不依赖 Qt/FFmpeg，先单测再接到 UI/解码。
add_library(lapian_framemath STATIC
  core/FrameMath.cpp
  core/FrameMath.h
  core/LoopRange.h
  core/MediaClock.cpp
  core/MediaClock.h)
lapian_apply_common_options(lapian_framemath)
target_include_directories(lapian_framemath PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# T5.1 标记 sidecar：Qt Core JSON + 原子写 + AppData 回退，不依赖 UI/FFmpeg。
add_library(lapian_marks STATIC
  core/MarkStore.cpp
  core/MarkStore.h)
lapian_apply_common_options(lapian_marks)
target_include_directories(lapian_marks PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lapian_marks PUBLIC Qt6::Core)

# T6.3 应用设置：%APPDATA%\拉片软件\settings.json、原子写、纯解析/校验；只依赖 Qt Core。
add_library(lapian_settings STATIC
  core/AppSettings.cpp
  core/AppSettings.h)
lapian_apply_common_options(lapian_settings)
target_include_directories(lapian_settings PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lapian_settings PUBLIC Qt6::Core)

# 在线层复用同一组业务接口；仅 HTTP 与 Range 传输实现按平台切换。
if(WIN32)
  set(LAPIAN_HTTP_SOURCES
    online/WinHttpApi.h
    online/WinHttpClient.cpp
    online/WinHttpDownloadTransport.cpp)
else()
  set(LAPIAN_HTTP_SOURCES
    online/CurlHttpClient.cpp
    online/CurlHttpClient.h
    online/CurlDownloadTransport.cpp
    online/CurlDownloadTransport.h)
endif()

add_library(lapian_online STATIC
  online/BiliAuth.cpp
  online/BiliAuth.h
  online/BiliResolver.cpp
  online/BiliResolver.h
  online/BiliResolveClient.cpp
  online/BiliResolveClient.h
  online/CacheStore.cpp
  online/CacheStore.h
  online/CacheManager.cpp
  online/CacheManager.h
  online/CredentialStore.cpp
  online/CredentialStore.h
  online/DownloadJob.cpp
  online/DownloadJob.h
  online/DownloadTransport.h
  online/FrameRatePolicy.cpp
  online/FrameRatePolicy.h
  online/GrowingSource.cpp
  online/GrowingSource.h
  online/IHttpClient.h
  online/PlayUrl.cpp
  online/PlayUrl.h
  online/QualityMenu.cpp
  online/QualityMenu.h
  online/QrCode.cpp
  online/QrCode.h
  online/QrLogin.cpp
  online/QrLogin.h
  online/QrLoginController.cpp
  online/QrLoginController.h
  online/Sessdata.h
  online/WinHttpClient.h
  ${LAPIAN_HTTP_SOURCES}
  online/qrcodegen/qrcodegen.cpp
  app/OnlineSession.cpp
  app/OnlineSession.h)
lapian_apply_common_options(lapian_online)
target_include_directories(lapian_online PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(lapian_online PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/online/qrcodegen")
if(WIN32)
  target_link_libraries(lapian_online PUBLIC Qt6::Core Qt6::Gui PRIVATE crypt32 lapian::ffmpeg)
else()
  target_link_libraries(lapian_online PUBLIC Qt6::Core Qt6::Gui
    PRIVATE lapian::curl lapian::ffmpeg "${LAPIAN_SECURITY_FRAMEWORK}")
endif()

# T7.3 长时浸泡：采样趋势/阈值判定，纯 C++，产品与单测共用。
add_library(lapian_soak STATIC
  app/SoakStats.cpp
  app/SoakStats.h)
lapian_apply_common_options(lapian_soak)
target_include_directories(lapian_soak PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# T5.2 标记侧栏：列表/存储位置/警告；产品与离屏测试共用。
add_library(lapian_mark_ui STATIC
  app/MarkPanel.cpp
  app/MarkPanel.h)
lapian_apply_common_options(lapian_mark_ui)
target_include_directories(lapian_mark_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lapian_mark_ui PUBLIC Qt6::Widgets lapian_marks)

# T5.4 截图命名/目录回退 + display matrix 旋转；纯函数，产品与单测共用。
add_library(lapian_snapshot STATIC
  media/SnapshotNaming.cpp
  media/SnapshotNaming.h
  app/SnapshotImage.cpp
  app/SnapshotImage.h)
lapian_apply_common_options(lapian_snapshot)
target_include_directories(lapian_snapshot PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lapian_snapshot PUBLIC Qt6::Gui lapian_framemath)

# T2.7 时间轴：纯 C++ 几何模型 + Qt Widgets 控件，产品与测试共用。
add_library(lapian_timeline STATIC
  core/TimelineModel.cpp
  core/TimelineModel.h
  app/TimelineWidget.cpp
  app/TimelineWidget.h)
lapian_apply_common_options(lapian_timeline)
target_include_directories(lapian_timeline PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lapian_timeline PUBLIC Qt6::Widgets)

# T1.1 媒体探测核心：纯 C++ + FFmpeg，不依赖 Qt，便于单测与后续复用。
add_library(lapian_media STATIC
  media/FrameCache.cpp
  media/FrameCache.h
  media/DragStrategy.cpp
  media/DragStrategy.h
  media/FrameIndex.cpp
  media/FrameIndex.h
  media/MediaInfo.h
  media/MediaProbe.cpp
  media/MediaProbe.h
  media/MediaSession.cpp
  media/MediaSession.h
  media/OnlineIndexBuilder.cpp
  media/OnlineIndexBuilder.h
  media/OnlinePreviewDecoder.cpp
  media/OnlinePreviewDecoder.h
  $<IF:$<PLATFORM_ID:Windows>,media/PathProbe.cpp,media/PathProbePosix.cpp>
  media/PathProbe.h
  media/PreviewDecoder.cpp
  media/PreviewDecoder.h
  media/PreviewRingCache.cpp
  media/PreviewRingCache.h
  media/SeekEngine.cpp
  media/SeekEngine.h
  media/StatusNotice.cpp
  media/StatusNotice.h
  media/VideoDecoder.cpp
  media/VideoDecoder.h)
lapian_apply_common_options(lapian_media)
target_include_directories(lapian_media PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# T8.6：MediaSession::OpenGrowing 持有 GrowingSource/GrowingFormat；本地路径分支
# 不调用这些类型，但静态库需要一个链接边。
target_link_libraries(lapian_media PUBLIC lapian::ffmpeg lapian_framemath lapian_online)

if(WIN32)
  # T3.3b: SoundTouch 2.3.3 (LGPL-2.1, shared DLL) for tempo change without pitch.
  set(LAPIAN_SOUNDTOUCH_ROOT "${CMAKE_SOURCE_DIR}/third_party/soundtouch")
  set(LAPIAN_SOUNDTOUCH_DLL "${LAPIAN_SOUNDTOUCH_ROOT}/build-msvc/Release/SoundTouch.dll")
  set(LAPIAN_SOUNDTOUCH_LIB "${LAPIAN_SOUNDTOUCH_ROOT}/build-msvc/Release/SoundTouch.lib")
  if(NOT EXISTS "${LAPIAN_SOUNDTOUCH_DLL}" OR NOT EXISTS "${LAPIAN_SOUNDTOUCH_LIB}")
    message(FATAL_ERROR "SoundTouch is not built. Run scripts\\build-soundtouch.ps1 first.")
  endif()
  add_library(soundtouch SHARED IMPORTED)
  set_target_properties(soundtouch PROPERTIES
    IMPORTED_LOCATION "${LAPIAN_SOUNDTOUCH_DLL}"
    IMPORTED_IMPLIB "${LAPIAN_SOUNDTOUCH_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${LAPIAN_SOUNDTOUCH_ROOT}/soundtouch/include")
  function(lapian_copy_soundtouch_dll target)
    set(_lapian_soundtouch_dll
        "${CMAKE_SOURCE_DIR}/third_party/soundtouch/build-msvc/Release/SoundTouch.dll")
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_lapian_soundtouch_dll}"
              "$<TARGET_FILE_DIR:${target}>"
      COMMENT "Copy SoundTouch DLL")
  endfunction()
endif()

if(WIN32)
  set(LAPIAN_AUDIO_RENDERER audio/WasapiRenderer.cpp)
else()
  set(LAPIAN_AUDIO_RENDERER audio/CoreAudioRenderer.cpp)
endif()

add_library(lapian_audio STATIC
  audio/AudioDecoder.cpp
  audio/AudioDecoder.h
  audio/AudioEngine.cpp
  audio/AudioEngine.h
  audio/AudioFormat.cpp
  audio/AudioFormat.h
  audio/WaveformData.cpp
  audio/WaveformData.h
  audio/WaveformGenerator.cpp
  audio/WaveformGenerator.h
  audio/TimeStretch.cpp
  audio/TimeStretch.h
  ${LAPIAN_AUDIO_RENDERER}
  audio/WasapiRenderer.h)
lapian_apply_common_options(lapian_audio)
target_include_directories(lapian_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
if(WIN32)
  target_link_libraries(lapian_audio PUBLIC lapian::ffmpeg lapian_online ole32 uuid avrt soundtouch)
else()
  target_link_libraries(lapian_audio PUBLIC lapian::ffmpeg lapian_online soundtouch
    "${LAPIAN_AUDIOUNIT_FRAMEWORK}" "${LAPIAN_COREAUDIO_FRAMEWORK}")
endif()

# UI v2 壳层（2026-10-26，3A；3D 起是唯一布局）：Tokens/Theme/PlayerShell/
# PlayerSplitter/TopBar/BannerSlot/StageStack/TransportBar。
qt_add_executable(lapian WIN32 MACOSX_BUNDLE
  app/main.cpp
  app/MainWindow.cpp
  app/MainWindow.h
  $<IF:$<PLATFORM_ID:Windows>,app/GfxWidget.cpp,app/GfxWidgetMac.cpp>
  app/GfxWidget.h
  app/ui/Tokens.h
  app/ui/Theme.h
  app/ui/Theme.cpp
  app/ui/Glyph.h
  app/ui/Glyph.cpp
  app/ui/TimelinePanel.h
  app/ui/TimelinePanel.cpp
  app/ui/PlayerSplitter.h
  app/ui/PlayerSplitter.cpp
  app/ui/TopBar.h
  app/ui/TopBar.cpp
  app/ui/BannerSlot.h
  app/ui/BannerSlot.cpp
  app/ui/StageStack.h
  app/ui/StageStack.cpp
  app/ui/TransportBar.h
  app/ui/TransportBar.cpp
  app/ui/RangeBar.h
  app/ui/RangeBar.cpp
  app/ui/InspectorPanel.h
  app/ui/InspectorPanel.cpp
  app/ui/PlayerShell.h
  app/ui/PlayerShell.cpp
  app/CacheSettingsDialog.cpp
  app/CacheSettingsDialog.h
  app/DragTestRunner.cpp
  app/DragTestRunner.h
  app/DragStrategyTestRunner.cpp
  app/DragStrategyTestRunner.h
  app/GopBoundaryTestRunner.cpp
  app/GopBoundaryTestRunner.h
  app/MemoryTestRunner.cpp
  app/MemoryTestRunner.h
  app/OnlineAuthTestRunner.cpp
  app/OnlineAuthTestRunner.h
  app/OnlineCacheTestRunner.cpp
  app/OnlineCacheTestRunner.h
  $<IF:$<PLATFORM_ID:Windows>,app/OnlineCacheManageTestRunner.cpp,app/OnlineCacheManageTestRunnerMac.cpp>
  app/OnlineCacheManageTestRunner.h
  app/OnlineDragPreviewTestRunner.cpp
  app/OnlineDragPreviewTestRunner.h
  $<IF:$<PLATFORM_ID:Windows>,app/OnlineFaultTestRunner.cpp,app/OnlineFaultTestRunnerMac.cpp>
  app/OnlineFaultTestRunner.h
  app/OnlineFetchTestRunner.cpp
  app/OnlineFetchTestRunner.h
  $<IF:$<PLATFORM_ID:Windows>,app/OnlineGrowingTestRunner.cpp,app/OnlineGrowingTestRunnerMac.cpp>
  app/OnlineGrowingTestRunner.h
  app/OnlineBufferTestRunner.cpp
  app/OnlineBufferTestRunner.h
  app/OnlineLongPressTestRunner.cpp
  app/OnlineLongPressTestRunner.h
  app/OnlineM1TestRunner.cpp
  app/OnlineM1TestRunner.h
  app/OnlineM2TestRunner.cpp
  app/OnlineM2TestRunner.h
  app/OnlineIndexTestRunner.cpp
  app/OnlineIndexTestRunner.h
  app/OnlineOfflineTestRunner.cpp
  app/OnlineOfflineTestRunner.h
  app/OnlineHttpSelfCheckRunner.cpp
  app/OnlineHttpSelfCheckRunner.h
  app/OnlineQrTestRunner.cpp
  app/OnlineQrTestRunner.h
  app/OnlineQualityTestRunner.cpp
  app/OnlineQualityTestRunner.h
  app/OnlineQualitySwitchTestRunner.cpp
  app/OnlineQualitySwitchTestRunner.h
  app/OnlineVipTestRunner.cpp
  app/OnlineVipTestRunner.h
  app/OnlineResolveDialog.cpp
  app/OnlineResolveDialog.h
  app/OnlineResolveTestRunner.cpp
  app/OnlineResolveTestRunner.h
  $<IF:$<PLATFORM_ID:Windows>,app/RobustTestRunner.cpp,app/RobustTestRunnerMac.cpp>
  app/RobustTestRunner.h
  app/PlaybackController.cpp
  app/PlaybackController.h
  $<IF:$<PLATFORM_ID:Windows>,app/ProcessMemory.cpp,app/ProcessMemoryMac.cpp>
  app/ProcessMemory.h
  app/SnapshotService.cpp
  app/SnapshotService.h)
lapian_apply_common_options(lapian)
target_include_directories(lapian PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(lapian PRIVATE LAPIAN_VERSION="${PROJECT_VERSION}")
if(WIN32)
  target_link_libraries(lapian PRIVATE lapian_core lapian_media lapian_framemath
                                       lapian_timeline lapian_audio lapian_marks
                                       lapian_mark_ui lapian_snapshot lapian_settings
                                       lapian_soak lapian_online
                                       lapian::ffmpeg
                                       Qt6::Widgets windowscodecs ole32 psapi)
else()
  target_link_libraries(lapian PRIVATE lapian_core lapian_media lapian_framemath
                                       lapian_timeline lapian_audio lapian_marks
                                       lapian_mark_ui lapian_snapshot lapian_settings
                                       lapian_soak lapian_online
                                       lapian::ffmpeg
                                       Qt6::Widgets)
  set_target_properties(lapian PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Lapian"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.happinessjo.lapian-player"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()

# T8.13 诊断（只在本地构建产物里生成 .map，用于把崩溃 RVA 映射到符号；不参与分发）
if(MSVC)
  target_link_options(lapian PRIVATE "/MAP:${CMAKE_BINARY_DIR}/lapian.map")
endif()

lapian_copy_ffmpeg_dlls(lapian)
lapian_copy_soundtouch_dll(lapian)
lapian_set_output_dir(lapian)

if(WIN32)
  # Windows 开发探针；macOS 使用 CTest 与产品内回归探针。
  add_executable(lapian_env_check tools/env_check/main.cpp)
  lapian_apply_common_options(lapian_env_check)
  target_include_directories(lapian_env_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_definitions(lapian_env_check PRIVATE LAPIAN_VERSION="${PROJECT_VERSION}")
  target_link_libraries(lapian_env_check PRIVATE lapian_core lapian::ffmpeg Qt6::Core)
  lapian_copy_ffmpeg_dlls(lapian_env_check)
  lapian_set_output_dir(lapian_env_check)

  add_executable(lapian_media_probe tools/media_probe/main.cpp)
  lapian_apply_common_options(lapian_media_probe)
  target_link_libraries(lapian_media_probe PRIVATE lapian_media)
  lapian_copy_ffmpeg_dlls(lapian_media_probe)
  lapian_set_output_dir(lapian_media_probe)

  add_executable(lapian_decode_probe tools/decode_probe/main.cpp)
  lapian_apply_common_options(lapian_decode_probe)
  target_link_libraries(lapian_decode_probe PRIVATE lapian_media)
  lapian_copy_ffmpeg_dlls(lapian_decode_probe)
  lapian_set_output_dir(lapian_decode_probe)

  add_executable(lapian_seek_probe tools/seek_probe/main.cpp)
  lapian_apply_common_options(lapian_seek_probe)
  target_link_libraries(lapian_seek_probe PRIVATE lapian::ffmpeg)
  lapian_copy_ffmpeg_dlls(lapian_seek_probe)
  lapian_set_output_dir(lapian_seek_probe)

  add_executable(lapian_audio_probe tools/audio_probe/main.cpp)
  lapian_apply_common_options(lapian_audio_probe)
  target_link_libraries(lapian_audio_probe PRIVATE lapian_audio)
  lapian_copy_ffmpeg_dlls(lapian_audio_probe)
  lapian_copy_soundtouch_dll(lapian_audio_probe)
  lapian_set_output_dir(lapian_audio_probe)

  add_executable(lapian_waveform_probe tools/waveform_probe/main.cpp)
  lapian_apply_common_options(lapian_waveform_probe)
  target_link_libraries(lapian_waveform_probe PRIVATE lapian_audio)
  lapian_copy_ffmpeg_dlls(lapian_waveform_probe)
  lapian_copy_soundtouch_dll(lapian_waveform_probe)
  lapian_set_output_dir(lapian_waveform_probe)

  add_executable(lapian_dash_spike tools/dash_spike/main.cpp)
  lapian_apply_common_options(lapian_dash_spike)
  target_link_libraries(lapian_dash_spike PRIVATE lapian_media lapian_audio)
  lapian_copy_ffmpeg_dlls(lapian_dash_spike)
  lapian_copy_soundtouch_dll(lapian_dash_spike)
  lapian_set_output_dir(lapian_dash_spike)
endif()
