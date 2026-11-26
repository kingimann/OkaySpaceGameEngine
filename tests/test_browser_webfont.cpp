// Browser (page engine): web font files (engine/include/okay/Browser/WebFont.hpp). A WOFF 1.0 file is built here by hand from
// the W3C "WOFF File Format 1.0" layout (header, table directory, one zlib-compressed and one stored table), decoded, and
// compared byte for byte with the sfnt it stands for (offset table, table records, 4-byte padding as the OpenType
// specification lays them out). Then broken and hostile files, and @font-face picking a WOFF / WOFF2 file only when there is
// no TrueType / OpenType one.
// WOFF2 (W3C "WOFF File Format 2.0"): files are built here from assets/fonts/DejaVuSans.ttf with a small WOFF2 writer of our
// own (the table directory with known tags and UIntBase128, the glyf / loca transform with its seven streams and triplet
// coding, the hmtx transform, a collection directory), the tables compressed with tests/brotli_writer.hpp (uncompressed and
// literal-only meta-blocks), decoded, and compared with the font: every other table byte for byte, every glyph by its points,
// contours, instructions and box, the checksums worked out again. Then hostile WOFF2 files.
#include "test_framework.hpp"
#include "brotli_writer.hpp"
#include "okay/Browser/Css.hpp"
#include "okay/Browser/WebFont.hpp"
#include "okay/Core/Inflate.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
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


// ---- WOFF2 -------------------------------------------------------------------------------------------------------------
static std::uint16_t Rd16(const Bytes& b, std::size_t at) { return std::uint16_t(b[at] << 8 | b[at + 1]); }
static std::uint32_t Rd32(const Bytes& b, std::size_t at) { return std::uint32_t(b[at]) << 24 | std::uint32_t(b[at + 1]) << 16 | std::uint32_t(b[at + 2]) << 8 | b[at + 3]; }
static std::uint32_t Tag(const char* t) { return std::uint32_t(std::uint8_t(t[0])) << 24 | std::uint32_t(std::uint8_t(t[1])) << 16 | std::uint32_t(std::uint8_t(t[2])) << 8 | std::uint8_t(t[3]); }

using Tables = std::map<std::uint32_t, Bytes>;   // tag -> table bytes
static Tables ReadSfnt(const Bytes& f) {   // (empty for a file that is not a whole sfnt)
    Tables t;
    const std::uint16_t n = f.size() >= 12 ? Rd16(f, 4) : 0;
    for (std::uint16_t i = 0; i < n && 12 + 16u * (i + 1u) <= f.size(); ++i) {
        const std::size_t r = 12 + 16u * i;
        const std::uint32_t off = Rd32(f, r + 8), len = Rd32(f, r + 12);
        if (off > f.size() || len > f.size() - off) return Tables();
        t[Rd32(f, r)] = Bytes(f.begin() + off, f.begin() + off + len);
    }
    return t;
}
static void Put255(Bytes& b, std::uint32_t v) {   // 255UInt16
    if (v < 253) b.push_back(std::uint8_t(v));
    else if (v < 506) { b.push_back(255); b.push_back(std::uint8_t(v - 253)); }
    else if (v < 762) { b.push_back(254); b.push_back(std::uint8_t(v - 506)); }
    else { b.push_back(253); Put16(b, std::uint16_t(v)); }
}
static void PutBase128(Bytes& b, std::uint32_t v) {   // UIntBase128
    std::uint8_t g[5];
    int n = 0;
    do { g[n++] = std::uint8_t(v & 0x7F); v >>= 7; } while (v);
    while (n--) b.push_back(std::uint8_t(g[n] | (n ? 0x80 : 0)));
}

