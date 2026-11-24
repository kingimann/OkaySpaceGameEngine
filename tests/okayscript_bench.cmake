# The okayscript_bench test: counts the instructions of OkayScript hot loops (bench_okayscript) with valgrind's
# callgrind, counting only inside OkayScriptVM::Load (the script itself), and fails when a case gets more than
# 10% slower than its stored baseline in tests/okayscript_bench_baseline.txt ("case instructions" per line).
#   cmake -DVALGRIND=<valgrind> -DBENCH=<bench_okayscript> -DBASELINE=<file> -DOUT=<dir> -P okayscript_bench.cmake
# Without valgrind it prints "okayscript_bench skipped" and ctest counts it as skipped. Release builds only (the
# baseline is for optimised code).
if(NOT VALGRIND OR NOT EXISTS "${VALGRIND}")
    message("okayscript_bench skipped: valgrind not found")
    return()
endif()
file(STRINGS "${BASELINE}" lines REGEX "^[a-z]+ [0-9]+")
set(failed OFF)
foreach(line IN LISTS lines)
    string(REGEX MATCH "^([a-z]+) ([0-9]+)" _ "${line}")
    set(name "${CMAKE_MATCH_1}")
    set(base "${CMAKE_MATCH_2}")
    set(outfile "${OUT}/callgrind.${name}.out")
    execute_process(COMMAND "${VALGRIND}" --tool=callgrind "--callgrind-out-file=${outfile}"
                            "--toggle-collect=*OkayScriptVM::Load*" "${BENCH}" "${name}"
                    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT rc EQUAL 0)
        message("${name}: bench_okayscript failed (${rc})")
        set(failed ON)
        continue()
    endif()
    file(STRINGS "${outfile}" summary REGEX "^summary: [0-9]+")
    string(REGEX MATCH "[0-9]+" got "${summary}")
    math(EXPR limit "${base} + ${base} / 10")
    if(got GREATER limit)
        message("${name}: ${got} instructions, more than 10% over the baseline ${base} (limit ${limit})")
        set(failed ON)
    else()
        message("${name}: ${got} instructions (baseline ${base}, limit ${limit})")
    endif()
endforeach()
if(failed)
    message(FATAL_ERROR "okayscript_bench: a hot loop got slower")
endif()
