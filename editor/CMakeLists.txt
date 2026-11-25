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

# Pin a specific Dear ImGui *docking-branch* commit so editor builds are
# reproducible (the docking branch has DockBuilder + IMGUI_HAS_DOCK). Fetching a
# bare branch name was non-deterministic across machines/CI, so we pin a SHA;
# checking out a SHA needs the full history (no GIT_SHALLOW). Override with
# -DOKAY_IMGUI_TAG=<ref> if you want a different revision (the tag is set in the
# top-level CMakeLists.txt, shared with the tests).
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        ${OKAY_IMGUI_TAG})
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

add_executable(okay-editor main.cpp EditorState.cpp FpHandsWindow.cpp
    BuildStamp.cpp                                                     # __DATE__ " " __TIME__ for the crash handler, out of main.cpp so ccache can reuse main.cpp
    UIGalleryWindow.cpp                                                # the UI Gallery window (search every UI part, preview it, add it)
    AnimatorWindow.cpp                                                 # the Animator window (state-machine graph)
    TimelineWindow.cpp                                                 # the Timeline window (cutscene sequences)
    NavigationWindow.cpp                                               # the Navigation window (navmesh bake + overlays)
    BuildSettingsWindow.cpp                                            # the Build Settings window: page list + search, Game and Advanced pages, summary, checks, history
    BuildGame.cpp                                                      # Build Game: export the game, Web / Android / Xbox data, the dedicated server (builder::)
    Updater.cpp                                                        # the self-updater and runtime fetcher (updater::): check, download with checksums, install
    ScriptAssist.cpp                                                   # Scripting Reference window; the Script Editor's completion, signatures, docs, C# members, palette
    FindInFiles.cpp                                                    # Find in Files: search every project script, open a hit in the Script Editor
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
    OkayPanels.cpp                                                     # Crash Log and Audio Mixer hosted by OkayUI (opt-in backend)
    OkayUIHostMode.cpp                                                 # window mode: OkayUI owns the window (OKAY_UI_HOST=okayui, dev switch): screen + state
    OkayUIHostRun.cpp                                                  # window mode: backend, window, painter, frame loop, the editor's game step
    OkayUIHostDialogs.cpp                                              # window mode: the error modal and the save question (OkayUI popups)
    OkayUIHostViews.cpp                                                # window mode: the Scene and Game views (SceneViewCore.hpp, the engine software renderer, no Dear ImGui)
    ScriptEditorCodeEdit.cpp                                           # the script editor's adapter over OkayUI::CodeEdit (no ImGui)
    ProblemsPanel.cpp                                                  # Problems panel + project diagnostics
    SceneManagerPanel.cpp                                              # Scene Manager window
    NetSimPanel.cpp                                                    # net simulator + traffic panel
    AntiCheatSetupUi.cpp                                               # easy anti-cheat setup: presets, Add Anti-Cheat, checklist
    UIInspectorLayout.cpp                                              # flex / mask / list view inspectors
    UIInspectorOverlay.cpp                                             # modal / context menu / keybind inspectors
    UIInspectorWidgetsMore.cpp                                         # search, date, files, accordion, crumbs, pages, skeleton, sheet
    InspectorPanel.cpp InspectorModel.cpp PrefabEdit.cpp                              # Inspector / Hierarchy: reference fields, generic component card, filter, drag-drop logic
    UIPreviewPainter.cpp                                               # engine UI renderer -> ImGui draw list
    ExampleScenes.cpp                                                  # File > Examples scenes saved into the project
    UIRuntimeTools.cpp                                                 # UI themes, runtime UI widgets, File > Examples > UI Gallery
    LocalizationWindow.cpp                                             # the Localization window (string table)
    WeaponEditor.cpp                                                   # the Weapons window (weapon definitions)
    LocalizationTools.cpp                                              # grid, check, scan and preview the window draws (ImGui-free)
    ModelingEasyUi.cpp                                                 # beginner view of the Modeling window (logic in ModelingEasy*.hpp)
    GuidedPanel.cpp                                                    # Guided page of the Modeling window (templates, parts, regeneration)
    ContentEditors.cpp                                                 # Items, Loot & Recipes (.okayitems, .okayloot, .okayrecipes)
    ProjectBrowser.cpp ProjectPanelUI.cpp                              # the Project panel: folder model + safe file operations, name prompt / delete confirm / import settings
    ProjectMenuDraw.cpp                                                # drawing of the Project panel's right-click menus and the Properties rows (ImGui only; drawn headless for the screenshots)
    LaunchFile.cpp                                                     # open a file with the system's program, in a text editor, or show it in the file manager (no shell; never a program or script)
    ${CMAKE_SOURCE_DIR}/launcher/VersionsManager.cpp                  # the version list reader + download plan (checksums required), shared with the launcher updater
    ProjectUpgrade.cpp ProjectUpgradeUI.cpp                            # the project version question, backup + upgrade, File > Project Upgrade...
    VsGraphShot.cpp                                                    # --vs-shot: screenshots of example graphs
    ${CMAKE_SOURCE_DIR}/shared/SdlDisplays.cpp                         # monitor list and display-mode changes (SDL, shared with the player)
    ${CMAKE_SOURCE_DIR}/shared/FileDialog.cpp                          # open / save / folder dialogs: system dialogs or the built-in browser (model)
    ${CMAKE_SOURCE_DIR}/shared/FileDialogUI.cpp)                       # the built-in browser's ImGui window
