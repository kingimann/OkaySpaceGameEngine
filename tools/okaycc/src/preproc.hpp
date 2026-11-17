#pragma once

#include "diag.hpp"
#include "files.hpp"

#include <string>
#include <vector>

namespace okaycc {

// Preprocess one translation unit. The returned text is tokens separated by
// one space, with newlines kept and directives removed.
// Written from ISO/IEC 9899:2018 clause 6.10. See docs/own_compiler_plan.md.
std::string preprocess(FileStore& files, const std::string& path,
                       const std::vector<std::string>& includePaths, Diagnostics& diags);

}  // namespace okaycc
