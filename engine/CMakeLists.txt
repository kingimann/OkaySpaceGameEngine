# OkaySpace engine library
add_library(okay_engine
    src/Core/Application.cpp
    src/Core/Scheduler.cpp
    src/Core/Prefs.cpp
    src/Core/DataAsset.cpp
    src/Input/Input.cpp
    src/Render/ConsoleRenderer.cpp
    src/Render/GLRenderer.cpp
    src/Render/D3D11Renderer.cpp
    src/Render/D3D12Renderer.cpp
    src/Scene/Component.cpp
    src/Scene/GameObject.cpp
    src/Scene/Transform.cpp
    src/Scene/Scene.cpp
    src/Scene/SceneManager.cpp
    src/Scene/SceneSerializer.cpp
    src/Components/Camera.cpp
    src/Components/SpriteRenderer.cpp
    src/Net/UdpSocket.cpp
    src/Net/NetworkManager.cpp
    src/Net/RelayServer.cpp
    src/Net/NetBackend.cpp
    src/Net/NetTransport.cpp
    src/VisualScript/NodeGraph.cpp
    src/Components/VisualScriptComponent.cpp
    src/Scripting/OkayScriptVM.cpp
    src/Scripting/ScriptVM.cpp
    src/Components/ScriptComponent.cpp
    src/Components/ActionList.cpp
    src/Components/UIDocument.cpp
    src/Components/UILayoutGroup.cpp
    src/Components/UITooltip.cpp
    src/Components/UITextBind.cpp
    src/Components/UIBarBind.cpp
    src/Components/UIDraggable.cpp
    src/Components/Draggable.cpp
    src/Components/Terrain.cpp
    src/Components/VoxelTerrain.cpp
    src/Components/Character.cpp
    src/Components/AnimClip.cpp
    src/Platform/Steam/NullSteamService.cpp
    src/Platform/Steam/Steam.cpp
    src/Components/SteamManager.cpp
    src/Platform/Account/Account.cpp
    src/Physics/Physics2D.cpp
    src/Physics/Physics3D.cpp
    src/Audio/AudioMixer.cpp
    src/Audio/AudioClip.cpp
    src/AI/Pathfinding.cpp
    src/Graphics/Image.cpp
    src/Graphics/Font.cpp
    src/Graphics/TtfFont.cpp
    src/Render/ModelImport.cpp
)

