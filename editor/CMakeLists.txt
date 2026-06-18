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

# Use the docking branch so the editor gets Unity-style dockable panels.
set(OKAY_IMGUI_TAG "docking" CACHE STRING "Dear ImGui git tag/branch to fetch")
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        ${OKAY_IMGUI_TAG}
    GIT_SHALLOW    TRUE)
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

add_executable(okay-editor main.cpp EditorState.cpp)
target_link_libraries(okay-editor PRIVATE okay::engine okay_imgui ${OKAY_SDL_LIB})
set_target_properties(okay-editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Headless self-test of the editor's (non-GUI) logic for CI.
if(OKAY_BUILD_TESTS)
    add_test(NAME editor COMMAND okay-editor --selftest)
endif()
