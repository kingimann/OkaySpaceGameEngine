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
# The page around it is ours: platform/web/shell.html (loading screen, errors in plain words, full-screen button) and
# platform/web/okay_web.js (downloads with progress, the game data pack, saves kept in IndexedDB, audio unlock, touch and
# context-menu manners). The game's files are not baked into the build: scripts/web_package.py packs them beside it, so
# one player build serves every game (docs/feature_map/18-build-export-release.md 18.2).
if(EMSCRIPTEN)
    add_executable(okay-player main.cpp ${CMAKE_SOURCE_DIR}/shared/SdlDisplays.cpp)
    target_link_libraries(okay-player PRIVATE okay::engine)
    target_include_directories(okay-player PRIVATE ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/platform/web)
    target_compile_options(okay-player PRIVATE "-sUSE_SDL=2")
    set(_okay_web_dir ${CMAKE_SOURCE_DIR}/platform/web)
    target_link_options(okay-player PRIVATE
        "-sUSE_SDL=2" "-sALLOW_MEMORY_GROWTH=1" "-sEXIT_RUNTIME=0"
        "-sENVIRONMENT=web"            # browser only: no Node.js or worker start-up code in the .js
        "-sFORCE_FILESYSTEM=1" "-lidbfs.js"   # okay_web.js writes the game pack into FS and mounts the saves on IDBFS
        "-sSTACK_SIZE=1048576"         # 1 MB like a desktop thread's default; Emscripten's 64 KB is too small for the scene loader
        "--pre-js=${_okay_web_dir}/okay_web.js"
        "--shell-file=${_okay_web_dir}/shell.html")
    # Release builds are optimised for a small download: -Oz shrinks the program at link time (wasm-opt), Closure
    # shrinks the JavaScript. OKAY_WEB_CLOSURE=OFF keeps readable JavaScript for debugging.
    option(OKAY_WEB_CLOSURE "Minify the web player's JavaScript with Closure in release builds" ON)
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
        target_link_options(okay-player PRIVATE "-Oz")
        if(OKAY_WEB_CLOSURE)
            target_link_options(okay-player PRIVATE "--closure=1")
        endif()
    endif()
    set_target_properties(okay-player PROPERTIES
        SUFFIX ".html"
        LINK_DEPENDS "${_okay_web_dir}/okay_web.js;${_okay_web_dir}/shell.html"
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

add_executable(okay-player main.cpp ${CMAKE_SOURCE_DIR}/shared/SdlDisplays.cpp)
target_link_libraries(okay-player PRIVATE okay::engine ${OKAY_PLAYER_SDL_LIB})
# Bytes of the player per engine module (docs/engine/modular_engine.md): GNU linkers (Linux, MinGW) write a link map
# into the build folder (not shipped), and scripts/module_sizes.py turns it into module_sizes.txt beside the player,
# which the editor's Build report shows. Without Python the size file is simply not made.
if(NOT MSVC AND NOT APPLE)
    set(_okay_player_map "${CMAKE_BINARY_DIR}/maps/okay-player.map")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/maps")
    target_link_options(okay-player PRIVATE "LINKER:-Map=${_okay_player_map}")
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_custom_command(TARGET okay-player POST_BUILD
            COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/scripts/module_sizes.py ${CMAKE_BINARY_DIR} ${_okay_player_map}
                    --out $<TARGET_FILE_DIR:okay-player>/module_sizes.txt
            VERBATIM)
    endif()
endif()
okay_win_version(okay-player "OkaySpace Player" "OkaySpacePlayer.exe")
target_include_directories(okay-player PRIVATE ${SDL2_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/shared)
# OkayUI in the runtime: games can render the immediate-mode toolkit (and the F1
# "Test UI" overlay proves it works in a real game). Linked when available.
if(TARGET okay_ui)
    target_link_libraries(okay-player PRIVATE okay_ui okay_ui_soft)   # okay_ui_soft: the software 2D painter of the own platform backend
    target_include_directories(okay-player PRIVATE ${CMAKE_SOURCE_DIR}/shared)
    target_compile_definitions(okay-player PRIVATE OKAY_HAVE_OKAYUI=1)
endif()
set_target_properties(okay-player PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# On Windows ship under the name the editor + launcher expect (OkaySpacePlayer.exe),
# and as a GUI app so players don't get a console window behind the game.
if(WIN32)
    set_target_properties(okay-player PROPERTIES
        OUTPUT_NAME "OkaySpacePlayer"
        WIN32_EXECUTABLE TRUE)
endif()
