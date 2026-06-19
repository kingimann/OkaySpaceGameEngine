# OkaySpace engine library
add_library(okay_engine
    src/Core/Application.cpp
    src/Core/Scheduler.cpp
    src/Core/Prefs.cpp
    src/Core/DataAsset.cpp
    src/Input/Input.cpp
    src/Render/ConsoleRenderer.cpp
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
    src/Components/UIDraggable.cpp
    src/Components/Terrain.cpp
    src/Platform/Steam/NullSteamService.cpp
    src/Platform/Steam/Steam.cpp
    src/Components/SteamManager.cpp
    src/Physics/Physics2D.cpp
    src/Physics/Physics3D.cpp
    src/Audio/AudioMixer.cpp
    src/Audio/AudioClip.cpp
    src/AI/Pathfinding.cpp
    src/Graphics/Image.cpp
    src/Graphics/Font.cpp
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
# Silence warnings from the third-party single-header libraries.
set_source_files_properties(src/Graphics/Image.cpp src/Graphics/Font.cpp
    PROPERTIES COMPILE_OPTIONS "-w")

target_compile_features(okay_engine PUBLIC cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(okay_engine PUBLIC Threads::Threads)

# Networking needs the Windows sockets library when targeting Windows.
if(WIN32)
    target_link_libraries(okay_engine PUBLIC ws2_32)
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

# Friendly alias so subprojects can link okay::engine.
add_library(okay::engine ALIAS okay_engine)
