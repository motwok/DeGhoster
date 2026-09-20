# Native x64/x86-Hook-DLL fuer DeGhoster.
# exports.def gives the entry points plain, undecorated names on BOTH bitnesses
# (x86 __stdcall would otherwise export _DgInstallHook@8 and break GetProcAddress).
add_library(DeGhoster.Hook SHARED Hook.cpp Hook.h res_version.rc exports.def)

# DLL-Name je nach Bitness: DeGhoster.Hook64.dll bzw. DeGhoster.Hook32.dll (ohne "lib"-Praefix).
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_bits 64)
else()
  set(_bits 32)
endif()
set_target_properties(DeGhoster.Hook PROPERTIES OUTPUT_NAME "DeGhoster.Hook${_bits}" PREFIX "")

target_compile_definitions(DeGhoster.Hook PRIVATE UNICODE _UNICODE)
target_link_libraries(DeGhoster.Hook PRIVATE user32 dwmapi)

# Fertige DLL flach nach <repo>/build/ (fuer alle Konfigurationen); Import-Lib bleibt im CMake-Baum.
set(_out "${CMAKE_SOURCE_DIR}/build")
set_target_properties(DeGhoster.Hook PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY         "${_out}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${_out}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_out}")
