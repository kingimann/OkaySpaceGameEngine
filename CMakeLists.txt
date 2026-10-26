cmake_minimum_required(VERSION 3.16)
project(OkaySpaceGameEngine
    VERSION 2.833.0
    DESCRIPTION "A modern C++ game engine"
    LANGUAGES C CXX)   # C enabled for vendored single-file C libs (e.g. tinyfiledialogs)

# Stamp the version into binaries (used by the editor's self-updater).
# The version string is only compiled into the editor and launcher (their targets
# add OKAY_ENGINE_VERSION), so a version bump doesn't change every file's compile
# flags and the release build's compiler cache keeps hitting.
set(OKAY_VERSION_DEFINE OKAY_ENGINE_VERSION="${PROJECT_VERSION}")

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
option(OKAY_USE_ASSIMP     "Import FBX/DAE/STL/... via Assimp (FetchContent; downloads at configure time)" OFF)
option(OKAY_BUILD_TOOLS    "Build developer tools (texture generator, ...)"    OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Sanitizer build (GCC/Clang), used by CI's sanitizer job. Off by default, e.g.
#   cmake -DOKAY_SANITIZE=address,undefined,float-cast-overflow ...
# Findings abort instead of printing and carrying on. GCC's "undefined" group
# leaves out float-cast-overflow (1e30 cast to an int), so name it explicitly.
set(OKAY_SANITIZE "" CACHE STRING "Build with -fsanitize=<list> (e.g. address,undefined); empty = off")
if(OKAY_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=${OKAY_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${OKAY_SANITIZE} -fno-sanitize-recover=all)
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
# Defaults point at the OkaySpace project so every build is online out of the
# box (accounts, Friends, Marketplace) even without CI secrets; override at
# configure time (or via the ACCOUNT_SERVER_URL/ACCOUNT_API_KEY CI secrets) to
# point a build at a different server.
set(OKAY_DEFAULT_ACCOUNT_URL "https://tgthoueacrlanpojxoen.supabase.co"
    CACHE STRING "Default account server URL compiled into builds")
set(OKAY_DEFAULT_ACCOUNT_KEY "sb_publishable_VYrh0rGNcTNTJRe1CovsoA_sP9ia4yn"
    CACHE STRING "Default account API key compiled into builds")
if(OKAY_DEFAULT_ACCOUNT_URL)
    add_compile_definitions(OKAY_DEFAULT_ACCOUNT_URL="${OKAY_DEFAULT_ACCOUNT_URL}")
endif()
if(OKAY_DEFAULT_ACCOUNT_KEY)
    add_compile_definitions(OKAY_DEFAULT_ACCOUNT_KEY="${OKAY_DEFAULT_ACCOUNT_KEY}")
endif()
# Default voice server for launcher calls ("host" or "host:port" of a running
# okayspace-relay; see docs/relay.md). Empty = players set one in Settings > Chat.
set(OKAY_DEFAULT_VOICE_RELAY "" CACHE STRING "Default voice relay (host[:port]) compiled into the launcher")
if(OKAY_DEFAULT_VOICE_RELAY)
    add_compile_definitions(OKAY_DEFAULT_VOICE_RELAY="${OKAY_DEFAULT_VOICE_RELAY}")
endif()
# Anti-cheat (opt-in, docs/anticheat.md): the ed25519 PUBLIC keys accepted on a
# build's Data/manifest.json, as hex, comma-separated (e.g. the release key, then
# the dev key). Public keys only; the private keys never go near CMake. Empty =
# none built in (a signature is checked but not its signer).
#  * OKAY_AC_TRUSTED_KEYS: the launcher's, which it applies to the official game.
#  * OKAY_AC_PLAYER_TRUSTED_KEYS: the player's. A player with keys refuses every
#    game not signed by them, so the general runtime (which runs anyone's
#    games) has none; set it only for a player that ships your own signed games.
set(OKAY_AC_TRUSTED_KEYS "" CACHE STRING "Anti-cheat: the launcher's trusted ed25519 public keys (hex, comma-separated)")
set(OKAY_AC_PLAYER_TRUSTED_KEYS "" CACHE STRING "Anti-cheat: the player's trusted ed25519 public keys (only for a player of your own signed games)")
include(${CMAKE_SOURCE_DIR}/cmake/OkayAntiCheatKeys.cmake)
okay_ac_check_trusted_keys("${OKAY_AC_TRUSTED_KEYS}")          # exactly 64 hex digits per key, or configure fails
okay_ac_check_trusted_keys("${OKAY_AC_PLAYER_TRUSTED_KEYS}")
# The salt of the anti-cheat hardware hash (shared/AntiCheatHardware.hpp): give your
# deployment its own so its hashes can't be matched with another's. Not a secret.
set(OKAY_AC_HW_SALT "" CACHE STRING "Anti-cheat: hardware hash salt (empty = the built-in default)")
if(OKAY_AC_HW_SALT)
    string(FIND "${OKAY_AC_HW_SALT}" "\"" _okay_ac_q)
    string(FIND "${OKAY_AC_HW_SALT}" "\\" _okay_ac_b)
    if(NOT _okay_ac_q EQUAL -1 OR NOT _okay_ac_b EQUAL -1)
        message(FATAL_ERROR "OKAY_AC_HW_SALT can't contain quotes or backslashes")
    endif()
    add_compile_definitions(OKAY_AC_HW_SALT="${OKAY_AC_HW_SALT}")
endif()

# The Dear ImGui *docking-branch* commit the editor (and the editor-model test)
# builds against; see editor/CMakeLists.txt. Override with -DOKAY_IMGUI_TAG=<ref>.
set(OKAY_IMGUI_TAG "036bf939b6f8d74ad76bcf926b757c56e68c54ff"
    CACHE STRING "Dear ImGui git commit/branch to fetch (must be the docking branch)")

# ---- The engine library ----------------------------------------------
add_subdirectory(engine)

# ---- OkayUI (SDL_Renderer immediate-mode UI toolkit) -----------------
# Self-gates on SDL2 (skips quietly if absent). Added before the editor and tests
# so both can link okay::ui. Only needed by SDL-based hosts and its own test.
# Skip OkayUI on Emscripten/WebGL: the web player links only okay::engine, and the
# toolkit includes a Direct3D 11 backend that can't compile to WASM.
if((OKAY_BUILD_EDITOR OR OKAY_BUILD_PLAYER OR OKAY_BUILD_TESTS) AND NOT EMSCRIPTEN)
    add_subdirectory(okayui)
endif()

# ---- Sample game ------------------------------------------------------
if(OKAY_BUILD_SANDBOX)
    add_subdirectory(sandbox)
endif()

# ---- Self-updating launcher ------------------------------------------
if(OKAY_BUILD_LAUNCHER)
    add_subdirectory(launcher)
    if(OKAY_AC_TRUSTED_KEYS AND TARGET okayspace-launcher)
        target_compile_definitions(okayspace-launcher PRIVATE OKAY_AC_TRUSTED_KEYS="${OKAY_AC_TRUSTED_KEYS}")
    endif()
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
    if(OKAY_AC_PLAYER_TRUSTED_KEYS AND TARGET okay-player)
        target_compile_definitions(okay-player PRIVATE OKAY_AC_TRUSTED_KEYS="${OKAY_AC_PLAYER_TRUSTED_KEYS}")
    endif()
endif()

# ---- Ship SDL2.dll beside the binaries ----
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
# A tiny console UDP forwarder; no graphics deps, always built — except on
# Emscripten/WebGL, where raw UDP sockets don't exist in the browser sandbox.
if(NOT EMSCRIPTEN)
    add_subdirectory(relay)
endif()

# ---- Tests ------------------------------------------------------------
if(OKAY_BUILD_TESTS)
    add_subdirectory(tests)
endif()
