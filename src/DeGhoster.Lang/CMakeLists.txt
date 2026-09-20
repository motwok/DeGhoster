# Per-language resource-only DLLs used ONLY as muirct sources (not shipped).
# Each holds one STRINGTABLE and is named "DeGhoster.exe" so muirct computes the
# same checksum as the LN split, associating the resulting .mui with the exe.
# The host's POST_BUILD step turns these into build/<culture>/DeGhoster.exe.mui.
include("${CMAKE_SOURCE_DIR}/cmake/Languages.cmake")

set(_muisrc "${CMAKE_SOURCE_DIR}/build/_muisrc")
set(_src "${CMAKE_SOURCE_DIR}/src/DeGhoster")

foreach(entry ${DEGHOSTER_LANGS})
  string(REPLACE "=" ";" parts "${entry}")
  list(GET parts 0 culture)
  list(GET parts 2 rc)
  set(tgt "DeGhosterLang_${culture}")
  add_library(${tgt} SHARED "${CMAKE_CURRENT_SOURCE_DIR}/mui_stub.cpp" "${_src}/${rc}")
  target_include_directories(${tgt} PRIVATE "${_src}")
  target_link_options(${tgt} PRIVATE /NOENTRY /NODEFAULTLIB /MANIFEST:NO)
  set_target_properties(${tgt} PROPERTIES
    PREFIX "" OUTPUT_NAME "DeGhoster" SUFFIX ".exe"
    RUNTIME_OUTPUT_DIRECTORY "${_muisrc}/${culture}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_muisrc}/${culture}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${_muisrc}/${culture}")
endforeach()
