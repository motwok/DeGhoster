# Native x64/x86 hook DLL for DeGhoster.
# exports.def gives the entry points plain, undecorated names on BOTH bitnesses
# (x86 __stdcall would otherwise export _DgInstallHook@8 and break GetProcAddress).
add_library(DeGhoster.Hook SHARED Hook.cpp Hook.h res_version.rc exports.def)

# DLL name depends on bitness: DeGhoster.Hook64.dll or DeGhoster.Hook32.dll (without the "lib" prefix).
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_bits 64)
else()
  set(_bits 32)
endif()
set_target_properties(DeGhoster.Hook PROPERTIES OUTPUT_NAME "DeGhoster.Hook${_bits}" PREFIX "")

target_compile_definitions(DeGhoster.Hook PRIVATE UNICODE _UNICODE)
target_link_libraries(DeGhoster.Hook PRIVATE user32 dwmapi)

# Emit the finished DLL flat into <repo>/build/ (for all configurations); the import lib stays in the CMake tree.
set(_out "${CMAKE_SOURCE_DIR}/build")
set_target_properties(DeGhoster.Hook PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY         "${_out}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${_out}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_out}")
