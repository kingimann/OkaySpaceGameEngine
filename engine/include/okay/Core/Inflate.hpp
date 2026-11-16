#pragma once
// ---------------------------------------------------------------------------
// okay::inflate: the ONE DEFLATE decoder of the engine (raw, zlib and gzip).
//
// Used by the image decoders (PNG), the model readers (.blend gzip, .x MSZIP, ZGL, 3MF/AMF zip, own FBX
// arrays) through okay::Inflate / okay::fbx::Inflate, and by the own HTTP client (gzip and zlib bodies).
// It used to exist three times (ImageDecode.cpp, FbxReader.cpp, OwnHttpInflate.hpp); this is the strictest
// of the three, kept once. The older callers keep their old, more forgiving behaviour through Options
// (see below), so nothing they accepted before is refused now.
//
// Provenance: written from RFC 1950 (zlib), RFC 1951 (deflate) and RFC 1952 (gzip) ONLY. No decoder source
// (zlib, puff, miniz, stb_image, libdeflate or any other) was read, fetched, copied or ported for this file.
// An earlier version of this file was flagged by an outside review as closely following a public reference
// decoder; this is a rewrite with a different design (see docs/provenance_audit.md).
//
// Design: a table-driven canonical Huffman decoder.
//  - Codes are assigned as RFC 1951 section 3.2.2 describes (next_code per length, symbols in order).
//  - Deflate packs Huffman codes most-significant-bit first into a least-significant-bit-first stream, so
//    every code is bit-reversed once when the table is built. A 10-bit first-level table is then indexed
//    directly by the next 10 bits of the stream and gives (symbol, length) in one load.
//  - Codes longer than 10 bits (up to 15) take a slow path: the 15 next bits are reversed once and compared
//    against the per-length range [first code, first code + count) of that length.
//  - The bit reader keeps a 64-bit buffer that is refilled bytewise; unused whole bytes are given back at
//    the end of the stream so the trailer position is exact.
//  - Stored, fixed-table and dynamic-table blocks are three separate paths. The length and distance base
//    and extra-bit tables are derived from the rule in RFC 1951 section 3.2.5 at compile time, and a test
//    checks them against the printed table.
// This is compression, not cryptography. Hostile input is the normal case:
//  - the output is capped (`maxOut`), memory grows only up to that cap, so a zip bomb stops at the cap;
//  - every table is validated (over-subscribed or incomplete codes, bad lengths, back-references that
//    start before the output) and every read is bounds checked;
//  - the gzip CRC-32 and size, or the zlib Adler-32, must match; extra bytes after the stream are refused
//    (no multi-member gzip).
// Memory: the working buffer never grows past `maxOut`, but a successful decode copies it into the caller's string, and the
// caller holds the encoded body too (see Limits::maxEncodedBytes). So the peak is about three times the cap, not one.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace okay::inflate {

namespace detail {

// ---- RFC 1951 section 3.2.5: length and distance symbols, derived from the rule ------------------
// Length symbols 257..285 (index 0..28): 8 symbols with no extra bits, then four symbols per extra-bit
// count 1..5, then symbol 285 is the single length 258. Distance symbols 0..29: four with no extra bits,
// then two per extra-bit count 1..13. Each base is the previous base plus 2^(previous extra bits).
struct SymbolTables {
    uint16_t lenBase[29];
    uint8_t lenExtra[29];
    uint16_t distBase[30];
    uint8_t distExtra[30];
    constexpr SymbolTables() : lenBase{}, lenExtra{}, distBase{}, distExtra{} {
        for (int i = 0; i < 29; ++i) lenExtra[i] = uint8_t(i < 8 || i == 28 ? 0 : i / 4 - 1);
        lenBase[0] = 3;
        for (int i = 1; i < 29; ++i) lenBase[i] = uint16_t(lenBase[i - 1] + (1 << lenExtra[i - 1]));
        lenBase[28] = 258;   // the one exception: 285 means exactly 258, not 227 + 32
        for (int j = 0; j < 30; ++j) distExtra[j] = uint8_t(j < 4 ? 0 : j / 2 - 1);
        distBase[0] = 1;
        for (int j = 1; j < 30; ++j) distBase[j] = uint16_t(distBase[j - 1] + (1 << distExtra[j - 1]));
    }
};
inline constexpr SymbolTables kSym{};

// Order in which the code-length code's own lengths are stored (RFC 1951 section 3.2.7).
inline constexpr uint8_t kClOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

// ---- bit reader -----------------------------------------------------------------------------------
struct BitReader {
    const uint8_t* src; size_t len; size_t pos = 0;
    uint64_t buf = 0; int cnt = 0;     // bits in `buf`, least significant first; bits above `cnt` are zero
    bool starved = false;              // asked for more bits than the input has

