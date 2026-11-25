#include "okayms/compile.hpp"
#include "okayms/unwind.hpp"

// The compiler's parts (docs/engine/modularization_plan.md, batch T1): this file keeps compile_c, which runs them.
#include "compile_internal.hpp"
#include "parser.hpp"
#include "preprocess.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>

namespace okayms {

using namespace compiler;

Result<CompileOutput> compile_c(const CompileInput& input) {
    Preprocessor pp;
    pp.includes = input.include_paths;
    for (const std::string& def : input.defines) {
        const size_t eq = def.find('=');
        Macro macro;
        if (eq == std::string::npos) macro.body = "1";
        else macro.body = def.substr(eq + 1);
        if (eq == std::string::npos) pp.macros[def] = macro;
        else pp.macros[def.substr(0, eq)] = macro;
    }
    const std::string stripped = strip_comments(input.source);
    pp.files.push_back(input.name);
    // A quoted include in the main file is looked up next to that file first ([cpp.include]).
    std::string main_dir;
    {
        const size_t slash = input.name.find_last_of("/\\");
        if (slash != std::string::npos) main_dir = slash == 0 ? std::string("/") : input.name.substr(0, slash);
        else if (!input.name.empty()) main_dir = ".";  // okay-cl /c file.cpp: the file is in the current directory
    }
    const std::string expanded = pp.run(stripped, main_dir);
    if (!pp.error.empty()) return Result<CompileOutput>::fail(pp.error);
    std::string lex_error;
    std::vector<Token> tokens = lex(expanded, &lex_error);
    // "file:line: " for a line of the preprocessed text, from the origin table.
    const auto where = [&pp](int line) {
        if (line <= 0 || static_cast<size_t>(line) > pp.origin.size()) return std::string();
        const auto& o = pp.origin[static_cast<size_t>(line) - 1];
        const std::string name = o.first >= 0 && static_cast<size_t>(o.first) < pp.files.size() ? pp.files[static_cast<size_t>(o.first)] : "?";
        return name + ":" + std::to_string(o.second) + ": ";
    };
    if (!lex_error.empty()) {
        const size_t at_line = lex_error.rfind("at line ");
        const int line = at_line == std::string::npos ? 0 : std::atoi(lex_error.c_str() + at_line + 8);
        return Result<CompileOutput>::fail(where(line) + lex_error);
    }
    Parser parser{tokens, 0, {}, {}, {}, {}, {}, {}, {}, {}, nullptr, {}, {}};
    {
        // bool is a one-byte unsigned type whose stored value is 0 or 1 ([basic.fundamental]).
        Type boolean;
        boolean.base = 2;
        boolean.is_unsigned = true;
        boolean.is_bool = true;
        parser.typedefs["bool"] = boolean;
        // __int64 is a 64-bit signed integer, __int32 an int (the Microsoft spellings).
        Type wide64;
        wide64.base = 1;
        wide64.wide = true;
        parser.typedefs["__int64"] = wide64;
        Type int32;
        int32.base = 1;
        parser.typedefs["__int32"] = int32;
        // char16_t and char32_t ([basic.fundamental] 9) hold UTF-16 and UTF-32 code units: here an unsigned 16-bit and
        // an unsigned 32-bit integer. wchar_t is 16 bits on Windows (the Microsoft x64 ABI). Own simplification: they are
        // not types of their own for overloading, and their decorated names are those of unsigned short and unsigned
        // int (MSVC writes _S, _U and _W).
        Type u16;
        u16.base = 4;
        u16.is_unsigned = true;
        parser.typedefs["char16_t"] = u16;
        parser.typedefs["wchar_t"] = u16;
        Type u32;
        u32.base = 1;
        u32.is_unsigned = true;
        parser.typedefs["char32_t"] = u32;
    }
    if (!parser.parse()) {
        const size_t pos = std::min(parser.at, parser.tokens.size() - 1);
        return Result<CompileOutput>::fail(where(parser.tokens[pos].line) + (parser.error.empty() ? "parse failed" : parser.error));
    }
    LineSource line_source;
    line_source.enabled = input.debug_lines;
    line_source.origin = &pp.origin;
    line_source.files = &pp.files;
    CompileOutput output;
    std::string error;
    if (!emit_functions(parser.funcs, parser.structs, parser.globals, parser.func_types, input.optimize, input.opt_level,
                        line_source, &error, &output))
        return Result<CompileOutput>::fail(error);
    return Result<CompileOutput>::success(std::move(output));
}

}  // namespace okayms
