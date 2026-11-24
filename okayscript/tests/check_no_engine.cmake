# The OkayScript language stays independent of the engine:
#   1. no file under okayscript/ includes an engine header ("okay/..." or <okay/...>, Okay.hpp);
#   2. the okayscript CLI's link libraries (passed in as LIBS) do not name the engine.
#   cmake -DSRC=<okayscript folder> -DLIBS=<link libraries of okayscript_cli> -P check_no_engine.cmake
file(GLOB_RECURSE files "${SRC}/*.hpp" "${SRC}/*.cpp" "${SRC}/*.h")
list(LENGTH files n)
if(n LESS 10)
    message(FATAL_ERROR "found only ${n} source files under ${SRC}: wrong folder?")
endif()
set(bad "")
foreach(f IN LISTS files)
    file(STRINGS "${f}" lines REGEX "^[ \t]*#[ \t]*include[ \t]*[\"<](okay/|Okay\\.hpp)")
    if(lines)
        list(APPEND bad "${f}: ${lines}")
    endif()
endforeach()
if(bad)
    string(REPLACE ";" "\n" bad "${bad}")
    message(FATAL_ERROR "engine headers included by the OkayScript language:\n${bad}")
endif()
if(LIBS MATCHES "okay_engine|okay::engine|okay_physcore|okay_crypto")
    message(FATAL_ERROR "okayscript_cli links an engine library: ${LIBS}")
endif()
message("okayscript: ${n} files, no engine includes; CLI links only: ${LIBS}")