target_compile_definitions(okay-editor PRIVATE ${OKAY_VERSION_DEFINE})
target_include_directories(okay-editor PRIVATE ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/launcher)
target_link_libraries(okay-editor PRIVATE okay::engine okay_imgui ${OKAY_SDL_LIB})
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
# OkayUI overlay demo (renders on top of ImGui through the same SDL_Renderer).
if(TARGET okay_ui)
    # Window mode's copies of the Console, Scene Manager and Project-dialog panels: the same sources, compiled against OkayUI's Dear ImGui compatibility header
    # (okayui/probe first on the include path, so no Dear ImGui is involved) in their own object library, then linked in. docs/ui/own_ui_toolkit.md 6.24 step 5.
    add_library(okay_editor_wm_panels OBJECT OkayUIHostPanels.cpp OkayUIHostObjects.cpp OkayUIHostMenu.cpp)   # + the Hierarchy and Inspector windows (InspectorPanel.cpp and EditorWidgets.cpp again, suffix _WO), 6.24 step 9
    target_include_directories(okay_editor_wm_panels BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/okayui/probe)
    target_include_directories(okay_editor_wm_panels PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/launcher ${CMAKE_SOURCE_DIR}/okayui/screens)
    target_link_libraries(okay_editor_wm_panels PRIVATE okay_ui okay_ui_screens okay::engine)
    target_sources(okay-editor PRIVATE $<TARGET_OBJECTS:okay_editor_wm_panels>)
    target_link_libraries(okay-editor PRIVATE okay_ui okay_ui_screens okay_ui_soft)
    target_link_libraries(okay-editor PRIVATE okay_ui okay_ui_screens)
    if(TARGET okay_ui_soft)   # the software painter: the editor's window on the own platform backend (OKAY_PLATFORM_BACKEND=own)
        target_link_libraries(okay-editor PRIVATE okay_ui_soft)
    endif()
    if(TARGET okay_ui_render)   # OkayUI's own OpenGL renderer for the hosted screens (OKAY_UI_OWN_RENDER=1, off by default; shared/OkayUIFrame.hpp)
        target_link_libraries(okay-editor PRIVATE okay_ui_render)
    endif()
    target_compile_definitions(okay-editor PRIVATE OKAY_HAVE_OKAYUI=1)
