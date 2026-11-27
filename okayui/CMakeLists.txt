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
add_library(okay_ui STATIC src/OkayUI.cpp src/OkayUI_Context.cpp src/OkayUI_FontAtlas.cpp src/OkayUI_Tooltips.cpp src/OkayUI_DrawList.cpp src/OkayUI_Tables.cpp src/OkayUI_Menus.cpp src/OkayUI_Windows.cpp src/OkayUI_Basic.cpp src/OkayUI_Style.cpp src/OkayUI_Design.cpp src/OkayUI_Capture.cpp src/OkayUI_Toolkit.cpp src/OkayUI_DragDrop.cpp src/OkayUI_Trees.cpp src/OkayUI_Tabs.cpp src/OkayUI_D3D11.cpp src/OkayUICodeEdit.cpp src/OkayUIDock.cpp src/OkayUIDockLayout.cpp src/OkayUIDockBuilder.cpp src/OkayUIDockNodes.cpp src/OkayUIColor.cpp src/OkayUIPlots.cpp src/OkayUIText.cpp src/OkayUICompat.cpp)
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
add_library(okay_ui_screens STATIC screens/OkayUIScreens.cpp screens/OkayUIPanels.cpp screens/OkayUIEditorPanels.cpp screens/BrowserView.cpp)
target_include_directories(okay_ui_screens PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/screens)
target_include_directories(okay_ui_screens PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../shared)    # ConsoleScriptLink.hpp, EditorPanelModels.hpp (in the public header)
target_link_libraries(okay_ui_screens PUBLIC okay_ui)
target_link_libraries(okay_ui_screens PUBLIC okay_browser)   # BrowserView: the Browser's page engine (engine/include/okay/Browser/)
add_library(okay::ui_screens ALIAS okay_ui_screens)

