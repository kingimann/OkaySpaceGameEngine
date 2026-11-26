// okay::brotli (okay/Core/Brotli.hpp): the engine's Brotli decoder, written from the text of RFC 7932 only. Section numbers
// below are the RFC's. See the header for provenance and limits.
#include "okay/Core/Brotli.hpp"

#include <cstring>

namespace okay::brotli {

namespace detail {

#include "BrotliDictionary.inc"   // kBrotliDictionary (Appendix A) and kBrotliTransforms (Appendix B), generated from the RFC

namespace {

// ---- section 5 and 6: length codes, worked out from the printed tables' rule ---------------------------------------------
// Each code covers 2^extra lengths, and the next code starts where the previous one ends, so the bases follow from the
// extra-bit counts and the first base. The extra-bit counts grow in the pattern the tables show.
struct LengthTables {
    std::uint32_t insBase[24], copyBase[24], blkBase[26];
    std::uint8_t insExtra[24], copyExtra[24], blkExtra[26];
    constexpr LengthTables() : insBase{}, copyBase{}, blkBase{}, insExtra{}, copyExtra{}, blkExtra{} {
        for (int c = 0; c < 24; ++c) {
            // insert: six codes without extra bits, then two codes per count 1..5, then 6..10, 12, 14 and 24
            insExtra[c] = std::uint8_t(c < 6 ? 0 : c < 16 ? (c - 4) / 2 : c < 21 ? c - 10 : c == 21 ? 12 : c == 22 ? 14 : 24);
            // copy: eight codes without extra bits, then two per count 1..5, then 6..10 and 24
            copyExtra[c] = std::uint8_t(c < 8 ? 0 : c < 18 ? (c - 6) / 2 : c < 23 ? c - 12 : 24);
        }
        insBase[0] = 0;
        copyBase[0] = 2;
        for (int c = 1; c < 24; ++c) {
            insBase[c] = insBase[c - 1] + (std::uint32_t(1) << insExtra[c - 1]);
            copyBase[c] = copyBase[c - 1] + (std::uint32_t(1) << copyExtra[c - 1]);
        }
        // block counts: four codes per count 2..5, then 6, 6, 7..13 and 24
        for (int c = 0; c < 26; ++c) blkExtra[c] = std::uint8_t(c < 16 ? 2 + c / 4 : c < 18 ? 6 : c < 25 ? c - 11 : 24);
        blkBase[0] = 1;
        for (int c = 1; c < 26; ++c) blkBase[c] = blkBase[c - 1] + (std::uint32_t(1) << blkExtra[c - 1]);
    }
};
constexpr LengthTables kLen{};

// The 64-code cells of the insert-and-copy alphabet (section 5): first insert code, first copy code of each cell; the
// first two cells also mean "distance symbol 0".
constexpr std::uint8_t kCellInsert[11] = {0, 0, 0, 0, 8, 8, 0, 16, 8, 16, 16};
constexpr std::uint8_t kCellCopy[11] = {0, 8, 0, 8, 0, 8, 16, 0, 16, 8, 16};

// ---- section 7.1: the literal context lookups, worked out from what the tables group together ----------------------------
constexpr bool IsVowel(int c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'; }
// Lut0 (UTF8 mode, the last byte): ASCII sorted into classes (white space, punctuation kinds, digits, vowels, consonants,
// upper and lower case), continuation bytes 0 / 1 and lead bytes 2 / 3 by their lowest bit.
constexpr std::uint8_t Lut0Of(int c) {
    if (c >= 0xC0) return std::uint8_t(2 + (c & 1));
    if (c >= 0x80) return std::uint8_t(c & 1);
    if (c == '\t' || c == '\n' || c == '\r') return 4;
    if (c < 0x20 || c == 0x7F) return 0;
    if (c == ' ') return 8;
    if (c >= '0' && c <= '9') return 44;
    if (c >= 'A' && c <= 'Z') return IsVowel(c + 32) ? 48 : 52;
    if (c >= 'a' && c <= 'z') return IsVowel(c) ? 56 : 60;
    switch (c) {
    case '"': case '\'': return 16;
    case '%': return 20;
    case '(': case '<': case '[': case '{': return 24;
    case ')': case '>': case ']': case '}': return 28;
    case ',': case ':': case ';': return 32;
    case '.': return 36;
    case '=': return 40;
    default: return 12;
    }
}
// Lut1 (UTF8 mode, the byte before): 0 control, space and continuation bytes, 1 punctuation, 2 digits, upper case and the
// three- and four-byte lead bytes, 3 lower case.
constexpr std::uint8_t Lut1Of(int c) {
    if (c >= 0xE0) return 2;
    if (c >= 0x80 || c <= 0x20 || c == 0x7F) return 0;
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) return 2;
    if (c >= 'a' && c <= 'z') return 3;
    return 1;
}
// Lut2 (Signed mode): the size class of the byte read as a small signed number.
constexpr std::uint8_t Lut2Of(int c) {
    return std::uint8_t(c == 0 ? 0 : c < 16 ? 1 : c < 64 ? 2 : c < 128 ? 3 : c < 192 ? 4 : c < 240 ? 5 : c < 255 ? 6 : 7);
}
struct ContextTables {
    std::uint8_t lut[3][256];
    constexpr ContextTables() : lut{} {
        for (int i = 0; i < 256; ++i) { lut[0][i] = Lut0Of(i); lut[1][i] = Lut1Of(i); lut[2][i] = Lut2Of(i); }
    }
};
constexpr ContextTables kCtx{};

// ---- section 8: words per length ----------------------------------------------------------------------------------------
constexpr std::uint8_t kWordBits[25] = {0, 0, 0, 0, 10, 10, 11, 11, 10, 10, 10, 10, 10, 9, 9, 8, 7, 7, 8, 7, 7, 6, 6, 5, 5};   // Appendix A, NDBITS
struct WordOffsets {
    std::uint32_t at[26];
    constexpr WordOffsets() : at{} {
        for (int l = 0; l < 25; ++l) at[l + 1] = at[l] + (l < 4 ? 0u : std::uint32_t(l) << kWordBits[l]);
    }
};
constexpr WordOffsets kWords{};
static_assert(sizeof(kBrotliDictionary) == 122784, "Appendix A states 122,784 bytes");

// ---- the bit reader (section 1.5.1: bits fill each byte from its least significant end) ----------------------------------
// Bytes past the end of the input read as zero, and Overrun() then says the stream asked for more than there is. Decoding
// stops at the next check, so the zeros never decide anything that is kept.
struct BitIn {
    const std::uint8_t* src;
    std::size_t n;
    std::size_t next = 0;      // index of the next byte to load (past n: a zero byte that does not exist)
    std::uint64_t buf = 0;     // loaded bits, the oldest in bit 0
    int have = 0;              // bits in buf

