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

# Offline shaders: okay-shaders writes the HLSL text and the manifest around fxc's .cso files (scripts/xbox-compile-shaders.ps1).
add_executable(okay-shaders tool/okay_shaders.cpp)
target_include_directories(okay-shaders PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
set_target_properties(okay-shaders PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# okay-uwp-audit: lists APIs a UWP app may not use in the engine sources the UWP target compiles (engine/uwp_exclude.txt).
add_executable(okay-uwp-audit tool/okay_uwp_audit.cpp)
target_include_directories(okay-uwp-audit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(okay-uwp-audit PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(OKAY_BUILD_TESTS)
    # The pure runtime pieces (shader bundle, display, lifecycle, input, paths, audio, backend over a fake host, build runner, audit).
    # NOT linked with okay::engine: UwpPlatform.cpp stands in for Platform.cpp here, exactly as in okay_engine_uwp.
    add_executable(test_xbox_uwp_runtime tests/test_xbox_uwp_runtime.cpp UwpPlatform.cpp ${CMAKE_SOURCE_DIR}/engine/src/Core/Paths.cpp)
    target_include_directories(test_xbox_uwp_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/engine/include)
    add_test(NAME xbox_uwp_runtime COMMAND test_xbox_uwp_runtime)
    # The engine source list is clean of forbidden APIs (text scan), and CMake and the C++ parser agree on the list.
    add_test(NAME xbox_uwp_audit COMMAND okay-uwp-audit ${CMAKE_SOURCE_DIR})
    add_test(NAME xbox_uwp_sources_cmake COMMAND ${CMAKE_COMMAND} -DOKAY_ROOT=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/engine/uwp_sources.cmake)
endif()
