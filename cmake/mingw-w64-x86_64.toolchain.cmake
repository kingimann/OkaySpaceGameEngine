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

# Link the C/C++ runtime (libgcc, libstdc++, winpthread) statically so no MinGW
# runtime DLLs are needed, but switch back to -Bdynamic afterwards so SDL2 links
# against its import library — i.e. ship one SDL2.dll beside the exe (Unity-style).
# Configure with -DOKAY_STATIC_SDL=ON to fully static-link into a single .exe instead.
if(OKAY_STATIC_SDL)
    set(CMAKE_EXE_LINKER_FLAGS_INIT
        "-static -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")
else()
    set(CMAKE_EXE_LINKER_FLAGS_INIT
        "-static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic")
endif()
