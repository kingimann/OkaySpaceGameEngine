# OkaySpace Launcher — a Dear ImGui + SDL2 front end (Create / Play / Marketplace).
# Needs SDL2 dev libraries; fetches Dear ImGui at configure time. Built when
# -DOKAY_BUILD_LAUNCHER=ON.
include(FetchContent)

find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    message(WARNING "okayspace-launcher needs SDL2 dev libraries; skipping.")
    return()
endif()

if(OKAY_EDITOR_STATIC_SDL AND TARGET SDL2::SDL2-static)
    set(OKAY_LAUNCHER_SDL SDL2::SDL2-static)
elseif(TARGET SDL2::SDL2)
    set(OKAY_LAUNCHER_SDL SDL2::SDL2)
else()
    set(OKAY_LAUNCHER_SDL ${SDL2_LIBRARIES})
endif()

# Reuse the same pinned Dear ImGui (docking) commit as the editor. FetchContent
# is idempotent, so if the editor already fetched it this is a no-op download.
set(OKAY_IMGUI_TAG "036bf939b6f8d74ad76bcf926b757c56e68c54ff"
    CACHE STRING "Dear ImGui git commit/branch to fetch")
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        ${OKAY_IMGUI_TAG})
FetchContent_MakeAvailable(imgui)

# A launcher-local ImGui lib (distinct target name from the editor's okay_imgui
# so both can build in one configure).
if(NOT TARGET launcher_imgui)
    add_library(launcher_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
    target_include_directories(launcher_imgui SYSTEM PUBLIC
        ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(launcher_imgui PUBLIC ${OKAY_LAUNCHER_SDL})
    target_compile_options(launcher_imgui PRIVATE -w)
endif()

add_executable(okayspace-launcher main.cpp)
target_compile_features(okayspace-launcher PRIVATE cxx_std_17)
target_include_directories(okayspace-launcher PRIVATE ${CMAKE_SOURCE_DIR}/shared)
target_link_libraries(okayspace-launcher PRIVATE launcher_imgui ${OKAY_LAUNCHER_SDL})
set_target_properties(okayspace-launcher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