    BitIn(const std::uint8_t* s, std::size_t len) : src(s), n(len) {}
    void Load() {
        while (have <= 56) {
            buf |= std::uint64_t(next < n ? src[next] : 0) << have;
            ++next;
            have += 8;
        }
    }
    std::uint32_t Get(int k) {   // k <= 24
        if (have < k) Load();
        const std::uint32_t v = std::uint32_t(buf & ((std::uint64_t(1) << k) - 1));
        buf >>= k;
        have -= k;
        return v;
    }
    std::uint32_t Look(int k) {   // the next k bits without taking them (k <= 24)
        if (have < k) Load();
        return std::uint32_t(buf & ((std::uint64_t(1) << k) - 1));
    }
    void Drop(int k) { buf >>= k; have -= k; }
    std::size_t BitsUsed() const { return next * 8 - std::size_t(have); }
    bool Overrun() const { return BitsUsed() > n * 8; }
    /// Moves to the next byte boundary; false when a skipped bit is not zero.
    bool ToByteBoundary() { return Get(int((8 - BitsUsed() % 8) % 8)) == 0; }
    /// At a byte boundary: drops the loaded bytes, so that `next` is the input position.
    void Unload() { next = BitsUsed() / 8; buf = 0; have = 0; }
};

// ---- prefix codes (section 3) ---------------------------------------------------------------------------------------------
// A canonical code is decoded with a direct table indexed by the next `bits` stream bits (at most kRootBits; a code's first
// bit is the stream's first bit, so table indexes are the codes bit-reversed). A longer code is found by walking the
// lengths above the table: the codes of one length are consecutive numbers starting at first[length].
constexpr int kRootBits = 8;
constexpr int kMaxLen = 15;

struct Code {
    std::uint32_t table = 0;   // offset of the direct table in Pools::tables
    std::uint32_t sorted = 0;  // offset of the symbols ordered by (length, symbol) in Pools::symbols
    std::uint8_t bits = 0;     // direct table width; 0 = a code of one symbol that takes no bits
    std::uint8_t longest = 0;
    std::uint16_t lone = 0;    // that symbol
    std::uint16_t first[kMaxLen + 1] = {}, count[kMaxLen + 1] = {}, start[kMaxLen + 1] = {};
};

struct Pools {
    std::vector<std::uint32_t> tables;    // entries: symbol << 4 | length, 0 = a code longer than the table
    std::vector<std::uint16_t> symbols;
    void Clear() { tables.clear(); symbols.clear(); }
};

std::uint32_t Mirror(std::uint32_t v, int width) {   // the low `width` bits of v in the opposite order
    std::uint32_t r = 0;
    for (int i = 0; i < width; ++i) { r = (r << 1) | (v & 1u); v >>= 1; }
    return r;
}

// Builds `c` from code lengths (0 = unused). False unless the lengths make a complete prefix code.
bool BuildCode(const std::uint8_t* lens, int size, Code& c, Pools& p) {
    std::uint32_t kraft = 0;
    int used = 0, only = 0;
    for (int l = 0; l <= kMaxLen; ++l) c.count[l] = 0;
    for (int s = 0; s < size; ++s) {
        if (!lens[s]) continue;
        ++c.count[lens[s]];
        kraft += std::uint32_t(1) << (kMaxLen - lens[s]);
        ++used;
        only = s;
    }
    if (used == 1) { c.bits = 0; c.lone = std::uint16_t(only); return true; }
    if (kraft != (std::uint32_t(1) << kMaxLen)) return false;
    c.longest = 0;
    for (int l = 1; l <= kMaxLen; ++l) if (c.count[l]) c.longest = std::uint8_t(l);
    c.bits = std::uint8_t(c.longest < kRootBits ? c.longest : kRootBits);
    // first code and first sorted slot of each length (section 3.2: shorter codes first, symbols in order within a length)
    std::uint32_t code = 0;
    std::uint16_t slot = 0;
    for (int l = 1; l <= kMaxLen; ++l) {
        code = (code + (l > 1 ? c.count[l - 1] : 0)) << 1;
        c.first[l] = std::uint16_t(code);
        c.start[l] = slot;
        slot = std::uint16_t(slot + c.count[l]);
    }
    c.table = std::uint32_t(p.tables.size());
    c.sorted = std::uint32_t(p.symbols.size());
    p.tables.resize(p.tables.size() + (std::size_t(1) << c.bits), 0);
    p.symbols.resize(p.symbols.size() + std::size_t(used));
    std::uint16_t placed[kMaxLen + 1] = {};
    for (int s = 0; s < size; ++s) {
        const int l = lens[s];
        if (!l) continue;
        const std::uint32_t k = placed[l]++;
        p.symbols[c.sorted + c.start[l] + k] = std::uint16_t(s);
        if (l > c.bits) continue;
        const std::uint32_t entry = std::uint32_t(s) << 4 | std::uint32_t(l);
        for (std::uint32_t i = Mirror(c.first[l] + k, l); i < (std::uint32_t(1) << c.bits); i += std::uint32_t(1) << l)
            p.tables[c.table + i] = entry;
    }
    return true;
}

int ReadSymbol(const Code& c, const Pools& p, BitIn& in) {
    if (!c.bits) return c.lone;
    const std::uint32_t look = in.Look(kMaxLen);
    const std::uint32_t e = p.tables[c.table + (look & ((1u << c.bits) - 1))];
    if (e) { in.Drop(int(e & 15)); return int(e >> 4); }
    const std::uint32_t asCode = Mirror(look, kMaxLen);   // the next 15 bits, first bit highest
    for (int l = c.bits + 1; l <= c.longest; ++l) {
        const std::uint32_t v = asCode >> (kMaxLen - l);
        if (v >= c.first[l] && v - c.first[l] < c.count[l]) { in.Drop(l); return p.symbols[c.sorted + c.start[l] + (v - c.first[l])]; }
    }
    return -1;   // not reached for a complete code
}

int BitsFor(int alphabet) {   // the smallest width that holds every symbol 0..alphabet-1 (section 3.4)
    int w = 0;
    while ((1 << w) < alphabet) ++w;
    return w;
}

// Section 3.5: a code-length code's own length, in the fixed variable-length code (0: 00, 1: 0111, 2: 011, 3: 10, 4: 01,
// 5: 1111, written as stored, read from the right).
int ReadLengthOfLength(BitIn& in) {
    if (!in.Get(1)) return in.Get(1) ? 3 : 0;
    if (!in.Get(1)) return 4;
    if (!in.Get(1)) return 2;
    return in.Get(1) ? 5 : 1;
}

// Reads one prefix code over an alphabet of `size` symbols (section 3.4 simple, 3.5 complex).
bool ReadCode(int size, Code& c, Pools& p, BitIn& in, std::string& why) {
    static std::uint8_t const kStoredOrder[18] = {1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    std::uint8_t lens[704];
    std::memset(lens, 0, std::size_t(size));
    const std::uint32_t hskip = in.Get(2);
    if (hskip == 1) {
        const int nsym = int(in.Get(2)) + 1, width = BitsFor(size);
        int sym[4];
        for (int i = 0; i < nsym; ++i) {
            sym[i] = int(in.Get(width));
            if (sym[i] >= size) { why = "brotli: simple prefix code symbol out of range"; return false; }
            for (int j = 0; j < i; ++j)
                if (sym[j] == sym[i]) { why = "brotli: simple prefix code repeats a symbol"; return false; }
        }
        static const std::uint8_t kShapes[5][4] = {{0, 0, 0, 0}, {1, 1, 0, 0}, {1, 2, 2, 0}, {2, 2, 2, 2}, {1, 2, 3, 3}};
        const int shape = nsym == 4 ? 3 + int(in.Get(1)) : nsym - 1;
        if (nsym == 1) { c.bits = 0; c.lone = std::uint16_t(sym[0]); return true; }
        for (int i = 0; i < nsym; ++i) lens[sym[i]] = kShapes[shape][i];
        if (!BuildCode(lens, size, c, p)) { why = "brotli: bad simple prefix code"; return false; }
        return true;
    }
    // complex: first the lengths of the code-length code, in the stored order, the first `hskip` taken as zero
    std::uint8_t clens[18] = {};
    int space = 32, nonzero = 0;
    for (int i = int(hskip); i < 18 && space > 0; ++i) {
        const int v = ReadLengthOfLength(in);
        clens[kStoredOrder[i]] = std::uint8_t(v);
        if (v) { space -= 32 >> v; ++nonzero; }
    }
    if (nonzero == 0 || (nonzero > 1 && space != 0)) { why = "brotli: bad code-length code"; return false; }
    Code lc;
    Pools lp;
    if (!BuildCode(clens, 18, lc, lp)) { why = "brotli: bad code-length code"; return false; }
    // then the symbols' lengths, with the repeat codes 16 (the last non-zero length) and 17 (zeros)
    int prev = 8, runCode = -1, run = 0, total = 0, at = 0;
    const int kFull = 32768;
    while (at < size && total < kFull) {
        const int s = ReadSymbol(lc, lp, in);
        if (s < 0 || in.Overrun()) { why = "brotli: prefix code cut off"; return false; }
        if (s < 16) {
            lens[at++] = std::uint8_t(s);
            if (s) { prev = s; total += kFull >> s; }
            runCode = -1;
            run = 0;
            continue;
        }
        const int extraBits = s == 16 ? 2 : 3;
        const int before = runCode == s ? run : 0;
        const int now = (before ? (before - 2) << extraBits : 0) + 3 + int(in.Get(extraBits));
        const int add = now - before;
        if (add > size - at) { why = "brotli: code length repeat runs past the alphabet"; return false; }
        const int value = s == 16 ? prev : 0;
        for (int k = 0; k < add; ++k) lens[at++] = std::uint8_t(value);
        if (value) total += add * (kFull >> value);
        runCode = s;
        run = now;
    }
    if (total != kFull) { why = "brotli: prefix code is not complete"; return false; }
    if (!BuildCode(lens, size, c, p)) { why = "brotli: bad prefix code"; return false; }
    return true;
}

// Section 9.2: the 1..11-bit count of block types or of trees (1..256).
std::uint32_t ReadCount256(BitIn& in) {
    if (!in.Get(1)) return 1;
    const int n = int(in.Get(3));
    if (n == 0) return 2;
    return (std::uint32_t(1) << n) + in.Get(n) + 1;
}

// ---- block switching (section 6) -----------------------------------------------------------------------------------------
struct Blocks {
    std::uint32_t types = 1, type = 0, previous = 1, left = 0;
    Code typeCode, countCode;
};

bool ReadBlockCount(Blocks& b, const Pools& p, BitIn& in, std::uint32_t& out) {
    const int s = ReadSymbol(b.countCode, p, in);
    if (s < 0 || s > 25) return false;
    out = kLen.blkBase[s] + in.Get(kLen.blkExtra[s]);
    return true;
}

bool ReadBlockSetup(Blocks& b, Pools& p, BitIn& in, std::string& why) {
    b.types = ReadCount256(in);
    b.type = 0;
    b.previous = 1;
    if (b.types < 2) { b.left = 16777216; return true; }
    if (!ReadCode(int(b.types) + 2, b.typeCode, p, in, why) || !ReadCode(26, b.countCode, p, in, why)) return false;
    if (!ReadBlockCount(b, p, in, b.left)) { why = "brotli: bad block count"; return false; }
    return true;
}

// Called before each element of the category: switches block when the current one is used up.
bool NextInBlock(Blocks& b, const Pools& p, BitIn& in) {
    if (b.types < 2) return true;
    if (b.left == 0) {
        const int s = ReadSymbol(b.typeCode, p, in);
        if (s < 0) return false;
        std::uint32_t t;
        if (s == 0) t = b.previous;
        else if (s == 1) t = b.type + 1 >= b.types ? 0 : b.type + 1;
        else t = std::uint32_t(s - 2);
        if (t >= b.types) return false;
        b.previous = b.type;
        b.type = t;
        if (!ReadBlockCount(b, p, in, b.left)) return false;
    }
    --b.left;
    return true;
}

// ---- context maps (section 7.3) ------------------------------------------------------------------------------------------
bool ReadContextMap(std::uint32_t trees, std::vector<std::uint8_t>& map, std::size_t size, Pools& p, BitIn& in, std::string& why) {
    map.assign(size, 0);
    if (trees < 2) return true;
    const int rleMax = in.Get(1) ? int(in.Get(4)) + 1 : 0;
    Code c;
    if (!ReadCode(int(trees) + rleMax, c, p, in, why)) return false;
    for (std::size_t i = 0; i < size;) {
        const int s = ReadSymbol(c, p, in);
        if (s < 0 || in.Overrun()) { why = "brotli: context map cut off"; return false; }
        if (s == 0) { map[i++] = 0; continue; }
        if (s <= rleMax) {
            const std::size_t zeros = (std::size_t(1) << s) + in.Get(s);
            if (zeros > size - i) { why = "brotli: context map run is too long"; return false; }
            i += zeros;   // (already zero)
            continue;
        }
        map[i++] = std::uint8_t(s - rleMax);
    }
    if (in.Get(1)) {
        // inverse move-to-front: each value is a position in a list of 0..255 that starts in order; the value found there is
        // the result and moves to the front of the list
        std::uint8_t order[256];
        for (int k = 0; k < 256; ++k) order[k] = std::uint8_t(k);
        for (std::uint8_t& v : map) {
            const std::uint8_t pos = v, value = order[pos];
            std::memmove(order + 1, order, pos);
            order[0] = value;
            v = value;
        }
    }
    for (std::uint8_t v : map)
        if (v >= trees) { why = "brotli: context map names a missing tree"; return false; }
    return true;
}

std::uint8_t LiteralContext(int mode, std::uint8_t p1, std::uint8_t p2) {
    switch (mode) {
    case 0: return std::uint8_t(p1 & 0x3F);                                   // LSB6
    case 1: return std::uint8_t(p1 >> 2);                                     // MSB6
    case 2: return std::uint8_t(kCtx.lut[0][p1] | kCtx.lut[1][p2]);          // UTF8
    default: return std::uint8_t(kCtx.lut[2][p1] << 3 | kCtx.lut[2][p2]);    // Signed
    }
}

// Ferment (section 8): changes the case of the character that starts at w[i] (ASCII letters; for a two-byte UTF-8 sequence
// the second byte's bit 5, for a longer one the third byte's bits 0 and 2), and returns how many bytes it covered.
int FermentAt(std::uint8_t* w, int n, int i) {
    const std::uint8_t lead = w[i];
    if (lead < 0xC0) {
        if (lead >= 'a' && lead <= 'z') w[i] = std::uint8_t(lead ^ 0x20);
        return 1;
    }
    if (lead < 0xE0) {
        if (i + 1 < n) w[i + 1] = std::uint8_t(w[i + 1] ^ 0x20);
        return 2;
    }
    if (i + 2 < n) w[i + 2] = std::uint8_t(w[i + 2] ^ 0x05);
    return 3;
}

} // namespace

const std::uint8_t* Dictionary() { return kBrotliDictionary; }
std::size_t DictionarySize() { return sizeof(kBrotliDictionary); }
int WordBits(int length) { return length >= 0 && length <= 24 ? kWordBits[length] : 0; }
std::uint32_t WordOffset(int length) { return length >= 0 && length <= 25 ? kWords.at[length] : 0; }
TransformInfo Transform(int id) {
    const BrotliTransformRow& r = kBrotliTransforms[id < 0 || id > 120 ? 0 : id];
    return {r.prefix, r.kind, r.suffix};
}
std::uint8_t ContextLut(int table, int i) { return kCtx.lut[table < 0 ? 0 : table > 2 ? 2 : table][i & 255]; }
std::uint32_t InsertBase(int code) { return kLen.insBase[code]; }
int InsertExtra(int code) { return kLen.insExtra[code]; }
std::uint32_t CopyBase(int code) { return kLen.copyBase[code]; }
int CopyExtra(int code) { return kLen.copyExtra[code]; }
std::uint32_t BlockCountBase(int code) { return kLen.blkBase[code]; }
int BlockCountExtra(int code) { return kLen.blkExtra[code]; }

int TransformWord(const std::uint8_t* word, int length, int id, std::uint8_t* dst) {
    const BrotliTransformRow& r = kBrotliTransforms[id];
    int n = 0;
    for (const char* s = r.prefix; *s; ++s) dst[n++] = std::uint8_t(*s);
    int from = 0, take = length;
    if (r.kind >= 3 && r.kind <= 11) { const int k = r.kind - 2; from = k < length ? k : length; take = length - from; }
    if (r.kind >= 12) { const int k = r.kind - 11; take = k < length ? length - k : 0; }
    std::memcpy(dst + n, word + from, std::size_t(take));
    if (r.kind == 1 && take > 0) FermentAt(dst + n, take, 0);
    if (r.kind == 2) for (int i = 0; i < take;) i += FermentAt(dst + n, take, i);
    n += take;
    for (const char* s = r.suffix; *s; ++s) dst[n++] = std::uint8_t(*s);
    return n;
}

} // namespace detail

// ---- the stream (sections 9 and 10) ----------------------------------------------------------------------------------------
bool Decode(const std::uint8_t* in, std::size_t n, std::vector<std::uint8_t>& out, const Options& op, std::size_t& consumed,
            std::string& why) {
    using namespace detail;
    out.clear();
    consumed = 0;
    auto fail = [&](const char* reason) { why = reason; out.clear(); return false; };
    BitIn bits(in, n);
    if (n == 0) return fail("brotli: empty input");

    // section 9.1: the window size
    int wbits;
    if (!bits.Get(1)) wbits = 16;
    else {
        const int a = int(bits.Get(3));
        if (a) wbits = 17 + a;
        else {
            const int b = int(bits.Get(3));
            if (b == 1) return fail("brotli: large-window streams (window bits above 24) are not RFC 7932");
            wbits = b ? 8 + b : 17;
        }
    }
    const std::size_t window = (std::size_t(1) << wbits) - 16;

    // stream state: the last four distances (section 4), the last two bytes (section 7.1)
    std::uint32_t ring[4] = {16, 15, 11, 4};   // ring[last] is the last distance
    int last = 3;
    std::size_t size = 0;                        // bytes produced
    // A command may produce no bytes (a dictionary word cut away by its transform) and some codes take no bits, so a
    // stream could repeat such commands for ever: each command of that kind counts against this budget.
    std::size_t idleBudget = n * 8 + 64;

    Pools pools;
    std::vector<Code> litCodes, insCodes, distCodes;
    std::vector<std::uint8_t> litMap, distMap, modes;
    Blocks blk[3];   // 0 literals, 1 insert-and-copy, 2 distances

    for (bool isLast = false; !isLast;) {
        // section 9.2: the meta-block header
        isLast = bits.Get(1) != 0;
        if (isLast && bits.Get(1)) break;   // ISLASTEMPTY
        const std::uint32_t mn = bits.Get(2);
        const int nibbles = mn == 3 ? 0 : int(mn) + 4;
        if (nibbles == 0) {
            // metadata: skipped, not part of the output
            if (bits.Get(1)) return fail("brotli: reserved bit set");
            const int skipBytes = int(bits.Get(2));
            std::size_t skip = 0;
            for (int k = 0; k < skipBytes; ++k) {
                const std::uint32_t byte = bits.Get(8);
                if (k == skipBytes - 1 && skipBytes > 1 && byte == 0) return fail("brotli: metadata length has a zero top byte");
                skip |= std::size_t(byte) << (8 * k);
            }
            if (skipBytes) ++skip;
            if (!bits.ToByteBoundary()) return fail("brotli: fill bits are not zero");
            if (bits.Overrun()) return fail("brotli: stream cut off");
            bits.Unload();
            if (skip > n - bits.next) return fail("brotli: metadata runs past the end");
            bits.next += skip;
            continue;
        }
        std::size_t mlen = 0;
        for (int k = 0; k < nibbles; ++k) {
            const std::uint32_t nib = bits.Get(4);
            if (k == nibbles - 1 && nibbles > 4 && nib == 0) return fail("brotli: meta-block length has a zero top nibble");
            mlen |= std::size_t(nib) << (4 * k);
        }
        ++mlen;
        if (bits.Overrun()) return fail("brotli: stream cut off");
        if (mlen > op.maxOutput - size) return fail("brotli: decoded data larger than the limit");
        const bool raw = !isLast && bits.Get(1);
        // room for the meta-block (the cap was checked above; growth is geometric but never past the cap)
        if (out.capacity() < size + mlen) {
            std::size_t want = out.capacity() * 2;
            if (want < size + mlen) want = size + mlen;
            if (want > op.maxOutput) want = op.maxOutput;
            out.reserve(want);
        }
        out.resize(size + mlen);
        std::uint8_t* const o = out.data();
        const std::size_t end = size + mlen;

        if (raw) {
            if (!bits.ToByteBoundary()) return fail("brotli: fill bits are not zero");
            if (bits.Overrun()) return fail("brotli: stream cut off");
            bits.Unload();
            if (mlen > n - bits.next) return fail("brotli: uncompressed meta-block runs past the end");
            std::memcpy(o + size, in + bits.next, mlen);
            bits.next += mlen;
            size = end;
            continue;
        }

        pools.Clear();
        for (Blocks& b : blk)
            if (!ReadBlockSetup(b, pools, bits, why)) { out.clear(); return false; }
        const int npostfix = int(bits.Get(2));
        const std::uint32_t ndirect = bits.Get(4) << npostfix;
        modes.resize(blk[0].types);
        for (std::uint8_t& m : modes) m = std::uint8_t(bits.Get(2));
        const std::uint32_t litTrees = ReadCount256(bits);
        if (!ReadContextMap(litTrees, litMap, std::size_t(64) * blk[0].types, pools, bits, why)) { out.clear(); return false; }
        const std::uint32_t distTrees = ReadCount256(bits);
        if (!ReadContextMap(distTrees, distMap, std::size_t(4) * blk[2].types, pools, bits, why)) { out.clear(); return false; }
        if (bits.Overrun()) return fail("brotli: stream cut off");
        const int distAlphabet = 16 + int(ndirect) + (48 << npostfix);
        litCodes.assign(litTrees, Code());
        insCodes.assign(blk[1].types, Code());
        distCodes.assign(distTrees, Code());
        for (Code& c : litCodes) if (!ReadCode(256, c, pools, bits, why)) { out.clear(); return false; }
        for (Code& c : insCodes) if (!ReadCode(704, c, pools, bits, why)) { out.clear(); return false; }
        for (Code& c : distCodes) if (!ReadCode(distAlphabet, c, pools, bits, why)) { out.clear(); return false; }
        if (bits.Overrun()) return fail("brotli: stream cut off");

        // section 9.3: the commands
        while (size < end) {
            const std::size_t before = size;
            if (!NextInBlock(blk[1], pools, bits)) return fail("brotli: bad block switch");
            const int cmd = ReadSymbol(insCodes[blk[1].type], pools, bits);
            if (cmd < 0) return fail("brotli: bad insert-and-copy code");
            const int cell = cmd >> 6;
            const int insCode = kCellInsert[cell] + ((cmd >> 3) & 7), copyCode = kCellCopy[cell] + (cmd & 7);
            const std::size_t insert = kLen.insBase[insCode] + bits.Get(kLen.insExtra[insCode]);
            const std::size_t copy = kLen.copyBase[copyCode] + bits.Get(kLen.copyExtra[copyCode]);
            if (insert > end - size) return fail("brotli: literals run past the meta-block");
            for (std::size_t k = 0; k < insert; ++k) {
                if (!NextInBlock(blk[0], pools, bits)) return fail("brotli: bad block switch");
                const std::uint8_t p1 = size ? o[size - 1] : 0, p2 = size > 1 ? o[size - 2] : 0;
                const std::uint32_t t = blk[0].type;
                const int lit = ReadSymbol(litCodes[litMap[64 * t + LiteralContext(modes[t], p1, p2)]], pools, bits);
                if (lit < 0) return fail("brotli: bad literal code");
                o[size++] = std::uint8_t(lit);
            }
            if (bits.Overrun()) return fail("brotli: stream cut off");
            if (size == end) break;   // the last command's copy length is ignored

            // the distance (section 4)
            int dcode = 0;
            std::uint64_t dist;
            if (cell < 2) dist = ring[last];
            else {
                if (!NextInBlock(blk[2], pools, bits)) return fail("brotli: bad block switch");
                const std::uint32_t ctx = copy > 4 ? 3 : std::uint32_t(copy - 2);
                dcode = ReadSymbol(distCodes[distMap[4 * blk[2].type + ctx]], pools, bits);
                if (dcode < 0) return fail("brotli: bad distance code");
                if (dcode < 16) {
                    // 0..3: the last four; 4..9 the last, 10..15 the second-to-last, each -1 +1 -2 +2 -3 +3
                    static const int kDelta[6] = {-1, 1, -2, 2, -3, 3};
                    std::int64_t v;
                    if (dcode < 4) v = ring[(last - dcode) & 3];
                    else v = std::int64_t(ring[dcode < 10 ? last : (last - 1) & 3]) + kDelta[(dcode - 4) % 6];
                    if (v <= 0) return fail("brotli: distance is not positive");
                    dist = std::uint64_t(v);
                } else if (std::uint32_t(dcode) < 16 + ndirect) dist = std::uint64_t(dcode - 15);
                else {
                    const std::uint32_t d = std::uint32_t(dcode) - ndirect - 16;
                    const int extraBits = 1 + int(d >> (npostfix + 1));
                    const std::uint64_t high = d >> npostfix, low = d & ((1u << npostfix) - 1);
                    const std::uint64_t offset = ((2 + (high & 1)) << extraBits) - 4;
                    dist = ((offset + bits.Get(extraBits)) << npostfix) + low + ndirect + 1;
                }
            }
            if (bits.Overrun()) return fail("brotli: stream cut off");
            const std::size_t reach = size < window ? size : window;
            if (dist > reach) {
                // section 8: a static dictionary word
                if (copy < 4 || copy > 24) return fail("brotli: dictionary word length out of range");
                const std::uint64_t wordId = dist - reach - 1;
                const int wb = kWordBits[copy];
                const std::uint64_t index = wordId & ((std::uint64_t(1) << wb) - 1), transform = wordId >> wb;
                if (transform > 120) return fail("brotli: dictionary transform out of range");
                std::uint8_t word[64];
                const int len = TransformWord(kBrotliDictionary + kWords.at[copy] + index * copy, int(copy), int(transform), word);
                if (std::size_t(len) > end - size) return fail("brotli: dictionary word runs past the meta-block");
                std::memcpy(o + size, word, std::size_t(len));
                size += std::size_t(len);
            } else {
                if (dcode != 0) { last = (last + 1) & 3; ring[last] = std::uint32_t(dist); }
                if (copy > end - size) return fail("brotli: copy runs past the meta-block");
                std::uint8_t* d = o + size;
                const std::uint8_t* s = d - dist;
                if (dist >= copy) std::memcpy(d, s, copy);
                else for (std::size_t k = 0; k < copy; ++k) d[k] = s[k];   // overlapping: repeats the last `dist` bytes
                size += copy;
            }
            if (size == before && idleBudget-- == 0) return fail("brotli: commands that produce nothing");
        }
    }
    // the end: fill bits of the last byte are zero; nothing (or, if allowed, anything) after it
    if (!bits.ToByteBoundary()) return fail("brotli: fill bits after the last meta-block are not zero");
    if (bits.Overrun()) return fail("brotli: stream cut off");
    consumed = bits.BitsUsed() / 8;
    if (consumed != n && !op.allowTrailing) return fail("brotli: bytes after the end of the stream");
    out.resize(size);
    return true;
}

} // namespace okay::brotli
