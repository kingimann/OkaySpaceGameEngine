# okay::imui - our own immediate-mode UI core (side by side with Dear ImGui, not used by the editor yet).
# Can be built alone:  cmake -S shared/imui -B build-imui && cmake --build build-imui
# or as part of the main build with -DOKAY_BUILD_IMUI=ON (the default).
cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(okay_imui CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release)
    endif()
    option(OKAY_BUILD_TESTS "Build the imui tests" ON)
    set(OKAY_SANITIZE "" CACHE STRING "Build with -fsanitize=<list>")
    if(MSVC)
        add_compile_options(/W4 /permissive-)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic)
    endif()
    if(OKAY_SANITIZE AND NOT MSVC)
        add_compile_options(-fsanitize=${OKAY_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
        add_link_options(-fsanitize=${OKAY_SANITIZE} -fno-sanitize-recover=all)
    endif()
    if(OKAY_BUILD_TESTS)
        enable_testing()
    endif()
endif()

get_filename_component(OKAY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# The library itself depends on nothing (no SDL, no OpenGL, no engine).
add_library(okay_imui STATIC Imui.cpp ImuiDraw.cpp ImuiSoft.cpp)
target_include_directories(okay_imui PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
add_library(okay::imui ALIAS okay_imui)

# Test and demo helpers: the engine's own 8x8 font glyphs and its own PNG writer, compiled in directly
# so these targets do not need the whole engine library.
add_library(okay_imui_support STATIC
    ${OKAY_ROOT}/engine/src/Graphics/Font.cpp
    ${OKAY_ROOT}/engine/src/Core/ImageWrite.cpp)
target_include_directories(okay_imui_support PUBLIC ${OKAY_ROOT}/engine/include)
target_link_libraries(okay_imui_support PUBLIC okay_imui)

add_executable(imui_demo demo/imui_demo.cpp)
target_link_libraries(imui_demo PRIVATE okay_imui_support)

if(OKAY_BUILD_TESTS)
    foreach(t imui_core imui_widgets imui_golden imui_fuzz)
        add_executable(test_${t} tests/test_${t}.cpp)
        target_link_libraries(test_${t} PRIVATE okay_imui_support)
        target_include_directories(test_${t} PRIVATE ${OKAY_ROOT}/tests)
        target_compile_definitions(test_${t} PRIVATE IMUI_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/tests/golden")
        add_test(NAME ${t} COMMAND test_${t})
    endforeach()
endif()
