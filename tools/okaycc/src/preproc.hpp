#pragma once

#include "diag.hpp"
#include "files.hpp"
#include "token.hpp"

#include <string>
#include <vector>

namespace okaycc {

// Preprocess one translation unit.
// Written from ISO/IEC 9899:2018 clause 6.10. See docs/platform/own_compiler_plan.md.
std::vector<Token> preprocessTokens(FileStore& files, const std::string& path,
                                    const std::vector<std::string>& includePaths, Diagnostics& diags);

// Tokens separated by one space, with newlines kept and directives removed.
std::string preprocess(FileStore& files, const std::string& path,
                       const std::vector<std::string>& includePaths, Diagnostics& diags);

}  // namespace okaycc
