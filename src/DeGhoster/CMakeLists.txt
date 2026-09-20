# Native Win32 host (x64), dependency-free.
add_executable(DeGhosterApp WIN32
  main.cpp
  MainWindow.cpp
  InfoWindow.cpp
  GhostEngine.cpp
  HookInjector.cpp
  Settings.cpp
  Theme.cpp
  ProcessUtil.cpp
  HoverButton.cpp
  Gfx.cpp
  Loc.cpp
  Autostart.cpp
  DeGhoster.rc)
set_target_properties(DeGhosterApp PROPERTIES OUTPUT_NAME "DeGhoster")
target_compile_definitions(DeGhosterApp PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_features(DeGhosterApp PRIVATE cxx_std_17)
# Source is UTF-8 (em-dash, ©): make MSVC read it as such.
target_compile_options(DeGhosterApp PRIVATE "/utf-8")
# Share the host<->hook message constants.
target_include_directories(DeGhosterApp PRIVATE "${CMAKE_SOURCE_DIR}/src/DeGhoster.Hook")
target_link_libraries(DeGhosterApp PRIVATE comctl32 dwmapi shell32 uxtheme gdiplus user32 gdi32)
# Manifest comes from DeGhoster.rc; suppress the linker-generated one.
target_link_options(DeGhosterApp PRIVATE "/MANIFEST:NO")

set(_out "${CMAKE_SOURCE_DIR}/build")
# NOTE: the host exe is intentionally NOT linked straight into build/. The
# en-US MUI split (below) reads the freshly linked, full exe via
# $<TARGET_FILE:DeGhosterApp> and writes the language-neutral module to
# build/DeGhoster.exe. Keeping the linker output separate makes the split
# idempotent (muirct refuses to split an already-split MUI file), so
# incremental rebuilds don't fail.

# Ship license/third-party notices as .txt next to the exe (info dialog links NOTICE.txt).
add_custom_command(TARGET DeGhosterApp POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE" "${_out}/LICENSE.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/NOTICE"  "${_out}/NOTICE.txt"
  VERBATIM)

# --- MUI: split the exe into a language-neutral module + per-language .mui ------
# muirct moves the STRINGTABLE into <culture>\DeGhoster.exe.mui; the OS resource
# loader then picks the language automatically (en-US is the ultimate fallback).
file(GLOB _muircts
  "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/muirct.exe"
  "C:/Program Files/Windows Kits/10/bin/*/x64/muirct.exe")
if(NOT _muircts)
  message(FATAL_ERROR "muirct.exe not found in the Windows SDK (needed for MUI localization).")
endif()
list(SORT _muircts)
list(REVERSE _muircts)
list(GET _muircts 0 MUIRCT)

set(_cfg "${CMAKE_CURRENT_SOURCE_DIR}/rcconfig.xml")
set(_muisrc "${_out}/_muisrc")
include("${CMAKE_SOURCE_DIR}/cmake/Languages.cmake")

# en-US: split the freshly linked (full) exe -> LN module written to
# build/DeGhoster.exe + en-US\DeGhoster.exe.mui. Reading $<TARGET_FILE> (which
# muirct never modifies) instead of splitting build/DeGhoster.exe in place keeps
# this step idempotent across incremental rebuilds.
add_custom_command(TARGET DeGhosterApp POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_out}/en-US"
  COMMAND "${MUIRCT}" -q "${_cfg}" -g 0x409 -x 0x409 "$<TARGET_FILE:DeGhosterApp>" "${_out}/DeGhoster.exe" "${_out}/en-US/DeGhoster.exe.mui"
  VERBATIM)

# Each additional language: build its .mui from the matching resource source.
foreach(entry ${DEGHOSTER_LANGS})
  string(REPLACE "=" ";" parts "${entry}")
  list(GET parts 0 culture)
  list(GET parts 1 langid)
  add_dependencies(DeGhosterApp "DeGhosterLang_${culture}")
  add_custom_command(TARGET DeGhosterApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_out}/${culture}"
    COMMAND "${MUIRCT}" -q "${_cfg}" -g 0x409 -x ${langid}
            "${_muisrc}/${culture}/DeGhoster.exe" "${_muisrc}/${culture}/DeGhoster.ln"
            "${_out}/${culture}/DeGhoster.exe.mui"
    VERBATIM)
endforeach()
