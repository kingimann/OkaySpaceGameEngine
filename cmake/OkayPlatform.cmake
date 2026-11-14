# okay_platform: the seam between the engine and SDL2 / the OS (engine/include/okay/Platform).
#
#   -DOKAY_PLATFORM_BACKEND=sdl2   (default) okay::platform::Default() is the SDL2 backend.
#   -DOKAY_PLATFORM_BACKEND=own    Default() is our own backend (Linux X11/ALSA/evdev through
#                                  dlopen, Windows Win32). SDL2 is NOT removed: the editor,
#                                  launcher and player still draw through SDL_Renderer and
#                                  ImGui's SDL backends, and the sdl2 backend stays compiled in
#                                  beside it so both can be tested side by side.
#
# Both backends are always built when they can be; the option only picks the default.
set(OKAY_PLATFORM_BACKEND "sdl2" CACHE STRING "Default okay::platform backend: sdl2 or own")
set_property(CACHE OKAY_PLATFORM_BACKEND PROPERTY STRINGS sdl2 own)
if(NOT OKAY_PLATFORM_BACKEND STREQUAL "sdl2" AND NOT OKAY_PLATFORM_BACKEND STREQUAL "own")
    message(FATAL_ERROR "OKAY_PLATFORM_BACKEND must be sdl2 or own (got '${OKAY_PLATFORM_BACKEND}')")
endif()

function(okay_add_platform_library)
    set(_srcs src/Platform/Platform.cpp src/Platform/SdlBackend.cpp)
    set(_defs "")
    set(_libs "")
    set(_incs "")

    # --- sdl2 backend -------------------------------------------------------------
    if(EMSCRIPTEN)
        list(APPEND _defs OKAY_PLATFORM_HAVE_SDL2)
        set(_sdl_opts "-sUSE_SDL=2")
    else()
        find_package(SDL2 QUIET)
        set(_sdl_opts "")
        if(SDL2_FOUND)
            list(APPEND _defs OKAY_PLATFORM_HAVE_SDL2)
            if(OKAY_EDITOR_STATIC_SDL AND TARGET SDL2::SDL2-static)
                list(APPEND _libs SDL2::SDL2-static)
            elseif(TARGET SDL2::SDL2)
                list(APPEND _libs SDL2::SDL2)
            else()
                list(APPEND _libs ${SDL2_LIBRARIES})
                list(APPEND _incs ${SDL2_INCLUDE_DIRS})
            endif()
        endif()
    endif()

    # --- own backend ----------------------------------------------------------------
    if(WIN32)
        list(APPEND _srcs src/Platform/OwnWin32.cpp)
        list(APPEND _defs OKAY_PLATFORM_HAVE_OWN)
        list(APPEND _libs user32 gdi32 opengl32 shell32)
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
    # The painter files (engine/src/Platform/Painter.cpp, SdlPainter.cpp) are compiled into okay_engine, not
    # into this library (they need the engine's image decoder); okay_engine takes the SDL include path and
    # options from here.
    set(OKAY_PLATFORM_SDL_INCLUDES ${_incs} PARENT_SCOPE)
    set(OKAY_PLATFORM_SDL_OPTIONS ${_sdl_opts} PARENT_SCOPE)
    add_library(okay_platform STATIC ${_srcs})
    target_include_directories(okay_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_include_directories(okay_platform PRIVATE ${_incs})
    target_compile_features(okay_platform PUBLIC cxx_std_17)
    target_compile_definitions(okay_platform PRIVATE ${_defs})
    if(_sdl_opts)
        target_compile_options(okay_platform PRIVATE ${_sdl_opts})
    endif()
    if(OKAY_PLATFORM_BACKEND STREQUAL "own")
        target_compile_definitions(okay_platform PRIVATE OKAY_PLATFORM_DEFAULT_OWN)
    endif()
    # PUBLIC: the test programs and apps ask which backends exist.
    foreach(_d ${_defs})
        target_compile_definitions(okay_platform PUBLIC ${_d})
    endforeach()
    target_link_libraries(okay_platform PUBLIC ${_libs} Threads::Threads)
    add_library(okay::platform ALIAS okay_platform)
endfunction()
