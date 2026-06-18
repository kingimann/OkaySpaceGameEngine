cmake_minimum_required(VERSION 3.16)
project(OkaySpaceGameEngine
    VERSION 0.1.0
    DESCRIPTION "A Unity-inspired C++ game engine"
    LANGUAGES CXX)

# ---- Global settings --------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OKAY_BUILD_SANDBOX "Build the sandbox demo game" ON)
option(OKAY_BUILD_TESTS   "Build the unit tests"        ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- The engine library ----------------------------------------------
add_subdirectory(engine)

# ---- Sample game ------------------------------------------------------
if(OKAY_BUILD_SANDBOX)
    add_subdirectory(sandbox)
endif()

# ---- Tests ------------------------------------------------------------
if(OKAY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
