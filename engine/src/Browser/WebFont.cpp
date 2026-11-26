// Browser (page engine): web font files. Written from the W3C "WOFF File Format 1.0" recommendation (sections 3 to 6: the
// header, the table directory, the compressed table data, the rebuilt sfnt's table order and 4-byte padding) and the OpenType
// specification's "Organization of an OpenType font" (the offset table and table records, searchRange / entrySelector /
// rangeShift). Inflate is the engine's own (okay/Core/Inflate.hpp). No code from any font library or browser.
// WOFF2: written from the W3C "WOFF File Format 2.0" recommendation (the header, the table directory with its known-tag
// list, UIntBase128 and 255UInt16, the collection directory, one Brotli stream for all tables, the transformed glyf / loca
// tables and the transformed hmtx table) and the OpenType specification's glyf, loca, hmtx, head and TTC header layouts.
// Brotli is the engine's own (okay/Core/Brotli.hpp). No code from woff2, fontTools, FreeType or any browser.
#include "okay/Browser/WebFont.hpp"

#include "okay/Core/Brotli.hpp"
#include "okay/Core/Inflate.hpp"

#include <algorithm>

namespace okay::browser {

namespace {

std::uint32_t Be32(const std::uint8_t* p) { return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3]; }
std::uint16_t Be16(const std::uint8_t* p) { return (std::uint16_t)((p[0] << 8) | p[1]); }
void Put32(std::uint8_t* p, std::uint32_t v) { p[0] = (std::uint8_t)(v >> 24); p[1] = (std::uint8_t)(v >> 16); p[2] = (std::uint8_t)(v >> 8); p[3] = (std::uint8_t)v; }
void Put16(std::uint8_t* p, std::uint16_t v) { p[0] = (std::uint8_t)(v >> 8); p[1] = (std::uint8_t)v; }

struct Entry { std::uint32_t tag, offset, compLength, origLength, checksum; };

bool DecodeWoff2(const std::uint8_t* d, std::size_t n, std::vector<std::uint8_t>& out, std::string& why, const WebFontLimits& lim);

} // namespace

FontFileKind FontFileKindOf(const std::uint8_t* d, std::size_t n) {
    if (!d || n < 4) return FontFileKind::Unknown;
    const std::uint32_t sig = Be32(d);
    if (sig == 0x00010000u || sig == 0x74727565u /* true */ || sig == 0x4F54544Fu /* OTTO */) return FontFileKind::Sfnt;
    if (sig == 0x774F4646u) return FontFileKind::Woff;
    if (sig == 0x774F4632u) return FontFileKind::Woff2;
    return FontFileKind::Unknown;
}