// One glyph of a glyf table, read for comparing (simple: contours, points, on-curve; composite: its bytes).
struct GlyphInfo {
    int contours = 0;
    std::int16_t box[4] = {0, 0, 0, 0};
    std::vector<std::uint16_t> ends;
    Bytes instructions, onCurve, composite;
    std::vector<int> x, y;
    bool overlapFlag = false;
    bool operator==(const GlyphInfo& o) const {
        return contours == o.contours && std::equal(box, box + 4, o.box) && ends == o.ends && instructions == o.instructions &&
               onCurve == o.onCurve && composite == o.composite && x == o.x && y == o.y && overlapFlag == o.overlapFlag;
    }
};
static std::uint32_t LocaAt(const Bytes& loca, int fmt, std::size_t i) { return fmt ? Rd32(loca, 4 * i) : 2u * Rd16(loca, 2 * i); }
static GlyphInfo ParseGlyph(const Bytes& glyf, std::uint32_t from, std::uint32_t to) {
    GlyphInfo g;
    if (from == to) return g;
    g.contours = std::int16_t(Rd16(glyf, from));
    for (int k = 0; k < 4; ++k) g.box[k] = std::int16_t(Rd16(glyf, from + 2 + 2u * k));
    std::size_t p = from + 10;
    if (g.contours < 0) {   // composite: keep the bytes after the header (components and instructions), without padding
        for (std::uint16_t f = 0x20; f & 0x20;) {
            f = Rd16(glyf, p);
            const std::size_t len = 4 + ((f & 1) ? 4 : 2) + ((f & 8) ? 2 : (f & 0x40) ? 4 : (f & 0x80) ? 8 : 0);
            p += len;
            if (!(f & 0x20) && (f & 0x100)) p += 2 + Rd16(glyf, p);
        }
        g.composite = Bytes(glyf.begin() + from + 10, glyf.begin() + p);
        return g;
    }
    for (int c = 0; c < g.contours; ++c) { g.ends.push_back(Rd16(glyf, p)); p += 2; }
    const std::uint16_t il = Rd16(glyf, p); p += 2;
    g.instructions = Bytes(glyf.begin() + p, glyf.begin() + p + il); p += il;
    const std::size_t n = g.ends.empty() ? 0 : g.ends.back() + 1u;
    Bytes flags;
    while (flags.size() < n) {
        const std::uint8_t f = glyf[p++];
        flags.push_back(f);
        if (f & 8) for (int r = glyf[p++]; r > 0; --r) flags.push_back(f);
    }
    g.overlapFlag = n && (flags[0] & 0x40);
    int v = 0;
    for (std::uint8_t f : flags) {
        if (f & 2) v += (f & 16) ? glyf[p] : -glyf[p], ++p;
        else if (!(f & 16)) { v += std::int16_t(Rd16(glyf, p)); p += 2; }
        g.x.push_back(v);
    }
    v = 0;
    for (std::uint8_t f : flags) {
        if (f & 4) v += (f & 32) ? glyf[p] : -glyf[p], ++p;
        else if (!(f & 32)) { v += std::int16_t(Rd16(glyf, p)); p += 2; }
        g.y.push_back(v);
        g.onCurve.push_back(f & 1);
    }
    return g;
}

// A point's delta in the transformed glyf's triplet coding: the flag byte (with the on-curve bit 7) and its data bytes.
static void PutTriplet(Bytes& flags, Bytes& data, int dx, int dy, bool on) {
    const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, sx = dx >= 0, sy = dy >= 0;
    int b;
    if (dx == 0 && ay < 1280) { b = ((ay >> 8) << 1) | sy; data.push_back(std::uint8_t(ay)); }
    else if (dy == 0 && ax < 1280) { b = 10 + (((ax >> 8) << 1) | sx); data.push_back(std::uint8_t(ax)); }
    else if (ax >= 1 && ax <= 64 && ay >= 1 && ay <= 64) {
        b = 20 + (((ax - 1) & 0x30) | (((ay - 1) >> 4) << 2) | (sy << 1) | sx);
        data.push_back(std::uint8_t(((ax - 1) & 15) << 4 | ((ay - 1) & 15)));
    } else if (ax >= 1 && ax <= 768 && ay >= 1 && ay <= 768) {
        b = 84 + 12 * ((ax - 1) >> 8) + 4 * ((ay - 1) >> 8) + (sy << 1) + sx;
        data.push_back(std::uint8_t(ax - 1)); data.push_back(std::uint8_t(ay - 1));
    } else if (ax < 4096 && ay < 4096) {
        b = 120 + (sy << 1) + sx;
        data.push_back(std::uint8_t(ax >> 4)); data.push_back(std::uint8_t((ax & 15) << 4 | ay >> 8)); data.push_back(std::uint8_t(ay));
    } else {
        b = 124 + (sy << 1) + sx;
        Put16(data, std::uint16_t(ax)); Put16(data, std::uint16_t(ay));
    }
    flags.push_back(std::uint8_t(b | (on ? 0 : 0x80)));
}

