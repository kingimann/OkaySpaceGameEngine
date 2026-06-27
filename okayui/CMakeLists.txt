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

# The Direct3D 11 backend (src/OkayUI_D3D11.cpp) is guarded by #if defined(_WIN32),
# so it compiles to nothing off Windows and never affects the Linux/SDL build.
add_library(okay_ui STATIC src/OkayUI.cpp src/OkayUI_D3D11.cpp)
target_include_directories(okay_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(NOT TARGET SDL2::SDL2)
    target_include_directories(okay_ui PUBLIC ${SDL2_INCLUDE_DIRS})
endif()
# okay::engine provides the 8x8 bitmap font used for button labels.
target_link_libraries(okay_ui PUBLIC okay::engine ${_okayui_sdl})
target_compile_features(okay_ui PUBLIC cxx_std_17)
# Direct3D 11 backend needs the D3D runtime + the HLSL compiler (Windows targets).
if(WIN32)
    target_link_libraries(okay_ui PUBLIC d3d11 d3dcompiler)
endif()

add_library(okay::ui ALIAS okay_ui)

# Standalone Direct3D 11 demo — a real DX11 app that renders an OkayUI panel.
# Windows only (GUI subsystem). Lets you verify the DirectX backend with no SDL.
if(WIN32)
    add_executable(okayui_d3d11_demo WIN32 d3d11_demo/main.cpp)
    target_link_libraries(okayui_d3d11_demo PRIVATE okay_ui d3d11)
    set_target_properties(okayui_d3d11_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()
