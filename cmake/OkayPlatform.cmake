# okay_platform: the seam between the engine and the OS (engine/include/okay/Platform).
#
# Desktop (Linux, Windows): our own backend is the only one (Linux X11/GLX + ALSA + evdev through dlopen,
# Windows Win32/WGL/WASAPI/XInput/HID). SDL2 left the desktop programs on 2026-11-27 (docs/platform/sdl_removal_plan.md).
# Web (Emscripten): the sdl2 backend over Emscripten's own SDL2 port (-sUSE_SDL=2; nothing from SDL is in the
# repository or downloaded by us), because there is no own web backend yet. That is the one place SDL2 remains.
if(EMSCRIPTEN)
    set(OKAY_PLATFORM_BACKEND "sdl2" CACHE STRING "okay::platform backend: sdl2 on the web (Emscripten), own everywhere else" FORCE)
else()
    set(OKAY_PLATFORM_BACKEND "own" CACHE STRING "okay::platform backend: sdl2 on the web (Emscripten), own everywhere else" FORCE)
endif()

function(okay_add_platform_library)
    set(_srcs src/Platform/Platform.cpp)
    set(_defs "")
    set(_libs "")
    set(_incs "")
    set(_sdl_opts "")

    # --- sdl2 backend: the web build only (Emscripten's SDL2 port) -------------------
    if(EMSCRIPTEN)
        list(APPEND _srcs src/Platform/SdlBackend.cpp)
        list(APPEND _defs OKAY_PLATFORM_HAVE_SDL2)
        set(_sdl_opts "-sUSE_SDL=2")
    endif()

    # --- own backend ----------------------------------------------------------------
    if(WIN32)
        list(APPEND _srcs src/Platform/OwnWin32.cpp src/Platform/OwnWin32Audio.cpp src/Platform/OwnWin32Hid.cpp)
        list(APPEND _defs OKAY_PLATFORM_HAVE_OWN)
        list(APPEND _libs user32 gdi32 opengl32 shell32 ole32 imm32 hid)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND _srcs src/Platform/OwnLinux.cpp src/Platform/OwnLinuxX11.cpp
                          src/Platform/OwnLinuxAudio.cpp src/Platform/OwnLinuxEvdev.cpp)
        list(APPEND _defs OKAY_PLATFORM_HAVE_OWN)
        list(APPEND _libs ${CMAKE_DL_LIBS})
        # X11 and GLX are loaded with dlopen at run time, so there is no link dependency.
        # The X11 / GL header files are only used for type and constant definitions; when
        # they are absent the X11 part compiles to "no display available".
        include(CheckIncludeFileCXX)
        check_include_file_cxx("X11/Xlib.h" OKAY_PLATFORM_HAVE_XLIB_H)
        check_include_file_cxx("GL/glx.h" OKAY_PLATFORM_HAVE_GLX_H)
        if(OKAY_PLATFORM_HAVE_XLIB_H AND OKAY_PLATFORM_HAVE_GLX_H)
            list(APPEND _defs OKAY_PLATFORM_X11_HEADERS)
        else()
            message(STATUS "OkaySpace: X11/GLX headers not found; the own Linux backend will have no windowing (audio, gamepad and timers still work)")
        endif()
    endif()

    if(NOT EMSCRIPTEN AND NOT TARGET Threads::Threads)
        find_package(Threads REQUIRED)
    endif()
    # The painter files (engine/src/Platform/Painter.cpp, GLPainter.cpp, and SdlPainter.cpp on the web) are compiled into
    # okay_engine, not into this library (they need the engine's image decoder); okay_engine takes the web's SDL option from here.
    set(OKAY_PLATFORM_SDL_OPTIONS ${_sdl_opts} PARENT_SCOPE)
    add_library(okay_platform STATIC ${_srcs})
    target_include_directories(okay_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_include_directories(okay_platform PRIVATE ${_incs})
    target_compile_features(okay_platform PUBLIC cxx_std_17)
    target_compile_definitions(okay_platform PRIVATE ${_defs})
    if(_sdl_opts)
        target_compile_options(okay_platform PRIVATE ${_sdl_opts})
    endif()
    if(NOT EMSCRIPTEN)
        target_compile_definitions(okay_platform PRIVATE OKAY_PLATFORM_DEFAULT_OWN)
    endif()
    # PUBLIC: the test programs and apps ask which backends exist.
    foreach(_d ${_defs})
        target_compile_definitions(okay_platform PUBLIC ${_d})
    endforeach()
    target_link_libraries(okay_platform PUBLIC ${_libs} Threads::Threads)
    add_library(okay::platform ALIAS okay_platform)
endfunction()
