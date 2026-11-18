# EXPERIMENTAL, UNCOMPILED, UNVERIFIED: the Xbox UWP app shell. Its own CMake project: it must NOT see the engine.
# Started by scripts/build-xbox-uwp.ps1 on a Windows PC with Visual Studio 2022 + the UWP tools:
#   cmake -S platform/xbox_uwp/app -B <dir> -G "Visual Studio 17 2022" -A x64 -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0
#   cmake --build <dir> --config Release
# Written from Microsoft's CMake "cross compiling for UWP" notes and the Windows SDK's C++/WinRT, DXGI and Direct3D 11 headers.
cmake_minimum_required(VERSION 3.20)
project(OkaySpaceXbox LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    message(FATAL_ERROR "This is the Xbox UWP app. Configure it with -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0 "
                        "(scripts/build-xbox-uwp.ps1 does). It cannot be built on Linux or as a desktop program.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(OkaySpaceXbox WIN32 main.cpp shaders/tri_vs.hlsl shaders/tri_ps.hlsl)
# HLSL is compiled to .cso at BUILD time: D3DCompile is not available inside a UWP app at run time.
set_source_files_properties(shaders/tri_vs.hlsl PROPERTIES VS_SHADER_TYPE Vertex VS_SHADER_MODEL 4.0 VS_SHADER_ENTRYPOINT main VS_SHADER_ENABLE_DEBUG OFF)
set_source_files_properties(shaders/tri_ps.hlsl PROPERTIES VS_SHADER_TYPE Pixel  VS_SHADER_MODEL 4.0 VS_SHADER_ENTRYPOINT main VS_SHADER_ENABLE_DEBUG OFF)
target_compile_options(OkaySpaceXbox PRIVATE /EHsc /permissive- /W4)
target_compile_definitions(OkaySpaceXbox PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(OkaySpaceXbox PRIVATE WindowsApp d3d11 dxgi)   # WindowsApp.lib is the UWP umbrella import library
set_target_properties(OkaySpaceXbox PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
