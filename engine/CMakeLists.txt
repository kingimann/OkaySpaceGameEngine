# OkaySpace engine library
add_library(okay_engine
    src/Core/Application.cpp
    src/Input/Input.cpp
    src/Render/ConsoleRenderer.cpp
    src/Scene/Component.cpp
    src/Scene/GameObject.cpp
    src/Scene/Transform.cpp
    src/Scene/Scene.cpp
    src/Components/Camera.cpp
    src/Components/SpriteRenderer.cpp
    src/Net/UdpSocket.cpp
    src/Net/NetworkManager.cpp
    src/VisualScript/NodeGraph.cpp
    src/Components/VisualScriptComponent.cpp
    src/Scripting/OkayScriptVM.cpp
    src/Scripting/ScriptVM.cpp
    src/Components/ScriptComponent.cpp
)

# Public headers live under engine/include; consumers use #include <Okay.hpp>.
target_include_directories(okay_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(okay_engine PUBLIC cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(okay_engine PUBLIC Threads::Threads)

# Networking needs the Windows sockets library when targeting Windows.
if(WIN32)
    target_link_libraries(okay_engine PUBLIC ws2_32)
endif()

# ---- Optional scripting backends -------------------------------------
# Lua: real Lua embedding. Enable with -DOKAY_WITH_LUA=ON (needs liblua dev).
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