bool DecodeWebFont(const std::uint8_t* d, std::size_t n, std::vector<std::uint8_t>& out, std::string& why, const WebFontLimits& lim) {
    out.clear();
    switch (FontFileKindOf(d, n)) {
    case FontFileKind::Sfnt:
        if (n < 12 || n > lim.maxSfntBytes) { why = "font file too small or too big"; return false; }
        out.assign(d, d + n);
        return true;
    case FontFileKind::Woff2: return DecodeWoff2(d, n, out, why, lim);
    case FontFileKind::Unknown: why = "not a font file"; return false;
    case FontFileKind::Woff: break;
    }
    // WOFF 1.0, section 3: the 44-byte header
    if (n < 44) { why = "WOFF header cut off"; return false; }
    const std::uint32_t flavor = Be32(d + 4), length = Be32(d + 8);
    const std::uint16_t numTables = Be16(d + 12), reserved = Be16(d + 14);
    const std::uint32_t totalSfnt = Be32(d + 16);
    if (length != n) { why = "WOFF length field does not match the file"; return false; }
    if (reserved != 0) { why = "WOFF reserved field is not 0"; return false; }
    if (numTables == 0 || numTables > lim.maxTables) { why = "WOFF table count out of range"; return false; }
    if (totalSfnt > lim.maxSfntBytes || totalSfnt < 12u + 16u * numTables) { why = "WOFF sfnt size out of range"; return false; }
    if (44u + 20u * (std::size_t)numTables > n) { why = "WOFF table directory cut off"; return false; }
    // section 4: the table directory (sorted by tag, data after the directory, inside the file)
    std::vector<Entry> e(numTables);
    std::uint64_t need = 12u + 16u * (std::uint64_t)numTables;
    for (std::size_t i = 0; i < numTables; ++i) {
        const std::uint8_t* p = d + 44 + 20 * i;
        e[i] = {Be32(p), Be32(p + 4), Be32(p + 8), Be32(p + 12), Be32(p + 16)};
        if (i > 0 && e[i].tag <= e[i - 1].tag) { why = "WOFF tables not in tag order"; return false; }
        if (e[i].compLength > e[i].origLength) { why = "WOFF table bigger compressed than plain"; return false; }
        if ((std::uint64_t)e[i].offset < 44u + 20u * (std::uint64_t)numTables || (std::uint64_t)e[i].offset + e[i].compLength > n) {
            why = "WOFF table outside the file"; return false;
        }
        need += ((std::uint64_t)e[i].origLength + 3u) & ~std::uint64_t(3);
        if (need > totalSfnt || need > lim.maxSfntBytes) { why = "WOFF tables bigger than the stated font size"; return false; }
    }
    // section 5: rebuild the sfnt: offset table, table records, the tables 4-byte aligned in directory order
    out.assign((std::size_t)need, 0);
    std::uint8_t* o = out.data();
    Put32(o, flavor);
    Put16(o + 4, numTables);
    std::uint16_t pow2 = 1, log2 = 0;
    while ((std::uint32_t)pow2 * 2u <= numTables) { pow2 = (std::uint16_t)(pow2 * 2); ++log2; }
    Put16(o + 6, (std::uint16_t)(pow2 * 16));
    Put16(o + 8, log2);
    Put16(o + 10, (std::uint16_t)(numTables * 16 - pow2 * 16));
    std::size_t at = 12u + 16u * numTables;
    std::vector<std::uint8_t> plain;
    for (std::size_t i = 0; i < numTables; ++i) {
        const Entry& t = e[i];
        std::uint8_t* rec = o + 12 + 16 * i;
        Put32(rec, t.tag); Put32(rec + 4, t.checksum); Put32(rec + 8, (std::uint32_t)at); Put32(rec + 12, t.origLength);
        if (t.compLength == t.origLength) std::copy(d + t.offset, d + t.offset + t.compLength, o + at);
        else {
            okay::inflate::Options op;
            op.maxOutput = t.origLength;
            op.hint = t.origLength;
            std::string err;
            if (!okay::inflate::Zlib(d + t.offset, t.compLength, plain, op, err) || plain.size() != t.origLength) {
                why = "WOFF table does not inflate to its size" + (err.empty() ? std::string() : ": " + err);
                out.clear();
                return false;
            }
            std::copy(plain.begin(), plain.end(), o + at);
        }
        at += (t.origLength + 3u) & ~std::size_t(3);
    }
    return true;
}

