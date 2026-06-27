cmake_minimum_required(VERSION 3.16)
project(OkaySpaceGameEngine
    VERSION 2.268.0
    DESCRIPTION "A Unity-inspired C++ game engine"
    LANGUAGES C CXX)   # C enabled for vendored single-file C libs (e.g. tinyfiledialogs)

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
option(OKAY_USE_SODIUM     "Authenticated packet encryption via libsodium"     ON)
option(OKAY_BUILD_TOOLS    "Build developer tools (texture generator, ...)"    OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Enable testing up front so subdirectories (editor, tests) can register tests.
if(OKAY_BUILD_TESTS)
    enable_testing()
endif()

# ---- Optional baked-in account server --------------------------------
# Compile a default account server URL + API key into every binary so a shipped
# build is online by default with no key file on disk. Intended to be injected
# from a CI secret at configure time:
#   cmake -DOKAY_DEFAULT_ACCOUNT_URL=https://xyz.supabase.co \
#         -DOKAY_DEFAULT_ACCOUNT_KEY=sb_publishable_...
# A Supabase publishable/anon key is safe to ship (protect data with RLS).
# Empty by default, so normal dev builds are unaffected. Applied to all targets
# created below (engine reads it via okay::Account; the launcher via its own
# OKAY_DEFAULT_ACCOUNT_* fallback).
set(OKAY_DEFAULT_ACCOUNT_URL "" CACHE STRING "Default account server URL compiled into builds")
set(OKAY_DEFAULT_ACCOUNT_KEY "" CACHE STRING "Default account API key compiled into builds")
if(OKAY_DEFAULT_ACCOUNT_URL)
    add_compile_definitions(OKAY_DEFAULT_ACCOUNT_URL="${OKAY_DEFAULT_ACCOUNT_URL}")
endif()
if(OKAY_DEFAULT_ACCOUNT_KEY)
    add_compile_definitions(OKAY_DEFAULT_ACCOUNT_KEY="${OKAY_DEFAULT_ACCOUNT_KEY}")
endif()

# ---- The engine library ----------------------------------------------
add_subdirectory(engine)

# ---- OkayUI (SDL_Renderer immediate-mode UI toolkit) -----------------
# Self-gates on SDL2 (skips quietly if absent). Added before the editor and tests
# so both can link okay::ui. Only needed by SDL-based hosts and its own test.
if(OKAY_BUILD_EDITOR OR OKAY_BUILD_PLAYER OR OKAY_BUILD_TESTS)
    add_subdirectory(okayui)
endif()

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

# ---- Ship SDL2.dll beside the binaries (Unity-style dynamic linking) ----
# Unless fully static (OKAY_STATIC_SDL), the Windows exes link SDL2 dynamically and
# need SDL2.dll next to them. Copy it into the output bin dir so a build is runnable
# in place and "Build Game" / packaging can grab it from there.
if(WIN32 AND NOT OKAY_STATIC_SDL AND DEFINED SDL2_DIR)
    # SDL2_DIR is .../<triple>/lib/cmake/SDL2; the DLL lives at .../<triple>/bin/SDL2.dll.
    set(_okay_sdl_dll "")
    foreach(_up "/../../../bin/SDL2.dll" "/../../bin/SDL2.dll")
        get_filename_component(_cand "${SDL2_DIR}${_up}" ABSOLUTE)
        if(EXISTS "${_cand}")
            set(_okay_sdl_dll "${_cand}")
            break()
        endif()
    endforeach()
    if(_okay_sdl_dll)
        add_custom_target(okay_copy_sdl2 ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_okay_sdl_dll}" "${CMAKE_BINARY_DIR}/bin/"
            COMMENT "Copying SDL2.dll next to the OkaySpace binaries")
    else()
        message(WARNING "SDL2.dll not found at ${_okay_sdl_dll}; ship it manually or use -DOKAY_STATIC_SDL=ON.")
    endif()
endif()

# ---- Developer tools (opt-in) ----------------------------------------
if(OKAY_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# ---- Reference relay (NAT traversal) ---------------------------------
# A tiny console UDP forwarder; no graphics deps, always built.
add_subdirectory(relay)

# ---- Tests ------------------------------------------------------------
if(OKAY_BUILD_TESTS)
    add_subdirectory(tests)
endif()
