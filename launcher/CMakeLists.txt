# OkaySpace Launcher (Create / Play / Marketplace / chat ...), drawn by OkayUI, our own UI toolkit (okayui/): the pages are written in
# Dear ImGui's names and compiled against OkayUI's compatibility layer (okayui/probe first on the include path: "imgui.h" and
# "ImGuiPainterRenderer.hpp" are OkayUI's). No Dear ImGui is in it. Needs SDL2 dev libraries (the window on Windows). Built when
# -DOKAY_BUILD_LAUNCHER=ON. docs/ui/own_ui_toolkit.md section 31.
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    message(WARNING "okayspace-launcher needs SDL2 dev libraries; skipping.")
    return()
endif()
if(NOT TARGET okay_ui OR NOT TARGET okay_ui_screens OR NOT TARGET okay_ui_soft)
    message(FATAL_ERROR "okayspace-launcher is drawn by OkayUI: okay_ui, okay_ui_screens and okay_ui_soft (okayui/CMakeLists.txt) must be in the build")
endif()

if(OKAY_EDITOR_STATIC_SDL AND TARGET SDL2::SDL2-static)
    set(OKAY_LAUNCHER_SDL SDL2::SDL2-static)
elseif(TARGET SDL2::SDL2)
    set(OKAY_LAUNCHER_SDL SDL2::SDL2)
else()
    set(OKAY_LAUNCHER_SDL ${SDL2_LIBRARIES})
endif()

# Embed a real Windows .exe icon (taskbar / Explorer / title bar) via windres.
set(OKAY_LAUNCHER_SOURCES main.cpp
    ChatText.cpp          # emoji, chat text formatting, link previews (moved out of main.cpp; docs/engine/modularization_plan.md Part C)
    AntiCheatPages.cpp    # Admin > Anti-cheat, my bans and appeals, session tickets (moved out of main.cpp)
    CoverArtAvatars.cpp   # game cover art, avatar textures (moved out of main.cpp)
    NavIcons.cpp          # the sidebar's line icons, round icon buttons, stars (moved out of main.cpp)
    NewsHistory.cpp       # What's New history, news.cfg (moved out of main.cpp)
    BrowserPage.cpp       # the Browser page: our own web pages shown with our own page engine (engine/include/okay/Browser/)
    AdminPage.cpp         # reports, Admin > Reports, the Admin page, the ban banner (moved out of main.cpp)
    PublicProfilePage.cpp # public profiles: the page, showcase, badges (moved out of main.cpp)
    MemberModerationUI.cpp   # kick / ban / unban / admin role for servers and group chats (moved out of main.cpp)
    GroupConversation.cpp    # the open group conversation (moved out of main.cpp)
    Multiplayer.cpp       # joining, inviting, presence, server browser, parties, the Multiplayer page (moved out of main.cpp)
    VersionsManager.cpp   # the Versions page: installed editors, install / remove / prune, Latest / LTS (no SDL or ImGui; tested by launcher_versions)
    VersionsNet.cpp       # the cancellable download behind it (NativeFetch: WinHTTP / our own client)
    VersionsUI.cpp        # the page itself
    CommandLine.cpp       # the Command line page's rules: words, Tab completion, history, running okay (no ImGui; tested by launcher_cli)
    CommandLineUI.cpp     # the Command line page and the "Open command line" action
    Terminal.cpp          # the Command line page's terminal emulator: screen, scrollback, VT parser, selection, keys (no ImGui; tested by launcher_terminal)
    ${CMAKE_SOURCE_DIR}/tools/okay-cli/CommandTable.cpp   # okay's own command list (Tab completion), std only
    ${CMAKE_SOURCE_DIR}/tools/okay-cli/CommandInput.cpp   # okay's typed-line rules: words, Tab completion, history, output lines (shared with the editor's panel)
    ${CMAKE_SOURCE_DIR}/tools/okay-cli/Process.cpp        # okay's one place that starts a program (no shell)
    ${CMAKE_SOURCE_DIR}/shared/BrowserHost.cpp                        # the Browser page's fetches and images (shared with browser/)
    ${CMAKE_SOURCE_DIR}/shared/FileDialog.cpp                         # file picker (chat attachments): system dialogs or the built-in browser
    ${CMAKE_SOURCE_DIR}/shared/FileDialogUI.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/Net/ServerQuery.cpp                # server browser queries (+ LAN scan)
    ${CMAKE_SOURCE_DIR}/engine/src/Net/UdpSocket.cpp)
if(WIN32)
    enable_language(RC)
    list(APPEND OKAY_LAUNCHER_SOURCES okayspace.rc
         TrayWin32.cpp)   # the system-tray icon and its menu (Shell_NotifyIconW); the decisions are in TrayLogic.hpp
