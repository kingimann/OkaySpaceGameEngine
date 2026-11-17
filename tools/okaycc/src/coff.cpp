#include "coff.hpp"

namespace okaycc {
namespace {

void u16(std::vector<unsigned char>& b, std::uint16_t v) {
    b.push_back(static_cast<unsigned char>(v & 0xffu));
    b.push_back(static_cast<unsigned char>((v >> 8) & 0xffu));
}

void u32(std::vector<unsigned char>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xffu));
}

void patch32(std::vector<unsigned char>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + static_cast<std::size_t>(i)] = static_cast<unsigned char>((v >> (8 * i)) & 0xffu);
}

}  // namespace

std::vector<unsigned char> writeCoff(const CoffObject& obj) {
    const std::size_t nsec = obj.sections.size();
    std::vector<unsigned char> out(20 + nsec * 40, 0);
    out[0] = 0x64;
    out[1] = 0x86;
    out[2] = static_cast<unsigned char>(nsec & 0xffu);
    out[3] = static_cast<unsigned char>((nsec >> 8) & 0xffu);

    std::size_t cursor = out.size();
    std::vector<std::size_t> rawAt(nsec), relocAt(nsec);
    for (std::size_t i = 0; i < nsec; ++i) {
        rawAt[i] = cursor;
        const auto& sec = obj.sections[i];
        out.insert(out.end(), sec.bytes.begin(), sec.bytes.end());
        while (out.size() % 4) out.push_back(0);
        cursor = out.size();
        relocAt[i] = sec.relocs.empty() ? 0 : cursor;
        for (const CoffReloc& r : sec.relocs) {
            u32(out, r.offset);
            u32(out, r.symbol);
            u16(out, r.type);
        }
        cursor = out.size();
    }

    for (std::size_t i = 0; i < nsec; ++i) {
        const CoffSection& sec = obj.sections[i];
        std::size_t h = 20 + i * 40;
        std::string name = sec.name.size() > 8 ? sec.name.substr(0, 8) : sec.name;
        for (std::size_t k = 0; k < name.size() && k < 8; ++k) out[h + k] = static_cast<unsigned char>(name[k]);
        patch32(out, h + 16, static_cast<std::uint32_t>(sec.bytes.size()));
        patch32(out, h + 20, sec.bytes.empty() ? 0 : static_cast<std::uint32_t>(rawAt[i]));
        patch32(out, h + 24, sec.relocs.empty() ? 0 : static_cast<std::uint32_t>(relocAt[i]));
        out[h + 32] = static_cast<unsigned char>(sec.relocs.size() & 0xffu);
        out[h + 33] = static_cast<unsigned char>((sec.relocs.size() >> 8) & 0xffu);
        patch32(out, h + 36, sec.flags);
    }

    std::size_t symAt = out.size();
    patch32(out, 8, static_cast<std::uint32_t>(symAt));
    patch32(out, 12, static_cast<std::uint32_t>(obj.symbols.size()));

    std::string strings;
    for (const CoffSymbol& s : obj.symbols) {
        std::uint32_t off = static_cast<std::uint32_t>(4 + strings.size());
        strings += s.name;
        strings.push_back('\0');
        for (int i = 0; i < 4; ++i) out.push_back(0);
        u32(out, off);
        u32(out, s.value);
        u16(out, static_cast<std::uint16_t>(s.section));
        u16(out, s.function ? 0x20 : 0);
        out.push_back(s.external ? 2 : 3);
        out.push_back(0);
    }
    u32(out, static_cast<std::uint32_t>(4 + strings.size()));
    out.insert(out.end(), strings.begin(), strings.end());
    return out;
}

}  // namespace okaycc
