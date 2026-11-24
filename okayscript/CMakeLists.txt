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

# The CLI must never pull in the engine (checked at configure time too, not only by the test).
get_target_property(_okayscript_cli_libs okayscript_cli LINK_LIBRARIES)
foreach(_l IN LISTS _okayscript_cli_libs)
    if(_l MATCHES "okay_engine|okay::engine")
        message(FATAL_ERROR "okayscript_cli must not link the engine (found ${_l})")
    endif()
endforeach()
