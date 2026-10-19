# 统一编译选项：C++20、MSVC 警告与 UTF-8 源文件
function(lapian_apply_common_options target)
  target_compile_features(${target} PRIVATE cxx_std_20)

  target_compile_definitions(${target} PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS)

  if(MSVC)
    target_compile_options(${target} PRIVATE
      /W4
      /permissive-
      /utf-8
      /Zc:__cplusplus
      /EHsc
      /MP
      $<$<CONFIG:Release>:/O2>)

    if(LAPIAN_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  endif()
endfunction()
