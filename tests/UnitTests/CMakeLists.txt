# Native unit tests (links the real host sources against a throwaway registry
# root). Built only in the x64 configure; not shipped.
add_executable(UnitTests UnitTests.cpp
  "${CMAKE_SOURCE_DIR}/src/DeGhoster/Settings.cpp"
  "${CMAKE_SOURCE_DIR}/src/DeGhoster/Theme.cpp"
  "${CMAKE_SOURCE_DIR}/src/DeGhoster/ProcessUtil.cpp"
  "${CMAKE_SOURCE_DIR}/src/DeGhoster/HookInjector.cpp"
  "${CMAKE_SOURCE_DIR}/src/DeGhoster/CursorImage.cpp"
  "${CMAKE_SOURCE_DIR}/src/DeGhoster/AutoZoom.cpp"
  "${CMAKE_SOURCE_DIR}/src/DeGhoster/CursorOverlay.cpp"
  "${CMAKE_SOURCE_DIR}/src/DeGhoster/GhostEngine.cpp"
  "${CMAKE_SOURCE_DIR}/src/DeGhoster/Controls.cpp"
  "${CMAKE_SOURCE_DIR}/src/DeGhoster/SettingsWindow.cpp"
  "${CMAKE_SOURCE_DIR}/src/DeGhoster/InfoWindow.cpp"
  "${CMAKE_SOURCE_DIR}/src/DeGhoster/HoverButton.cpp"
  "${CMAKE_SOURCE_DIR}/src/DeGhoster/Gfx.cpp"
  "${CMAKE_SOURCE_DIR}/src/DeGhoster/Loc.cpp"
  "${CMAKE_SOURCE_DIR}/src/DeGhoster/PopupMenu.cpp")
set_target_properties(UnitTests PROPERTIES OUTPUT_NAME "UnitTests")
target_include_directories(UnitTests PRIVATE
  "${CMAKE_SOURCE_DIR}/src/DeGhoster"
  "${CMAKE_SOURCE_DIR}/src/DeGhoster.Hook")   # Hook.h (DGH_CLOAK / DWMWA_CLOAK)
target_compile_definitions(UnitTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_features(UnitTests PRIVATE cxx_std_17)
target_compile_options(UnitTests PRIVATE "/utf-8")
target_link_libraries(UnitTests PRIVATE user32 gdi32 dwmapi advapi32 comctl32 shell32 gdiplus
  magnification shcore uxtheme)

set(_out "${CMAKE_SOURCE_DIR}/build")
set_target_properties(UnitTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY         "${_out}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${_out}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_out}")
