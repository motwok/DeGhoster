# Versioning via GitVersion (https://gitversion.net).
#
# Runs GitVersion at configure time, exposes the results as CMake variables and
# generates src/DeGhoster/Version.h (from src/Version.h.in) for the binaries'
# VERSIONINFO resource. Also writes build/version.json so the ZIP/MSI packaging
# scripts name their artifacts and stamp the MSI ProductVersion consistently.
#
# If GitVersion cannot be found (e.g. a source drop without git, or the tool is
# not installed) the build still succeeds with a 0.0.0 fallback and a warning.

# Locate a GitVersion runner: prefer the pinned local tool, then a global tool.
find_program(DOTNET_EXECUTABLE NAMES dotnet)
find_program(GITVERSION_EXECUTABLE NAMES dotnet-gitversion gitversion)

set(_gv_json "")
set(_gv_ok FALSE)

# Try `dotnet gitversion` (respects .config/dotnet-tools.json) first, then a
# standalone gitversion/dotnet-gitversion on PATH.
if(DOTNET_EXECUTABLE)
  execute_process(
    COMMAND "${DOTNET_EXECUTABLE}" tool restore
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE _restore_rc)
  execute_process(
    COMMAND "${DOTNET_EXECUTABLE}" gitversion
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE _gv_json ERROR_VARIABLE _gv_err RESULT_VARIABLE _gv_rc
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(_gv_rc EQUAL 0 AND _gv_json)
    set(_gv_ok TRUE)
  endif()
endif()

if(NOT _gv_ok AND GITVERSION_EXECUTABLE)
  execute_process(
    COMMAND "${GITVERSION_EXECUTABLE}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE _gv_json ERROR_VARIABLE _gv_err RESULT_VARIABLE _gv_rc
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(_gv_rc EQUAL 0 AND _gv_json)
    set(_gv_ok TRUE)
  endif()
endif()

# Small helper to pull a scalar string field out of the GitVersion JSON.
macro(_gv_field _key _var _default)
  set(${_var} "${_default}")
  if(_gv_ok)
    string(JSON _tmp ERROR_VARIABLE _jsonerr GET "${_gv_json}" "${_key}")
    if(NOT _jsonerr AND NOT _tmp STREQUAL "")
      set(${_var} "${_tmp}")
    endif()
  endif()
endmacro()

if(_gv_ok)
  _gv_field("Major"                DGV_MAJOR         0)
  _gv_field("Minor"                DGV_MINOR         0)
  _gv_field("Patch"                DGV_PATCH         0)
  _gv_field("CommitsSinceVersionSource" DGV_REVISION 0)
  _gv_field("MajorMinorPatch"      DGV_MMP           "0.0.0")
  _gv_field("FullSemVer"           DGV_FULLSEMVER    "0.0.0")
  _gv_field("InformationalVersion" DGV_INFORMATIONAL "0.0.0")
  _gv_field("ShortSha"             DGV_SHORTSHA      "")
else()
  message(WARNING
    "GitVersion not available; using 0.0.0 fallback for the version resource. "
    "Install it with: dotnet tool restore  (or  dotnet tool install -g gitversion.tool)")
  set(DGV_MAJOR 0)
  set(DGV_MINOR 0)
  set(DGV_PATCH 0)
  set(DGV_REVISION 0)
  set(DGV_MMP "0.0.0")
  set(DGV_FULLSEMVER "0.0.0")
  set(DGV_INFORMATIONAL "0.0.0")
  set(DGV_SHORTSHA "")
endif()

# CommitsSinceVersionSource can be null/empty on a clean tagged commit.
if(NOT DGV_REVISION MATCHES "^[0-9]+$")
  set(DGV_REVISION 0)
endif()

message(STATUS "DeGhoster version: ${DGV_INFORMATIONAL} "
               "(file ${DGV_MAJOR}.${DGV_MINOR}.${DGV_PATCH}.${DGV_REVISION}, "
               "MSI ${DGV_MMP})")

# Generate the version header consumed by src/version.rc (same directory, so
# every wrapper's  #include "../version.rc"  ->  #include "Version.h"  resolves).
configure_file(
  "${CMAKE_SOURCE_DIR}/src/Version.h.in"
  "${CMAKE_SOURCE_DIR}/src/Version.h"
  @ONLY)

# Expose the version to the packaging scripts (single source of truth).
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/version.json.in"
  "${CMAKE_SOURCE_DIR}/build/version.json"
  @ONLY)
