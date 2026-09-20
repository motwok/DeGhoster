# Ghost-window simulator for the integration tests (built for both bitnesses:
# GhostSim64.exe in the x64 configure, GhostSim32.exe in the x86 configure).
# Console subsystem so the harness can also read the HWND from stdout.
add_executable(GhostSim GhostSim.cpp)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_bits 64)
else()
  set(_bits 32)
endif()
set_target_properties(GhostSim PROPERTIES OUTPUT_NAME "GhostSim${_bits}")

target_compile_definitions(GhostSim PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_features(GhostSim PRIVATE cxx_std_17)
target_link_libraries(GhostSim PRIVATE user32 dwmapi)

set(_out "${CMAKE_SOURCE_DIR}/build")
set_target_properties(GhostSim PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY         "${_out}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${_out}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_out}")
