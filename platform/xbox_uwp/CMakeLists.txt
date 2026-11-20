# EXPERIMENTAL Xbox One Developer Mode export (UWP). OFF by default: -DOKAY_BUILD_XBOX_UWP=ON.
# Everything Xbox lives in this folder. Removing it needs only the option, its add_subdirectory line in the top
# CMakeLists.txt and the few `#if OKAY_XBOX_UWP` lines in editor/main.cpp. docs/platform/xbox_uwp.md, docs/platform/xbox_uwp.md.
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

# okay-xbox-export: writes a minimal TEST export folder from the command line (scripts/build-xbox-uwp.ps1 -MinimalTest compiles it with cl.exe).
# It compiles the same four engine image files the script gives cl.exe (no engine library), so this build and the script's build are the same thing.
add_executable(okay-xbox-export tool/okay_xbox_export.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/Graphics/Image.cpp ${CMAKE_SOURCE_DIR}/engine/src/Core/ImageDecode.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/Core/ImageDecodeJpeg.cpp ${CMAKE_SOURCE_DIR}/engine/src/Core/ImageWrite.cpp)
target_include_directories(okay-xbox-export PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_include_directories(okay-xbox-export PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/engine/include)
set_target_properties(okay-xbox-export PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

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
    # The log file the owner reads from the Device Portal (XboxLog.hpp). Header-only, no engine.
    add_executable(test_xbox_uwp_next tests/test_xbox_uwp_next.cpp)
    target_include_directories(test_xbox_uwp_next PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME xbox_uwp_next COMMAND test_xbox_uwp_next ${CMAKE_CURRENT_BINARY_DIR})
    # Gamepad hot-plug and player slots (XboxPadHub.hpp). Header-only, no engine.
    add_executable(test_xbox_uwp_padhub tests/test_xbox_uwp_padhub.cpp)
    target_include_directories(test_xbox_uwp_padhub PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME xbox_uwp_padhub COMMAND test_xbox_uwp_padhub)
    # The non-blocking SDK check (XboxAsyncProbe.hpp): state machine, "UI call returns in under 5 ms while a 300 ms probe runs", vswhere JSON, cache file.
    add_executable(test_xbox_uwp_async tests/test_xbox_uwp_async.cpp)
    target_include_directories(test_xbox_uwp_async PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    find_package(Threads)
    if(Threads_FOUND)
        target_link_libraries(test_xbox_uwp_async PRIVATE Threads::Threads)
    endif()
    add_test(NAME xbox_uwp_async COMMAND test_xbox_uwp_async)
    # The real pipeline on Linux: export folder -> our packer -> our reader, plus static checks of the PowerShell scripts and the manifest.
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME xbox_uwp_pipeline COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/scripts/check_xbox_uwp.py pipeline
                 --export $<TARGET_FILE:okay-xbox-export> --appx $<TARGET_FILE:okay-appx> --work ${CMAKE_CURRENT_BINARY_DIR}/pipeline)
        add_test(NAME xbox_uwp_scripts COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/scripts/check_xbox_uwp.py scripts ${CMAKE_SOURCE_DIR})
    endif()
    # The engine source list is clean of forbidden APIs (text scan), and CMake and the C++ parser agree on the list.
    add_test(NAME xbox_uwp_audit COMMAND okay-uwp-audit ${CMAKE_SOURCE_DIR})
    add_test(NAME xbox_uwp_sources_cmake COMMAND ${CMAKE_COMMAND} -DOKAY_ROOT=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/engine/uwp_sources.cmake)

    # Models through OUR OWN readers only, compiled the way okay_engine_uwp does (OKAY_XBOX_UWP, every OKAY_OWN_<FMT>_DEFAULT that is ON, no ufbx.h, no Assimp):
    # ModelImport.cpp and FbxScene.cpp are compiled again into this test (its objects win over the same ones in the engine library). Linux, no Windows needed.
    include(${CMAKE_CURRENT_SOURCE_DIR}/engine/uwp_own_readers.cmake)
    okay_uwp_own_readers("${CMAKE_SOURCE_DIR}/CMakeLists.txt" _xbox_mi_defs _xbox_own_on)
    add_executable(test_xbox_uwp_models tests/test_xbox_uwp_models.cpp ${CMAKE_SOURCE_DIR}/engine/src/Render/ModelImport.cpp ${CMAKE_SOURCE_DIR}/engine/src/Render/FbxScene.cpp)
    target_link_libraries(test_xbox_uwp_models PRIVATE okay::engine)
    target_include_directories(test_xbox_uwp_models PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/engine/src)
    target_compile_definitions(test_xbox_uwp_models PRIVATE OKAY_XBOX_UWP=1 OKAY_SOURCE_DIR="${CMAKE_SOURCE_DIR}" OKAY_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}")
    set_source_files_properties(${CMAKE_SOURCE_DIR}/engine/src/Render/ModelImport.cpp PROPERTIES COMPILE_DEFINITIONS "${_xbox_mi_defs}")
    add_test(NAME xbox_uwp_models COMMAND test_xbox_uwp_models WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()