# Public headers live under engine/include; consumers use #include <Okay.hpp>.
target_include_directories(okay_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Vendored single-header libraries (stb_image / stb_image_write), used only by
# the Image translation unit. Kept PRIVATE so it doesn't leak to consumers.
target_include_directories(okay_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/font8x8
)
# Silence warnings from the third-party single-header libraries. `-Wno-narrowing`
# is required in addition to `-w`: the vendored font8x8 tables initialise `char`
# arrays with 0xFF, which Clang (Emscripten/WASM) treats as a hard narrowing ERROR
# that `-w` does not downgrade, even though GCC/MinGW accept it.
set_source_files_properties(src/Graphics/Image.cpp src/Graphics/Font.cpp src/Graphics/TtfFont.cpp
    PROPERTIES COMPILE_OPTIONS "-w;-Wno-narrowing")

target_compile_features(okay_engine PUBLIC cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(okay_engine PUBLIC Threads::Threads)

# Networking needs the Windows sockets library when targeting Windows.
if(WIN32)
    target_link_libraries(okay_engine PUBLIC ws2_32)
    # The Direct3D 11 scene renderer (D3D11Renderer.cpp, guarded by _WIN32) needs the
    # D3D runtime + HLSL compiler. Off Windows the file compiles to nothing.
    target_link_libraries(okay_engine PUBLIC d3d11 d3dcompiler)
    # The Direct3D 12 scene renderer (D3D12Renderer.cpp, guarded by _WIN32) needs the
    # D3D12 runtime, DXGI and the COM IIDs (dxguid). Off Windows the file is empty.
    target_link_libraries(okay_engine PUBLIC d3d12 dxgi dxguid)
endif()

# ---- Optional packet encryption (libsodium) --------------------------
# Authenticated encryption for the UDP transport. Enabled when libsodium is found.
#  * Native: discovered via pkg-config (apt: libsodium-dev).
#  * MinGW cross-compile: point -DOKAY_SODIUM_PREFIX at the prebuilt
#    libsodium-win64 dir (the one containing include/sodium.h and lib/libsodium.a).
# Without it the transport still builds and runs — just unencrypted (the
# NetworkManager encryption toggle becomes a no-op), so it's a soft dependency.
if(OKAY_USE_SODIUM)
    set(_okay_sodium FALSE)
    if(OKAY_SODIUM_PREFIX)
        # Explicit prefix (e.g. the MinGW prebuilt): reference the static lib directly
        # rather than via find_library, which is fussy across cross toolchains.
        set(_sodium_a "${OKAY_SODIUM_PREFIX}/lib/libsodium.a")
        if(EXISTS "${_sodium_a}" AND EXISTS "${OKAY_SODIUM_PREFIX}/include/sodium.h")
            target_include_directories(okay_engine SYSTEM PUBLIC "${OKAY_SODIUM_PREFIX}/include")
            target_link_libraries(okay_engine PUBLIC "${_sodium_a}")
            target_compile_definitions(okay_engine PUBLIC SODIUM_STATIC=1)  # static .a on Windows
            set(_okay_sodium TRUE)
        endif()
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SODIUM QUIET libsodium)
            if(SODIUM_FOUND)
                target_include_directories(okay_engine SYSTEM PRIVATE ${SODIUM_INCLUDE_DIRS})
                target_link_libraries(okay_engine PUBLIC ${SODIUM_LIBRARIES})
                set(_okay_sodium TRUE)
            endif()
        endif()
    endif()
    if(_okay_sodium)
        target_compile_definitions(okay_engine PUBLIC OKAY_HAVE_SODIUM)
        message(STATUS "OkaySpace: packet encryption ENABLED (libsodium)")
    else()
        message(STATUS "OkaySpace: libsodium not found; packet encryption disabled")
    endif()
endif()

# ---- Optional scripting backends -------------------------------------
# Lua: real Lua embedding. Enable with -DOKAY_WITH_LUA=ON (needs liblua dev).
# Disabled by default — the engine ships OkayScript as its scripting language.
if(OKAY_WITH_LUA)
    find_package(Lua QUIET)
    if(NOT LUA_FOUND)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LUA QUIET lua5.4 lua-5.4 lua)
            set(LUA_LIBRARIES ${LUA_LIBRARIES})
            set(LUA_INCLUDE_DIR ${LUA_INCLUDE_DIRS})
        endif()
    endif()
    if(LUA_LIBRARIES)
        target_sources(okay_engine PRIVATE src/Scripting/LuaScriptVM.cpp)
        target_include_directories(okay_engine SYSTEM PRIVATE ${LUA_INCLUDE_DIR})
        target_link_libraries(okay_engine PRIVATE ${LUA_LIBRARIES})
        target_compile_definitions(okay_engine PUBLIC OKAY_WITH_LUA)
        message(STATUS "OkaySpace: Lua scripting ENABLED")
    else()
        message(WARNING "OKAY_WITH_LUA set but Lua not found; skipping Lua backend")
    endif()
endif()

# Steam: real Steamworks backend. Enable with -DOKAY_WITH_STEAM=ON and point at
# the SDK with -DSTEAMWORKS_SDK_PATH=/path/to/sdk (Valve's SDK is proprietary
# and cannot be bundled). Without this, the in-memory simulation backend is used.
if(OKAY_WITH_STEAM)
    if(NOT STEAMWORKS_SDK_PATH)
        message(FATAL_ERROR "OKAY_WITH_STEAM requires -DSTEAMWORKS_SDK_PATH=/path/to/steamworks_sdk")
    endif()
    if(WIN32)
        set(_steam_lib "${STEAMWORKS_SDK_PATH}/redistributable_bin/win64/steam_api64.lib")
    elseif(APPLE)
        set(_steam_lib "${STEAMWORKS_SDK_PATH}/redistributable_bin/osx/libsteam_api.dylib")
    else()
        set(_steam_lib "${STEAMWORKS_SDK_PATH}/redistributable_bin/linux64/libsteam_api.so")
    endif()
    target_sources(okay_engine PRIVATE src/Platform/Steam/SteamworksService.cpp)
    target_include_directories(okay_engine SYSTEM PRIVATE "${STEAMWORKS_SDK_PATH}/public")
    target_link_libraries(okay_engine PRIVATE "${_steam_lib}")
    target_compile_definitions(okay_engine PUBLIC OKAY_WITH_STEAM)
    message(STATUS "OkaySpace: Steamworks backend ENABLED (${_steam_lib})")
