// Browser (page engine): web font files. Written from the W3C "WOFF File Format 1.0" recommendation (sections 3 to 6: the
// header, the table directory, the compressed table data, the rebuilt sfnt's table order and 4-byte padding) and the OpenType
// specification's "Organization of an OpenType font" (the offset table and table records, searchRange / entrySelector /
// rangeShift). Inflate is the engine's own (okay/Core/Inflate.hpp). No code from any font library or browser.
#include "okay/Browser/WebFont.hpp"

#include "okay/Core/Inflate.hpp"

#include <algorithm>

namespace okay::browser {

namespace {

std::uint32_t Be32(const std::uint8_t* p) { return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3]; }
std::uint16_t Be16(const std::uint8_t* p) { return (std::uint16_t)((p[0] << 8) | p[1]); }
void Put32(std::uint8_t* p, std::uint32_t v) { p[0] = (std::uint8_t)(v >> 24); p[1] = (std::uint8_t)(v >> 16); p[2] = (std::uint8_t)(v >> 8); p[3] = (std::uint8_t)v; }
void Put16(std::uint8_t* p, std::uint16_t v) { p[0] = (std::uint8_t)(v >> 8); p[1] = (std::uint8_t)v; }

struct Entry { std::uint32_t tag, offset, compLength, origLength, checksum; };

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
    case FontFileKind::Woff2: why = "WOFF2 fonts are not read yet (Brotli)"; return false;
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

} // namespace okay::browser
