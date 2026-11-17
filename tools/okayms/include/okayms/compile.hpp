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
    // /O1. Drops an if whose condition is a constant. /O2 does not set this.
    bool optimize = false;
};

struct CompileOutput {
    ObjectFile object;
    std::vector<std::string> exports;
};

Result<CompileOutput> compile_c(const CompileInput& input);

}  // namespace okayms