// ---- WOFF2 ------------------------------------------------------------------------------------------------------------------
namespace {

constexpr std::uint32_t TagOf(const char (&s)[5]) {
    return std::uint32_t(std::uint8_t(s[0])) << 24 | std::uint32_t(std::uint8_t(s[1])) << 16 | std::uint32_t(std::uint8_t(s[2])) << 8 | std::uint8_t(s[3]);
}
constexpr std::uint32_t kGlyf = TagOf("glyf"), kLoca = TagOf("loca"), kHmtx = TagOf("hmtx"), kHhea = TagOf("hhea"), kHead = TagOf("head");

// The known tags: a table directory entry's flags byte names one of these by its index (63 = the tag follows).
const std::uint32_t kKnownTags[63] = {
    TagOf("cmap"), TagOf("head"), TagOf("hhea"), TagOf("hmtx"), TagOf("maxp"), TagOf("name"), TagOf("OS/2"), TagOf("post"),
    TagOf("cvt "), TagOf("fpgm"), TagOf("glyf"), TagOf("loca"), TagOf("prep"), TagOf("CFF "), TagOf("VORG"), TagOf("EBDT"),
    TagOf("EBLC"), TagOf("gasp"), TagOf("hdmx"), TagOf("kern"), TagOf("LTSH"), TagOf("PCLT"), TagOf("VDMX"), TagOf("vhea"),
    TagOf("vmtx"), TagOf("BASE"), TagOf("GDEF"), TagOf("GPOS"), TagOf("GSUB"), TagOf("EBSC"), TagOf("JSTF"), TagOf("MATH"),
    TagOf("CBDT"), TagOf("CBLC"), TagOf("COLR"), TagOf("CPAL"), TagOf("SVG "), TagOf("sbix"), TagOf("acnt"), TagOf("avar"),
    TagOf("bdat"), TagOf("bloc"), TagOf("bsln"), TagOf("cvar"), TagOf("fdsc"), TagOf("feat"), TagOf("fmtx"), TagOf("fvar"),
    TagOf("gvar"), TagOf("hsty"), TagOf("just"), TagOf("lcar"), TagOf("mort"), TagOf("morx"), TagOf("opbd"), TagOf("prop"),
    TagOf("trak"), TagOf("Zapf"), TagOf("Silf"), TagOf("Glat"), TagOf("Gloc"), TagOf("Feat"), TagOf("Sill")};

// A bounds-checked reader over one byte range; every read says whether the bytes were there.
struct Reader {
    const std::uint8_t* p;
    std::size_t n, at = 0;
    Reader(const std::uint8_t* data, std::size_t size) : p(data), n(size) {}
    bool U8(std::uint8_t& v) { if (at >= n) return false; v = p[at++]; return true; }
    bool U16(std::uint16_t& v) { if (at > n || n - at < 2) return false; v = Be16(p + at); at += 2; return true; }
    bool U32(std::uint32_t& v) { if (at > n || n - at < 4) return false; v = Be32(p + at); at += 4; return true; }
    bool Take(std::size_t k, const std::uint8_t*& v) { if (at > n || n - at < k) return false; v = p + at; at += k; return true; }
    // UIntBase128: big-endian groups of 7 bits, the top bit says another byte follows; at most 5 bytes, no leading zero group,
    // no value past 32 bits
    bool Base128(std::uint32_t& v) {
        v = 0;
        for (int i = 0; i < 5; ++i) {
            std::uint8_t b;
            if (!U8(b)) return false;
            if (i == 0 && b == 0x80) return false;
            if (v & 0xFE000000u) return false;
            v = (v << 7) | (b & 0x7Fu);
            if (!(b & 0x80)) return true;
        }
        return false;
    }
    // 255UInt16: one byte below 253 is the value; 253 = a 16-bit value follows; 255 = one byte + 253; 254 = one byte + 506
    bool Var255(std::uint16_t& v) {
        std::uint8_t c;
        if (!U8(c)) return false;
        if (c == 253) return U16(v);
        if (c == 255 || c == 254) {
            std::uint8_t b;
            if (!U8(b)) return false;
            v = std::uint16_t(b + (c == 255 ? 253 : 506));
            return true;
        }
        v = c;
        return true;
    }
};

void Push16(std::vector<std::uint8_t>& b, std::uint32_t v) { b.push_back(std::uint8_t(v >> 8)); b.push_back(std::uint8_t(v)); }

bool BitSet(const std::uint8_t* bitmap, std::size_t i) { return (bitmap[i >> 3] >> (7 - (i & 7))) & 1u; }

// One point of a transformed simple glyph: the flag byte's low 7 bits choose how many data bytes follow and how they split
// into the x and y deltas; bit 0 (and bit 1 when both are coded) is the sign, set = positive; bit 7 set = off-curve.
bool ReadTriplet(std::uint8_t flag, Reader& r, int& dx, int& dy) {
    const int b = flag & 0x7F;
    auto sign = [](int bit, int v) { return bit ? v : -v; };
    const int sx = flag & 1, sy = (flag >> 1) & 1;
    std::uint8_t a = 0, c = 0, e = 0, f = 0;
    if (b < 84) {
        if (!r.U8(a)) return false;
        if (b < 10) { dx = 0; dy = sign(sx, ((b & 14) << 7) + a); }                         // y only, 8 bits + 0..1024
        else if (b < 20) { dx = sign(sx, (((b - 10) & 14) << 7) + a); dy = 0; }             // x only
        else {                                                                              // 4 + 4 bits, + 1..49
            const int k = b - 20;
            dx = sign(sx, 1 + (k & 0x30) + (a >> 4));
            dy = sign(sy, 1 + ((k & 0x0C) << 2) + (a & 0x0F));
        }
        return true;
    }
    if (b < 120) {                                                                          // 8 + 8 bits, + 1, 257 or 513
        if (!r.U8(a) || !r.U8(c)) return false;
        const int k = b - 84;
        dx = sign(sx, 1 + ((k / 12) << 8) + a);
        dy = sign(sy, 1 + (((k % 12) >> 2) << 8) + c);
        return true;
    }
    if (b < 124) {                                                                          // 12 + 12 bits
        if (!r.U8(a) || !r.U8(c) || !r.U8(e)) return false;
        dx = sign(sx, (a << 4) | (c >> 4));
        dy = sign(sy, ((c & 0x0F) << 8) | e);
        return true;
    }
    if (!r.U8(a) || !r.U8(c) || !r.U8(e) || !r.U8(f)) return false;                         // 16 + 16 bits
    dx = sign(sx, (a << 8) | c);
    dy = sign(sy, (e << 8) | f);
    return true;
}

// The transformed glyf table rebuilt into glyf and loca (and every glyph's xMin, for a transformed hmtx).
bool RebuildGlyf(const std::uint8_t* t, std::size_t n, std::size_t cap, std::vector<std::uint8_t>& glyf, std::vector<std::uint8_t>& loca,
                 std::vector<std::int16_t>& xMins, std::string& why) {
    if (n < 36) { why = "WOFF2 glyf header cut off"; return false; }
    const std::uint16_t options = Be16(t + 2), numGlyphs = Be16(t + 4), indexFormat = Be16(t + 6);
    if (indexFormat > 1) { why = "WOFF2 glyf index format out of range"; return false; }
    std::size_t at = 36;
    Reader s[7] = {Reader(t, 0), Reader(t, 0), Reader(t, 0), Reader(t, 0), Reader(t, 0), Reader(t, 0), Reader(t, 0)};   // nContour, nPoints, flag, glyph, composite, bbox, instruction
    for (int k = 0; k < 7; ++k) {
        const std::uint32_t len = Be32(t + 8 + 4 * k);
        if (len > n - at) { why = "WOFF2 glyf streams run past the table"; return false; }
        s[k] = Reader(t + at, len);
        at += len;
    }
    const std::uint8_t* overlap = nullptr;
    if (options & 1) {
        if ((numGlyphs + 7u) / 8u > n - at) { why = "WOFF2 glyf overlap bitmap cut off"; return false; }
        overlap = t + at;
    }
    Reader& nContours = s[0]; Reader& nPoints = s[1]; Reader& flags = s[2]; Reader& glyphs = s[3]; Reader& composite = s[4];
    Reader& bbox = s[5]; Reader& instructions = s[6];
    const std::uint8_t* bboxBitmap;
    if (!bbox.Take(4u * ((numGlyphs + 31u) / 32u), bboxBitmap)) { why = "WOFF2 glyf bbox bitmap cut off"; return false; }
    if (nContours.n < 2u * numGlyphs) { why = "WOFF2 glyf contour stream cut off"; return false; }

    glyf.clear();
    loca.clear();
    xMins.assign(numGlyphs, 0);
    std::vector<std::uint32_t> offsets(numGlyphs + 1u, 0);
    std::vector<std::uint16_t> endPts;
    std::vector<std::uint8_t> pointFlags, xs, ys, packed;
    std::vector<int> px, py;
    auto bad = [&](const char* reason) { why = reason; glyf.clear(); loca.clear(); return false; };
    for (std::uint32_t g = 0; g < numGlyphs; ++g) {
        offsets[g] = std::uint32_t(glyf.size());
        std::uint16_t raw;
        nContours.U16(raw);
        const std::int16_t nc = std::int16_t(raw);
        const bool hasBox = BitSet(bboxBitmap, g);
        std::int16_t box[4] = {0, 0, 0, 0};
        auto readBox = [&] {
            for (std::int16_t& v : box) { std::uint16_t u; if (!bbox.U16(u)) return false; v = std::int16_t(u); }
            return true;
        };
        if (nc == 0) {
            if (hasBox) return bad("WOFF2 empty glyph with a bounding box");
            continue;
        }
        if (nc < 0) {
            if (nc != -1) return bad("WOFF2 glyph contour count out of range");
            if (!hasBox || !readBox()) return bad("WOFF2 composite glyph without its bounding box");
            Push16(glyf, 0xFFFFu);
            for (std::int16_t v : box) Push16(glyf, std::uint16_t(v));
            bool instr = false;
            for (std::uint16_t cf = 0x20; cf & 0x20;) {
                const std::size_t from = composite.at;
                std::uint16_t glyphIndex;
                if (!composite.U16(cf) || !composite.U16(glyphIndex)) return bad("WOFF2 composite stream cut off");
                const std::size_t more = ((cf & 0x0001) ? 4u : 2u) + ((cf & 0x0008) ? 2u : (cf & 0x0040) ? 4u : (cf & 0x0080) ? 8u : 0u);
                const std::uint8_t* rest;
                if (!composite.Take(more, rest)) return bad("WOFF2 composite stream cut off");
                glyf.insert(glyf.end(), composite.p + from, composite.p + composite.at);
                if (cf & 0x0100) instr = true;
            }
            if (instr) {
                std::uint16_t len;
                const std::uint8_t* code;
                if (!glyphs.Var255(len) || !instructions.Take(len, code)) return bad("WOFF2 glyph instructions cut off");
                Push16(glyf, len);
                glyf.insert(glyf.end(), code, code + len);
            }
            xMins[g] = box[0];
        } else {
            endPts.clear();
            std::uint32_t points = 0;
            for (int c = 0; c < nc; ++c) {
                std::uint16_t k;
                if (!nPoints.Var255(k)) return bad("WOFF2 point count stream cut off");
                if (k == 0) return bad("WOFF2 contour without points");
                points += k;
                if (points > 65535u) return bad("WOFF2 glyph has too many points");
                endPts.push_back(std::uint16_t(points - 1));
            }
            const std::uint8_t* fl;
            if (!flags.Take(points, fl)) return bad("WOFF2 flag stream cut off");
            px.resize(points);
            py.resize(points);
            int x = 0, y = 0;
            pointFlags.clear(); xs.clear(); ys.clear();
            for (std::uint32_t i = 0; i < points; ++i) {
                int dx, dy;
                if (!ReadTriplet(fl[i], glyphs, dx, dy)) return bad("WOFF2 glyph stream cut off");
                x += dx;
                y += dy;
                if (x < -32768 || x > 32767 || y < -32768 || y > 32767) return bad("WOFF2 glyph point out of range");
                px[i] = x;
                py[i] = y;
                std::uint8_t f = (fl[i] & 0x80) ? 0 : 0x01;   // on-curve
                if (i == 0 && overlap && BitSet(overlap, g)) f |= 0x40;
                if (dx == 0) f |= 0x10;
                else if (dx >= -255 && dx <= 255) { f |= 0x02; if (dx > 0) f |= 0x10; xs.push_back(std::uint8_t(dx < 0 ? -dx : dx)); }
                else Push16(xs, std::uint16_t(std::int16_t(dx)));
                if (dy == 0) f |= 0x20;
                else if (dy >= -255 && dy <= 255) { f |= 0x04; if (dy > 0) f |= 0x20; ys.push_back(std::uint8_t(dy < 0 ? -dy : dy)); }
                else Push16(ys, std::uint16_t(std::int16_t(dy)));
                pointFlags.push_back(f);
            }
            std::uint16_t len;
            const std::uint8_t* code;
            if (!glyphs.Var255(len) || !instructions.Take(len, code)) return bad("WOFF2 glyph instructions cut off");
            if (hasBox) { if (!readBox()) return bad("WOFF2 bbox stream cut off"); }
            else {
                box[0] = box[2] = std::int16_t(px[0]);
                box[1] = box[3] = std::int16_t(py[0]);
                for (std::uint32_t i = 1; i < points; ++i) {
                    box[0] = std::min<std::int16_t>(box[0], std::int16_t(px[i])); box[2] = std::max<std::int16_t>(box[2], std::int16_t(px[i]));
                    box[1] = std::min<std::int16_t>(box[1], std::int16_t(py[i])); box[3] = std::max<std::int16_t>(box[3], std::int16_t(py[i]));
                }
            }
            Push16(glyf, std::uint16_t(nc));
            for (std::int16_t v : box) Push16(glyf, std::uint16_t(v));
            for (std::uint16_t e : endPts) Push16(glyf, e);
            Push16(glyf, len);
            glyf.insert(glyf.end(), code, code + len);
            // flags, a run of equal flags written once with the repeat bit (0x08) and a count
            for (std::size_t i = 0; i < pointFlags.size();) {
                std::size_t j = i + 1;
                while (j < pointFlags.size() && pointFlags[j] == pointFlags[i] && j - i < 256) ++j;
                if (j - i > 2) { glyf.push_back(std::uint8_t(pointFlags[i] | 0x08)); glyf.push_back(std::uint8_t(j - i - 1)); }
                else for (std::size_t k = i; k < j; ++k) glyf.push_back(pointFlags[k]);
                i = j;
            }
            glyf.insert(glyf.end(), xs.begin(), xs.end());
            glyf.insert(glyf.end(), ys.begin(), ys.end());
            xMins[g] = box[0];
        }
        while (glyf.size() % 4) glyf.push_back(0);
        if (glyf.size() > cap) return bad("WOFF2 glyf table larger than the limit");
    }
    offsets[numGlyphs] = std::uint32_t(glyf.size());
    if (indexFormat == 0 && glyf.size() > 2u * 65535u) return bad("WOFF2 glyf too big for short loca offsets");
    for (std::uint32_t o : offsets) {
        if (indexFormat == 0) Push16(loca, o / 2);
        else { Push16(loca, o >> 16); Push16(loca, o & 0xFFFFu); }
    }
    return true;
}

std::uint32_t TableSum(const std::uint8_t* p, std::size_t n) {   // the OpenType table checksum: big-endian 32-bit words, zero padded
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < n; i += 4) {
        std::uint32_t w = 0;
        for (std::size_t k = 0; k < 4; ++k) w = (w << 8) | (i + k < n ? p[i + k] : 0u);
        sum += w;
    }
    return sum;
}

