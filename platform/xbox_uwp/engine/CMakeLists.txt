# okay_engine_uwp: the engine reduced to what a UWP app (Xbox One, Developer Mode) may use. EXPERIMENTAL, UNCOMPILED, UNVERIFIED.
# Only configured when OKAY_BUILD_XBOX_UWP is ON and the toolchain is a UWP one (CMAKE_SYSTEM_NAME=WindowsStore); anything else leaves it out,
# so the desktop build is untouched. Included by the UWP app project (platform/xbox_uwp/app) with -DOKAY_SOURCE_ROOT=<repo>.
#
# What it leaves out or replaces (docs/xbox_uwp.md, "The reduced engine target"; the list is checked by okay-uwp-audit):
#   OpenGL renderer, Direct3D 12 renderer, Photon adapter      -> uwp_exclude.txt
#   d3dcompiler (D3DCompile)       -> not linked; D3D11Renderer loads .cso files (ShaderBundle.hpp) when OKAY_XBOX_UWP is defined
#   winhttp                        -> not linked; NativeHttp.hpp has no native client under OKAY_XBOX_UWP (HTTP calls report "not reached")
#   dbghelp (crash handler)        -> lives in shared/, not part of the engine list
#   LoadLibraryA (XInput, GL, advapi32) -> Windows.Gaming.Input (XboxInput.hpp); BCryptGenRandom for random bytes
#   SDL2, win32 window, WGL        -> the UWP platform backend (platform/xbox_uwp/XboxUwpBackend.hpp) and UwpPlatform.cpp replace Platform.cpp
#   getenv("APPDATA"/"LOCALAPPDATA") -> Paths::SetEnvOverride(ApplicationData folders)
#   libsodium                      -> not available: packet encryption OFF by default (OKAY_XBOX_OWN_CRYPTO=ON is an experiment), Ed25519 uses okay::crypto (unreviewed; this target is never a release)
#   ufbx, Assimp                   -> not compiled: our own readers only (OKAY_OWN_*_DEFAULT below)
# 2D drawing: SDL_Renderer does not exist here. The software painter (okayui/soft) draws into RGBA pixels that the app shell shows with
# the Direct3D 11 swap chain (XboxPresent.hpp) or Window::PresentPixels.
cmake_minimum_required(VERSION 3.20)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    message(FATAL_ERROR "okay_engine_uwp is only for a UWP toolchain (-DCMAKE_SYSTEM_NAME=WindowsStore). It is not part of the desktop build.")
endif()
if(NOT OKAY_SOURCE_ROOT)
    message(FATAL_ERROR "Pass -DOKAY_SOURCE_ROOT=<the OkaySpace source folder> (scripts/build-xbox-uwp.ps1 and the editor do).")
endif()
include(${CMAKE_CURRENT_LIST_DIR}/uwp_sources.cmake)

set(_eng_cmake "${OKAY_SOURCE_ROOT}/engine/CMakeLists.txt")
okay_uwp_collect(okay_crypto "${_eng_cmake}" _crypto_srcs)
okay_uwp_collect(okay_engine "${_eng_cmake}" _engine_srcs)
okay_uwp_excluded("${CMAKE_CURRENT_LIST_DIR}/uwp_exclude.txt" _excluded)
if(NOT _engine_srcs OR NOT _crypto_srcs)
    message(FATAL_ERROR "Could not read the source lists from ${_eng_cmake}")
endif()
set(_kept "")
foreach(_s IN LISTS _engine_srcs)
    if(NOT _s IN_LIST _excluded)
        list(APPEND _kept "${OKAY_SOURCE_ROOT}/engine/${_s}")
    endif()
endforeach()
set(_crypto "")
foreach(_s IN LISTS _crypto_srcs)
    list(APPEND _crypto "${OKAY_SOURCE_ROOT}/engine/${_s}")
endforeach()

set(_defs OKAY_XBOX_UWP=1 WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS OKAY_ENGINE_VERSION="uwp-experimental")
set(_incs ${OKAY_SOURCE_ROOT}/engine/include ${OKAY_SOURCE_ROOT}/engine/src ${OKAY_SOURCE_ROOT}/shared)