struct GlyfStreams { Bytes header, s[7], overlap; };   // nContour, nPoints, flag, glyph, composite, bbox, instruction
static Bytes JoinGlyf(GlyfStreams g) {
    Bytes b = g.header;
    for (int k = 0; k < 7; ++k) Set32(b, 8 + 4u * k, std::uint32_t(g.s[k].size()));
    for (int k = 0; k < 7; ++k) b.insert(b.end(), g.s[k].begin(), g.s[k].end());
    b.insert(b.end(), g.overlap.begin(), g.overlap.end());
    return b;
}
// The transformed glyf of a font (explicitBoxEvery: also write the box of every n-th simple glyph, though it can be worked out;
// overlapGlyph: mark that glyph's first point "overlap" through the optional bitmap, -1 for none).
static GlyfStreams TransformGlyf(const Tables& t, int explicitBoxEvery, int overlapGlyph) {
    const Bytes& glyf = t.at(Tag("glyf"));
    const Bytes& loca = t.at(Tag("loca"));
    const int fmt = std::int16_t(Rd16(t.at(Tag("head")), 50));
    const std::uint16_t ng = Rd16(t.at(Tag("maxp")), 4);
    GlyfStreams g;
    Put16(g.header, 0); Put16(g.header, overlapGlyph >= 0 ? 1 : 0); Put16(g.header, ng); Put16(g.header, std::uint16_t(fmt));
    g.header.resize(36, 0);
    Bytes bitmap(4u * ((ng + 31u) / 32u), 0), boxes;
    if (overlapGlyph >= 0) { g.overlap.assign((ng + 7u) / 8u, 0); g.overlap[std::size_t(overlapGlyph) / 8] |= std::uint8_t(0x80 >> (overlapGlyph % 8)); }
    int simpleCount = 0;
    for (std::uint32_t i = 0; i < ng; ++i) {
        const std::uint32_t a = LocaAt(loca, fmt, i), e = LocaAt(loca, fmt, i + 1);
        const GlyphInfo gi = ParseGlyph(glyf, a, e);
        Put16(g.s[0], std::uint16_t(std::int16_t(a == e ? 0 : gi.contours)));
        if (a == e) continue;
        auto putBox = [&] { bitmap[i / 8] |= std::uint8_t(0x80 >> (i % 8)); for (std::int16_t v : gi.box) Put16(boxes, std::uint16_t(v)); };
        if (gi.contours < 0) {
            putBox();
            // components to the composite stream; the instructions (after the components) to the glyph / instruction streams
            std::size_t p = 0;
            bool instr = false;
            for (std::uint16_t f = 0x20; f & 0x20;) {
                f = Rd16(gi.composite, p);
                const std::size_t len = 4 + ((f & 1) ? 4 : 2) + ((f & 8) ? 2 : (f & 0x40) ? 4 : (f & 0x80) ? 8 : 0);
                g.s[4].insert(g.s[4].end(), gi.composite.begin() + p, gi.composite.begin() + p + len);
                instr = instr || (f & 0x100);
                p += len;
            }
            if (instr) {
                const std::uint16_t il = Rd16(gi.composite, p);
                Put255(g.s[3], il);
                g.s[6].insert(g.s[6].end(), gi.composite.begin() + p + 2, gi.composite.begin() + p + 2 + il);
            }
            continue;
        }
        std::uint16_t prevEnd = 0xFFFF;
        for (std::uint16_t e2 : gi.ends) { Put255(g.s[1], std::uint16_t(e2 - prevEnd)); prevEnd = e2; }
        int px = 0, py = 0;
        for (std::size_t k = 0; k < gi.x.size(); ++k) { PutTriplet(g.s[2], g.s[3], gi.x[k] - px, gi.y[k] - py, gi.onCurve[k] != 0); px = gi.x[k]; py = gi.y[k]; }
        Put255(g.s[3], std::uint16_t(gi.instructions.size()));
        g.s[6].insert(g.s[6].end(), gi.instructions.begin(), gi.instructions.end());
        const int mnx = *std::min_element(gi.x.begin(), gi.x.end()), mxx = *std::max_element(gi.x.begin(), gi.x.end());
        const int mny = *std::min_element(gi.y.begin(), gi.y.end()), mxy = *std::max_element(gi.y.begin(), gi.y.end());
        const bool same = gi.box[0] == mnx && gi.box[1] == mny && gi.box[2] == mxx && gi.box[3] == mxy;
        if (!same || (explicitBoxEvery && ++simpleCount % explicitBoxEvery == 0)) putBox();
    }
    g.s[5] = bitmap;
    g.s[5].insert(g.s[5].end(), boxes.begin(), boxes.end());
    return g;
}

struct W2Entry { std::uint32_t tag; Bytes data; int version; std::uint32_t origLength; };   // data: as stored (transformed or not)
static const char* const kKnown[63] = {"cmap", "head", "hhea", "hmtx", "maxp", "name", "OS/2", "post", "cvt ", "fpgm", "glyf", "loca", "prep", "CFF ", "VORG", "EBDT",
    "EBLC", "gasp", "hdmx", "kern", "LTSH", "PCLT", "VDMX", "vhea", "vmtx", "BASE", "GDEF", "GPOS", "GSUB", "EBSC", "JSTF", "MATH", "CBDT", "CBLC", "COLR", "CPAL",
    "SVG ", "sbix", "acnt", "avar", "bdat", "bloc", "bsln", "cvar", "fdsc", "feat", "fmtx", "fvar", "gvar", "hsty", "just", "lcar", "mort", "morx", "opbd", "prop",
    "trak", "Zapf", "Silf", "Glat", "Gloc", "Feat", "Sill"};
