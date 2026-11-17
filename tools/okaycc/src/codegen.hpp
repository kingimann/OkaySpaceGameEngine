#pragma once

#include "diag.hpp"
#include "ir.hpp"

#include <string>

namespace okaycc {

// AT&T x86-64 assembly for the System V AMD64 ABI. Address-taken values live
// in stack slots. Up to three others live in rbx, r12, and r13.
std::string emitAssembly(const Module& m);
std::string compileToAssembly(const std::string& source, Diagnostics& diags,
                               const std::string& file = "t.c");

}  // namespace okaycc
