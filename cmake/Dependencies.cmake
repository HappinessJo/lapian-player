# FFmpeg 依赖（LGPL、动态链接）
#
# 两种来源，目录结构一致（include/ lib/ bin/）：
#   1. 阶段 A 临时脚手架：BtbN 的 win64-lgpl-shared 预编译包（不随包分发）
#   2. T0.1 完成态：scripts/build-ffmpeg.ps1 产出的 FFmpeg 7.1 最小构建
#
# 查找顺序：-DFFMPEG_ROOT > 环境变量 FFMPEG_ROOT > third_party/ffmpeg/{build-msvc,prebuilt}/*

set(FFMPEG_ROOT "" CACHE PATH "FFmpeg 前缀目录（包含 include/ 与 lib/）")

if(NOT FFMPEG_ROOT AND DEFINED ENV{FFMPEG_ROOT} AND EXISTS "$ENV{FFMPEG_ROOT}/include/libavformat/avformat.h")
  set(FFMPEG_ROOT "$ENV{FFMPEG_ROOT}")
endif()

if(NOT FFMPEG_ROOT)
  foreach(_sub IN ITEMS build-msvc prebuilt)
    file(GLOB _candidates LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/${_sub}/*")
    foreach(_candidate IN LISTS _candidates)
      if(NOT FFMPEG_ROOT AND EXISTS "${_candidate}/include/libavformat/avformat.h")
        set(FFMPEG_ROOT "${_candidate}")
      endif()
    endforeach()
  endforeach()
endif()

if(NOT FFMPEG_ROOT)
  message(FATAL_ERROR
    "找不到 FFmpeg。请先运行 scripts/build-ffmpeg.ps1（阶段 B）"
    "或用 -DFFMPEG_ROOT 指向含 include/ 与 lib/ 的 FFmpeg 目录。")
endif()

set(LAPIAN_FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
if(NOT EXISTS "${LAPIAN_FFMPEG_INCLUDE_DIR}/libavformat/avformat.h")
  message(FATAL_ERROR "FFmpeg 头文件缺失：${LAPIAN_FFMPEG_INCLUDE_DIR}/libavformat/avformat.h")
endif()

set(LAPIAN_FFMPEG_LIBRARIES "")
foreach(_lib IN ITEMS avformat avcodec avutil swscale swresample)
  find_library(LAPIAN_FFMPEG_${_lib}_LIBRARY NAMES ${_lib}
    HINTS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
  if(NOT LAPIAN_FFMPEG_${_lib}_LIBRARY)
    message(FATAL_ERROR "FFmpeg 缺少导入库 ${_lib}：${FFMPEG_ROOT}/lib")
  endif()
  list(APPEND LAPIAN_FFMPEG_LIBRARIES "${LAPIAN_FFMPEG_${_lib}_LIBRARY}")
endforeach()

add_library(lapian_ffmpeg INTERFACE)
target_include_directories(lapian_ffmpeg INTERFACE "${LAPIAN_FFMPEG_INCLUDE_DIR}")
target_link_libraries(lapian_ffmpeg INTERFACE ${LAPIAN_FFMPEG_LIBRARIES})
add_library(lapian::ffmpeg ALIAS lapian_ffmpeg)

# 只复制真正链接的 5 个运行时 DLL，避免把 avfilter/avdevice/postproc 带进分发。
# 预编译包与自建包的 DLL 命名不同：自建 LGPL 包可能带版本后缀（如 avformat-62.dll）。
set(LAPIAN_FFMPEG_RUNTIME_DLLS "")
foreach(_lib IN ITEMS avformat avcodec avutil swscale swresample)
  file(GLOB _matched_dlls
    "${FFMPEG_ROOT}/bin/${_lib}.dll"
    "${FFMPEG_ROOT}/bin/${_lib}-*.dll")
  foreach(_dll IN LISTS _matched_dlls)
    list(APPEND LAPIAN_FFMPEG_RUNTIME_DLLS "${_dll}")
  endforeach()
endforeach()

# 把版本号带进配置日志，方便与 docs/deps.md 对照
if(EXISTS "${LAPIAN_FFMPEG_INCLUDE_DIR}/libavutil/ffversion.h")
  file(READ "${LAPIAN_FFMPEG_INCLUDE_DIR}/libavutil/ffversion.h" _lapian_ffversion)
  if(_lapian_ffversion MATCHES "FFMPEG_VERSION \"([^\"]+)\"")
    message(STATUS "FFmpeg: ${CMAKE_MATCH_1} (${FFMPEG_ROOT})")
  endif()
endif()

list(LENGTH LAPIAN_FFMPEG_RUNTIME_DLLS _lapian_dll_count)
if(_lapian_dll_count LESS 5)
  message(WARNING "FFmpeg bin/ 下只找到 ${_lapian_dll_count} 个运行时 DLL（期望 5 个）")
endif()
