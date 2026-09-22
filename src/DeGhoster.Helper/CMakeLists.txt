# Injector helper. Built in BOTH configures: the host never loads a hook DLL
# itself, it always delegates to a helper of the target's bitness (see ADR-0005).
add_executable(DeGhoster.Helper WIN32 Helper.cpp res_version.rc)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_bits 64)
else()
  set(_bits 32)
endif()
set_target_properties(DeGhoster.Helper PROPERTIES OUTPUT_NAME "DeGhoster.Helper${_bits}")

target_compile_definitions(DeGhoster.Helper PRIVATE UNICODE _UNICODE)
target_link_libraries(DeGhoster.Helper PRIVATE user32 shell32)

set(_out "${CMAKE_SOURCE_DIR}/build")
set_target_properties(DeGhoster.Helper PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY         "${_out}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${_out}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_out}")
