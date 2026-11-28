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

namespace {
// The thread_local text added to a file that defines main or entry and names thread_local (Round 35; ISO/IEC 14882
// [basic.stc.thread], [basic.start.term]; PE/COFF "Grouped Sections" and "TLS Callback Functions"). The four markers
// get their sections in finish_program (.CRT$XDA and .CRT$XDZ around the thread_local constructors of every file,
// .okaytd$A and .okaytd$Z around their destructors); the linker puts each file's entries between them and makes
// __okay_tls_callback the image's TLS callback, which the Windows loader calls for main's thread before main (reason 1)
// and for every other thread when it starts (2) and ends (3). main calls __okay_run_td at its end (main's thread's
// objects). A program linked with okay-ms's runtime has the same in runtime/startup.c (__okay_tls_callback_rt), for
// files that have thread_local objects when the file with main has none. Our own design, written from those documents;
// the walkers skip null entries (a marker, or padding).
const char* const kThreadLocalText = R"OKMS(
typedef void (*__okay_pfv)(void);
static __okay_pfv __okay_xd_a = 0;
static __okay_pfv __okay_xd_z = 0;
static __okay_pfv __okay_td_a = 0;
static __okay_pfv __okay_td_z = 0;
static void __okay_run_tls_table(__okay_pfv *from, __okay_pfv *to, int backward) {
    if (backward) {
        while (to > from + 1) {
            --to;
            if (*to) (*to)();
        }
    } else {
        for (__okay_pfv *p = from + 1; p < to; ++p)
            if (*p) (*p)();
    }
}
static void __okay_run_td(void) { __okay_run_tls_table(&__okay_td_a, &__okay_td_z, 1); }
extern "C" void __okay_tls_callback(void *module, unsigned long reason, void *reserved) {
    if (reason == 1 || reason == 2) __okay_run_tls_table(&__okay_xd_a, &__okay_xd_z, 0);
    else if (reason == 3) __okay_run_td();
}
)OKMS";

// Whether the tokens define main or entry at namespace scope: `int main(` or `void entry(` at brace depth 0, with a body.
bool defines_start(const std::vector<Token>& tokens) {
    int depth = 0;
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        const Token& t = tokens[i];
        if (t.kind == TLBrace) ++depth;
        else if (t.kind == TRBrace) --depth;
        if (depth != 0 || t.kind != TIdent || (t.text != "main" && t.text != "entry") || i == 0) continue;
        if (tokens[i + 1].kind != TLParen || (tokens[i - 1].kind != TInt && tokens[i - 1].kind != TVoid)) continue;
        int paren = 0;
        for (size_t k = i + 1; k < tokens.size(); ++k) {
            if (tokens[k].kind == TLParen) ++paren;
            else if (tokens[k].kind == TRParen && --paren == 0) return k + 1 < tokens.size() && tokens[k + 1].kind == TLBrace;
        }
    }
    return false;
}
}  // namespace

Result<CompileOutput> compile_c(const CompileInput& input) {
    Preprocessor pp;
    pp.includes = input.include_paths;
    // The predefined macros of the compiler okay-ms stands in for (Microsoft's "Predefined macros" page, x64): every
    // Windows target defines _WIN32, a 64-bit one _WIN64, an x64 one _M_X64 and _M_AMD64 as 100. _MSC_VER 1916 is the
    // last Visual Studio 2017 compiler, the first with the C++17 language complete; the engine tests only whether it is
    // defined (then it uses __forceinline, <intrin.h> with _umul128/_BitScanForward64/_BitScanReverse64, and
    // #pragma comment(lib), which okay-ms all handle). /U NAME leaves one out.
    {
        const std::pair<const char*, const char*> predefined[] = {
            {"_WIN32", "1"}, {"_WIN64", "1"}, {"_M_X64", "100"}, {"_M_AMD64", "100"}, {"_MSC_VER", "1916"},
            {"_MSC_FULL_VER", "191627045"}, {"_MSVC_LANG", "201703L"}, {"_INTEGRAL_MAX_BITS", "64"},
            {"__cplusplus", "201703L"}};
        for (const auto& p : predefined) {
            if (std::find(input.undefines.begin(), input.undefines.end(), p.first) != input.undefines.end()) continue;
            Macro macro;
            macro.body = p.second;
            pp.macros[p.first] = macro;
        }
    }
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
    bool names_thread_local = false;
    for (const Token& t : tokens)
        if (t.kind == TIdent && t.text == "thread_local") names_thread_local = true;
    if (names_thread_local && defines_start(tokens)) {
        std::string startup_error;
        std::vector<Token> startup = lex(kThreadLocalText, &startup_error);
        if (!startup_error.empty()) return Result<CompileOutput>::fail("start-up text: " + startup_error);
        for (Token& t : startup) t.line = 0;
        if (!startup.empty() && startup.back().kind == TEnd) startup.pop_back();
        size_t end = tokens.size();
        if (end > 0 && tokens[end - 1].kind == TEnd) --end;
        tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(end), startup.begin(), startup.end());
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
