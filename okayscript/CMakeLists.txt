# OkayScript: our own scripting language as a library that depends only on the C++17 standard library, so programs
# other than the engine can use it. It holds the whole language: lexer, parser, syntax tree, and the interpreter core
# (a template over the value type, Runtime.hpp). The engine's OkayScriptVM runs this same core on its own value
# (engine/src/Scripting/OkayScriptVM.cpp) and adds its builtins and scene names. Nothing in this folder may include an
# engine header: only include/ is on the include path, and the okayscript_no_engine test checks the sources and the
# CLI's link line (docs/scripting/scripting.md "Standalone OkayScript").
add_library(okayscript_lang STATIC
    src/Lexer.cpp
    src/Value.cpp
    src/Ast.cpp
    src/Parser.cpp
    src/Instantiate.cpp
    src/Stdlib.cpp
)
target_include_directories(okayscript_lang PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(okayscript_lang PUBLIC cxx_std_17)
add_library(okayscript::lang ALIAS okayscript_lang)

# The command-line runner: `okayscript file.okay [args]` / `okayscript -e "code"`. Links only okayscript_lang.
add_executable(okayscript_cli cli/main.cpp)
target_link_libraries(okayscript_cli PRIVATE okayscript_lang)
set_target_properties(okayscript_cli PROPERTIES OUTPUT_NAME okayscript)
if(MINGW)
    # One file with no MinGW runtime DLLs next to it.
    target_link_options(okayscript_cli PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Neither the library nor the CLI may pull in the engine, directly or through anything they link. Checked once every
# target exists (end of the top-level configure): the whole link closure of both, written to
# okayscript_link_closure.txt for the okayscript_no_engine test.
function(okayscript_link_closure target out)
    set(seen "")
    set(todo ${target})
    while(todo)
        list(POP_FRONT todo t)
        if(t IN_LIST seen)
            continue()
        endif()
        list(APPEND seen ${t})
        if(TARGET ${t})
            get_target_property(aliased ${t} ALIASED_TARGET)
            if(aliased)
                list(APPEND todo ${aliased})
            endif()
            foreach(prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
                get_target_property(libs ${t} ${prop})
                if(libs)
                    foreach(l IN LISTS libs)
                        string(REGEX REPLACE "^\\$<LINK_ONLY:(.*)>$" "\\1" l "${l}")
                        list(APPEND todo ${l})
                    endforeach()
                endif()
            endforeach()
        endif()
    endwhile()
    set(${out} "${seen}" PARENT_SCOPE)
endfunction()
function(okayscript_check_no_engine)
    set(report "")
    foreach(t okayscript_lang okayscript_cli)
        okayscript_link_closure(${t} closure)
        foreach(l IN LISTS closure)
            if(l MATCHES "okay_engine|okay::engine|okay_physcore|okay::physcore|okay_crypto|okay::crypto|okay_platform|okayui|okay::ui")
                message(FATAL_ERROR "${t} must not link the engine: its link closure has ${l} (${closure})")
            endif()
        endforeach()
        string(APPEND report "${t}: ${closure}\n")
    endforeach()
    file(WRITE "${CMAKE_BINARY_DIR}/okayscript_link_closure.txt" "${report}")
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL okayscript_check_no_engine)
