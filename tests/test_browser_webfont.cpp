// Browser (page engine): web font files (engine/include/okay/Browser/WebFont.hpp). A WOFF 1.0 file is built here by hand from
// the W3C "WOFF File Format 1.0" layout (header, table directory, one zlib-compressed and one stored table), decoded, and
// compared byte for byte with the sfnt it stands for (offset table, table records, 4-byte padding as the OpenType
// specification lays them out). Then broken and hostile files, WOFF2 (refused: not read yet), and @font-face picking a WOFF
// file only when there is no TrueType / OpenType one.
#include "test_framework.hpp"
#include "okay/Browser/Css.hpp"
#include "okay/Browser/WebFont.hpp"
#include "okay/Core/Inflate.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace okay::browser;
using Bytes = std::vector<std::uint8_t>;

static void Put32(Bytes& b, std::uint32_t v) { b.push_back((std::uint8_t)(v >> 24)); b.push_back((std::uint8_t)(v >> 16)); b.push_back((std::uint8_t)(v >> 8)); b.push_back((std::uint8_t)v); }
static void Put16(Bytes& b, std::uint16_t v) { b.push_back((std::uint8_t)(v >> 8)); b.push_back((std::uint8_t)v); }
static void Set32(Bytes& b, std::size_t at, std::uint32_t v) { b[at] = (std::uint8_t)(v >> 24); b[at + 1] = (std::uint8_t)(v >> 16); b[at + 2] = (std::uint8_t)(v >> 8); b[at + 3] = (std::uint8_t)v; }

// A zlib stream (RFC 1950) of `n` copies of byte `c` (c < 144), one fixed-Huffman block (RFC 1951 3.2.6): the byte as a literal,
// then length-10 copies at distance 1 and literals for the rest.
static Bytes ZlibRun(std::uint8_t c, std::size_t n) {
    Bytes out{0x78, 0x01};
    std::uint32_t acc = 0;
    int bits = 0;
    auto put = [&](std::uint32_t v, int len) { acc |= v << bits; bits += len; while (bits >= 8) { out.push_back((std::uint8_t)acc); acc >>= 8; bits -= 8; } };
    auto code = [&](std::uint32_t v, int len) { std::uint32_t r = 0; for (int i = 0; i < len; ++i) r |= ((v >> i) & 1u) << (len - 1 - i); put(r, len); };
    put(1, 1); put(1, 2);                    // BFINAL, BTYPE = 01 (fixed)
    code(0x30u + c, 8);                      // the literal
    std::size_t done = 1;
    while (done + 10 <= n) { code(264 - 256, 7); code(0, 5); done += 10; }   // length 10 (symbol 264), distance 1 (code 0)
    while (done < n) { code(0x30u + c, 8); ++done; }
    code(0, 7);                              // end of block (256)
    if (bits > 0) out.push_back((std::uint8_t)acc);
    std::string plain(n, (char)c);
    const std::uint32_t ad = okay::inflate::detail::Adler32(plain);
    Put32(out, ad);
    return out;
}

struct Table { std::uint32_t tag; Bytes data; bool compress; };

// The sfnt the tables make (what the decoder must give back), and the WOFF file for them.
static Bytes Sfnt(const std::vector<Table>& tables) {
    Bytes b;
    Put32(b, 0x00010000u);
    Put16(b, (std::uint16_t)tables.size());
    std::uint16_t p2 = 1, lg = 0;
    while (p2 * 2u <= tables.size()) { p2 = (std::uint16_t)(p2 * 2); ++lg; }
    Put16(b, (std::uint16_t)(p2 * 16)); Put16(b, lg); Put16(b, (std::uint16_t)(tables.size() * 16 - p2 * 16));
    std::uint32_t at = 12 + 16 * (std::uint32_t)tables.size();
    for (const Table& t : tables) { Put32(b, t.tag); Put32(b, 0x12345678u); Put32(b, at); Put32(b, (std::uint32_t)t.data.size()); at += ((std::uint32_t)t.data.size() + 3u) & ~3u; }
    for (const Table& t : tables) { b.insert(b.end(), t.data.begin(), t.data.end()); while (b.size() % 4) b.push_back(0); }
    return b;
}
static Bytes Woff(const std::vector<Table>& tables) {
    Bytes b;
    Put32(b, 0x774F4646u); Put32(b, 0x00010000u); Put32(b, 0); Put16(b, (std::uint16_t)tables.size()); Put16(b, 0);
    Put32(b, (std::uint32_t)Sfnt(tables).size());
    Put16(b, 1); Put16(b, 0);
    for (int k = 0; k < 5; ++k) Put32(b, 0);   // no metadata, no private data
    const std::size_t dir = b.size();
    b.resize(b.size() + 20 * tables.size());
    for (std::size_t i = 0; i < tables.size(); ++i) {
        const Table& t = tables[i];
        const Bytes data = t.compress ? ZlibRun(t.data[0], t.data.size()) : t.data;
        while (b.size() % 4) b.push_back(0);
        const std::size_t off = b.size();
        b.insert(b.end(), data.begin(), data.end());
        Set32(b, dir + 20 * i, t.tag); Set32(b, dir + 20 * i + 4, (std::uint32_t)off); Set32(b, dir + 20 * i + 8, (std::uint32_t)data.size());
        Set32(b, dir + 20 * i + 12, (std::uint32_t)t.data.size()); Set32(b, dir + 20 * i + 16, 0x12345678u);
    }
    while (b.size() % 4) b.push_back(0);
    Set32(b, 8, (std::uint32_t)b.size());
    return b;
}