endif()

# Bake a default Steam App ID into the build (release uses the real id; dev stays
# 480 = Spacewar). Set with -DOKAY_STEAM_APP_ID=<id>.
if(DEFINED OKAY_STEAM_APP_ID)
    target_compile_definitions(okay_engine PUBLIC OKAY_STEAM_APP_ID=${OKAY_STEAM_APP_ID})
    message(STATUS "OkaySpace: default Steam App ID = ${OKAY_STEAM_APP_ID}")
endif()

# C#: real Mono embedding. Enable with -DOKAY_WITH_CSHARP=ON (needs Mono dev).
if(OKAY_WITH_CSHARP)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(MONO QUIET mono-2)
    endif()
    if(MONO_FOUND)
        target_sources(okay_engine PRIVATE src/Scripting/CSharpScriptVM.cpp)
        target_include_directories(okay_engine SYSTEM PRIVATE ${MONO_INCLUDE_DIRS})
        target_link_libraries(okay_engine PRIVATE ${MONO_LIBRARIES})
        target_link_directories(okay_engine PRIVATE ${MONO_LIBRARY_DIRS})
        target_compile_definitions(okay_engine PUBLIC OKAY_WITH_CSHARP)
        message(STATUS "OkaySpace: C# (Mono) scripting ENABLED")
    else()
        message(WARNING "OKAY_WITH_CSHARP set but Mono not found; skipping C# backend")
    endif()
endif()

# ---- Optional model import via Assimp (FBX / DAE / STL / PLY / 3DS / ...) ----
# The built-in loaders already cover OBJ + glTF with no dependency; Assimp adds the
# long tail of formats. FetchContent DOWNLOADS Assimp at configure time, so this is
# OFF by default to keep the default build self-contained (no network needed). Turn it
# on with -DOKAY_USE_ASSIMP=ON; ModelImport.cpp then routes those formats through it.
if(OKAY_USE_ASSIMP)
    include(FetchContent)
    # Build only what we need — keep it lean and quiet.
    set(ASSIMP_BUILD_TESTS                       OFF CACHE BOOL "" FORCE)
    set(ASSIMP_INSTALL                           OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ASSIMP_TOOLS                OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT    OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT    OFF CACHE BOOL "" FORCE)
    set(ASSIMP_NO_EXPORT                         ON  CACHE BOOL "" FORCE)
    set(ASSIMP_WARNINGS_AS_ERRORS                OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_FBX_IMPORTER       ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_OBJ_IMPORTER       ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_GLTF_IMPORTER      ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_COLLADA_IMPORTER   ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_STL_IMPORTER       ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_PLY_IMPORTER       ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_3DS_IMPORTER       ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_BLEND_IMPORTER     ON CACHE BOOL "" FORCE)
    FetchContent_Declare(assimp
        GIT_REPOSITORY https://github.com/assimp/assimp.git
        GIT_TAG        v5.4.3
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(assimp)
    target_link_libraries(okay_engine PUBLIC assimp)
    target_compile_definitions(okay_engine PUBLIC OKAY_HAVE_ASSIMP)
    message(STATUS "OkaySpace: Assimp model import ENABLED (FBX/DAE/STL/PLY/3DS/Blend)")
endif()

# Friendly alias so subprojects can link okay::engine.
add_library(okay::engine ALIAS okay_engine)
