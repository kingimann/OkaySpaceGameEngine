#pragma once

#include "diag.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace okaycc {

// A static ELF64 executable for x86-64. The bytes are encoded here from the
// Intel 64 instruction format. The system assembler and linker are not used.
std::vector<unsigned char> compileToElf(const std::string& source, Diagnostics& diags,
                                        const std::string& file = "t.c",
                                        const std::vector<std::string>& includes = {});

// A relocatable ELF64 object. Undefined names stay unresolved.
// Keep records for unwinding in every function (for a file that has no try or throw itself, but
// is linked with code that does). Off by default.
void setForceExceptions(bool on);

std::vector<unsigned char> compileToObject(const std::string& source, Diagnostics& diags,
                                           const std::string& file = "t.c",
                                           const std::vector<std::string>& includes = {});

}  // namespace okaycc
