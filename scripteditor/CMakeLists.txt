# Script Editor program (system "Script Editor", docs/feature_map/13-scripting.md 13.5.13): the script editor in a window of its
# own (okay-script-editor, OkaySpaceScriptEditor.exe on Windows). The script editor itself is shared/ScriptIdeApp.cpp, which the
# editor also shows in a window (editor/ScriptEditorAppHost.cpp; the editor compiles the same sources into itself). Drawn in
# OkayUI's window mode through okay::platform (okay_ui_soft's painter frame drawer), like the browser program. Built whenever
# OkayUI is (on by default, CLAUDE.md rule 17); shipped in the Windows package's Tools folder (scripts/build-windows-dist.sh).
if(NOT TARGET okay_ui_soft OR NOT TARGET okay::engine)
    return()
endif()

# The Script Editor without a window: the app, the headless model it decides with and our code widget's adapter. The program
# and the test link it; the editor compiles these sources itself (it has them already for its own Script Editor window).
add_library(okay_script_editor_app STATIC
    ${CMAKE_SOURCE_DIR}/shared/ScriptIdeApp.cpp             # the script editor in a rectangle (tree, tabs, find, history, checks)
    ${CMAKE_SOURCE_DIR}/shared/ScriptIdeAppScene.cpp        # the host's scene scripts, New Script, Save changes?, reopen, language
    ${CMAKE_SOURCE_DIR}/shared/ScriptIdeAppNav.cpp          # find / replace, go to line / symbol / definition, references, history
    ${CMAKE_SOURCE_DIR}/shared/ScriptIdeAppEdit.cpp         # format, rename with a preview, quick fixes, join lines, signature help
    ${CMAKE_SOURCE_DIR}/shared/ScriptIdeAppView.cpp         # minimap, pinned header, changed lines, the disk, Insert and Help
    ${CMAKE_SOURCE_DIR}/editor/ScriptIde.cpp                # the headless model: script list, tree, Go to File, find, lint, Local History
    ${CMAKE_SOURCE_DIR}/editor/ScriptEditorCodeEdit.cpp)    # OkayUI::CodeEdit with a script language's words and the code font
target_include_directories(okay_script_editor_app PUBLIC ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/editor)
target_link_libraries(okay_script_editor_app PUBLIC okay_ui okay::engine)
target_compile_features(okay_script_editor_app PUBLIC cxx_std_17)
find_package(Threads)
if(TARGET Threads::Threads)
    target_link_libraries(okay_script_editor_app PUBLIC Threads::Threads)   # the symbol index builder's worker (okay::thr)
endif()

add_executable(okay-script-editor
    main.cpp
    ${CMAKE_SOURCE_DIR}/shared/FileDialog.cpp)   # File > Open Folder > Browse...: the system's folder dialog
target_link_libraries(okay-script-editor PRIVATE okay_script_editor_app okay_ui_soft okay::engine)
target_compile_features(okay-script-editor PRIVATE cxx_std_17)
okay_win_version(okay-script-editor "OkaySpace Script Editor" "OkaySpaceScriptEditor.exe")
set_target_properties(okay-script-editor PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(WIN32)
    set_target_properties(okay-script-editor PROPERTIES OUTPUT_NAME "OkaySpaceScriptEditor" WIN32_EXECUTABLE TRUE)
    target_link_libraries(okay-script-editor PRIVATE shell32 comdlg32 ole32 user32)
endif()

if(OKAY_BUILD_TESTS AND OKAY_BUILD_OKAYUI_TESTS AND TARGET okay_ui_render AND NOT WIN32)
    # The app drawn standalone and in a host's rectangle (goldens), opening a folder, editing and saving, Find in Files, Local
    # History, the checks and the settings file (okayui/tests/test_okayui_script_ide_app.cpp).
    add_executable(test_okayui_script_ide_app ${CMAKE_SOURCE_DIR}/okayui/tests/test_okayui_script_ide_app.cpp)
    target_link_libraries(test_okayui_script_ide_app PRIVATE okay_script_editor_app okay_ui_render)
    target_include_directories(test_okayui_script_ide_app PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/okayui/tests)
    target_compile_definitions(test_okayui_script_ide_app PRIVATE OKAYUI_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/okayui/tests/golden")
    add_test(NAME okayui_script_ide_app COMMAND test_okayui_script_ide_app)
    # The program starts on a project folder, draws a few frames and ends by itself, on a private Xvfb display (skipped without Xvfb).
    add_test(NAME script_editor_program_starts
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/run_under_xvfb.sh $<TARGET_FILE:okay-script-editor> --frames 5 ${CMAKE_SOURCE_DIR}/examples/lua)
    set_tests_properties(script_editor_program_starts PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 ENVIRONMENT "OKAY_OWN_PAINTER=software"
                         PASS_REGULAR_EXPRESSION "\\[script-editor\\] settings .*folder .*examples/lua")
endif()
