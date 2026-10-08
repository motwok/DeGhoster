# A helper that always fails, for the integration tests (FailingHelper64.exe in
# the x64 configure, FailingHelper32.exe in the x86 one). Windows subsystem like
# the real helper, so it never flashes a console.
add_executable(FailingHelper WIN32 FailingHelper.cpp)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_bits 64)
else()
  set(_bits 32)
endif()
set_target_properties(FailingHelper PROPERTIES OUTPUT_NAME "FailingHelper${_bits}")

target_compile_definitions(FailingHelper PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_features(FailingHelper PRIVATE cxx_std_17)

set(_out "${CMAKE_SOURCE_DIR}/build")
set_target_properties(FailingHelper PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY         "${_out}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${_out}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_out}")
