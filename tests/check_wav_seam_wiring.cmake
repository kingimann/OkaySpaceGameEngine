# Run with: cmake -DSOURCE_ROOT=<repo root> -P check_wav_seam_wiring.cmake
#
# Our own WAV decoder (okay::wav) is the only WAV reader. SDL_LoadWAV / SDL_FreeWAV and the old OKAY_OWN_WAV switch
# (okay::wav::OwnWavEnabled) were deleted (rule 17: the replacement is the path and the old code goes in the same change),
# and so was the older streaming header parser (ParseWav, ConvertWavFrames, WavInfo): AudioStream uses okay::wav::WavStream.
# This fails and names the file if any of them comes back anywhere in shipped code (launcher/, shared/, editor/,
# player/, engine/, tools/, okayui/, platform/ and the top CMakeLists.txt). Comments are not counted. It also checks
# that the launcher's seam, shared/WavPlayback.hpp, still loads through okay::wav::LoadForPlayback.
cmake_minimum_required(VERSION 3.16)
if(NOT IS_DIRECTORY "${SOURCE_ROOT}")
    message(FATAL_ERROR "check_wav_seam_wiring: SOURCE_ROOT (${SOURCE_ROOT}) is not a directory")
endif()
set(_files "${SOURCE_ROOT}/CMakeLists.txt")
foreach(_d launcher shared editor player engine tools okayui platform)
    if(IS_DIRECTORY "${SOURCE_ROOT}/${_d}")
        file(GLOB_RECURSE _more "${SOURCE_ROOT}/${_d}/*.cpp" "${SOURCE_ROOT}/${_d}/*.hpp" "${SOURCE_ROOT}/${_d}/*.h"
                                "${SOURCE_ROOT}/${_d}/*.c" "${SOURCE_ROOT}/${_d}/*.mm" "${SOURCE_ROOT}/${_d}/CMakeLists.txt")
        list(APPEND _files ${_more})
    endif()
endforeach()
set(_bad "")
foreach(_f IN LISTS _files)
    if(NOT EXISTS "${_f}" OR _f MATCHES "/(third_party|_deps|build[^/]*)/")
        continue()
    endif()
    file(READ "${_f}" _code)
    if(_f MATCHES "CMakeLists\\.txt$")
        string(REGEX REPLACE "#[^\n]*" "" _code "${_code}")      # CMake comments
    else()
        string(REGEX REPLACE "//[^\n]*" "" _code "${_code}")     # C++ line comments
    endif()
    if(_code MATCHES "SDL_LoadWAV|SDL_FreeWAV|OwnWavEnabled|OKAY_OWN_WAV|ParseWav|ConvertWavFrames|WavInfo")
        list(APPEND _bad "${_f}")
    endif()
endforeach()
if(_bad)
    string(REPLACE ";" "\n  " _list "${_bad}")
    message(FATAL_ERROR "check_wav_seam_wiring: SDL_LoadWAV / SDL_FreeWAV / the old OKAY_OWN_WAV switch / the old streaming WAV parser found in:\n  ${_list}\nUse okay::wav (LoadForPlayback, DecodeWav, WavStream); there is no other WAV reader.")
endif()
file(READ "${SOURCE_ROOT}/shared/WavPlayback.hpp" _seam)
if(NOT _seam MATCHES "LoadForPlayback")
    message(FATAL_ERROR "check_wav_seam_wiring: shared/WavPlayback.hpp no longer loads through okay::wav::LoadForPlayback")
endif()
message(STATUS "check_wav_seam_wiring: ok")
