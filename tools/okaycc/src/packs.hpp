#pragma once
// Variadic function templates, expanded by pack length on the token stream before parsing (packs.cpp).
#include "token.hpp"

#include <vector>

namespace okaycc {
namespace compiler {
std::vector<Token> expandPacks(const std::vector<Token>& toks);
}  // namespace compiler
}  // namespace okaycc
