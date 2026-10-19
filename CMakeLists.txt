cmake_minimum_required(VERSION 3.25)

project(lapian
  VERSION 0.1.0
  DESCRIPTION "拉片软件 — 帧级参考片审阅器（T0.1 骨架）"
  LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "拉片软件仅支持 Windows。")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "仅支持 x64 构建。")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LAPIAN_BUILD_TESTS "构建单元测试" ON)
option(LAPIAN_WARNINGS_AS_ERRORS "把编译警告当作错误" OFF)

include(CompilerOptions)
include(Dependencies)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup(REQUIRES 6.8)

message(STATUS "Qt: ${Qt6_VERSION} (${Qt6_DIR})")

add_subdirectory(src)

if(LAPIAN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
