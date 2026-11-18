#include "coff.hpp"
#include "harness.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

static std::uint16_t r16(const std::vector<unsigned char>& b, std::size_t at) {
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}

static std::uint32_t r32(const std::vector<unsigned char>& b, std::size_t at) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(b[at + static_cast<std::size_t>(i)]) << (8 * i);
    return v;
}

int main() {
    okaycc::CoffObject obj;
    okaycc::CoffSection text;
    text.name = ".text";
    text.bytes = {0xE8, 0, 0, 0, 0, 0xC3};
    okaycc::CoffReloc rel;
    rel.offset = 1;
    rel.symbol = 1;
    rel.type = 4;
    text.relocs.push_back(rel);
    text.flags = 0x60000020u;
    obj.sections.push_back(std::move(text));

    okaycc::CoffSymbol def;
    def.name = "answer";
    def.section = 1;
    def.function = true;
    obj.symbols.push_back(def);
    okaycc::CoffSymbol undef;
    undef.name = "puts";
    undef.section = 0;
    undef.function = true;
    obj.symbols.push_back(undef);

    std::vector<unsigned char> bytes = okaycc::writeCoff(obj);
    bool ok = true;
    auto fail = [&](const char* msg) {
        std::fprintf(stderr, "FAIL coff %s\n", msg);
        ok = false;
    };
    if (bytes.size() < 20 || r16(bytes, 0) != 0x8664) fail("machine");
    if (r16(bytes, 2) != 1) fail("section count");
    if (r16(bytes, 16) != 0) fail("optional header");
    std::uint32_t symAt = r32(bytes, 8);
    std::uint32_t nsym = r32(bytes, 12);
    if (nsym != 2 || symAt + 36 > bytes.size()) fail("symbol table");
    std::uint32_t raw = r32(bytes, 20 + 20);
    std::uint32_t rawSize = r32(bytes, 20 + 16);
    if (rawSize != 6 || raw + 6 > bytes.size() || bytes[raw + 5] != 0xC3) fail("text bytes");
    std::uint32_t reloc = r32(bytes, 20 + 24);
    if (r16(bytes, 20 + 32) != 1 || reloc + 10 > bytes.size()) fail("reloc count");
    if (r32(bytes, reloc) != 1 || r32(bytes, reloc + 4) != 1 || r16(bytes, reloc + 8) != 4) fail("reloc");
    auto nameOf = [&](std::size_t sym) {
        std::size_t at = symAt + sym * 18;
        std::uint32_t off = r32(bytes, at + 4);
        std::uint32_t strAt = symAt + nsym * 18;
        std::string s;
        for (std::size_t i = strAt + off; i < bytes.size() && bytes[i]; ++i) s.push_back(static_cast<char>(bytes[i]));
        return s;
    };
    if (nameOf(0) != "answer" || nameOf(1) != "puts") fail("names");
    if (static_cast<std::int16_t>(r16(bytes, symAt + 12)) != 1) fail("defined section");
    if (r16(bytes, symAt + 18 + 12) != 0) fail("undefined section");
    if (ok) ++okaycc_test::gPass;
    else ++okaycc_test::gFail;
    std::printf("coff: %d passed, %d failed\n", okaycc_test::gPass, okaycc_test::gFail);
    return okaycc_test::gFail == 0 ? 0 : 1;
}
