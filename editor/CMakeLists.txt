# OkaySpace Editor — Dear ImGui + SDL2 + OpenGL desktop editor.
#
# Requires SDL2 + OpenGL dev libraries and (at configure time) network access to
# fetch Dear ImGui. Enable from the top level with -DOKAY_BUILD_EDITOR=ON.
include(FetchContent)

find_package(SDL2 QUIET)
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL QUIET)

if(NOT SDL2_FOUND OR NOT OpenGL_FOUND)
    message(WARNING "okay-editor needs SDL2 and OpenGL dev libraries; skipping.")
    return()
endif()

set(OKAY_IMGUI_TAG "v1.90.4" CACHE STRING "Dear ImGui git tag to fetch")
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        ${OKAY_IMGUI_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

# Dear ImGui ships no CMake; build the core + SDL2/OpenGL3 backends ourselves.
add_library(okay_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(okay_imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(okay_imgui PUBLIC SDL2::SDL2 OpenGL::GL)
target_compile_options(okay_imgui PRIVATE -w) # silence third-party warnings

add_executable(okay-editor main.cpp EditorState.cpp)
target_link_libraries(okay-editor PRIVATE okay::engine okay_imgui SDL2::SDL2 OpenGL::GL)
set_target_properties(okay-editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Headless self-test of the editor's (non-GUI) logic for CI.
if(OKAY_BUILD_TESTS)
    add_test(NAME editor COMMAND okay-editor --selftest)
endif()
