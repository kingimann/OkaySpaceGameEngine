# OkaySpace Editor — Dear ImGui (docking) + SDL2 renderer desktop editor.
#
# Requires SDL2 dev libraries and (at configure time) network access to fetch
# Dear ImGui. Enable from the top level with -DOKAY_BUILD_EDITOR=ON.
include(FetchContent)

find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    message(WARNING "okay-editor needs SDL2 dev libraries; skipping.")
    return()
endif()

# The editor renders the ImGui UI through SDL's 2D renderer (Direct3D on
# Windows, Metal on macOS, OpenGL elsewhere), so no OpenGL dependency is needed.

# SDL2 link target. With -DOKAY_EDITOR_STATIC_SDL=ON, link SDL2 statically so
# the editor ships as a single self-contained .exe (no SDL2.dll alongside).
option(OKAY_EDITOR_STATIC_SDL "Statically link SDL2 into the editor" OFF)
if(OKAY_EDITOR_STATIC_SDL AND TARGET SDL2::SDL2-static)
    set(OKAY_SDL_LIB SDL2::SDL2-static)
elseif(TARGET SDL2::SDL2)
    set(OKAY_SDL_LIB SDL2::SDL2)
else()
    set(OKAY_SDL_LIB ${SDL2_LIBRARIES})
endif()
# Note: we use SDL_MAIN_HANDLED and our own main(), so SDL2main is not linked.

# Pin a specific Dear ImGui *docking-branch* commit so editor builds are
# reproducible (the docking branch has DockBuilder + IMGUI_HAS_DOCK). Fetching a
# bare branch name was non-deterministic across machines/CI, so we pin a SHA;
# checking out a SHA needs the full history (no GIT_SHALLOW). Override with
# -DOKAY_IMGUI_TAG=<ref> if you want a different revision.
set(OKAY_IMGUI_TAG "036bf939b6f8d74ad76bcf926b757c56e68c54ff"
    CACHE STRING "Dear ImGui git commit/branch to fetch (must be the docking branch)")
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        ${OKAY_IMGUI_TAG})
FetchContent_MakeAvailable(imgui)

# Dear ImGui ships no CMake; build the core + SDL2 backends ourselves.
add_library(okay_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
target_include_directories(okay_imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(okay_imgui PUBLIC ${OKAY_SDL_LIB})
target_compile_options(okay_imgui PRIVATE -w) # silence third-party warnings

add_executable(okay-editor main.cpp EditorState.cpp
    ${CMAKE_SOURCE_DIR}/engine/third_party/tinyfd/tinyfiledialogs.c)  # native file dialogs
target_include_directories(okay-editor PRIVATE ${CMAKE_SOURCE_DIR}/shared
    ${CMAKE_SOURCE_DIR}/engine/third_party/tinyfd)
set_source_files_properties(${CMAKE_SOURCE_DIR}/engine/third_party/tinyfd/tinyfiledialogs.c
    PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(okay-editor PRIVATE okay::engine okay_imgui ${OKAY_SDL_LIB})
if(WIN32)
    target_link_libraries(okay-editor PRIVATE comdlg32 ole32 shell32 user32)
endif()
# OkayUI overlay demo (renders on top of ImGui through the same SDL_Renderer).
if(TARGET okay_ui)
    target_link_libraries(okay-editor PRIVATE okay_ui)
    target_compile_definitions(okay-editor PRIVATE OKAY_HAVE_OKAYUI=1)
endif()
set_target_properties(okay-editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    OUTPUT_NAME "OkayEngine")    # the name the launcher looks for

# On Windows, build the editor as a GUI app so no console window opens behind it
# (it's launched from the launcher). The headless --selftest is CI-only (Linux).
if(WIN32)
    set_target_properties(okay-editor PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Headless self-test of the editor's (non-GUI) logic for CI.
if(OKAY_BUILD_TESTS)
    add_test(NAME editor COMMAND okay-editor --selftest)
endif()
