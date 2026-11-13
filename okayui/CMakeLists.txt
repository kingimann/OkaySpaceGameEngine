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
add_library(okay_ui STATIC src/OkayUI.cpp src/OkayUI_FontAtlas.cpp src/OkayUI_D3D11.cpp src/OkayUICodeEdit.cpp)
target_include_directories(okay_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The launcher look's font: Roboto (shared/RobotoFont.h) baked with the engine's own
# TrueType rasterizer (Graphics/TtfRaster).
target_include_directories(okay_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shared)
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

# The real screens the launcher and the editor host as the opt-in OkayUI backend (What's New, Console). Plain functions
# over OkayUI and the engine's console model; no SDL and no Dear ImGui in them, so the headless tests drive them as is.
add_library(okay_ui_screens STATIC screens/OkayUIScreens.cpp)
target_include_directories(okay_ui_screens PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/screens)
target_include_directories(okay_ui_screens PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shared)   # ConsoleScriptLink.hpp
target_link_libraries(okay_ui_screens PUBLIC okay_ui)
add_library(okay::ui_screens ALIAS okay_ui_screens)

# Standalone Direct3D 11 demo — a real DX11 app that renders an OkayUI panel.
# Windows only (GUI subsystem). Lets you verify the DirectX backend with no SDL.
if(WIN32)
    add_executable(okayui_d3d11_demo WIN32 d3d11_demo/main.cpp)
    target_link_libraries(okayui_d3d11_demo PRIVATE okay_ui d3d11)
    set_target_properties(okayui_d3d11_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# ---- Headless harness: software backend, tests, demo ---------------------------------------
# OkayUI's draw list (OkayUI::GetDrawData) rasterised on the CPU into an RGBA bitmap, so the toolkit
# is tested with no window, GPU or display: behaviour tests (synthetic mouse, keys and text fed to
# OkayUI::BeginFrame), golden images, a fuzz test. See docs/own_ui_toolkit.md.
# The software side is always built (it is small): the draw-list rasteriser the tests use and the software
# okay::platform::Painter the player draws with on the own platform backend (own code, not ready, off by default).
add_library(okay_ui_soft STATIC soft/OkayUI_Soft.cpp soft/OkayUI_SoftPainter.cpp)
target_include_directories(okay_ui_soft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/soft)
target_link_libraries(okay_ui_soft PUBLIC okay_ui okay_ui_screens)

if(OKAY_BUILD_OKAYUI_TESTS AND OKAY_BUILD_TESTS)

    # What the tests need: the shared test framework header and the committed golden images.
    foreach(t okayui_soft okayui_behaviour okayui_golden okayui_drawlist okayui_fuzz okayui_painter okayui_values okayui_codeedit okayui_codeedit_fuzz okayui_screens okayui_host)
        add_executable(test_${t} tests/test_${t}.cpp)
        target_link_libraries(test_${t} PRIVATE okay_ui_soft)
        target_include_directories(test_${t} PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/shared)
        target_compile_definitions(test_${t} PRIVATE OKAYUI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
        add_test(NAME ${t} COMMAND test_${t})
        # No window, no GPU: make sure SDL never tries to open a display.
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    endforeach()
endif()

if(OKAY_BUILD_OKAYUI_TESTS AND OKAY_BUILD_TESTS)
    # Renders a window of widgets to a PNG: okayui_headless_demo [out.png] [classic|launcher]
    add_executable(okayui_headless_demo demo/okayui_headless_demo.cpp)
    target_link_libraries(okayui_headless_demo PRIVATE okay_ui_soft)
    target_include_directories(okayui_headless_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