struct W2Table {
    std::uint32_t tag = 0, origLength = 0, streamLength = 0, streamOffset = 0;
    std::uint8_t version = 0;
    bool transformed = false;
    std::vector<std::uint8_t> rebuilt;   // glyf, loca, hmtx when transformed
    bool done = false;                   // rebuilt is filled
    const std::uint8_t* data = nullptr;  // the final bytes
    std::size_t size = 0;
    std::uint32_t outOffset = 0, checksum = 0;
};

struct W2Font { std::uint32_t flavor = 0; std::vector<std::uint16_t> tables; };

bool DecodeWoff2(const std::uint8_t* d, std::size_t n, std::vector<std::uint8_t>& out, std::string& why, const WebFontLimits& lim) {
    out.clear();
    // the 48-byte header
    if (n < 48) { why = "WOFF2 header cut off"; return false; }
    const std::uint32_t flavor = Be32(d + 4), length = Be32(d + 8);
    const std::uint16_t numTables = Be16(d + 12), reserved = Be16(d + 14);
    const std::uint32_t compressedSize = Be32(d + 20);
    if (length != n) { why = "WOFF2 length field does not match the file"; return false; }
    if (reserved != 0) { why = "WOFF2 reserved field is not 0"; return false; }
    if (numTables == 0 || numTables > lim.maxTables) { why = "WOFF2 table count out of range"; return false; }
    // the table directory
    Reader r(d, n);
    r.at = 48;
    std::vector<W2Table> tables(numTables);
    std::uint64_t streamTotal = 0;
    for (W2Table& t : tables) {
        std::uint8_t fl;
        if (!r.U8(fl)) { why = "WOFF2 table directory cut off"; return false; }
        if ((fl & 63) == 63) { if (!r.U32(t.tag)) { why = "WOFF2 table directory cut off"; return false; } }
        else t.tag = kKnownTags[fl & 63];
        t.version = std::uint8_t(fl >> 6);
        if (!r.Base128(t.origLength)) { why = "WOFF2 table length is not a valid UIntBase128"; return false; }
        // glyf and loca: version 0 is the transform, 3 none; hmtx: 1 is the transform, 0 none; every other table: 0 only
        if (t.tag == kGlyf || t.tag == kLoca) {
            if (t.version != 0 && t.version != 3) { why = "WOFF2 glyf / loca transform version not known"; return false; }
            t.transformed = t.version == 0;
        } else if (t.tag == kHmtx) {
            if (t.version > 1) { why = "WOFF2 hmtx transform version not known"; return false; }
            t.transformed = t.version == 1;
        } else if (t.version != 0) { why = "WOFF2 transform on a table that has none"; return false; }
        t.streamLength = t.origLength;
        if (t.transformed && !r.Base128(t.streamLength)) { why = "WOFF2 transform length is not a valid UIntBase128"; return false; }
        if (t.tag == kLoca && t.transformed && t.streamLength != 0) { why = "WOFF2 transformed loca is not empty"; return false; }
        if (t.origLength > lim.maxSfntBytes) { why = "WOFF2 table larger than the limit"; return false; }
        t.streamOffset = std::uint32_t(streamTotal);
        streamTotal += t.streamLength;
        if (streamTotal > lim.maxSfntBytes) { why = "WOFF2 tables larger than the limit"; return false; }
    }
    // the collection directory, or one font with every table
    std::vector<W2Font> fonts;
    std::uint32_t ttcVersion = 0;
    if (flavor == TagOf("ttcf")) {
        std::uint16_t numFonts;
        if (!r.U32(ttcVersion) || !r.Var255(numFonts)) { why = "WOFF2 collection directory cut off"; return false; }
        if (ttcVersion != 0x00010000u && ttcVersion != 0x00020000u) { why = "WOFF2 collection version not known"; return false; }
        if (numFonts == 0 || numFonts > lim.maxTables) { why = "WOFF2 collection font count out of range"; return false; }
        fonts.resize(numFonts);
        for (W2Font& f : fonts) {
            std::uint16_t count;
            if (!r.Var255(count) || !r.U32(f.flavor)) { why = "WOFF2 collection directory cut off"; return false; }
            if (count == 0 || count > numTables) { why = "WOFF2 collection font table count out of range"; return false; }
            f.tables.resize(count);
            for (std::uint16_t& i : f.tables)
                if (!r.Var255(i) || i >= numTables) { why = "WOFF2 collection table index out of range"; return false; }
        }
    } else {
        fonts.resize(1);
        fonts[0].flavor = flavor;
        for (std::uint16_t i = 0; i < numTables; ++i) fonts[0].tables.push_back(i);
    }
    for (const W2Font& f : fonts) {   // one table per tag in each font
        std::vector<std::uint32_t> tags;
        for (std::uint16_t i : f.tables) tags.push_back(tables[i].tag);
        std::sort(tags.begin(), tags.end());
        if (std::adjacent_find(tags.begin(), tags.end()) != tags.end()) { why = "WOFF2 font has a table twice"; return false; }
    }
    // the one Brotli stream with every table, in directory order
    if (compressedSize > n - r.at) { why = "WOFF2 compressed data runs past the file"; return false; }
    std::vector<std::uint8_t> stream;
    okay::brotli::Options op;
    op.maxOutput = std::size_t(streamTotal);
    op.allowTrailing = true;
    std::string err;
    if (!okay::brotli::Decode(d + r.at, compressedSize, stream, op, err) || stream.size() != streamTotal) {
        why = "WOFF2 table data does not decompress to its size" + (err.empty() ? std::string() : ": " + err);
        return false;
    }
    for (W2Table& t : tables)
        if (!t.transformed) { t.data = stream.data() + t.streamOffset; t.size = t.origLength; t.done = true; }

    // the transforms, font by font (a collection's fonts may share tables: each is rebuilt once)
    for (const W2Font& f : fonts) {
        int glyf = -1, loca = -1, hmtx = -1, hhea = -1;
        for (std::uint16_t i : f.tables) {
            const std::uint32_t tag = tables[i].tag;
            if (tag == kGlyf) glyf = i;
            else if (tag == kLoca) loca = i;
            else if (tag == kHmtx) hmtx = i;
            else if (tag == kHhea) hhea = i;
        }
        const bool glyfT = glyf >= 0 && tables[std::size_t(glyf)].transformed, locaT = loca >= 0 && tables[std::size_t(loca)].transformed;
        if (glyfT != locaT) { why = "WOFF2 glyf and loca must both be transformed or both not"; return false; }
        std::vector<std::int16_t> xMins;
        std::uint16_t numGlyphs = 0;
        if (glyfT) {
            W2Table& g = tables[std::size_t(glyf)];
            W2Table& l = tables[std::size_t(loca)];
            const std::uint8_t* src = stream.data() + g.streamOffset;
            if (g.streamLength >= 6) numGlyphs = Be16(src + 4);
            if (!g.done || (hmtx >= 0 && tables[std::size_t(hmtx)].transformed && !tables[std::size_t(hmtx)].done)) {
                // (into new vectors: a glyf shared by an earlier font of a collection is rebuilt again only for its xMin
                // values, and its bytes stay where they are)
                std::vector<std::uint8_t> glyfBytes, locaBytes;
                if (!RebuildGlyf(src, g.streamLength, lim.maxSfntBytes, glyfBytes, locaBytes, xMins, why)) return false;
                if (!g.done) {
                    if (locaBytes.size() != l.origLength) { why = "WOFF2 loca length does not match the glyph count"; return false; }
                    g.rebuilt.swap(glyfBytes);
                    l.rebuilt.swap(locaBytes);
                    g.data = g.rebuilt.data(); g.size = g.rebuilt.size(); g.done = true;
                    l.data = l.rebuilt.data(); l.size = l.rebuilt.size(); l.done = true;
                }
            }
        }
        if (hmtx >= 0 && tables[std::size_t(hmtx)].transformed && !tables[std::size_t(hmtx)].done) {
            W2Table& h = tables[std::size_t(hmtx)];
            if (!glyfT) { why = "WOFF2 transformed hmtx needs a transformed glyf"; return false; }
            if (hhea < 0 || tables[std::size_t(hhea)].size < 36) { why = "WOFF2 transformed hmtx needs hhea"; return false; }
            const std::uint16_t numHMetrics = Be16(tables[std::size_t(hhea)].data + 34);
            if (numHMetrics == 0 || numHMetrics > numGlyphs) { why = "WOFF2 hhea metric count out of range"; return false; }
            Reader hr(stream.data() + h.streamOffset, h.streamLength);
            std::uint8_t hf;
            if (!hr.U8(hf) || (hf & 0xFC)) { why = "WOFF2 hmtx transform flags not known"; return false; }
            const std::uint8_t *adv, *lsb = nullptr, *lsb2 = nullptr;
            if (!hr.Take(2u * numHMetrics, adv) || (!(hf & 1) && !hr.Take(2u * numHMetrics, lsb)) ||
                (!(hf & 2) && !hr.Take(2u * (numGlyphs - numHMetrics), lsb2))) {
                why = "WOFF2 hmtx data cut off"; return false;
            }
            h.rebuilt.clear();
            for (std::uint32_t i = 0; i < numGlyphs; ++i) {
                if (i < numHMetrics) {
                    h.rebuilt.push_back(adv[2 * i]); h.rebuilt.push_back(adv[2 * i + 1]);
                    if (lsb) { h.rebuilt.push_back(lsb[2 * i]); h.rebuilt.push_back(lsb[2 * i + 1]); }
                    else Push16(h.rebuilt, std::uint16_t(xMins[i]));
                } else if (lsb2) {
                    const std::size_t k = 2 * (i - numHMetrics);
                    h.rebuilt.push_back(lsb2[k]); h.rebuilt.push_back(lsb2[k + 1]);
                } else Push16(h.rebuilt, std::uint16_t(xMins[i]));
            }
            if (h.rebuilt.size() != h.origLength) { why = "WOFF2 hmtx length does not match the metrics"; return false; }
            h.data = h.rebuilt.data(); h.size = h.rebuilt.size(); h.done = true;
        }
    }
    for (const W2Table& t : tables)
        if (!t.done) { why = "WOFF2 transformed table outside any font"; return false; }

    // the sfnt (or TTC): headers, then every table once, 4-byte aligned, in directory order
    const bool ttc = ttcVersion != 0;
    std::uint64_t at = ttc ? 12u + 4u * fonts.size() + (ttcVersion == 0x00020000u ? 12u : 0u) : 0u;
    std::vector<std::uint32_t> fontAt;
    for (const W2Font& f : fonts) { fontAt.push_back(std::uint32_t(at)); at += 12u + 16u * f.tables.size(); }
    for (W2Table& t : tables) {
        at = (at + 3u) & ~std::uint64_t(3);
        t.outOffset = std::uint32_t(at);
        at += t.size;
        if (at > lim.maxSfntBytes) { why = "WOFF2 font larger than the limit"; return false; }
    }
    at = (at + 3u) & ~std::uint64_t(3);
    if (at > lim.maxSfntBytes) { why = "WOFF2 font larger than the limit"; return false; }
    out.assign(std::size_t(at), 0);
    std::uint8_t* o = out.data();
    for (W2Table& t : tables) {
        if (t.size) std::copy(t.data, t.data + t.size, o + t.outOffset);
        if (t.tag == kHead && t.size >= 12) Put32(o + t.outOffset + 8, 0);   // checkSumAdjustment is worked out below
        t.checksum = TableSum(o + t.outOffset, t.size);
    }
    if (ttc) {
        Put32(o, TagOf("ttcf"));
        Put32(o + 4, ttcVersion);
        Put32(o + 8, std::uint32_t(fonts.size()));
        for (std::size_t k = 0; k < fonts.size(); ++k) Put32(o + 12 + 4 * k, fontAt[k]);
    }
    std::vector<bool> headDone(tables.size(), false);
    for (std::size_t k = 0; k < fonts.size(); ++k) {
        const W2Font& f = fonts[k];
        std::uint8_t* h = o + fontAt[k];
        const std::uint16_t count = std::uint16_t(f.tables.size());
        Put32(h, f.flavor);
        Put16(h + 4, count);
        std::uint16_t pow2 = 1, log2 = 0;
        while ((std::uint32_t)pow2 * 2u <= count) { pow2 = (std::uint16_t)(pow2 * 2); ++log2; }
        Put16(h + 6, (std::uint16_t)(pow2 * 16));
        Put16(h + 8, log2);
        Put16(h + 10, (std::uint16_t)(count * 16 - pow2 * 16));
        std::vector<std::uint16_t> sorted(f.tables);
        std::sort(sorted.begin(), sorted.end(), [&](std::uint16_t a, std::uint16_t b) { return tables[a].tag < tables[b].tag; });
        int head = -1;
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            const W2Table& t = tables[sorted[i]];
            std::uint8_t* rec = h + 12 + 16 * i;
            Put32(rec, t.tag); Put32(rec + 4, t.checksum); Put32(rec + 8, t.outOffset); Put32(rec + 12, std::uint32_t(t.size));
            if (t.tag == kHead) head = sorted[i];
        }
        // head.checkSumAdjustment: 0xB1B0AFBA minus the sum of the whole font (its offset table and its tables); a head shared
        // by several fonts of a collection gets the first font's value
        if (head >= 0 && tables[std::size_t(head)].size >= 12 && !headDone[std::size_t(head)]) {
            std::uint32_t sum = TableSum(h, 12u + 16u * count);
            for (std::uint16_t i : f.tables) sum += tables[i].checksum;
            Put32(o + tables[std::size_t(head)].outOffset + 8, 0xB1B0AFBAu - sum);
            headDone[std::size_t(head)] = true;
        }
    }
    return true;
}

} // namespace

} // namespace okay::browser
