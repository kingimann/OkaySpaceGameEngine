# EXPERIMENTAL, UNCOMPILED, UNVERIFIED: the Xbox UWP app shell. Its own CMake project: it must NOT see the engine.
# Started by scripts/build-xbox-uwp.ps1 on a Windows PC with Visual Studio 2022 or newer + the UWP tools (the SDK check picks the generator, install and toolset):
#   cmake -S platform/xbox_uwp/app -B <dir> -G "<generator for the installed Visual Studio, e.g. Visual Studio 17 2022>" -A x64 -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0
#   cmake --build <dir> --config Release
# Written from Microsoft's CMake "cross compiling for UWP" notes and the Windows SDK's C++/WinRT, DXGI and Direct3D 11 headers.
cmake_minimum_required(VERSION 3.20)
project(OkaySpaceXbox LANGUAGES CXX)   # C is enabled only in engine mode (the engine's ufbx.c), below

if(NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    message(FATAL_ERROR "This is the Xbox UWP app. Configure it with -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0 "
                        "(scripts/build-xbox-uwp.ps1 does). It cannot be built on Linux or as a desktop program.")
endif()

# Language standard. C++/WinRT uses coroutines: in C++17 mode it falls back to <experimental/coroutine>, which newer MSVC STLs reject with
# error STL1011 (the owner's first build, MSVC 14.52). The triangle shell therefore builds as C++20 (real <coroutine>). Engine mode keeps C++17
# (the engine's headers were written for it: u8string() / u8path changed in C++20) and silences the experimental-coroutine error instead.
# /await must never be on: it is removed from the flags below, and scripts/build-xbox-uwp.ps1 logs what the generated vcxproj really contains.
option(OKAY_XBOX_APP_ENGINE "Link okay_engine_uwp and run the exported game (EXPERIMENTAL, UNVERIFIED)" OFF)
if(OKAY_XBOX_APP_ENGINE)
    set(CMAKE_CXX_STANDARD 17)
else()
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
string(REPLACE "/await" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "/ZW" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")   # C++/WinRT is plain C++, not C++/CX

add_executable(OkaySpaceXbox WIN32 main.cpp shaders/tri_vs.hlsl shaders/tri_ps.hlsl)
# Package.appxmanifest: the MSBuild WindowsStore project of an executable asks for one and fails without. This one is a MINIMAL stand-in so the
# build can finish; the real package (manifest, tiles, Data) is written and packed by OUR packer (okay-appx), never by MSBuild, so MSBuild is told
# not to sign or package anything. The three 1x1 images in Assets\ exist only because that manifest names them.
target_sources(OkaySpaceXbox PRIVATE Package.appxmanifest Assets/StoreLogo.png Assets/Square44x44Logo.png Assets/Square150x150Logo.png)
set_source_files_properties(Assets/StoreLogo.png Assets/Square44x44Logo.png Assets/Square150x150Logo.png PROPERTIES VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION "Assets")
set_target_properties(OkaySpaceXbox PROPERTIES
    VS_GLOBAL_AppxPackageSigningEnabled false
    VS_GLOBAL_GenerateAppxPackageOnBuild false
    VS_GLOBAL_AppxBundle Never)
# C++/WinRT (plain C++), not C++/CX: no /ZW. The props file turns CompileAsWinRT off in the project.
set_target_properties(OkaySpaceXbox PROPERTIES VS_USER_PROPS "${CMAKE_CURRENT_SOURCE_DIR}/uwp_cpp.props")
# The C++ runtime: the shell uses the dynamic VC runtime, so the package depends on the Microsoft.VCLibs.140.00 framework package (the generated
# manifest says so; scripts/build-xbox-uwp.ps1 prints how to install it from the Visual Studio ExtensionSDKs folder). The static CRT (/MT) was NOT
# chosen: UWP support for it is unverified. Debug is not supported (the debug runtime framework is not packaged).
if(NOT CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(WARNING "Debug is not supported by the Xbox test app. Use Release.")
endif()
# HLSL is compiled to .cso at BUILD time: D3DCompile is not available inside a UWP app at run time.
set_source_files_properties(shaders/tri_vs.hlsl PROPERTIES VS_SHADER_TYPE Vertex VS_SHADER_MODEL 4.0 VS_SHADER_ENTRYPOINT main VS_SHADER_ENABLE_DEBUG OFF)
set_source_files_properties(shaders/tri_ps.hlsl PROPERTIES VS_SHADER_TYPE Pixel  VS_SHADER_MODEL 4.0 VS_SHADER_ENTRYPOINT main VS_SHADER_ENABLE_DEBUG OFF)
target_compile_options(OkaySpaceXbox PRIVATE /EHsc /permissive- /W4)
target_compile_definitions(OkaySpaceXbox PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS)   # belt and braces for the STL1011 error
target_link_libraries(OkaySpaceXbox PRIVATE WindowsApp d3d11 dxgi)   # WindowsApp.lib is the UWP umbrella import library

# Engine mode (default OFF: the shell then only draws the test triangle). ON runs an exported game from package\Data through
# okay_engine_uwp (platform/xbox_uwp/engine). Needs -DOKAY_SOURCE_ROOT=<OkaySpace source folder>. UNVERIFIED: never configured or built on Windows.
if(OKAY_XBOX_APP_ENGINE)
    enable_language(C)
    if(NOT OKAY_SOURCE_ROOT)
        message(FATAL_ERROR "OKAY_XBOX_APP_ENGINE=ON needs -DOKAY_SOURCE_ROOT=<the OkaySpace source folder>")
    endif()
    add_subdirectory(${OKAY_SOURCE_ROOT}/platform/xbox_uwp/engine ${CMAKE_BINARY_DIR}/engine_uwp)
    target_include_directories(OkaySpaceXbox PRIVATE ${OKAY_SOURCE_ROOT}/platform/xbox_uwp)
    target_compile_definitions(OkaySpaceXbox PRIVATE OKAY_XBOX_APP_ENGINE=1)
    target_link_libraries(OkaySpaceXbox PRIVATE okay_engine_uwp mmdevapi)   # mmdevapi.lib: ActivateAudioInterfaceAsync (XboxWasapi.hpp)
endif()
set_target_properties(OkaySpaceXbox PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
