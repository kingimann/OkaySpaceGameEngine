#pragma once

#include "diag.hpp"
#include "files.hpp"

#include <string>
#include <vector>

namespace okaycc {

// Preprocess and parse a small C subset, then print the tree.
// The grammar is the relevant part of ISO/IEC 9899:2018 clauses 6.5 to 6.9.
std::string astOf(const std::string& source, Diagnostics& diags);
std::string astOfFile(FileStore& files, const std::string& path,
                      const std::vector<std::string>& includePaths, Diagnostics& diags);

// Parse and type-check the subset. Errors land in diags.
void checkSource(const std::string& source, Diagnostics& diags, const std::string& file = "t.c");

}  // namespace okaycc