    BitReader(const uint8_t* d, size_t n) : src(d), len(n) {}
    void Refill() {
        if (cnt <= 56 && len - pos >= 8) {   // whole bytes at once while 8 input bytes remain: one load, not a loop
            uint64_t w;
            std::memcpy(&w, src + pos, 8);   // little-endian host assumed (as everywhere in the engine)
            const int take = (63 - cnt) >> 3;   // bytes that fit above `cnt`: 0 to 7, so the shift below is < 64
            buf |= (w & ((uint64_t(1) << (take * 8)) - 1)) << cnt;   // keep the bits above the new `cnt` zero
            pos += size_t(take); cnt += take * 8;
            return;
        }
        while (cnt <= 56 && pos < len) { buf |= uint64_t(src[pos++]) << cnt; cnt += 8; }
    }
    uint32_t Take(int n) {             // n <= 16
        if (cnt < n) {
            Refill();
            if (cnt < n) { starved = true; return 0; }
        }
        uint32_t v = uint32_t(buf & ((uint64_t(1) << n) - 1));
        buf >>= n; cnt -= n;
        return v;
    }
    void Drop(int n) { buf >>= n; cnt -= n; }
    // Throw away the rest of the current byte, and hand whole unread bytes back to the input.
    void AlignToByte() { Drop(cnt & 7); pos -= size_t(cnt >> 3); buf = 0; cnt = 0; }
    // Number of input bytes used once the stream is over (a partly used byte counts as used).
    size_t BytesUsed() const { return pos - size_t(cnt >> 3); }
};

// The low `bits` bits of v (bits <= 16) in reverse order: all 16 bits reversed by swapping ever larger groups
// (single bits, pairs, nibbles, bytes), then shifted down.
inline uint32_t Reverse(uint32_t v, int bits) {
    v &= (1u << bits) - 1u;   // only the low `bits` bits take part, as before
    v = ((v >> 1) & 0x5555u) | ((v & 0x5555u) << 1);
    v = ((v >> 2) & 0x3333u) | ((v & 0x3333u) << 2);
    v = ((v >> 4) & 0x0F0Fu) | ((v & 0x0F0Fu) << 4);
    v = ((v >> 8) & 0x00FFu) | ((v & 0x00FFu) << 8);
    return v >> (16 - bits);
}

// ---- canonical Huffman decoder ---------------------------------------------------------------------
constexpr int kFastBits = 10;
constexpr int kMaxBits = 15;

struct DecoderBase {
    enum class Shape { Oversubscribed, Incomplete, Complete };
    enum : int { kBadCode = -1, kStarved = -2 };
};

// FastBits: the widest the direct lookup table gets. Literal/length and distance codes use kFastBits; the code-length
// code (lengths of at most 7 bits) uses 7, so each dynamic block fills a 128-entry table for it instead of 1024.
// The table actually used is `bits` wide: FastBits, or the longest code when that is shorter (a small block's distance
// code often has codes of at most 5 or 6 bits: 64 entries to fill instead of 1024). Every code no longer than `bits`
// is found in the table, longer ones (only when bits == FastBits) by the slow path, so the width changes nothing but
// the work.
template <int FastBits>
struct DecoderT : DecoderBase {
    // quick[i]: (symbol << 4 | length) for the code whose reversed bits are the low `bits` bits of i, 0 if none; only
    // the first 2^bits entries are used
    uint16_t quick[1 << FastBits];
    int bits = FastBits;                  // table width in use (1..FastBits)
    uint32_t mask = (1u << FastBits) - 1; // (1 << bits) - 1
    uint16_t sorted[288];                 // symbols ordered by (length, symbol)
    uint16_t lowestCode[kMaxBits + 1];     // first canonical code of each length
    uint16_t slotBase[kMaxBits + 1];     // index in `sorted` of the first symbol of each length
    uint16_t countOf[kMaxBits + 1];
    int used = 0;                         // symbols that have a code

