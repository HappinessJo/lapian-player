cmake_minimum_required(VERSION 3.25)
project(growing_probe LANGUAGES CXX)

# Standalone S2 spike. Not part of the product build.
# Configure:
#   cmake -S docs/perf/spike/growing -B <build> -G "Visual Studio 17 2022" -A x64 ^
#         -DFFMPEG_ROOT=<repo>/third_party/ffmpeg/build-msvc/ffmpeg-7.1.1-minimal

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT FFMPEG_ROOT)
  message(FATAL_ERROR "pass -DFFMPEG_ROOT=<dir containing include/ and lib/>")
endif()

add_executable(growing_probe growing_probe.cpp)
target_include_directories(growing_probe PRIVATE "${FFMPEG_ROOT}/include")
target_link_directories(growing_probe PRIVATE "${FFMPEG_ROOT}/lib")
target_link_libraries(growing_probe PRIVATE avformat avcodec avutil)

if(MSVC)
  target_compile_options(growing_probe PRIVATE /utf-8 /W3)
endif()
