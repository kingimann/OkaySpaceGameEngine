# OkayUI — a tiny immediate-mode UI toolkit rendered through SDL_Renderer
# (Direct3D 11 on Windows, Metal on macOS, OpenGL on Linux). It draws on top of
# Dear ImGui without conflicts because it shares ImGui's SDL_Renderer pipeline.
#
# Built only when SDL2 dev libraries are present; otherwise quietly skipped so a
# headless engine-only build is unaffected.
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    return()
endif()

if(TARGET SDL2::SDL2)
    set(_okayui_sdl SDL2::SDL2)
else()
    set(_okayui_sdl ${SDL2_LIBRARIES})
endif()

add_library(okay_ui STATIC src/OkayUI.cpp)
target_include_directories(okay_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(NOT TARGET SDL2::SDL2)
    target_include_directories(okay_ui PUBLIC ${SDL2_INCLUDE_DIRS})
endif()
# okay::engine provides the 8x8 bitmap font used for button labels.
target_link_libraries(okay_ui PUBLIC okay::engine ${_okayui_sdl})
target_compile_features(okay_ui PUBLIC cxx_std_17)

add_library(okay::ui ALIAS okay_ui)
