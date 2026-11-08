# CMake toolchain for cross-compiling a Windows .exe from Linux using MinGW-w64.
#
#   cmake -S . -B build-win \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.toolchain.cmake
#   cmake --build build-win
#
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# libwinpthread.a carries a VERSIONINFO of its own ("POSIX WinThreads for Windows")
# in its version.o member. Linked whole-archive (below), it ends up in every exe and
# is what Task Manager shows as the program's name. Link a copy of the archive without
# that one member: the threading code is identical. If the copy can't be made, fall
# back to plain -lwinpthread exactly as before.
set(_okay_pthread -lwinpthread)
execute_process(COMMAND ${TOOLCHAIN_PREFIX}-gcc -print-file-name=libwinpthread.a
                OUTPUT_VARIABLE _okay_pth_src OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(_okay_pth_src AND EXISTS "${_okay_pth_src}" AND DEFINED CMAKE_BINARY_DIR)
    set(_okay_pth_dir "${CMAKE_BINARY_DIR}/okay-winpthread")
    set(_okay_pth_dst "${_okay_pth_dir}/libwinpthread.a")
    file(MAKE_DIRECTORY "${_okay_pth_dir}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${_okay_pth_src}" "${_okay_pth_dst}"
                    RESULT_VARIABLE _okay_r1 ERROR_QUIET)
    if(_okay_r1 EQUAL 0)
        execute_process(COMMAND ${TOOLCHAIN_PREFIX}-ar d "${_okay_pth_dst}" version.o
                        RESULT_VARIABLE _okay_r2 ERROR_QUIET)
        execute_process(COMMAND ${TOOLCHAIN_PREFIX}-ranlib "${_okay_pth_dst}"
                        RESULT_VARIABLE _okay_r3 ERROR_QUIET)
        if(_okay_r2 EQUAL 0 AND _okay_r3 EQUAL 0)
            set(_okay_pthread "${_okay_pth_dst}")
        endif()
    endif()
endif()

# Link the C/C++ runtime (libgcc, libstdc++, winpthread) statically so no MinGW
# runtime DLLs are needed, but switch back to -Bdynamic afterwards so SDL2 links
# against its import library — i.e. ship one SDL2.dll beside the exe.
# Configure with -DOKAY_STATIC_SDL=ON to fully static-link into a single .exe instead.
if(OKAY_STATIC_SDL)
    set(CMAKE_EXE_LINKER_FLAGS_INIT
        "-static -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive ${_okay_pthread} -Wl,--no-whole-archive")
else()
    set(CMAKE_EXE_LINKER_FLAGS_INIT
        "-static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive ${_okay_pthread} -Wl,--no-whole-archive,-Bdynamic")
endif()
