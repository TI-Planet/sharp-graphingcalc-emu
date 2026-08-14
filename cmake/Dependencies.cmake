include(FetchContent)

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
