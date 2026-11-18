include(FetchContent)

if(EMSCRIPTEN OR EL9950_FETCH_FRONTEND_DEPS)
  function(add_webp_decoder)
    set(BUILD_SHARED_LIBS OFF)
    foreach(component IN ITEMS ANIM_UTILS CWEBP DWEBP GIF2WEBP IMG2WEBP VWEBP WEBPINFO WEBPMUX LIBWEBPMUX EXTRAS)
      set(WEBP_BUILD_${component} OFF)
    endforeach()
    set(WEBP_USE_THREAD OFF)
    FetchContent_Declare(libwebp
      URL https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz
      URL_HASH SHA256=e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(libwebp)
    # Build and install only what the emulator uses, including with CMake 3.25.
    set_property(DIRECTORY "${libwebp_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
    target_include_directories(webpdecoder SYSTEM INTERFACE "$<BUILD_INTERFACE:${libwebp_SOURCE_DIR}/src>")
    add_library(WebP::webpdecoder ALIAS webpdecoder)
  endfunction()
  add_webp_decoder()
else()
  find_package(WebP CONFIG QUIET)
  if(NOT TARGET WebP::webpdecoder)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WEBPDECODER REQUIRED IMPORTED_TARGET libwebpdecoder)
    add_library(WebP::webpdecoder ALIAS PkgConfig::WEBPDECODER)
  endif()
endif()

if(EMSCRIPTEN)
  add_library(SDL3::SDL3 INTERFACE IMPORTED)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.2b
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(imgui)
  return()
endif()

if(NOT EL9950_FETCH_FRONTEND_DEPS)
  find_package(SDL3 CONFIG REQUIRED)
  set(imgui_SOURCE_DIR "" CACHE PATH "Path to a Dear ImGui checkout")
  if(NOT EXISTS "${imgui_SOURCE_DIR}/imgui.cpp")
    message(FATAL_ERROR "Set imgui_SOURCE_DIR when EL9950_FETCH_FRONTEND_DEPS=OFF")
  endif()
  return()
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build all fetched frontend dependencies statically" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.2.20
  GIT_SHALLOW TRUE
)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.2b
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL imgui)
