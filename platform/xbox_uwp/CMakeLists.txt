# EXPERIMENTAL Xbox One Developer Mode export (UWP). OFF by default: -DOKAY_BUILD_XBOX_UWP=ON.
# Everything Xbox lives in this folder. Removing it needs only the option, its add_subdirectory line in the top
# CMakeLists.txt and the few `#if OKAY_XBOX_UWP` lines in editor/main.cpp. docs/xbox_uwp.md, docs/xbox_uwp_audit.md.
#
# What is built here on any host: the headers are header-only (the editor includes them) and their test.
# The UWP app in app/ is NOT part of this build: it is its own small CMake project (it must not see the engine), which
# scripts/build-xbox-uwp.ps1 configures on a Windows PC with Visual Studio 2022 and the UWP tools. UNVERIFIED: never built.

add_library(okay_xbox_uwp INTERFACE)
add_library(okay::xbox_uwp ALIAS okay_xbox_uwp)
target_include_directories(okay_xbox_uwp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if(OKAY_BUILD_TESTS)
    add_executable(test_xbox_uwp tests/test_xbox_uwp.cpp)
    target_link_libraries(test_xbox_uwp PRIVATE okay::engine okay::xbox_uwp)
    target_include_directories(test_xbox_uwp PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
    add_test(NAME xbox_uwp COMMAND test_xbox_uwp)
endif()

# okay-appx: our own APPX packer and checker (no MakeAppx). Plain C++, header-only SHA-256, no engine link.
# Builds on Linux and on Windows (MinGW or MSVC). scripts/build-xbox-uwp.ps1 builds it on the Windows PC.
add_executable(okay-appx tool/okay_appx.cpp)
target_include_directories(okay-appx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/engine/include)
set_target_properties(okay-appx PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
