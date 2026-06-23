# OkaySpace Player — the standalone SDL2 runtime that runs a built game.
#
# This is what the editor's "Build Game" ships beside an exported scene: a
# single executable that loads game.okayscene and plays it. No ImGui, just SDL2.
# Enabled with -DOKAY_BUILD_PLAYER=ON (also built when -DOKAY_BUILD_EDITOR=ON).

# ---- Web build (Emscripten) ------------------------------------------
# SDL2 comes from Emscripten's ports (-sUSE_SDL=2), not find_package. Outputs
# okay-player.html/.js/.wasm you can serve in a browser. Build via
# scripts/build-web.sh (or emcmake cmake ...). Assets are preloaded at build
# time; drop a game.okayscene next to this CMakeLists or pass --preload-file.
if(EMSCRIPTEN)
    add_executable(okay-player main.cpp)
    target_link_libraries(okay-player PRIVATE okay::engine)
    target_compile_options(okay-player PRIVATE "-sUSE_SDL=2")
    target_link_options(okay-player PRIVATE
        "-sUSE_SDL=2" "-sALLOW_MEMORY_GROWTH=1" "-sEXIT_RUNTIME=0")
    set_target_properties(okay-player PROPERTIES
        SUFFIX ".html"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/web)
    return()
endif()

find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    message(WARNING "okay-player needs SDL2 dev libraries; skipping.")
    return()
endif()

# SDL2 link target. With -DOKAY_EDITOR_STATIC_SDL=ON, link SDL2 statically so
# the player ships as a single self-contained .exe (no SDL2.dll alongside).
if(OKAY_EDITOR_STATIC_SDL AND TARGET SDL2::SDL2-static)
    set(OKAY_PLAYER_SDL_LIB SDL2::SDL2-static)
elseif(TARGET SDL2::SDL2)
    set(OKAY_PLAYER_SDL_LIB SDL2::SDL2)
else()
    set(OKAY_PLAYER_SDL_LIB ${SDL2_LIBRARIES})
endif()
# Note: we use SDL_MAIN_HANDLED and our own main(), so SDL2main is not linked.

add_executable(okay-player main.cpp)
target_link_libraries(okay-player PRIVATE okay::engine ${OKAY_PLAYER_SDL_LIB})
target_include_directories(okay-player PRIVATE ${SDL2_INCLUDE_DIRS})
set_target_properties(okay-player PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# On Windows ship under the name the editor + launcher expect (OkaySpacePlayer.exe).
if(WIN32)
    set_target_properties(okay-player PROPERTIES OUTPUT_NAME "OkaySpacePlayer")
endif()
