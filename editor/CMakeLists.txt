# OkaySpace Editor: the desktop editor. Its sources are written in Dear ImGui's names and compiled only against OkayUI's compatibility
# layer (okayui/probe first on the include path: "imgui.h" and "ImGuiPainterRenderer.hpp" are OkayUI's), so no Dear ImGui is in it
# (2026-11-27, docs/ui/own_ui_toolkit.md section 33). Input: shared/ImGuiPlatformFeed.hpp; drawing: OkayUI's frame through the window's
# okay::platform::Painter. Needs SDL2 dev libraries (the window on Windows). Enable from the top level with -DOKAY_BUILD_EDITOR=ON.
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND OR NOT TARGET okay_ui OR NOT TARGET okay_ui_screens OR NOT TARGET okay_ui_soft)
    message(WARNING "okay-editor needs SDL2 dev libraries and OkayUI; skipping.")
    return()
endif()

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

add_executable(okay-editor main.cpp EditorState.cpp FpHandsWindow.cpp
    HangWatchdog.cpp                                                   # the hang watchdog: a stuck main loop is reported (okay_hang.txt) and, on Windows, Wait / Close is offered
    BuildStamp.cpp                                                     # __DATE__ " " __TIME__ for the crash handler, out of main.cpp so ccache can reuse main.cpp
    UIGalleryWindow.cpp                                                # the UI Gallery window (search every UI part, preview it, add it)
    AnimatorWindow.cpp                                                 # the Animator window (state-machine graph)
    TimelineWindow.cpp                                                 # the Timeline window (cutscene sequences)
    NavigationWindow.cpp                                               # the Navigation window (navmesh bake + overlays)
    BuildSettingsWindow.cpp                                            # the Build Settings window: page list + search, Game and Advanced pages, summary, checks, history
    BuildGame.cpp                                                      # Build Game: export the game, Web / Android / Xbox data, the dedicated server (builder::)
    Updater.cpp                                                        # the self-updater and runtime fetcher (updater::): check, download with checksums, install
    ScriptAssist.cpp                                                   # Scripting Reference window; the Script Editor's completion, signatures, docs, C# members, palette
    ScriptAssistBindings.cpp                                           # Script assist from the engine's binding table: component members, argument hints, docs
    FindInFiles.cpp                                                    # Find in Files: search every project script, open a hit in the Script Editor
    ScriptEditorAppHost.cpp ${CMAKE_SOURCE_DIR}/shared/ScriptIdeApp.cpp   # the Script Editor window: the Script Editor program's app
    ${CMAKE_SOURCE_DIR}/shared/ScriptIdeAppScene.cpp ${CMAKE_SOURCE_DIR}/shared/ScriptIdeAppNav.cpp ${CMAKE_SOURCE_DIR}/shared/ScriptIdeAppEdit.cpp
    ${CMAKE_SOURCE_DIR}/shared/ScriptIdeAppView.cpp   # its scene scripts, New Script, find, navigation, edits, the view and the disk
    VisualScriptAppHost.cpp ${CMAKE_SOURCE_DIR}/shared/VisualScriptApp.cpp   # the Visual Script window: the Visual Script program's app
    ${CMAKE_SOURCE_DIR}/shared/VisualScriptAppCanvas.cpp   # its canvas and Add Block list
    ${CMAKE_SOURCE_DIR}/shared/VisualScriptAppAids.cpp ${CMAKE_SOURCE_DIR}/shared/VisualScriptAppFields.cpp   # its aids, panels and fields
    ScriptIde.cpp                                                      # the Script Editor's workspace model: Quick Open, symbol index, rename, find/replace, quick fixes, local history
    PluginRegistry.cpp                                                 # editor plugins: the commands and providers they add to the Script Editor
    PluginScriptApi.cpp                                                # editor plugins: the script_* builtins (commands, lint, completion, formatter, hover, snippets, panels)
    AssetManagerWindow.cpp                                             # the Asset Manager window: Marketplace downloads listed by type, imported into the project
    PluginsWindow.cpp                                                  # editor plugins (OkayScript): editor_* builtins, Plugins menu, Plugin Manager
    DialogueWindow.cpp                                                 # the Dialogue window (dialogue graphs + quests)
    BehaviorTreeWindow.cpp                                             # the Behavior Tree window (NPC AI trees)
    LevelTools.cpp                                                     # foliage brush, terrain layers, decals, cables, measuring tool, orthographic views
    SplineTools.cpp                                                    # splines: Scene-view editing, inspectors, lines / trails
    WindTools.cpp                                                      # wind: zones and arrows in the Scene view, inspector rows, menus, Lighting > Wind
    VolumeTools.cpp                                                    # volumes: Scene-view shapes, handles, draw tool, inspector, menus
    EditorWidgets.cpp                                                  # shared widgets (accent, icons, search field)
    ConsolePanel.cpp                                                   # ConsoleLog + Console panel
    OkayPanels.cpp                                                     # the panels drawn by OkayUI's own screens (Crash Log, Audio Mixer, History, Problems, ...)
    ScriptEditorCodeEdit.cpp                                           # the script editor's adapter over OkayUI::CodeEdit (no ImGui)
    ProblemsPanel.cpp                                                  # Problems panel + project diagnostics
    ProjectDiagRunner.cpp                                              # when the Problems checks run: the project-wide one on a worker
    WorkerCall.cpp                                                     # one call on its own thread, collected on a later frame (update check, Problems pass)
    SceneManagerPanel.cpp                                              # Scene Manager window
    NetSimPanel.cpp                                                    # net simulator + traffic panel
    AntiCheatSetupUi.cpp                                               # easy anti-cheat setup: presets, Add Anti-Cheat, checklist
    UIInspectorLayout.cpp                                              # flex / mask / list view inspectors
    UIInspectorOverlay.cpp                                             # modal / context menu / keybind inspectors
    UIInspectorWidgetsMore.cpp                                         # search, date, files, accordion, crumbs, pages, skeleton, sheet
    InspectorPanel.cpp InspectorModel.cpp PrefabEdit.cpp                              # Inspector / Hierarchy: reference fields, generic component card, filter, drag-drop logic
    UIPreviewPainter.cpp                                               # engine UI renderer -> the UI's draw list
    ExampleScenes.cpp                                                  # File > Examples scenes saved into the project
    UIRuntimeTools.cpp                                                 # UI themes, runtime UI widgets, File > Examples > UI Gallery
    LocalizationWindow.cpp                                             # the Localization window (string table)
    WeaponEditor.cpp                                                   # the Weapons window (weapon definitions)
    LocalizationTools.cpp                                              # grid, check, scan and preview the window draws (ImGui-free)
    ModelingEasyUi.cpp                                                 # beginner view of the Modeling window (logic in ModelingEasy*.hpp)
    ModelingSidebarUi.cpp                                              # the Modeling window's page list (icons + names, folds to icons)
    GuidedPanel.cpp                                                    # Guided page of the Modeling window (templates, parts, regeneration)
    ContentEditors.cpp                                                 # Items, Loot & Recipes (.okayitems, .okayloot, .okayrecipes)
    ProjectBrowser.cpp ProjectPanelUI.cpp                              # the Project panel: folder model + safe file operations, name prompt / delete confirm / import settings
    ProjectMenuDraw.cpp                                                # drawing of the Project panel's right-click menus and the Properties rows (ImGui only; drawn headless for the screenshots)
    LaunchFile.cpp                                                     # open a file with the system's program, in a text editor, or show it in the file manager (no shell; never a program or script)
    WebRun.cpp                                                         # Build Settings > Web: web build sizes and the local Run in browser server (127.0.0.1 only)
    ${CMAKE_SOURCE_DIR}/launcher/VersionsManager.cpp                  # the version list reader + download plan (checksums required), shared with the launcher updater
    ProjectUpgrade.cpp ProjectUpgradeUI.cpp                            # the project version question, backup + upgrade, File > Project Upgrade...
    VsGraphShot.cpp                                                    # --vs-shot: screenshots of example graphs
    ${CMAKE_SOURCE_DIR}/shared/PlatformDisplays.cpp                    # monitor list and display-mode changes (okay::platform, shared with the player)
    ${CMAKE_SOURCE_DIR}/shared/FileDialog.cpp                          # open / save / folder dialogs: system dialogs or the built-in browser (model)
    ${CMAKE_SOURCE_DIR}/shared/FileDialogUI.cpp)                       # the built-in browser's window
