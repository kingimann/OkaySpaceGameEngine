#include "okay/Graphics/TtfFont.hpp"
#include "RobotoFont.h"     // the launcher's font: RobotoMedium_compressed_data_base85 (shared/)

#include <cstring>

namespace okay {
namespace {

// ---- Dear ImGui's compressed-font format: base85 text -> stb_compress stream -> TTF.
// Decompression from stb.h (public domain) by Sean Barrett, as used by Dear ImGui.
unsigned Decode85Byte(char c) { return c >= '\\' ? c - 36 : c - 35; }
void Decode85(const unsigned char* src, unsigned char* dst) {
    while (*src) {
        unsigned tmp = Decode85Byte(src[0]) + 85 * (Decode85Byte(src[1]) + 85 * (Decode85Byte(src[2]) +
                       85 * (Decode85Byte(src[3]) + 85 * Decode85Byte(src[4]))));
        dst[0] = (tmp >> 0) & 0xFF; dst[1] = (tmp >> 8) & 0xFF; dst[2] = (tmp >> 16) & 0xFF; dst[3] = (tmp >> 24) & 0xFF;
        src += 5; dst += 4;
    }
}
unsigned char *g_outEnd, *g_outBegin, *g_out;
const unsigned char* g_inBegin;
void Match(const unsigned char* data, unsigned len) {
    if (g_out + len > g_outEnd) { g_out += len; return; }
    if (data < g_outBegin) { g_out = g_outEnd + 1; return; }
    while (len--) *g_out++ = *data++;
}
void Lit(const unsigned char* data, unsigned len) {
    if (g_out + len > g_outEnd) { g_out += len; return; }
    if (data < g_inBegin) { g_out = g_outEnd + 1; return; }
    std::memcpy(g_out, data, len);
    g_out += len;
}
#define IN2(x) ((i[x] << 8) + i[(x) + 1])
#define IN3(x) ((i[x] << 16) + IN2((x) + 1))
#define IN4(x) ((i[x] << 24) + IN3((x) + 1))
const unsigned char* Token(const unsigned char* i) {
    if (*i >= 0x20) {
        if (*i >= 0x80)      Match(g_out - i[1] - 1, i[0] - 0x80 + 1), i += 2;
        else if (*i >= 0x40) Match(g_out - (IN2(0) - 0x4000 + 1), i[2] + 1), i += 3;
        else                 Lit(i + 1, i[0] - 0x20 + 1), i += 1 + (i[0] - 0x20 + 1);
    } else {
        if (*i >= 0x18)      Match(g_out - (IN3(0) - 0x180000 + 1), i[3] + 1), i += 4;
        else if (*i >= 0x10) Match(g_out - (IN3(0) - 0x100000 + 1), IN2(3) + 1), i += 5;
        else if (*i >= 0x08) Lit(i + 2, IN2(0) - 0x0800 + 1), i += 2 + (IN2(0) - 0x0800 + 1);
        else if (*i == 0x07) Lit(i + 3, IN2(1) + 1), i += 3 + (IN2(1) + 1);
        else if (*i == 0x06) Match(g_out - (IN3(1) + 1), i[4] + 1), i += 5;
        else if (*i == 0x04) Match(g_out - (IN3(1) + 1), IN2(4) + 1), i += 6;
    }
    return i;
}
unsigned Decompress(unsigned char* output, const unsigned char* i, unsigned outCap) {
    if (IN4(0) != 0x57bC0000 || IN4(4) != 0) return 0;
    const unsigned olen = (i[8] << 24) + (i[9] << 16) + (i[10] << 8) + i[11];
    if (olen > outCap) return 0;
    g_inBegin = i; g_outEnd = output + olen; g_outBegin = output;
    i += 16;
    g_out = output;
    for (;;) {
        const unsigned char* old = i;
        i = Token(i);
        if (i == old) return (*i == 0x05 && i[1] == 0xfa && g_out == output + olen) ? olen : 0;
        if (g_out > output + olen) return 0;
    }
}
#undef IN2
#undef IN3
#undef IN4

} // namespace

bool EmbeddedUIFontData(std::vector<unsigned char>& out) {
    static std::vector<unsigned char> cache;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const char* b85 = RobotoMedium_compressed_data_base85;
        const std::size_t b85len = std::strlen(b85);
        std::vector<unsigned char> comp(((b85len + 4) / 5) * 4 + 8);
        Decode85((const unsigned char*)b85, comp.data());
        const unsigned cap = (comp[8] << 24) + (comp[9] << 16) + (comp[10] << 8) + comp[11];
        std::vector<unsigned char> ttf((std::size_t)cap + 16);
        const unsigned n = Decompress(ttf.data(), comp.data(), cap);
        if (n) { ttf.resize(n); cache.swap(ttf); }
    }
    out = cache;
    return !out.empty();
}

} // namespace okay
