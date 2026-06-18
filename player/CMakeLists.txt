# OkaySpace Player — the standalone SDL2 runtime that runs a built game.
#
# This is what the editor's "Build Game" ships beside an exported scene: a
# single executable that loads game.okayscene and plays it. No ImGui, just SDL2.
# Enabled with -DOKAY_BUILD_PLAYER=ON (also built when -DOKAY_BUILD_EDITOR=ON).
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