    Shape Build(const uint8_t* lens, int n) {
        for (int l = 0; l <= kMaxBits; ++l) countOf[l] = 0;
        for (int i = 0; i < n; ++i) ++countOf[lens[i]];
        used = n - countOf[0];
        // Kraft sum in units of 2^-15: a prefix code is complete exactly when it reaches 2^15.
        uint32_t kraft = 0;
        int longest = 0;
        for (int l = 1; l <= kMaxBits; ++l) { kraft += uint32_t(countOf[l]) << (kMaxBits - l); if (countOf[l]) longest = l; }
        if (kraft > (1u << kMaxBits)) return Shape::Oversubscribed;
        bits = longest < 1 ? 1 : (longest < FastBits ? longest : FastBits);
        mask = (1u << bits) - 1;
        // A complete code whose codes all fit the table writes every entry below: nothing to clear first.
        if (!(kraft == (1u << kMaxBits) && longest <= bits)) std::memset(quick, 0, sizeof(uint16_t) << bits);
        // RFC 1951 3.2.2: next_code[bits] = (next_code[bits-1] + bl_count[bits-1]) << 1
        uint32_t next[kMaxBits + 2]; uint32_t code = 0; uint16_t slot = 0;
        lowestCode[0] = 0; slotBase[0] = 0;
        for (int l = 1; l <= kMaxBits; ++l) {
            code = (code + (l > 1 ? countOf[l - 1] : 0)) << 1;
            next[l] = code; lowestCode[l] = uint16_t(code);
            slotBase[l] = slot; slot = uint16_t(slot + countOf[l]);
        }
        uint16_t fill[kMaxBits + 1];
        for (int l = 0; l <= kMaxBits; ++l) fill[l] = 0;
        for (int sym = 0; sym < n; ++sym) {
            int l = lens[sym];
            if (!l) continue;
            sorted[slotBase[l] + fill[l]++] = uint16_t(sym);
            uint32_t c = next[l]++;
            if (l <= bits) {
                uint32_t rev = Reverse(c, l);
                for (uint32_t i = rev; i < (1u << bits); i += (1u << l)) quick[i] = uint16_t(sym << 4 | l);
            }
        }
        return kraft == (1u << kMaxBits) ? Shape::Complete : Shape::Incomplete;
    }

