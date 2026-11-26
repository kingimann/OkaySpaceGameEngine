#pragma once
// Tests only: a small Brotli WRITER (RFC 7932), so the tests can build streams by hand and know exactly what each one says.
// Written from the RFC's text (section 1.5.1 bit packing, 3.2 canonical codes, 3.4 / 3.5 prefix code formats, 4 distances,
// 5 insert-and-copy codes, 9 the stream and meta-block headers). It does not search for matches: the caller lists the
// commands. Its own tables are typed in from the RFC's printed tables (not taken from the decoder), so a decoder table that
// was worked out wrongly does not agree with them. Used by tests/test_brotli.cpp and tests/test_browser_webfont.cpp.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace brw {

struct BitWriter {
    std::vector<std::uint8_t> bytes;
    int used = 0;   // bits used in the last byte (0 = the next bit starts a new byte)
    void Bit(unsigned b) {
        if (used == 0) bytes.push_back(0);
        if (b & 1u) bytes.back() = std::uint8_t(bytes.back() | (1u << used));
        used = (used + 1) & 7;
    }
    // an integer value: least significant bit first (section 1.5.1)
    void Put(std::uint64_t v, int bits) { for (int i = 0; i < bits; ++i) Bit(unsigned(v >> i) & 1u); }
    // a prefix code: most significant bit first
    void PutCode(std::uint32_t code, int len) { for (int i = len - 1; i >= 0; --i) Bit((code >> i) & 1u); }
    void ToByte() { while (used) Bit(0); }
    void Bytes(const std::uint8_t* p, std::size_t n) { ToByte(); bytes.insert(bytes.end(), p, p + n); }
};

// Section 3.2: the canonical codes of a list of lengths (0 = no code).
inline std::vector<std::uint32_t> CanonicalCodes(const std::vector<int>& lens) {
    int count[16] = {0};
    for (int l : lens) if (l) ++count[l];
    std::uint32_t next[16] = {0}, code = 0;
    for (int b = 1; b < 16; ++b) { code = (code + std::uint32_t(count[b - 1])) << 1; next[b] = code; }
    std::vector<std::uint32_t> out(lens.size(), 0);
    for (std::size_t s = 0; s < lens.size(); ++s) if (lens[s]) out[s] = next[lens[s]]++;
    return out;
}

// Lengths of a complete code over `used` symbols of an alphabet: as equal as possible (2^L - k of length L - 1, the rest L).
inline std::vector<int> FlatLengths(int alphabet, const std::vector<int>& used) {
    std::vector<int> lens(std::size_t(alphabet), 0);
    const int k = int(used.size());
    if (k < 2) return lens;
    int L = 0;
    while ((1 << L) < k) ++L;
    int shorter = (1 << L) - k;
    for (int s : used) lens[std::size_t(s)] = shorter-- > 0 ? L - 1 : L;
    return lens;
}

struct Code {
    std::vector<int> lens;
    std::vector<std::uint32_t> codes;
    bool zeroBits = false;   // one symbol: no bits
    void Emit(BitWriter& w, int sym) const { if (!zeroBits) w.PutCode(codes[std::size_t(sym)], lens[std::size_t(sym)]); }
};

inline int WidthFor(int alphabet) { int w = 0; while ((1 << w) < alphabet) ++w; return w; }

// Section 3.4: a simple prefix code over 1..4 symbols (in the order given; treeSelect for 4 symbols).
inline Code WriteSimple(BitWriter& w, int alphabet, const std::vector<int>& syms, int treeSelect = 0) {
    Code c;
    c.lens.assign(std::size_t(alphabet), 0);
    w.Put(1, 2);
    w.Put(syms.size() - 1, 2);
    for (int s : syms) w.Put(std::uint64_t(s), WidthFor(alphabet));
    static const int kShapes[5][4] = {{0, 0, 0, 0}, {1, 1, 0, 0}, {1, 2, 2, 0}, {2, 2, 2, 2}, {1, 2, 3, 3}};
    const int shape = syms.size() == 4 ? 3 + treeSelect : int(syms.size()) - 1;
    if (syms.size() == 4) w.Put(std::uint64_t(treeSelect), 1);
    for (std::size_t i = 0; i < syms.size(); ++i) c.lens[std::size_t(syms[i])] = kShapes[shape][i];
    c.zeroBits = syms.size() == 1;
    c.codes = CanonicalCodes(c.lens);
    return c;
}

// One entry of the code-length sequence of a complex code: a length 0..15, or 16 / 17 with the value of its extra bits.
struct Token { int sym; int extra; };