# Standalone Direct3D 11 demo — a real DX11 app that renders an OkayUI panel.
# Windows only (GUI subsystem). Lets you verify the DirectX backend with no SDL.
if(WIN32)
    add_executable(okayui_d3d11_demo WIN32 d3d11_demo/main.cpp)
    target_link_libraries(okayui_d3d11_demo PRIVATE okay_ui d3d11)
    set_target_properties(okayui_d3d11_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# Backend check tools: write the render-test scenes as draw data plus the software reference picture, and (Windows, or Wine) draw
# them with the Direct3D 11 backend and compare. See docs/ui/own_ui_toolkit.md, the render interface section.
if(WIN32)
    add_executable(okayui_render_d3d11_check tools/okayui_render_d3d11_check.cpp)
    target_link_libraries(okayui_render_d3d11_check PRIVATE okay_ui_render d3d11)
endif()

# ---- Headless harness: software backend, tests, demo ---------------------------------------
# OkayUI's draw list (OkayUI::GetDrawData) rasterised on the CPU into an RGBA bitmap, so the toolkit
# is tested with no window, GPU or display: behaviour tests (synthetic mouse, keys and text fed to
# OkayUI::BeginFrame), golden images, a fuzz test. See docs/ui/own_ui_toolkit.md.
# The software side is always built (it is small): the draw-list rasteriser the tests use and the software
# okay::platform::Painter the player draws with on the own platform backend (own code, not ready, off by default).
add_library(okay_ui_soft STATIC soft/OkayUI_Soft.cpp soft/OkayUI_SoftPainter.cpp)
target_include_directories(okay_ui_soft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/soft)
target_link_libraries(okay_ui_soft PUBLIC okay_ui okay_ui_screens)

# ---- Render interface and backends (OkayUIRender.hpp): own code, off by default ----------------------------------------
# One interface from OkayUI's draw data to a backend, a validator for it, and the backends: software (the reference),
# okay::platform::Painter, OpenGL 3.3 core / ES 3.0 (entry points through a get-proc function, no link-time GL), Direct3D 11
# (Windows), and SdlHost (OkayUIRender_SDL.cpp): the OpenGL backend inside SDL's OpenGL renderers and the Direct3D 11 backend inside
# SDL's direct3d11 renderer, which the editor and launcher link for their hosted OkayUI screens and use only with OKAY_UI_OWN_RENDER=1
# (off by default).
add_library(okay_ui_render STATIC render/OkayUIRender.cpp render/OkayUIRender_Soft.cpp render/OkayUIRender_Painter.cpp
            render/OkayUIRender_GL.cpp render/OkayUIRender_D3D11.cpp render/OkayUIRender_SDL.cpp)
target_link_libraries(okay_ui_render PUBLIC okay_ui_soft)

if(OKAY_BUILD_OKAYUI_TESTS AND OKAY_BUILD_TESTS)

    # What the tests need: the shared test framework header and the committed golden images.
    foreach(t okayui_soft okayui_behaviour okayui_golden okayui_drawlist okayui_fuzz okayui_painter okayui_values okayui_codeedit okayui_codeedit_fuzz okayui_dock okayui_dock_fuzz okayui_compose okayui_windows okayui_windows_fuzz okayui_color okayui_widgets okayui_tables okayui_render okayui_render_gl okayui_render_sdlhost okayui_render_bench okayui_screens okayui_host okayui_compat okayui_scopes okayui_internals okayui_dockbuilder okayui_panels okayui_itemedit okayui_calls okayui_names okayui_host_platform okayui_docknodes okayui_channels okayui_host_shapes okayui_fonts okayui_frameloop okayui_dockaware okayui_imvector okayui_numfmt okayui_editor_panels okayui_hierarchy okayui_inspector okayui_imcompat okayui_capture okayui_frame_budget okayui_browser okayui_design)
        add_executable(test_${t} tests/test_${t}.cpp)
        target_link_libraries(test_${t} PRIVATE okay_ui_render)
        target_include_directories(test_${t} PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/shared)
        target_compile_definitions(test_${t} PRIVATE OKAYUI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
        add_test(NAME ${t} COMMAND test_${t})
        # No window, no GPU: make sure SDL never tries to open a display.
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    endforeach()
    # Several contexts (okayui/src/OkayUI_Context.cpp): behaviour, and with nm that every data symbol of okay_ui is listed for a
    # context or shared on purpose (the launcher's chat window is a second context).
    add_executable(test_okayui_contexts tests/test_okayui_contexts.cpp)
    target_link_libraries(test_okayui_contexts PRIVATE okay_ui_render)
    target_include_directories(test_okayui_contexts PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/shared)
    target_compile_definitions(test_okayui_contexts PRIVATE OKAYUI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
    if(CMAKE_NM AND NOT WIN32)
        target_compile_definitions(test_okayui_contexts PRIVATE OKAYUI_NM="${CMAKE_NM}" OKAYUI_LIB_PATH="$<TARGET_FILE:okay_ui>")
    endif()
    add_test(NAME okayui_contexts COMMAND test_okayui_contexts)
    set_tests_properties(okayui_contexts PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    # Browser round 3: the page corpus (tests/data/browser_pages) drawn through the browser view and compared with goldens.
    add_executable(test_okayui_browser_pages tests/test_okayui_browser_pages.cpp)
    target_link_libraries(test_okayui_browser_pages PRIVATE okay_ui_render)
    target_include_directories(test_okayui_browser_pages PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/shared)
    target_compile_definitions(test_okayui_browser_pages PRIVATE OKAYUI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden"
                               OKAY_BROWSER_PAGES_DIR="${CMAKE_SOURCE_DIR}/tests/data/browser_pages")
    add_test(NAME okayui_browser_pages COMMAND test_okayui_browser_pages)
    set_tests_properties(okayui_browser_pages PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    # The whole browser (shared/BrowserApp.cpp), shared by the browser program and the launcher's Browser page, with its tab host
    # (fetches, images, games: the engine's HTTP seam, decoders and threads) and okay's one place that starts a program.
    if(TARGET okay::engine)
        add_executable(test_okayui_browser_app tests/test_okayui_browser_app.cpp
            ${CMAKE_SOURCE_DIR}/shared/BrowserApp.cpp ${CMAKE_SOURCE_DIR}/shared/BrowserHost.cpp ${CMAKE_SOURCE_DIR}/tools/okay-cli/Process.cpp)
        target_link_libraries(test_okayui_browser_app PRIVATE okay_ui_render okay_browser okay::engine)
        target_include_directories(test_okayui_browser_app PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/tests
            ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/tools/okay-cli)
        target_compile_definitions(test_okayui_browser_app PRIVATE OKAYUI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
        add_test(NAME okayui_browser_app COMMAND test_okayui_browser_app)
        set_tests_properties(okayui_browser_app PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    endif()
    # The OpenGL and performance tests make a GL context with EGL through dlopen (no link-time GL).
    target_link_libraries(test_okayui_render_gl PRIVATE ${CMAKE_DL_LIBS})
    target_link_libraries(test_okayui_render_bench PRIVATE ${CMAKE_DL_LIBS})
    # Port probe, run: editor panels written for Dear ImGui, compiled against OkayUI through the stand-in imgui.h in probe/ and drawn headless
    # (docs/ui/own_ui_toolkit.md 6.14). The probe directory comes FIRST so no real imgui.h is used. Fake engine state, stubs for what main.cpp gives.
    function(okayui_port_test name)
        add_executable(test_${name} tests/test_${name}.cpp ${ARGN})
        target_include_directories(test_${name} BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/probe)
        target_include_directories(test_${name} PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/editor ${CMAKE_SOURCE_DIR}/launcher ${CMAKE_CURRENT_SOURCE_DIR}/screens)
        target_link_libraries(test_${name} PRIVATE okay_ui_render)
        target_compile_definitions(test_${name} PRIVATE OKAYUI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
        add_test(NAME ${name} COMMAND test_${name})
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    endfunction()
    okayui_port_test(okayui_port_console ../editor/ConsolePanel.cpp ../editor/EditorWidgets.cpp)
    # editor/ModalGuard.hpp's OkayUI part (the editor built on OkayUI): one question at a time, undrawn modals closed, the trail.
    okayui_port_test(okayui_port_modalguard)
    okayui_port_test(okayui_port_versions ../launcher/VersionsUI.cpp ../launcher/VersionsManager.cpp ../launcher/VersionsNet.cpp)
    okayui_port_test(okayui_port_project ../editor/ProjectPanelUI.cpp ../editor/ProjectMenuDraw.cpp ../editor/ProjectBrowser.cpp)
    okayui_port_test(okayui_port_project2 ../editor/ProjectPanelUI.cpp ../editor/ProjectMenuDraw.cpp ../editor/ProjectBrowser.cpp)
    # The Scene Manager panel over a small stand-in EditorState (tests/shadow/EditorState.hpp, switched on by the macro; the real class owns the engine's scene and play loop).
    # (Window mode, OKAY_UI_HOST=okayui, and its tests okayui_hostmode, hoststate, hostpanels, hostobjects, hostmenu, hostdialogs and hostviews were
    # deleted on 2026-11-27: the whole editor is drawn by OkayUI now, so the second, OkayUI-owned editor window was a copy. docs/ui/own_ui_toolkit.md 33.)
    okayui_port_test(okayui_frametick)   # the editor's game step (editor/EditorFrameTick.hpp) with a fake editor state
    okayui_port_test(okayui_port_scenemanager ../editor/SceneManagerPanel.cpp ../editor/ConsolePanel.cpp ../editor/EditorWidgets.cpp)
    target_compile_definitions(test_okayui_port_scenemanager PRIVATE OKAYUI_PORT_STAND_IN_EDITORSTATE)

    # A real okay::Scene rendered by the engine's software renderer through editor/SceneViewCore.hpp (headless pictures of a scene).
    add_executable(test_okayui_sceneview tests/test_okayui_sceneview.cpp)
    target_include_directories(test_okayui_sceneview PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/editor)
    target_link_libraries(test_okayui_sceneview PRIVATE okay_engine)
    add_test(NAME okayui_sceneview COMMAND test_okayui_sceneview)

    # The script editor's adapter over CodeEdit (editor/ScriptEditorCodeEdit.cpp: no ImGui, no SDL).
    add_executable(test_okayui_script_adapter tests/test_okayui_script_adapter.cpp ../editor/ScriptEditorCodeEdit.cpp)
    target_link_libraries(test_okayui_script_adapter PRIVATE okay_ui_soft)
    target_include_directories(test_okayui_script_adapter PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/editor ${CMAKE_SOURCE_DIR}/shared)
    add_test(NAME okayui_script_adapter COMMAND test_okayui_script_adapter)
    set_tests_properties(okayui_script_adapter PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
endif()

if(OKAY_BUILD_OKAYUI_TESTS AND OKAY_BUILD_TESTS)
    # Renders a window of widgets to a PNG: okayui_headless_demo [out.png] [classic|launcher]
    add_executable(okayui_render_dump tools/okayui_render_dump.cpp)
    target_link_libraries(okayui_render_dump PRIVATE okay_ui_render)
    target_include_directories(okayui_render_dump PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_executable(okayui_headless_demo demo/okayui_headless_demo.cpp)
    target_link_libraries(okayui_headless_demo PRIVATE okay_ui_soft)
    target_include_directories(okayui_headless_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