// A WOFF2 file: directory, optional collection directory (fonts: flavor + table indexes), one Brotli stream.
static Bytes Woff2File(std::uint32_t flavor, const std::vector<W2Entry>& es, bool literalBrotli,
                       const std::vector<std::pair<std::uint32_t, std::vector<std::uint16_t>>>& fonts = {}) {
    Bytes dir;
    Bytes all;
    for (const W2Entry& e : es) {
        int idx = 63;
        for (int k = 0; k < 63; ++k) if (Tag(kKnown[k]) == e.tag) idx = k;
        dir.push_back(std::uint8_t(idx | (e.version << 6)));
        if (idx == 63) Put32(dir, e.tag);
        PutBase128(dir, e.origLength);
        const bool transformed = ((e.tag == Tag("glyf") || e.tag == Tag("loca")) && e.version == 0) || (e.tag == Tag("hmtx") && e.version == 1);
        if (transformed) PutBase128(dir, std::uint32_t(e.data.size()));
        all.insert(all.end(), e.data.begin(), e.data.end());
    }
    if (!fonts.empty()) {
        Put32(dir, 0x00010000u);
        Put255(dir, std::uint32_t(fonts.size()));
        for (const auto& f : fonts) { Put255(dir, std::uint32_t(f.second.size())); Put32(dir, f.first); for (std::uint16_t i : f.second) Put255(dir, i); }
    }
    Bytes br;
    if (literalBrotli) {
        brw::BitWriter w;
        brw::WriteWindow(w, 22);
        const std::size_t chunk = 1u << 20;
        for (std::size_t at = 0; at < all.size(); at += chunk) {
            brw::Command c;
            c.literals.assign(all.begin() + std::ptrdiff_t(at), all.begin() + std::ptrdiff_t(std::min(all.size(), at + chunk)));
            c.last = true;
            brw::WriteCommands(w, {c}, at + chunk >= all.size(), c.literals.size());
        }
        w.ToByte();
        br = w.bytes;
    } else br = brw::StoredStream(all.data(), all.size());
    Bytes f;
    Put32(f, 0x774F4632u); Put32(f, flavor); Put32(f, 0); Put16(f, std::uint16_t(es.size())); Put16(f, 0);
    Put32(f, 0); Put32(f, std::uint32_t(br.size())); Put16(f, 1); Put16(f, 0);
    for (int k = 0; k < 5; ++k) Put32(f, 0);
    f.insert(f.end(), dir.begin(), dir.end());
    f.insert(f.end(), br.begin(), br.end());
    while (f.size() % 4) f.push_back(0);
    Set32(f, 8, std::uint32_t(f.size()));
    return f;
}
static std::uint32_t SumOf(const Bytes& b, std::size_t from, std::size_t len) {
    std::uint32_t s = 0;
    for (std::size_t i = 0; i < len; i += 4) {
        std::uint32_t w = 0;
        for (std::size_t k = 0; k < 4; ++k) w = w << 8 | (i + k < len ? b[from + i + k] : 0u);
        s += w;
    }
    return s;
}

