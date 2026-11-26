# Site check (okay-sitecheck, docs/feature_map/17-launcher-platform.md 17.20; docs/platform/browser_plan.md "Site check"): loads a
# list of real sites through our own browser (the browser program's tab host, page engine and OkayUI view, drawn by the software
# painter) and ranks what fails on them. A developer tool: built with the browser program (it returns quietly when OkayUI is not
# built), never packaged or released.
if(NOT TARGET okay_ui_screens OR NOT TARGET okay_ui_soft OR NOT TARGET okay::engine)
    return()
endif()

add_library(okay_sitecheck_report STATIC SiteReport.cpp)   # the causes, the per-site JSON and the ranking (no network)
target_include_directories(okay_sitecheck_report PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(okay_sitecheck_report PUBLIC okay::engine)

add_executable(okay-sitecheck
    main.cpp
    ${CMAKE_SOURCE_DIR}/shared/BrowserHost.cpp       # the browser program's tab host: fetches, cookies, redirects, images
    ${CMAKE_SOURCE_DIR}/tools/okay-cli/Process.cpp)  # what the tab host links for games (none are run here)
target_include_directories(okay-sitecheck PRIVATE ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/tools/okay-cli
    ${CMAKE_SOURCE_DIR}/okayui/screens ${CMAKE_SOURCE_DIR}/okayui/soft)
target_link_libraries(okay-sitecheck PRIVATE okay_sitecheck_report okay_ui_screens okay_ui_soft okay_browser okay::engine)
target_compile_features(okay-sitecheck PRIVATE cxx_std_17)
set_target_properties(okay-sitecheck PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(WIN32)
    target_link_libraries(okay-sitecheck PRIVATE winhttp)
endif()

# The test: the causes and the ranking, the reporting hooks of the page engine (CSS, "not implemented: X") and of the tab host.
if(OKAY_BUILD_TESTS AND NOT WIN32)
    add_executable(test_site_check test_site_check.cpp ${CMAKE_SOURCE_DIR}/shared/BrowserHost.cpp ${CMAKE_SOURCE_DIR}/tools/okay-cli/Process.cpp)
    target_include_directories(test_site_check PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/shared ${CMAKE_SOURCE_DIR}/tools/okay-cli)
    target_link_libraries(test_site_check PRIVATE okay_sitecheck_report okay_browser okay::engine)
    add_test(NAME site_check COMMAND test_site_check)
    set_tests_properties(site_check PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "browser")
endif()