    // Returns the symbol, kBadCode (no such code) or kStarved (the input ends inside the code).
    int Next(BitReader& br) const {
        if (br.cnt < kMaxBits) br.Refill();
        uint16_t e = quick[br.buf & mask];
        if (e) {
            int l = e & 15;
            if (l > br.cnt) return kStarved;
            br.Drop(l);
            return e >> 4;
        }
        // slow path: compare the next 15 bits, as a most-significant-first number, with each length's range
        uint32_t v = uint32_t(br.buf & ((1u << kMaxBits) - 1));
        v = Reverse(v, kMaxBits);
        for (int l = bits + 1; l <= kMaxBits; ++l) {   // (codes longer than the table exist only when bits == FastBits)
            uint32_t prefix = v >> (kMaxBits - l);
            uint32_t k = prefix - lowestCode[l];
            if (prefix >= lowestCode[l] && k < countOf[l]) {
                if (l > br.cnt) return kStarved;
                br.Drop(l);
                return sorted[slotBase[l] + k];
            }
        }
        // no code matches; if the stream ended early the zero padding may be the reason
        return br.cnt < kMaxBits && br.pos >= br.len ? kStarved : kBadCode;
    }
};
using Decoder = DecoderT<kFastBits>;

struct FixedCodes {
    Decoder lit, dist;
    FixedCodes() {
        uint8_t l[288];
        for (int i = 0; i < 288; ++i) l[i] = uint8_t(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
        lit.Build(l, 288);
        uint8_t d[32];
        for (int i = 0; i < 32; ++i) d[i] = 5;
        dist.Build(d, 32);
    }
};

// ---- output buffer: grows only up to the cap, never doubles past it --------------------------------
// Writes straight into the caller's vector (no second copy of the result). `size` is the number of bytes
// produced; the vector itself is sized in steps and trimmed to `size` at the end. Vec is std::vector<uint8_t>, or a
// byte vector whose allocator leaves new bytes unset on resize (no zero fill: every byte below `size` is written by
// the decoder before anything reads it, and the rest is cut off at the end).
template <class Vec>
struct OutputT {
    Vec& v; size_t size = 0, limit; bool capped = false;
    OutputT(Vec& vec, size_t maxOut) : v(vec), limit(maxOut) {}
    // Make room for `extra` more bytes (the caller has already checked size + extra <= limit).
    void Room(size_t extra) {
        size_t need = size + extra;
        if (need <= v.size()) return;
        size_t want = v.size() * 2 > need ? v.size() * 2 : need;
        if (want < 4096) want = 4096;
        if (want > limit) want = limit;
        if (want < need) want = need;
        v.reserve(want);   // exact: vector::resize alone may over-allocate past the cap
        v.resize(want);
    }
    uint8_t* Data() { return v.data(); }
};

} // namespace detail

// ---- decoding options ------------------------------------------------------------------------------
// The strict defaults are what the HTTP client needs. The older callers (images, models) pass Lenient().
struct Options {
    size_t maxOutput = size_t(1) << 31;   // output cap (zip-bomb guard); memory never grows past it
    size_t hint = 0;                      // expected output size, used for the first allocation only
    bool stopAtMax = false;               // reaching maxOutput is success and decoding stops there (PNG: only the pixel
                                          // bytes matter; a damaged or cut-off tail after them is not examined)
    bool strictCodes = true;              // refuse incomplete Huffman codes (single-code trees stay legal)
    bool checkTrailer = true;             // zlib: window size, Adler-32, no extra bytes; gzip is always checked
    static Options Lenient(size_t maxOut, size_t hintBytes = 0) {
        Options o; o.maxOutput = maxOut; o.hint = hintBytes; o.strictCodes = false; o.checkTrailer = false; return o;
    }
};

namespace detail {

// Copies a match of `length` bytes that starts `back` bytes behind `d` (back >= 1; the bytes may overlap, which
// repeats the last `back` bytes). Same result as copying one byte at a time; it only moves more bytes per step:
// eight at a time while the source is at least eight bytes behind (each step then reads bytes already written),
// one repeated byte as a fill, and a plain copy when the two ranges do not overlap at all.
inline void CopyMatch(uint8_t* d, size_t back, size_t length) {
    const uint8_t* s = d - back;
    if (back >= length) { std::memcpy(d, s, length); return; }
    if (back == 1) { std::memset(d, *s, length); return; }
    size_t i = 0;
    if (back >= 8) for (; i + 8 <= length; i += 8) { uint64_t w; std::memcpy(&w, s + i, 8); std::memcpy(d + i, &w, 8); }
    for (; i < length; ++i) d[i] = s[i];
}

// CopyMatch for the fast path, where the output has at least `length + 8` bytes of room: it may write up to 7 bytes
// past the end of the match (they are overwritten by what follows, or cut off when the output is trimmed), so short
// matches take one or two 8-byte steps instead of a call. Each 8-byte step reads only bytes that are already final:
// with back >= 8 the source of a step ends before the step's own destination begins.
inline void CopyMatchFast(uint8_t* d, size_t back, size_t length) {
    const uint8_t* s = d - back;
    if (back >= 8) {
        size_t i = 0;
        do { uint64_t w; std::memcpy(&w, s + i, 8); std::memcpy(d + i, &w, 8); i += 8; } while (i < length);
        return;
    }
    if (back == 1) { std::memset(d, *s, length); return; }
    for (size_t i = 0; i < length; ++i) d[i] = s[i];
}

// Decodes the symbols of one Huffman-coded block until its end-of-block symbol.
template <class Output>
inline bool RunBlock(BitReader& br, const Decoder& lit, const Decoder& dist, Output& out, const Options& op, std::string& why) {
    static const char* const kTooBig = "decoded body larger than the limit";
    for (;;) {
        // Fast path, taken while at least 8 input bytes are left and the output has room for the longest match (258
        // bytes, plus 8 for CopyMatchFast) without growing. The bit buffer is topped up to at least 48 bits before each symbol, which is what one
        // length/distance pair can need (15 + 5 + 15 + 13), so no read below can run out of bits and the output never
        // needs to grow: the same checks in the same order as the general path, without its refill and room tests.
        // The reader and output state live in locals here (stores into the output bytes could otherwise alias them)
        // and are written back before anything else looks at them.
        if (br.len - br.pos >= 8 && out.v.size() - out.size >= 258 + 8) {
            const uint8_t* const src = br.src;
            const size_t inSafe = br.len - 8;
            size_t pos = br.pos; uint64_t buf = br.buf; int cnt = br.cnt;
            uint8_t* const o = out.v.data();
            const size_t outSafe = out.v.size() - (258 + 8), limit = out.limit;
            const bool stop = op.stopAtMax;
            const uint32_t litMask = lit.mask, distMask = dist.mask;
            size_t size = out.size;
            auto save = [&] { br.pos = pos; br.buf = buf; br.cnt = cnt; out.size = size; };
            auto load = [&] { pos = br.pos; buf = br.buf; cnt = br.cnt; };
            while (pos <= inSafe && size <= outSafe) {
                if (cnt < 48) {   // whole bytes above `cnt` (the same refill as BitReader::Refill, 8 bytes are there)
                    uint64_t w; std::memcpy(&w, src + pos, 8);
                    const int take = (63 - cnt) >> 3;
                    buf |= (w & ((uint64_t(1) << (take * 8)) - 1)) << cnt;
                    pos += size_t(take); cnt += take * 8;
                }
                int sym;
                if (const uint16_t e = lit.quick[buf & litMask]) { const int l = e & 15; buf >>= l; cnt -= l; sym = e >> 4; }
                else {
                    save(); sym = lit.Next(br); load();
                    if (sym < 0) { why = sym == Decoder::kStarved ? "deflate data ends early" : "invalid deflate code"; return false; }
                }
                if (sym < 256) {
                    if (size >= limit) {
                        save();
                        if (stop) { out.capped = true; return true; }
                        why = kTooBig; return false;
                    }
                    o[size++] = uint8_t(sym);
                    if (stop && size == limit) { save(); out.capped = true; return true; }
                    // Up to two more literals straight from the table while their bits are in the buffer (no refill
                    // needed): each is what the loop's next turn would decode. Here size <= outSafe + 1 < v.size() <=
                    // limit, so the limit and stop tests above cannot fire for them (the output never grows past the
                    // cap); the loop's own bounds are tested again at its top.
                    for (int extra = 0; extra < 2; ++extra) {
                        const uint16_t e2 = lit.quick[buf & litMask];
                        const int l2 = e2 & 15;
                        if (e2 == 0 || (e2 >> 4) >= 256 || l2 > cnt) break;
                        buf >>= l2; cnt -= l2;
                        o[size++] = uint8_t(e2 >> 4);
                    }
                    continue;
                }
                if (sym == 256) { save(); return true; }
                const int li = sym - 257;
                if (li >= 29) { save(); why = "invalid deflate length symbol"; return false; }
                const int le = kSym.lenExtra[li];
                size_t length = kSym.lenBase[li] + uint32_t(buf & ((uint64_t(1) << le) - 1));
                buf >>= le; cnt -= le;
                int ds;
                if (const uint16_t e = dist.quick[buf & distMask]) { const int l = e & 15; buf >>= l; cnt -= l; ds = e >> 4; }
                else {
                    save(); ds = dist.Next(br); load();
                    if (ds < 0) { why = ds == Decoder::kStarved ? "deflate data ends early" : "invalid deflate code"; return false; }
                }
                if (ds >= 30) { save(); why = "invalid deflate distance symbol"; return false; }
                const int de = kSym.distExtra[ds];
                const size_t back = kSym.distBase[ds] + uint32_t(buf & ((uint64_t(1) << de) - 1));
                buf >>= de; cnt -= de;
                if (back > size) { save(); why = "back-reference starts before the output"; return false; }
                if (length > limit - size) {
                    if (!stop) { save(); why = kTooBig; return false; }
                    length = limit - size;   // keep the part that fits, then stop
                }
                CopyMatchFast(o + size, back, length);
                size += length;
                if (stop && size == limit) { save(); out.capped = true; return true; }
            }
            save();
            continue;   // near the end of the input or of the output buffer: the general path below
        }
        int sym = lit.Next(br);
        if (sym < 0) { why = sym == Decoder::kStarved ? "deflate data ends early" : "invalid deflate code"; return false; }
        if (sym < 256) {
            if (out.size >= out.limit) {
                if (op.stopAtMax) { out.capped = true; return true; }
                why = kTooBig; return false;
            }
            out.Room(1);
            out.Data()[out.size++] = uint8_t(sym);
            // stopAtMax: everything wanted is here; what follows (even a cut-off or damaged tail) is not examined
            if (op.stopAtMax && out.size == out.limit) { out.capped = true; return true; }
            continue;
        }
        if (sym == 256) return true;
        int li = sym - 257;
        if (li >= 29) { why = "invalid deflate length symbol"; return false; }
        size_t length = kSym.lenBase[li] + br.Take(kSym.lenExtra[li]);
        int ds = dist.Next(br);
        if (ds < 0) { why = ds == Decoder::kStarved ? "deflate data ends early" : "invalid deflate code"; return false; }
        if (ds >= 30) { why = "invalid deflate distance symbol"; return false; }
        size_t back = kSym.distBase[ds] + br.Take(kSym.distExtra[ds]);
        if (br.starved) { why = "deflate data ends early"; return false; }
        if (back > out.size) { why = "back-reference starts before the output"; return false; }
        if (length > out.limit - out.size) {
            if (!op.stopAtMax) { why = kTooBig; return false; }
            length = out.limit - out.size;   // keep the part that fits, then stop
        }
        out.Room(length);
        CopyMatch(out.Data() + out.size, back, length);   // an overlapping copy repeats the pattern
        out.size += length;
        if (op.stopAtMax && out.size == out.limit) { out.capped = true; return true; }
    }
}

// ---- the three block kinds --------------------------------------------------------------------------
template <class Output>
inline bool StoredBlock(BitReader& br, Output& out, std::string& why) {
    br.AlignToByte();
    if (br.len - br.pos < 4) { why = "deflate data ends early"; return false; }
    const uint8_t* h = br.src + br.pos;
    uint32_t n = h[0] | (uint32_t(h[1]) << 8), inv = h[2] | (uint32_t(h[3]) << 8);
    br.pos += 4;
    if ((n ^ 0xffffu) != inv) { why = "stored block length check failed"; return false; }
    if (n > br.len - br.pos) { why = "deflate data ends early"; return false; }
    // A stored block is all or nothing, even with stopAtMax: its length is declared up front, so a block that does not
    // fit is a stream that does not match what the caller expects (the older image decoder refused it too, and the
    // differential test pins that). Matches are different: they are cut at the cap, as the older decoder did.
    if (n > out.limit - out.size) { why = "decoded body larger than the limit"; return false; }
    out.Room(n);
    if (n) std::memcpy(out.Data() + out.size, br.src + br.pos, n);
    out.size += n; br.pos += n;
    return true;
}

template <class Output>
inline bool FixedBlock(BitReader& br, Output& out, const Options& op, std::string& why) {
    // Built on first use (a function-local static). C++11 makes that initialisation thread-safe, but this has not been
    // proven under MinGW's "win32" thread model; nothing here uses <mutex>, <thread> or call_once (CLAUDE.md rule 21a).
    // If it ever matters, build FixedCodes at compile time like CrcTable, or construct it per call (it is small).
    static const FixedCodes fixed;
    return RunBlock(br, fixed.lit, fixed.dist, out, op, why);
}

template <class Output>
inline bool DynamicBlock(BitReader& br, Output& out, const Options& op, std::string& why) {
    int hlit = int(br.Take(5)) + 257, hdist = int(br.Take(5)) + 1, hclen = int(br.Take(4)) + 4;
    if (br.starved) { why = "deflate data ends early"; return false; }
    if (hlit > 286 || hdist > 30) { why = "deflate table sizes out of range"; return false; }
    uint8_t clLens[19] = {0};
    for (int i = 0; i < hclen; ++i) clLens[kClOrder[i]] = uint8_t(br.Take(3));
    if (br.starved) { why = "deflate data ends early"; return false; }
    DecoderT<7> cl;
    auto cs = cl.Build(clLens, 19);
    if (cs == Decoder::Shape::Oversubscribed || (op.strictCodes && cs != Decoder::Shape::Complete)) {
        why = "deflate code-length code is not a complete prefix code"; return false;
    }

    const int total = hlit + hdist;
    uint8_t lens[286 + 30] = {0};
    int at = 0;
    while (at < total) {
        int sym = cl.Next(br);
        if (sym < 0) { why = sym == Decoder::kStarved ? "deflate data ends early" : "invalid deflate code"; return false; }
        if (sym < 16) { lens[at++] = uint8_t(sym); continue; }
        uint8_t value = 0; int run;
        if (sym == 16) {
            if (at == 0) { why = "deflate repeat with nothing to repeat"; return false; }
            value = lens[at - 1]; run = 3 + int(br.Take(2));
        } else if (sym == 17) run = 3 + int(br.Take(3));
        else run = 11 + int(br.Take(7));
        if (br.starved) { why = "deflate data ends early"; return false; }
        if (run > total - at) { why = "deflate code lengths overrun the table"; return false; }
        while (run--) lens[at++] = value;
    }
    if (lens[256] == 0) { why = "deflate block has no end-of-block code"; return false; }

    Decoder lit, dist;
    auto ls = lit.Build(lens, hlit);
    if (ls == Decoder::Shape::Oversubscribed) { why = "deflate literal code is over-subscribed"; return false; }
    if (op.strictCodes && ls == Decoder::Shape::Incomplete && lit.used != 1) { why = "deflate literal code is incomplete"; return false; }
    auto ds = dist.Build(lens + hlit, hdist);
    if (ds == Decoder::Shape::Oversubscribed) { why = "deflate distance code is over-subscribed"; return false; }
    if (op.strictCodes && ds == Decoder::Shape::Incomplete && dist.used > 1) { why = "deflate distance code is incomplete"; return false; }
    return RunBlock(br, lit, dist, out, op, why);
}

// ---- checksums ----------------------------------------------------------------------------------------
struct CrcTable {
    uint32_t t[256];
    constexpr CrcTable() : t{} {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
    }
};
inline constexpr CrcTable kCrc{};   // computed at compile time: no run-time initialisation, no data race

inline uint32_t Crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) c = kCrc.t[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}
inline uint32_t Crc32(const std::string& s) { return Crc32(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

inline uint32_t Adler32(const uint8_t* p, size_t n) {
    constexpr uint32_t kMod = 65521;
    uint32_t a = 1, b = 0;
    size_t i = 0;
    while (i < n) {
        size_t stop = i + 5552 < n ? i + 5552 : n;   // largest run before 32-bit overflow
        for (; i < stop; ++i) { a += p[i]; b += a; }
        a %= kMod; b %= kMod;
    }
    return (b << 16) | a;
}
inline uint32_t Adler32(const std::string& s) { return Adler32(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

inline uint32_t Le32(const uint8_t* p) { return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

} // namespace detail

// ---- public entry points ------------------------------------------------------------------------------
// All three clear `out` and return false (with `why` set) on any problem; on success `out` holds exactly the
// decoded bytes. Input may be any pointer when n == 0.

/// Raw deflate stream (RFC 1951). `consumed` is the number of input bytes used (not meaningful with stopAtMax).
/// Vec: std::vector<uint8_t> or another vector of bytes (see detail::OutputT).
template <class Vec>
inline bool RawInto(const uint8_t* in, size_t n, Vec& out, const Options& op, size_t& consumed, std::string& why) {
    using namespace detail;
    out.clear();
    BitReader br(in, n);
    OutputT<Vec> o(out, op.maxOutput);
    if (op.hint) {
        // The first allocation is only a guess, so it never needs to be more than the stream could possibly produce:
        // DEFLATE cannot expand more than 1032:1 (a 258-byte match costs at least two bits plus a code). A header that
        // claims gigabytes with a few bytes of data therefore allocates (and zero-fills) next to nothing; real data
        // just grows the buffer as it goes.
        size_t first = op.hint < op.maxOutput ? op.hint : op.maxOutput;
        const size_t possible = n > (SIZE_MAX - 4096) / 1032 ? SIZE_MAX : n * 1032 + 4096;
        o.Room(first < possible ? first : possible);
    }
    bool ok = true, final = false;
    do {
        if (op.stopAtMax && o.size == o.limit) break;   // all the wanted bytes are here
        final = br.Take(1) != 0;
        uint32_t kind = br.Take(2);
        if (br.starved) { why = "deflate data ends early"; ok = false; break; }
        switch (kind) {
            case 0: ok = StoredBlock(br, o, why); break;
            case 1: ok = FixedBlock(br, o, op, why); break;
            case 2: ok = DynamicBlock(br, o, op, why); break;
            default: why = "reserved deflate block type"; ok = false; break;
        }
        if (o.capped) break;
    } while (ok && !final);
    if (!ok) { out.clear(); return false; }
    consumed = br.BytesUsed();
    out.resize(o.size);
    return true;
}
inline bool Raw(const uint8_t* in, size_t n, std::vector<uint8_t>& out, const Options& op, size_t& consumed, std::string& why) {
    return RawInto(in, n, out, op, consumed, why);
}

/// zlib stream (RFC 1950). With op.checkTrailer: window <= 32K, Adler-32 must match, no bytes after it.
template <class Vec>
inline bool ZlibInto(const uint8_t* in, size_t n, Vec& out, const Options& op, std::string& why) {
    using namespace detail;
    out.clear();
    // CMF FLG; CM 8, header check divisible by 31, no preset dictionary
    const uint32_t cmf = n > 0 ? in[0] : 0, flg = n > 1 ? in[1] : 0;
    const bool badHeader = op.checkTrailer ? (n < 6 || (cmf >> 4) > 7) : n < 2;
    if (badHeader || (cmf & 0x0f) != 8 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20)) { why = "not a zlib stream"; return false; }
    size_t used = 0;
    if (!RawInto(in + 2, n - 2, out, op, used, why)) return false;
    if (!op.checkTrailer) return true;
    const size_t off = 2 + used;
    if (n - off != 4) { why = "bad zlib trailer"; out.clear(); return false; }
    const uint32_t want = (uint32_t(in[off]) << 24) | (uint32_t(in[off + 1]) << 16) | (uint32_t(in[off + 2]) << 8) | in[off + 3];
    if (want != Adler32(out.data(), out.size())) { why = "zlib checksum mismatch"; out.clear(); return false; }
    return true;
}
inline bool Zlib(const uint8_t* in, size_t n, std::vector<uint8_t>& out, const Options& op, std::string& why) {
    return ZlibInto(in, n, out, op, why);
}

/// One gzip member (RFC 1952). CRC-32 and size are always checked; extra bytes (a second member) are refused.
inline bool Gzip(const uint8_t* p, size_t n, std::vector<uint8_t>& out, const Options& op, std::string& why) {
    using namespace detail;
    out.clear();
    // RFC 1952 section 2.3: ID1 ID2 CM FLG MTIME(4) XFL OS, then optional fields, then data, CRC32, ISIZE
    if (n < 18 || p[0] != 0x1f || p[1] != 0x8b || p[2] != 8) { why = "not a gzip stream"; return false; }
    const unsigned flg = p[3];
    if (flg & 0xe0) { why = "reserved gzip flags"; return false; }
    size_t off = 10;
    if (flg & 0x04) {   // FEXTRA
        if (n - off < 2) { why = "truncated gzip header"; return false; }
        off += 2 + (size_t(p[off]) | (size_t(p[off + 1]) << 8));
    }
    for (unsigned bit : {0x08u, 0x10u}) {   // FNAME, FCOMMENT: zero-terminated
        if (!(flg & bit)) continue;
        while (off < n && p[off] != 0) ++off;
        if (off >= n) { why = "truncated gzip header"; return false; }
        ++off;
    }
    if (flg & 0x02) off += 2;   // FHCRC (not checked)
    if (off > n || n - off < 8) { why = "truncated gzip header"; return false; }
    size_t used = 0;
    if (!Raw(p + off, n - off, out, op, used, why)) return false;
    off += used;
    if (n - off != 8) { why = "trailing bytes after gzip data"; out.clear(); return false; }
    if (Le32(p + off) != Crc32(out.data(), out.size()) || Le32(p + off + 4) != uint32_t(out.size() & 0xffffffffu)) {
        why = "gzip checksum mismatch"; out.clear(); return false;
    }
    return true;
}

} // namespace okay::inflate