int main() {
    RUN_SUITE("browser web fonts: WOFF 1.0 and WOFF2");
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
        CHECK(FontFileKindOf(woff2.data(), woff2.size()) == FontFileKind::Woff2 && refused(woff2));   // (a WOFF2 header cut off)
        CHECK(refused(Bytes{'G', 'I', 'F', '8', '9', 'a'}));
        WebFontLimits tight;
        tight.maxSfntBytes = 200;
        Bytes out;
        std::string why;
        CHECK(!DecodeWebFont(woff.data(), woff.size(), out, why, tight));
    }
    {
        // ---- WOFF2 from DejaVu Sans ----
        std::ifstream file(std::string(OKAY_SOURCE_DIR) + "/assets/fonts/DejaVuSans.ttf", std::ios::binary);
        const Bytes ttf((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        CHECK(ttf.size() > 100000);
        if (ttf.size() < 100000) TEST_MAIN_RESULT();
        const std::uint32_t kGlyfT = Tag("glyf"), kLocaT = Tag("loca"), kHmtxT = Tag("hmtx"), kHeadT = Tag("head");
        const Tables src = ReadSfnt(ttf);
        const int fmt = std::int16_t(Rd16(src.at(kHeadT), 50));
        const std::uint16_t ng = Rd16(src.at(Tag("maxp")), 4), nh = Rd16(src.at(Tag("hhea")), 34);
        CHECK(ng > 1000 && nh <= ng && src.at(kHmtxT).size() == 4u * nh + 2u * (ng - nh));
        // the font as it goes into WOFF2 with a transformed hmtx: every left side bearing equal to its glyph's xMin (0 for an
        // empty glyph), which is what lets the transform leave them out
        Tables norm = src;
        for (std::uint32_t i = 0; i < ng; ++i) {
            const std::uint32_t a = LocaAt(src.at(kLocaT), fmt, i), e = LocaAt(src.at(kLocaT), fmt, i + 1);
            const std::int16_t xMin = a == e ? 0 : std::int16_t(Rd16(src.at(kGlyfT), a + 2));
            const std::size_t at = i < nh ? 4u * i + 2 : 4u * nh + 2u * (i - nh);
            norm[kHmtxT][at] = std::uint8_t(std::uint16_t(xMin) >> 8); norm[kHmtxT][at + 1] = std::uint8_t(xMin);
        }
        int overlapGlyph = 36;
        while (ParseGlyph(src.at(kGlyfT), LocaAt(src.at(kLocaT), fmt, std::size_t(overlapGlyph)), LocaAt(src.at(kLocaT), fmt, std::size_t(overlapGlyph) + 1)).contours <= 0) ++overlapGlyph;

        // the decoded font checked against `want`: the sfnt layout, every checksum, and every table (glyf by its glyphs)
        auto checkFont = [&](const Bytes& out, const Tables& want, bool glyphsOnly, int overlap) {
            bool ok = out.size() > 12 && Rd32(out, 0) == 0x00010000u && Rd16(out, 4) == want.size();
            if (!ok) return false;
            const Tables got = ReadSfnt(out);
            ok = ok && got.size() == want.size();
            std::uint32_t prevTag = 0;
            for (std::uint16_t i = 0; i < Rd16(out, 4); ++i) {
                const std::size_t r = 12 + 16u * i;
                const std::uint32_t tag = Rd32(out, r), off = Rd32(out, r + 8), len = Rd32(out, r + 12);
                ok = ok && tag > prevTag && off % 4 == 0;
                prevTag = tag;
                Bytes body(out.begin() + off, out.begin() + off + len);
                if (tag == kHeadT) { body[8] = body[9] = body[10] = body[11] = 0; }
                ok = ok && Rd32(out, r + 4) == SumOf(body, 0, body.size());
            }
            ok = ok && SumOf(out, 0, out.size()) == 0xB1B0AFBAu;   // head.checkSumAdjustment makes the whole font add up
            if (!ok) std::cerr << "  WOFF2: the rebuilt sfnt's layout or checksums are wrong\n";
            for (const auto& kv : want) {
                if (!got.count(kv.first)) { ok = false; continue; }
                const Bytes& g = got.at(kv.first);
                if (kv.first == kHeadT) { ok = ok && g.size() == kv.second.size() && std::equal(g.begin(), g.begin() + 8, kv.second.begin()) && std::equal(g.begin() + 12, g.end(), kv.second.begin() + 12); continue; }
                if (glyphsOnly && (kv.first == kGlyfT || kv.first == kLocaT)) continue;
                if (g != kv.second) { ok = false; std::cerr << "  WOFF2: table " << std::string(1, char(kv.first >> 24)) << std::string(1, char(kv.first >> 16)) << std::string(1, char(kv.first >> 8)) << std::string(1, char(kv.first)) << " differs\n"; }
            }
            if (glyphsOnly && got.count(kGlyfT) && got.count(kLocaT)) {
                const Bytes& gg = got.at(kGlyfT); const Bytes& gl = got.at(kLocaT);
                ok = ok && gl.size() == want.at(kLocaT).size();
                int differ = 0;
                for (std::uint32_t i = 0; ok && i < ng; ++i) {
                    GlyphInfo a = ParseGlyph(want.at(kGlyfT), LocaAt(want.at(kLocaT), fmt, i), LocaAt(want.at(kLocaT), fmt, i + 1));
                    const GlyphInfo b = ParseGlyph(gg, LocaAt(gl, fmt, i), LocaAt(gl, fmt, i + 1));
                    if (int(i) == overlap) a.overlapFlag = true;
                    differ += !(a == b);
                }
                if (differ) std::cerr << "  WOFF2: " << differ << " glyphs differ\n";
                ok = ok && differ == 0;
            }
            return ok;
        };
        // A: no transforms (glyf and loca version 3), uncompressed meta-blocks: the font comes back table for table
        std::vector<W2Entry> plain;
        for (const auto& kv : src) plain.push_back({kv.first, kv.second, (kv.first == kGlyfT || kv.first == kLocaT) ? 3 : 0, std::uint32_t(kv.second.size())});
        const Bytes fileA = Woff2File(0x00010000u, plain, false);
        Bytes out;
        std::string why;
        CHECK(FontFileKindOf(fileA.data(), fileA.size()) == FontFileKind::Woff2);
        CHECK(DecodeWebFont(fileA.data(), fileA.size(), out, why));
        if (!why.empty()) std::cerr << "  WOFF2 A: " << why << "\n";
        CHECK(checkFont(out, src, false, -1));
        // B: glyf / loca transformed (some boxes written though they could be worked out, one overlap mark), hmtx
        // transformed (both bearing arrays left out), literal-only compressed meta-blocks
        std::vector<W2Entry> tr;
        const Bytes glyfT = JoinGlyf(TransformGlyf(norm, 5, overlapGlyph));
        Bytes hmtxT{3};
        for (std::uint32_t i = 0; i < nh; ++i) { hmtxT.push_back(norm.at(kHmtxT)[4 * i]); hmtxT.push_back(norm.at(kHmtxT)[4 * i + 1]); }
        for (const auto& kv : norm) {
            if (kv.first == kGlyfT) tr.push_back({kv.first, glyfT, 0, std::uint32_t(kv.second.size())});
            else if (kv.first == kLocaT) tr.push_back({kv.first, Bytes(), 0, std::uint32_t(kv.second.size())});
            else if (kv.first == kHmtxT) tr.push_back({kv.first, hmtxT, 1, std::uint32_t(kv.second.size())});
            else tr.push_back({kv.first, kv.second, 0, std::uint32_t(kv.second.size())});
        }
        const Bytes fileB = Woff2File(0x00010000u, tr, true);
        CHECK(fileB.size() < ttf.size());   // the transforms really make it smaller (no compression: literals only)
        CHECK(DecodeWebFont(fileB.data(), fileB.size(), out, why));
        if (!why.empty()) std::cerr << "  WOFF2 B: " << why << "\n";
        CHECK(checkFont(out, norm, true, overlapGlyph));
        // a transformed glyf with no explicit boxes but the needed ones, no overlap bitmap, and hmtx not transformed
        for (W2Entry& e : tr) {
            if (e.tag == kGlyfT) e.data = JoinGlyf(TransformGlyf(norm, 0, -1));
            if (e.tag == kHmtxT) { e.data = norm.at(kHmtxT); e.version = 0; }
        }
        const Bytes fileC = Woff2File(0x00010000u, tr, false);
        CHECK(DecodeWebFont(fileC.data(), fileC.size(), out, why) && checkFont(out, norm, true, -1));

        // hostile WOFF2 files: refused with a reason, nothing out
        auto refusedW2 = [&](const Bytes& b, const WebFontLimits& lim = WebFontLimits()) {
            Bytes o;
            std::string w;
            return !DecodeWebFont(b.data(), b.size(), o, w, lim) && o.empty() && !w.empty();
        };
        for (std::size_t cut : {std::size_t(47), std::size_t(60), fileB.size() / 2, fileB.size() - 8}) {
            Bytes b(fileB.begin(), fileB.begin() + std::ptrdiff_t(cut));
            Set32(b, 8, std::uint32_t(b.size()));
            CHECK(refusedW2(b));
        }
        Bytes b = fileA; b[14] = 1; CHECK(refusedW2(b));                                   // reserved not 0
        b = fileA; Set32(b, 8, 12345); CHECK(refusedW2(b));                                // length field
        b = fileA; b[12] = 0; b[13] = 0; CHECK(refusedW2(b));                              // no tables
        WebFontLimits tight;
        tight.maxSfntBytes = 100000;
        CHECK(refusedW2(fileA, tight) && refusedW2(fileB, tight));                         // over the size limit
        // a small font for the directory cases: head (54 bytes) and a private table
        const Bytes head54(54, 7);
        auto tiny = [&](std::vector<W2Entry> es) { return Woff2File(0x00010000u, es, false); };
        const Bytes ok1 = tiny({{kHeadT, head54, 0, 54}, {Tag("zzzz"), Bytes(5, 9), 0, 5}});
        CHECK(DecodeWebFont(ok1.data(), ok1.size(), out, why) && ReadSfnt(out).count(Tag("zzzz")) && ReadSfnt(out).at(Tag("zzzz")) == Bytes(5, 9));
        b = ok1; b[48] |= 0x40; CHECK(refusedW2(b));                                       // a transform version on head
        b = ok1; b.insert(b.begin() + 49, 0x80); Set32(b, 8, std::uint32_t(b.size())); CHECK(refusedW2(b));   // UIntBase128 with a leading zero
        b = ok1; b[49] = 0xFF; b[50] = 0xFF; b[51] = 0xFF; b[52] = 0xFF; b[53] = 0xFF; CHECK(refusedW2(b));   // UIntBase128 past 5 bytes
        CHECK(refusedW2(tiny({{kHeadT, head54, 0, 60}})));                                // the stream is shorter than the tables
        CHECK(refusedW2(tiny({{kHeadT, head54, 0, 54}, {kHeadT, head54, 0, 54}})));       // a table twice
        CHECK(refusedW2(tiny({{kHeadT, head54, 0, 0x7FFFFFF0u}})));                       // a 2 GB table
        CHECK(refusedW2(tiny({{kHeadT, head54, 0, 54}, {kLocaT, Bytes(4, 0), 0, 8}})));   // a transformed loca with data
        // glyf transformed, loca not
        std::vector<W2Entry> mix = tr;
        for (W2Entry& e : mix) if (e.tag == kLocaT) { e.version = 3; e.data = norm.at(kLocaT); }
        CHECK(refusedW2(Woff2File(0x00010000u, mix, false)));
        // damaged glyph streams
        auto withGlyf = [&](const GlyfStreams& g) {
            std::vector<W2Entry> v = tr;
            for (W2Entry& e : v) if (e.tag == kGlyfT) e.data = JoinGlyf(g);
            return Woff2File(0x00010000u, v, false);
        };
        GlyfStreams g = TransformGlyf(norm, 0, -1);
        g.s[3].resize(g.s[3].size() - 40); CHECK(refusedW2(withGlyf(g)));                 // glyph stream cut off
        g = TransformGlyf(norm, 0, -1); g.s[2].resize(g.s[2].size() / 2); CHECK(refusedW2(withGlyf(g)));   // flag stream cut off
        g = TransformGlyf(norm, 0, -1); g.s[0].resize(g.s[0].size() - 2); CHECK(refusedW2(withGlyf(g)));   // contour stream cut off
        g = TransformGlyf(norm, 0, -1); g.s[6].clear(); CHECK(refusedW2(withGlyf(g)));     // instructions missing
        g = TransformGlyf(norm, 0, -1); Set32(g.header, 4, 0x0002FFFFu); CHECK(refusedW2(withGlyf(g)));    // index format 0xFFFF
        g = TransformGlyf(norm, 0, -1); g.header[5] = 0xFF; g.header[4] = 0x7F; CHECK(refusedW2(withGlyf(g)));   // 32767 glyphs: past the contour stream
        {
            // a composite glyph's box left out of the bitmap
            g = TransformGlyf(norm, 0, -1);
            std::uint32_t comp = 0;
            while (std::int16_t(Rd16(g.s[0], 2u * comp)) != -1) ++comp;
            g.s[5][comp / 8] = std::uint8_t(g.s[5][comp / 8] & ~(0x80 >> (comp % 8)));
            CHECK(refusedW2(withGlyf(g)));
        }
        {
            // a transformed hmtx with flags past bit 1, and one whose metric count is past the glyph count
            std::vector<W2Entry> v = tr;
            for (W2Entry& e : v) if (e.tag == kHmtxT) { e.version = 1; e.data = hmtxT; e.data[0] = 7; }
            CHECK(refusedW2(Woff2File(0x00010000u, v, false)));
            for (W2Entry& e : v) if (e.tag == Tag("hhea")) { e.data[34] = 0xFF; e.data[35] = 0xFF; }
            for (W2Entry& e : v) if (e.tag == kHmtxT) e.data[0] = 3;
            CHECK(refusedW2(Woff2File(0x00010000u, v, false)));
        }
        // random damage to a small transformed font (the first 40 glyphs): refused or decoded, never a crash
        {
            Tables sub;
            sub[kHeadT] = src.at(kHeadT);
            Bytes maxp = src.at(Tag("maxp")); maxp[4] = 0; maxp[5] = 40; sub[Tag("maxp")] = maxp;
            Bytes hhea = src.at(Tag("hhea")); hhea[34] = 0; hhea[35] = 40; sub[Tag("hhea")] = hhea;
            sub[kLocaT] = Bytes(src.at(kLocaT).begin(), src.at(kLocaT).begin() + (fmt ? 4 : 2) * 41);
            sub[kGlyfT] = Bytes(src.at(kGlyfT).begin(), src.at(kGlyfT).begin() + LocaAt(src.at(kLocaT), fmt, 40));
            Bytes hm(160);
            for (std::uint32_t i = 0; i < 40; ++i) {
                const std::uint32_t a = LocaAt(sub[kLocaT], fmt, i), e = LocaAt(sub[kLocaT], fmt, i + 1);
                const std::int16_t xMin = a == e ? 0 : std::int16_t(Rd16(sub[kGlyfT], a + 2));
                hm[4 * i] = std::uint8_t(i); hm[4 * i + 1] = 0; hm[4 * i + 2] = std::uint8_t(std::uint16_t(xMin) >> 8); hm[4 * i + 3] = std::uint8_t(xMin);
            }
            sub[kHmtxT] = hm;
            Bytes hmT{3};
            for (std::uint32_t i = 0; i < 40; ++i) { hmT.push_back(hm[4 * i]); hmT.push_back(hm[4 * i + 1]); }
            std::vector<W2Entry> v;
            for (const auto& kv : sub) {
                if (kv.first == kGlyfT) v.push_back({kv.first, JoinGlyf(TransformGlyf(sub, 3, 2)), 0, std::uint32_t(kv.second.size())});
                else if (kv.first == kLocaT) v.push_back({kv.first, Bytes(), 0, std::uint32_t(kv.second.size())});
                else if (kv.first == kHmtxT) v.push_back({kv.first, hmT, 1, std::uint32_t(kv.second.size())});
                else v.push_back({kv.first, kv.second, 0, std::uint32_t(kv.second.size())});
            }
            const Bytes small40 = Woff2File(0x00010000u, v, false);
            CHECK(DecodeWebFont(small40.data(), small40.size(), out, why));
            const Tables got = ReadSfnt(out);
            CHECK(got.count(kHmtxT) && got.at(kHmtxT) == hm);
            std::uint32_t rs = 77;
            int decoded = 0;
            for (int it = 0; it < 3000; ++it) {
                Bytes m = small40;
                for (int f = 0; f < 3; ++f) { rs = rs * 1664525u + 1013904223u; m[48 + (rs >> 8) % (m.size() - 48)] ^= std::uint8_t(1u << ((rs >> 4) & 7)); }
                Bytes o;
                std::string w;
                decoded += DecodeWebFont(m.data(), m.size(), o, w);
            }
            std::cout << "  damaged small WOFF2 files decoded: " << decoded << " of 3000\n";
            CHECK(decoded < 3000);
        }
        // a collection: two fonts sharing head; the result is a TrueType collection with one copy of head
        {
            const Bytes cmap(20, 'C'), hhea(36, 'H'), maxp(6, 'M'), name(13, 'N');
            std::vector<W2Entry> es = {{Tag("cmap"), cmap, 0, 20}, {kHeadT, head54, 0, 54}, {Tag("hhea"), hhea, 0, 36}, {Tag("maxp"), maxp, 0, 6}, {Tag("name"), name, 0, 13}};
            const Bytes ttc = Woff2File(0x74746366u /* ttcf */, es, true, {{0x00010000u, {2, 1, 0}}, {0x4F54544Fu, {4, 1, 3}}});
            CHECK(DecodeWebFont(ttc.data(), ttc.size(), out, why));
            if (!why.empty()) std::cerr << "  WOFF2 collection: " << why << "\n";
            CHECK(out.size() > 12 && Rd32(out, 0) == 0x74746366u && Rd32(out, 4) == 0x00010000u && Rd32(out, 8) == 2);
            if (out.size() > 20) {
                const std::uint32_t f0 = Rd32(out, 12), f1 = Rd32(out, 16);
                CHECK(Rd32(out, f0) == 0x00010000u && Rd16(out, f0 + 4) == 3 && Rd32(out, f1) == 0x4F54544Fu && Rd16(out, f1 + 4) == 3);
                // font 0: cmap, head, hhea in tag order; font 1: head, maxp, name; head at one offset
                CHECK(Rd32(out, f0 + 12) == Tag("cmap") && Rd32(out, f0 + 28) == kHeadT && Rd32(out, f0 + 44) == Tag("hhea"));
                CHECK(Rd32(out, f1 + 12) == kHeadT && Rd32(out, f1 + 28) == Tag("maxp") && Rd32(out, f1 + 44) == Tag("name"));
                CHECK(Rd32(out, f0 + 28 + 8) == Rd32(out, f1 + 12 + 8));
                const std::uint32_t nameAt = Rd32(out, f1 + 44 + 8);
                CHECK(Bytes(out.begin() + nameAt, out.begin() + nameAt + 13) == name && Rd32(out, f1 + 44 + 4) == SumOf(name, 0, 13));
            }
            CHECK(refusedW2(Woff2File(0x74746366u, es, false, {{0x00010000u, {2, 1, 9}}})));   // a table index past the directory
            CHECK(refusedW2(Woff2File(0x74746366u, es, false, {{0x00010000u, {1, 1}}})));      // a font with head twice
        }
    }
    {
        // @font-face: TrueType / OpenType first; then a WOFF file; then WOFF2
        const StyleSheet a = ParseStyleSheet("@font-face{font-family:A;src:url(a.woff2) format('woff2'), url(a.woff) format('woff'), url(a.ttf) format('truetype')}");
        CHECK(a.fontFaces.size() == 1 && a.fontFaces[0].url == "a.ttf");
        const StyleSheet b = ParseStyleSheet("@font-face{font-family:B;src:url(b.woff2) format('woff2'), url(b.woff) format('woff')}");
        CHECK(b.fontFaces.size() == 1 && b.fontFaces[0].url == "b.woff");
        const StyleSheet c = ParseStyleSheet("@font-face{font-family:C;src:url(c.woff)}");
        CHECK(c.fontFaces.size() == 1 && c.fontFaces[0].url == "c.woff");
        const StyleSheet d = ParseStyleSheet("@font-face{font-family:D;src:url(d.woff2)}");
        CHECK(d.fontFaces.size() == 1 && d.fontFaces[0].url == "d.woff2");
        const StyleSheet e = ParseStyleSheet("@font-face{font-family:E;src:url(e.bin) format('woff2'), url(e.svg) format('svg')}");
        CHECK(e.fontFaces.size() == 1 && e.fontFaces[0].url == "e.bin");
    }
    TEST_MAIN_RESULT();
}
