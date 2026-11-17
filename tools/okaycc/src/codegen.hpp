#pragma once

#include "diag.hpp"
#include "ir.hpp"

#include <string>

namespace okaycc {

// AT&T x86-64 assembly for the System V AMD64 ABI. Every value lives in a
// stack slot; the only registers used as scratch are caller-saved.
std::string emitAssembly(const Module& m);
std::string compileToAssembly(const std::string& source, Diagnostics& diags);

}  // namespace okaycc