// Section 3.5: a complex prefix code written from its code-length tokens; `lens` is what the tokens expand to (the caller
// works it out; WriteComplex below does it for plain lengths).
inline Code WriteComplexTokens(BitWriter& w, const std::vector<Token>& tokens, const std::vector<int>& lens, int hskip = 0) {
    static const int kOrder[18] = {1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    std::vector<int> usedSyms;
    for (const Token& t : tokens) {
        bool seen = false;
        for (int u : usedSyms) seen = seen || u == t.sym;
        if (!seen) usedSyms.push_back(t.sym);
    }
    std::vector<int> clens = FlatLengths(18, usedSyms);
    const bool single = usedSyms.size() == 1;
    if (single) clens[std::size_t(usedSyms[0])] = 1;   // (written as a length; it takes no bits when it is the only one)
    w.Put(std::uint64_t(hskip), 2);
    int space = 32;
    for (int i = hskip; i < 18; ++i) {
        const int v = clens[std::size_t(kOrder[i])];
        // the fixed code for the lengths 0..5, in the order the bits are read
        static const int kBits[6][5] = {{2, 0, 0}, {4, 1, 1, 1, 0}, {3, 1, 1, 0}, {2, 0, 1}, {2, 1, 0}, {4, 1, 1, 1, 1}};
        for (int b = 1; b <= kBits[v][0]; ++b) w.Bit(unsigned(kBits[v][b]));
        if (v) space -= 32 >> v;
        if (!single && space == 0) break;
    }
    Code clc;
    clc.lens = clens;
    clc.codes = CanonicalCodes(clens);
    clc.zeroBits = single;
    for (const Token& t : tokens) {
        clc.Emit(w, t.sym);
        if (t.sym == 16) w.Put(std::uint64_t(t.extra), 2);
        if (t.sym == 17) w.Put(std::uint64_t(t.extra), 3);
    }
    Code c;
    c.lens = lens;
    c.codes = CanonicalCodes(lens);
    return c;
}

// A complex code from plain lengths: each length written as itself, up to the last non-zero one.
inline Code WriteComplex(BitWriter& w, const std::vector<int>& lens, int hskip = 0) {
    std::size_t last = 0;
    for (std::size_t s = 0; s < lens.size(); ++s) if (lens[s]) last = s;
    std::vector<Token> tokens;
    for (std::size_t s = 0; s <= last; ++s) tokens.push_back({lens[s], 0});
    return WriteComplexTokens(w, tokens, lens, hskip);
}

// A code for the symbols `used`: simple when allowed and possible, otherwise complex.
inline Code WriteCodeFor(BitWriter& w, int alphabet, std::vector<int> used, bool preferSimple) {
    std::vector<int> uniq;
    for (int s : used) { bool seen = false; for (int u : uniq) seen = seen || u == s; if (!seen) uniq.push_back(s); }
    if (uniq.empty()) uniq.push_back(0);
    if (uniq.size() == 1 || (preferSimple && uniq.size() <= 4)) return WriteSimple(w, alphabet, uniq, uniq.size() == 4 ? 1 : 0);
    return WriteComplex(w, FlatLengths(alphabet, uniq));
}

// ---- headers (section 9) ---------------------------------------------------------------------------------------------------
inline void WriteWindow(BitWriter& w, int wbits) {
    if (wbits == 16) { w.Put(0, 1); return; }
    if (wbits >= 18) { w.Put(1, 1); w.Put(std::uint64_t(wbits - 17), 3); return; }
    w.Put(1, 1); w.Put(0, 3);
    w.Put(wbits == 17 ? 0 : std::uint64_t(wbits - 8), 3);
}
inline void WriteMlen(BitWriter& w, std::size_t mlen) {
    const std::size_t v = mlen - 1;
    int nibbles = 4;
    while (nibbles < 6 && (v >> (4 * nibbles)) != 0) ++nibbles;
    w.Put(std::uint64_t(nibbles - 4), 2);
    w.Put(v, 4 * nibbles);
}
inline void WriteStoredBlock(BitWriter& w, const std::uint8_t* p, std::size_t n) {   // ISLAST 0, uncompressed
    w.Put(0, 1);
    WriteMlen(w, n);
    w.Put(1, 1);
    w.Bytes(p, n);
}
inline void WriteLastEmpty(BitWriter& w) { w.Put(1, 1); w.Put(1, 1); w.ToByte(); }
/// Header of a compressed meta-block with one block type per category, one literal and one distance tree.
inline void WritePlainHeader(BitWriter& w, bool isLast, std::size_t mlen, int npostfix, int ndirect, int contextMode) {
    w.Put(isLast ? 1 : 0, 1);
    if (isLast) w.Put(0, 1);
    WriteMlen(w, mlen);
    if (!isLast) w.Put(0, 1);
    w.Put(0, 1); w.Put(0, 1); w.Put(0, 1);   // NBLTYPESL, NBLTYPESI, NBLTYPESD = 1
    w.Put(std::uint64_t(npostfix), 2);
    w.Put(std::uint64_t(ndirect >> npostfix), 4);
    w.Put(std::uint64_t(contextMode), 2);
    w.Put(0, 1); w.Put(0, 1);                // NTREESL, NTREESD = 1
}

/// A whole stream of uncompressed meta-blocks (at most `chunk` bytes each), as the RFC's trivial compressor lays them out.
inline std::vector<std::uint8_t> StoredStream(const std::uint8_t* p, std::size_t n, std::size_t chunk = 65536) {
    BitWriter w;
    WriteWindow(w, 16);
    for (std::size_t at = 0; at < n; at += chunk) WriteStoredBlock(w, p + at, n - at < chunk ? n - at : chunk);
    WriteLastEmpty(w);
    return w.bytes;
}

// ---- commands (sections 4 and 5), with the RFC's printed tables -------------------------------------------------------------
const std::uint32_t kInsFirst[24] = {0, 1, 2, 3, 4, 5, 6, 8, 10, 14, 18, 26, 34, 50, 66, 98, 130, 194, 322, 578, 1090, 2114, 6210, 22594};
const int kInsBits[24] = {0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 12, 14, 24};
const std::uint32_t kCopyFirst[24] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 18, 22, 30, 38, 54, 70, 102, 134, 198, 326, 582, 1094, 2118};
const int kCopyBits[24] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 24};

