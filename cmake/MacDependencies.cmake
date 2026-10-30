set(SOUNDTOUCH_ROOT "" CACHE PATH "SoundTouch 前缀目录")
find_path(LAPIAN_SOUNDTOUCH_INCLUDE_DIR
  NAMES SoundTouch.h
  HINTS "${SOUNDTOUCH_ROOT}/include"
  PATH_SUFFIXES soundtouch
  REQUIRED)
find_library(LAPIAN_SOUNDTOUCH_LIBRARY
  NAMES soundtouch
  HINTS "${SOUNDTOUCH_ROOT}/lib"
  REQUIRED)
add_library(soundtouch UNKNOWN IMPORTED GLOBAL)
set_target_properties(soundtouch PROPERTIES
  IMPORTED_LOCATION "${LAPIAN_SOUNDTOUCH_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${LAPIAN_SOUNDTOUCH_INCLUDE_DIR}")

function(lapian_copy_soundtouch_dll target)
endfunction()

set(CURL_ROOT "" CACHE PATH "libcurl 前缀目录")
find_path(LAPIAN_CURL_INCLUDE_DIR
  NAMES curl/curl.h
  HINTS "${CURL_ROOT}/include"
  REQUIRED)
find_library(LAPIAN_CURL_LIBRARY
  NAMES curl
  HINTS "${CURL_ROOT}/lib"
  REQUIRED)
add_library(lapian_curl UNKNOWN IMPORTED GLOBAL)
set_target_properties(lapian_curl PROPERTIES
  IMPORTED_LOCATION "${LAPIAN_CURL_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${LAPIAN_CURL_INCLUDE_DIR}")
add_library(lapian::curl ALIAS lapian_curl)

find_library(LAPIAN_SECURITY_FRAMEWORK Security REQUIRED)
find_library(LAPIAN_AUDIOUNIT_FRAMEWORK AudioUnit REQUIRED)
find_library(LAPIAN_COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
