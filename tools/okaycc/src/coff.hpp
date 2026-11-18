#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace okaycc {

// A relocatable x86-64 COFF object, written from the Microsoft PE/COFF spec.
// This is not wired to the code generator yet. It shares no code with okayms.
struct CoffReloc {
    std::uint32_t offset = 0;
    std::uint32_t symbol = 0;
    std::uint16_t type = 4;  // IMAGE_REL_AMD64_REL32
};

struct CoffSection {
    std::string name;
    std::vector<unsigned char> bytes;
    std::vector<CoffReloc> relocs;
    std::uint32_t flags = 0;
};

struct CoffSymbol {
    std::string name;
    std::int16_t section = 0;  // 1-based, 0 when the name is undefined
    std::uint32_t value = 0;
    bool function = false;
    bool external = true;
};

struct CoffObject {
    std::vector<CoffSection> sections;
    std::vector<CoffSymbol> symbols;
};

std::vector<unsigned char> writeCoff(const CoffObject& obj);

}  // namespace okaycc
