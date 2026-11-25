# Run with: cmake -DSOURCE_ROOT=<repo root> -P check_no_curl.cmake
#
# No shipped program (engine, launcher, editor, player, server, shared code, platform ports) may start the external
# `curl` program. Every HTTP call goes through the seam in engine/include/okay/Platform/NativeFetch.hpp, which uses only
# our own code: WinHTTP on Windows, our own HTTP/1.1 client (okay::ownhttp) elsewhere. This check fails and names the
# file when the source of a shipped program
#   - has the "curl" program name as a string (narrow or wide: "curl", L"curl", "curl -...", curl.exe; lower case, so a
#     "Curl" scene key is not counted), or
#   - calls or includes the old curl fallback (RunCurlProcess, CurlFallback.hpp), or
#   - starts another outside downloader instead (the old last resorts: "wget", PowerShell's Invoke-WebRequest /
#     Invoke-RestMethod, bitsadmin, URLDownloadToFile).
# Comments (// at the start of a line or after a space) are not counted. Build tooling (scripts/*.sh, CI) is not shipped
# and not checked here.
cmake_minimum_required(VERSION 3.16)
if(NOT IS_DIRECTORY "${SOURCE_ROOT}")
    message(FATAL_ERROR "check_no_curl: SOURCE_ROOT (${SOURCE_ROOT}) is not a directory")
endif()
set(_bad "")
set(_globs "")
foreach(_d IN ITEMS engine launcher editor player server shared platform)
    foreach(_ext IN ITEMS cpp hpp h c inl mm)
        list(APPEND _globs "${SOURCE_ROOT}/${_d}/*.${_ext}")
    endforeach()
endforeach()
file(GLOB_RECURSE _files ${_globs})
list(FILTER _files EXCLUDE REGEX "/(third_party|_deps|build[^/]*)/")
foreach(_f IN LISTS _files)
    file(READ "${_f}" _text)
    # drop // comments (not the // inside "https://")
    string(REGEX REPLACE "(^|\n|[ \t;{}])//[^\n]*" "\\1" _code "${_text}")
    set(_why "")
    if(_code MATCHES "\"curl\"")
        set(_why "a \"curl\" program name")
    elseif(_code MATCHES "\"curl -")
        set(_why "a curl command line")
    elseif(_code MATCHES "[Cc][Uu][Rr][Ll]\\.[Ee][Xx][Ee]")
        set(_why "curl.exe")
    elseif(_code MATCHES "RunCurlProcess|CurlFallback\\.hpp")
        set(_why "the old curl fallback")
    elseif(_code MATCHES "\"wget\"|\"wget -|Invoke-WebRequest|Invoke-RestMethod|[Bb]itsadmin|URLDownloadToFile")
        set(_why "an outside downloader")
    endif()
    if(_why)
        file(RELATIVE_PATH _rel "${SOURCE_ROOT}" "${_f}")
        list(APPEND _bad "${_rel} (${_why})")
    endif()
endforeach()
if(_bad)
    string(REPLACE ";" "\n  " _list "${_bad}")
    message(FATAL_ERROR "check_no_curl: shipped code still starts curl or another outside downloader:\n  ${_list}\n"
        "Use okay::http::FetchToFile / HeadDump / FetchPrefix / Send (NativeFetch.hpp): WinHTTP on Windows, our own client elsewhere.")
endif()
message(STATUS "check_no_curl: ok")