endif()
# Our own image decoder (cover art, avatars, chat images): self-contained, std-only, so it is
# compiled straight into the launcher rather than linking the engine.
list(APPEND OKAY_LAUNCHER_SOURCES
    ${CMAKE_SOURCE_DIR}/engine/src/Core/ImageDecode.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/Core/ImageDecodeJpeg.cpp ${CMAKE_SOURCE_DIR}/engine/src/Core/ImageDecodeWebp.cpp ${CMAKE_SOURCE_DIR}/engine/src/Core/ImageDecodeSvg.cpp)
add_executable(okayspace-launcher ${OKAY_LAUNCHER_SOURCES})
okay_win_version(okayspace-launcher "OkaySpace Launcher" "OkaySpace.exe")
target_compile_definitions(okayspace-launcher PRIVATE ${OKAY_VERSION_DEFINE} ${OKAY_MAIN_DEFINE})
target_compile_features(okayspace-launcher PRIVATE cxx_std_17)
# shared/ for the launcher's AppIcon, and the engine's public headers for the
# (header-only) account service it shares with the engine. The account core needs only
# the standard library; the engine library is linked only for the 2D painter the splash
# screen draws through (okay::platform::WrapSdl2Renderer, whose font and image upload
# need the engine's TrueType and image code). It is a static library, so the linker takes
# only the objects the launcher actually uses.
target_include_directories(okayspace-launcher BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/okayui/probe)   # "imgui.h" is OkayUI's
# shared/ for the launcher's AppIcon, and the engine's public headers for the (header-only) account service it shares with the engine.
target_include_directories(okayspace-launcher PRIVATE
    ${CMAKE_SOURCE_DIR}/shared
    ${CMAKE_SOURCE_DIR}/tools/okay-cli
    ${CMAKE_SOURCE_DIR}/engine/include
    ${CMAKE_SOURCE_DIR}/okayui/include
    ${CMAKE_SOURCE_DIR}/okayui/screens)
# OKAY_ON_OKAYUI: the shared helpers (shared/ImGuiPlatformFeed.hpp, ...) take their OkayUI path.
target_compile_definitions(okayspace-launcher PRIVATE OKAY_ON_OKAYUI=1)
# OkayUI (the toolkit, its screens such as What's New and Tips, the software painter's frame drawer) and the Browser page (the same
# browser as the browser program: shared/BrowserApp.cpp over okay_browser).
target_sources(okayspace-launcher PRIVATE ${CMAKE_SOURCE_DIR}/shared/BrowserApp.cpp)
target_link_libraries(okayspace-launcher PRIVATE okay_ui_screens okay_ui okay_ui_soft okay_browser)
# okay::platform: the seam over SDL2 (timers, audio devices); it brings its own SDL2 link.
target_link_libraries(okayspace-launcher PRIVATE ${OKAY_LAUNCHER_SDL} okay::platform)
if(TARGET okay_engine)
    target_link_libraries(okayspace-launcher PRIVATE okay_engine)
endif()
# engine/include/okay/AntiCheat/AntiCheatCrypto.hpp is a thin wrapper over okay::crypto (our own Ed25519 and SHA-512; libsodium is used for
# Ed25519 instead where the build has it). Link the crypto library by itself so the launcher does not depend on the
# engine for it. (okay_crypto is always defined: engine/ is always added.)
if(TARGET okay_crypto)
    target_link_libraries(okayspace-launcher PRIVATE okay_crypto)
else()
    message(FATAL_ERROR "okayspace-launcher needs okay_crypto (engine/CMakeLists.txt) for engine/include/okay/AntiCheat/AntiCheatCrypto.hpp")
endif()
# The Windows system-tray icon (Shell_NotifyIcon) lives in shell32.
if(WIN32)
    target_link_libraries(okayspace-launcher PRIVATE shell32 comdlg32 ole32 user32 ws2_32 winhttp)   # + file dialogs, voice UDP, native HTTPS
endif()
set_target_properties(okayspace-launcher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    OUTPUT_NAME "OkaySpace")    # the single thing the user double-clicks

# On Windows, build as a GUI app so double-clicking it doesn't open a console
# window behind the launcher. (Child commands are already run hidden.)
if(WIN32)
    set_target_properties(okayspace-launcher PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(OKAY_ENABLE_TEST_HOOKS)   # scripted input for headless runs (shared/TestHooks.hpp); off in every normal and release build
    target_compile_definitions(okayspace-launcher PRIVATE OKAY_ENABLE_TEST_HOOKS=1)
endif()