endif()
if(OKAY_UI_DEFAULT_BACKEND STREQUAL "okayui" OR OKAY_OWN_STACK)   # OKAY_OWN_STACK: the dogfood build starts on OkayUI
    target_compile_definitions(okay-editor PRIVATE OKAY_UI_DEFAULT_BACKEND_OKAYUI=1)
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
    target_compile_definitions(okay-editor PRIVATE OKAY_XBOX_UWP=1)
    target_include_directories(okay-editor PRIVATE ${CMAKE_SOURCE_DIR}/platform/xbox_uwp)
endif()

if(OKAY_ENABLE_TEST_HOOKS)   # scripted input for headless runs (shared/TestHooks.hpp); off in every normal and release build
    target_compile_definitions(okay-editor PRIVATE OKAY_ENABLE_TEST_HOOKS=1)
endif()

# ---- The editor on OkayUI (OKAY_EDITOR_ON_OKAYUI, off by default) --------------------------------------------------------------------------------
# The SAME sources as okay-editor, compiled against OkayUI's Dear ImGui compatibility header (okayui/probe first on the include path: <imgui.h> is OkayUI's),
# so no Dear ImGui is in the program. Input is shared/ImGuiPlatformFeed.hpp (OKAY_ON_OKAYUI makes it the only path), the painter-renderer is
# okayui/probe/ImGuiPainterRenderer.hpp, the backend entry points shared/OkayUIImGuiBackendShim.cpp. Output: bin/OkayEngineOkayUI(.exe) next to OkayEngine(.exe).
# Not built in the session that added it (the editor needs the whole engine library): checked by the CI job and by the compat probe. docs/ui/own_ui_toolkit.md
if(OKAY_EDITOR_ON_OKAYUI AND TARGET okay_ui AND TARGET okay_ui_screens)
    get_target_property(_ed_srcs okay-editor SOURCES)
    list(FILTER _ed_srcs EXCLUDE REGEX "TARGET_OBJECTS")   # the window-mode panel copies are Dear ImGui-API sources compiled for the other editor
    list(FILTER _ed_srcs EXCLUDE REGEX "okayspace\\.rc$")
    add_executable(okay-editor-okayui ${_ed_srcs} ${CMAKE_SOURCE_DIR}/shared/OkayUIImGuiBackendShim.cpp)
    target_compile_definitions(okay-editor-okayui PRIVATE ${OKAY_VERSION_DEFINE} OKAY_ON_OKAYUI=1 OKAY_UI_DEFAULT_BACKEND_OKAYUI=1)
    target_compile_features(okay-editor-okayui PRIVATE cxx_std_17)
    target_include_directories(okay-editor-okayui BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/okayui/probe ${CMAKE_SOURCE_DIR}/okayui/probe/backends)
    target_include_directories(okay-editor-okayui PRIVATE ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/launcher
        ${CMAKE_SOURCE_DIR}/okayui/include ${CMAKE_SOURCE_DIR}/okayui/screens ${CMAKE_SOURCE_DIR}/okayui/soft)
    target_link_libraries(okay-editor-okayui PRIVATE okay::engine okay_ui okay_ui_screens okay_ui_soft ${OKAY_SDL_LIB})
    if(OKAY_ENABLE_TEST_HOOKS)
        target_compile_definitions(okay-editor-okayui PRIVATE OKAY_ENABLE_TEST_HOOKS=1)
    endif()
    if(WIN32)
        enable_language(RC)
        target_sources(okay-editor-okayui PRIVATE okayspace.rc)
        okay_win_version(okay-editor-okayui "OkaySpace Editor (OkayUI)" "OkayEngineOkayUI.exe")
        target_link_libraries(okay-editor-okayui PRIVATE comdlg32 ole32 shell32 user32 dbghelp)
    endif()
    set_target_properties(okay-editor-okayui PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin OUTPUT_NAME "OkayEngineOkayUI")
endif()
