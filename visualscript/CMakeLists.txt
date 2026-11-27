# Visual Script program (system "Visual Script", docs/feature_map/14-visual-scripting.md 14.9): the Visual Script editor in a window of its own
# (okay-visual-script, OkaySpaceVisualScript.exe on Windows). The editor itself is shared/VisualScriptApp.cpp, which the editor also
# shows in its Visual Script window (editor/VisualScriptAppHost.cpp; the editor compiles the same sources into itself); the graphs
# and their rules are the editor's model (editor/VisualScriptModel.hpp, header-only). Drawn in OkayUI's window mode through
# okay::platform, like the Script Editor program. Built whenever OkayUI is (on by default, CLAUDE.md rule 17); shipped in the
# Windows package's Tools folder (scripts/build-windows-dist.sh).
if(NOT TARGET okay_ui_soft OR NOT TARGET okay::engine)
    return()
endif()

# The Visual Script app without a window. The program and the test link it; the editor compiles these sources itself.
add_library(okay_visual_script_app STATIC
    ${CMAKE_SOURCE_DIR}/shared/VisualScriptApp.cpp          # files, tabs, menus, Details, Problems / Navigator / OkayScript / Compare
    ${CMAKE_SOURCE_DIR}/shared/VisualScriptAppCanvas.cpp    # the canvas and the Add Block list
    ${CMAKE_SOURCE_DIR}/shared/VisualScriptAppAids.cpp      # start cards, the help card, hints, recipes, templates, the outline, menus
    ${CMAKE_SOURCE_DIR}/shared/VisualScriptAppFields.cpp)   # the Details pane's fields and the saved values
target_include_directories(okay_visual_script_app PUBLIC ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/editor)
target_link_libraries(okay_visual_script_app PUBLIC okay_ui okay::engine)
target_compile_features(okay_visual_script_app PUBLIC cxx_std_17)

add_executable(okay-visual-script
    main.cpp
    ${CMAKE_SOURCE_DIR}/shared/FileDialog.cpp)   # File > Open > Browse...: the system's file dialog
target_link_libraries(okay-visual-script PRIVATE okay_visual_script_app okay_ui_soft okay::engine)
target_compile_features(okay-visual-script PRIVATE cxx_std_17)
okay_win_version(okay-visual-script "OkaySpace Visual Script" "OkaySpaceVisualScript.exe")
set_target_properties(okay-visual-script PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(WIN32)
    set_target_properties(okay-visual-script PROPERTIES OUTPUT_NAME "OkaySpaceVisualScript" WIN32_EXECUTABLE TRUE)
    target_link_libraries(okay-visual-script PRIVATE shell32 comdlg32 ole32 user32)
endif()

if(OKAY_BUILD_TESTS AND OKAY_BUILD_OKAYUI_TESTS AND TARGET okay_ui_render AND NOT WIN32)
    # The app drawn standalone and in a host's rectangle (goldens), opening, editing and saving graphs, the Add Block list, wiring
    # plugs, the IDE tools, the host hooks and the settings file (okayui/tests/test_okayui_visual_script_app.cpp).
    add_executable(test_okayui_visual_script_app ${CMAKE_SOURCE_DIR}/okayui/tests/test_okayui_visual_script_app.cpp
        ${CMAKE_SOURCE_DIR}/editor/EditorState.cpp ${CMAKE_SOURCE_DIR}/shared/FileDialog.cpp)   # a real editor scene for the editor's parts
    target_link_libraries(test_okayui_visual_script_app PRIVATE okay_visual_script_app okay_ui_render)
    target_include_directories(test_okayui_visual_script_app PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/okayui/tests)
    target_compile_definitions(test_okayui_visual_script_app PRIVATE OKAYUI_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/okayui/tests/golden")
    add_test(NAME okayui_visual_script_app COMMAND test_okayui_visual_script_app)
    set_tests_properties(okayui_visual_script_app PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    # The program starts on a graph, draws a few frames and ends by itself, with no display (SDL's dummy video driver).
    add_test(NAME visual_script_program_starts
             COMMAND okay-visual-script --frames 5 ${CMAKE_SOURCE_DIR}/examples/visual_script/playground/coverage.okayvs)
    set_tests_properties(visual_script_program_starts PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;OKAY_PLATFORM_BACKEND=sdl2"
                         PASS_REGULAR_EXPRESSION "\\[visual-script\\] settings .*folder .*playground, 1 tab")
endif()
