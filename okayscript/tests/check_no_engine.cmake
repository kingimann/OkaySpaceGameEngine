# The OkayScript language stays independent of the engine:
#   1. no file under okayscript/ includes an engine header: okay/..., Okay.hpp (the engine umbrella), an engine/editor/okayui/shared/
#      platform path, or any path that climbs out with ../ ;
#   2. the link closure of okayscript_lang and of the okayscript CLI (CLOSURE: the file CMake wrote at configure time,
#      okayscript_link_closure.txt) holds no engine library.
#   cmake -DSRC=<okayscript folder> -DCLOSURE=<okayscript_link_closure.txt> -P check_no_engine.cmake
file(GLOB_RECURSE files "${SRC}/*.hpp" "${SRC}/*.cpp" "${SRC}/*.h" "${SRC}/*.ipp" "${SRC}/*.inl" "${SRC}/*.cc")
list(LENGTH files n)
if(n LESS 10)
    message(FATAL_ERROR "found only ${n} source files under ${SRC}: wrong folder?")
endif()
set(bad "")
foreach(f IN LISTS files)
    file(STRINGS "${f}" lines REGEX "^[ \t]*#[ \t]*include[ \t]*[\"<]([^\">]*/)?(okay/|Okay\\.hpp|engine/|editor/|okayui/|shared/|platform/|\\.\\./)")
    if(lines)
        list(APPEND bad "${f}: ${lines}")
    endif()
endforeach()
if(bad)
    string(REPLACE ";" "\n" bad "${bad}")
    message(FATAL_ERROR "engine headers included by the OkayScript language:\n${bad}")
endif()
if(NOT EXISTS "${CLOSURE}")
    message(FATAL_ERROR "no link closure file ${CLOSURE} (configure did not write it)")
endif()
file(READ "${CLOSURE}" closure)
if(closure MATCHES "okay_engine|okay::engine|okay_physcore|okay_crypto|okay_platform|okayui")
    message(FATAL_ERROR "the OkayScript language links an engine library:\n${closure}")
endif()
if(NOT closure MATCHES "okayscript_lang:" OR NOT closure MATCHES "okayscript_cli:")
    message(FATAL_ERROR "the link closure file does not list okayscript_lang and okayscript_cli:\n${closure}")
endif()
message("okayscript: ${n} files, no engine includes; link closures:\n${closure}")
