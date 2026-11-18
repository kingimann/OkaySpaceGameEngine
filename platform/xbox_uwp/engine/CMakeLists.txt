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
#   libsodium                      -> not available: packet encryption OFF, Ed25519 uses okay::crypto (unreviewed; this target is never a release)
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

enable_language(C)   # third_party/ufbx/ufbx.c
set(_defs OKAY_XBOX_UWP=1 WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS OKAY_ENGINE_VERSION="uwp-experimental")
set(_incs ${OKAY_SOURCE_ROOT}/engine/include ${OKAY_SOURCE_ROOT}/engine/src ${OKAY_SOURCE_ROOT}/shared)

add_library(okay_crypto_uwp STATIC ${_crypto})
target_include_directories(okay_crypto_uwp PUBLIC ${OKAY_SOURCE_ROOT}/engine/include)
target_compile_definitions(okay_crypto_uwp PUBLIC OKAY_XBOX_UWP=1 WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(okay_crypto_uwp PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_features(okay_crypto_uwp PUBLIC cxx_std_17)
target_link_libraries(okay_crypto_uwp PUBLIC bcrypt)

add_library(okay_engine_uwp STATIC ${_kept} ${OKAY_SOURCE_ROOT}/platform/xbox_uwp/UwpPlatform.cpp)
target_include_directories(okay_engine_uwp PUBLIC ${_incs} ${OKAY_SOURCE_ROOT}/platform/xbox_uwp)
target_include_directories(okay_engine_uwp PRIVATE ${OKAY_SOURCE_ROOT}/engine/third_party/stb ${OKAY_SOURCE_ROOT}/engine/third_party/ufbx)
target_compile_definitions(okay_engine_uwp PUBLIC ${_defs})
set_target_properties(okay_engine_uwp PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)   # the engine is C++17 even when the shell is C++20
target_compile_features(okay_engine_uwp PUBLIC cxx_std_17)
# libraries a UWP app may link: the umbrella WindowsApp.lib, Direct3D 11, DXGI, Winsock (internetClient capability), BCrypt. NOT linked:
# d3dcompiler, d3d12, dxguid, winhttp, dbghelp, opengl32, user32/gdi32, SDL2.
target_link_libraries(okay_engine_uwp PUBLIC okay_crypto_uwp WindowsApp d3d11 dxgi ws2_32 bcrypt)
if(MSVC)
    target_compile_options(okay_engine_uwp PRIVATE /EHsc /permissive- /bigobj /W3)
    set_source_files_properties(${OKAY_SOURCE_ROOT}/engine/third_party/ufbx/ufbx.c PROPERTIES COMPILE_OPTIONS "/w")
endif()
add_library(okay::engine_uwp ALIAS okay_engine_uwp)
list(LENGTH _kept _kept_n)
list(LENGTH _excluded _excl_n)
message(STATUS "okay_engine_uwp: ${_kept_n} engine sources (${_excl_n} excluded by uwp_exclude.txt)")
