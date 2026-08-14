cmake_minimum_required(VERSION 3.25)
project(el9950_emu VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EL9950_BUILD_TESTS "Build deterministic headless tests" ON)
option(EL9950_BUILD_FRONTEND "Build the SDL3 + Dear ImGui application" ON)
option(EL9950_FETCH_FRONTEND_DEPS "Fetch pinned SDL3/ImGui dependencies" ON)

add_library(el9950_core
  src/bus.cpp
  src/cpu_tlcs900.cpp
  src/crc32.cpp
  src/debugger.cpp
  src/emulator.cpp
  src/flash.cpp
  src/keyboard.cpp
  src/lcd.cpp
  src/link.cpp
  src/rom.cpp
  src/sha256.cpp
  src/tmp91c025.cpp
  third_party/mame-tlcs900/tlcs900.cpp
  third_party/mame-tlcs900/dasm900.cpp
)
target_include_directories(el9950_core
  PUBLIC include
  PRIVATE third_party/mame-tlcs900
)
target_compile_definitions(el9950_core PRIVATE EL9950_STANDALONE_MAME_CORE=1)
if(MSVC)
  target_compile_options(el9950_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(el9950_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(el9950-headless src/headless_main.cpp)
target_link_libraries(el9950-headless PRIVATE el9950_core)

if(EL9950_BUILD_FRONTEND)
  include(cmake/Dependencies.cmake)
  add_executable(el9950-emu
    src/frontend/app.cpp
    src/frontend/main.cpp
    src/frontend/stb_image.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
  )
  target_include_directories(el9950-emu PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends third_party/stb)
  target_link_libraries(el9950-emu PRIVATE el9950_core SDL3::SDL3)
  if(EMSCRIPTEN)
    target_compile_options(el9950-emu PRIVATE -sUSE_SDL=3)
    set_target_properties(el9950-emu PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    target_link_options(el9950-emu PRIVATE
      "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
      "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets/SHARP_EL-9950G.png@/assets/SHARP_EL-9950G.png"
      -sALLOW_MEMORY_GROWTH=1 -sASYNCIFY=1 -sUSE_SDL=3
      "-sEXPORTED_FUNCTIONS=[\"_main\",\"_el9950_web_load_rom\"]"
      "-sEXPORTED_RUNTIME_METHODS=[\"ccall\",\"FS\"]"
      -lidbfs.js
    )
    target_compile_definitions(el9950-emu PRIVATE EL9950_WEB=1 EL9950_SKIN_PATH="/assets/SHARP_EL-9950G.png")
  elseif(APPLE)
    target_sources(el9950-emu PRIVATE assets/SHARP_EL-9950G.png)
    set_source_files_properties(assets/SHARP_EL-9950G.png PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_compile_definitions(el9950-emu PRIVATE EL9950_BUNDLE_SKIN=1)
    set_target_properties(el9950-emu PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_BUNDLE_NAME "EL-9950 Emulator"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_GUI_IDENTIFIER "org.tiplanet.el9950-emu"
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
    )
  else()
    target_compile_definitions(el9950-emu PRIVATE EL9950_SKIN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/SHARP_EL-9950G.png")
  endif()
endif()

if(EL9950_BUILD_TESTS)
  enable_testing()
  add_executable(el9950-tests tests/tests.cpp)
  target_link_libraries(el9950-tests PRIVATE el9950_core)
  add_test(NAME el9950-tests COMMAND el9950-tests)
endif()