add_library(okay_crypto_uwp STATIC ${_crypto})
target_include_directories(okay_crypto_uwp PUBLIC ${OKAY_SOURCE_ROOT}/engine/include)
target_compile_definitions(okay_crypto_uwp PUBLIC OKAY_XBOX_UWP=1 WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(okay_crypto_uwp PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_features(okay_crypto_uwp PUBLIC cxx_std_17)
target_link_libraries(okay_crypto_uwp PUBLIC bcrypt)

# Painter.cpp: the shared part of okay::platform::Painter (texture cache, font atlas upload); the engine adds it with target_sources, not in the add_library list.
add_library(okay_engine_uwp STATIC ${_kept} ${OKAY_SOURCE_ROOT}/engine/src/Platform/Painter.cpp ${OKAY_SOURCE_ROOT}/platform/xbox_uwp/UwpPlatform.cpp)
target_include_directories(okay_engine_uwp PUBLIC ${_incs} ${OKAY_SOURCE_ROOT}/platform/xbox_uwp)
target_compile_definitions(okay_engine_uwp PUBLIC ${_defs})
set_target_properties(okay_engine_uwp PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)   # the engine is C++17 even when the shell is C++20
target_compile_features(okay_engine_uwp PUBLIC cxx_std_17)
# libraries a UWP app may link: the umbrella WindowsApp.lib, Direct3D 11, DXGI, Winsock (internetClient capability), BCrypt. NOT linked:
# d3dcompiler, d3d12, dxguid, winhttp, dbghelp, opengl32, user32/gdi32, SDL2.
target_link_libraries(okay_engine_uwp PUBLIC okay_crypto_uwp WindowsApp d3d11 dxgi ws2_32 bcrypt)
if(MSVC)
    target_compile_options(okay_engine_uwp PRIVATE /EHsc /permissive- /bigobj /W3)
endif()

# ---- our own code instead of third-party code (docs/xbox_uwp_thirdparty.md) ----
# Models: ufbx.c is excluded (uwp_exclude.txt), Assimp is never linked (no OKAY_HAVE_ASSIMP), so every model format goes through our own readers. Their
# OKAY_OWN_<FMT>_DEFAULT switches are read from the ON options of the root CMakeLists.txt, so a reader added there is on here too without an edit.
# ModelImport.cpp and FbxScene.cpp also compile their "other reader" paths out under OKAY_XBOX_UWP: a file our reader refuses fails with a plain message.
include(${CMAKE_CURRENT_LIST_DIR}/uwp_own_readers.cmake)
okay_uwp_own_readers("${OKAY_SOURCE_ROOT}/CMakeLists.txt" _mi_defs _own_on)
set_source_files_properties(${OKAY_SOURCE_ROOT}/engine/src/Render/ModelImport.cpp PROPERTIES COMPILE_DEFINITIONS "${_mi_defs}")
list(LENGTH _own_on _own_n)
message(STATUS "okay_engine_uwp: own model readers ON by default: ${_own_n} switches (${_own_on})")
# Sound: the own WAV decoder is the WAV reader here (there is no SDL_LoadWAV); OGG, MP3 and FLAC are our own decoders in every build.
target_compile_definitions(okay_engine_uwp PRIVATE OKAY_OWN_WAV_DEFAULT=1)

# Crypto. Default: NO packet encryption (libsodium does not exist here and our own crypto stays off, CLAUDE.md rule 18). Hashes (SHA-256), random bytes and Ed25519
# checks are our own okay_crypto_uwp code either way. OKAY_XBOX_OWN_CRYPTO=ON is an EXPERIMENT switch: it turns packet encryption on through okay::crypto,
# which is NOT independently reviewed. Never for a release: scripts/check_release_own_stack.sh refuses a release tree that has the name set, and
# build-xbox-uwp.ps1 prints a warning and writes it into the export README.
option(OKAY_XBOX_OWN_CRYPTO "EXPERIMENT ONLY: packet encryption through our own okay::crypto (not independently reviewed; never for a release)" OFF)
if(OKAY_XBOX_OWN_CRYPTO)
    target_compile_definitions(okay_engine_uwp PUBLIC OKAY_OWN_CRYPTO=1)
    message(WARNING "OKAY_XBOX_OWN_CRYPTO=ON: own crypto, NOT independently reviewed. For experiments only; do not ship.")
endif()

# HTTP for content downloads. Default windows-web-http (Windows.Web.Http, does https). "own" = our okay::ownhttp over Winsock, PLAIN http only (no TLS of our own).
set(OKAY_XBOX_HTTP "windows-web-http" CACHE STRING "Content HTTP client: windows-web-http (default) or own (plain http only)")
set_property(CACHE OKAY_XBOX_HTTP PROPERTY STRINGS windows-web-http own)
if(NOT OKAY_XBOX_HTTP STREQUAL "windows-web-http" AND NOT OKAY_XBOX_HTTP STREQUAL "own")
    message(FATAL_ERROR "OKAY_XBOX_HTTP must be windows-web-http or own, not '${OKAY_XBOX_HTTP}'")
endif()
if(OKAY_XBOX_HTTP STREQUAL "own")
    target_compile_definitions(okay_engine_uwp PUBLIC OKAY_XBOX_HTTP_OWN=1)
endif()
message(STATUS "okay_engine_uwp: content HTTP = ${OKAY_XBOX_HTTP}; own crypto = ${OKAY_XBOX_OWN_CRYPTO}")
add_library(okay::engine_uwp ALIAS okay_engine_uwp)
list(LENGTH _kept _kept_n)
list(LENGTH _excluded _excl_n)
message(STATUS "okay_engine_uwp: ${_kept_n} engine sources (${_excl_n} excluded by uwp_exclude.txt)")
