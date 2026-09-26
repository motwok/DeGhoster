# AnyDesk session simulator for the cursor overlay tests (built for both
# bitnesses, like the real 32-bit AnyDesk client: CursorSim64.exe in the x64
# configure, CursorSim32.exe in the x86 configure). Not shipped.
add_executable(CursorSim CursorSim.cpp)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_bits 64)
else()
  set(_bits 32)
endif()
set_target_properties(CursorSim PROPERTIES OUTPUT_NAME "CursorSim${_bits}")

target_compile_definitions(CursorSim PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_features(CursorSim PRIVATE cxx_std_17)
target_link_libraries(CursorSim PRIVATE user32 gdi32)

set(_out "${CMAKE_SOURCE_DIR}/build")
set_target_properties(CursorSim PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY         "${_out}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${_out}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_out}")