int main() {
    RUN_SUITE("browser web fonts: WOFF 1.0");
    const std::vector<Table> tables = {{0x636D6170u /* cmap */, Bytes(300, 'A'), true}, {0x68656164u /* head */, Bytes{1, 2, 3, 4, 5, 6, 7}, false},
                                       {0x6C6F6361u /* loca */, Bytes(57, 'B'), true}};
    const Bytes woff = Woff(tables);
    const Bytes want = Sfnt(tables);
    {
        CHECK(FontFileKindOf(woff.data(), woff.size()) == FontFileKind::Woff);
        CHECK(woff.size() < want.size());   // the compressed tables really are compressed
        Bytes got;
        std::string why;
        CHECK(DecodeWebFont(woff.data(), woff.size(), got, why));
        CHECK(got == want);
        if (got != want) std::cerr << "  " << why << " got " << got.size() << " bytes, want " << want.size() << "\n";
        CHECK(FontFileKindOf(got.data(), got.size()) == FontFileKind::Sfnt);
        // an sfnt goes through as it is
        Bytes same;
        CHECK(DecodeWebFont(want.data(), want.size(), same, why) && same == want);
    }
    {
        // broken and hostile files: each refused with a reason, nothing out
        auto refused = [&](Bytes b) {
            Bytes out;
            std::string why;
            const bool ok = DecodeWebFont(b.data(), b.size(), out, why);
            return !ok && out.empty() && !why.empty();
        };
        for (std::size_t cut : {std::size_t(3), std::size_t(20), std::size_t(43), std::size_t(60), woff.size() - 4}) {
            Bytes b(woff.begin(), woff.begin() + (std::ptrdiff_t)cut);
            CHECK(refused(b));   // cut off (the length field no longer matches)
        }
        Bytes b = woff; Set32(b, 44 + 4, 0xFFFFFF00u); CHECK(refused(b));                 // a table offset past the end
        b = woff; Set32(b, 44 + 8, 0x7FFFFFFFu); CHECK(refused(b));                       // a compressed length past the end
        b = woff; Set32(b, 44 + 12, 10); CHECK(refused(b));                               // bigger compressed than plain
        b = woff; Set32(b, 44 + 12, 301); CHECK(refused(b));                              // inflates to the wrong size
        b = woff; b[12] = 0xFF; b[13] = 0xFF; CHECK(refused(b));                          // 65535 tables
        b = woff; b[14] = 1; CHECK(refused(b));                                           // reserved not 0
        b = woff; Set32(b, 16, 0x7FFFFFFFu); CHECK(refused(b));                           // a 2 GB font
        b = woff; Set32(b, 16, 40); CHECK(refused(b));                                    // tables bigger than the stated size
        b = woff; Set32(b, 44 + 20, 0x00000001u); CHECK(refused(b));                      // tags out of order
        b = woff; for (std::size_t k = 120; k < 130 && k < b.size(); ++k) b[k] ^= 0x5A; CHECK(refused(b));   // damaged zlib data
        const Bytes woff2{'w', 'O', 'F', '2', 0, 1, 0, 0, 0, 0, 0, 0};
        CHECK(FontFileKindOf(woff2.data(), woff2.size()) == FontFileKind::Woff2 && refused(woff2));
        CHECK(refused(Bytes{'G', 'I', 'F', '8', '9', 'a'}));
        WebFontLimits small;
        small.maxSfntBytes = 200;
        Bytes out;
        std::string why;
        CHECK(!DecodeWebFont(woff.data(), woff.size(), out, why, small));
    }
    {
        // @font-face: TrueType / OpenType first; a WOFF file when there is none; WOFF2 alone is not taken
        const StyleSheet a = ParseStyleSheet("@font-face{font-family:A;src:url(a.woff2) format('woff2'), url(a.woff) format('woff'), url(a.ttf) format('truetype')}");
        CHECK(a.fontFaces.size() == 1 && a.fontFaces[0].url == "a.ttf");
        const StyleSheet b = ParseStyleSheet("@font-face{font-family:B;src:url(b.woff2) format('woff2'), url(b.woff) format('woff')}");
        CHECK(b.fontFaces.size() == 1 && b.fontFaces[0].url == "b.woff");
        const StyleSheet c = ParseStyleSheet("@font-face{font-family:C;src:url(c.woff)}");
        CHECK(c.fontFaces.size() == 1 && c.fontFaces[0].url == "c.woff");
        const StyleSheet d = ParseStyleSheet("@font-face{font-family:D;src:url(d.woff2)}");
        CHECK(d.fontFaces.empty());
    }
    TEST_MAIN_RESULT();
}
