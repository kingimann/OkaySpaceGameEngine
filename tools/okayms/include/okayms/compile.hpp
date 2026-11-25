#pragma once

// A small C compiler. The language is the subset of ISO/IEC 9899 used by
// the programs in this tree. It writes COFF for the Windows x64 convention.
// It does not use tools/okaycc.

#include "okayms/coff.hpp"

#include <string>
#include <utility>
#include <vector>

namespace okayms {

struct CompileInput {
    std::string source;
    std::string name = "source.c";
    std::vector<std::string> include_paths;
    // Each entry is "NAME" or "NAME=value". NAME alone means 1.
    std::vector<std::string> defines;
    // /U NAME: a predefined macro (below) that is not defined. okay-ms stands in for the Microsoft x64 compiler, so it
    // predefines what that compiler documents for x64: _WIN32, _WIN64, _M_X64, _M_AMD64 (100), _MSC_VER (1916),
    // _MSVC_LANG (201703), _INTEGRAL_MAX_BITS (64), __cplusplus (201703, as with /Zc:__cplusplus),
    // and _CPPRTTI is not defined (no /GR yet). Defines given in `defines` replace a predefined value.
    std::vector<std::string> undefines;
    // 1 is /O1 (constant ifs and register allocation). 2 is /O2, which also
    // propagates constants and removes dead stores.
    int opt_level = 0;
    bool optimize = false;
    // /Z7: write CodeView line records into a .debug$S section. There is no
    // PDB, no symbol records and no type records.
    bool debug_lines = false;
};

struct CompileOutput {
    ObjectFile object;
    std::vector<std::string> exports;
};

Result<CompileOutput> compile_c(const CompileInput& input);

}  // namespace okayms
