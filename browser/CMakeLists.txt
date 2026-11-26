# OkaySpace Browser: the browser program (system "Browser", docs/feature_map/17-launcher-platform.md 17.18; plan:
# docs/platform/browser_plan.md). A window of its own on the Browser's page engine (okay_browser), the OkayUI view
# (okay_ui_screens), the shared browser (shared/BrowserApp.cpp: the launcher's Browser page draws the same one) and tab host (shared/BrowserHost.cpp), drawn in OkayUI's window mode through okay::platform
# (okay_ui_soft's painter frame drawer). Games on a page run with the player (okay-player), found beside this program.
# Built whenever OkayUI is (on by default, CLAUDE.md rule 17). The Windows package copies it into Tools/ (scripts/build-windows-dist.sh)
# and the launcher's Command line page has a button for it; it is not updated on its own (it comes with each release).
if(NOT TARGET okay_ui_screens OR NOT TARGET okay_ui_soft)
    return()
endif()

add_executable(okay-browser
    main.cpp
    ${CMAKE_SOURCE_DIR}/shared/BrowserApp.cpp        # the browser itself: tabs, menus, find, downloads (shared with the launcher's Browser page)
    ${CMAKE_SOURCE_DIR}/shared/BrowserHost.cpp       # the tab's fetches, images and games (shared with the launcher's page)
    ${CMAKE_SOURCE_DIR}/tools/okay-cli/Process.cpp)  # okay's one place that starts a program (the player; no shell)
target_include_directories(okay-browser PRIVATE
    ${CMAKE_SOURCE_DIR}/shared
    ${CMAKE_SOURCE_DIR}/tools/okay-cli
    ${CMAKE_SOURCE_DIR}/okayui/screens)
target_link_libraries(okay-browser PRIVATE okay_ui_screens okay_ui_soft okay_browser okay::engine)
target_compile_features(okay-browser PRIVATE cxx_std_17)
okay_win_version(okay-browser "OkaySpace Browser" "OkaySpaceBrowser.exe")
set_target_properties(okay-browser PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(WIN32)
    set_target_properties(okay-browser PROPERTIES OUTPUT_NAME "OkaySpaceBrowser" WIN32_EXECUTABLE TRUE)
    target_link_libraries(okay-browser PRIVATE winhttp)
endif()