inline int LengthCode(const std::uint32_t* first, std::uint32_t len) { int c = 23; while (first[c] > len) --c; return c; }

/// The insert-and-copy symbol for an insert code and a copy code (the table in section 5); implicit = distance symbol 0.
inline int CommandSymbol(int ins, int copy, bool implicit) {
    if (implicit && ins < 8 && copy < 16) return (copy < 8 ? 0 : 64) + (ins << 3) + (copy & 7);
    static const int kCells[9][3] = {{128, 0, 0}, {192, 0, 8}, {256, 8, 0}, {320, 8, 8}, {384, 0, 16}, {448, 16, 0}, {512, 8, 16}, {576, 16, 8}, {640, 16, 16}};
    for (const auto& c : kCells)
        if (ins >= c[1] && ins < c[1] + 8 && copy >= c[2] && copy < c[2] + 8) return c[0] + ((ins - c[1]) << 3) + (copy - c[2]);
    return -1;
}

struct Dist { int sym; std::uint32_t extra; int bits; };
/// Section 4, the other way round: a distance symbol >= 16 and its extra bits for a distance (sym -1 if none can say it).
inline Dist DistanceFor(std::uint64_t dist, int npostfix, int ndirect) {
    if (dist >= 1 && dist <= std::uint64_t(ndirect)) return {15 + int(dist), 0, 0};
    const int count = 48 << npostfix;
    for (int k = 0; k < count; ++k) {
        const int nbits = 1 + (k >> (npostfix + 1));
        const std::uint64_t h = std::uint64_t(k) >> npostfix, l = std::uint64_t(k) & ((1u << npostfix) - 1);
        const std::uint64_t offset = ((2 + (h & 1)) << nbits) - 4;
        if (dist < std::uint64_t(ndirect) + 1 + l) continue;
        const std::uint64_t rest = dist - std::uint64_t(ndirect) - 1 - l;
        if (rest & ((1u << npostfix) - 1)) continue;
        const std::uint64_t q = rest >> npostfix;
        if (q >= offset && q - offset < (std::uint64_t(1) << nbits)) return {16 + ndirect + k, std::uint32_t(q - offset), nbits};
    }
    return {-1, 0, 0};
}

struct Command {
    std::vector<std::uint8_t> literals;
    std::uint32_t copy = 4;     // copy length (ignored for the last command when the literals end the meta-block)
    bool implicit = false;      // distance symbol 0 through the insert-and-copy code
    Dist dist{0, 0, 0};         // the distance symbol (0..15 special, then direct / coded) and its extra bits
    bool last = false;          // ends the meta-block after its literals: no distance written
};

/// One compressed meta-block (one block type each, one literal / distance tree) with these commands.
inline void WriteCommands(BitWriter& w, const std::vector<Command>& cmds, bool isLast, std::size_t mlen, int npostfix = 0, int ndirect = 0,
                          int contextMode = 0, bool preferSimple = false) {
    WritePlainHeader(w, isLast, mlen, npostfix, ndirect, contextMode);
    std::vector<int> lits, syms, dists;
    std::vector<int> cmdSym;
    for (const Command& c : cmds) {
        for (std::uint8_t b : c.literals) lits.push_back(b);
        const int ic = LengthCode(kInsFirst, std::uint32_t(c.literals.size())), cc = LengthCode(kCopyFirst, c.copy);
        cmdSym.push_back(CommandSymbol(ic, cc, c.implicit));
        syms.push_back(cmdSym.back());
        if (!c.implicit && !c.last) dists.push_back(c.dist.sym);
    }
    const Code lit = WriteCodeFor(w, 256, lits, preferSimple);
    const Code ins = WriteCodeFor(w, 704, syms, preferSimple);
    const Code dst = WriteCodeFor(w, 16 + ndirect + (48 << npostfix), dists, preferSimple);
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        const Command& c = cmds[i];
        const int ic = LengthCode(kInsFirst, std::uint32_t(c.literals.size())), cc = LengthCode(kCopyFirst, c.copy);
        ins.Emit(w, cmdSym[i]);
        w.Put(c.literals.size() - kInsFirst[ic], kInsBits[ic]);
        w.Put(c.copy - kCopyFirst[cc], kCopyBits[cc]);
        for (std::uint8_t b : c.literals) lit.Emit(w, b);
        if (c.implicit || c.last) continue;
        dst.Emit(w, c.dist.sym);
        w.Put(c.dist.extra, c.dist.bits);
    }
}

} // namespace brw
