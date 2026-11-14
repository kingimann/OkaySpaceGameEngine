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
    # 32-bit ImWchar: emoji (U+1F000+) survive in text inputs instead of "??".
    target_compile_definitions(launcher_imgui PUBLIC IMGUI_USE_WCHAR32)
endif()

# Embed a real Windows .exe icon (taskbar / Explorer / title bar) via windres.
set(OKAY_LAUNCHER_SOURCES main.cpp
    VersionsManager.cpp   # the Versions page: installed editors, install / remove / prune, Latest / LTS (no SDL or ImGui; tested by launcher_versions)
    VersionsNet.cpp       # the cancellable curl download behind it
    VersionsUI.cpp        # the page itself
    ${CMAKE_SOURCE_DIR}/shared/FileDialog.cpp                         # file picker (chat attachments): system dialogs or the built-in browser
    ${CMAKE_SOURCE_DIR}/shared/FileDialogUI.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/Net/ServerQuery.cpp                # server browser queries (+ LAN scan)
    ${CMAKE_SOURCE_DIR}/engine/src/Net/UdpSocket.cpp)
if(WIN32)
    enable_language(RC)
    list(APPEND OKAY_LAUNCHER_SOURCES okayspace.rc)
endif()
# Our own image decoder (cover art, avatars, chat images): self-contained, std-only, so it is
# compiled straight into the launcher rather than linking the engine.
list(APPEND OKAY_LAUNCHER_SOURCES
    ${CMAKE_SOURCE_DIR}/engine/src/Core/ImageDecode.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/Core/ImageDecodeJpeg.cpp)
add_executable(okayspace-launcher ${OKAY_LAUNCHER_SOURCES})
okay_win_version(okayspace-launcher "OkaySpace Launcher" "OkaySpace.exe")
target_compile_definitions(okayspace-launcher PRIVATE ${OKAY_VERSION_DEFINE})
target_compile_features(okayspace-launcher PRIVATE cxx_std_17)
# shared/ for the launcher's AppIcon, and the engine's public headers for the
# (header-only) account service it shares with the engine. The account core needs only
# the standard library; the engine library is linked only for the 2D painter the splash
# screen draws through (okay::platform::WrapSdl2Renderer, whose font and image upload
# need the engine's TrueType and image code). It is a static library, so the linker takes
# only the objects the launcher actually uses.
target_include_directories(okayspace-launcher PRIVATE
    ${CMAKE_SOURCE_DIR}/shared
    ${CMAKE_SOURCE_DIR}/engine/include)
# okay::platform: the seam over SDL2 (timers, audio devices); it brings its own SDL2 link.
target_link_libraries(okayspace-launcher PRIVATE launcher_imgui ${OKAY_LAUNCHER_SDL} okay::platform)
if(TARGET okay_engine)
    target_link_libraries(okayspace-launcher PRIVATE okay_engine)
endif()
# shared/AntiCheatCrypto.hpp calls okay::crypto when the build has -DOKAY_OWN_CRYPTO=ON (unreviewed, opt-in); link
# the crypto library by itself so the launcher does not depend on the engine for that.
if(TARGET okay_crypto)
    target_link_libraries(okayspace-launcher PRIVATE okay_crypto)
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
