cmake_minimum_required(VERSION 3.20)

project(egg_hunt LANGUAGES C CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link the CRT statically on MSVC so game_app.exe runs on a clean machine with
# no Visual C++ Redistributable installed. Without this the executable imports
# MSVCP140.dll / VCRUNTIME140.dll and fails to start on a stock Windows box --
# a demo-day failure mode that never shows up on a developer machine.
# Must be set before FetchContent_MakeAvailable so raylib and Catch2 inherit it;
# mixing static and dynamic CRTs in one binary is a link error.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG c1ab645ca298a2801097931d1079b10ff7eb9df8
    GIT_SHALLOW FALSE
)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG 2b60af89e23d28eefc081bc930831ee9d45ea58b
    GIT_SHALLOW FALSE
)

FetchContent_MakeAvailable(raylib Catch2)

add_library(
    game_core
    STATIC
    src/core/fixed.h
    src/core/rng.h
    src/core/input.h
    src/core/events.h
    src/core/map.h
    src/core/map.cpp
    src/core/state.h
    src/core/state.cpp
    src/core/player.h
    src/core/player.cpp
    src/core/entities.h
    src/core/entities.cpp
    src/core/framebuffer.h
    src/core/render.cpp
    src/core/raycast.h
    src/core/raycast.cpp
    src/core/textures.h
    src/core/textures.cpp
    src/core/sprites.h
    src/core/sprites.cpp
    src/core/hud.h
    src/core/hud.cpp
    src/core/audio.h
    src/core/audio.cpp
)
target_include_directories(game_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(game_core PUBLIC cxx_std_20)

add_executable(game_app src/app/main.cpp)
target_link_libraries(game_app PRIVATE game_core raylib)
target_compile_features(game_app PRIVATE cxx_std_20)

enable_testing()
# Each test file is owned by one workstream; sessions must not add sources to this list.
add_executable(
    game_tests
    tests/test_smoke.cpp
    tests/test_raycast.cpp
    tests/test_player.cpp
    tests/test_entities.cpp
    tests/test_sprites.cpp
    tests/test_hud.cpp
    tests/test_audio.cpp
    tests/test_replay.cpp
    tests/test_render.cpp
)
target_link_libraries(game_tests PRIVATE game_core Catch2::Catch2WithMain)
target_compile_features(game_tests PRIVATE cxx_std_20)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(game_tests)
