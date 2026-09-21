# 32-bit injector (built only in the x86 configure).
add_executable(DeGhoster.Helper32 WIN32 Helper.cpp res_version.rc)
set_target_properties(DeGhoster.Helper32 PROPERTIES OUTPUT_NAME "DeGhoster.Helper32")
target_compile_definitions(DeGhoster.Helper32 PRIVATE UNICODE _UNICODE)
target_link_libraries(DeGhoster.Helper32 PRIVATE user32 shell32)

set(_out "${CMAKE_SOURCE_DIR}/build")
set_target_properties(DeGhoster.Helper32 PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY         "${_out}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${_out}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_out}")
