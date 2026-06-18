cmake_minimum_required(VERSION 3.16)
project(OkaySpaceGameEngine
    VERSION 0.89.0
    DESCRIPTION "A Unity-inspired C++ game engine"
    LANGUAGES CXX)

# Stamp the version into binaries (used by the editor's self-updater).
add_compile_definitions(OKAY_ENGINE_VERSION="${PROJECT_VERSION}")

# ---- Global settings --------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OKAY_BUILD_SANDBOX  "Build the sandbox demo game"       ON)
option(OKAY_BUILD_TESTS    "Build the unit tests"              ON)
option(OKAY_BUILD_LAUNCHER "Build the self-updating launcher"  ON)
option(OKAY_BUILD_EDITOR   "Build the desktop GUI editor (needs SDL2+OpenGL)" OFF)
option(OKAY_BUILD_PLAYER   "Build the standalone game player runtime (needs SDL2)" OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Enable testing up front so subdirectories (editor, tests) can register tests.
if(OKAY_BUILD_TESTS)
    enable_testing()
endif()

# ---- The engine library ----------------------------------------------
add_subdirectory(engine)

# ---- Sample game ------------------------------------------------------
if(OKAY_BUILD_SANDBOX)
    add_subdirectory(sandbox)
endif()

# ---- Self-updating launcher ------------------------------------------
if(OKAY_BUILD_LAUNCHER)
    add_subdirectory(launcher)
endif()

# ---- Feature examples -------------------------------------------------
if(OKAY_BUILD_SANDBOX)
    add_subdirectory(examples)
endif()

# ---- Desktop GUI editor ----------------------------------------------
if(OKAY_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

# ---- Standalone game player ------------------------------------------
# The editor's "Build Game" ships this runtime, so build it alongside the editor.
if(OKAY_BUILD_PLAYER OR OKAY_BUILD_EDITOR)
    add_subdirectory(player)
endif()

# ---- Tests ------------------------------------------------------------
if(OKAY_BUILD_TESTS)
    add_subdirectory(tests)
endif()