target_compile_definitions(okay-editor PRIVATE ${OKAY_VERSION_DEFINE} OKAY_UI_DEFAULT_BACKEND_OKAYUI=1)
target_compile_features(okay-editor PRIVATE cxx_std_17)
target_include_directories(okay-editor BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/okayui/probe)   # "imgui.h" is OkayUI's
target_include_directories(okay-editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/launcher   # editor/: shared/ScriptIdeApp.hpp includes ScriptIde.hpp
    ${CMAKE_SOURCE_DIR}/okayui/include ${CMAKE_SOURCE_DIR}/okayui/screens ${CMAKE_SOURCE_DIR}/okayui/soft)
target_link_libraries(okay-editor PRIVATE okay::engine okay_ui okay_ui_screens okay_ui_soft ${OKAY_SDL_LIB})
# View > Panels > Command Line (shared/CommandLinePanel.hpp): okay's commands run inside the editor. okay_cli_panel is made after
# this folder (top CMakeLists.txt, with okay_cli_lib), so it is named through generator expressions; without it the panel is absent.
target_link_libraries(okay-editor PRIVATE $<TARGET_NAME_IF_EXISTS:okay_cli_panel>)
target_compile_definitions(okay-editor PRIVATE $<$<TARGET_EXISTS:okay_cli_panel>:OKAY_HAVE_CLI_PANEL=1>)
if(WIN32)
    # Embed the OkaySpace app icon so OkayEngine.exe shows a real taskbar /
    # Explorer / shortcut icon (same icon as the launcher, so they match).
    enable_language(RC)
    target_sources(okay-editor PRIVATE okayspace.rc)
    okay_win_version(okay-editor "OkaySpace Editor" "OkayEngine.exe")
    target_link_libraries(okay-editor PRIVATE comdlg32 ole32 shell32 user32)
    # dbghelp: the crash handler resolves each backtrace frame to a function name
    # (and file:line where available) so okay_crash.txt is human-readable.
    target_link_libraries(okay-editor PRIVATE dbghelp)
    # Keep DWARF line info in the shipped editor so a crash's module+offset backtrace
    # resolves to exact source lines (addr2line) and dbghelp can report file:line.
    # Debug info lives in a separate part of the PE and doesn't slow the code.
    target_compile_options(okay-editor PRIVATE -g)
    target_link_options(okay-editor PRIVATE -g)
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

# EXPERIMENTAL Xbox One Developer Mode (UWP) export entry in File > Build Settings. OFF unless -DOKAY_BUILD_XBOX_UWP=ON
# (docs/platform/xbox_uwp.md). Header-only: nothing to link.
if(OKAY_BUILD_XBOX_UWP)
    target_compile_definitions(okay-editor PRIVATE OKAY_XBOX_UWP_EXPORT=1)   # only the File > Build Settings export entry: NEVER OKAY_XBOX_UWP, which makes the engine headers act as the Xbox app (no WinHTTP, Xbox IPC and sign-in)
    target_include_directories(okay-editor PRIVATE ${CMAKE_SOURCE_DIR}/platform/xbox_uwp)
endif()

if(OKAY_ENABLE_TEST_HOOKS)   # scripted input for headless runs (shared/TestHooks.hpp); off in every normal and release build
    target_compile_definitions(okay-editor PRIVATE OKAY_ENABLE_TEST_HOOKS=1)
endif()
