cmake_minimum_required(VERSION 3.25)
project(DeGhoster LANGUAGES CXX)

# Emit PDBs for every configuration: required by OpenCppCoverage (see
# tests/coverage.ps1 / .github/workflows/coverage.yml) and useful for crash
# diagnostics. Release stays optimized - /DEBUG only adds symbols, and
# /OPT:REF,/OPT:ICF undo the linker's default de-optimization under /DEBUG.
# PDBs are not shipped (the ZIP/MSI copy an explicit file list).
if(MSVC)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "ProgramDatabase")
  add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()

# Derive the version from git (GitVersion) and generate src/Version.h for the
# binaries' VERSIONINFO resource. Runs for every configure (x86 and x64).
include("${CMAKE_SOURCE_DIR}/cmake/Version.cmake")

# Test ghost-window simulator (built for both bitnesses; not shipped).
option(DEGHOSTER_BUILD_TESTS "Build the GhostSim test simulator (x86 + x64)" ON)

# Native hook DLL (bitness = platform of the configure step).
add_subdirectory(src/DeGhoster.Hook)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  # x86 configure: 32-bit injector (the x64 host launches it for 32-bit targets).
  add_subdirectory(src/DeGhoster.Helper32)
else()
  # x64 configure: the host plus the satellite MUI resource DLLs.
  add_subdirectory(src/DeGhoster)
  add_subdirectory(src/DeGhoster.Lang)
endif()

# GhostSim is bitness-agnostic; build it in whichever configure is running so
# both GhostSim64.exe (x64) and GhostSim32.exe (x86) end up in build/.
if(DEGHOSTER_BUILD_TESTS)
  add_subdirectory(tests/GhostSim)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)   # native unit tests: x64 configure only
    add_subdirectory(tests/UnitTests)
  endif()
endif()
